---
name: tw-port-checklist
description: "RenoDX workflow for Total War (TW) addons: bringing up a new or older TW game (Rome II, Thrones, Troy, Pharaoh, WH1, WH2, WH3, Shogun 2) and porting features between TW addons in both directions, Attila (src/games/twattila) first. Covers the dlaa_helper process and bridge, DLSS, DLAA, FSR, XeSS, Ray Reconstruction, frame generation, Reflex, render scale, jitter, motion vectors, RT shadow coverage (casters, cut-outs, grass, trees, far cards, units out of view, black patches), the game's shadow maps, AO, GI, path-traced AO, double work, screen-space shadows (Bend), smoke, particles, SSR, water, fog, HDR output, PsychoV35, Hue Clip, replaced shaders, frame traces, shader census, caster census, engine values, the graphics page, presets, crashes, GPU resets, device loss, game runs, log checks, the test list, install, release zips. Use when bringing up, porting, reviewing or debugging a TW feature, adding a hash or a row, or editing shared code under twattila or twrome2."
argument-hint: "target game, feature or phase, and the Attila or WH3 code it comes from"
---

# Total War Port Checklist

Bring up a TW game, or bring a feature from one TW addon to another, so that the first build is right and the maintainer's test round confirms instead of discovers. Shared code is any file another game folder includes: mostly `twattila/` and `twrome2/`, also `twwarhammer2/` (AO, light bounce), `talesofarise_dlss/` (NGX, frame generation, pixel trace: in Attila and the helper) and Shogun 2's Bend shader. List who includes a file before editing it: `grep -rlE '\.\./(twattila|twrome2|twwarhammer2|talesofarise_dlss|twshogun2completeedition)/' src/games`.

Many checks use the maintainer's `tools/tw` folder (harness, WARP tests, census scripts), which is outside the repo: ask the maintainer for its path, and without it say in the report which checks couldn't run.

WH3's port took 13 versions, about 180 commits and 105 test items for the maintainer; most of what the game showed an offline check could have shown first ([references/bringup.md](references/bringup.md) lists them). This skill is those checks.

Before editing C++, read `AGENTS.md`, `src/games/AGENTS.md` and `docs/CXX_STYLE.md`. This skill adds what those can't know.

## Related skills

| Need | Use |
|---|---|
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
| [render-scale-dlss.md](references/render-scale-dlss.md) | Render scale below 100%, DLSS, FSR, XeSS, RR inputs, presets, jitter, motion vectors |
| [sss-particles-water-fog.md](references/sss-particles-water-fog.md) | Bend screen-space shadows, smoke, particles, reflections, water, fog |
| [helper-fg-stability.md](references/helper-fg-stability.md) | The helper and bridge, frame generation, device loss, unload, crashes, GPU resets, CPU cost |
| [shaders-ui.md](references/shaders-ui.md) | Replaced shaders, decompiler traps, HDR output, the graphics page, presets, tooltips |
| [testing.md](references/testing.md) | Offline tests, game runs, the maintainer's test list, reading their evidence, profiling |
| [release.md](references/release.md) | Closing a round: builds, review, release zip, install, report |

## Before you start

1. `git status`, `git log` and the checklist in `HANDOFF.md`: an earlier attempt may have got further. Continue it, don't redo it.
2. **The newest helper and bridge, never an older copy.** A new or older TW game, and any non-TW addon on the bridge, builds against today's `twattila/dlaa_bridge.h` and ships with the `renodx-tw-dlaa64.exe` built from the same commit; an addon built against an older bridge is rebuilt and shipped with the matching helper. What the newer helpers fixed: the top of [helper-fg-stability.md](references/helper-fg-stability.md).
3. Read the reference file for each area you touch, and every row of [game-facts.md](references/game-facts.md) for the target game.
4. Read the target game's own shader, buffer and trace. **Never carry over the source game's values, registers, slots, formulas, encodings, scales or texture slots unread.** "Same as Attila" needs file and symbol for both games.
5. Ask of the feature: does the game already do this? The same effect computed twice is the most common waste ([ao-gi.md](references/ao-gi.md)).
6. A new TW game gets the whole package in its first round: HDR (already in every TW addon) plus everything Attila has. A "won't get" needs its proof in the commit (hash not in the dump, pass not in any trace), or it will be asked for later. The cost to avoid is the maintainer's hours of in-game testing: every check that can run offline runs before the build reaches them.
7. Order areas by what they need: the RT scene before anything that replaces the game's shadows; a reader census before any replacement; the render-scale audit before tuning effects at one scale.

## A new game in order

