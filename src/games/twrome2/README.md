# Total War: ROME II - Emperor Edition (RenoDX HDR, ATN)

### Faster Game CPU Code (2026-11-14; built only, not run in game)

New:
- **Faster Game CPU Code (Experimental, off)**, under the ray-traced options' More Options, as Attila's and Thrones': the soldier draw's rounding, the half floats (sprite soldiers) and the animations' fmod run faster with the same results, each checked against this game's own code (every register, the x87 state; test/tw_engine_code with this game's DLL).

Test (ATN): a big battle, Faster Game CPU Code on: ReShade.log "soldier draw rounding, half floats, fmod faster", then "on"; FPS against off; soldiers, sprites and their animations as before.

### Round 3 (2026-11-14, torch and smoke shadows and water reflections from the rays; built only, not run in game)

New:
- **Torch and Fire Shadows (Experimental, off)**, under the ray-traced shadows' More Options: torches, fires and the other point lights cast ray-traced shadows (each light read from its own draw); with Path-Traced Global Illumination they also light what the screen never saw. RT/PT turns it on, as Pharaoh's.
- **Smoke Shadows (Experimental, off)** and Smoke Shadow Quality: the smoke of arrow and shot impacts, of battle and campaign fires and of collapsing buildings dims the rays' sunlight on what lies below it, as Attila's. Fire's glow, the units' dust, trails and torches cast none.
- **Water reflections from rays**: Reflections and Water's Method gains **Ray-Traced (DXR, Experimental)** (off by default): the sea, lakes and rivers reflect what the rays find, also what is off screen or hidden, in this frame's water; with Samples, Reuse, Steadier and Softer Water Reflections. RT/PT turns it on, as Pharaoh's.

Fixed:
- Smoke Shadows' smoke took its texture one level blurrier than the game draws it (the effects' viewport read as half its size).

Not done:
- Torch shadows from the low-detail and older-GPU light shaders (in no run) and under in-game MSAA (no rays there).
- Smoke shadows from artillery trails: in no run yet.
- Cutscene Point Light Shadows: High: the game never reads that value.

Test (ATN): a siege at night and a daytime battle with burning buildings, RT shadows on. Torch and Fire Shadows on: walls, units and trees throw torch shadows, no black or flashing patches; ReShade.log has "Local lights: the first read from a light draw's constants (b2)" and "First run: Torch and Fire Shadows". Smoke Shadows on: the ground under thick smoke is darker in sunlight, the shadow moves with the smoke; the log has "Smoke Shadows: one particle a quad of vertex shader". Reflections Method Ray-Traced on a coastal or river battle: walls, ships and trees in the water, also those off screen, no black or shifted reflections near the shore, the log has "First run: Ray-Traced Water Reflections"; also below 100%. All off: the picture of Round 2. FPS in a big siege with each on and off.

Build: `cmake --build build32 --config Release --target renodx-twrome2.addon32`.

### Round 2 (2026-11-13, toward Attila's package; stamped "rome2 r2" and installed 22:59 with helper110, not run in game)

New:
- **Water Clarity** and **Water Caustics (Experimental, off)** on the battle water (plane Ultra and High, rivers), as Attila. At 1 / 0: the game's water. RT/PT leaves Caustics off here.
- **Skip the Shadow Maps (Experimental, off)**, under the ray-traced shadows' More Options: nothing is drawn into the game's near sun shadow map, the rays alone shade there (the water, rivers and campaign sea too); the far baked shadows stay.
- **Campaign Tree Detail Distance** (Graphics 2 of 2) and Attila's **With my 4x / 8x unit sizes** button. What I would use: unchanged.

