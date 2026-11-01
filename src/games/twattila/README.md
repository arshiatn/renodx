# Total War: ATTILA (RenoDX HDR, ATN)

Build: `cmake --build --preset clang-x86-release --target renodx-twattila.addon32` (32-bit game).

Download: https://github.com/arshiatn/renodx/releases (Nexus blocks the zip; Steam Workshop takes only the game's .pack
mods, not ReShade add-ons or the helper). Ray and path tracing: NVIDIA RTX with DLSS; fastest on RTX 40 and 50 with a
current driver (DXR 1.2: their hardware speeds up Opacity Micromaps and Shader Execution Reordering).

## v125 = players' v2.01c (2026-11-01; renodx-tw-dlaa64.exe helper67 as v122, bridge v20: ship them together)
- DLSS Ray Reconstruction runs only while something is ray-traced: without rays DLSS uses its usual model (the setting
  stays as chosen for when ray tracing comes back).

## v124 = players' v2.01b (2026-11-01; renodx-tw-dlaa64.exe helper67 as v122, bridge v20: ship them together)
- RT/PT preset: turns DLSS Ray Reconstruction on if it is off (without it nothing denoises the rays: grass shadows flickered
  when moving).

## v123 = players' v2.01a (2026-11-01; renodx-tw-dlaa64.exe helper67 as v122, bridge v20: ship them together)
- Output Auto is HDR10 with Windows HDR on (was scRGB): Frame Generation takes no scRGB, and turned on in a game started
  in it did nothing. With Output scRGB a red note says to restart (the next start uses HDR10).
- RT/PT preset: Importance-Sampled Torch Shadows (RIS) on.

## v122 = players' v2.01 (2026-11-01; renodx-tw-dlaa64.exe helper67, bridge v20: ship them together)
- The red "ray tracing and path tracing do not work correctly yet" note is gone; Graphics 1 of 2's last section is "Ray
  Tracing Extra Options".
- Rays a Pixel (Use higher for lower scalings): up to 8 rays a rendered pixel (Match Output too: up to 8).
- RT/PT preset: Smoke Shadows on (cheap since v121).

