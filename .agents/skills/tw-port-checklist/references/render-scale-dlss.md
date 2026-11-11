# Render scale, upscalers, jitter, motion vectors

## 1. Scale checklist

Run it at 100, 67, 50 and 33%, with a cold start at the low scale and after a live change. A proof at 100% says nothing below it.

- **Sizes**: whole aspect units; the game's rounding; screen against viewport registers; overscan; a check frame per new size; forget a size when its source goes.
- **Viewports and scissors**: scaled at 0,0 for display-size and power-of-two targets; scissors back after the tone mapper; `Evaluate` takes the viewport, not the texture.
- **uv to NDC**: in the game's shaders and in our replacements (patch order).
- **Level chains**: mips, Hi-Z, depth levels, bloom, exposure, AO downsample; half-size targets read whole.
- **Overscan readers**: AO, heat haze, secondary LUT, DOF, distortion, fog of war.
- **Pixel tolerances**: Bend over the share, SSS softness, reflection blur, the stored hit distance, the flare occlusion count, trail texels.
- **The game's own scale terms**: sharpening, the TAA's weight, LOD and culling at load, its own DLSS.
- **Samplers**: by kind; main view only.
- **Our effects**: AO resolution x share, Light Bounce one step finer per halving, reflection rays up a step, a 1:1 march at or below half, guides and Depth Levels over the rendered corner, debug views stretched.
- **After DLSS**: markers need a display-size depth; particles; UI.
- **Jitter**: phase count, amplitude and sign per method and scale; FG's camera without jitter.
- **States**: menus, upscaler not running, registers set by a replaced pass.
- **Tools**: traces at 100% and 50% with pass and index diffs; `scale_audit.py` (it lists dispatches but passes nothing: read each CS's mapping); `p9_ndc_trace.py`; WARP with the picture in the top-left of a 2x texture against 100%.

## 2. Motion-vector checklist

- **Census**: class every G-buffer VS by what its velocity output holds (WH3: a table by VS hash). Fix by token copy (`dxbc::AddPreviousPosition`: the same bits by construction); swap a PS only with a listed VS and an R16G16 target.
- **Camera**: per frame, without jitter, snapshot at the evaluation.
- **Skinned units**: repeated bone packs within a frame (section 3), keys per frame, the campaign's placement scale.
- **Rigid placed rows**: `SV_InstanceID`, repacked streams, radius x placement scale.
- **Wind**: only time is last frame's.
- **Usual gaps**: deformed cloth, sails, ropes; banners (rows only, CPU-moved buffers); water (one history texture per slot); glowing and dissolving weapons (PS without the velocity output); particles, heat haze. WH3's still missing: skinned units C0ED2883 and 1405BFB4 (28 draws a frame), banners 444F1190, bones-plus-sway shaders, sails and ropes, water (Experimental), wind gusts, particles.
- **Proofs**: paused, row on against off; WARP stream-output bits; the Motion Debug View per class; cost inside one run.

## 3. Lessons

| Mistake | Rule | Check |
|---|---|---|
| Reflections faded from the right and bottom at Performance and Ultra Performance (39% of the picture at 33%): WH3's mip and blur CS (79F74068, 66C301FF, FB547085) and Hi-Z step CB0A1AF6 clamp at the texture's size; the resolve picked levels by display width [454e0ffa, d70febbf] | Every pass that builds levels or downsamples stops at the picture's share at each level and picks levels by render width | `warp_tests/ssr_mips`, `scale_copies`: share 1 bit-equal, 0 cells read from outside at 2/3, 1/2, 1/3 |
| Positions built from the texture's uv: torch light slid bottom-right; grass darker outside the top-left ninth; GI bounce unsteady; water colour wrong at 50% [1c821965, 104eee9f, 98bc6456, 20336fc6] | Follow every uv back to its roots (`p9_ndc_trace.py`; an 8-line window missed about 30 shaders). NDC used only for depth is harmless | WARP with the picture top-left in a 2x texture against 100% (water: 6868 to 21822 values off, then 0 to 244) |
| WH3 over-sharpened at 50% (edge energy 2.1x of 100%): its sharpening adds `clamp((1 - s) x 1.5)` for its own upscale [b2020682]. The game's own DLSS wrote 0.58 into its scale [57b255ed] | List every read of the scale and of the viewport constants in the exe and the post shaders; decide each | Edge energy ratio 50% against 100% at most 1.15 |
| LOD and culling count render pixels, and models read them at load: 28 M indices at 50%, 50 M at 100% | `.inverse` pixel tweaks x 1 / share squared (13 options) from a kept share or the prefs file, before any TAA pass; Full Detail Below 100% (+1.7 ms rays off, +6.1 ms RT or PT) [67529bfe] | Index counts by VS from two traces; a slider's factor against share squared proves the law |
| Texture LOD bias greyed out (its rule read `dlss_mode`, 0 in a game whose scale is its own float) [842816f0]; terrain and buildings stayed soft: only anisotropic samplers were cloned, they sample through named trilinear ones [04ec8cd7] | Log samplers by kind and bias at the low scale; clone wrapping trilinear samplers too, never clamping or post ones; main view only. A sampler bias adds to the game's (0, -0.5, -2, leaves -0.87) | `test/tw_texture_lod_bias`; page dump in every state |
| Aspect drifted, still pixels got motion toward the edges: each axis rounded alone. WH3 rounds up and truncates floats | Sizes in whole 16x9 units; write scales that give whole sizes in float32 (`AspectScale`); at 1440p Ultra Performance is 0.3375. Learn the scale from the TAA's constants, checked once per size against the overscan, else refuse it | `b4_aspect` (8 display sizes); the scale-choice test over the real header (91 checks) |
| Jitter: Attila injects Halton(2,3) over max(8, 8 / s squared) phases, at least 32 under RR [c5e069fa]. WH3 keeps the game's: 16 phases, 0.43 px, short of 32 at 50% (left) | Check phases, amplitude and sign per method and scale. FG needs the camera without jitter; take near and far from the constants, not the projection (11984 for 12000); `Evaluate` needs a camera snapshot taken at the TAA | Harness sign pairs at that scale |
| Shields, swords and spears jittered under DLSS in a paused battle once the camera had been around: the game packs bones, draws, and packs again into the same pages; our records read at frame end gave one round's bones to another round's soldiers; a 64-entry clear wrapped [2501d12d] | A motion source the game can rewrite within a frame is read before the rewrite or marked stale; keys last one frame; identities never wrap | `test/tw_unit_pack_rounds` (game VS 5F9CD405: old code 6.398 px wrong, fix 0.000) |
| Campaign: rigid pieces wrote about 95 px of motion with a still camera: placement cells of 1 m overflowed in a world 1/20 to 1/100 of a battle's [e209bf5d] | Scale every world-sized constant on the campaign (`PlacementScale`); Rome II, Thrones and the 64-bit games' copies of `motion.hlsli` don't have it yet | `attila_motion/placement_test`; game: a campaign city, still camera, Motion Debug View grey |
| Motion "for everything" claimed | A census of velocity writers, not a claim (section 2) | `b5_census.py`, `b5_motion_test` |
| Presets L and M darken fine contrast below 100% at any feed: they are the models', not the square root's (L 0.911 linear, 0.930 sqrt; M 0.922, 0.904; K 0.998). The default flipped four times | Offer K, M, L with their numbers in the tooltip; no hidden rule; under RR the upscaler's preset row does nothing | `HARNESS_UPSCALER_TONES` at 100, 67, 50, 33% |
| L, M and RR flatten darks on linear HDR input (always auto-exposed) | Where the input is linear, feed sqrt and square back (`aa_dlaa_prepare.cs`); test with the input's real range (`HARNESS_RR_IMAGE`, `HARNESS_SR=12`), a graded shot is too bright | Harness |
| RR inputs taken from Attila's rules | Guides from that game's sun shader (WH3: roughness `rt2.y`, F0 = lerp(0.08 x `rt1.a`, `rt1.rgb`, `rt2.x`), normals from all 10 bits [f884b6bc]); specular albedo with Fresnel (EnvBRDFApprox2) [442fc15b]; hit distance 65504 for sky; ReSTIR reuse independent under RR [c5e069fa]. Only preset E reads the DOF guide; no preset read the particle mask [e60a7983]. Below 100% RR's colour is the tone mapper's picture, so copies taken at the particle composite are in another space | `rr_specular`, `rr_scaled_wh3` (72 of 72), `HARNESS_RR_EXTRA` |
| RR costs 8.3 ms at 4K against DLSS's 1.74; a cheap ray row switched it on | RR only for rows that need it; say its cost in the tooltip | `fg_bridge_test` timings |
| Jitter or motion sign or scale wrong: looks sharp when still | Prove by pairs, right against wrong (FSR 0.081 against 0.18 to 0.24, XeSS 0.097, DLSS K 0.106) | Harness jitter sign pairs |
| A gate for one feature took another's: TAA and T2x lost jitter for twelve versions [5693b7cb, dfb7bbfd] | After any change around jitter, list every AA method's jitter | Read `anti_aliasing.hpp` per method |
| Render scale active without DLSS running: picture stuck top-left when the helper is missing | Gate the scale on the upscaler really running; a game's own scale slider has no such gate | Read the gate |
| A game that owns its scale, driven by the addon: a new size cost a frame of the game's TAA, and the game's exit saves what the addon wrote | Write only at present, whole in float32; publish the size at the write; a size proven once needs no check frame. The game copies the float early and writes its file about a second after `destroy_device`: keep the found value outside the game's file and restore through that copy | Log one line per change; quit at a mode, read the file's write time against the log |
| A pass the game skips at low scale: WH3's AO downsample 49F538CF stops below about 2/3 | Keep what a pass reads from a pass that always runs; diff pass lists at 100% and 50% | Traces |
| A texture with its own overscan: WH3's AO read at uv x overscan ratio (1.5 at 50%) | Replacing it at display size changes the read too (`ssao_constants_slot`); start the game below 100% | `warp_tests/ao_filter/run_ao_ratio.ps1` |
| A game's own scale may sit top-left in a display-size texture | `Evaluate` and motion scale take the viewport's size; a viewport == screen filter must go | A trace at 50% |
| Below 100%, by name: scissor left scaled (flare, markers), flare occlusion, grass depth in split uv form (`Kind::SPLIT`), AO blur and water refraction read whole, refraction under three names, SSS tolerances, SSS far fade on the backdrop, SSS debug view through DLSS, bloom share, marker depth stretched; WH3 GI voxels' relevance 1810C89D and shadow blur DDB14D2C reading depth as if the picture filled the texture [98bc6456] | Run the same list per game | `scale_audit patch_test`; a trace at 50% |
| Scaling a pass by a share: WH3's stored hit distance is render pixels over display width, so a one-to-one march halved it, or the resolve's cone doubled | Before scaling a pass, write down what each stored value means in pixels; scale from the bound target's real size, not a share | WARP at 50% against 100% |
| A learned render size outlived what proved it: `MainViewport` false, Bend, GI, AO filter stop | Tie the size to its source: no upscaler or no such pass for a second, forget it | State-machine test over the real header |
| Upscaling after the tone mapper: the game's values at the lower scale may never have been seen | First frame at a size checks them, the upscaler from the next; a failed check gives the game's own chain back until the size changes | State-machine replay; name the assumed values |
| A resolve kept over the upscaler's output reads other textures at display pixels: their data is only in the render-size corner | Skip it on those frames and copy the picture, or feed it a scaled copy | WARP |
| The TAA slot runs before the tone mapper's check | Decide per size before the slot writes; at most one raw frame on a late failure | Read the order |
| Post-TAA 3D effects draw after DLSS's frame | They stay outside DLSS: own resolution and depth. With the upscaler after the tone mapper they are in its input: particles carry the jitter | Read the trace |
| Motion cost: SO left bound through the game's draws stalled about 10 us per unit draw [3ac1b1ad]; a second prelude [840c5433]; buffer copies between draws 1.24 ms | Targets kept, SO only, no PS, compute at frame end, async requests. Don't port object motion where the game's velocity is real | Toggle within one run |
| A PS swapped by hash also runs with static VS (velocity 0) and in passes with colour in that target | Swap only with a VS known to have a previous position, and the target's format checked per draw | Paused, row on against off: no cell gains colour |
| In-game TAA must stay on when DLSS hooks its pass; other AA modes never run that CS | Tooltip and a status line when not evaluated | Read |
| `reset` only on a skipped frame | Attila's checks for cuts | Read |
| No sticky "camera found" | Require a camera seen this frame | Read |
| `main_view`, `frame_history::render_width` and `tonemapped` unset by a host below 100%: `MainViewport` always true, UI 3D views enter the scene | Set them in every host that scales | Read |
| `Release(true)` forgotten on leaving DLSS: the bridge's textures stay | Call it | Helper "release" lines |
| The camera in a shared constant buffer: `Upload` reads mapped memory at unmap, a mutex per map | Keep hook costs measured | Timing in one run, then remove |
