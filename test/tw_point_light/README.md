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

Why mad(), the written-out acos and the precise result: without them fxc (10.0.26100) regrouped skinlm's
`wrap_back x (wrap.y x c)` into `(wrap_back x wrap.y) x c` (up to 3141 ulp on 2.8 % of values), and dxc kept its own
`Acos` and fast `fmul` / `fadd` (up to 8e8 ulp on the slm lobe). With them both are bit-exact. The precise reaches the
caller's input lines (the probe's position is written with mad() for that reason).

## Gates and controls

- W1 fxc: the pixel probe at the variant's target, every pixel the variant's model, 4 seeds x HDR on / off (98,304
  rgba values a variant; 42 % of the slm and 56 % of the skinlm pixels lit) against the game's DXBC: bit-exact, else
  <= 2 ulp with the instruction named. Shadow variants on the hard path (`g_shadow_soft_edges` 0, the lit map: V
  exactly 1).
- W1 dxc: the compute probe on the same inputs (73,728 rgb values a variant), the same bar.
- model: per slm / skinlm pair, a random stencil & 3 per pixel: 1 = the slm DXBC's value, 3 = the skinlm one's, 0 / 2
  = rgb 0 (20 pairs, bit-exact).
- table: the variant's DXBC binds `s_ssao` exactly when the table says slm. static: fxc ps_4_0 / ps_4_1 / ps_5_0 /
  cs_5_0 `/O3`, dxc cs_6_5 / cs_6_9 `-O3`, no warnings.
- Controls (each must change lit pixels): the falloff kind swapped (40), the model swapped (40), the albedo squared on
  skinlm (Revision 1 S6, 20).
- Information: the gather 4.1 soft path (PCF) over a lit map is <= 3 ulp off V = 1 (the game's own weight sum x 1/15,
  `0x3d888889`, is not exactly 1); the 5.0 soft path is exactly 1.

## Results (2026-11-25: ALL OK, 146 gates pass, 100 controls fail as they must)

The variant table for 4b / 4c (hash -> falloff, model, lowdef; the target and "shadow" do not change the light):

