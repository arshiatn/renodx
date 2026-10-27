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
  resolution 2x / 4x / 8x (16384, the maximum; v43: character shadows stop at 8192, above it a tester saw bands
  on characters), cascades 6 / 7 / 8, shadow distance 1.5x-256x, draw distance 1.5x-256x (v43: without
  e_ViewDistRatioLights: shadows flickered in one area with it raised, whatever the shadow settings; v44: its own
  Light distance option 1.5x-8x, and Shadow pool, e_ShadowsPoolSize taken out of Shadow resolution: matched, 2x or
  4x (16384), never below Shadow resolution's factor so its larger light maps still fit), detail / tessellation / particles / decals 2x-256x, water 2x-16x, texture pool 2047 MB max (one check, +0x174015F,
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
  Both were options in v39; ATN: always better with DLSS / DLAA. Armour shimmer with DLAA (ATN) is
  identical without the HDR addon: the game's specular aliasing, not FP16 targets. v56 had an option to keep the
  filter on; removed in v57 (ATN: off gives much better highlights).
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
  lens flares t5 are at the render size and filtered up); then the game's own at the render size. Its PS_ScreenSize
  stays the render size: the HDR addon's post shaders take their texel offsets from t0's size instead. The native upscale
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

## v45: face-area LOD

ATN (debugForClaude14): a vase switched to a lower model 2-3 m away with Detail distance 256x. Ryse picks a static
object's LOD by its triangles' size on screen (`e_LodFaceArea`, threshold `e_LodFaceAreaTargetSize`; both in the
code dump), which `e_LodRatio` does not move. Detail distance now also divides `e_LodFaceAreaTargetSize` (smaller is
finer). Untested.

## v46: light shadow flicker above Very High

ATN (debugForClaude15, MAXED OUT: shadow resolution 8x, pool 4x, light distance 8x): light and shadow spots on the
walls and flags changed frame to frame once the hall's torch side came into view. The shadows of lights share one
pool (e_ShadowsPoolSize, 16384 at most) and each light's map is capped by e_ShadowsMaxTexRes, the sun's resolution:
at 8x (16384) one near light could take the whole pool, and light distance 8x made many more lights cast shadows.
Now: Shadow resolution stops at 4x (e_ShadowsMaxTexRes 8192, half the largest pool, as the game's 2048 / 4096), and
Light distance divides e_ShadowsCastViewDistRatioLights by its factor (a light casts shadows within its view
distance x that ratio: dump +0x18C7E9, +0x250632), so lights reach farther but as many cast shadows as in the game.
Saved 8x reads as 4x. Untested.

## v47: shadow trace (debug)

v46 did not fix the flicker (ATN). Advanced -> "Trace the shadow maps for 300 frames" writes ryse_shadow_trace.txt
next to the exe: a header with every graphics cvar (the game's value -> the addon's request -> what the game kept,
so engine-side clamps show), then per frame every depth target drawn without a color target (sun cascades, the
lights' pool, per-object shadows) with each viewport drawn into it and its draw count, and the share of the target
used. Maps that come and go between frames with the camera still show what flickers.

## v48: what the shadow trace showed (debugForClaude16)

MAXED OUT vs Very High at the flickering spot, 300 frames each: the lights' shadow maps never changed (MAX: the same
39 maps in the 16384 pool, 74% used; Very High: 38 maps in 8192, 66%), and draws per map changed as rarely in both.
So the pool did not overflow; the flicker is not in the shadow maps. e_ShadowsCastViewDistRatioLights was refused
(flags 0x800080, read back 1): removed from Light distance. Every other cvar kept the requested value. Shadow
resolution stays at 4x max (4x maps fill 74% of the largest pool; 8x would need 4x that). Next suspect, from the
two runs' differences: Light distance (8x vs the game's) with tiled deferred shading (r_DeferredShadingTiled 2,
e_DynamicLightsMaxCount 512, e_DynamicLightsConsistentSortOrder 1).

## v49: the flicker's cause, and an options measurement

ATN: Light distance alone did not help, lowering Draw distance did. CLightEntity::GetMaxViewDist (dump +0x609D1) is
max(e_ViewDistMin, size x e_ViewDistRatioLights x lod ratio): Draw distance 256x put e_ViewDistMin at 2560, so every
light in the level was active and lights / shadows flickered. e_ViewDistMin now stops at 20 (Very High's); objects'
distance comes from the ratios, which still scale (the minimum only matters for the smallest objects).
Advanced -> "Measure every option's choices": each option's choices in turn (not the graphics preset and the texture
pool), 60 frames to settle and 60 measured: scene draws / indices, shadow-map draws / indices, frame ms, in
ryse_options_measure.txt. Where the numbers stop changing, higher choices draw nothing more.
Not exposed yet (settable): e_MergedMeshesLodRatio 3, e_TerrainLodRatio 1, e_VegetationSpritesMinDistance 8,
e_CoverageBufferResolution 128, r_SilhouettePOM 0.

## v50: Light distance removed, caps from the measurement (debugForClaude17)

