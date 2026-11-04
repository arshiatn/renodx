# Total War: WARHAMMER III (RenoDX HDR, ATN)

Build: `cmake --build --preset clang-x64-release --target renodx-twwarhammer3.addon64`.

## v32 (in progress, batch 3; renodx-tw-dlaa64.exe helper76, bridge v23; run in game by replay, campaign, benchmarks: see each line)

- **Black patches in ray tracing, fixed, run in game** (replay at 100% and 50%, campaign): the terrain's second pass was in the scene as a surface. It tessellates coarsely and only draws skirts; streamed out past its geometry shader, its coarse hills lay over the real ones, sun and AO rays alike. Replay's first view: 12.69% of the view black, 28 blobs; now 0, 0. Also out: `fx_rigidterrain` (boxes that paint terrain inside themselves: the campaign's rectangles) and what the terrain's mask discards (the campaign's sea).
- The terrain gets its room in the frame first (it joined last and went short at 100%); what doesn't fit is left out of other meshes, leaves first by draw order (Log Details names them).
- Path-Traced AO is gone from WH3's page and a saved one turns off: with spherical harmonics on the game's ambient cube isn't its sky. The paths' sky is the learned shade.
- **Ambient occlusion below 100%, fixed, run in game** (replay and campaign, started at 50%, against 100%, one view each): GT-VBAO's depth now comes from the game's sweep, as the downsample it read stops below about 2/3 scale (a frozen depth); its settings reach it at b10 (it had them by luck at 100%, and read radius 0 at 50%: flat white). The AO filter and Ray-Traced AO read the game's AO where its sun draws read it, and the sun reads their textures one to one: the filter darkened the right and bottom third (cells 0.76, now 1.0 to 1.1), RTAO lay 1.5x off. Game's SSAO was right at 50% already: Debug View 8 correlates 0.986 with 100%.
- **Debug View's Screen-Space AO** works in WH3: what the sun draws read, the game's under RTAO. Troy, Pharaoh, Warhammer I and II: still magenta, nothing changed there.
- `rt_terrain_keep_gs.h` is built by hand: the command is in the `.hlsl`.
- Log only, run in game: the helper's idle release no longer says it "started again"; Reflex's line says when frame generation paces the
  game; FSR Frame Generation's status has a real count (2.0 here); no "smoke shadows" in the status; Below 100%'s line printed once the size
  holds (one line per drag: not proven, no script can drag); "without room" every 10 s under Log Details, with the numbers.
- The page's presets leave Log Details alone.
- With the game's own DLSS (`gfx_dlss` above 0) its TAA is gone and the addon's upscaling doesn't run: keep it at 0.
- Verify runs: `tools/tw/wh3_batch3/facts.md`. Start form: `game_startup_mode replay "<full path>.replay"`; a bare name crashes the game.

## v31 (not released, built only, not run in game; renodx-tw-dlaa64.exe helper75, bridge v23)

- **Still computed twice, known:** the game's TAA runs and is thrown away under any upscaler (its other outputs are read later). The sun's cascades and their mask
  still render under RT shadows; rays multiply them (fog reads them too). RTAO multiplies the game's SSAO; under Path-Traced AO it feeds only the specular. With
  rays on, the terrain's geometry runs twice a frame. Point lights: see Torch and Fire Shadows.

- **Torch and Fire Shadows (Experimental, Shadows, More Options under Ray-Traced; off, not in the RT/PT preset).** With Ray-Traced Shadows on, our copies of the
  game's 8 point light shaders multiply each light by its traced visibility; off, or before a trace, the game's own shaders run untouched. With Path-Traced Global
  Illumination the lights also light what the screen never saw, at the light shaders' own scale of their diffuse. Torch light in fog is left out: nothing shows what a
  light's colour is worth in the fog's light. Which shaders a battle really uses, and whether its lights are drawn directly: never seen, no trace has one.
  Four of the 8 read the game's point light shadow map too, and it still renders: with this on, Point Lights With Shadows on Graphics 2 of 2 can go down to 0.5x.
- **Below 100% Resolution Scale, leaving it:** with the row on, picking Game's TAA or turning the in-game anti-aliasing off kept the last size, and Bend's shadows, Light
  Bounce and the AO filter stopped after an in-game scale change. Now the size is forgotten without an upscaler, or after a second without a TAA pass; the first
  frame back only checks the constants again. The screen-space shadow Debug View now shows the whole mask below 100%.
- **Ray Reconstruction's and the paths' guides by sun shader:** the two sun shaders that keep the normal in 8 bits and have no metal (F9F8CE80, 18A84893) and the one with a
  flat 0.04 (76A707E5) now get guides as they read the G-buffer themselves. Which setting picks these three isn't known; no trace has one.
- **Fog Shadows (Experimental, Ray Tracing Extra Options; off, not in the RT/PT preset).** The game's fog already has shafts from its shadow maps. On, rays take their
  place (they aren't stacked): the march skips the maps, only the sun's share of the fog goes dark, measured over the first 400 m. One of the fog's light shaders, the one
  in the trace; the other 5 stay as the game draws them.
- **Ray-Traced Reflections and Ray-Traced Specular Occlusion (Experimental, Reflections section, off; in the RT/PT preset).** The game's own
  screen-space reflections are on water only: its code for other surfaces is there but never runs. With the rays on, our copy of its resolve opens
  that path for armour, metal and what else the game marks so: on water the rays leave from the water, off water they replace the
  cubes' reflection where they meet something, and occlusion dims the cubes where something blocks them. Both off, the game's own shaders run
  untouched. Needs in-game Screen Space Reflections on. If the sun shader is one of the 3 of 7 that add the cubes differently, the
  game's reflections stay and the log says so once. FidelityFX SSSR isn't offered: the game's march already is a hierarchical one.
- **Water Clarity and Water Caustics (Experimental, off, Reflections section):** Clarity on the water plane and the sea (the shaders that read the
  game's scatter), Caustics on the two water plane shaders where it was read. battle_water, battle_river and the rest take a colour per material:
  neither row changes them. `ReShade.log`'s "Water: first draw of shader" says which one a map draws.
- **Ray Reconstruction's and the paths' guides had the wrong material channels** since batch 3: roughness was 1 - the AO channel (0 on much of
  the scene), specular a flat 0.04, normals from 8 of 10 bits. Now as the sun light and the resolve read them: roughness in the
  material's y, specular from metal, water its own.
- **Screen-space and ray-traced sun shadows now cut the sun.** In the sun shader the game mostly uses they only darkened the ambient: the
  patch took the ambient occlusion for the shadow. Bend's and the rays' shadows look stronger now, and with rays the sun follows them on top of
  the game's own shadow. One sun shader has no shadow at all and is left alone.
- **Path-Traced AO (Experimental, AO's method, off):** Global Illumination's paths bring the sky's light in place of the game's ambient, all of
  it (cube, spherical harmonics, the game's probes). Own Sky Rays goes with it. The RT/PT preset stays on RTAO: how bright it comes out against
  the game's ambient isn't known yet. Leaves keep the game's light from behind, reflections their share. Forward draws, water and fog keep the
  game's ambient.
- **Path Tracing's sky** now comes from the game's ambient cube (bluer from above), also with the game's shadows. Needs helper75.
- **Light Bounce and paths respect metal:** armour got a full bounce before; the game's ambient gives metal none.
- **Coloured Shadows** on capture flags (with Light Through Leaves; the colour needs Ray Reconstruction or path-traced Global Illumination).
- **The hills are in the rays:** the tessellated terrain, as the game drew it this frame (the same triangles, so the rays start on the ground they hit). Only
  what's in view: a hill behind the camera casts no shadow. With rays on the terrain's geometry runs twice a frame and the trace waits for its triangle count.
- **Game's Global Illumination (Experimental, Effects tests, default on):** the game's own bounce light, which Light Bounce comes on top of. Whether it
  turns off at once, next battle or after a restart isn't known.
- Log Details writes once per sun shader whether spherical harmonics and the game's Global Illumination are on, the ambient cube and its scale.
- **Below 100% Resolution Scale (Experimental, off):** DLSS, FSR or XeSS also when the in-game Resolution Scale is below 100%. The game's
  own chain stays at the smaller size; the tone mapper's picture goes through the upscaler up to your resolution; flares, vignette, sharpening
  and UI at your resolution. Off, and at 100% either way, nothing changes. The first frame at each size only checks the game's constants; if
  they aren't as expected the status line says so, the values are in `ReShade.log`, and the game's TAA and upscale stay. Bloom now sees
  the unresolved picture, and particles go through the upscaler: ATN's test decides. No LOD bias here: the game has its own.
  Rays a Pixel's Match Output is back: up to 8 rays at the game's lower scales (at 100% it is still 1).
- **Ray tracing's scene: leaves, grass, 4-bone units, capture flags.** Leaves and blades cast shadows with their holes (alpha tested against
  their own map), the flag waves in its shadow. Tree billboards stay out (they face the camera), so do `grass_rock` and two grass families no
  trace has shown yet. Burning trees keep their leaf shadow. With ray tracing off, nothing changes. Five rows show now: Light Through Leaves,
  Grass Shadow Range (works on NVIDIA's Mega Geometry only: elsewhere dynamic grass isn't apart from the rest), the three Opacity Micromaps rows.
- **Path-Traced Global Illumination takes the G-buffer** (albedo, normals) with any upscaler, as it should have: it had none, and guessed both.
- **Graphics 2 of 2 on values that exist.** Of its 430 entries the game had 31 (a status line said so): its names changed. Now 46 of
  46: the same main rows, and 18 values in Grass & Trees, Shadows, Units and Effects (Tests), found by reading the game's own list of values
  in Warhammer3.exe. New: Grass Size, Grass Height, Grass Edge Blend, Edge Blend, the eight shadow maps' reach (in metres, 0: the
  game's), Point Lights With Shadows, Cloth Smoothing, Ragdoll Limit. At their defaults they write nothing.
- Everything else on that page is gone: those rows did nothing, or three threading switches nobody needs. Their saved values no
  longer apply. Audio Memory is gone too, and with it a thread that retried for a long time at start and kept the sliders from
  applying until it ended. A saved changed audio memory's entry in ReShade.ini's early load list is taken out.
- `engine_values.txt` is now the real list: every plain value's type, default and direct reads.
- **Fog Strength** (Extra): how thick the battle's fog and haze are. 0 = none, 1 = the game's (bit for bit), 2 = double. Thick fog
  at 2 is brighter than the game's fog colour: the fog's light is doubled, and what's behind it is already gone.

- **SDR (monitors without HDR)** preset: Rendering Mode SDR, Output SDR, the rest at Reset's. A first start (no settings saved yet)
  on an SDR monitor picks it by itself, with Output on Auto.
- Psycho V30's Hue Shift is now called Highlight Hue Shift; its saved value stays.

Verify: Resolution Scale 50%, DLSS, Below 100% on: `ReShade.log` "Upscaling below 100%: 1920x1080 as expected", status line DLSS's, picture whole,
UI and markers in place. Graphics 2 of 2's status line says 46 of 46 in `ReShade.log`; Battle Shadow Map 1 at 500 m: nearby shadows softer, at once.
Fog Strength 0 in a battle: no fog, no leftover glow; 2: thicker.
Bend on, sun low: sunlight cut behind soldiers, not only the ambient. Log Details on: a "Sun shader" line per shader; Path-Traced AO on an open field: not much brighter or darker than RTAO (else send that line).

## v30 (not released, built only, not run in game)

v29 is used twice: the zip twwarhammer3_v29.zip (2026-10-28, its README ends at v28) and the notes below (2026-11-03, never zipped). This zip has both.

- **The zip's folder** has what goes next to Warhammer3.exe: the add-on, renodx-tw-dlaa64.exe helper74 (bridge v23), and for it
  nvngx_dlss.dll and nvngx_dlssd.dll 310.9.16, Streamline's, FSR's, XeSS's, Opacity Micro-Map's and Agility's DLLs, with their licences.
- **FSR and XeSS** beside DLSS, in place of the game's TAA at Resolution Scale 100% (Experimental: never run in this game).
  **The row is one now: Anti-Aliasing, on the keys the other Total War addons use. Saved `Dlss*` settings stop applying:**
  choose DLSS again; its preset default is L (was M). A saved DLSS on a GPU that isn't NVIDIA's becomes XeSS on Intel's, else FSR.
- **NVIDIA Reflex** with any anti-aliasing (it needed frame generation before), and its Frame Rate Limit.
- **Upscaling Debug View**: the motion the upscaler gets, over the picture (colour: direction, brighter: faster).
- A red line on the first page when only the add-on is installed, without the helper.
- **Frame Generation in the helper**, as in the other Total War add-ons: on and off without a restart, on HDR10, scRGB and SDR
  output (it no longer forces HDR10), its memory back when off; HUD Recomposition, Preset, Debug View and a status line work.
  Its HUD-less image has the flares, vignette and sharpening, and no UI or portraits. Needs Streamline's files next to
  renodx-tw-dlaa64.exe. ReShade.ini's `[Warhammer III]` frame generation keys stop applying.
- **Graphics 1 of 2 in Attila's layout**, the same code: Anti-Aliasing and Upscaling, Shadows, Ambient Occlusion, Global
  Illumination, Ray Tracing Extra Options, each with a method slider and More Options; every setting keeps its key. Presets Reset,
  No RT/PT and RT/PT (Attila's values until they're chosen for this game). New rows: Frame Generation Method (default Auto, it was
  DLSS only), Ray Tracing Denoiser (NRD, Experimental). Not here: Upscaling Mode and the render scale's rows (the game owns its
  scale), SMAA, CMAA2, own TAA, Reflections and Water, smoke, Path-Traced AO; ray-traced AO multiplies the game's, so its method sets the
  game's AO rather than GT-VBAO under it (a setup saved with both loses GT-VBAO).

## v29 (2026-11-03, built only, not run in game)

- **Gamma Correction** (Rendering Mode SDR, on by default). On: any in-game Gamma shown as gamma 2.2, today's picture. Off:
  the game's own picture at the in-game Gamma. Rendering Mode SDR on Output SDR ignores Paper White (as Attila).
- **Extra in Rendering Mode SDR**: Bloom, Lens Flare, Vignette, Distortion, Sharpening and DOF work there (at 1 the picture is unchanged). Film Grain and Indicators stay HDR-only.
- **Wrong constants from the decompiler** (checked against the game's own shaders):
  - Depth of field focus: the near side's fade uses its own width, and the reset path its own limit (near = near limit made a hard foreground edge there).

## v28 (2026-10-27, batch 5, untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper22** (in the zip; bridge v11, needs this addon): **ReSTIR GI v2** (compared with
  Embark Studios' kajiya, MIT / Apache 2.0, not copied): fireflies clamped, history only on a surface facing the same
  way, and every third frame the reused light is checked again: a light switched on or off shows within 3 frames
  (was about 20). **Partitioned TLAS** fixed (its update operations stalled the GPU after BLAS compaction) and on
  again.

## v27 (2026-10-27, untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper21** (in the zip). Fix: the last zip's helper19 built a partitioned TLAS (NVAPI) that
  stalled every ray tracing request by 0.3-0.8 s in the GPU test; it stays off now (RENODX_TW_PTLAS=1 turns it on for
  tests). **ReSTIR GI** for Path Tracing: each texel's paths are resampled with last frame's at the same surface and with
  five neighbours' (their solid angle's Jacobian; a last ray checks the chosen surface is in view): in the GPU test about
  a quarter of the noise at one path a texel, the same brightness.
- Frame Generation **Dynamic** (NVIDIA's dynamic multi frame generation, up to 6x for the display's refresh rate) and
  **5x / 6x** (RTX 50 series), applied live.
- **Ray Tracing preset** buttons Low / Medium / High / Ultra: shadow softness, AO rays and rate, Light Bounce rays, path
  bounces, reflection samples (High: the defaults).
- **Ray-Traced AO Rate**: Quarter traces one pixel of each 2 x 2 a frame, in turn (DLSS smooths it): a quarter of the
  rays.
- With **Ray Reconstruction** it is the only denoiser: the Light Bounce's denoiser, the AO filter and the reflections'
  accumulation and filter step aside (the reflections go out as traced).
- Less CPU work a frame: static meshes the ray tracing scene already holds are recognised by a short key (shader, draw
  arguments, first vertex and index buffer) and only marked drawn.
- DLSS below 100% stays off: after the TAA the game still draws 3D effects into the scene at the render size with the
  render-size depth, then the bloom chain and the final composite at the render size before its own upscale; DLSS's
  display-size output would need all of them moved to the display's size (and a display-size depth), which needs
  in-game testing.

## v26 (2026-10-27, untested in game)

- **Ray Reconstruction** (Experimental, DLSS section, with DLSS): NVIDIA's Ray Reconstruction in place of DLSS through
  the helper, its guides from WH3's G-buffer at the frame's first sun light draw (rr_guides.cs: the view-space normal
  from the upper 8 of each 10 bits turned to world space by inv_view, diffuse albedo = the colour x 0.96, specular
  albedo 0.04 and roughness = 1 - the material channel's z, as the deferred sun light reads them: WH3 lights
  dielectrics only), its camera from the uploaded projection and view-projection.
- DLSS below 100% stays off: the game's post-processing would need to run at the display's size.

## v25 (2026-10-27, untested in game; the helper GPU-tested)

- **Ray tracing** (Experimental, DLSS section, with DLSS): Ray-Traced Sun Shadows, Ambient Occlusion, Light Bounce and
  Path Tracing through the helper DLSS uses (renodx-tw-dlaa64.exe helper19: the other TW games' module, rt_core.hpp /
  rt_scene.hpp). The scene: the G-buffer's opaque meshes as the game's own vertex shaders draw them (their SV_Position
  streamed out and made world positions again with the frame's camera, as WH3 has no capture versions; checked on the
  GPU: back to the same positions within 6e-8), units every frame. Not in it yet: leaves (no alpha test without their
  UVs), grass, billboards, decals, water and the tessellated terrain. The shadows go to the screen-space shadows'
  occlusion, the AO into the AO the sun draws read, the Light Bounce's trace through rays. No reflections (no G-buffer
  guides yet) and no Ray Reconstruction yet (its guides need WH3's G-buffer decoded: view-space normals in rt0, sRGB
  base colour in rt1, material in rt2).
- **Frame Rate Limit** (DLSS section, with frame generation): NVIDIA Reflex's cap for the rendered frames.
- Frame generation stays off for two presents after a resize (Streamline's set-options race).

## v24 (2026-10-27)

- **DLSS** (Graphics 1 of 2, DLSS section; keep in-game Anti-aliasing on TAA or TAA High): NVIDIA's DLSS replaces the
  game's TAA resolve (cs 0x1ADBFF18 TAA / 0x6A8FC282 TAA High; taa_dlss.hpp). Its inputs are the game's own: this
  frame's jittered colour (t4), depth (t0's x), the velocity buffer its TAA uses (t2: motion vectors for everything the
  game draws, units, wind and cloth included) and its jitter (camera g_camera_jitter, the projection's own); they go
  through the Total War helper (renodx-tw-dlaa64.exe, as the other TW games' DLAA) and the result replaces the TAA's.
  Presets K / M (default) / L. At 100% only (DLAA): below it (Resolution Scale) the game draws the 3D and its post
  chain into the targets' top-left and upscales at the end, so DLSS stays off there (the camera's viewport is not the
  screen's) and the game's TAA runs. The game's own DLSS (preferences gfx_dlss, no menu option) never starts, even with
  NVIDIA's DLL in place: not used. Checked in game: DLAA active at 100%, a camera pan stays sharp (no ghosting: the
  velocity's sign and scale right); at 50% DLSS off and the picture intact.
- **Frame Generation** (2x-4x, Reflex Boost; restart): DLSS Frame Generation in the game's process (Streamline, as
  Shogun 2's) with DLSS's depth, motion and jitter and the game's camera (twattila/fg_camera.hpp), the HUD-less frame
  after the tone mapper (t00); output HDR10 with it (it takes no scRGB). Needs sl.*.dll and nvngx_dlssg.dll next to
  the game. Checked in game: generating 2x with the HUD-less frame.
- Not yet: DLSS below 100% (the post chain after the TAA would have to run at the display's size), Ray Reconstruction
  (its guides from WH3's G-buffer) and ray tracing (Attila's rt_scene needs WH3's scene shaders and camera layout).
- Debug: renodx_trace_now next to Warhammer3.exe traces the next frame into tw_frame_trace.txt (checked each second, the
  file removed), direct and indirect draws, with the tone mapper's overscan and the camera's near, far and depth direction;
  [renodx] FrameTrace=1 traces after 600 presents with the 3D view's camera or TAA. The log says when each hook first ran.

## v23 (2026-10-26, untested in game)

- Screen-space shadows fixed (ATN: lines, shadows pointing away from the sun): the shared code read the sun's direction
  from Attila's lighting buffer (b2, c0.y) and its camera's layout in every game. Each sun draw now names its own (from
  the game's shader dump: tools\tw\sun_draw_layouts.py): its lighting at b2 / b3 / b4 and its camera's other layout
  (projection at c6, view-projection at c10), copied into Attila's for these passes. WARP check
  (tools\tw\warp_tests\sss\sss_scene.cpp, the sun direction at c0.y / c0.x / c1.x): the same shadows.
- Graphics 2 of 2 trimmed (ATN): More Tests keep only campaign map, grass & trees, units, buildings and culling (More
  Tests: Buildings & Culling); Attila's tests stay.
- Graphics 2 of 2: the debug buttons Attila no longer has removed (Trace graphics settings, the two Watch buttons, Trace
  ambient occlusion).
- Ambient Occlusion (GT-VBAO): Temporal Smoothing, Temporal Edge Fix and Denoiser as Attila's (twattila/ao_filter.hpp:
  before the frame's first sun draw, the AO texture the sun draws read goes through Attila's resolve (last frame's AO
  moved with the camera, held within this frame's 3 x 3) and its 5 x 5 depth-aware denoiser, then the sun draws read the
  result); the bitmask's noise moves each frame with Temporal Smoothing. GPU test (tools\tw\warp_tests\ao_filter): noisy
  AO 0.144 -> 0.001, mean kept, sky untouched, also with the camera moving.

## v22 (2026-10-26)

- Attila's layout (ATN): Settings Mode HDR & Effects / Graphics 1 of 2 (Ambient Occlusion, Global Illumination,
  Screen-Space Shadows; no Anti-Aliasing: the game has TAA) / Graphics 2 of 2 (Graphics (Experimental) and the tests), Attila's colours
  (twattila/feature_settings.hpp).
- Screen-Space Shadows (Bend Studio's, twattila/screen_space_shadows.hpp): the sun shaders that compare the shadow maps get every comparison scaled; the ones that read the screen-space
  shadow mask (t_shadow) get its reads scaled. 5 of the 7 sun draws: 76A707E5 and F9F8CE80 have no sun shadows (v31; before, a look-alike put it on the ambient occlusion of 3 of them).
- Graphics 2 of 2: the executable is protected (its code cannot be read), so names and types come from Pharaoh's code (Warhammer III's names are packed: all of Pharaoh's):
  Attila's 16 and 1185 More Tests (unsure; a name the game lacks shows as not found). At-start tests as
  Attila. Engine value lookup is one pass for all names (a search per name took minutes with 1000+). engine_values.txt lists them.
- Bloom less blocky (Rome II's fix): the tone mapper reads the bloom with a cubic B-spline over 4 x 4 texels (stock: one bilinear tap). Its bright
  pass is full resolution (no blocks there).
- No screen-space reflections added: the game has none to improve (cube maps only); a new pass is a
  follow-up.

## v16 (2026-10-24)

- Light Bounce: the G-buffer read through twattila/light_bounce_gbuffer.hlsli (MSAA builds for the games with MSAA;
  this one has none: unchanged).

## v15 (2026-10-23)

- AO Depth Levels (ATN: Rome II's frame time spikes gone with them): twattila/depth_levels.hpp builds the AO's depth at
  1/2 to 1/32 (5 mips of R32G32_UINT, one compute dispatch a level, depth_levels.cs_5_0.hlsl: each texel one depth texel
  of its 2 x 2 in a rotated grid, its bits and coordinate) before the Visibility Bitmask pass and binds them at t41
  (twwarhammer2/line_sweep_ao.hpp OnCollect: ssao01 its own t0, ssao02 the kept depth at t2). A sample 8 x 2^n to 16 x
  2^n depth texels away reads level n at that depth texel's own position (depth_levels.hlsli, BitmaskSampleLevel in
  bitmask.hlsli; as Scalable Ambient Obscurance). Same look; the far samples stay in the texture cache, so more
  Directions / Quality cost much less.
- Light Bounce levels: the prepared light and geometry with 5 more mips (twattila/light_bounce_levels.ps_5_0.hlsl after
  the prepare: light the mean of 2 x 2, geometry one of them in a rotated grid); the trace's far samples read them
  (BitmaskSampleLevel). Same look, cheaper at Full / Half resolution and large radii.
- AO presets (ATN): Thickness 0.11 in both Recommended presets; a yellow line above them: "Prefer darker shading behind
  trees and objects? Pick a preset, then raise Thickness (e.g. 0.20)."

## v14 (2026-10-23)

- Graphics values applied from the game's device (init_device), not only from its first present: the game parses its
  LOD values 0.8 s after the addon's load (+0x58cafc) and copies them per model at load (+0x1a0363c -> model + 0x84).
- New: Unit / Building / Tree / Grass / Object / Terrain Draw Distance (battle_cull_aabb_at_num_pixels_* 16 and
  campaign_* 8, divided: the pixels an object's bounds must cover to be drawn; copied by +0x1a51b3c at load; trees also
  campaign_tree_display_max_distance, objects battle_small_prop_cull_distance 500), Grass Density
  (grass_density_multiplier, 0.5 - 2x), Terrain Tessellation (terrain_tessellation_quality_battle 1 / _campaign 0.25).
  battle / campaign_shadow_split_0-3 not added: overrides, off by default (the game's own table then).
- Graphics values checked with tools\tw\tw_tweak_survey.py (every engine tweak of the exe and the code that reads it;
  it matches the watch traces): sliders whose values the game never reads removed, new ones on values it reads.
- Watch v2 (tw::engine_tweaks::watch): no crash at its end (Dr7 cleared in the handler while stopping), a getter's
  caller logged, new threads armed, readers logged as found; buttons "Watch graphics values (60 s)" and "... at the
  next start (5 min)" (ReShade.ini [Total War] WatchAtStart, once).

## v12 (2026-10-23)

- Watch graphics values (10 s) button: hardware data breakpoints on 4 engine values in every game thread
  (tw::engine_tweaks::watch), the reading instructions and counts to ReShade.log: the graphics traces showed every
  value found, written and kept, with no visible effect (ATN debug3 / debug4), so the game reads them elsewhere or
  only at some point.
- line_sweep_bitmask.hlsli: any group size, the includer's camera, the output's own size (unchanged here).

## v11 (2026-10-23)

- As Attila (ATN): Settings Mode "HDR & Effects" / "Graphics"; Light Bounce (Experimental, new section:
  twattila/light_bounce.hpp on the sun draws below); Ambient Occlusion and Light Bounce presets (Recommended,
  Recommended (heavier but looks better) with 4 AO directions, Reset) and Attila's tooltips; graphics presets without
  the AO; AO / Light Bounce titles light blue, two empty lines after Graphics. No AO Temporal Smoothing (its AO passes
  have no camera to reproject with).
- Sun draws: the 7 directionallight shaders (one per model via g_skin_enable), camera b1 (the 3K layout);
  light_bounce.hlsli: view-space normals from the 10-bit channel 1, albedo x 0.96.
- Trace graphics settings (button).

## v10 (2026-10-23)

- Settings Mode Image / Graphics (was Simple / Advanced; ATN): Image shows every tone mapping, grading, effect and
  output setting (the old Advanced-only ones too), Graphics only the graphics sections (Graphics, Ambient Occlusion);
  About in both. IsGraphicsSettings and one loop at the end of ConfigureAddon.

## v9 (2026-10-23)

- Ambient Occlusion Directions (2 / 4 / 8 slices around the view vector, default 4 as before; SI.ssao_directions):
  fewer is faster.

## v8 (2026-10-23)

- Ambient occlusion faster and Quality: twwarhammer2/bitmask_ao.hlsli takes Attila's bitmask.hlsli (fast acos,
  sectors), 8 / 16 / 32 samples per side (Ambient Occlusion Quality, SI.ssao_quality) and stops a slice once all
  sectors are covered.

## v7 (2026-10-23)

- Ambient occlusion: ATN's settings run the collect without depth (fx_ssao 0x2A629B2B: a plain 3 x 3 average), now
  replaced too (ssao02); its Visibility Bitmask gets the depth downsample's output (this frame's; since v32 the sweep's
  depth) at t2. The bitmask code shared by both collects: line_sweep_bitmask.hlsli.
- Graphics: the game copies its LOD values into each model when it makes it (+0x1419e7eed), so the sliders apply from
  the next battle or campaign load (tooltips); Attila's names (... Distance, Object -> Model Detail).

## v6 (2026-10-22)

- Graphics (Experimental): Unit, Building, Tree, Grass, Object, Terrain and Effects Detail (0.25 - 8x): the game's
  num_pixels_per_triangle_lod_change_* tweaks divided by the slider (Warhammer III's layout: lea r8, [name]; lea rcx,
  [object]; value at +0x60; read live by the LOD code), Reset and MAX. Untested.
- Ambient Occlusion (Experimental, needs in-game SSAO): Visibility Bitmask in the line sweep's collect
  (ssao01_0xEE5C29AE, written from the disassembly: the game's collect or the bitmask over a 10x10 tile with the
  game's 3x3 depth weights), the camera from the tonemap (t00 b1, bound at b1). The AO trace also logs the collect.
  Untested; if it shows no change, the game may use its other collect (0x2A629B2B).
- The present event registered without FIX_BLOOM too.

## v5 (2026-10-22)

- Graphics (Experimental), Ambient Occlusion and their preset buttons: Advanced only (the round-4 check never hid
  them).
- Ambient occlusion Radius default 1 (ATN; Thickness stays 0.15).
- Video AutoHDR and Video Dithering at the end of Extra (next to the post-processing settings).
- No live counters (no CPU work per frame for them).
- Output: SDR (gamma 2.2, 10-bit) and Auto (scRGB with Windows HDR on, else SDR); default Auto.
- PsychoV31: the handoff from the game's per-map Reinhard matches its slope at mid gray (as Warhammer II).
- Graphics (Experimental), Advanced: "Trace ambient occlusion" on the AO depth pass (now replaced code,
  ssao00_0x49F538CF), for the Visibility Bitmask AO.

## v4 (2026-10-22)

- PsychoV31 Highlight Hue Shift default 0.15 (ATN; Reset and the presets too).
- HDR presets: "Recommended (for 2000nits+)" (Bloom 0.25) is now Recommended; the other one removed (ATN).
- Graphics (Experimental, Advanced): AO pass counter and "Trace ambient occlusion" (the next frame's fx_ssao passes:
  their textures, UAVs and constants to ReShade.log; for the Visibility Bitmask AO with its overscan), and "Save game
  code" (Warhammer3.exe registers its engine values another way: to find them offline).
- HDR (Extended): the shoulder above 1 per channel (as Ryse fix32 / Rome Remastered v3; t00 ApplyHDR1PeakShoulder): bright fire turns
  yellow / white at its core as in SDR, not flat salmon.
- Bloom: no cap in WH3's own high pass (fx_high_pass): unchanged.

## v3 (2026-10-22)

- General: yellow note "Keep the in-game brightness at 100%."
- Presets: both Recommended: Reset, then PsychoV31 at its defaults and Video AutoHDR 0.20 (2000+ nits: Bloom 0.25);
  Reset turns Video AutoHDR off.
- Extra: Video AutoHDR / Video Dithering (cutscene00 and the blurred videos, cutscene01_0x6613007D).
- Not yet: the Visibility Bitmask AO (its line sweep, fx_ssao, works with an overscan the AO's mapping depends on:
  needs a trace) and engine sliders (Warhammer3.exe registers its tweaks another way).
- About: NexusMods button next to ATN's Ko-fi; every section title green, Graphics (Experimental) pink
  (settings.hpp default_section_tint / section_tints).

## v2 (2026-10-20)

- PsychoV31: Highlight Hue Shift 0 by default (ATN); a note above Highlight Saturation / Hue Shift / Saturation
  Shift ("Fire too pink, too orange or too pale? Change the settings below.", as Tales of Arise), Highlight
  Saturation moved next to them.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (Extended) / HDR (PsychoV30) / HDR (PsychoV31, new, in t00): the same PsychoV31 sliders as
  the other Total War mods (Musa's custom test31; Highlight Hue / Saturation Shift toward the SDR image's clip).
- `lensflare.hlsli`: the four flares' copies of ApplyWH3LensFlare once (ScaleLensOverlay), also for lensblood00: the blood
  splatter on the lens (its red reached ~9.5 on the 16-bit frame where the 8-bit one clipped at 1; now clamped and at
  Paper White as the flares).
- Shader packs (dump `bin\cmd_Decompiler-1.3.16\dump_shader_twwarhammer3_pack`; WH3's names are hashes, no entry points):
  the other "missing" ones are vertex shaders, the VR stereo correction pass, and DOF / summed-area-table permutations
  of other DOF qualities: the mod's DOF shaders are hand rebuilds for the DLSS mod's render scale, not ported blind.
- About: credits, ATN's Ko-fi.

Verify: SDR / Extended / V30 / V31; a unit taking damage close to the camera (blood splatter).
