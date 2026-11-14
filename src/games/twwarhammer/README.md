# Total War: WARHAMMER (RenoDX HDR, ATN)

### Phase I, round 3 (2026-11-14 morning; built only, not run in game)

- **Ray Reconstruction's guides**: WH1's are Warhammer II's file and read the G-buffer as the sun shaders do (230F2241 asm
  212-370: normals from channel 1's two angles, roughness 1 - its z, diffuse albedo the albedo x (1 - channel 3's specular
  colour), specular albedo that colour; water writes flag 64, specular 0.04, gloss 0.95 itself). No change;
  test/tw_warhammer_shaders part 9 proves it against the game's bytes (CONTROLS: the angles the other way, the albedo
  without the specular colour).
- **Light Through All Banners (Experimental, off; Warhammer III's row)**: the units' banners are rigidtextureblend meshes
  (DBCC30FD, E7F0D71A with 2 bones), the shaders of other unit parts too, so only with this row: thin in the rays and in
  the screen-space shadows (InstallThin; their vertex shaders give SV_Position first, as sss_thin reads; Coloured Shadows
  with the G-buffer's colour linear: the sun shaders' diffuse is, part 10). E7F0D71A stays out of the rays (not a caster
  there yet).
- **Light Through Foliage (Experimental, off)**: the leaves' G-buffer shader 744FDFF0 writes channel 3's flag 128 and the
  sun shaders let the sun through from behind only there (part 11), Warhammer III's bit (`foliage_material_bit`): the
  screen-space shadows that leaves cast let that share of the sun through.

Test list (in game, ATN):
1. Battle with banners, Light Through All Banners on: their shadows lighter and coloured (screen-space and RT); off: as
   before; nothing else (shields, cloaks) lighter.
2. Battle in a forest, Light Through Foliage at 0.5: the leaves' screen-space shadows lighter, walls' and soldiers' as
   dark as before.

Not done: Sprite Shadows (WH1's far soldiers are points the game's geometry shader 12D3A4BC / D6954DFC turns into quads;
the rays capture a vertex shader's vertices with the draw's own indices, so they need new shared capture work, and none of
ATN's traces drew one).

### Phase I, round 2 (2026-11-14 night; built only, not run in game)

- **The paths' sky from the spherical harmonics, Path-Traced AO (Experimental)**: ATN's traces have
  g_use_spherical_harmonics on in every battle and campaign frame. rt_sun reads t_world_spherical_harmonics at the camera's
  place (the playable area c17 in cells of 3 x 3 texels, all 9 terms; the cube when off); the ambient patch sits after the
  harmonics' if on c11.z (shared PatchAmbient AMBIENT_AFTER_HARMONICS_Z: 15 of the 31 keep their ambient in rA.xzw; the
  other games' bytes unchanged). Path-Traced (ReSTIR GI, Experimental) is in the AO methods; RT/PT picks it as WH2's does.
- **More in the rays**: far tree cards (Warhammer II's capture: the same shader), the campaign's plants (placed, standing
  and still as Attila's), Far Tree Shadows and Shadow Casters Out of View (Warhammer II's shadow shaders: the same code),
  both Experimental, off. Out: floating banners and the campaign's far terrain (neither drawn into the shadow map).
- **Torch and Fire Shadows (Experimental, off)**: the point lights' two shaders (smooth and linear falloff) with each
  light's traced visibility; their instances as Warhammer II's (ATN's 03:30 traces: colour, place and reach 50 / 150 m).
- **Rays Replace Shadow Maps (Experimental, off)**: the 18 sun shaders that read the shadow map see a cleared one while the
  rays' shadow is bound: the rays' shadow alone, cloud shadows the game's. The maps are still drawn.
- **Skip the Shadow Maps (Experimental, off, under Rays Replace)**: nothing drawn into the shadow map; its 7 other readers
  in ATN's traces (battle and campaign water, forward-lit units and props, particles) take the rays' shadow instead (Rome
  II's forward patch). 0BF2DB7F / BCB9B1D6 have no SV_Position declared and 7865CDC9 only its w: the shared Patch's opt-in
  `declare_position` declares its xy (the other games' bytes unchanged: Patch's defaults on all 21300 PS of the 8 dumps,
  forward and not). test/tw_warhammer_shaders part 8: each bit-identical without the rays and fully shadowed with t49 1,
  their shadow constants filled by name (CONTROL: the shadow map's value changes each one's output).
- **Below 100%**: the SSR result added to the scene (69A57B14) reads its share (ATN's Performance / Ultra Performance
  traces); the tone mapper's bloom read is right there (drawn after the bloom of the whole view).

Test list (in game, ATN):
1. Battle, RT/PT: AO method Path-Traced; corners and under units darker, open ground as before; no black or white frames.
2. Campaign, RT/PT with Path-Traced AO: the sky's light on hills as before (compare Game's SSAO), no colour shift.
3. Battle at DLSS Performance: SSR reflections on water and armour where they are at DLAA (no doubled or stretched copy).
4. Battle with forest edges: Far Tree Shadows on, far trees cast shadows that stay put when the camera turns.
5. Campaign close to forests: the plants and trees cast ray-traced shadows (RT/PT), none floating or swaying.
6. Shadow Casters Out of View on: units behind the camera still shade the ground in front; off: as before.
7. Night siege, Torch and Fire Shadows on: walls and units block the torches' light; off: the game's light.
8. Rays Replace Shadow Maps on: one sun shadow (the rays'), Softness changes it; clouds' shadows still move.
9. Skip the Shadow Maps on (with Rays Replace): water, forward-lit units and particles keep the sun's shadow; big battles
   faster; off: as before.
10. ReShade.log: "drawn with the rays" lines for D0C67938 / 5D1695BC, no device loss, the frame time as before.

Not done: Fog Shadows (WH1's battle fog is analytic, no shadowed march to give the
rays); the campaign's far terrain CDCF1ADD (where it lies against the near terrain); the SSR march's patch below 100%
(021D7515, needs a picture); the reader census without SSAO / SSR (needs ATN's start with both off).

### Phase I (2026-11-14; built only, not run in game)

- **Render scale below 100%**: the weather effects (3619B413, 4BEEAAE2, 5B006315, 9A369793; 5B006315 drawn in the 10-27
  battle trace at 50%) map their uv to the viewport themselves: view_share leaves them (`SetViewportChains(true)`); the scale
  census's BUG (A) 4 -> 0. The SSR resolve replacement (6B80E35B) takes the frame's share where view_share patches the game's
  bytes: the position from the view's uv, the projected uv in the targets' share. test/tw_warhammer_shaders: 50% the same as
  100% on WARP, the march's direction the same; controls without each share differ.
- **The paths' sky and sun** (`InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4)`): rt_sun takes the ambient cube (c2-c7) x 1 and the
  sun colour (c1) x 0.318310 x 0.01, what the game's sun shaders light the diffuse colour by with g_hdr_on 1 (all 31 the same;
  230F2241's own bytes measured on WARP: 0.0031831, 0.318310 with g_hdr_on 0; ATN's battle and campaign traces: g_hdr_on 1).
  Float 44 is g_hdr_on here, not Attila's ambient switch: WH1's rt_sun wrapper turns that off. Path-Traced Ambient's rows
  now show (Experimental, off). The unlit (ulm) and default reflection sun PS stay unhooked: they write the G-buffer's
  colour or black.
- **Graphics 2 of 2 as Attila's** (as Warhammer II's round): 31 more of Attila's tests whose values engine_tweaks::Find
  finds in WH1 (emulated on the decrypted image; its misses hold all 10 of the 2026-11-14 log's "not found"): grass slopes,
  projected tree LODs, vegetation blocks, billboards, object / building detail and culling, shadow biases, warped / new
  shadows, sprite shadows, cutscene point shadows high, particle sorting, night lights, shared poses, fast soldier culling,
  campaign clouds / trees / plants / sea, voice memory limit, sound file memory. Out: Far Buildings, Building Collapse
  Effects / Animations and 7 More Tests rows (never found). Rope Distance without ROPE_DISTANCE(_FADEOUT); WH1's own values
  in the tooltips that differ (blood 0.05, audio 62 / 10 / 164 / 4 / 2 MB). Reset puts the tests back too.
- **Graphics 1 of 2 as Warhammer II's / Pharaoh's** (`graphics_page::Apply`): Reset / No RT/PT (Recommended) / RT/PT,
  the shadow, AO, GI and reflection methods, FSR and XeSS (a saved DLSS becomes XeSS on Intel, FSR on AMD), TAA Motion,
  Texture LOD Bias on All Textures (Experimental, off), the full-download note; Bend's shadows off while the rays' sun
  shadows run. The rays' sun shadows multiply the game's maps, RTAO multiplies its SSAO; Path-Traced AO not in the
  methods yet. Every key kept (page dump: +5 keys, none lost).
- **Hue Clip default 90** (was Attila's 40): measured on WH1's own tone mappers (test/tw_psychov35 part 5 curve 7, the
  battle's whiteout 0 / 10 from ATN's traces): fire's hue against the game's SDR crosses 0 between 0.80 and 0.90, sky's
  between 1.00 and 1.25; Attila's 40 came from its stock path's highlight limit, which WH1's has not.

Test list (in game, ATN):
1. Battle, Upscaling DLAA then Performance: weather (rain, snow) and the SSR reflections sit where they do at DLAA.
2. Graphics 1 of 2: Reset, No RT/PT, RT/PT each; the methods' rows follow; FSR / XeSS listed; nothing left on after Reset.
3. RT/PT in a battle: sun shadows on top of the game's, RTAO darker in corners, RT reflections on water and armour.
4. Graphics 2 of 2: ReShade.log "Engine values found: N of N" (none missing); Reset puts every test at Game's.
5. A test or two from the new rows (Object Detail Distances, Shadow Slope Bias) change what they say; MAX unchanged.
6. Night siege: torches and fires as before (no row changed them).
7. Campaign: the map, shroud and water as before; the minimap's picture as before.
8. Recommended (PsychoV35): fires and sky against SDR at Hue Clip 90 (new default) and 40 (the old one).

Not done (this round): the paths' sky from the game's spherical harmonics (ATN's traces: on in battle and campaign) and the
ambient patch after them, so Path-Traced AO stays out of the methods; far tree cards, texture-blend props, campaign
vegetation and campaign terrain in the rays (caster census MISSING); Rays Replace / Skip the Shadow Maps, Torch and Fire
Shadows, Fog Shadows (WH2 v48's rows); every scale below 100% from a trace (the census's 7 unclear); the reader census;
the lights' instance rows; Faster Game CPU Code (no byte twin of Attila's x87 routines in WH1's x64 code).

### Trace build (2026-11-14, for the data run; built only, not run in game)

- **Frame traces on demand and at 100%** (`twattila/frame_trace.hpp` Options, as Warhammer II's): `renodx_trace_now` next to
  warhammer.exe traces the next frame (looked for every 60 presents); `[renodx] FrameTrace=1` counts the 3D view at any scale.
  One draw each of the 11 single-sampled sun PS, the four tone mappers, the point and spot lights, the battle fog, water, river
  and lit particles writes its constants (b0-b6, 256 floats); the particles' blend and their VS's buffers; instanced draws'
  counts apart. With `[renodx] FrameTraceReads=1` the point and spot lights' instance rows, also the frame after
  (`trace_reads.hpp`).
- **Run hooks** (`twattila/test_hooks.hpp`): `RENODX_TW_SET`, `RENODX_TW_SHOT`, `RENODX_TW_PROFILE`, timed from the first
  present; nothing without them. Every feature as before (page dump: the same but the Build Date).

Build: `cmake --build build --config Release --target renodx-twwarhammer.addon64`. Manual check: in a battle, an empty file
`renodx_trace_now` next to warhammer.exe: within a second `tw_frame_trace.txt` appears and ReShade.log says
"Frame trace: N lines saved".

### Round v47 (2026-11-14, shared code from Attila / Pharaoh; built only, not run in game)

- **Fixed: the Ray Tracing Debug View's Screen-Space AO was magenta.** No AO pass hook keeps it here (the game's AO is compute line sweeps), so it is now what each sun draw reads at its `t_ssao` (the game's SSAO or GT-VBAO, blurred), copied before the rays' AO and the AO filter take its place: `rt_scene::InstallSunDebugAmbient` (`twattila/rt_scene.hpp`, Pharaoh's hook moved there). Its mapping from the shaders' bytes: all 30 sun draws with an AO slot read it at `(SV_Position + g_vpos_texel_offset) x g_screen_size.zw`, as their G-buffer, which is the Debug View's own mapping (`tools/tw/debug_ao_v47/ao_mapping.py`; the same bytes in Pharaoh's 24, where a trace proved it; Warhammer III's read it through their overscans and fail the check). Page dump: the same but the Build Date.

Build: `cmake --build build --config Release --target renodx-twwarhammer.addon64`. Manual check: with DLAA or an upscaler and the helper, Ray Tracing Debug View on Screen-Space AO is no longer magenta: it shows the AO of the Ambient Occlusion row's method (Game's or GT-VBAO; white open, darker in corners and under units) lined up with the picture, also below 100% render scale.

### Round v46 (2026-11-13, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Fixed: the Ray Tracing Debug View rows never showed.** They wait for `rt_scene::OnTonemapDrawn`, which this add-on's tone mapper hook never called (Pharaoh had the same gap, 041821eb); now right after the anti-aliasing's, as in Attila. Its Screen-Space AO view stays magenta: no AO pass hook here, and Pharaoh's copy at the sun draws is not ported. Found by the skill's `scripts/static_checks.py`; the page dump (which sets the flag itself) is the same but the Build Date.

Build: `cmake --build build --config Release --target renodx-twwarhammer.addon64`. Manual check: with DLAA or an upscaler and the helper, the Ray Tracing Debug View row shows; its Sun Shadows view shows the rays' raw shadow as the picture.

### Round v45 (2026-11-12, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Bend Screen-Space Shadows: Auto Thickness removed** (ATN: it broke at some angles).

### Round v42 (2026-11-11, shared code from Attila / Warhammer III; built, not released, not run in game)

- **Object Motion Vectors: each soldier keeps its own bones.** When the game packs its soldiers' bones into the animation pages more than once a frame (big battles), a later round's soldier at a reused row shared a key with an earlier one and could take its last-frame bones: false motion on still soldiers' shields, swords and spears under DLSS, FSR or XeSS. Keys are now this frame's and new after every page write (`twattila/object_motion.hpp`; the 64-page clear that gave page 65 page 1's key is gone). WARP `test/tw_unit_pack_rounds` (Attila's path; this add-on claims bones in the draw, so the keys are its fix).
- **Bend Screen-Space Shadows: Auto Thickness (Experimental, off)** (each object as thick as it is wide: fuller shadows behind walls and carts, poles stay thin) and **Softer Away from the Caster (Experimental, 0 = off)** (sharp at contact, softer farther away, by the sun's size). Every row off: today's shaders and picture (WARP `test/tw_screen_space_shadows`, to the bit).

Build: `cmake --build build --config Release --target renodx-twwarhammer.addon64`. Manual check: a paused big battle, the camera around the armies and back, Object Motion Vectors on: weapons on still soldiers stay still. Screen-space shadows with the new rows off: the picture as before.

### AO depth-level recovery (F15, 2026-11-09, GPT-6 Astra; not run in game)

Failed depth-level texture/view creation now releases all partial views and retries at the same resolution after a one-second cooldown. Build: `cmake --build build --config Release --target renodx-twwarhammer.addon64`. Manual check: use an AO mode that reads depth levels, change resolution/render scale, and verify stable AO; after a transient allocation failure, depth levels should recover without another resolution change. Failure injection and real D3D11 WARP readback results are in `docs/Attila-crash-audit-2026-11-08.md` at the repository root. No shader behavior was changed.

### Startup worker lifetime (2026-11-08, GPT-6 Astra; not run in game)

The startup audio-setting worker is now cancelled and waited for through its actual thread exit before unloading the add-on. Its progress flag alone did not cover capture and CRT cleanup. Manual check: unload/reload during the first minute with an audio-memory test enabled, then after it completes; no worker may execute unloaded code. Shared build and offline checks are recorded in HANDOFF.md.

Build: `cmake --build --preset clang-x64-release --target renodx-twwarhammer.addon64`.

## v37 (2026-11-12, PsychoV35 round; built only, not run in game)

- **HDR (PsychoV35)** (Rendering Mode 3): Musa's custom PsychoV35 (`twattila/psycho_test35.hlsli`, shared with Attila and WH3) with its own sliders (`v35_*`) and **Hue Clip Emulation** (`twattila/psycho35.hlsli`, `psycho35_settings.hpp`). Hue Clip Emulation (0 - 100, default 40) Attila's engine (`AttilaEngineHueClip`, the bloom already in the scene, no highlight limit); default 40 = Attila's (part 4). Reset and Recommended in PsychoV35 (no Extended mode here).
- Hue Clip default from test/tw_psychov35 part 5 (WARP): its kind of stock curve on the scene, PsychoV35 right after, the OkLab hue error against the per-channel SDR clip where it clips; not measured on this game's own chain (Thrones and WH1 take Attila's).
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
- **Fog & Sky Smoothing** (Off / Low / Medium / High + Strength, off by default, as Attila): the sky cube's vertical lines in thick fog. The fog shader 0xE684D54E is now replaced; it was never seen drawn in a trace, and its MSAA variant stays stock.
- **Extra in Rendering Mode SDR**: Lens Flare, Vignette and Distortion work there (at 1 the picture is unchanged). Film Grain and Indicators stay HDR-only.
- **Wrong constants from the decompiler** (checked against the game's own shaders):
  - In-game depth of field: the sharp range around the focus was about 30% too narrow (f x f, the game has 2 x f x f).
  - Rendering Mode SDR: bright colours fade to white softly as in the game (start read twice: a hard contour). HDR unchanged.
  - Motion vectors and ray tracing: emissive wind mask Y of units with it.

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
  replaces the screen-space march, which is then skipped (the environment_specular_light resolve cs 0x6B80E35B; its raycast and blurs are left out); the game's environment stays for the rest (the
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
  the display's size (the shadow cascades' depth analysis) read only its rendered part. After DLSS, depth of field, distortion, fog of war and photo
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
  renodx-tw-dlaa64.exe): Attila's (rt_scene.hpp) on WH1's 29 scene shaders (shader model 4.0 / 4.1 / 5 builds of
  terrain, rigid, branches, leaves, units; the trees from fxc's disassembly: 3Dmigoto's decompile of them is wrong).
  WH1 has no ambient occlusion pass of the addon's: the trace runs at the first sun draw and its ambient visibility
  multiplies the AO the sun draws read (rt_ambient_combine, before the AO filter). GPU test: 29 of 29 captures match.
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
  the game's shader dump: tools\tw\sun_draw_layouts.py): its lighting at b3 / b4 / b5. WARP check
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
  drawn smaller and DLSS upscales it; Ray Reconstruction's guides from its G-buffer (shared with Warhammer II: normal
  angles, gloss in channel 1's z, specular colour in channel 3). The screen-space shadows and the AO filter keep to the
  3D image's corner of the screen-size textures (Bend's RenderSize).
- DLSS / DLAA motion vectors for everything that moves on its own (as Attila's v69-v70: twattila/anti_aliasing.hpp
  object motion), 33 of the game's vertex shaders: grass, leaves and branches (3Dmigoto's decompiles were wrong:
  translated), campaign vegetation, units (2- and 4-bone, dismembered), rigid objects, sails, ropes, both oceans. Made
  by tools\tw\attila_motion\port_motion.py (classify_motion.py picks them from the dump; asm_to_hlsl.py where 3Dmigoto
  crashed or was wrong, bit-exact); GPU check test_motion.py: every motion shader's SV_Position bit-identical to the
  game's, last frame's position as the game's shader at last frame's camera / time / placement.
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
  Attila's 16 and 421 More Tests (unsure; a name the game lacks shows as not found). At-start tests as
  Attila. Engine value lookup is one pass for all names (a search per name took minutes with 1000+). engine_values.txt lists them.
- Bloom less blocky (Rome II's fix): the high passes average every source pixel a bloom texel covers (twattila/bloom_high_pass.hlsli; stock:
  one tap) and the bloom is read with a cubic B-spline over 4 x 4 texels (the tone mappers; stock: one bilinear tap).
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
  (twwarhammer2/line_sweep_ao.hpp DispatchInstead: the blur's depth, t0). A sample 8 x 2^n to 16 x 2^n depth texels away
  reads level n at that depth texel's own position (depth_levels.hlsli, BitmaskSampleLevel in bitmask.hlsli; as Scalable
  Ambient Obscurance). Same look; the far samples stay in the texture cache, so more Directions / Quality cost much
  less.
- Light Bounce levels: the prepared light and geometry with 5 more mips (twattila/light_bounce_levels.ps_5_0.hlsl after
  the prepare: light the mean of 2 x 2, geometry one of them in a rotated grid); the trace's far samples read them
  (BitmaskSampleLevel). Same look, cheaper at Full / Half resolution and large radii.
- AO presets (ATN): Thickness 0.11 in both Recommended presets; a yellow line above them: "Prefer darker shading behind
  trees and objects? Pick a preset, then raise Thickness (e.g. 0.20)."

## v15 (2026-10-23)

- Removed (never read): Model Detail (battle_entity_lod*), Building Detail (battle_building_lod*); Campaign Detail
  keeps rigid_campaign_tree_lod only ("Campaign Tree Detail Distance"; campaign_tree / grass_lod* never read).
- New: Tree Detail Distance (tree_lod_scaler 1.35, divided: on the camera distance), Bush Detail Distance
  (scrub_lod_distance_default 175, per scrub at load), Terrain Draw Distance (terrain_visible_tiles 500), Campaign Draw
  Distance (campaign_culling_ratio 0.003, divided), Shadow Distance (shadowmap_split_scale 1, 0.5 - 4x), Small Object
  Shadows (cull_small_shadow_size 0.02, divided), Unit Shadow Distance (*_IMPOSTER_SHADOW_DISTANCE and
  entity_display_shadow_max_shadow_distance, 10000). All values at object + 0x68, read through the getter +0x162f10.
- Graphics values checked with tools\tw\tw_tweak_survey.py (every engine tweak of the exe and the code that reads it;
  it matches the watch traces): sliders whose values the game never reads removed, new ones on values it reads.
- Watch v2 (tw::engine_tweaks::watch): no crash at its end (Dr7 cleared in the handler while stopping), a getter's
  caller logged, new threads armed, readers logged as found; buttons "Watch graphics values (60 s)" and "... at the
  next start (5 min)" (ReShade.ini [Total War] WatchAtStart, once).

## v13 (2026-10-23)

- Game's ambient occlusion costs what it did again: the replaced AO pass is the game's code only and the Visibility
  Bitmask is the addon's own 8 x 8 compute shader dispatched in its place (twwarhammer2/line_sweep_ao.hpp
  DispatchInstead, the addon's settings bound at b13). Before, one 1024-thread shader held both (fxc X4714: fewer
  threads in flight even for the game's path). Here in place of the blur (ao_bitmask_blur), the collect never skipped
  (its replacement removed): at settings without a blur the game's AO shows, never an unwritten texture (Warhammer
  II's black-after-loading).

## v12 (2026-10-23)

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
- Sun draws: slm (6) / skinlm (5), camera b2 (the reflection's b1); decode twwarhammer2/light_bounce.hlsli (spherical
  normals, albedo x (1 - specular)).
- Trace graphics settings (button): every slider's engine values to ReShade.log (tw::engine_tweaks::Trace).

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

- PsychoV31 Highlight Hue Shift default 0.05 (ATN).
- Model Detail Distance: battle_entity_lod0-3_distance and the very large ones (5 / 10 / 30 / 100 m and 30 / 60 / 90 /
  120 m), up to 8x.
- Ambient occlusion: the Visibility Bitmask moves into the AO's horizontal blur (line_sweep_blur.hlsli), from that
  pass's own depth and camera (this frame's) over its own size, then a 5 x 5 depth-aware average; the collect is
  skipped with it (ATN: Warhammer II's marks stayed). line_sweep_ao.hlsli removed.
- Shadow Map Size: the game's shadowmap_extreme_quality (engine_tweaks.hpp flag tweaks, 1 byte), which gives the
  highest Shadows setting an 8192 shadow map (its code: quality 4 / 5, enough video memory), set at the game's device
  creation. Untested. Warhammer: 4096 -> 8192.

## v6 (2026-10-22)

- Ambient occlusion Radius up to 10x (ATN).
- Engine values: 45 of 45 (4 before): engine_tweaks.hpp ObjectValue gave up when no object lea preceded the name;
  Warhammer's follows it.
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
- Graphics (Experimental): unit detail per size, animation, variety, footstep, terrain, building and campaign detail
  (the game's tweaks, value at +0x68).

## v4 (2026-10-22)

- Ambient occlusion in its own section after Graphics, "Ambient Occlusion (Experimental, needs in-game SSAO)" (ATN),
  labels Strength / Radius / Thickness; defaults Strength 1, Radius 4x, Thickness 0.15 m (ATN). Advanced: "Ambient
  occlusion passes per frame" (twwarhammer2/ao_trace.hpp: counts the AO shaders' draws / dispatches; 0 = the game
  never ran them).
- Graphics (Experimental) and the AO section: Advanced only; both titles pink.
- Reset: PsychoV31 (no Extended mode here; ATN).
- Graphics (Experimental, Advanced): "Save game code": the exe's image from memory (its protection decrypts the code at
  start) to Downloads\<exe>.memory.bin, to find engine values for new sliders offline (ao_trace.hpp SaveGameCode).
- Bloom (as Attila): the high pass's hard cap of the bloom source (luminance 25) is a logarithmic shoulder in HDR
  (bloom.hlsli; bloom00_0x93ED7AC4 / bloom01_0xCFFF920A): bright sources no longer all get the same flat halo. SDR unchanged.
- Visibility Bitmask AO: every depth sample's position at the centre of the depth texel it read (bitmask_ao.hlsli
  AoSamplePosition). Off-centre positions with a texel's depth made sloped ground occlude itself in a regular dot
  pattern (ATN's Warhammer II "matrix" pattern, debug1).

## v3 (2026-10-22)

- General: yellow note "Use these in-game settings: Gamma 22 and Brightness 10."
- Presets: Recommended (new): Reset, then PsychoV31 at its defaults and Video AutoHDR 0.20; Reset turns Video AutoHDR off.
- Extra: Video AutoHDR / Video Dithering (cinematic00, autohdr.hlsli).
- Graphics (Experimental): Ambient Occlusion Game's / Visibility Bitmask (default; Strength = an exponent, Radius
  1 = 2 m, Thickness). The line sweep (0x0C83DB37) is skipped, its depth and camera bound for the collect
  (ssao00_0x38F6D9B6: its AO is half size in the top-left quarter), which computes the AO
  (twwarhammer2/line_sweep_ao.hpp, line_sweep_ao.hlsli, bitmask_ao.hlsli: Therrien, Levesque, Gilet 2023 on GTAO, as
  Attila's); the game's blur follows. Credit in About.
- About: NexusMods button next to ATN's Ko-fi; every section title green, Graphics (Experimental) pink
  (settings.hpp default_section_tint / section_tints).
- Check: in-game SSAO on, a battle: Ambient Occlusion Game's vs Visibility Bitmask (no dark halos around soldiers);
  a video with Video AutoHDR 0 / 0.2.

## v2 (2026-10-20)

- PsychoV31: Highlight Hue Shift 0 by default (ATN); a note above Highlight Saturation / Hue Shift / Saturation
  Shift ("Fire too pink, too orange or too pale? Change the settings below.", as Tales of Arise), Highlight
  Saturation moved next to them.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (PsychoV30, default) / HDR (PsychoV31): the same PsychoV31 sliders as the other Total War
  mods (Musa's custom test31; Highlight Hue / Saturation Shift toward the SDR image's per-channel clip).
- Shared code: `hdr_to_screen.hlsli` (HdrToScreenOutput: matrix + three LUTs on a bounded proxy, then V30 / V31; t01-t08),
  `tone_mapper.hlsli` (ApplyWarhammerSCurve + ToneMapperHdr: t00 / t09 / t10), `lensflare.hlsli`.
- Shader packs (dump `bin\cmd_Decompiler-1.3.16\dump_shader_twwarhammer_pack`): t09 / t10 the constant tone mappers
  (fixed exposure); lensflare00-03 (flare_0-3, none were covered: drawn on the 2.2 frame, now at Paper White) with a new
  Lens Flare slider.
- Not ported: the constant tone mapper "with_bloom_and_gamma" (display-ready output, likely the 3D UI portholes).
- About: credits, ATN's Ko-fi.

Verify: SDR / V30 / V31 in a battle and on the campaign map; Lens Flare slider with the sun in view.


### F1 helper lifetime repair (2026-11-09, GPT-6 Astra)

AddonUninit now shuts down the shared helper bridge after unregistering its users. The watcher exits before module unload; child process and IPC ownership are released. Shutdown drains outstanding GPU fence generations, with ordered CPU fence recovery only after confirmed helper exit. Failed launches clean up partial ownership and existing IPC sessions are not overwritten.

Offline checks: x86/x64 production-header tests cover process/thread cleanup, failure injection, 20 reloads with stable handle counts, and three interleaved D3D11/D3D12 WARP fence generations under normal completion and forced shutdown. No game was launched. Manually check helper-enabled unload/reload and device recreation, then repeat the reported battle. The original battle freeze is not proven fixed; unexpected mid-run helper failures remain audit F7.
