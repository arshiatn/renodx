# tw_fog_restir: U6 part 1 gates (Volumetric ReSTIR fog)

`src/games/twattila/dlaa_helper/rt_fog_reservoir.hlsli` (the fog local stream's reservoir math behind `FOG_RESTIR` in
`rt_fog.cs_6_5.hlsl`; design `plan_1117/designs/unified_pt.md` 4.6 and U6) run in `fog_probe.hlsl` on D3D12 WARP (dxc
`cs_6_5 -O3` as build.ps1; `cs_6_9` compiled statically). Each of 65536 trials runs the helper's stages with its own
numbers on a synthetic scene and shades the chosen sample with an analytic visibility (the plane z = 0 blocks):

- candidates: `FogCandidates` (8 squared-spacing points, the list's pick, the defensive uniform pick);
- temporal: the candidates merged with an independent reservoir of the same ray at M = 4 drawn from last frame's lights
  (`ResampleFogReservoirs`, `ChooseFogReservoir`; a still camera; its domain `last_trace`, so a light new this trace
  has target 0 there);
- spatial: four reservoirs of four rays (other directions, one range far shorter, one a sky ray), one and two final
  samples;
- controls: the neighbours merged with M-share weights instead of Talbot's MIS, the candidates without the defensive
  pick, and the temporal merge with last frame's domain's target kept for a new light (as part 1 first built it).

`tests.cpp` averages the trials against a double Simpson quadrature (2M intervals) of (1 / R) x the integral over the
ray of every light's colour x falloff x visibility, and the one-light case's quadrature against its closed form. It also
compiles the four FOG_RESTIR builds of `rt_fog.cs_6_5.hlsl` (main / shade, cs_6_5 / cs_6_9 with OPACITY_MICROMAPS, as
build.ps1) and fails on any `alloca` (a local-memory array) in their DXIL.

- Run: `powershell -File test\tw_fog_restir\run.ps1 [-Bin <folder of dxcompiler.dll and dxil.dll>] [-Out <build
  folder>]`. Builds with clang-cl into `tools\tw\testbin\tw_fog_restir` (or `-Out`), takes `tools\tw\gpu_job.lock`
  (lab\gpu_lock.py) for the WARP run, logs to `last_run.txt` (UTF-8) there, exits 0 only when every gate passes and
  every expected-biased control is caught.

## Gates

| Gate | Bar |
|---|---|
| S1 one linear light: quadrature = closed form | relative < 1e-6 |
| FOG_RESTIR builds (4) | compile, no `alloca` |
| every stage unbiased (S1, S2 twelve lights / list of 5 / plane, S3 eight smooth lights / no defensive pick, S5 128 lights / full list of 32 with ids up to 127, S6 a light new this trace) | worst channel within 4 standard errors |
| power: each stage's standard error | < 1% of the reference |
| S2 controls caught (M-share weights; no defensive pick) | beyond 6 standard errors |
| S5 control caught (no defensive pick) | beyond 6 standard errors |
| S6 control caught (history target kept for the new light) | beyond 6 standard errors |
| S4 nothing reaches the ray | every stage exactly 0 |
| finite outputs | no NaN / Inf |
| `FogTarget` vs a double | relative 2e-5 |
| `PackFogReservoir` / `UnpackFogReservoir` | t, W, light, range exact; M its f16; target not kept (0) |
| `FogListOf` size, `CameraOf` (every scenario) | exact; relative 1e-5 |

The define-off check (rt_fog.h / rt_fog_omm.h byte-identical to the tree before U6) is the helper build's (build.ps1).