## v121 (2026-11-01; renodx-tw-dlaa64.exe helper66, bridge v20: ship them together)
- Smoke Shadows no longer kill the frame rate under arrow volleys (ATN's battles: 100-300 ms traces, now 4-11 ms): the
  smoke is traced on a ray of its own (8 particles a sun ray at most, 2 a fog sample) in a TLAS of its own, built as a
  sorted BLAS (Mega Geometry's clusters of it spanned the battlefield); its quads have the particle's area (an arrow's thin
  trail took a disk as wide as it was long) and faint or tiny particles none.
- Flickering shadows in huge battles: the TLAS takes 524,288 instances (262,144 ran out: kept meshes left out, others each
  frame); captures 1024 a frame, drawn meshes and placements 16,384 (bridge v20: addon and helper ship together).
- Swaying grass at a battle's start: Mega Geometry's room starts at half its limit on GPUs over 20 GB (it grew four times
  in the first seconds, what did not fit left out meanwhile).
- Turning RT/PT off gives its video memory back after 10 s (the helper kept its ray tracing and Ray Reconstruction devices:
  15 GB after switching off, 7.5 GB never on); they start again when turned on.
- RT/PT preset: Swaying Shadows Range 50 m, Grass Shadow Range 0 m (everywhere); Far Tree Shadows and Sprite Shadows off
  by default and in the preset. Graphics 2 of 2: Grass Height Distance back to 1.00x in both presets and Reset.
- Effects at Output Resolution checked: GT-VBAO, Light Bounce, SSSR, the game's SSR blur, RT shadow softness, SSS's length
  and the game's SSAO follow it; ray-traced effects trace at the render size by design.

## v120 (2026-10-31; renodx-tw-dlaa64.exe helper62 as v119: ship them together)
- Graphics 1 of 2: Reset brings back the game's SSAO, SSR, Water Clarity and Water Caustics (the rest at its default).
  Shadow Map Size and UI 4K Mod Compatibility moved there (Shadows), shown only with the game's shadow maps; the
  presets leave Shadow Map Size alone (it needs a restart).
- Campaign cities no longer jitter under DLSS: the campaign map's world is about a twentieth of the battle's, so its
  object motion's placement table (made for metres) put whole cities in one cell, and wall and tower pieces took a
  neighbour's place (ATN's trace at Carthago Nova: ~95 pixels of motion with a still camera). Its cells and reach now
  shrink there (motion.hlsli PlacementScale). Not tested in game yet.
- Not in v120 (next batch): newest FSR + FSR Frame Generation, ray tracing on AMD and Intel with NVIDIA NRD denoisers.

## v119 (2026-10-31; renodx-tw-dlaa64.exe helper62: ship them together)

- **The campaign map's plants and forests are in the ray-traced scene**: rigidcampaignvegetation (0x924D7813, 0x819A414C) and
  rigidcampaigntrees (0x4657DA8A) were never captured, so forests had no ray-traced shadows at all. They are placed as the
  battle's trees are: each mesh once, its copies from their instance rows, so they cost little (62 fps with them, 66 without, in
  the save's first view; 45 fps when they were captured every frame). They stand upright in the scene: the game leans each plant
  by the camera's height, and a shadow leaning with the camera would swing.
- **Blocks of black shadow on the campaign map**: palm and pine meshes are rigid meshes with cut-out cards. The scene had no alpha
  test for rigid meshes, so every card was a solid rectangle. The game's own shadow pass knows which meshes are cut out
  (ps30_shadow_alpha_test): their diffuse maps are noted there, and those meshes are cut out in the scene too, in battles as well.
- **The campaign's sea takes the rays' shadows** (it was lit everywhere under Ray-Traced Shadows). Its rays start on the water, not
  on the sea floor under it: the floor's tile edges shaded themselves, a thin stepped line over the open sea.
- **Beaches below 100%**: the water refraction pass fills its whole half-size target, and the water reads it by its name. Ocean,
  ocean2 and the campaign's sea and lakes call it by other names than rivers do, so they read the wrong part of it. Fixed for all
  of them, naval battles too. The audit of a whole campaign frame found nothing else.
- **Campaign animals jittering under DLAA**: their rainbow in the Motion Vectors view was last frame's bones: at the frame's end the
  animation texture no longer holds what they drew with. On the campaign map they now move by their place alone: smooth, one
  colour each.
- **Below 100% without DLSS running** (DLSS's files missing, or an AMD or Intel GPU): the picture stayed in the top-left part of the
  screen over grey. The render scale now only runs while DLSS answers.
- **Ray tracing is NVIDIA only for now**: on other GPUs it stays off (it needs Ray Reconstruction, and no other driver has run it).
- **Graphics 1 of 2**: Shadows moved above Ambient Occlusion, with three presets on top: Reset (everything but Anti-Aliasing and
  DLSS), No RT/PT (Recommended) and RT/PT (ATN's settings). The Ray Tracing Low / Medium / High / Ultra buttons are gone, in every
  game that shares them.
- **HDR presets and PsychoV31's defaults**: Highlight Saturation 0.70, Highlight Hue Shift 0.04. The SDR preset keeps 1 and 0.15.
- **Graphics 2 of 2**: UI 4K Mod Compatibility only locks Shadow Map Size now, everything else works with it on. The three Shadow
  Test sliders are gone. Grass Detail Distance and Grass Density moved next to Grass Height Distance. Grass Always Drawn is gone.
- Tested in game: both campaign saves here, a battle, DLSS's files missing, the helper missing, the DXR 1.1 path without micromaps
  or Mega Geometry (test switches RENODX_TW_NO_DXR12, RENODX_TW_NO_CLUSTERS; 30 fps against 60 on the RTX 5090). Harness: 50 of 50
  modes. Not seen: ATN's pines near Augusta Treverorum (under the save's shroud here), a real RTX 20 / 30 or AMD card.

## v118 (2026-10-31; renodx-tw-dlaa64.exe helper61: ship them together)

- **Shadows, AO or GI gone, fixed at the cause**: a ray tracing request could overflow one buffer: 256 new meshes in a frame plus
  a finished micromap bake. That request failed, and it stayed failed: one of the three request slots never recorded again, so one
  effect at a time was gone (first the sun shadows, after toggles the Global Illumination). What brought everything back was the
  GPU reset below, not the AO toggle. The test that reproduces it (256 new leaves a frame with micromaps) failed the same way
  before and passes now. Should a request ever fail again, ray tracing starts again by itself in a moment, nothing paused (the
  fourth failure turns it off).
- **The freeze is fixed, the real one**: a trace could be recorded while the last trace was still running on the GPU, and the GPU
  reset at its sway or GPU micromap passes. With Light Bounce on that cannot happen: the traces take turns in one slot, and each
  waits for the last. With it off they change slots: right after the toggle, or with Light Bounce off from the start at the
  battle's second trace. A reset pauses sway and GPU micromaps until the game restarts, so it showed as one freeze, with those
  options quietly off after it. Now a trace always waits for the last one, at no cost. Test with ATN's settings: 33 of 33 runs reset
  before, 0 of 24 now (the sequence that froze, 18 toggles in a battle, traces alone, Light Bounce off from the start). What
  exactly goes wrong inside the driver is not known. v117's fix was real but only half of it: its test settings could not show this.
- **Water that looked like screen-space reflections** under SSAO / GT-VBAO / RT AO was the same failure: the log says the
  reflections stopped exactly then, and came back exactly then. The AO method has nothing to do with it.
- **Smoke Shadows came back for good**: a particle draw holds its whole list, nearly all of it invisible (742 of 47884 had any
  opacity in a battle frame). The first 65536 taken filled up with those, and the catapults' smoke, drawn later, was left out as
  long as that list stayed. Now only particles with opacity go into the scene: up to 524288 are read, 32768 kept.
- **Sides of the screen in RT reflections**: what a reflection shows past the screen's edge has no colour known yet, so it came out
  pale, right in the middle and wrong at the sides, as screen-space reflections are. It takes the colour at the screen's edge
  now (banks and walls go on past it). Rays a wave's normal turned into the water no longer show the riverbed: they bounce off,
  as off the next wave.
- **Far trees have ray-traced shadows** (Shadows > More Options > Far Tree Shadows, on), next to Sprite Soldier Shadows: cards
  turned to the sun, showing the tree from there. Cost in the replay's tower view: scene +0.3 ms, trace +0.06, reflections +0.03,
  Global Illumination +1 ms. Sprites and far trees are met by the sun's rays alone now, so sprites got cheaper.
- **Dead bodies** (and anything lying on the ground) in the shade: under Path-Traced GI a ray from their underside met the ground
  right beside it and brought back that ground's full shade light, so their lower edges glowed and no contact shadow showed. What
  a ray meets within 30 cm now counts for less, the closer the less. Found by reading the code: not yet seen on a body (the test
  replays can't bring the camera to one in the shade). Corners in the city came out soft, not crushed.
- **The page names its technology**: Game's SSAO, GT-VBAO, Ray-Traced (RTAO), Path-Traced (ReSTIR GI); Game's Shadow Maps, Bend
  Screen-Space, Ray-Traced (DXR); GT-VBGI, Ray-Traced (One Bounce), Path-Traced (ReSTIR GI); Game's SSR, FidelityFX SSSR, Ray-Traced
  (DXR); DLSS Ray Reconstruction, DLSS Frame Generation, Opacity Micromaps. Saved settings and presets stay as they are.
- **Nothing debug runs unless asked**: with Log Details off the helper writes no GPU timestamps, no status text, no pass markers
  and no log lines but errors, and D3D12's crash recording (DRED) is off. Log Details on when a battle starts turns all of it on,
  also where a GPU reset stopped. The one-time Screen-Space Shadows report (46 log lines and a stall each time they came on) is gone.

## v117 (2026-10-31; renodx-tw-dlaa64.exe helper60: ship them together)

- **The freeze is fixed**: the GPU reset came from GPU Micromap Baking running beside the game's frame (since v114). Test
  (86 s a run, the sequence that froze): 3 of 5 runs hung that way, 0 of 8 with the bakes back where the game waits for them,
  0 of 8 with GPU baking off. The finished build: 0 of 8 on that sequence, 0 of 6 toggling 18 settings during a battle. The
  bakes cost at most 1.5 ms a frame, 8 bakes a frame at most, and only when something new appears. A second GPU reset no longer
  crashes the helper and the game.
- **New Graphics 1 of 2**: one section per job (Anti-Aliasing and DLSS, Ambient Occlusion, Shadows, Global Illumination,
  Reflections and Water, Ray Tracing), all titles one colour. Each has a Method slider and shows only that method's settings;
  the rest is under More Options (closed, its own colour). DLAA in Anti-Aliasing is called DLSS now. Saved settings and presets
  stay as they were: the Method sliders read and write the same keys. Leaving Path-Traced Global Illumination while on
  Path-Traced AO moves AO to Ray-Traced (the game's ambient would come back with no AO on it). Every method slider and More
  Options was clicked through in game.
- **LOD changes under RT Shadows**: while the camera moves, every mesh is updated every frame, so the old level no longer leaves
  up to 3 frames before the new one comes in. About 0.1 ms CPU while moving, nothing when still.
- **Sprite soldiers have ray-traced shadows** (Shadows > More Options > Sprite Soldier Shadows, on): cards facing the sun, cut
  out by the sprite's alpha, so the shadow keeps its width and does not swing with the camera.
- **Helmets**: rays started 3-5 cm off the surface at close range, past cheek guards and helmet rims. Now 3 mm plus 0.1% of the
  distance. No acne in the tower view; a close-up soldier is not tested (the test keys can't get there).
- **Light Through Foliage** (Screen-Space shadows, 0.5): shadows cast by grass and leaves let that share of the sun through; walls
  and soldiers keep full shadows, on foliage too. The game marks foliage nowhere, so the addon compares the depth before and
  after the foliage's depth pass.
- **Rays a Pixel** replaces Ray Tracing at Output Resolution (same saved key): 1, Match Output (its old On), 2, 3, 4, also at
  DLAA; More Options switches it per effect. Global Illumination and Path-Traced AO take it now, they never did. A ray-traced
  image above the render size is not possible: shading runs at the render size until DLSS. Extra rays from their own spots of a
  pixel are the same rays a higher resolution would trace.
- **Double work removed**: no depth pyramid when ray-traced reflections take every pixel; the Light Bounce's depth levels only for
  its screen-space trace. RT reflections, GT-VBAO, RT / PT AO and RT Shadows were already replacing the game's passes.
- **Log Details** (Ray Tracing > More Options, off): the statistics written every 10 s, their clocks and GPU queries, and a slow
  frame every 10 s under RT Shadows are off unless asked for. Turn it on for bug reports.
- Water Caustics is not ray traced: an animated pattern on the river bed.

## v116 (2026-10-30; renodx-tw-dlaa64.exe helper59: ship them together)

- **Tower shadow flicker fixed, near and far**: on a round tower one wall facet at a time flickered between lit and
  shadowed. A surface almost edge-on to the sun was judged by a normal from the depth buffer, which changes a little
  every frame. Those pixels now ask the triangle itself. Test (two shots of a dome at its shadow edge): pixels that change
  1.24% -> 0.23%. The facet stays as hard an edge as the mesh is: the game's smooth shading and the real geometry differ
  there.
- **River: 53 -> 41 ms a frame** (camera down at the water, 19 -> 24.5 fps), same picture. Rays from surfaces facing away
  from the sun walked every grass field they crossed although they ignore grass (sun rays 17 -> 8.6 ms). The short
  grass rays of PT AO and RT AO now use the still copies of the grass (a quarter of the cost, Light Bounce 10 -> 6 ms); only
  the sun's rays use the swaying grass.
- **Level of detail changes at once**: the old level's copy now leaves the scene the frame after the game stops drawing it,
  even while the same mesh is still drawn elsewhere (it took up to 8 frames). Why it ever waited: still meshes are only
  updated every 4th frame to save CPU, so "not updated" could mean "not its turn".
- **Freeze report**: the log now names the leaf meshes being built when the GPU hangs. Checked with a forced GPU reset:
  reflections, RT AO and Light Bounce all work after ray tracing restarts.
- After a GPU reset, GPU Micromap Baking, Swaying Shadows Range, Off-Screen Sway, Shader Execution Reordering and SHaRC stay
  paused until the game restarts (by design); a second reset leaves ray tracing off.

## v115 (2026-10-30; renodx-tw-dlaa64.exe helper58: ship them together)

- **RT AO, SSAO and GT-VBAO no longer dark after Path Tracing**: switching Light Bounce from Path Tracing to Rays or Off
  left the game's ambient light switched off (a texture PT AO binds stayed bound; the Light Bounce redraws the lighting
  itself, which skipped the step that unbinds it).
- **Specular Occlusion works with RT reflections on**: the rougher surfaces (the ones the reflections leave to the game)
  ignored it before; now it darkens the game's reflection there.
- **Water Clarity and Water Caustics work**: their hook was never registered (also true for the river / lake /
  particles' ray-traced shadows under Ray-Traced Shadows, torch shadows, fog shadows and Smoke Shadows: all hooked now).
  Your Clarity 16 / Caustics 2 are strong: the river turns see-through; tune them.
- **No fps loss while the camera moves, grass shadows no longer darker until it stops**: grass copies were doubled in
  the ray tracing scene while moving (each blade's height changes with the camera's distance). Test, camera moving:
  sun rays 3.0 -> 1.3 ms, Light Bounce 10.7 -> 6.1 ms.
- **Level of detail changes**: the old level's copy now leaves the scene at once when the game stops drawing it (it
  darkened the new one for 16 frames); 8 frames at most otherwise.
- **Less noise in RT AO, PT AO and reflections under Ray Reconstruction**: their own smoothing over frames is back (v107
  / v108 left it all to RR, which leaves part of the noise).

## v114 (2026-10-30; renodx-tw-dlaa64.exe helper57: ship them together)

- **Ray tracing no longer stays off after a one-off error** (your debug32: one request failed at the battle's start;
  when you later turned Specular Occlusion off, ray tracing could not start again until a restart, so the RT / PT
  toggles did nothing and the fps went up). Such an error now costs one frame. If it happens again, renodx-tw-dlaa64.log
  names the passes of the failing request (it did not happen in game with the D3D12 debug layer).
- **PT AO no longer "cooked"**: the game's sun light shaders scale their ambient (x1.44 on your desert map) and their
  foliage ones add a sky sheen from the same ambient cube. PT AO took the whole cube away, sheen included, and its sky
  ignored that scale: darker and warmer. Now only the game's diffuse ambient is replaced, at the game's scale. In game,
  same view: PT AO vs RT AO -0.4% brightness, blue -3% (the sand's bounced light, which RT AO does not have).
- **GPU micromap bakes no longer hold up the frame**: they run after the frame's ray tracing, beside the game (a battle's
  start bakes up to ~70 ms of them).
- The D3D12 debug layer's only complaint (GPU timers read before they were written) fixed; no visible change.

## v113 (2026-10-30, untested in game; renodx-tw-dlaa64.exe helper56: ship them together)

- **GPU micromap bakes run side by side**: with GPU Micromap Baking, a frame's leaf / grass bakes now run together
  (step by step) instead of one after another with a pause after each of their ~11 small passes. Test: 33 large bakes
  171 ms -> 68 ms of GPU time, 4 bakes 18 -> 11 ms; real leaves are smaller, so they should gain more.
- **No more CPU bakes** with GPU Micromap Baking on: what does not fit a frame's budget waits and is baked on the GPU over
  the next frames (its UVs and mask are copied right away). Before, the overflow went to the CPU.

## v112 (2026-10-30, untested in game; renodx-tw-dlaa64.exe helper55: ship them together)

- **Reflection Reuse: Own ReSTIR** (Experimental, the third choice next to From ReSTIR GI, so you can compare; needs no
  Path Tracing): rough reflections (roughness 0.3-0.75) keep their own samples (at half resolution, ~130 MB at 4K) and
  reuse them over frames and between neighbouring pixels on the same surface. Test scene, 1 reflection ray a pixel:
  colour error 0.114 against plain rays' 0.148; after the reflections' smoothing 0.092 against 0.175 (plain 1-ray
  reflections also came out too dark there; Own ReSTIR not). The reflection pass costs ~60% more.
- From ReSTIR GI is unchanged; in the same test it was worse than plain rays (0.30 against 0.09 after smoothing, 4 rays):
  compare all three in game.

## v111 (2026-10-30, untested in game; renodx-tw-dlaa64.exe helper54: ship them together)

- **PT AO closer to RT AO**: what blocks the sky now follows RT AO's rules: its range (the AO Range setting; before, PT
  AO used the Light Bounce range, e.g. 50 m against RT AO's 6 m, so far more of the sky was blocked) and grass close by.
  Light bounced off nearby surfaces stays.

## v110 (2026-10-30, untested in game; renodx-tw-dlaa64.exe helper53: ship them together)

- **Grass black after moving the camera**: v108 made grass skip its alpha tests (its micromaps taken as solid or clear),
  so a little bit of blade made a whole patch of a grass card solid. The still grass copies, shown while the camera moves
  with W/A/S/D and past the swaying range, then blocked the sun: whole fields dark. Grass does its alpha tests again (as
  before v108). **Fast Leaf Shadows** (Experimental) still skips them, for leaves and grass, if you want the fps back.
- The AO rays do their alpha tests again too (v109 had them skip them; same dark patches).

## v109 (2026-10-30; renodx-tw-dlaa64.exe helper52: ship them together)

- **The freeze was the GPU micromap bake** (not the reflections: the log named a request made after the freeze). A bake
  goes through every texel its triangles cover; a triangle stretched over more than one texture width (garbage UVs in
  some of the game's meshes) could take seconds. Such triangles are now left to the rays' alpha test, so no bake can
  run long. The freeze log now names the right request.
- **More GPU micromap bakes**: up to 64 a frame (131072 triangles); past that the CPU bakes them. Before, leaves past
  the GPU's share (24) never got micromaps. The profile has the bakes' GPU time (gpu_bakes, gpu_bakes_ms).
- **PT AO fixed (dark, warm, "cooked")**: ReSTIR GI (always on with Path Tracing) dropped every ray that reached the
  sky, so PT AO was only the ground's warm bounce light, getting darker over the reservoirs' 1-2 s. The sky is in the
  reservoirs now, scaled so open ground gets exactly the game's own ambient (its formula, its colour). Checked in game:
  PT AO and RT AO now match within a few levels over the whole picture (debug30's PT AO was clearly darker and warmer).
- **Grass going dark after camera moves**: new grass has no micromaps for 1-2 s (its cluster set, bake and templates),
  so each blade is an alpha test; past 64 of them a sun ray counted as blocked: black. Now it counts as lit (the grass
  is only briefly a bit lighter instead). Not reproduced in my replays (no way to zoom into dense grass there).
- **AO cheaper in forests and fields**: the AO rays take leaves' and grass's micromaps as they are (2-state: no alpha
  tests; soft AO barely changes).
- **Coloured Shadows** (new, Ray Tracing section, on): sunlight through cloth, sails and banners takes their colour. Needs
  Light Through Leaves and Ray Reconstruction (the colour comes from RR's albedo where the screen shows the cloth).
  Checked in game: the sun shaders take it and the picture is unchanged elsewhere; the coloured light itself is
  untested (no banners close to the camera in my replays).
- **Reflection Reuse (Experimental, Off by default)**: "From ReSTIR GI": rough reflections also take the path-traced
  Light Bounce's ReSTIR GI samples (less noise; needs Path Tracing). Runs in game; how it looks is untested. The second
  option ("Own ReSTIR": reflections with their own reservoirs) is not done yet.
- Known: on one desert map the first ray tracing request of a battle fails once (also in v108; harmless: the next
  frame traces).

## v108 (2026-10-30, untested in game; renodx-tw-dlaa64.exe helper47: ship them together)

- **PT AO no longer goes dark**: its first 1-2 s were still the game's ambient (PT AO takes over after a short warm-up,
  now 30 frames). Two faults made it dark: the game's AO / GT-VBAO stayed on and stacked on it (now off with PT AO, as
  with RT AO), and light bouncing off walls and ground came back without their ambient light (now added).
- **Grass and rivers faster**: the sun's shadow rays (22-27 ms zoomed into grass or over a river's grassy bed in ATN's
  trace) test grass only over their first stretch, where a blade can still be above them (2.5 m of rise), and grass
  with micromaps skips its alpha tests (2-state, as NVIDIA suggests for shadow rays). Test field: 4.8 -> 2.1 ms.
- **Grass shadows settle sooner**: three times as many grass micromaps baked a frame on the GPU (new grass's shadows
  were dark for 1-2 s until baked).
- **Ray-Traced Light Bounce on its own**: with Rays or Path Tracing, the screen-space Light Bounce (GT-VBGI) no longer
  shapes it: its own resolution (new **Ray-Traced Light Bounce Resolution**: Full / Half / Quarter, Half by default;
  before it ran at the screen-space one's, e.g. a quarter), every texel each frame (no Checkerboard), and under Ray
  Reconstruction its light goes in as traced (no screen-space resolve or denoiser: RR denoises it).
- **Water Caustics work now**: v107's never reached the water (another patch changed the water shaders first).

## v107 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper46: ship them together)

- **After a GPU freeze, RR and frame generation work again**: when the GPU drops the helper's ray tracing device, the
  fence shared with the game was stuck, so nothing waited for the helper any more (RR and MFG broken, fps "better"). The
  addon now makes a new one at once. Ray tracing starts again once, with GPU Micromap Baking and the Experimental
  swaying shadows options, Shader Execution Reordering and SHaRC paused until the game restarts; a second freeze turns
  ray tracing off for the session (four in a row made Windows refuse the helper new GPU devices, frame generation's
  too). RR and frame generation get new devices after a reset and retry at the next set. The log names each pass and
  buffer where the GPU stopped.
- **River / grass fps**: grass meshes first drawn before the helper took compact draws (a battle's first frames) were
  built with their clusters each over a whole grass patch: rays through grass cost about 4x (test field with ATN's
  settings: 30 ms -> 7.6 ms). They are now ordered by place; the log counts "unsorted" sets (should stay 0).
- **Surfaces black for a few seconds**: an object's coarser level of detail stayed in the ray-traced scene for 300
  frames after the game switched to a finer one while zooming, darkening it. Objects not drawn for 16 frames now leave
  the scene when they are inside the view (off-screen ones still cast shadows).
- **NVIDIA's guidelines**: GPU Micromap Baking is on by default (NVIDIA's OMM SDK: the GPU baker for baking in game; no
  longer Experimental). Under Ray Reconstruction the AO goes to RR raw (RR replaces denoisers that accumulate frames).
- **Water Caustics (Experimental, 1 by default, 0 off)**: rippling light on river and lake beds, seen through the water
  (the game's two water shaders get it as they are created; the caustics follow the world, 4 m tiles, drifting).
- Path Tracing: ReSTIR GI reuses every ray already; in the test scene 4 rays left 0.080 noise, 16 rays 0.064 at 1.5x the
  cost: 4-8 rays are enough with it.

## v106 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper44: ship them together)

- **SHaRC Radiance Cache (Experimental, with Path Tracing)**: NVIDIA's SHaRC holds the light of what the screen does not
  show (more bounces off-screen, paths end sooner); about 0.4 ms at 4K in the test scene. The reflections keep the old
  cache (SHaRC there hung the GPU at 4K in tests).
- **Importance-Sampled Torch Shadows (Experimental)**: two torch / fire shadow rays a pixel, picked by how much light
  they give (as ReSTIR DI picks lights); 32 fires at 4K: trace 2.98 -> 2.47 ms. Dim fires' shadows follow moving units
  slower.
- About: SHaRC credited (NVIDIA RTX SDKs License: SHARC_LICENSE.txt; this software contains source code provided by
  NVIDIA Corporation).

## v105 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper41: ship them together)

- **Menu frame rate**: the ray tracing's draw hooks and DLSS's motion redraws stop where they have nothing to do (the
  menus, before the helper runs).
- **Ray-Traced Water Reflections**: rivers and lakes reflect by ray tracing from their own surface (their depth, smooth),
  on by default with Ray-Traced Reflections; off keeps their screen-space reflections.
- **Path-Traced Ambient (Experimental)**: with Path Tracing the paths bring the sky's light in place of the game's
  ambient light and the ray-traced AO (less noise, no AO pass; small contact shadows follow the Light Bounce's
  resolution).
- **GPU Micromap Baking (Experimental)**: Leaf Micromaps baked by NVIDIA's GPU baker, as NVIDIA's guide asks (no
  mid-frame read-backs, inputs stay in VRAM, a budget of 8 bakes a frame): frees the CPU threads.
- **Swaying Shadows Range (Experimental)**: trees and grass sway in the ray tracing only this close to the camera; farther
  ones stay still (their still copy): faster. 0 m: everywhere.
- **Off-Screen Sway (Experimental)**: off: trees and grass away from the view stay still in the ray tracing.
- **Fast Leaf Shadows (Experimental, with Leaf Micromaps)**: leaves skip their alpha tests (2-state micromaps) for the
  sun's, the AO's and the torches' rays.
- **Grass Shadow Range (Experimental)**: past it grass casts no ray-traced shadows (4K test field: trace 4.1 -> 2.1 ms at
  3 m). 0 m: everywhere.
- **Path Tracing SER (Experimental)**: shader execution reordering for Path Tracing and Ray-Traced Reflections (slower in
  the test scene; for tests in big cities).

## v104 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper36: ship them together)

- **Ray Reconstruction keeps dark tones**: every RR preset lifted shadows below ~1.4 nits to one flat violet level (the
  units' legs and their shadows). RR now gets the colour's square root, as DLSS presets L and M do.
- **Water reflections with Ray-Traced Reflections**: rivers and lakes keep the screen-space reflections. The rays left
  from the riverbed (the water is drawn after the G-buffer they start from): a bright sky and none of the bridge.
- **Water Clarity** goes up to 16 (murky rivers needed more than 4), and the log says once where it applies.
- **Torch and Fire Light in Fog and Smoke**: also on the particles' second shader (fire smoke), and as strong as the
  game's own sunlight on them (it was about 12 times weaker).
- **Grass Always Drawn**: grass outside the view (with 16 m to spare) stays out of the ray-traced scene. With it the game
  draws every grass cell, also behind you, past what the scene holds (trees' and grass's shadows flickered).
- **Grass shadows settle faster**: grass's micromaps one level coarser (a quarter of the baking).

## v103 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper35: ship them together)

- **Shadow leaks fixed**: surfaces facing away from the sun are traced too (they were left lit, trusting the game's
  shading to darken them): grass behind a big wall and the edge of a tower were lit in the wall's shadow.
- **Grass shadows settle faster**: new grass's opacity micromaps bake 4 at a time, grass first (about twice as fast).
  Before they are baked, dense grass is darker for a moment (the old delay was 2-3 s).
- **Sun shadows with Light Through Leaves**: a pixel in the open takes one ray instead of two.
- Mega Geometry's memory grows in steps of half again (it reached 4.9 GB in the third profile).

## v102 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper34: ship them together)

ATN's second profile (the same place, v101): frame 45 -> 33 ms, the trace 17 -> 9 ms, reflections 6.3 -> 1.3 ms, Light
Bounce 3.1 -> 1.0 ms; the sun and AO now 8.7 of the trace's 9 ms.
- **Faster swaying grass (and units, swaying leaves)**: each mesh's ray-traced clusters are grouped by place (they
  followed the mesh's triangle order: a grass patch's clusters each spanned the whole patch). A 4K test field of swaying
  grass: its share of the trace 0.34 -> 0.05 ms.
- **Swaying Grass Shadows** (On / Off, in the Ray Tracing section): Off puts all grass in still, without the swaying
  grass's per-frame work (~1.5 ms in the profile) and its memory (up to ~3 GB of VRAM).
- **Fewer memory regrowths**: Mega Geometry's memory at least doubles when it grows (it regrew 12 times as the grass came
  into view, reallocating GBs each time).
- More room for placed grass (262 K instances; the profile reached 126 K of 131 K).
- **Profile**: the sun's and the AO's costs timed apart.

## v101 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper33: ship them together)

From ATN's first profile (Ctesiphon, 4K, every ray-traced effect on, grass maxed): the helper's GPU work was ~28 ms of a
45 ms frame (the trace 17 ms, reflections up to 22 ms); the game's thread barely touched (0.4 ms).
- **Grass much cheaper in ray tracing**: grass is its own group in the scene now. The sun's rays still meet it (a blade
  blocks the sun, also with Light Through Leaves) and the AO's rays within 0.5 m, but point light shadows, fog, Light
  Bounce, path tracing and reflections pass through it; grass off-screen leaves the scene after a few frames instead
  of 5 s. A 4K test field of 3721 grass patches, every effect on: 13.7 -> 7.1 ms (reflections 5.7 -> 2.4, trace 4.9 ->
  2.6, Light Bounce 1.9 -> 1.2).
- **Less VRAM**: Mega Geometry's memory grows to what a battle needs (half again) instead of doubling (the profile's
  166 K clusters had made it 4.8 GB).
- **Profile**: Record Ray Tracing Profile now times each pass too (sun and AO, point lights, fog; radiance cache, paths,
  reuse, reflections), and its per-frame counts are filled in.

## v100 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper32: ship them together)

- **Record Ray Tracing Profile (30 s)** (end of the Ray Tracing section): every frame's ray tracing costs into
  tw_rt_profile.csv, the helper's requests (each pass's CPU and GPU time, the scene's counts) into
  tw_rt_profile_helper.csv, both next to the game's exe. The data for the next optimization batch.
- **Less work on the game's thread**: new meshes' clusters are made on a worker thread (they were made inside the
  game's draws, up to 16 meshes a frame: stutter when new meshes came into view).

## v99 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper31: ship them together)

- **Light Through Leaves much cheaper**: sun rays no longer step through every leaf layer of a forest; they count the
  leaves they cross (none, one, more) after the rest of the scene. Dense leaves at 4K: 16.2 -> 2.5 ms (0.9 without
  it). Over forests this was the 17-18 ms traces and the stutter while panning.
- **Fewer hitches**: Mega Geometry's memory grows on a worker thread before it runs out (each growth, up to 2.5 GB,
  stalled a frame); new leaves' micromap reads make 3 buffers a frame instead of 2-3 each.
- **Grass no longer left out**: grass past its share of the frame (Grass Always Drawn, high density and distance) goes
  in as still patches instead of being dropped (whole fields lost their shadows); the share's grass still sways. Far
  blades shrink as the game draws them.

## v98 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper30 and bridge v18: ship them together)

- **One Light Bounce tracer**: Ray-Traced Light Bounce is now Off / Rays / Path Tracing (path tracing and rays could
  both be on before; only one ran). Path Tracing must be picked again once. While rays or paths find the light, the
  Lighting section's screen-space search settings (Radius, Quality, Directions, ...) are greyed out.
- **Grass micromaps**: with Leaf Micromaps on, grass gets opacity micromaps too, so most grass alpha tests run in the
  hardware (50,000 grass cards in view at 4K: tracing 3.6 -> 2.7 ms).
- **Soft torch and fire shadows** (with Ray Reconstruction): Torch and Fire Shadow Softness (their size, default
  0.25 m; 0 sharp).
- **Torch and fire light in fog and smoke**: with Ray-Traced Fog Shadows, torches and fires light the fog around them
  (their shafts and shadows in it); smoke and dust near them are lit too. Torch and Fire Light in Fog and Smoke (0-2,
  default 1; ~0.1 ms).
- **Ray Tracing at Output Resolution**: below 100% render scale, as many shadow, AO and reflection rays as at DLAA (up
  to 4 a pixel): less noise and finer shadow edges at DLAA's ray tracing cost.
- **Water Clarity**: see further into rivers and lakes (1: the game's).
- **Path tracing**: paths that escape bring the game's own sky light by direction (bluer from above), leaves, cloth
  and sails pass light through (green light under trees, sunlit leaves glow from behind), two torch lights sampled a
  bounce.

## v97 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper29: ship them together)

- **Smoke costs almost nothing now**: Smoke Shadows' particles are a part of the ray-traced scene only the sun's (and
  the fog's) rays see; a sun ray stops once the smoke has darkened it. 1000 particles in view at 4K: tracing 81 -> 8 ms,
  reflections 35 -> 1.5 ms, Light Bounce 13 -> 0.8 ms.
- **Faster reflections and Light Bounce in foliage**: with Leaf Micromaps their rays skip the alpha tests (leaves in
  view: reflections 39 -> 21 ms, bounce 18 -> 13 ms). AO and torch shadows keep them (without, rays through a canopy
  never stop: 5x the cost).
- **City wall cracks no longer lit**: at corners, cracks and edges (where the normal from the depth can be wrong) rays
  start toward the camera, and those pixels are traced even when they seem to face away from the sun.
- **~3 GB less VRAM**: Mega Geometry's memory starts at 330 MB (was 3.3 GB) and grows only when a battle needs more.

## v96 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper28: ship them together)

- **Less AO noise under Ray Reconstruction**: tested with RR itself (the game's nvngx_dlssd 310.9.16, preset F) on 4-ray
  AO, RR left 19-23% of the noise. The AO rays now spread over the hemisphere (stratified, turned per pixel by
  interleaved gradient noise) and the AO keeps its own history under RR again: what RR leaves is a third of before
  (harness, wall foot: 0.0245 -> 0.0080).
- **More grass in the ray-traced scene**: the grass share follows the GPU's memory, 4x on GPUs over 20 GB (16M corners;
  ~70M grass vertices a frame were left out), 2x over 12 GB.

## v95 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper27 and bridge v17: ship them together)

- **Ray Reconstruction is the only denoiser**: under RR the reflections' denoiser and history, the helper's ray-traced
  AO history and the soft-shadow filter are off (RR denoises them); soft sun shadows trace one ray toward a point of the
  sun's disk instead. RR gets the reflections' **specular hit distance** (how far each pixel's reflection rays went:
  reflections in motion), **32 jitter phases** (NVIDIA's minimum for RR), NVIDIA's sky guide values, and ReSTIR GI's
  reuse varied per frame (RR expects independent samples).
- **Ray-Traced Ambient Occlusion On replaces** the game's ambient occlusion and GT-VBAO (Alone folded in). **Units and
  grass** are always in the ray-traced scene (their toggles are gone).
- **4x battles and walls**: the shares are bigger (8192 vertex rows, 32M triangles, 524,288 clusters on GPUs over 20 GB)
  and units take at most three quarters of them, so walls and buildings drawn after the soldiers keep a quarter. Rocks,
  cliffs and other meshes blended into the terrain (rigidterrainblend) are in the ray-traced scene now.
- **Faster light bounce and reflections**: the helper's paths no longer use shader execution reordering (measured slower
  in every pass: 4K bounce 1.18 -> 0.84 ms, reflections 2.82 -> 2.11 ms in the test scene).
- **Many fires**: past 8 lights reaching a pixel, the 4 most important are traced every frame and the rest in turns,
  keeping last frame's shadow where the surface was seen (4K, 32 lights reaching every pixel: 6.64 -> 5.17 ms).
- **Static meshes share memory pools** (NVIDIA RTXMU's approach): no driver allocation per mesh (64 KB at least each);
  the helper logs the pools when they grow.
- Log: the leaf micromap tally at most every 10 s, the Light Bounce's and reflections' GPU times only while they run.

## v94 (2026-10-29, untested in game; renodx-tw-dlaa64.exe helper26, bridge v16 and omm-lib.dll: ship them together)

- **Ray-Traced Shadows are every shadow**: the sun's (the game's sun shadow maps are no longer drawn and Screen-Space
  Shadows turn off with them), the torches' and fires' (Ray-Traced Point Light Shadows folded in: a drawn light takes
  the nearest traced one within 25 cm, carried torches keep theirs) and banners' and sails' (always cast). The forward
  passes that read the game's shadow map (near grass, particles) take the rays' shadows. The separate Shadows Only and
  Point Light toggles are gone.
- **Moving shadows**: trees' leaves sway in their shadows (placed leaves streamed every frame with the wind within a
  budget, their still copies hidden while drawn; branches stay still). Grass and banners were captured every frame with
  the wind already.
- **4x unit size battles**: compact capture (bridge v16): a soldier mesh's indices are read back once into cluster sets
  and its vertices stream once each (not three a triangle), the terrain's too; 262,144 clusters a frame on GPUs over
  12 GB (half on smaller ones). Placed meshes (bridge v14): rigid objects and trees keyed by their geometry, built once,
  placed on the GPU every frame, kept 5 s off-screen.
- **Faster scene builds** (RTX Mega Geometry cluster templates): a mesh's clusters are built once as templates and
  instantiated each frame with their new positions (harness, 80,000 clusters: scene 1.48 -> 1.08 ms on the GPU).
- **Leaf Micromaps (Experimental, on)**: NVIDIA's Opacity Micro-Map SDK bakes new leaves' opacity micromaps on the CPU
  (the GPU bake that reset the GPU is gone), so shadow rays through trees run fewer alpha tests. omm-lib.dll and
  OMM_SDK_LICENSE.txt ship with the helper; its log names each batch ("Leaf micromaps: N meshes baked").
- **Ray-Traced Smoke Shadows (Experimental, off)**: smoke and dust dim the ray-traced sunlight they drift through, a
  frame late (the lit particles streamed out, made soft disks facing the sun).
- **Lighter helper**: its effects' histories (paths, ReSTIR GI, reflections, AO: ~1.3 GB at 4K) are made at first use
  and released after 600 frames unused.
- **Robustness**: garbage triangles left out before the BLAS builds; after a GPU reset the helper logs where the GPU
  stopped (DRED) and starts a new device; the terrain captured without the game's camera-distance height push (the
  black circle under the camera); the status line shows its GPU times first; a cost report every 10 s; the Debug
  View's scene tests.

## v93 (2026-10-28, untested in game)

- **Screen-space shadows no longer darken the campaign's sky** (ATN debug21, DLAA: the clouds past the map's edge
  shadowed up to a hard edge). ATN's log: that backdrop's depth is 0.99935 with the campaign's depth at infinity
  1.002, three quarters of the way to the far plane, a lit mesh whose depth comes nearer towards the top, so Bend
  shadowed it against itself (the game's shadow maps never reach it). Bend's shadow now fades out from 40% to 60% of
  the far plane's distance (bend_sss_gpu.hlsli FarFade: (depth_inf - 1) / (depth_inf - depth), the light's z being
  the depth at infinity). Battles' far plane is 13 km away. All TW games.

## v92 (2026-10-28, untested in game)

- **Screen-space shadows' Debug View goes through DLSS** like the shadows in the picture (ATN debug20: at Ultra
  Performance it showed the render-size mask stretched after DLSS, 3 x 3 pixel blocks the game never shows). It is now
  drawn into the tone mapper's render-size part before the anti-aliasing (the tone mapper hooks call screen-space
  shadows first, once a frame), so DLSS upscales it from its jittered frames. The shadows themselves feed the lighting
  at the render size, as everything in the 3D image does: Effects at Output Resolution cannot make them finer.

