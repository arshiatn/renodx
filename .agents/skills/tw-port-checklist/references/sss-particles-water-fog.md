# Screen-space shadows, particles, reflections, water, fog

## 1. Screen-space shadows (Bend)

**What it does**: Bend's wavefront march over a copy of the scene depth, at the first main-view sun draw: `sss_depth`, `sss_setup` (the dispatch list built on the GPU), 8 indirect `sss_bend` dispatches, optional `sss_blur`. The patched sun shaders multiply their shadow term by 1 - t49, so only direct sunlight changes. Smoke (`smoke_map`, Attila only) and the RT occlusion use the same t49.

**What it includes** (defaults in `twattila/feature_settings.hpp`): Softness (Off) and Softer Away from the Caster (Experimental, off); Light Through Banners (0.35) and Coloured Shadows (on; t50, only where the rays bind none); Torch and Fire Shadows (Off, 4, 8, 16 or 32 lights within a range; Attila, Thrones, WH3); Light Through Foliage (0.5; Attila from depth copies around its prefill, WH3 from its G-buffer foliage bit). Auto Thickness was removed. The 128-light rows are the rays' ([rt-scene.md](rt-scene.md)).

**Cost** (4K, 100%): base 0.81 ms; Banners +0.08, Coloured +0.60, Softer +0.53 ms; torches 0.35 to 1.4 ms; smoke map 0.1 to 0.16 ms.

**What it can't do**: casters off screen, reach past 1920 px, smoke alone, colour or softness for torch shadows, banners under MSAA. It needs the game's shadows on.

**Against RT**: it runs on any GPU without the helper, cheaply, on exactly what this frame drew. It loses off-screen units, thickness, light through every thin thing, point lights, far reach.

**Order for a new game**: the game's own SSS or contact march, and smoke in its mask; the sun PS list, patch route and layout (`sun_draw_layouts.py`); WARP patch test; the depth source, near, far and sky z; render-share scaling and the far fade; gates (off under RT shadows, `MainViewport`, the Debug View in the real tone-mapper hook); extras by install; `test/tw_screen_space_shadows` and timings.

