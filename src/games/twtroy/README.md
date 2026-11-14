# A Total War Saga: TROY (RenoDX HDR, ATN)

### Parity round (2026-11-14; built only, not run in game)

Parity table against Attila's page: `tools/tw/troy/parity_1114.md` (have 20, done now 4, needs a trace 6, won't get 12 with
proofs, open 7).
- **Fixed: Graphics 2 of 2 never changed anything.** Troy copies each engine value's name into a heap string, so the
  shared finder found none of them, and four audio tests that never resolved held every row back. Troy's own finder
  (`tweak_objects.hpp`) reads its 167 values: Terrain Shadows, Campaign Sun Camera Depth, Grass Cell Distance, Grass Height
  Distance and 17 More Tests work; the 384 rows whose values Troy doesn't have are gone. `test/tw_troy_tweaks`: 167 of 167.
- **New: Depth of Field** (Extra, 0-2): the blur's radius in the game's four DOF tone mappers; 1 the game's.
- **New (Experimental, off): Smoke Shadows without rays** (Attila's): smoke dims the sun in the game's shadow maps.
- **New: Fog Strength** (Extra, 0-2): the battle fog's thickness; 1 the game's bits (`test/tw_troy_shaders`, WARP, also with
  the game's MSAA and below 100%).

Test list (in-game AA off, DLSS):
1. Log: a "Graphics:" line at the first battle frame.
2. Grass Height Distance 2, next battle: full grass farther away.
3. Terrain Shadows Off, then On: hills stop and start casting.
4. Depth of Field 0 and 2 with the in-game DOF on: sharp, then a wider blur; 1 as before.
5. Fog Strength 0 and 2 in a hazy battle: no fog, then thicker; 1 as before, also with the game's MSAA.
6. Fog Strength 2 at DLSS Performance: the fog lines up with the scene, no band at the screen's edge.
7. Smoke Shadows (game's shadow maps, rays off) in a burning siege: smoke dims the sun under it, no flicker.
8. Any More Tests row that crashes or does nothing: name it.

Not done: VFX Brightness (Pharaoh's 2,775-line rebuild of its particle shader, again on Troy's twin 563BA3E8);
Shadow Map Size (Pharaoh's code patterns are not in Troy: find Troy's shadow atlas size in the decrypted exe first);
Particle Resolution (Attila's layer expects one vertex a particle: check it on Troy's instanced quads); campaign
vegetation 04C7C807 in the rays (a new capture) and the campaign's terrain shadows out of view (cascade_keep rows); the
leaves' cut-out at the game's 0.01 (a per-row cut-off in the helper, shared); banners and sails, campaign trees 07321725,
fur, displacement meshes and sprite soldiers in the rays (need a trace: a naval battle, a campaign region with those trees,
a battle with fur and with sprites). Won't get (proofs in the table): Audio Memory, Faster Game CPU Code, the unit / building
/ campaign detail hooks and values, 50 test rows, Fog & Sky Smoothing, Modded Auto Exposure, Attila's SSR rows.

### v54 (2026-11-14: RT/PT as Attila's; stamped and installed 10:45 with renodx-tw-dlaa64.exe helper112, not run in game)

- **RT/PT now matches Attila's preset** (ATN 2026-11-14; page dump against Attila's): Far Tree and Smoke Shadows and Water Caustics on, Light Through Foliage 0.5 (rests under the rays); of this game's own rows Shadow Casters Out of View on (as Warhammer III's), Rays Replace and Skip the Shadow Maps and Hills Out of View off. Defaults, labels and No RT/PT as before.

### Round v53 (2026-11-14, stamped and installed 07:50 with renodx-tw-dlaa64.exe helper112, not run in game)

- **New (Experimental): Water Clarity and Water Caustics** (under Reflections and Water). Clarity 1 is the game's look; higher lets you see deeper into rivers, lakes and the sea. Caustics (off; battles only) adds rippling light on the bed. They use Pharaoh's rows on Troy's 68 water shaders (every sea, lake and river variant). WARP (`tools/tw/troy/water`, Pharaoh's water test on Troy's bytes): all 68 give the game's bits at the defaults; Clarity and the caustics match the game with its opacity or refraction scaled (worst 4.8e-7). Every control moves.
- **New (Experimental, off; on in RT/PT): ray-traced Water Reflections.** Rivers and the sea reflect what the rays find, also off screen, in place of the game's screen-space reflections. Pharaoh's hooks are now shared code (`twattila/water_reflections.hpp`, Pharaoh unchanged), on Troy's twins of the same chain, and Troy's reflection shader got the rays in Pharaoh's form. WARP (`tools/tw/troy/water/ssr_rays`): with no rays bound it gives the game's bits at 100%, 50% and 67%. With the rays it matches the expected blend (worst 3.4e-6), including below 100%, where it reads the render pixel. Specular Occlusion halves the game's cube to the bit. Every control fails.
- **New (Experimental, in Torch and Fire Shadows): the fires cast ray-traced shadows.** A burning siege's fires light the scene through the game's particle lights, which keep their lights in the particle list, not in the instances the torches use. The add-on now reads each fire's place, reach and colour from that list each frame (shared code: `twattila/rt_scene.hpp`, only Troy uses it), so their light stops at walls and units as the torches' does. WARP (`tools/tw/troy/fires`): with nothing bound the fires' shader gives the game's bits; with the rays it gives the game's light where the ray reaches and none where blocked, bit for bit, also at 50%; each fire's place, reach and colour read from 1,024 particles to the bit. Every control fails.

Test list (in-game AA off, in-game Screen Space Reflections on, DLSS, RT/PT preset):
1. A river or coastal battle, Reflections Ray-Traced and Water Reflections on: the water mirrors hills, walls and units, also those off screen.
2. The same with Reflections back on Game's SSR: the game's reflections, unchanged.
3. Water Reflections on, DLSS Performance: the reflections line up with the scene, no offset at the screen's edges.
4. Softer Water Reflections on, then Samples and Reuse: softer, less noisy reflections.
5. Specular Occlusion on: the sky's reflection darkens under bridges and next to walls.
6. Water Clarity 4 in a river battle: the riverbed shows deeper; 1 looks as before.
7. Water Caustics 1 in a battle by shallow water: light ripples on the bed; none on the campaign map.
8. Campaign map with Water Clarity above 1: the sea and lakes clearer, the coast unchanged.
9. The game's MSAA on: the game's own reflections stay (no black water).
10. A night siege with Torch and Fire Shadows on: walls and units block the burning buildings' light, the lit side stays as bright as before; no flicker while fires start and go out.
11. The same with Torch and Fire Shadows off: the fires light the scene as before.
12. Log: one "particle lights streamed" line at the first fire.

Not done: Light Through Banners on sails (no trace draws a ship); the leaves' cut-out at 0.5 (the game's 0.01, and its fade).

