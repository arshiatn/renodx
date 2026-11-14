"""Caster census of a Total War dump against the addon's ray-traced scene (rt_scene SCENE_SHADERS).

Trace mode (default): every vertex shader that frame traces draw into the G-buffer (a colour target with depth) or the sun's
shadow atlas (a square depth target at least --atlas wide, no colour), and whether the scene holds it.
Whole-dump mode (--whole-dump): every vertex shader in the dump, no trace needed, classed by its names and bytes; traces, if
given, only fill the "in a trace" column.

usage:
  python -I caster_census.py --dump <dump folder: cso\\ and names.tsv> --cache <asm folder> --addon <game addon.cpp>
                             --trace NAME=<tw_frame_trace.txt or a folder of traces> [--trace ...] [--atlas 4096] [--fxc <fxc.exe>]
                             [--out <table.txt>]
  python -I caster_census.py --whole-dump --dump ... --cache ... --addon ... [--trace NAME=<file or folder> ...] [--out <table.txt>]

The addon's tables: SCENE_SHADERS, SmokeVertexShader arrays (rt_scene::PARTICLE_VERTEX_SHADERS too) and a host's scene_draw,
read from --addon and the files it includes from its own folder (Shogun 2: ray_tracing.hpp).

Trace mode verdicts per vertex shader:
  held                 in SCENE_SHADERS (its pixel_masks list: G-buffer pixel shaders outside it are listed as left out)
  MISSING              drawn into the G-buffer, not in the scene: a caster the rays never hold (find its capture: the
                       nearest held VS with the same inputs, diff the asm)
  atlas only           drawn only into the shadow atlas: out of view casters (cascade_keep); depth-only ones (no world
                       output) can't be kept without unprojecting the light's matrix
Limits: only what the traces drew (dead shaders and other modes need a trace of their own); draw-time facts (a dynamic
vertex buffer, instance data, which PS pairs with which VS, a depth prefill sharing the VS) come from the traces, not the
bytes; alpha tests: run ps_discards on the listed pixel shaders (a discard of t0.w at TEXCOORD = a cut-out).

Whole-dump verdicts per vertex shader (family from names.tsv's source file, pass from its entry point, else from the bytes):
  held                 in SCENE_SHADERS (cascade_keep: out of view too), a smoke table, or a main-pass caster under a scene_draw host
  in view only         a shadow, depth or point-light pass VS whose family's main pass is held: out of view needs a cascade_keep row
  NOT HELD             a caster family's world-space geometry the rays never get, ranked by family; in no trace = level B (one
                       trace, or a WARP check of its capture, before it ships); "addon names it": the addon mentions the hash
                       elsewhere (a skip list, a comment saying why it stays out)
  not a caster         UI, full-screen and post, lights, sky and fog, water, decals and overlays, effects and particles (smoke
                       tables aside), debug and editor, input-layout stubs (no output)
  dead                 every name overridden by a later pack (names.tsv's fourth column: Rome II's data.pack builds)
The limits are printed under the table.
"""
import argparse
import collections
import concurrent.futures
import os
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--dump', required=True)
parser.add_argument('--cache', required=True)
parser.add_argument('--addon', required=True)
parser.add_argument('--trace', action='append', default=[])
parser.add_argument('--atlas', type=int, default=4096)
parser.add_argument('--fxc', default=r'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\fxc.exe')
parser.add_argument('--out')
parser.add_argument('--whole-dump', action='store_true')
args = parser.parse_args()

# The vertex shaders' asm, cached.
os.makedirs(args.cache, exist_ok=True)
csos = sorted(name for name in os.listdir(os.path.join(args.dump, 'cso')) if '.vs_' in name and name.endswith('.cso'))


def disassemble(name):
    out = os.path.join(args.cache, name.replace('.cso', '.asm'))
    if not os.path.exists(out):
        subprocess.run([args.fxc, '/nologo', '/dumpbin', os.path.join(args.dump, 'cso', name), '/Fc', out], capture_output=True)


