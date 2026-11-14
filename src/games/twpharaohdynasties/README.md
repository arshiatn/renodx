# Total War: PHARAOH DYNASTIES (RenoDX HDR, ATN)

## To test in game (the maintainer; open items, newest first)

Send `ReShade.log`, `renodx-tw-dlaa64.log` and `ReShade.ini` after each.

1. Control: Reset, siege benchmark: looks like v55.
2. Ray-Traced Water Reflections (the game's Screen Space Reflections on, MSAA off) on and off: the river (siege) and the sea
   (battle): flicker, brightness against the game's, sun glints.
3. Specular Occlusion on and off on the water.
4. Smoke Shadows on and off in the burning siege: smoke shadows the ground; fire glow and dust don't.
5. RT/PT turns Water Reflections and Specular Occlusion on: keep that or leave them off? Smoke Shadows stays off.
6. Coloured Shadows (Bend) under cloth: tinted, not bent.
7. GT-VBAO at DLSS Ultra Performance, campaign and battle: AO over the whole view, no rectangle in the top left; blurred as
   at 100%.
8. Clouds in the campaign at Ultra Performance against Quality: they stay put and as sharp as the scene.
9. Campaign with Rays Replace Shadow Maps (or Skip the Shadow Maps): palms cast ray-traced shadows with holes between the leaves;
   settlements and cliffs cast.
10. Steady Torch Shadows on against off (helper106 installed with this build), a night siege, still camera: the torch shadows
    stop flickering; the cost from Record Ray Tracing Profile.
11. Water Clarity 4 against 1 and Water Caustics 1 against 0, a river battle and the campaign sea: clearer water, light
    ripples on battle river beds, none on the campaign.
12. Below 100% in the campaign and a battle: the sun's lighting, the fog, the shroud and the water's reflections stay in place
    at the screen's edges (35 shaders no longer scaled twice).
13. Path-Traced (ReSTIR GI) on, a sunny battle: the sun's bounce light under roofs and in shade, against v60 (v60's paths
    took it only from the screen).
14. Ray-Traced Water Reflections against the game's: sun glints and bright sky in the water no brighter than the game's own.
15. In-game effects quality Low / Medium (0-2) with Smoke Shadows on: the smoke shadows the ground as at Ultra.
16. A settlement with pools (phar_building_water_01) at DLSS Ultra Performance: the pools' AO in place, not squeezed to the
    top left.
17. DLSS Performance and Ultra Performance, the siege's river and a sea battle, Ray-Traced Water Reflections off and on: the
    water's reflections sit where they do at 100% (not stretched from the picture's top-left part), ripples bend them as much.
18. v64 (helper108): Ray Tracing Extra Options, More Options: Ray Tracing Debug View is there. Point Light Shadows shows the
    torch and fire shadows (grey lit, red blocked); Screen-Space AO the game's AO or GT-VBAO's; All the eight tiled.
19. v64, Ray Reconstruction selected, Pharaoh's folder as now (no nvngx_dlssd.dll): ReShade.log "Ray Reconstruction didn't
    start"; campaign torches at night, still camera: their shadows sharp and still, no crawling noise.
20. v64: Steady Torch Shadows on, the same place: no black dots around the torches (replaces line 10).
21. v64: campaign at Ultra Performance against Quality: the shroud's clouds over unexplored land sit in the same place.
22. v64: nvngx_dlssd.dll 310.9.16 copied next to nvngx_dlss.dll: renodx-tw-dlaa64.log "Ray Reconstruction ready", the torch
    shadows soft and calm.
23. v65 (helper109), the RT/PT preset, a battle: the same picture as v64, the helper's frame time no higher.
24. v66 (helper110), the RT/PT preset, a battle: the same picture as v65.
25. v67: the RT/PT preset (Torch and Fire Shadows on), Fire Shadows from Particle Lights off, a burning night siege: the same
    picture as v66.
26. v67: Fire Shadows from Particle Lights on (More Options, under Torch and Fire Shadows): the burning buildings' light
    stops at walls and units; the lit side as bright as before; no flicker as fires start and go out.
27. v67: with it on, ReShade.log "the particle lights streamed from their vertex shader" once, at the first fire.

### Round v67 (2026-11-14, built only, not run in game)

- **New (Experimental, off, not in the RT/PT preset): Fire Shadows from Particle Lights**, under Torch and Fire Shadows
  (More Options, shown while Torch and Fire Shadows is on). Burning buildings and other fires light the scene through the
  game's particle lights, which keep their lights in the particle list, not in the instances the torches use. With the row
  on, the add-on reads each fire's place, reach and colour from that list every frame (Troy's code, shared in
  `twattila/rt_scene.hpp`), so their light stops at walls and units as the torches' does.
- Off, Torch and Fire Shadows is as in v66: the fires' draws are left alone, and their shader gives the game's picture to the
  bit. WARP (`tools/tw/pharaoh/fires`, Troy's fires test on Pharaoh's bytes):
  - With nothing bound: the game's bits at 100%, and the bits of the game's shader as the add-on patches it today at 50%.
  - With the rays: the game's light where the ray reaches, none where it is blocked, bit for bit.
  - Each fire's place, reach and colour from 1,024 particles, to the bit.
  - Every control fails.
- Page dump against v66: the one new row; no other row or page state changes.

### Round v66 (2026-11-13, night merges; stamped and installed 22:59 with helper110, not run in game)

- Nothing changes in Pharaoh: helper110's faster sway counting and Attila's Grass Ray Tracing Range don't run here (the
  sway and grass range rows stay hidden). Page dump against v65: the hidden Grass Ray Tracing Range row only.
- Ships with helper110 (the helper of Attila v159 and WH3 v58); with helper109 it is the same.

### Round v65 (2026-11-13, CPU round; stamped and installed 21:04 with helper109, not run in game)

