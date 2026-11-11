# RT scene: shadows that hold everything

Ray-traced and path-traced shadows, AO and light need every caster the game's shadow maps had. One family missing (trunks, banners, far trees, units behind the camera) looks worse than the game's own maps, and in WH3 each missing family cost a test round. Build the whole census before the first test round.

Order found in WH3 (each a round): units only; leaves on the wrong slot, grass, tessellated terrain; the sun patch on the ambient; terrain skirts and proxy boxes as black patches; banners and cut-outs; buildings, rocks, indirect grass; placed objects; the dragon off screen; casters from the shadow pass; trunks, banners and missed cut-outs again; a tent stand-in; trees past 40 m; the far-card budget. Attila's list was similar: the sky dome blocking every ray, rows under a triangle list, back faces, 2-state grass, sprite soldiers, far trees, smoke, palms, grass twice, cut-outs, cloth, receivers.

## 1. Coverage checklist (a new game, in order)

1. **Sun shaders**: t49 lands on the sun term, not the ambient (WARP sun test). List every shadow-map reader and every forward pass that lights itself (dump for candidates, traces by handle).
2. **Multiply or Replace** decided first (section 5). Log the cascade splits; find any kept static cache (copy shader, array swap on scroll).
3. **Census of the main and the shadow pass** in traces: VS, PS, index count, instances. Map each shadow VS to its main-pass family. Flag stand-ins (in the shadow pass only) and LOD pairs.
4. **Terrain**: tessellated? A GS that only makes skirts? A camera-distance height push? A mask discard (sea)? Its room first. Where the hills' shadow comes from.
5. **Rigid statics** (buildings, rocks, props): a VS shared with water or decals needs a PS allow list; proxy boxes (PS reads depth and discards) stay out; a position from `SV_InstanceID` means `.dynamic`; place a mesh only per rule 3 below; rows drawn as points.
6. **Moving rigid** (weapons, armour, siege engines, projectiles): last frame's rows mean moving; classify siege engines and projectiles in the census, nothing records them as a family yet.
7. **Units**: every VS variant (burn, wind alpha), shares, cut-outs, dissolve, units out of view from the shadow pass (`rt_outside_view` and its UV mask), sprite soldiers (sun-facing, sorted last).
8. **Trees**: trunks and branches (a two-matrix blend?), leaves (mask slot, burn map), sway, 4-state micromaps, far cards (sun-facing, own count, sorted last), trees past cascade 1.
9. **Grass**: direct and indirect (`CaptureIndirect`), mask per PS, captured from the G-buffer draw only, dedupe on x and z, shrink by distance, never 2-state, receivers take the full ray, captured only while a ray that meets it runs.
10. **Thin surfaces**: cloth, sails, flags, banners, decided by model or material family (Attila's unit banners are not thin); time sway means `.dynamic`; two-sided sheets traced from the camera's side; a sheet counts once.
11. **Cut-out census** over every scene row and the shadow pass (rule 7).
12. **Smoke and particles**: does the game already shadow them? Count only particles with opacity, the smallest give way, own TLAS, every lit shader found.
13. **Point and fog lights**: the light VS layout, the trace cap logged, the game's point-light shadow map; fog replaced, not multiplied.
14. **Budgets**: each family logs "left out" or "without room"; a harness stress mode per family; the kept limits (6 M triangles, 12,288 meshes).
15. **The game's maps**: skip only per-frame draws; Shadow Maps Under Rays for the readers left; RT or PT to Reset gives the game's shadows back at once.
16. **Test list**: one item per family, with Debug View 1 by draw and the Sun view.

## 2. Families: where drawn, budget, overflow answer

Write one line per family for each game. A budget without a stated overflow answer, a log line and a stress mode is a bug waiting.

| Family | Drawn by | Capture | Budget and overflow answer |
|---|---|---|---|
| Terrain | Main pass; WH3 tessellated, plus a coarse pass whose GS only makes skirts | `.tessellated`: DS streamed out, count read at the trace; the skirt pass and the height push left out | Room first; "left out" line |
| Rigid statics | Main pass; behind the camera only in the shadow pass | Placed BLAS when the VS is rows x VB position and nothing else; else captured each frame | Per-kind share; "without room" |
| Units | Main pass culls behind the camera; the shadow pass draws them [e8aaefcb] | Stream out with bones; out of view through `rt_outside_view` | Per-kind share [da53d23a] |
| Trees | Main pass; WH3 draws them each frame only into cascade 1 (40 m), farther only at the kept cache's refresh [2ca382d2] | Trunks as drawn (second rows) [10ea2b61]; leaves with their mask slot | Per-kind share |
| Far cards and sprite soldiers | Main pass, sun-facing | Sorted last in the capture list by flag, never by position [10ea2b61] | Counted apart from smoke; past 64 *blocked* [5d240089] |
| Grass | G-buffer draw, direct and GPU-culled indirect | Masked per PS; dedupe on x and z; 4-state micromaps plus alpha | While micromaps bake, past 64: *lit* [ad70391a] |
| Cloth and thin | Main pass | Thin by model family; two-sided from the camera's side | Sheets within 10 cm count once, up to 8 surfaces, then *blocked* [f19e5423] |
| Smoke | Particle draws | Regular sorted BLAS, own TLAS, own ray [6ad8a2c2, 321434ab] | Smallest give way [6875b28f, f8606d9a] |
| TLAS and room | - | - | Grow on a worker at 3/4, step x4/3 [c7ffc569]; start at half the limit on big GPUs [965c89cd] |

Overflow answers: blocked for solids and cards; lit only for grass still baking. The harness modes `far_cards_20` and `far_cards_48` prove the far-card answer (helper102 lit 28 of 28, helper103 shadowed 28 of 28).

## 3. Rules learned

| Mistake | Rule | Check |
|---|---|---|
| Far-tree shadows flickered, a dark wedge at each tree's foot: the helper took sun-only captures from the *tail* of the frame's list, and WH3 draws units and terrain after its cards [10ea2b61] | No meaning by position in the capture list: a flag or an explicit sort. List every first or last rule in `rt_d3d12` when porting | Capture order in a trace; game: Far Tree Shadows at a forest edge, low sun |
| Far trees lit through a wood, shadows changing with LOD: cards counted against smoke's particle count, and the ray gave up lit [5d240089]. Grass past 64 went black, then lit; cloth's 4 sheets let no light through [ad70391a, f19e5423] | One line per family: budget, overflow answer, log line, stress mode | Harness stress per family |
| Black trunks in AO, shadows and PT AO: WH3's tree VS 474F97FB and 783383C6 blend two sets of per-instance rows by `v6.x`; a placed BLAS is its first instance's blend. Open | Place a mesh only when its VS position is rows x VB position and nothing else per instance (no blend, bones, wind); else capture it each frame | WARP with two or more instances whose extra per-instance data differs; game: AO Debug View on pines, Placed Objects off and on |
| A black spot on tents: a 204-index stand-in drawn alone by the shadow pass is also in the G-buffer pass, captured over the 13,632-index cloth; rays start between the sheets. Open | Census meshes per material, main against shadow pass; a mesh only in the shadow pass is a stand-in: take it only out of view, or not at all | Census over traces by index counts; game: a tent, Shadow Casters Out of View off and on |
| A dragon's shadow gone just off screen; a flat picture after RT or PT to Reset mid-battle [e8aaefcb, 9a182b5f, 2ca382d2, dfb2334e] | Per family: which pass draws it each frame, to what range, and whether the game keeps a static cache. Skip only draws into the per-frame maps | WARP `outside_view_wh3`, `casters_wh3`; harness `HARNESS_KEPT`; game: pan off a forest and a wall, then Reset mid-battle |
| Shortcuts on the receiving pixel leaked light: pixels facing away trusted N.L [90bebfa1]; those then took a solids-only ray, grass in a house's shadow 4.6x too bright [b9f2df6a]; a two-sided banner traced by each triangle's facing came out blocky [ad21eedd] | Every receiver takes the full set of casters; facing from the game's shading normal; thin or two-sided sheets trace from the camera's side | Harness plate behind a wall, `leaf_cut_out*`, `cloth_folds_self` against `cloth_folds_smooth` |
| The cut-out census came out incomplete five times: black beards (92 PS) [78c0f619]; palms as solid cards [43dddcf3]; 541 shaders, then two missed because the script took one discard per shader [5eed585f], then 66 in no trace [10ea2b61]; alpha-tested rigid counted as leaves let a quarter of the sun through houses | Classify every discard of every PS, in scene rows and the shadow pass: base alpha means a mask; dither means solid; an instance fade is read; dissolve or a multi-layer product is level C. Solid cut-outs are their own group, never leaves | The census script prints only the declined shaders |
| No light shader seen drawn; vertices read as lights under an unknown layout; torch rays clamped at 4 when Rays a Pixel went 8 [8f0035de] | Read the light VS layout; log the trace cap; take back the game's point-light shadow share | WARP `lights_wh3`; game: siege at night, Debug View 6 not magenta |
| Fog Shadows alone turned RR on, waited for the terrain count, captured grass: 90 to 50 fps [bf21d2d2] | Each ray feature declares what it needs (terrain, grass, RR); capture only for rays that run (`CheapFogAlone`) | Harness fog-only scene: 0.83 against 0.19 ms |
| Hills Out of View could not change the sun's shadow: WH3's hill shadow is a height-map shadow map | Read where each shadow comes from before building a row for it | The shadow pass's draws by family |
| A run plan that couldn't measure: the AO view needs the AO row on (magenta otherwise); a test key the game doesn't have; a helper that didn't log placed instances | Check each planned shot against a page dump of the preset, and each log line it relies on | `page_dump` |

## 4. Capture traps

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| A scene table line finds leaf UVs itself | `SceneCoverage` runs only for hosts with `scene_draw` (`rt_core.hpp`). A table host sets `.foliage`, `.uv_semantic`, `.uv_semantic_index` by hand, or leaves are solid cards | Read the leaf VS and PS; RT Debug View with Scene Test |
| The clip mask is PS t0's alpha | `rt_core.hpp` reads `mask_slot`, default t0 (t1 for `far_tree`). Grass may test another slot, with t0 a virtual-texture page; one VS can serve families that differ (`pixel_masks`). Read which texture the `discard` tests, in every PS of the VS's families | Disassemble the PS; the bound textures in a trace |
| Billboards go in the scene | Clip-space cards built from `cb0[0].xz - position` face the camera, so their shadow turns with it. Sun-facing cards are sorted last and counted apart from smoke | Read the VS basis before listing it |
| `.dynamic = false` limits capture cost | Any draw with a `D3D11_USAGE_DYNAMIC` vertex buffer is captured each frame anyway | Log Details |
| Any capture list works | `rt_core.hpp` returns on anything but a triangle list; a tessellated VS gives control points: such an entry needs `.tessellated`. Its hull shader culls by frustum and constants, so one key can be two draws | WARP stream-out test with the real bytes (`rt_terrain_wh3`: written equals rasterized); a "left out" line |
| A family name says it draws a surface | WH3's `fx_rigidterrain` VS 5767F87C draws boxes whose PS reads depth, discards and paints terrain inside: black patches in sun and AO [24319a55] | Scan each row's PS for "reads depth or G-buffer and discards" |
| A clip-space game can place meshes as Attila does | `rt_vertices` unprojects every texel, and a placed draw's rows are texels. WH3's placed parts go in the captured buffer's top (`WORLD_BASE`), after the clip-space vertices (`RT_VERTICES_WORLD_AFTER`) | `Static BLASes:` line, `rt_capture_test.py` |
| One VS means one kind of mesh | WH3's 4F35D09F draws buildings, water, decals and effects: rows need a PS allow list | Log "left out: vs ... with ps ..."; water or vfx in the capture lines is a bug |
| An indirect draw can stream in with a count read later | WH3's GPU-culled grass streamed as a soup: trace 225 ms against 5. Read its numbers and capture it as a direct draw (`CaptureIndirect`); a copy read without waiting skips draws empty lately | GPU ms before and after; PresentMon paused with the read on and off |
| A draw streamed out is what the game draws | Stream output replaces the game's GS and runs no PS: WH3's coarse terrain GS only makes skirts, and its PS's mask also discards what SO keeps | For a tessellated or GS draw read the GS and the PS's discard; Debug View 1 one draw at a time |
| A mesh placed by `SV_InstanceID` from a buffer isn't in a capture's key: static VBs kept as first captured, stale after a camera move (WH3's black banners C0ED2883) [ec69928e] | `.dynamic` unless the position comes only from the VB and constants in the key: read the VS's position chain | The hit point read back through a Debug View: 0 to 2 m from the pixel means a copy |
| Scene Tests don't clear kept static meshes: "Terrain Only" still held them | After a Scene Test change wait for the keep, or restart rays; Scene Test 4 for what's kept | Rays off and on clears them |
| A Debug View's colour isn't linear in a shot (gamma 2.2, white 124 nits in scRGB; sharpening smears codes) | Encode values in 8-pixel bands; calibrate against numbers logged from C++ | Calibration script's exponents all 2.2 |
| One mesh captured twice: depth prefill plus G-buffer paid the share twice; hidden and shown instances raced in the dedupe [78c0f619] | In the main pass, capture only with a colour target bound (foliage with rows and thin only). The shadow-pass caster capture is the deliberate exception | Group draws by vs, ps, targets in a trace |
| Rows drawn under the game's triangle list streamed nothing: giant shapes, long lines [28832b16] | Point list for one-vertex-per-instance draws | Harness D3D11 side, read back |
| Scenery re-keyed every frame (instance streams repacked) | Key by geometry; BLAS once, rows each draw | "new captures" line, harness |
| Dead entries crowd out live ones: a particle draw holds its whole list | Count only what has opacity [85b0c873] | Log counts |
| Smoke as Mega Geometry clusters: trace 3.6 to 194 ms | Regular sorted BLAS, own TLAS, own ray | `HARNESS_SMOKE_STRESS` |
| LOD switch: the old copy darkened the new for 16 frames; gaps while moving [6fe88cde, 6902136f]; an out-of-view mesh's two LODs both stayed | Drop drawn-out copies at once; place every frame while the camera moves; one model per place [0a1cd78f] | Harness scene test |
| Dedupe key held a camera-distance shrink: grass doubled while moving | Key on x and z, nothing camera-driven | Harness |
| Terrain captured with the camera-distance height push: black circle under the camera [bad338fa] | Capture without the push; clip-space captures inherit jitter and any push | RT ground against depth in the Debug View |
| UI's small 3D views and sky domes in the scene: every ray blocked [0443497a] | `main_view` gate; dome rule in `rt_core.hpp` | Trace viewports; Log Details |
| Garbage triangles spread the bounds: trace 1000 ms | Helper NaNs them and logs the shader | "garbage triangles" line |
| Grass forced to 2-state micromaps: fields black; OMM-opaque parts never reach the candidate loop | 4-state plus alpha; step nearest hits | Harness |
| Cluster templates without a per-triangle geometry index: garbage GeometryIndex | Zero-filled buffer | Harness candidate debug |
| BVH order changing with the same input moves shadows (sway rows packed by thread order) [b11f567c] | Deterministic order: a ray past `MAX_CANDIDATES` depends on it | Harness frame-to-frame pixel count |
| Rays start wrong: own blade [b11f567c], own surface [8e044106], riverbed under water, 3 mm helmets [6902136f]; terminator facets trust the depth normal [db34e39d] | Per-surface offsets; ask the triangle when edge-on | Harness plate checks |
| Screen-edge pixels take a normal from past the view: black first and last columns [5a995075, 88270d2b]; border fade cut reflections [97e0fe87] | Use the neighbour inside; test at the edges | Harness edge modes |
| A hull or vertex shader's culling and factors read the screen: behind the camera they mean nothing | An own hull shader for what's out of view (`rt_outside_view`) | WARP `outside_view_wh3`: behind the camera too |
| Nothing marks foliage in stencil or G-buffer | Don't assume; depth copies around the prefill | Trace, dump |
| Foliage via an extra target at SV_Target3 wrote nothing and fought kept targets | Don't | - |
| A row is alive because its table entry exists | Coloured Shadows' filter comes only from `.thin` captures (or `InstallThin` under SSS): no such entry, a dead row | Grep what the feature reads back to the capture flag |

## 5. The game's shadow maps under rays

- **Multiply or Replace, first.** Under Multiply, light through leaves and cloth shows only past the maps' reach. Replace (default since WH3 v33) needs the off-screen casters of section 3 first.
- **Readers left under Replace**: water (WH3 2076D289, DFA2AEA2, 034010C4), lit effects (9EA8D889), particles' fog. In WH3, 454 shaders can read the maps and 7 were seen drawn: list them from traces, `InstallShadowReaders`, and Shadow Maps Under Rays for the rest.
- **`InstallShadowsOnly`** learns the map from a square depth texture bound at the sun draw; a game whose sun PS reads a screen mask binds none, so it can't work there.
- **Skip only per-frame draws.** Anything kept across frames (the static cache, its copy, new strips on scroll) stays drawn, or RT or PT to Reset shows a flat picture until the cache refreshes [dfb2334e].
- **Proof**: Reset mid-battle gives the game's shadows back at once; the double-work table in [ao-gi.md](ao-gi.md) lists the maps' cost.
