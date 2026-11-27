"""scan.py <helper build folder> [rt_d3d12.hpp]: the Attila D3D12 helper's ray queries against its micromapped TLAS
(X18). Every header build.ps1 writes carries dxc's DXIL listing (its #if 0 block); this reads each one's ray query
allocations.

src: D3D12 Raytracing spec (DirectX-Specs d3d/Raytracing.md, "Opacity micromaps and TraceRayInline"): a RayQuery without
RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS that meets a triangle with an OMM is undefined behaviour; shader model 6.9 carries
the flag in allocateRayQuery2's third operand (bit 0), a 6.5 shader's allocateRayQuery has none.

Rules:
- a DXR 1.2 header (name *_omm, or a shader model 6.9 build: SER, SHaRC) opts in at every ray query, and has one;
- a DXR 1.1 header that traces has its _omm twin (the helper runs the twin when the device has DXR 1.2: its TLAS may be
  micromapped), the test builds' too (rt_probe, rt_reference_paths / _reflect: a build with RENODX_TW_REFERENCE_BUILD
  set holds them);
- with rt_d3d12.hpp given: every DXR 1.2 header's array (its -Vn name) is read there (made into a pipeline).

  python -I test\\tw_rt_omm_queries\\scan.py <out folder of build.ps1> src\\games\\twattila\\dlaa_helper\\rt_d3d12.hpp
      the fixed helper: exit 0
  python -I test\\tw_rt_omm_queries\\scan.py <out folder of atn-mods 7e61f92ae's build.ps1>
      CONTROL: rt_cache_fill, rt_probe, rt_reference_paths and rt_reference_reflect have no twin, exit 1
"""
import pathlib
import re
import sys

SM69 = {'rt_path_ser', 'rt_path_ser_one', 'rt_path_sharc', 'rt_path_sharc_one', 'rt_sharc_update', 'rt_sharc_update_regir'}
PLAIN = re.compile(r'@dx\.op\.allocateRayQuery\(i32 178, i32 \d+\)')
FLAGGED = re.compile(r'@dx\.op\.allocateRayQuery2\(i32 258, i32 \d+, i32 (\d+)\)')

folder = pathlib.Path(sys.argv[1])
host = pathlib.Path(sys.argv[2]).read_text(errors='replace') if len(sys.argv) > 2 else None
queries = {}
for header in sorted(folder.glob('*.h')):
  listing = header.read_text(errors='replace').split('#endif', 1)[0]
  flags = [int(flag) for flag in FLAGGED.findall(listing)]
  plain = len(PLAIN.findall(listing))
  if plain or flags:
    queries[header.stem] = (plain, sum(1 for flag in flags if flag & 1), sum(1 for flag in flags if not flag & 1))
failures = []
for name, (plain, with_omm, without_omm) in queries.items():
  if name.endswith('_omm') or name in SM69:
    if plain or without_omm or not with_omm:
      failures.append(f'{name}: DXR 1.2 build with {plain} plain and {without_omm} unflagged ray queries ({with_omm} flagged)')
    if host is not None and not re.search(r'\b' + name.upper() + r'\b', host):
      failures.append(f'{name}: {name.upper()} is read nowhere in rt_d3d12.hpp')
  elif name + '_omm' not in queries:
    failures.append(f'{name}: {plain} DXR 1.1 ray queries and no _omm twin (runs on the micromapped TLAS)')
print(f'{len(queries)} headers with ray queries in {folder}')
for line in failures:
  print('FAIL ' + line)
print('PASS' if not failures else f'{len(failures)} failure(s)')
sys.exit(1 if failures else 0)