ATN: with e_ViewDistMin capped (v49), any Light distance above the game's flickers -> the option is gone (the game's
e_ViewDistRatioLights stays). Measurement at the game's start (outdoors, camera still), per choice: Detail distance
2275 -> 3071 scene draws (13.1 -> 22.8 M indices), the same at 64x and 256x; Draw distance 3039 -> 3071, the same
from 64x; Tessellation frame time 14.29 -> 14.42 ms by 4x, flat after; Shadow resolution 13.25 -> 14.51 ms (4x kept).
Caps: Draw distance and Detail distance 64x, Tessellation 4x. No change in that scene (nothing to measure there):
Shadow pool, cascades, Shadow distance, Particles, Water, Decals: kept until measured where they matter.
Recommended MAX and MAXED OUT are now the same (every option's last item).

## v51: everything up to 64x in powers of two, sliders

ATN: every multiplying option ends at 64x with each power of two on the way (Shadow distance 1.5x / 2x ... 64x,
Draw distance 1.5x / 2x / 3x / 4x ... 64x, Detail distance, Tessellation, Particles, Decals 2x ... 64x); Water stays
at 16x, shadow resolution / pool at 4x, the texture pool at 2 GB. Old saved highest choices (256x) read as 64x.
The options are sliders over their items, each showing its current item (as RenoDX's labelled settings;
ImGui::SliderInt with the item as its format, AlwaysClamp | NoInput).

## v52: five more options (ATN testing them)

Grass detail distance (e_MergedMeshesLodRatio x: grass LOD distance x the ratio, +0x820B6), Terrain detail
(e_TerrainLodRatio divided: terrain LOD from the screen error x the ratio, +0x150FA3), Trees as 3D models
(e_VegetationSpritesMinDistance x: the sprite distance is at least it, +0x16ECA0), all to 64x; Occlusion culling
accuracy (e_CoverageBufferResolution x, up to 1024: the engine clamps it there, +0x201FF3 / +0x2022A5); Parallax
silhouettes (r_SilhouettePOM on). In no preset except MAXED OUT; two test buttons set only these five to the game's
values or their highest.

## v53: the five v52 options removed; Ultra and Extreme

Measurement (debugForClaude18): every choice of the five gave the same scene / shadow draws and indices, frame time
within noise (13.72-13.81 ms); ATN saw no difference either. Removed. (Trees as 3D models was also redundant: the
sprite distance is max(the ratio's, the minimum), and Draw distance already scales the ratio.)
Buttons: Reset, Very High, Ultra (distances / details 16x, shadow resolution 2x, pool 2x, 7 cascades, water 8x),
Extreme (32x, 4x, 4x, 8, 16x), MAXED OUT (64x). Recommended MAX removed (it equalled MAXED OUT).

## v54: five buttons

Reset, Very High, Ultra (8x, shadow res 2x, pool 2x, 7 cascades, water 4x), Extreme (16x, 2x, pool 4x, 7, 8x),
Insane (32x, 4x, 4x, 8, 16x), MAXED OUT (64x). GRAPHICS_PRESETS table + QualityOption::preset_choices. The shadow pool
only reserves video memory (measured: frame time the same at every pool size).

## v58: Water capped at 4x (chapter 3 crash)

Water 8x / 16x (Extreme, Insane, MAXED OUT) crashed the game at the start of chapter 3 (tester, then ATN). Both crash
dumps: the render thread's memcpy writes past a 4 MB (0x400000) buffer; the copy is (X - 1) x (Y - 1) x 12 bytes of
the sea's grid (5532492 = 679^2 x 12 at 8x, 22162572 = 1359^2 x 12 at 16x; e_WaterTessellationAmountX / Y 85 at Very
High). It fits up to X = Y = 592 (~7x). Water now ends at 4x (339^2 x 12 = 1.4 MB); Extreme / Insane / MAXED OUT use
4x; a saved 8x / 16x loads as 4x (choices are clamped).

## v59: more LOD / streaming cvars (ATN: "every LOD cvar"), experimental options

Every cvar of the game (Bin64\ryse_cvars.txt, 3141) was checked with its help text from the exe (tool rx.py: the
IConsole::Register calls). Flags 0x800000 are refused (left out). Already finest: e_LodMin 0, e_CharLodMin 0,
e_ObjQuality 4, e_MaxViewDistSpecLerp 1, r_EnvCMResolution / r_EnvTexResolution at their maximum.
- Draw distance: + e_ViewDistRatioCustom (players, AI), + e_MergedMeshesActiveDist (grass patches streamed in, up to
  16x like the grass pools).
- Detail distance: + ca_AttachmentCullingRation (attachments such as shields culled by size on screen).
- New **Model streaming** (in the presets: Very High 2x, Ultra 8x, Extreme 16x, Insane / MAXED OUT 32x):
  e_StreamCgfPoolSize ("Render mesh cache size in MB", 80: with our Detail distance the detailed models no longer fit,
  likely why a tester's shield and rocks loaded late), max 2047 (MB << 20 in a signed 32-bit compare, +0x17630a /
  +0x176457, as the texture pool); streaming limits e_StreamCgfMaxTasksInProgress (<= 256),
  e_StreamCgfMaxNewTasksPerUpdate (<= 64), r_TexturesStreamingMaxRequestedMB (<= 64), r_TexturesStreamingMaxRequestedJobs
  (<= 256): compared with counters, not array sizes.
- **Experimental** (QualityOption::experimental: in no preset, Reset only, listed last): Texture streaming detail
  (r_TexturesStreamingMipBias 0 / -1 / -2), Reflection distance (e_RecursionViewDistRatio x up to 1.0), Sun shadow
  updates (e_GsmCacheLodOffset 3 / 5 / 8), Lights per object (e_DynamicLightsMaxEntityLights 16 / 32: a 32-bit light
  mask, +0xf9a3), Foliage wind distance (e_FoliageWindActivationDist x), Water effects (ripples / caustics resolution
  up to 4096, caustics distances), Light beams (r_BeamsMaxSlices 200 / 300). e_ParticlesLod left out (ATN: changes
  particle counts).

## v60: no chapter crashes (ATN, all maxed); experimental options folded into the sliders

- ATN: every chapter runs with everything maxed (v59).
- e_StreamCgfMaxTasksInProgress / e_StreamCgfMaxNewTasksPerUpdate are VF_CHEAT (flags 0x2): the game put them back
  every frame, each time with "CXConsole::OnBeforeVarChange(): variable [...] is marked [VF_CHEAT]" in its console
  (ATN's screenshot). Removed. New guard: a cvar the game puts back to its value right after a set 3 times is left
  alone (QualityCvar::reverts, log warning, overlay "refused").
- Water: 4x looked bad on the sea (ATN). Presets and MAXED OUT use 2x (QualityOption::maxed_choice); 4x and 6x (509^2
  x 12 = 3.1 MB, under the 4 MB bank; 7x would not fit) stay to see them. Ripples / caustics resolution and caustics
  distances joined it.
- The v59 Experimental options are now in the sliders (ATN): Texture streaming (2 GB, sharper -1, sharpest -2:
  r_TexturesStreamingMipBias as per-item values, QualityCvar::absolute / values; presets Very High 2 GB, Ultra /
  Extreme sharper, Insane / MAXED OUT sharpest), Draw distance (+ e_RecursionViewDistRatio up to 1,
  e_FoliageWindActivationDist up to 16x), Shadow cascades (+ e_GsmCacheLodOffset = the cascades: none cached), Detail
  distance (+ e_DynamicLightsMaxEntityLights up to 32), Particles (+ r_BeamsMaxSlices up to 300).
- Open: some soldiers' armour / shields only reach full detail within 2-3 m (ATN, everything maxed, Model streaming
  32x after a chapter load). Not known yet whether it is the model (LOD) or the texture (streaming).

## v61: armour / shields at full detail (Detail distance 64x)

ATN: some soldiers' armour, cloth and shields snap to a more detailed model only within 2-3 m, with everything maxed
(screenshots debugForClaude20). In the exe: attachments read "LodRatio" or "LodDistances" ("%f,%f,%f,%f,%f") from their
CDF (+0x8e4cfd). The attachment LOD (+0x881950, vtable slot +0x128): ratio mode turns LodRatio into distances with the
LOD params' e_LodFaceAreaTargetSize (+0x16a990, so our Detail distance scales it); LodDistances mode compares the camera
distance with the authored metres as they are. Nothing scales those (e_AttachmentLodDistScale is passed in the LOD
params, +0x17cfac, but this function does not use it). Both are then clamped to the params' e_LodMin / e_LodMax
(+0x17cf72, 3D engine cvars +0x2fc / +0x41c). e_LodMax is VF_CHEAT, so Detail distance 64x writes 0 to it in memory
(QualityCvar::engine_offset, from_choice 6; EngineCvars() shares the e_ForceDetailLevelForScreenRes pin's search):
every model, armour and shields too, at its most detailed at any distance. Below 64x the game's e_LodMax (6) stays.

## v62: Water 2x maximum; armour blend shapes (the real cause)

- Water ends at 2x (ATN: 4x looked bad, 6x crashed the PC). QualityOption::maxed_choice removed (unused).
- v61's e_LodMax 0 did not stop the armour switch at 2-3 m (ATN; the log showed e_LodMax found, 6). The switch is the
  blend shapes: CryAnimation culls, every frame, each blend shape whose largest vertex move x weight projects to fewer
  than ca_vaBlendCullingThreshold pixels (1; +0x8e0241: x 1 / (distance x tan(fov)) x screen height, flag at
  +0x8e03bd). Armour morphs of a few millimetres fall under 1 pixel at about 3 m and the armour snaps to its smaller
  base shape ("the armour on his arm gets bigger" up close). Detail distance now divides it (64x: 1/64 pixel).
  The cvar lives in the CryAnimation console object (static at +0x2240930, field +0x1ec); flags 0, set through the
  console. e_LodMax 0 stays at 64x (attachments with CDF "LodDistances").

## v63: armour test (debug)

v62 did not stop the armour growing within 2-4 m either (ATN; log: ca_vaBlendCullingThreshold found, set by Detail
distance). Advanced debug combo **Armour test (temporary)**, not saved: each test overrides one cvar (the quality
options skip it meanwhile; Off restores the value it found): ca_vaBlendEnable 0, ca_vaEnable 0,
e_TessellationMaxDistance 0.5, e_LodMin 2, e_CharLodMin 2. Up close, the test that makes the armour look like it does
from far away names the system (blend shapes, vertex animation, tessellation / displacement, model LOD, character LOD).

## v64: character test toggle and pixel trace (debug)

v63's armour tests changed nothing (ATN, also e_LodMin / e_CharLodMin at Detail 32x, where e_LodMax stays the
game's). ATN: the arm armour does not grow, it only sits on the arm within ~2-4 m. e_LodMax 0 at Detail 64x stays
(ATN: every model at its most detailed, fps do not matter).
- Advanced **Max every character setting (test)** (replaces the armour test, not saved): ca_vaBlendCullingThreshold 0,
  ca_AttachmentCullingRation 1000000, e_LodMin 0, e_CharLodMin 0, ca_vaSkipVertexAnimationLOD 0 through the console
  (the quality options skip them meanwhile); written in the CryAnimation console object (found from
  ca_ForceUpdateSkeletons' reader, FORCE_SKELETON_UPDATE, field +0xb8): ca_ForceUpdateSkeletons 1,
  ca_UseJointMasking 0 (+0x144), ca_ClothMaxChars 100 (+0x1cc), ca_FacialAnimationRadius 1000 (+0x194),
  ca_FacialAnimationFramerate 240 (+0x128). Off restores.
- Advanced **Trace this pixel for one frame** (pixel_trace.hpp, from the Tales DLAA addon): red cross, slider for its
  position; ryse_pixel_trace.txt next to the exe lists every command that changed the pixel with its draw parameters
  (index count, first index, vertex offset), vertex / index buffers (pointer, size, stride, offset), the vertex shader's
  constant buffer sizes and textures, and the pixel shader's textures. Far vs near on the arm armour: other mesh (index
  count / buffers), other skinning shader (vs hash), or the same draw with other bone data.

## v65: the trace saves the skinning bones

ATN's v64 traces (debugForClaude22, arm armour far / near): the same draw both times (54876 indices from 20136, same
vertex / index buffers, same 2048 textures). The shaders differ (far vs 0x1DD1EFED / ps 0xB02408C9, near vs 0xC38FB39D
/ ps 0x3433A827) only by object motion blur (the far pair also writes the velocity: CB_SkinQuat_Prev, o3); skinning
and G-buffer output are the same. So the pose (the bone data) differs: some armour helper is not updated far away.
"Max every character setting" (full skeleton updates, no joint masking) did not change it, but it fixed swords and
armour going through cloth beyond 5 m at no fps cost (ATN: keep it). v65: for each draw that changes the pixel, the
trace also copies the vertex shader's b9 (CB_SkinQuat_Cur, 768 dual quaternions, 24576 B) at the draw and lists every
used bone (rotation x y z w | dual x y z w) after the commands. Far vs near: bones that differ far more than the idle
animation are the helpers that only update near.

## v66: soldier skeleton log (debug)

ATN's v65 traces (debugForClaude23: far, farthest, close; same draw each time). Far, the armour's draw gets 254 bones;
close, 306. The extra close ones are helpers: 253 (= 252), 257, 258 = 259, 260 = 261 move (5-9 degrees, 0.14-0.25 m,
none equal to a joint), the rest stay at rest. Far, those slots hold stale data from the bone buffer's last user
(identity or garbage), so armour skinned to them stays in its bind pose: it does not sit on the arm. Why the count
differs: with r_GlobalCharBoneIds 1 (the game's) the skin draws with the master character's bone buffer, filled with
that character's joint count (CCharInstance +0xbb4, its skeleton's, set once in the constructor +0x8eb620; renderer
+0x17ee08f). So far and near the soldier is drawn with two different character instances (skeletons of 254 and 306
joints), not with the same one updated differently. Advanced **Log soldier skeletons (test)**: Detours hooks on the
character constructor and on the skin attachment's bone setup (CAttachmentSKIN::GetVertexTransformationData
+0x8e2570) log to ReShade.log each new character (joints, file, the game code that made it, exe+ return addresses)
and each skin's master character (joints, file) against the skin's own skeleton (remap table), again when it changes.

## v67: skeleton log in its own file

v66's test log was lost: the game was started again before ReShade.log was copied (it is overwritten at each start).
The skeleton log now also appends to ryse_skeletons.txt next to Ryse.exe (a dated header per game start).

## v68: skipped joints log, skin every joint (debug)

v67 log (debugForClaude25): no character is made or swapped on the way to the soldiers. Each soldier is a body
character (romn_lgnr*.cdf, 257 joints, 2 skins) and a head character (heads/soldier_*/sld*_head_v*.cdf, 515 joints,
10 skins). The armour's bone buffer is the head character's (515 used in every trace): far only joints 0-253 hold this
frame's skinning, near also 254-305 and 383-446. The skinning computation (+0x8ee9d0) computes only joints whose flag
0x20 (joint + 0x12) is set, so the set of skinned joints changes with distance.
- **Log soldier skeletons (test)** now hooks the skinning computation: for each character of 200+ joints it logs which
  joints are skipped (ranges), again when that changes, and how many skipped joints have a valid pose rotation.
- **Skin every joint (test)**: the loop's flag jump (je, +0x8eeea6) becomes a 2-byte jmp to the next instruction:
  every joint is skinned from its pose. If the armour sits from far away without breaking anything, that is the fix.

## v69: animation LOD (debug)

v68 log (debugForClaude26): the body (257 joints, the armour's bone buffer) skips only joint 254 in skinning, at any
distance; "Skin every joint" changed nothing (ATN), removed. So the far pose itself differs: far vs near (v65 traces;
far and farthest agree within 1-4 degrees) the hands turn 28-37 degrees, joints 137, 144-146, 148, 155-157 7-42
degrees, 253 follows 252 only near. The sampling commands' controller lookup (+0x934a70) takes the character's
animation LOD (sampling state +0x34) and, above 0, the model's chrparams <Lod><JointList> (+0x248, joint name
CRC32s): joints not in it get no controller and keep their default pose.
- **Log soldier skeletons (test)**: per model of 200+ joints, every joint's name, parent, flags and the LOD lists that
  keep it (once); per character, its animation LOD with the time, again when it changes (ryse_skeletons.txt).
- **Animate every joint (test)**: the lookup's "LOD above 0" jump (je +0x934ba0, one byte) becomes a jmp to "no
  list": every joint is animated at any distance.

## v70: Character animation option (the armour fix)

ATN's v69 test: "Animate every joint" fixes the armour, no fps difference (7950X3D). The v69 log (debugForClaude27):
soldiers' bodies (257 joints) have 4 animation LOD joint lists (126, 125, 54, 25 joints), heads 2 (87, 16); soldiers a
few metres away run at animation LOD 2-19 (the player 0). No list keeps L/R_UpArmTwist1 (54, 97), parents of the
LGNR_*_lorica_shoulder01-04_HLP plates (the lorica's shoulder guards), nor the forearm / upper arm / thigh twists,
the shoulder and knee helpers or slave_02: from LOD 1 on they keep their default pose.
- New game setting **Character animation** (last row, saved, every preset: Full): "Full at any distance" makes the
  animation LOD list jump (+0x934ba0, je -> jmp) skip the lists, so every joint is animated at any distance, and sets
  the old test's CryAnimation values (written in its console object): ca_ForceUpdateSkeletons 1, ca_UseJointMasking
  0, ca_ClothMaxChars 100, ca_FacialAnimationRadius 1000, ca_FacialAnimationFramerate 240. "In-game setting" puts
  the code and the values back.
- Removed the debug toggles (Max every character setting, Log soldier skeletons, Animate every joint, skeleton_log.hpp).
  The pixel trace stays (Advanced).

## v71: Simulation distance option, reflection test

- New game setting **Simulation distance** (saved; Very High 2x, Ultra 8x, Extreme / Insane 16x): es_MaxPhysDist 100,
  es_MaxPhysDistInvisible 25, es_MaxPhysDistCloth 300 (physics forcefully put to sleep beyond), g_distanceForceNoLegRaycasts
  5 (soldiers' feet stop following the ground), g_avatar_ground_alignment_enable_max_distSqr 400 (20 m, the factor
  squared), e_WindBendingDistRatio 0.5 (plants bend while 1 - distance / (draw distance x ratio) > 0, +0x183276; VF_CHEAT,
  3D engine cvars +0x26c, up to 2), p_splash_dist0 / 1 7 / 30, e_SkyUpdateRate 1 -> 100 (% of a full dynamic sky update
  per frame), at 16x es_UsePhysVisibilityChecks 0.
- **Decals** also multiplies e_DecalsNeighborMaxLifeTime 4 (s before older marks fade under newer ones) and
  e_DecalsMaxUpdatesPerFrame 4 (up to 256).
- Advanced **Reflection test (temporary)** for the soldiers' blurry, greyish armour reflections (ATN): no screen-space
  reflections, them at quarter resolution, no env probes, no reflection occlusion (r_ssdo 2), no rain wetness, e_GI 1.
  Renderer statics written in memory (FindIntCvar), e_GI through the console. How the tiled lighting (cs 0x897232B5)
  builds the reflection: probe cubemap at mip 6 x (1 - gloss), x max(0.3, 1 - r_ssdoAmount 4 x the directional
  occlusion toward it) when r_ssdo is 3 (SSDOParams.y, +0x1894c8f), then lerp to the screen-space reflections by their
  alpha. Those are traced at half resolution (ps 0xE0B7E115: 4 + 28 x gloss linear steps along 1.5 x the pixel's
  depth, a loose hit test, no refinement, the previous frame's scene), blurred into 1/2-1/16 resolution levels and
  picked by gloss^1.5 (ps 0x437BA5AF), then multiplied by the EnvBRDF.

## v72: Screen-space reflections, experimental settings, more detail and simulation

ATN's v71 reflection test: every test only made things worse; the grey on the soldiers' armour is the reflection of
the tree branches above them, blurred. The Reflection test is removed.
- New game setting **Screen-space reflections** (Very High: High, Ultra: Very High, Extreme / Insane: Ultra, MAXED
  OUT: Extreme): the game's trace (ps 0xE0B7E115) is drawn with the addon's 0x52594506 (command_action by shader hash;
  the game's shader and b12 put back after), parameters in ReflectionConstants b12 (REFLECTION_QUALITIES: steps x 4, 8,
  16, 32 along the game's ray x 1, 2, 4, 8, i.e. 4x the game's steps per metre). From the game's first sample, each
  crossing behind the depth buffer is bisected 6 times and kept when it lands within the game's tolerance (~0.6 of its
  step: the surface's thickness), else the ray goes on; it stops leaving the screen. The game's facing and edge fades,
  the previous frame's scene, its 100 x luminance clamp and NaN check are kept. The blur by gloss^1.5 (composite ps
  0x437BA5AF) stays the game's (ATN: no "sharper" item, it changes what the artists wanted).
- **Experimental** section (simple mode too; the preset buttons leave it alone): Global illumination (e_GI 1, light
  propagation volumes; ATN: about 10% fps, no visible difference found, the fps stayed lower after turning it off),
  Cloth cutting (g_avatar_cut_cloth 1, "Enables dynamic cloth cutting by avatars"), Particle count (e_ParticlesLod 2 /
  4: x each emitter's count, 3D engine cvars +0x408 read at +0xf5e67).
- Water: r_WaterVolumeCaustics 1 (off in the game: the caustics rows had no caustics). Tessellation: from 2x
  r_SilhouettePOM 1 (the game compiles those shaders on demand: r_ShadersAllowCompilation 1). Detail distance:
  e_TerrainLodRatio divided (down to 1/8; sector LOD from distance x it, +0x150fa3), e_TerrainDetailMaterialsViewDistZ
  128 (up to 2048) / XY 2048 (up to 16384) in memory (VF_CHEAT, 3D engine cvars +0x5c8 / +0x324). Draw distance:
  r_RainMaxViewDist_Deferred 150 (up to 2000).
- Simulation distance also: e_MergedMeshesDeformViewDistMod 0.45 (grass touch bending, up to 1), es_FarPhysTimeout 4,
  e_FoliageBranchesTimeout 4, g_crowdEmotionUpdateTimer 0.25 (divided, down to 0.02). Decals (now "Decals and
  debris"): es_DebrisLifetimeScale, g_breakageFadeDelay 6, g_breakage_particles_limit 160 (up to 2560).
- Looked at, left out: r_EnvTexResolution (3 = 512 is the code's largest, other values give 256), r_WaterUpdateFactor
  (already 0: water reflections every frame), e_CoverageBufferResolution (read when the buffer is made, 1-1024),
  ca_cloth_* (ignored while ca_cloth_vars_reset is 2, VF_CHEAT), physics solver iterations (not visible).

## v73: Particles per kind, no Global illumination

ATN's v72 test: Global illumination shows no difference (removed). Particle count (e_ParticlesLod) needed a loaded
save; 4x is "really nice" but too much for blood and smoke, fire arrows "INSANELY good": one slider per kind wanted.
- e_ParticlesLod is used once, in the emitter's emit count scale (+0xf5e50, EMIT_COUNT_SCALE_CODE): emitter +0xa4 =
  e_ParticlesLod (3D engine cvars +0x408) x the spawn count scale (+0x90), x the attached geometry's size^1/2/3 for
  counts per unit (+0x88 mode, +0xf8 size), computed when the emitter starts or restarts (+0xf8140, e.g. emitter
  vtable slot 75 +0xf5260). The emitter (vtable +0x1ecbf30; slot 10 returns "ParticleEmitter") keeps its top effect at
  +0x60; its GetName (slot 9, +0xf4b90) is the effect's vtable +0x30.
- particle_counts.hpp: a Detours hook on it multiplies +0xa4 by the factor of the effect's kind, from its name
  (lowercase; checked in this order): blood (blood, gore, bleed, wound, decapit, dismember, gib), sparks (spark, ember,
  cinder), fire (fire, flame, burn, torch, brazier, candle, lava, napalm), smoke and dust (smoke, steam, fog, mist,
  vapo, dust, ash, cloud, haze), other. The game names effects Library.Group.Effect (e.g.
  Character.Weapons.arrows_trail_fire). Experimental options Fire / Spark / Smoke and dust / Blood / Other particles,
  1.5x-8x. Advanced: "Save particle effect names" writes every top effect seen with its kind
  (ryse_particle_effects.txt) to refine the words.
- Cloth cutting (ATN: does it work?): g_avatar_cut_cloth is read at +0x14fe919: on a "slash" hit the game asks the
  physical world for entities in a box around the blade (vtable +0x100, up to 16), and for soft bodies (type 8) and
  ropes (type 7) runs a slice action (+0x38). It cuts physics cloth and ropes in levels; the soldiers' clothes are
  CryAnimation cloth (no physical entity) and are not cut.
- Other switched-off features whose code still runs (Experimental): Leg colliders (g_avatar_legs_colliders_dist 0 ->
  50, +0x14e24c0), Physical hit reactions (ca_physicsProcessImpact, CryAnimation console +0x16c, read at +0x8cad01 /
  +0x8d4d2c), Explosion craters (e_TerrainDeformations, 3D engine cvars +0x30c), Deformable objects
  (e_DeformableObjects +0x238), Terrain sun shadows (e_GsmCastFromTerrain, +0x1c4). Left out: ca_UseDecals (blood on
  characters; CryAnimation console +0xc0 is never read: compiled out), g_avatar_interest_module_enabled (Avatar
  CInterest: target selection weights, gameplay).

## v74: Particles per effect part, no leg colliders

ATN's v73 test: Leg colliders make characters fly (removed). Fire 8x also gave fires and torches far too much smoke:
the smoke is a part (sub-effect) of the fire effect, and v73 scaled whole emitters by the top effect's name. Names
seen (debugForClaude28): "ash" matched slash / bash / splash.
- particle_counts.hpp now scales each part: the particle params (type info built at +0xddf1f: fCount +8, sTexture
  +0xa0, sMaterial +0xc0) of every container the container spawn (+0x101010; container = [first argument], params +8,
  main emitter +0x100; the spawn count is params count x the emitter's count scale +0xa4) sees. The hook writes the
  params' count = the game's x the factor of the part's kind, from its texture and material, else its top effect's
  name, and keeps the game's count while the params hold what it wrote (another texture at the same address:
  classified again). Applies at once, running effects too. The emit count scale hook is gone.
- Words now match a name's words (split at non-letters and lower -> upper case): the start of a word, or anywhere in it
  from 4 letters. Blood also: spurt, execution, excecution (the game's spelling). Fire: braz (Arrow_brazer).
- "Cloth cutting" is now "Sword slashes cut banners and ropes".

## v75: chapter-change crash, fire lights, plant motion

ATN's v74 test (debugForClaude29):
- Crash on a chapter change (Ryse.exe.17080.dmp): the addon's Present called GetFVal on a deleted ICVar (entry 3 before
  the end of Simulation distance's cvars: g_crowdEmotionUpdateTimer). The level's crowd (CCrowdReaction, built at
  +0x147a2a0 by a per-level object, +0x1666010) registers it and a chapter change deletes it. Left out. The other game
  cvars the options use come from CCryAction::Init (+0x5ff2d0, +0x605050) or the game's cvar setup (+0x1622000 ->
  +0x1484ed0): registered once.
- Fire above 2-4x made lights blink. The tiled lighting takes at most 255 lights per frame (+0x18938b4: cmp r12d, 0xff;
  the skipped count at +0xe8, "Skipped Lights" in r_DeferredShadingTiled 3) and skips the rest, others each frame.
  Every particle of params with a light source adds a light (+0x10dfc0, called per particle of a container with lights
  by +0xd7a50; particle +0x80 = its container). particle_counts.hpp hooks it: a particle keeps its light while a hash
  of its address is below the game's count / the hook's count, so the game's number of lights and their brightness
  stay, the same particles every frame.
- Plants moving "like 24 fps": ATN's trace (ryse_pixel_trace.txt) hit a plant drawn from two 16-byte streams (position
  + tangents), no skinning stream: merged-mesh vegetation. The game's vegetation vertex shaders have no time-based
  wind (e.g. 0x012B518E: BendInfo only); merged meshes bend on the CPU (spines). The merged-mesh render node (+0x81d70)
  simulates a node again only when frame - its last update ([node + 0x54]) > {3, 5, 7, 11}[LOD] (mov [rbp + 0x30..0x3c]
  at +0x8204d): the nearest plants every 4th frame (25 Hz at 100 fps), farther ones every 6th / 8th / 12th. Each update
  hands its job (+0x74180, clamps to 0.08 s) one frame's time (+0x8178b). New option "Plant motion" (plant_motion.hpp,
  every preset): the intervals 0 and, in a stub, the time step x 1 / (interval + 1) of the node's LOD ([node + 0xb8]):
  the same motion speed as the game's, every frame.

## v76: plant motion removed, particle lights held under the limit

ATN's v75 test: no crash on chapter changes. "Plant motion: Every frame" made grass far slower ("like 1 fps"); trees
unchanged (another system). Fire above 1.5x still blinked with many fire arrows.
- Plant motion removed (plant_motion.hpp deleted). Why it cannot work this way: each dispatch of a merged-mesh node
  (+0x81d70) first waits for its jobs and clears its groups' job entries (+0x80640), and the update (+0x808c0, from the
  manager's pending list +0x86e50 -> +0x83660) rebuilds them; the spine simulation (+0x6e420) steps a fixed 1/30 s with a
  per-spine accumulator (+0x6f1e4: [spine + 0x10] ms x 0.001 + dt; while > 1/30: step; kept in [0, 0.0343]). So the
  grass moves in 1/30 s simulation steps whatever the update rate; the game's intervals ({3, 5, 7, 11} frames by LOD
  0-5 & 3, LOD = clamp(dist^2 / (size^2 x e_MergedMeshesLodRatio), 0, 5)) with one frame's dt per update run it at
  1/4-1/12 of real time. Smoother motion at the game's speed would need a smaller fixed step, which changes the
  springs' behaviour (stiffness / damping per step).
- Particle lights: child effects multiply the light-carrying particles too, so the per-params share was not enough.
  particle_counts.hpp now also hooks the tiled light list (+0x18908d0, this = renderer +0xd550): after it, [this +
  0xe8] = the lights it skipped and the cull info array (+0x2b831a0 via `lea rdi; cmp r12d, 0xff` at +0x18938ad, 80-byte
  entries, volume type 0 = the end) gives the lights taken. Particle lights rank by the same per-particle pick; only
  the lowest lit_buckets (of 256, by the pick's top byte) light. Skipped lights: the highest lit buckets go until their
  candidates cover the skipped count + 8; else one bucket comes back while its candidates fit with 8 lights free. The
  same particles keep their lights from frame to frame. Also active at the in-game particle counts (holds the game's
  own overflow too).
- The 255 itself: static arrays (cull info 255 x 80 B at +0x2b831a0, 24 code references; shade info 255 x 256 B at
  +0x2b88150, 5), GPU buffers uploaded at those sizes (+0x1893a99), the lighting cs 0x897232B5 (4 rounds of 64 threads
  over 255 slots, groupshared per-tile lists [255]; replaced by the HDR addon) and the forward tiled shaders. Raising it
  means moving both arrays, larger buffers and a new cs in both addons: not done.

## v77: raised light limit (Experimental "Light limit": 1024)

ATN: raise the 255 (go back if it breaks). Everything the limit lives in:
- The light list (+0x18908d0, the tiled shading object = renderer +0xd550) fills two static arrays, cull info (255 x
  0x50 B, +0x2b831a0) and shade info (255 x 0x100 B, +0x2b88150), clears both whole (memset 0x4fb0 / 0xff00), stops at
  255 (cmp r12d, 0xff at +0x372, the end entry +0x2fe4, the sun +0x304c: it is appended last and dropped when 255
  lights came first; a shadow-casting light's entries cmp eax, 0xff +0x23c5) and uploads both whole (+0x31cd, +0x31df)
  into the object's GPU buffers (+8 cull, +0x28 shade; the game's Create +0x17b5330(buffer, count, stride, format,
  flags, data), count at buffer +0x18), made once for 255 in +0x1893eb0. 11 rip-relative leas reach the arrays, all in
  the light list (a byte scan over the code for operands into both ranges). particle_counts.hpp: LIGHT_LIST_SITES (19
  operands, each checked before use), 1024-entry arrays from AllocateNearExe; in the light list hook (render thread)
  the buffers are made again by the game's Create at 1024 and the operands patched, reverted in the other order. The
  hook checks the buffers' count every frame (the game makes them again on a resolution change).
- The lighting cs 0x897232B5 (8x8 threads per 8x8-pixel tile) culls the cull info in 4 unrolled rounds of 64 (index
  < 255; a zero volume type ends the list), appends to two groupshared lists [255] (opaque g2, transparent g3, counts
  g0 / g1), sorts their first 64 (bitonic, empty = 255), writes the transparent list to an R8_UINT buffer (255 per
  tile, 255 = empty; read by the forward shaders), then shades 3 loops over the opaque count (probes, types <= 4, the
  rest, the list's order by index = by type). tiled_lights_cs.h is that shader edited in its disassembly (3Dmigoto
  cmd_Decompiler -d / -a --copy-reflection; byte-identical round trip): the opaque empty marker 0xFFFFFFFF (light
  indices above 255 sort before it), round 4's registers saved (it reuses r0.zw, r2.x, r2.z, r4.x), then after the
  game's rounds a barrier and a loop over lights 255-1023 (round 2's culling, opaque list only, at most 255 per tile),
  and the 3 shading counts clamped to 255. Tools: tools/ryse/tiled_lights_raise.py (the edit), tiled_lights_test.cpp
  (WARP: the game's vs the edited shader on random lights: 40 lights bit-identical, 300 with 40 inside identical, 10
  lights at 255-264 identical to the same at 30-39, they change the image vs without, 600 everywhere runs),
  bytes_to_header.py. OnLightingDispatch swaps it in while the limit is raised.
- Particle lights: the budget (v76) uses the current limit and array.
- Unload leaves the patches (the arrays are never freed, the buffers stay 1024): consistent without the addon.

## v78: light limit 1024 / 4096, also per pixel block; the light budget removed

ATN's v77 test (debugForCladue30: "raised light limit ready"): no crash; blinking now starts between Fire 4x and 6x
(before: from 1.5x). The cause: each 8x8-pixel tile still held 255 lights. Besides the groupshared lists [255] and the
count clamps, each of the 3 shading loops reads its next entry at min(i, 254) (imin ... l(254)): past 255 lights a tile
shaded its last entry again and again. tiled_lights_cs.h (tools/ryse/tiled_lights_raise.py, 4096): the opaque list
4096 (groupshared 16 KB + the transparent 1 KB, of 32 KB), the per-tile guard, the 3 count clamps and the 3 next-entry
clamps at 4096 / 4095; the transparent list stays 255 (8-bit buffer). WARP: the earlier tests alike; 600 lights in every
tile now all shaded (differs from the game's 255), the same twice; 4000 run.
- Light limit: In-game (255) / 1024 / 4096; the arrays are made for 4096, the sites' operands (limit, bytes) built for
  the chosen limit each time it changes (the game's code back first, then the buffers, then the new operands).
- Below the slider: the last frame's lights taken of the limit and dropped ([list + 0xe8]).
- The v76 light budget (particle lights held under the limit by their pick) is removed (ATN: not needed with the raised
  limit); the per-params light share (v75) stays.

## v79: light limit removed; the game's particle limits raised

ATN's v78 test (debugForClaude31 / 32, 4096 lights, every particle option 8x): the counter stays under 255 lights in
every frame, torches still vanish, come back and flicker, and 1024 / 4096 alone cost 20-30% fps (likely the 16 KB
groupshared list per tile: fewer tiles in flight). With the v75 share particle lights stay at the game's count, so the
light limit is never reached: removed (tiled_lights_cs.h, the light list patches and buffers, OnLightingDispatch).
The light list hook stays for the counter. The flicker is the game's two particle limits:
- Live particles: each takes a 256-byte block from a pool made once with the particle system (e_ParticlesPoolSize
  12288 KB at +8 of the 3D engine's cvars: 49152 blocks; getter +0x1007f0, pool +0x2deae80: SLIST_HEADER, base +0x10,
  block size +0x18, blocks +0x1c). The particle allocation (+0xd5b90, one caller +0x1050f7) pops a block and returns
  null when the list is empty: no particle. Freeing pushes the block back (+0xd9dcf, +0xda0ea); nothing checks a
  block's address. particle_counts.hpp hooks the allocation: on null it pushes the game's pool size once more (12 MB,
  InterlockedPushListSListEx), up to 15 times (16x).
- Particle vertices per frame: r_ParticleVerticePoolSize (15360, +0x22ba514; 3 buffers of 44-byte vertices and 16-bit
  indices, 3 per vertex, made by +0x17ee7b0 from the render thread's init +0x17fa9a0 once the device exists). Each
  effect part (CREParticle) takes its vertices (+0x1823cd0, an atomic add per frame buffer); when they do not fit it
  returns false and the part is not drawn that frame: whole effects blink. The base vertex goes to the render object,
  but whether the written indices count from it is not proven: the addon sets it to 65535 at the game's device creation
  (init_device; the cvar is registered before), the most 16-bit indices reach either way (4.3x).
- Below Other particles: the last frame's lights (taken / skipped), effect parts not drawn and particles not spawned
  (last frame and in all), and the particle memory (1x + the growths). e_ParticlesEmitterPoolSize (6144 KB, sub-emitters,
  +0x100730) is left as it is.

## v80: particle vertex counter, particle memory 16x from the start (test)

ATN's v79 test: the sliders go much higher now (not all at 8x at once: fine); the 65535 vertices stay. Particle memory
showed 2x-3x.
- Below Other particles also: particle vertices X of Y, X = the most a frame buffer held (after each fitting part the
  renderer's bytes used for buffer frame % 3, renderer +0x5d30 + 4 x buffer, renderer from the vertex allocation's mov
  rdi at +0x1e; its most since the last light list), Y = the buffer's size (renderer +0x5d48 / 44: shows the raise
  took), and the vertices of the parts that did not fit.
- Particle memory: all 15 growths at once when the hooks install (16x, 192 MB; ATN's test). The on-demand growth stays
  (no-op at the most).

## v81: particle vertices raised when the buffers are made

ATN's v80 test (debugForClaude33): the counter showed 30720 vertices. The log: init_device 1 saw 0 (the cvar not yet
registered), 2 saw the addon's 65535, 3 (the game's device) 30720: the game's settings set r_ParticleVerticePoolSize
30720 (2x the default), and again after device 3, before its render thread init (+0x17fa9a0) made the buffers.
Now a detour on the buffers' maker (+0x17ee7b0, PARTICLE_BUFFERS_CODE; this = the renderer, one argument) sets 65535
right before it reads the cvar (logged "Particle vertex buffers made: 30720 -> 65535"). Installed at init_device
(each addon load; removed with the others on unload). 65535 is 2.1x the game's 30720.

## v82: Particle memory option, particle sliders up to 4x, Experimental warning

ATN's v81 test (debugForClaude34): 65535 shown; the log has the buffers made 3 times (one from the main thread at
device creation, two on the render thread), each after the game set 30720 again, each raised. At high sliders some
parts still did not fit.
- Fire, Smoke and dust, Other particles: up to 4x (saved higher items clamp to 4x); Sparks and Blood keep 8x.
- New Experimental "Particle memory" (Off / 8x / 16x) above them: the pool grown to that many times the game's when the
  hooks install or the option rises (SetParticleMemory, Present thread; never shrunk). The on-demand growth (v79) is
  gone; the allocation hook only counts missed particles. Off: the particle sliders are greyed out and set back to the
  in-game setting (saved).
- A warning above the Experimental options: heavy, may not work; blinking particles: lower the sliders (the vertex
  buffer); missing particles: raise Particle memory.

## v83: Particle memory Off / 16x, Experimental Reset and ATN's setting

ATN's v82 test (debugForClaude35: the buffers raised as before, nothing new): with every slider at its highest some
particles still blink or go missing (the 65535 vertices per frame, kept).
- Particle memory: Off / 16x (a saved 8x loads as 16x).
- The warning: "These can be heavy and may not work. If particles blink or go missing, lower the particle sliders."
- Under it two buttons for the Experimental options only: Reset (each to its first item, the in-game setting / off)
  and ATN's setting (QualityOption::atn_choice: every switch on, Particle memory 16x, fire 2x, sparks 8x, smoke and
  dust 1.5x, blood 4x, other 1.5x). Both saved.

## v84: Detail limit (LOD) option

ATN: e_LodMax 0 (with Detail distance 64x since the shield / armour fix) is heavy on some maps; a slider of its own.
- New option "Detail limit (LOD)" under Detail distance: e_LodMax In-game (6) / 3 / 2 / 1 / 0 (written in the 3D engine's
  cvars +0x41c, VF_CHEAT). 0: the most detailed model for everything, also attachments with fixed LodDistances.
  Presets leave it at the in-game setting; MAXED OUT: 0.
- Detail distance no longer sets e_LodMax (64x before): a saved 64x needs Detail limit 0 for the same result.

## v85: release polish (Simple / Advanced, tooltips, presets' Detail limit)

- Settings mode: "Advanced (For debugging and extra information)". Simple shows settings and warnings only: the particle
  counts (lights, particle vertices, parts not drawn, particles not spawned) and the frames-shown-by-the-game count
  moved to Advanced. The counting hooks count only while Advanced is open (particle_counts KeepCounting from the
  overlay, 120 frames; otherwise they pass straight through).
- Per-frame cost of the Advanced tools with the overlay closed: none beyond a few atomic loads per draw (traces,
  captures, shadow trace and option measure run only when started; the GPU timers only within 120 frames of the
  overlay drawn in Advanced).
- Tooltips: no code addresses, test notes or matching rules (Draw / Detail distance, Tessellation, Water, both
  streaming options, the three switches, the particle options, Character animation).
- Presets set Detail limit: Very High in-game, Ultra 3, Extreme 2, Insane 1 (MAXED OUT 0).

## v86: Experimental heading

"Experimental: the presets above leave these alone; only the buttons below change them."

## v87: ATN's setting blood 3x

ATN's setting: Blood particles 3x (was 4x). ATN: Detail limit changes no fps with Detail distance at 64x: expected,
64x already puts nearly every object at its most detailed model; the limit then only reaches attachments with fixed
switch distances (shields, armour).

## v88: overlay texts (when settings apply, Experimental switches)

- The game settings' heading: most apply at once; some only after a level load or a restart (texture streaming,
  water), detail can take a few seconds to stream in.