| Mistake | Rule | Check |
|---|---|---|
| 64-px blocks, a shadowed arc on flat ground, one corner fully shadowed for four versions: SRVs bound before UAVs, so D3D11 nulled the previous pass's outputs. Four builds chased "16-bit depth" and "camera mapping" first | In chained compute passes bind UAVs before SRVs. When the GPU disagrees with WARP, capture the real input and the GPU's output once and replay the addon's exact bind order on WARP before forming a theory | `warp_tests/sss/sss_real.cpp` reproduced the GPU mask exactly |
| The sun patch on the wrong term: in three WH3 sun shaders t49 sat on the AO, so Bend and the RT sun only darkened the ambient [b7076619] | Write down what t49 multiplies, per sun PS ([game-facts.md](game-facts.md) lists the routes) | WARP: t49, t50, t51 unbound equals the game (0 of 131,072 differ) |
| Depth: the G-buffer's depth too coarse far away; sky at z = w streaked; the campaign backdrop at 75% of far; a side-on sun with w = 0 | Copy the depth-stencil the sun draws keep bound (R24 or R32, MSAA sample 0); skip sky pixels; fade 0.4 to 0.6 of far; z = `_m22 / _m23` and w's sign from the divisor | `sss_scene` with that camera and 6 sun directions, horizon included |
| Sun direction and camera layout assumed to be Attila's: SSS lines, shadows 180 degrees off | Name them per draw (`sun_draw_layouts.py`) | WARP `sss_scene` |
| Striped hills and combed fences below 100% | Thickness and the bilinear threshold are tolerances per pixel step: divide them by the render share, samples x share; extended variants round counts to fours | `test/tw_screen_space_shadows` case 1 at 50% and 58% |
| Bend over the game's own march doubles contact shadows; Shogun 2's sss00/01 already is Bend; contrast 4 flipped close-up shadows on and off each frame | Grep the dump for the game's march and smoke in its mask first; replace it, don't stack; stability rows (contrast 2, IgnoreEdgePixels) | Read the mask shader |
| Per-sample loops and runtime-skipped branches still took registers: Banners +2.6 ms, Auto +3.7 ms | Compile variants instead of branching (Bend's 4-lane loops, 4 variants); with every row off the shader is today's bytes | Diff plain-shader bytes old against new |
| Bend's waves write some pixels twice: 92 of 129,600 differ between runs of the same shader | Compare against several runs of the original, not one | - |
| The soft mask read in frames its pass never ran; t50 bound after a resize; objects kept while rows were off (about 117 MB) | Read a mask only in the frame its writer ran (frame stamp); remember every bound SRV of ours; free objects at the first present a row is off | Test cases 4 (CreateTexture2D failing) and 7 |
| Rows that show where their install never ran | Coloured Shadows and Light Through Banners need the replay of known banner VS (`InstallThin`: R32F, DepthBias -4, every state put back, off under MSAA; the VS outputs SV_Position at o0, check with `fxc -dumpbin`); Torch and Fire Shadows need the light draws after the sun draw | Rows hidden where `InstallThin` or `InstallPointLights` didn't run |
| Light Through Foliage took Attila's depth copies around a foliage prefill; WH3 has no such prefill, its foliage draws mixed with others, and its stencil refs (9, 145, 1, 17) mark no foliage | Read the game's own sun and contact-shadow shaders for a material test first: WH3's sun mask lets light through where G-buffer channel 3's alpha has bit 128 (`foliage_material_bit`, read at `gbuffer_slot + 2`, off under MSAA) | `test/tw_screen_space_shadows` material case: 0 pixels apart from the depth-copy result on WARP |

## 2. Particles and transparency

Order: trace the composite, its layers and readers; count only particles with opacity; VFX rows on `o0.xyz` only; half size (blends, readers, depth); history tests; RR inputs per preset; `smoke_map`, then RT smoke.

| Mistake | Rule | Check |
|---|---|---|
| Particle Trails flagged blocks while paused: WH3's MBOIT moments are half size and point-copied (A759AD5D), so jitter flips 2-px texels at edges [716ae95f] | History rejection on a half-size or point-copied buffer tests the range of the neighbours around last frame's place | `warp_tests/smear_wh3` (edge flags 216 to 0) |
| Smooth Half-Size Particles' first design threw flipping sparks away; an HLSL array made a 32-byte struct's cbuffer 68 bytes [f51b4d05] | Write the flip, move, pan and cut cases before the shader; the reflected cbuffer size equals `sizeof` | `test/tw_particle_history` |
| A redirected draw unbound the OM UAVs (`OMGetRenderTargets` doesn't report them); WH3's composite carried a writable DSV on the depth being read [ade0799d] | `OMGetRenderTargetsAndUnorderedAccessViews`, restore UAVs with their counts; refuse other blends, scissor or MSAA with one log line; compare the DSV's resource with every SRV read | Read the redirect in `twattila/particle_layer.hpp` |
| RR's particle inputs: skipping the denoiser loses DOF's blur; Picture Before Particles makes the rest 2.5% brighter; the particle mask was read by no preset. Below 100% RR's colour comes after the tone mapper, so the composite is redrawn unblended into a cleared texture [ef9be189] | Test each optional NVIDIA input per preset in the harness, and know its colour space at each scale. Particles at output resolution are impossible: they would lose exposure, bloom, DOF and grading, and there is no display-size depth | `HARNESS_RR_EXTRA` |
| Smoke: Bend never sees particles; RGBA16F height moments failed | `smoke_map` splats last frame's lit particles into a 1024 squared RGBA32F map from the sun | `smoke_map_test` |
| Smoke shadows flickered at Ultra and the log's 262,144 particles looked like a full stream: that is the whole pool, drawn every frame, dead ones too; the siege used 35,304 of 65,536 smoke slots. Likely cause, unproven as of 2026-11: each sun ray stops after 32 smoke particles that dim it or 64 it meets, and which come first changes every frame | Read what a count counts before raising a limit; log how often a ray hits its cap | A siege with Ultra against Extreme in one run |
| Particles at a half or quarter layer smeared across sharp edges (units, walls against smoke) | Full-Resolution Particle Edges (Cantlay, GPU Gems 3 ch. 23): `particle_edges` marks edge pixels in stencil with the scene depth in an own DSV (D24S8, D32S8 for float depth); each particle draw is drawn again there at full size with the game's depth state and stencil EQUAL 1; the composite takes stencil EQUAL 0. Reset the edge target when the layer is made again | `test/tw_particle_history` case 11: full-screen smoke at Quarter 26 steps off without edges, at most 1 with; a first version over the quads' edges, which the check skips, passed both ways |

## 3. Reflections and water

Order: which pass reflects which surface (its gate and flags); water hashes and G-buffer timing from a trace; Hi-Z and mips at the render share; that game's lobe, F0 and light; water rays only with the fog leg; patch order (`Repatched`); Reset restores everything.

| Mistake | Rule | Check |
|---|---|---|
| Rays on water started at the riverbed: water draws its own G-buffer after the deferred light | Decide per surface which pass reflects; take water's start from its own G-buffer | Trace order |
| Far hills as dark patches on rivers at any scale: ray hits carry no fog, and the game's reflection comes from the fogged picture (the game's own march lacks that fog leg too) | Rays on water only together with Fog in Reflections (`rt_leg_fog`, fog light learned from A8F5202D) | WARP opacity against the game, none apart by more than 2e-4 |
| Attila's lobe (alpha at most 0.10) used for WH3, whose lobe is roughness squared over 2 [35ccb165]; Attila's own lobe was 6 to 10x wider than its highlight; with GI off hits read last frame's picture | Lobe, F0 and light from that game's resolve; pass the reflection pass's light buffer | WARP against the resolve |
| A reflection pass that isn't another game's: WH3's SSR gates on water alone, the rest is dead code; FidelityFX SSSR rows had nothing to add until All Surfaces (+0.7 ms) made the branch live | Read the gate and the march in that game's bytes before carrying rows over | Asm: the flag test before the march |
| A delta over what another pass added must take back exactly that (the dead branch's 0.04 against the sun's 0.08 x colour alpha) | Run the adding pass with its input and without; compare the difference with the delta | WARP with a control that must fail |
| SSR below 100%: the Hi-Z step and the mip chains read the whole target [d70febbf, 454e0ffa]; Shogun 2 traced at half its render size (640x360 at 33%); SSSR needs a nearest-depth pyramid and hit refinement against full-resolution depth | Clamp copies at the picture's share; march one to one at or below half | `warp_tests/scale_copies`, `warp_tests/ssr` |
| The plan's water hash was wrong (A5FC3927; the real ones 4F35D09F with DFA2AEA2, 2076D289, 034010C4); water refraction read by several names | Take hashes from a trace; pass slots as parameters | Dump; trace |
| Water clarity is per material | Scale each Beer-Lambert exponent [2705350a] | WARP |
| The 8-bit environment cube caps sky reflections at paper white | Left as is: the developers' look | The cube's format in a trace |
| Water reflections flickered on a paused picture and Samples did nothing: water's guide roughness (0.01 to 0.03) is below the 0.05 mirror line, so one ray, no history, no reuse, no filter, and 12.5% of hits re-rolled cached light into a random path each frame [8f866568] | Reflections never re-roll cached light; know which reflection rows act on mirrors (Samples, Reuse and Rays a Pixel don't); a mirror's steadiness is its own row: a neighbour clamp (Steadier) or a 0.06 floor with history (Softer) [a50849fd, 29e8e4e4] | `HARNESS_REFLECT_FLICKER`: 0.0163 to 0.0084 without the re-roll, 0.00134 with Softer |
| Water's reflection history always failed: the helper copied positions at the frame's first paths request (the Light Bounce), before the addon rewrote them to the water surface, so deep water compared against last frame's sea floor [29e8e4e4] | A history test reads positions of the same surface as this frame's: copy them at the request that rewrote them | Harness: history frames > 1 on water only with the copy (0.00658 without it, 0.00134 with) |
| A game chain skipped before our replacement existed leaves the game's apply reading a buffer nothing wrote (Pharaoh's 9A55B7F2 / 0B78827B / 20 mips) [f306ff2d] | Create the replacement shader first; skip only on that frame's water_traced, and log the skip on the skip path | The add-on's "left out" line, not the frame trace (it logs bypassed commands too) |
| TW_HOST's `RewriteGuides` reads PS slots 0-2, stale at a compute dispatch: rays hooked at a CS march left from the riverbed [96486b63] | At a CS hook pass `rewrite_guides = RewriteSunGuides` (the guides from the sun draw's decode) | `water_traced` and "tracing, the water from its own surface" in the log |
| A compute hook has no render target to show MSAA [96486b63] | Read SampleDesc of the CS's multisampled view slots; with MSAA on leave the game's chain and log it once | Page dump / WARP with a 4x view |
| Hits read an R16G16B16A16F light buffer rt_core didn't copy (WH3 had its own copy) [27cc31a8] | One shared light copy per format in rt_core's `Reflections` behind the Host span; the other games' path byte-identical | WARP copy 0 texels apart + a whole-buffer control; other games' page dumps date only |
| Samples and Reuse rows act only on rough water: on the mirror they did nothing [a389c431] | Show and gate them only with Softer Water | Page dump states |

