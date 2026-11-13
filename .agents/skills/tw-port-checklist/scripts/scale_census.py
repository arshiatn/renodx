"""Render-scale census of a Total War dump (DLSS / FSR / XeSS below 100%: the 3D image in the top-left share of
display-size targets, view_share.hpp's patches): every pixel and compute shader that reads screen space or builds
positions from a texture uv, what the addon does with it, and a verdict (OK / BUG + fix kind / unclear).

usage:
  python -I scale_census.py --cso <dump cso folder> --names <names.tsv> --addon <game addon.cpp> --shaders <its shaders folder>
                      --replay <scale_census_replay.exe> --cache <listing folder, scratch> [--traces <tw_frame_trace.txt> ...]
                      [--fxc <fxc.exe>] [--table <out .txt, markdown>] [--tsv <out .tsv>] [--all]

  scale_census_replay.exe (scale_census_build.ps1: the repo's view_share.hpp with a hook) replays Reflect + Patch on every
  shader with the addon's RENDER_SCALE_READS (and SetViewportChains(true) when the addon calls it); this script disassembles every pixel / compute shader with fxc (cached), follows
  each texture read's coordinate and each uv -> NDC / clip -> uv mad back to its roots, classes them, and subtracts what
  the addon handles: the replay's patch sites (minus over-patched ones), RENDER_SCALE_READS slots, FULL_DEPTH_SHADERS,
  the replaced shaders (0xHASH in the shaders folder's file names, minus custom_shaders.erase), shaders the addon names.
  With traces: which trace draws each shader, at a scaled viewport or not, and the bound textures' sizes, so full-screen
  uv reads of render-scale textures are told apart from reads of the pass's own whole inputs.

Classes of a coordinate (roots):
  pixel  SV_Position / thread id only (ld at the pixel)            -> right at any render scale
  tex    SV_Position x the camera's texture size (c27 / c31) or a named screen-size constant (inv_rt_size,
         g_Resolution, ...): 0..share of a display-size texture  -> right for reads; NDC from it needs view_share's patch
  view   also the camera's viewport (c29): the engine's own dynamic-resolution path -> right (c29 is the render size)
  vonly  SV_Position x the viewport alone (0..1 over the view)    -> reads of display-size textures need x share
  input  a TEXCOORD of a full-screen vertex shader (0..1 over the draw's viewport)
  proj   from the camera's projection (clip -> uv)
  world  anything else (mesh uvs, world positions, other reads)
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys

COMP = "xyzw"
READ_OPS = ("sample", "ld", "gather4")
SCREEN_NAME = re.compile(r"(inv_rt_size|rt_size|resolution|screen_size|screen_dimensions|screen_res|viewport_size|viewport_dim|"
                         r"texel_size|pixel_size|target_size|target_dim|buffer_size|frame_size|inv_size|inverse_size|"
                         r"texture_dimensions|texture_size|render_size)", re.I)
TEXTURE_CAMERA = {"g_screen_size", "g_render_target_dimensions"}
VIEW_CAMERA = {"g_viewport_dimensions"}
PROJECTION_CAMERA = re.compile(r"^(view_projection|projection|inv_view_projection|inv_projection|view|inv_view|"
                               r"previous_view_projection|prev_view_projection|g_prev.*projection.*)$")
# Render targets by their usual names (what a pass reads that the 3D drew at the render scale).
RENDER_TARGET = re.compile(r"(gbuffer|depth|ssao|scene|light_buffer|lighting|refraction|reflection|inscattering|velocity|motion|"
                           r"hdr|bloom|glow|colour_buffer|color_buffer|frame|distance|shadow_mask|screen|ssr|fog|_src|backbuffer|"
                           r"rt_|_rt|generated|blur|history|accum)", re.I)
# Mesh materials (their TEXCOORDs are mesh uvs, not a full-screen pass's).
MESH_NAME = re.compile(r"(shaders__|rigid|weighted|terrain|spline|particle|decal|cloth|tree|grass|unit|building|water|sea_|"
                       r"debug|editor|gizmo|loadingscreen|sprite|font|text|ui_|flag|banner|skin|hair|foliage|sky|cloud|smoke|"
                       r"projectile|trail|ribbon|billboard|vfx)", re.I)
STATIC_TEXTURE = re.compile(r"(lut|noise|random|palette|gradient|font|atlas|ramp|dither|blue_noise|lookup|bayer|_map$|sheen|"
                            r"environment|cube|sky|caustic|cloud|puddle|detail|normal_map|diffuse_map|mask_map)", re.I)


# ---------------------------------------------------------------------------------------------------------- listings
def parse_operand(text):
    text = text.strip()
    text = text.lstrip("-").strip("|")
    if text.startswith("l("):
        return {"kind": "imm", "text": text}
    m = re.match(r"^([a-zA-Z]+)(\d*)(\[[^\]]*\])?(\[[^\]]*\])?(?:\.([xyzw]+))?$", text)
    if not m:
        return {"kind": "other", "text": text}
    reg, idx, b1, b2, sw = m.groups()
    return {"kind": reg, "name": reg + idx + (b1 or "") + (b2 or ""), "swizzle": sw or "xyzw", "text": text}


def split_operands(rest):
    out, depth, cur = [], 0, ""
    for ch in rest:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [o.strip() for o in out]


def parse_listing(text):
    """The instructions (view_share's order: declarations and the immediate constant buffer left out) and the RDEF."""
    lines = text.splitlines()
    code, started, in_icb = [], False, 0
    for number, line in enumerate(lines, 1):
        s = line.strip()
        if re.match(r"^(ps|vs|cs|gs|hs|ds)_\d_\d$", s):
            started = True
            continue
        if not started or not s or s.startswith("//"):
            continue
        if s.startswith("dcl_immediateConstantBuffer"):
            in_icb = s.count("{") - s.count("}")
            continue
        if in_icb > 0:
            in_icb += s.count("{") - s.count("}")
            continue
        if s.startswith("dcl_"):
            continue
        op = s.split(" ")[0]
        base = re.sub(r"\(.*", "", op)
        rest = s[len(op):].strip()
        code.append({"line": number, "op": base, "full": op, "operands": [parse_operand(o) for o in split_operands(rest)] if rest else [],
                     "text": s})
    # Bindings, cbuffer variables, input signature.
    bindings, cbuffers = {}, {}
    for m in re.finditer(r"^// (\S+)\s+(texture|cbuffer|UAV|sampler)\s+\S+\s+\S+\s+(t|cb|u|s)(\d+)\s+\d+", text, re.M):
        name, kind, prefix, slot = m.groups()
        bindings[prefix + slot] = name
        if kind == "cbuffer":
            cbuffers[name] = int(slot)
    variables = {}
    for block in re.finditer(r"^// cbuffer (\S+)\s*\n// \{\n(.*?)^// \}", text, re.M | re.S):
        slot = cbuffers.get(block.group(1))
        if slot is None:
            continue
        for v in re.finditer(r"^//\s+(?:row_major |column_major )?\S+\s+(\w+)(?:\[\d+\])?;\s*// Offset:\s*(\d+) Size:\s*(\d+)", block.group(2), re.M):
            offset, size = int(v.group(2)), int(v.group(3))
            for register in range(offset // 16, (offset + max(size, 1) - 1) // 16 + 1):
                variables.setdefault((slot, register), v.group(1))
    inputs = {}
    sig = re.search(r"// Input signature:(.*?)// Output signature", text, re.S)
    if sig:
        for m in re.finditer(r"^// (\w+)\s+(\d+)\s+[xyzw ]+?\s+(\d+)\s+(\w+)\s+(\w+)", sig.group(1), re.M):
            inputs.setdefault("v" + m.group(3), []).append(m.group(1) + m.group(2))
    numthreads = re.search(r"dcl_thread_group (\d+), (\d+), (\d+)", text)
    return {"code": code, "bindings": bindings, "cbuffers": cbuffers, "variables": variables, "inputs": inputs,
            "camera": cbuffers.get("camera", cbuffers.get("camera_VS_PS")),
            "numthreads": tuple(int(x) for x in numthreads.groups()) if numthreads else None}


def source_components(operand, component, op):
    sw = operand["swizzle"]
    if op in ("dp2", "dp3", "dp4"):
        return {sw[i] if len(sw) == 4 else sw[0] for i in range(int(op[2]))}
    if len(sw) == 1:
        return {sw}
    return {sw[COMP.index(component)]}


def roots(code, index, reg, components, depth=0, seen=None):
    """Roots of temp `reg`'s `components` as read at instruction `index`: inputs, constant registers, reads."""
    seen = set() if seen is None else seen
    out = set()
    if depth > 60:
        return out
    for component in components:
        key = (index, reg, component)
        if key in seen:
            continue
        seen.add(key)
        for at in range(index - 1, -1, -1):
            ins = code[at]
            if not ins["operands"]:
                continue
            d = ins["operands"][0]
            if d["kind"] not in ("r",) or d.get("name") != reg or component not in d["swizzle"]:
                continue
            op = ins["op"]
            if op.startswith(READ_OPS) or op.startswith("resinfo") or op.startswith("bufinfo"):
                out.add("READ")
                break
            for src in ins["operands"][1:]:
                if src["kind"] in ("imm", "other", "t", "s", "u"):
                    continue
                comps = source_components(src, component, op)
                if src["kind"] == "r":
                    out |= roots(code, at, src["name"], comps, depth + 1, seen)
                else:
                    out.add(src["name"] + "." + "".join(sorted(comps, key=COMP.index)))
            break
    return out


def read_components(ins):
    addr = ins["operands"][1]
    n = 3 if ("texture3d" in ins["full"] or "array" in ins["full"]) else 2
    sw = addr.get("swizzle", "xy")
    return addr, {sw[k] if len(sw) == 4 else sw[0] for k in range(min(n, 2))}


class Shader:
    def __init__(self, h, stage, listing):
        self.hash, self.stage, self.listing = h, stage, listing
        self.camera = listing["camera"]
        inputs = listing["inputs"]
        self.position_inputs = {v for v, names in inputs.items() if any(n.startswith("SV_Position") for n in names)}
        self.other_inputs = {v for v, names in inputs.items() if not any(n.startswith("SV_") for n in names)}
        names = [n for v, ns in inputs.items() for n in ns if not n.startswith("SV_")]
        # A full-screen pass: SV_Position and one or two TEXCOORDs, nothing of a mesh.
        self.full_screen = stage == "ps" and 0 < len(names) <= 2 and all(n.startswith("TEXCOORD") for n in names)

    def variable(self, root):
        m = re.match(r"^cb(\d+)\[(\d+)\]", root)
        if not m:
            return None, None
        slot, register = int(m.group(1)), int(m.group(2))
        return slot, self.listing["variables"].get((slot, register))

    def tags(self, rs):
        """T texture size, V viewport, P projection, S named screen constant, C other constant, X pixel / thread id,
        I TEXCOORD, R another read."""
        tags = set()
        for r in rs:
            base = r.split(".")[0]
            if r == "READ":
                tags.add("R")
            elif base in self.position_inputs or base.startswith(("vThreadID", "vThreadIDInGroup", "vThreadGroupID")):
                tags.add("X")
            elif base in self.other_inputs:
                tags.add("I")
            elif base.startswith("cb"):
                slot, name = self.variable(base)
                if slot is not None and slot == self.camera:
                    if name in TEXTURE_CAMERA:
                        tags.add("T")
                    elif name in VIEW_CAMERA:
                        tags.add("V")
                    elif name and PROJECTION_CAMERA.match(name):
                        tags.add("P")
                    else:
                        tags.add("C")
                elif name and SCREEN_NAME.search(name):
                    tags.add("S")
                else:
                    tags.add("C")
            elif base.startswith("icb"):
                tags.add("C")
        return tags

    @staticmethod
    def classify(tags, op=""):
        if "I" in tags and "X" not in tags:
            # A TEXCOORD scaled by the camera's sizes (v1 x c29 / c31): the engine's own render-share path.
            return "iscaled" if ({"V", "T", "S"} & tags) else "input"
        if "X" in tags:
            if "P" in tags or "R" in tags:
                return "world"  # a position rebuilt from the depth, another read: not a screen uv as such
            if op.startswith("ld"):
                return "pixel"  # texel indices from the pixel (a viewport or size there only clamps or scales a fixed grid)
            if "V" in tags and ("T" in tags or "S" in tags):
                return "view"
            if "V" in tags:
                return "vonly"
            if "T" in tags or "S" in tags:
                return "tex"
            return "pixel" if "C" not in tags else "cbpos"
        if "P" in tags:
            return "proj"
        return "world"

    def scan(self):
        code = self.listing["code"]
        self.reads, self.ndc, self.proj = [], [], []
        for i, ins in enumerate(code):
            op = ins["op"]
            ops = ins["operands"]
            if op.startswith(READ_OPS) and len(ops) >= 3:
                addr, comps = read_components(ins)
                tex = next((o["name"] for o in ops[2:] if o["kind"] == "t"), None)
                if tex is None:
                    continue
                if addr["kind"] == "r":
                    rs = roots(code, i, addr["name"], comps)
                elif addr["kind"] == "v" or addr["kind"].startswith("vThread"):
                    rs = {addr["name"] + "." + "".join(sorted(comps))}
                else:
                    rs = set()
                tags = self.tags(rs)
                self.reads.append({"index": i, "line": ins["line"], "op": op, "slot": tex, "name": self.listing["bindings"].get(tex, "?"),
                                   "tags": tags, "cls": self.classify(tags, op)})
            elif op == "mad" and len(ops) == 4 and ops[2]["kind"] == "imm" and ops[1]["kind"] in ("r", "v"):
                imm = ops[2]["text"]
                is_ndc = "2.000000, -2.000000" in imm or "4.000000, -4.000000" in imm
                is_proj = "0.500000, -0.500000" in imm
                if not (is_ndc or is_proj):
                    continue
                d, src = ops[0], ops[1]
                comps = set()
                for c in d.get("swizzle", "xy"):
                    sw = src.get("swizzle", "xyzw")
                    comps.add(sw[COMP.index(c)] if len(sw) == 4 else sw[0])
                rs = roots(code, i, src["name"], comps) if src["kind"] == "r" else {src["name"] + "." + "".join(sorted(comps))}
                tags = self.tags(rs)
                entry = {"index": i, "line": ins["line"], "tags": tags, "cls": self.classify(tags)}
                (self.ndc if is_ndc else self.proj).append(entry)
        return self


# ------------------------------------------------------------------------------------------------------- addon tables
def parse_addon(addon_path, shaders_dir, view_share_path):
    text = open(addon_path, encoding="utf-8", errors="replace").read()
    region = {}
    block = re.search(r"RENDER_SCALE_READS\s*=\s*\{\{(.*?)\}\};", text, re.S)
    if block:
        for m in re.finditer(r"\.shader_hash\s*=\s*0x([0-9A-Fa-f]{8})\s*,\s*\.slots\s*=\s*(0b[01]+|0x[0-9A-Fa-f]+|\d+)(\s*,\s*\.whole\s*=\s*true)?",
                             block.group(1)):
            region[m.group(1).upper()] = (int(m.group(2), 0), bool(m.group(3)))
    full_depth = set()
    block = re.search(r"FULL_DEPTH_SHADERS\s*=\s*\{([^;]*)\};", text, re.S)
    if block:
        full_depth = {h.upper() for h in re.findall(r"0x([0-9A-Fa-f]{8})", block.group(1))}
    whole = []
    if "view_share::WHOLE_TEXTURES" in text and view_share_path and os.path.exists(view_share_path):
        m = re.search(r"WHOLE_TEXTURES\s*=\s*\{([^}]*)\}", open(view_share_path, encoding="utf-8").read())
        if m:
            whole = re.findall(r'"([^"]+)"', m.group(1))
    # Tables naming shader hashes (the addon's own handling: draws it hooks, replaces or patches).
    named = {}
    table = None
    for line in text.splitlines():
        t = re.search(r"\b(constexpr|static|const)\b[^=;]*?\b([A-Z][A-Z0-9_]{2,})\s*=", line)
        if t:
            table = t.group(2)
        for h in re.findall(r"0x([0-9A-Fa-f]{8})\b", line):
            named.setdefault(h.upper(), set()).add(table or "addon.cpp")
        if line.strip().startswith("}};") or line.strip() == "};":
            table = None
    erased = set(h.upper() for h in re.findall(r"custom_shaders\.erase\(0x([0-9A-Fa-f]{8})\)", text))
    for name, index in re.findall(r"custom_shaders\.erase\((\w+)\[(\d+)\]\)", text):
        m = re.search(name + r"\s*=\s*\{\{?([^;]*)\}?\};", text, re.S)
        if m:
            hashes = re.findall(r"0x([0-9A-Fa-f]{8})", m.group(1))
            if int(index) < len(hashes):
                erased.add(hashes[int(index)].upper())
    replaced = {}
    if shaders_dir and os.path.isdir(shaders_dir):
        def uses_share(path, depth=0, seen=None):
            seen = set() if seen is None else seen
            if depth > 6 or path in seen or not os.path.exists(path):
                return False
            seen.add(path)
            body = open(path, encoding="utf-8", errors="replace").read()
            if re.search(r"FrameRenderShare|DrawRenderShare|CameraRenderShare|RenderShareTexel|render_scale\.hlsli", body):
                return True
            for inc in re.findall(r'#include\s+"([^"]+)"', body):
                if uses_share(os.path.normpath(os.path.join(os.path.dirname(path), inc)), depth + 1, seen):
                    return True
            return False

        for f in os.listdir(shaders_dir):
            m = re.search(r"0x([0-9A-Fa-f]{8})", f)
            if m and f.endswith(".hlsl"):
                replaced[m.group(1).upper()] = {"file": f, "share": uses_share(os.path.join(shaders_dir, f))}
    return {"region": region, "full_depth": full_depth, "whole": whole, "named": named, "erased": erased, "replaced": replaced,
            "chains": "SetViewportChains(true)" in text}


# ------------------------------------------------------------------------------------------------------------ traces
def parse_traces(paths):
    """Per shader: the traces drawing it, its draws (viewport scaled or not, bound textures, render-space or not)."""
    out = {}
    for path in paths:
        label = os.path.basename(os.path.dirname(path)) + "/" + os.path.basename(path).replace("tw_frame_trace", "trace").replace(".txt", "")
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        # The render share: the camera's viewport over its screen before the tone mapper.
        share = 1.0
        for line in lines:
            m = re.search(r"camera screen (\d+)x(\d+) viewport (\d+)x(\d+)", line)
            if m and int(m.group(3)) < int(m.group(1)):
                share = int(m.group(3)) / int(m.group(1))
                break
        display = None
        for line in lines:
            for w, hgt in re.findall(r"\brt\d 0x[0-9a-f]+ (\d+)x(\d+)", line):
                if display is None or int(w) * int(hgt) > display[0] * display[1]:
                    display = (int(w), int(hgt))
        render_space = set()
        tonemapped = False
        for line in lines:
            if line.startswith("MARK tone mapper drawn"):
                tonemapped = True
                continue
            m = re.match(r"^(draw|indexed|dispatch)\S*\s+(\S+).*?\b(ps|cs) ([0-9A-F]{8})", line)
            if not m:
                continue
            h, stage = m.group(4), m.group(3)
            vp = re.search(r"\| vp (\d+)x(\d+) at (\d+),(\d+)", line)
            targets = re.findall(r"\b(?:rt\d|ds|u\d) (0x[0-9a-f]+) (\d+)x(\d+)", line)
            scaled = False
            if vp and targets and share < 1.0:
                vw, vh, x, y = (int(g) for g in vp.groups())
                tw, th = int(targets[0][1]), int(targets[0][2])
                scaled = x == 0 and y == 0 and abs(vw - tw * share) <= 1.5 and abs(vh - th * share) <= 1.5 and vw < tw
            if stage == "cs" and share < 1.0 and not tonemapped:
                scaled = True  # a compute pass before the tone mapper works on the render share
            textures = {s: (hd, int(w), int(hh)) for s, hd, w, hh in re.findall(r"\b(t\d+) (0x[0-9a-f]+) (\d+)x(\d+)", line)}
            entry = out.setdefault(h, {"stage": stage, "traces": {}})
            draws = entry["traces"].setdefault(label, [])
            draws.append({"full_screen": m.group(1) == "draw" and m.group(2) in ("3", "4"), "scaled": scaled, "post": tonemapped, "textures": textures, "share": share, "display": display,
                          "render_space": {s for s, (hd, w, hh) in textures.items() if hd in render_space}})
            if scaled:
                render_space.update(hd for hd, w, hh in targets)
    return out


# ----------------------------------------------------------------------------------------------------------- verdict
def is_display_texture(size, display):
    if display is None:
        return False
    w, h = size
    return any(w == display[0] >> k and h == display[1] >> k for k in range(4))


def verdict(shader, replay, tables, traces, name=""):
    """(verdict, fix, notes) and whether the shader is a screen-space candidate."""
    h = shader.hash
    notes, bugs, unclear = [], [], []
    region_slots, region_whole = tables["region"].get(h, (0, False))
    replaced = tables["replaced"].get(h) if h not in tables["erased"] else None
    sites = []
    if replay and replay["sites"] != "-":
        for s in replay["sites"].split(" "):
            m = re.match(r"(\w+)@(\d+):([A-Z-]+)(!?)", s)
            if m:
                sites.append({"kind": m.group(1), "index": int(m.group(2)), "roots": m.group(3), "over": bool(m.group(4))})
    site_at = {s["index"]: s for s in sites}
    drawn_full_screen = any(d.get("full_screen") for ds in (traces.get(h, {}).get("traces", {}) if traces else {}).values() for d in ds)
    full_screen = shader.full_screen and (drawn_full_screen or not MESH_NAME.search(name))
    screen_reads = [r for r in shader.reads if r["cls"] in ("pixel", "tex", "view", "vonly", "cbpos", "iscaled")
                    or (r["cls"] == "input" and full_screen)]
    screen_ndc = [n for n in shader.ndc if n["cls"] != "world"]
    screen_proj = [p for p in shader.proj if p["cls"] == "proj" or "X" in p["tags"]]
    candidate = bool(screen_reads or screen_ndc or screen_proj or sites)
    if not candidate:
        return None
    handled = []
    if replaced:
        handled.append("replaced" + (" (render share)" if replaced["share"] else ""))
    if h in tables["erased"] and h in tables["replaced"]:
        handled.append("replacement erased (game's code)")
    if region_slots:
        handled.append("region " + ",".join(f"t{i}" for i in range(64) if region_slots >> i & 1) + (" whole" if region_whole else ""))
    if h in tables["full_depth"]:
        handled.append("full depth")
    if sites:
        kinds = {}
        for s in sites:
            kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
        handled.append("patched " + " ".join(f"{k}x{v}" for k, v in sorted(kinds.items())))
    named = sorted(tables["named"].get(h, set()) - {"RENDER_SCALE_READS", "FULL_DEPTH_SHADERS"})
    if named:
        handled.append("addon: " + ",".join(named)[:60])

    # Over-patched sites (the rule: the chain also reaches the viewport, c29). A replaced shader: view_share sees the
    # replacement (built with /Qstrip_reflect: no RDEF, so no camera and no patch), the game's sites do not apply.
    for s in (sites if not replaced else []):
        if not s["over"]:
            continue
        line = next((x["line"] for x in shader.ndc + shader.proj if x["index"] == s["index"]), None)
        where = f"{s['kind']} L{line}" if line else f"{s['kind']}@{s['index']}"
        if "R" in s["roots"] or (s["kind"] == "NDC" and "P" in s["roots"]):
            unclear.append(f"over-patch rule on a mixed chain ({where}, roots {s['roots']})")
        else:
            bugs.append(("A", f"over-patched {where}: its uv is the engine's view uv (c29 x c31) already, the patch divides it again",
                         "skip the site: Classify must return VIEW when any root is the viewport (c29)"))

    if replaced:
        # The replacement's own code decides; the game's is only a hint.
        risky = [r for r in screen_reads if r["cls"] in ("input", "vonly")] + [n for n in screen_ndc if n["cls"] in ("tex", "cbpos")]
        game_sites = [s for s in sites if s["kind"] in ("NDC", "PROJECTED", "SPLIT")]
        if replaced["share"]:
            # Built with /Qstrip_reflect: Reflect takes a code without RDEF that declares cb12 as render_scale (Pharaoh v58).
            notes.append("a render-share replacement (no RDEF, dcl cb12: view_share binds its cb12)")
        elif game_sites and not all(s["over"] for s in game_sites):
            bugs.append(("B", "the replacement keeps the game's " + ", ".join(f"{s['kind']}@{s['index']}" for s in game_sites if not s["over"]) +
                         " that view_share patches in the game's code; the stripped replacement is not patched",
                         "divide the uv by FrameRenderShare() in the replacement before the NDC (with the Reflect fix)"))
        elif game_sites:
            notes.append("the game's code is over-patched; the replacement escapes it")
        elif risky:
            unclear.append("replacement without a render share; the game's code reads " +
                           ", ".join(sorted({(r.get('slot', 'NDC') + ' ' + r['cls']) for r in risky}))[:120])
    else:
        # NDC built from a texture uv (or a named / unnamed screen constant) that no site patches.
        for n in screen_ndc:
            site = site_at.get(n["index"])
            if n["cls"] in ("tex", "cbpos") and site is None:
                if "T" in n["tags"]:
                    bugs.append(("B", f"NDC L{n['line']} from the texture uv (c27 / c31) not patched", "view_share NDC patch (form not matched)"))
                elif "S" in n["tags"]:
                    bugs.append(("B", f"NDC L{n['line']} from SV_Position x a non-camera screen size", "replacement with FrameRenderShare (view_share sees only the camera's)"))
                else:
                    unclear.append(f"NDC L{n['line']} from SV_Position x another constant")
            if n["cls"] == "vonly":
                notes.append(f"NDC L{n['line']} from the viewport alone: right")
        # Projected uvs (clip -> uv) of the camera not patched.
        for p in screen_proj:
            site = site_at.get(p["index"])
            if p["cls"] == "proj" and site is None:
                unclear.append(f"projected uv L{p['line']} (camera projection) not patched: right only if it is not a read of a render-scale texture")
        # Reads.
        draws = traces.get(h, {}).get("traces", {}) if traces else {}
        for r in screen_reads:
            slot = int(r["slot"][1:])
            if region_slots >> slot & 1:
                continue
            if r["cls"] == "vonly":
                if not RENDER_TARGET.search(r["name"]):
                    continue  # a tiled / dithered texture at the view's pixels
                bugs.append(("B", f"{r['slot']} {r['name']} L{r['line']} sampled at the view's uv (SV_Position x 1 / c29 alone): the whole "
                             "texture over the view below 100%", f"RENDER_SCALE_READS slot {r['slot']} (x the frame's share)"))
            elif r["cls"] == "input":
                if STATIC_TEXTURE.search(r["name"]) and not RENDER_TARGET.search(r["name"]):
                    continue
                evidence = []
                for label, ds in draws.items():
                    for d in ds:
                        tex = d["textures"].get(r["slot"])
                        if not tex:
                            continue
                        size = (tex[1], tex[2])
                        if d["scaled"] and is_display_texture(size, d["display"]):
                            evidence.append(("bug", f"{label}: drawn at the render share, {r['slot']} {size[0]}x{size[1]} display-size"))
                        elif not d["scaled"] and r["slot"] in d["render_space"]:
                            evidence.append(("bug", f"{label}: drawn whole, {r['slot']} {size[0]}x{size[1]} written at the render share this frame"))
                        else:
                            evidence.append(("ok", f"{label}: {'scaled' if d['scaled'] else 'whole'} draw, {r['slot']} {size[0]}x{size[1]}"))
                if any(e[0] == "bug" for e in evidence):
                    bugs.append(("B", f"{r['slot']} {r['name']} L{r['line']} read at the full-screen uv: " +
                                 next(e[1] for e in evidence if e[0] == "bug"), f"RENDER_SCALE_READS slot {r['slot']}"))
                elif not evidence and RENDER_TARGET.search(r["name"]):
                    unclear.append(f"{r['slot']} {r['name']} L{r['line']}: full-screen uv read (no trace draws it: render-scale texture or not?)")
    if bugs:
        kinds = sorted({b[0] for b in bugs})
        return {"verdict": "BUG (" + "/".join(kinds) + ")", "why": "; ".join(b[1] for b in bugs), "fix": "; ".join(sorted({b[2] for b in bugs})),
                "handled": handled, "notes": notes + unclear}
    if unclear:
        return {"verdict": "unclear", "why": "; ".join(unclear), "fix": "", "handled": handled, "notes": notes}
    why = []
    classes = sorted({r["cls"] for r in screen_reads} | {"NDC " + n["cls"] for n in screen_ndc})
    if handled:
        why.append("handled")
    why.append("reads " + ",".join(classes))
    return {"verdict": "OK", "why": "; ".join(why), "fix": "", "handled": handled, "notes": notes}


# -------------------------------------------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cso", required=True)
    ap.add_argument("--names", required=True)
    ap.add_argument("--addon", required=True)
    ap.add_argument("--shaders", default="")
    ap.add_argument("--view-share", default="", help="twattila/view_share.hpp (default: next to the addon's game folder)")
    ap.add_argument("--replay", required=True)
    ap.add_argument("--traces", nargs="*", default=[])
    ap.add_argument("--fxc", default=r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\fxc.exe")
    ap.add_argument("--cache", required=True)
    ap.add_argument("--table", default="")
    ap.add_argument("--tsv", default="")
    ap.add_argument("--all", action="store_true", help="also list the OK rows one by one")
    args = ap.parse_args()

    view_share_path = args.view_share or os.path.normpath(os.path.join(os.path.dirname(args.addon), "..", "twattila", "view_share.hpp"))
    tables = parse_addon(args.addon, args.shaders, view_share_path)
    names = {}
    for line in open(args.names, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 3 and parts[0].startswith("0x"):
            names.setdefault(parts[0][2:].upper(), []).append(re.sub(r"^fxc\\(dx11_)?|\.fxc$", "", parts[2]))

    # The replay (view_share's Reflect + Patch with the addon's region reads).
    os.makedirs(args.cache, exist_ok=True)
    region_file = os.path.join(args.cache, "_region_table.txt")
    with open(region_file, "w") as f:
        for h, (slots, whole) in tables["region"].items():
            f.write(f"{h} {slots}\n")
    run = subprocess.run([args.replay, args.cso, region_file, ",".join(tables["whole"]), "1" if tables["chains"] else "0"], capture_output=True,
                         text=True)
    replay = {}
    for line in run.stdout.splitlines():
        p = line.split("\t")
        if len(p) >= 13:
            replay[p[0]] = {"stage": p[1], "camera": p[2], "render_scale": p[6] == "1", "ndc": int(p[8]), "projected": int(p[9]),
                            "region": int(p[10]), "whole": int(p[11]), "sites": p[12]}

    # Listings (fxc /dumpbin, cached).
    files = [f for f in sorted(os.listdir(args.cso)) if re.match(r"0x[0-9A-Fa-f]{8}\.(ps|cs)_\d_\d\.cso$", f)]

    def listing(f):
        out = os.path.join(args.cache, f.replace(".cso", ".asm"))
        if not os.path.exists(out):
            text = subprocess.run([args.fxc, "/nologo", "/dumpbin", os.path.join(args.cso, f)], capture_output=True, text=True).stdout
            with open(out, "w", encoding="utf-8") as w:
                w.write(text)
        return f, open(out, encoding="utf-8", errors="replace").read()

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        listings = dict(pool.map(listing, files))
    traces = parse_traces(args.traces) if args.traces else {}

    rows = []
    counts = {"shaders": 0, "candidates": 0}
    for f in files:
        m = re.match(r"0x([0-9A-Fa-f]{8})\.(ps|cs)_", f)
        h, stage = m.group(1).upper(), m.group(2)
        counts["shaders"] += 1
        shader = Shader(h, stage, parse_listing(listings[f])).scan()
        # The listing's instruction order must be view_share's (the sites are its instruction indices).
        for site in ((replay.get(h) or {}).get("sites") or "-").split(" "):
            m = re.match(r"(\w+)@(\d+):", site)
            if not m:
                continue
            code = shader.listing["code"]
            index = int(m.group(2))
            want = "mad" if m.group(1) in ("NDC", "PROJECTED") else ("sample", "ld", "gather4")
            if index >= len(code) or not code[index]["op"].startswith(want):
                counts["site index mismatches"] = counts.get("site index mismatches", 0) + 1
        v = verdict(shader, replay.get(h), tables, traces, (names.get(h) or [""])[0])
        if v is None:
            continue
        counts["candidates"] += 1
        drawn = traces.get(h, {}).get("traces", {})
        def short(label):
            run, trace = label.split("/", 1)
            return run.split("_")[0] + "/" + trace.replace("trace_", "").replace("trace", "f")
        drawn_text = " ".join(f"{short(label)}{'s' if any(d['scaled'] for d in ds) else ''}{'p' if all(d['post'] for d in ds) else ''}"
                              for label, ds in sorted(drawn.items()))
        rows.append({"hash": h, "stage": stage, "name": (names.get(h) or ["?"])[0], "verdict": v["verdict"], "why": v["why"], "fix": v["fix"],
                     "handled": "; ".join(v["handled"]), "notes": "; ".join(v["notes"]), "traces": drawn_text or "-",
                     "reads": " ".join(sorted({f"{r['slot']}:{r['cls']}" for r in shader.reads if r["cls"] != "world"})),
                     "ndc": " ".join(f"L{n['line']}:{n['cls']}" for n in shader.ndc),
                     "proj": " ".join(f"L{p['line']}:{p['cls']}" for p in shader.proj),
                     "sites": (replay.get(h) or {}).get("sites", "-")})

    order = {"BUG (A)": 0, "BUG (A/B)": 1, "BUG (B)": 2, "unclear": 3, "OK": 4}
    rows.sort(key=lambda r: (order.get(r["verdict"], 9), r["traces"] == "-", r["hash"]))
    for r in rows:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    counts["drawn in a trace"] = sum(1 for r in rows if r["traces"] != "-")
    overpatched = [r for r in rows if "!" in r["sites"]]

    def cell(text, limit=260):
        text = (text or "").replace("|", "/").replace("\n", " ")
        return text if len(text) <= limit else text[:limit - 3] + "..."

    out = []
    out.append(f"Census of {counts['shaders']} pixel / compute shaders ({os.path.basename(os.path.dirname(os.path.abspath(args.cso)))}): "
               f"{counts['candidates']} read screen space or build positions from it; " +
               ", ".join(f"{k} {v}" for k, v in counts.items() if k not in ("shaders", "candidates")) + ".")
    out.append("")
    out.append("Traces: run/scale (f: the run's tw_frame_trace), s drawn at a scaled viewport (or a compute pass before the tone mapper), "
               "p only after the tone mapper. Rows with the same finding are one row (the first hash's name and lines).")
    out.append("")
    out.append("| Hashes | Stage | Name | Verdict | Why | Fix | Handled | Traces |")
    out.append("|---|---|---|---|---|---|---|---|")
    groups = {}
    for r in rows:
        if r["verdict"] == "OK" and not args.all:
            continue
        key = (r["verdict"], r["stage"], re.sub(r"L\d+|@\d+", "", r["why"]), r["fix"], re.sub(r"; addon: .*", "", r["handled"]))
        groups.setdefault(key, []).append(r)
    for key, members in groups.items():
        first = members[0]
        hashes = " ".join(m["hash"] + ("*" if m["traces"] != "-" else "") for m in members)
        traced = "; ".join(f"{m['hash']}: {m['traces']}" for m in members if m["traces"] != "-")
        out.append(f"| {hashes} | {first['stage']} | {cell(first['name'], 70)}{' (+' + str(len(members) - 1) + ')' if len(members) > 1 else ''} | "
                   f"{first['verdict']} | {cell(first['why'])} | {cell(first['fix'], 140)} | {cell(first['handled'], 100)} | {cell(traced or '-', 200)} |")
    if not args.all:
        ok = [r for r in rows if r["verdict"] == "OK"]
        out.append("")
        traced_ok = [r for r in ok if r["traces"] != "-"]
        out.append(f"OK: {len(ok)} shaders ({len(traced_ok)} drawn in a trace; all in the --tsv). Drawn: " +
                   ", ".join(f"{r['hash']} ({cell(r['why'], 40)})" for r in traced_ok))
    out.append("")
    out.append(f"Over-patch rule hits over the whole dump ({len(overpatched)} shaders; kind@view_share instruction:roots, ! the hit; "
               "T texture size, V viewport, P projection, I input, C constant, R read):")
    for r in overpatched:
        hits = " ".join(s for s in r["sites"].split(" ") if s.endswith("!"))
        out.append(f"- {r['hash']} {r['stage']} {cell(r['name'], 60)} [{r['verdict']}]: {hits}{' (traced ' + r['traces'] + ')' if r['traces'] != '-' else ''}")
    text = "\n".join(out) + "\n"
    if args.table:
        open(args.table, "w", encoding="utf-8").write(text)
    else:
        sys.stdout.write(text)
    if args.tsv:
        keys = ["hash", "stage", "name", "verdict", "why", "fix", "handled", "notes", "traces", "reads", "ndc", "proj", "sites"]
        with open(args.tsv, "w", encoding="utf-8") as f:
            f.write("\t".join(keys) + "\n")
            for r in rows:
                f.write("\t".join(str(r[k]).replace("\t", " ") for k in keys) + "\n")
    print(", ".join(f"{k} {v}" for k, v in counts.items()), file=sys.stderr)


if __name__ == "__main__":
    main()