Details, exit criteria and the WH3 history in [bringup.md](references/bringup.md).

0. Offline census first, no game run: shader names, engine values, the scale census over the whole dump, the caster census over the whole dump, then over any trace, the game-facts sheet, a parity table against Attila's page; a first build from the closest TW addon with only the trace and log rows, every feature off.
1. One data run, only when the maintainer allows it: frame traces in four scenes, on the maintainer's settings and on defaults, at 100% and at the game's own lower scales. Each run folder through `tools/tw/check_logs.py` before reading it.
2. Offline from the traces: pass map, reader census by handle, capture candidates, pass diffs between scales and settings; the game-facts rows answered again from the traces.
3. Features in dependency order, each with its offline check green before the next. Never skipped: the game's sun colour into `rt_sun` texel 2 for Path-Traced GI, the AO hook only where the AO pass binds a camera ([game-facts.md](references/game-facts.md)), every full-screen quad's uv read below 100% ([render-scale-dlss.md](references/render-scale-dlss.md)).
4. The settings matrix offline: GI, paths, RR, 50%, FG, Reset mid-battle; each row's cost alone.
5. One bundled game run, its plan checked against a page dump of the maintainer's preset.
6. The maintainer's test list ([testing.md](references/testing.md) section 3).

## Proof levels for a game fact

Everything a port assumes about a game is one of these. Note the level in the commit.

| Level | What was seen | What may ship |
|---|---|---|
| A | Pass seen in a frame trace, and its bytes read | Ships normally; still off by default if it changes the picture when wrong |
| B | In the shader dump with bytes read, not in any trace | Off by default, Experimental, a line on the in-game test list |
| C | Needs a value or hash nobody has seen | Left out, row hidden, a line on the test list |

A hash goes in only with its proof in the commit message: the shader's family name, the bytes read, the trace line if any. A value read from an agent's game run counts as level A only if the run used the maintainer's ini and preset: agent settings once hid the game's GI from a whole round.

**Stop a piece when** a hash isn't in the dump, the fix needs a guessed offset or register, two attempts failed at the same offline check, the piece has eaten more than twice its size with no green check, or shared code would change another game's behaviour without a parameter. Then revert only that piece, hide its row, leave a `NOT DONE: what, why, smallest step` line in `HANDOFF.md`, and go on. With a feature unproven, the game must behave exactly as without it.

## The maintainer's rules

- **Game runs** only when the maintainer allows them, per batch; the permission ends when the batch closes. Bundle what needs the game into one run. Back up the maintainer's `ReShade.ini`, `ReShadePreset.ini`, the game's preferences and the replays and saves a run can write (WH3: `$WH3_PROTECTED` in `test_scripts/wh3_common.ps1`), right before each install or run, never restore an older copy, compare hashes after.
- **Unproven work** ships as an Experimental row, off, with a player tooltip and one verdict line on the test list. Defaults change only from measured numbers. A section's More Options may hold as many Experimental rows as needed; the maintainer decides each row: on by default, in which presets, or left under More Options. Give them the numbers for that decision (cost, what it fixes, what it risks).
- **Look**: keep the developers' look; scale quality, distance and resolution; a look change is an Experimental row, off. No temporal dither on coverage (DLSS smears it). No flicker fix that dims or compresses highlights: fix the source. Recommended uses PsychoV35 in every HDR addon: a new game takes `twattila/psycho35_settings.hpp` with its own `HUE_CLIP_DEFAULT`, measured with `test/tw_psychov35`; ask the maintainer for its hue settings (Hue Clip's default is measured, see [shaders-ui.md](references/shaders-ui.md)).
- **Cost**: no per-frame counters or log lines, no per-frame CPU on the game's thread without a measured reason, hot callbacks return on one atomic.
- **Rows**: tooltips short and for players, naming nothing the game lacks; keys stay when rows move or saved values are lost.
- **Every setting**: players run every shader model, quality level and preset. A pass or permutation the maintainer's settings never draw is still fixed and ported when it can be proven offline (WARP against the game's bytes: level B, Experimental or a test line); skip it only with the one trace it needs or a WON'T GET proof.
- **Two ways**: a fix in shared code or a pattern goes to every addon that carries it; a new game's row goes back to Attila.
- **Process**: one implementer at a time on one tree, on a verified plan; read-only surveys may run beside it. Parallel rounds each get their own worktree and game folder: check its base first (`git merge-base --is-ancestor <atn-mods tip> HEAD`: a worktree was once made on `main`), give each agent its own scratch-file prefix (they share one scratchpad), and merge one branch at a time, then verify the merged tree once (every target, the test suites, the harness by mode name, page dumps) before stamping. The batch checklist lives in `HANDOFF.md`, ticked in the commit of every step, so a stop or a compaction loses nothing.
- **Git**: one commit per item with cause and numbers, pushed to the fork's `atn-mods` right away, never upstream. No tags (the maintainer tags). No Nexus text.
- **Builds**: never mix addons built before and after an upstream merge in one game folder. No DevKit left in a TW game folder.
- **Licences**: licence text for anything shipped; no code from mods without a licence.

