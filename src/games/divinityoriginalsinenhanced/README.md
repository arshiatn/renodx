# Divinity: Original Sin Enhanced Edition — RenoDX

HDR for the D3D11 game (`EoCApp.exe`, 64-bit). Build target `divinityoriginalsinenhanced` (amd64): rebuild the add-on
and every shader together, the shader-data layout (`shared.h`) is shared by all of them.

## Rendering modes

| Value | Menu name | What it does |
| --- | --- | --- |
| 0 | SDR (Native) | The game's own curve, grade and fade. |
| 1 | HDR (UC2 Extended / PsychoV30) | The earlier mode: UC2 Extended with Musa's PsychoV30 (`twattila/psycho_test30.hlsli`, one copy with Divinity 2 and Attila). |
| 2 | HDR (UC2 Extended / Pragmap) | The earlier mode with Pragmap's fire / candle boosts. |
| 3 | HDR (Extended) | Default and Reset. The game's curve extended past white, then the game's grade once after it (`GradeAfterCurve`: a colour past white graded at its brightest channel's 1 and scaled back; Smooth Grade Shoulder (Experimental, off): a smooth shoulder from 0.8 instead, up to 0.033 OkLab near white), Hue Clip Emulation 70. |
| 4 | HDR (PsychoV35) | Recommended. Musa's PsychoV35 on the game's mid gray, then the grade. |

## Presets

All four keep Peak, Paper White, UI, Settings Mode and Output (SDR also sets Output SDR).

- **Recommended**: HDR (PsychoV35) with less bloom.
- **Reset**: every image row at its default, HDR (Extended).
- **SDR (monitors without HDR)**: SDR (Native) and Output SDR (restart if it was HDR).
- **PsychoV30 (earlier Recommended)**: HDR (UC2 Extended / PsychoV30) with less bloom and the brazier / candle boosts.

## Rows

- General: Peak, Paper White, UI, Rendering Mode. Page 1 says to keep the game's Gamma Correction at 1: no shader of
  the game reads it (its value goes to a renderer method as 1 / value; the tone map encodes a fixed 1 / 2.2).
- Extended: Hue Clip Emulation. PsychoV30 / Pragmap rows only in their modes (Advanced).
- Extra: Bloom, VFX Brightness (Experimental, 1 the game's, HDR; restart after first moving it from 1: the effects'
  colour x the row, `vfx_brightness.hpp`: 258 unlit effect shaders patched as the game creates them, the slider minus 1
  at cb7 on their draws, the add-on's fire and particle shaders by shared.h; the effects that read the scene's colour
  (refraction, heat haze, shields, water: 21) and those lit by the sun's shadows and SSAO (effect surfaces: 49) are
  left out), Vignette, Godrays. Game-Specific: UI, Player Light Intensity (lights with shadows: point and spot
  shadow passes), Local Light Intensity (lights without), Sun Shadow Softness (Experimental: the PCSS light size x
  the row, Ultra shadows), Smooth Grade Shoulder (Experimental, off; Extended and PsychoV35), Game's Screen-Space
  Reflections (Experimental, off, restart: writes the settings object's SSR byte at swap chain creation, see Engine
  facts; only in the exe those addresses were read from).
- Output (restart): Auto (default: scRGB with Windows HDR on, else SDR), HDR10, scRGB, SDR. Decided again at every
  new swap chain (`twattila/output_encoding.hpp`).
- Tools (Advanced): Trace One Frame (`tw_frame_trace.txt`), Time Each Pass (One Frame) (`divinity_pass_timing.txt`).

The game makes a new swap chain 2-4 times a start and at each window-mode change: `twattila/device_loss.hpp` lets the
last flip chain go first (`release_flip_chains_before_recreate`) and handles a removed device. LightVolume's SMAA
passes and the scene's float targets are upgraded with view cloning and the hot swap.

## Graphics (Settings Mode)

Needs renodx-tw-dlaa64.exe and its DLLs next to the add-on (the full download); without them the page says so.

- Anti-Aliasing: the game's FXAA / SMAA, or the helper's DLSS (as DLAA), FSR or XeSS at 100% (`upscaling.hpp`). The
  game has no jitter and no motion vectors: the main view's PerView (b12, a perspective at the display's size) is
  jittered as the game uploads it (Halton 2 / 3 over 8 frames), the depth comes from gBuffer1's linear depth (kept at
  the sun pass 0xCCCB7253) and the camera's motion from that depth and both frames' cameras (`d1_upscale_inputs`). At
  the tone mapper's draw its Base goes through the upscaler and the result is written back into it
  (`d1_upscale_output`): fog of war, outlines and the UI stay the game's. DLSS's presets L / M and Ray Reconstruction
  get the colour's square root. Frame generation takes the back buffer before CombineUI as its HUD-less image; Reflex
  for the game's presents. The in-game anti-aliasing must be off.
