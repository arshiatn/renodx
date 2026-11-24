# Total War: ROME REMASTERED (RenoDX HDR, ATN)

Build: `cmake --build --preset clang-x64-release --target renodx-twromeremastered.addon64`.

### Open items G04 (2026-11-22; built only, not run in game)

- New: **Texture LOD Bias** at the game's own Resolution Scale: while DLSS / FSR / XeSS draw in Feral's upscaler's
  place below 100%, textures as sharp as at the display's resolution (Matched) or one / two steps more (the page's row).
- Changed: Modded Auto Exposure **Sensitivity** default 1 (was 2; ATN).
- Changed: the **Fog Shadows** row hidden (this game has no fog the rays shade).
- Changed: **PsychoV35's defaults follow the game's look**: Exposure 1.10, Shadows 1.07, Highlight Contrast 0.92 (the
  other V35 rows Musa's; minimax-fitted on WARP against Rendering Mode SDR, Exposure the fifth knob, ATN). Below 15
  nits PsychoV35 over the game's SDR: x0.89..x0.92 before, within 1.0 % now, colour the same; highlights within 7.6 % of
  Musa's own. Recommended and Reset apply it; rows you saved stay yours. Level B: Hable's white 11.2 assumed (it
  follows the auto exposure), not traced.
- Fixed: **FXAA** (fxaa00 / fxaa01) as the game's: their gather read red, the game's alpha / green, and the edge blend's
  last step lost its offset (WARP: 0 texels apart from the game's shaders, 637 before).

### Next round (2026-11-16 tw3_; built only, not run in game)

- New: **Modded Auto Exposure (Experimental, off)** and its Sensitivity (Extra, Advanced, HDR modes), as Attila's and
  Pharaoh's rows on Feral's own auto exposure (shaders/autoexposure00_0x6A1D2178, the one-thread pass after its 64-bin
  histogram of the scene's log luminance): each bin's value in the metered average at most the average x 2^Sensitivity,
  so a large bright sky or fire darkens the picture less. The bright level the tone mapper's white point follows stays
  the game's. Off and in SDR: the game's output to the bit.
- Proven offline: test/tw_romer_auto_exposure (WARP, the game's 0x6A1D2178 against the add-on's: off in every mode and
  SDR on 0 bits apart in 1200 runs over 200 histograms; the walk read from the assembly within 4e-6; on, the capped
  mean within 4e-6; each with a CONTROL). A battle-like histogram (ground, sky, a fire tail): on lifts the exposure
  +0.01 stops at Sensitivity 2, +0.19 at 1, +0.29 at 0.6.
- PsychoV35 (Recommended) against Rendering Mode SDR below white: x0.89 - 0.92 below 15 nits, colour the same
  (test/tw_shogun2_romer_toe, Hable white 11.2).
- In game (HDR, Advanced): a battle under a bright sky, Modded Auto Exposure off / on at Sensitivity 1: on is a little
  brighter in the ground; the sun, fire and sky keep their brightness.

### Fix round (2026-11-16; built only, not run in game)

- New: **FSR and XeSS (Experimental)** through the helper in DLSS's place (Anti-Aliasing: FSR, XeSS); frame generation
  backends as Attila's.
- New: **Time Each Pass (One Frame)** under Tools, and the test hooks (RENODX_TW_SHOT / PROFILE / SET) as every other
  TW add-on.
- Fixed: a saved DLSS is XeSS on Intel's GPUs, else FSR, off NVIDIA (the vendor read first, then the rows; Reflex shows
  on NVIDIA only).
- Fixed: one sun-shadow method: Bend's contact shadows are off where the rays shadowed this frame.
- Fixed: frame trace marks are formatted only while a frame is traced (no per-frame strings).
- Credits for every method the add-on ships (Light Bounce's Visibility Bitmask, SMAA, CMAA2, FidelityFX SSSR, Detours).
- Not done: the rays still multiply the game's cascades (units, grass and banners are not in the rays yet: one battle
  trace with `[renodx] FrameTraceReads=1` first); Modded Auto Exposure (Feral's auto exposure compute) and the texture
  LOD bias wait for their traces.

### Ray-Traced AO in place of the game's (2026-11-16; built only, not run in game)

- **Ray-Traced AO replaces ASSAO and the trees' own AO** (ATN: never on top of them): after ASSAO's apply its target is
  cleared to open, then the traced visibility multiplied in (the rays' AO alone), and the trees' AO (TreeAOInject) is left
  out while the rays replace it this frame, as the Test row does. All 372 pixel shaders in the dump that read t13 read its
  red only.
- Proven offline: test/tw_romer_rays part 4: the cleared R8 then the multiply is the visibility to the bit, 2304 of 2304
  texels (CONTROL multiplied over the game's 204: 2268 differ). Whole suite PASS.
- In game: Ray-Traced AO in a battle: corners and tree feet lighter than before (no ASSAO under the rays' AO).

### Parity round 2 (2026-11-14 evening; built only, not run in game)

- Shadow casters checked against ATN's 91 traces and the whole shader dump (85 vertex shaders the census could not
  place): none is missing. 29 are held: 10 by the rays' capture of the depth prepass, 8 as the forward pass's copies of
  those (the same meshes: equal index counts in every trace), 11 never traced but in a form the capture takes. 33 wait
  for one battle trace with `[renodx] FrameTraceReads=1`: the 24 unit, 8 grass and 1 banner shaders. Far trees' cards
  keep the game's own shadow (they face the camera). The other 21 cast nothing: UI, 3D markers, campaign overlays, the
  banners' glow, fire layers, shore foam, the far-card generator. Found: the game's own shadows have no grass and no
  banners, so in the rays both would be new shadows. No code change.
- Attila's Two Shadow Cascades Under Rays and Skip the Game's Shadow Map Work: not for this game. The game's own Shadow
  Distance option sets 1 to 4 cascades, and here the rays add to the game's shadows instead of replacing them: units
  and far trees still need every cascade. Rarer Objects in the Rays: Battle Objects already lets every untested object
  in, and the game has no tessellated objects or ropes. No code change.
- Evidence: `tools\tw\romeremastered\p2rr_*.txt` and the ledger `tools\tw\parity\romeremastered.tsv` in the maintainer's
  tools.

Test (ATN): nothing new. The battle trace above (Not done in the Parity round) settles the 33 lines.

### Fable fixes and parity DO NOW (2026-11-14; built only, not run in game)

- Fixed: the battle's terrain (prepass 7DD754DA) is flagged terrain in the rays. Its triangles longer than the helper's
  200 m part limit were dropped. `test/tw_romer_rays` case 1.
- Fixed: one mesh drawn again with another object matrix (VS b7) is captured each time. The later BasicLit draws of a
  frame were skipped (rt_core's new opt-in `SceneShader::every_draw`). Case 8.
- Fixed: the rays' positions 1-5 km out are within a millimetre of the depth (were 0.36 m off, captures 3.9 m). The
  pixels are unprojected in doubles (rt_positions_double; a GPU without doubles keeps the old path), the captures as
  offsets from the eye (rt_core's new opt-in `Host::clip_relative`). Case 9.
- New: Upscaling Debug View, Motion Vectors (the motion DLSS gets). Shared: AppendAa's opt-in `motion_view`.
  `test/tw_romer_dlaa` case 4.
- New: Bend's Softer Away from the Caster (Experimental, off) and Debug View (Shadows, + Screen Edge) are drawn and their
  rows shown. Case 6.

Test (ATN):
1. Battle, RT/PT: hills' shadows and AO reach the far terrain, with no holes in long ground triangles.
2. Battle, RT/PT, Ray Tracing Debug View Sun Shadows: buildings and props repeated across the map (walls, houses) all
   cast shadows. Far shadows sit on their casters, with no shimmer at 1-5 km.
3. DLSS, Upscaling Debug View Motion Vectors: colours while panning, black when still.
4. Bend on, Softer Away from the Caster 1-2: shadows sharp at the feet, softer far from the caster. Debug View
   Shadows / + Screen Edge at 100% and at 67%.

### Reset hunt (2026-11-14; built only, not run in game)

The battle's two helper resets (ATN's 10:24 run; nvlddmkm 153 at 10:27:39 - 10:27:47, 0x887A0006 on the helper's device
only) are **not reproduced offline and their cause is not proven**: Battle Objects stays at Tested. No DRED exists for
the run (Log Details was off). The harness (4K helper, the run's own draw lists from traces 12 and 15 at 2572x1446,
Windows' D3D12 / DXR 1.1 as ATN's install without `D3D12\D3D12Core.dll`) ran clean, 0 resets, with: partly NaN
triangles, whole meshes at the origin, 144 piled alpha-tested layers, all cluster sets at once, a 12.5 s pause (the
helper's idle release) then everything at once. Ranked suspects:
1. The driver on real battle content on Windows' own D3D12 (DXR 1.1): only Attila and WH3 ship the Agility SDK, and no
   other game on this path has run a battle with every prepass draw captured each frame.
2. Wrong geometry for a whole family: stretched triangles under the helper's 200 m part limit make a trace slow (proven:
   0.75 ms -> 118 ms with 2% of the vertices wrong at 2572x1446), but a 2 s hang needs far more, and no source was found.
3. A cluster set kept for an index buffer the game writes again under the same key.
- New: with Battle Objects at Solid or All, the helper's device runs with DRED and the log's details as Log Details
  gives them (set the row, then restart the game), and if its device starts again within 5 s of a trace the untested
  objects go out until the game starts again (one log line; ATN's second reset came 0.2 s after the scene was back).
  `test/tw_romer_rays` case 1.
- Evidence, lists and the harness's battle mode (a patch script, the shared harness unchanged):
  `tools\tw\romeremastered\reset_hunt_1114` in the maintainer's tools.

Test (ATN): Battle Objects Solid, restart, one battle with sun shadows + AO: a reset or not; then All the same way.
After a reset send `renodx-tw-dlaa64.log` (DRED's last GPU work, the left-out triangles by vertex shader) and
ReShade.log. If All resets: copy Attila's `D3D12` folder next to this game's `renodx-tw-dlaa64.exe` (the log then says
DXR 1.2) and run All once more: suspect 1 against 2.

### Parity round (2026-11-14; built only, not run in game)

- New: **Attila's graphics page** (ATN: "the same/similar page layouts as Attila and thus the buttons too"): Anti-Aliasing
  and Upscaling, Shadows, Ambient Occlusion and Ray Tracing Extra Options in Advanced, with **Reset**, **No RT/PT
  (Recommended)** and **RT/PT** (Attila's preset: ray-traced sun shadows + RTAO + DLSS; this game has no GI or
  path-traced AO, so those stay out). Graphics (Experimental) is gone: its rows moved there with the same keys (Battle
  Objects and the two Remove tests under Ray Tracing Extra Options). A saved DLAA comes back as DLSS (SettingsRevision 1).
  New with it: DLSS's preset (default L, its dark tones kept as Attila's), frame generation's preset, HUD recomposition
  and debug view, the ray tracing rows Attila shows (Softness, AO range, debug views). Shared: three opt-in page fields
  (`has_gtvbao`, `has_gi`, `every_row`); every other game's page dump unchanged.
- New: Bend's **More Options** as Attila's: Softness (1-3 px, along surfaces), Contrast, Hard Shadow Samples, Fade-Out
  Samples, Edge Threshold, Ignore Edge Pixels, Precision Offset, Sample Along the Ray, Mirror Screen Edges. Defaults give
  the same shadows as before (`test/tw_romer_rays` case 6).
- About: Bend Studio's and the helper's credits, a GitHub (Downloads) button.
- New: **DLSS below 100% with ray tracing on**: v13 kept DLAA at the render size with a ray row on (the rays captured
  only a depth at the bridge's size), Feral's upscaler after. Now DLSS takes the upscaler's place with the rays too:
  they capture and trace the render-size depth (rt_core's new opt-in `Host::depth_at_render_size`; every other game
  keeps today's rule), their results in the bridge's top-left corner. Below 100% the Ray Tracing Debug View is drawn
  before DLSS (it upscales it as the picture). Log: `DLAA: on, DLSS 2572x1446 to 3840x2160 (Resolution Scale below 100%).`
  with a ray row on too.
- Parity table (every Attila feature: have / do now / needs a trace / won't get, with proof):
  `tools\tw\romeremastered\parity_1114.md` in the maintainer's tools.

Test (ATN; the v13 lines first):
1. Control: page Reset, Anti-Aliasing Game's: the picture as v12 (the game's shadows, AO and SMAA).
2. Settings Mode Advanced: the four sections show; a saved DLAA shows as DLSS.
3. Campaign, RT/PT, restart once: ray-traced sun shadows + AO + DLSS on (ReShade.log `DLAA: on`, the helper's log).
4. Battle, No RT/PT (Recommended): Bend's contact shadows under soldiers; Shadows > More Options > Softness Off / 2 px:
   softer edges, no halos (decides the default).
5. Battle, DLSS preset L vs K: dark tones the same, L steadier (decides the default).
6. Page Reset mid-battle: the game's shadows and AO back, no reset in renodx-tw-dlaa64.log.
7. Battle at Resolution Scale 67%, RT/PT (Battle Objects Tested): sharp at 4K, the rays' shadows on the right places
   (Ray Tracing Debug View Sun Shadows lines up with the picture), ReShade.log's DLSS line.

Not done (the Upscaling Debug View, Softer Away from the Caster and Bend's Debug View: done above):
- Texture LOD Bias below 100%, units' object motion, units / banners / grass in the rays, static captures, smoke shadows:
  each needs a fact no trace shows yet. Step: one battle trace with `[renodx] FrameTraceReads=1` at 67%, and a siege
  with fire.
- GT-VBAO, Global Illumination, reflections, the Warscape engine pages: not in this game (proof in the table).

## v13 (2026-11-14, ATN's 67% / 50% runs; built only, not run in game; installed: INSTALLED_PLACEHOLDER)

- Fixed (safe side): ATN's 67% run reset the TW helper's ray tracing device twice within seconds of a battle's start
  (0x887A0006, sun shadows + AO on; then off until a restart), none in 1.5 min of the campaign map before. Not Ray
  Reconstruction: the add-on asks for none (no guides, no RR line in the helper's log) and DLAA kept running after.
  The battle's scene holds draws the campaign's never had (its terrain, props, trees, alpha-tested plants); the cause
  among them is not found (the harness with Rome-like dynamic leaves ran clean). The rays now see only the kinds traced
  without a reset (`ray_tracing.hpp` TESTED_VERTEX_SHADERS: AerialMap, AerialMapTrees, PBR 9594545F, BasicLit); the
  units' banners never (their own matrix at b0, not the main camera). Graphics (Experimental) **Ray Tracing Battle
  Objects (Test)**: Tested (default) / Solid (+ the battle's solid draws) / All (as v11).
- Test: `test/tw_romer_rays` case 1 (the battle's kinds out at Tested, solid ones in at Solid, an alpha-tested one only
  at All, a banner never).
- DLAA below 100% Resolution Scale: v9's text said off, but v9-v12 ran DLAA at the render size (the scene's depth and
  the tone mapper both 2572x1446 at 67%), then Feral's upscaler. Now (plan item 4, ATN's 67% / 50% traces: the tone
  mapper and SMAA at the render size, the exe's upscaler VS 9F3C9B3D / PS 3C19543E into a 3840x2160 FP16 target, the UI
  on it) **DLSS** from the tone mapper's output to the upscaler's size, copied into its target in its place (`dlaa.hpp`
  Upscale; the upscaler's four PS hashes). Frame generation keeps DLSS's result as the HUD-less frame. With a ray row on
  it stays as before (DLAA at the render size, Feral's upscaler after): rt_core captures the main view's depth at the
  bridge's size. Log: `DLAA: on, DLSS 2572x1446 to 3840x2160 (Resolution Scale below 100%).`
- Test: `test/tw_romer_dlaa` case 6 (DLSS's result copied into the upscaler's target, its draw skipped; no result /
  another size / DLAA off: the game's draw).

Test (ATN): a battle with Ray-Traced Sun Shadows + AO at Battle Objects Tested: no reset (renodx-tw-dlaa64.log); then
Solid, then All (one each, a restart after a reset): which one resets. DLAA (rays off) at 67% and 50%: sharp at 4K, no
shake; the log's DLSS line; the helper's "Upscaling: DLSS ... to 3840 x 2160".

## v12 (2026-11-14, crash fix; built only, not run in game; installed: INSTALLED_PLACEHOLDER)

- Fixed: changing Resolution Scale in game crashed (ATN 10:22, crash_1022.dmp: exe + 0x6ee594, an AddRef on a null
  view). The game rebuilds its swap chain in the format its back buffer's view reports, which the HDR upgrade made
  16-bit; for that format the game's own code makes no sRGB view and later uses the missing one. `swap_chain_format.hpp`
  hooks the game's rebuild (exe + 0xFB420, found by its bytes; another build is left alone) and gives it the game's own
  8-bit format back, so the rebuild runs as at the start.
- Test: `test/tw_romer_dlaa` case 5 (the installed exe's bytes found at + 0xFB420, not in another exe; the formats given).

Test (ATN): Resolution Scale 100% to 67% and back, in the menu and in a battle: no crash; ReShade.log has "Swap chain
rebuilt at ...: the game's format 87 for the upgraded 10." once.

## v11 (2026-11-14, phase I part 4; stamped and installed 07:50 with renodx-tw-dlaa64.exe helper112, not run in game)

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