## 4. Fog

Order: does the fog march the shadow maps; fog PS by trace, a proof level each; Strength through its own buffer (value minus default), bit-exact at 1; Fog Shadows replacing the march, with the fixed costs gated; the fog leg and fog learning last.

| Mistake | Rule | Check |
|---|---|---|
| Fog Shadows alone took 90 to 50 fps: any ray row switched on RR (+6.6 ms at 100%), the terrain count wait (14 to 22 ms of the frame thread) and grass capture [bf21d2d2] | Each ray feature declares what it needs (terrain, grass, RR) and captures only for rays that run. With fog the only ray effect, `CheapFogAlone` (`twattila/rt_core.hpp`) keeps RR off (`rt_scene.hpp` `Configure`) and reads the terrain count late | Harness fog-only scene: 0.83 against 0.19 ms |
| Fog Shadows alone had no shafts: `rt_sun` ran only with RT sun shadows | Grep every reader of what you gate | Page dump of the settings' combinations |
| WH3's fog already marches its shadow maps; Attila's factor would darken twice | Replace the march (the game's gate false, rays in its place), don't multiply on top | WARP: ours at visibility 1 against the game with its own strength at 0 |
| At Fog Strength 2 thick fog is 1.8x as bright; a correct limit needs T from a side draw (34719E09) | The maintainer's decision, open as of 2026-11 | - |
| A swapped copy that needs a value | Its own small buffer holding value minus default, so unbound (0) is the game's and WARP can test it unbound | WARP unbound |
| Decompiled fog or sky: a horizon row as vertical lines through fog (an 8-bit low-res sky cube) | Fog and Sky Smoothing only with the cause proven in that game | Contrast-stretch a shot |
