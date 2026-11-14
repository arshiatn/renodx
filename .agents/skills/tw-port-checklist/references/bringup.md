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
| GI off crashed the game | Read the engine value's constructor and its graphics-change listener ([game-facts.md](game-facts.md)) |
| "Cooked" light with GI off; PT AO too dark | Read the sun shader's probe lerp; count readers; test with GI on, as the game runs mid-battle |
| Fog Shadows alone cost 40 fps | Audit what each row switches on ([sss-particles-water-fog.md](sss-particles-water-fog.md)) |
| Texture LOD Bias did nothing on terrain | Sampler census in traces |
| Flat picture after a preset switch | The array swap of the kept shadow cache was already in a trace |
| ReShade's banner on the FG window | `fg_bridge_test` with the game's `dxgi.dll` beside the helper |
| Bounces did nothing, far cards lit, weapon jitter | Harness and WARP tests, built only after the report |

Only look verdicts, the real crash path and real-scene cost needed the game.

Root causes, each now a rule below: features built before the data run; agent runs on agent settings (shadow quality and GI off, where the maintainer runs both on); game passes replaced without a reader census; registers assumed from Attila; a "this game won't get" list later overturned row by row (Upscaling Mode, LOD Bias, Object Motion, VFX rows, the shadow-map skip, Shadow Map Size, far trees); 19 Experimental rows shipped without the numbers the maintainer decides each by; a run plan never checked against the maintainer's preset (a debug view that needed a row the preset had off; a test key the game doesn't have).

Shipped, then changed: path-traced AO five times, the game's SSAO handling five, its GI four, shadow-map replacement five, the DLSS preset default four. Each change came from a fact a census would have given up front.

## Phase 0: offline census

Everything here runs on the game's shader folder, the exe and any trace that already exists: no game run, no cost in game. Do it before asking for the data run.