- The Experimental warning: the on / off switches are features the game shipped off; they may not work or may crash
  the game, turn them off if something goes wrong.

## v89: Detail limit applies at once

ATN: at MAXED OUT (DLSS 14%, CPU-bound) Detail limit changed nothing at game start (130 fps); after switching through
the presets and back, Detail limit 6 gave 240 fps and visible changes. The cause (CryEngine's SFrameLodInfo): the
3D engine's OnFrameStart (+0x17cb10) hands it {ID, e_LodRatio, e_LodFaceAreaTargetSize, e_LodMin, e_LodMax, ...}
every frame through SetFrameLodInfo (+0x17f1b0, vtable +0x210; the engine's copy at +0xbb0); it bumps the ID only when
the ratio or target size change, and objects take the min / max LOD again only on a new ID. So e_LodMax (Detail limit)
applied only after Detail distance changed (presets) or a level load. A detour (addon.cpp SET_FRAME_LOD_INFO) bumps the
ID when the min / max LOD change too. The v87 note (64x already at full detail) was wrong: Detail limit 0 is heavy
(more detailed models, more draws: 130 vs 240 fps CPU-bound); the Detail distance tooltip no longer says so.

## v90: v89's hook removed; Detail limit explained; the settings heading in color

ATN's v89 test: Detail limit still did little until the in-game graphics preset was switched and back. e_LodMax is
read only by the frame LOD info builders, and the only users of the info's min / max LOD are the attachments (the
attachment LOD, +0x881950, from the engine's live GetFrameLodInfo every frame, +0x8835cd / +0x8e04ee): world objects'
LOD (GetObjectLOD +0x5f580: the object's LOD distances, cached per info ID, +0x16a990) is not clamped by it. So
v89's ID bump changed nothing and is removed. Attachments without their own LodRatio / LodDistances keep their last
LOD ([attachment + 0xc1], written only by that update and a copy) and only clamp it: lowering the limit applies at
once, raising it again only once the game rebuilds the characters (level / checkpoint load, an in-game graphics preset
switch). The tooltip says so (soldiers' armour, shields and weapons; CPU heavy with many soldiers). The heading above
the game settings is drawn in the Experimental warning's color.

## v91: Detail limit folded into Detail distance; shorter tooltips and texts

- ATN: two LOD sliders confuse. Detail distance sets e_LodMax again: In-game / 2x the game's, 4x 4, 8x 3, 16x 2, 32x 1,
  64x 0 (the presets' pairs of v85-v90: Ultra 8x / 3, Extreme 16x / 2, Insane 32x / 1, MAXED OUT 64x / 0). The
  "LodMax" option and its saved key are gone.
- Every option tooltip and the overlay texts shortened (no cvar lists); Water no longer says it needs a level load
  (ATN: it applies almost at once).

## v92: six more Experimental switches; heading texts

- Experimental (untested; ATN's setting turns them on): Depth of field quality (r_DepthOfFieldBokehQuality 1), High
  quality shadows (e_ShadowsHighQualityMode 1-3: extra cascades / fitted to the depth of field / to last frame's
  scene), View-aligned shadows (e_GsmViewSpace 1), Indirect light (r_SSII 1), Grass tessellation
  (e_MergedMeshesTesselationSupport 1), Enemies skip their taunt (agent_skipTauntBeforeAttack 1, read before each
  attack at +0x146a571; VF_CHEAT: QualityCvar::static_int, written at the static FindIntCvar finds). All flags 0
  except the last (console sets).
- The orange heading: "(brackets: at Very High)" removed; a second line: the heaviest options (Screen-space
  reflections GPU, Detail distance CPU and GPU).
- AI: who attacks when is the enemy attack manager's data, Scripts/AI/AttackPatterns/*.xml (Sequences, AttackTypes,
  Attackers, AttackerConstraints, e.g. MoreThan / LastManStanding / DistanceToTarget; console ai_reload_attack_manager;
  loader near +0x145df80), inside the encrypted Scripts.pak: no cvar for the number of attackers.

## v93: save the enemy attack patterns (Advanced, debug)

ATN: read what the game uses before deciding on more aggressive enemies. The attack manager loads
Scripts/AI/AttackPatterns/*.xml from the encrypted Scripts.pak once when it starts (its Init: vtable +0x1dff058 slot 1,
+0x145edb0 -> +0x145e3a0 -> each file through +0x145df80, which calls ISystem::LoadXmlFromFile, vtable +0x348). Its
reload command ai_reload_attack_manager (handler +0x145ee00) is VF_CHEAT: IConsole::ExecuteString (vtable +0x110)
refuses it ("[EXECUTE] command %s is marked [VF_CHEAT]"). attack_patterns.hpp: the Advanced button sets "Ryse AA"
SaveAttackPatterns; at the next start the loader is hooked at the device's creation (before the game starts its
systems) and copies each file it reads through the game's CryPak (gEnv->pCryPak = gEnv->pSystem - 0x78; FOpen +0x110,
FReadRaw +0x120, FClose +0x1a8; FGetSize is +0x160, not used: read in chunks) into Bin64\ryse_attack_patterns, then
clears the flag. Text or binary XML (CryXmlB) as stored.

## v94: High quality shadows and View-aligned shadows removed (Experimental)

ATN: with both on, distant shadows disappear and shadows look worse than before. e_ShadowsHighQualityMode (3D engine
cvars +0x53c) is the game's cutscene shadow mode forced in gameplay: each frame (+0x1c38c3) the engine copies its
shadow cvars into the frame's settings, and in the cutscene mode (+0xac0 == 1) or with this cvar at 1 it takes
e_GsmLodsNum (+0x50c) at least 8, e_GsmRange (+0x3c4) at most 0.8 m, e_GsmRangeStep (+0x338) at most 2 and the HQ
biases (e_ShadowsConstBiasHQ +0x538, e_ShadowsSlopeBiasHQ +0x380): 8 small cascades ending near 100 m, instead of
Shadow cascades / distance's far ones. 2 fits them to the depth of field focus (Dof_FocusDistance / Range, +0x246f40)
and 3 to the last frame's scene: cutscene fits too. e_GsmViewSpace (+0x444) only sets each cascade frustum's flag
(+0x91, +0x2485e5 / +0x248dab); shadows disappear with it in ATN's test: cut. The attack patterns: one file,
Scripts/AI/AttackPatterns/enemyattackpatterns.xml (CryXmlB), saved when a level loads (not at the menu).

## v95: Near shadow sharpness (Experimental)

The cutscene mode's good part without its short reach. Cascade i reaches e_GsmRange * e_GsmRangeStep^i (the game: 3 m,
step 3, 5 cascades: 3 / 9 / 27 / 81 / 243 m; ryse_cvars.txt). 2x / 3x / 4x divide e_GsmRange (1.5 / 1 / 0.75 m) and
take e_GsmRangeStep x the factor's (cascades - 1)th root (QualityCvar::cascade_root, e_GsmLodsNum read at 3D engine
cvars +0x50c after Shadow cascades applies): every cascade gets smaller, the last one keeps its reach. 8 cascades, 4x:
0.75 / 2.7 / 10 / 37 / 134 / 491 / 1797 / 6561 m. A distance just past a new cascade's edge falls into the next, larger
one: softer there. ATN's setting: 3x.

## v96: Near shadow sharpness removed

ATN: it looked worse than before. The engine's only cutscene graphics switch is the shadow mode (3D engine +0xac0,
SetShadowMode vtable +0x630 at +0x18aa40, getter +0x638): TrackView camera nodes set 2 (cascades fitted to the shot's
focus, 3D engine vtable +0x4f0 first), a flow node sets 1. ATN's cvar dumps in gameplay and in a cutscene
(debugForClaude36) are identical: cutscenes use the same settings as gameplay.

## v97: Terrain sun shadows removed; ATN's setting leaves Skip taunts off

ATN: terrain shadows (e_GsmCastFromTerrain) are bugged in some chapters.

## v98: DLSS input capture

For what flickers while paused (ATN; maybe the reflections): Advanced, "Capture DLSS inputs" saves the next 4
evaluations' inputs raw, at DLAA too (dlss_capture.hpp, as twshogun2_dlss's capture that found Shogun 2's trees and
banners): ryse_dlss_capture_<frame>_<color / motion / depth>.bin (header: width, height, DXGI format, bytes per texel;
then the rows; color R16G16B16A16_FLOAT, motion R16G16_FLOAT in pixels, depth R32_FLOAT) and ryse_dlss_capture.txt.
Paused, every pixel's motion should be 0: the ones that are not show the surface DLSS drops each frame; color changing
between the frames with 0 motion shows an effect that itself changes (noise, the reflections' jitter).
tools\shogun2\capture_decode.py reads both games' captures.

## v99: DLSS gets the depth

ATN's paused capture (debugForClaude38): motion 0 everywhere (max 0.07 px), the image moving exactly by the jitter DLSS
is told (within 0.02 px), but DLSS's depth was 1 on every pixel: the near plane, no depth at all. The preparation
pass made it from PER_BATCH's PS_NearFarClipDist (near / (linear depth x far)), which the game's TAA (0x121C45E4)
declares but never reads, so the engine leaves it unset (paused for certain; likely in play too, since v21): no depth
edges for DLSS's history, thin details (columns, foliage, plumes) left to color alone. 0x52594501 now writes the
depth buffer's value for the reconstructed surface with this frame's camera (g_VS_ViewProjMatr's clip z / w), reverse-Z
for DLSS's DepthInverted flag (the camera's convention measured on the CPU: z / w 1 and 1000 units ahead).

## v100: Highlight stability

ATN debugForClaude39 (standing still; v99's depth now varies, motion 0): the armour flickers from sub-pixel glints that
only some jitter positions hit: in 16 px blocks the brightest pixel jumps 0.2 -> 2.3 -> 0.2 -> 2.2, or 0.4 -> 11.4 in one
of 4 frames, against 0.13 around it. With the HDR addon they reach many times paper white (the game's 8-bit SDR clamped
them at 1), and DLSS lets the rare bright samples through. "Highlight stability" (DLSS preset's line; Ryse AA
HighlightStability): the preparation pass compresses the brightest channel above a knee (knee + d / (1 + d)), the copy
pass expands it exactly (highlight_stability.hlsli): a steady image is unchanged, a glint seen in some frames counts
like a soft highlight. Steadier (default): above 1 (paper white); Steadiest: all (Reinhard); Off: as v99.

## v101: Highlight stability removed

ATN: not wanted (the image's brightness changed with it). The armour's glint flicker stays open: the glints are thinner
than a pixel, so the camera jitter DLSS needs hits them in some frames and misses them in others (lighting, depth and
motion are still). While paused, Jitter "The game's" stops the jitter (no flicker, a little softer).

## v111: NVIDIA's DLSS notice

- NVIDIA_DLSS_NOTICE.txt in the zip (the DLSS SDK's licence asks for its notice and attribution); nothing else changed.

## v110: Light limit 512, Particle lights removed; G-buffer filter texts, DLSS settings coloured

- ATN: the Light limit alone costs no fps: default, Reset and ATN's setting 512. Tooltip: video memory about 1.4 MB
  more at 4096 (the two GPU light buffers, 4096 x 0x150 B; the arrays near the exe, 1.4 MB, are made either way).
- Particle lights removed (ATN: Every particle crashed the game, mid-game or fresh start, even at 4096); with it
  QualityOption.own_choice.
- G-buffer filter: Off (Recommended, better highlights but less stable) / On (Vanilla, weaker highlights but more
  stable), tooltip to match.
- The DLSS section's settings in its heading's lavender (B39DDB).

## v109: Light limit back (each tile 255), Particle lights

- ATN, chapter 5: lights flicker at the game's 255-light limit (20-40 dropped). Light limit (Experimental, yellow):
  In-game (255) / 350 (default) / 512 / 1024 / 4096. v78's code back (light list sites, arrays near the exe, buffers
  made again, the addon's lighting cs while raised), but each 8x8 pixel tile keeps the game's 255 (its groupshared list
  and shading loops as the game's; v78 raised those too: 20-30% fps). The extra culling loop stops at the list's end:
  the cost grows with the lights in view, not the limit. Shader: tools/ryse/tiled_lights_raise.py LIGHTS=4096
  PER_TILE=255, WARP-tested (identical up to 255 lights, 600 lights in one tile, 4000 lights run).
- Particle lights (Experimental, yellow): The game's (default: more particles keep the game's light count) or Every
  particle. No button changes it (QualityOption.own_choice).
- QualityOption.default_choice: the item before one is saved and the Reset buttons' (Light limit 350). ATN's setting:
  350 too. The graphics presets leave Experimental options alone. The cost of 4096 vs 350 was not measured: Advanced >
  Measure every option's choices includes the Light limit (frame ms).

## v108: Audio, longer SSR, G-buffer filter choice

- Language section renamed Audio; Dynamic sound mixing at its bottom (QualityOption.audio: drawn there, no preset
  button changes it; the options' slider drawing is one local lambda, option_slider).
- Screen-space reflections: Extreme without "experimental"; two more, Extreme 2x / 3x range (Experimental): 16x / 24x
  the game's ray length with 64 / 96x its steps (the same 4x per metre). Not in any preset.
- G-buffer filter (DLSS section): Off (default, as before: r_DeferredShadingFilterGBuffer 0 with DLSS / DLAA) or On
  (the game's, as with TAA; RendererPin.keep_game; ReShade.ini Ryse AA GBufferFilter). A note under it: DLSS / DLAA
  also turns anisotropic filtering on for alpha-tested textures (r_TexNoAnisoAlphaTest 0).

## v107: jitter while paused back

- v106 reverted (ATN: dead, it got worse): the addon's own jitter skips the game's pause check again (as v105).

## v106: no jitter while paused

- ATN (debugForClaude41: dead / paused, DLSS and DLAA: the whole armour shimmered; TAA still): the addon's own jitter
  also skipped the game's pause check (bit 27 of the renderer's per-frame flags, where the jitter is added to the
  projection), so paused frames kept jittering on sub-pixel glints. That check stays now (the JITTER_APPLY nop patch is
  gone): a paused frame is not jittered and DLSS keeps it still. Slow motion and hit stops keep the own jitter (the
  setup stub, as before). DLSS gets the jitter measured in the frame: 0 there.

## v105: coloured headings

- Overlay headings (ATN, as Shogun 2's DLSS addon): DLSS lavender, Graphics and Graphics (Experimental) pink, Language
  and About green.

## v104: voice and text language

- Voice language / Text language (Simple view, saved in [Ryse Game Settings] VoiceLanguage / TextLanguage):
  g_languageAudio / g_language exist (ATN's ryse_cvars.txt, string cvars, flags 0x10080). The game fills them from the
  platform's language only while they are empty (+0xee69fa / +0xee6aa5), and their change callbacks reopen the language
  packs at run time (+0xede6a0: closes the old Localization/<language>.pak, opens the new; +0xede5e0: the texts and
  their reload). Set (const char*) = ICVar +0x48 (the same start code). Applied from the present thread once the
  console exists, when a choice changes; "In-game setting" puts the game's value back. From the next scene / loading.
- (Gemini's list, checked: s_CompressedDialog only keeps dialog compressed in memory, s_Compression picks a decoder
  (PC: MP2) for files that exist in one format; neither changes the dub's quality. s_HDR is v103's switch.)

## v103: sound mixing, links

- Experimental "Dynamic sound mixing" (s_HDR, CryEngine's HDR sound: the loudest sound lowers quieter ones, voices too,
  heard as compression; ATN: the German dub). Off: every sound at its own volume. The dub itself: German.pak is 333 MB
  against English.pak's 515 MB, its files are encoded smaller (s_Compression: PC prefers MP2), not changeable.
- Voice / text language apart: g_language / g_languageAudio are not registered as cvars here (only names in the
  facial animation code); the language paks open at start (Localization/<language>.pak voices, <language>_xml.pak
  texts). Workaround without the addon: copy English.pak over German.pak (backup first) for English voices with German
  texts. An in-addon switch would need a hook on the pak open.
- Ko-fi + NexusMods buttons at the end of the Simple view (tinted as the HDR addon's).

## v102: less CPU

- The draw and copy events of the debug traces (frame trace, shadow trace, options measurement) are registered only
  while one runs (before: every draw and copy of the game went through them).
- Constant mirror: until the TAA pass's constant buffers are watched, every map, update, copy, new and destroyed
  resource returns at once (no GetDesc, no lock).

## v112: Frame generation (Experimental, untested in game)

- NVIDIA's DLSS Frame Generation + Reflex through Streamline, Tales of Arise's (talesofarise_dlss/frame_generation.hpp,
  ported from Phantom FX by Bustanity, MIT: PHANTOM_FX_LICENSE.txt), now shared: the game's swap chain is replaced by a
  stand-in whose back buffer is copied into a D3D12 swap chain Streamline presents (DXGI's CreateSwapChain and
  CreateSwapChainForHwnd hooked behind ReShade), DLSS's depth and motion copied each frame, the camera given per game.
  Setup as Tales': copy `sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.reflex.dll`, `sl.pcl.dll` and
  `nvngx_dlssg.dll` (Streamline SDK 2.14.16, `bin\x64`) next to the addon, tick **Frame generation (Experimental,
  restart)** and restart the game; modes 2x-6x, Dynamic, Off (Reflex only), Reflex Boost. RTX 40 or newer, hardware
  GPU scheduling, HDR10 or SDR output (no scRGB). The stand-in path is GPU-tested with fg_test (CreateSwapChain and, FG_HWND=1,
  CreateSwapChainForHwnd); untested in this game.
- The camera from CryEngine's view-projection (PER_FRAME c0-c3, column vectors, its jitter in rows 0 and 1 through row
  3): the jitter taken out, scaled so the forward row has unit length, the projection's x / y scales, depth terms,
  near and far (reversed depth) and the right / up / forward axes split out, and last frame's unjittered
  view-projection for clip-to-previous-clip (FrameGenerationCamera; tools\ryse\fg_camera_check.py checks the math on a
  known camera: all OK). The HUD-less image after the post pass (the game's at 100%, the addon's at the display size
  below), through the HDR addon's swap chain shader.

## v113: one-frame trace

Debug: `renodx_trace_now` next to Ryse.exe traces the next frame (Tales' frame_trace.hpp, checked each second, the file
removed): tales_frame_trace.txt (every draw, dispatch, clear and copy with shaders, targets and inputs) and the traced
shaders' bytecode (when an addon keeps it). Found the sun shadow mask's cascade passes for the HDR addon's fix50.

## v114: frame generation's frame rate limit, resize fix

- **Frame rate limit** (frame generation settings, 0-240 fps; ReShade.ini FrameGenerationFrameLimit): NVIDIA Reflex's
  cap for the rendered frames (frame generation multiplies them). A cap the game always reaches keeps the frame rate
  steady: less VRR flicker on OLED displays (ATN's Ryse flicker with frame generation: PresentMon at 2x showed even
  pacing, 11.6 ms between display changes with 0.66 ms deviation, so the flicker comes from the base rate's swings and
  hitches, which the cap smooths).
- Frame generation stays off for two presents after a resize: off and on in the same frame raced Present inside
  Streamline (its "repeated slDLSSGSetOptions" warning at every resize; the screen could keep showing old frames after
  the startup fullscreen switch).
- Debug: ReShade.ini FrameGenerationReflexSleep=0 skips Reflex's sleep after each present (its markers stay).
