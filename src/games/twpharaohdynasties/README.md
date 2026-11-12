# Total War: PHARAOH DYNASTIES (RenoDX HDR, ATN)

### Round v55 (2026-11-13, screen-space shadow extras, smoke; built only, not run in game)

- **New: Light Through Banners and Coloured Shadows (Experimental, off) for Bend's screen-space shadows**, under Bend Screen-Space's More Options. The siege's cloth (`phar_rigid_battle_cloth_wind` VS 2F2A0E7E, PS 5277CF9F and 97850C83) and flags (`rigid_default_flag` VS 8DE94E2F, PS 369C9E2E), in the rays' scene since v49, are drawn again after their G-buffer draws into the banners' depth (shared `twattila/screen_space_shadows.hpp` `InstallThin`, as Attila and Warhammer III; both VS write SV_Position at o0, `fxc /dumpbin`; off under the game's MSAA). Their shadows let the sun through, coloured by the cloth with Coloured Shadows. The G-buffer's colour is an sRGB texture (R8G8B8A8_UNORM_SRGB, format 29 at the sun draw's t9 in every trace), so it is read linear (power 1, as Warhammer III). The banners (76225B91) stay out, as in the rays: the game draws them unlit (material 10) and gives them no shadow. Level A for the draws (siege trace: 3 cloth and 2 flag draws), none in the battle traces. `test/tw_screen_space_shadows` case 3 in Pharaoh's form (the sun's direction at float 4 of lighting_VS_PS, an sRGB colour): occlusion 0.323 and filter 0.000 / 0.875 / 0.875, as Attila's form gives (checked within 0.01); CONTROL: a decoy direction at Attila's float 1, read there, occlusion 0.000.
- **New: Light Through Foliage (Experimental, off)** for Bend, under its More Options, from the G-buffer's own foliage bit (Warhammer III's route; shared `twattila/graphics_page.hpp` `Bindings::foliage_light_tested`, false here: the row Experimental, 0 by default; Attila and Warhammer III unchanged). Channel 3's alpha byte (the sun draw's t10) has bit 128 only on foliage: the material census of the data run's six traces (`tools\tw\pharaoh\v55\material_census.py`) lists the 50 vertex and pixel shader pairs that write it, and the bytes their PS write (`fxc /dumpbin`): leaves 5FC6C4AF, far tree cards 3DE66666 and the far forest's blocks 6622ADC5 128, grass 12A15935 160; branches 0, rigid objects and buildings 1, units and props 33 or 1, skin 8, banners 10, terrain 21, water and rivers 64; 11 blend with that byte write-masked. The sun shader's own contact march lets half the sun through an occluder with bit 128 (F883F5B2: `and r20.z, r20.z, l(128)`, then the occluder's transmission halfway to 1). Level A. C: the roads (8A4D87B7, siege only) and terrain_to_gbuffer (768B1C08) write that byte through a blend whose factors no trace logs; the game's own lighting reads the same bit there. `test/tw_screen_space_shadows` case 1: Pharaoh's bytes (bit 128 on the bush, the seven others in turns elsewhere) against the depth copies 0 pixels apart (x86 214 shadowed alike, amd64 215); CONTROL: the water's bit 64 as the mark, 213 and 212 apart.
- **Fixed: Coloured Shadows filtered the sun's direction, not its colour** (the rays' Coloured Shadows since v49; Bend's from now). The sun shaders' patch multiplied lighting_VS_PS c1 by the filter at t50: Attila's sun colour, here `sun_direction` (`sun_colour` is c3, in the RDEF of all 25 sun shaders), so under cloth the sun's direction was bent, not tinted. Shared `twattila/screen_space_shadows.hpp`: `sun_colour_register` (1 by default; Pharaoh sets 3) and `Patch`'s colour register. WARP on the game's bytes (`tools\tw\pharaoh\sun_test`, v55, 2 runs of 4 synthetic scenes and the 3 traces' constants): with a filter F of (0.5, 0.75, 0) bound, the 18 patched shaders with a filter against the game's with its sun colour x (1 - F), 0 of 4,128,768 values apart; a filter of 0 bound against the game's 0; with t49 = 1 too, against the game's with every comparison 0 and the colour filtered 0; CONTROL: v54's patch (c1) 1,595,190 apart, the filter's reach 935,178. The whole sun suite passes with the addon's register (DRAW 25 of 25, v51 25 of 25, v54, v55). Every other game's patch is the same to the byte: 21,946 pixel shaders of the nine TW dumps, forward and not, 5,254 patches, 0 apart at the default register (`tools\tw\pharaoh\v55\patch_same`).
- **Checked: Torch and Fire Shadows for Bend** (installed since v50 with the point lights: the light draw E318CE77 comes after the sun draw, siege trace lines 1803 and 1814). `test/tw_screen_space_shadows` case 5 with Pharaoh's 64-byte instances (E19549A1's vb1: colour, position and reach, spot, cone): the same light picked and 0 of 129,600 pixels' lit bits apart from the 48-byte ones'. Nothing changed.
- **Page** (`tools\tw\pharaoh\build_dump.py v56` against v55): three rows changed, nothing else but the build date. Light Through Foliage (Experimental) added at 0; Light Through Banners (Experimental) 0.35 to 0 and Coloured Shadows (Experimental) on to off, with Pharaoh's tooltips (cloth and flags). All three show in 512 of the 3,076 states (Bend with More Options open). The presets audit: the two hidden writes that acted in v54 (No RT/PT's 0.35 and on) are gone. Attila (`dump_build.py a v165` against v164) and Warhammer III (`w v61` against v60): only the date.

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