### Round v52 (2026-11-14, built only, not run in game)

- **New (Experimental, off): Smoke Shadows.** Smoke and dust dim the ray-traced sunlight, as in Pharaoh: Troy's particle shaders are Pharaoh's twins (the vertex shader reads the particle's orientation as halves; the lit pixel shaders bind their textures one slot higher). WARP (`tools/tw/troy/smoke`, Pharaoh's smoke test on Troy's bytes and ATN's constants): every particle's place and size 0 of 120 apart, its opacity 0 of 69 apart for both pixel shaders (worst 9.1e-7), the texture level 0 of 23; every control fails as it must.
- **Fixed: the rays no longer take the sea, lakes and road overlays as shadow casters.** The rigid default shader also draws them; only its three G-buffer materials are in the scene now (Pharaoh's form). Fences, palms and other alpha-tested props cast their cut-out shadow, not a solid card.
- **Fixed (Far Tree Shadows): campaign billboards under the shroud cast no shadow,** and the rays no longer unbind the shroud the game's next billboards read (cards could vanish with Far Tree Shadows on). WARP: a half-hidden shroud gives the game's cards, 960 of 1,536 corners hidden in both.
- **New: Log Details counts the point lights and the fires' particle lights** (Pharaoh's count, now shared code).

Test list (in-game AA off, DLSS, RT/PT preset):
1. Smoke Shadows on, a burning siege: smoke columns dim the sunlight on the ground under them; no flicker.
2. Smoke Quality Low and Maximum: the same shadows, Maximum with fewer holes.
3. A coastal or river battle with rays on: no shadow on the sea floor from the water's surface; roads not darker than before.
4. Fences, palm cards and other cut-out props: their shadows show the gaps.
5. Campaign map, Far Tree Shadows on: far forests cast; none from under the shroud; no billboards vanish.
6. Torch and Fire Shadows on at night: light stops at walls and units.
7. Campaign trees cast shadows that match the trees while the camera moves.
8. Bend or ray-traced shadows on: no dark halo near walls and units (the game's contact march off).
9. Log Details on: one light count line in the log, at the first night battle frame.
10. DLAA, then DLSS Performance: dark scenes keep their shadow detail.

Not done: the fires' particle lights in the rays (after the WH2 agent's point light reader change); Water Clarity / Caustics and ray-traced water reflections (68 twins of Pharaoh's water shaders, each row's operand read on Troy's bytes, then Pharaoh's water_v58 WARP; the reflections' hooks moved to shared code first); Light Through Banners on sails (no trace draws a ship); the leaves' cut-out at 0.5 (the game's 0.01, and its fade). Not needed: Fog Shadows (Troy's battle fog 018FED0C reads no shadow map), an SDR preset or settings revision (Pharaoh has neither for this; the SDR tone mapper and SDR output are there), Hue Clip measured on 0C12E608 (that tone mapper is the ACES fit to 4.8e-4: the default 80 is Pharaoh's row).

### Round v51 (2026-11-14, built only, not run in game)

- **New (Experimental, off; on in RT/PT): Torch and Fire Shadows.** The point lights (143A02ED, two instanced draws a frame in ATN's third session) cast their shadows from rays: the light shader replaced in Pharaoh's form (`shaders/pointlight00`), the light's place read from its 64-byte instances. WARP (`tools/tw/troy/point_light`): the built shader against the game's bytes at 100%, 0 of 393,216 values apart (7,773 lit; control 8,958 apart). The night siege's fires (particle lights 643FC156) keep the game's light: already patched for the render share (as Pharaoh's 5AD46115), no shadows from rays in Pharaoh either.
- **New: the campaign's trees cast ray-traced shadows** (54D2CFBD: the rigid standard capture with the UV, cut out by the leaves' alpha, the sway left out; Pharaoh's palms). WARP (`tools/tw/troy/trees`): 1.4e-08 apart from the game's place; with the sway on, 8.3e-04 (the control).
- **New (with Far Tree Shadows): the campaign's far tree billboards cast too** (DF8EAF69, turned to the sun as the battle's far trees; the game draws them no shadow). WARP: a level camera with the sun behind gives the game's own card, 8.4e-06 m apart; 30 degrees off, 1.68 m (the control).
- **New: one method for contact shadows.** Where Bend or the rays shadow a sun draw, the game's own 8-step contact march is off (Pharaoh's hook, now shared: `twattila/contact_shadows_off.hpp`, Pharaoh's behaviour unchanged). WARP (`tools/tw/troy/sun_test` v51 on the game's bytes and ATN's traces): all 25 sun shaders (the 12 skin ones have no march: nothing changes there).

Test list (in-game AA off, DLSS, RT/PT preset):
1. A battle with torches (by night or day): torch and fire light stops at walls and units (Torch and Fire Shadows on).
2. The same with it off: the game's torch light; no flicker either way.
3. The night siege's fires keep their light at every Upscaling Mode (Ultra Performance too).
4. Campaign map: trees cast shadows that match the trees, also while the camera moves.
5. Far Tree Shadows on, campaign map zoomed out: far forests cast shadows; zooming in, no doubled tree shadows.
6. Bend or ray-traced shadows on: contact shadows look as before at a distance, no dark halo near walls and units.
7. Rays Replace Shadow Maps on: the sun's shadows come from the rays alone; far terrain keeps the game's baked shadow.
8. Rivers and the sea at Ultra Performance: reflections line up with the scene.
9. Shadow Casters Out of View on: tree and leaf shadows at the screen's edge stay while panning.
10. DLAA, then DLSS Performance: dark scenes keep their shadow detail.

Not done: the fires' particle lights (643FC156) in the rays (their positions are in the particle list, not instances; rt_scene's point lights belong to another agent's change; Pharaoh has none); the billboards' shroud test (cards under the campaign shroud cast too); the leaves' cut-out at 0.5 (the game's 0.01, and its fade).

### Round v50 (2026-11-14, built only, not run in game)

- **New (Experimental, off): Rays Replace Shadow Maps and Skip the Shadow Maps.** The 12 sun shaders that read the per-frame shadow maps (the twins of Pharaoh's 12) take them as lit where the rays' shadow is bound, so the sun's shadow comes from the rays alone; the baked far map stays the game's. The hook moved from Pharaoh into shared code (`twattila/sun_replace_maps.hpp`, Pharaoh's behaviour unchanged). WARP (`tools/tw/troy/sun_test` v54 on the game's bytes, synthetic and ATN's trace constants): the 12 listed are exactly the sun shaders reading the atlas through s2; rays only = the game's with the atlas lit, 0 of 2,752,512 values apart; off again = today's; controls 1.26 M apart.

Test list (in-game AA off, DLSS, RT/PT preset):
1. Rays Replace Shadow Maps on: the sun's shadows come from the rays alone (Softness shows in full); far terrain keeps the game's baked shadow.
2. Skip the Shadow Maps on (with 1): the same picture, a little faster; turning both off brings the game's shadows back.
3. Rivers and the sea at Ultra Performance: reflections line up with the scene, rough water as soft as at DLAA.
4. Shadow Casters Out of View on: tree (and leaf) shadows at the screen's edge stay while panning.
5. DLAA, then DLSS Performance: dark scenes keep their shadow detail.
6. Path-Traced AO: the sky light looks like the game's ambient.
7. Coloured Shadows on: banner and sail shadows take their colour.
8. Ultra Performance: GT-VBAO / SSAO fill the whole view; the campaign shroud's edge lines up.
9. Rays on: grass and far trees cast ray-traced shadows (Far Tree Shadows on).
10. Light Through Foliage 0.5: leaf and grass shadows let some sun through; walls stay dark.

Not done: point lights / the fires' particle lights in the rays (a replaced 143A02ED reading rt_local_lights.hlsli: the session-3 reads give its 64-byte instances); campaign trees 54D2CFBD / billboards DF8EAF69 (a cut-out capture of Troy's rigid standard); the game's contact march off under ours (not moved into shared code: NOT DONE).

### Round v49 (2026-11-14, ATN's third session's traces at 50% / 33%; built only, not run in game)

- **Fixed: below 100% the water's and metal's screen-space reflections read the whole screen into the view.** The SSR apply 00645DA1 (its quad's TEXCOORD0 spans the viewport: the traces' vertex reads) now reads at the render share, as Pharaoh's twin (`shaders/ssrapply00`, one decompiler bug corrected). WARP: 100% the game's bytes to the bit; 33% = 100% (1 pixel in 6 scenes within 0.011: bilinear weights).
- **New (Experimental, with Shadow Casters Out of View): leaves out of the view cast ray-traced shadows** (Troy's leaf shadow VS writes no TEXCOORD4: Pharaoh's geometry shader gets `RT_NO_FADE`, Pharaoh's bytes unchanged).
- Facts from the traces: the AO, shroud and SSR fixes' shaders are drawn at 50% / 33% as the census assumed (now level A); point lights 143A02ED: two instanced draws a frame (7 lights in one, 64-byte instances) in the night battle and the campaign; the night's fires: particle lights 643FC156; the helper's RT log: static meshes in dynamic vertex buffers captured every frame (0.1-0.4 ms CPU).

Test list (in-game AA off, DLSS Performance or Ultra Performance):
1. Rivers and the sea: reflections line up with the scene at every Upscaling Mode (no stretched copy of the screen).
2. Rough water: its reflections as soft at Ultra Performance as at DLAA.
3. Shadow Casters Out of View on: tree shadows at the screen's edge stay while panning (leaves too, not only trunks).
4. DLAA, then DLSS Performance: dark scenes keep their shadow detail (Round v48's encoding fix).
5. RT/PT preset: battle and campaign render; Path-Traced AO's sky light looks like the game's ambient.
6. Coloured Shadows on: banner and sail shadows take their colour, not a blue-grey tint everywhere.
7. Ultra Performance: GT-VBAO / the game's SSAO fill the whole view; the campaign shroud's edge lines up.
8. Rays on: grass and far trees cast ray-traced shadows (Far Tree Shadows on).
9. Light Through Foliage 0.5: leaf and grass shadows let some sun through; walls stay dark.
10. FSR / XeSS on another GPU: no DLSS start; the page's three presets switch cleanly.

Not done: point lights and the fires' particle lights in the rays (a replaced 143A02ED reading rt_local_lights.hlsli, Pharaoh's fire02 form); Rays Replace Shadow Maps / Shadows Only (Pharaoh's OnSunReplaceMaps on Troy's 12 twins, then the sun test's v54 P-tests); the game's contact march off under ours (Pharaoh's OnSunContactShadows, best moved to shared code); campaign trees 54D2CFBD / billboards DF8EAF69 (a cut-out capture of Troy's rigid standard); the RT log of the new casters (session 3 ran the trace build).

### Round v48 (2026-11-14, Pharaoh's engine twin ported; built only, not run in game)

Proof: Troy's shaders are Pharaoh's sources in another build (same-name twins; a few the same bytes), checked on Troy's own bytes (RDEF, signatures, instructions), WARP where code runs, and ATN's 2026-11-14 traces (campaign + night siege, 100%, in-game AA off). Tools: `tools/tw/troy` (plan_1114.md).
- **Fixed: DLSS / DLAA / FSR / XeSS got the frame squared.** Troy's tone mapper 0C12E608 leaves a linear frame (WARP ramp on its bytes); the DLAA prepare / finish wrappers now take it linear (`TW_AA_FRAME_LINEAR`, Pharaoh's). Darks no longer crushed to 0 below 2.4e-4.
- **Fixed: Coloured Shadows tinted by the sun's direction.** All 25 sun shaders hold `sun_colour` at c3 (c1 is the direction): `sun_colour_register = 3` (WARP: 18 shaders with the filter, 0 values apart; the old register's control differs).
- **New: the paths' sky and sun light from the game.** `InstallSunDraws(..., 24, 12)` and Troy's `rt_sun` (no scale switch, the sun light x 0x389FB405): Path-Traced AO (ReSTIR GI) in the AO methods (WARP: the cube at float 24 in all 25).
- **Fixed: below 100% the game's AO and its blurs read the whole depth into the share** (Pharaoh v53 / v58 on the same bytes); view_share's viewport chains and the shroud decals' `inv_rt_size` as Pharaoh's (WARP 33% = 100% beyond the edges; census unclear 807 -> 1).
- **New: Graphics 1 of 2 as Pharaoh's** (Reset / No RT/PT / RT/PT, FSR, XeSS, FG Method, TAA Motion, LOD Bias on All Textures, NRD); every key kept.
- **New (Experimental, off): Light Through Foliage** (the G-buffer's bit 128: leaves, cards, blocks 128, grass 160), **Shadow Casters / Hills Out of View** (the shadow pass's casters: Pharaoh's geometry shaders on Troy's 9 shadow VS), **Far Tree Shadows** (the far cards turned to the sun).
- **New: the rays capture the drawn grass 2C7FBC7D** (its own capture) **and the far tree cards E25688ED** (Pharaoh's).

Build: `cmake --build build --config Release --target renodx-twtroy.addon64`. Test list (in-game AA off, DLSS):
1. DLAA, then DLSS Performance: dark scenes (night siege) keep their shadow detail; no banding near black.
2. RT/PT preset: battle and campaign render; the sky light (Path-Traced AO) looks like the game's ambient, not brighter or tinted.
3. Coloured Shadows on: shadows of banners / sails take their colour, not a blue-grey tint everywhere.
4. DLSS Ultra Performance: GT-VBAO and the game's SSAO fill the whole view (no top-left rectangle, no smear).
5. Campaign map at Ultra Performance: the shroud's edge lines up with the map.
6. Rays on: grass and far trees cast ray-traced shadows (Far Tree Shadows on).
7. Shadow Casters Out of View on: shadows of trees / buildings just outside the view stay when panning.
8. Light Through Foliage 0.5: leaves' and grass shadows let some sun through; walls stay dark.
9. Upscaling Mode / FSR / XeSS on another GPU: no DLSS start; the page's three presets switch cleanly.

Not done (what, why, smallest step): Rays Replace Shadow Maps + Shadows Only (12 twins; WARP P-tests first); point lights 143A02ED (one campaign draw; the night siege's fires are particle lights 643FC156, Pharaoh 5AD46115); the SSR apply 00645DA1 below 100% (Pharaoh's 926-line apply re-decompiled on Troy's bytes, then its WARP); leaves out of view (a TEXCOORD4-less RT_LEAF variant); campaign trees 54D2CFBD / billboards DF8EAF69 (Troy's rigid-standard cut-out capture); the game's contact march off under ours (byte 4, Pharaoh v51); water (68 twins), smoke, fog, InstallThin; SDR preset, SETTINGS_REVISION, Hue Clip measured on 0C12E608.

### Round v47 (2026-11-14, shared code from Attila / Pharaoh; built only, not run in game)

- **Fixed: the Ray Tracing Debug View's Screen-Space AO was magenta.** No AO pass hook keeps it here (ssao01 binds no camera), so it is now what each sun draw reads at its `t_ssao` (the game's SSAO or GT-VBAO, blurred), copied before the rays' AO and the AO filter take its place: `rt_scene::InstallSunDebugAmbient` (`twattila/rt_scene.hpp`, Pharaoh's hook moved there). Its mapping from the shaders' bytes: all 24 sun draws with an AO slot read it at `(SV_Position + g_vpos_texel_offset) x g_screen_size.zw`, as their G-buffer, which is the Debug View's own mapping (`tools/tw/debug_ao_v47/ao_mapping.py`; the same bytes in Pharaoh's 24, where a trace proved it; Warhammer III's read it through their overscans and fail the check). Page dump: the same but the Build Date.

Build: `cmake --build build --config Release --target renodx-twtroy.addon64`. Manual check: with DLAA or an upscaler and the helper, Ray Tracing Debug View on Screen-Space AO is no longer magenta: it shows the AO of the Ambient Occlusion row's method (Game's or GT-VBAO; white open, darker in corners and under units) lined up with the picture, also below 100% render scale.

### Round v46 (2026-11-13, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Fixed: the Ray Tracing Debug View rows never showed.** They wait for `rt_scene::OnTonemapDrawn`, which this add-on's tone mapper hook never called (Pharaoh had the same gap, 041821eb); now right after the anti-aliasing's, as in Attila. Its Screen-Space AO view stays magenta: no AO pass hook here, and Pharaoh's copy at the sun draws is not ported. Found by the skill's `scripts/static_checks.py`; the page dump (which sets the flag itself) is the same but the Build Date.

Build: `cmake --build build --config Release --target renodx-twtroy.addon64`. Manual check: with DLAA or an upscaler and the helper, the Ray Tracing Debug View row shows; its Sun Shadows view shows the rays' raw shadow as the picture.

### Round v45 (2026-11-12, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Bend Screen-Space Shadows: Auto Thickness removed** (ATN: it broke at some angles).

### Round v42 (2026-11-11, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Object Motion Vectors: each soldier keeps its own bones.** When the game packs its soldiers' bones into the animation pages more than once a frame (big battles), a later round's soldier at a reused row shared a key with an earlier one and could take its last-frame bones: false motion on still soldiers' shields, swords and spears under DLSS, FSR or XeSS. Keys are now this frame's and new after every page write (`twattila/object_motion.hpp`; the 64-page clear that gave page 65 page 1's key is gone). WARP `test/tw_unit_pack_rounds` (Attila's path; this add-on claims bones in the draw, so the keys are its fix).
- **Bend Screen-Space Shadows: Auto Thickness (Experimental, off)** (each object as thick as it is wide: fuller shadows behind walls and carts, poles stay thin) and **Softer Away from the Caster (Experimental, 0 = off)** (sharp at contact, softer farther away, by the sun's size). Every row off: today's shaders and picture (WARP `test/tw_screen_space_shadows`, to the bit).

Build: `cmake --build build --config Release --target renodx-twtroy.addon64`. Manual check: a paused big battle, the camera around the armies and back, Object Motion Vectors on: weapons on still soldiers stay still. Screen-space shadows with the new rows off: the picture as before.

### AO depth-level recovery (F15, 2026-11-09, GPT-6 Astra; not run in game)

Failed depth-level texture/view creation now releases all partial views and retries at the same resolution after a one-second cooldown. Build: `cmake --build build --config Release --target renodx-twtroy.addon64`. Manual check: use an AO mode that reads depth levels, change resolution/render scale, and verify stable AO; after a transient allocation failure, depth levels should recover without another resolution change. Failure injection and real D3D11 WARP readback results are in `docs/Attila-crash-audit-2026-11-08.md` at the repository root. No shader behavior was changed.

### Startup worker lifetime (2026-11-08, GPT-6 Astra; not run in game)

The startup audio-setting worker is now cancelled and waited for through its actual thread exit before unloading the add-on. Its progress flag alone did not cover capture and CRT cleanup. Manual check: unload/reload during the first minute with an audio-memory test enabled, then after it completes; no worker may execute unloaded code. Shared build and offline checks are recorded in HANDOFF.md.

Build: `cmake --build --preset clang-x64-release --target renodx-twtroy.addon64`.

## v36 (2026-11-12, PsychoV35 round; built only, not run in game)

- **HDR (PsychoV35)** (Rendering Mode 4): Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`). Hue Clip Emulation (0 - 100, default 80) toward the stock ACES fit on each channel (`HueTransfer`); default 80 (part 5: the ACES fit). Recommended in PsychoV35; Reset stays HDR (Extended).
- Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain.
- Other modes: unchanged code paths (not bit-checked on this game; Attila's and WH3's are).

## v35 (2026-11-05, built only, not run in game; renodx-tw-dlaa64.exe helper89, bridge v29: ship them together)

- **Shared helper:** cloth and sails can trace without shading themselves (WH3's Smooth Banner Shadows, batch 5); no row here, nothing changes.
- **Shared helper:** ray-traced reflections without path tracing read last frame's colour squared back under L, M and Ray Reconstruction, and the Light Bounce without paths fills their radiance cache (WH3's batch 5).
- **A Windows crash report at each helper start, gone:** ReShade next to renodx-tw-dlaa64.exe loads this addon into it too; the addon said no in DllMain and the runtime's cleanup then faulted inside the loader (ntdll c0000005 at 0x272c5). The helper lived on. Now it says no in AddonInit and ReShade unloads it (WH3's batch 5; same lines here).
- **The helper's log:** a real fault leaves one line and renodx-tw-dlaa64.dmp; whose device a GPU reset removed, with its reason and the time.
- **Path-Traced AO learns 150 frames before it starts, not 30** (WH3's batch 5: a battle's first frames gave 30 % less light). AO under Ray Reconstruction stays REBLUR, as before (Attila and WH3 have a row for it). Not run in game.

## v34 (2026-11-03, built only, not run in game)

- **Path Tracing with the game's or Bend's shadows**: the paths now get the sun's direction there too (as Attila's v135).
- **Frame generation with Output SDR**: the picture no longer turns red (as Attila).
- **Frame generation with Output scRGB**: works when turned on in game, no restart (as Attila). Ship with the helper built from
  this commit or later, not helper71.
- **Gamma Correction** (Rendering Mode SDR, on by default). On: any in-game Gamma shown as gamma 2.2, today's picture. Off:
  the game's own picture at the in-game Gamma. Rendering Mode SDR on Output SDR ignores Paper White (as Attila).
- **Extra in Rendering Mode SDR**: Bloom, Lens Flare, God Rays, Vignette, Distortion and Chromatic Aberration work there (at 1 the picture is
  unchanged). Film Grain and Indicators stay HDR-only. So does Sharpening: RenoDX's own, which stock SDR doesn't have.
- **Wrong constants from the decompiler** (checked against the game's own shaders):
  - In-game depth of field: the sharp range around the focus was about 30% too narrow (f x f, the game has 2 x f x f).
  - Battle sky's fog: thins toward its top as in the game (the bottom plane read the top one: a hard ceiling).
  - Branches and leaves in motion vectors and ray tracing: vertical wind strength (read the horizontal one).

## v33 (2026-10-27, batch 5, untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper22** (in the zip; bridge v11, needs this addon): **ReSTIR GI v2** (compared with
  Embark Studios' kajiya, MIT / Apache 2.0, not copied): fireflies clamped, history only on a surface facing the same
  way, and every third frame the reused light is checked again: a light switched on or off shows within 3 frames
  (was about 20). **Partitioned TLAS** fixed (its update operations stalled the GPU after BLAS compaction) and on
  again. **Mesh instancing**: instanced draws build one BLAS placed once per instance.
- **Light Through Leaves** (off by default, with Ray-Traced Sun Shadows): leaves let a quarter of the sun through
  each, cloth and sails about a third; only where the rays are the shadow (past the game's own shadow maps).

## v32 (2026-10-27, untested in game; the helper GPU-tested)

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

## v31 (2026-10-27, untested in game; the helper GPU-tested)

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

## v30 (2026-10-27)

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

## v29 (2026-10-27, untested in game)

- **Ray-traced sun shadows and ambient occlusion** (Experimental, Anti-Aliasing section, with DLAA / DLSS and
  renodx-tw-dlaa64.exe): Attila's (rt_scene.hpp) on Troy's 16 scene shaders (geom_new / custom terrain, rigid_default
  and 30 other materials sharing it, rigidstandard, texture blend (+ wind, 2 bones), vertex push, terrain blend,
  branches, leaves, 2- and 4-bone units with and without dismemberment); 4-bone units translated from fxc's
  disassembly. GPU test: 16 of 16 captures match (leaves compared projected: their capture divides by w).
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

## v28 (2026-10-26, untested in game)

- **Frame Generation (Experimental)** (Anti-Aliasing: Off / 2x / 3x / 4x, Reflex Boost; needs DLAA or DLSS), as Attila's
  v74: NVIDIA's DLSS Frame Generation in renodx-tw-dlaa64.exe (Streamline's sl.*.dll and nvngx_dlssg.dll next to it,
  STREAMLINE_LICENSE.txt), presenting into a child window over the game's; the HUD-less image from the tone mapper's
  output through the addon's swap chain shader. Needs an RTX 40 or newer GPU, a windowed or borderless game, Output
  HDR10 or SDR; turn the game's VSync off. GPU tests: 2 / 3 / 4 frames presented per rendered frame, the mouse goes to
  the game's window, the HUD-less image drawn without touching the game's bindings.
- Object motion: its target half the size (R32G32: the motion as two halves and the depth; 66 MB less at 4K) and its
  depth match absolute (4e-6, was 0.1% of the depth: surfaces drawn later in front no longer take an object's motion far
  away). twattila/object_motion.hpp now holds it (shared with Shogun 2's DLSS addon).

## v27 (2026-10-26, untested in game)

- Screen-space shadows fixed (ATN: lines, shadows pointing away from the sun): the shared code read the sun's direction
  from Attila's lighting buffer (b2, c0.y) and its camera's layout in every game. Each sun draw now names its own (from
  the game's shader dump: tools\tw\sun_draw_layouts.py): its lighting at b2 / b3, the sun direction at c1.x. WARP
  check (tools\tw\warp_tests\sss\sss_scene.cpp, the sun direction at c0.y / c0.x / c1.x): the same shadows.
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
  drawn smaller and DLSS upscales it; Ray Reconstruction's guides from its G-buffer (shared with Pharaoh: octahedral
  normal, gloss in channel 1's z, specular colour in channel 3). The screen-space shadows and the AO filter keep to the
  3D image's corner of the screen-size textures (Bend's RenderSize).
- DLSS / DLAA motion vectors for everything that moves on its own (as Attila's v69-v70: twattila/anti_aliasing.hpp
  object motion), 20 of the game's vertex shaders: grass, leaves, branches, campaign vegetation, units (2-bone, 4-bone,
  fur, dismembered), rigid objects, sails, ropes, both oceans. Made by tools\tw\attila_motion\port_motion.py
  (classify_motion.py picks them from the dump; 3Dmigoto's decompiler crashes on the 4-bone units:
  tools\tw\attila_motion\asm_to_hlsl.py translates fxc's disassembly, bit-exact); GPU check test_motion.py: every motion
  shader's SV_Position bit-identical to the game's, last frame's position as the game's shader at last frame's camera /
  time / placement; sails and ropes not checked (the test knows the cloth layout only).
- renodx-tw-dlaa64.exe (bridge v4, rebuilt): ray tracing BLASes compacted; switching between Ray Reconstruction and DLSS
  / DLAA frees the other's NVIDIA feature; the addon waits up to 5 s for a new NVIDIA feature (was 100 ms: changing the
  preset or Ray Reconstruction in game turned DLAA off).


## v26 (2026-10-26, untested in game)

- DLAA bridge v4 (Attila's ray tracing and Ray Reconstruction share it): this addon needs the renodx-tw-dlaa64.exe in
  this zip (an older helper refuses it, and DLAA stays off). Nothing else changes here: the shared anti-aliasing,
  screen-space shadow and motion code took Attila's additions without using them.

## v25 (2026-10-26)

- The DLAA helper (renodx-tw-dlaa64.exe) updated with Attila's DLSS render scale (its shared block v2: the output size);
  DLAA here works as before. Use this helper with this addon (an older one is refused).

## v24 (2026-10-26)

- Anti-Aliasing DLAA (NVIDIA's, RTX; ATN): NVIDIA's DLSS runs only in 64-bit processes, so a 64-bit helper
  (renodx-tw-dlaa64.exe next to the addon, with nvngx_dlss.dll; twattila/dlaa_helper, build.ps1) runs it on textures
  and a fence shared with the game (twattila/dlaa_bridge.hpp / dlaa_bridge.h): this frame linear in BT.2020, the
  scene's depth and the camera's motion (aa_dlaa_prepare: the TAA's jitter and cameras), NVIDIA's result back to the
  2.0 encoding (aa_dlaa_finish). The game's GPU waits for the helper's fence value only after it answered (no answer in
  100 ms, 5 s the first time while NVIDIA sets up: DLAA off, the frame as it is). DLAA Preset K / M / L. Motion is the
  camera's only: units, flags and swaying grass / trees can smear (their own motion vectors are the next step).
  Tested on the RTX 5090 without the game (tools\tw\warp_tests\dlaa\bridge_test.cpp, 32-bit: 117 of 120 frames
  through DLAA, output finite); not yet in game.

## v23 (2026-10-26)

- Attila's layout (ATN): Settings Mode HDR & Effects / Graphics 1 of 2 (Ambient Occlusion, Global Illumination,
  Screen-Space Shadows, Anti-Aliasing) / Graphics 2 of 2 (Graphics (Experimental) and the tests), Attila's colours
  (twattila/feature_settings.hpp).
- Screen-Space Shadows (Bend Studio's, twattila/screen_space_shadows.hpp): this game's shadow ends differently in each sun shader, so the patch scales every shadow map comparison
  (sample_c / gather4_c: the shadow is their filtered average) by the screen-space shadow. All shadowed sun
  shaders of the pack patch and create on WARP.
- Graphics 2 of 2: the executable is protected (its code cannot be read), so names and types come from Pharaoh's code (Troy's names are packed too: all of Pharaoh's):
  Attila's 16 and 1185 More Tests (unsure; a name the game lacks shows as not found). At-start tests as
  Attila. Engine value lookup is one pass for all names (a search per name took minutes with 1000+). engine_values.txt lists them.
- Bloom less blocky (Rome II's fix): the high passes average every source pixel a bloom texel covers (twattila/bloom_high_pass.hlsli; stock:
  one tap) and the bloom is read with a cubic B-spline over 4 x 4 texels (hdr_to_screen's glow and the DOF composites; stock: one bilinear tap).
- Engine values (new here): Graphics (Experimental) holds their status line.
- Sky Debanding (Pharaoh's, twpharaohdynasties/sky_deband.hlsli) on the battle skydome (0x934FE258), in Extra,
  default Off.
- No screen-space reflections added: the game's own (ssr_new: projected water reflections) stays.

## v15 (2026-10-24)

- Light Bounce with the game's MSAA (ATN: "support every AA"): its *_ms directional light draws hooked too (16: slm / skinlm, edges / interior,
  from the pack dump: each one's G-buffer slot and camera), the light buffer resolved for the trace, the prepare and the
  apply as *_ms builds (twattila/light_bounce_gbuffer.hlsli: Texture2DMS, the prepare at sample 0, the apply per
  sample as the game's MSAA lighting). FXAA / MLAA / off were already fine.

## v14 (2026-10-24)

- Ambient Occlusion Game's at its own cost (ATN): the replaced AO pass (the HBAO setting's ssao01) is the game's code only (#else of
  AO_BITMASK); the Visibility Bitmask is the addon's own pixel shader compiled from the same file (ao_bitmask.ps_5_0.hlsl: #define
  AO_BITMASK), which twattila/ao_pass.hpp draws in its place (the game's bindings, the addon's settings at b13) only
  when it is selected. Before, one shader held both paths, so Game's paid for ours.

## v13 (2026-10-23)

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

## v12 (2026-10-23)

- Game's ambient occlusion costs what it did again: the replaced AO pass is the game's code only and the Visibility
  Bitmask is the addon's own 8 x 8 compute shader dispatched in its place (twwarhammer2/line_sweep_ao.hpp
  DispatchInstead, the addon's settings bound at b13). Before, one 1024-thread shader held both (fxc X4714: fewer
  threads in flight even for the game's path). Here in place of the blur (ao_bitmask_blur); the collect never skipped
  (its replacement removed).

## v11 (2026-10-23)

- As Attila (ATN): Settings Mode "HDR & Effects" / "Graphics"; Light Bounce (Experimental, new section:
  twattila/light_bounce.hpp on the sun draws below); Ambient Occlusion and Light Bounce presets (Recommended,
  Recommended (heavier but looks better) with 4 AO directions, Reset) and Attila's tooltips; graphics presets without
  the AO; AO / Light Bounce titles light blue, two empty lines after Graphics. No AO Temporal Smoothing (its AO passes
  have no camera to reproject with).
- Sun draws: directionallight ps40 slm (5) / skinlm (4), camera b1, Pharaoh's decode
  (twpharaohdynasties/light_bounce.hlsli).

## v10 (2026-10-23)

- Settings Mode Image / Graphics (was Simple / Advanced; ATN): Image shows every tone mapping, grading, effect and
  output setting (the old Advanced-only ones too), Graphics only the graphics sections (Ambient Occlusion); About in
  both. IsGraphicsSettings and one loop at the end of ConfigureAddon.

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

## v6 (2026-10-22)

- Ambient occlusion Radius up to 10x (ATN).
- Ambient occlusion: the depth read over the part of its texture the game's blur uses (line_sweep_ao.hlsli
  depth_size), not the whole texture: Warhammer II's is larger, and its rest showed old frames (ATN's marks).

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

## v4 (2026-10-22)

- Ambient occlusion in its own section after Graphics, "Ambient Occlusion (Experimental, needs in-game SSAO)" (ATN),
  labels Strength / Radius / Thickness; defaults Strength 1, Radius 4x, Thickness 0.15 m (ATN). Advanced: "Ambient
  occlusion passes per frame" (twwarhammer2/ao_trace.hpp: counts the AO shaders' draws / dispatches; 0 = the game
  never ran them).
- Graphics (Experimental) and the AO section: Advanced only; both titles pink.
- PsychoV31 Highlight Hue Shift default 0.10 (ATN; Reset and the presets too).
- Graphics (Experimental, Advanced): "Save game code": the exe's image from memory (its protection decrypts the code at
  start) to Downloads\<exe>.memory.bin, to find engine values for new sliders offline (ao_trace.hpp SaveGameCode).
- HDR (Extended): the shoulder above 1 per channel (as Ryse fix32 / Rome Remastered v3; hdr_to_screen.hlsli): bright fire turns
  yellow / white at its core as in SDR, not flat salmon.
- Bloom (as Attila): the high pass's hard cap of the bloom source (luminance 25) is a logarithmic shoulder in HDR
  (bloom.hlsli; bloom00_0x8CBF5F17 / bloom01_0xE0C933D9): bright sources no longer all get the same flat halo. SDR unchanged.
- Visibility Bitmask AO: every depth sample's position at the centre of the depth texel it read (bitmask_ao.hlsli
  AoSamplePosition). Off-centre positions with a texel's depth made sloped ground occlude itself in a regular dot
  pattern (ATN's Warhammer II "matrix" pattern, debug1).

## v3 (2026-10-22)

- General: yellow note "Use these in-game settings: Gamma 22 and Brightness 10."
- Presets: Recommended: Reset, then PsychoV31 at its defaults and Video AutoHDR 0.20; Reset turns Video AutoHDR off.
- Extra: Video AutoHDR / Video Dithering (menuvideo00 and the blurred videos, menuvideo01_0xFBC512EF).
- Graphics (Experimental): Ambient Occlusion Game's / Visibility Bitmask (default; Strength = an exponent, Radius
  1 = 2 m, Thickness). The line sweep (0x39989DF1) is skipped, its depth and camera bound for the collect (ssao00_0x19CF3C83), which
  computes the AO (twwarhammer2/line_sweep_ao.hpp, line_sweep_ao.hlsli, bitmask_ao.hlsli: Therrien, Levesque, Gilet
  2023 on GTAO, as Attila's); the game's blur follows. Credit in About.
- The default AO (ssao_algorithm 1: ssao2.hlsl HBAO, ssao01_0xD42C3523): Visibility Bitmask with the game's radius,
  strength and limit by distance.
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
  other Total War mods (Musa's custom test31; Highlight Hue / Saturation Shift toward the SDR image's per-channel clip),
  on t00's scene-linear handoff (as PsychoV30).
- `hdr_to_screen.hlsli`: the helpers t01-t08 each carried (native grade bridge, Extended grading and shoulder) once,
  plus ApplyTroyPsycho (V30 / V31) and the SDR / HDR ends used by the photo passes.
- Shader packs (dump `bin\cmd_Decompiler-1.3.16\dump_shader_twtroy_pack`): photo01-04 = hdr_to_screen
  ps40_main_photo / ps50_main_photo_ms with and without distortion (photo mode had no HDR before), with the Film Grain
  and Distortion sliders; dof01 = dof00 for MSAA (ps50_dof_preprocess_ms: god rays / glow sliders, finite depth).
- Not needed: the god ray blurs' wide / rgba permutations (the mod's blurs are stock), hdr_tone_mapper's basic /
  constant / reinhard / passthrough tone mappers (Troy uses the ACES one, t00).
- About: credits, ATN's Ko-fi.

Verify: SDR / Extended / V30 / V31 in a battle and on the campaign map; photo mode (all four with MSAA on / off and
distortion on / off); DOF with MSAA.


### F1 helper lifetime repair (2026-11-09, GPT-6 Astra)

AddonUninit now shuts down the shared helper bridge after unregistering its users. The watcher exits before module unload; child process and IPC ownership are released. Shutdown drains outstanding GPU fence generations, with ordered CPU fence recovery only after confirmed helper exit. Failed launches clean up partial ownership and existing IPC sessions are not overwritten.

Offline checks: x86/x64 production-header tests cover process/thread cleanup, failure injection, 20 reloads with stable handle counts, and three interleaved D3D11/D3D12 WARP fence generations under normal completion and forced shutdown. No game was launched. Manually check helper-enabled unload/reload and device recreation, then repeat the reported battle. The original battle freeze is not proven fixed; unexpected mid-run helper failures remain audit F7.
