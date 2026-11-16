"""Every byte pattern of Tales of Arise's code patches and hooks, run on the game's code.

code_patch::FindPattern takes a site only when its pattern matches once in the exe's executable sections, else none
(FindPatterns: every match, for the console variable lookups and the world time manager). This runs the patterns as
written in the sources (string literals of hex bytes and ??, adjacent literals joined) on the same sections:
- Tales of Arise: tales_image.bin, the decrypted image the add-on's Save Image writes (file offset = RVA; the exe is
  SteamStub-encrypted on disk).
A pattern passes when it matches once (FindPatterns users: at least once). Tales' image was saved with the add-on
running, so its patched sites hold the add-on's bytes: there a patched span matches the original or the replacement
(PATCHED below, from the add-on's code; the NPC fade rows' immediates from their own table). CONTROLS that must FAIL the same check: the
first pattern with its first fixed byte changed (no match), a run of int3 padding (many matches), and a pattern
that does match once with one more fixed byte from beyond its end changed (no match); each control failing is the
pass. Usage: python -I run.py [--tales IMAGE] [--game tales]
"""
import argparse
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GAMES = {
    "tales": {
        "image": r"D:\SteamLibrary\steamapps\common\Tales of Arise\Arise\Binaries\Win64\tales_image.bin",
        "mapped": True,
        "sources": ["src/games/talesofarise_dlss/engine_hooks.hpp", "src/games/talesofarise/dlss_graphics.hpp"],
        "engine": "UE_4_21",
    },
}
# Searched with FindPatterns (every match is used): at least one match.
ANY_MATCH = {"WORLD_TIME_MANAGER", "lookup_pattern"}
# The add-on's own replacements: (offset, bytes) matched as the original or these.
PATCHED = {
    "SUN_DISTANCE_LIMIT": [(0, "90 90 90 90 90 90 90 90")],  # engine_hooks.hpp: the minss becomes 8 nops
    "RENDER_TARGET_SIZE": [(17, "90 E8 ?? ?? ?? ??")],  # RENDER_TARGET_UPDATE: nop; call the stub
    "SCENE_CAPTURE_SHOW_FLAGS": [(0, "81 8B B8 02 00 00 08 20 00 00")],  # skit resolution: or instead of and
    # dlss_graphics.hpp: jne becomes jmp (JITTER_SCALE_BRANCH), its displacement 0 and the multiply (JITTER_SCALE_MULTIPLY,
    # 18 bytes) two loads of the Halton sample and a nop.
    "JITTER_SCALE": [(8, "EB 00"), (10, "F3 0F 10 3D ?? ?? ?? ?? F3 44 0F 10 05 ?? ?? ?? ?? 90")],
}
TOKEN = r"(?:[0-9A-F]{2}|\?\?)"
PATTERN_TEXT = re.compile(rf"^{TOKEN}(?: {TOKEN})*$")
LITERALS = re.compile(r'"([^"\\\n]*)"((?:\s*"[^"\\\n]*")*)')
NAME_BEFORE = re.compile(r"(?:constexpr\s+std::string_view\s+(\w+)\s*=|\.(\w+)\s*=)\s*$")


def ExecutableSections(data, mapped):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        sys.exit("not a PE image")
    count, optional_size = struct.unpack_from("<H12xH", data, pe + 6)
    table = pe + 24 + optional_size
    sections = []
    for index in range(count):
        name, virtual_size, rva, raw_size, raw_offset, characteristics = struct.unpack_from(
            "<8sIIII16xI", data, table + index * 40)
        if (characteristics & 0x20000000) == 0:  # IMAGE_SCN_MEM_EXECUTE
            continue
        size = virtual_size or raw_size
        start = rva if mapped else raw_offset
        if not mapped:
            size = min(size, raw_size)
        sections.append((name.rstrip(b"\0").decode(), start, start + size))
    return sections


def Patterns(source, engine):
    text = (ROOT / source).read_text(encoding="utf-8")
    found = []
    for match in LITERALS.finditer(text):
        joined = match.group(1) + "".join(re.findall(r'"([^"\\\n]*)"', match.group(2)))
        joined = joined.strip()
        if not PATTERN_TEXT.match(joined) or len(joined.split()) < 4:
            continue
        line = text.count("\n", 0, match.start()) + 1
        before = text[max(0, match.start() - 160):match.start()]
        name_match = NAME_BEFORE.search(before)
        name = (name_match.group(1) or name_match.group(2)) if name_match else "table row"
        spans = PATCHED.get(name, [])
        # NPC_FADE_DEFAULTS rows: {pattern, fade_in, fade_out}, two float immediates the NPC distance scale rewrites.
        if fade := re.match(r"\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", text[match.end():]):
            name = "NPC_FADE_DEFAULTS row"
            spans = [(int(fade.group(1)), "?? ?? ?? ??"), (int(fade.group(2)), "?? ?? ?? ??")]
        found.append((f"{source}:{line}", name, joined, spans))
    return found