with concurrent.futures.ThreadPoolExecutor(8) as pool:
    list(pool.map(disassemble, csos))
asm = {name[2:10].upper(): open(os.path.join(args.cache, name.replace('.cso', '.asm')), encoding='utf-8', errors='replace').read()
       for name in csos}

# Source names: names.tsv (hash, pack, name, 1 if a later pack overrides it), Shogun 2's shader_names.txt (file, name); Warhammer
# III's hashed file names shortened. `live`: a name no later pack overrides (Rome II's data.pack builds under data_rome2.pack's).
names = collections.defaultdict(set)
live = set()
for listing in ('names.tsv', 'shader_names.txt'):
    path = os.path.join(args.dump, listing)
    for line in open(path, encoding='utf-8', errors='replace') if os.path.exists(path) else []:
        parts = line.rstrip('\n').split('\t')
        found = re.match(r'0x([0-9A-Fa-f]{8})', parts[0])
        if found and len(parts) >= 2:
            name = re.sub(r'^(fxc\\)?(dx1[12]_)?(enable_\w+?_(?=sm_\d))?', '', parts[2] if len(parts) >= 3 else parts[1])
            names[found.group(1).upper()].add(re.sub(r'(_hlsl_[0-9a-f]{32})?\.(hlsl_compiled|fxc)$', '', name))
            if len(parts) < 4 or parts[3] != '1':
                live.add(found.group(1).upper())

depth_only = {vs: len(re.findall(r'^dcl_output(?:_siv)? (o\d+)', text, re.M)) == 1 for vs, text in asm.items()}

# What the traces drew: per VS, per trace and kind, draws, indices and pixel shaders; `seen`: per VS, each trace drawing it at all
# and how (G colour and depth targets, D depth only at any size, o other).
traces = []
for entry in args.trace:
    tag, path = entry.split('=', 1)
    if not os.path.isdir(path):
        traces.append((tag, path))
        continue
    for directory, _, files in os.walk(path):
        if 'backup' not in re.split(r'[\\/]', directory):
            traces += [(tag + '/' + os.path.relpath(os.path.join(directory, f), path).replace('\\', '/').replace('.txt', ''), os.path.join(directory, f))
                       for f in sorted(files) if 'trace' in f and f.endswith('.txt')]
drawn = collections.defaultdict(lambda: [0, 0, set()])
seen = collections.defaultdict(lambda: collections.defaultdict(set))
traced = set()
for tag, path in traces:
    for line in open(path, encoding='utf-8', errors='replace'):
        match = re.match(r'(\w+) (\d+) vs (\w+) ps (\w+)', line)
        if not match or match.group(1) in ('MARK', 'clear', 'copy'):
            continue
        traced.add(tag)
        targets = line.split(' | ')[1] if ' | ' in line else ''
        depth = re.search(r'ds 0x\w+ (\d+)x(\d+)', targets)
        colour = re.search(r'rt0 0x\w+ (\d+)x(\d+)', targets)
        seen[match.group(3).upper()][tag].add('G' if colour and depth and colour.group(1) != '1' else 'D' if depth and not colour else 'o')
        if depth and not colour and depth.group(1) == depth.group(2) and int(depth.group(1)) >= args.atlas:
            kind = 'atlas'
        elif colour and depth and colour.group(1) != '1':
            kind = 'gbuf'
        else:
            continue
        record = drawn[(match.group(3).upper(), tag, kind)]
        record[0] += 1
        record[1] += int(match.group(2))
        record[2].add(match.group(4).upper())

# What the addon's scene holds: SCENE_SHADERS' hashes, each one's pixel_masks list and its flags (the addon and its own includes).
addon = open(args.addon, encoding='utf-8', errors='replace').read()
for include in re.findall(r'#include "\./([\w.]+)"', addon):
    path = os.path.join(os.path.dirname(args.addon), include)
    addon += '\n' + (open(path, encoding='utf-8', errors='replace').read() if os.path.exists(path) else '')