## Outside Total War

Tales of Arise, Ryse, Expeditions: Rome and later non-TW games share part of the code and all of the method.

- **Shared code**: `twattila/rt_core.hpp` through a host's `scene_draw` and `main_view` (Ryse); the helper and `dlaa_bridge` (Ryse, Expeditions, Shogun 2); `talesofarise_dlss/` (NGX, frame generation: Attila and the helper include it); `psycho35_settings.hpp` and `credits.hpp` in every HDR addon.
- **Method**: proof levels, a fact sheet per game, the maintainer's rules above, WARP tests against the game's bytes, page dumps, Experimental rows, the round report. [game-facts.md](references/game-facts.md)'s Warscape questions (t49 routes, `shared.h` registers, cascades) become the engine's: who owns jitter and scale, depth direction, tiled or deferred lights.
- **Differences**: no offline shader dump for CryEngine (a packed `ShaderCache.pak`) or for code Denuvo hides, so level B means "seen in a runtime dump". Most have two addons (HDR plus `_dlss`, graphics rows in the DLSS overlay), their own trace header (`talesofarise_dlss/frame_trace.hpp`) and tool folders beside `tools/tw` (`toa`, `ryse`, `exr`, `expeditions`, `shogun2`, `ingame`). Tales takes Ray Reconstruction through NGX D3D12: D3D11 RR with a GPU wait went black.
- A rule that holds in a second engine belongs in a shared skill, not in two copies.

## Checks by kind of change

**Shared code** (any file another game folder includes; the grep at the top lists who): build every addon that includes it, 32-bit too; a change in `talesofarise_dlss/ngx_backend.hpp` also rebuilds the helper. Attila, Rome II and Thrones are only meaningful from `build32`. After a shared edit:

```text
cmake --build build --config Release --target renodx-ruse.addon64 renodx-twromeremastered.addon64 renodx-talesofarise.addon64 renodx-expeditionsrome.addon64 renodx-twtroy.addon64 renodx-twpharaohdynasties.addon64 renodx-twwarhammer.addon64 renodx-twwarhammer2.addon64 renodx-twwarhammer3.addon64 renodx-twshogun2completeedition.addon64 renodx-twshogun2completeedition_dlss.addon64 renodx-rysesonofrome.addon64 renodx-rysesonofrome_dlss.addon64 renodx-expeditionsrome_dlss.addon64 renodx-talesofarise_dlss.addon64
cmake --build build32 --config Release --target renodx-twattila.addon32 renodx-twrome2.addon32 renodx-twthronesofbritannia.addon32
```

A behaviour one game needs goes behind a parameter that defaults to today's value.

**Helper or `dlaa_bridge.h`**: build into a new folder passed as the first argument of `src\games\twattila\dlaa_helper\build.ps1`, as an absolute path (a relative one fails writing `fg_ui.h`) (`build\tw_dlaa_helperN`; without it the script writes over `build\tw_dlaa_helper`), never over an old one. A change to the bridge's layout bumps `VERSION` and keeps `OLDEST_VERSION` readable; `Prepare` publishes only addon-owned fields. Copy the new helper into `build\tw_rt_harness` and `build\tw_fg_test` first (the scripts run whatever helper sits there), rebuild the harness after a bridge change (it includes `dlaa_bridge.hpp`), and check the helper log's version line. Then run the full harness by mode name (`removal`, `switching_removal`, `release_*`, `leaf_storm`), `fg_bridge_test` and the `tw_helper_lifetime`, `tw_gpu_completion` suites. Addon and helper ship together.

