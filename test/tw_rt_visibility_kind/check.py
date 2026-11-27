"""check.py <fixed build> <control build> <atn-mods build>: the Attila helper's visibility fixes X10 and X09 in its DXR 1.2
builds, read from the DXIL listings build.ps1 writes into its headers (their #if 0 blocks). No GPU.

The three folders are build.ps1 outputs: this tree's; this tree's with RENODX_TW_CONTROL_DEFINES="CONTROL_SURFACE_CUT_OUTS_CULLED
CONTROL_VISIBILITY_ALPHA_TESTED CONTROL_BAKE_CUTOFF_RAW" (every fix's control); atn-mods 7e61f92ae's (the trees' common base).
1. CONTROL: every header of the control build is the base's to the byte (each fix sits behind its control alone).
2. X10: every TRACE_SURFACE of a DXR 1.2 build (its culled query: RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | CULL_NON_OPAQUE,
   640, on NO_GRASS_SMOKE, 197) has the solid cut-outs' alpha-tested nearest query beside it (512 on CUT_OUTS, 64); the
   control build has none.
3. X09: the reuse / validation / final rays of the paths and reflections test TRACE_SURFACE's visibility: accept-first
   culled queries (644) on NO_GRASS_SMOKE come with an alpha-tested CUT_OUTS one (516 on 64) each; with the control, the
   alpha-tested ones on NO_GRASS_SMOKE (516 on 197) as before.
Pairs are (the query's template flags, its TraceRayInline's instance mask), counted a header.

  python -I test\\tw_rt_visibility_kind\\check.py <fixed> <control> <base>   (exit 0: all hold)
"""
import collections
import pathlib
import re
import sys

ALLOCATE = re.compile(r'(%\d+) = call i32 @dx\.op\.allocateRayQuery2?\(i32 \d+, i32 (\d+)')
TRACE = re.compile(r'@dx\.op\.rayQuery_TraceRayInline\(i32 179, i32 (%\d+), %dx\.types\.Handle %\d+, i32 [%\w.]+, i32 (\d+),')
SURFACE = (640, 197)
CUT_OUT_NEAREST = (512, 64)
SOLID_ANY = (644, 197)
CUT_OUT_ANY = (516, 64)
ALPHA_ANY = (516, 197)
# The builds whose reuse / final rays X09 moves (rt_path.hlsli's ReSTIR GI and the reflections' reservoirs).
X09_BUILDS = ('rt_reuse', 'rt_reflect', 'rt_path')


def pairs(path):
  listing = path.read_text(errors='replace').split('#endif', 1)[0]
  flags = dict(ALLOCATE.findall(listing))
  return collections.Counter((int(flags[handle]), int(mask)) for handle, mask in TRACE.findall(listing) if handle in flags)


fixed, control, base = (pathlib.Path(argument) for argument in sys.argv[1:4])
failures = []
same = 0
for header in sorted(base.glob('*.h')):
  twin = control / header.name
  if not twin.exists() or twin.read_bytes() != header.read_bytes():
    failures.append(f'CONTROL {header.name}: not the base\'s to the byte')
  else:
    same += 1
print(f'1. control build: {same} headers the base\'s to the byte')
print('| header | surface (640, 197) | cut-out nearest (512, 64) | solid any (644, 197) | cut-out any (516, 64) | alpha any (516, 197) | control: cut-out nearest, alpha any |')
print('|---|---|---|---|---|---|---|')
for header in sorted(fixed.glob('*.h')):
  now = pairs(header)
  if not now:
    continue
  was = pairs(control / header.name) if (control / header.name).exists() else collections.Counter()
  if now == was:
    continue
  print(f'| {header.stem} | {now[SURFACE]} | {now[CUT_OUT_NEAREST]} | {now[SOLID_ANY]} | {now[CUT_OUT_ANY]} | {now[ALPHA_ANY]} | '
        f'{was[CUT_OUT_NEAREST]}, {was[ALPHA_ANY]} |')
  if now[SURFACE] and now[CUT_OUT_NEAREST] < now[SURFACE]:
    failures.append(f'X10 {header.stem}: {now[SURFACE]} TRACE_SURFACE queries, {now[CUT_OUT_NEAREST]} cut-out queries')
  if was[CUT_OUT_NEAREST]:
    failures.append(f'X10 {header.stem}: the control build has cut-out nearest queries')
  if header.stem.endswith('_omm') and header.stem.startswith(X09_BUILDS):
    moved = now[SOLID_ANY] - was[SOLID_ANY]
    if moved <= 0 or now[CUT_OUT_ANY] - was[CUT_OUT_ANY] < moved or was[ALPHA_ANY] - now[ALPHA_ANY] != moved:
      failures.append(f'X09 {header.stem}: {moved} rays moved to the solid kind, alpha-tested {was[ALPHA_ANY]} -> {now[ALPHA_ANY]}')
for line in failures:
  print('FAIL ' + line)
print('PASS' if not failures else f'{len(failures)} failure(s)')
sys.exit(1 if failures else 0)