scene = re.search(r'SCENE_SHADERS = \{\{(.*?)\n\}\};', addon, re.S)
held = {}
flags = {}
for entry in re.finditer(r'\{\.hash = 0x([0-9A-Fa-f]{8})([^\n]*)', scene.group(1) if scene else ''):
    masks = re.search(r'\.pixel_masks = (\w+)', entry.group(2))
    listed = None
    if masks:
        array = re.search(masks.group(1) + r' = \{\{(.*?)\n\}\};', addon, re.S)
        listed = set(re.findall(r'pixel_shader = 0x([0-9A-Fa-f]{8})', array.group(1))) if array else set()
        listed = {hash_.upper() for hash_ in listed}
    held[entry.group(1).upper()] = listed
for chunk in re.split(r'\{\.hash = 0x', re.sub(r'//[^\n]*', '', scene.group(1)) if scene else '')[1:]:
    flags[chunk[:8].upper()] = chunk

if not args.whole_dump:
    rows = []
    for vs in sorted({key[0] for key in drawn}):
        kinds = {kind for (hash_, _, kind) in drawn if hash_ == vs}
        pixel_shaders = set().union(*(record[2] for (hash_, _, kind), record in drawn.items() if hash_ == vs and kind == 'gbuf'))
        counts = ' '.join(f'{tag}:{kind} {record[0]}/{record[1]}' for (hash_, tag, kind), record in sorted(drawn.items()) if hash_ == vs)
        if vs in held:
            left_out = sorted(pixel_shaders - held[vs]) if held[vs] is not None else []
            verdict = 'held' + (f', PS left out: {",".join(left_out)}' if left_out else '')
        elif 'gbuf' in kinds:
            verdict = 'MISSING'
        else:
            verdict = 'atlas only' + (' (depth-only VS)' if depth_only.get(vs) else '')
        rows.append(f'{vs}\t{verdict}\t{"; ".join(sorted(names.get(vs, []))[:3])}\t{counts}\tPS {",".join(sorted(pixel_shaders)) or "-"}')
    text = 'vs\tverdict\tnames\tdraws/indices by trace:kind\tG-buffer PS\n' + '\n'.join(rows) + '\n'
    if args.out:
        open(args.out, 'w', encoding='utf-8').write(text)
    print(text)
    raise SystemExit

# ------------------------------------------------------------------------------------------------------------- whole dump
# Smoke tables (particles the rays hold apart from the scene) and a scene_draw host (every main-pass G-buffer draw is the scene).
smoke = set()
for block in re.findall(r'SmokeVertexShader, \d+> \w+ = \{\{(.*?)\}\};', addon, re.S):
    smoke |= {hash_.upper() for hash_ in re.findall(r'\.hash = 0x([0-9A-Fa-f]{8})', block)}
rt_scene_path = os.path.join(os.path.dirname(args.addon), '..', 'twattila', 'rt_scene.hpp')
if 'rt_scene::PARTICLE_VERTEX_SHADERS' in addon and os.path.exists(rt_scene_path):
    smoke |= {hash_.upper() for hash_ in re.findall(r'PARTICLE_VERTEX_SHADER = 0x([0-9A-Fa-f]{8})', open(rt_scene_path, encoding='utf-8').read())}
scene_draw = re.search(r'\.scene_draw = \w', addon) is not None
# Hashes the addon names elsewhere (a skip list, a comment giving why it stays out): read that before calling a gap.
named = {}
table = None
for line in addon.splitlines():
    found = re.search(r'\b([A-Z][A-Z0-9_]{2,})\s*=\s*\{', line)
    table = found.group(1) if found else table
    code, _, comment = line.partition('//')
    for hash_ in re.findall(r'\b(?:0x)?([0-9A-Fa-f]{8})\b', code):
        named.setdefault(hash_.upper(), table or 'code')
    for hash_ in re.findall(r'\b(?:0x)?([0-9A-Fa-f]{8})\b', comment):
        named.setdefault(hash_.upper(), 'a comment')
    if line.strip() in ('}};', '};'):
        table = None