def EnginePattern(engine):
    source = "src/games/talesofarise_dlss/engine_cvars.hpp"
    text = (ROOT / source).read_text(encoding="utf-8")
    at = text.index(f"inline constexpr Engine {engine} = {{")
    match = re.compile(r'\.lookup_pattern\s*=\s*"([^"]+)"').search(text, at)
    return (f"{source}:{text.count(chr(10), 0, match.start()) + 1}", "lookup_pattern", match.group(1), [])


def Compile(pattern, spans):
    choices = [[token] for token in pattern.split()]
    for offset, replacement in spans:
        for index, token in enumerate(replacement.split()):
            choices[offset + index].append(token)
    parts = []
    for tokens in choices:
        if "??" in tokens:
            parts.append(b".")
        elif len(set(tokens)) == 1:
            parts.append(re.escape(bytes([int(tokens[0], 16)])))
        else:
            parts.append(b"[" + b"".join(re.escape(bytes([int(token, 16)])) for token in sorted(set(tokens))) + b"]")
    return re.compile(b"".join(parts), re.S)


def Matches(data, sections, pattern, spans=()):
    expression = Compile(pattern, spans)
    hits = []
    for _, begin, end in sections:
        position = begin
        while True:
            match = expression.search(data, position, end)
            if match is None:
                break
            hits.append(match.start())
            position = match.start() + 1
    return hits


def Check(name, hits):
    return len(hits) >= 1 if name in ANY_MATCH else len(hits) == 1


def Run(game, image_path):
    config = GAMES[game]
    data = pathlib.Path(image_path).read_bytes()
    sections = ExecutableSections(data, config["mapped"])
    patterns = [found for source in config["sources"] for found in Patterns(source, config["engine"])]
    patterns.append(EnginePattern(config["engine"]))
    print(f"== {game}: {image_path} ({len(data) >> 20} MB), executable sections "
          + ", ".join(f"{name} 0x{begin:X}-0x{end:X}" for name, begin, end in sections))
    failures = 0
    unique_site = None
    for where, name, pattern, spans in patterns:
        hits = Matches(data, sections, pattern)
        patched = ""
        if not Check(name, hits) and spans and config["mapped"]:
            hits = Matches(data, sections, pattern, spans)
            patched = " (patched by the add-on in this image)"
            if name == "NPC_FADE_DEFAULTS row":
                # One NPC distance scale multiplies both immediates: their ratio stays the original's.
                tokens = pattern.split()
                (fade_in, _), (fade_out, _) = spans
                original = [struct.unpack("<f", bytes(int(token, 16) for token in tokens[at:at + 4]))[0]
                            for at in (fade_in, fade_out)]
                scaled = [[struct.unpack_from("<f", data, hit + at)[0] for at in (fade_in, fade_out)] for hit in hits]
                kept = [(hit, low) for hit, (low, high) in zip(hits, scaled)
                        if 0 < low < 1e30 and abs(high / low - original[1] / original[0]) < 1e-5]
                hits = [hit for hit, _ in kept]
                if kept:
                    patched = f" (scaled by the add-on's NPC distance in this image: x{kept[0][1] / original[0]:.3f})"
        passed = Check(name, hits)
        failures += not passed
        if passed and len(hits) == 1 and not patched and unique_site is None:
            unique_site = (pattern, hits[0])
        shown = ", ".join(f"0x{hit:X}" for hit in hits[:4]) + (" ..." if len(hits) > 4 else "")
        print(f"  {'PASS' if passed else 'FAIL'} {len(hits):5d} {where} {name}{patched}: {shown}")
    # Controls: each must fail the check the patterns passed.
    controls = []
    first = patterns[0][2].split()
    fixed = next(index for index, token in enumerate(first) if token != "??")
    first[fixed] = f"{int(first[fixed], 16) ^ 0x5A:02X}"
    controls.append(("first pattern, one fixed byte changed", "control", " ".join(first)))
    controls.append(("int3 padding", "control", "CC CC CC CC CC CC CC CC"))
    if unique_site is not None:
        pattern, site = unique_site
        length = len(pattern.split())
        extended = pattern + " " + f"{data[site + length] ^ 0xFF:02X}"
        controls.append((f"a unique pattern + its next byte changed (at 0x{site + length:X})", "control", extended))
    control_failures = 0
    for where, name, pattern in controls:
        hits = Matches(data, sections, pattern)
        failed = not Check(name, hits)
        control_failures += not failed
        print(f"  CONTROL {'FAILS (good)' if failed else 'PASSES (bad)'} {len(hits):5d} {where}")
    print(f"  {game}: {len(patterns) - failures} / {len(patterns)} patterns pass, "
          f"{len(controls) - control_failures} / {len(controls)} controls fail")
    return failures + control_failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tales", default=GAMES["tales"]["image"])
    parser.add_argument("--game", choices=list(GAMES), action="append")
    arguments = parser.parse_args()
    images = {"tales": arguments.tales}
    bad = sum(Run(game, images[game]) for game in (arguments.game or list(GAMES)))
    print("PASS" if bad == 0 else f"FAIL ({bad})")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
