# Screen-space shadows, particles, reflections, water, fog

## 1. Screen-space shadows (Bend)

**What it does**: Bend's wavefront march over a copy of the scene depth, at the first main-view sun draw: `sss_depth`, `sss_setup` (the dispatch list built on the GPU), 8 indirect `sss_bend` dispatches, optional `sss_blur`. The patched sun shaders multiply their shadow term by 1 - t49, so only direct sunlight changes. Smoke (`smoke_map`, Attila only) and the RT occlusion use the same t49.

**What it includes** (since WH3 v42 and Attila v146, all Experimental, off): Auto Thickness and Softer Away from the Caster (all 7 SSS addons); Light Through Banners, Coloured Shadows (t50, only where the rays bind none) and Torch and Fire Shadows (Attila, Thrones, WH3); Light Through Foliage (Attila only).

**Cost** (4K, 100%): base 0.81 ms; Banners +0.08, Coloured +0.60, Softer +0.53, Auto +0.73 ms; torches 0.35 to 1.4 ms; smoke map 0.1 to 0.16 ms.

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

## 2. Particles and transparency

Order: trace the composite, its layers and readers; count only particles with opacity; VFX rows on `o0.xyz` only; half size (blends, readers, depth); history tests; RR inputs per preset; `smoke_map`, then RT smoke.

| Mistake | Rule | Check |
|---|---|---|
| Particle Trails flagged blocks while paused: WH3's MBOIT moments are half size and point-copied (A759AD5D), so jitter flips 2-px texels at edges [716ae95f] | History rejection on a half-size or point-copied buffer tests the range of the neighbours around last frame's place | `warp_tests/smear_wh3` (edge flags 216 to 0) |
| Smooth Half-Size Particles' first design threw flipping sparks away; an HLSL array made a 32-byte struct's cbuffer 68 bytes [f51b4d05] | Write the flip, move, pan and cut cases before the shader; the reflected cbuffer size equals `sizeof` | `test/tw_particle_history` |
| A redirected draw unbound the OM UAVs (`OMGetRenderTargets` doesn't report them); WH3's composite carried a writable DSV on the depth being read [ade0799d] | `OMGetRenderTargetsAndUnorderedAccessViews`, restore UAVs with their counts; refuse other blends, scissor or MSAA with one log line; compare the DSV's resource with every SRV read | Read |
| RR's particle inputs: skipping the denoiser loses DOF's blur; Picture Before Particles makes the rest 2.5% brighter; the particle mask was read by no preset. Below 100% RR's colour comes after the tone mapper, so the composite is redrawn unblended into a cleared texture [ef9be189] | Test each optional NVIDIA input per preset in the harness, and know its colour space at each scale. Particles at output resolution are impossible: they would lose exposure, bloom, DOF and grading, and there is no display-size depth | `HARNESS_RR_EXTRA` |
| Smoke: Bend never sees particles; RGBA16F height moments failed | `smoke_map` splats last frame's lit particles into a 1024 squared RGBA32F map from the sun | `smoke_map_test` |

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
| Water Motion couldn't be proven | Stays Experimental under the stop rule | - |
| The 8-bit environment cube caps sky reflections at paper white | Left as is: the developers' look | - |

## 4. Fog

Order: does the fog march the shadow maps; fog PS by trace, a proof level each; Strength through its own buffer (value minus default), bit-exact at 1; Fog Shadows replacing the march, with the fixed costs gated; the fog leg and fog learning last.

| Mistake | Rule | Check |
|---|---|---|
| Fog Shadows alone took 90 to 50 fps: any ray row switched on RR (+6.6 ms at 100%), the terrain count wait (14 to 22 ms of the frame thread) and grass capture [bf21d2d2] | Each ray feature declares what it needs | Harness fog-only scene: 0.83 against 0.19 ms |
| Fog Shadows alone had no shafts: `rt_sun` ran only with RT sun shadows | Grep every reader of what you gate | Page dump of the settings' combinations |
| WH3's fog already marches its shadow maps; Attila's factor would darken twice | Replace the march (the game's gate false, rays in its place), don't multiply on top | WARP: ours at visibility 1 against the game with its own strength at 0 |
| At Fog Strength 2 thick fog is 1.8x as bright; a correct limit needs T from a side draw (34719E09) | Left for the maintainer's decision | - |
| A swapped copy that needs a value | Its own small buffer holding value minus default, so unbound (0) is the game's and WARP can test it unbound | WARP unbound |
| Decompiled fog or sky: a horizon row as vertical lines through fog (an 8-bit low-res sky cube) | Fog and Sky Smoothing only with the cause proven in that game | Contrast-stretch a shot |