# Families by the source file's name (first match: units and cloth before the words their materials share with effects and UI,
# "weighted ... blood", "ui_capture_flag"), then a few entry-point hints. Casters in report order.
CASTERS = ['skinned', 'rigid', 'trees', 'leaves', 'grass', 'far cards', 'sprites', 'cloth', 'terrain', 'world (unknown)']
MAIN = ('main', 'GS/HS')  # passes that draw what the view sees
FAMILIES = [
    ('skinned', r'weighted'),
    ('cloth', r'cloth|sail|banner|flag|rope|wind_plane|curtain|awning'),
    ('ui', r'^sprite$|font|^ui_|_ui_|indicator|outline|cursor|minimap|loadingscreen|stance|selection|tutorial|campaignborder'),
    ('post', r'blur|hdr|tone_?mapper|bloom|glow|depth_?of_?field|fxaa|smaa|ssao|(^|_)a?oit|compression|passthrough|clear|^fade$|texturedebug|'
             r'virtualtexture|downsample|deferred|full_?screen|lense?_?flare|distortionview|alpha_blend|quadonly|post_?process'),
    ('debug', r'debug|editor|terry|collision|gizmo|visibility|grid|^test|_test|testdata|example'),
    ('light', r'(point|spot|directional|sun|sky|environment_specular_|ambient_)light|gobo|light_?probe'),
    ('sky and fog', r'sky|cloud|^sun$|star|shroud|aurora|vortex|godray|mist|fog'),
    ('water', r'ocean|water|river|^sea|_sea|seainteraction|wake|lake|splash|bowwave|lava|fluid|ice_?berg|maelstorm'),
    ('decal', r'decal|spline|footprint|road|border|trade_route|coast|blood|ground_plane|deployment|projected'),
    ('effects', r'particle|vfx|trail|(^|_)rain|smoke|flame|fire_base|feedback|ribbon|projectile|spark|soul|portal|beam|lightning|weather'),
    ('far cards', r'treebillboard|tree_billboard|billboard'),
    ('sprites', r'imposter'),
    ('grass', r'grass'),
    ('leaves', r'leaf|vegetation|foliage|shrub|bush|plant|fern|hedge'),
    ('trees', r'branch|tree|trunk|palm'),
    ('skinned', r'skin(?!lm)|horse'),
    ('terrain', r'^rigidterrain$|terraintile|tessellation|campaignterrain|terrain_global|terrain_stencil|^terrain|ground_mesh'),
    ('rigid', r'rigid|building|prop|rock|wall|static|standard|alphablend|default|mesh|basic|simple|ambientmap|common|anisotropic|'
              r'vertexpush|tint|metal|glass|crystal|stone|wood|tent|gate|ship|siege|statue|mountain|cliff|parallax|tiling|zup'),
]
CAMERA = re.compile(r'^(view_projection|view|projection|world_view_projection|g_world_view|g_projection|g_world_view_projection|'
                    r'g_frustum_points|g_overlay_view_projection)$')


def name_parts(name):
    """(source file, entry point) of a short name: rigidleafcommon_v3 / vs30_shadow; no entry in Warhammer III's names."""
    found = re.match(r'(?:sm_\d_\d_)?(?:default_)?(.*?)(?:\.hlsl_(?:sm_\d_\d__)?vs_(\w+))?$', name)
    file = re.sub(r'^(fx_+|shaders_+)', '', found.group(1).replace('.xml', '').replace('_xml', ''))
    return file.lower(), (found.group(2) or '').lower()


