# Total War Saga: THRONES OF BRITANNIA (RenoDX HDR, ATN)

Build: `cmake --build --preset clang-x86-release --target renodx-twthronesofbritannia.addon32` (32-bit game).

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
- **Ray-Traced Reflections** and **Ray-Traced Specular Occlusion** (Experimental, with DLAA / DLSS and in-game Screen
  Space Reflections on): GGX samples of each pixel's reflection (Ray-Traced Reflection Samples 1-8; mirrors one ray)
  traced through the scene in the helper and kept over frames by world position. Where they meet a surface its light
  replaces the screen-space march, which is then skipped (Attila's and Thrones' environment_specular_light pass, ssr00 / ssr01); the game's environment stays for the rest (the
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

## v34 (2026-10-27)

- **DLSS below 100%: the game's own passes** (ATN: post-processing, shadows, reflections and our effects at DLSS):
  twattila/view_share.hpp patches the game's shaders as they are created. Positions rebuilt from texture
  coordinates (point lights, decals, battle fog, water, refraction, particles, the game's SSAO, markers, the SSR
  ray casts) use the view's share of the targets, projected reads land in it, and passes that read the 3D image at
  the display's size (the bloom / exposure downsample: bloom lines up with the image, the tone mapper reads it at
  the view's coordinates; the shadow cascades' depth analysis) read only its rendered part. After DLSS, depth of field, distortion, fog of war and photo
  mode read depth and distortion at the render share (twattila/render_scale.hlsli). WARP test: every patched shader
  of 8 dumps creates. Checked in game at 50% (Troy, Rome II): full frame, frame generation on.
- **Ray-Traced Light Bounce** (Experimental, Anti-Aliasing section, with DLAA / DLSS and the Light Bounce on): the
  Light Bounce finds the light arriving at each texel with rays through the ray-traced scene (cosine-weighted, Rays
  1-16, Range 1-50 m) in place of its screen-space search: a ray's hit gives the prepared light where the camera sees
  that same surface (else nothing, as before), so no thickness guess behind what the camera sees and light from
  farther away. Its resolve, denoiser and apply are the Light Bounce's (rt_scene.hpp TraceLightBounce, helper
  rt_bounce.cs_6_5.hlsl; bridge v7, renodx-tw-dlaa64.exe helper16: the zip's helper must be the new one).

## v33 (2026-10-27, untested in game)

- **Ray-traced sun shadows and ambient occlusion** (Experimental, Anti-Aliasing section, with DLAA / DLSS and
  renodx-tw-dlaa64.exe): Attila's (rt_scene.hpp) on Thrones' 26 scene shaders (its terrain and units, Attila's rigid,
  branch and leaf shaders); GPU test: 26 of 26 captures match.
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
  use GT-VBAO below 100%. **Hierarchical reflections**: the march's screen is the view's (the render size), the light
  buffer and the resolve's history read at the view's share of their texture; the resolve's positions from the view's
  coordinates. WARP test (ssr_test.cpp at 100% and 50%): identical.
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
  the game's shader dump: tools\tw\sun_draw_layouts.py): 10 of its sun shaders keep their lighting at b1. WARP check
  (tools\tw\warp_tests\sss\sss_scene.cpp, the sun direction at c0.y / c0.x / c1.x): the same shadows.
- Graphics 2 of 2 trimmed (ATN): More Tests keep only campaign map, grass & trees, units, buildings and culling (More
  Tests: Buildings & Culling); Attila's tests stay.
- Fixed: with Global Illumination on, the Light Bounce drew the sun draws itself and the anti-aliasing never saw the
  main camera and depth there (DLAA / TAA without them): its hook now runs first.
- DLSS Render Scale and Ray Reconstruction (Experimental, as Attila's; Anti-Aliasing DLAA): below 100% the 3D image is
  drawn smaller and DLSS upscales it; Ray Reconstruction's guides from Attila's G-buffer (the same). The screen-space
  shadows and the AO filter keep to the 3D image's corner of the screen-size textures (Bend's RenderSize).
- DLSS / DLAA motion vectors for everything that moves on its own (as Attila's v69-v70: twattila/anti_aliasing.hpp
  object motion), 32 of the game's vertex shaders: Attila's shaders (the same) with its own units and old ocean. Made by
  tools\tw\attila_motion\port_motion.py (classify_motion.py picks them from the dump); GPU check test_motion.py: every
  motion shader's SV_Position bit-identical to the game's, last frame's position as the game's shader at last frame's
  camera / time / placement; cloth checked too.
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

