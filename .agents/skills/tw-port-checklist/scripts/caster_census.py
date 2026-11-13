"""Caster census of a Total War dump against the addon's ray-traced scene (rt_scene SCENE_SHADERS): every vertex shader that
frame traces draw into the G-buffer (a colour target with depth) or the sun's shadow atlas (a square depth target at least
--atlas wide, no colour), and whether the scene holds it.

usage:
  python -I caster_census.py --dump <dump folder: cso\\ and names.tsv> --cache <asm folder> --addon <game addon.cpp>
                             --trace NAME=<tw_frame_trace.txt> [--trace ...] [--atlas 4096] [--fxc <fxc.exe>] [--out <table.txt>]

Verdicts per vertex shader:
  held                 in SCENE_SHADERS (its pixel_masks list: G-buffer pixel shaders outside it are listed as left out)
  MISSING              drawn into the G-buffer, not in the scene: a caster the rays never hold (find its capture: the
                       nearest held VS with the same inputs, diff the asm)
  atlas only           drawn only into the shadow atlas: out of view casters (cascade_keep); depth-only ones (no world
                       output) can't be kept without unprojecting the light's matrix
Limits: only what the traces drew (dead shaders and other modes need a trace of their own); draw-time facts (a dynamic
vertex buffer, instance data, which PS pairs with which VS, a depth prefill sharing the VS) come from the traces, not the
bytes; alpha tests: run ps_discards on the listed pixel shaders (a discard of t0.w at TEXCOORD = a cut-out).
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
args = parser.parse_args()

# The vertex shaders' asm, cached.
os.makedirs(args.cache, exist_ok=True)
csos = [name for name in os.listdir(os.path.join(args.dump, 'cso')) if '.vs_' in name]


def disassemble(name):
    out = os.path.join(args.cache, name.replace('.cso', '.asm'))
    if not os.path.exists(out):
        subprocess.run([args.fxc, '/nologo', '/dumpbin', os.path.join(args.dump, 'cso', name), '/Fc', out], capture_output=True)


with concurrent.futures.ThreadPoolExecutor(8) as pool:
    list(pool.map(disassemble, csos))

names = collections.defaultdict(set)
for line in open(os.path.join(args.dump, 'names.tsv'), encoding='utf-8', errors='replace'):
    parts = line.rstrip('\n').split('\t')
    if len(parts) >= 3:
        name = re.sub(r'_enable_\w+?_sm_', '_sm_', parts[2].replace('fxc\\dx11_', '').replace('.fxc', ''))
        names[parts[0].upper().replace('0X', '')].add(name)

depth_only = {}
for name in csos:
    text = open(os.path.join(args.cache, name.replace('.cso', '.asm')), encoding='utf-8', errors='replace').read()
    outputs = re.findall(r'^dcl_output(?:_siv)? (o\d+)', text, re.M)
    depth_only[name[2:10].upper()] = len(outputs) == 1

# What the traces drew: per VS, per trace and kind, draws, indices and pixel shaders.
drawn = collections.defaultdict(lambda: [0, 0, set()])
for entry in args.trace:
    tag, path = entry.split('=', 1)
    for line in open(path, encoding='utf-8', errors='replace'):
        match = re.match(r'(\w+) (\d+) vs (\w+) ps (\w+)', line)
        if not match or match.group(1) in ('MARK', 'clear', 'copy'):
            continue
        targets = line.split(' | ')[1] if ' | ' in line else ''
        depth = re.search(r'ds 0x\w+ (\d+)x(\d+)', targets)
        colour = re.search(r'rt0 0x\w+ (\d+)x(\d+)', targets)
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

# What the addon's scene holds: SCENE_SHADERS' hashes, and each one's pixel_masks list.
addon = open(args.addon, encoding='utf-8', errors='replace').read()
scene = re.search(r'SCENE_SHADERS = \{\{(.*?)\n\}\};', addon, re.S)
held = {}
for entry in re.finditer(r'\{\.hash = 0x([0-9A-Fa-f]{8})([^\n]*)', scene.group(1) if scene else ''):
    masks = re.search(r'\.pixel_masks = (\w+)', entry.group(2))
    listed = None
    if masks:
        array = re.search(masks.group(1) + r' = \{\{(.*?)\n\}\};', addon, re.S)
        listed = set(re.findall(r'pixel_shader = 0x([0-9A-Fa-f]{8})', array.group(1))) if array else set()
        listed = {hash_.upper() for hash_ in listed}
    held[entry.group(1).upper()] = listed

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