def classify(vs, text):
    """(family, pass, evidence) of a vertex shader from its names and what its bytes tell (signatures, used constants, resources;
    a stripped RDEF with constant buffers counts as a camera)."""
    signature = re.search(r'// Input signature:(.*?)// Output signature', text, re.S)
    inputs = re.findall(r'^// (\w+)\s+(\d+)\s+[xyzw]+\s+\d+', signature.group(1), re.M) if signature else []
    used = set(re.findall(r'^//\s+(?:row_major |column_major )?\S+\s+(\w+)(?:\[\d+\])?;\s*// Offset:\s*\d+ Size:\s*\d+$', text, re.M))
    resources = ' '.join(re.findall(r'^// (\S+)\s+(?:texture|cbuffer|sampler|UAV)\s', text, re.M)).lower()
    semantics = {semantic.upper() for semantic, _ in inputs}
    outputs = re.search(r'// Output signature:(.*?)\n[^/]', text, re.S)
    bytes_ = {
        'outputs': len(re.findall(r'^// [A-Za-z_]\w*\s+\d+\s', outputs.group(1), re.M)) if outputs else 0,
        'position': re.search(r'^dcl_output_siv o\d+\.\w+, position', text, re.M) is not None,
        'camera': any(CAMERA.match(v) for v in used) or ('dcl_constantbuffer' in text and '// Resource Bindings:' not in text),
        'bones': bool({'BLENDINDICES', 'BLENDWEIGHT'} & semantics) or re.search(r'animation_map|bone|skeleton|palette', resources) is not None,
        'instanced': 'SV_INSTANCEID' in semantics or any(s.upper() == 'POSITION' and int(i) > 0 for s, i in inputs) or 'instance' in resources,
        'wind': any(re.search(r'wind|sway|tremble|gust|weaviness', v) for v in used) or 'wind' in resources,
        'height map': re.search(r'height_?map', resources) is not None,
        'simulation': '_sim_' in resources,
        'main markers': 'pixel_selection' in resources,  # the G-buffer pass's object ids (Warhammer III's shadow VS bind rigid_config too)
    }
    evidence = [key for key in ('bones', 'instanced', 'wind', 'height map', 'simulation') if bytes_[key]]
    if bytes_['outputs'] == 0:
        return 'stub', 'main', ['no output']
    if not bytes_['position']:
        evidence.append('GS/HS next')
    if not bytes_['camera']:
        evidence.append('no camera')
    parts = [name_parts(name) for name in sorted(names.get(vs, []))]  # sorted: ties go the same way every run
    votes = collections.Counter()
    for file, entry in parts:
        family = next((family for family, pattern in FAMILIES if re.search(pattern, file)), None)
        if family in (None, 'rigid'):
            family = ('trees' if 'campaign_trees' in entry else 'grass' if 'grass' in entry else 'far cards' if 'billboard' in entry
                      else family)
        if family == 'terrain' and re.search(r'blend|normalize|stencil|to_gbuffer', entry):
            family = 'terrain texture pass'
        if re.search(r'debug|editor', entry):
            family = 'debug'
        if re.search(r'coast|trade_route|border', entry):
            family = 'decal'
        votes[family or 'unknown'] += 1
    # A VS shared by many materials draws a caster if a quarter of its names do (Warhammer III's 4F35D09F: buildings, water, effects).
    known = sum(count for family, count in votes.items() if family != 'unknown')
    ranked = ([family for family, count in votes.most_common() if family in CASTERS and count * 4 >= known] +
              [family for family, _ in votes.most_common() if family != 'unknown'])
    family = (ranked or ['unknown'])[0]
    others = sorted(set(votes) - {family})
    if others:
        evidence.append('also ' + '/'.join(others))
    if family in ('rigid', 'unknown') and bytes_['bones']:
        family = 'skinned'
    if family == 'unknown' and bytes_['height map']:
        family = 'terrain'
    if family == 'unknown':
        world = bytes_['camera'] or bytes_['bones'] or bytes_['instanced']
        family = 'world (unknown)' if world else 'screen (unknown)'
    entries = collections.Counter(
        'point' if 'pointlight' in entry else 'shadow' if 'shadow' in entry else 'depth' if 'depth' in entry else 'main'
        for _, entry in parts if entry)
    if entries:
        pass_ = 'main' if 'main' in entries else entries.most_common(1)[0][0]
    elif not bytes_['position']:
        pass_ = 'GS/HS (bytes)'  # tessellation (a main pass) or a point light's cube faces: counted as main
    elif not bytes_['camera'] and (bytes_['bones'] or bytes_['instanced']):
        pass_ = 'point (bytes)'
    elif bytes_['main markers'] or bytes_['outputs'] >= 6:
        pass_ = 'main (bytes)'
    else:
        pass_ = 'shadow (bytes)'
    if family in CASTERS and not bytes_['camera'] and pass_.startswith('main') and bytes_['position'] and not (bytes_['bones'] or bytes_['instanced']):
        family = 'screen (no camera)'
    return family, pass_, evidence


