# Ryse: Son of Rome — DLAA / DLSS

Separate addon, independent of the HDR addon (`rysesonofrome`). Build target: `renodx-rysesonofrome_dlss.addon64`
(CMake links NVIDIA's NGX library for `expeditionsrome_dlss` and `rysesonofrome_dlss`).

## How it works

- Hooks the game's TAA draw `0x121C45E4` (CryEngine, SMAA 1TX style; after tonemap and SMAA, before post and
  Native Upscaling). The render size is its textures' (Rendering Resolution, below).
- Preparation (`0x52594501`, private): drawn with the game's TAA vertex shader, inputs, PER_BATCH and viewport.
  Writes reverse-Z depth (near / view depth), motion in pixels and linear color (sRGB view, or the gamma 2.2 round
  trip for 8-bit UNORM / HDR addon FP16 targets). Camera motion: the surface the game reconstructs for the pixel (its
  view ray TEXCOORD1 x linear depth) projected with `mViewProjPrev` and with this frame's camera (`g_VS_ViewProjMatr`,
  VS PER_FRAME, as uv), each without its part along view forward (the projection's jitter; the same removal for both
  frames), the camera position folded into the translation in double. v22 used the TAA's texture coordinates as the
  current position: the view ray holds this frame's jitter, so a still camera had ~0.1 pixel of motion that changed
  every frame (DLSS's wobble); v21's float reprojection of large world coordinates added ~0.1 pixel of noise near the
  camera. If this frame's camera does not put the surface on the pixel (within 1 pixel), the TAA's uv is used. Object
  motion: the packed velocity from the game's buffer.
- Jitter: from this frame's `g_VS_ViewProjMatr` (VS PER_FRAME b2, CPU copy of the game's uploads); when the camera
  bound at the TAA is not jittered, the main camera's (same `g_VS_WorldViewPos`) latest jittered upload of the frame.
  Pattern: CryEngine `r_PostAAMode` (0 none, 1 2x, 2 4x, 3 8x,
  4 sparse grid 8x8, 5 random, 6 Halton 8x, 7 Halton random). The game sets its own (2x) from its settings,
  so `system.cfg` does not stick: `engine_memory.hpp` finds the int in memory (the `Register` call that loads
  the name into rdx and the static's address into r8; the code is only decrypted at runtime, Steam DRM) and
  the addon writes the chosen pattern every frame it differs while DLSS is on, the game's own when off.
- CryEngine's jitter is the renderer's sub-pixel offset (NDC, renderer +0xcd0), added to the projection's
  m20 / m21 while `r_PostAAMode` > 0. Its setup zeroes the offset and computes one only while the view allows it,
  `r_PostAAEdgeFilter` is 3 (SMAA 2TX), every time-scale channel runs at 1 or more (`ITimer::GetTimeScale(1..5)`:
  no slow motion, hit stop or pause, which Ryse uses all the time) and a frame counter moved (frozen while
  paused). With Jitter "the addon's own" (default; the option is at the bottom of Advanced), a jump at the setup's start (5 bytes, one aligned 8-byte
  store, found by a unique pattern, checked before writing, restored when DLSS is off) goes to a stub next to
  the exe that keeps the view's flag, stores the addon's Halton (2, 3) offset (8 positions, written every
  Present) and continues on the game's "no jitter" path. Where each camera's projection gets the offset, the
  game also requires bit 27 of the renderer's per-frame flags (+0x4524) to be clear, and it is set while paused:
  that check's `and cl, al` becomes two nops (one aligned 8-byte store; the per-camera checks for
  render-to-texture / shadow passes and recursion stay). The jitter that reaches the frame is still measured
  from the matrices for DLSS, and logged on every start / stop, plus a summary every 600 TAA frames.
- Debug: Advanced "Save the game's code for analysis" writes the decrypted image (`Bin64\ryse_image.bin`, file
  offsets = RVAs) for offline disassembly.
