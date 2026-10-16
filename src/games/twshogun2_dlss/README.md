# RenoDX — Total War: SHOGUN 2 Complete Edition DLSS

The game's "DLSS" is DLAA only. empire.retail.x64.dll links NVIDIA's NGX SDK 3.10.4 (and FSR3, XeSS): the feature
is created with the display size as input and output (`PerfQualityValue` 5 = DLAA; +0x1abc6f0: Width / Height /
OutWidth / OutHeight from the object at +0x3457890, +0x10 / +0x14) and evaluated with the display size as render
subrect (pass +0x1ad1580, SDK helper NGX_D3D11_EVALUATE_DLSS_EXT +0x1abd950; MV scale -1 / 1). The engine has no
render resolution of its own (gfx_aa 5 / 6 / 7 = FSR / DLSS / XeSS as anti-aliasing; FSR3's render resolution helper
is never called). RE helpers: `tools\shogun2\sx.py`.

Plan (ATN, 2026-10-14: both):
1. Shared base (v1): hooks on the game's DLSS creation and evaluation (`game_dlss.hpp`, patterns), overlay status,
   one-frame trace (`frame_trace.hpp`: draws, dispatches, copies, DLSS evaluate; shogun2_frame_trace.txt).
2. Lower game resolution, 4K output: DLSS created with the display's size as output, the tonemap and UI composite
   drawn again at that size.
3. Render scale inside the display-size targets: the 3D passes' viewports scaled, DLSS's render subrect and jitter to
   match.

## v1 test

In-game Anti-aliasing: DLSS. The overlay tab "Shogun 2 DLSS" shows what the game created and passes each frame; press
"Trace one frame" in a battle and send shogun2_frame_trace.txt and ReShade.log.

## v1 result (debug1), v2

The hooks work (3840x2160 -> 3840x2160). The game passes jitter 0 to DLSS (camera +0x28 +0x394 / +0x398 x the
size x 0.5 / -0.5, +0x1ad1745). Frame order: shadows (4096), G-buffer (rt0-2 RGBA16F + D24S8 at 3840x2160),
lighting / screen-space compute (some at 1920x1080), fog, transparents, depth of field (compute), DLSS, bloom
(1920 / 960), auto exposure, tonemap + flares + vignette, UI into its own target (and unit cards at 237x237), UI
resolve: everything before DLSS is the 3D image, everything after it stays at the display size. The shaders already
tell screen size, viewport (size, origin) and render target size apart (camera.hlsl, the 3K engine's).
v2: the trace adds the camera constants (screen, viewport, origin, render target, jitter, previous jitter) per run of
draws and per dispatch.

## v2 result (debug2), v3: camera jitter for the game's DLSS

Traces with DLSS, TAA High and TAA: TAA jitters the camera (g_camera_jitter about 0.0002 in clip space), DLSS not
(0 everywhere, and 0 passed to DLSS). Camera constants: screen = viewport = render target = 3840x2160 in every pass.
The jitter (+0x199c4d0: 16-point table, 2 x offset / size, current +0x394, previous +0x39c of the camera) runs from the
frame setup only while a TAA object is active (+0x1a9100c: +0xdf8 / +0xe08 / +0xe18 with +0x60 and +0x18 set).
v3: while the game's DLSS ran since the last present, that condition's first test becomes a jump to the jitter (5
bytes, restored with any other anti-aliasing and on unload); overlay "Camera jitter" (ReShade.ini [Shogun 2 DLSS]
Jitter, on) and a debug "Jitter sign" (the jitter DLSS is told, X / Y flipped).

## v3 result, v4: render scale (experimental)

ATN: jitter on is clearly better, the game's sign is right. The game's 16-point jitter is enough for DLAA (NVIDIA:
8 x scale^-2 phases), not below it.
v4, overlay "Render scale (experimental)": 100% (DLAA) / 77% / 66.7% / 58% / 50% / 33.3% (ReShade.ini RenderScale).
- game_dlss.hpp: the game's feature handle (holder from the DLSS pass's "cmp [rip + holder], 0", +0x1ad159d) cleared on
  a change, so the pass creates it again: input = round(display x scale), output the display, PerfQualityValue
  immediate (CreateFeature +0x1aa) to the matching mode; evaluated with that render subrect and the jitter x scale (the
  game turns clip-space jitter into pixels with the display size). The jitter function (+0x199c4d0) is replaced while
  the scale is below 1 and DLSS runs: Halton (2, 3) in render pixels, max(16, ceil(8 / scale^2)) phases.