- **Faster** (Attila v158's): **the helper keeps the plant sway's GPU buffers** (the RT/PT preset, or Swaying Shadows
  Range above 0 m or Off-Screen Sway off): helper107-108 made and freed three of them (16 MiB) every frame, in ATN's GPU
  recording ~1.3 ms of the GPU's memory paging and ~2.4 ms of the helper's ray-tracing thread a frame. helper109 makes
  them once (harness: 3 new buffers a frame against 0, the same results).
- **Faster**: **the effects' camera copies** (AO, light bounce, screen-space shadows): each made a new D3D11 buffer at
  every call; now one is kept (WARP: 9.6 -> 4.1 us a call); **the rays' draw capture**: 27 -> 3 ns a captured draw
  (~0.03 ms a frame in an 8x battle).
- Nothing changes on screen. Needs helper109 for the first line; with helper108 the rest stays.

### Round v64 (2026-11-13, torch shadows and Debug Views; stamped and installed 19:55 with helper108 and nvngx_dlssd.dll 310.9.16 (Attila's copy; the folder had none), not run in game)

- **Fixed: torch shadows that crawled like noise with DLSS Ray Reconstruction selected.** Pharaoh's folder has no
  nvngx_dlssd.dll, so Ray Reconstruction never started (every helper log with the rays: "Ray Reconstruction unavailable")
  and the helper ran plain DLSS, while the rays were still made raw for it: soft torch shadows from a new random point
  each frame, NRD off, the AO and reflections unfiltered. Now the helper (helper108) tells the add-on, and the rays run as
  without Ray Reconstruction: sharp, steady torch shadows and NRD. ReShade.log: "Ray Reconstruction didn't start". The
  game's torches don't move (two frames of a campaign trace and a siege trace: the same positions). For Ray Reconstruction
  itself nvngx_dlssd.dll (310.9.16) goes next to nvngx_dlss.dll. Harness (`tools\tw\rt_harness\torch_v64.sh`, a torch on
  a pole, Ray Reconstruction asked for, no nvngx_dlssd.dll): helper107 102.8 torch bits changed a frame, helper108 0; with
  the DLL both run Ray Reconstruction as before.
- **Fixed: Steady Torch Shadows' black dots around torches.** Its soft shadows sent each pixel's ray to the same point of the
  light every frame: a fixed pattern of lit and dark pixels that nothing averages. Now Steady's torch shadows are sharp
  and steady. Harness: 30 dots that stay (helper107) against 0 (helper108), the same as sharp shadows. Shared: every TW game.
- **Fixed: the campaign's shroud below 100%.** Its decals (the clouds over unexplored land) placed themselves from a third of
  the view at Ultra Performance (their own target size, which the render scale doesn't change). The shared render-scale
  patch now knows that size too (`view_share::SetTargetSizes`, Pharaoh only). WARP (`tools\tw\warp_tests\pharaoh\decal_v64`,
  `patch_v64`): the 4 decal shaders at 33% as at 100% (largest 1.2e-7), the game's bytes there apart; every other shader of
  the 10 TW dumps patched as before.
- **New: Ray Tracing Debug View** (Ray Tracing Extra Options, More Options) and Ray Tracing Scene (Test) under it: they were
  on the page but never shown, Pharaoh's tone mapper never drew them. Point Light Shadows shows the torch and fire shadows;
  Screen-Space AO the AO the sun light reads (the game's or GT-VBAO's).
- Checked, nothing changed: the campaign's global terrain (026CBC61) isn't in the rays, so nothing is doubled under the
  terrain tiles (377DE0D5); every tile lies inside its chunks' bounds, so it stays out until a trace shows its mesh has holes
  there.
- **Not done:** the torch softness, the rays' 0.5 m clearance and the AO / Light Bounce ranges are in map units on the
  campaign (Pharaoh has no campaign scale for them); Troy, Rome II, Thrones and WH1-2 still don't draw the Debug View; run
  in game.

### Round v63 (2026-11-13, stamped and installed 18:38, helper107 unchanged, not run in game)

- **Fixed: rough water's reflections below 100% as soft as at 100%.** The game picks the reflections' blur level by the
  water's roughness in the reflection texture's texels, and below 100% that texture holds only the render's share of the
  texels the screen had at 100%: rough water blurred 2x wider at 50% and 3x at 33%. Our copy of the SSR apply
  (shaders/ssrapply00, drawn below 100%) now takes a level lower by log2 of the share (1 at 50%, 1.58 at 33%). At 100%
  nothing changes.
- **WARP** (`tools\tw\warp_tests\pharaoh\apply_share_v63`, `run_v63.txt`): 100% bit-identical to v62's build and the game's
  bytes, with and without the rays and a mip chain; v62's cases (one mip) bit-identical to v62 at 50% / 33%; rough water
  (level 2-5, each reflection level one colour, linear in its screen footprint): 50% bit-identical to the game's 100%, 33%
  inside the game's 100% at the level +- 1/256 (WARP keeps a few bits of the level's fraction); CONTROLs v62 at 50% / 33%
  apart in every value (largest 1.9 / 3.1).
- **Not done: run in game.** Built and checked on WARP only.

### Round v62 (2026-11-13, open items; stamped and installed 17:15, helper107 unchanged, not run in game)

- **Fixed: the water's reflections below 100%** (7b). The game's SSR apply (DE4F92CD) reads every texture at its quad's uv,
  which B61's trace shows is 0..1 over the viewport at any scale, while the code takes it for the whole target's: below 100%
  it read the mask, depth, normals and its reflections from the whole display-size textures (the top-left part stretched),
  and Ray-Traced Water Reflections at twice the pixel. Our copy (shaders/ssrapply00) now takes the uv x the draw's share,
  and the ripple distortion x the share; it draws below 100% with or without the rays. At 100% nothing changes (the game's
  own bytes without the rays).
- **Smoke just off screen: no change.** B61's sorted particles projected with the draw's camera: 78 on screen, none beside the
  view, 12 behind the camera (their shadows fall behind it too). A wider cull in the capture would add nothing there.
- **WARP** (`tools\tw\warp_tests\pharaoh\apply_share_v62`, `run_v62.txt`): 100% bit-identical to v61's build and to the
  game's bytes; 50% bit-identical to 100% with and without the rays; 33% 0 apart (the distortion's bilinear read listed: WARP's
  filter weights differ with the texture's size at the same coordinate, 0 apart with a point sampler); CONTROLs the game's
  bytes, v61's copy and ours without the share at 50%: 5,424-5,908 of 6,144 apart.
- **Not done: run in game.** Built and checked on WARP only.
- **Not done (listed):** the SSR colour's blur level (its mip, 16 x roughness) counts texels, so rough water blurs twice as
  much at 50% as at 100% (a look choice for ATN); the game's own contact shadows below 100% (7c, as in v61); smoke beside
  the view needs a trace with smoke just off the screen's edge.

### Round v61 (2026-11-13, open items; stamped and installed 16:27 with helper107 (unchanged), not run in game by ATN)

- **New: Path-Traced ambient gets the sun's light where no view reaches** (Attila's helper107 fix, now Pharaoh's): rt_sun
  writes the game's sun colour (lighting_VS_PS c3) to its texel 2, scaled as Pharaoh's sun shaders light the diffuse
  colour (x 0x389FB405, 7.615e-05: F883F5B2's literal in all 25), so the helper's RT_PATH_SUN_LIGHT turns on with
  Path-Traced. No helper change. The shared rt_sun takes the scale as a define; Attila's bytes are unchanged.
- **Fixed: Smoke Shadows at effects quality 0-2.** The lit smoke PS there (E8524BB1) keeps its diffuse, constants and
  sampler three / two slots lower than 3F1B86E1; the shared particle capture now shifts them per shader
  (`rt_core.hpp` SmokePixelShader). Its MSAA twin 740B2536 stays out (no rays under the game's MSAA).
- **Changed: Ray-Traced Water Reflections at most 25 a channel**, as the game's own apply (its RGBA8 march colour x 5,
  squared). No row: the game's look. Bit-identical below 25.
- **Fixed: the building pools' AO below 100%** (phar_building_water_01 03B9CEFE, 9D28319F, 40154AD7, AC04E623: t_ssao read at
  the view's uv; now render-scale reads).
- **Fixed: "no main camera" in the log every 5 s** with ray tracing on: the game's AO pass (D42C3523) binds no camera, so the
  rays no longer hook it (their AO goes in at the sun draws, as before). Troy the same.
- **Debug: `[renodx] FrameTraceReads=1`** in ReShade.ini (off by default; off: nothing hooked) adds to a frame trace the
  smoke's sorted particles and emitter rows, the point lights' instances (and the next frame's), the SSR apply quad's
  vertices and the campaign terrain's world bounds; the shroud's and the campaign terrain's constants are traced too.
- **WARP** (`tools\tw\warp_tests\pharaoh`, each `run_v61.txt`): `smoke_v61`, `ssr_v61`, `scale_v61`, `sun_scale_v61` (the
  sun shaders at 33% with the game's contact shadows off), `rt_sun_v61`, `trace_reads_v61` (x86 and amd64) all pass;
  `apply_scale_v61` lists the SSR apply's two cases (below).
- **Not done: run in game.** Built and checked on WARP only.
- **Not done: the SSR apply below 100% (7b).** DE4F92CD reads at its vb0 TEXCOORD0: right below 100% only if the quad's uv
  covers the render share (WARP: 0 apart then; 5,892-5,944 of 6,144 apart if it holds 0..1). FrameTraceReads' quad line
  settles it; the fix then: the target uv from SV_Position.
- **Not done: the game's own contact shadows below 100% (7c).** With the game's contact march on (only where neither Bend
  nor the rays shadow), 5 of 9 sun shaders read its depth at the whole texture's uv and step 6 / its size: 573-1,180 of
  6,144 apart at 33%. The rays' and Bend's presets turn that march off.
- **Not done:** 9DB92B1E (a smoke PS with baked shadows only) in the dump, not in Smoke Shadows; the shroud decals' c13 and
  026CBC61's overlap with 377DE0D5 wait for a campaign trace with FrameTraceReads.

### Round v60 (2026-11-13, stamped and installed 15:05 with helper107, not run in game)

- Shared, no change here: Attila's Path-Traced GI of no view (helper107) needs the game's sun colour; Pharaoh's sun
  shaders (F883F5B2: wrap lighting, cloud shadows) light it another way and aren't read for it yet, so its paths stay as in
  v59. Its rt_sun writes the same texels as v59's (WARP: texel 2 stays zero).
- Log: the "Ray tracing with" line names the Light Bounce's rays and bounces.

### Round v59 (2026-11-13, water rows on; stamped and installed 13:35 with helper106, not run in game)

- **Changed: Softer Water Reflections and Steadier Water Reflections on by default and in RT/PT**, no longer Experimental
  (ATN's Attila test: they fix the unstable water). A saved Off stays Off. With Softer on, Samples and Reuse now show and act
  on the water by default (Pharaoh's rule).
- Shared, no change here: Attila's soldier shadow distance under the rays; Warhammer III's Grass Without Waiting (Pharaoh has
  no GPU-placed grass, so no row).
- Page dump (`build_dump.py r59final` against `r59pre`): the two water rows' defaults and labels; the preset census lists two fewer
  writes that change nothing (Samples, Reuse now shown).

### Round v58 (2026-11-13, campaign fixes; stamped and installed 12:05 with helper106 (Steady Torch Shadows needs it), not run in game)

- **Fixed: GT-VBAO a rectangle in the top left at Ultra Performance** (ATN, campaign). Every addon shader is built with `/Qstrip_reflect`, so the shared `twattila/view_share.hpp` Reflect never saw `render_scale` at b12 in our replaced shaders (ssao01 D42C3523 with ao_bitmask in its place, the tone mappers, fire02 E318CE77, the DOF pair): they got no cb12 of their own and read whatever b12 another draw left. The AO pass is the frame's first such reader: b12 null, share 1, the whole view's AO squeezed into the render corner. Reflect now takes a code without RDEF that declares cb12 as `render_scale` (0 of the 3168 game shaders, 70 of 179 built). Shared: every TW addon with view_share (Attila, Thrones, Rome II, Troy, WH1, WH2) had the same bug at its render-share replacements; WH3 doesn't install view_share. The game's AO blurs 7F637259 / BFD285BD are replaced (`shaders/ssao_blur.hlsli`): the region read scaled their taps too (a third of a texel apart at 33%). `twwarhammer2/bitmask_ao.hlsli` holds the depth levels' reads on the picture's last texel (the same texels wherever the depth is whole: Troy, WH2, WH3, Shogun 2).
- **Fixed: the campaign's clouds sliding toward the top left at low scales.** `t_shroud_clouds` (drawn at the render share) was read after DLSS at the display uv: the glow bright pass 57E5140E (t0 a region read with `RegionRead::size_slot` 1: t1's size says whether the share applies; in battles t0 is a 1024 x 1024 stand-in) and the six replaced tone mappers t01-t04, dof00, dof01 (`twattila/render_scale.hlsli` `RenderShareUv`).
- **Fixed: 35 shaders scaled twice below 100%.** Pharaoh maps uvs to its viewport itself (c29 x c31); view_share's `SetViewportChains(true)` (Pharaoh only) leaves a uv any of whose factors is the viewport: the sun shaders' screen-space environment occlusion (24), shroud_plane (7), battle fog 99164788 / 0CB1C821, the fog's sun scan DB53BDDC, the SSR ray cast 6ECC6281.
- **Fixed: palms and settlements out of the rays** (ATN: palms without ray-traced shadows with the shadow maps skipped). 1C4B8681 (campaign_trees: 6A599241 plus a sway of at most 0.12 m x the wind multiplier) captured by `rt_capture_rigid_standard_cut_out` (6A599241's world position, the UV in w), placed by 6A599241's record, foliage and still (the sway left out); 6A599241's four alpha-tested PS (4B9BDD76, 7550C192, 81931C18, BD6C63DE) cut out; BUILDING_MASKS takes D24616D0 (settlements, cliffs) and the solid 509F97FB, 730C5322, 85E52BED, A6C653E1. Swaying Shadows Range and Off-Screen Sway stay hidden (shared `rt_scene.hpp`: still foliage doesn't show them; the palms had made them appear).
- **New: Steady Torch Shadows (Experimental, off)**, under the torch rows (shared: every TW game's page). The helper's torch bits change every frame by design (Ray Reconstruction's random disk point, the turns, Importance-Sampled Torch Shadows' kept bits at a jittered pixel), and under NRD nothing averages them. On: every light a pixel reaches is traced every frame and each soft ray goes to the same disk point. Needs helper106 (`build\tw_dlaa_helper106`, not installed; older helpers ignore the flag, bit 28); the bridge stays v31.
- **New: Water Clarity (Experimental, 1) and Water Caustics (Experimental, 0)** in Reflections and Water, as Warhammer III's, on 84 water pixel shaders. The shared `twattila/water_effects.hpp` takes the opacity operand per row (`WaterShader::opacity`: Pharaoh's absorption is exp2(-depth x opacity) without WH3's log2(e); cb4[3].w sea, cb5[3].w campaign sea, v11.w river, cb3[45].w big river); campaign water without caustics.
- **WARP** (`tools\tw\warp_tests\pharaoh`, each `run_v58.txt`): `ao_v58` PASS (our blurs at 100% 0 of 57,600 values apart from the game's; at 33% against 100% the bitmask 0 of 12,464 pixels inside 8 px of the edges, after the blurs 0; CONTROL cb12 unbound 13,936 of 14,400 apart, v53's blurs 14,087). `scale_v58` PASS (57E5140E 0 of 12,288, CONTROL 4,950; the clouds line 0 bits apart, CONTROL 12,288; 99164788 0, CONTROL 692; DB53BDDC visibility 0, CONTROL apart). `patch_v58`: chains off byte-identical on all 10 TW dumps (Attila 2418, Rome II 2257, Thrones 2372, Troy 2882, WH1 2623, WH2 3595, WH3 2584, Pharaoh 2897, Shogun 2 1074, Rome Remastered 672), on: exactly the census's 35 change. `trees_v58`: at wind 0, 1C4B8681 and 6A599241 against the UV capture 1.36e-8 (relative), CONTROL wind 1 8.25e-4. `water_v58`: WH3 6 of 6 byte-identical to v57; Pharaoh 84 of 84 as the prototype, neutral 0 bits apart of 19,464,192 values, clarity and tile 0 misses, controls apart (136 runs). The sun, `ssr_v56` and `smoke_v57` suites pass on the final build.
- **Harness** (`tools\tw\rt_harness\light_flicker_v58.txt`, GPU, 40 frames, the last 20): helper106 without the flag = helper105 to the bit (39,910.6 bits changed a frame, jitter + 27 lights); Steady with jitter 28,344 (the jitter's own edges), Importance 66,801; still camera, RR soft 0.25 m: 24,154 to 0 (27 lights), 5,870 to 0 (3 lights), Importance 3,107 to 0; every harness check passes.
- **Census** (the skill's `scale_census.py` and `caster_census.py`, `tools\tw\pharaoh\v58`): the scale census of 2885 PS / CS (1789 read screen space) went from BUG (A) 34, BUG (B) 27, unclear 6 to 0, 11 and 31 (`census_pharaoh_after.txt`; the 24 sun shaders now unclear for their second, mixed-chain site). The caster census (campaign, B, W1): 19 held, 31 not in the scene (sprites, sky, water, lights, fog, full-screen passes and the rows below), 4 atlas only.
- **Page** (`build_dump.py v62` against v60): 685 to 688 rows, none lost: Steady Torch Shadows (Experimental) default 0 in Shadows, Water Clarity (Experimental) 1 and Water Caustics (Experimental) 0 in Reflections and Water; RT/PT turns Water Caustics on (ATN's list). Attila (`a v171` against `a v169`) and Warhammer III (`w v67` against `w v65`): only Steady Torch Shadows (shown with the torch rows) and the date.

- **Not done: run in game.** Built and checked on WARP and the harness only.
- **Not done: the scale census's 11 BUG (B) rows**: the campaign's shroud decals 61C823A0 / 8852FBD3 / AABB5EBA / 5078A6E0 (NDC from their own inv_rt_size: a campaign trace below 100% with c13's value), the SSR apply DE4F92CD's quad uv (vb0 at 50%), phar_building_water_01 03B9CEFE / 9D28319F / 40154AD7 / AC04E623 (t_ssao at the view's uv; in no trace: a trace that draws them, then `RENDER_SCALE_READS` t12 / t9 / t11), 3F1B86E1 / E8524BB1 (read by hand: the NDC reaches only the depth column, right). Unclear: the 24 sun shaders' main position site (a mixed chain; patched since v53, unchanged now; their sun draw at 33% against 100% in a trace settles it), the SSR variants 6ECC6281, 11C3B7AE, 5DB9A046, F93FA73F (in no trace), god rays 7019D8BF, the gizmo BBD2C559 / FD367E1C. Left as they are: 99164788's 5x5 taps spaced by its buffer's size (a third apart at 33%), DB53BDDC's edge fade from the texture uv.
- **Not done: casters still out of the rays**: 026CBC61 (the campaign's global terrain, 40 draws, 2.5 M indices: whether 377DE0D5's tiles already cover it needs one campaign trace with both), 58733DB8 (the campaign's blend-mask drapes, on the terrain), CE5AA8D1 (campaign tree billboards, no shadow draw), 74776F85 (terrain blend), D12E68D5 (roads), 1F0734D0 (projected decals); FE92BA2D (far forest blocks) and 76225B91 (banners) out on purpose. Atlas only: 0B28E4BC (the campaign terrain's shadow VS, with world outputs: a candidate to keep hills out of view in the campaign), 5DC7A874 (the drapes' shadow VS), 35D229C0 / BDC503B1 (depth only). Replace per caster is impossible at the sun PS (one comparison sampler for the atlas).
- **Not done: whether the game moves its torches.** Steady fixes the helper's part; a trace of two frames' light draws in a night siege shows whether the light positions move too.

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64` (45 s; the shared `view_share.hpp`, `render_scale.hlsli`, `water_effects.hpp`, `rt_core.hpp`, `rt_scene.hpp`, `graphics_page.hpp` and helper changes: the tw-port-checklist x64 list and build32 Attila, Rome II, Thrones built too; `test/tw_*` pass on x86 and amd64). Helper: `src\games\twattila\dlaa_helper\build.ps1 build\tw_dlaa_helper106`. Manual check: the campaign at DLSS Ultra Performance with GT-VBAO and Rays Replace Shadow Maps: AO over the whole view, clouds in place, palms casting.

### Round v57 (2026-11-13, smoke shadows; stamped and installed 03:17 with helper105; one agent siege run W2 with the row on: the capture line, 67 to 105 smoke particles a frame, no crash; the shadows' look not seen)

- **New: Smoke Shadows (Experimental, off)** in Shadows with Ray-Traced (DXR), and Smoke Shadow Quality under its More Options: the smoke of burning buildings and of the siege dims the rays' sunlight on what lies below it, as Attila's Smoke Shadows. Pharaoh draws its lit particles as instances of a quad (vfx vs50_main_instancing 109B79CE with vfxextremeultra 3F1B86E1, ps_main_high_quality; siege trace `W1_015828` line 3156: indexed, 6 indices x 91 instances, vb0 the corners, blend ONE / INV_SRC_ALPHA), not as Attila's one vertex a particle, so the shared `twattila/rt_core.hpp` `CaptureParticles` has a second form (`rt_scene::InstallSmokeShadows`' quad shader): at that draw our geometry shader `rt_particle_quads` streams out one record an instance, in the layout rt_smoke already reads (centre, colour and opacity, size); rt_smoke and the helper's smoke rays are unchanged. What the bytes say (B, `fxc /dumpbin` in `tools\tw\pharaoh\v55\asm`):
  - The five pixel shaders 109B79CE draws with (3F1B86E1; 2A315C27 into the distortion buffer; ACE25F9D and CFB48C60, meshes; 5AD46115, the particles' lights) all read the same frame-wide sorted list (t2: 91 in every draw of the trace). The vertex shader keeps an emitter only where its m_draw_flags meets the draw's g_draw_flags (cb_vfx: 0x40200001 at 3F1B86E1) and puts the rest at (-10, -10, -10, 0) with no colour. Only 3F1B86E1's quads cast.
  - The vertex shader writes eleven outputs (v55's note said two): SV_POSITION, COLOR (w the alpha), the emitter (TEXCOORD0), the uv (TEXCOORD1), the next cell's uv and blend (TEXCOORD3), the world corner and age (TEXCOORD4), the corner in the quad (TEXCOORD8) and a frame. Each corner's world position carries the emitter's depth bias toward the camera, in view z only; the record's corners have it put back (until view_projection's w is SV_POSITION's w again). The centre and its uv are the quad's corner (0, 0) by barycentrics of the first triangle; the size, the quad's world spans.
  - Opacity as 3F1B86E1 computes its alpha at that centre: the diffuse array's alpha (both cells of an animated texture) at the mip level the pixel shader's sample takes there (the uv's change across one pixel of the camera's viewport, from the corners: a far puff averages its texture as the game draws it, a near one is read at its top mip), sRGB-decoded, times COLOR's w, then the alpha crush; its soft edge against the scene's depth depends on the view and is left out. Only lit emitters cast (m_behaviour_mask 0x80000, the pixel shader's sun lighting). Fire's glow is the texture's colour above its alpha, added through the same blend, so it casts nothing. Particles drawn on the screen (no world position), drawn as meshes (the draw flags' quad bit 0x200000 missing) and around the camera cast none.
- RT/PT leaves Smoke Shadows off (the shared `graphics_page.hpp` `Bindings::smoke_shadows_tested`, true for every other game); Reset and No RT/PT too. The hidden row was saved at RT/PT's 1 by earlier versions (every keyed setting is saved): cleared once from the three presets (`SettingsRevision` 1, as Warhammer III's).
- **WARP** on the game's bytes (`tools\tw\warp_tests\pharaoh\smoke_v57`: the siege trace's camera and cb_vfx, 144 particles of 12 emitters: camera-facing, upright, flat and velocity-aligned quads, pivots, depth biases up to 2.5 m, another draw's emitter, screen-space, unlit, alpha crush, animated cells, the size limit; two index orders): the records' centres and sizes against the corners' own (the game's vertex shader streamed out as it is, each corner solved from its SV_POSITION with view_projection in double) 0 apart of 120 placed (tolerance 2e-6 x (600 m + the distance), worst 0.056 of it), the 12 culled and 12 screen-space without a record; CONTROL without the bias put back: 60 apart. Opacity against 3F1B86E1's own alpha at the centre's pixel, 69 particles each drawn alone: 0 apart within 4 ulps with a texture flat where it is read (the PS's COLOR comes interpolated, 1-2 ulps off), 0 apart within 1.12e-3 with a smooth one (bilinear weights have 8 bits an axis; worst 7.5e-4); CONTROLs: without the texture's alpha 39 apart, with unlit emitters 12. The mip level, from a texture whose mips hold one alpha each: the level the pixel shader's sample took (from its pixel quad) against the record's, 23 particles (19 between the first and the last mip), worst 0.0039 of a level apart; CONTROL at the top mip 22 apart. The decode's 1 / 1.055 is the pixel shader's own bits (0x3F72A76F: fxc rounds 0.947867298 one lower).
- **Page** (`tools\tw\pharaoh\build_dump.py v59` against v58, the dump now with the smoke installed): 611 keys, none lost. Smoke Shadows (Experimental), default 1 to 0 (it was hidden), shown with Ray-Traced (DXR); Smoke Shadow Quality under More Options (default 3, as before). RT/PT turns on what it did in v58 (no smoke shadows). Attila (`a v168`, `a v169`) and Warhammer III (`w v64`, `w v65`): only the date; `p v60` after the review's fixes: the same as v59 but the date.

- **Not done: run in game.** Built and checked on WARP only (the shared stream-out call itself runs only in the game). What ATN's test should show (siege benchmark, RT/PT, then Smoke Shadows (Experimental) on, Log Details on):
  1. Control: Smoke Shadows off looks like v56.
  2. On: the log's "Ray tracing: Smoke Shadows: one particle a quad of vertex shader 109B79CE (N instances in this draw, room for 524288)." once, and "Smoke Shadows, N particles a frame, most quads Q, least L." in the status lines; the burning buildings' smoke darkens the ground under it, the flames don't.
  3. Smoke Shadow Quality Low against Ultra in one run (paused): holes and flicker in thick smoke; the cost from Record Ray Tracing Profile.
  4. The camera down in smoke: no dark disc around the view's centre; zooming out and in, the smoke's shadow keeps its strength (the opacity follows the game's mip).
  5. Cavalry's dust: does it cast, and should it? (the bytes can't tell dust from smoke).
- **Not done: smoke just off screen casts nothing.** The vertex shader culls particles outside the view (its frustum test, widened by m_vs_viewport_culling_expansion + g_draw_flags.y): their records have no opacity. Whether the sorted list itself holds off-screen particles is open (the sort's compute shader not read; the traces hold 13 to 91 instances a frame). Smallest step: read the shader that writes t2, then the stream-out draw with our own cb_vfx, g_draw_flags.y raised (the draw flags kept), WARP against off-screen particles.
- **Not done: E8524BB1** (ps_main: the same alpha, read from t3 / t5 / s1, its shadow mode low; in the dump only, B) and the MSAA permutation 740B2536 (no rays under MSAA) left out. Dust and smoke apart: level C (needs each emitter's t8 behaviour mask and diffuse slice logged in a smoky battle). The opacity is one sample at the centre: a near puff with a hole in its middle casts less than it covers; the game's sampler (anisotropic or not) is not in the trace, the record takes D3D's isotropic level.

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64` (44 s for the add-on's one source; the shared `rt_core.hpp`, `rt_scene.hpp` and `graphics_page.hpp` changes: the tw-port-checklist x64 list and build32 Attila, Rome II, Thrones built too). After a change to `rt_particle_quads.gs_5_0.hlsl` run its fxc line (the file's head), then strip the header's trailing spaces. Manual check: a siege with burning buildings, Ray-Traced (DXR) shadows and Smoke Shadows (Experimental) on: the ground under the smoke darker, moving with the smoke; Off: as v56.

### Round v56 (2026-11-13, ray-traced water reflections; stamped and installed 01:57, one agent siege run W1: the hooks, tracing and skip lines logged, reflections 1.61 ms GPU, no crash; the water's look not seen)

- **New: Ray-Traced Water Reflections (Experimental, off)**, in Reflections and Water: the method Ray-Traced (DXR, Experimental) with Water Reflections (Experimental) on, an upscaler, and the game's Screen Space Reflections on (the game's chain is the only place the water's reflection is drawn: `tools\tw\pharaoh\reflections_plan.md`). At the game's march, backward_ssr 9A55B7F2 (a compute dispatch), the shared `twattila/rt_core.hpp` `rt_scene::Reflections` traces from the water's own depth (t1, R24G8) with this pass's light (t3, R16G16B16A16F, before the fog) as the hits' light: `rt_light_copy`, Warhammer III's copy now in `twattila` (the bridge's R11G11B10F, written within the traced size, negatives 0), runs for that format through `rt_host.light_copy`. `rt_host.rewrite_guides` is `RewriteSunGuides`: TW_HOST's `RewriteGuides` reads the pixel shader slots, stale at a dispatch. Where the rays left from the water, the game's march, its downsample 0B78827B and its 20 mips (10 x 55298A11, 10 x F49D4738) are left out (their target's only other reader in the A2 (33 to 100%), B and S1 traces is the apply), and the apply DE4F92CD draws our copy (`shaders/ssrapply00_0xDE4F92CD.ps_5_0.hlsl`; the game's shader bound again after the draw): the rays' light (t47.rgb, without the game's x 5 squared decode, never below 0) times the water's own weight as the reflection's colour, the share of rays that met a surface (t47.a) as its confidence; the cube, the sun's lobe and the fog stay the game's. Under the game's MSAA the game's reflections stay (t2 / t4 multisampled; one log line). Our copy is 3Dmigoto's decompile with its two traps fixed (the cube pair's lerp is g_skydome_lerp at cb0[0].y, not the bool at cb0[0].x; the fog's bottom and top heights written as one); `cb_audit.py`: it reads every constant the game's does, plus cb10[0].x (the water flag). Samples and Reuse show and act only with Softer Water Reflections (calm water is a mirror: one ray, no reuse).
- **New: Ray-Traced Specular Occlusion (Experimental, off)**, shown with Water Reflections: the cube the water reflects dimmed where the rays found its lobe blocked (t46, `RtOccludedEnvironment`); the sun's lobe stays.
- **WARP** on the game's bytes of DE4F92CD against the built copy (`tools\tw\warp_tests\pharaoh\ssr_v56`, 24 runs of 96 x 64, a real camera, both `misc` switches, no MSAA): nothing bound 0 of 589,824 values apart at 100%, 0 of 1,179,648 at 50% and 67%; t47 bound with the water flag 0: 0 apart; flag 1 against the cube's light + (t47.rgb x the weight - it) x t47.a: 0 misses (36,940 mirrored pixels, 36,830 partial); t46 0.5 against the game's with its cubes x 0.5: 0 bits apart. Controls: the game's decode on t47 207,453 misses, the game's own cubes 277,923; at 50% t47 read at the render pixel 0 misses, at the viewport's uv (CONTROL) 268,723. Raw 3Dmigoto 165,780 apart. The shared light copy against Warhammer III's old path (`tools\tw\warp_tests\light_copy_v56`): 0 texels apart in 4 cases (100%, a share, odd sizes, NaN / infinity / negatives), what lies outside the traced size kept; CONTROL the whole buffer 207 apart.
- **New: the frame trace's extras** (shared `twattila/frame_trace.hpp`, behind its Options; empty for every other game): the blend state at the first draw of listed pixel shaders (here the apply DE4F92CD and the particles' 3F1B86E1: alpha to coverage, blend factor, sample mask, per target the colour and alpha factors and the write mask), the vertex shader's buffers at each draw of listed vertex shaders (here the particles' 109B79CE: per slot the buffer's bytes, stride and flags, the view's first and count), the index and instance counts apart on instanced draws, and 3F1B86E1's constants (`value_hashes`). These are the smoke shadows' and the apply's open questions (v55's Not done).
- **Page** (`tools\tw\pharaoh\build_dump.py v58` against v57): 604 to 611 keys, none lost; the Reflections and Water section (method Game's SSR / Ray-Traced (DXR, Experimental), its note, Samples, Reuse, Water Reflections (Experimental), Steadier, Softer, Specular Occlusion (Experimental)); RT/PT's, Hills Out of View's, SER's and the denoiser's tooltips now name the reflections. RT/PT writes Ray-Traced Reflections, Water Reflections and Specular Occlusion 1 (the shared preset, as the fog and torch rows); Reset 0. Attila (`a v167`) and Warhammer III (`w v63`): only the date.

- **Not done: run in game.** All of it is built and checked on WARP only. What a trace must show (a battle by a river and a siege by the sea, in-game SSR on, MSAA off, Log Details on): "Ray-traced reflections: tracing, the water from its own surface.", "Ray-traced reflections: the hits read the pass's light (R16G16B16A16F, copied)", "First run: Ray-Traced Water Reflections", and with the game's MSAA "the game's MSAA is on" once; the frame trace with the rays on: no 9A55B7F2, 0B78827B, 55298A11 or F49D4738, DE4F92CD's blend line. Risks: water brighter than the game's (its reflection was RGBA8, at most 25 after its decode; the rays' isn't clipped), no fog in the reflected leg, a rougher look on waves than the game's planar mirror, shallow and campaign water untraced (the chain's mask is the sea's and the river's bit 64).
- **Not done: Smoke Shadows** (v55's Not done): the trace's extras above are its smallest next step; no capture yet.

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64` (the shared `rt_core.hpp`, `frame_trace.hpp` and `rt_light_copy` changes: the tw-port-checklist x64 list and build32 Attila, Rome II, Thrones built too; the Pharaoh sun suite, `test/tw_screen_space_shadows` and the shared lifetime suites pass). Manual check: a battle by a river with DLSS, in-game Screen Space Reflections on, Reflections Ray-Traced (DXR) and Water Reflections on: the river reflects the banks, trees and units, also what is off screen, without the game's streaks at the screen's edges; its colour and brightness near the game's (Water Reflections off). Specular Occlusion on: the sky's reflection darker under bridges and banks. In-game SSR off: the water reflects only the cube, as the game's. The game's MSAA on: the game's reflections.

### Round v55 (2026-11-13, screen-space shadow extras, smoke; stamped and installed with helper105 for the maintainer's test, not run in game)

- **New: Light Through Banners and Coloured Shadows (Experimental, off) for Bend's screen-space shadows**, under Bend Screen-Space's More Options. The siege's cloth (`phar_rigid_battle_cloth_wind` VS 2F2A0E7E, PS 5277CF9F and 97850C83) and flags (`rigid_default_flag` VS 8DE94E2F, PS 369C9E2E), in the rays' scene since v49, are drawn again after their G-buffer draws into the banners' depth (shared `twattila/screen_space_shadows.hpp` `InstallThin`, as Attila and Warhammer III; both VS write SV_Position at o0, `fxc /dumpbin`; off under the game's MSAA). Their shadows let the sun through, coloured by the cloth with Coloured Shadows. The G-buffer's colour is an sRGB texture (R8G8B8A8_UNORM_SRGB, format 29 at the sun draw's t9 in every trace), so it is read linear (power 1, as Warhammer III). The banners (76225B91) stay out, as in the rays: the game draws them unlit (material 10) and gives them no shadow. Level A for the draws (siege trace: 3 cloth and 2 flag draws), none in the battle traces. `test/tw_screen_space_shadows` case 3 in Pharaoh's form (the sun's direction at float 4 of lighting_VS_PS, an sRGB colour): occlusion 0.323 and filter 0.000 / 0.875 / 0.875, as Attila's form gives (checked within 0.01); CONTROL: a decoy direction at Attila's float 1, read there, occlusion 0.000.
- **New: Light Through Foliage (Experimental, off)** for Bend, under its More Options, from the G-buffer's own foliage bit (Warhammer III's route; shared `twattila/graphics_page.hpp` `Bindings::foliage_light_tested`, false here: the row Experimental, 0 by default; Attila and Warhammer III unchanged). Channel 3's alpha byte (the sun draw's t10) has bit 128 only on foliage: the material census of the data run's six traces (`tools\tw\pharaoh\v55\material_census.py`) lists the 50 vertex and pixel shader pairs that write it, and the bytes their PS write (`fxc /dumpbin`): leaves 5FC6C4AF, far tree cards 3DE66666 and the far forest's blocks 6622ADC5 128, grass 12A15935 160; branches 0, rigid objects and buildings 1, units and props 33 or 1, skin 8, banners 10, terrain 21, water and rivers 64; 11 blend with that byte write-masked. The sun shader's own contact march lets half the sun through an occluder with bit 128 (F883F5B2: `and r20.z, r20.z, l(128)`, then the occluder's transmission halfway to 1). Level A. C: the roads (8A4D87B7, siege only) and terrain_to_gbuffer (768B1C08) write that byte through a blend whose factors no trace logs; the game's own lighting reads the same bit there. `test/tw_screen_space_shadows` case 1: Pharaoh's bytes (bit 128 on the bush, the seven others in turns elsewhere) against the depth copies 0 pixels apart (x86 214 shadowed alike, amd64 215); CONTROL: the water's bit 64 as the mark, 213 and 212 apart.
- **Fixed: Coloured Shadows filtered the sun's direction, not its colour** (the rays' Coloured Shadows since v49; Bend's from now). The sun shaders' patch multiplied lighting_VS_PS c1 by the filter at t50: Attila's sun colour, here `sun_direction` (`sun_colour` is c3, in the RDEF of all 25 sun shaders), so under cloth the sun's direction was bent, not tinted. Shared `twattila/screen_space_shadows.hpp`: `sun_colour_register` (1 by default; Pharaoh sets 3) and `Patch`'s colour register. WARP on the game's bytes (`tools\tw\pharaoh\sun_test`, v55, 2 runs of 4 synthetic scenes and the 3 traces' constants): with a filter F of (0.5, 0.75, 0) bound, the 18 patched shaders with a filter against the game's with its sun colour x (1 - F), 0 of 4,128,768 values apart; a filter of 0 bound against the game's 0; with t49 = 1 too, against the game's with every comparison 0 and the colour filtered 0; CONTROL: v54's patch (c1) 1,595,190 apart, the filter's reach 935,178. The whole sun suite passes with the addon's register (DRAW 25 of 25, v51 25 of 25, v54, v55). Every other game's patch is the same to the byte: 21,946 pixel shaders of the nine TW dumps, forward and not, 5,254 patches, 0 apart at the default register (`tools\tw\pharaoh\v55\patch_same`).
- **Checked: Torch and Fire Shadows for Bend** (installed since v50 with the point lights: the light draw E318CE77 comes after the sun draw, siege trace lines 1803 and 1814). `test/tw_screen_space_shadows` case 5 with Pharaoh's 64-byte instances (E19549A1's vb1: colour, position and reach, spot, cone): the same light picked and 0 of 129,600 pixels' lit bits apart from the 48-byte ones'. Nothing changed.
- **Page** (`tools\tw\pharaoh\build_dump.py v56` against v55): three rows changed, nothing else but the build date. Light Through Foliage (Experimental) added at 0; Light Through Banners (Experimental) 0.35 to 0 and Coloured Shadows (Experimental) on to off, with Pharaoh's tooltips (cloth and flags). All three show in 512 of the 3,076 states (Bend with More Options open). The presets audit: the two hidden writes that acted in v54 (No RT/PT's 0.35 and on) are gone. Attila (`dump_build.py a v165` against v164) and Warhammer III (`w v61` against v60): only the date.

- **Not done: Smoke Shadows (the shadow map and the rays').** The census left the particle layout at level C; the bytes now say the shared capture can't take it. `twattila/rt_core.hpp` `CaptureParticles` (smoke_map's and rt_smoke's only input) takes non-indexed draws of one vertex a particle (Attila's A7F257B5 with a geometry shader making the quad) and streams out its SV_POSITION as the world centre, COLOR's w as opacity, TEXCOORD3 and TEXCOORD0's x as the size, with the emitters' behaviours at the VS's cb3. Pharaoh's lit particles (VS 109B79CE `vs50_main_instancing`, PS 3F1B86E1 `ps_main_high_quality`, under MSAA 740B2536) are GPU-simulated quads: in the siege trace one indexed instanced draw (line 1882: 510 indices x instances, vb0 stride 12 the quad's corners, vb1 stride 112, blend on), in the battle 150 (A). The VS (B, `tools\tw\pharaoh\v55\asm`) takes only POSITION (the corner) and SV_InstanceID, reads each particle through the sorted list t2 from structured buffers (t6 particles, 64 bytes: position at 16, velocity, life; t1 emitter instances, 128 bytes; t4 emitter constants, 112 bytes, the behaviour mask at 0) and the curves in the texture array t3, and writes SV_POSITION in clip space (view_projection, cb0[9-12], x the corner's world position), TEXCOORD0 as uint; its RDEF has only cb0 (camera) and cb1 (cb_vfx): no cb3. So `CaptureParticles` returns at once on these draws (no vertex count), and streamed as they are they would give corners, not particles. The other particle path, VS 578435F4 `vs50_main` with GS BA7CA9D1 (a point a particle, the quad made in the GS: Attila's form), is in the dump only, drawn in no trace (B). Rows stay hidden (`smoke_shadows_installed` false). What a trace must show (a smoky siege, burning buildings, ATN's settings): which path draws (109B79CE or 578435F4 + BA7CA9D1, with 3F1B86E1, E8524BB1 or 740B2536); per lit draw the index and instance counts apart (the trace logs their product) and the first six indices with vb0's four corners; the VS's buffers t1, t2, t4, t6 (the trace lists PS and CS slots only: their element counts bound the capture); the lit draws' SrcBlend and DestBlend (the trace logs the switch only; smoke is blended over, fire's glow added, and only the first casts); the camera at the first 3F1B86E1 draw (`value_hashes`), for the space of the corner positions; the emitters' behaviour masks of rain, dust and the camera's haze, which must cast none. Then the work: stream out 109B79CE's corners at the lit draws, one compute pass making a 48-byte record a quad (centre, colour and opacity, size, emitter) into rt_scene's particle stream; `smoke_map` and `rt_smoke` read it unchanged. Smallest next step: 0x3F1B86E1 in the trace's `value_hashes` and the instance count logged apart (shared `frame_trace.hpp`, behind a parameter), one siege trace.
- **Planned, no code: Ray-Traced Water Reflections** (`tools\tw\pharaoh\reflections_plan.md`). The game's screen-space reflections are the water's alone: the `ssr_new` chain's mask is channel 3's bit 64, which only the sea (water_troy 833E7963) and the river (battle_river_troy 4CA0D9CC) write (six-trace census, A). Its march, backward_ssr 9A55B7F2, projects the lit picture over the water's plane (a planar mirror, `atomic_umax` into a buffer). The apply PS DE4F92CD then blends that with the game's cube, a sun-disk lobe and the fog, times the water's own Fresnel weight, into the light buffer (A, bytes read). With the game's SSR off the chain doesn't run and the water reflects the cube itself (`g_ssr_enabled`, run D, A). Everything else keeps the sun shaders' cube specular. The plan follows Warhammer III's march hook and replaced resolve:
  - `rt_scene::Reflections` at 9A55B7F2: the water's depth t1, its light t3 through `rt_light_copy` (moved to twattila, shared with Warhammer III), the game's march left out.
  - DE4F92CD replaced: t47's light times the same weight, its share as the confidence.
  - `rt_host.rewrite_guides = RewriteSunGuides`, needed: TW_HOST's reads PS slots that are stale at a dispatch.
  - Ray Reconstruction's water guides: done since v53.
  - The rows: Experimental, off.

  Needs a trace: the apply's blend factors. Risks: water brighter than the game's (RGBA8, clipped at 25), a rougher look on waves than the planar mirror, no fog in the reflected leg, MSAA (keep the game's), shallow and campaign water untraced.

- RT/PT's GT-VBAO off (v54's local wrap of the button) is now in the shared `graphics_page.hpp`, for every game whose rays multiply the game's AO (`rays_replace_ao` false: here and Warhammer III, whose RT/PT left GT-VBAO on). Page dump v57 vs v56: the same but the build date (RT/PT still writes `ssao_mode 1 -> 0`).

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64` (the shared `screen_space_shadows.hpp` and `graphics_page.hpp` changes: the tw-port-checklist x64 list and build32 Attila, Rome II, Thrones built too; `test/tw_screen_space_shadows` x86 and amd64 PASS). Manual check: a siege with Bend Screen-Space, Light Through Banners 0.35 and Coloured Shadows on (Log Details on): the shadows of tents' cloth and flags are lighter and take their colour; with the rays' Coloured Shadows the ground under cloth keeps the sun's direction (no brighter or darker patches than without the cloth). Light Through Foliage 0.5: Bend's shadows of leaves and grass half as dark, walls' and soldiers' as before. With the game's MSAA both banner rows do nothing (one log line).

### Round v54 (2026-11-12, fog shadows, rays replacing the shadow maps, presets; stamped and installed 23:50 for the maintainer's test, not run in game)

- **New: Fog Shadows (Experimental, off)** in Ray Tracing Extra Options with an upscaler (with any Shadows method, as Warhammer III's), and Torch and Fire Light in Fog (Experimental, off) under Shadows' More Options with Torch and Fire Shadows. The fog's in-scattering (battlefog inscattering_transmittance 598C59F3, shadows_hardware_gather_5_0: the only fog light shader in the data run's battle, siege and SSAO-off traces, drawn at a quarter of the render) marches the sun's shadow maps 16 steps per pixel. With the row on, the shared fog hook (`twattila/rt_scene.hpp` `InstallFogShadows`) binds the rays' fog (t43 visibility, t44 torch light), and in that draw our copy (`shaders/battlefog00_0x598C59F3.ps_5_0.hlsl`) replaces the game's march: its gate is false (the game's own path with `g_fog_shadow_strength.y` 0, the shadow ranges and the sun disc kept), the sun's light in the fog is multiplied by the rays' visibility at the render pixel the game reads its depth at, and the torches' light is added. The game's shader is bound again after the draw, and with nothing bound the game's own bytes draw. Fog Shadows alone use no Ray Reconstruction (Warhammer III's `cheap_fog_alone`). RT/PT turns the row on. The other permutations (fetch4, gather 4.1, pcf, baked shadows only) were drawn in no trace and stay the game's. Our copy is 3Dmigoto's decompile with its traps fixed (two two-component constants written as one, three float-and-mask selects truncated to ints, the march kept a `[loop]`: 28 KB built against the game's 37 KB). WARP on the game's bytes with a real camera, 24 runs (`tools\tw\warp_tests\pharaoh\fog_v54`, the built cso): nothing bound 0 of 786,432 values apart from the game (raw 3Dmigoto 567,295); rays 1 against the game with its march off 0 of 393,216 apart, rays 0 against the march off and no sun 0 apart, rays 0.5 halfway 0 apart (largest 1.5e-5); a visibility ramp read at the depth's render pixel at 100% and 50% 0 of 294,912 and 73,728 apart; torch light L against the sun's colour raised by L 0 of 294,912 apart; transmittance (o1, o0.w) unchanged, 0 of 1,671,168. Controls: the game as drawn (march on) 193,123 apart, the ramp at the fog texel's own pixel 296,348. `cb_audit.py`: the fog reads every constant the game's does, plus the torches' glow.
- **New: Rays Replace Shadow Maps (Experimental, off) and Skip the Shadow Maps (Experimental, off)**, Warhammer III's rows, under Shadows' More Options with Ray-Traced (DXR) shadows. Warhammer III replaces a mask pass; Pharaoh's sun shaders read the maps themselves, so the switch is in their draws. 12 of the 25 sun shaders read the per-frame atlas (`g_tShadowBuffer_SM50` t3 through s2, their only use of s2: RDEF and disassembly; the other 13 read the baked map alone or no map, and use s2 for another texture). In those draws, with the rays' shadow bound, s2 is a comparison sampler that passes every comparison, so each patched comparison (each x the rays' visibility) gives the rays' shadow alone; the game's sampler is bound again after the draw. The kept baked map (t1 through s1: what the game baked for the map, read from 192 m with the traces' constants, the far forest blocks the rays don't hold among it) stays the game's, and so do the atlas's other readers (water, rivers, particles, the fog without Fog Shadows). Skip the Shadow Maps (only with Replace on) skips the atlas's draws through Attila's Ray-Traced Shadows Only; shared `twattila/rt_scene.hpp`: the map it learns is the host's `cascade_target` where one is named, so never Pharaoh's kept baked map (Attila unchanged). Level B for Skip: the atlas is drawn with a depth test (LESS_EQUAL) and writes every frame, so the game clears it every frame (the frame trace records no depth clears). Shadow Map Size now hides under the rays while Replace is on (`rays_multiply_maps` off); shared `twattila/graphics_page.hpp`: `rays_replace_maps_tested` (false: the row Experimental, off) and its tooltip as a page text (Warhammer III unchanged). Shadow Casters Out of View's tooltip is now the sun shadows' one, as Warhammer III's. WARP on the game's bytes of the 12 shaders, 14 scenes each (synthetic, and the battle's, siege's and SSAO-off traces' constants; `tools\tw\pharaoh\sun_test`, P): the rays' shadow 0 or 1 per pixel against the game's bytes with the atlas lit or every comparison 0, 0 of 2,752,512 values apart; a fractional shadow against today's with nothing drawn into the atlas 0 apart; Replace off after on (the rebind) 0 apart from today's; `ATLAS_SUN_SHADERS` = the RDEF's 12. Controls: today's with the game's atlas 1,486,680 apart (871,016 with the traces' constants), no rebind 1,486,680; the baked map lit too 819,478 (0 with the traces' constants: their test pixels lie within 192 m).
- **Fixed: RT/PT left GT-VBAO on under its Path-Traced AO.** The page's default AO is GT-VBAO and RT/PT turns the paths on without touching it, so GT-VBAO ran hidden (the AO method reads Path-Traced) and the paths' AO multiplied its shading. The AO method's own Path-Traced choice puts the game's SSAO under the paths here (the rays multiply it, `graphics_page.hpp` `ChooseAo`); RT/PT now does the same.
- **Fixed: Torch and Fire Light in Fog (Experimental) started at 1**; it starts off now, as Fog Shadows (RT/PT turns both on).
- **Checked: every button of the page** (page dump `tools\tw\pharaoh\build_dump.py v55`, its v54 section; DLSS on; pressed from the defaults, after RT/PT, after No RT/PT and with every row of the page off its default). Reset leaves nothing of ours running and brings back the game's contact march, SSAO, SSR, shadow maps and fog march, every row of the page at its default (Shadow Map Size and Log Details stay, by design). No RT/PT runs Bend, GT-VBAO and GT-VBGI; RT/PT the rays' shadows, bounce and paths, the torches' shadows and the fog's. No button writes a hidden row that runs: the hidden rows the defaults hold (Smoke Shadows and its quality, Sprite Soldier Shadows, the three sway rows, Bend's Light Through Banners and Coloured Shadows) have nothing to act on here (no smoke or sprite hook, grass and leaves captured as drawn, no banner pass). RT/PT's banner, sheet, reflection and water values have no row here and are passed by; the AO and GI section presets show only with GT-VBAO and GT-VBGI. The dump now installs what AttachAddon installs (the point lights with Bend's torch rows, far trees, the fog, Ray-Traced Shadows Only): v54's dump left out Bend's Torch and Fire Shadows and Far Tree Shadows, rows the game has shown since v50 and v52.

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64` (the shared `graphics_page.hpp`, `rt_scene.hpp` and `rt_core.hpp` changes: the tw-port-checklist x64 list and build32 Attila, Rome II, Thrones built too). Manual check: a foggy battle with DLSS and Fog Shadows on (Log Details on): ReShade.log has "First run: Fog Shadows: the fog's in-scattering 598C59F3 drawn with the rays.", the fog loses its glow in the shadows of walls, trees and hills, and with the row off the fog is v53's. A night siege with Torch and Fire Shadows and Torch and Fire Light in Fog: the fog glows around torches. Ray-Traced (DXR) shadows with Rays Replace Shadow Maps: ReShade.log has "First run: Rays Replace Shadow Maps", Softness shows in full, water and rivers keep their shadows, Shadow Map Size hides; with Skip the Shadow Maps too: faster with many units, water loses the sun's shadow, nothing stale; both off: v53's shadows. RT/PT: the AO method reads Path-Traced and GT-VBAO's rows stay hidden.

### Round v53 (2026-11-12, render scale, Ray Reconstruction guides, frame generation slot; stamped and installed 22:23 for the maintainer's test, not run in game)

- **Fixed: torches, fires and the game's AO below 100%** (DLSS, FSR or XeSS at a render scale). `scale_audit.py` and `scale_scan.py` on the data run's battle at 100 / 67 / 50 / 33% and the siege at 50%, every hit read: the game's own shaders are patched by `view_share.hpp` or take the camera's viewport the addon rewrites, but three passes of ours or the game's read the whole texture. The point light `fire02` (E318CE77, replaced since v50) rebuilt each pixel's world position from the whole target, so below 100% torches lit the wrong ground; it now takes the draw's share, as the game's particle lights 5AD46115 already did. The AO generate (`ssao01` D42C3523, and the addon's Visibility Bitmask and upsample built from it) read the depth at the viewport's uv over the whole texture; it now reads the share's corner, with the share's pixel size. The game's two AO blurs (7F637259, BFD285BD) read their input the same way and are now `RENDER_SCALE_READS`. The rest hold: the particle shader 3F1B86E1 (replaced) uses its NDC only through the inverse projection's depth column, which doesn't depend on x and y; the SSR compute and the terrain depth copy use the game's viewport mapping; the god rays' bright pass 30B7AD3C (unchanged) reads the depth's corner through the viewport the addon rewrites. The light and AO shaders' constants are in the frame trace now, to show the game keeps their 100% values. WARP (`tools\tw\warp_tests\pharaoh\scale_v53`): the picture in the top-left of a target twice its size against 100%, 0 values apart for `fire02` (98,304 values, 12,040 pixels lit), the game's AO (125 of 3,072 pixels occluded; v52's replacement 0 apart from the game's own bytes), the bitmask AO (1,955 occluded) and both blurs (10,980 values, 3 texels from the edges; within them 13 and 4 of 1,308, largest 0.054: they read past the picture where the game's sampler clamps at the target); 100%: 0 apart from v52. Controls: v52's at 2x 23,240, 3,382, 5,171, 4,216 and 4,237 apart. God rays at 50%: 260 of 2,880 apart (their 4 taps a render pixel apart), the whole depth read 1,956.
- **Ray Reconstruction's guides are Pharaoh's own materials** (they were the bare specular colour). Read from the sun shader F883F5B2's bytes: the specular albedo is the weight the sun puts on its sky reflection (the specular colour with its Fresnel at this view, toward 1 on rough surfaces less, and its visibility), the diffuse albedo the game's ambient (albedo x (1 - specular colour)), the roughness 1 - gloss (that reflection's), emissive pixels neither. Water writes its G-buffer after the sun draws (water_troy 833E7963, flags 64, albedo 1, gloss 1), so the guides are written again after it, at the sky dome 9CF980D0 (once a frame, after the water, in every trace), from the sun draw's channels: on water no diffuse, the water's own Fresnel 0.02 and a roughness of at least 0.01 (Softer Water Reflections' floor, as Warhammer III; its row stays hidden here: it needs ray-traced reflections, which Pharaoh doesn't have). Shared `twattila/anti_aliasing.hpp` keeps the sun draw's channels for the frame (`RewriteSunGuides`; Attila and the others unchanged). Troy keeps v52's guides (its own copy, byte for byte the same build). WARP against the game's bytes of F883F5B2 with all six traces' cameras and lighting (`tools\tw\warp_tests\pharaoh\guides_v53`): the specular albedo against the sun's reflection of an environment of 1, 0 of 81,522 values apart beyond 2e-4 (largest 1.1e-4, the view ray rebuilt from depth 0.5); the diffuse albedo against its ambient of a cube of 1, 0 of 41,292 beyond 2e-5; emissive pixels 0 apart (the sun 4 x the albedo whatever the sky); normals and roughness 0 apart from v52; water 0 apart from 0 / 0.02 / 0.01 (0.06 at that floor). Controls: v52's bare specular colour 68,847 apart, the albedo alone 41,280, v52 on water 86,010.
- **Checked: frame generation's and Reflex's settings register is right (b13).** The RDEF of all 3,156 dumped shaders (`tools\tw\cbuffer_slots.py`): the game binds b0 to b8 (b8 in 88 of them), so b9 to b13 are free. The addon's settings are at b13 (`shared.h`, the shader and swap-chain modules' constant buffer index), frame generation's swap chain shader reads them there (`frame_generation::Install`'s slot 13, the proxy shader built from Pharaoh's `shared.h`), its HDR10 shader reads no constants, Reflex none; the addon's own shaders use b9 (torch lights), b10 / b11 (cameras), b12 (render scale). Nothing to change.
- **Fixed: v52's addon unload** (the game's exit, ReShade unloading its addons). v52's contact-shadow cleanup at the addon's end called the device handler with no device, which then asked that missing device for its API: a crash. Now no device means the addon's end, and the copy is dropped (read from the code; not run).
- **Tests: the shared code's lifetime suites pass** on WARP and the CPU, amd64 and x86 (`test\tw_*\run.ps1`, their default runs): `tw_depth_levels`, `tw_device_loss`, `tw_fg_initialization`, `tw_gpu_completion`, `tw_helper_lifetime`, `tw_preset_writer`, `tw_sampler_lifetime`, `tw_screen_space_shadows`, `tw_startup_worker`, `tw_texture_lod_bias`, `tw_unit_pack_rounds`, and `tw_motion_record` (x86 only). Not run: `tw_native_hook_lifetime` and `tw_particle_history` (code Pharaoh doesn't include), `tw_game_gi` and `tw_shadow_map_size` (Warhammer III's), `tw_psychov35` (Attila's and Warhammer III's shaders).

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64` (the shared `anti_aliasing.hpp` change: the tw-port-checklist x64 list and build32 Attila, Rome II, Thrones built too). Manual check: DLSS at 50% in a siege: torches and fires light the ground under them as at 100%, the game's SSAO (Visibility Bitmask off) sits in corners and under units as at 100%, the SSR on water as at 100%; Ray Reconstruction on in a battle by a river: the water's reflections steady, stone and metal no blurrier than without it; quitting the game ends with no crash.

### Round v52 (2026-11-12, review fixes; stamped and installed with helper105 for the maintainer's test, not run in game)

- **Fixed: Sprite Soldier Shadows is hidden.** It gated only flat soldiers turned to the sun, and both sun-facing entries here are far tree cards (97C53219, EB2A56A1: Far Tree Shadows' row), so it did nothing. No trace drew sprite soldiers. Shared `twattila/rt_scene.hpp`: the row shows only where a sun-facing entry isn't a far tree (Attila unchanged; Warhammer III hides it too).
- **Fixed: Torch and Fire Light in Fog and Smoke is hidden.** It reaches the picture only through the fog shadows (Pharaoh has no fog hook) or through lit particle shaders that read the lights' glow (none of Pharaoh's does: `fire02` reads only the traced bits). Shared `twattila/rt_scene.hpp`: the row shows only where fog shadows or that particle hook are installed (Attila, Thrones and Warhammer III unchanged).
- **Fixed: v51's contact-shadow copy at a device's end.** Any D3D11 device ending (not only the game's) dropped the copy and the game's lighting buffer held for the draw, so one ending between a sun draw and its end would have left that sun draw's slot empty for the rest of the frame. Now only the copy's own device drops it, and the game's buffer is always bound again after the draw.
- **Fixed: alpha-tested buildings and props cut out in the rays** (correction to v49: its mask slots did nothing, every building material went in solid). 1ACBD907's four alpha-tested materials (rigid_default_alpha, its colorise and Pharaoh's two: 246D4945, AC279A1B, B50D4F41, B87E0393) discard where their diffuse map's alpha is below 0.5, the rays' own cut-out, so their draws are now captured with the UV (`rt_capture_rigid_default_cut_out`, as Attila's rigid cut-outs) and cut out by that map (`CUT_OUT_MASKS`, the shared `clip_cut_outs` path Warhammer III's buildings take). The three blend-mask walls and terrain blends stay solid: their test can't fire on a texture's 0-1 values. WARP on the game's bytes (`tools\tw\warp_tests\pharaoh\cut_out_v52`): stream out world and UV 0 apart from the game's; each of the four, the game's draw against the captured triangles with the rays' test, 0 of 3,310 texels apart; controls: today's solid capture 1,683 apart, the other UV pair 2,062, the shadow pass's 0.2 cutoff 1,214; the blend masks keep all 3,310 (a float mask outside 0-1: all 3,310 discarded). Out of view (Shadow Casters Out of View) these buildings stay solid. Off with Leaf Micromaps off, as other cut-outs.
- **Fixed: `[renodx] FrameTrace=1` at 100% with Anti-Aliasing Off.** Its 600 presents of the 3D view counted only frames the anti-aliasing ran in, so with it off the trace never started (the `renodx_trace_now` marker worked). Now any frame the tone mapper draws counts, with any anti-aliasing or none (shared `twattila/anti_aliasing.hpp`; only Pharaoh traces at 100%).

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: Shadows' More Options with Ray-Traced (DXR) shadows show no Sprite Soldier Shadows and no Torch and Fire Light in Fog and Smoke row; a siege with Ray-Traced (DXR) shadows and Log Details on: ReShade.log has "cut out: vs 1ACBD907 with ps AC279A1B by its t0" (or one of the other three) and the rays' shadows of alpha-tested props show the holes the props show on screen; Anti-Aliasing Off with `FrameTrace=1` writes `tw_frame_trace.txt` after about 10 s of a battle.

### Round v51 (2026-11-12, one method each: the game's contact shadows and SSEO; built only, not run in game)

- **Fixed: contact shadows drawn once.** The game's sun shader marches its own short contact shadows toward the sun (`g_contact_shadows`, 16 steps at ATN's settings in every data-run trace), and Bend's screen-space shadows and the ray-traced shadows drew theirs on top of it. Now a sun draw where Bend's or the rays' shadow is bound (t49) reads a copy of its lighting constants with the game's march off (byte 4 of lighting_VS_PS), and the game's constants are bound again after the draw. With Game's Shadow Maps, or while the rays aren't ready, the game's own contact shadows stay: its bytes and constants untouched. Contact Shadow Quality (Graphics 2 of 2) acts only then; its tooltip says so. WARP on the game's bytes of all 25 sun shaders with the battle's, the siege's and the SSAO-off run's constants (`tools\tw\pharaoh\sun_test`, 6 scenes each): the flag gates the march alone (read once, by its `if_nz`; with it 0 the march's own inputs change 0 values), ours off 0 values apart from v50 and from the game, ours on 0 apart from the game's constants with the flag 0; controls: the flag reaches 91,113 values, today's stacked march 84,735, a copy at the wrong byte 1,620,708.
- **Kept: the game's screen-space environment occlusion (SSEO), under every method.** Read from its bytes, it works on the sun shader's reflection of the sky cube alone: along a glossy pixel's reflection it looks for what on screen blocks the sky, and puts that thing's lit colour there (quality 4, which in-game SSAO 3 gives) or darkens the reflection (quality 1 and 2). GT-VBGI, the ray-traced bounce and path tracing add only diffuse light (the albedo times the bounce), and our AO takes the game's SSAO slot, which the game itself multiplies into that reflection, as before. So nothing of ours does SSEO's work twice, and without it the sky would show in reflections where a wall or a soldier stands. WARP on the game's bytes with the traces' constants, all 25 sun shaders: with the specular coefficient 0, SSEO 4 against 0 changes 0 values (CONTROL with it 1: 6,094), the colour against darkening alone (quality 2) 12,058. Under Path-Traced AO the colour SSEO puts there is still lit by the game's sky: its ambient patch leaves SSEO alone (6,092 values apart from the cube zeroed by name). The double-work table (`tw-port-checklist` `references/ao-gi.md`) has Pharaoh's rows.
- **Reset gives the game's effects back at once.** The page's Reset (or Shadows on Game's Shadow Maps) turns Bend and the rays off, so at the next frame's sun draws no shadow of ours is bound and the game's contact shadows run again; SSEO was never off. Nothing is kept across frames: the copy is made in each draw and the game's own constants are bound again after it, so even a game that rebinds nothing it thinks bound reads its own. Every button in the page dump, pressed from the defaults, after RT/PT and after No RT/PT (DLSS on): only No RT/PT (Bend) and RT/PT (the rays) turn the game's contact shadows off, the page's Reset turns them back on from both, the HDR, Graphics 2 of 2, AO and GI Resets leave the Shadows method as it was. WARP: a frame with ours, then a frame after Reset with nothing bound again, 0 values apart from the game's (91,113 without the rebind).

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: a battle with Log Details on and Shadows on Bend Screen-Space: ReShade.log has "[Total War] First run: the game's contact shadows off in sun draw F883F5B2 (ours shadow it)."; soldiers' feet and wall bases take Bend's contact shadow alone. Shadows on Game's Shadow Maps: the game's contact shadows as in v50, and Contact Shadow Quality changes them. RT/PT, then Reset mid-battle: the game's contact shadows are back in the next frame. In-game SSAO 3 against 0, under every method: glossy stone and metal reflect what stands next to them with SSEO, the sky without it.

### Round v50 (2026-11-12, casters out of view, point lights, 3D UI below 100%; built only, not run in game)

- **New: Shadow Casters Out of View (Experimental, off) and Hills Out of View (Experimental, off)** under Shadows' More Options with Ray-Traced (DXR) shadows. Trees, buildings, rocks and units the sun's shadow pass draws outside the view (behind the camera too) go into the ray-traced scene, so the rays near the screen's edges see them. Hills Out of View adds the shadow pass's terrain. The game's shadow maps already shadow from behind the camera here, so these rows are for ray-traced AO, Global Illumination and torch rays. The shadow pass's eight vertex shaders (terrain 67E28AF9, rigid DC5FE525, units BC8DA38B / 731EF7DD / 039A9620, branches 5AFB107C, leaves A709F459, far cards EB2A56A1) keep their own constants. A geometry shader (`rt_outside_view.gs_5_0.hlsl`) keeps the triangles outside the main view and streams out the world positions those shaders already write. Leaves and far cards keep their UV for the cut-out. Only the per-frame 16384 x 16384 atlas counts; the kept 4096 x 4096 baked map doesn't. Shared: `twattila/rt_core.hpp` `Host::cascade_world` / `Host::cascade_target` (Warhammer III unchanged) and `graphics_page::Bindings::units_out_of_view_tested`.
- **New: Torch and Fire Shadows (Experimental, off)** under Shadows' More Options with Ray-Traced (DXR) shadows. Torches and fires cast ray-traced shadows. With Path-Traced Global Illumination they also light the paths, at the light shader's own diffuse scale. The point light draw (VS E19549A1, PS E318CE77) comes after the sun draw. Its instances are read as Warhammer III's: POSITION2 gives position and reach, POSITION1 gives colour. `fire02` multiplies by the light's traced bit. With nothing bound it draws v49's picture to the bit. Its light rows (Softness, Lights, Range, Light in Fog and Smoke) show only with it on. Bend's Torch and Fire Shadows (Experimental, Off) under Bend Screen-Space's More Options now has the light draws too. RT/PT turns the ray-traced row on, as in Warhammer III.
- **New: Log Details' light count.** One line, `[Total War] Light draws in a frame: point lights (E318CE77) N draws, N instances, N indices; particle lights (5AD46115) ...`, the first frame either kind draws and again the first frame point lights draw.
- **Fixed: unit markers and the other 3D UI sprites below 100%** (DLSS, FSR or XeSS at a render scale). They draw at the display's size after the tone mapper (VS 45A3B2DF). They test against the scene's depth, but at a render scale that depth holds the picture in its top-left corner only, so hills and walls hid them in the wrong places. The depth is now stretched for them first, as in Attila (`FULL_DEPTH_SHADERS`). In a battle one of their draws tests against the depth's copy, so that copy is stretched too (shared `twattila/anti_aliasing.hpp` `Install`'s `full_depth_copy`, off in the other games).

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: a battle with Ray-Traced (DXR) shadows, Ray-Traced AO and Shadow Casters Out of View on. A wall or tree just off the screen's edge darkens the ground next to it as it does on screen. Turning the row off and on causes no stutter and no missing shadows. In a siege with Log Details and Torch and Fire Shadows on, ReShade.log has the light-draw line and "Local lights: first N read from the light draws", and no "input layout has no POSITION1 and POSITION2" line. Walls near torches cast their shadows. With DLSS at 50%, the unit markers hide behind hills and walls as they do at 100%.

### Round v49 (2026-11-12, ray-traced scene tables; built only, not run in game)

- **Fixed: buildings and props no longer go into the ray-traced scene twice.** Their vertex shader (1ACBD907) also draws the depth prefill, the sea and some effects; only its 21 G-buffer pixel shaders are in the scene now, each with the texture its cut-out tests. The sea, the spline borders and the ash no longer cast ray-traced shadows.
- **New: far tree cards cast ray-traced shadows** (the Far Tree Shadows row, now shown, on by default), turned to the sun as the game's own shadow pass turns them. The far forest's blocks stay out: the game gives them no shadow either.
- **New: cloth and flags cast ray-traced shadows**, moving with the wind as drawn, and colour sunlight through them with Coloured Shadows. Tree branches are now taken as drawn every frame (they blend two placements).
- **New: DLSS / DLAA / FSR / XeSS motion for banners, cloth and flags** (they moved with the camera only before).
- **Fixed: the tables lost five entries this game never draws** (the campaign oceans of Troy and Warhammer II and an older terrain-blend shader: here the water and the terrain-blend props are drawn by other shaders).
- Not changed: the fog, smoke, point light and shadow-reader hooks. Their shaders are in the census (fog 598C59F3, particles 3F1B86E1 / 740B2536 on 109B79CE, sea 833E7963, river 4CA0D9CC), but none of them reads the rays' slots, and here the rays multiply the game's shadow maps, so a hook would do nothing until the replace mode, the fog march and the particle layout are done.

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: a siege with Ray-Traced (DXR) shadows and Log Details on: ReShade.log's "left out: vs 1ACBD907 with ps ..." lines name only the prefill, sea and effect pixel shaders (6FDC1F3D, 833E7963, 0051B762, 1B1F50CA), far trees' cards shade the ground at a distance, cloth and flags cast moving shadows, the sea casts none; with DLSS, waving flags and banners stay sharp.

### Round v48 (2026-11-12, page fixes; built only, not run in game)

- **New: the full-download note on General** (as Attila and Warhammer III): without renodx-tw-dlaa64.exe installed, a red line says DLSS, FSR, XeSS, Frame Generation and ray tracing need the full download from GitHub.
- **New: GitHub (Downloads)** in About, beside Ko-fi and NexusMods: https://github.com/arshiatn/renodx/releases.
- **Fixed: Shadow Map Size stays on Graphics 1 of 2 with Ray-Traced shadows.** Here the rays' sun shadow multiplies the game's shadow maps instead of replacing them, so their size still matters (`graphics_page::Bindings::rays_multiply_maps`, on for this game only; Attila and Warhammer III keep the row hidden under rays).
- **Fixed: Graphics 2 of 2 tooltips without words in capitals** ("their detail DISTANCES grow", "cast SHADOWS"; the shared `twattila/engine_tests.hpp`, every Total War add-on).

Download: https://github.com/arshiatn/renodx/releases (the full download, with renodx-tw-dlaa64.exe for DLSS, FSR, XeSS, Frame Generation and ray tracing).

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: without the helper, General shows the red full-download line and About's GitHub (Downloads) opens the releases page; Graphics 1 of 2 with Shadows on Ray-Traced (DXR) still lists Shadow Map Size.

### Round v47 (2026-11-12, graphics page, tests page, sun facts; built only, not run in game)

- **New: Graphics 1 of 2 laid out as Attila's and Warhammer III's.** Anti-Aliasing and Upscaling, Shadows, Ambient Occlusion, Global Illumination and Ray Tracing Extra Options, each with a Method slider and only that method's rows, the rest under More Options. Every setting keeps its saved value. Shadow Map Size moved here from Graphics 2 of 2 (same sizes).
- **New: Reset, No RT/PT (Recommended) and RT/PT buttons** on Graphics 1 of 2. RT/PT is ATN's Attila setup, and the ray-traced rows start at its values (the methods themselves stay off until you choose them).
- **New: FSR and XeSS** in Anti-Aliasing beside DLSS, and Attila's TAA Motion Vectors, Frame Generation Method, Ray Tracing Denoiser (NRD by default) and AO under Ray Reconstruction rows.
- **Fixed: Bend's screen-space shadows no longer run under ray-traced shadows** (the Shadows method picks one of them, as in Attila).
- **New: 24 of Attila's tests on Graphics 2 of 2**, every one whose values this game has: Tree LOD Scale, Projected Tree LODs, Far Tree Shadow Shapes, Object Detail Distances, Object Size Factors, Big Building Detail, Rope Distance, Objects / Buildings Never Vanish, Building Distance Culling, Building Detail by Size, Fixed Building Detail Range, Shadow Slope Bias, Sprite Soldier Shadows, Cutscene Point Light Shadows: High, Sort Particles, Shared Pose Scale, Fast Soldier Culling, All Soldiers as Sprites, Campaign Cloud Distance, Campaign Trees / Plants Never Vanish, Campaign Sea Transparency, Campaign Sea Pattern Scale.
- **Fixed: 11 rows that did nothing are gone** (their values don't exist in this game): Far Buildings, Building Collapse Effects, Building Collapse Animations, and More Tests' vegetation_create_default_soft_collision, CLOTH_FLAG_COMPOSITE_TEXTURE_OFFSET_X / _Y, ENTITY_DISPLAY_ENABLE_ALPHA_CHANGE, FLAG_DISPLAY_REGISTRATION_ENABLED, texture_manager_drop_extra_campaign_entity_mip, BATTLE_VISIBILITY_HEIGHT_OFFSET, enable_rendered_visibility. Every value left on the page is found in Pharaoh.exe (checked offline).
- **New: Attila's two Graphics presets**, "With my 4x / 8x unit sizes" and "With vanilla unit sizes". Reset and the presets also put every test back to the game's.
- **Fixed: Blood Build-Up, Campaign Sun Camera Depth, Shadow Cascade Blend and Rope Distance** tooltips give this game's values, not Attila's.
- **New: Path-Traced AO (Experimental)** in Ambient Occlusion, with Own Sky Rays and Steady Bounce Light: the rays now read the game's ambient light, which path-traced Global Illumination also takes as the sky's light.
- **Fixed: the rays' sky light no longer changes with the clouds' position** (they read a value that is Attila's ambient scale but this game's cloud shadow position).
- **New: AMD and Intel GPUs** see no DLSS-only rows, and a saved DLSS starts as FSR (XeSS on Intel), as in Attila.

Build: `cmake --build build --config Release --target renodx-twpharaohdynasties.addon64`. Manual check: Graphics 1 of 2 shows the five sections with their Method sliders and your old values; ReShade.log's "[Total War] Graphics: Engine values found: N of N" has no "not found"; with Path-Traced AO the ground under open sky stays about as lit as with the game's ambient.

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
