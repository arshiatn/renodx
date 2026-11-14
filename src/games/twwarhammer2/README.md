# Total War: WARHAMMER II (RenoDX HDR, ATN)

### Round v49 (2026-11-14, ATN's session traces of 03:38-03:48; built only, not run in game)

- **Fixed below 100%: the screen-space reflections came out twice their size.** The SSR's copy (environment_specular_light
  ps40_copy 7F23C472) sampled its resolve's whole output while the resolve wrote only the render share; it now reads that
  share (a `view_share` region read, as Pharaoh's). The traces at DLSS Performance and Ultra Performance drew it at 1920 x 1080
  and 1280 x 720 over a 3840 x 2160 texture. The scale census on all 26 traces finds no other pass below 100%
  (`tools/tw/wh2/scale_census_wh2_atn2.txt`; the unit portraits' tone mapper runs a display-size chain of its own).
- **Torch and Fire Shadows now cover the spot lights** (spotlight F550D451, gobospotlight 0764179D; 26 in the siege, 1 in the
  campaign): the traces' instance rows give each one's place (POSITION1), range (POSITION2.x) and colour (POSITION4). Our copies
  of both shaders put the rays' visibility on the light the surface takes; their light shafts stay the game's. They rank among
  the torches by their colour but light nothing in the paths, the fog or the glow (a spot would light all around there).
  Shared: `rt_scene::InstallPointLights` takes where a light family's instances keep each light (`LightInstances`, the point
  lights' by default) and a second family. WARP `tools/tw/warp_tests/spot_wh2` passes: nothing bound, the game's bits; each
  pixel's bit exact; a light the rays don't have, as the game draws it; shafts kept (3Dmigoto's -pi in the shafts' atan2 put
  back to the game's).
- **New: Smoke Shadows (Experimental, off).** Every lit particle in the traces is one indirect draw of 1FC776EF (a GPU-sorted
  list, 19597 in the siege) with F32116B4: the same vertex shader family and outputs as Pharaoh's, so Pharaoh's quad shader at
  F32116B4's slots (now `twattila/rt_particle_quads_instancing.hlsli`, Pharaoh's bytes the same). The rays' capture streams
  indirect draws out (`SmokeVertexShader::indirect`: 65536 records emptied first, the GPU's count filled). WARP
  `tools/tw/warp_tests/smoke_wh2` passes: places, sizes, opacity and mip level as the game's shaders draw them, every record
  past the GPU's count empty (its controls fail).
- **The campaign's trees and plants in the rays** (rigidcampaignvegetation 41147CD3, 156 draws a frame, in no scene before):
  as drawn (their wind, their lean toward a high camera), cut out by their diffuse map's alpha. GPU check: the capture's world
  position times the camera is the game's SV_Position (1e-8 relative).
- **Shadow Casters Out of View (Experimental, off) takes two more of the shadow pass's draws**: the campaign trees' 7D979F60
  (49 draws, cut out by 5906CF6E's alpha test) and, with Far Tree Shadows, the far trees' cards 75914932 (cut out by
  B7ED5B72's, at the nearest of the two views it blends; a card it fades out stays out). Pharaoh's `rt_outside_view` has two
  new variants for them (`RT_WORLD_UV`, `RT_CARD_SPLIT`; its other variants' bytes the same). WARP
  `tools/tw/warp_tests/outside_view_wh2` passes (its controls fail).
- **Fixed: alpha-tested props cast solid shadows in the rays.** Fences, palms and other cut-out props of the rigid shader
  (16AFAF14) cast their cut-out shadow now, as the game's shadow pass does: its five alpha-tested G-buffer materials (the
  traces' census of every scene shader's pixel shaders) are cut by their diffuse map's alpha at 0.5, as Troy's and Pharaoh's
  (`rt_capture_rigid_standard_cut_out`; its world on the GPU: 1.4e-8 relative). The campaign's objects go through one of
  them (the shroud's material), so they are cut where their textures are.
- **New: Light Through Foliage (Experimental, off)** for Bend's screen-space shadows, as Troy's and Pharaoh's: the
  G-buffer's foliage bit (128 in channel 3's alpha byte, which the game's own sun shaders test). In the traces leaves, far
  cards and grass set it, and so do the blend meshes (76B5602C, the banners' too) and the campaign's terrain-blend objects
  (41FD3F65, 255), as the game's own code reads them (`tools/tw/wh2/material_writes_wh2.txt`).
- **Rays Replace Shadow Maps through the shared hook** (`twattila/sun_replace_maps.hpp` with its sampler slot, s1 here):
  the same as before.
- Checked, nothing to change: the siege's walls, buildings, units, trees and terrain are all in the rays (the caster census
  on traces 17-30: every G-buffer draw of scene geometry held; the shadow pass's casters out of view as v48). The campaign's
  far terrain (geometry_41_global CC981FF9) stays out: its 49 chunks lie under the 161 near terrain patches the rays have
  (both drawn into the G-buffer, the shadow pass draws it alone), so the two would shadow each other.
- The rays' cost in ATN's session (the helper's GPU ms of one frame in each mode, trace + reflections): campaign 3.8 + 3.2 at
  DLAA, 1.0 + 1.0 at DLSS Performance, 0.4 + 0.5 at Ultra Performance; siege 6.9 + 4.7, 1.7 + 1.3, 0.8 + 0.7.

Build: `cmake --build build --config Release --target renodx-twwarhammer2.addon64`. Test list (RT/PT, DLSS Performance):
1. A battle by water or wet stone: reflections sit where they belong (before: stretched to twice their size).
2. A siege at night with Torch and Fire Shadows: the city's spot lights shadow walls and soldiers; their cones as before.
3. Smoke Shadows on, a burning siege: the smoke shades the ground and walls under it. Send ReShade.log (1FC776EF's line).
4. The campaign: forests take ray-traced AO and shadows, no dark patches under trees; frame time as v48.
5. Shadow Casters Out of View on, the campaign at a low camera: trees just off screen shadow into it as leaves, not cards;
   in a battle with Far Tree Shadows on, distant forests behind the camera keep their shadows.
6. Fences, palms and bushes in a battle: their ray-traced shadows show the gaps; campaign towns' shadows as v48.
7. Bend's shadows, Light Through Foliage 0.5: lighter shadows under trees and grass, walls and soldiers as dark as before.

Not done:
- The campaign's far terrain (above).
- Smoke Shadows' lower-effects twins DDABB799 / 687378DB drew in no trace (one slot below F32116B4's: a shift away).
- Out of the rays: the depth-only EF11AD35 (no world to read) and the floating banners 7DF335FF (the game gives them none).
- The rigid default shader 6B25B4D2 also draws the battle water, the campaign sea and overlays into the G-buffer; they stay
  casters (the water is opaque there, nothing under it shows; listing its 25 traced materials would drop untraced ones).
- Particle Resolution (Attila's particle layer): this game's lit particles are one GPU-sorted indirect draw of their own
  vertex shader, not Attila's; the layer needs an indirect path first (a shared change).
- Water Clarity / Caustics: the battle water and the campaign sea are opaque G-buffer materials of the rigid default shader,
  none of Pharaoh's water shaders; their operands not yet read.

### Round v48 (2026-11-14, Attila's package with the data run's findings; built only, not run in game)

- **Graphics 1 of 2 as Attila's and Pharaoh's** (`twattila/graphics_page.hpp`): Reset, No RT/PT (Recommended) and RT/PT;
  one method slider each for Shadows, Ambient Occlusion, Global Illumination and Reflections, the rest under More Options.
  FSR and XeSS beside DLSS (with their Frame Generation); a saved DLSS becomes FSR on AMD, XeSS on Intel. New: Texture LOD
  Bias on All Textures (Experimental, off). Every saved setting kept. RT/PT here: the rays' sun shadows on top of the game's
  shadow maps, path-traced AO and Global Illumination, ray-traced reflections, torch and fog shadows. Bend's
  screen-space shadows now stay off while the rays' sun shadows run (before, both could).
- **New, Experimental, off** (the data run's traces: one shader each, in every battle):
  - Torch and Fire Shadows from rays (the point lights' draw, VS D2A0FF23 / PS D96A8DC3: 131 lights in run B, 25 m reach)
    and from the screen-space shadows (Bend, no rays): the same lit bits in our copy of the light shader.
  - Rays Replace Shadow Maps (the six sun shaders read the atlas only through s1: an ALWAYS sampler there) and Skip the
    Shadow Maps under it (Ray-Traced Shadows Only skips the atlas's casters). Cloud shadows stay the game's.
  - Fog Shadows (the fog's in-scattering 492B87C7: the march's steps within 400 m take the rays' visibility, further on the
    game's maps) and Torch and Fire Light in Fog. Three decompiler errors fixed against the game's bytecode.
  - Path-Traced AO ("Path-Traced (ReSTIR GI, Experimental)"): the paths' sky is the game's own ambient, the spherical
    harmonics every battle uses (the cube without them), and their sun the game's; the sun shaders' diffuse ambient is taken
    away at its one place (`screen_space_shadows::PatchAmbient`, Warhammer II's form; WARP on all 31).
  - Far Tree Shadows (the far trees' cards, both kinds, turned to the sun), Shadow Casters Out of View (terrain, buildings
    and soldiers the shadow pass draws outside the view) and Hills Out of View.
- **Graphics 2 of 2 as Attila's:** 23 more of Attila's tests (Tree LOD Scale, Projected Tree LODs, Object Detail
  Distances, Buildings Never Vanish, Sort Particles, All Soldiers as Sprites, Campaign Cloud Distance and more); Reset puts
  the tests back too. Out: 12 rows whose values the game never finds (Far Buildings, Building Collapse Effects /
  Animations and 9 More Tests). Offline, every engine value of the page is found as the game finds it (231 of 231;
  `tools/tw/find_emu.py` on Warhammer2.exe reproduced the 2026-10-27 run's 12 misses first). Tooltips give this game's values.
- **Fixed below 100%: two compute passes no longer squeezed twice** (the MSAA SSR resolve and the particle collision map
  their uv to the viewport themselves; `view_share::SetViewportChains`, as Pharaoh's).
- **Hue Clip Emulation's default measured on this game's own tone mapper** (`test/tw_psychov35` part 5, at three burns):
  100 stays.
- Checked in the data run, nothing to change: the render scale reached the traced frames (A: viewports 3840 / 2560 / 1920 /
  1280); the bloom reads its render region (uv x c29.zw); Ray Reconstruction's guides decode as the game's light shaders do.

Build: `cmake --build build --config Release --target renodx-twwarhammer2.addon64`. Test list (a battle benchmark, DLSS Quality):
1. Your saved settings: the picture as v47. Send ReShade.log.
2. Reset, then No RT/PT: Bend's contact shadows, GT-VBAO, GT-VBGI on. Graphics 2 of 2: "Engine values found", no misses.
3. RT/PT (Path-Traced AO, torch and fog shadows on): open ground as bright and as coloured as with No RT/PT.
4. Torch and Fire Shadows (rays) on, a siege at night: walls and soldiers shadow the torch light. Then Bend's at 8, rays off.
5. Rays Replace Shadow Maps, then Skip the Shadow Maps: soft sun shadows alone; distant forests keep theirs with Far Tree
   Shadows on, and with Shadow Casters Out of View on nothing behind the camera loses its shadow.
6. Fog Shadows on, a foggy map with low sun: light shafts, no seam at 400 m; Torch and Fire Light in Fog at 1: glow.
7. Texture LOD Bias on All Textures on at DLSS Performance: sharper ground and walls, or shimmer? (decides its default)
8. DLSS Performance, a fire or flaming arrows: sparks bounce on the ground, not above or under it.

Not done:
- Smoke Shadows: the particles' vertex shader (C3300B96, one vertex a particle) writes the atlas cell's UV rectangle at
  TEXCOORD3, which Attila's capture reads into its record (centre, colour, size): it needs a WH2 record of its own.
- Ray-traced water reflections: no water pass of its own; the battle water (battle_water 8686041E) is drawn into the
  G-buffer and takes the same screen-space reflection resolve every surface does, which Reflections DXR replaces.
- Spot lights (5E0BC81E with PS 0764179D / F550D451): their data is instance rows too, but laid out apart from the point
  lights' (position at POSITION1, colour at POSITION4); the one traced sits about 3 km out with 1000 m reach.
- Out of the rays: the far cards' shadow version 75914932 (its outputs packed apart from Pharaoh's), the depth-only
  EF11AD35, the floating banners 7DF335FF (the game gives them no shadow). Near cards: the cull distance is the pixel
  shader's, so cards inside it cast with the 3D trees there.
- The night run (F) drew a day sun: night untested.
- Attila's two unit-size presets: this game has no Dead Unit, Building or Campaign Detail rows (ATN: which rows they map to).
- Shadow Slope Bias: its value has a constructor, but the game's lookup never finds it.

### Trace build (2026-11-14, for the data run; built only, not run in game)

- **Frame traces on demand and at 100%** (`twattila/frame_trace.hpp` Options, as Pharaoh's): `renodx_trace_now` next to the exe
  traces the next frame (looked for every 60 presents); `[renodx] FrameTrace=1` counts the 3D view at any scale. One draw each
  of the 11 single-sampled sun PS, the tone mapper, the point and spot lights, the battle fog, water, river and lit particles
  writes its constants (b0-b5, 256 floats); the particles' blend and their VS's buffers; instanced draws' counts apart. With
  `[renodx] FrameTraceReads=1` the point and spot lights' instance rows, also the frame after (`trace_reads.hpp`).
- **Run hooks** (`twattila/test_hooks.hpp`): `RENODX_TW_SET`, `RENODX_TW_SHOT`, `RENODX_TW_PROFILE`, timed from the first
  present; nothing without them. Every feature as before (page dump: the same but the Build Date).

Build: `cmake --build build --config Release --target renodx-twwarhammer2.addon64`. Manual check: in a battle, an empty file
`renodx_trace_now` next to Warhammer2.exe: within a second `tw_frame_trace.txt` appears and ReShade.log says
"Frame trace: N lines saved".

### Round v47 (2026-11-14, shared code from Attila / Pharaoh; built only, not run in game)

- **Fixed: the Ray Tracing Debug View's Screen-Space AO was magenta.** No AO pass hook keeps it here (the game's AO is compute line sweeps), so it is now what each sun draw reads at its `t_ssao` (the game's SSAO or GT-VBAO, blurred), copied before the rays' AO and the AO filter take its place: `rt_scene::InstallSunDebugAmbient` (`twattila/rt_scene.hpp`, Pharaoh's hook moved there). Its mapping from the shaders' bytes: all 30 sun draws with an AO slot read it at `(SV_Position + g_vpos_texel_offset) x g_screen_size.zw`, as their G-buffer, which is the Debug View's own mapping (`tools/tw/debug_ao_v47/ao_mapping.py`; the same bytes in Pharaoh's 24, where a trace proved it; Warhammer III's read it through their overscans and fail the check). Page dump: the same but the Build Date.

Build: `cmake --build build --config Release --target renodx-twwarhammer2.addon64`. Manual check: with DLAA or an upscaler and the helper, Ray Tracing Debug View on Screen-Space AO is no longer magenta: it shows the AO of the Ambient Occlusion row's method (Game's or GT-VBAO; white open, darker in corners and under units) lined up with the picture, also below 100% render scale.

### Round v46 (2026-11-13, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Fixed: the Ray Tracing Debug View rows never showed.** They wait for `rt_scene::OnTonemapDrawn`, which this add-on's tone mapper hook never called (Pharaoh had the same gap, 041821eb); now right after the anti-aliasing's, as in Attila. Its Screen-Space AO view stays magenta: no AO pass hook here, and Pharaoh's copy at the sun draws is not ported. Found by the skill's `scripts/static_checks.py`; the page dump (which sets the flag itself) is the same but the Build Date.

Build: `cmake --build build --config Release --target renodx-twwarhammer2.addon64`. Manual check: with DLAA or an upscaler and the helper, the Ray Tracing Debug View row shows; its Sun Shadows view shows the rays' raw shadow as the picture.

### Round v45 (2026-11-12, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Bend Screen-Space Shadows: Auto Thickness removed** (ATN: it broke at some angles).

### Round v42 (2026-11-11, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Object Motion Vectors: each soldier keeps its own bones.** When the game packs its soldiers' bones into the animation pages more than once a frame (big battles), a later round's soldier at a reused row shared a key with an earlier one and could take its last-frame bones: false motion on still soldiers' shields, swords and spears under DLSS, FSR or XeSS. Keys are now this frame's and new after every page write (`twattila/object_motion.hpp`; the 64-page clear that gave page 65 page 1's key is gone). WARP `test/tw_unit_pack_rounds` (Attila's path; this add-on claims bones in the draw, so the keys are its fix).
- **Bend Screen-Space Shadows: Auto Thickness (Experimental, off)** (each object as thick as it is wide: fuller shadows behind walls and carts, poles stay thin) and **Softer Away from the Caster (Experimental, 0 = off)** (sharp at contact, softer farther away, by the sun's size). Every row off: today's shaders and picture (WARP `test/tw_screen_space_shadows`, to the bit).

Build: `cmake --build build --config Release --target renodx-twwarhammer2.addon64`. Manual check: a paused big battle, the camera around the armies and back, Object Motion Vectors on: weapons on still soldiers stay still. Screen-space shadows with the new rows off: the picture as before.

### AO depth-level recovery (F15, 2026-11-09, GPT-6 Astra; not run in game)

Failed depth-level texture/view creation now releases all partial views and retries at the same resolution after a one-second cooldown. Build: `cmake --build build --config Release --target renodx-twwarhammer2.addon64`. Manual check: use an AO mode that reads depth levels, change resolution/render scale, and verify stable AO; after a transient allocation failure, depth levels should recover without another resolution change. Failure injection and real D3D11 WARP readback results are in `docs/Attila-crash-audit-2026-11-08.md` at the repository root. No shader behavior was changed.

### Startup worker lifetime (2026-11-08, GPT-6 Astra; not run in game)

The startup audio-setting worker is now cancelled and waited for through its actual thread exit before unloading the add-on. Its progress flag alone did not cover capture and CRT cleanup. Manual check: unload/reload during the first minute with an audio-memory test enabled, then after it completes; no worker may execute unloaded code. Shared build and offline checks are recorded in HANDOFF.md.

Build: `cmake --build --preset clang-x64-release --target renodx-twwarhammer2.addon64`.

## v37 (2026-11-12, PsychoV35 round; built only, not run in game)

- **HDR (PsychoV35)** (Rendering Mode 4): Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`). Hue Clip Emulation (0 - 100, default 100) toward the stock Extended Reinhard on the luminance and its per-channel clip (WH3's form); default 100 (part 5's WH3 form, which gives WH3's own 1.00). Recommended in PsychoV35; Reset stays HDR (Extended).
- Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain.
- Other modes: unchanged code paths (not bit-checked on this game; Attila's and WH3's are).

## v36 (2026-11-05, built only, not run in game; renodx-tw-dlaa64.exe helper89, bridge v29: ship them together)

- **Shared helper:** cloth and sails can trace without shading themselves (WH3's Smooth Banner Shadows, batch 5); no row here, nothing changes.
- **Shared helper:** ray-traced reflections without path tracing read last frame's colour squared back under L, M and Ray Reconstruction, and the Light Bounce without paths fills their radiance cache (WH3's batch 5).
- **A Windows crash report at each helper start, gone:** ReShade next to renodx-tw-dlaa64.exe loads this addon into it too; the addon said no in DllMain and the runtime's cleanup then faulted inside the loader (ntdll c0000005 at 0x272c5). The helper lived on. Now it says no in AddonInit and ReShade unloads it (WH3's batch 5; same lines here).
- **The helper's log:** a real fault leaves one line and renodx-tw-dlaa64.dmp; whose device a GPU reset removed, with its reason and the time.
- **Path-Traced AO learns 150 frames before it starts, not 30** (WH3's batch 5: a battle's first frames gave 30 % less light). AO under Ray Reconstruction stays REBLUR, as before (Attila and WH3 have a row for it). Not run in game.

## v35 (2026-11-03, built only, not run in game)

- **Path Tracing with the game's or Bend's shadows**: the paths now get the sun's direction there too (as Attila's v135).
- **Ray-Traced Reflections with the game's or Bend's shadows**: what they meet off screen now has the sun on it (as Attila's v135).
- **Frame generation with Output SDR**: the picture no longer turns red (as Attila).
- **Frame generation with Output scRGB**: works when turned on in game, no restart (as Attila). Ship with the helper built from
  this commit or later, not helper71.
- **Gamma Correction** (Rendering Mode SDR, on by default). On: any in-game Gamma shown as gamma 2.2, today's picture. Off:
  the game's own picture at the in-game Gamma. Rendering Mode SDR on Output SDR ignores Paper White (as Attila).
- **Extra in Rendering Mode SDR**: Lens Flare, Vignette, Distortion and Sharpening work there (at 1 the picture is unchanged). Film Grain and Indicators stay HDR-only.
- **Wrong constants from the decompiler** (checked against the game's own shaders):
  - In-game depth of field: the sharp range around the focus was about 30% too narrow (f x f, the game has 2 x f x f).
  - Branches and leaves in motion vectors and ray tracing: vertical wind strength (read the horizontal one).
  - Motion vectors: emissive wind mask Y of units with it.

## v34 (2026-10-27, batch 5, untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper22** (in the zip; bridge v11, needs this addon): **ReSTIR GI v2** (compared with
  Embark Studios' kajiya, MIT / Apache 2.0, not copied): fireflies clamped, history only on a surface facing the same
  way, and every third frame the reused light is checked again: a light switched on or off shows within 3 frames
  (was about 20). **Partitioned TLAS** fixed (its update operations stalled the GPU after BLAS compaction) and on
  again. **Mesh instancing**: instanced draws build one BLAS placed once per instance.
- **Light Through Leaves** (off by default, with Ray-Traced Sun Shadows): leaves let a quarter of the sun through
  each, cloth and sails about a third; only where the rays are the shadow (past the game's own shadow maps).

## v33 (2026-10-27, untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper21** (in the zip). Fix: the last zip's helper19 built a partitioned TLAS (NVAPI) that
  stalled every ray tracing request by 0.3-0.8 s in the GPU test; it stays off now (RENODX_TW_PTLAS=1 turns it on for
  tests). **ReSTIR GI** for Path Tracing: each texel's paths are resampled with last frame's at the same surface and with
  five neighbours' (their solid angle's Jacobian; a last ray checks the chosen surface is in view): in the GPU test about
  a quarter of the noise at one path a texel, the same brightness.
- Frame Generation **Dynamic** (NVIDIA's dynamic multi frame generation, up to 6x for the display's refresh rate) and
  **5x / 6x** (RTX 50 series).
- **Ray Tracing preset** buttons Low / Medium / High / Ultra: shadow softness, AO rays and rate, Light Bounce rays, path
  bounces, reflection samples (High: the defaults).
- **Ray-Traced AO Rate**: Quarter traces one pixel of each 2 x 2 a frame, in turn (DLSS smooths it): a quarter of the
  rays.
- With **Ray Reconstruction** it is the only denoiser: the Light Bounce's denoiser, the AO filter and the reflections'
  accumulation and filter step aside (the reflections go out as traced).
- Less CPU work a frame: static meshes the ray tracing scene already holds are recognised by a short key (shader, draw
  arguments, first vertex and index buffer) and only marked drawn.
- **Ray-Traced Grass** (off by default): the grass casts ray-traced shadows and shade too (its blades captured every
  frame through capture versions of the game's grass vertex shaders, alpha-tested; within the units' share of
  triangles). Heavy with dense grass.

## v32 (2026-10-27, untested in game; the helper GPU-tested)

- **Path Tracing** (Experimental, Anti-Aliasing section, with DLAA / DLSS): the ray-traced Light Bounce's rays become
  paths (Path Tracing Bounces 1-4): a ray that meets a surface the camera does not see takes its light from a world
  radiance cache (filled from the screen every frame), else from the sun and the game's own lighting learned from the
  screen times the surface's colour, and goes on from there. The Light Bounce's resolve, denoiser and apply follow.
- **Ray-Traced Reflections** and **Ray-Traced Specular Occlusion** (Experimental, with DLAA / DLSS and in-game Screen
  Space Reflections on): GGX samples of each pixel's reflection (Ray-Traced Reflection Samples 1-8; mirrors one ray)
  traced through the scene in the helper and kept over frames by world position. Where they meet a surface its light
  replaces the screen-space march, which is then skipped (the environment_specular_light resolve cs 0x7E1CDD0A; its raycast and blurs are left out); the game's environment stays for the rest (the
  sky). Specular occlusion dims the environment where something blocks it.
- Fix: the Ray-Traced Light Bounce alone (without ray-traced sun shadows or AO) never got its textures.
- **Frame Rate Limit** (Anti-Aliasing section, with frame generation): NVIDIA Reflex's cap for the rendered frames
  (frame generation multiplies them). A cap the game always reaches keeps the frame rate steady: less VRR flicker on
  OLED displays.
- renodx-tw-dlaa64.exe **helper19** (in the zip, needed for the above): the paths and reflections as DXR 1.2 ray
  generation shaders with shader execution reordering (DispatchRays), **RTX Mega Geometry** (NVAPI cluster operations:
  the units built every frame as clusters into one cluster BLAS, in place of the refitted BLAS) and a **partitioned
  TLAS** (each frame only the instances that changed are written); DXR 1.1 runs the same effects without them. GPU test
  (tools\tw\rt_harness, the addon's bridge and the helper without the game): sun shadow, AO, a unit's shadow, mirror
  reflections, the sky, specular occlusion and the path-traced bounce come out as expected in DXR 1.2 and 1.1, with and
  without the NVAPI paths. NVIDIA NVAPI (MIT): NVAPI_LICENSE.txt.

## v31 (2026-10-27)

- **DLSS below 100%: the game's own passes** (ATN: post-processing, shadows, reflections and our effects at DLSS):
  twattila/view_share.hpp patches the game's shaders as they are created. Positions rebuilt from texture
  coordinates (point lights, decals, battle fog, water, refraction, particles, the game's SSAO, markers, the SSR
  ray casts) use the view's share of the targets, projected reads land in it, and passes that read the 3D image at
  the display's size read only its rendered part. After DLSS, depth of field, distortion, fog of war and photo
  mode read depth and distortion at the render share (twattila/render_scale.hlsli). WARP test: every patched shader
  of 8 dumps creates. Checked in game at 50% (Troy, Rome II): full frame, frame generation on.
- **Ray-traced shadows: the floating unit banners cast none** (ATN: waving army flags with wrong shadows): they were
  in the ray-traced scene (rigidtextureblend, 2 bones) with the pose of the frame they were first seen in. Out now, as
  the game gives them no shadow (WH2 checked in game: banner shadows gone, units and monsters keep theirs).
- **Ray-Traced Light Bounce** (Experimental, Anti-Aliasing section, with DLAA / DLSS and the Light Bounce on): the
  Light Bounce finds the light arriving at each texel with rays through the ray-traced scene (cosine-weighted, Rays
  1-16, Range 1-50 m) in place of its screen-space search: a ray's hit gives the prepared light where the camera sees
  that same surface (else nothing, as before), so no thickness guess behind what the camera sees and light from
  farther away. Its resolve, denoiser and apply are the Light Bounce's (rt_scene.hpp TraceLightBounce, helper
  rt_bounce.cs_6_5.hlsl; bridge v7, renodx-tw-dlaa64.exe helper16: the zip's helper must be the new one).

## v30 (2026-10-27, untested in game)

- **Ray-traced sun shadows and ambient occlusion** (Experimental, Anti-Aliasing section, with DLAA / DLSS and
  renodx-tw-dlaa64.exe): Attila's (rt_scene.hpp) on WH2's 16 scene shaders (Troy's family); no ambient occlusion pass
  of the addon's: the trace runs at the first sun draw and its ambient visibility multiplies the AO the sun draws read
  (rt_ambient_combine, before the AO filter). GPU test: 16 of 16 captures match.
- **DLSS Mode** (Anti-Aliasing, with DLAA; as Tales of Arise and Shogun 2): DLAA (100%), Ultra Quality (77%),
  Quality (66.7%), Balanced (58%), Performance (50%), Ultra Performance (33.3%), Custom (Custom Render Scale 1-100%;
  was a 33-100% slider: a saved scale below 100% is now the Custom value, pick Custom to use it).
- **DLSS render scale, checked below 100%**: render sizes are now whole units of the display's aspect ratio (16 x 9 at
  4K, as Tales of Arise, Ryse and Shogun 2; each side was rounded on its own before, so the aspect drifted a little and
  static pixels got motion toward the edges). **Texture LOD Bias** (Anti-Aliasing: Off / Matched / Sharper / Sharpest,
  default Sharper; Shogun 2's sampler clones, now twattila/texture_lod_bias.hpp, for the main view only): textures stay
  as sharp as at the display's resolution. Screen-space shadows keep their length on screen (their samples x the
  render share).
- Ray tracing (helper15, bridge v6): the helper loads the Agility SDK 1.619.6 from its D3D12 folder (DXR 1.2 on drivers
  with shader model 6.9: opacity micromaps for the leaves), units are refitted between rebuilds, and the sun's disk
  gives soft shadows (Sun Softness). Ray Reconstruction there now passes the colour's exposure and create flags (no
  change for these games). Ship renodx-tw-dlaa64.exe, D3D12\D3D12Core.dll and AGILITY_SDK_LICENSE.txt together.

## v29 (2026-10-26, untested in game)

- **Frame Generation (Experimental)** (Anti-Aliasing: Off / 2x / 3x / 4x, Reflex Boost; needs DLAA or DLSS), as Attila's
  v74: NVIDIA's DLSS Frame Generation in renodx-tw-dlaa64.exe (Streamline's sl.*.dll and nvngx_dlssg.dll next to it,
  STREAMLINE_LICENSE.txt), presenting into a child window over the game's; the HUD-less image from the tone mapper's
  output through the addon's swap chain shader. Needs an RTX 40 or newer GPU, a windowed or borderless game, Output
  HDR10 or SDR; turn the game's VSync off. GPU tests: 2 / 3 / 4 frames presented per rendered frame, the mouse goes to
  the game's window, the HUD-less image drawn without touching the game's bindings.
- Object motion: its target half the size (R32G32: the motion as two halves and the depth; 66 MB less at 4K) and its
  depth match absolute (4e-6, was 0.1% of the depth: surfaces drawn later in front no longer take an object's motion far
  away). twattila/object_motion.hpp now holds it (shared with Shogun 2's DLSS addon).

## v28 (2026-10-26, untested in game)

- Screen-space shadows fixed (ATN: lines, shadows pointing away from the sun): the shared code read the sun's direction
  from Attila's lighting buffer (b2, c0.y) and its camera's layout in every game. Each sun draw now names its own (from
  the game's shader dump: tools\tw\sun_draw_layouts.py): its lighting at b2 / b3 / b4. WARP check
  (tools\tw\warp_tests\sss\sss_scene.cpp, the sun direction at c0.y / c0.x / c1.x): the same shadows.
- Graphics 2 of 2 trimmed (ATN): More Tests keep only campaign map, grass & trees, units, buildings and culling (More
  Tests: Buildings & Culling); Attila's tests stay.
- Graphics 2 of 2: the debug buttons Attila no longer has removed (Trace graphics settings, the two Watch buttons, Trace
  ambient occlusion).
- Fixed: with Global Illumination on, the Light Bounce drew the sun draws itself and the anti-aliasing never saw the
  main camera and depth there (DLAA / TAA without them): its hook now runs first.
- Ambient Occlusion (GT-VBAO): Temporal Smoothing, Temporal Edge Fix and Denoiser as Attila's (twattila/ao_filter.hpp:
  before the frame's first sun draw, the AO texture the sun draws read goes through Attila's resolve (last frame's AO
  moved with the camera, held within this frame's 3 x 3) and its 5 x 5 depth-aware denoiser, then the sun draws read the
  result); the bitmask's noise moves each frame with Temporal Smoothing. GPU test (tools\tw\warp_tests\ao_filter): noisy
  AO 0.144 -> 0.001, mean kept, sky untouched, also with the camera moving.
- DLSS Render Scale and Ray Reconstruction (Experimental, as Attila's; Anti-Aliasing DLAA): below 100% the 3D image is
  drawn smaller and DLSS upscales it; Ray Reconstruction's guides from its G-buffer (normal angles, gloss in channel 1's
  z, specular colour in channel 3). The screen-space shadows and the AO filter keep to the 3D image's corner of the
  screen-size textures (Bend's RenderSize).
- DLSS / DLAA motion vectors for everything that moves on its own (as Attila's v69-v70: twattila/anti_aliasing.hpp
  object motion), 24 of the game's vertex shaders: grass, leaves, branches, campaign vegetation, units (2- and 4-bone,
  animated overlays, wind, dismembered), rigid objects, sails, ropes, both oceans. Made by
  tools\tw\attila_motion\port_motion.py (classify_motion.py picks them from the dump; asm_to_hlsl.py where 3Dmigoto
  crashed, bit-exact); GPU check test_motion.py: every motion shader's SV_Position bit-identical to the game's, last
  frame's position as the game's shader at last frame's camera / time / placement.
- renodx-tw-dlaa64.exe (bridge v4, rebuilt): ray tracing BLASes compacted; switching between Ray Reconstruction and DLSS
  / DLAA frees the other's NVIDIA feature; the addon waits up to 5 s for a new NVIDIA feature (was 100 ms: changing the
  preset or Ray Reconstruction in game turned DLAA off).


## v27 (2026-10-26, untested in game)

- DLAA bridge v4 (Attila's ray tracing and Ray Reconstruction share it): this addon needs the renodx-tw-dlaa64.exe in
  this zip (an older helper refuses it, and DLAA stays off). Nothing else changes here: the shared anti-aliasing,
  screen-space shadow and motion code took Attila's additions without using them.

## v26 (2026-10-26)

- The DLAA helper (renodx-tw-dlaa64.exe) updated with Attila's DLSS render scale (its shared block v2: the output size);
  DLAA here works as before. Use this helper with this addon (an older one is refused).

## v25 (2026-10-26)

- Anti-Aliasing DLAA (NVIDIA's, RTX; ATN): NVIDIA's DLSS runs only in 64-bit processes, so a 64-bit helper
  (renodx-tw-dlaa64.exe next to the addon, with nvngx_dlss.dll; twattila/dlaa_helper, build.ps1) runs it on textures
  and a fence shared with the game (twattila/dlaa_bridge.hpp / dlaa_bridge.h): this frame linear in BT.2020, the
  scene's depth and the camera's motion (aa_dlaa_prepare: the TAA's jitter and cameras), NVIDIA's result back to the
  2.0 encoding (aa_dlaa_finish). The game's GPU waits for the helper's fence value only after it answered (no answer in
  100 ms, 5 s the first time while NVIDIA sets up: DLAA off, the frame as it is). DLAA Preset K / M / L. Motion is the
  camera's only: units, flags and swaying grass / trees can smear (their own motion vectors are the next step).
  Tested on the RTX 5090 without the game (tools\tw\warp_tests\dlaa\bridge_test.cpp, 32-bit: 117 of 120 frames
  through DLAA, output finite); not yet in game.

## v24 (2026-10-26)

- Attila's layout (ATN): Settings Mode HDR & Effects / Graphics 1 of 2 (Ambient Occlusion, Global Illumination,
  Screen-Space Shadows, Anti-Aliasing) / Graphics 2 of 2 (Graphics (Experimental) and the tests), Attila's colours
  (twattila/feature_settings.hpp).
- Screen-Space Shadows (Bend Studio's, twattila/screen_space_shadows.hpp): the gather 5.0 sun shaders (what current GPUs run) get every shadow map comparison scaled (the shadow is
  their filtered average). The older pcf / gather 4.1 / software variants read a resolved shadow buffer: not
  patched.
- Graphics 2 of 2: the executable is protected (its code cannot be read), so names and types come from Pharaoh's code, the ones its exe holds:
  Attila's 16 and 523 More Tests (unsure; a name the game lacks shows as not found). At-start tests as
  Attila. Engine value lookup is one pass for all names (a search per name took minutes with 1000+). engine_values.txt lists them.
- Bloom less blocky (Rome II's fix): the high passes average every source pixel a bloom texel covers (twattila/bloom_high_pass.hlsli; stock:
  one tap) and the bloom is read with a cubic B-spline over 4 x 4 texels (the tone mapper; stock: one bilinear tap).
- Sky Debanding (Attila's, twattila/sky.hlsli) on skydome main / gbuffer, in Extra, default Off.
- No screen-space reflections added: the game has none to improve (cube maps only); a new pass is a
  follow-up.

## v17 (2026-10-24)

- Light Bounce with the game's MSAA (ATN: "support every AA"): its *_ms directional light draws hooked too (20: slm / skinlm, edges / interior, every shadow method,
  from the pack dump: each one's G-buffer slot and camera), the light buffer resolved for the trace, the prepare and the
  apply as *_ms builds (twattila/light_bounce_gbuffer.hlsli: Texture2DMS, the prepare at sample 0, the apply per
  sample as the game's MSAA lighting). FXAA / MLAA / off were already fine.

## v16 (2026-10-23)

- AO Depth Levels (ATN: Rome II's frame time spikes gone with them): twattila/depth_levels.hpp builds the AO's depth at
  1/2 to 1/32 (5 mips of R32G32_UINT, one compute dispatch a level, depth_levels.cs_5_0.hlsl: each texel one depth texel
  of its 2 x 2 in a rotated grid, its bits and coordinate) before the Visibility Bitmask pass and binds them at t41
  (line_sweep_ao.hpp DispatchInstead: the kept depth, t2). A sample 8 x 2^n to 16 x 2^n depth texels away reads level n
  at that depth texel's own position (depth_levels.hlsli, BitmaskSampleLevel in bitmask.hlsli; as Scalable Ambient
  Obscurance). Same look; the far samples stay in the texture cache, so more Directions / Quality cost much less.
- Light Bounce levels: the prepared light and geometry with 5 more mips (twattila/light_bounce_levels.ps_5_0.hlsl after
  the prepare: light the mean of 2 x 2, geometry one of them in a rotated grid); the trace's far samples read them
  (BitmaskSampleLevel). Same look, cheaper at Full / Half resolution and large radii.
- AO presets (ATN): Thickness 0.11 in both Recommended presets; a yellow line above them: "Prefer darker shading behind
  trees and objects? Pick a preset, then raise Thickness (e.g. 0.20)."

## v15 (2026-10-23)

- Removed (never read): Building Detail (battle_building_lod*), Campaign Detail (campaign_tree / grass_lod*,
  rigid_campaign_tree_lod not run in play).
- Shadow Map Size: shadowmap_extreme_quality is never read; now the highest Shadows setting's sizes written into its
  code (+0x7e157d: 8192.f, 8192.f, 4096 -> x1.5, engine_tweaks code constants): 8192 (game's) / 12288. The lower
  settings use 4096 x shadow_resolution_multiplier (not changed).
- New: Bush Detail Distance (scrub_lod_distance_default), Campaign Draw Distance (campaign_culling_ratio, divided),
  Shadow Detail Distance (shadow_lod_distance_scaler, divided; the game sets it per Shadows setting), Small Object
  Shadows (cull_small_shadow_start / end_size, divided). far_buildings is on by default (not added).
- Graphics values checked with tools\tw\tw_tweak_survey.py (every engine tweak of the exe and the code that reads it;
  it matches the watch traces): sliders whose values the game never reads removed, new ones on values it reads.
- Watch v2 (tw::engine_tweaks::watch): no crash at its end (Dr7 cleared in the handler while stopping), a getter's
  caller logged, new threads armed, readers logged as found; buttons "Watch graphics values (60 s)" and "... at the
  next start (5 min)" (ReShade.ini [Total War] WatchAtStart, once).

## v13 (2026-10-23)

- Game's ambient occlusion costs what it did again: the replaced AO pass is the game's code only and the Visibility
  Bitmask is the addon's own 8 x 8 compute shader dispatched in its place (twwarhammer2/line_sweep_ao.hpp
  DispatchInstead, the addon's settings bound at b13). Before, one 1024-thread shader held both (fxc X4714: fewer
  threads in flight even for the game's path). Here in place of the collect (ao_bitmask_collect.cs_5_0.hlsl).

## v12 (2026-10-23)

- Ambient occlusion black after loading / afterimage (ATN's AO and pixel traces): at ATN's settings the blur
  (0xC7B1F6F7) never runs and the lighting reads the collect's output, which the addon skipped. The Visibility Bitmask
  is the collect's now (twwarhammer3/line_sweep_bitmask.hlsli, 32 x 32 groups, inv_projection at c17 of the sweep's
  camera still bound at b0) from this frame's depth: the depth downsample (0xF1CE6943, replaced with the game's code)
  has its input kept (line_sweep_ao KeepComputeDepth / KeepComputeCamera, bound at t2 / b0); the game's collect until
  then. The blur is the game's again (its replacement removed).
- Watch graphics values (10 s) button: hardware data breakpoints on 4 engine values in every game thread
  (tw::engine_tweaks::watch), the reading instructions and counts to ReShade.log: the graphics traces showed every
  value found, written and kept, with no visible effect (ATN debug3 / debug4), so the game reads them elsewhere or
  only at some point.

## v11 (2026-10-23)

- As Attila (ATN): Settings Mode "HDR & Effects" / "Graphics"; Light Bounce (Experimental, new section:
  twattila/light_bounce.hpp on the sun draws below); Ambient Occlusion and Light Bounce presets (Recommended,
  Recommended (heavier but looks better) with 4 AO directions, Reset) and Attila's tooltips; graphics presets without
  the AO; AO / Light Bounce titles light blue, two empty lines after Graphics. No AO Temporal Smoothing (its AO passes
  have no camera to reproject with).
- Sun draws: slm (6, camera b1) / skinlm (5, camera b2); light_bounce.hlsli (spherical normals; skin keeps the angles
  in its upper bits).
- Traces for the AO bug (black after loading, afterimage): Trace ambient occlusion (ao_trace.hpp on the collect
  0x19CF3C83 and the blur 0xC7B1F6F7), Pixel trace (a tree in Graphics; warhammer2_pixel_trace.txt), Trace graphics
  settings.

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

- Ambient occlusion: the Visibility Bitmask moves into the AO's horizontal blur (line_sweep_blur.hlsli), from that
  pass's own depth and camera (this frame's) over its own size, then a 5 x 5 depth-aware average; the collect is
  skipped with it (ATN: Warhammer II's marks stayed). line_sweep_ao.hlsli removed.
- Shadow Map Size: the game's shadowmap_extreme_quality (engine_tweaks.hpp flag tweaks, 1 byte), which gives the
  highest Shadows setting an 8192 shadow map (its code: quality 4 / 5, enough video memory), set at the game's device
  creation. Untested.

## v6 (2026-10-22)

- Ambient occlusion Radius up to 10x (ATN).
- Ambient occlusion: the depth read over the part of its texture the game's blur uses (line_sweep_ao.hlsli
  depth_size), not the whole texture: Warhammer II's is larger, and its rest showed old frames (ATN's marks).
- "What I would use:" with Campaign Detail 4x (ATN).

## v5 (2026-10-22)

- Graphics (Experimental), Ambient Occlusion and their preset buttons: Advanced only (the round-4 check never hid
  them).
- Ambient occlusion Radius default 1 (ATN; Thickness stays 0.15).
- Video AutoHDR and Video Dithering at the end of Extra (next to the post-processing settings).
- No live counters (no CPU work per frame for them).
- Output: SDR (gamma 2.2, 10-bit) and Auto (scRGB with Windows HDR on, else SDR); default Auto.
- Ambient occlusion: the depth and camera come from the AO blur (now replaced code, ssao01 with
  line_sweep_blur.hlsli), so the collect always has them. Hooks on passes without replacement code never run, which
  skipped the collect and left stale AO (ATN's horizontal lines).
- Graphics presets: Reset, MAX, "What I would use:" (ATN).
- PsychoV31: the handoff from the game's per-map Reinhard matches its slope at mid gray, so maps are no longer much
  brighter or darker than SDR.

## v4 (2026-10-22)

- Ambient occlusion in its own section after Graphics, "Ambient Occlusion (Experimental, needs in-game SSAO)" (ATN),
  labels Strength / Radius / Thickness; defaults Strength 1, Radius 4x, Thickness 0.15 m (ATN). Advanced: "Ambient
  occlusion passes per frame" (twwarhammer2/ao_trace.hpp: counts the AO shaders' draws / dispatches; 0 = the game
  never ran them).
- Graphics (Experimental) and the AO section: Advanced only; both titles pink.
- PsychoV31 Highlight Hue Shift default 0.05 (ATN; Reset and the presets too).
- Graphics presets MAX (every slider at its maximum, Visibility Bitmask AO) and Reset.
- HDR (Extended): the shoulder above 1 per channel (as Ryse fix32 / Rome Remastered v3; hdr_to_screen.hlsli ApplyHDR1PeakShoulder): bright fire turns
  yellow / white at its core as in SDR, not flat salmon.
- Bloom (as Attila): the high pass's hard cap of the bloom source (luminance 25) is a logarithmic shoulder in HDR
  (bloom.hlsli; bloom00_0xE2587B5E / bloom01_0x4F050BBD): bright sources no longer all get the same flat halo. SDR unchanged.
- Visibility Bitmask AO: every depth sample's position at the centre of the depth texel it read (bitmask_ao.hlsli
  AoSamplePosition). Off-centre positions with a texel's depth made sloped ground occlude itself in a regular dot
  pattern (ATN's Warhammer II "matrix" pattern, debug1).
- ATN: the campaign slider and some unit sliders seem to do nothing. The unit distances are read every time from
  the tweaks (+0x142256720), per unit size class; campaign_tree_lod* has no direct reader (maybe looked up by name).

## v3 (2026-10-22)

- General: yellow note "Use these in-game settings: Gamma 22 and Brightness 10."
- Presets: Recommended: Reset, then PsychoV31 at its defaults and Video AutoHDR 0.20; Reset turns Video AutoHDR off.
  "Recommended (for 2000+nits)" removed (it only changed PsychoV30's Highlights: the same as Recommended in V31).
- Extra: Video AutoHDR / Video Dithering (cinematic00 and the blurred videos, cinematic01_0x492E3556).
- Graphics (Experimental): Ambient Occlusion Game's / Visibility Bitmask (default; Strength = an exponent, Radius
  1 = 2 m, Thickness). The line sweep (0x4D2C2BF5) is skipped, its depth and camera bound for the collect (ssao00_0x19CF3C83), which
  computes the AO (twwarhammer2/line_sweep_ao.hpp, line_sweep_ao.hlsli, bitmask_ao.hlsli: Therrien, Levesque, Gilet
  2023 on GTAO, as Attila's); the game's blur follows. Credit in About.
- Graphics (Experimental), engine tweaks (twrome2/engine_tweaks.hpp, as Pharaoh; Warhammer2.exe's 430 MB protection
  section is not scanned): Unit Detail per unit size (very small to very large, *_IMPOSTER_DISTANCE), Unit Animation
  (*_SHARED_POSE / FACIAL / HANDS), Unit Variety (*_NO_VARIANT), Footstep Effects, Model Detail (distance_lod_scaler),
  Terrain Detail, Building Detail (battle_building_lod0-3), Campaign Detail (campaign tree / grass LODs, rigid trees).
- About: NexusMods button next to ATN's Ko-fi; every section title green, Graphics (Experimental) pink
  (settings.hpp default_section_tint / section_tints).
- Check: in-game SSAO on, a battle: Ambient Occlusion Game's vs Visibility Bitmask (no dark halos around soldiers);
  a video with Video AutoHDR 0 / 0.2.

## v2 (2026-10-20)

- PsychoV31: Highlight Hue Shift 0 by default (ATN); a note above Highlight Saturation / Hue Shift / Saturation
  Shift ("Fire too pink, too orange or too pale? Change the settings below.", as Tales of Arise), Highlight
  Saturation moved next to them.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (Extended) / HDR (PsychoV30) / HDR (PsychoV31, new): the same PsychoV31 sliders as the
  other Total War mods (Musa's custom test31; Highlight Hue / Saturation Shift toward the SDR image's per-channel clip).
- `hdr_to_screen.hlsli`: the helpers t01 / t02 each carried once, ApplyWarhammer2Psycho (V30 / V31), the SDR / HDR ends.
- Shader packs (dump `bin\cmd_Decompiler-1.3.16\dump_shader_twwarhammer2_pack`), main passes that had no HDR:
  t03 / t04 hdr_to_screen without distortion (also ps40_main_sm40), t05-t08 with MSAA (ps50_main_high_ms; DOF and
  distortion on / off). lensflare00-03 (none were covered; the 2.2 frame at Paper White) with a new Lens Flare slider.
- Not ported: hdr_tone_mapper's basic / constant tone mappers (WH2 uses the Reinhard one, t00).
- About: credits, ATN's Ko-fi.

Verify: SDR / Extended / V30 / V31; in-game distortion off (t03 / t04) and MSAA on (t05-t08); Lens Flare slider.


### F1 helper lifetime repair (2026-11-09, GPT-6 Astra)

AddonUninit now shuts down the shared helper bridge after unregistering its users. The watcher exits before module unload; child process and IPC ownership are released. Shutdown drains outstanding GPU fence generations, with ordered CPU fence recovery only after confirmed helper exit. Failed launches clean up partial ownership and existing IPC sessions are not overwritten.

Offline checks: x86/x64 production-header tests cover process/thread cleanup, failure injection, 20 reloads with stable handle counts, and three interleaved D3D11/D3D12 WARP fence generations under normal completion and forced shutdown. No game was launched. Manually check helper-enabled unload/reload and device recreation, then repeat the reported battle. The original battle freeze is not proven fixed; unexpected mid-run helper failures remain audit F7.