## v91 (2026-10-28, untested in game)

- **Screen-space shadows below 100%** (ATN debug19, Ultra Performance and Quality, the toggle made no difference):
  distant hills striped with false shadow and fence shadows combed. Bend's two depth tolerances are per pixel, tuned
  for one pixel per step: below 100% a render pixel holds more of the scene, so the depth steps between neighbours and
  along the ray grow by 1 / render share. Its bilinear threshold (slopes read as edges, not pushed back, shadowed
  themselves: the stripes) and its surface thickness (thin occluders fell between the samples of every other line:
  the combs) are now divided by the render share, as the sample count is multiplied by it. All TW games.

## v90 (2026-10-28, untested in game)

- **Frame generation's HUD-less image** now comes from hdr_to_screen (the HDR output, grading and fog of war), not the
  tone mapper before it: ATN's debug views showed it much darker and the UI layer covering the whole screen (so HUD
  Recomposition treated every pixel as UI). anti_aliasing.hpp: a later tone mapper hook of the same frame (after DLAA
  ran) redraws it (frame_generation::KeepOutput); once a frame has that pass, KeepFrame stops drawing it at the tone
  mapper (it comes back if a frame lacks the later pass). Same for Thrones and Rome II (their hdr_to_screen passes).

## v89 (2026-10-28, untested in game)

- **Frame Generation Debug View** steps aside while the ReShade menu is open (ATN: the HUD-less image has no menu in
  it, so the menu could not be seen to turn the view off).
- **Below 100%, the AO blur and the water refraction keep their 100% detail**: the game draws them at half the
  display's size (ssao.hlsl blur_x / blur_y, which every light reads as s_ssao, and waterrefraction.hlsl, which water and
  rivers read as s_terrain_refraction), and the render scale halved them again (a quarter of the display at
  Performance). They now draw their whole half-size target (view_share.hpp RegionRead::whole, their inputs read at the
  render share), and their readers read them over the draw's share (WHOLE_TEXTURES, patched by name: 78 light and 16
  water shaders; ssr00 / ssr01 and point lights 00 / 02 / 04 / 06 in HLSL). Always on below 100% (as cheap as at
  100%). WARP check of every Attila / Thrones / Rome II dump shader: all patched ones create, no other patch changed.
  The rest of a 50% frame trace is at the render size or the display's: bloom (a fixed 256 x 256 chain from the whole
  render), exposure, lens flares, depth of field, heat haze, markers and UI after DLSS; shadow maps and the reflection
  cube have fixed sizes.

## v88 (2026-10-28, untested in game)

- **Effects at Output Resolution** (Anti-aliasing, on by default, as Tales of Arise's; all TW games): while DLSS renders
  below 100%, the effects sized in render pixels follow the render scale and keep their 100% detail (ATN debug18:
  softer reflections and shadows at low DLSS modes). Ambient occlusion's Resolution x the render share (at least
  full), Light Bounce's Resolution one step finer per step of 2, Reflection Rays Half / Quarter up by the steps per
  area (Quality: Quarter traces Half), the game's reflection blur (reflections_blur, in texels of the full-size
  target) x the render share, Screen-space Shadows' Softness x the render share (nearest pixel), ray-traced ambient
  occlusion's Quarter rate off from one step of 2 down. Only while DLSS really scales (not in menus).

## v87 (2026-10-28, untested in game; renodx-tw-dlaa64.exe helper24, bridge v13: ship them together)