**New replacement shader**: TW folders name it `shaders/<name>_0x<CRC>.<target>.hlsl` (not `src/games/AGENTS.md`'s CRC-only form; `cb_audit` reads only this form). Build first, then run [scripts/cb_audit.py](scripts/cb_audit.py) with `python -I`, naming the game (`cb_audit.py twwarhammer3`), and read each line of part 2; the new shader must not be in its last line's "not compared" list. Then a WARP test against the game's bytes with a control that must fail. The script reads the decompiler's pack under `bin/`, which isn't in the repo.

**Any C++ change in a TW addon or the helper**: [scripts/static_checks.py](scripts/static_checks.py) prints no new finding: every `rt_scene::Install` host calls `rt_scene::OnTonemapDrawn` from its tone mapper hook (else the Debug View rows stay hidden, and the page dump can't show it), and no `&x` goes into a `ComPtr<T>*` callee that reads it (`std::addressof`). `--rev <fix>^` is its control.

**New `command_action::Register`**: without callback data use `[]<typename Context>`; a `<typename T, typename Context>` lambda is dropped silently. [scripts/scan_register.py](scripts/scan_register.py) must print nothing. Add a one-shot "applied" log line. A `.bypass` nearby needs its state back in `finalize_callback`. Every `Register` and `Install` has its `Unregister` and `Uninstall` in `DetachAddon`.

**New callback on the sun draws**: say where in the run order it lands and why. Registration order is run order.

**New caster family or capture row**: its line in the census of [rt-scene.md](references/rt-scene.md), with which pass draws it, its budget, its overflow answer, a log line and a harness stress mode. [scripts/caster_census.py](scripts/caster_census.py) `--whole-dump` lists every caster VS the scene doesn't hold, traces or not; on campaign and battle traces it lists what they drew and the scene misses.

**Replacing or skipping a game pass** (AO, GI, shadow maps, fog march, SSR): readers by resource handle in traces at 100% and 50%; every term the output multiplies; per-frame or kept across frames; never switch a render stage's engine value back on live ([game-facts.md](references/game-facts.md) traps); exactly one method, enforced at load and at present. See [ao-gi.md](references/ao-gi.md).

**A ray feature or row**: write down what it switches on (RR, NRD, the terrain wait, grass capture) and measure its fixed cost alone. A cheap feature must not pull in the costly ones.

**Anything below 100%**: the scale checklist of [render-scale-dlss.md](references/render-scale-dlss.md) at 100, 67, 50 and 33%, and a cold start at the low scale. Done means every screen-space pass swept at Ultra Performance in the campaign and in a battle, and [scripts/scale_census.py](scripts/scale_census.py) (replay built by [scripts/scale_census_build.ps1](scripts/scale_census_build.ps1)) run over the whole dump.

**Motion vectors**: a census of what each G-buffer VS writes to velocity; anything the game rewrites within a frame is read before the rewrite or marked stale.

**New work on the game's device** (side draws, compute, stream output, queries): targets kept, stream output without PS, compute at frame end, no CPU wait on the GPU; an off row and a Flush candidate per batch. Statics on the game's device are released at `destroy_device`; the addon never makes a device and never calls the game's device off its thread. See [helper-fg-stability.md](references/helper-fg-stability.md).

**New rows**: check the key and label set; hidden when without effect in this game, and a shared row made visible by a new table entry counts as a new row. Dry-run every preset button in a page dump.

**Patch scripts**: written to a file, not a heredoc (backslashes collapse, raw NUL bytes end up in sources). Text searches treat a file with a NUL as binary and skip it: find such files with `rg -a`, then fix them.

## Testing without the game

Inventory and traps in [testing.md](references/testing.md). In short:

- In the repo: `test/tw_*/run.ps1`, x86 and amd64, after any change they cover.
- Outside it, in the maintainer's `tools/tw`: `all_modes.sh` (compare by mode name), `rt_harness`, `fg_child/fg_bridge_test`, `warp_tests`, `page_dump`, the sun patch test, the scale and census scripts.
- The harness covers the bridge, the helper and part of the D3D11 capture. It doesn't run a game's passes or its CPU cost at game scale: say so in the report.
- A pass counts only with a "ready" or "applied" line; a run that left no new log is invalid. `tools/tw/check_logs.py <folder>` flags both and the known-bad lines (the data-run scripts print it at their end).
- What only the game can prove goes on the test list.

## Asking the maintainer for a test

At most 10 lines, never what an offline check can show: format and dry-run in [testing.md](references/testing.md) section 3.

## Finishing a round

Work through [release.md](references/release.md) in order (rebuild at HEAD right before stamping, never after; stamp and install only through `tools/tw/install/tw_install.py`, dry run first, which refuses an old build or a folder without NVIDIA's three DLLs; zips through `release_zip.py` / `player_zip.py`, which carry them): the double-work question, every consumer built, one independent review and one agent for its fixes, then the report per game (done, not done, what the maintainer tests, what wasn't run). Every bug the maintainer found in game becomes a row in the right reference file, with the offline check that would have caught it.