- Sky Debanding (Attila's, twattila/sky.hlsli) in Extra, default Off: skydome main / gbuffer (Attila's two shaders).
- Reflections: the hierarchical march now in twattila/ssr_march.hlsli (shared with Rome II's water reflections); same
  result (the hit's thickness from its distance to the camera instead of its view depth: a little thicker at the
  screen's sides).

## v26 (2026-10-26, untested)

- Attila's feature set (ATN's batch): Settings Mode HDR & Effects / Graphics 1 of 2 (AO, GI, screen-space shadows,
  anti-aliasing) / Graphics 2 of 2 (Graphics (Experimental) + tests), Attila's section colours
  (twattila/feature_settings.hpp). Reflections: Attila's hierarchical SSR (same shaders, 0x66006BEA / 0x47D75349:
  thin wrappers of twattila's with Thrones' shared.h) with Reflection Method / Denoiser / Rays. Screen-Space Shadows
  (Bend; twattila/screen_space_shadows.hpp patches Thrones' 43 sun light shaders the same way: WARP patch test 43 / 43)
  and Anti-Aliasing (SMAA, CMAA2, SMAA T2x, TAA on the tone mappers' on_drawn), their compute shaders as wrappers of
  Attila's. Graphics 2 of 2: Attila's tests (twattila/engine_tests.hpp; the same engine values, audio memory at start),
  engine_values.txt (655 values Thrones' code reads: tools\tw tw_tweaks32 / tw_tweak_uses32 / tw_engine_values).

## v18 (2026-10-24)

- Light Bounce with the game's MSAA (ATN: "support every AA"): its *_ms directional light draws hooked too (24: slm / skinlm, highdef / lowdef, every shadow method,
  from the pack dump: each one's G-buffer slot and camera), the light buffer resolved for the trace, the prepare and the
  apply as *_ms builds (twattila/light_bounce_gbuffer.hlsli: Texture2DMS, the prepare at sample 0, the apply per
  sample as the game's MSAA lighting). FXAA / MLAA / off were already fine.

## v17 (2026-10-24)

- Ambient Occlusion Game's at its own cost (ATN): the replaced AO pass (ssao00) is the game's code only (#else of
  AO_BITMASK); the Visibility Bitmask is the addon's own pixel shader compiled from the same file (ao_bitmask.ps_5_0.hlsl: #define
  AO_BITMASK), which twattila/ao_pass.hpp draws in its place (the game's bindings, the addon's settings at b13) only
  when it is selected. Before, one shader held both paths, so Game's paid for ours.
- Light Bounce with the game's MSAA: not supported (ATN's "no effect"): the game then lights with directionallight
  ps41_*_highdef_ms (G-buffer as Texture2DMS), not in the sun draw list (found with a one-time draw log, removed).

## v16 (2026-10-23)

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

## v15 (2026-10-23)

- AO Depth Levels (twattila/ao_pass.hpp, ao_depth_levels; twattila/bitmask_ssao.hlsli): before the AO draw, the game's
  depth at 1/2 to 1/32 of its size (5 levels; each texel one depth texel of its 2 x 2 in a rotated grid, with that
  texel's coordinate), bound at t41. A sample 8 x 2^n to 16 x 2^n depth texels from the pixel reads level n, at that
  depth texel's own position (as Scalable Ambient Obscurance, McGuire et al. 2012). Same samples and look; the far ones
  stay in the texture cache, so more Directions / Quality cost much less (ATN: frame time spikes above 1 direction).
  ao_temporal.hpp is now ao_pass.hpp (Temporal Smoothing in it too).

## v14 (2026-10-23)

- "What I would use:" (ATN's screenshot): Unit Detail 1.75x, Dead Unit Detail 0.60x, Unit Animation / Variety /
  Footstep Effects 2.50x, Model Detail 1.50x, Building Detail 3x, Campaign Detail 4x, Shadow Map Size 8192; Reflection
  Quality 1x.

## v13 (2026-10-23)

- Ambient Occlusion and Light Bounce tooltips as Attila's (ATN); the Ambient Occlusion choice names the game's own
  method on its own line.

## v12 (2026-10-23)

- Settings Mode "HDR & Effects" / "Graphics" (ATN).
- Ambient Occlusion and Light Bounce presets at the top of the AO section (ATN): Recommended (AO Thickness 0.14,
  Quality High, 1 direction; Light Bounce Strength 4, Radius 6.5 m, Quarter, Ultra, 2 directions), Recommended
  (heavier but looks better: AO Thickness 0.11, 2 directions), Reset (both sections at their defaults with the game's
  AO). The graphics presets no longer set them. AO / Light Bounce titles light blue, two empty lines after Graphics.
- AO Directions 1 / 2 / 4 / 8 (2^the setting, default 4) and at most one step per AO pixel of the radius.
- Copyright headers on the lighting / AO files; About credits the Light Bounce (same paper).
- AO Temporal Smoothing (default Off): twattila/ao_temporal.hpp on the AO draw; the bitmask AO is
  twattila/bitmask_ssao.hlsli now (Attila, Thrones, Rome II).

## v11 (2026-10-23)

- Settings Mode Image / Graphics (was Simple / Advanced; ATN): Image shows every tone mapping, grading, effect and
  output setting (the old Advanced-only ones too), Graphics only the graphics sections (Graphics, Ambient Occlusion,
  Light Bounce); About in both. IsGraphicsSettings and one loop at the end of ConfigureAddon.
- Light Bounce as Attila v25: prepare pass (mean light + packed geometry), Temporal Smoothing (default On), Resolution
  up to Eighth, Directions up to 16.

## v10 (2026-10-23)

- Ambient Occlusion Directions (2 / 4 / 8 slices around the view vector, default 4 as before; SI.ssao_directions):
  fewer is faster.
- Light Bounce Resolution (Full / Half / Quarter, default Half), Quality (8 / 16 / 32 samples per side, its own now,
  default 8) and Directions (1 / 2 / 4, default 2, as before). twattila/light_bounce.hpp: Install takes a SunDraw
  table (shader, standard or not, G-buffer and camera slots); each sun draw's G-buffer and camera are bound again at
  t42-t45 / b11 (light t40, bounce t41, constants b12), so the trace and apply (light_bounce_trace.hlsli /
  light_bounce_apply.hlsli) are game-neutral and each game's light_bounce.hlsli decodes its own G-buffer. The bounce
  texture is RT / scale, the trace viewport the game's / scale. Installed after renodx::mods::shader::Use (its
  injection on replaced sun shaders runs first).

## v9 (2026-10-23)

- Ambient occlusion faster: twattila/bitmask.hlsli (shared by every Total War bitmask AO and the Light Bounce): acos
  within 0.007 rad (GTAO's approximation; a sector is 0.1 rad), and a slice stops once all 32 sectors are covered.
- Ambient Occlusion Quality (Normal / High / Ultra: 8 / 16 / 32 samples per side of a slice; SI.ssao_quality).
- Light Bounce (Experimental, new section; Strength 0 = off, Radius 3 m): Attila's (twattila/light_bounce.hpp and its
  shaders through light_bounce_*.hlsl wrappers), on this game's directionallight ps30 slm / skinlm draws with the
  camera at b0 and the G-buffer at t0-t3 (LIGHT_BOUNCE_*_SHADERS in addon.cpp).

## v8 (2026-10-23)

- "What I would use:" graphics preset (ATN): Reflection Quality 2x, Unit Detail 2.5x, Dead Unit Detail 0.6x, Unit
  Animation / Variety / Footstep Effects 3x, Model Detail 1.5x, Building Detail 6x, Campaign Detail 4x, Big Battle
  Shadow Fix off, Shadow Map Size 8192.
- PsychoV31 Highlight Hue Shift default 0.13 (ATN; Reset and Recommended too).
- Ambient Occlusion tooltip: the game's HBAO (4 directions, everything solid: dark halos) vs the Visibility Bitmask
  (GTAO with visibility bitmasks, 4 slices x 16 samples, a thickness per sample) (ATN).
- CPU: the DebugUnits probe removed (hidden ReShade.ini counters in the imposter hook and a per-frame status update);
  the Graphics status line (engine_tweaks.hpp StatusLine) shows only when something was not found or failed.
- Campaign trees (graphics_hooks.hpp OnDrawTree): no lookup at 1x before any change, and none for a model's instances
  drawn after each other (the thread's last tree, checked by its first LOD distance).

## v7 (2026-10-23)

- Shadow Map Size (2048 / 4096 / 8192 at Shadows Max): Attila's code (graphics_hooks.hpp shadow_map). Untested.

## v6 (2026-10-22)

- Ambient occlusion Radius up to 10x (ATN).
- Dead Unit Detail: Thrones' per-soldier update has a 0x148 frame (Attila 0x144), so the alive flag was read from the
  wrong place (graphics_hooks.hpp Game::THRONES).

## v5 (2026-10-22)

- Graphics (Experimental), Ambient Occlusion and their preset buttons: Advanced only (the round-4 check never hid
  them).
- Ambient occlusion Radius default 1 (ATN; Thickness stays 0.15).
- Video AutoHDR and Video Dithering at the end of Extra (next to the post-processing settings).
- No live counters (no CPU work per frame for them).
- Output: SDR (gamma 2.2, 10-bit) and Auto (scRGB with Windows HDR on, else SDR); default Auto.
- Graphics (Experimental) as Attila (ATN): Reflection Quality, unit distances, Dead, Animation, Variety, Footstep,
  Model Detail, Building, Campaign, Big Battle Shadow Fix (off). Untested.

## v4 (2026-10-22)

- Ambient occlusion in its own section after Graphics, "Ambient Occlusion (Experimental, needs in-game SSAO)" (ATN),
  labels Strength / Radius / Thickness; defaults Strength 1, Radius 4x, Thickness 0.15 m (ATN). Advanced: "Ambient
  occlusion passes per frame" (twwarhammer2/ao_trace.hpp: counts the AO shaders' draws / dispatches; 0 = the game
  never ran them).
- Graphics (Experimental) and the AO section: Advanced only; both titles pink.
- PsychoV31 Highlight Hue Shift default 0.20 (ATN; Reset and the presets too).
- Reset: PsychoV31 (no Extended mode here; ATN).
- Graphics presets MAX (Reflection Quality 8x, Visibility Bitmask AO) and Reset (all graphics / AO defaults).
- Bloom (as Attila): the high pass's hard cap of the bloom source (luminance 25) is a logarithmic shoulder in HDR
  (bloom.hlsli; Attila's bloom00_0xCB39FE73 / bloom01_0x9C0FC283): bright sources no longer all get the same flat halo. SDR unchanged.

## v3 (2026-10-22)

- General: yellow note "Use these in-game settings: Gamma 100% and Brightness 36%."
- Presets: Recommended (new): Reset, then PsychoV31 at its defaults and Video AutoHDR 0.20; Reset turns Video AutoHDR off.
- Extra: Video AutoHDR / Video Dithering (cutscenes00).
- Graphics (Experimental): Attila's Visibility Bitmask AO (the same shader, ssao00_0xBD389EF1) and Reflection Quality
  (the same SSR shaders, ssr00_0x66006BEA / ssr01_0x47D75349). Credit in About.
- About: NexusMods button next to ATN's Ko-fi; every section title green, Graphics (Experimental) pink
  (settings.hpp default_section_tint / section_tints).
- Check: in-game SSAO on, a battle: Ambient Occlusion Game's vs Visibility Bitmask (no dark halos around soldiers);
  a video with Video AutoHDR 0 / 0.2.

## v2 (2026-10-20)

- PsychoV31: Highlight Hue Shift 0 by default (ATN); a note above Highlight Saturation / Hue Shift / Saturation
  Shift ("Fire too pink, too orange or too pale? Change the settings below.", as Tales of Arise), Highlight
  Saturation moved next to them.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (PsychoV30, default) / HDR (PsychoV31): the same PsychoV31 sliders as Rome II v1 (Musa's
  custom test31; Highlight Hue / Saturation Shift toward the SDR image's per-channel clip).
- Shared code: `hdr_to_screen.hlsli` (t01-t16, gamma 2.0 frame), `tone_mapper.hlsli` (ToneMapperHdr: t00 / t17 /
  t18), `lensflare.hlsli` (ApplyBritanniaLensFlare: lensflare00-03).
- Shader packs (Attila's hashes; dump `bin\cmd_Decompiler-1.3.16\dump_shader_twthronesofbritannia_pack`): t17 / t18
  the constant tone mappers (fixed exposure), lensflare01-03 (flare_1-3: the Lens Flare slider on every flare).
- Not ported: the constant tone mappers "with_bloom_and_gamma" (display-ready output, likely the 3D UI portholes).
- The addon still lists Rome II's tone mapper hashes (commented out); harmless.
- About: credits, ATN's Ko-fi.

Verify: SDR / V30 / V31 in a battle and on the campaign map; Lens Flare slider with the sun in view.