- **Shader names** from the game's packs: `tw_pack.py`, `tw_names.py` to `names.tsv` (in `bin/cmd_Decompiler-1.3.16/dump_shader_<game>_pack/`, not in the repo); `tw_shaders.py`, `tw_missing.py`, `tw_family.py`; `vs_names.py`. Check every hash you plan on against `names.tsv` (a fog-looking hash at `gfx_fog 0` was fog of war).
- **Engine values**: `tw_tweak_survey.py`, `tw_engine_values.py`. Take names from the value constructors' call sites, never from strings.
- **Port scripts** (the maintainer's `tools/tw`): `port/port_tw_game.py` (pages, SSS, AA, tests, wrappers); `rt_port.py` and `rt_scene_map.py` (Attila's scene shaders mapped by `names.tsv`: level B at best); `tw_game_tests.py` (Graphics 2 of 2); `cbuffer_slots.py` (free slots); `scale_families.py`; `tooltip_sync.py`; `find_emu.py` (`engine_tweaks::Find` emulated offline). The page dump has stubs only for Attila and WH3 (`dump_a.cpp`, `dump_w.cpp`); older census scripts are batch-local (`wh3_batch3/trace_facts.py`, `wh3_batch5/v37_*_readers.py`): a stub and game-neutral copies are Phase 0 work.
- **Scale census** (whole dump, no traces needed; rerun after every view_share or render-scale change): build the replay with [scale_census_build.ps1](../scripts/scale_census_build.ps1) `-Out <scratch>` (the repo's current `view_share.hpp` with a hook), then [scale_census.py](../scripts/scale_census.py) `--cso --names --addon --shaders --replay --cache <scratch>` and `--traces` once there are any. Every PS and CS that reads screen space or builds positions from it, against what the addon does (view_share's replayed sites, `RENDER_SCALE_READS`, `FULL_DEPTH_SHADERS`, replacements, `SetViewportChains`): BUG (A) over-patched, BUG (B) not handled, unclear, OK. Limits: static (last writer in program order, branches ignored); full-screen passes guessed from the input signature and names; which textures are render-size and which shaders run come only from the traces given (dead shaders look live); replacement HLSL is checked only for using the share; CS grids aren't checked. About a minute for 2900 shaders.
- **Caster census, whole dump** (no traces needed; rerun after every `SCENE_SHADERS` change): [caster_census.py](../scripts/caster_census.py) `--whole-dump --dump --cache <scratch> --addon` (`--trace NAME=<trace or folder>` fills an "in a trace" column). Every VS in the dump gets a family (skinned, rigid, trees, leaves, grass, far cards, sprites, cloth, terrain, or a non-caster: UI, post, light, sky, water, decal, effects, debug, stub) and a pass (main, shadow, depth, point-light, GS/HS), and a verdict against `SCENE_SHADERS`, the smoke tables and a `scene_draw` host (the addon and its own includes): held, in view only (a shadow-pass VS of a family held in the main pass), NOT HELD (ranked by family; in no trace = level B, one trace or a WARP check of its capture before it ships), not a caster, dead (overridden by a later pack). "addon names it" marks a hash the addon mentions elsewhere: read why before calling it a gap. Limits: families from source file names by regex, refined by bytes (bones, height map); passes from the entry point, from the bytes where there is none (WH3's hashed names, marked `(bytes)`); "in view only" by source file, not by mesh; which draws are G-buffer draws, PS pairs, dynamic buffers and permutations a setting selects come only from traces; unused shaders look live. Seconds a game, traces included.
- **Caster census, traces** (any older trace first, level A* for what it drew; again after the data run and for each new scene or mode): the same script without `--whole-dump`, `--trace NAME=<tw_frame_trace.txt or folder>`, lists every VS a trace drew into the G-buffer or the sun's atlas (`--atlas`: the map's width in those traces, 4096 by default; Attila's traces draw 2048) against `SCENE_SHADERS`: held (with G-buffer PS outside its `pixel_masks`), MISSING, atlas only (depth-only VS can't be kept out of view without unprojecting). Limits: only what the traces drew; draw-time facts (dynamic buffers, instance data, VS and PS pairs, a prefill sharing the VS) come from the traces; alpha tests need `ps_discards` on the listed PS. Campaign and battle traces both.
- **An addon that already exists**: several TW addons besides Attila and WH3 ship script-ported rows (RT, DLSS, FG, SSS) never run in game (their READMEs say "built only"). Page dump and the install list first, every table entry classed A, B or C, shipped keys kept; fix what's there before adding.
- **Game-facts sheet**: answer every row of [game-facts.md](game-facts.md) you can from the dump and the exe, with a proof level; the rest wait for the traces.
- **Parity table** against Attila's page dump: each row gets it, or doesn't with proof.
- **Start from** the closest TW addon by bitness, sun-shader patch route and capture model (clip-space or world-space), not from `src/games/generic`.
- **A first build** from that addon with only Frame Trace, Log Details and Record Ray Tracing Profile (30 s), every feature off: the data run needs it. It also takes `twattila::test_hooks::Install` (`RENODX_TW_SET`, `SHOT`, `PROFILE`) and the `frame_trace` Options the run uses (`on_demand`, `at_100`, `value_hashes` for the sun, tone mapper and water constants); dry-run every `-Set` key and value against its page dump.
- **An unmodded `ReShade.log`** (from the maintainer, or from the data run's first start): device flags (single-threaded?), flip or blt chain, probe devices, fullscreen, bitness.

## Phase 1: the data run

The maintainer allows one agent-driven data run at the start of each new TW game: start, settle, trace, save the logs, quit. It collects data and tests nothing; feature tests still need the maintainer's OK per batch. Give the agent that drives it medium effort: the steps are scripted, the reading happens offline afterwards.

- Port the run and check scripts first (`run_game.ps1`, `check_run.py`), so the run reports itself.
- **Taking a trace**: `[renodx] FrameTrace=1` in `ReShade.ini`, read at start: one frame 600 presents after the render scale began, and only below 100% (`anti_aliasing.hpp` passes `render_width < display_width`); it writes `tw_frame_trace.txt` next to the exe, over the last one. Shared `frame_trace` Options: `on_demand` (the next frame when `renodx_trace_now` appears next to the exe) and `at_100`, both only in Pharaoh, Rome II and WH3 (its own in `twwarhammer3/taa_dlss.hpp`) as of 2026-11; Attila has neither, so its FrameTrace gives one frame a start and none at 100%. Without them plan one trace a start: about ten, not the whole matrix. Buffer contents (particles, light instances, quad vertices, structured rows) come from Options `draw_reads` with `[renodx] FrameTraceReads=1` (read at start; `pharaoh_data_run.ps1 -Ini` sets it): the game's layouts stay in its adapter (`twpharaohdynasties/trace_reads.hpp`), off nothing hooks, and the frame after is traced for them too [9299b7cb].
- Frame Traces in a settled battle (mid-battle: the first frame has other flags), the siege benchmark with fire, a dark or night map, the campaign. At 100%, and at 67, 50 and 33% through the game's own scale setting; where the addon owns the scale, the low-scale traces come after Phase 3 step 5.
- Log Details from the start, Record Ray Tracing Profile (30 s) with ReShade's menu closed (it needs the helper's AA on).
- On the maintainer's ini and preset, and again on defaults.
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
| 6. A complete RT scene | Census | All 16 steps of [rt-scene.md](rt-scene.md); stress modes per family; the sun colour at its diffuse scale into `rt_sun` texel 2 ([game-facts.md](game-facts.md)), `HARNESS_PEDESTAL` |
| 7. Replacing game passes: shadow maps, AO, GI, fog march | 6, reader census; AO draws to `rt_scene` only if the AO PS binds a camera ([game-facts.md](game-facts.md)) | WARP "0 values differ" plus a control that must differ; the double-work table |
| 8. RR guides, AO under RR | 6, 7 | `HARNESS_RR`, `rr_specular` |
| 9. Particles, VFX, SSS, reflections, water, fog, look rows | 5 | The per-area order in [sss-particles-water-fog.md](sss-particles-water-fog.md) |
| 10. Presets, Graphics 2 of 2 | All | Every button in a page dump; Reset gives the game's own effects back |

A step starts only when the steps it needs are green. A step that can't go green stops by the stop rule, row hidden.

## Phase 4: the settings matrix offline

GI on and off, paths on and off, RR on and off, 100% and 50%, FG on and off, a preset switch and Reset mid-run. For each row: its fixed cost alone, what it switches on, the page dump in every state.

## Phase 5: one bundled run (ask first)

Planned and dry-run as in [testing.md](testing.md) section 3; a GPU reset needs at least 6 runs per state, the control first ([helper-fg-stability.md](helper-fg-stability.md)).

## Phase 6: the maintainer's list

In the format of [testing.md](testing.md) section 3.
