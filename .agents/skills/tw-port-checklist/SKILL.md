---
name: tw-port-checklist
description: "RenoDX workflow for Total War (TW) addons: bringing up a new or older TW game (Rome II, Thrones, Troy, Pharaoh, WH1, WH2, WH3, Shogun 2) and porting features between TW addons in both directions, Attila (src/games/twattila) first. Covers the dlaa_helper process and bridge, DLSS, DLAA, FSR, XeSS, Ray Reconstruction, frame generation (child window, backend Auto), Reflex, Frame Rate Limit, Xe Low Latency, helper-free AA (SMAA, CMAA2, SMAA T2x, TAA) and its colour domain, the vendor fallback from a saved DLSS, render scale, jitter, motion vectors, RT shadow coverage (casters, cut-outs, grass, trees, far cards, units out of view, black patches), cluster sets after index rewrites, ray tracing range rows (Grass / Units Ray Tracing Range, Swaying Shadows Range), ropes and sails, the game's shadow maps, Two Shadow Cascades Under Rays, AO, GI, AO reader lists, path-traced AO, the seen-vs-unseen path probe, grace windows for a method switch, torch light and shadows from path tracing, path-traced reflection occlusion, Lean ReSTIR Reuse, SER, CPU cost, the light sort reuse, byte-pattern engine hooks, UI 4K Mod Compatibility, in-game Gamma and Brightness, patch slot collisions, test switches (RENODX_TW_TEST_*), the fast draw dispatch and its gates, the game's own CPU hot spots, double work, screen-space shadows (Bend), smoke, particles, SSR, water, fog, HDR output, PsychoV35, Hue Clip, tone mapper edge inputs, replaced shaders, shared shaders, frame traces, shader census, caster census, engine values, the graphics page, dependent rows, presets, crashes, GPU resets, device loss, x86 address space, flip chains on a window switch, game runs, log checks, the test list, install, release zips, parity with Attila across every TW game, closing a round with nothing dropped. Use when bringing up, porting, reviewing or debugging a TW feature, adding a hash or a row, or editing shared code under twattila or twrome2."
argument-hint: "target game, feature or phase, and the Attila or WH3 code it comes from"
---

# Total War Port Checklist

Bring up a TW game, or bring a feature from one TW addon to another, so that the first build is right and the maintainer's test round confirms instead of discovers. Shared code is any file another game folder includes: mostly `twattila/` and `twrome2/`, also `twwarhammer2/` (AO, light bounce), `talesofarise_dlss/` (NGX, frame generation, pixel trace: in Attila and the helper) and Shogun 2's Bend shader. List who includes a file before editing it: `grep -rlE '\.\./(twattila|twrome2|twwarhammer2|talesofarise_dlss|twshogun2completeedition)/' src/games`.

Many checks use the maintainer's `tools/tw` folder (harness, WARP tests, census scripts), which is outside the repo: ask the maintainer for its path, and without it say in the report which checks couldn't run.

WH3's port took 13 versions, about 180 commits and 105 test items for the maintainer; most of what the game showed an offline check could have shown first ([references/bringup.md](references/bringup.md) lists them). This skill is those checks.

