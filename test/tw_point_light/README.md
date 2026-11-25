# tw_point_light: PT DI step 4a, W1

`src/games/twattila/tw_point_light.hlsli` (`TwPointLight`) against each of Attila's 40 point-light pixel shaders: the
game's own DXBC (`bin\cmd_Decompiler-1.3.16\dump_shader_twattila_pack\cso\<hash>.<target>.cso`, data.pack) run on WARP
on the same inputs. Design: `designs/pt_di_rtxdi.md` 2.1, 6 (W1), Revision 2 (R2.1 point 13, R2.5).

- `point_light_ps.hlsl`: the pixel probe, drawn as the game's shaders are (same interpolants, textures, samplers and
  constants, bound by name), compiled by fxc `/O3` at each variant's own target; it samples the G-buffer and SSAO and
  rebuilds the world position as the game's DXBC does (the probe's lines), then calls `TwPointLight`.
- `point_light_cs.hlsl`: the compute probe, records of what the pixel probe read, compiled by dxc `cs_6_5 -O3` (the
  helper's compiler) and run on D3D12 WARP; fxc `cs_5_0` and dxc `cs_6_9` compile it too.
- `tests.cpp`: the gates, controls and the variant table. Run (WARP only, no GPU job):
  `powershell -File test\tw_point_light\run.ps1 -Bin D:\Dev\renodx\bin` (the folder of dxcompiler.dll, dxil.dll and
  cmd_Decompiler-1.3.16). Builds with clang-cl into `tools\tw\testbin\tw_point_light`, logs to `last_run.txt` there,
  exits 0 only when every gate passes and every control fails.

## What the include transcribes

| Part | The game's DXBC | `TwPointLight` |
|---|---|---|
| model | each light drawn twice, stencil ref 1 (slm) then 3 (skinlm), read mask 3 (O1, judge_m1_o1.md 2) | `surface.model` = stencil & 3: 1 slm, 3 skinlm, else 0 |
| falloff | smooth `(1 - saturate(d / r))^2`, linear without the square | `linear_falloff` (the light's `rt_light_kinds` byte) |
| HDR | `0 < g_hdr_on`: diffuse x 0.004 / 1, specular x 1 / 273, the lobe's gloss^2 term 0.9959 / -0.0001 | `diffuse_scale` = the sun texture's texel 2 w (0.004 or 1) |
| slm | albedo squared, energy-normalised lobe over a half-degree disk (acos, Winitzki erf / erfinv), Fresnel-like rise, x SSAO | the same operations in the same order |
| skinlm | albedo not squared, Phong (R.V)^(63 gloss + 1.6) x ch2.w x N.L, wrap light by ch3.y, diffuse x 0.9, no SSAO | the same |
| lowdef | the draw's output saturated (`mul_sat`) | `low_def` |
| shadow / gather 4.1 / 5.0 | the game's point shadow map (slm: (light x V) x falloff; skinlm: light x (V x falloff)) | not in the include: V = 1 (the caller's visibility) |
