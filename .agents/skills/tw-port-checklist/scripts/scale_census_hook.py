"""Writes view_share_hooked.hpp (scale_census_build.ps1 runs it): a copy of the repo's twattila/view_share.hpp with absolute includes and one hook that
hands scale_census_replay.cpp the patch sites Patch() found (before it patches). Run before building scale_census_replay.exe, so
the census always replays the current view_share.hpp.

usage: python -I scale_census_hook.py <repo root> <out header>
"""
import os
import re
import sys


def main():
    repo, out = sys.argv[1], sys.argv[2]
    source_dir = os.path.join(repo, "src", "games", "twattila")
    text = open(os.path.join(source_dir, "view_share.hpp"), encoding="utf-8").read()
    utils = os.path.join(repo, "src", "utils").replace("\\", "/")
    here = source_dir.replace("\\", "/")
    text = text.replace('#include "../../utils/', f'#include "{utils}/')
    text = re.sub(r'#include "\./([^"]+)"', lambda m: f'#include "{here}/{m.group(1)}"', text)
    text = text.replace("#include <vector>", "#include <vector>\n#include <functional>", 1)
    hook_declaration = (
        "// census: the sites Patch() found (instruction index, kind: 0 NDC 1 PROJECTED 2 REGION 3 SPLIT 4 WHOLE).\n"
        "struct CensusSite { size_t instruction = 0; int kind = 0; std::array<uint32_t, 2> components = {}; };\n"
        "inline std::function<void(const std::vector<Instruction>&, const std::vector<CensusSite>&, const Reflection&)> census_hook;\n\n"
    )
    anchor = "// The patched code (empty: nothing to patch) and what was patched, for the log."
    if anchor not in text:
        raise SystemExit("anchor for the hook declaration not found in view_share.hpp")
    text = text.replace(anchor, hook_declaration + anchor, 1)
    call_anchor = "  if (sites.empty()) return result;"
    if call_anchor not in text:
        raise SystemExit("anchor for the hook call not found in view_share.hpp")
    hook_call = (
        "  if (census_hook) {\n"
        "    std::vector<CensusSite> census_sites;\n"
        "    for (const auto& site : sites) census_sites.push_back({site.instruction, static_cast<int>(site.kind), site.components});\n"
        "    census_hook(instructions, census_sites, reflection);\n"
        "  }\n"
    )
    text = text.replace(call_anchor, hook_call + call_anchor, 1)
    open(out, "w", encoding="utf-8").write(text)


if __name__ == "__main__":
    main()