The engine-neutral method (proof levels, the stop rule, the maintainer's rules, measurement, the test-list format, agents and tools, the helper and bridge rules, the fast draw dispatch) is in `game-port-method`: read it first. This skill adds what is true only for Total War. Before editing C++, read `AGENTS.md`, `src/games/AGENTS.md` and `docs/CXX_STYLE.md`.

## Related skills

| Need | Use |
|---|---|
| The method and rules every game shares; UE4, CryEngine | `game-port-method` |
| Anything in a TW addon: bring-up, port, capture, helper, rows | This skill and the reference files below |
| A new game folder, build targets, links | `setup-game-mod-dev`, with TW's overrides: Release builds from `build` and `build32`, plain copies (no symlinks), no DevKit (FrameTrace and capture marks instead), a pinned addon renamed aside; start from the closest TW addon, not `src/games/generic` |
| Proving where the output goes, swapchain and proxy state | `swapchain-resource-analysis` |
| Tone mapper and LUT math in a proven pass | `handle-sdr-tonemap-lut` |

## Reference files

Read the file for every area the work touches before planning. Each row is a mistake already made once: the rule, and how to check it without the game.

| File | Read when |
|---|---|
| [bringup.md](references/bringup.md) | Starting a new or older TW game; planning the order of a big round |
| [game-facts.md](references/game-facts.md) | Anything that reads a game's registers, slots, formulas, encodings, scales or engine values |
| [rt-scene.md](references/rt-scene.md) | RT and PT shadows, captures, caster families, cut-outs, budgets, the game's shadow maps under rays |
| [ao-gi.md](references/ao-gi.md) | Replacing or skipping any game pass; AO, GI, path-traced AO, sky light, denoising, double work |
| [render-scale-dlss.md](references/render-scale-dlss.md) | Render scale below 100%, DLSS, FSR, XeSS, RR inputs, presets, jitter, motion vectors, the helper-free AA's colour domain |
| [sss-particles-water-fog.md](references/sss-particles-water-fog.md) | Bend screen-space shadows, smoke, particles, reflections, water, fog |
| [helper-fg-stability.md](references/helper-fg-stability.md) | The helper and bridge, frame generation, device loss, swap chains made again, unload, crashes, GPU resets, CPU cost |
| [shaders-ui.md](references/shaders-ui.md) | Replaced shaders, decompiler traps, HDR output, the graphics page, presets, tooltips |
| [testing.md](references/testing.md) | Offline tests, test switches, game runs, the maintainer's test list, reading their evidence, profiling |
| [release.md](references/release.md) | Closing a round: builds, review, release zip, install, report |

## Before you start

1. `game-port-method`'s Before you start (git, `HANDOFF.md`, the worktree's base, the target's own bytes, double work: the table in [ao-gi.md](references/ao-gi.md)).
2. The newest helper and bridge (`game-port-method`'s rule): today's `twattila/dlaa_bridge.h` and the `renodx-tw-dlaa64.exe` built from the same commit. What the newer helpers fixed: the top of [helper-fg-stability.md](references/helper-fg-stability.md).
3. Read the reference file for each area you touch, and every row of [game-facts.md](references/game-facts.md) for the target game. Warscape's registers, slots, encodings, scales and texture slots differ per game: "same as Attila" needs file and symbol for both games.
4. A new TW game gets the whole package in its first round: HDR (already in every TW addon) plus everything Attila has. A "won't get" needs its proof in the commit (hash not in the dump, pass not in any trace), or it will be asked for later, and first the second look of `game-port-method`'s Parity ledger rule (the blocker's number, the replace-the-input route tried). The cost to avoid is the maintainer's hours of in-game testing: every check that can run offline runs before the build reaches them.
5. Order areas by what they need: the RT scene before anything that replaces the game's shadows; a reader census before any replacement; the render-scale audit before tuning effects at one scale.

## A new game in order

Details, exit criteria and the WH3 history in [bringup.md](references/bringup.md).

0. Offline census first, no game run: shader names, engine values, the scale census over the whole dump, the caster census over the whole dump, then over any trace, the game-facts sheet, a page-dump driver and a parity ledger against Attila's page ([scripts/parity_check.py](scripts/parity_check.py)); a first build from the closest TW addon with only the trace and log rows, every feature off.
1. One data run, only when the maintainer allows it: frame traces in four scenes, on the maintainer's settings and on defaults, at 100% and at the game's own lower scales. Each run folder through `tools/tw/check_logs.py` before reading it.
2. Offline from the traces: pass map, reader census by handle, capture candidates, pass diffs between scales and settings; the game-facts rows answered again from the traces.
3. Features in dependency order, each with its offline check green before the next. Never skipped: the game's sun colour into `rt_sun` texel 2 for Path-Traced GI, the AO hook only where the AO pass binds a camera ([game-facts.md](references/game-facts.md)), every full-screen quad's uv read below 100% ([render-scale-dlss.md](references/render-scale-dlss.md)).
4. The settings matrix offline: GI, paths, RR, 50%, FG, Reset mid-battle; each row's cost alone.
5. One bundled game run, its plan checked against a page dump of the maintainer's preset.
6. The maintainer's test list ([testing.md](references/testing.md) section 3).

## Proof levels and the stop rule

As in `game-port-method`. In TW, level A is a pass in a `tw_frame_trace.txt` with its bytes read; level B is in the pack's dump (`names.tsv`) with its bytes read; a hash's proof in the commit names its family from `names.tsv`.

## The maintainer's rules, TW part

Every rule in `game-port-method`'s list holds; these are the Total War additions.

- **Game runs**: the files a run can write include the replays and saves (WH3: `$WH3_PROTECTED` in `test_scripts/wh3_common.ps1`); the standing OKs and the data run are in [testing.md](references/testing.md) section 2.
- **Look**: a new TW game's `HUE_CLIP_DEFAULT` is measured with `test/tw_psychov35` (Hue Clip's default, see [shaders-ui.md](references/shaders-ui.md)).
- **Two ways**: a new game's row goes back to Attila.
- **Process**: the merged tree's verification includes the harness by mode name.
- **Nothing dropped**: "too big" needs a measured number (Shogun 2's units were 1.18 M corners, not "too big"). A round is done only when [scripts/parity_check.py](scripts/parity_check.py) prints no open row for any TW game: per game, each Attila row and each caster the scene doesn't hold is HAVE, DONE, NEEDS A TRACE (scene named) or WON'T GET (proof) in its ledger (2026-11-13: every new game was missing rows, casters and settings Attila had).
- **Builds**: no DevKit left in a TW game folder.

## Outside Total War

Non-TW games (Tales of Arise, Ryse, Expeditions: Rome and later ones) follow `game-port-method` and its engine notes, not this skill. What they take from TW folders, so the grep at the top lists them as consumers:

- `twattila/rt_core.hpp` through a host's `scene_draw` and `main_view` (Ryse, Expeditions); the helper and `dlaa_bridge` (Ryse, Expeditions, and Shogun 2, a TW game with its own host); `psycho35_settings.hpp` and `credits.hpp` in every HDR addon.
- The other way: `talesofarise_dlss/` (NGX, frame generation, pixel trace) is included by Attila and the helper, so a change there rebuilds both.
- Ryse, Tales and Expeditions are one addon each since 2026-11-15, like Shogun 2 (`game-port-method`'s [addon-split.md](../game-port-method/references/addon-split.md)); `talesofarise_dlss/` stays as a header folder (no addon of its own); they keep their own trace header (`talesofarise_dlss/frame_trace.hpp`) and tool folders beside `tools/tw` (`toa`, `ryse`, `exr`, `expeditions`, `shogun2`, `ingame`).

## Checks by kind of change

**Shared code** (any file another game folder includes; the grep at the top lists who): build every addon that includes it, 32-bit too; a change in `talesofarise_dlss/ngx_backend.hpp` also rebuilds the helper. Attila, Rome II and Thrones are only meaningful from `build32`. After a shared edit:

```text
cmake --build build --config Release --target renodx-ruse.addon64 renodx-twromeremastered.addon64 renodx-talesofarise.addon64 renodx-expeditionsrome.addon64 renodx-twtroy.addon64 renodx-twpharaohdynasties.addon64 renodx-twwarhammer.addon64 renodx-twwarhammer2.addon64 renodx-twwarhammer3.addon64 renodx-twshogun2completeedition.addon64 renodx-rysesonofrome.addon64
cmake --build build32 --config Release --target renodx-twattila.addon32 renodx-twrome2.addon32 renodx-twthronesofbritannia.addon32
```

A behaviour one game needs goes behind a parameter that defaults to today's value. A shader two games use lives in the shared folder (`twattila/`) as an `.hlsli`, each game's file a thin include; a game folder never includes another game's shader (Divinity 2's bounce shaders included Expeditions' 0x7A1E0D48 / 49 until they moved to `twattila/rt_bounce_prepare.hlsli` / `rt_bounce_resolve.hlsli`, byte-identical builds [1713459d]).

**Helper or `dlaa_bridge.h`**: build into a new folder passed as the first argument of `src\games\twattila\dlaa_helper\build.ps1`, as an absolute path (a relative one fails writing `fg_ui.h`) (`build\tw_dlaa_helperN`; without it the script writes over `build\tw_dlaa_helper`), never over an old one. A change to the bridge's layout bumps `VERSION` and keeps `OLDEST_VERSION` readable; `Prepare` publishes only addon-owned fields. Copy the new helper into `build\tw_rt_harness` and `build\tw_fg_test` first (the scripts run whatever helper sits there), rebuild the harness after a bridge change (it includes `dlaa_bridge.hpp`), and check the helper log's version line. Then run the full harness by mode name (`removal`, `switching_removal`, `release_*`, `leaf_storm`), `fg_bridge_test` and the `tw_helper_lifetime`, `tw_gpu_completion` suites. Addon and helper ship together.

**New replacement shader**: TW folders name it `shaders/<name>_0x<CRC>.<target>.hlsl` (not `src/games/AGENTS.md`'s CRC-only form; `cb_audit` reads only this form). Build first, then run [scripts/cb_audit.py](scripts/cb_audit.py) with `python -I`, naming the game (`cb_audit.py twwarhammer3`), and read each line of part 2; the new shader must not be in its last line's "not compared" list. Then a WARP test against the game's bytes with a control that must fail. The script reads the decompiler's pack under `bin/`, which isn't in the repo.

**Any C++ change in a TW addon or the helper**: [scripts/static_checks.py](scripts/static_checks.py) prints no new finding: every `rt_scene::Install` host calls `rt_scene::OnTonemapDrawn` from its tone mapper hook (else the Debug View rows stay hidden, and the page dump can't show it), and no `&x` goes into a `ComPtr<T>*` callee that reads it (`std::addressof`). `--rev <fix>^` is its control.

**New `command_action::Register`**: without callback data use `[]<typename Context>`; a `<typename T, typename Context>` lambda is dropped silently. [scripts/scan_register.py](scripts/scan_register.py) must print nothing. Add a one-shot "applied" log line. A `.bypass` nearby needs its state back in `finalize_callback`. Every `Register` and `Install` has its `Unregister` and `Uninstall` in `DetachAddon`.

**New TW add-on, or a new command-wide callback** (`command_action::Register` without a shader hash): the add-on calls `twattila::draw_dispatch::Use(DLL_PROCESS_ATTACH)` right after `mods::shader::Use` (or `command_action::Use`), and its detach just before theirs. An add-on without resource upgrades (a `_dlss` add-on) calls `command_action::fast::Use` itself. Gates, the reason and the checks: `game-port-method`'s fast draw dispatch section.

**New callback on the sun draws**: say where in the run order it lands and why. Registration order is run order.

**New caster family or capture row**: its line in the census of [rt-scene.md](references/rt-scene.md), with which pass draws it, its budget, its overflow answer, a log line and a harness stress mode. [scripts/caster_census.py](scripts/caster_census.py) `--whole-dump` lists every caster VS the scene doesn't hold, traces or not; on campaign and battle traces it lists what they drew and the scene misses.

**Replacing or skipping a game pass** (AO, GI, shadow maps, fog march, SSR): readers by resource handle in traces at 100% and 50%; every term the output multiplies; per-frame or kept across frames; never switch a render stage's engine value back on live ([game-facts.md](references/game-facts.md) traps); exactly one method, enforced at load and at present. See [ao-gi.md](references/ao-gi.md).

**A ray feature or row**: write down what it switches on (RR, NRD, the terrain wait, grass capture) and measure its fixed cost alone. A cheap feature must not pull in the costly ones.

**Anything below 100%**: the scale checklist of [render-scale-dlss.md](references/render-scale-dlss.md) at 100, 67, 50 and 33%, and a cold start at the low scale. Done means every screen-space pass swept at Ultra Performance in every scene type (land and sea battles, siege, campaign, menu), and [scripts/scale_census.py](scripts/scale_census.py) (replay built by [scripts/scale_census_build.ps1](scripts/scale_census_build.ps1)) run over the whole dump.

**Motion vectors**: a census of what each G-buffer VS writes to velocity; anything the game rewrites within a frame is read before the rewrite or marked stale.

**New work on the game's device** (side draws, compute, stream output, queries): targets kept, stream output without PS, compute at frame end, no CPU wait on the GPU; an off row and a Flush candidate per batch. Statics on the game's device are released at `destroy_device`; the addon never makes a device and never calls the game's device off its thread. See [helper-fg-stability.md](references/helper-fg-stability.md).

**New rows**: check the key and label set; hidden when without effect in this game, and a shared row made visible by a new table entry counts as a new row. Dry-run every preset button in a page dump.

## Testing without the game

Inventory and traps in [testing.md](references/testing.md). In short:

- In the repo: `test/tw_*/run.ps1`, x86 and amd64, after any change they cover.
- Outside it, in the maintainer's `tools/tw`: `all_modes.sh` (compare by mode name), `rt_harness`, `fg_child/fg_bridge_test`, `warp_tests`, `page_dump`, the sun patch test, the scale and census scripts.
- The harness covers the bridge, the helper and part of the D3D11 capture. It doesn't run a game's passes or its CPU cost at game scale: say so in the report.
- `tools/tw/check_logs.py <folder>` flags a run without a new log or without its "ready" / "applied" lines, and the known-bad lines (the data-run scripts print it at their end).
- What only the game can prove goes on the test list.
- Costs and frame times come from measurements (`game-port-method`'s Measurement section): the harness and Nsight for the helper, Time Each Pass and frame traces for the game's passes, PresentMon and WPR for the CPU. Attila's fixed scene: `tools/tw/bench/constantinople_bench.ps1`, the Constantinople siege after its scripted camera move, the CPU fix's prefetch build as the baseline, with the maintainer's standing OK.

## Asking the maintainer for a test

The format is `game-port-method`'s; the TW parts of the dry-run in [testing.md](references/testing.md) section 3.

## Finishing a round

Work through [release.md](references/release.md) in order (rebuild at HEAD right before stamping, never after; stamp and install only through `tools/tw/install/tw_install.py`, dry run first, which refuses an old build or a folder without NVIDIA's three DLLs; zips through `release_zip.py` / `player_zip.py`, which carry them): the double-work question, the parity check over every TW game, every open item a `HANDOFF.md` checkbox with an owner, every consumer built, one independent review of the merged tree and one agent for its fixes, then the report per game (done, not done, what the maintainer tests, what wasn't run). Every bug the maintainer found in game becomes a row in the right reference file, with the offline check that would have caught it.