- Ray tracing (Experimental, restart after turning the first effect on; `ray_tracing.hpp`): a host of the Total War
  helper's rt_core. Every G-buffer draw goes into the scene (skinned characters every frame, alpha-tested materials
  with their cut-out); the frame is traced at DownsampleDepth (0xEB741AE5: the G-buffer done, before SSAO and the
  shadow casters; else at the shadow filter or the sun pass). Ray-Traced Shadows replace the sun's shadow filter
  (CascadedPCSS / Unfiltered / VSM are left out; `d1_rt_shadow` writes the mask the sun pass and the forward materials
  read); Skip the Game's Shadow Maps (Experimental, off) leaves out every draw into the sun's shadow atlas while the
  frame was traced before them (R1 T1: 705 draws, 0.331 ms of GPU; the atlas's only reader in R1's traces is the filter
  the rays replace). Ray-Traced AO is drawn over the SSAO
  result (`d1_rt_ambient`), so the sun pass and the forward materials read the rays' AO alone (the game's SSAO must
  stay on); Skip the Game's SSAO (Experimental, off; after the page's last section until the page has a place for it)
  leaves DeferredSSAO and its two blurs out while the frame was traced before them (R1 T1: 0.086 ms of GPU; their result
  has no reader before the sun pass, which draws the rays' AO over all of it). Ray Reconstruction gets the G-buffer guides (`d1_rt_guides`). The RT/PT preset is Attila's.
  Global Illumination (Strength, off by default; the rays' One Bounce or the paths' ReSTIR GI, Experimental): before
  DeferredFog (0xEDA7F8A1) the scene, lit by the sun and every local light, is prepared, traced, resolved over frames
  (Expeditions' `rt_bounce_prepare` / `rt_bounce_resolve`) and added to it times each pixel's diffuse colour
  (`d1_rt_bounce_apply`, gBuffer3 as the sun pass's ambient takes it). Path-Traced Ambient (Experimental, off, left out
  of RT/PT): the sun pass's sky and ground ambient (PerFrame b13) become the paths' sky (`d1_rt_sky`, the bridge's sun
  texels 3-8); once the paths have learned the game's light the sun pass draws with that ambient zeroed (a PerFrame copy
  at b13, the game's bound again after) and SSAO is skipped, its result open (1): the paths bring the sky's light and its
  shade. The forward surfaces (blood, ooze, fire, electrified) keep the game's ambient. The paths' sky is the helper's
  six-colour cube: exact facing the six axes, up to |sky - ground| / 8 off between them (R1: 0.094 of 0.81 in blue at
  30 degrees above the horizon).
  Not yet: reflections.

## Tests (offline, WARP)

- `test/tw_divinity1/run.ps1`: the tone map 0x30A1D40D against the game's bytes: SDR (Native) exact, Extended and
  PsychoV35 on the game's mid gray (control: mode 1's curve on a curve), Smooth Grade Shoulder on (slope, the stock
  below 0.8) with a hard-knee control, the row off bit-equal to the tone map before it (a6762afab^; control: the row on).
  Part 7, the V35 fit: PsychoV35's Contrast / Shadows / Saturation / Highlight Contrast minimax-fitted to the game's
  SDR below white (grey 0.05 - 1 and 1 - 15 nits, OkLab chroma at equal lightness 1 - 30 nits) with R1's traced save
  constants (level A), PsychoV35's highlights kept within 15 % at 1000 / 203 (20 % at 2500 / 275); shared.h DIVINITY_V35_* must be the fit's best (Reset
  and Recommended start from them); controls: Musa's own sliders must miss by over 5 %, Extended must be the game's
  grey. The defines are Musa's own (1) until the fit has run; built only, not run in game.
- `test/tw_d1_rays/run.ps1`: the helper's inputs and the rays' replacements, the game's sun pass reading them: the motion
  and the rays' positions within 0.002 px, the colour's round trip, the guides against its own normal decode, the shadow
  mask and the AO over the SSAO result giving its light, Path-Traced Ambient's sky texels against the pass's ambient
  facing each axis (1.2e-7), the pass from PerFrame without the ambient bit-equal to it from R1's zeroed, the bounce's
  apply against the pass's ambient (2.4e-7); each with a control that must fail.
- `test/tw_d1_post/run.ps1`: 22 replaced passes Vanilla exact against the game's census bytes, each with a wrong copy
  that must fail; the light rows at 2 give twice the game's light. The PCSS sun shadows (0x8F114E47) also with R1 save
  T1's traced PerShadow / PerView (`pcss_r1_cb0.txt`, `pcss_r1_cb12.txt`; the cascades' projection, not in the trace,
  orthographic from their widths and far planes).