KINDS = (('out of view', r'\.cascade_keep = '), ('terrain', r'\.terrain = true'), ('grass', r'\.grass = true'), ('far tree', r'\.far_tree = true'),
         ('sun-facing', r'\.sun_facing = true'), ('thin', r'\.thin = true'), ('foliage', r'\.foliage = true'), ('tessellated', r'\.tessellated = true'),
         ('indirect', r'\.indirect = true'), ('placed', r'\.rows_shader = '), ('every frame', r'\.dynamic = true'), ('PS list', r'\.pixel_masks = '))
rows = {}
for vs, text in asm.items():
    family, pass_, evidence = classify(vs, text)
    rows[vs] = {'family': family, 'pass': pass_, 'evidence': evidence, 'files': {name_parts(name)[0] for name in names.get(vs, [])}}
held_main_files = set()
for vs, row in rows.items():
    main = row['pass'].startswith(MAIN)
    if vs in held or vs in smoke or (scene_draw and main and row['family'] in CASTERS):
        row['verdict'] = 'held'
        row['why'] = (', '.join(tag for tag, pattern in KINDS if re.search(pattern, flags.get(vs, ''))) or 'scene' if vs in held
                      else 'smoke table' if vs in smoke else 'scene_draw (every main-pass G-buffer draw)')
        if main:
            held_main_files |= row['files']
for vs, row in rows.items():
    if 'verdict' in row:
        continue
    if vs in names and vs not in live:
        row['verdict'], row['why'] = 'dead', 'every name overridden by a later pack'
    elif row['family'] not in CASTERS:
        row['verdict'], row['why'] = 'not a caster', row['family']
    elif not row['pass'].startswith(MAIN) and row['files'] & held_main_files:
        row['verdict'], row['why'] = 'in view only', 'main pass held: ' + ', '.join(sorted(row['files'] & held_main_files))[:60]
    else:
        row['verdict'] = 'NOT HELD'
        row['why'] = ('level A gap: drawn in a trace' if seen.get(vs) else 'level B: in no trace') + ('' if row['pass'].startswith(MAIN)
                                                                                                   else '; no held main pass')
        row['why'] += f'; addon names it in {named[vs]}' if vs in named else ''

counts = collections.Counter(row['verdict'] for row in rows.values())
not_held = [vs for vs, row in rows.items() if row['verdict'] == 'NOT HELD']
main_not_held = [vs for vs in not_held if rows[vs]['pass'].startswith(MAIN)]
out = [f'Whole-dump caster census: {os.path.basename(os.path.normpath(args.dump))}, {len(rows)} vertex shaders; addon '
       f'{os.path.basename(os.path.dirname(os.path.abspath(args.addon)))}/{os.path.basename(args.addon)}: SCENE_SHADERS {len(held)} '
       f'(cascade_keep {sum(1 for chunk in flags.values() if ".cascade_keep = " in chunk)}), smoke {len(smoke)}, '
       f'scene_draw {"yes" if scene_draw else "no"}; traces {len(traced)}.',
       f'held {counts["held"]}, in view only {counts["in view only"]}, NOT HELD {counts["NOT HELD"]} (main pass {len(main_not_held)}; '
       f'in a trace {sum(1 for vs in not_held if seen.get(vs))}, level B {sum(1 for vs in not_held if not seen.get(vs))}), '
       f'not a caster {counts["not a caster"]}, dead {counts["dead"]} (every name overridden by a later pack).',
       '',
       'NOT HELD by family (main pass first; * drawn in a trace, level A; the rest level B):']
