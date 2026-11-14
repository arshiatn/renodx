# Total War: WARHAMMER III (RenoDX HDR, ATN)

### Parity round (2026-11-14; built only, not run in game)

- New: **Unit/Building Indicator Brightness (Extra, Experimental)**, as Attila's: the selection markers' colour x the slider
  in HDR modes. At 1 (default) and in SDR the game's own shaders run; WARP: our copies of fx_indicator's four colour passes
  are the game's bits at 1. Indicators drawn through the game's transparency (MBOIT) path keep the game's brightness.
- New, Graphics 2 of 2 (Tests), each Game's / Off / On, Game's (default) writes nothing: **Far Tree Block Shadows**
  (enable_billboard_block_shadows), **Point Light Shadow Maps** (ENABLE_POINT_LIGHT_SHADOWS), **Debris Shadows**
  (ENABLE_DYNAMIC_SHADOWS_FOR_INCIDENTAL_DESTRUCTION), **Ragdolls** (ENABLE_SIMPLE_RAG_DOLL_UPDATE). The addon finds each at
  the byte the game's code reads (checked offline against Warhammer3.exe).
- Fixed: **ray-traced shadows of the siege tentacles in view** (vertex-push, 1C3500F0: only their out-of-view copy was in
  the rays) and of fx_rigidtextureblend's small campaign meshes (03298FD5). WARP: both captured where the game draws them.