- `test/tw_d1_vfx/run.ps1`: VFX Brightness's patch on each of the 258 effect shaders' census bytes: the program the
  game's plus the brightness (D3DDisassemble), and on WARP the game's bits with cb7 unbound and at 1, rgb x 2 at 2;
  controls: a pixel drawn, the colour changed at 2.
- `test/tw_d1_swapchain`, `test/tw_device_loss`, `test/tw_output_encoding`: the window's chains, device loss, Output.
- `test/tw_psychov30_nan/run.ps1`: PsychoV30's NaN / infinity guards live against a copy without them.

The census (`tools/nontw/divinity1/census`, the game's shaders from its .pak files and exe) stays outside the repo.

## Engine facts (static reads of EoCApp.exe)

The Steam exe, 21,605,888 bytes, SHA-256 f7c759e7...33d88, ASLR on (addresses at its image base 0x140000000).

- Screen-space reflections: `SSREnabled` in graphicSettings.lsx does nothing. The key is built as a FixedString
  (0x1407989C2), but the lsx loader (0x1407E2D40...: MotionBlurEnabled into the settings object's byte 0x4C,
  SSAOEnabled 0x4D, ShadowsEnabled 0x4F) and the saver (0x1407E2141...: the same keys) skip it, so the lsx the game
  writes at exit should lose the line (a run keeping that lsx confirms it). The settings object (pointer at 0x1412E3E30, built at 0x1407E34C0 / 0x14085E96E: dword
  0x01000101 at 0x4C) keeps byte 0x4E, SSR, at 0. The renderer reads that byte at its start (0x1408225F8) and in its
  settings apply (0x140820990, vtable slot 0x141064570). On a change it creates SSR (0x140820040) or releases it
  (0x14081FEB0). So SSR can only be turned on by writing that byte in the game's memory (the Game's Screen-Space
  Reflections row).

## Frame facts (R1's traces: the menu and the Act 2 / 3 save)

- Upgraded RGBA8 targets: the tone map's and EoCFog's outputs, SMAA's edges and weights (each read only by a replaced,
  Vanilla-exact pass) and the back buffer. In the save the half-size SSAO targets were RGBA16F (in the menu RGBA8):
  DeferredSSAO (0x385DCF3C) writes unsaturated, its blurs (0x0524EF64, 0xB887004F) saturate, so the sun pass and the
  forward materials see the game's range at 16-bit precision. The G-buffer, the shadow mask and the UI layer stay RGBA8.
- Clamps: the 9 census candidates did not draw in R0 or R1. The 7 material emissive clamps are authored in the
  material graphs, bloom 0x921AE8C3's saturate is in its source, DOF 0x95B9D5E8 saturates only its CoC: the
  developers' look, left as it is.
- Tone map (0x30A1D40D): the ColorMatrix is a saturation matrix with weights 0.3086 / 0.6094 / 0.0820 (save 1.1, menu
  1.14); Params A / B / D and PPHDRExposure's (min, max) are atmospheres.tsv's: the save's ThePrisonGardenOfStoneKing
  Sunny (A 0.22, B 0.18, D 0.2, exposure 1 / 1.5), the menu's Menuscreen_Cyseal (A 0.48, B 0.14, D 0.62, 0.85 / 1.5).
  Source / Target colour were white in both: a scene with coloured ones (the beach, Black Cove) is not traced yet.
- Fire 0xED93F883 (alpha blend): (texture x 0.5 + 0.5) x vertex colour, fogged; candles 0x93AEBB93 (additive): texture x
  vertex colour x Brightness x fades. Their and the lights' constants are not in R1's traces (the frame trace has them
  since this build).

## Credits

RenoDX, PsychoV30, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom PsychoV30 and
PsychoV35. UC2 Extended by Musa and DabVeed. Divinity: Original Sin Enhanced Edition by ATN.
