# Replacing game passes: AO, GI, double work

What "save double work" means: when ours replaces a game pass, the game's pass stops (or keeps only what still has a reader), and every term it fed takes exactly one occlusion or light. In WH3 the game's SSAO handling changed five times and its GI four, each time from a fact a reader census would have given first.

## 1. Rules for replacing or skipping any game pass

Shadow maps, SSAO, GI, the fog's shadow march, SSR: the same rules.

1. **Readers by resource handle, in traces**, for every method and at 100% and 50%. The dump gives candidates only. Code-read audits were wrong both ways: WH3's water and fog were said to read the SSAO, the reflections to lose it; by handle only the sun draws read it, and the reflection resolve 89AF6FF5 binds a 1x1 white texture there [d6d9f92b].
2. **Every term the output multiplies**, per reader. WH3's `t_ssao` also occludes the cube's reflection; Attila's AO darkened the RT reflections a second time [43dc26e6]. Each term gets exactly one occlusion.
3. **Per-frame or kept across frames.** A per-frame output may be skipped. Anything kept (caches, kept arrays, probes, voxels, histories) is never skipped, or is refilled on the way back. Every skip comes back the next frame.
4. **Split what the pass carries.** A game's GI may carry the sky's light and shade as well as the bounce: remove only the bounce (section 2, first row).
5. **A render stage's engine value**: see [game-facts.md](game-facts.md)'s traps.
6. **Exactly one method**, enforced at load and at present, not only in the slider: a saved ini once ran RTAO and GT-VBAO together, the slider showing one while both multiplied (3.3% brighter alone than over the game's) [6b805678]. Check with a page dump of the saved-ini case.
7. **Attach to a pass that always runs** (the sun draw). WH3's AO downsample stops below about 2/3 scale; Pharaoh has no AO draw to wait for; Attila's GT-VBAO needs the game's SSAO on [f98564bb, 49cb4d47].
8. **Calibrate against the branch the shader takes in steady state**, from mid-battle traces, as the game uploaded the values.
9. **Prove it**: WARP on the game's bytes with ours in the slot, "0 values differ" where it must be the game's, plus a control that must differ. Then the pass's time saved, measured.

## 2. Lessons

| Mistake | Rule | Check |
|---|---|---|
| Turning off WH3's GI also removed its sky shade: shade 2 to 2.8x brighter, dark tones bluer ("cooked"). In 9EAD8951 the probes *replace* the cube: `(vis, A) = lerp((1, cube x fudge), probe, fade)`; a probe ray that misses brings the sky, one that hits the bounce (x `g_irradiance_power`, CS DC860C60). With the flag at 0 all 1019 readers took the cube unshaded [2a717fd0] | Keep the game's flag; zero only the bounce: a constants copy for DC860C60 with `g_irradiance_power` (cb2 float at 208) at 0 | Compare flag 1 against 0 *with probe data present*, averaged over shaded texels (`test/tw_game_gi`); game: GI off and on, shade near 1x |
| PT AO calibrated against the spherical-harmonics branch while the GI is on mid-battle (60 of 61 traces): our sky came out 1/12 to 1/58 of the shade. Log Details said GI 0: it read the battle's first frame [be734990] | Calibrate against the steady-state branch; never trust a one-shot first-frame log | `rt_sun_test` (probe sky against a CPU model, within 1.2%); the helper's learned-light share line near 1 |
| Attila's PT AO "cooked" for five reasons: the game's AO still multiplied under it [201dd567]; ReSTIR gave escaped rays no weight [0254ad1b]; zeroing the whole cube lost foliage's sky sheen and the x1.44 scale [4db7aa68]; the ambient uses `normalize(n x (1, 4, 1))` [35048c6f]; occlusion range 50 m instead of RTAO's 6 m [b0bd2639] | PT AO replaces exactly the game's diffuse ambient term, with the game's scale, formula and foliage share, and RTAO's range; sky samples survive every resampler | `make_sun_patch_test.py`, `warp_tests/sun_wh3` (t51 = 1 leaves 0; game minus patched equals the formula); game: PT AO against RTAO on an open field |
| Learned light froze early: 30% darker than the same view a minute later; Bounces did nothing because a cache cell out of sight kept its first paths for 1800 frames [0d8b36ac, 85bfb847] | Learn after the battle settles (150 frames); every cache keeps relearning (an eighth of paths); log what was learned at each start | A setting changed halfway must end equal to that setting alone; harness `bounces_*`, `paths_kept_cube` |
| The kept shadow cache's copy skipped with the maps: flat picture after RT or PT to Reset mid-battle [dfb2334e] | Classify outputs before skipping (rule 3) | Game: a preset switch mid-battle; far shadows and AO right at once |
| GT-VBAO depth from a downsample that stops below 2/3 scale; settings read at b10 while we bound b13; display texcoords taken as screen positions; GT-VBGI's history camera overwritten by the battle portrait | Attach to a pass that runs at every scale and setting; settings at the game's register; `MainViewport` for histories | Start the game at 50%; `warp_tests/ao/render_scale_test.cpp` |
| AO denoiser default under RR flipped three times: "RR only" left about 20% of the noise; REBLUR cost 0.85 ms and 215 MB and darkened a wall's foot (0.74 to 0.57) | RR has no AO input: AO keeps its own history under RR. Light History (raw rays plus 20% history) is the default, REBLUR an option [f481d567]; ReSTIR shows one sample whatever the ray count, so Own Sky Rays [e392d43b]. Where NRD didn't run, our own filters stay on | `HARNESS_RR` numbers: no history, settled, mean |
| SHaRC hung the GPU at propagation depth 4 with cache resampling; its reflections variant hung at 4K; a `-Od` build failed PSO creation and still counted as a pass | Bisect cache variants with an env var inside one helper build; require a "ready" line naming the variant | Harness |
| PT GI lit a hidden step face in shade with its sunlit neighbours' light, and the brightness followed the view: the paths' world radiance cache was keyed by position alone (0.5 to 2 m cells at TW distances), and a screen lookup took any pixel within 2% of the depth [50c8661c, 9b9d5418] | Key a radiance cache by cell and facing (6-way); no cached answer for a ray shorter than its cell; a screen lookup checks the facing too; last frame's tone-mapped picture is never light where a light buffer exists | Harness `HARNESS_CACHE_LEAK` (hidden view 0.172 to 0.020 against 0.033 seen), `HARNESS_PATH_ROOM` (0.442 to 0.062) |

## 3. Procedure for a new game's AO and GI

1. **Inventory**: AO, GI, sun and ambient families from `names.tsv`; traces at 100% and 50% with the game's AO and GI each on and off, in a settled battle; diff the pass lists.
2. **Readers** by handle per output, in every trace; the terms each multiplies; note 1x1 stand-ins.
3. **The ambient formula** per sun shader from its disassembly: SH, cube and probe branches and their lerp, fudge and scale, normal stretch, metal rule, foliage sheen, AO divisor. Count flag values across traces. WARP on the game's bytes.
4. **Constants**: the settings register from `shared.h`; buffers by size only if unique; overscan factors; `cb_audit`; WARP with the slot unbound equals the game's.
5. **Skip the game's AO**: only per-frame passes whose every reader is ours; clear to open until ours is ready; one method enforced at present; the Screen-Space AO Debug View; Time Each Pass.
6. **Replace the GI**: zero only the bounce through a constants copy at its dispatch; keep the sky passes; the engine's GI value left alone (rule 5); in game GI off and on twice in one battle.
7. **Sky and PT AO**: calibrate against the branch in use, with the game's scale and formula and RTAO's range; sky in every resampler; learn after the battle settles; caches relearn.
8. **Denoise**: Light History under RR, REBLUR optional, NRD fallbacks; Rays a Pixel must lower the noise.
9. **Presets and switches**: a page dump of every preset button; every skip back the next frame; nothing kept skipped; bypass state restored in `finalize_callback`.
10. **Close with the double-work table** (section 4).

## 4. The double-work table

At each round's end, one line per game pass that our features touch:

| Pass | Cost | Readers left | Still needed? | Gone under |
|---|---|---|---|---|
| e.g. WH3 SSAO | 0.51 ms at 4K | Sun draws only | No | RTAO, GT-VBAO, PT AO (0.09 ms left) |
| e.g. WH3 GI probes, six passes | 0.22 ms | 1019 sun-shader readers | Yes: they carry the sky's shade | Bounce zeroed under our GI |
| e.g. dynamic shadow cascades | per game | Water, lit effects, fog | Under Multiply | Replace, kept cache stays |

Known costs (RTX 5090, 4K): NRD memory SIGMA 360 MB, AO 220 MB, Light Bounce 125 MB, reflections 580 MB; Attila PT Light Bounce in the harness Full 4.25 against Half 1.41 ms (5.18 ms in game); WH3 at Performance: bounce 1.80 ms, sun and AO rays 1.18 ms.
