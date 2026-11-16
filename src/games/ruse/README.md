# RenoDX — R.U.S.E.

R.U.S.E. renders with D3D9 (64-bit). RenoDX keeps the game's packed 10-bit scene targets, draws the game's frame into an
FP16 clone of its 8-bit back buffer and presents it through a DX11 HDR swap chain (the device proxy). Turn the in-game
HDR setting on: that path renders the 10-bit scene with bloom that the final post (`0x232EB5DF` and its permutations)
tone maps. The game has no tone mapper of its own; its 8-bit back buffer clips at 1.

## Rendering modes

| Mode | Behaviour |
|---|---|
| SDR | The game's own picture (its map response, clipped highlights), scaled by Paper White. |
| PsychoV31 - Native Tone | PsychoV31 after the map's gamma / brightness response. |
| PsychoV31 - Full Replacement | PsychoV31 in place of that response (the earlier builds' last V31 mode). |
| PsychoV35 | Recommended and the default for new players. Musa's PsychoV35 (`twattila/psycho_test35.hlsli`) with its own sliders (`v35_*`, `twattila/psycho35_settings.hpp`) and Hue Clip Emulation (default 100), in place of the game's clip at 1, after the map's Brightness / Gamma response (test/tw_psychov35 part 6: slope at grey = the map's Gamma, the 15 pre-inverted markers at their authored colour). |
| Extended | The SDR picture up to Paper White, each channel above it rolled off to Peak (Ryse fix32's Neutwo shoulder). Reset selects it. |

The `rendering_mode` key: 0 SDR, 1 Native Tone, 2 Full Replacement, 3 PsychoV35, 4 Extended. Output: HDR10, scRGB, SDR or
Auto (default: scRGB with Windows HDR on, else SDR; `twattila/output_encoding.hpp`). SDR output keeps the game's picture
(Peak, Paper White and UI at the back buffer's 203-nit white). A first start without Windows HDR takes the SDR preset;
Output Auto switches to Recommended and back as Windows HDR changes.

Presets: Reset (Extended), SDR (Rendering Mode SDR and Output SDR), Recommended (PsychoV35, Motion Blur off, Video
AutoHDR 0.2). They write the look rows only, never Peak, Paper White or UI.

## Settings pages

Settings Mode: Simple, Advanced, Graphics 1 of 2, Graphics 2 of 2 (About on every page).

- **Graphics 1 of 2** (Attila's sections, every row Experimental, off, restart): Ray Tracing (RT/PT and No RT/PT, the
  red full-download line without the helper, Readable Scene Depth and Scene Replay for tests), Anti-Aliasing and
  Upscaling, Shadows, Ambient Occlusion, Global Illumination, Reflections and Water.
- **Graphics 2 of 2**: Shadow Map Size, Sprite Resolution, Sprite Anti-Aliasing and Detail Distance write the game's own
  `Option.ini` ([advanced_video]: ShadowRTSize, ImposteurTextureSize, ImposteurRTAA / ImposteurShadowRTAA,
  LevelBuild_*) above its menu, at each start (the game reads the file before its device and writes it only when its
  options menu applies); Game's gives the game's value back (kept in ReShade.ini [RUSE Option.ini]).
- **Output** (Advanced): Output, Wait for Each Frame Copy (the proxy's two GPU idle waits, on as shipped), Frame Trace
  and Trace One Frame (`renodx_trace_now` next to the exe works too).

## The D3D9 bridge (`bridge.hpp`)

Anti-Aliasing (Experimental): DLAA, FSR Native AA or XeSS Native AA through the TW helper (`renodx-tw-dlaa64.exe`, the
full download) on the DX11 proxy's device. The game's D3D9Ex device makes the shared textures and the proxy opens them.
Each frame, before the bloom's first pass reads the resolved main target: a D3D9 pass writes the scene as light, the
INTZ depth (Readable Scene Depth: the back-buffer-sized single-sample D24 surfaces made INTZ) and the camera's motion
(the camera pass `0x88292FC6`'s matrices); the helper runs; a second pass writes the result back in the game's
encoding. The scene draws get a Halton jitter in their camera registers c0-c3. GPU Sync waits on shared fences instead
of the CPU, at the anti-aliasing and at the trace point's two hand-overs (bridge.hpp HandToProxy / HandToHost). The GPU's vendor comes from the proxy's device: off NVIDIA's GPUs the row lists Game's / FSR / XeSS, and a
saved DLAA runs (and is saved) as XeSS Native AA on Intel's, else FSR Native AA (one log line).

The row sets the game's own MSAA to 1x (`Option.ini` MainRTAA 1: RUSE.exe pairs 1, 2, 4, 8, 8.5, 16, 16.5 with "1x"
... "16x MSAA"; ATN's 8.5 is "8x MSAA"), when it changes and at each start, for the game's next start; Game's gives the
game's value back. A start whose main depth is still multisampled leaves the scene draws alone (no jitter, no rays: the
pre-pass would be the only readable depth): one log line and a red line in the section say to restart.

Frame Generation (2x-6x, Dynamic), NVIDIA Reflex, Frame Rate Limit, HUD Recomposition and the FG preset use
`twattila/frame_generation.hpp` and `reflex.hpp`: the HUD-less image is the final post's target copied before the
first UI draw; Reflex runs on the proxy's presents and the D3D9Ex queue is one frame deep. Reflex shows on NVIDIA's GPUs;
Frame Rate Limit on NVIDIA's or under XeSS frame generation (Xe Low Latency).

## Ray tracing and the screen-space rows (`ray_tracing.hpp`, `bridge_replay.hpp`)

The rays trace the scene a D3D9 draw replay captures on the proxy's D3D11 device (`rt_core.hpp`'s clip-space host): the
275 census vertex shaders transpiled to vs_5_0 with stream output (`shaders/replay`, each proved against the game's
vs_3_0 on an SM3 vertex interpreter), static draws placed once. The helper works in metres: the camera and the ranges go
to it at 0.01 a world unit (a centimetre: the map's terrain box is 26 km square); in centimetres near 2e6 its scene data
dropped every triangle as garbage (past 200 km) and nothing cast a shadow. Its camera is the depth's as drawn: the scene
draws' jitter and D3D9's pixel centres (half a pixel left of and above D3D11's; without them every ray started under
the ground at 256 x 144, at 4K about a quarter of the rays' surface offset). Ours replaces the game's, one method an effect:

| Row | What it does |
|---|---|
| Ray-Traced Ground Shadows | The traced sun visibility with the game's shadow-map term in the ground mask, the darker of the two (the pre-pass `0x8588D239` writes only that term into the red of the mask the 110 terrain and 94 water shaders read by screen position). A frame without a trace keeps the game's. |
| Ray-Traced Object Shadows | 642 object pixel shaders, patched in their bytecode (`ru_scene_patches.py`): after their shadow-map factor's off-map select, `cmp rD, -rT.w, rD, rT.x` with s15 the visibility (its alpha 1: the rays in place of the factor, where they found the sun blocked; an alpha-0 texel when untraced: the game's). Moving units are a frame late (last frame's main depth reprojected). |
| Screen-Space Ground Shadows | Bend Studio's march with the game's shadow maps (the darker of the two in the ground mask). Off under the ground rays. Its IgnoreEdgePixels on (Bend's advice for large flat surfaces): far ground's row-to-row depth steps pass its edge threshold, and such a row's sample shadowed the pixel beside it (single dark pixels on lit ground). |
| Screen-space rows' depth | Bend, GT-VBAO and GT-VBGI march over O1's start depth: last frame's main depth (kept at the bloom) reprojected over the ground pre-pass's. The pre-pass draws the terrain alone; on its depth they found only its relief, which the shadow maps already shade (run_rn3: no visible change for +1.1 / +2.5 / +2.6 ms GPU). Without the anti-aliasing's bridge (GT-VBAO alone) there is no main depth: the terrain's. |
| Ray-Traced Ground Ambient Occlusion / Screen-Space Ground Ambient Occlusion | The rays' AO, else GT-VBAO, into the 108 terrain shaders' ambient (s14). Both skipped once Path-Traced Light Bounce replaces the ambient. |
| Ground Light Bounce | Screen-Space: GT-VBGI on the ground. Ray-Traced / Path-Traced: the helper's Light Bounce through guides the patched terrain and object shaders write (oC1 albedo, oC2 normal), into the same s14 texel; Path-Traced brings the sky in place of the game's flat ambient on the ground and the objects (Lean and Near ReSTIR Reuse on). |
| Water Reflections | Screen-Space: a march over the start depth, its hits in last frame's scene; Ray-Traced: the helper's reflections; the 120 water shaders blend it over their cube by its share (s13). |
| Rays Replace Shadow Maps (live, Experimental, off) | On: the rays alone, in place of the game's shadow-map term (ground) and factor (objects). Off: the rays keep the maps until a run proves they hold the maps' casters (ours replaces the game's where proven). |
| Rays Off Above (live) | The map table: over this eye height (metres) no trace, the game's shadows (and their maps); 0 m: any height. ReShade.log gets the eye's height when the row changes and when the rays go off or come back (`[R.U.S.E.] Rays Off Above`, 16 lines a start at most). |
| Units Ray Tracing Range (live) | Moving units (the replay's compact draws) farther than this out of the scene; 0 m: all. |
| Time Each Pass (One Frame) | The next frame's D3D9 timestamps at its bounds (start, camera pass, trace point and its end, bloom, after the anti-aliasing, final post, present) into ReShade.log. The add-on's CPU waits for the D3D11 queue and the helper show as D3D9 time between the trace point and its end. With rt_core's Record Ray Tracing Profile (`RENODX_TW_PROFILE`) the CPU's steps (replay, ground passes: D3D9 wait, capture, trace request, D3D11 and helper wait; anti-aliasing) log one line at its end. |
| Ground Debug View (live) | Over the scene at the bloom: the ground mask's red (the game's term with ours), our sun visibility (rays or Bend), the ground's occlusion (rays or GT-VBAO); black shadowed, magenta where that frame made none. |

Not covered: 2 object shaders whose shadow select writes an output, 60 lit object shaders the guides can't patch, 12
terrain shaders (they keep the game's shading). The game's shadow-map pass still draws (its readers: water, sprites,
the shaders above).

The log says what runs: `[R.U.S.E.] Anti-aliasing: DLAA ready at WxH`, `... applied`, `[R.U.S.E.] Ray tracing:
tracing (...)` or `... not tracing (why)`, `[R.U.S.E.] Patched game shaders installed: ...`, `[R.U.S.E.] Frame
generation: ...`; each at start or at a change, never a line a frame.

The trace point: after the ground mask's copy, at the main pass's clear of the pre-pass's depth (the game's AA off: both
passes draw into it), else at the main pass's first draw. A frame without rays (the helper didn't trace, or no trace
point: menus, the map table) asks again 60 frames on; the replay records only a try's frame and the one before, and a
try whose last frame wasn't recorded whole shows the game's shadows that frame. Scene draws: a frame-sized target and
viewport (not the sprite atlas or the bloom, which draw into the pre-pass's target); the replay leaves out vertex
shaders that read fewer than c0-c3 (the camera pass's quad).

## Postprocess, effects, video and UI

All seven final post permutations use `shaders/postprocess.hlsli` with the game's register bindings, blur kernels,
bloom and packed scene-range restoration:

| Native sampling | Bloom | No bloom |
|---|---|---|
| 17 taps, weighted | `0x232EB5DF` | `0x44550114` |
| 13 taps, uniform | `0xC3EBA3CA` | `0x2D5EFDE2` |
| 9 taps, uniform | `0xFB692F37` | `0x7E93D12F` |
| Single tap | — | `0x0106B253` |

Advanced → Extra: Bloom and Vignette (HDR modes), Motion Blur (0 = the centre sample only; the multi-tap posts) and
Depth of Field (the two DOF filters `0x6F08A739` / `0xDC8B14DB` bypassed, the masks `0x971790DF` / `0x7E5A7D9B` alpha
scaled), Video AutoHDR (default 0) and Video Dithering on every video and loading-screen draw.

UI and video: 26 replacements (ScaleForm, interface, text effects, the cutscene videos `0x1D8E1FA8` / `0x66D7BE52`, the
loading screens) keep the game's math and scale only their final back-buffer draw (UI by the UI row, video by Paper
White). The native DX9 ReShade GUI is blocked after a successful proxy present (`R.U.S.E.: native DX9 GUI draw filter
installed.`); the proxy's overlay stays.

Gamma 2.2 is the mod's working decode / encode. The final response is `Brightness * rgb * luma^(Gamma - 1)` (the map's
c5.x / c6.x, per-map TPostProcess values: `tools/nontw/ruse/map_postprocess.tsv`).

The injection: `ShaderInjectData`, 44 floats in DX9 c200-c210 (the game's shaders read c0-c199; c210 holds the rays'
texel size for the patched shaders); the proxy reads it at b13.

## Build and verification

```powershell
cmake --preset clang-x64
cmake --build build --config Release --target renodx-ruse.addon64
```

Offline checks (tools/nontw/ruse/scripts, outside the repo):

- `ru_post_vanilla.ps1`: the seven final posts in SDR at Paper White 203 = the game's bytecode on the SM3 interpreter;
  controls Paper White 250 and PsychoV35 must differ.
- `ru_c1_verify.ps1`: the UI / video / cutscene / DOF replacements' native functions = the game's, and their whole main
  at neutral injection = the game's exactly; a control each (UI scale, video scale, Depth of Field, a wrong copy) must differ.
- `ru_scene_patches.py`: every patched scene shader = the game's at its neutral texel, different at an active one; the
  shadow patches free of the shadow map at alpha 1. `ru_d3d9_validate.py`: the D3D9 runtime accepts each (NULLREF
  device, with a refused control).
- `test/tw_psychov35` part 6 (WARP): grey, slope, markers and Extended's ramp for the three grades.
- `test/tw_ruse_bridge` (GPU): H1 INTZ, shared textures, the 10-bit round trip and jitter; H2 the helper's DLAA / FSR /
  XeSS and frame generation on synthetic frames, the rays end to end at the game's coordinates (case 12); 6b O1's
  reprojection and the screen-space rows over it; H3 the replay against the census VS.

In game: in-game AA becomes 1x after the first start with Anti-Aliasing on (check the log's `Option.ini MainRTAA 8.5 ->
1`), the next start logs `Anti-aliasing: DLAA ready` and `... applied`; with any ground row `Ground passes ready at
WxH: ...`; with the rays, `Ray tracing: tracing (...)`.
Compare each row off / on in one battle (F1 takes the HDR runtime's screenshot).