for family in CASTERS:
    members = sorted((vs for vs in not_held if rows[vs]['family'] == family),
                     key=lambda vs: (not rows[vs]['pass'].startswith(MAIN), not seen.get(vs), vs))
    if members:
        listed = ', '.join(f'{vs}{"*" if seen.get(vs) else ""} {rows[vs]["pass"]} {sorted(names.get(vs, ["?"]))[0][:48]}'
                           f'{" [addon: " + named[vs] + "]" if vs in named else ""}' for vs in members)
        out.append(f'  {family} {len(members)}: {listed}')
out += ['', 'In view only (their family held in the main pass; casters out of view need a cascade_keep row):']
by_family = collections.defaultdict(list)
for vs, row in sorted(rows.items()):
    if row['verdict'] == 'in view only':
        by_family[row['family']].append(vs + ('*' if seen.get(vs) else ''))
out += [f'  {family} {len(members)}: {" ".join(members)}' for family, members in sorted(by_family.items())]
out += ['', 'vs\tverdict\tfamily\tpass\theld as / why\tbytes\tnames\tin a trace (traces drawing it / all: G colour + depth, D depth only, o other)']
order = {'NOT HELD': 0, 'in view only': 1, 'held': 2, 'not a caster': 3, 'dead': 4}
for vs, row in sorted(rows.items(), key=lambda item: (order[item[1]['verdict']], item[1]['family'], item[0])):
    tags = seen.get(vs, {})
    kinds = ', '.join(f'{kind} {sum(1 for how in tags.values() if kind in how)}' for kind in 'GDo')
    out.append(f'{vs}\t{row["verdict"]}\t{row["family"]}\t{row["pass"]}\t{row["why"]}\t{" ".join(row["evidence"]) or "-"}\t'
               f'{"; ".join(sorted(names.get(vs, []))[:2])[:110] or "-"}\t'
               f'{f"{len(tags)}/{len(traced)} ({kinds}) {sorted(tags)[0]}" if tags else "-"}')
out += ['',
        'Limits: static. Family from the source file names by regex (a VS of many materials: a caster family if a quarter of its names',
        'are one), refined by bytes (bones, height map); pass from the entry point (vs*_shadow, _depth, _pointlight); without one',
        '(Warhammer III, unnamed) from the bytes: no SV_Position = GS/HS (tessellation or a point light\'s cube faces, counted as main),',
        'a G-buffer marker (pixel selection) or 6+ outputs = main, a world position without a camera = point-light pass,',
        'else shadow, marked (bytes). "in view only" by a held main-pass VS of the same source file (not proof of the same meshes).',
        'A VS with no names table entry has no family but by its bytes. Which draws are G-buffer draws,',
        'which pixel shaders pair (pixel_masks, cut-outs, a VS shared with water or effects), dynamic buffers, instance data and settings',
        'or modes that select a permutation come only from traces. A scene_draw host holds every main-pass caster here; at run time',
        'only its opaque G-buffer draws. Only overridden packs count as dead: other unused shaders look live, so NOT HELD in no trace',
        'is level B until one trace or a WARP check of its capture. "addon names it": read why it stays out before calling it a gap.']
text = '\n'.join(out) + '\n'
if args.out:
    open(args.out, 'w', encoding='utf-8').write(text)
print(text)