| # | hash | target | variant | W1 fxc (WARP) | W1 dxc cs_6_5 (WARP, rgb) | soft PCF (info) | controls: values apart |
|---|---|---|---|---|---|---|---|
| 00 | 0x6426B042 | ps_4_0 | smooth slm highdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 01 | 0x90981DA1 | ps_4_0 | smooth skinlm highdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 02 | 0x89FF812E | ps_4_0 | smooth slm lowdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 03 | 0xFD3BF1A8 | ps_4_0 | smooth skinlm lowdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 04 | 0x593989DE | ps_4_0 | linear slm highdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 05 | 0x0D7A1213 | ps_4_0 | linear skinlm highdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 06 | 0xB8F368CF | ps_4_0 | linear slm lowdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 07 | 0xC9E4E52D | ps_4_0 | linear skinlm lowdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 08 | 0xF59F5A63 | ps_4_1 | linear skinlm highdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 09 | 0x80A99678 | ps_4_1 | linear skinlm lowdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 10 | 0xC15B836C | ps_4_1 | linear slm highdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 11 | 0xC499C463 | ps_4_1 | linear slm lowdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 12 | 0x448E583F | ps_4_1 | linear shadow skinlm highdef | BIT-EXACT | BIT-EXACT | 25 of 24576 apart, <= 2 ulp | kind 5400, model 5412, squared albedo 5185 |
| 13 | 0x3DD3CC25 | ps_4_1 | linear shadow skinlm lowdef | BIT-EXACT | BIT-EXACT | 25 of 24576 apart, <= 2 ulp | kind 5400, model 5412, squared albedo 5185 |
| 14 | 0x2F02F050 | ps_4_1 | linear shadow slm highdef | BIT-EXACT | BIT-EXACT | 14 of 24576 apart, <= 3 ulp | kind 4227, model 5412 |
| 15 | 0xEA5A7C4F | ps_4_1 | linear shadow slm lowdef | BIT-EXACT | BIT-EXACT | 14 of 24576 apart, <= 3 ulp | kind 4227, model 5412 |
| 16 | 0xE521F992 | ps_4_1 | smooth skinlm highdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 17 | 0x27D78FB0 | ps_4_1 | smooth skinlm lowdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 18 | 0xD0F7E8B9 | ps_4_1 | smooth slm highdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 19 | 0xA932AFD8 | ps_4_1 | smooth slm lowdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 20 | 0x42375E7D | ps_4_1 | smooth shadow skinlm highdef | BIT-EXACT | BIT-EXACT | 24 of 24576 apart, <= 2 ulp | kind 5400, model 5412, squared albedo 5185 |
| 21 | 0x0CF2D4D9 | ps_4_1 | smooth shadow skinlm lowdef | BIT-EXACT | BIT-EXACT | 24 of 24576 apart, <= 2 ulp | kind 5400, model 5412, squared albedo 5185 |
| 22 | 0x181545DF | ps_4_1 | smooth shadow slm highdef | BIT-EXACT | BIT-EXACT | 14 of 24576 apart, <= 2 ulp | kind 4227, model 5412 |
| 23 | 0xA7D5B0B1 | ps_4_1 | smooth shadow slm lowdef | BIT-EXACT | BIT-EXACT | 14 of 24576 apart, <= 2 ulp | kind 4227, model 5412 |
| 24 | 0x43CA2116 | ps_5_0 | linear skinlm highdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 25 | 0xBB678513 | ps_5_0 | linear skinlm lowdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 26 | 0x463D51CA | ps_5_0 | linear slm highdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 27 | 0x194C988B | ps_5_0 | linear slm lowdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 28 | 0x507F23D6 | ps_5_0 | linear shadow skinlm highdef | BIT-EXACT | BIT-EXACT | BIT-EXACT | kind 5400, model 5412, squared albedo 5185 |
| 29 | 0x819D9051 | ps_5_0 | linear shadow skinlm lowdef | BIT-EXACT | BIT-EXACT | BIT-EXACT | kind 5400, model 5412, squared albedo 5185 |
| 30 | 0xD18997A5 | ps_5_0 | linear shadow slm highdef | BIT-EXACT | BIT-EXACT | BIT-EXACT | kind 4227, model 5412 |
| 31 | 0xB5C224DF | ps_5_0 | linear shadow slm lowdef | BIT-EXACT | BIT-EXACT | BIT-EXACT | kind 4227, model 5412 |
| 32 | 0x2CDF370B | ps_5_0 | smooth skinlm highdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 33 | 0xA2EDBD29 | ps_5_0 | smooth skinlm lowdef | BIT-EXACT | BIT-EXACT | - | kind 5400, model 5412, squared albedo 5185 |
| 34 | 0xD17728DD | ps_5_0 | smooth slm highdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 35 | 0x490DFC4B | ps_5_0 | smooth slm lowdef | BIT-EXACT | BIT-EXACT | - | kind 4227, model 5412 |
| 36 | 0xC1FE2079 | ps_5_0 | smooth shadow skinlm highdef | BIT-EXACT | BIT-EXACT | BIT-EXACT | kind 5400, model 5412, squared albedo 5185 |
| 37 | 0x134C5AA8 | ps_5_0 | smooth shadow skinlm lowdef | BIT-EXACT | BIT-EXACT | BIT-EXACT | kind 5400, model 5412, squared albedo 5185 |
| 38 | 0xE48A600D | ps_5_0 | smooth shadow slm highdef | BIT-EXACT | BIT-EXACT | BIT-EXACT | kind 4227, model 5412 |
| 39 | 0x5C3798EA | ps_5_0 | smooth shadow slm lowdef | BIT-EXACT | BIT-EXACT | BIT-EXACT | kind 4227, model 5412 |

Not covered: V other than 1 (the game's own shadow factor cannot be read out of its shader); the 64-bit port's hash
census (a changed hash re-runs W1 on it); the add-on's light surface and the helper's DI passes (4b / 4c).