- render_scale.hpp: from present to the DLSS evaluation, viewports and scissors at the display size (or half) at 0, 0
  become the render size; camera constants mapped for writing (constant buffers of 704-1024 bytes, screen_size and
  g_viewport_dimensions both the display size) get g_viewport_dimensions = the render size before the unmap. Compute
  dispatches unchanged. Overlay: viewports scaled and camera constants patched in the last frame.
Known: an old feature is left at each scale change (NGX release not hooked).

## v4 result (debug3), v5

The render scale works: DLSS reports 1280x720 -> 3840x2160 at 33%, 25 viewports and 9 camera constants scaled per frame.
77% failed: DLSS refuses UltraQuality (the game tried to create it every frame); v5 uses Quality from 65% up.
The depth of field (3x too blurry at 33%), its auto focus and the tonemap's distortion lookup are fixed in the HDR
addon (twshogun2 v5, README "Render scale").

## v6: custom render scale

Render scale "Custom": 1-100% (ReShade.ini RenderScaleCustom), applied when the slider is let go (each new scale
recreates the game's DLSS; the old feature is left). DLSS mode: the same ratios (Quality from 65%, Ultra Performance
below 45%); below 33% DLSS may refuse it. The effects that broke at lower scales (river copy in the sky, extra
shadows, AO, fire light, auto exposure) are fixed in the HDR addon (twshogun2 v6).

## v6 result (debug5), v7: motion vectors, texture LOD bias

