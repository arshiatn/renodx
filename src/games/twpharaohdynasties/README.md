# Total War: PHARAOH DYNASTIES (RenoDX HDR, ATN)

### Round v46 (2026-11-12, trace build for the data run; built only, not run in game)

- **Scripted runs:** `RENODX_TW_SET` / `RENODX_TW_SHOT` / `RENODX_TW_PROFILE` work here too (`twattila/test_hooks.hpp`, as Attila: the clock starts at the first present).
- **Frame trace on demand and at 100%:** an empty file `renodx_trace_now` next to Pharaoh.exe traces the next frame (looked for each second, then removed; any anti-aliasing, any scale); `[renodx] FrameTrace=1` also traces at 100% (600 presents of the 3D view). Both write `tw_frame_trace.txt` next to the exe, over the last one (the shared `twattila/frame_trace.hpp`, off in the other games; WH3's own marker is now this one).
- **The trace lists the sun's and the tone mapper's constants:** the first draw of the sun light shaders F883F5B2 (standard) and E238D4C7 (skin) and of the tone mapper 976C1C74 writes its VS and PS constant buffers b0-b5, up to 256 floats each (near / far, sun direction and colour, the ambient cube, the shadow splits and baked map).
- **Graphics sliders work from the first frame:** the four audio memory tests are gone (Sound Memory, Voice Memory, Streaming Memory, Streaming Manager Memory: keys `AudioDefaultMemory`, `AudioVoiceMemory`, `AudioStreamingMemory`, `AudioStreamingManagerMemory`). Their `SOUND_*` values don't exist in this game (never found in the 2026-10-27 run), and their at-start retry held every Graphics slider back for about 60 s at each start. Saved values of those keys are ignored.
- **DLSS / DLAA / Ray Reconstruction get the right brightness:** this game's frame is linear after its ACES tone mapper (976C1C74), but the shared anti-aliasing squared it as if it were Attila's square-root frame (0.01 reached DLSS as 0.0001, values at or below 0.00024 came back black, presets L / M and Ray Reconstruction 10x too dark at 0.01). Pharaoh's `aa_dlaa_prepare` / `aa_dlaa_finish` now define `TW_AA_FRAME_LINEAR 1` (the shared shaders' new constant; 0 elsewhere, every other game's compiled bytes unchanged). WARP on the game's bytes (513-value ramp): all within 1%.
- **Still dead, left for the Graphics 2 of 2 round** (their engine values were never found, 2026-10-27; the rows do nothing): `GraphicsFarBuildings` (Far Buildings), `GraphicsCollapseEffects` (Building Collapse Effects), `GraphicsCollapseAnimations` (Building Collapse Animations); More Tests `GraphicsTest_vegetation_create_default_soft_collision`, `GraphicsTest_CLOTH_FLAG_COMPOSITE_TEXTURE_OFFSET_X`, `GraphicsTest_CLOTH_FLAG_COMPOSITE_TEXTURE_OFFSET_Y`, `GraphicsTest_ENTITY_DISPLAY_ENABLE_ALPHA_CHANGE`, `GraphicsTest_FLAG_DISPLAY_REGISTRATION_ENABLED`, `GraphicsTest_texture_manager_drop_extra_campaign_entity_mip`, `GraphicsTest_BATTLE_VISIBILITY_HEIGHT_OFFSET`, `GraphicsTest_enable_rendered_visibility`.
- **Helper:** builds against today's `twattila/dlaa_bridge.h` (bridge 31; nothing here pins an older layout): ship with renodx-tw-dlaa64.exe helper105 (`build\tw_dlaa_helper105`), not an older one.

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: in a battle, create an empty `renodx_trace_now` next to Pharaoh.exe; within a second ReShade.log says "Frame trace: N lines saved to ...\tw_frame_trace.txt" and the file is gone. Graphics sliders apply at once after a start.

### Round v45 (2026-11-12, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Bend Screen-Space Shadows: Auto Thickness removed** (ATN: it broke at some angles).

### Round v42 (2026-11-11, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Object Motion Vectors: each soldier keeps its own bones.** When the game packs its soldiers' bones into the animation pages more than once a frame (big battles), a later round's soldier at a reused row shared a key with an earlier one and could take its last-frame bones: false motion on still soldiers' shields, swords and spears under DLSS, FSR or XeSS. Keys are now this frame's and new after every page write (`twattila/object_motion.hpp`; the 64-page clear that gave page 65 page 1's key is gone). WARP `test/tw_unit_pack_rounds` (Attila's path; this add-on claims bones in the draw, so the keys are its fix).
- **Bend Screen-Space Shadows: Auto Thickness (Experimental, off)** (each object as thick as it is wide: fuller shadows behind walls and carts, poles stay thin) and **Softer Away from the Caster (Experimental, 0 = off)** (sharp at contact, softer farther away, by the sun's size). Every row off: today's shaders and picture (WARP `test/tw_screen_space_shadows`, to the bit).

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: a paused big battle, the camera around the armies and back, Object Motion Vectors on: weapons on still soldiers stay still. Screen-space shadows with the new rows off: the picture as before.

### AO depth-level recovery (F15, 2026-11-09, GPT-6 Astra; not run in game)

Failed depth-level texture/view creation now releases all partial views and retries at the same resolution after a one-second cooldown. Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: use an AO mode that reads depth levels, change resolution/render scale, and verify stable AO; after a transient allocation failure, depth levels should recover without another resolution change. Failure injection and real D3D11 WARP readback results are in `docs/Attila-crash-audit-2026-11-08.md` at the repository root. No shader behavior was changed.

### Startup worker lifetime (2026-11-08, GPT-6 Astra; not run in game)

The startup audio-setting worker is now cancelled and waited for through its actual thread exit before unloading the add-on. Its progress flag alone did not cover capture and CRT cleanup. Manual check: unload/reload during the first minute with an audio-memory test enabled, then after it completes; no worker may execute unloaded code. Shared build and offline checks are recorded in HANDOFF.md.

Build: `cmake --build --preset clang-x64-release --target renodx-twpharaohdynasties.addon64`.

## v40 (2026-11-12, PsychoV35 round; built only, not run in game)

- **HDR (PsychoV35)** (Rendering Mode 4): Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`). Hue Clip Emulation (0 - 100, default 80) toward the stock ACES fit on each channel and its crossfade to the raw scene (`HueTransfer`); default 80 (part 5: the ACES fit). The three Recommended presets in PsychoV35; Reset stays HDR (Extended).
- Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain.
- Other modes: unchanged code paths (not bit-checked on this game; Attila's and WH3's are).

## v39 (2026-11-05, built only, not run in game; renodx-tw-dlaa64.exe helper89, bridge v29: ship them together)

- **Shared helper:** cloth and sails can trace without shading themselves (WH3's Smooth Banner Shadows, batch 5); no row here, nothing changes.
- **Shared helper:** ray-traced reflections without path tracing read last frame's colour squared back under L, M and Ray Reconstruction, and the Light Bounce without paths fills their radiance cache (WH3's batch 5).
- **A Windows crash report at each helper start, gone:** ReShade next to renodx-tw-dlaa64.exe loads this addon into it too; the addon said no in DllMain and the runtime's cleanup then faulted inside the loader (ntdll c0000005 at 0x272c5). The helper lived on. Now it says no in AddonInit and ReShade unloads it (WH3's batch 5; same lines here).
- **The helper's log:** a real fault leaves one line and renodx-tw-dlaa64.dmp; whose device a GPU reset removed, with its reason and the time.
- **Path-Traced AO learns 150 frames before it starts, not 30** (WH3's batch 5: a battle's first frames gave 30 % less light). AO under Ray Reconstruction stays REBLUR, as before (Attila and WH3 have a row for it). Not run in game.

## v38 (2026-11-03, built only, not run in game)

- **Path Tracing with the game's or Bend's shadows**: the paths now get the sun's direction there too (as Attila's v135).
- Known, not fixed: ray-traced grass shadows. The grass's opacity is a texture array and the layer changes inside a draw; the scene reads one
  plain texture. Solid, missing or wrong shapes are possible: only the game can tell.
- **Frame generation with Output SDR**: the picture no longer turns red (as Attila).
- **Frame generation with Output scRGB**: works when turned on in game, no restart (as Attila). Ship with the helper built from
  this commit or later, not helper71.
- **Gamma Correction** (Rendering Mode SDR, on by default). On: any in-game Gamma shown as gamma 2.2, today's picture. Off:
  the game's own picture at the in-game Gamma. Rendering Mode SDR on Output SDR ignores Paper White (as Attila).
- **Extra in Rendering Mode SDR**: Bloom, Lens Flare, God Rays, Vignette, Distortion and Chromatic Aberration work there (at 1 the picture is
  unchanged). Film Grain and Indicators stay HDR-only. So does Sharpening: RenoDX's own, which stock SDR doesn't have.
- **Wrong constants from the decompiler** (checked against the game's own shaders):
  - In-game depth of field: the sharp range around the focus was about 30% too narrow (f x f, the game has 2 x f x f).
  - Glow in height fog: the top plane read the bottom one (small).
  - Motion vectors: flame grass noise Y and feather wind on winged units.

## v37 (2026-10-27, batch 5, untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper22** (in the zip; bridge v11, needs this addon): **ReSTIR GI v2** (compared with
  Embark Studios' kajiya, MIT / Apache 2.0, not copied): fireflies clamped, history only on a surface facing the same
  way, and every third frame the reused light is checked again: a light switched on or off shows within 3 frames
  (was about 20). **Partitioned TLAS** fixed (its update operations stalled the GPU after BLAS compaction) and on
  again. **Mesh instancing**: instanced draws build one BLAS placed once per instance.
- **Light Through Leaves** (off by default, with Ray-Traced Sun Shadows): leaves let a quarter of the sun through
  each, cloth and sails about a third; only where the rays are the shadow (past the game's own shadow maps).
- **Ray-Traced Grass** now has Pharaoh's grass to capture (grass.hlsl, its blades every frame).

## v36 (2026-10-27, untested in game; the helper GPU-tested)

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

## v35 (2026-10-27, untested in game; the helper GPU-tested)

- **Path Tracing** (Experimental, Anti-Aliasing section, with DLAA / DLSS): the ray-traced Light Bounce's rays become
  paths (Path Tracing Bounces 1-4): a ray that meets a surface the camera does not see takes its light from a world
  radiance cache (filled from the screen every frame), else from the sun and the game's own lighting learned from the
  screen times the surface's colour, and goes on from there. The Light Bounce's resolve, denoiser and apply follow.
- Ray-traced reflections: not here (the game has no reflection pass for objects; its screen-space ones are
  the water's, and water is not in the traced positions). Their settings are hidden.
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

## v34 (2026-10-27)

- **DLSS below 100%: the game's own passes** (ATN: post-processing, shadows, reflections and our effects at DLSS):
  twattila/view_share.hpp patches the game's shaders as they are created. Positions rebuilt from texture
  coordinates (point lights, decals, battle fog, water, refraction, particles, the game's SSAO, markers, the SSR
  ray casts) use the view's share of the targets, projected reads land in it, and passes that read the 3D image at
  the display's size (the glow bright pass after DLSS: bloom lines up with the image) read only its rendered part. After DLSS, depth of field, distortion, fog of war and photo
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

## v33 (2026-10-27, untested in game)

- **Ray-traced sun shadows and ambient occlusion** (Experimental, Anti-Aliasing section, with DLAA / DLSS and
  renodx-tw-dlaa64.exe): Attila's (rt_scene.hpp) on Pharaoh's 16 scene shaders (Troy's, Pharaoh's hashes); GPU test:
  16 of 16 captures match.
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

## v32 (2026-10-26, untested in game)

- **Frame Generation (Experimental)** (Anti-Aliasing: Off / 2x / 3x / 4x, Reflex Boost; needs DLAA or DLSS), as Attila's
  v74: NVIDIA's DLSS Frame Generation in renodx-tw-dlaa64.exe (Streamline's sl.*.dll and nvngx_dlssg.dll next to it,
  STREAMLINE_LICENSE.txt), presenting into a child window over the game's; the HUD-less image from the tone mapper's
  output through the addon's swap chain shader. Needs an RTX 40 or newer GPU, a windowed or borderless game, Output
  HDR10 or SDR; turn the game's VSync off. GPU tests: 2 / 3 / 4 frames presented per rendered frame, the mouse goes to
  the game's window, the HUD-less image drawn without touching the game's bindings.
- Object motion: its target half the size (R32G32: the motion as two halves and the depth; 66 MB less at 4K) and its
  depth match absolute (4e-6, was 0.1% of the depth: surfaces drawn later in front no longer take an object's motion far
  away). twattila/object_motion.hpp now holds it (shared with Shogun 2's DLSS addon).

## v31 (2026-10-26, untested in game)

- Screen-space shadows fixed (ATN: lines, shadows pointing away from the sun): the shared code read the sun's direction
  from Attila's lighting buffer (b2, c0.y) and its camera's layout in every game. Each sun draw now names its own (from
  the game's shader dump: tools\tw\sun_draw_layouts.py): its lighting at b2 / b3 / b4, the sun direction at c1.x.
  WARP check (tools\tw\warp_tests\sss\sss_scene.cpp, the sun direction at c0.y / c0.x / c1.x): the same shadows.
- Graphics 2 of 2 trimmed (ATN): More Tests keep only campaign map, grass & trees, units, buildings and culling (More
  Tests: Buildings & Culling); Attila's tests stay.
- Fixed: with Global Illumination on, the Light Bounce drew the sun draws itself and the anti-aliasing never saw the
  main camera and depth there (DLAA / TAA without them): its hook now runs first.
- Ambient Occlusion (GT-VBAO): Temporal Smoothing, Temporal Edge Fix and Denoiser as Attila's (twattila/ao_filter.hpp:
  before the frame's first sun draw, the AO texture the sun draws read goes through Attila's resolve (last frame's AO
  moved with the camera, held within this frame's 3 x 3) and its 5 x 5 depth-aware denoiser, then the sun draws read the
  result); the bitmask's noise moves each frame with Temporal Smoothing. GPU test (tools\tw\warp_tests\ao_filter): noisy
  AO 0.144 -> 0.001, mean kept, sky untouched, also with the camera moving.
- DLSS Render Scale and Ray Reconstruction (Experimental, as Attila's; Anti-Aliasing DLAA): below 100% the 3D image is
  drawn smaller and DLSS upscales it; Ray Reconstruction's guides from its G-buffer (octahedral normal, gloss in channel
  1's z, specular colour in channel 3). The screen-space shadows and the AO filter keep to the 3D image's corner of the
  screen-size textures (Bend's RenderSize).
- DLSS / DLAA motion vectors for everything that moves on its own (as Attila's v69-v70: twattila/anti_aliasing.hpp
  object motion), 27 of the game's vertex shaders: grass, leaves, branches, campaign vegetation, units (2, 4 and 8
  bones, fur, feather helmets, dismembered), rigid objects, sails, ropes, both oceans. Made by
  tools\tw\attila_motion\port_motion.py (classify_motion.py picks them from the dump; 3Dmigoto's decompiler crashes on
  the 4- / 8-bone units: asm_to_hlsl.py, bit-exact); GPU check test_motion.py: every motion shader's SV_Position
  bit-identical to the game's, last frame's position as the game's shader at last frame's camera / time / placement; the
  feather-helmet units draw nothing with the test's random inputs (identical, not checked further).
- renodx-tw-dlaa64.exe (bridge v4, rebuilt): ray tracing BLASes compacted; switching between Ray Reconstruction and DLSS
  / DLAA frees the other's NVIDIA feature; the addon waits up to 5 s for a new NVIDIA feature (was 100 ms: changing the
  preset or Ray Reconstruction in game turned DLAA off).


## v30 (2026-10-26, untested in game)

- DLAA bridge v4 (Attila's ray tracing and Ray Reconstruction share it): this addon needs the renodx-tw-dlaa64.exe in
  this zip (an older helper refuses it, and DLAA stays off). Nothing else changes here: the shared anti-aliasing,
  screen-space shadow and motion code took Attila's additions without using them.

## v29 (2026-10-26)

- The DLAA helper (renodx-tw-dlaa64.exe) updated with Attila's DLSS render scale (its shared block v2: the output size);
  DLAA here works as before. Use this helper with this addon (an older one is refused).

## v28 (2026-10-26)

- Anti-Aliasing DLAA (NVIDIA's, RTX; ATN): NVIDIA's DLSS runs only in 64-bit processes, so a 64-bit helper
  (renodx-tw-dlaa64.exe next to the addon, with nvngx_dlss.dll; twattila/dlaa_helper, build.ps1) runs it on textures
  and a fence shared with the game (twattila/dlaa_bridge.hpp / dlaa_bridge.h): this frame linear in BT.2020, the
  scene's depth and the camera's motion (aa_dlaa_prepare: the TAA's jitter and cameras), NVIDIA's result back to the
  2.0 encoding (aa_dlaa_finish). The game's GPU waits for the helper's fence value only after it answered (no answer in
  100 ms, 5 s the first time while NVIDIA sets up: DLAA off, the frame as it is). DLAA Preset K / M / L. Motion is the
  camera's only: units, flags and swaying grass / trees can smear (their own motion vectors are the next step).
  Tested on the RTX 5090 without the game (tools\tw\warp_tests\dlaa\bridge_test.cpp, 32-bit: 117 of 120 frames
  through DLAA, output finite); not yet in game.

## v27 (2026-10-26)

- Attila's layout (ATN): Settings Mode HDR & Effects / Graphics 1 of 2 (Ambient Occlusion, Global Illumination,
  Screen-Space Shadows, Anti-Aliasing) / Graphics 2 of 2 (Graphics (Experimental) and the tests), Attila's colours
  (twattila/feature_settings.hpp).
- Screen-Space Shadows (Bend Studio's, twattila/screen_space_shadows.hpp): this game's shadow ends differently in each sun shader, so the patch scales every shadow map comparison
  (sample_c / gather4_c: the shadow is their filtered average) by the screen-space shadow. All shadowed sun
  shaders of the pack patch and create on WARP.
- Graphics 2 of 2: Attila's tests the game's code reads (16) and 1185 More Tests (every other graphics value Pharaoh's
  code reads; more_tests.h from tools\tw\tw_game_tests.py), at-start tests as Attila. Engine value lookup is
  one pass for all names now (twrome2/engine_tweaks.hpp: a search per name took minutes with 1000+). engine_values.txt lists them.
- Bloom less blocky (Rome II's fix): the high passes average every source pixel a bloom texel covers (twattila/bloom_high_pass.hlsli; stock:
  one tap) and the bloom is read with a cubic B-spline over 4 x 4 texels (hdr_to_screen's glow and the DOF composites; stock: one bilinear tap).
- Sky Debanding's filter moved into sky_deband.hlsli (shared with Troy), same result.
- No screen-space reflections added: the game's own (ssr_new: projected water reflections) stays.

## v19 (2026-10-24)

- Light Bounce with the game's MSAA (ATN: "support every AA"): its *_ms directional light draws hooked too (16: slm / skinlm, edges / interior,
  from the pack dump: each one's G-buffer slot and camera), the light buffer resolved for the trace, the prepare and the
  apply as *_ms builds (twattila/light_bounce_gbuffer.hlsli: Texture2DMS, the prepare at sample 0, the apply per
  sample as the game's MSAA lighting). FXAA / MLAA / off were already fine.

## v18 (2026-10-24)

- Ambient Occlusion Game's at its own cost (ATN): the replaced AO pass (the HBAO setting's ssao01) is the game's code only (#else of
  AO_BITMASK); the Visibility Bitmask is the addon's own pixel shader compiled from the same file (ao_bitmask.ps_5_0.hlsl: #define
  AO_BITMASK), which twattila/ao_pass.hpp draws in its place (the game's bindings, the addon's settings at b13) only
  when it is selected. Before, one shader held both paths, so Game's paid for ours.

## v17 (2026-10-23)

- AO Depth Levels (ATN: Rome II's frame time spikes gone with them): twattila/depth_levels.hpp builds the AO's depth at
  1/2 to 1/32 (5 mips of R32G32_UINT, one compute dispatch a level, depth_levels.cs_5_0.hlsl: each texel one depth texel
  of its 2 x 2 in a rotated grid, its bits and coordinate) before the Visibility Bitmask pass and binds them at t41
  (twwarhammer2/line_sweep_ao.hpp DispatchInstead: the blur's depth, t0; the HBAO setting's ssao01 through
  twattila/ao_pass.hpp on its draw (its t0)). A sample 8 x 2^n to 16 x 2^n depth texels away reads level n at that depth
  texel's own position (depth_levels.hlsli, BitmaskSampleLevel in bitmask.hlsli; as Scalable Ambient Obscurance). Same
  look; the far samples stay in the texture cache, so more Directions / Quality cost much less.
- Light Bounce levels: the prepared light and geometry with 5 more mips (twattila/light_bounce_levels.ps_5_0.hlsl after
  the prepare: light the mean of 2 x 2, geometry one of them in a rotated grid); the trace's far samples read them
  (BitmaskSampleLevel). Same look, cheaper at Full / Half resolution and large radii.
- AO presets (ATN): Thickness 0.11 in both Recommended presets; a yellow line above them: "Prefer darker shading behind
  trees and objects? Pick a preset, then raise Thickness (e.g. 0.20)."

## v16 (2026-10-23)

- Game's ambient occlusion costs what it did again: the replaced AO pass is the game's code only and the Visibility
  Bitmask is the addon's own 8 x 8 compute shader dispatched in its place (twwarhammer2/line_sweep_ao.hpp
  DispatchInstead, the addon's settings bound at b13). Before, one 1024-thread shader held both (fxc X4714: fewer
  threads in flight even for the game's path). Here in place of the line-sweep blur (ao_bitmask_blur); the collect
  never skipped (its replacement removed). The HBAO path (ssao2, a pixel shader) still holds both.

## v15 (2026-10-23)

- Ambient Occlusion and Light Bounce tooltips as Attila's (ATN); the Ambient Occlusion choice names the game's own
  method on its own line.
- Recommended (heavier but looks better): 4 ambient occlusion directions (ATN).

## v14 (2026-10-23)

- Settings Mode "HDR & Effects" / "Graphics" (ATN).
- Ambient Occlusion and Light Bounce presets at the top of the AO section (ATN): Recommended (AO Thickness 0.14,
  Quality High, 1 direction; Light Bounce Strength 4, Radius 6.5 m, Quarter, Ultra, 2 directions), Recommended
  (heavier but looks better: AO Thickness 0.11, 2 directions), Reset (both sections at their defaults with the game's
  AO). The graphics presets no longer set them. AO / Light Bounce titles light blue, two empty lines after Graphics.
- AO Directions 1 / 2 / 4 / 8 (2^the setting, default 4) and at most one step per AO pixel of the radius.
- Copyright headers on the lighting / AO files; About credits the Light Bounce (same paper).
- No AO Temporal Smoothing: its AO pass (ssao2 generate) has no camera matrices to reproject with.
- The pixel trace (Debug) removed for the release: it drew a red cross on screen in Graphics mode.

## v13 (2026-10-23)

- Settings Mode Image / Graphics (was Simple / Advanced; ATN): Image shows every tone mapping, grading, effect and
  output setting (the old Advanced-only ones too), Graphics only the graphics sections (Graphics, Ambient Occlusion,
  Light Bounce, Debug); About in both. IsGraphicsSettings and one loop at the end of ConfigureAddon.
- Light Bounce as Attila v25: prepare pass (mean light + packed geometry), Temporal Smoothing (default On), Resolution
  up to Eighth, Directions up to 16.

## v12 (2026-10-23)

- Ambient Occlusion Directions (2 / 4 / 8 slices around the view vector, default 4 as before; SI.ssao_directions):
  fewer is faster.
- Light Bounce (Experimental, new section, Advanced): twattila/light_bounce.hpp on directionallight.hlsl's sun draws
  (5 standard, 4 skin; camera b1, G-buffer from t5 / t7 / t8) with light_bounce.hlsli: normal from channel 1 xy,
  albedo channel 2 (sRGB view) x (1 - channel 3 specular), emissive (flags 2 + 8) none. Strength (0 off), Radius,
  Resolution, Quality, Directions.

## v11 (2026-10-23)

- Black and garbage polygons (ATN debug2, the pixel trace): the Blood pack's blood on the lens (lenseflare.hlsl
  ps40_blood 0xE4931F75) writes alpha down to -8.55 and red 1 - alpha, which the game's 8-bit target clamped; the
  upgraded FP16 one blended red -80 / alpha 83. lensblood00 clamps it, at Paper White as the flares. The five lens
  flares' copies of the clamp and scale are one lensflare.hlsli (ApplyPharaohLensOverlay).
- Ambient occlusion faster and Quality: twwarhammer2/bitmask_ao.hlsli takes Attila's bitmask.hlsli (fast acos,
  sectors), 8 / 16 / 32 samples per side (Ambient Occlusion Quality, SI.ssao_quality) and stops a slice once all
  sectors are covered.

## v10 (2026-10-23)

- Shadow Map Size 6144 (Game's) / 8192 / 12288 / 16384 (shadow_map.hpp): the shadow setup (+0x31e51ce) takes size and
  tile (half: the cascades' tiles in one texture) per Shadows quality from .rdata (+0x3da08a0; rows 4 / 5: 6144 /
  8192) or, at quality 4 with shadowmap_extreme_quality, 8192 / 4096 immediates (+0x31e5271). Rows 4 and 5 and the
  immediates get the size and half of it (the flag tweak is gone). D3D11 makes one 16384 x 16384 depth texture in
  every depth format (1-2 GB).
- "What I would use:" preset (ATN): Unit Detail 1.2x (all sizes), Dead 0.15x, Animation / Variety / Footstep 2x, Model
  3x, Grass 1.25x, Small Objects 4x, Terrain 4x, Building 6x, Campaign 4x, Reflection 1x, Contact Shadow 4x, Shadow
  Map Size 8192. MAX: Shadow Map Size 16384.
- Debug (Advanced): pixel trace, Tales of Arise's talesofarise_dlss/pixel_trace.hpp (file_prefix "pharaoh"): every
  draw / dispatch / clear / copy that changes the pixels under the red line during one frame, to
  pharaoh_pixel_trace.txt (+ pharaoh_trace_patches) next to the exe. For ATN's black polygons (debug1): unit sliders
  at 0.1x did not remove them, so not (only) the unit distances.

## v9 (2026-10-23)

- Dead Unit Detail down to 0.01 (ATN; it works).
- Ambient occlusion: the Visibility Bitmask moves into the AO's horizontal blur (line_sweep_blur.hlsli), from that
  pass's own depth and camera (this frame's) over its own size, then a 5 x 5 depth-aware average; the collect is
  skipped with it (ATN: Warhammer II's marks stayed). line_sweep_ao.hlsli removed.
- Shadow Map Size: the game's shadowmap_extreme_quality (engine_tweaks.hpp flag tweaks, 1 byte), which gives the
  highest Shadows setting an 8192 shadow map (its code: quality 4 / 5, enough video memory), set at the game's device
  creation. Untested. Pharaoh: 6144 -> 8192 (its table at +0x3da08a0).

## v8 (2026-10-22)

- Ambient occlusion Radius up to 10x (ATN).
- Ambient occlusion: the depth read over the part of its texture the game's blur uses (line_sweep_ao.hlsli
  depth_size), not the whole texture: Warhammer II's is larger, and its rest showed old frames (ATN's marks).
- Dead Unit Detail Distance (0.05 - 1x): Pharaoh.exe's inlined imposter test (+0x6fe1c4: comiss xmm0, xmm10; jb) jumps
  to a stub (dead_units.hpp) that scales a dead soldier's distance (soldier r15, alive bit 0 of +0x150). Untested.

## v7 (2026-10-22)

- Graphics (Experimental), Ambient Occlusion and their preset buttons: Advanced only (the round-4 check never hid
  them).
- Ambient occlusion Radius default 1 (ATN; Thickness stays 0.15).
- Video AutoHDR and Video Dithering at the end of Extra (next to the post-processing settings).
- No live counters (no CPU work per frame for them).
- Output: SDR (gamma 2.2, 10-bit) and Auto (scRGB with Windows HDR on, else SDR); default Auto.
- Ambient occlusion: the depth and camera come from the AO blur (now replaced code, ssao01 with
  line_sweep_blur.hlsli), so the collect always has them. Hooks on passes without replacement code never run, which
  skipped the collect and left stale AO (ATN's horizontal lines).
- Graphics presets: Reset, then MAX.

## v6 (2026-10-22)

- Ambient occlusion in its own section after Graphics, "Ambient Occlusion (Experimental, needs in-game SSAO)" (ATN),
  labels Strength / Radius / Thickness; defaults Strength 1, Radius 4x, Thickness 0.15 m (ATN). Advanced: "Ambient
  occlusion passes per frame" (twwarhammer2/ao_trace.hpp: counts the AO shaders' draws / dispatches; 0 = the game
  never ran them).
- Graphics (Experimental) and the AO section: Advanced only; both titles pink.
- PsychoV31 Highlight Hue Shift default 0.06 (ATN; Reset and the presets too).
- Shadow Distance removed (ATN).
- Graphics presets MAX (every slider at its maximum, Visibility Bitmask AO) and Reset.
- HDR (Extended): the shoulder above 1 per channel (as Ryse fix32 / Rome Remastered v3; hdr_to_screen.hlsli): bright fire turns
  yellow / white at its core as in SDR, not flat salmon.
- Bloom (as Attila): the high pass's hard cap of the bloom source (luminance 25) is a logarithmic shoulder in HDR
  (bloom.hlsli; bloom00_0x723A992D / bloom01_0x144277AF): bright sources no longer all get the same flat halo. SDR unchanged.
- Visibility Bitmask AO: every depth sample's position at the centre of the depth texel it read (bitmask_ao.hlsli
  AoSamplePosition). Off-centre positions with a texel's depth made sloped ground occlude itself in a regular dot
  pattern (ATN's Warhammer II "matrix" pattern, debug1).

## v5 (2026-10-22)

- General: yellow note "Use these in-game settings: Gamma 22 and Brightness 10."
- Presets: the three Recommended: Reset, then PsychoV31 at its defaults and Video AutoHDR 0.20 (the VFX boosts kept);
  Reset turns Video AutoHDR off.
- Extra: Video AutoHDR / Video Dithering (cinematics00 and the blurred videos, cinematics01_0x47A9D4C7).
- Graphics (Experimental): Ambient Occlusion Game's / Visibility Bitmask (default; Strength = an exponent, Radius
  1 = 2 m, Thickness). The line sweep (0x60FF920D) is skipped, its depth and camera bound for the collect (ssao00_0x19CF3C83), which
  computes the AO (twwarhammer2/line_sweep_ao.hpp, line_sweep_ao.hlsli, bitmask_ao.hlsli: Therrien, Levesque, Gilet
  2023 on GTAO, as Attila's); the game's blur follows. Credit in About.
- The default AO (ssao_algorithm 1: ssao2.hlsl HBAO, ssao01_0xD42C3523): Visibility Bitmask with the game's radius,
  strength and limit by distance.
- Graphics (Experimental), engine tweaks: Unit Detail per unit size, Unit Animation, Unit Variety, Footstep Effects
  (as Warhammer II), Building Detail and Campaign Detail.
- About: NexusMods button next to ATN's Ko-fi; every section title green, Graphics (Experimental) pink
  (settings.hpp default_section_tint / section_tints).
- Check: in-game SSAO on, a battle: Ambient Occlusion Game's vs Visibility Bitmask (no dark halos around soldiers);
  a video with Video AutoHDR 0 / 0.2.

## v4 (2026-10-20)

- Graphics (Experimental): a yellow note on top ("Experimental: these may not work, and may crash the game.").

## v3 (2026-10-20)

- PsychoV31: Highlight Hue Shift 0 by default (ATN); a note above Highlight Saturation / Hue Shift / Saturation
  Shift ("Fire too pink, too orange or too pale? Change the settings below.", as Tales of Arise), Highlight
  Saturation moved next to them.
- Graphics (Experimental), before Output (renamed from Graphics). Shared `twrome2/engine_tweaks.hpp`: engine tweaks
  by name, code constants by byte pattern (written with VirtualProtect), sliders with their own maximum; Rome II /
  Attila also hook the LOD pick.
- Sliders, all live: Model Detail Distance (up to 8x), Grass Distance (8x), Small Object Distance (8x, now
  also `terrain_blended_objects_cull_distance_nomask` / `_mask_low`), Terrain Detail Distance (4x:
  `terrain_distance_scale_low..ultra` 0.4 / 0.55 / 0.8 / 1 and `terrain_far_shader_distance`), Shadow
  Distance (4x: `shadowmap_split_scale` and its `_terry_battle` / `_terry_campaign`), Reflection Quality (4x:
  `screen_space_environment_occlusion_step_count` 16, pushed to the SSR shader every frame +0x30d4fdf),
  Contact Shadow Quality (4x: `screen_space_shadows_step_count` 16).
- Sharpening: PHARAOH has no sharpening setting (g_sharpening_strength 0). `sharpening.hlsli`: RenoDX's own
  (0.10, Troy's) x the slider (0-4) in HDR, in ps40_sharpen and in ps40_chromatic_aberration (the game may
  draw that pass instead).

Verify: Advanced "Engine values found: 17 of 17"; each slider in a battle.

## v2 (2026-10-20)

- Graphics section (1x = the game's, up to 4x), live:
  - Model Detail Distance: `distance_lod_scaler` (1 / slider): the LOD pick (+0x30e59e0) compares the camera
    distance squared x it squared with each LOD's distance, for every model (the shadow pass too).
  - Grass Distance: `grass_visible_range` and `grass_fade_start` (the grass quality setting writes them: 250 / 200
    at the highest).
  - Small Object Distance: `cull_small_screenspace_objects_default_factor` and
    `cull_small_screenspace_vegetation_factor` (small-object culling, read every frame).
- Shared code: `twrome2/engine_tweaks.hpp` (Rome II, Attila, Pharaoh): the engine's named tweaks are found by
  name at the first frame (x86 empire.retail.dll: `push name; mov ecx, object; call constructor`, the value at
  object + 0x48; x64 Pharaoh.exe: `lea object; lea rdx, [name]; call constructor; movss [object + 0x60]`) and
  scaled every frame; a value the game sets again (its graphics settings) becomes the new base. Advanced shows
  "Engine values found: n of n".

Verify: Advanced shows "Engine values found: 5 of 5"; in a battle, models switch to their detailed versions
farther away at 2x+, grass reaches farther, small props stay visible; changing the game's grass setting keeps the
slider applied.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (Extended) / HDR (PsychoV30) / HDR (PsychoV31, new): the same PsychoV31 sliders as the
  other Total War mods (Musa's custom test31; Highlight Hue / Saturation Shift toward the SDR image's per-channel clip).
- `hdr_to_screen.hlsli`: the helpers t01-t04 each carried (native grade bridge, Extended grading and shoulder) once,
  ApplyPharaohPsycho (V30 / V31), and the SDR / HDR ends of the new passes (Troy's).
- Shader packs (dump `bin\cmd_Decompiler-1.3.16\dump_shader_twpharaohdynasties_pack`), passes that had no HDR:
  - t05-t08: hdr_to_screen with in-game depth of field on (ps40_main_dof / ps50_main_dof_ms, distortion on / off).
  - photo01-04: photo mode (ps40_main_photo / ps50_main_photo_ms, distortion on / off).
  - dof00 / dof01: the DOF composite (god rays and glow into the frame, view depth into alpha; MSAA): the decompiler's
    UAV gaps filled as in Troy's dof00, God Rays / Bloom sliders, finite depth (no NaN in the DOF blur).
  - sharpening00: the game's sharpening (Troy's sharpening00, the same code): it clipped HDR to SDR white whenever the
    in-game sharpening was on. Its Sharpening slider is back (it was commented out; without it the field would be 0).
- Not ported: lenseflare ps40_blood (blood on the lens), the particle shader's other shadow permutations.
- About: credits, ATN's Ko-fi.

Verify: SDR / Extended / V30 / V31; in-game depth of field on (t05-t08 + dof00, with MSAA dof01); photo mode;
in-game sharpening on (sharpening00, Sharpening slider).


### F1 helper lifetime repair (2026-11-09, GPT-6 Astra)

AddonUninit now shuts down the shared helper bridge after unregistering its users. The watcher exits before module unload; child process and IPC ownership are released. Shutdown drains outstanding GPU fence generations, with ordered CPU fence recovery only after confirmed helper exit. Failed launches clean up partial ownership and existing IPC sessions are not overwritten.

Offline checks: x86/x64 production-header tests cover process/thread cleanup, failure injection, 20 reloads with stable handle counts, and three interleaved D3D11/D3D12 WARP fence generations under normal completion and forced shutdown. No game was launched. Manually check helper-enabled unload/reload and device recreation, then repeat the reported battle. The original battle freeze is not proven fixed; unexpected mid-run helper failures remain audit F7.