- Page dump against v59: only the rows above are new; after RT/PT only the per-game rows differ from Attila's, as before.
- Test list (ReShade.log after each; Log Details on):
  1. Control, any battle, every row above at its default: as v59.
  2. Battle at night, HDR: select units, Unit/Building Indicator Brightness 0.5, 2, 1: markers dimmer, brighter, the game's.
  3. A siege with Chaos tentacles (the replays' siege), RT/PT: the tentacles in view cast ray-traced shadows (v59: none).
  4. Campaign map, RT/PT: no stray shadows near settlements and ships (the small meshes now cast).
  5. Battle by a far forest, low sun: Far Tree Block Shadows Off, then On: far tree shadows go, then come back (else after
     the next battle load); keep the row or hide it.
  6. Night siege, game's shadows: Point Light Shadow Maps Off: torch shadows from the game go, fps against Game's; On after
     Off may need the next battle: a crash goes in the report.
  7. Siege, a wall breaking: Debris Shadows On: falling pieces cast shadows.
  8. Big battle: Ragdolls Off: bodies fall without ragdolls, CPU time against Game's.
- Not done:
  - Full-Resolution Particle Edges and Quarter: WH3's half-size particles are MBOIT (moments, then colour), so Attila's edge
    pass (one blended layer redrawn under stencil) does not carry. Smallest step: a stencil edge mask at the half layer's
    depth and both MBOIT passes redrawn into the sharp layer there, on WARP first.
  - Reflection Quality: low value (Ray-Traced Water Reflections cover water). Smallest step: a step cap for the water
    march's 100 iterations in its flags buffer, the march swapped in off 1.
  - Modded Auto Exposure: the game's auto exposure (1E448816) runs in none of 95 traces; its buffer reads 0 in all 39 that
    logged it: nothing to change. Fog & Sky Smoothing: the fog of 93 of 95 traces reads no sky cube; the rest read the
    game's own blurred mip, not Attila's 8-bit cube; the sky has Sky Debanding.
  - The indicators' MBOIT resolve (492ED8DA): our copy was not the game's bits at 1. Smallest step: its moment math put
    back in the game's order against fxc's listing.
  - Snow / rain / storm VFX rows: need a trace of a snow or storm battle.

## v59 (2026-11-14 night; stamped and installed 07:50 with renodx-tw-dlaa64.exe helper112, not run in game)

- New: **Lean ReSTIR Reuse (Experimental, off)** under Global Illumination's More Options with Path Tracing (helper112):
  ReSTIR GI's reuse 2.72 -> 1.02 ms in Nsight at 4K, the same picture in the quality scorer.
- Shared code moved / extended for the other TW games: page dump against v58: that row only.

## v58 (2026-11-13, night merges; stamped and installed 22:59 with renodx-tw-dlaa64.exe helper110, not run in game)

- Faster (Attila v159's helper110): **Swaying Shadows Range / Off-Screen Sway**: the sway's counting pass runs on 64 GPU
  threads instead of one (harness 4K, 150 k clusters: 0.4-0.5 ms less a frame, same picture).
- Attila's Grass Ray Tracing Range stays hidden here: the helper leaves out placed grass's instances, and WH3's grass has
  no instance rows for it. Page dump against v57: that hidden row only.
- Needs helper110 for the first line; with helper109 nothing changes.

## v57 (2026-11-13, CPU round; stamped and installed 21:04 with renodx-tw-dlaa64.exe helper109, not run in game)

- Faster (Attila v158's): **the helper keeps the plant sway's GPU buffers** (the RT/PT preset, or Swaying Shadows Range
  above 0 m or Off-Screen Sway off): helper107-108 made and freed three of them (16 MiB) every frame, in ATN's GPU
  recording ~1.3 ms of the GPU's memory paging and ~2.4 ms of the helper's ray-tracing thread a frame. helper109 makes
  them once (harness: 3 new buffers a frame against 0, the same results).
- Faster: **the effects' camera copies** (AO, light bounce, particle history, screen-space shadows): each made a new
  D3D11 buffer at every call; now one is kept (WARP: 9.6 -> 4.1 us a call); **the rays' draw capture**: 27 -> 3 ns a
  captured draw (~0.03 ms a frame in an 8x battle).
- Nothing changes on screen. Needs helper109 for the first line; with helper108 the rest stays.

## v56 (2026-11-13; stamped and installed 19:55 with renodx-tw-dlaa64.exe helper108, not run in game)

- Fixed (Attila v157's helper108): **Steady Torch Shadows (Experimental)** sharp and steady, no fixed black dots under Ray
  Reconstruction; **Ray Reconstruction selected but not started** (no nvngx_dlssd.dll): the rays run as without it, not raw,
  and ReShade.log says "Ray Reconstruction didn't start". Warhammer III's folder has the DLL: nothing changes while it stays.
- Needs helper108; with helper107 both stay as in v55.

## v55 (2026-11-13; stamped and installed 17:15, renodx-tw-dlaa64.exe helper107 unchanged, not run in game)

- New: **Path-Traced Ambient takes the game's sun light** (Attila's helper107 fix, now Warhammer III's): rt_sun writes the
  sun colour (lighting_VS_PS c1) to its texel 2, scaled as all 7 sun light shaders light the diffuse colour (x 0x389FB405,
  7.615e-05), so the helper lights the paths' hits with the game's sun instead of the light learned from the screen, which
  followed the view. No helper change.
- Log: the one-shot "Sun shader" line names the sun colour.
- WARP (`tools\tw\warp_tests\wh3_rt_sun_v55`, `run_v55.txt`): texel 2 = c1 x the literal, the other texels v54's bit for
  bit with spherical harmonics, the game's GI and the probes' sky on and off; without the colour's place texel 2 zero, the
  rest v54's; CONTROL the unscaled colour apart.
- Test (ATN): RT/PT with Path-Traced Ambient, a sunny battle, turn the camera around a building: the bounce light in shade
  keeps its brightness as the view turns (v54 brightened with the view); the log's "Sun shader" line has a sun colour.
- Not done: run in game.

## v54 (2026-11-13; stamped and installed 15:05 with renodx-tw-dlaa64.exe helper107, not run in game)

- Shared, no change here: Attila's Path-Traced GI of no view (helper107) needs the game's sun colour from its sun light
  shaders; Warhammer III's aren't read for it yet, so its paths stay as in v53 (helper107 gives them the same picture).
- Log: the "Ray tracing with" line names the Light Bounce's rays and bounces.
- Not done: Warhammer III's sun colour for that fix (its own rt_sun and its sun shaders' diffuse term).

- Changed: **Softer Water Reflections** and **Steadier Water Reflections** are on by default and in RT/PT, no longer
  Experimental (ATN's Attila test: they fix the unstable water). A saved Off stays Off; RT/PT turns them on.
- New: **Grass Without Waiting (Experimental, off)**, after Terrain Without Waiting in Ray Tracing Extra Options' More Options.
  The grass the game places on the GPU (its indirect draws) gave the rays its numbers through a read that waited for the
  graphics card, 1.2 ms a frame where that grass is drawn (sieges, benchmarks); with the row on they come from the copy an
  earlier frame made, never a wait. That grass's shadows can lag it by a frame or two. On WARP (`test/tw_indirect_counts`):
  0.011 ms a frame against the wait's 3.8, an earlier frame's numbers in every capture, never this frame's or an empty one's.
- Not done: the form without any lag (the capture drawn as an indirect draw onto emptied room, or the numbers copied on the
  GPU for the helper): a bridge and helper change.
- Shared, no change here: Attila's soldier shadow distance under the rays (Attila's Unit Detail hook only).
- Page dump (`w r155final` against `w r155pre`): the two water rows' defaults and labels, and the new row (default 0, shown where
  Terrain Without Waiting is); buttons unchanged.

## v52 (2026-11-13; stamped and installed 12:05 with renodx-tw-dlaa64.exe helper106, not run in game)

- New: **Steady Torch Shadows** (Experimental, off; shown with the torch rows): every light a pixel reaches is traced every
  frame with the same soft-shadow disk points (helper106).
- Shared, no change here: Pharaoh's water opacity form beside Warhammer III's (its 6 water shaders byte-identical).

## v51 (2026-11-13, the reflections' light copy shared with Pharaoh, Pharaoh's smoke quad form beside the capture (not used here); stamped and installed 03:17, not run in game; renodx-tw-dlaa64.exe helper105 as in v50)

- Moved: **Reflections Lit as This Frame**'s copy (`rt_light_copy`, 3AC630DF's R16G16B16A16F input into the bridge's R11G11B10F) is now the shared `twattila/rt_core.hpp` one (`Host::light_copy`, Pharaoh v56's water reflections use it too). The same picture: WARP against v50's path 0 texels apart in 4 cases (`tools\tw\warp_tests\light_copy_v56`; CONTROL 207 apart). Its log line is now `Ray-traced reflections: the hits read the pass's light (R16G16B16A16F, copied)` (was `First run: the frame's light for the reflections' hits`). Page dump `w v63` against v62: the date only.

Build: `cmake --build build --config Release --target renodx-twwarhammer3.addon64`. Manual check: as v50's; ReShade.log shows the new line once with Reflections Lit as This Frame on.

## v50 (2026-11-13, RT/PT no longer leaves GT-VBAO under Path-Traced AO; stamped and installed 00:43, not run in game; renodx-tw-dlaa64.exe helper105 as in v49)

- Fixed: **RT/PT** now turns GT-VBAO off. Here the paths' AO multiplies the game's SSAO (`graphics_page::Bindings::rays_replace_ao` false), so with GT-VBAO, the page's default, still on it ran hidden under the paths and its shading added to theirs. The AO method's Path-Traced choice already turned it off (`ChooseAo`); the preset now writes the same (`ssao_mode` 0), in the shared `graphics_page.hpp`. Attila's RT/PT is unchanged (its rays replace the game's AO). Page dump `w v62` vs v61: only RT/PT's writes gain `ssao_mode 1 -> 0`, besides the build date.

Build: `cmake --build build --config Release --target renodx-twwarhammer3.addon64` (shared change: the tw-port-checklist x64 list and build32 Attila, Rome II, Thrones built too). Manual check: press RT/PT; the AO method reads Path-Traced and GT-VBAO's rows stay hidden, and corners under the paths look as they did in v49 with GT-VBAO turned off by hand.

## v49 (2026-11-12, Shadow Map Size under Screen-Space Shadows and rays; stamped and installed 22:32, not run in game; renodx-tw-dlaa64.exe helper105 as in v48)

- **Shadow Map Size** shows under Bend Screen-Space (as before) and now under Ray-Traced (DXR) too while **Rays Replace Shadow Maps** is off: then the rays multiply the game's shadow maps, so their size still matters. With that row on (the default) the rays alone shadow sunlit surfaces and the row stays hidden (`graphics_page::Bindings::rays_replace_maps`, read live; Attila and Pharaoh unchanged).

## v48 (2026-11-12, after v47's install; stamped and installed, not run in game; renodx-tw-dlaa64.exe helper105 as in v47)

- Fixed: **Sprite Soldier Shadows** is no longer shown: Warhammer III has no sprite soldiers (its sun-facing cards are far trees).
- The `renodx_trace_now` frame trace moved into the shared trace code (same behaviour).

## v47 (2026-11-12, ATN's third list; stamped and installed, not run in game; renodx-tw-dlaa64.exe helper105)

- **Torch and fire shadows: up to 128 lights** (bridge v31, helper104; Experimental rows, defaults as before): **Torch and Fire Shadow Lights** 32 / 64 / 128 (the nearest and brightest first) and **Torch and Fire Shadow Range** (metres from the camera, 0 any). Each 8 x 8 block of the screen traces the 32 lights that reach it most (the helper's `rt_lights` lists them below the light mask's pixels), so a burning city's far fires keep their shadows without each pixel tracing more. The light shaders match the drawn light within its block's list (`rt_local_lights.hlsli`). Harness: default and 125 lights PASS (trace 0.15 -> 0.19 ms, 29 -> 125 lights).
- **Screen-space torch shadows:** a 32 option and their own range row (WARP test PASS: the pick within the range is the CPU's).
- **Correction to v149's smoke note:** the stream was not full. The log's 262,144 particles a frame is the game drawing its whole particle pool every frame (dead ones too); the stream holds 524,288 and the siege wanted at most 35,304 of High's 65,536 quads. The likely cause of the flicker is each sun ray's cap in the smoke (it stops after 32 dimming / 64 met particles, which come first following the smoke's BVH, rebuilt each frame). Extreme and Maximum raise that cap; an A/B of Ultra against Extreme in one siege tells.
- Particles: unchanged (the game's own half layer; Attila's Quarter and edges are Attila's only for now).
- **About:** the shared credits in `twattila/credits.hpp`; MiniEngine's licence next to CMAA2; every zip gets ReShade's, ImGui's, Detours', frozen's and gtl's licences.
- **Light Through Foliage** for the screen-space shadows (0.5 as in Attila): grass, leaves and tree cards are found by the G-buffer's foliage bit (128 in channel 3's alpha, the bit the game's own sun mask lets light through); off under MSAA. WARP test PASS: 0 pixels apart from the depth-copy result.
- **RT/PT preset = Attila's** (ATN's 2026-11-12 settings, `graphics_page::RAYS_PRESET`), and its values are now this page's defaults for every row the methods leave (the methods stay off): Bounces 1; Far Tree, Fog, Smoke and Sprite Soldier Shadows, Light Through Leaves, Torch and Fire Shadows with RIS, Light in Fog, Steady Bounce Light, Specular Occlusion, Water Reflections and Caustics on; GI Range 50 m, Rays 1, Resolution Full; Reflection Reuse 2; Softness 0.8; Smoke Shadow Quality Ultra; Swaying Shadows Range 50 m.

- **Rays a Pixel: Shadows / Ray-Traced AO / Reflections / Global Illumination** under Anti-Aliasing's More Options (ATN); Rays a Pixel stays under Upscaling Mode.
- The Frame Generation line "pauses while this menu is open, applies when it closes" in yellow.
- Fixed: **Path Tracing's light too bright on surfaces the camera doesn't see** (helper105; ATN's report): with SHaRC off the paths' own radiance cache keyed only a world cell (0.5 to 2 m at Total War's distances), so a hidden step face or a wall's back in shade took the light of the sunlit top or front beside it, more or less as the view hid it. Its cells are now one per face a surface turns to, a ray shorter than its cell takes no cached light (as SHaRC's), and last frame's colour (here the tone mapper's output) stands in only where a game passes no light of this frame. Harness, a wall's back in shade (seen from 45 m): the shadowed ground behind it 0.172 with the back hidden, 0.033 with it seen; now 0.116 hidden, the same as with the cache off (0.116).
- Fixed: **the same, where the screen's light stood in** (helper105): a ray's hit took a pixel's light within 5 cm and 2% of the depth of it (0.95 m at 45 m), whatever that pixel showed, so a wall's back or a step's face in shade got the sunlit top seen there. Now only from a pixel showing that face (normals within 75 degrees). Harness, the wall's back hidden: 0.116 -> 0.020 (0.033 seen); a closed room the screen doesn't show: its floor's bounce 0.442 with helper104, 0.062 now.
- Fixed: **warm dots jumping about in ray-traced water reflections while paused** (helper105): water is a mirror to the reflections (one ray a pixel, no history, no filter), and an eighth of their hits each frame left the radiance cache out for a one-bounce path of their own (helper102's relearning, meant for the Light Bounce's paths, which keep it). The reflections no longer do. Harness, still water (roughness 0.03) reflecting shapes the screen doesn't show: mean change a frame 0.035 -> 0.015.
- New: **Steadier Water Reflections (Experimental, off)**, under Water Reflections (helper105, `RT_REFLECTIONS_STEADY`): a mirror's reflection (calm water: one ray a pixel, no history, no filter) stays within what its 5 x 5 neighbours on the same surface show, a tenth of their range past it. Harness (still water reflecting shapes the screen doesn't show): mean change a frame 0.0084 -> 0.0083, pixels a frame changing by more than 0.05: 1307 -> 1301. Small: what is left changes a radiance cache cell at a time, which a neighbour clamp can't see. Test it for isolated dots only.
- Reflections' **Samples** tooltip: more rays smooth rough metal and armour; water and mirrors take one ray.
- New: **Softer Water Reflections (Experimental, off)**, after Steadier Water Reflections (helper105, `RT_REFLECTIONS_WATER_POSITIONS`): the water's roughness guide at least 0.06 (Attila's guides give 0.03, Warhammer III's 0.01; Ray Reconstruction gets the same), past the reflections' mirror limit (0.05): water takes Samples' rays, a 3 x 3 filter and about 4.5 frames of history. That history's test reads last frame's water surface (copied at the reflections request), not the riverbed the frame's first request kept: two copies at the render size a frame (about 0.3 ms at 4K, not measured; 130 MB at 4K while on). Harness (still water): mean change a frame 0.0084 -> 0.0013, pixels a frame changing by more than 0.05: 1307 -> 0; a light change shows 92% of itself the next frame before, 20 / 35 / 56 / 77% after 1 / 2 / 4 / 8 frames with the row (the roughness without the copy: 89%, its history failed).
- Fixed (2026-11-12, after the installed v47 copy; built only): **Sprite Soldier Shadows is hidden.** Its row gated only flat soldiers turned to the sun, and every sun-facing draw here is a far tree (CC8297ED, 01830D95: Far Tree Shadows' row), so it did nothing. Shared `twattila/rt_scene.hpp`: the row shows only where a sun-facing entry isn't a far tree (Attila's sprite soldiers 1B9E7692). The RT/PT preset's value for it stays and does nothing (page dump: the row's states and the date only).
- Shared `twattila/engine_tests.hpp` (2026-11-12, after the installed v47 copy; built only): Attila's test tooltips lost their words in capitals (GRASS, SHADOWS and the other section names, there since those became constants). This page is unchanged: its tests carry their own tooltips (page dump: the build date only).

## v46 (2026-11-12, PsychoV35 in every game; built only, not stamped, not run in game; renodx-tw-dlaa64.exe helper103 as in v45)

- **Red line under Anti-Aliasing** (ATN: two players had no upscaler): "In-game Anti-Aliasing must be TAA or TAA High for DLSS, FSR, XeSS and Frame Generation (restart after changing it)." The upscalers take over the game's TAA pass; `gfx_aa` below 2 (Off, FXAA) has none, and preferences.script.txt is read once at start (`taa_dlss.hpp`). The tooltip and status line name the same two (TAA, TAA High).
- PsychoV35's pieces shared by every game: `ApplyPsychoV35` takes a game's own mid gray and contrast scale (defaults as before), Musa's file has SM3 forms (RUSE) behind `__SHADER_TARGET_MAJOR`; SM4+ the same code. No change in the picture here.

## v45 (2026-11-12, ATN's list; stamped and installed, not run in game; renodx-tw-dlaa64.exe helper103 as in v44)

- **PsychoV35 in every Recommended preset** (ATN; the SDR preset stays SDR). It is now **Musa's custom PsychoV35** (`twattila/psycho_test35.hlsli`, his file, only its include path changed) with **its own sliders** (`v35_*`, apart from V31's) in V31's order: Exposure, Highlights, Shadows, Contrast, Highlight Contrast, Shadow Contrast, Flare, Saturation / Purity, Dechroma, Highlight Roll-off, Gamut Compression, then "Fire too pink, too orange or too pale?", Highlight Saturation and **Hue Clip Emulation** (was Hue Clip; default 100). Anchored at mid gray (0.18) as V31; Highlight Roll-off 1 = Musa's compression 1.5. Rendering Mode label "HDR (PsychoV35)".
- **Smoke Shadow Quality: Extreme and Maximum** above Ultra: 64 layers a sun ray, and room for 2x / 4x Ultra's smoke quads and streamed particles. ATN's siege log at Ultra: the particle stream was full every 10 s window (262,144 a frame on average), so which smoke got a shadow changed from frame to frame (flicker).
- **Rays a Pixel's four rows** (Shadows, AO, Reflections, Global Illumination) now sit under Rays a Pixel in Anti-Aliasing and Upscaling, shown when it gives more than one ray.
- **Ray Tracing Denoiser: NRD by default** (was Built-in). Only used while DLSS Ray Reconstruction is off and something is ray-traced.
- **RT/PT preset: ATN's settings** (Softness 0.80, Smoke Quality Ultra, RIS, Sprite Soldier and Far Tree Shadows, Light Through All Banners, Smooth Banner Shadows, Steady Bounce Light, reflections' Reuse Own ReSTIR; the rest as before).
- **More Options reverted** on HDR & Effects and on Graphics 2 of 2. Ray-Traced and Path-Traced AO and Reflections and Water show every row again; GT-VBAO's More Options holds its Thickness too.
- **Screen-space shadows:** Auto Thickness removed (ATN: it broke at some angles); everything but Length under More Options; Shadow Map Size shown under Bend Screen-Space too; **Light Through Banners (0.35) and Coloured Shadows on by default**.
- Once, at the first start: the saved Ray Tracing Denoiser, Light Through Banners and Coloured Shadows are cleared, so they take the new defaults.
- **About:** the Ko-fi, NexusMods and GitHub buttons first, then the credits and licences.

Reset stays HDR (Extended). Page dump against v44: 12 new keys (`v35_*`), ScreenSpaceShadowsAutoThickness gone, no other key added or lost. WARP test/tw_psychov35 (x86 + amd64): SDR / Extended / V30 / V31 0 pixels different from d3d6425b; V35 finite and within Peak (Gamut Compression 0: within 1%).

## v44 (2026-11-12, a cleaner page; renodx-tw-dlaa64.exe helper103 as in v43; built, not run in game, not installed)

Same picture and settings as v43: every key kept, only where rows show moved (Attila's `graphics_page.hpp`). Page dump against v43: three new More Options lines, no key added or lost.

- **Rays a Pixel under Upscaling Mode** (ATN: players at Ultra Performance never found it at the page's end), shown while something is ray-traced, now with Attila's label "(Use higher for lower scalings)". Its default stays 1. The page's presets no longer change it (RT/PT set it back to 1 before).
- **Into More Options** (closed at each start): Bend's five Experimental rows; Steady Bounce Light and Sky as Bright as the Game's; Reflections Lit as This Frame, Fog in Reflections, Reflections as Rough as the Game's (before a stray row at the page's end) and Ray-Traced Specular Occlusion; Ray Tracing Denoiser and AO under Ray Reconstruction; Graphics 2 of 2's test sections (one More Options under Graphics (Experimental)); on HDR & Effects the three particle rows of VFX.

## v43 (2026-11-11, PsychoV35 round; ATN's test; renodx-tw-dlaa64.exe helper103 as in v42; not run in game; no players' zip)

The first build after the upstream merge (d3d6425b). With Rendering Mode SDR / Extended / V30 / V31 the picture is v42's (WARP below). Page against v42's: the new rows, Rendering Mode's label and tooltip, the credits and the Build Date.

- **Rendering Mode "PsychoV35 (Experimental)"** (mode 4, not the default; the Recommended presets stay PsychoV31): t00's handoff as in V31, then ShortFuse's `psychotm_test35` anchored at 0.18 in and out (the handoff carries the game's exposure), V31's LUT bridge order. PsychoV31's Exposure, Highlights, Shadows, Contrast, Saturation / Purity, Highlight Roll-off and Gamut Compression rows drive it; V31's highlight saturation / hue shifts are not used. Contrast is kept at 0.01 or above; V35's NaN on near-black pixels is black.
- **Hue Clip** (PsychoV35, 0 - 100, default 100; Reset writes it): before the grade, the middle channel bent toward where the stock Extended Reinhard's per-channel clip, saturate(g_brightness * x), puts it (Attila's `HueClip`); sky blue twice as far, clamped. Default 100: the game's Reinhard keeps growing into the clip, so its SDR fire and sky are far more yellow / cyan (OkLab hue error against SDR where it clips: fire 30.1 -> 5.2 degrees, sky 26.6 -> 7.1).

Tests: WARP `test/tw_psychov35` x86 + amd64 PASS: SDR / Extended / V30 / V31 0 pixels different from d3d6425b in t00, every other shader the same bytes; V35 within peak and finite over the ramps at 1000 / 400 nits and SDR.

## v42 (2026-11-11, round v42; renodx-tw-dlaa64.exe helper103 as in v41, bridge v30: ship them together; not run in game; no players' zip)

Every new row is Experimental and off: with them off the picture is v41's (WARP below). Page against v41's: the six rows, the DLSS Preset tooltip without Auto (e9bb6b9e, after v41's dump) and the Build Date.

- **Smooth Half-Size Particles (Experimental, off; VFX):** the game's half-size particle layer (what it draws soft, and all sprites with Particles at Half Resolution) kept over frames and brought up by depth. Its composite (43606BB8) is drawn with `particle_composite.ps_5_0.hlsl`: the same sums, with the half layer's colour and zeroth moment smoothed (`twattila/particle_history.hpp`: last frame's moved by the depth, held within this frame's 3 x 3, kept while this frame's 3 x 3 mean matches last frame's) and each pixel taking the 2 x 2 texels around it by depth (bilinear unless one is more than 10% off, then by nearness: the rule of the game's own unused filtered composite 1EF17B33). Smoke no longer covers the edge of a soldier in front of it; a flickering spark averages, a moving one stays whole. WARP `test/tw_particle_history`: at a first frame on flat depth it is the game's composite to 2.75e-6; at a box edge the box gets 0.2% of the smoke behind it, the game's composite half. RTX 5090, 4K: +0.07 ms.
- **Bend Screen-Space Shadows: five Experimental rows, off** (shared `twattila/screen_space_shadows.hpp`, `screen_space_lights.hpp`; an extended march `sss_bend_extended` in four variants that runs only where a row asks for it):
  - **Auto Thickness:** each object taken to be as thick as it is wide (Ottosson's rule, CC0): fuller shadows behind houses and walls, poles as thin as before.
  - **Light Through Banners** (0 to 1) and **Coloured Shadows:** banners, flags, tents and sails (233A0281, 219075F3, 72CDA7A1, 444F1190; C0ED2883 with Light Through All Banners) drawn again into their own depth after the game's draw; their shadows keep the row's share of the sun, tinted by the cloth (t50, where the rays bind none).
  - **Softer Away from the Caster** (the sun's size in degrees, 0 = off): sharp at contact, softer farther from the caster; a fixed disc, no temporal pattern.
  - **Torch and Fire Shadows** (Off / 4 / 8 / 16, without ray-traced point light shadows): the light draws' instances copied on the GPU, the nearest and brightest picked as the rays pick them, one screen-space march each over the rectangle their reach covers (two screens' cap); the replaced light shaders read the result as they read the rays'.
  - Not possible in screen space, said in D.md: casters off screen, far reach. Smoke under them was already the game's (its t_vfx_shadows in the sun mask).
  - RTX 5090, 4K at 100% (WARP-checked shaders timed on hardware): Banners +0.08 ms, Coloured +0.60, Softer +0.53, Auto +0.73, torches 0.35 / 0.65 / 1.2 ms for 4 / 8 / 16 lights 40 m away (at most 1.4 ms close).
- **Weapon jitter after the camera comes back (Attila's fix, shared):** no code of Warhammer III's changed: it has no unit motion of ours (its skinned units' motion is the game's; ours only MOVING_VS weapons and TIME_VS / GAP_PS, all in the game's draw). If weapons jitter here, Object Motion Vectors off / on tells the game's from ours (run request B4).

Tests: WARP `test/tw_particle_history`, `test/tw_screen_space_shadows` (rows off to the bit against the plain march; 50% / 58% render scale counts), every `test/tw_*` x86 / amd64 PASS (round v42 E). Helper103 unchanged: seven harness modes on the stamped helper pass.

## v41 (2026-11-11, fixes; renodx-tw-dlaa64.exe helper103, bridge v30: ship them together; not run in game; no players' zip)

- **Path-Traced AO no longer too dark ("cooked").** With the game's Global Illumination on, its sun shader lights open ground from its light probes (the environment maps), not from the sky's spherical harmonics our Path-Traced AO took: our sky was 12 to 58 times dimmer than the screen's shade. New row **Sky as Bright as the Game's** (on): our sky built from the same probes. Off: v40's. The game's SSAO stays skipped under ray-traced AO, Path-Traced AO and GT-VBAO.
- **Cheap ray features stay cheap.** Fog Shadows alone no longer switch on Ray Reconstruction or wait for the terrain's count, and the grass is captured only while a ray that sees it runs (sun shadows, AO, Path-Traced AO).
- **Far Tree Shadows keep their shape** (helper103): the far trees' cards no longer use up the smoke's count, so a ray through a wood stops at the first solid card whatever their order.
- **DLSS Preset Auto removed:** a saved Auto loads as L.
- **Frame generation after DLSS 6x -> FSR:** ReShade no longer loads into the helper (its "errors loading add-ons" banner sat on FSR's window); a count above what the running backend presents asks for its most.
- **Texture LOD Bias on All Textures (Experimental, off):** the bias also on textures the game filters without anisotropy (its terrain, building, grass and texture-blend shaders take named samplers, one of them trilinear by name). With Log Details: one line with the samplers the bias takes.
- **Skip the Shadow Maps:** the game's kept shadow maps stay drawn, so switching the rays off mid-battle (RT/PT -> Reset) gives the game's shadows back at once (they stayed empty or stale before: a flat picture).
- **Shadow Maps Under Rays (Experimental, off):** with Rays Replace Shadow Maps, the game's shadow maps at 2048 or 1024 for what still reads them (water, lit effects, particles): about 400 / 500 MB less video memory. From the next start, until the one after.
- Details, numbers and the test list: `tools/tw/wh3_v41/reports/v41.md`.

## v40 (2026-11-11, fixes and performance; renodx-tw-dlaa64.exe helper102, bridge v30: ship them together; not run in game; no players' zip)

- **Helper102** (Attila v144's): Global Illumination > Path-Traced > **Bounces** now does what it says and ends at 2 (3 and 4 measured the same as 2; a saved 3 or 4 runs as 2). It only matters for light from places the camera does not see; out-of-sight light keeps updating during a battle. Frame Generation's status says that it pauses while this menu is open.
- **Upscaling below 100%: two copies of the whole display less each frame** (the upscaler's result is written straight into the picture; the Light Bounce copies only the rendered part of the light). Same picture, bit for bit on WARP. About 0.08 ms at 4K on an RTX 5090: small.
- **Terrain Without Waiting (Experimental, off):** a battle's first frames, and the first after a menu, wait for the terrain once more, so the rays never start without terrain.
- **Looked at and not built, with numbers** (`tools/tw/wh3_v40/reports/v40.md`): a depth pre-pass for grass and leaves is slower on the RTX 5090, not faster (2.6 to 3.4 ms became 4.7 to 5.4 ms in the test scene); the wait at the sun's draw is the helper's own GPU work plus about 0.45 ms of hand-over, not idle time.
- **Fastest switches you already have** (4K, path tracing): Light Bounce Resolution Half (about 1 ms), Reflections off or fewer samples, Ray Reconstruction off (DLSS alone is much cheaper; the rays then need the denoisers). Upscaling Mode, measured in ATN's 30 s recording while he stepped through it: Ultra Performance 15.6 ms a frame, Performance 20.5, Balanced 22.7, Quality 26.1, 100% (DLAA) 41.3.

## v39 (2026-11-11; renodx-tw-dlaa64.exe helper99, bridge v30, both unchanged; not run in game; no players' zip)

- **Global Illumination no longer brightens the shade ("cooked").** Since v37 our Global Illumination switched the game's light probes off, and with them the sky's shade under trees, roofs and walls: shade was 2 to 2.8 times as bright as in the game's own picture (sunlit ground the same). New row **Sky Shade From the Game's Probes**, on: the game's probes bring the sky's light as they do in the game's own picture, only their bounce is replaced by ours. Fog, water and effects get the game's probes back too. Off: v38's picture.
- **Trees Out of View up to 150 m (Experimental, off)**, for Skip the Shadow Maps: the game draws trees every frame only within the first shadow map (40 m); farther ones behind the camera reached the rays seldom and were lost when ray tracing restarted. The row has the game draw every tree within 150 m each frame, so their ray-traced shadows stay. Needs Rays Replace Shadow Maps and Shadow Casters Out of View. Smoke's shadows near the camera get softer. Farther than 150 m: as before.
- **DLSS Preset:** L is the default again (Auto stays a choice).
- Details, numbers and the test list: `tools/tw/wh3_batch6/reports/v39.md`; performance ideas: `perf_ideas.md` beside it.

## v38 (2026-11-10, batch 6; renodx-tw-dlaa64.exe helper99, bridge v30: ship them together; one replay run before the close's fixes, see each section; no players' zip)

- **Turning Global Illumination off in a battle no longer crashes the game** (v37). Run in game: off and on twice, 82 s more, no crash.
- **Reflections Lit as This Frame, on:** ray-traced reflections with Global Illumination off are lit by this frame (they read last frame's picture, with black where the screen never looked). Run in game.
- **After a GPU reset** the game gets its window back, and the HDR output is chosen again for each new swap chain. A real reset: not tested.
- **DLSS Preset: Auto** (a choice; L stays the default, ATN 2026-11-10): L at 100%, K below, where L and M darken fine detail.
- **Particles Skip the Denoiser and Picture Before Particles work below 100%** (both Experimental, off).
- **Far Tree Shadows:** the far trees' cards no longer stand among the units for the other rays.
- **New rows, all Experimental and off:** Shadow Map Size (8192), Terrain Without Waiting, Fog in Reflections, Skip the Shadow Maps, One Model per Place Out of View, Cut-outs on Units Out of View, Cut-outs on More Models, Tree Trunks as Drawn, No Shadows From Hidden Parts.
- **Not fixed:** the black spot on tents with ray-traced shadows (cause not proven; three checks at the tent, below).

Details per phase below. Round report and test list: `tools/tw/wh3_batch6/reports/round_report.md`.

### Close: the review's fixes (WH3 batch 6 C, 2026-11-10; not run in game)

- **When frames stop for 20 s** the log gets its one line as before; `renodx-stall.dmp` is written only once a device was lost in this session, or with `StallDump=1` under `[Total War]` in ReShade.ini. A long load is such a stop too, and the dump's memory could end a 32-bit game near its limit: each stack goes straight to the file now, and nothing thrown leaves the watching thread.
- **A chosen HDR10 or scRGB output** stays unless no display has Windows HDR on (the window can still stand on an SDR display when its swap chain is made). **Auto** keeps its last output when Windows gives no answer.
- **Graphics (Experimental) values** are no longer scaled twice after a device that never drew a frame.
- **Shadow maps' copy (C883E3FA):** the game's own size constant on every map the game made; the map's own size only on one Shadow Map Size made larger.
- **Fog in Reflections** (Experimental, off) puts every target of the fog's draw back, and clears its numbers when a frame has no fog draw.
- **`Shadow map sizes` and `Cascades' split distances` log lines** only with Log Details: a default start reads no buffer back.
- **Bitmask AO** keeps its depth, camera and passes when another device ends.
- The game's quit cannot wait on the watching thread for ever (2 s at most).

Tests: `test/tw_device_loss`, `tw_native_hook_lifetime`, `tw_shadow_map_size`, `tw_game_gi` `run.ps1`.

### AO depth-level recovery (F15, 2026-11-09, GPT-6 Astra; not run in game)

Failed depth-level texture/view creation now releases all partial views and retries at the same resolution after a one-second cooldown. Build: `cmake --build build --config Release --target renodx-twwarhammer3.addon64`. Manual check: use an AO mode that reads depth levels, change resolution/render scale, and verify stable AO; after a transient allocation failure, depth levels should recover without another resolution change. Failure injection and real D3D11 WARP readback results are in `docs/Attila-crash-audit-2026-11-08.md` at the repository root. No shader behavior was changed.

Build: `cmake --build --preset clang-x64-release --target renodx-twwarhammer3.addon64`.

### Terrain's count, DLSS preset, particles under Ray Reconstruction below 100%, motion logs (WH3 batch 6 P5, 2026-11-10; not run in game; helper98 unchanged)

- **Terrain Without Waiting (`RayTracedTerrainCountLater`, Shadows' More Options, Experimental, off):** the rays' terrain is the tessellated draw streamed out, and how many triangles it wrote is known only to the GPU: each frame the game's thread waited for that count (8 to 16 ms a frame in a battle's first 30 s, 0.2 ms later; batch 5). On: the count is read whenever the GPU has it (a frame or more late, no wait, no Flush), the frame's triangles are streamed onto empty vertices (a NaN x: the helper skips them) as many as the newest count and an eighth, and the capture takes that size. Too few after a camera cut: the last triangles are missing from the rays for one or two frames. The Flush before the terrain's stream output stays. `twattila/rt_core.hpp` Settings::terrain_count_later (only this game has tessellated scene shaders). Offline: `tools/tw/warp_tests/terrain_count_later` (0 earlier-frame vertices; CPU at the trace 15.2 ms with the wait behind 15 ms of GPU work, 0.001 ms without, RTX 5090), harness modes `terrain_tail*` (200 K and 1 M empty triangles in a terrain capture: all checks pass; they cost the helper's scene pass 0.7 and 1.1 ms). Batch 3's try of the same idea gained no fps in two steady views (the wait burns a core while the GPU is behind) and p99 rose 0.8 ms: the run decides (P5-1).
- **DLSS Preset: Auto (new; the default is L again since v39): L at 100%, K below.** L came out 10% darker at 50% (M 2.4%, K 0.7%; batch 3). Not the square root: through real DLSS 310.9 in the harness (`HARNESS_UPSCALER_TONES`, 1280 x 720 out, a two-stop texture with texels of two output pixels) L gives 0.911 with the light as it is and 0.930 with its square root at 50%, M 0.922 and 0.904, K 0.998 (K 0.990 to 0.998 at 100, 67, 50 and 33%; flat pictures 1.00 in every preset). With the light as it is L also loses dark detail under a bright band (match 0.59 against 0.86): the square root stays. A saved K, M or L stays what it was; the choice is this game's alone (`addon.cpp`).
- **Particles Skip the Denoiser and Picture Before Particles now work below 100%** (both Experimental, off). There the upscaler's colour is the tone mapper's picture, so no copy taken at the particles' composite is in its space. The composite (43606BB8) is drawn once more without blending into a texture of the addon's (light, transmittance), the colour before it is copied, and after the tone mapper `rr_extra_scaled.cs_5_0.hlsl` takes the picture without particles as the picture times before / after of the scene's light at the pixel; the layer is what makes NVIDIA's blend give the picture again. The particles stay in the game's picture (bloom and depth of field take them, unlike at 100%). WARP `tools/tw/warp_tests/rr_scaled_wh3`: no particles: the colour equals dlss_scaled_prepare's to the bit; layer + (1 - a) x colour is the picture to 2e-6; the layer is never below 0. Depth of Field Guide stays at 100%.
- **Particle Trails:** last frame's taps outside the rendered part count as no particles (below 100% the part lies in the texture's top-left). At 100% nothing changes (WARP smear_wh3: the same flags).
- **Object motion, found by a census of all 93 traces:** the skinned texture-blend units (vs C0ED2883 with ps 1405BFB4, 28 draws a frame in the replays) move by bones and get the camera's motion only: no previous position in the vertex shader, no velocity target in the pixel shader. Capture flags (vs 444F1190) have no last rows either. Nothing in their bytes gives a last position, so no fix yet; with Log Details on, 6 frames of 4 draws each are read back and logged (`Object motion: ... instance data`): the flags' 120-byte rows and the units' 184-byte records, for the run to say whether instances keep their order and whether the record's +48 / +64 / +80 are last frame's rows. Sails and ropes are drawn in no trace.

Build: `cmake --build build --config Release --target renodx-twwarhammer3.addon64`. In game: `tools/tw/wh3_batch6/run_requests.md` P5-1 to P5-7.

### Casters out of view and the shadow maps (WH3 batch 6 P4, 2026-11-10; not run in game; needs **helper98**, bridge v30)

Three rows under Shadows' More Options, all Experimental and off; with all off nothing changes but two log lines and the Skip Trees tooltip. An older helper ignores the new flag.

- **One Model per Place Out of View (`RayTracedOneAtAPlace`):** a kept caster behind the camera goes when the frame placed another mesh where it stands (within 2.5 cm) and its own mesh was not drawn, or was placed whole without it: the game swapped the level of detail, or drew the ruin. Before, both stayed until the scene ended. Helper: `rt_place.cs_6_5.hlsl` PLACE_ONE_AT_A_PLACE, flag `RT_ONE_AT_A_PLACE`. Not covered: a thing that moved away or vanished with nothing drawn in its place (it stays until seen), and what the shadow pass never draws.
- **Cut-outs on Units Out of View (`RayTracedCascadeUnitCutOuts`):** units captured from the shadow pass (BADA4FE3, 46E626D5) take the mask the game's own shadow pixel shader tests (B2358145: t0's alpha at 0.5 at TEXCOORD1, by its bytes), their UV streamed through `rt_outside_view_uv.gs_5_0.hlsl`. Off: solid, as before. Units' weapons and shields in the shadow pass are 53C5A7EC's draws, placed since v34.
- **Skip the Shadow Maps (`RayTracedSkipCascades`):** with Rays Replace Shadow Maps no caster is drawn into the cascades (13 vertex shaders; the units' and trees' capture for the rays runs before the skip). Who still reads the maps then, by bytes and 93 traces: water (2076D289, DFA2AEA2, 034010C4), lit effect meshes (9EA8D889), the particles' fog (VS 652893D4) and the particles' shadows through D5DCC85F's depth copy: they lose the sun's shadow. The fog reads none with Fog Shadows on (battlefog02 / 03 take the rays' texture). 454 pixel shaders of the dump can read the maps; 7 were ever seen drawn. Skip Trees in Shadow Maps stays as the smaller step.
- **Hills Out of View** does nothing for the sun's shadow by construction: the hills' shadow is the game's terrain shadow map (from the height map, not from the view), which the rays' mask keeps. The row only feeds AO, Light Bounce and reflections, as its tooltip says.
- **Shadow Map Size:** no row. With Log Details (since v38; a default start reads nothing back), log lines `Shadow map sizes:` and `Cascades' split distances:` print what the game keeps (the shared shadow constants' first four floats; C883E3FA's b0 when the static maps are renewed; 800F7F7F's and D5DCC85F's targets, viewports and inputs), and a Frame Trace lists those shaders' constants.

Build: `cmake --build build --config Release --target renodx-twwarhammer3.addon64`; helper: `src\games\twattila\dlaa_helper\build.ps1 build\tw_dlaa_helper98`; `bin\fxc.exe` line in `rt_outside_view_uv.gs_5_0.hlsl` after a change to it. Test in game: `tools/tw/wh3_batch6/run_requests.md` P4-1 to P4-8.

### After the run (WH3 batch 6 P7, 2026-11-10; nothing of it run in game; **helper99**, bridge v30)

- **Shadow Map Size (`GraphicsShadowMapSize`, Experimental, off; shown with the game's shadow maps; applies after a restart):** the sun's shadow maps at 8192 in place of 4096, at the game's highest Shadows setting, about 1.5 GB more video memory. The game's code holds no size to change, so `shadow_map_size.hpp` puts it where D3D sees it: the two 4096 x 4096 [4] R32 depth arrays are made larger at their creation; every draw into them gets the larger viewport and scissor (known by the bound depth, so no caster shader is missed); `shadowmap_PS`'s size (its first four floats; the buffer is 640 bytes in all 454 readers and nothing else is) is written where the game uploads it (`buffer_writes.hpp`, the Global Illumination flag's way); the kept maps' copy (C883E3FA) and the two half-size copies (800F7F7F, D5DCC85F) are replaced by copies that read the map's own size (`shaders/shadowcopy*`), the same to the bit at the game's size. With the row off at the game's start no event is registered. Test: `test\tw_shadow_map_size\run.ps1` (WARP: the maps' creation, viewport and scissor in both call orders and on a deferred context, the constants, a caster read back at the same uv at 1x and 2x, the three shaders against the game's bytes). Not reached: what the game computes itself from 4096 (sample bias, texel snapping). In game: start with the row on, Game's Shadow Maps, highest Shadows: the log must say `Shadow Map Size: the game's shadow maps made at 8192` and `the shared shadow constants say 8192`; shadows sharper, at the same place, in the fog and on water too, particles' shadows as before.
- **Reflections Lit as This Frame** is on by default (below). **Picture Before Particles** greys out with Particles Skip the Denoiser: the colour is already that picture (the log says so).
- Frame Trace's state line ends with `cull N [ccw] bias N`; Log Details says `solid, its cut-outs without room` once a vertex shader; a device's end says `References on the game's device: N as its end began, M after the addon's releases.`; the helper says `Upscaling: ..., preset P, ...` at a change and, with Log Details, the placed instances after One Model per Place changed.
- **No Shadows From Hidden Parts** stays off: its check failed in the replay (0 of 512 draws) and the tent's cause is not proven. At the tent's view: (1) Shadow Casters Out of View off: spots gone = the shadow pass's solid copy; (2) the row on: gone = the fade; (3) Log Details + Frame Trace: the 204-index draw's `cull` against the cloth's, and a `solid, its cut-outs without room` line.

### Reflections' light and fog (WH3 batch 6 P3, 2026-11-10; not run in game; needs **helper97**, bridge v30)

Two rows under Reflections and Water. Since P7 (the run of 2026-11-10) **Reflections Lit as This Frame is on by default and no longer Experimental**: with Global Illumination off the reflections' view went from 3.68x of the path-traced reference to 1.03x, under Path Tracing the picture stayed within the live noise floor, cost none (22.10 against 22.18 ms). Fog in Reflections stays Experimental and off (+0.4 ms, no river seen yet). The helper is new (build/tw_dlaa_helper97): ship it with the addon. An older helper ignores the fog's flag (no fog in reflections), nothing else differs; the bridge's block did not change.

- **Reflections Lit as This Frame (`RayTracedReflectionLight`):** the game's own screen-space march reads the lit scene of this frame (cs 3AC630DF copies the light buffer into the radiance's first level: R16G16B16A16F, the water and our Global Illumination in it, before the fog). The rays' hits read the Light Bounce's prepared light instead, and with Global Illumination off (no Light Bounce request) last frame's picture, which below 100% is after the tone mapper; what the screen never saw stayed black, since the helper learns its lighting only from a light buffer. With the row the addon keeps 3AC630DF's input, writes it as R11G11B10F (`rt_light_copy`) and passes it with the reflections' request in every Global Illumination state (ours on, ours off with the game's own back, Path-Traced AO): hits the screen sees take the same values as the game's march, what it never saw the sun through a shadow ray and the light learned from that buffer. Log: `First run: the frame's light for the reflections' hits`.
- **Fog in Reflections (`RayTracedReflectionFog`):** the game draws its fog after the reflection pass (34719E09's opacity, A8F5202D's light, 78E4CBF2), over the water at the water's depth. So what a reflection shows has no fog between the water and the thing reflected, in the game's own march too; with rays every far hill is met, and comes out darker than the hill above the water. With the row the helper puts the game's fog on that leg: 34719E09's opacity with the ray's start in the camera's place and the clear distances less what the light travelled to the water (`twattila/rt_leg_fog.hlsli`), and the fog's light learned from the game's own (A8F5202D's target over what its opacity hides, a 32 x 18 grid, `rt_fog_learn`, once a frame after that draw; the reflections one frame on read it from the sun texture's texels 9-13). Only over the scene's light before its fog (the row above, or a Light Bounce request that frame). Left out: the game's noise on the fog's height, its legacy forced fog, the fog's light by direction (one colour for the frame), the five other fog light shaders (none drew in any run: the row does nothing with them). Log: `First run: the fog's numbers and light for the reflections, after ps A8F5202D`.

Build: `cmake --build build --config Release --target renodx-twwarhammer3.addon64`; helper: `src\games\twattila\dlaa_helper\build.ps1 build\tw_dlaa_helper97`. Offline checks: `tools/tw/wh3_batch6/p3` (WARP: the fog's opacity against the game's bytes, the two new shaders), harness modes `reflect_no_bounce*` and `reflect_fog*` in all_modes.sh. In game: `tools/tw/wh3_batch6/run_requests.md` P3-1 to P3-5.

### Ray-traced shadow capture (WH3 batch 6 P2, 2026-11-10; not run in game; helper96 unchanged)

- **Black spot on a tent under ray-traced shadows (ATN's debug9):** the tent's cloth (8EE34EC5 + C998A3A2) is two meshes of one material, 13632 indices that the picture shows and 204 that the shadow pass (53C5A7EC) draws alone: the cloth's coarse stand-in for the shadow maps. The black shapes are that stand-in, where it lies over the sagging cloth: polygons, about its 68 triangles. 130 pixel shaders discard by their instance's fade (`floor(TEXCOORD6.w) * 0.001 - t_dither`); the rays' rows never read it. **No Shadows From Hidden Parts (Experimental, off)** leaves a placed instance out of the rays while its pixel shader shows it on less than half its pixels. Not seen: that the stand-in is drawn at fade 0 (no trace holds instance data). Log Details names the draws with instances left out (`faded rows:`).
- **Far Tree Shadows flicker (ATN's debug9):** the cards facing the sun were meant for the instance only the sun's rays meet, but the helper takes that from the tail of a frame's captures, and here a unit's draw and the terrain's capture always follow them: every card stood among the units, in the ambient's, lights' and paths' rays too (a dark wedge at each tree's foot, changing with the draw order). The captures are ordered now, sun-facing ones last. Shared code: the same for Attila's sprite soldiers.
- **Black trunks, next step:** a tree's vertices blend two sets of instance rows (v8-10, v11-13, by each vertex's v6.x), a placed mesh is one instance's blend. Log Details now says how far the second rows lie from the first, per draw (`second rows:`): alike across a draw's instances means one shape fits, apart means each tree needs its own. **Tree Trunks as Drawn (Experimental, off)** captures the trunks every frame from the game's own vertex shader, as Placed Objects off did for everything (no black trunks there): each trunk's own shape, at that cost.
- **Cut-outs:** 66 more pixel shaders of the game's files cut by their base colour's alpha at 0.5 beside the fade (the table's script took one discard a shader); no frame trace shows them drawn, so they wait behind **Cut-outs on More Models (Experimental, off)**. 8 more read the alpha through a branch or change it first: left solid. A Frame Trace prints fx_rigidtextureblend's constants (1405BFB4: 8 layer weights a draw), which its cut-out needs.

### Global Illumination Off in a battle: the crash (WH3 batch 6 P1, 2026-11-10; not run in game)

- **v37 crashed the game when Global Illumination went from on to Off in a battle** (it looked like a hang: the game's own crash handler was compressing its report). Cause, from the game's crash dump and its code: v37 set the engine value gi_enabled to 0 while ours was on and back to the game's 1 when ours went off. The game's GI stage makes its textures only while that value is not 0 and runs whenever it is not 0, so it ran without textures (a read of null + 0x18 at Warhammer3.exe+0x1A45803).
- **gi_enabled is never written now.** The game's GI stays out of the picture through its flag in the lighting constants alone (g_gi_enabled 0 in every upload, as in v37). Its six GI compute passes run again while ours is on (about 0.22 ms GPU in batch 5's siege measurement).
- The lighting buffers are known from their creation (480-byte constant buffers), not looked up at every map, and a map's pointer is written through only by the thread that mapped it, before that unmap: an unmap from another thread, a destroyed buffer or a map never unmapped drops the pointer. Test: `test/tw_game_gi/run.ps1` (WARP, x86 and amd64).
- Test in game: a battle with Light Bounce on (Path Tracing), then Off, then on again, twice; the game goes on. Log: "the game's Global Illumination flag off" once.

## v37 (2026-11-06, ATN: only our AO and GI; helper89 unchanged; nothing here run in game)

- **The game's SSAO isn't computed while our AO feeds it (no row, default):** under RTAO and Path-Traced AO its three passes (depth downsample, sweep, collect) go, the result cleared to open; the sun reads the rays' AO alone. In 94 traces the sun draws are its only reader: the reflections' resolve binds a 1x1 white texture there, water and fog never bind it (v36's "reflections lose the game's AO" was wrong). Skip the Game's SSAO under RTAO removed. WARP on the game's bytes: the 6 sun shaders with it read exactly the rays' AO, 0 values off.
- **Path-Traced AO now has the rays' AO in the sun's AO slot** (RTAO from the same trace): the cube's reflections and what the paths leave are occluded by ours, not by an open slot. Costs RTAO's rays.
- **GT-VBAO: the game's sweep is skipped** (its line buffer isn't read by GT-VBAO); the first frame, before a camera is kept, reads open.
- **Game's AO method: the game's bytes, unchanged.**
- **The game's Global Illumination is off while ours is on (Light Bounce, any method; default):** its engine value gi_enabled 0, and its flag g_gi_enabled 0 in every upload of its lighting constants (all 1019 shaders that read it), so nothing reads probes that stopped. Ours off: the game's. WARP on the game's bytes, 26 traced readers: flag 0, probes changed, 0 values differ. Game's Global Illumination row (Graphics 2 of 2) removed.
- Test: RTAO and Path-Traced AO, paused: armour and water as before, no halos from the game's AO; GT-VBAO unchanged; Light Bounce on and off: indoor and under trees change, no frozen light when the camera moves far. Log: "the game's Global Illumination flag off" once.

## v36 (batch 5, ATN's test of v35; renodx-tw-dlaa64.exe helper89, bridge v29: ship them together; one replay run paused, R1, nothing here tested by play)

- **Blocky RT shadows on banners (ATN's debug8):** the dark patches on a unit banner are whole triangles of its mesh. The game lights a banner from both sides by its smooth normal (989BED69 reads SV_IsFrontFace); the rays decided by each triangle whether it faces the sun, and traced a turned-away one against the banner itself, so a waving banner at a low sun went dark triangle by triangle. Row Smooth Banner Shadows (Experimental, off, More Options of the rays): cloth and sails trace from the camera's side through themselves, their own triangles within 3 m passed; other things' shadows on them and their shadows on the ground stay (harness: a folded sheet 67 % dark off, 0 % on, a solid card's shadow on it 97 %). Also two unit pixel shaders cut out by their base colour's alpha in the rays as drawn (AF697DAE, F2BC0D72: the table's script skipped them for their second, dither discard). Not run in game.
- **Ray-traced reflections without Path-Traced GI (ATN's broken water):** with One Bounce or GI off the rays' hits read last frame's picture, which the upscaler got as its square root under Ray Reconstruction, L and M: half the contrast, a whitish grey (harness: a red wall of 2.00 / 0.10 came back 1.41 / 0.32). It is squared back now. One Bounce also learns its light and fills the radiance cache in its own request, before the water's surface replaces the scene's, as path tracing did: rays from the water read the same light in both modes (harness: what the screen never saw 0.06 before, 0.67 now, 0.44 with two path bounces). GI off still reads last frame's picture, and below 100% that is after the tone mapper. Water Reflections' tooltip: best with ray-traced GI. Not run in game: the river of ATN's shots, One Bounce against ReSTIR GI.
- **Battle water and rivers below 100% render scale (no row):** the game's water picks between two refractions by a depth test whose position it rebuilt from the screen texture's share, not the view's, so below 100% (the view in the top-left of the game's textures) the test was made at the wrong place along the shore. Vanilla does the same at its own resolution scale. Below 100% the six battle water and river shaders now run a copy that takes the view's share; at 100% the game's own shader runs as before. WARP, the game's bytes, the view at 50% against the same view at 100%: values off by more than 0.01 from 6868-21822 down to 0-244 per shader, at 100% the copy's bits equal the game's. Not run in game.
- **The helper's Windows crash report at each start is gone:** it was never the helper's code. ReShade next to renodx-tw-dlaa64.exe loads this addon into it too; the addon said no in DllMain, and after that no the runtime's cleanup faulted inside the loader (ntdll c0000005 at 0x272c5, then ReShade's error 1114 in ReShade.log1). The process lived on. Now the addon says no in AddonInit, so ReShade just unloads it. Real helper faults now leave one line in its log and renodx-tw-dlaa64.dmp.
- **Freeze after a GPU reset: named next time, not fixed.** The log now says whose device was removed and why (0x887A0006 hung, its own work; 0x887A0007 reset, another's), the last frame's age, whether the game's window was in front, each time the window comes to or leaves the front, and after 20 s without a frame where the frame thread stands. The helper logs its five devices the same way, also when no request comes.
- **The terrain's count can't hold the game's thread any more:** one Flush, then at most 250 ms; past it one log line and that frame's rays go without the terrain. The cost line says how much of "tracing" this wait is.
- **Black trunks in AO, shadows and Path-Traced AO (ATN's dark patch):** one mesh is placed at every place from its first instance, so a trunk the game draws can stand inside the placed shape, and every ray from it hits. No Black Patches on Trunks (Experimental, off, Graphics 1 of 2, Shadows, More Options): rays start past the solid shapes between the pixel and the camera, 1.5 m at most. Why shapes differ by instance isn't proven; seen on the replay's pines (with Placed Objects off: none). The row itself not run in game.
- **Tree Trunk Cut-outs (Experimental, off):** fx_rigidbranch_v3 discards at its base colour's alpha 0.5, the rays had it solid. Ran on the replay: trace ms unchanged (3.95), 44 meshes baked (1.6 MB). It does not remove the black trunks.
- **Path-Traced AO too dark until toggled:** what it learned came from the battle's first 30 frames, 30 % darker than the same paused view learned 60 s later (the helper logs it at each start). It now waits 150 frames. Steady Bounce Light (Experimental, off): that light from the game's cube, of no view. On the replay's open view neither changes the picture by a code: ATN compares in a forest. 150 frames not run in game.
- **AO under Ray Reconstruction (Ray Tracing Extra Options, with RR on and RTAO or Path-Traced AO):** Light History (default) or REBLUR. Light History is what Attila had up to v135: raw rays plus 20% of the last frames, Ray Reconstruction cleans the rest, and it undoes the REBLUR that v34 / v138 forced. REBLUR: steadier when moving, about 0.85 ms and 215 MB more at 4K, Path-Traced AO darker at a wall's foot (0.74 to 0.57 on the harness's scene). Harness, same numbers as helper78 and helper79: RTAO 2 rays, RR without history, wall foot 0.00554 Light History, 0.00226 REBLUR. Not run in game.
- **Ray Reconstruction's specular albedo has its Fresnel now (no row):** it was the bare F0; NVIDIA's guide (4.2.1) wants it pre-integrated at this view and roughness. Metal and shiny surfaces at a glancing angle tell RR a stronger reflection than before, rough ones a weaker one (0.39 to 17 times the bare value on a made G-buffer). Water and sky unchanged, the diffuse albedo keeps the bare F0. WARP against the guide's function and a worked-out NdotV. Not run in game: look at armour under RR.
- **Skip the Game's SSAO under RTAO (still Experimental, off; RTAO only again):** siege benchmark, 4K, ATN's ray settings: the AO pass 0.51 to 0.09 ms of GPU (Time Each Pass), no reset in 2 runs with it on. Not the same picture: the game's reflections multiply by that AO too and lose it. Under Path-Traced AO nothing takes the AO's place in the sun's slot, so its cube reflections and GI lost theirs: the row no longer acts there. Test: RTAO, row on and off, paused, armour and water.
- **Skip Trees in Shadow Maps (still Experimental, off):** trees out of view keep their ray-traced shadows with it on now (their capture ran after the draw this row skips: it was dropped). Siege benchmark: shadow maps 1.01 to 0.60 ms of GPU. Off by default: fog still loses the trees' shadows.
- **Time Each Pass (One Frame), Ray Tracing Extra Options > More Options:** the GPU's time of every draw and dispatch of the next frame into wh3_pass_timing.txt next to the exe, by pass and by shader. A CPU-limited frame's waits count too. Run in game (siege benchmark, 4 runs).
- **Particle Trails Under Upscaling (still Experimental, off):** the moving blocks ATN saw while paused came from this row. A particle texel is 2 render pixels, the jitter flips whole texels at every object's edge, and any flip dropped the history. Now only a change outside the texels around last frame's place does. Moving 2 px a frame or less keeps its trail; a one-texel blade coming into coverage still flags. WARP only. Not run in game.

## v35 (2026-11-05, ATN asked for Ray Reconstruction's unused inputs; renodx-tw-dlaa64.exe helper82, bridge v25: ship them together; nothing here run in game)

Graphics 1 of 2, Upscaling's More Options, with Ray Reconstruction on. All off, at 100% render scale only (below it RR's picture comes after the tone mapper, in another space). None of them helps AO: RR has no AO or shadow input.
- **Particles Skip the Denoiser (Experimental):** the particles' composite (43606BB8) goes to a layer of its own, the colour stays without them, RR only upscales the layer. For smoke and fire that smear. They lose depth of field's blur. Harness: output = layer + (1 - alpha) x colour, 0.4016 for 0.4 over 0.0022.
- **Picture Before Particles (Experimental):** RR's guide against ghosting and vanishing particles: the colour copied before the composite, in the colour input's format and square root. Does nothing with the row above. With depth of field on, RR also sees the blur as a difference: use the guide below. Harness: read; the rest of the picture 2.5% brighter.
- **Depth of Field Guide (Experimental):** the game's depth of field runs before RR's point (fx_depthoffield, then its TAA). Needs RR preset E: other presets ignored it in the harness.
- Left out: Particle mask (no preset read it, output identical to 5 digits). Diffuse hit distance (RTAO keeps no distance, the Light Bounce's exists only under REBLUR and at its smaller size). Disocclusion mask (no format known, no previous depth kept). Subsurface guide (the game has no such blur).

## v34 (batch 4, ATN's test of v33; renodx-tw-dlaa64.exe helper81, bridge v24: ship them together; nothing here run in game)

- **Shadow Casters Out of View (was Units Out of View), not run in game:** on with rays' sun shadows, Rays Replace or not. Everything the game's shadow pass draws casts: trees, buildings, rocks, shrubs, props, flags, units. Far billboard trees stay under their own row, off. Placed Objects keep what they've seen until the battle ends, within 6 M triangles and 12288 meshes, not 300 frames; things seen moving 300 frames. Limit: what the camera never saw and the cascades don't reach (log: Cascades' split distances).
- Ray Reconstruction: RTAO and Path-Traced AO go through NVIDIA's REBLUR first, the AO the NRD setting gives. RR only saw AO multiplied into the colour and is no AO denoiser. about 0.85 ms and 215 MB more at 4K. Harness numbers: Attila's v138. Not run in game.
- **Pixel trace (Ray Tracing Extra Options, More Options), not run in game:** the row from Attila. wh3_pixel_trace.txt next to the exe: every pass that changes the pixels under the red line, then the rays' AO at the cross, which no pass shows: AO around the pixel, what the sun draws read, view depth, world position, camera, the normal the rays start from. Below 100% the position follows each draw's viewport. Not logged: where the rays hit (AO 0 = all of them hit within the range); that needs the helper. Put the cross on a black spot and send the file.
- **Kept casters, review's fixes, not run in game:** row off (or AO alone) is v33's rule again. A solid instance placed lately with one of its mesh now placed within 1-2 m is taken as moved: no trail from a mover the game doesn't mark (harness: gone up to 1.5 m a frame, 3 m a frame still leaves one; a leaves' mesh is never tested). An instance both passes drew is the main pass's, every frame: no flicker at the view's edge. Over budget, kept at least two placing turns.
- Known: an out-of-view mesh's two levels of detail (main pass's, shadow pass's) can both stay until the scene ends: more instances, a slightly wider shadow.
- **Spot Filter removed:** ATN: looks broken.
- **Grass Fade Distance removed:** near the old fade distance blades grew and flickered. Flicker: the copy flattened this frame's blade by the row, last frame's by the game's distance, so motion said the blade rose. Growth: generated grass ends 16 cells around the camera and shrinks to 0 in the last cell, which the game hides by flattening first, and no grass exists past that whatever the fade. Longer grass needs another dispatch of the grass generator, not a fade.

## v33 (2026-11-05, batch 4; renodx-tw-dlaa64.exe helper78, bridge v23: ship them together; nothing here run in game; no players' zip)

- **Light Bounce under spherical harmonics, not run in game:** its paths keep the learned shade; the harmonics' sky only with Path-Traced AO on.
- **Units Out of View's cost, not measured:** every unit's shadow pass draw is captured again under Replace. Row off if the siege pulses or slows.
- **VFX section, not run in game:** brightness of Fire & Glow, Smoke & Dust, Spell Meshes, Fog Particles (1: the game's shaders). Particles at Half Resolution (Experimental, off): all sprites in the game's half-size layer. Particle Trails Under Upscaling (Experimental, off): history dropped where particles changed.
- **Water Clarity and Water Caustics reach battle water and rivers, not run in game:** the shaders in every trace (battle_water, battle_river, polluted too). Clarity scales their absorption; Caustics multiply their bed. Both rows off, the game's own shader runs. The campaign's sea has no depth in its colour: it stays. Test: a river, Clarity 4, Caustics 1, at once on the row.
- **Reflections as Rough as the Game's (Experimental, off; needs helper78), not run in game:** the rays' lobe came from Attila (alpha 0.0064 / gloss^2, under 0.11 at any roughness: near mirrors). WH3's own light and resolve are alpha = roughness^2 (k = roughness^2 / 2 in 9EAD8951 and the resolve). Test: armour close up, on and off.
- **Skip the Game's SSAO under RTAO (Experimental, off), not run in game:** the game's two AO dispatches go, its result cleared to open. Test: RTAO on, row on and off, paused: same picture, a little faster. Only while rays run: with the helper down the game's AO stays.
- **After a GPU reset, not run in game:** the game no longer hangs making its new swap chain (ClearState + Flush of the lost device; its blt fallback left as it asked; no frame for 90 s closes it). Test WARP swap_loss.
- **Texture LOD Bias, Effects at Output no longer greyed, not run in game.**
- **Reflections below 100%, not run in game:** the game's mip chains stop at the picture, under Effects at Output; reflections reach the right and bottom. Test WARP ssr_mips.
- **Reflections' depth levels below 100%, not run in game:** the march's hierarchical depth stops at the picture too (its edge cells took depths from the unused part). Test WARP scale_copies.
- **GI and Shadow Blur at Scale (Experimental, off), not run in game:** both read the depth as if the picture filled its texture. WARP scale_copies: at 50% and 33% both match the game at 100% (shadow blur: all but its right and bottom edge), the game's own bytes don't. Water has the same line: not fixed.
- **Rays Replace Shadow Maps on by default, Units Out of View (Experimental, on), not run in game:** the game's main pass culls a unit behind the camera, its shadow pass still draws it. Under Replace, `fx_weighted`'s shadow VS (BADA4FE3, 46E626D5) go into the ray scene, streamed out with the main view's matrix through `rt_outside_view`, which collapses triangles in the view (the main pass has those). Solid, no weapons (rigid `6EAC8CE5` also draws all buildings: 20 M indices in the siege). WARP `warp_tests/outside_view_wh3`.
- **Skip Trees in Shadow Maps (Experimental, off), not run in game:** under Replace, 4 tree VS skip the cascades (52 M indices on the replay, 36 M in the siege); fog loses their shadows. The cascades themselves stay: fog, forward passes and the unit capture read them.
- **Light Through All Banners (Experimental, off), not run in game:** 444F1190 banners are thin (35% through), C0ED2883's aren't, and C0ED2883 draws other unit parts too, so a row.
- **Path-Traced AO is back (Experimental, off, not run in game):** with spherical harmonics on the paths' sky is now those harmonics at the camera's place, along six sides (same terms as the game's sun shader, WARP); with them off the game's cube.

## v32 (2026-11-05, batch 3; renodx-tw-dlaa64.exe helper77, bridge v23; run in game by replay, campaign, benchmarks: see each line; no players' zip yet)

- **Quit at a mode: the game's file keeps its own scale, run in game** (quit at Performance: file 1; see Upscaling Mode). A patch that moves the code: the row hides, no mode runs, and the file was never touched.
- **No upscaler, no scale written:** with the helper failed, `gfx_aa` below TAA or `gfx_dlss` above 0 in the game's file (read at the start), the row writes nothing, and what it wrote goes back. `gfx_dlss` run; the other two read.
- Saved values from before v32 of two rows that were hidden, Torch and Fire Light in Fog and Water Reflections, go back to off once (`SettingsRevision` in ReShade.ini).
- **Water Reflections (Experimental, off), run in game** (replay paused, 4K, 100% and Mode P, with and without Ray Reconstruction): with Ray-Traced Reflections water keeps the game's reflections; armour, metal and wet ground take the rays. Rays on water showed far hills as dark patches: their hits have no fog. Row on: the old look. The rays still leave from the water, for Ray Reconstruction's guides.
- **DLSS Preset's tooltip:** L comes out darker below 100% (replay, mode P against DLAA, same paused moment: L 0.904, worst cell 0.81; M 0.976; K 0.993). Not cured; K or M at half.
- **Reflections, All Surfaces (Experimental, off), run in game** (replay paused and live, 4K, DLSS at 100%): the Method slider's middle place: the game's own march, closed to water here since WH2, on armour, metal, stone and wood too, no rays needed. GPU busy 17.2 > 17.9 ms on the hill view, a close view -12% in its darkest cell (rough wood); paused shots steady (0.006 between two). Its copy, water and all else bit for bit off (WARP). Armour's look and shimmer in motion: your eye.
- **Water's reflection at half scale or below, run in game** (replay paused, Mode P, row Effects at Output): the march, its depth and blurs one pixel to a render pixel: at 50% the game marches a quarter of the display, the tree's reflection blurred away. Now as sharp as at 100%, edge energy of the river +5.6%. Above half the targets don't hold it: the game's. With the row off, the game's.
- **Ray-Traced Reflections on every sun shader:** the rule that closed the pass for 3 of 7 is gone: skin writes flags 0, so the resolve leaves those pixels alone; 76A707E5 has its own rule (F0 0.04, no SSAO, off water only): WARP 0 misses, water too, and the control misses. Those 3 drew in no run.
- **Fog Strength on all the fog's shaders:** C506E37C, 4347C118, 7998DC13 (full-resolution light) and legacy C5B0D1F3 (Darklands) swapped in only off 1. WARP: 0 bits at 1 (legacy 15 of 393216 by 4.8e-07, and off 1 only), right at 0 and 2. They drew in no run. `gfx_fog 0` needed nothing (438D86B8 goes through 78E4CBF2, run in game: 0 and 2 act as on High).
- **The campaign map in the scene, run in game** (Eataine's save at 4K, three heights, 100% and 50%): its ground tiles, trees and bushes in the rays, a terrain triangle over a hole in the height map left out, the rays' ranges in the map's units (a metre = 0.1 there). Black against rays off 22% > 3.2% on the first view, 0.27% from high up; what's left is under trees and on rock faces turned from the sun. Cost there: scene 0.67, trace 3.6 ms. Trees are half lit, half dark with rays; the game's own without screen-space shadows are lit, and Screen-Space Shadows alone darkens them 4.6 times.
- **Hills Out of View (Experimental, off), run in game** (replay, paused): the terrain out of the view in the scene too: terrain 0.64 > 0.76 M triangles, trace 4.40 > 5.10 ms. No cell of AO changed past the noise on the one low view tried.
- **Far Tree Shadows (row, off), run in game** (replay, paused): far trees cast sun rays, the card turned to the sun. +0.06 M triangles; no view tried showed a change. Attila's way, by WH3's bytes.
- **RTAO in place of the game's SSAO, run in game** (replay paused, 1080p): always, no row: a saved value is ignored. Against rays off: view +3.3%, largest cell +9.5%; on top of the SSAO, as before: -0.1%. The game's pass still runs: water and fog read it.
- **Torch and Fire Shadows on in RT/PT, run in game** (siege, skaven benchmarks, replay; 1080p, day or dusk; 150-odd lights read, 32 traced, Debug View not magenta): trace 0.93 to 1.07 ms with sun and AO. **Night and fire's look: not judged by anyone.** With them the paths get the torches' light on what the screen never saw.
- **Rays Replace Shadow Maps, earlier state, run in game** (replay paused, stepped pan): the sun's mask with lit cascades and no contact march; smoke, clouds, the terrain's map stay. Off: the game's bytes. Softness shows: shadow's floor 10.7, 14.2, 15.6 nits at 0.1, 0.5, 2 on one edge. Against the game's maps underneath: view +0.6 to 1.1%. **A flying dragon off screen lost its shadow** (one cell +14.8%, 1 of 1287): rays only have units in view, so it stays off. Campaign: not run (test PC's memory).
- **Fog Shadows on all 6 of the fog's light shaders**: A8F5202D and 438D86B8 (`gfx_fog 0`) run in game, the other 4 drawn in no run. **Torch and Fire Light in Fog (Experimental), a row at 0, not in RT/PT:** the formula tested on WARP (t44 = the sun's colour raised by the same light, 0 misses of 294912 on each); its look not judged by anyone. Saved values from older versions may be 1.
- **Black patches in ray tracing, fixed, run in game** (replay at 100% and 50%, campaign): the terrain's second pass was in the scene as a surface. It tessellates coarsely and only draws skirts; streamed out past its geometry shader, its coarse hills lay over the real ones, sun and AO rays alike. Replay's first view: 12.69% of the view black, 28 blobs; now 0, 0. Also out: `fx_rigidterrain` (boxes that paint terrain inside themselves: the campaign's rectangles) and what the terrain's mask discards (the campaign's sea).
- The terrain gets its room in the frame first (it joined last and went short at 100%); what doesn't fit is left out of other meshes, leaves first by draw order (Log Details names them).
- **Unit banners black with rays on, fixed, run in game** (replay, camera moved with rays on): meshes of `fx_rigidtextureblend` (VS C0ED2883) are placed from a buffer a capture's key doesn't hold, so they were kept as first captured: old copies through the banners, in every ray, strips of shadow on the hill. They are captured every frame now, as units are.
- **Cut-outs in the scene, run in game** (replay, Fallen Gates benchmark): fences, cards, beards and fur are cut out by their base colour map's alpha in rays, not solid (541 pixel shaders read from their bytes, 6 seen drawing, `cut_out_masks.hpp`). A pixel shader not listed stays solid. With Leaf Micromaps off, or the atlas's share full, they stay solid. In one paused run: pines' shadow cell 22% more light in the Sun view, picture within 1%, trace 5.10 > 5.20 ms.
- **Placed Objects, default On, run in game** (siege, replay, Fallen Gates; 5 min replay: memory flat; later 4K and campaign too: soak below): buildings, rocks, trees and soldiers' weapons enter the ray tracing once, then only their instances' rows (F0395CC4, 4F35D09F, 41AC9CB1, 33AF7AE5, 474F97FB, 783383C6 standing, kept off screen; 8EE34EC5, 2C45437C moving, rows every frame). RT/PT, replay at 100%: 52.0 to 38.9 ms; at 50%: 32.7 to 22.0 ms; vertices a frame 25 to 5 M. Off: captured every frame as before.
- **The siege's GPU reset is fixed** (nvlddmkm 153, the game's device hung, with any ray effect, 14 of 47 runs): one Flush before the terrain's stream output, which shared a batch with the tree simulation. 0 of 38 with it, RT/PT, Ray Reconstruction and frame generation too, at 1080p with low textures; 4K Sun + AO 0 of 14; **RT/PT at 4K: 3 full runs, 2 more with Ray Reconstruction and frame generation, 0 resets**; 20 min at Mode P with frame generation, 10 min at 100%, 10 min campaign: memory flat (the replay ends at 6 min, the rest is the results panel's still scene). Why the GPU stopped in that batch isn't known. Every reset leaves its process's memory until a reboot (Windows counted 109 GB after 60 resets): if rays fail with no memory after one, restart the PC.
- **Buildings, tents, shrubs, mountains, rocks in the scene, run in game** (replay, siege benchmark, campaign): the vertex shader most stone and wood draw with (4F35D09F) also draws water, decals and effects, so only its 146 pixel shaders of solid materials count (named in `building_masks.hpp`, none reads the depth or the G-buffer). Rocks 41AC9CB1, 33AF7AE5, campaign mountains 70F4C9F9, simulated trees 31584506, 9498C59D. Shrubs cut out. Replay: 12.9 to 15.2 M triangles, GPU scene +0.3 ms, trace +0.3 ms; the picture moves up to 7% in a cell where a wall now shades ray-traced light.
- **The grass fields the game culls on the GPU are in the scene, run in game** (battle benchmark, siege benchmark): 6 to 8 M vertices of grass drawn by indirect draws, left out until now. Their numbers are read from the GPU only while a draw was not empty lately, so a map without such fields pays nothing (replay paused: 42.67 ms with or without the check; reading every frame cost 1.2 ms). Trace about as before (4.2-4.7 ms on the battle benchmark).
- Rows added, in no trace so far, active only if a map draws them: the units' other vertex shaders (burn, wind alpha, nurgle), rigid wind planes as cloth. Their masks make them solid to the sun, so they don't tint Coloured Shadows.
- **Ambient occlusion below 100%, fixed, run in game** (replay and campaign, started at 50%, against 100%, one view each): GT-VBAO's depth now comes from the game's sweep, as the downsample it read stops below about 2/3 scale (a frozen depth); its settings reach it at b10 (it had them by luck at 100%, and read radius 0 at 50%: flat white). The AO filter and Ray-Traced AO read the game's AO where its sun draws read it, and the sun reads their textures one to one: the filter darkened the right and bottom third (cells 0.76, now 1.0 to 1.1), RTAO lay 1.5x off. Game's SSAO was right at 50% already: Debug View 8 correlates 0.986 with 100%.
- **Debug View's Screen-Space AO** works in WH3: what the sun draws read, the game's under RTAO. Troy, Pharaoh, Warhammer I and II: still magenta, nothing changed there.
- **Object Motion Vectors, default On, run in game** (replay paused and live, siege, Morathi's, skaven benchmarks, campaign): what the game leaves out of its velocity now tells the upscaler how it moves: glowing, dissolving and iridescent weapons and props on moving units, and whatever sways by time on a zero previous position (capture flags, wind planes, tentacles, baked animation, VFX meshes). The draw itself writes it, as the game's own moving meshes do: no redraw, no table. Row Off: the game's bytes. Paused against Off: 0.004 of an 8-bit step over the view. Cost: 21.55 to 21.64 ms (0.4%).
  - Unit banners here don't sway (a test with time pushed far gave them no motion, a small flag beside them did), and they keep no last position: a marching unit's banner still has only the camera's motion. Not done: bones or rows with sway (banners by wind, liquid): drawn in no trace.
  - **Water Motion (Experimental, off):** the fill's camera motion at the water's depth, not the riverbed's: traces show the water writing the depth after the fill's copy of it. No pan reached water on screen in 3 tries: not seen, only that nothing breaks with it on in 7 runs.
- **Upscaling Mode, live, run in game** (replay, by the test hook; FSR, XeSS too): Game's (default), DLAA, Ultra Quality, Quality, Balanced, Performance, Ultra Performance, Custom. The row writes the one float the menu's slider sets, found by the code that uses it, no exe patch; found nothing, the row hides. Sizes are whole in both of the game's roundings (2960x1665, 2560x1440, 2224x1251, 1920x1080, 1280x720 at 4K; Custom in steps of the display's aspect). One new size: one frame of the game's TAA, its first in a session; a size used before: none, and frame generation stays up either way. Custom 1 to 100%: whole picture down to 1% (32x18), no refusal. The game's slider loses while a mode is on.
  - **Quit at a mode:** the game copies the scale to its preference before its device goes and writes the file a second after. The addon puts the old value (`UpscalingGameScale` in ReShade.ini) in that preference at `destroy_device`, by the method the game's load path calls, only if it agreed with the live float before any write. Run: quit at Performance, file `gfx_resolution_scale 1`.
  - **The Below 100% row is gone:** an upscaler on means it also runs below 100%. A size the constants don't agree with is still refused.
  - **Texture LOD Bias** (default Sharper): samplers cloned in the main view (viewport at the render size, before the tone mapper; a trace shows shadow maps out). The game's own samplers have fixed biases (0, -0.5, -2) that don't follow the scale. Edge energy at 50 against 100: Off 0.78 to 0.96, Matched 0.90 to 0.97, Sharper 0.99 both views, Sharpest 1.00 to 1.07.
  - **Effects at Output Resolution** shown and honoured: Light Bounce, Bend's softness, RTAO's rate follow it. On against off, rays off, one paused view: under the noise.
  - Status line says the sizes. `gfx_dlss` above 0 in the game's file: status and log say so, the row writes nothing (run).
- **Below 100%, run in game** (replay and campaign, started at 50%, against 100%, one view each):
  - **Models as at 100%:** the game picks LODs and culls by the render's pixels: 50.2 M indices in the G-buffer at 100%, 28.0 M at 50%. `Full Detail Below 100%` (on) puts them back from the next load: 50.19 M at 50%, campaign 17.72 M both. Cost at 50%, ATN's x8 sliders, one view: rays off 11.84 to 13.55 ms, RT/PT 25.85 to 31.95 ms. Off: as before. A scale changed inside a battle counts from the next load.
  - **Sharpening:** the game adds up to 1 to its strength below 100% for its own upscale (0.225 + 0.75 at 50%: read in game). That part is off behind the upscaler. Edge energy 50 against 100: 2.04 to 2.14 before, 0.80 to 0.84 now. Game's TAA keeps it.
  - **Heat haze** the same size on screen as at 100%, **secondary LUT's distance** right: WARP on the shader's lines (`warp_tests/t00_scale`). Haze's look at scale: not run by eye.
  - Screen-Space Shadows' Debug View goes through the upscaler like the rest (it showed the corner): correlates 0.976 with 100%.
  - G-buffer guides and Depth Levels only over the render's corner.
  - Jitter: read in game: 16 phases, reach 0.43 render pixels at both scales. Amplitude right. DLSS would like 32 at 50%: not changed.
- Screen-Space Shadows keep their textures for 600 frames after they turn off (Ray-Traced Shadows on and off made a 4K copy each time). Attila too; not run in game there.
- `rt_terrain_keep_gs.h` is built by hand: the command is in the `.hlsl`.
- Log only, run in game: the helper's idle release no longer says it "started again"; Reflex's line says when frame generation paces the
  game; FSR Frame Generation's status has a real count (2.0 here); no "smoke shadows" in the status; Below 100%'s line printed once the size
  holds (one line per drag: not proven, no script can drag); "without room" every 10 s under Log Details, with the numbers.
- The page's presets leave Log Details alone.
- Verify runs: `tools/tw/wh3_batch3/facts.md`. Start form: `game_startup_mode replay "<full path>.replay"`; a bare name crashes the game.

## v31 (not released, built only, not run in game; renodx-tw-dlaa64.exe helper75, bridge v23)

- **Still computed twice, known:** the game's TAA runs and is thrown away under any upscaler (its other outputs are read later). The sun's cascades and their mask
  still render under RT shadows; rays multiply them (fog reads them too). RTAO replaces the game's SSAO; under Path-Traced AO it feeds only the specular. With
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
  untouched. Needs in-game Screen Space Reflections on. FidelityFX SSSR isn't offered: the game's march already is a hierarchical one.
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
  what's in view (Hills Out of View adds the rest): a hill behind the camera casts no ray-traced shadow; the game's terrain shadow stays. With rays on the terrain's geometry runs twice a frame and the trace waits for its triangle count.
- **Game's Global Illumination (Experimental, Effects tests, default on):** the game's own bounce light, which Light Bounce comes on top of. Whether it
  turns off at once, next battle or after a restart isn't known.
- Log Details writes once per sun shader whether spherical harmonics and the game's Global Illumination are on, the ambient cube and its scale.
- **Below 100% Resolution Scale (Experimental, off):** DLSS, FSR or XeSS also when the in-game Resolution Scale is below 100%. The game's
  own chain stays at the smaller size; the tone mapper's picture goes through the upscaler up to your resolution; flares, vignette, sharpening
  and UI at your resolution. Off, and at 100% either way, nothing changes. The first frame at each size only checks the game's constants; if
  they aren't as expected the status line says so, the values are in `ReShade.log`, and the game's TAA and upscale stay. Bloom now sees
  the unresolved picture, and particles go through the upscaler: ATN's test decides. No LOD bias here then; v32 has it.
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
  scale), SMAA, CMAA2, own TAA, Reflections and Water, smoke, Path-Traced AO; ray-traced AO replaces the game's, its method sets the
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
  frame's jittered colour (t4), depth (t0's x), the velocity buffer its TAA uses (t2: units, grass and leaves move in it; flags and glowing weapons
  by Object Motion Vectors, v32) and its jitter (camera g_camera_jitter, the projection's own); they go
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


### F1 helper lifetime repair (2026-11-09, GPT-6 Astra)

AddonUninit now shuts down the shared helper bridge after unregistering its users. The watcher exits before module unload; child process and IPC ownership are released. Shutdown drains outstanding GPU fence generations, with ordered CPU fence recovery only after confirmed helper exit. Failed launches clean up partial ownership and existing IPC sessions are not overwritten.

Offline checks: x86/x64 production-header tests cover process/thread cleanup, failure injection, 20 reloads with stable handle counts, and three interleaved D3D11/D3D12 WARP fence generations under normal completion and forced shutdown. No game was launched. Manually check helper-enabled unload/reload and device recreation, then repeat the reported battle. The original battle freeze is not proven fixed; unexpected mid-run helper failures remain audit F7.


### F4/F8 sampler ownership (2026-11-09, GPT-6 Astra)
Sampler slots now own original COM references and are scoped to each device. References replaced in slot callbacks are released after unlocking. Duplicate sampler-init notifications preserve existing owned clones; teardown restores originals and clears each device cache. Final-release callbacks no longer acquire slot locks, and nested addon-callback suppression is preserved. WARP tests pass x86/x64 for originals surviving game release, clone identity preservation, two-device isolation, tracking restoration, per-device cleanup and shutdown. All 14 shared consumers built. No game run.