- Game settings above its High (overlay, saved in ReShade.ini `[Ryse Game Settings]`): the game's menu has Low /
  Medium / High; autoexec.cfg's 14 sys_spec groups at 4 (very high) go one level further. The cvar whitelist rejects
  the rest in config files, so the addon sets them through the console (GetCVar, ICVar Set), below the DLSS status
  in Simple mode: Engine quality (the 14 sys_spec groups: High 3 / Very High 4, absolute), Shadow resolution
  (e_ShadowsMaxTexRes 2048 at very high, e_ShadowsPoolSize, e_ShadowsResScale x2 / x4), Shadow distance
  (e_ShadowsCastViewDistRatio x1.5 / x2), Draw distance (e_ViewDistRatio, -Vegetation, -Detail, -Lights,
  e_MergedMeshesViewDistRatio, e_MergedMeshesInstanceDist, e_VegetationSpritesDistanceRatio,
  e_ProcVegetationMaxViewDistance x1.5 / x2 / x3), Detail distance (e_LodRatio, e_AttachmentLodDistScale,
  r_DetailDistance x2 / x4), Tessellation (e_TessellationMaxDistance x2 / x4, r_TessellationTriangleSize / 2 / 4),
  Texture streaming pool (r_TexturesStreamPoolSize x2 / x4, 1536 MB at very high), Decals (e_DecalsLifeTimeScale,
  e_DecalsRange x2 / x4). v32 adds: per-object (character) shadows e_ShadowsPerObjectTexRes (4096) to Shadow
  resolution; Shadow cascades (e_GsmLodsNum 5 -> 6 / 7 / 8, absolute; 8 = CryEngine's most); e_ViewDistMin and the
  grass / vegetation pools (e_MergedMeshesPool, e_ProcVegetationMaxObjectsInChunk / MaxSectorsInCache, up to 16x)
  to Draw distance; r_VegetationSpritesTexRes (64, up to 2048) to Detail distance; Particles
  (e_ParticlesMaxScreenFill, e_ParticlesMinDrawPixels /, r_ParticlesTessellationTriSize /, at least 1); Water
  (e_WaterTessellationAmount, -X, -Y, up to 16x). e_DecalsRange is refused (reads back 20; flags 0x800000), so
  it was dropped. Each option's first item, "In-game setting", is what the player set in the game; Nx = N times
  what the in-game graphics preset sets ("Engine quality" is now "Graphics preset (in-game)"). v33 choices: shadow
  resolution 2x / 4x / 8x (16384, the maximum), cascades 6 / 7 / 8, shadow distance 1.5x-256x, draw distance
  1.5x-256x, detail / tessellation / particles / decals 2x-256x, water 2x-16x, texture pool 2047 MB max (one check, +0x174015F,
  converts MB to bytes in a signed 32-bit int: negative from 2048 MB, so 3 / 4 GB left some textures unloaded and
  12 / 24 GB (0) all; ATN v34-v36 tests). Buttons: Reset (all In-game setting), Very High (graphics preset Very High, the rest about 2x),
  Recommended MAX (every option's last item, shadow resolution 4x: 8x costs a lot for no visible difference, ATN),
  MAXED OUT (every option's last item). v31 test: no visible change past the engine's caps; shadow resolution costs
  the most. A note under the DLSS preset: changing the AA mode or render scale restarts the game's auto exposure
  (resolution change), which settles by itself, and textures reload (ATN: some can stay missing or blurry; a
  checkpoint load or a restart fixes it). Factors multiply the in-game value, read again when the game sets its own (its
  menu). Applied once the game's TAA is seen (its startup spec is set by then), not restored at unload (the game
  does not save these cvars). Values at very high: Bin64\ryse_cvars.txt (debug button below). The game's
  full-screen chromatic aberration (r_ChromaticAberration, CA_Sharpening 0xC53CE326) was tried here (v30) and moved
  to the HDR addon's post pass.
- Vegetation detail at the display width: the vegetation groups' sprite switch distance (StatInstGroup update,
  +0x16EB00) is x the render width / 1024 (or x e_ForceDetailLevelForScreenRes / 1024 when set). Below 100% trees
  switched to their sprites (no shadows) sooner, so shadows ended closer at every smaller scale (ATN). While the
  addon renders below the display width it writes the display width into e_ForceDetailLevelForScreenRes (3D engine
  cvars +0x3f0, pattern VEGETATION_SCREEN_RES; its ICVar refuses sets, flags 0x800000); the game's value comes back
  with DLAA / Off and at unload. A settings hash (+0x17CC10) includes it, so the groups update by themselves.
- Texture LOD bias (DLSS section; Off / Matched / Sharper (default) / Sharpest): below 100% the game's anisotropic
  samplers (material textures; post passes filter linear or point, shadow maps compare) are bound again after its
  PSSetSamplers (ReShade raises push_descriptors after the call) as cached clones with MipLODBias +
  log2(render / display width), minus 1 (Sharper) or 2 (Sharpest). DLAA: no bias.
- While DLSS / DLAA runs (AA mode not Off), two renderer statics are written directly (FindIntCvar), the game's values
  back with Off and at unload: r_TexNoAnisoAlphaTest 0 (anisotropic filtering also on foliage, fences, cloth; read
  while drawing, +0x18338EB; its ICVar refuses sets, flags 0x800100) and r_DeferredShadingFilterGBuffer 0 (no
  smoothing of normals / gloss against specular shimmer, which DLSS / DLAA calm; finer surface detail and highlights).
  Both were options in v39; ATN: always better with DLSS / DLAA.
- Debug: Advanced "Save the game's settings" writes every console variable with its value and flags
  (`Bin64\ryse_cvars.txt`), walking the console's name map (CXConsole +0xf8). The console comes from
  con_restricted's registration (pattern `CONSOLE_REGISTRATION`); the result also shows con_restricted and
  whether GetCVar (+0xb0) returns the renderer's r_Width. The game's cvar whitelist (HDR addon README) rejects
  most cvars from config files; these can only be set in code.
- SMAA skipped: the game runs SMAA (edge detection `0x15D2F34D`, blend weights `0x5DC96409`,
  neighborhood blending `0xF2D692D1`) right before its TAA. While DLSS replaced the last frame's TAA, the first two
  are skipped and the blend becomes a copy of its color input (`0x52594503`), so DLSS gets the raw frame (NVIDIA:
  no other anti-aliasing before DLSS) and the three passes cost nothing.
- Off releases only NVIDIA's feature (its memory); the runtime stays initialized, so switching back takes tens
  of milliseconds instead of a 1-3 second freeze.
- At process exit NGX is not released (the game never releases its device; NGX crashed inside its model
  module during the addon unload).
- NVIDIA evaluates in a private context state; the copy pass (`0x52594502`, private) writes the result with the game's
  encoding: DLAA into the game's TAA target. The stock TAA draw is skipped; any failure runs it instead.
- Modes like Expeditions: DLAA (100%), DLSS Ultra Quality (77%, on NVIDIA's Quality model), Quality (67%), Balanced
  (58%), Performance (50%), Ultra Performance (33%), Custom (14-100%: below, the game renders at its minimum). The render size is the game's Rendering
  Resolution: `r_Width` / `r_Height` with Native Upscaling (`r_FullscreenNativeRes`); the game renders into targets of
  that size, its native upscale (`0xCD8548F6`, bicubic-like, in linear) stretches the result to the back buffer and
  its UI follows at the display size. Its menu offers 50-100% in steps of 10; the cvars take any size. The addon sets
  them through the renderer's ICVar pointers (+0xb828 / +0xb830 / +0xf540; ICVar GetIVal vtable +0x10, Set(int)
  +0x38; the renderer is the object `RT_RenderScene`'s start passes, unique pattern), as the game's menu does: its
  display check resizes the targets (a short hitch). Set once a mode's size stayed unchanged for 30 presents (the
  Custom slider), again whenever the game sets its own (its menu), back to the game's own with AA Off; stops if the
  game keeps setting its own. Below the display size only with Native Upscaling on (else the game would change the
  display mode). The game's own is kept in ReShade.ini (`GameRenderWidth` / `GameRenderHeight`) while the addon's is
  set and restored on the next start if the game ended with the addon's. Sizes keep the display's exact aspect ratio
  (steps of its reduced ratio, 16x9 at 4K: Quality 2560x1440, Balanced 2224x1251, Ultra Quality 2960x1665; the HDR
  addon upgrades only textures of that ratio, v20: 2227x1253 stayed 8-bit and clipped HDR), at least the game's
  minimum (it renders smaller sizes at 512x300: 544x306 at 4K, Custom 14%). The jitter uses 8 x (display / render)^2
  positions. Every DLSS mode's preset hint is set (NVIDIA picks the mode by the ratio; v20: 40% used its default M).
  After a size change the game's eye adaptation starts again and does not move while paused. A size the game sets
  itself (its settings at startup, its menu) counts once (v21 counted it every present until its own was set again,
  so a mode saved in ReShade.ini stopped at startup).
- DLSS gets a fixed exposure of 1 (1x1 texture; no AutoExposure: it measured every jittered frame, the input is
  already exposed by the game's tonemap).
- Upscaling (render size below the display): NVIDIA outputs at the display size, encoded like the game's TAA output
  into the addon's copy of the TAA target at that size (`taa_full`: the target's format and view). The stock TAA still
  draws at the render size (the frame the native upscale shows if the addon's does not come). The post pass
  (`0xD0B247FF`, `0xB022AFEA`, `0xC3A469C1`, `0xCC6DF78E`: sharpening, film grain, lens flares) is drawn first at the
  display size into `post_full` (its target's format), with `taa_full` as t0 (the game's t0 view), the addon's
  full-screen triangle (`0x52594504`, texture coordinates 0-1) and the game's pixel shader and other bindings (its
  lens flares t5 are at the render size and filtered up); then the game's own at the render size. That redraw gets a
  copy of the game's PER_BATCH (b0, c0-c5) with PS_ScreenSize (size, 0.5 / size; c3 with sharpening, else c2) set to
  the display size: with the render size, its texel offsets (sharpening taps, the lens flares' red shift, the HDR
  addon's chromatic aberration) grew by display / render, about 7x at 14% (v42; needs partial constant buffer
  updates, D3D11.1, else logged and left as it was). The native upscale
  then copies `post_full` 1:1 instead (`0x52594503`, t1; stored values: the game's upscale decodes sRGB and encodes
  again), into the back buffer where the UI follows. Frames upscaled but not shown (no post or native upscale seen)
  show the game's own soft frame and are counted in the overlay.
- v13-v19 used the game's dynamic resolution (overload scene manager, `osm_*`, renderer vtable +0x168): it renders
  every pass into the top-left corner of full-size targets. On PC (where the game never uses it) its motion blur
  (PostAA +0x180c670 / MotionBlur +0x18c92c0 build `mViewProjPrev` with the static scale-bias +0x221cc10, without the
  corner), depth of field (black) and something before the TAA (nested, frozen copies of the scene) break with it; the passes after
  the TAA and the UI expected the corner too. A TAA viewport smaller than its textures is now refused (stock TAA).
- The backend's size checks allow per-axis rounding (x and y ratios up to 16 pixels apart); each axis is at least
  128 pixels. A size that does not match the mode's scale (the frames before its resolution is set) uses the Custom
  backend, which takes any scale.
- Debug: Advanced "Log one frame's passes after the TAA" (also automatic for the frame after the first upscaled one):
  every draw after the TAA and fullscreen-sized ones (6 vertices or fewer) before it, with pixel / vertex shader
  hashes, first target, viewport and pixel shader inputs t0-t7, plus copies (`[Ryse DLSS trace]` in ReShade.log).
- Debug: Advanced "Save two upscaled frames' stages": the next upscaled frame's DLSS input, motion (pixels / 32
  around gray; fine: -0.5 to 0.5 pixels), output, `taa_full`, lens flares (t5) and `post_full`, and the frame after
  it's input, output, lens flares and `post_full` (`_next`: what changes between two frames), as 8-bit PNGs
  `Bin64/ryse_capture_*.png` (`0x52594505`: values x scale + bias, saturated, ^gamma); both frames' jitter in the log.
