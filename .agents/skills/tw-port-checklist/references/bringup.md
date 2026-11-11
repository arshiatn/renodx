# Bringing up a TW game

The order for a new or older TW game, and for any round big enough to need one. Tools named here live in the maintainer's `tools/tw` folder, outside the repo, unless a repo path is given: ask the maintainer for its path.

## Why this order

WH3, from the first build to v43: 13 versions, about 180 commits, about 10 rounds of the maintainer's feedback, 105 numbered test items, and over 100 agent game runs in one batch (60 of them GPU resets that held 109 GB of commit until a reboot). v31 shipped 13 phases built on one old trace.

Most of what the game showed could have been found offline:

| Found in game | Offline check that finds it |
|---|---|
| Black patches on the ground | Read the terrain's GS and its PS's discard before streaming it out |
| GT-VBAO flat white below 100% | Read the game's `shared.h`: settings at b10, not Attila's b13 |
| Stale banner shadows | Read the VS position chain: rows from `SV_InstanceID` |
| GI off crashed the game | Read the engine value's constructor and its graphics-change listener |
| "Cooked" light with GI off; PT AO too dark | Read the sun shader's probe lerp; count readers; test with GI on, as the game runs mid-battle |
| Fog Shadows alone cost 40 fps | Audit what each row switches on |
| Texture LOD Bias did nothing on terrain | Sampler census in traces |
| Flat picture after a preset switch | The array swap of the kept shadow cache was already in a trace |
| ReShade's banner on the FG window | `fg_bridge_test` with the game's `dxgi.dll` beside the helper |
| Bounces did nothing, far cards lit, weapon jitter | Harness and WARP tests, built only after the report |

Only look verdicts, the real crash path and real-scene cost needed the game.

Root causes, each now a rule below: features built before the data run; agent runs on agent settings (shadow quality and GI off, where the maintainer runs both on); game passes replaced without a reader census; registers assumed from Attila; a "this game won't get" list later overturned row by row (Upscaling Mode, LOD Bias, Object Motion, VFX rows, the shadow-map skip, Shadow Map Size, far trees); 19 Experimental rows shipped without the numbers the maintainer needs to decide each (on by default, in presets, or left under More Options); a run plan never checked against the maintainer's preset (a debug view that needed a row the preset had off; a test key the game doesn't have).

Shipped, then changed: path-traced AO five times, the game's SSAO handling five, its GI four, shadow-map replacement five, the DLSS preset default four. Each change came from a fact a census would have given up front.

## Phase 0: offline census

- **Shader names** from the game's packs: `tw_pack.py`, `tw_names.py` to `names.tsv` (in `bin/cmd_Decompiler-1.3.16/dump_shader_<game>_pack/`, not in the repo); `tw_shaders.py`, `tw_missing.py`, `tw_family.py`; `vs_names.py`. Check every hash you plan on against `names.tsv` (a fog-looking hash at `gfx_fog 0` was fog of war).
- **Engine values**: `tw_tweak_survey.py`, `tw_engine_values.py`. Take names from the value constructors' call sites, never from strings.
- **Game-facts sheet**: answer every row of [game-facts.md](game-facts.md) you can from the dump and the exe, with a proof level; the rest wait for the traces.
- **Parity table** against Attila's page dump: each row gets it, or doesn't with proof.
- **Start from** the closest TW addon by bitness, sun-shader patch route and capture model (clip-space or world-space), not from `src/games/generic`.
- **A first build** from that addon with only Frame Trace, Log Details and Record Profile, every feature off: the data run needs it.
- **An unmodded `ReShade.log`** (from the maintainer, or from the data run's first start): device flags (single-threaded?), flip or blt chain, probe devices, fullscreen, bitness.

## Phase 1: the data run (ask first)

- Port the run and check scripts first (`run_game.ps1`, `check_run.py`), so the run reports itself.
- Frame Traces in a settled battle (mid-battle: the first frame has other flags), the siege benchmark with fire, a dark or night map, the campaign. At 100%, and at 67, 50 and 33% through the game's own scale setting; where the addon owns the scale, the low-scale traces come after Phase 3 step 5.
- Log Details from the start, Record Profile for 30 s with ReShade's menu closed.
- On the maintainer's ini and preset, and again on defaults. Fresh backups right before.
- With each game setting you will touch (AO, GI, SSR, shadow quality, fog) on and off.

## Phase 2: offline from the traces

- **Pass map**: order, targets, formats, viewports, per scene and scale.
- **Reader census** by resource handle for everything you will touch: AO, the GI flag and probes, shadow arrays and any kept cache, SSR inputs, fog. The dump gives candidates only.
- **Capture candidates** by VS family and PS: the discard's texture, instance source, GS or tessellation, indirect draws; main pass and shadow pass.
- **Diffs**: pass lists and index counts between scales and settings; flag values counted across all traces.
- **Constants**: `cb_audit.py`; a buffer identified by size only if no other buffer has that size.
- **Game facts again**: answer the rows of [game-facts.md](game-facts.md) that waited, and re-check the others against the traces.

## Phase 3: features in dependency order

| Step | Needs | Exit check offline |
|---|---|---|
| 1. Output, page skeleton, presets | Facts | `page_dump`; HDR ramps; key diff |
| 2. Stability wiring: `device_loss`, `output_encoding`, lifetime pairs | - | `test/tw_device_loss`, `test/tw_helper_lifetime`; the first-build list in [helper-fg-stability.md](helper-fg-stability.md) |
| 3. AA at 100%: DLAA, DLSS, jitter, velocity census | 2 | Harness sign pairs; motion census table |
| 4. FG and Reflex: injection slot from `shared.h` | 3 | `fg_bridge_test` per backend |
| 5. Render scale, as one audit | 3 | The scale checklist at four scales in [render-scale-dlss.md](render-scale-dlss.md) |
| 6. A complete RT scene | Census | All 16 steps of [rt-scene.md](rt-scene.md); stress modes per family |
| 7. Replacing game passes: shadow maps, AO, GI, fog march | 6, reader census | WARP "0 values differ" plus a control that must differ; the double-work table |
| 8. RR guides, AO under RR | 6, 7 | `HARNESS_RR`, `rr_specular` |
| 9. Particles, VFX, SSS, reflections, water, fog, look rows | 5 | The per-area order in [sss-particles-water-fog.md](sss-particles-water-fog.md) |
| 10. Presets, Graphics 2 of 2 | All | Every button in a page dump; Reset gives the game's own effects back |

A step starts only when the steps it needs are green. A step that can't go green stops by the stop rule, row hidden.

## Phase 4: the settings matrix offline

GI on and off, paths on and off, RR on and off, 100% and 50%, FG on and off, a preset switch and Reset mid-run. For each row: its fixed cost alone, what it switches on, the page dump in every state.

## Phase 5: one bundled run (ask first)

- The plan dry-run against a page dump of the maintainer's current preset: each planned shot's rows exist and show, each debug view is made, each test key exists in this game.
- Every log line the plan relies on exists in the build.
- Controls first; on and off inside one run; for a GPU reset at least 6 runs per state, the control first.

## Phase 6: the maintainer's list

10 lines or fewer, in the format of [testing.md](testing.md).