ATN: every scale below 100% (even 99%) flickers on grass; DLAA does not; textures look lower. The jitter is not it:
the game's own (+0x199c4d0) is the same formula as the addon's Halton (current from frame, previous from frame - 1,
2 x offset / size), its eval jitter reads the camera (+0x1ad174a: width x camera +0x394 x 0.5, height x +0x398 x
-0.5), and the motion vectors are viewport-aware (camera.hlsl convert_cs_velocity_to_pixel: g_viewport_dimensions;
debug5: every scaled pass sees the patched camera). The game creates DLSS with flags 1 (IsHDR; 0x41 with its auto
exposure flag, +0x1abc833 / +0x1abc84b): no MVLowRes, so below the display size DLSS read the motion vectors as
display-sized (wrong place and length). v7 sets MVLowRes while scaled (FLAGS_IMMEDIATES). Texture LOD bias (as Ryse):
Off / Matched / Sharper (default, NVIDIA's) / Sharpest, on the game's anisotropic samplers in the 3D span.
Draw distance, shadow and reflection ranges: no geometry shader reads the screen or viewport size (LOD is the CPU's);
the lower look was texture levels (bias) and the motion vectors. debug5 presets: Low / Medium / High / Very High at
33% run no unfixed path (old AO and summed-area-table DOF never run; Medium adds the low CoC pass, as the high one).

## v7 result, v8: the jitter in the inverse matrices

ATN: the motion vector fix works. Shadows at 33% (SSR / SSS off, debug6) are blurrier than with DLAA, with acne on
grazing rock. The shadow lookup does not depend on the resolution; the vertex shaders add no jitter (it is in the
projection). If the game's inverse matrices leave it out, every world position rebuilt from the depth (shadow mask,
lights, fog) sits at the unjittered pixel centre: no subpixel detail for DLSS and, at 33%, 3x the clip error (acne).
v8 checked each scaled camera upload (inv_projection x projection == the jitter's translation -> fold it in). debug7
log: "kept" (identity: the game's inverses already have the jitter). Removed in v9; the softer shadows / rock stripes at
33% have another cause (open).

## v10: texture LOD bias only in the main view

ATN (debug9 / debug10): shadows thinner and with more holes below 100%, with screen-space shadows on and off. The
texture LOD bias re-bound every anisotropic sampler in the 3D span, the shadow map passes too: the trees' alpha-tested
leaves were cut with much sharper mips (-2.6 at 33%, Sharper) into the shadow maps. Now only while the main view's
viewport is bound (render_scale::MainView: the last bound viewport is the display size, scaled).

## v11: DLSS presets, graphics options here

- DLSS preset (E, K, M default, L, as rysesonofrome_dlss; ReShade.ini Preset): the game's create sets every mode's
  DLSS.Hint.Render.Preset.* to 0 through NVSDK_NGX_Parameter_SetUI (+0x207fcc0, the call at create +0xc5); that
  setter is hooked and those hints get the chosen preset. A change recreates the feature (as a new scale).
- Graphics options (ATN: as Ryse / Expeditions / Tales): SSR resolution moved here (ReShade.ini SSRResolution); its
  shaders are the HDR addon's, which reads the value every frame.

## v12: Simple / Advanced, grass distance, LOD bias fix, leaf tests

- Overlay as rysesonofrome_dlss (ATN): Settings mode Simple (render scale, DLSS preset, texture LOD bias, then the
  Graphics sliders) / Advanced (ReShade.ini SettingsMode: camera jitter, jitter sign, particle mask, status, trace).
  Graphics options are sliders over named choices, as Ryse's game settings.
- Texture LOD bias (ATN, v11: shadows right, but many textures blurrier whatever the bias): the trace (debug10) has
  the shadow maps (1022 draws) right before the G-buffer, and the game skips PSSetSamplers when a slot already holds
  the sampler, so many materials kept the shadow pass's unbiased one. The game's pixel shader samplers are tracked per
  slot (immediate context) and bound again, as clones or as the game's, whenever the main view starts or ends
  (texture_lod_bias::OnBindViewports, registered after render_scale's).
- Grass distance (ATN: grass popping, LOD steps): the grass is a "Scrub Node" (constructor +0x1b06e00:
  grass_lod_distance = 450 at node +0xd0, an int; +0xd4 100.0). Its update (+0x1b15f20) draws the grass cells within
  that distance (box and squared distance) and the grass shader setup (+0x1b48f51) gives rigidgrass
  inv_lod_distance = 1 / it, which shrinks (vertex height) and thins (dissolve) the blades linearly from the camera
  toward it. grass_distance.hpp hooks the update and scales the value (Game, 1.5x, 2x, 3x, 4x; ReShade.ini
  GrassDistance). No separate grass mesh LOD was found: the near changes are that shrink / thinning, which this makes
  slower. (The engine's force_lod / enable_force_lod tweaks are terrain cells, debug_terrain_culling_v2.)
- Leaves and flowers flickering with jitter (debug10: MVs and dissolve looked right): two leaf-specific suspects.
  1. The game's DLSS pass (+0x1ad1580) gives DLSS FSR's reactive mask (render targets +0xe8, the same resource FSR
     gets as reactive at +0x1ad1a73) as pInIsParticleMask. It is the G-buffer's SV_Target4: rigidgrass writes its
     vertex alpha there, rigidleaf_v3 / rigidbranch_v3 declare it and never write it (undefined). Advanced: "DLSS
     particle mask" off passes none (ReShade.ini ParticleMask).
  2. Leaves write their own motion vectors (TAA_VELOCITY_PIXEL: motion_vectors.hlsl skips them) into SV_Target3; the
     trace only listed 3 render targets, so whether that target is bound was not seen. The trace now lists all 8, every
     render target clear and the masks DLSS gets.
- engine.hpp: the engine module and pattern search, shared by game_dlss.hpp and grass_distance.hpp.

## v13

ATN (debug11): grass distance fixes a lot of the grass pop-in; the particle mask off changes nothing (leaves: see the HDR
addon's v16). Grass distance up to 32x (14400 m: the squared distance still fits the game's int). The trace shows the
G-buffer's rt3 (motion vectors, cleared each frame) and rt4 (reactive mask, R8) bound for the leaves.

## v14: wind motion vectors, terrain detail

ATN (HDR v16 / DLSS v13): one flowering bush still flickers with jitter (paused; SSS on / off and maxed shadows the
same). debug9's pair: only the petals' edges change between frames, their insides do not: wrong motion, not shading.
- The leaves' motion vectors (rigidleaf_v3 vs30_main, DXBC: cb0[35].xxyy, so both times are used; the decompile
  hides it) move each vertex with the wind at time_in_sec and again at prev_time_in_sec / g_prev_wind_direction. The
  camera upload (+0x1a92700) takes them from the scene (+0xcb8 / +0xcbc), whose time update (+0x1a968e0, virtual
  slot 0x890) sets previous = time, time = new: the previous time is that of the last update, not of the last frame.
  Untested theory: paused the update stops, the swaying flowers keep one step of wind motion, and DLSS moves their
  edges every frame (stable without jitter: the same image every frame).
- render_scale.hpp (now at any scale, its map / unmap also while not scaling): the main camera's prev_time_in_sec is
  the last frame's main camera time_in_sec. Advanced: "Wind motion fix" (ReShade.ini WindMotion, on). The trace adds
  time / prev to the camera line.
- Terrain detail (experimental, ATN): the terrain cells' LOD grid (+0x1b4f697) adds the engine's debug tweak force_lod
  to each cell's base when enable_force_lod is set (read at +0x1b4f998): "Finest everywhere" sets them to 1 / 0
  (terrain_detail.hpp, ReShade.ini TerrainDetail). No tree or plant model LOD distance has been found yet
  (force_render_lod is a debug filter, variant_force_lod the units).

## v15: model detail distance; wind motion fix removed

ATN (debug12): the Wind motion fix changes nothing (removed; the trace showed time = prev while paused). Grass
distance 32x, yet grass and flowers still pop in and "get more" nearing them (white flower pixels in the distance).
- Every grass instance within the grass distance is placed (the update's cell body +0x1b0b7e0: squared distance, the
  terrain's grass map, then submitted); no thinning there.
- The clumps are rigid models with levels of detail in the file: battleterrain\vegetation\climate\default\grass\
  grass_hi / med / low.rigid_model_v2 (RMV2 v2): 3 levels, 25 / 50 / 100 m (index data 744 / 312 / 48 bytes). The
  loader (+0x1b375ca) stores each level's distance squared at LOD +0x70; SelectLod (+0x1b26470: the camera distance
  squared plus its zoom term, d2 + (fov - 70 deg) x d2 x rigid_fov_scale, against the levels') and its fade
  (+0x1b265b0) pick and blend them for rigid models, called from the rigid / scrub node code (+0x1b0fdfb ..
  +0x1b139ba).
- model_detail.hpp hooks both and divides the distance squared by scale^2 (Model detail distance: Game, 1.5x .. 16x;
  ReShade.ini ModelDetail). Rocks, props and buildings get it too. Buildings' small props at lower Building quality
  are hidden at prop_hide_distance (+0x1b4e960; not at the highest quality).
- Not found yet: trees (tree_mesh, TREE_LOD_LIST, billboards).
- warscape_rigid_lod_range_tables (db, by level count: 4 levels 200 / 400 / 750 m, 5 levels + 1500 m) and per-model
  warscape_rigid_lod_tables only cover battle buildings.

## v16: render sizes at the display's exact aspect; DLSS input capture

- ATN: custom scales flicker (99%), the presets do not. The game's motion vectors pass (motion_vectors.hlsl get_ws_pos)
  takes its aspect from g_viewport_dimensions (x x w): with each length rounded on its own (99%: 3802 x 2138, 1.77830
  against 1.77778) static pixels got up to about 0.6 px of motion toward the screen edges. Render sizes are now a
  whole number of the display's aspect units (width and height over their gcd: 16 x 9 at 3840 x 2160; RenderSize), so
  the aspect is exact: 33.3% / 50% / 66.7% unchanged, 58% 2224 x 1251, 77% 2960 x 1665, 99% 3808 x 2142. DLSS's
  jitter is converted with render / display instead of the scale.
- debug14 (Ultra Performance, paused): the flowering bush and the green bush (rigidleaf_v3, 0x09117812) and the tree
  trunks (rigidbranch_v3, 0x29EB778A) differ between two frames (correlation 0.91-0.98), stones and grass do not
  (0.999); no coherent shift. The tree shaders' motion vectors are the grass's code (their compiled vertex shaders use
  time_in_sec for both frames: no wind motion; the engine sets g_prev_wind_direction = g_wind_direction, +0x1b73ed6).
  Advanced "Capture DLSS inputs" saves what the next 4 evaluations get (color, motion vectors, depth: the render
  subrect, raw texels; shogun2_dlss_capture_*), and the trace writes the camera constants whenever they change (the
  buffer, a hash, the projection's jitter terms) to see whether the trees draw with their own.

## v17: the trees' motion vectors found; particle resolution; SSR ray steps, shadow samples

- debug15 capture (Ultra Performance, paused, tools\shogun2\capture_decode.py): about 10% of the pixels had motion
  vectors over 0.5 px (up to 1000 px), all on trees (the bushes, branches, trunks), the same in all 4 frames; grass and
  ground 0. The trace's camera constants were the same for every G-buffer draw (one hash). The game's tree shaders
  read their previous position with the wrong w: fixed in the HDR addon (v20). The particle mask test is removed (the
  game's mask is passed again).
- Particle resolution (particle_resolution.hpp, ReShade.ini ParticleResolution): ATN, below 100%: smoke around the
  camera at a burning house flickered, then sat on the screen in the wrong place until zooming out. The engine's VFX
  dynamic resolution (effects_overlay's input_view_scale) shrinks the particle passes' viewports while particles cover
  much of the screen; those viewports are neither the display size nor half of it, so the render scale kept them
  display-sized (particles about 3x too large, stretched from the corner). Its tweak vfx_enable_dynres (+0x1b2cbda,
  then scale 1) is off below 100%, and at 100% with "Full".
- SSR ray steps (SSRSteps: 100 x 1 / 2 / 4) and Shadow samples (SSSSamples: 120 / 240): written for the HDR addon.
- engine::RipTarget shared (terrain detail, particle resolution, the DLSS pass and preset hooks).

## v18: the particles' reduced resolution scaled; SSR to 16x, SSS to 960

ATN: Particle resolution Full (v17's smoke fix below 100%) costs 80 -> 30 fps in thick smoke, with no visible gain.
The VFX dynamic resolution is driven by GPU time (the effects view, vtable slot 22 +0x1b2cbd0: GPU timestamps, budget
33% of the frame, s from 0.3 to 1 in 0.01 steps per frame; its rect x s, truncated). Its viewports, scissors and camera
g_viewport_dimensions (int(display x s), the display's aspect) are now scaled to the same fraction of the render size
(render_scale.hpp ScaledParticleSize; unrounded viewports, as the effects overlay reads them back at s x the pixel).
Particle resolution is the game's by default at every scale again; Full turns vfx_enable_dynres off.
SSR ray steps up to 16x (1600), Shadow samples up to 8x (960).

## v19: particle resolution minimum

ATN: below 100% smoke is now in place but blurry. The VFX dynamic resolution's scale s steps down to a minimum of 0.3
(of the particles' full size, the render size) while the particles take more than 33% of the frame. That minimum is a
variable only that code reads (+0x2e7b744, found from its read at +0x1b2cd43): "Particle resolution minimum" (ReShade.ini
ParticleMinimum, replacing ParticleResolution) sets it to a share of the display (30% = the game's lowest at 100%, up to
100%), divided by the render scale and capped at 1 (s never goes above 1, the render size). Game: 0.3 as before.

## v20: particle resolution minimum by the render scale

ATN: v19's slider did not seem to do anything, and should follow the render scale itself. Its minimum is now 0.3 /
render scale (at most 1): the particles never get below the game's lowest share of the display (30%), at any scale. The
slider and ReShade.ini ParticleMinimum are gone. Advanced shows the particles' lowest scale last frame (of the render
size, from their viewports): 100% while the game keeps them full (no GPU-time pressure), so a raised minimum changes
nothing then.

## v21: shadow samples to 3840, SSR ray steps to 64x

Shadow samples: Game (120), 2x-32x (3840); SSR ray steps: Game (100), 2x-64x (6400). Both are the HDR addon's (v25).

## v22: graphics presets; SSR ray steps removed

Graphics: "Recommended (ATN's settings)" (SSR resolution Full, Shadow samples 4x, Terrain detail Finest everywhere, the
rest default) and "Reset" (all graphics options to their defaults). SSR ray steps removed (ATN: no visible or
performance change; ReShade.ini SSRSteps is no longer read).

## v23: shadow stability, shorter tooltips

Graphics: "Shadow stability (SSS)" (Game, Steadier (softer), Steadiest (softer, thinner); ReShade.ini SSSStability, read
by the HDR addon v27; in both presets at Game). Tooltips shortened.

## v24: jitter for the HDR addon, shadow softness

- dlss_jitter.hpp: each evaluation's jitter (InJitterOffset, render pixels; 0 while the game's DLSS does not run)
  shared with the HDR addon (cross_addon Shared), which un-jitters the heat haze's distortion texture (HDR v28).
- Graphics: "Shadow softness" (Game, Soft, Softer; ReShade.ini ShadowSoftness, HDR addon v28), in both presets at Game.

## v25: shadow map resolution and stability

- Shadow map resolution: the sun's cascades are 4096 x 4096 (x 4 splits) at the highest Shadows setting, x the engine
  tweak shadow_resolution_multiplier (ShadowMapView.cpp, read at +0x1a77c27 when the game creates the shadow map,
  +0x1a77b90: start-up or its Shadows option changed). Game / 2x / 3x / 4x (16384, D3D11's largest; R32: 4 GB).
  shadow_resolution.hpp sets it at device creation and when changed.
- Shadow map stability (HDR addon v31, ReShade.ini ShadowMapStability): normal offset for the cascades, SSS on or off.
- ATN asked for SSS-off shadow improvements: both work with SSS on too (its pass combines the cascades).

## v26: shadow softness removed, Recommended shadow stabilities

- Shadow softness removed (ATN: not needed with the others).
- Recommended (ATN's settings): Shadow stability (SSS) and Shadow map stability at Steadier.

## v27: particle resolution option, preset K

- Particle resolution (Graphics, ReShade.ini ParticleResolution): On (default, as v20: the particles' minimum follows
  the render scale) / Off (the game's 30% of the render size). Recommended (ATN's settings): Off.
- DLSS preset K by default (ATN: steadier at DLAA).
- Frame trace: the camera's time - prev time at full precision (debug18 banner).

## v28: particle resolution Off by default

- ATN: Particle resolution Off (the game's own scaling) by default and in Reset, as in Recommended.

## v29: less CPU

- The frame trace's draw, dispatch, clear and copy events are registered only for the traced frame (before: every draw
  went through them).
- The debug view's counts (viewports, camera constants, particle scale) are kept only while ReShade's overlay shows them.
- Texture LOD bias: the game's pixel shader samplers are recorded only while a bias can apply (render scale below 100%
  with the bias on), starting from the immediate context's current samplers; at 100% its sampler and viewport events
  return before any lock.
- Render scale: a buffer unmap takes the lock only while a camera buffer is mapped (before: every unmap).

## v30: SSR ray steps back

- SSR ray steps (ReShade.ini SSRSteps: Game (100), 2x-64x (6400); the HDR addon's ssrray00) back in Graphics after SSR
  resolution, Game in both presets (ATN: the reflections are on the sea in sea battles; v22 had tested on land).

## v31: SSR ray steps removed again

ATN: no visible or performance change in sea battles either (ReShade.ini SSRSteps no longer read).