- Before DLSS the game still applies its motion blur and depth of field (both before its tonemap; NVIDIA recommends
  them after upscaling): the game's Motion Blur option sharpens moving scenes. Film grain, sharpening and lens flares
  come after DLSS. v23 checks (two frames, still): the input moves by the jitter the addon reports (within 0.01
  pixel), motion vectors under 0.01 pixel, the output moves under 0.02 pixel (about 1% of its pixels change by about
  1/255 at fine edges: DLSS's residual with jitter).
- `nvngx_dlss.dll` (310.5+ for presets M/L) next to the addon or the game (`Bin64`).

## Manual verification

1. Game: Temporal Anti-Aliasing on, Native Upscaling on. Overlay "Ryse DLSS": AA mode DLAA, Advanced: Jitter the addon's own.
2. Status "DLAA active"; Advanced: jitter 8 positions also while moving, in slow motion and pauses,
   `r_PostAAMode: 6`, "Jitter setup: at +0x... replaced by the addon's own jitter; pause flag check: at +0x...,
   skipped", history resets not climbing.
3. ReShade.log: `[Ryse DLSS] Found r_PostAAMode ...`, `r_PostAAMode is ...`, `frame ... jitter ...` lines (first
   32 frames after each reset) and NGX creation.
4. Static camera: no shimmering/wobble. Moving camera / characters: no ghosting trails.
5. DLSS Quality / Performance / Ultra Performance / Custom: a short hitch, then status "<mode> active", "Input 2560x1440
   -> output 3840x2160" (Quality at 4K), Advanced "Rendering Resolution 2560x1440 (the game's own: ...)"; the image
   sharp (not the game's soft upscale), film grain and lens flares in place, HUD sharp; depth of field in executions
   and particles normal; no "Frames DLSS upscaled but the game showed its own" line; AA Off puts the game's own
   Rendering Resolution back.
6. Quit the game with DLAA on: no crash dump in `%LOCALAPPDATA%\CrashDumps`.