- **HUD Recomposition** now gives DLSS-G a one-byte **UI alpha** (kBufferTypeUIAlpha, NVIDIA's faster UI input) in
  place of the 8-byte UI colour and alpha: an eighth of the layer's writes and of Streamline's copy each frame (ATN:
  120 fps without recomposition, 115 with it).
- **Frame Generation Debug View** (Off / HUD-less Image / UI Layer): the helper presents what DLSS-G gets, the HUD-less
  image (any HUD in it smears) or the frame with its UI layer in magenta (the parts laid over the generated frames).
  GPU test (fg_bridge_test, 32-bit through helper24): 2x with and without recomposition and both views, "HUD
  recomposed" with the UI alpha.

## v86 (2026-10-28, untested in game)

- **NVIDIA Reflex** (Off / On / On + Boost; replaces Reflex Boost, all TW games): Reflex without frame generation, with
  any anti-aliasing. reflex.hpp sets NVAPI's sleep mode on the game's D3D11 device, calls its Sleep right after each
  present (ReShade's finish_present) and sends the latency markers; with frame generation presenting it steps aside
  (the helper's Streamline runs Reflex). NVAPI loaded by function id (no 32-bit nvapi.lib; nvapi.hpp, now shared with
  object motion's UAV overlap). 32-bit GPU test on the RTX 5090: NVAPI takes the structs, the driver reports low
  latency on and the game's sleep used, a 30 fps limit gives 32.8 ms frames. **Frame Rate Limit** works with Reflex
  on too (it needed frame generation).
- Frame Generation Preset's tooltip: what nvngx_dlssg 310.9 does with them (EndpointConfiguration::ReadRegkeys, DRS
  0x10E41DF1): A forces UI recomposition off, B on, C (Lite: candidate blending) off. NVIDIA's DLSS-G indicator shows
  no letter: DLSSG.UserInterfaceRecompositionEnabled and DLSSG.LiteCandidateBlendingEnabled show the result.

## v85 (2026-10-28, the Nexus beta)

- **SDR (monitors without HDR)** preset (yellow): Output SDR, Recommended without the VFX boost, no video AutoHDR. In
  SDR output the game's and the UI's white are the monitor's own, so it sets no Paper White or UI nits.
- **UI 4K Mod Compatibility** is On by default (no DisableGraphics in ReShade.ini): the engine settings stay off until
  it is turned Off and the game restarted. A red note says so next to it and on top of Graphics 2 of 2.
- DLAA / DLSS **Preset L** by default (all TW games: shared settings).
- A red note above the ray tracing settings: they do not work correctly yet (all TW games).
- Frame Generation's tooltip: 3x to 6x and Dynamic need an RTX 50 (2x: RTX 40).
- About: NVIDIA DLSS, Reflex, Streamline and NVAPI attribution (NVIDIA's license asks for it there), Microsoft's Agility
  SDK, kajiya and Phantom FX.
- Nexus zip RenoDX_Attila_Beta_v85.zip: the files at its root (next to Attila.exe), nvngx_dlss.dll included now (the
  NVIDIA RTX SDKs license allows it inside an application), a licenses folder and a player read-me.

## v84 (2026-10-28, untested in game)

- **Below 100%** (DLSS modes and custom render scales):
  - **Grass** looks as at DLAA all over the screen. Outside the top-left part it read the ground's distance from the
    wrong place, so the blades did not blend into the ground: more, darker grass, reaching farther with Grass Detail
    Distance.
  - **Unit markers** above the units show all over the screen again (they disappeared in the top-left part): the
    scene's depth is stretched to the full screen for them.

## v83 (2026-10-28; the night batch untested in game, only smoke-run in a replay)

- **Frame generation**:
  - **HUD Recomposition** (on by default) lays the HUD over the generated frames instead of warping it with the scene,
    so the HUD no longer smears. The helper takes the UI as the pixels where the presented frame differs from its
    HUD-less image.
  - **Frame Generation Preset** (Driver's / A / B / C) chooses NVIDIA's DLSS-G preset. DLSS-G reads it from the NVIDIA
    driver profile of renodx-tw-dlaa64.exe (key 0x10E41DF1), so the helper writes it there; it applies after a game
    restart. In nvngx_dlssg 310.9: A plain, B UI recomposition, C "FG Lite" (lighter).
  - The game's GPU no longer waits for the helper's copy right after each present, only before its next writes.
  - The Frame Rate Limit slider shows its value.
  - NVIDIA Control Panel's global VSync applies to renodx-tw-dlaa64.exe and caps frame generation at the refresh rate:
    turn it off for that exe.
  - renodx-tw-dlaa64.exe **helper23** (bridge v12) ships with this version and is needed by it.
- **Below 100% render scale (DLSS modes)**:
  - The lens flare and the unit markers above the units show again (the scaled scissor rectangles stayed bound after
    the tone mapper). The flare's sun occlusion is counted at the render size, and is now corrected for it.
  - Fixed: the bloom's position, the game's SSR, Ambient Occlusion Game's (its positions and radius).
  - Fixed: screen-space shadows (no lines at some scales).
  - The grass's 100 m cut now follows Grass Height Distance, so grass shows past 100 m.
- DLSS presets **L / M** no longer crush dark tones (they get the colour's square root) or pulse on armour.
- Less CPU per frame: the DLSS helper's requests no longer wait for it, and object motion keeps its bindings across
  moving draws.

## v82 (2026-10-27, tested in game in battle replays; torches untested)

- Torch light and the battle fog are right again below 100% render scale (the DLSS modes): the point light and fog
  shaders replaced for their ray-traced shadows had lost the render scale's correction, so torch light slid toward the
  bottom right.
- **Path Tracing** and **Ray-Traced Light Bounce** no longer switch ray tracing on while the game's Light Bounce is off
  (they have nothing to trace then): every unit and plant was captured each frame for nothing (day siege, 4K DLAA + Ray
  Reconstruction: the game's CPU time 14.2 -> 7.8 ms a frame; far more with soldiers close).
- **Ray Tracing Debug View**: Screen-Space AO (the game's or GT-VBAO's ambient occlusion); All now tiles eight. A crash
  when it was on before ray tracing started is fixed.
- Frame generation needs Output HDR10 or SDR (scRGB: it does not generate, the log says so).

## v81 (2026-10-27, tested in game: the campaign and a battle replay)

- Campaign map: **Ray-Traced Sun Shadows** now work there. The render scale's patch changed 8 sun shaders (the
  campaign's among them) before the shadow patch saw them, so neither the ray-traced nor the screen-space shadows
  reached them. And the campaign's sky dome (a rigid mesh 900 units across, around the camera) no longer blocks every
  sun ray (a static mesh holding the camera and larger than the view reaches casts no shadow).
- **Ray-Traced Shadows Only** (Experimental): the game's sun shadow maps are not drawn, the rays alone cast sun
  shadows (battle replay, 4K DLAA: 12.1 -> 11.9 ms a frame).
- **Ray-Traced Ambient Occlusion** kept over frames: the campaign's houses no longer shimmer (pixels changing between
  frames: 2.3-3.0% -> 0.5-0.7%; without ray tracing 0.9%).
- **Ray Tracing Debug View**: one ray-traced effect's raw result as the picture, or All seven tiled; magenta: it made
  nothing this frame (off, or not traced).
- The UI's small 3D views (portraits) stay out of the ray-traced scene.
- renodx-tw-dlaa64.exe **helper22** rebuilt: the partitioned TLAS only with RENODX_TW_PTLAS=1 (a city siege stalled
  at 3 fps with it), the ambient occlusion's history.

## v80 (2026-10-27, tested in game in battle replays)

- renodx-tw-dlaa64.exe **helper22** rebuilt: ray tracing no longer stalls the game. The helper answers each ray
  tracing request as soon as it has copied it and records it on a worker thread of its own (battle replay, 4K DLAA:
  Ray-Traced Sun Shadows 72 -> 87 fps, as fast as without ray tracing; everything on: the game's CPU time 18 -> 12 ms
  a frame).

## v79 (2026-10-27, batch 5, untested in game; the helper GPU-tested)

- renodx-tw-dlaa64.exe **helper22** (in the zip; bridge v11, needs this addon): **ReSTIR GI v2** (compared with
  Embark Studios' kajiya, MIT / Apache 2.0, not copied): fireflies clamped, history only on a surface facing the same
  way, and every third frame the reused light is checked again: a light switched on or off shows within 3 frames
  (was about 20). **Partitioned TLAS** fixed (its update operations stalled the GPU after BLAS compaction) and on
  again. **Mesh instancing**: instanced draws build one BLAS placed once per instance.
- **Light Through Leaves** (off by default, with Ray-Traced Sun Shadows): leaves let a quarter of the sun through
  each, cloth and sails about a third; only where the rays are the shadow (past the game's own shadow maps).
- **Ray-Traced Point Light Shadows** (off by default): torches and fires cast ray-traced shadows (the 32
  nearest and brightest; all 8 point light shaders without the game's point shadow maps). With them on, their
  light also reaches what the Light Bounce / Path Tracing meets off screen.
- **Ray-Traced Fog Shadows** (Experimental, off): light shafts: the battle fog loses the sun's glow where it
  lies in shadow (8 rays along each camera ray at a quarter of the pixels).
- Banners, tents and sails are in the ray tracing scene now (with Ray-Traced Units): their shadows, shade,
  bounce and reflections.

## v78 (2026-10-27, untested in game; the helper GPU-tested)

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

## v77 (2026-10-27, untested in game; the helper GPU-tested)

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

## v76 (2026-10-27)

- **DLSS below 100%: the game's own passes** (ATN: post-processing, shadows, reflections and our effects at DLSS):
  twattila/view_share.hpp patches the game's shaders as they are created. Positions rebuilt from texture
  coordinates (point lights, decals, battle fog, water, refraction, particles, the game's SSAO, markers, the SSR
  ray casts) use the view's share of the targets, projected reads land in it, and passes that read the 3D image at
  the display's size (the bloom / exposure downsample: bloom lines up with the image, the tone mapper reads it at the
  view's coordinates; the shadow cascades' depth analysis) read only its rendered part. After DLSS, depth of field, distortion, fog of war and photo
  mode read depth and distortion at the render share (twattila/render_scale.hlsli). WARP test: every patched shader
  of 8 dumps creates. Checked in game at 50% (Troy, Rome II): full frame, frame generation on.
- **Ray-Traced Light Bounce** (Experimental, Anti-Aliasing section, with DLAA / DLSS and the Light Bounce on): the
  Light Bounce finds the light arriving at each texel with rays through the ray-traced scene (cosine-weighted, Rays
  1-16, Range 1-50 m) in place of its screen-space search: a ray's hit gives the prepared light where the camera sees
  that same surface (else nothing, as before), so no thickness guess behind what the camera sees and light from
  farther away. Its resolve, denoiser and apply are the Light Bounce's (rt_scene.hpp TraceLightBounce, helper
  rt_bounce.cs_6_5.hlsl; bridge v7, renodx-tw-dlaa64.exe helper16: the zip's helper must be the new one).

## v75 (2026-10-27, untested in game)

- Ray tracing in the other Total War games (Rome II, Thrones, Troy, Pharaoh, WH1, WH2) with Attila's module; for
  games without an AO pass of the addon's it traces at the sun draws (rt_ambient_combine.cs).
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

## v74 (2026-10-26, untested in game)

- **Frame Generation (Experimental)** (Anti-Aliasing: Off / 2x / 3x / 4x, Reflex Boost; needs DLAA or DLSS): NVIDIA's DLSS
  Frame Generation runs only in 64-bit processes, so renodx-tw-dlaa64.exe presents (dlaa_helper/fg_d3d12.hpp,
  Streamline 2.14.16, as Tales of Arise's: sl.interposer.dll, sl.common.dll, sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll,
  nvngx_dlssg.dll next to it). It puts a child window over the game's (disabled: the game's window keeps the mouse and
  keyboard; Streamline counts a window of its parent process's foreground window as focused) with a D3D12 swap chain;
  the game's own swap chain still presents underneath. frame_generation.hpp keeps each frame's HUD-less image (the tone
  mapper's output after DLAA, before the UI) with DLAA's depth, motion and camera; after ReShade drew its overlay the
  HUD-less image goes through the addon's swap chain shader, the back buffer is copied, and the helper presents both
  (bridge v5; Reflex paces the game's next frame). Needs an RTX 40 or newer GPU, a windowed or borderless game, Output
  HDR10 or SDR (frame generation takes no scRGB); turn the game's VSync off. GPU test (fg_bridge_test, a stand-in game
  window starting the helper through the bridge): 2 frames presented per rendered frame at 2x, 4 at 4x; the mouse over
  the frame goes to the game's window. The same in Rome II, Thrones, Troy, Pharaoh, WH1 and WH2 (their AA settings).
- **Ray tracing: units and leaves** (ATN): soldiers and horses (the 8 skinned main shaders, rt_capture_unit_*.vs) are
  captured every frame (up to about a million triangles, Ray-Traced Units On / Off) and built into one BLAS of that
  frame; leaves (rt_capture_leaf_*.vs) are in the scene as foliage: their alpha-tested holes are not known to the helper
  (the textures stay in the game), so each leaf a ray crosses lets half the light through. GPU tests: the 11 capture
  shaders against the game's (compare_test), rt_dynamic_test (under one leaf layer 0.50, a moving box's shadow follows
  it with none left behind), rt_bridge_test again.
- Object motion moved into its own module (object_motion.hpp, shared with Shogun 2's DLSS addon); unchanged for Attila
  (compare_test, deformed_test, placement_test again). Its depth match is now absolute (4e-6, was 0.1% of the depth:
  far away that let surfaces drawn later in front take the object's motion).
- The limbs' own swing has been in since v71 (each soldier's bones kept a frame in a pool: motion_unit.ps ClaimBones,
  the motion shaders' MotionAnimationLoad). deformed_test now takes any vertex layout: sails, sail symbols and ropes
  checked too (before: cloth only).
- Not done, by design: particles and projectile trails. They are blended (no depth: the motion under them is the
  scene's) and their points mostly stay where they were made, so their own motion would not help DLSS; its transformer
  presets (K, M, L) also ignore the "bias current colour" mask that would.

## v73 (2026-10-26, untested in game)

- Ray-Traced Ambient Occlusion: Off / On / Alone (ATN: run the ray tracing without the screen-space effects). Alone
  replaces the game's ambient occlusion and GT-VBAO (ssao00 outputs none, ao_pass draws nothing; the ray-traced one is
  multiplied in after): their cost saved; units, leaves and grass then get only what the scene casts on them.
  Ray-traced sun shadows already work with Screen-Space Shadows off.
- Fixed: with Global Illumination on, the Light Bounce draws the sun draws itself and no hook after it ran, so the ray
  tracing never got the sun's direction there. The AO pass, the ray tracing, then the Light Bounce (in that order).
- Screen-space shadows: 10 of Attila's sun shaders keep their lighting at b1 (the shared code read b2 there); each sun
  draw now names its own lighting buffer and sun direction (tools\tw\sun_draw_layouts.py, from the dump). Below 100%
  DLSS render scale they keep to the 3D image's corner (Bend's RenderSize).
- renodx-tw-dlaa64.exe: ray tracing BLASes compacted (the GPU's compacted size read back a few frames later, then
  copied; rt_bridge_test 2816 -> 1024 bytes a mesh); switching between Ray Reconstruction and DLSS / DLAA frees the
  other's NVIDIA feature (bridge_test ALTERNATE=1: 118 of 120 frames over three switches); the addon waits up to 5 s for
  a new NVIDIA feature (was 100 ms: changing the preset or Ray Reconstruction in game turned DLAA off).
- Units' motion: last frame's pose at last frame's time (nothing changes for Attila's units: they do not use it; the
  other games' fur does).
- Measured (bridge_test, 2880 x 2880 = 4K's pixels, RTX 5090): the helper's DLAA with preset M 1.46 GB of video memory, K
  0.86 GB, E 0.59 GB, Ray Reconstruction 2.06 GB; the game's side 0.28 GB (0.62 GB with Ray Reconstruction's guides). A
  32-bit D3D11 process held 8 GB of video memory with 0.43 GB of its address space: video memory is not the 4 GB limit.

## v72 (2026-10-26, untested in game)

- **Ray-Traced Sun Shadows** and **Ray-Traced Ambient Occlusion** (Experimental; Anti-Aliasing section, with DLAA /
  DLSS: they run in renodx-tw-dlaa64.exe, which needs an RTX / DXR 1.1 GPU; RayTracedSunShadows,
  RayTracedAmbientOcclusion, RayTracedAORange 0.5-10 m (2), RayTracedAORays 1-8 (2)):
  - The scene (rt_scene.hpp): terrain, rigid objects (buildings, walls, props) and tree branches. Each new draw is
    captured once: drawn again with its rt_capture_* shader (the game's vertex shader made to output only its world
    position, by tools\tw\attila_motion\make_wind_motion_vs.py world mode; all 15 checked on the GPU against the
    game's: within 1e-7) into stream output, then copied into the bridge's shared vertex texture. A draw's key: its
    arguments, its vertex / index buffers and offsets, its vertex constant buffers b1-b4 and shader resources. Moving
    rigid meshes (the motion vectors' readback) are left out. Not in the scene: units, leaves and grass (alpha
    tested), particles, the ocean, cloth.
  - The helper (bridge v4; dlaa_helper/rt_d3d12.hpp): one bottom-level structure per key (D3D12, built once), a top
    level of what was drawn (terrain only in frames that draw it; other meshes stay as casters 300 frames after last
    drawn, freed after 3600). At the game's ambient occlusion pass: each pixel's world position from the G-buffer
    depth (inverse view-projection), then one compute pass (rt_trace.cs_6_5, inline ray queries): one ray toward the
    sun, and cosine-weighted rays over the range for ambient occlusion.
  - Sun visibility joins the screen-space shadows (the darker of both, at t49; also with Screen-Space Shadows off).
    Ambient occlusion multiplies the game's (it needs the game's SSAO on).
- GPU test (tools\tw\warp_tests\dlaa\rt_bridge_test.cpp, 32-bit through the helper): a square's shadow on the ground
  0, open ground 1, the square's top 1, ambient visibility beside the square 0.87, open ground 1.
- Memory: the addon's vertex texture and stream output buffer 64 MB each (VRAM; D3D11 default resources are outside
  the game's 4 GB address space), the helper's vertex buffer 64 MB plus the acceleration structures (VRAM, in the
  64-bit helper).
- Every other TW game: the new renodx-tw-dlaa64.exe (bridge v4) in their zips.

## v71 (2026-10-26, untested in game)

- **Ray Reconstruction (Experimental)** for DLAA / DLSS (Anti-Aliasing, AntiAliasingRayReconstruction: Off, NVIDIA's,
  D, E, F; Attila only: its G-buffer): NVIDIA's denoising DLAA / DLSS in place of the usual one. At the frame's first
  sun light draw of the main view (the G-buffer bound) aa_rr_guides writes its guides into the bridge's shared textures
  (diffuse albedo: albedo, gamma 2, x 1 - metalness; specular albedo: 0.04 toward the albedo with metalness; world
  normals; roughness 1 - gloss); the camera's view = inverse projection x view-projection (the registers'
  column-vector matrices, sent as row-vector ones), the unjittered projection. Bridge v3 (dlaa_bridge.h: guide handles,
  flag, preset, matrices appended; the helper still answers v2 addons, mapping the block by its version).
- The helper runs Ray Reconstruction through D3D12 (dlaa_helper/rr_d3d12.hpp: its own device, queue and three command
  lists in turn; the addon's textures and fence opened in D3D12; its queue waits for the addon's fence value and
  signals the next). Found while testing: NVIDIA's D3D11 Ray Reconstruction never finishes (black output) or crashes
  in NVIDIA's D3D11 driver (nvwgf2umx) when the GPU is waited for right after it, which a cross-process fence does;
  Super Resolution stays on D3D11. Needs nvngx_dlssd.dll next to nvngx_dlss.dll (310.9.16 from the Streamline SDK
  2.14.16 tested).
- GPU test (tools\tw\warp_tests\dlaa\bridge_test.cpp, 32-bit through the helper, RR=1 SIZE=512, RTX 5090): DLAA
  with Ray Reconstruction mean 0.561 / 0.533 (310.7 / 310.9.16; input 0.55), DLSS 2x 0.572; presets Default, D, E, F;
  Super Resolution unchanged (118 of 120 frames each).
- Every other TW game: aa_rr_guides wrappers (the shared module creates it); their option is not offered (other
  G-buffers).
- The zip has renodx-tw-dlaa64.exe again (v70's missed it) and NVIDIA's nvngx_dlssd.dll.

## v70 (2026-10-26, untested in game)

- DLAA / DLSS motion vectors for everything else that moves on its own (anti_aliasing.hpp DrawObjectMotion; motion
  shaders made by tools\tw\attila_motion\make_wind_motion_vs.py, all checked on the GPU against the game's shaders):
  - Rigid objects (ships, siege engines, projectiles, anything rigidcommon / textureblend draws: 0x347F3DD9 0xD830AD9B
    0xB06CB969 0x10FE7EBC; rows in POSITION1-3): most stand still, so each mesh (its index buffer and range) is
    recorded two frames when first seen and every 32 frames, and every frame while moving: its instances' rows through
    stream output (motion_record_*.vs, one point per instance, not rasterized), matched once a frame by
    motion_rigid.cs against last frame's table (the same mesh nearest where it was predicted to be: position + last
    frame's velocity; a new fast one within 2.5 m), then put in this frame's table where it will be next frame. Each
    draw's flags (moved / still / new) are read back a few frames later (no stall): a mesh seen moving is drawn again
    with its motion shader (motion_rigid_*.vs, last frame's rows by the same prediction, wind at last frame's time) for
    10 s (600 frames) after it last moved. Standing meshes cost one tiny point draw every 32 frames.
  - Cloth, sails, sail symbols and ropes (0x772B26FF 0x902253FD 0xD11B5EC7 0x202FA188 0xB2CCA764 0xE9C34FAB
    0x8D0D26B9: the CPU moves their vertices each frame): each draw is captured first (motion_capture_*.vs +
    motion_capture.ps: one point per index on one pixel writes its vertex's world position, by SV_VertexID, into this
    frame's positions; its first index's vertex is the draw's anchor, recorded in the table), then drawn again with
    last frame's position of each vertex (the same mesh's anchor nearest this frame's). Ring vertex buffers (another
    base vertex each frame) and many flags of one mesh are fine (GPU test).
  - The ocean: the old one's waves (0xAB20418F) at last frame's time; ocean2's (deep, shallow, coast: 0x9EECBA5D
    0x97101A59 0xE0671FA4 0xF64EE8CB) with last frame's displacement textures (copied at the frame's first ocean draw,
    two copies in turn, at t16 + their slot).
  - Not needed: water planes and rivers (their time only scrolls textures), terrain, static trees. Not done:
    particles and projectile trails (blended), tree billboards turning with the camera (small), limbs' own swing.
- Shared table code: motion.hlsli MotionRecord (units, rigid, captures), PLACEMENT_CELLS as a constant; motion_VS has
  motion_draw (deformed: first position, positions, first vertex). The wind draws no longer save the table views.
- GPU tests (tools\tw\attila_motion): compare_test (rigid with last frame's and predicted placement, ocean with last
  frame's textures, old ocean, earlier shaders again: all bit-identical), rigid_test (record + match over 4 frames:
  flags, standing / moving / 2 m-a-frame objects, motion shader's last-frame positions exact), deformed_test (two
  cloths of one mesh in a ring buffer, exact), placement_test (units again after the shared record code).
- Memory (VRAM, outside the game's 4 GB address space as D3D11 default buffers): rigid records 4 MB, flags 3 x 32 KB,
  deformed positions 2 x 16 MB, anchors 64 KB, ocean copies 2 per displacement texture.

## v69 (2026-10-26, untested in game)

- DLAA / DLSS unit motion vectors: soldiers and horses moving, charging and turning get their own motion (their limbs'
  swing within a frame stays the camera's). The 8 main-pass skinned vertex shaders (weightedcommon.hlsl vs30_main /
  main_4_bones, with or without dismemberment, shader models 3 and 4.1: 0x5F9CD405 0x0ECFE74C 0xDAC80ECC 0xC2AA8FDA
  0xA691CFF4 0x372628AB 0xEA1AEB0E 0x31B66A15) are redrawn like the wind's (anti_aliasing.hpp DrawObjectMotion, the
  shared path now for both) with their motion versions (make_wind_motion_vs.py `:unit`). Nothing names a soldier from
  one frame to the next (the instance order and the bone texture are rebuilt every frame), so a placement table does
  (motion.hlsli): the units' redraw (motion_unit.ps, early depth off: hidden units too) records each instance's world
  rows (TEXCOORD0-2) with the draw's mesh key (its index buffer and range) in every 1 m cell within 0.5 m of it (64K
  cells x 8 slots, a key per slot claimed with InterlockedCompareExchange; two tables in turn, this frame's at u1 / u2,
  last frame's at the vertex shader's t14 / t15); the next frame each unit takes last frame's rows of the same mesh
  nearest its own (within 0.5 m: soldiers stand further apart, and move less than that in a frame) and runs the game's
  skinning with its current pose there: its last-frame position. None found (new, culled last frame, another LOD): the
  camera's motion. Constants: motion.hlsli's motion_VS (now with the units' mesh key, table valid, cells - 1), shared
  by all motion shaders. GPU tests: compare_test.cpp (all 16 motion shaders: the game shader's SV_Position, and last
  frame's position as the game shader at last frame's time / placement / camera; units with and without a table) and
  placement_test.cpp (frame 1 records through motion_unit.ps, frame 2 finds it exactly; another mesh finds nothing).
  Not done: ships, siege engines, projectiles (rigid instanced draws: the same table would carry them), cloth / sails
  (moved by the CPU), the limbs' own swing (the bone texture's last frame would need keeping per soldier).


- DLAA / DLSS wind motion vectors: swaying grass, leaves and branches get their own motion, so DLSS follows them instead
  of smearing (the game has no motion vectors; until now DLAA had the camera's only). Each main-pass draw of their 8
  vertex shaders (grass 0xF3741248 / 0x01FA9CDE: the addon's grass00 / grass01; leaves 0x46C616F0 / 0x5B880546 /
  0xCABCB9D1; branches 0x77B993FD / 0xB8C3E83D / 0x6EEF4062) is drawn again (anti_aliasing.hpp DrawWindMotion, post
  callback) with its motion version (motion_*.vs, made by tools\tw\attila_motion\make_wind_motion_vs.py: the game's code
  as a function run at this frame's and last frame's time_in_sec with identity rows, so it gives the world position;
  SV_Position with the game's jittered rows, the two clip positions with both frames' unjittered cameras, motion_VS
  b12) and motion_wind.ps (motion in render pixels, depth) into an RGBA32F target the size of the main depth, against the
  game's depth (its comparison made inclusive, no writes, rasterizer biased 4 units toward the camera). DLSS's
  preparation (aa_dlaa_prepare t2) takes that motion where its depth is the final one (else the camera's: leaf cut-outs,
  anything drawn in front later). Camera time_in_sec (c26) noted with each camera upload; last frame's camera as the
  temporal path's. Only with DLAA (and DLSS render scale), from the frame after DLAA ran. Shared module: other TW games
  pass no wind shaders (Install's WindMotion), so nothing changes for them. GPU test
  (tools\tw\attila_motion\compare_test.cpp, stream output, 4096 random vertices each): all 8 motion shaders give the
  game shader's SV_Position (bit-identical; grass00 8.6e-8 relative: the addon's grass) and last frame's position as
  the game shader at last frame's time and camera. Units: v69.


- DLSS Render Scale (Experimental; Anti-Aliasing DLAA, Attila only: AntiAliasingRenderScale 33-100%, 100% = DLAA):
  below 100% the 3D image is drawn at that fraction of the display's size and DLSS upscales it (anti_aliasing.hpp,
  as Shogun 2's render scale): from present to the tone mapper, viewports and scissors at the display's size or a
  power-of-two fraction of it (half, quarter ... sixteenth: bloom's levels) at 0, 0 are scaled, and the main camera's
  g_viewport_dimensions (camera_VS_PS c29) becomes the render size; the game's post passes take texture coordinates
  from the pixel position and the screen size (the tone mapper: g_screen_size.zw x vpos), so the image stays in the
  targets' top-left corner. DLSS's inputs from that corner (aa_dlaa_prepare at the render size: motion in render
  pixels), the helper creates NVIDIA's feature with the render size in and the display's out (dlaa_bridge.h v2:
  output_width / height; Custom DLSS, MVLowRes) and evaluates it; the UI after the tone mapper stays at the display's
  size. Jitter: Halton (2, 3) over 8 / scale^2 phases (NVIDIA's), in render pixels. Starts the frame after DLAA ran
  (a menu frame ends it). GPU test (tools\tw\warp_tests\dlaa\bridge_test.cpp, DLSS=1): 128 to 256 through the
  32-bit bridge, 117 of 120 frames ("Custom DLSS active (upscaling)"). Not done yet: texture LOD bias (textures a
  little softer below 100%), the addon's own effects (SSR, SSS, GT-VBAO / GT-VBGI) at the render size, units' / wind
  motion vectors. Rebuild every TW game together (the helper's shared block is v2).

## v66 (2026-10-26)

- Anti-Aliasing DLAA (NVIDIA's, RTX; ATN): NVIDIA's DLSS runs only in 64-bit processes, so a 64-bit helper
  (renodx-tw-dlaa64.exe next to the addon, with nvngx_dlss.dll; twattila/dlaa_helper, build.ps1) runs it on textures
  and a fence shared with the game (twattila/dlaa_bridge.hpp / dlaa_bridge.h): this frame linear in BT.2020, the
  scene's depth and the camera's motion (aa_dlaa_prepare: the TAA's jitter and cameras), NVIDIA's result back to the
  2.0 encoding (aa_dlaa_finish). The game's GPU waits for the helper's fence value only after it answered (no answer in
  100 ms, 5 s the first time while NVIDIA sets up: DLAA off, the frame as it is). DLAA Preset K / M / L. Motion is the
  camera's only: units, flags and swaying grass / trees can smear (their own motion vectors are the next step).
  Tested on the RTX 5090 without the game (tools\tw\warp_tests\dlaa\bridge_test.cpp, 32-bit: 117 of 120 frames
  through DLAA, output finite); not yet in game.
- Screen-Space Shadows patch: three ways now (screen_space_shadows.hpp): Attila's / Thrones' / Rome II's shadow end
  before the div; else every shadow map comparison scaled (Troy, Pharaoh, Warhammer); else the t_shadow mask's reads
  (Warhammer III). Inserted after the matched instruction's real length (Rome II's 7-token `min`).
- Reflections: the hierarchical march moved into ssr_march.hlsli (shared with Rome II's water); the hit's thickness
  from its distance to the camera (was its view depth: a little thicker at the screen's sides).
- bloom_filter.hlsli: BLOOM_SAMPLE_ONLY for the newer games' tone mappers; sky.hlsli includes shared.h only without SI.

## v65 (2026-10-26, untested)

- Reflections (ATN's SSR 1-3): the hierarchical march's hits refined (4 halvings against the full-resolution depth
  between a texel before the hit and it), a wider screen border fade (8% a side, on top of FidelityFX's 5%), and a
  resolve after the draw (ssr_resolve.cs; the draw writes each pixel's reflected distance and confidence into a third
  target the addon binds): Reflection Rays Every Pixel / Half (checkerboard) / Quarter (2 x 2 in turn), the untraced
  pixels filled from the traced ones around by depth; Reflection Denoiser (default On): the history read where the
  reflected point was (surface + reflected distance along the view ray, a miss 1000 m), clamped to the traced colours
  around, 0.85 of it. WARP (tools\tw\warp_tests\ssr\resolve_test.cpp): undrawn pixels exact, fill off by 0.005, a red
  history clamped (0.026 off instead of 3.8), a matching history kept.
- Grass Trace button (Graphics 2 of 2, grass): one frame of the scrub into ReShade.log (cells, bias, every DrawScrub by
  10 m distance bins and whether a LOD takes it, the first 16 models' LOD distances), to find why grass still ends
  ~100 m with v64.

## v64 (2026-10-26, untested)

- The grass "circle" (ATN: Grass Distance / Density never got grass past 100 m): the grass pixel shaders (rigidgrass(wet)
  ps30 gbuffer / gbuffer_no_clip / gbuffer_ms, forward_lit_oit / aoit for every shadow method: 13) blend each blade into
  the ground's colour by 100 m (mul rA.c, rB.c, l(0.01) before min), so the blades past it are drawn but look like the
  ground. grass_fade.hpp patches them as the game creates them (that 0.01 read from cb12[0].x, cb12 declared) and binds
  0.01 / Grass Height Distance on their draws (command_action), so Grass Height Distance now moves both the blades'
  height and this fade. WARP (tools\tw\warp_tests\grass\patch_test.cpp on the dump's csos): 13 / 13 patched and created,
  the fade reads cb12[0].x.
- dxbc.hpp: the DXBC checksum, program read / write shared by the sun shader patch (screen_space_shadows.hpp) and the
  grass one (patch_test: the 42 sun shaders still patch and create).
- What the scrub hooks do (debug9 / ATN): battle grass models' LODs are the model file's (type 0x40, +0x17fbd30) and
  Grass Always Drawn On skips the per-plant thinning; so Grass Distance is now "Grass Detail Distance" (fuller versions
  and cells farther), Grass Density says Grass Always Drawn On already draws every plant.

## v63 (2026-10-26, untested)

- Grass Density (Graphics (Experimental), 1-8x; ATN: more grass inside the same circle, not farther). The scrub gives
  each placed plant a random bias (byte / 255 x 200 m, squared, added to its distance squared) against the clumps'
  last LOD (100 m), so near the camera only half the map's grass is drawn and it thins out to the edge. The ScrubUpdate
  hook divides +0x84 by the density: 2x draws all of it near the camera, higher keeps it fuller towards the edge.
- Mirror Screen Edges in the shader now (bend_sss_gpu.hlsli MirrorEdges, sss.hlsli g_sss_mirror_edges): left, right
  and bottom only. Past the top (the far side) the mirrored ground came nearer and shadowed everything (ATN debug10).
  WARP (sss_scene.cpp, SSS_MIRROR): +267 false pixels of 1.8 M with a side sun, none with the sun behind.
- TAA / SMAA T2x jitter on swaying trees and grass (ATN): where the clip cuts the history away (their wind motion is
  not the camera's), the output was this jittered frame. The resolve now takes this frame at the pixel's centre (3 x 3,
  weights by distance to the jitter-shifted texels, UE's Blackman-Harris Gaussian; aa.hlsli g_taa_jitter). WARP RMS x
  1000: TAA moving 61 -> 55, still 46 -> 43, moving box 51 -> 46, SMAA T2x 60 -> 54 (the jitter's sign flipped: 61 /
  54 / 65: worse, so the sign is right).

## v62 (2026-10-26, untested)

- Grass Distance (Graphics (Experimental), 1-4x). ATN debug9: the grass "circle" ends ~100 m from the camera (trace:
  95.5 m inside, 101 m outside). Grass and small plants go through the scrub, not the LOD pick: ScrubUpdate
  (+0x17fb510) collects cells within +0x80 (scrub_lod_distance_default, 200 m) and gives each instance a bias (its byte
  / 255 x +0x84, 200.f, squared) that thins them with distance; DrawScrub (+0x17f9800, the vtable slot before DrawTree)
  adds it to the distance squared and tests the model's LOD entries (grass clumps' last ~100 m). Hooks: DrawScrub scales
  the model's LOD distances (the tree hook's code, now shared: ScaleModelLods), ScrubUpdate writes +0x80 / +0x84 x the
  scale, so the grass thins the same way, farther out. Same code in Thrones (patterns found; no slider there yet).
  Grass Height Distance must be at least as high (the grass VS flattens blades at 100 m x it).
- Screen-space shadows: the lines at Length 960 / 1920 towards the sun (ATN debug9) were Bend's 64-pixel rings: the
  rays ran almost into the sun's screen point, where Bend divides the depth by the distance to it and its error grows
  past the shadow window. bend_sss_gpu.hlsli (Shogun 2's too) ignores samples nearer that point than half the pixel's
  own distance. WARP (sss_scene.cpp, 3840 x 2160, flat ground, sun ahead, 24-bit depth): 1920 false shadows 43260 ->
  38803 (480's level), the stripes gone.
- Mirror Screen Edges (SSS toggle, Off): the border band without shadows (debug9: the sun behind the camera, every ray
  runs off the screen; the grass is there) gets them from the depth mirrored past the edge (a guess). Debug View's red
  now stops halfway to the sun's point as the rays do.

## v61 (2026-10-26, untested)

- Screen-Space Shadows: Debug View Off / Shadows / Shadows + Screen Edge (ATN: grass at the screen's border gets no
  SSS). After the tone mapper (and the AA) sss_debug.cs draws this frame's occlusion as the picture (white lit, black
  shadowed) and, with Screen Edge, red where the pixel's ray towards the sun's screen point (away from it with the sun
  behind the camera) leaves the screen before the Length: nothing past the edge can cast a shadow there. A red band
  matching the missing shadows = the screen-space limit; missing shadows outside it = a bug.

## v60 (2026-10-26, untested)

- Anti-Aliasing: SMAA T2x and TAA (ATN: SMAA too blurry, CMAA2 too weak, grass aliasing). anti_aliasing.hpp jitters
  the main view's camera_VS_PS as the game uploads it (Map / Unmap or UpdateSubresource; a perspective camera with the
  display's viewport: projection, view-projection and both inverses; TAA Halton (2, 3) x 8, SMAA T2x SMAA's two
  sub-samples); the sun light draws (LIGHT_BOUNCE_DRAWS) give the camera the scene was lit with (the buffer bound at
  their camera slot) and the depth-stencil buffer. After the tone mapper: aa_taa_resolve.cs reprojects the history by
  the camera only (no motion vectors in the game: nearest depth in 3 x 3, Catmull-Rom history, clip to the 3 x 3 range
  and mean +- 1.5 deviations in YCoCg, half weight where the distance disagrees), aa_taa_sharpen.cs sharpens (clamped
  to the neighbours). SMAA T2x: SMAA with the frame's sub-sample indices, then the same resolve at 0.5. Cameras uploaded
  after the tone mapper (the UI's) stay unjittered; no jitter after a frame without a main view (menus). Settings: TAA
  Strength (0.5-0.97, 0.9), Sharpening (0.25). Needs the in-game AA off (one sample). ReShade.log: "the main view's
  camera found for TAA", "the scene's depth ... copied for TAA".
- WARP (tools\tw\warp_tests\aa\taa_test.cpp, the module itself on a ray-traced moving scene vs 4 x 4 supersampling,
  RMS x 1000): no AA 83; TAA 61 moving / 46 still / 51 with a moving box; SMAA T2x 60; SMAA 1x 63; TAA without the
  camera's motion 97 (the reprojection works). Jitter shift and inverses exact to 1e-6.
- frame_history::DepthFormats shared by the screen-space shadows and the AA.

## v59 (2026-10-26, untested)

- Reflections (ATN's water trace debug8: the sea's reflection is the game's SSR pass environment_specular_light
  ps40 0x47D75349 / 0x66006BEA, stencil 1 = water and metal): Reflection Method Game's / Hierarchical (default). ssr_hiz.hpp
  builds a min-depth pyramid (R32, full chain; ssr_hiz_copy / _reduce.cs) from the pass's t3 depth once a frame on its
  draw and binds it at t10; ssr_hiz.hlsli runs AMD FidelityFX SSSR's hierarchical traversal (ffx_sssr/ffx_sssr.h, MIT,
  marked change: FFX_SSSR_NO_WAVE_OPS) from the game's start point (its depth bias) in the game's jittered direction,
  FidelityFX's hit checks, misses keep the environment cube; the game's march stays as the else branch. Reflection
  Quality: x 64 steps. WARP (tools\tw\warp_tests\ssr): ground + wall scene, 97.4% agreement with exact reflections.
- Screen-space shadows: dark streaks in the sky (ATN SSS_glitch.png) = sky pixels at the far plane (the sky VS writes
  z = w): Bend's thickness scale (distance to the far plane) is 0 there and divides into garbage; those pixels are skipped
  now (bend_sss_gpu.hlsli, Shogun 2 too).
- Sky Debanding: 12 taps on 2 rings (was 24 on 3) in a square-root encoding (was sRGB pow); default Off, Off in the
  presets (ATN: too heavy).
- Tests (ATN): Water & Ships, Terrain and Destruction Fires removed, the generated "(More Tests)" gone except All Soldiers
  as Sprites (force_unit_imposters), Effects Wind (VFX_WIND_SCALE_FACTOR), Sea Wind Speed and Campaign Sea Transparency /
  Pattern Scale (new Sea (Tests), -1..10 / -10..10; their old keys kept). Top sliders split: Unit Animation -> + Facial /
  Hand Animation (graphics_hooks Scales: Rome II / Thrones pass one value for all three), Campaign Detail -> Campaign Tree
  Detail / Plant Detail / Prop Distance; presets set all of them.
- engine_values.txt (tools\tw\tw_engine_values.py): the 632 graphics engine values the game reads, the mod's marked.
- No UI scale value: ui_width / ui_height / ui_rect_* are set by the game itself (a setter at +0x17672a0).

## v58 (2026-10-26, untested)

- Audio Memory (Tests, restart the game), ATN: audio cuts out with thousands of arrows. The game's Wwise init (+0x137612e)
  reads SOUND_DEFAULT_BUFFER_SIZE_IN_KB (default pool, 23 MB) / _LOW_RATIO (0.9: low memory mode), SOUND_LOWER_ENGINE_
  DEFAULT_BUFFER_SIZE_IN_KB (lower engine pool, 13 MB) / _LOW_RATIO (0.7), SOUND_WAVEFILE_BUFFER_SIZE_IN_KB (86 MB),
  SOUND_STREAM_IO_BUFFER_SIZE_IN_KB (1 MB), SOUND_STREAM_MANAGER_BUFFER_SIZE_IN_KB (256 KB): 7 sliders (system RAM in
  the 32-bit address space: capped). Read once, so written early: AttachAddon starts a thread that writes them as soon as
  the game's static initializers set them (engine_tweaks ApplyWhenSet; their values logged with Trace), the per-present
  engine values wait for it; while any is changed, ReShade.ini [ADDON] LoadFromDllMain lists the addon (SyncEarlyLoad) so
  ReShade loads it as the game starts, before its audio (removed again at the game's values).

## v57 (2026-10-26, untested)

- Grass Height Distance (Grass & Trees (Tests), 0.25x-10x, a shader option): the grass vertex shaders (rigidgrass
  vs30_main_grass sm_5_0 0xF3741248 / sm_3_0 0x01FA9CDE, vs30_shadow_grass 0xDFECAA1A; shaders/grass00-02) shrink the
  blades by 0.01 x distance (flat at 100 m): divided by SI.grass_height_distance. Signatures checked against the originals.
- 217 more engine tests (7 "(More Tests)" sections): twattila/engine_values.h, generated by tools\tw\tw_engine_values.py
  from tw_tweak_uses32.py's list (every graphics-related value the code reads, not already a slider; gameplay, audio, AI,
  UI, debug views and unsafe sizes (terrain_scale, heightfield heights, grid sizes, cache block size) left out). Kinds:
  scaled float / int, flag Game's / Off / On, and 'a' / 'j' values at 0 or -1 set to the slider's value
  (engine_tweaks Tweak::absolute: the slider at the game's value writes the game's back).

## v56 (2026-10-25, untested)

- Anti-Aliasing (Experimental, in-game AA off), Graphics 1 of 2, title aquamarine: Off / SMAA / CMAA2, Quality Low-Ultra
  (SMAA's presets; CMAA2's edge thresholds), Extra Sharpness (CMAA2). twattila/anti_aliasing.hpp: compute after the
  tone mapper (the replaced t00-t10's on_drawn, before the UI) on its RGBA16F target at the display's size: copy ->
  SMAA 1x (smaa/SMAA.hlsl, MIT, unchanged; aa_smaa_*.cs via SMAA_CUSTOM_SL, its quality macros from the constants) or
  CMAA2 (cmaa2/CMAA2.hlsl, Apache 2.0, marked change: CMAA2_RUNTIME_SETTINGS; on a linear BT.2020 copy, only its changed
  pixels converted back) -> copy back. WARP (tools\tw\warp_tests\aa\aa_test.cpp): only edge pixels change, wide-colour
  pixels bit-exact, CMAA2 stable over frames. Only the chosen method's storage is allocated.
- Screen-space shadows: Softness (sss_blur.cs: 3 x 3 taps 1-3 px apart, same surface only), Contrast, Hard Shadow
  Samples, Fade-Out Samples (Bend's macros now run-time fields in twshogun2completeedition/shaders/bend_sss_gpu.hlsli,
  Shogun 2's defaults unchanged), Edge Threshold, Ignore Edge Pixels, Precision Offset, Sample Along the Ray.
- Grass pop at ~25 m (ATN, every grass slider maxed): not in the grass shaders (height shrinks smoothly to 100 m) or the
  grass cells (200 m); OIT queue switch at OIT_GRASS / OIT_TREE_MAX_DISTANCE only with in-game OIT on ([device +
  0x716bc]: 1 OIT, 2 AOIT). Waiting for ATN's pixel trace on a popping plant.

## v55 (2026-10-25, untested)

- Settings Mode "HDR & Effects" / "Graphics 1 of 2" (AO, GI, screen-space shadows) / "Graphics 2 of 2" (Graphics
  (Experimental) and the tests). Screen-space shadows title lavender (0xC08CFF), an empty line after GI.
- AO presets (twattila/lighting_settings.hpp, all TW games): Faster = Recommended at half resolution, "Heavier
  (Recommended)" renamed Recommended, new Heavier (25 m, 64 samples), Radius max 30 m, Temporal Edge Fix off by default
  and in every preset (ATN: ghosting). GI Faster (Recommended): 32 samples (Quality Ultra).
- Graphics 2 of 2 tests (ATN: every graphics value, unsure ones too): 78 sliders / switches in 8 sections, each the
  game's engine values the code reads (tools\tw\tw_tweak_uses32.py on empire.retail.dll: float tweaks ctor +0xbeb60,
  bools +0xbedd0, ints +0xc85d0, value at object + 0x48). Switches: Game's / Off / On (engine_tweaks Tweak::three_way).
  Grass: inside OIT_GRASS_MAX_DISTANCE (25 m; AOIT 15 m) grass goes into the blended (OIT) queue, beyond it the
  cut-out one (+0x17f37ba: queue 0x10 vs 0x14 / 0x15): likely ATN's "grass only 10-20 m" (Soft Grass Distance).
  engine_tweaks.hpp x86 Find: one pass over the code's static initializers, names read where they point (a search per
  name took seconds with 100+ names).
- Pixel trace (talesofarise_dlss/pixel_trace.hpp, attila_pixel_trace.txt) at the end of Graphics (Experimental): to find
  the sea / river reflection pass.
- Sky Debanding (Extra (Experimental), default Medium): skydome ps30_main 0xA5BA3E04 / ps30_gbuffer 0x9436471B
  (sky.hlsli): Pharaoh's sky deband (24 taps on 3 rings, 16-64 cube texels, 6 / 11 / 20 code thresholds) + 4 codes of
  screen dither, on the sky cube.

## v33 (2026-10-24)

- Light Bounce with the game's MSAA (ATN: "support every AA"): its *_ms directional light draws hooked too (24: slm / skinlm, highdef / lowdef, every shadow method,
  from the pack dump: each one's G-buffer slot and camera), the light buffer resolved for the trace, the prepare and the
  apply as *_ms builds (twattila/light_bounce_gbuffer.hlsli: Texture2DMS, the prepare at sample 0, the apply per
  sample as the game's MSAA lighting). FXAA / MLAA / off were already fine.

## v32 (2026-10-24)

- Ambient Occlusion Game's at its own cost (ATN): the replaced AO pass (ssao00) is the game's code only (#else of
  AO_BITMASK); the Visibility Bitmask is the addon's own pixel shader compiled from the same file (ao_bitmask.ps_5_0.hlsl: #define
  AO_BITMASK), which twattila/ao_pass.hpp draws in its place (the game's bindings, the addon's settings at b13) only
  when it is selected. Before, one shader held both paths, so Game's paid for ours.
- Light Bounce with the game's MSAA: not supported (ATN's "no effect"): the game then lights with directionallight
  ps41_*_highdef_ms (G-buffer as Texture2DMS), not in the sun draw list (found with a one-time draw log, removed).

## v31 (2026-10-23)

- AO Depth Levels (ATN: Rome II's frame time spikes gone with them): twattila/depth_levels.hpp builds the AO's depth at
  1/2 to 1/32 (5 mips of R32G32_UINT, one compute dispatch a level, depth_levels.cs_5_0.hlsl: each texel one depth texel
  of its 2 x 2 in a rotated grid, its bits and coordinate) before the Visibility Bitmask pass and binds them at t41
  (ao_pass.hpp on the ssao00 draw; its pixel-shader levels pass of v16 / v30 / v15 replaced). A sample 8 x 2^n to 16 x
  2^n depth texels away reads level n at that depth texel's own position (depth_levels.hlsli, BitmaskSampleLevel in
  bitmask.hlsli; as Scalable Ambient Obscurance). Same look; the far samples stay in the texture cache, so more
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

## v30 (2026-10-23)

- AO Depth Levels (ao_pass.hpp, ao_depth_levels; bitmask_ssao.hlsli): before the AO draw, the game's
  depth at 1/2 to 1/32 of its size (5 levels; each texel one depth texel of its 2 x 2 in a rotated grid, with that
  texel's coordinate), bound at t41. A sample 8 x 2^n to 16 x 2^n depth texels from the pixel reads level n, at that
  depth texel's own position (as Scalable Ambient Obscurance, McGuire et al. 2012). Same samples and look; the far ones
  stay in the texture cache, so more Directions / Quality cost much less (ATN: frame time spikes above 1 direction).
  ao_temporal.hpp is now ao_pass.hpp (Temporal Smoothing in it too).

## v29 (2026-10-23)

- Ambient Occlusion and Light Bounce tooltips as Attila's (ATN); the Ambient Occlusion choice names the game's own
  method on its own line.

## v28 (2026-10-23)

- Settings Mode "HDR & Effects" / "Graphics" (ATN).
- Ambient Occlusion and Light Bounce presets at the top of the AO section (ATN): Recommended (AO Thickness 0.14,
  Quality High, 1 direction; Light Bounce Strength 4, Radius 6.5 m, Quarter, Ultra, 2 directions), Recommended
  (heavier but looks better: AO Thickness 0.11, 2 directions), Reset (both sections at their defaults with the game's
  AO). The graphics presets no longer set them. AO / Light Bounce titles light blue, two empty lines after Graphics.
- AO Directions 1 / 2 / 4 / 8 (2^the setting, default 4) and at most one step per AO pixel of the radius.
- Copyright headers on the lighting / AO files; About credits the Light Bounce (same paper).
- AO Temporal Smoothing (default Off): twattila/ao_temporal.hpp on the AO draw; the bitmask AO is
  twattila/bitmask_ssao.hlsli now (Attila, Thrones, Rome II).
- Graphics presets (ATN): Reflection Quality 1x in both, Building Detail 3x (4x / 8x units) and 4x (vanilla).

## v27 (2026-10-23)

- Settings Mode "HDR & Effects" / "Graphics" (ATN).
- Ambient Occlusion Temporal Smoothing (default On; ao_temporal.hpp): the frame's ssao00 draw also writes (result,
  distance) into one of two R16G16F histories (SV_Target1, the game's blend kept for its own target), reads the other
  (t40) and the last camera (b10, a copy of b0 after the draw), keeps 90% where the surface is the same (distance
  within 5%), noise moving every frame (SI.ssao_frame). The game's draw itself: bindings put back in the command's
  post callback. frame_history.hpp: the history targets and the camera copy, shared with the Light Bounce.
- Graphics: UI 4K Mod Compatibility slider (writes ReShade.ini [Attila] DisableGraphics, from the next start; tooltip
  with the manual way), two empty lines after the section, the ambient occlusion and Light Bounce titles light blue
  (0x7FBFFF), Graphics pink. The graphics presets no longer touch the ambient occlusion or the Light Bounce (their own
  presets later, ATN).

## v26 (2026-10-23)

- Settings Mode Image / Graphics (was Simple / Advanced; ATN): Image shows every tone mapping, grading, effect and
  output setting (the old Advanced-only ones too), Graphics only the graphics sections (Graphics, Ambient Occlusion,
  Light Bounce); About in both. IsGraphicsSettings and one loop at the end of ConfigureAddon.

## v25 (2026-10-23)

- Ambient Occlusion Directions 1 / 2 / 4 / 8 (2^the setting now, default 4), and at most one step per AO pixel of the
  radius (distant pixels: fewer samples, same look).
- Light Bounce: Resolution Eighth, Directions 8 / 16. A prepare pass (light_bounce_prepare) writes per trace texel the
  mean light of its pixels (less noise) and the view-space position + octahedron normal in one RGBA32 uint texel: the
  trace reads 2 small textures per sample instead of 3 full-size ones plus the G-buffer decode. At most one step per
  texel of the radius.
- Light Bounce Temporal Smoothing (default On): the trace writes one of two history textures and blends the other
  (bilinear, 90% kept) where the surface is the same (distance from the last camera within 3% + 2% x scale); the last
  camera is a copy of the camera constants made after each trace (b10), and the noise moves every frame (interleaved
  gradient noise + 5.588238 x frame). Off: the noise stands still as before.
- Shared code (twattila/light_bounce*.hlsli, light_bounce.hpp, each game's light_bounce.hlsli BouncePrevious and
  light_bounce_prepare wrapper) also in Thrones / Rome II / Pharaoh / Shogun 2 sources; not shipped there yet.

## v24 (2026-10-23)

- Ambient Occlusion Directions (2 / 4 / 8 slices around the view vector, default 4 as before; SI.ssao_directions):
  fewer is faster.
- Light Bounce Resolution (Full / Half / Quarter, default Half), Quality (8 / 16 / 32 samples per side, its own now,
  default 8) and Directions (1 / 2 / 4, default 2, as before). twattila/light_bounce.hpp: Install takes a SunDraw
  table (shader, standard or not, G-buffer and camera slots); each sun draw's G-buffer and camera are bound again at
  t42-t45 / b11 (light t40, bounce t41, constants b12), so the trace and apply (light_bounce_trace.hlsli /
  light_bounce_apply.hlsli) are game-neutral and each game's light_bounce.hlsli decodes its own G-buffer. The bounce
  texture is RT / scale, the trace viewport the game's / scale. Installed after renodx::mods::shader::Use (its
  injection on replaced sun shaders runs first).
- ReShade.ini [Attila] DisableGraphics=1: no engine changes from Graphics (Experimental) (no hooks, code patches,
  tweaks or shadow map size) and its sliders hidden, for the UI 4K mod. SSR Quality, the ambient occlusion and the
  Light Bounce (shaders) stay.

## v23 (2026-10-23)

- Ambient occlusion faster: twattila/bitmask.hlsli (shared by every Total War bitmask AO and the Light Bounce): acos
  within 0.007 rad (GTAO's approximation; a sector is 0.1 rad), and a slice stops once all 32 sectors are covered.
- Light Bounce faster: traced once a frame at half resolution (after the first standard sun draw: one light buffer
  copy instead of one per sun draw) with the addon's full-screen triangle (light_bounce_vs); each sun draw adds it
  back up with a depth-aware 3 x 3 upscale (light_bounce_apply; the trace keeps each texel's view depth in alpha).
  Skin drawn before the trace takes the last frame's. The sun shader lists are in addon.cpp (light_bounce.hpp Install
  takes them: Thrones / Rome II too); Update takes the swapchain.

## v22 (2026-10-23)

- Ambient Occlusion Quality (Normal / High / Ultra: 8 / 16 / 32 samples per side of a slice; ssao00 steps = 8 <<
  SI.ssao_quality). The paper (Therrien, Levesque, Gilet 2023): more samples keep thin occluders at a large radius;
  more sectors than 32 do nothing visible.
- Light Bounce (Experimental, new section; Strength 0 = off by default, Radius 3 m): screen-space indirect light with
  the same visibility bitmask (the paper's Algorithm 1: a sample's light counts for the sectors it is the first to
  cover, x both cosines). light_bounce.hpp registers command_action callbacks on the 26 directionallight ps30 slm /
  skinlm permutations (not ulm, not MSAA) and, per sun draw: the game's draw, a copy of the light buffer (RT0),
  light_bounce_trace into the addon's R16G16B16A16F bounce texture (cleared once a frame; alpha = written) with the
  game's vertices and depth / stencil (the same pixels), then light_bounce_apply (3 x 3 depth-aware average x albedo
  (G-buffer 2, gamma 2) x (1 - metalness), as the game's ambient) added (ONE / ONE, RGB) to the light buffer. Its
  constants at b12, light copy t10, bounce t11 (the game's put back after). Main view only (the display's size).
- Untested: check the bounce on sunlit walls / ground next to shadows, the cost at Quality Ultra and Radius 10.

## v21 (2026-10-23)

- Shadow Map Size 8192 in both of ATN's presets (ATN).
- Ambient Occlusion tooltip: the game's HBAO (4 directions, everything solid: dark halos) vs the Visibility Bitmask
  (GTAO with visibility bitmasks, 4 slices x 16 samples, a thickness per sample) (ATN).
- CPU: the DebugUnits probe removed (hidden ReShade.ini counters in the imposter hook and a per-frame status update);
  the Graphics status line (engine_tweaks.hpp StatusLine) shows only when something was not found or failed.
- Campaign trees (graphics_hooks.hpp OnDrawTree): no lookup at 1x before any change, and none for a model's instances
  drawn after each other (the thread's last tree, checked by its first LOD distance).

## v20 (2026-10-23)

- Shadow Map Size labels: 2048 (Game's) / 4096 / 8192 (ATN). The 4x / 8x unit sizes preset keeps the game's size
  (ATN), the vanilla one 8192.

## v19 (2026-10-22)

- Graphics (Experimental): Shadow Map Size (Game's / 2x / 4x; 4x = 8192 at Shadows Max). The shadow setup
  (+0x17665e0) picks the sun's shadow map size per Shadows level (Max 2048, High 2048, Medium 1024) as immediates
  and makes the textures from them; graphics_hooks.hpp shadow_map scales all six (one byte pattern, written at the
  game's device creation, restored at detach). Applies after a restart. Left alone if another mod changed the sizes.
  ATN's two presets: 4x. Untested.

## v18 (2026-10-22)

- Ambient occlusion Radius up to 10x (ATN).

## v17 (2026-10-22)

- Graphics (Experimental), Ambient Occlusion and their preset buttons: Advanced only (the round-4 check never hid
  them).
- Ambient occlusion Radius default 1 (ATN; Thickness stays 0.15).
- Video AutoHDR and Video Dithering at the end of Extra (next to the post-processing settings).
- No live counters (no CPU work per frame for them).
- PsychoV31 Highlight Hue Shift default 0.15 (ATN; Reset too).

## v16 (2026-10-22)

- Ambient occlusion in its own section after Graphics, "Ambient Occlusion (Experimental, needs in-game SSAO)" (ATN),
  labels Strength / Radius / Thickness; defaults Strength 1, Radius 4x, Thickness 0.15 m (ATN). Advanced: "Ambient
  occlusion passes per frame" (twwarhammer2/ao_trace.hpp: counts the AO shaders' draws / dispatches; 0 = the game
  never ran them).
- Graphics (Experimental) and the AO section: Advanced only; both titles pink.
- Big Battle Shadow Fix last in Graphics and off by default; the presets leave it off (ATN).
- ATN's presets no longer change the AO (its new defaults are ATN's).
- Reset: PsychoV31 (no Extended mode here; ATN).
- graphics_hooks.hpp: Rome II's unit and dead unit patterns beside Attila's (Update(..., rome2)).

## v15 (2026-10-22, Attila done)

- ATN's presets, both: AO Strength 0.39, Radius 4x, Thickness 0.19 m, Reflection Quality 2x, Building Detail 6x
  (atn_settings); vanilla: Unit Detail 1.20x.
- The ambient occlusion choice: "Game's" / "Visibility Bitmask" (was "RenoDX": ATN, misleading).
- Note: "... Some apply only after loading a battle or a save." / "The two presets below are the settings I would use
  myself." Top line: "Use these in-game settings: Gamma 20 and Brightness 10."

## v14 (2026-10-22)

- One Graphics (Experimental) section: the Lighting sliders (Ambient Occlusion, its Strength / Radius / Thickness,
  Reflection Quality) moved in; tooltips rewritten for players.
- Presets "With my 4x / 8x unit sizes" (Unit Detail x4 0.69, Dead 0.5, Animation 1.5, Variety / Footstep 3, Campaign 4,
  Reflection 4) and "With vanilla unit sizes" (Unit Detail 2, Dead 0.85, Animation / Variety / Footstep 3, Campaign 4,
  Reflection 4); each starts from every graphics setting's default (graphics_defaults), so nothing is left over. Reset:
  all defaults. The yellow note's second line: the presets are ATN's own settings.
- Building Detail Distance (new): battle_building_lod0-3_distance (100, 200, 300, 600 m), copied into a building model's
  LOD entries when it loads (+0x17fc1c1; the no-draw distance tweak only applies with battle_building_no_draw_enable,
  which the game leaves off: why Building Draw Distance did nothing). From the next battle.
- Shadow Distance removed (the cascade table code too). Campaign Detail max 4x.
- AO method: the posted reference (cdrinmatane.github.io, ssaovb-code; no licence on its code, not used) matches this
  implementation from the paper: linear sector space around the projected normal, countbits per slice. It marks a sector
  when touched (ceil); this one when half covered (round, the paper's round criterion), a little lighter.



- ATN: Animated Soldier Limit above 1x made soldiers invisible (taller animation pages; most likely the 32-bit game's
  address space: dynamic textures are mapped into it). Removed; the page height is the game's again.
- Big Battle Shadow Fix (default on) instead: the bone packing (PackAnimations +0x1893cc0) is hooked. The main pass
  packs, draws and packs again until all soldiers are drawn (+0x17fbb80); the shadow pass packs once per frame
  (+0x186be70) and the soldiers that do not fit keep an older frame's offset: their shadows are drawn with other
  soldiers' bones (the huge jumping shadows). When the shadow pass leaves soldiers over, the unit detail (sprite) and
  animation distances are multiplied down (by about the share that fit), held 3 s, then recover 0.2% a frame.
  DebugUnits shows the left-over count and the factor.
- Contact shadows removed (ATN: misplaced next to the game's shadows, noisy), with the four sun light replacements:
  the game's sun light shaders again.
- Building Draw Distance removed (ATN: no effect, city battles too).
- Reflection Quality (was Water Reflection Quality): stencil 1 is shiny surfaces such as armour (ATN), not water.
- "In-game: Gamma 20 and Brightness 10."
- Dead Unit Detail: works (ATN).



- Shadows jumping / huge stretched shadows in big zoomed fights (ATN debug1 flickering_shadow (1) / (2): giant soldier
  shapes in the far field, gone the next frame): the soldiers' bone matrices go to animation textures (t_animation_map,
  pages of 512 x 672 texels, 6 or 10 pages, made by +0x184d800; filled by +0x182e080, which stops at a page's height
  via its vtable +0x68). Soldiers that do not fit keep an old frame's offset and are drawn with other soldiers' bones.
  New Graphics slider Animated Soldier Limit 1x-8x: the page creation's push 0x2a0 (pattern ANIMATION_PAGE_SIZE)
  written x it (max 16384), from the next battle (pages made again). Presets: 4x / 8x unit sizes 8x, vanilla 4x.
- Dead Unit Detail: v10 / v11 read the caller's [esp+0xd8] as an animation id; DebugUnits showed 7487 tests per frame,
  0 dead, ids like WHALE_DIVE: not an animation id. Now the soldier's display state (the per-soldier update's third
  argument, return address + 12 + 0x144 + 8 + 12) flags +0xe4 bit 0: set for the living (+0x456b43 picks the living
  alpha speed or dead_time_alpha_fade_out_seconds by it). DebugUnits shows tests and dead per frame.
- Unit Detail: the 4 sprite distances as the sliders (Unit Detail: Foot Soldiers / Cavalry / Elephants / Artillery,
  0.1x-8x, the old Sprite Distance keys); Unit Detail Distance and the 4 fade widths removed (fade widths: the game's).
- Contact shadows (ATN debug1): grass skipped as receiver and occluder (G-buffer target 3 = (1, 1, 1, 0) for grass),
  fade 40-80 m (no more distant / sky artifacts; the campaign camera is beyond), thinner (0.15 m), 2-pixel minimum from
  the start (round edges were darkened), the ray starts 2 x bias off the surface.
- UI: section titles one colour (Extra's green), Lighting / Graphics (Experimental) pink; Extra (Experimental) right
  after Extra, Lighting before Graphics; yellow "In-game: Gamma 20 (max) and Brightness 10." on top.
- PsychoV31 Highlight Hue Shift default 0.20 (Reset and the Recommended presets too); Video AutoHDR 0.20 in the
  Recommended presets, 0 in Reset.



- Lighting (Experimental), new section (each needs its in-game setting on; ATN has in-game SSAO off: turn it on):
  - Ambient Occlusion: Game's (ssao.hlsl pixel_resolve, NVIDIA-style HBAO with 4 directions, kept as a function) or
    RenoDX (default): GTAO slices (4) and horizon samples (8 per side) with a 32-sector visibility bitmask and a constant
    thickness per depth sample (Therrien, Levesque, Gilet 2023, the paper Musa shared): light passes behind thin
    objects, no dark halos around soldiers / spears / trees. Same inputs (depth, the game's g_R radius, g_Strength,
    g_MinStrength), same output; the game's bilateral blur after. Strength, Radius, Thickness (Advanced) sliders.
    `shaders/ssao00_0xBD389EF1.ps_5_0.hlsl` (the original is ps_4_0; countbits needs SM5).
  - Contact Shadows (default on) + Length: the deferred sun light (directionallight.hlsl, shadows_hardware_gather_5_0:
    slm / skinlm, highdef / lowdef: sunlight00-03) multiplies its cascaded shadow by a 16-step ray toward the sun
    through the depth buffer (contact_shadows.hlsli; 0.4 m near, longer / thicker with distance). Rebuilt from the
    3Dmigoto decompiles with its mistakes fixed against the assembly (make_attila_light.py in the session scratchpad):
    sun_direction (cb2[0].yzw) printed as g_apply_environment_specular, and the next cascade's matrix / bounds / split end
    (cb1[i*4+6..9], cb1[i+42], cb1[i+37]) printed without their +1. Not covered: MSAA (_ms) and older shadow paths.
  - Water Reflection Quality 1x-8x: environment_specular_light.hlsl standard_lighting_model (+ full_res): the water
    SSR's g_ray_max_samples x it (ssr00 / ssr01; sun_direction.y fix as above).
- Output: SDR (gamma 2.2, Peak = Paper White = UI = display white; V31 BT.709) and Auto (default: scRGB with Windows HDR,
  else SDR), as Tales / Shogun 2. HDR_PEAK / ATTILA_PAPER_WHITE in shared.h.
- Unit Detail test (Advanced): the 8 imposter values on their own x Unit Detail (sprite distance and fade width for
  small / medium / large / huge). Dead Unit Detail did nothing (ATN): hidden probe [Attila] DebugUnits=1 in ReShade.ini
  shows per frame: imposter tests, how many had a death animation, the most common animation ids (status line).
- Status line only when something was not found / failed (or with DebugUnits); the full result stays in ReShade.log.
- About: NexusMods button next to Ko-fi (nexusmods.com/totalwarattila/mods/158); AO method credit. Section titles
  coloured (settings.hpp section_tints, opt-in).



- Dead Unit Detail Distance (0.05x-1x of the living ones' imposter distance): hooks ImposterDistance (+0x48a8f0,
  size + random -> distance). Its only caller is the per-soldier update (+0x4567e0, call at +0x456d20); from there the
  soldier's animation sample sits in the caller's frame, id at [esp + 0xd8] before the two pushed arguments
  (return address + 12 + 0xd8). Death animation ids (from the id -> name table at .rdata ~0x1bd52cc): 125-129,
  138-139, 185-324 (DEATH_*; the dead keep their last one). Only that call site (return address checked) is scaled.
- Graphics presets: "With 4x / 8x unit sizes" (Unit Animation / Variety / Footstep Effects 3x, Unit Detail 0.45x,
  Campaign Detail 4x), "With vanilla unit sizes" (the same with Unit Detail 2x), Reset (all 1x).
- Shadows jumping when zoomed into big fights (ATN): no shadow pool tweak in the exe; to test: Unit Animation 1x vs 3x
  (each soldier animated on its own past the shared-pose distance).

## v9 (2026-10-20)

- Campaign trees (ATN: the far fifth of the screen stayed low LOD; no slider changed it): they are drawn by
  DrawTree (+0x17ee130, a vtable method: a tree instance's LOD by its distance, with tree_lodding_method projected and
  x tree_lod_scaler) against the model's LOD entries, not the LOD pick, and their setup-time tweaks
  (campaign_tree_lod0-3) only apply to models set up after the change. Now Campaign Detail Distance hooks DrawTree and
  scales the tree model's LOD distances live (per model: the game's values kept, a value the game sets again is the
  new base); campaign_tree_lod0-3 no longer scaled (no double scaling). Plants (campaign_grass_lod*) and props stay.
- Water Reflections removed (ATN): the 10 environment_specular_light replacements and SI.water_reflections gone.
- Unit distances (the 18 of UnitDistances), new sliders 0.1x-4x: Unit Animation Distance (shared pose 8-11, facial
  13, hand 14), Unit Variety Distance (no-variant 12), Footstep Effects Distance (15). Unit Detail Distance stays the
  imposters (0-7). Dead soldiers: the engine has a dead imposter state (IMPOSTER_DEAD) and corpses use the same
  imposter distances; a dead-only distance needs the per-soldier update (+0x456843..., one large function) traced.
- Video AutoHDR tooltip: only above mid gray, the brightest the most; 1 = video white at Peak.
- graphics_hooks::Update takes a Scales struct.

## v8 (2026-10-20)

- Extra (end): Water Reflections, Video AutoHDR, Video Dithering. `autohdr.hlsli`: Tales of Arise's Video AutoHDR
  (Ryse's): linear luminance above mid gray stretched (smooth start, hue kept) so SDR white reaches
  lerp(1, Peak / Paper White, amount); HDR modes only.
- Video AutoHDR (0-1, default 0) + Video Dithering (default On: 1 code value of interleaved gradient noise first):
  cutscene00 (the Bink YUV video shader), on the 2.2 decode before the 2.0 frame encode.
- Water Reflections (0-1, default 0; ATN's idea: 0 the game's, 1 reaching Peak): the deferred water reflection pass
  (environment_specular_light, stencil 1 = water; 10 permutations as waterreflection00-09: sm_4_0 / 4_1 / 5_0 x
  shadow methods x full res / MSAA) reflects s_environment where SSR misses (the sky). That cube is 8-bit
  (environment_map.dds, BC1), so the sky on the sea stopped at Paper White; the stretch applies to that sample only.
  Campaign sea and battle water. With SSR off the waterplane shaders sample the cube themselves (not covered).

## v7 (2026-10-20, Attila done for now)

- Unit Detail Distance: 0.1x-8x.
- Campaign Detail Distance (replaces Campaign Prop Distance): campaign trees (model type 0x6e) and plants (0x6f) take
  campaign_tree_lod0-3 (30 / 60 / 90 / 120 m; lod4 10000 stays the last) and campaign_grass_lod0-4 (60 m) in the model
  setup (+0x17fbd00) when they load (next campaign load), and trees are picked by their own loop (+0x17ee130, a vtable
  method; not the LOD pick: Model Detail never reached them). Live: rigid_campaign_tree_lod (100 m) and
  campaign_props_max_distance (60 m). Model Detail Distance's tooltip now says battle.
- Campaign sea reflections (ATN: capped at 203 nits): not clipped. The campaign water (waterplane ps30_campaign_*) and
  the deferred reflection pass (environment_specular_light) reflect the sky from s_environment when SSR misses, which
  is terrain\campaigns\<map>\lighting\environment_map.dds: a 512 x 512 BC1 cube (8-bit, white = 1.0 = Paper White).
  The sky itself is the FP16 skybox cubemap.dds, so the reflection is dimmer than the sky by the texture's format.
  Left as is (ATN: no artificial boost).
- Recommended presets (no VFX boost / All Conditions / Day / Night): Rendering Mode PsychoV31 (V31's own sliders, hue
  shift 0); Reset to default stays PsychoV30 (the default mode).

## v6 (2026-10-20)

- Shadow Distance (0.5x-4x, live): the shadow cascade table (.rdata +0x1c88aa0, battle and campaign x 4 shadow
  qualities x 4 cascade widths; battle at the highest 30 / 90 / 210 / 450 m = a 780 m range) scaled in place
  (VirtualProtect, the game's copy restored on detach). Both shadow paths read it every frame (new_shadows' fit
  +0x1794430, the old setup +0x1784c00) and the shadow range sums it (+0x17520cd). Below 1: the same shadow maps over
  less ground (sharper near the camera, shorter reach). v4's hook scaled the fitted splits instead; at the highest
  quality the range is already 780 m, so farther shadows are hard to see. No shadow map resolution found statically
  (no per-quality size table; would need a runtime resource log).
- engine_tweaks.hpp: FindPattern takes the memory to search (code or data); Slider takes a minimum.

## v5 (2026-10-20)

- ATN (v4): Grass and Shadow Distance cost a lot and changed nothing visible: removed (hooks, the grass vertex shaders
  and SI.grass_distance gone). Unit Detail Distance looked good but was CPU-heavy: it scaled all 18 unit distances.
- Unit Detail Distance: only the 8 imposter values now (where units turn into flat sprites, per unit size, and the
  random switch width). Shared poses, variety, facial / hand animation, footsteps, unit shadows stay the game's
  (the 18, named from the game's ENTITY_LOD_OVERRIDE tweaks, are listed in graphics_hooks.hpp).
- Model Detail Distance (the LOD pick hook, v4's Object Detail): every model's 3D LOD by distance. A scan of the whole
  engine for compares against a LOD entry's distance (tools/tw/x86_lod_compare.py) finds only the pick (and the
  fade-out at the last LOD, and two sorts), so soldiers' parts (4 LODs, authored 100 / 200 / 400 / 500 m; mesh types
  0x41 / 0x4f / 0x44 ... take the setup's default path) go through it too, as trees, buildings (battle_building_lod*),
  props and siege engines (the warscape_rigid_lod_range db table: 200 / 400 / 750 / 1500 m). Also scales
  rigid_campaign_tree_lod (100 m, the campaign map's rigid tree LOD).
- New: Building Draw Distance (battle_building_no_draw_distance_calibration 400: buildings are not drawn beyond
  size / 2 x it; set when a building loads: next battle), Campaign Prop Distance (campaign_props_max_distance 60 m x
  the quality's 0.25-1, read per query: live).

Verify: Advanced "Engine values found: 3 of 3; object detail hooked; units hooked"; soldiers at 100-300 m with Model
Detail 1x vs 4x (their 3D detail), Unit Detail (sprites farther), far buildings after a battle load, campaign props.

## v4 (2026-10-20)

- Graphics (Experimental): a yellow note on top ("Experimental: these may not work, and may crash the game.").
- ATN (v3): Model Detail changed only trees; Grass, Terrain Detail and Shadow Distance did nothing. Found why:
  - Units have their own distances: UnitDistances (+0x43c160, thiscall, out + 4 args, every frame) fills 18
    floats per Unit Detail quality (imposter distance per unit size, mesh detail, shared poses, no-variant, unit
    shadows; the ENTITY_LOD_OVERRIDE tweaks only replace them when that bool is on). New Unit Detail Distance
    (up to 8x): the hook scales all 18 (`graphics_hooks.hpp`).
  - Model Detail (the LOD pick hook) renamed Object Detail Distance (trees, other objects).
  - Grass: the scrub node update (+0x17fb510, every frame) culls cells at node +0x80 (int, 200 m), but the grass
    vertex shaders (rigidgrass / rigidgrasswet vs30_main_grass sm_3_0 0x01FA9CDE, sm_5_0 0xF3741248,
    vs30_shadow_grass 0xDFECAA1A) flatten the blades by 100 m (0.01 x distance). Now: the update hooked (node
    +0x80 x slider, per node, live) and the shaders' 0.01 / SI.grass_distance (new shader_injection field;
    max(, 1): stock without the injection). v3's scrub tweak and 200 m code constant removed.
  - Shadows: with the new_shadows tweak (+0x1795270, unless the graphics vfunc 0x370 returns 2) the shadow setup
    takes the cascade splits FitShadowSplits (+0x1794430) fits into the camera's shadow range, not
    shadowmap_split_scale. The hook moves every split away from the near distance (+0xbd4) by the slider;
    shadowmap_split_scale stays for the old path.
  - Terrain Detail Distance removed (terrain_far_shader_distance: no visible change).

Verify: Advanced "Engine values found: 1 of 1; object detail hooked; units, grass and shadows hooked"; in a battle:
Unit Detail (units stay 3D farther), Grass (taller grass farther), Shadow Distance, live.

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
- Bloom (ATN: looks clamped): the high pass (`bloom.hlsli`, bloom00 / bloom01: both cutoffs) caps each
  pixel's bloom at luminance 25, invisible in SDR but a flat halo in HDR. HDR: stock up to 25, then
  25 + 25 x log2(luminance / 25).

Verify: Advanced "Engine values found: 4 of 4; model detail hooked"; bloom around the sun and fires in HDR.

## v2 (2026-10-20)

- Graphics section (1x = the game's, up to 4x; models and grass set up after the change: next battle or
  campaign load):
  - Model Detail Distance: the LOD 0-3 distances of `battle_entity_*`, `battle_building_*`, `battle_ship_*` and
    `campaign_tree_*` (Attila's per-quality factors are constants in the code, +0x17fbd30, so the distances are
    scaled instead).
  - Grass Distance: `scrub_lod_distance_default` (200 m), copied into each grass node when it is built
    (+0x17cb28e).
- Shared code: `twrome2/engine_tweaks.hpp` (Rome II, Attila, Pharaoh): the engine's named tweaks are found by
  name at the first frame (x86 empire.retail.dll: `push name; mov ecx, object; call constructor`, the value at
  object + 0x48; x64 Pharaoh.exe: `lea object; lea rdx, [name]; call constructor; movss [object + 0x60]`) and
  scaled every frame; a value the game sets again (its graphics settings) becomes the new base. Advanced shows
  "Engine values found: n of n".

Verify: Advanced shows "Engine values found: 17 of 17"; set a slider, load a battle: units / buildings keep
their detailed models farther away, grass reaches farther.

## v1 (2026-10-20)

- Rendering Mode: SDR / HDR (PsychoV30, default) / HDR (PsychoV31): the same PsychoV31 sliders as Rome II v1 (Musa's
  custom test31; Highlight Hue / Saturation Shift toward the SDR image's per-channel clip).
- Shared code: `hdr_to_screen.hlsli` (every hdr_to_screen permutation, gamma 2.0 frame), `tone_mapper.hlsli`
  (ApplyAttilaSCurve + ToneMapperHdr: t00 / t09 / t10), `lensflare.hlsli` (ApplyAttilaLensFlare: lensflare00-03).
- Shader packs (dump `bin\cmd_Decompiler-1.3.16\dump_shader_twattila_pack`): t09 / t10 the constant tone mappers
  (fixed exposure), lensflare01-03 (flare_1-3: the Lens Flare slider on every flare), vignette00 (the lens flare's
  vignette) with a new Vignette slider.
- Not ported: the constant tone mappers "with_bloom_and_gamma" (+ MSAA 2x / 4x / 8x and stencil alpha; they write
  display-ready output, likely the 3D UI portholes) and the particle shader's other shadow permutations (the mod's
  vfx00 / vfx01 are hand-fixed decompiles, not a one-line edit).
- About: credits, ATN's Ko-fi.

Verify: SDR / V30 / V31 in a battle and on the campaign map; Lens Flare and Vignette sliders with the sun in view.
