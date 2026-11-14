# Total War: ROME REMASTERED (RenoDX HDR, ATN)

Build: `cmake --build --preset clang-x64-release --target renodx-twromeremastered.addon64`.

## v11 (2026-11-14, phase I part 4; built only, not run in game; installed: INSTALLED_PLACEHOLDER)

- Graphics (Experimental, Advanced, off): **Frame Generation** (Off, 2x-6x, Dynamic: NVIDIA DLSS Frame Generation through
  the TW helper; needs DLAA, a windowed or borderless game, the game's VSync off). The HUD-less image is the tone mapper's
  output after DLAA (ATN's traces: SMAA only copies it under DLAA, the UI comes after), the camera the main camera's
  unjittered rows. **NVIDIA Reflex** (Off, On, On + Boost) and **Frame Rate Limit** (Reflex's, 0 off).
- **Screen-Space Shadows** with **Length** and **Thickness** (`contact_shadows.hpp`): Bend Studio's screen-space shadows
  (twattila's `sss_setup` / `sss_bend`) over the scene's depth toward the sun (the frame constants' c2), multiplied into
  the game's shadow mask after CSMResolve: soldiers, weapons and small details get contact shadows the cascades miss.
  Any GPU, no helper, no DLAA needed.
- **Ray Tracing Debug View** (Off, Sun Shadows, Ambient Occlusion; shown with a ray row on) and Tools > **Record Ray
  Tracing Profile (30 s)** (`tw_rt_profile.csv` next to the exe).
- Test: `test/tw_romer_rays` case 6 (WARP: a pillar's shadow from a sun at the camera's left lands where the CPU puts it;
  CONTROL: a sun at the right leaves it lit).

Test (ATN): DLAA + Frame Generation 2x then 4x (borderless): frame rate, the HUD not warped, ReShade.log's frame
generation lines; Reflex On; Screen-Space Shadows on in a sunny battle (with the Test row, Bend's alone: away from the
sun); a ray row + its Debug View; Record Ray Tracing Profile.

## v10 (2026-11-14, phase I part 3: ray tracing; built only, not run in game)

- Graphics (Experimental, Advanced, off; restart the game after turning one on): **Ray-Traced Sun Shadows** and
  **Ray-Traced Ambient Occlusion** (`ray_tracing.hpp` on the TW helper's DXR, `twattila/rt_core.hpp`). The scene: the
  game's depth prepass (terrain, buildings, trees), captured every frame; soldiers, grass and far tree cards stay out
  (the game's own shadows keep them). Traced before ASSAO's apply; the traced sun multiplies the game's shadow mask, the
  traced AO ASSAO's result: the game's shadows and AO stay, the rays add theirs. With **Remove the Game's Sun Shadows
  (Test)** on, the shadows are the rays' alone (the same for AO). Needs the full download; DLAA not needed.
- Test: `test/tw_romer_rays` (WARP: the scene's draws, the trace's camera, the sun, the multiply, and the game's own
  forward PBR shader lit with the multiplied mask and AO as with the products).

Test (ATN): a battle with Ray-Traced Sun Shadows (restart): small buildings' and trees' contact shadows sharper, the
game's shadows still there; with the Test row on the rays' alone; then Ray-Traced AO; the frame rate with each.

## v9 (2026-11-14, phase I part 2: from ATN's first traces; built only, not run in game)

- Graphics (Experimental, Advanced) > **Anti-Aliasing**: Game's (default) / **DLAA** (`dlaa.hpp`). NVIDIA's DLAA through
  the TW helper (the add-on now needs the full download for it; a red note says so when the helper is missing). The game
  has no TAA, jitter or motion vectors: a Halton jitter goes into the main camera (VS b5) at its upload, the camera's
  motion comes from the depth (Attila's `aa_dlaa_prepare` / `aa_dlaa_finish`), DLAA runs after ResolveMainColor and
  the game's SMAA finds no edges under it. 100% Resolution Scale only (below it the log says why it is off); the game's
  FXAA still runs on top; MSAA: off. No object motion yet: moving units ghost more than the camera's motion.
- Log: `[Rome Remastered] DLAA: on, WxH.` once, or `DLAA: off: ...` with the reason; a traced frame marks `DLAA ...`.
- Test: `test/tw_romer_dlaa` (WARP: the trace's cameras, the motion against a CPU reprojection, the colour round trip).

Test (ATN): Anti-Aliasing DLAA, the game's Anti-Aliasing SMAA or off, Resolution Scale 100%: a still battle and the
campaign map steady (no shake), edges smooth; the log's DLAA line; at 67% the log's "off" line and no shake; DLAA back
to Game's: SMAA's edges back.

## v8 (2026-11-14, phase I part 1: trace-free items; built only, not run in game)

- Stability: `twattila/device_loss.hpp` (a removed device's chain let go before the new one, stall lines) and
  `twattila/output_encoding.hpp` (Output decided for every new swap chain; Auto as before: scRGB with Windows HDR on, else
  SDR).
- Graphics (Experimental, Advanced, off; Reset turns them off): **Remove the Game's Sun Shadows (Test)** (the cascades'
  screen-space shadow mask written to 1 after CSMResolve) and **Remove the Game's Ambient Occlusion (Test)** (ASSAO's
  output written to 1, the trees' AO left out). They mark where the ray-traced shadows and AO will go.
- Tools: **Trace One Frame** (`tw_frame_trace.txt` next to the exe about a second later).
- No helper yet: DLSS and the rays wait for the first traces. The 2026-11-14 data run reached no frame: the exe is set
  to "Run this program as an administrator" (Compatibility tab), so every start asked for UAC. A trace needs ATN: that
  box cleared, or his own start from the menu with the trace watcher (tools\tw\romeremastered\plan_1114.md section 7).

Test (ATN, a battle): both Test rows off looks like v7; Sun Shadows on: only the sun's shadows gone (ambient and AO
stay); AO on: the soft contact darkening gone, shadows stay; ReShade.log has one "Test: ... written to 1" line each.

## v7 (2026-11-14, trace build for the data run; built only, not run in game)

- Frame Trace (`twattila/frame_trace.hpp`; ReShade.ini `[renodx] FrameTrace=1`, read at start): one frame 600 3D frames in
  (the post passes count the 3D view, also at 100%), or the next frame when `renodx_trace_now` sits next to the exe;
  `tw_frame_trace.txt` next to the exe. Constants once each (VS / PS b0-b8: the camera at b5, the frame at b8) of the tone
  mappers, the shadow mask resolve PS, ASSAO's apply, TreeAOInject, the sky and the exe's upscaler; particle / fire / heat
  haze blends; the unit VS's inputs.
- `[renodx] FrameTraceReads=1` (`trace_reads.hpp`): each traced draw's VS b5 and PS b8 buffers (identity, usage, rows once a
  buffer), a unit draw's instance and bone rows, in the traced frame and the frame after.
- Log: `[Rome Remastered] Post pass ...` (viewport, target and input sizes) at the first 3D frame and in each traced frame.
- Nothing else changes: HDR, presets and rows as v6.

## v6 (2026-11-12, PsychoV35 round; built only, not run in game)

- **HDR (PsychoV35)** (Rendering Mode 4): Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`). Hue Clip Emulation (default 80) before the LUT, toward the stock Hable on the luminance and its per-channel clip. Recommended in PsychoV35; Reset stays HDR (Extended).
- Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain.

## v5 (2026-10-22)

- Graphics (Experimental), Ambient Occlusion and their preset buttons: Advanced only (the round-4 check never hid
  them).
- Ambient occlusion Radius default 1 (ATN; Thickness stays 0.15).
- Video AutoHDR and Video Dithering at the end of Extra (next to the post-processing settings).
- No live counters (no CPU work per frame for them).
- Output: SDR (gamma 2.2, 10-bit) and Auto (scRGB with Windows HDR on, else SDR); default Auto.

## v4 (2026-10-22)

- PsychoV31 Highlight Hue Shift default 0.06 (ATN; Reset and the presets too).
- Bloom (as Attila): the high pass's hard cap of the bloom source (the brightest channel at cb0[0].z) is a logarithmic shoulder in HDR
  (bloom.hlsli; Bloom.hlsl PS_ApplyThresholdAndBloomDownsample, bloom01_0x8199AACD): bright sources no longer all get the same flat halo. SDR unchanged.
- (Extended's per-channel shoulder: v3.)

## v3 (2026-10-22)

- General: yellow note "Use these in-game settings: Gamma 50% and Brightness 100%."
- Presets: "Recommended (PsychoV30)" is now Recommended: Reset, then PsychoV31 at its defaults and Video AutoHDR 0.20;
  Reset turns Video AutoHDR off.
- Extra: Video AutoHDR / Video Dithering (cutscene00).
- HDR (Extended): the shoulder above 1 per channel (as Ryse fix32): bright fire turns yellow / white at its core as in
  SDR (the brightest-channel roll-off kept its full orange: flat, salmon).
- About: NexusMods button next to ATN's Ko-fi; every section title green, Graphics (Experimental) pink
  (settings.hpp default_section_tint / section_tints).

## v2 (2026-10-20)

- PsychoV31: Highlight Hue Shift 0 by default (ATN); a note above Highlight Saturation / Hue Shift / Saturation
  Shift ("Fire too pink, too orange or too pale? Change the settings below.", as Tales of Arise), Highlight
  Saturation moved next to them.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (Extended) / HDR (PsychoV30) / HDR (PsychoV31, new): the same PsychoV31 sliders as the
  other Total War mods (Musa's custom test31; Highlight Hue / Saturation Shift toward the SDR image's per-channel clip),
  on the same scene-linear handoff as PsychoV30.
- `resolve.hlsli`: ResolveMainColor (Feral's resolve: auto exposure + Hable, LUT, brightness, vignette, tint, dither),
  every permutation through one function with RMC_TONEMAP / RMC_LUT / RMC_VIGNETTE / RMC_TINT. t00 (the mod's original)
  plus t01-t11: FeralShaders holds 12 distinct ResolveMainColor shaders (names in
  `bin\cmd_Decompiler-1.3.16\dump_shader_twromeremastered_pack\names.tsv`, tools\tw\tw_feral_names.py). Tone mapping
  off (6 of them): the stored scene is SDR, shown at Paper White in every mode (the Vignette slider still works).
- fxaa01 (fxaa00's twin with luma computed in the shader): stock, registered for the 16-bit target.
- distortion01 (the heat haze's offsets-only permutation): Distortion slider.
- About: credits, ATN's Ko-fi.

Verify: SDR / Extended / V30 / V31 in a battle and on the campaign map; with vignette / LUT options changed; the game's
HDR / tone mapping option off (SDR at Paper White); FXAA on.