Fixed:
- The Ray Tracing Debug View is drawn.
- Below 100%: the final pass's distortion and depth of field at the right place again.
- DLAA / DLSS read the picture's encoding right (gamma 2.2 there, as Attila's).
- Ray Reconstruction no longer takes the ground for water.
- With in-game MSAA on, ray tracing and frame generation stay off (DLAA / DLSS can't run there; before: the rays ran on).
- Frame generation: lens flares are part of the generated picture, not taken for UI (as Attila).

Not done:
- Ray-traced water reflections: the water is drawn after its reflections, with no surface to trace from yet.
- Point light shadows: the game's lights come one draw per light from a constant buffer, not Attila's instances.
- Smoke shadows: many effect shaders, the shared code takes one.
- Cutscene Point Light Shadows: High: the game never reads that value.

Test (ATN): a battle, RT shadows on, More Options: Skip the Shadow Maps on: shadows still on units, walls and water, faster in sieges; DLSS with Ray Reconstruction on grass and the ground (no smearing); Water Clarity 4 and Caustics 1 on a river; in-game MSAA x2 / x4: HDR right, no rays, no frame generation. Next trace: a menu-started one with this build, below 100%, a siege at night, MSAA off.

Build: `cmake --build build32 --config Release --target renodx-twrome2.addon32`.

### Trace build (2026-11-13, for the data run; built only, not run in game)

- **Scripted runs and traces for the bring-up data run, nothing else changed** (5cbbff5e): `RENODX_TW_SET` (keys and button labels), `RENODX_TW_SHOT`, `RENODX_TW_PROFILE` as Attila and Pharaoh (`twattila/test_hooks.hpp`); the frame trace also on demand (`renodx_trace_now` next to the exe) and at 100%, with the constants of the sun draws, tone mapper, water, SSAO resolve, battle fog and units once, and the units' vertex inputs and instance counts at each draw. Page dump before / after: the same rows.

Build: `cmake --build build32 --config Release --target renodx-twrome2.addon32`. Manual check: with `renodx_trace_now` put next to `Rome2.exe` in a battle, `ReShade.log` says "Frame trace: N lines saved to" within a few seconds.

### Round v45 (2026-11-12, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Bend Screen-Space Shadows: Auto Thickness removed** (ATN: it broke at some angles).

### Round v42 (2026-11-11, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Object Motion Vectors: each soldier keeps its own bones.** When the game packs its soldiers' bones into the animation pages more than once a frame (big battles), a later round's soldier at a reused row shared a key with an earlier one and could take its last-frame bones: false motion on still soldiers' shields, swords and spears under DLSS, FSR or XeSS. Keys are now this frame's and new after every page write (`twattila/object_motion.hpp`; the 64-page clear that gave page 65 page 1's key is gone). WARP `test/tw_unit_pack_rounds` (Attila's path; this add-on claims bones in the draw, so the keys are its fix).
- **Bend Screen-Space Shadows: Auto Thickness (Experimental, off)** (each object as thick as it is wide: fuller shadows behind walls and carts, poles stay thin) and **Softer Away from the Caster (Experimental, 0 = off)** (sharp at contact, softer farther away, by the sun's size). Every row off: today's shaders and picture (WARP `test/tw_screen_space_shadows`, to the bit).

Build: `cmake --build build32 --config Release --target renodx-twrome2.addon32`. Manual check: a paused big battle, the camera around the armies and back, Object Motion Vectors on: weapons on still soldiers stay still. Screen-space shadows with the new rows off: the picture as before.

### AO depth-level recovery (F15, 2026-11-09, GPT-6 Astra; not run in game)

Failed depth-level texture/view creation now releases all partial views and retries at the same resolution after a one-second cooldown. Build: `cmake --build build32 --config Release --target renodx-twrome2.addon32`. Manual check: use an AO mode that reads depth levels, change resolution/render scale, and verify stable AO; after a transient allocation failure, depth levels should recover without another resolution change. Failure injection and real D3D11 WARP readback results are in `docs/Attila-crash-audit-2026-11-08.md` at the repository root. No shader behavior was changed.

### Native hook removal (F14, 2026-11-09, GPT-6 Astra; not run in game)

Failed native hook removal now keeps the addon loaded until process exit, retains its hook records for retry, and makes surviving callbacks pass through. Normal successful removal releases its extra module reference. Test build: the existing 32-bit target below. Manual check: unload/reload after Graphics hooks activate; normal removal should unload, while a logged removal failure deliberately keeps the DLL resident until restarting the game. Offline failure/retry tests and shared builds are recorded in `docs/Attila-crash-audit-2026-11-08.md` at the repository root.

### Startup worker lifetime (2026-11-08, GPT-6 Astra; not run in game)

The startup audio-setting worker is now cancelled and waited for through its actual thread exit before unloading the add-on. Its progress flag alone did not cover capture and CRT cleanup. Manual check: unload/reload during the first minute with an audio-memory test enabled, then after it completes; no worker may execute unloaded code. Shared build and offline checks are recorded in HANDOFF.md.

Build: `cmake --build --preset clang-x86-release --target renodx-twrome2.addon32` (32-bit game).

## v48 (2026-11-12, PsychoV35 round; built only, not run in game)

- **HDR (PsychoV35)** (Rendering Mode 3): Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`). Hue Clip Emulation (0 - 100, default 80) toward the stock xyY path (the luminance clamped at the white point before bloom, the log S-curve, each channel clipped); default 80 (part 5: a luminance curve, then the clip). Reset and Recommended in PsychoV35 (no Extended mode here).
- Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain.
- Other modes: unchanged code paths (not bit-checked on this game; Attila's and WH3's are).

## v47 (2026-11-05, WH3's batch 5; built only, not run in game; renodx-tw-dlaa64.exe helper89, bridge v29: ship them together)

- **Shared helper:** ray-traced reflections without path tracing read last frame's colour squared back under Ray Reconstruction, and the Light Bounce without paths fills their radiance cache (WH3's batch 5). Its log: a real fault leaves one line and renodx-tw-dlaa64.dmp; whose device a GPU reset removed, with its reason and the time.
- **Path-Traced AO learns 150 frames before it starts, not 30** (WH3's batch 5: a battle's first frames gave 30 % less light). AO under Ray Reconstruction stays REBLUR, as before (Attila and WH3 have a row for it). Not run in game.

## v46 (2026-11-03, built only, not run in game)

- **Path Tracing with the game's or Bend's shadows**: the paths now get the sun's direction there too (as Attila's v135).
- **Frame generation with Output SDR**: the picture no longer turns red (as Attila).
- **Frame generation with Output scRGB**: works when turned on in game, no restart (as Attila). Ship with the helper built from
  this commit or later, not helper71.
- **Gamma Correction** (Rendering Mode SDR, on by default). On: any in-game Gamma shown as gamma 2.2, today's picture. Off:
  the game's own picture at the in-game Gamma. Rendering Mode SDR on Output SDR ignores Paper White (as Attila).
- **Fog & Sky Smoothing** (Off / Low / Medium / High + Strength, off by default, as Attila): the sky cube's vertical lines in thick fog. The fog shader 0x259AEC56 is now replaced.
- **Extra in Rendering Mode SDR**: Bloom, Lens Flare, God Rays, Vignette, DOF and Distortion work there (at 1 the picture is unchanged). Film Grain and Indicators stay HDR-only.
- **Wrong constants from the decompiler** (checked against the game's own shaders):
  - In-game depth of field: the sharp range around the focus was about 30% too narrow (f x f, the game has 2 x f x f).
  - UI and the historical battle map: sprites the game flags to swap red and blue now swap.
  - Constant tone mappers (0x32F67EF7, 0x682B15ED) in Rendering Mode SDR: brightness used the log range, now the game's. HDR unchanged.

## v45 (2026-10-28, untested in game)

- Screen-space shadows fade out from 40% to 60% of the far plane's distance (Attila v93: the campaign's sky backdrop
  shadowed itself).

## v44 (2026-10-28, untested in game)

- Screen-space shadows' Debug View goes through DLSS like the shadows in the picture (Attila v92).

## v43 (2026-10-28, untested in game)

- Screen-space shadows below 100%: no more striped hills and combed fence shadows (Attila v91: Bend's surface
  thickness and bilinear threshold over the render share).

## v42 (2026-10-28, untested in game)

- Frame generation's HUD-less image from the frame's last output pass (hdr_to_screen, the HDR output), not the first
  one AA ran at (Attila v90: there it was darker, and HUD Recomposition took the whole screen for UI).

## v41 (2026-10-28, untested in game)

- **Below 100%, the AO blur and the water refraction keep their 100% detail** (Attila v89): they draw their whole
  half-size target, and the 72 light and 59 water shaders that read them read the whole view.
- Frame Generation Debug View steps aside while the ReShade menu is open.

## v40 (2026-10-28, untested in game; renodx-tw-dlaa64.exe helper24, bridge v13: ship them together)

- **Effects at Output Resolution** (Anti-aliasing, on by default): while DLSS renders below 100%, ambient occlusion,
  Light Bounce, shadow softness and ray-traced ambient occlusion keep their 100% detail (Attila v88).
- **NVIDIA Reflex** (Off / On / On + Boost): without frame generation too, with any anti-aliasing; Frame Rate Limit
  works with Reflex on (Attila v86).
- **HUD Recomposition** gives DLSS-G a one-byte UI alpha (faster); **Frame Generation Debug View** (Attila v87).
- DLSS preset L by default; a red note in Ray Tracing: not working correctly yet (Attila v85).

## v39 (2026-10-28, untested in game)

- **Below 100%** (DLSS modes and custom render scales): **grass** looks as at DLAA all over the screen. Outside the
  top-left part it read the ground's distance from the wrong place, so the blades did not blend into the ground.

## v37 (2026-10-27, batch 5, untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper22** (in the zip; bridge v11, needs this addon): **ReSTIR GI v2** (compared with
  Embark Studios' kajiya, MIT / Apache 2.0, not copied): fireflies clamped, history only on a surface facing the same
  way, and every third frame the reused light is checked again: a light switched on or off shows within 3 frames
  (was about 20). **Partitioned TLAS** fixed (its update operations stalled the GPU after BLAS compaction) and on
  again. **Mesh instancing**: instanced draws build one BLAS placed once per instance.
- **Light Through Leaves** (off by default, with Ray-Traced Sun Shadows): leaves let a quarter of the sun through
  each, cloth and sails about a third; only where the rays are the shadow (past the game's own shadow maps).

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
- **Ray-Traced Grass** (off by default): the grass casts ray-traced shadows and shade too (its blades captured every
  frame through capture versions of the game's grass vertex shaders, alpha-tested; within the units' share of
  triangles). Heavy with dense grass.

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
  the display's size (the bloom / exposure downsample: bloom lines up with the image, the tone mapper reads it at the
  view's coordinates) read only its rendered part. After DLSS, depth of field, distortion, fog of war and photo
  mode read depth and distortion at the render share (twattila/render_scale.hlsli). WARP test: every patched shader
  of 8 dumps creates. Checked in game at 50% (Troy, Rome II): full frame, frame generation on.
- Lens flares drawn after DLSS (no viewport of their own) get the full screen again.
- **Ray-Traced Light Bounce** (Experimental, Anti-Aliasing section, with DLAA / DLSS and the Light Bounce on): the
  Light Bounce finds the light arriving at each texel with rays through the ray-traced scene (cosine-weighted, Rays
  1-16, Range 1-50 m) in place of its screen-space search: a ray's hit gives the prepared light where the camera sees
  that same surface (else nothing, as before), so no thickness guess behind what the camera sees and light from
  farther away. Its resolve, denoiser and apply are the Light Bounce's (rt_scene.hpp TraceLightBounce, helper
  rt_bounce.cs_6_5.hlsl; bridge v7, renodx-tw-dlaa64.exe helper16: the zip's helper must be the new one).

## v33 (2026-10-27, untested in game)

- **Ray-traced sun shadows and ambient occlusion** (Experimental, Anti-Aliasing section, with DLAA / DLSS and
  renodx-tw-dlaa64.exe): Attila's (rt_scene.hpp) on Rome II's 45 scene shaders (old and custom terrain, rigid, rigid
  blend / wind, branches, leaves, units; two builds of most), capture shaders made from the game's own
  (tools\tw\rt_port.py); GPU test (rt_capture_test.py): 45 of 45 match the game's positions.
- **DLSS Mode** (Anti-Aliasing, with DLAA; as Tales of Arise and Shogun 2): DLAA (100%), Ultra Quality (77%),
  Quality (66.7%), Balanced (58%), Performance (50%), Ultra Performance (33.3%), Custom (Custom Render Scale 1-100%;
  was a 33-100% slider: a saved scale below 100% is now the Custom value, pick Custom to use it).
- **DLSS render scale, checked below 100%**: render sizes are now whole units of the display's aspect ratio (16 x 9 at
  4K, as Tales of Arise, Ryse and Shogun 2; each side was rounded on its own before, so the aspect drifted a little and
  static pixels got motion toward the edges). **Texture LOD Bias** (Anti-Aliasing: Off / Matched / Sharper / Sharpest,
  default Sharper; Shogun 2's sampler clones, now twattila/texture_lod_bias.hpp, for the main view only): textures stay
  as sharp as at the display's resolution. Screen-space shadows keep their length on screen (their samples x the
  render share).
  **GT-VBAO** turned texture coordinates (the display's) straight into screen positions: below 100% its geometry was
  stretched; now the view's own coordinates (g_viewport_dimensions), and samples past the view's edge (stale texels
  there below 100%) are skipped. WARP test (warp_tests\ao\render_scale_test.cpp): the same scene at 50% in the
  top-left of 2x textures matches 100% (mean difference 0.0005). The game's own HBAO has the same flaw (its code):
  use GT-VBAO below 100%.
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
  the game's shader dump: tools\tw\sun_draw_layouts.py): Rome II's sun direction is at c0.x, and 6 of its sun shaders
  (no shadows) have an older camera layout, copied into Attila's for these passes. WARP check
  (tools\tw\warp_tests\sss\sss_scene.cpp, the sun direction at c0.y / c0.x / c1.x): the same shadows.
- Graphics 2 of 2 trimmed (ATN): More Tests keep only campaign map, grass & trees, units, buildings and culling (More
  Tests: Buildings & Culling); Attila's tests stay.
- Fixed: with Global Illumination on, the Light Bounce drew the sun draws itself and the anti-aliasing never saw the
  main camera and depth there (DLAA / TAA without them): its hook now runs first.
- DLSS Render Scale and Ray Reconstruction (Experimental, as Attila's; Anti-Aliasing DLAA): below 100% the 3D image is
  drawn smaller and DLSS upscales it; Ray Reconstruction's guides from Attila's G-buffer (the same). The screen-space
  shadows and the AO filter keep to the 3D image's corner of the screen-size textures (Bend's RenderSize).
- DLSS / DLAA motion vectors for everything that moves on its own (as Attila's v69-v70: twattila/anti_aliasing.hpp
  object motion), 59 of the game's vertex shaders: grass and plants, leaves and branches (both builds of each, and the
  low-quality ones), units (bone history off: Rome II samples its bones by coordinates), rigid objects (ships, siege),
  cloth, sails, ropes, both oceans. Made by tools\tw\attila_motion\port_motion.py (classify_motion.py picks them from
  the dump); GPU check test_motion.py: every motion shader's SV_Position bit-identical to the game's, last frame's
  position as the game's shader at last frame's camera / time / placement; cloth checked with deformed_test, sails and
  ropes not (the test knows the cloth layout only).
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
- Screen-Space Shadows (Bend Studio's, twattila/screen_space_shadows.hpp): Rome II's 40 shadowed sun shaders end their
  shadow with `min rN.c, rN.c, rM.c` (the cascades joined) before the same `div`; the patch takes it (and inserts after
  the instruction's real length: a fixed 5 tokens broke the 7-token `min`). All 40 patched and created on WARP.
- Anti-Aliasing (SMAA / CMAA2 / TAA, twattila/anti_aliasing.hpp) on the tone mappers, as Attila.
- Graphics 2 of 2: Attila's tests that Rome II's code reads (42 of 61) and 525 More Tests (every other graphics value it
  reads; more_tests.h from tools	w	w_game_tests.py), at-start tests as Attila; engine_values.txt lists the values.
- Water reflections: Reflection Method Hierarchical (default): helpers.hlsl's water SSR (waterreflection00, 0x7C9C8ECF)
  marches the FidelityFX hierarchical way (twattila/ssr_march.hlsli, shared with Attila's; world space here, the pyramid
  from its depth at t1) instead of 200 x quality fixed steps: no gaps or length limit. Game's: the original.
- Sky Debanding (Attila's, twattila/sky.hlsli) in Extra (Experimental), default Off: skydome main / gbuffer (3 shaders).

## v19 (2026-10-24)

- Light Bounce with the game's MSAA (ATN: "support every AA"): its *_ms directional light draws hooked too (24: slm / skinlm, highdef / lowdef, every shadow method; 4x,
  from the pack dump: each one's G-buffer slot and camera), the light buffer resolved for the trace, the prepare and the
  apply as *_ms builds (twattila/light_bounce_gbuffer.hlsli: Texture2DMS, the prepare at sample 0, the apply per
  sample as the game's MSAA lighting). FXAA / MLAA / off were already fine.

## v18 (2026-10-24)

- Ambient Occlusion Game's at its own cost (ATN): the replaced AO pass (ssao00, ssao01) is the game's code only (#else of
  AO_BITMASK); the Visibility Bitmask is the addon's own pixel shader compiled from the same file (ao_bitmask00 / 01.ps_5_0.hlsl: #define
  AO_BITMASK), which twattila/ao_pass.hpp draws in its place (the game's bindings, the addon's settings at b13) only
  when it is selected. Before, one shader held both paths, so Game's paid for ours.
- Light Bounce with the game's MSAA: not supported (ATN's "no effect"): the game then lights with directionallight
  ps41_*_highdef_ms (G-buffer as Texture2DMS), not in the sun draw list (found with a one-time draw log, removed).

## v17 (2026-10-23)

- AO Depth Levels (ATN: Rome II's frame time spikes gone with them): twattila/depth_levels.hpp builds the AO's depth at
  1/2 to 1/32 (5 mips of R32G32_UINT, one compute dispatch a level, depth_levels.cs_5_0.hlsl: each texel one depth texel
  of its 2 x 2 in a rotated grid, its bits and coordinate) before the Visibility Bitmask pass and binds them at t41
  (twattila/ao_pass.hpp on the ssao00 draw; its pixel-shader levels pass of v16 / v30 / v15 replaced). A sample 8 x 2^n
  to 16 x 2^n depth texels away reads level n at that depth texel's own position (depth_levels.hlsli, BitmaskSampleLevel
  in bitmask.hlsli; as Scalable Ambient Obscurance). Same look; the far samples stay in the texture cache, so more
  Directions / Quality cost much less.
- Light Bounce levels: the prepared light and geometry with 5 more mips (twattila/light_bounce_levels.ps_5_0.hlsl after
  the prepare: light the mean of 2 x 2, geometry one of them in a rotated grid); the trace's far samples read them
  (BitmaskSampleLevel). Same look, cheaper at Full / Half resolution and large radii.
- AO presets (ATN): Thickness 0.11 in both Recommended presets; a yellow line above them: "Prefer darker shading behind
  trees and objects? Pick a preset, then raise Thickness (e.g. 0.20)."
- Bloom less blocky (ATN): the high passes average the game's high pass of every source pixel a bloom texel covers
  (twattila/bloom_filter.hlsli BloomHighPassAveraged, the game's per pixel as BloomHighPassTap in bloom.hlsli; stock: one
  tap, small bright sources popping in and out as blocks), and the tone mappers read the bloom with a cubic B-spline
  over 4 x 4 texels (SampleBloomSmooth; stock: one bilinear tap, blocks and diamonds around bright sources).

## v16 (2026-10-23)

- "Directions Share Samples" removed (ATN: the AO got lighter with more directions: each one had too few samples to
  find what shades it).
- AO Depth Levels (twattila/ao_pass.hpp, ao_depth_levels; twattila/bitmask_ssao.hlsli): before the AO draw, the game's
  depth at 1/2 to 1/32 of its size (5 levels; each texel one depth texel of its 2 x 2 in a rotated grid, with that
  texel's coordinate), bound at t41. A sample 8 x 2^n to 16 x 2^n depth texels from the pixel reads level n, at that
  depth texel's own position (as Scalable Ambient Obscurance, McGuire et al. 2012). Same samples and look; the far ones
  stay in the texture cache, so more Directions / Quality cost much less (ATN: frame time spikes above 1 direction).
  ao_temporal.hpp is now ao_pass.hpp (Temporal Smoothing in it too).

## v15 (2026-10-23)

- AO frame time spikes (ATN: "mountains" above 1 direction): every direction took all of the Quality's samples, so 4
  directions cost 4x one, most where near surfaces fill the screen (steps up to 8 x 2^Quality there). "Directions Share
  Samples" (ssao_shared_samples, default on; twattila/bitmask_ssao.hlsli): the slices split the samples (at least 2
  per side each), so any number of directions costs about what one does. Off: the old cost, for comparing. The field
  is in Attila's and Thrones' shared.h too (0 there: unchanged until ATN confirms).

## v14 (2026-10-23)

- Ambient Occlusion and Light Bounce tooltips as Attila's (ATN); the Ambient Occlusion choice names the game's own
  method on its own line.
- Recommended (heavier but looks better): 4 ambient occlusion directions (ATN).

## v13 (2026-10-23)

- Settings Mode "HDR & Effects" / "Graphics" (ATN).
- Ambient Occlusion and Light Bounce presets at the top of the AO section (ATN): Recommended (AO Thickness 0.14,
  Quality High, 1 direction; Light Bounce Strength 4, Radius 6.5 m, Quarter, Ultra, 2 directions), Recommended
  (heavier but looks better: AO Thickness 0.11, 2 directions), Reset (both sections at their defaults with the game's
  AO). The graphics presets no longer set them. AO / Light Bounce titles light blue, two empty lines after Graphics.
- AO Directions 1 / 2 / 4 / 8 (2^the setting, default 4) and at most one step per AO pixel of the radius.
- Copyright headers on the lighting / AO files; About credits the Light Bounce (same paper).
- AO Temporal Smoothing (default Off): twattila/ao_temporal.hpp on the AO draw; the bitmask AO is
  twattila/bitmask_ssao.hlsli now (Attila, Thrones, Rome II). Both AO shaders (0x37F7EDEF, 0x1ED22F9C).

## v12 (2026-10-23)

- Settings Mode Image / Graphics (was Simple / Advanced; ATN): Image shows every tone mapping, grading, effect and
  output setting (the old Advanced-only ones too), Graphics only the graphics sections (Graphics, Ambient Occlusion,
  Light Bounce); About in both. IsGraphicsSettings and one loop at the end of ConfigureAddon.
- Light Bounce as Attila v25: prepare pass (mean light + packed geometry), Temporal Smoothing (default On), Resolution
  up to Eighth, Directions up to 16.

## v11 (2026-10-23)

- Ambient Occlusion Directions (2 / 4 / 8 slices around the view vector, default 4 as before; SI.ssao_directions):
  fewer is faster.
- Light Bounce Resolution (Full / Half / Quarter, default Half), Quality (8 / 16 / 32 samples per side, its own now,
  default 8) and Directions (1 / 2 / 4, default 2, as before). twattila/light_bounce.hpp: Install takes a SunDraw
  table (shader, standard or not, G-buffer and camera slots); each sun draw's G-buffer and camera are bound again at
  t42-t45 / b11 (light t40, bounce t41, constants b12), so the trace and apply (light_bounce_trace.hlsli /
  light_bounce_apply.hlsli) are game-neutral and each game's light_bounce.hlsli decodes its own G-buffer. The bounce
  texture is RT / scale, the trace viewport the game's / scale. Installed after renodx::mods::shader::Use (its
  injection on replaced sun shaders runs first).

## v10 (2026-10-23)

- Ambient occlusion faster: twattila/bitmask.hlsli (shared by every Total War bitmask AO and the Light Bounce): acos
  within 0.007 rad (GTAO's approximation; a sector is 0.1 rad), and a slice stops once all 32 sectors are covered.
- Ambient Occlusion Quality (Normal / High / Ultra: 8 / 16 / 32 samples per side of a slice; SI.ssao_quality).
- Light Bounce (Experimental, new section; Strength 0 = off, Radius 3 m): Attila's (twattila/light_bounce.hpp and its
  shaders through light_bounce_*.hlsl wrappers), on this game's directionallight ps30 slm / skinlm draws with the
  camera at b0 and the G-buffer at t0-t3 (LIGHT_BOUNCE_*_SHADERS in addon.cpp). 20 variants with the camera elsewhere
  (older shader models) are left out.

## v9 (2026-10-23)

- "What I would use:" Dead Unit Detail 0.5x and Shadow Map Size 8192 (ATN).
- Ambient Occlusion tooltip: the game's HBAO (4 directions, everything solid: dark halos) vs the Visibility Bitmask
  (GTAO with visibility bitmasks, 4 slices x 16 samples, a thickness per sample) (ATN).
- CPU: the DebugUnits probe removed (hidden ReShade.ini counters in the imposter hook and a per-frame status update);
  the Graphics status line (engine_tweaks.hpp StatusLine) shows only when something was not found or failed.

## v8 (2026-10-23)

- Shadow Map Size: the game's shadowmap_extreme_quality (engine_tweaks.hpp flag tweaks, 1 byte), which gives the
  highest Shadows setting an 8192 shadow map (its code: quality 4 / 5, enough video memory), set at the game's device
  creation. Untested. Rome II: Extreme 4096 -> 8192.

## v7 (2026-10-22)

- Ambient occlusion Radius up to 10x (ATN).
- "What I would use:" keeps Unit Animation at 1x (ATN: no flicker zooming on big units).

## v6 (2026-10-22)

- Graphics (Experimental), Ambient Occlusion and their preset buttons: Advanced only (the round-4 check never hid
  them).
- Ambient occlusion Radius default 1 (ATN; Thickness stays 0.15).
- Video AutoHDR and Video Dithering at the end of Extra (next to the post-processing settings).
- No live counters (no CPU work per frame for them).
- Output: SDR (gamma 2.2, 10-bit) and Auto (scRGB with Windows HDR on, else SDR); default Auto.
- Graphics presets: Reset, MAX, "What I would use:" (ATN: units 2, dead 0.3, animation / variety / footstep 3, model
  1.5, terrain 4, building 8).
- No Big Battle Shadow Fix: Rome II has no animation-page packing like Attila's.

## v5 (2026-10-22)

- Ambient occlusion in its own section after Graphics, "Ambient Occlusion (Experimental, needs in-game SSAO)" (ATN),
  labels Strength / Radius / Thickness; defaults Strength 1, Radius 4x, Thickness 0.15 m (ATN). Advanced: "Ambient
  occlusion passes per frame" (twwarhammer2/ao_trace.hpp: counts the AO shaders' draws / dispatches; 0 = the game
  never ran them).
- Graphics (Experimental) and the AO section: Advanced only; both titles pink.
- PsychoV31 Highlight Hue Shift default 0.05 (ATN; Reset and the presets too).
- Reset: PsychoV31 (no Extended mode here; ATN).
- Dead Unit Detail Distance (Attila's ImposterDistance hook with Rome II's patterns: +0x4978a0, its caller +0x464960 in
  the per-soldier update +0x464430: frame 0x1d0, the alive flag at +0xd8).
- Grass Distance and Campaign Detail removed: ATN saw no effect (Rome II has no campaign tree function like Attila's;
  grass is limited in the grass shader and the engine's scrub nodes, as Attila found).
- Graphics presets MAX (every slider at its maximum) and Reset.
- AO: ATN saw no change between Game's and Visibility Bitmask; the replaced resolves (0x37F7EDEF data.pack,
  0x1ED22F9C data_rome2.pack) are the only AO shaders in the packs. The new pass counter shows whether they run.
- Bloom (as Attila): the high pass's hard cap of the bloom source (Y at 2.1 x the white point (xyY)) is a logarithmic shoulder in HDR
  (bloom.hlsli; bloom00-03: 0xA74B1774 / 0xE0291BA9 / 0xBE392B96 / 0xD1672A46): bright sources no longer all get the same flat halo. SDR unchanged.

## v4 (2026-10-22)

- General: yellow note "Use these in-game settings: Gamma 22 and Brightness 10."
- Presets: both "My settings" in PsychoV31 with Video AutoHDR 0.20; Reset: PsychoV30 (the default), Video AutoHDR off.
  The 1000 nits preset's tooltip said 2500.
- Extra: Video AutoHDR / Video Dithering (video00_0x64CB1923).
- Graphics (Experimental): a yellow note on top; Visibility Bitmask AO (ssao00_0x37F7EDEF, ssao01_0x1ED22F9C: the 25 m
  fading permutation); Attila's unit sliders (Unit Detail: Foot Soldiers / Cavalry / Elephants / Artillery, Unit
  Animation, Unit Variety, Footstep Effects) through Attila's UnitDistances hook (twattila/graphics_hooks.hpp: Rome
  II's own pattern, +0x4497e0, the same 18 distances); Building Detail (battle_building_lod0-3) and Campaign Detail
  (rigid_campaign_tree_lod). Shadow Distance removed (as Attila). Credit in About.
- About: NexusMods button next to ATN's Ko-fi; every section title green, Graphics (Experimental) pink
  (settings.hpp default_section_tint / section_tints).
- Check: in-game SSAO on, a battle: Ambient Occlusion Game's vs Visibility Bitmask (no dark halos around soldiers);
  a video with Video AutoHDR 0 / 0.2.

## v3 (2026-10-20)

- PsychoV31: Highlight Hue Shift 0 by default (ATN); a note above Highlight Saturation / Hue Shift / Saturation
  Shift ("Fire too pink, too orange or too pale? Change the settings below.", as Tales of Arise), Highlight
  Saturation moved next to them.
- Graphics (Experimental), before Output (renamed from Graphics). Shared `twrome2/engine_tweaks.hpp`: engine tweaks
  by name, code constants by byte pattern (written with VirtualProtect), sliders with their own maximum; Rome II /
  Attila also hook the LOD pick.
- Model Detail Distance (up to 8x, live): v2 scaled the LOD distances / per-quality factors, which the game copies
  into each model when it loads (ATN: no change). Now the LOD pick is hooked (`Entry* __thiscall PickLod(model,
  distance squared, next)`, the same code in both games: Rome II +0x14f0710, Attila +0x183d570): the camera's
  distance squared / slider squared, every frame, every model. The fade-out at the last LOD (draw distance) stays.
- Grass Distance (up to 8x, next battle load): v2 scaled only `scrub_lod_distance_default`, the grass node's cell
  distance; the node's constructor also stores a second distance, a constant 200 m (Rome II node +0x80 at
  +0x1492663, Attila node +0x84 at +0x17cb350), now scaled too (code constant).
- Terrain Detail Distance (up to 4x): `terrain_far_shader_distance` (1500 m: the cheaper far terrain shader).
- Shadow Distance (up to 4x): `shadowmap_split_scale` (1: every shadow cascade's width; Attila +0x1784e52, read
  every frame).

Verify: Advanced "Engine values found: 4 of 4; model detail hooked"; Model Detail Distance changes units' detail
live; Grass Distance after loading a battle.

## v2 (2026-10-20)

- Graphics section (1x = the game's, up to 4x; models and grass set up after the change: next battle or
  campaign load):
  - Model Detail Distance: `lod_multiplier_for_low` ... `_extreme` (0.25 / 0.25 / 0.35 / 0.5 / 2 / 3): the model
    setup (+0x14b9770) multiplies every LOD distance by the one for the unit detail setting.
  - Grass Distance: `scrub_lod_distance_default` (200 m), copied into each grass node when it is built
    (+0x1492653).
- Shared code: `twrome2/engine_tweaks.hpp` (Rome II, Attila, Pharaoh): the engine's named tweaks are found by
  name at the first frame (x86 empire.retail.dll: `push name; mov ecx, object; call constructor`, the value at
  object + 0x48; x64 Pharaoh.exe: `lea object; lea rdx, [name]; call constructor; movss [object + 0x60]`) and
  scaled every frame; a value the game sets again (its graphics settings) becomes the new base. Advanced shows
  "Engine values found: n of n".

Verify: Advanced shows "Engine values found: 7 of 7"; set a slider, load a battle: units / buildings keep their
detailed models farther away, grass reaches farther.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (PsychoV30, default) / HDR (PsychoV31). PsychoV31 (Musa's custom test31, as in Shogun 2
  and Tales of Arise): Exposure, Highlights, Shadows, Contrast, Highlight / Shadow Contrast, Flare, Saturation,
  Dechroma, Highlight Saturation, Highlight Roll-off, Gamut Compression + Mode, Highlight Hue / Saturation Shift
  (toward the SDR image's per-channel clip). Anchored at mid gray; the colour matrix runs before it as for V30.
- Shared code: `hdr_to_screen.hlsli` (HdrToScreenOutput / SdrToScreenOutput: every hdr_to_screen permutation),
  `tone_mapper.hlsli` (ToneMapperHdr: the tone mapper's HDR path, t00 / t09 / t10).
- Shader packs (tools: `C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw`, dump in
  `bin\cmd_Decompiler-1.3.16\dump_shader_twrome2_pack`, names.tsv = every fxc in data / data_rome2 by name). New
  permutations the game loads (data_rome2.pack): t09 / t10 the constant tone mappers (fixed exposure), t11-t14
  hdr_to_screen ps30_main_low (low post-processing quality), dof03 (circle of confusion without distortion), dof04 /
  dof05 (pixel shader DOF blurs: no SDR clip in HDR), vignette00 (the lens flare's vignette: Vignette slider), and the
  VFX effects' sm_3_0 / sm_4_1 permutations (28 files, `*v1_` / `*v2_`: the VFX sliders at every shadow setting).
- Sliders work in both HDR modes (`HDR >= 1`).
- About: credits, ATN's Ko-fi.

Verify: SDR / V30 / V31 in a battle and on the campaign map; low post-processing quality (t11-t14); DOF with
distortion off (dof03); VFX sliders with low shadows.


### F1 helper lifetime repair (2026-11-09, GPT-6 Astra)

AddonUninit now shuts down the shared helper bridge after unregistering its users. The watcher exits before module unload; child process and IPC ownership are released. Shutdown drains outstanding GPU fence generations, with ordered CPU fence recovery only after confirmed helper exit. Failed launches clean up partial ownership and existing IPC sessions are not overwritten.

Offline checks: x86/x64 production-header tests cover process/thread cleanup, failure injection, 20 reloads with stable handle counts, and three interleaved D3D11/D3D12 WARP fence generations under normal completion and forced shutdown. No game was launched. Manually check helper-enabled unload/reload and device recreation, then repeat the reported battle. The original battle freeze is not proven fixed; unexpected mid-run helper failures remain audit F7.
