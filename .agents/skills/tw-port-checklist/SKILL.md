---
name: tw-port-checklist
description: "RenoDX workflow for Total War (TW) addons: bringing up a new or older TW game (Rome II, Thrones, Troy, Pharaoh, WH1, WH2, WH3, Shogun 2) and porting features between TW addons in both directions, Attila (src/games/twattila) first. Covers the dlaa_helper process and bridge, DLSS, DLAA, FSR, XeSS, Ray Reconstruction, frame generation, Reflex, render scale, jitter, motion vectors, RT shadow coverage (casters, cut-outs, grass, trees, far cards, units out of view, black patches), the game's shadow maps, AO, GI, path-traced AO, double work, screen-space shadows (Bend), smoke, particles, SSR, water, fog, HDR output, PsychoV35, Hue Clip, replaced shaders, frame traces, shader census, engine values, the graphics page, presets, crashes, GPU resets, device loss, game runs, the test list, release. Use when bringing up, porting, reviewing or debugging a TW feature, adding a hash or a row, or editing shared code under twattila or twrome2."
argument-hint: "target game, feature or phase, and the Attila or WH3 code it comes from"
---

# Total War Port Checklist

Bring up a TW game, or bring a feature from one TW addon to another, so that the first build is right and the maintainer's test round confirms instead of discovers. Attila carries most of the shared code (`src/games/twattila`, `src/games/twrome2`); 13 addons compile it directly.

Many checks use the maintainer's `tools/tw` folder (harness, WARP tests, census scripts), which is outside the repo: ask the maintainer for its path, and without it say in the report which checks couldn't run.

WH3's port took 13 versions, about 180 commits and 105 test items for the maintainer; most of what the game showed an offline check could have shown first ([references/bringup.md](references/bringup.md) lists them). This skill is those checks.

Before editing C++, read `AGENTS.md`, `src/games/AGENTS.md` and `docs/CXX_STYLE.md`. This skill adds what those can't know.

## Related skills

| Need | Use |
|---|---|
| Anything in a TW addon: bring-up, port, capture, helper, rows | This skill and the reference files below |
| A new game folder, build targets, links | `setup-game-mod-dev`; for TW use FrameTrace and capture marks instead of the DevKit, and start from the closest TW addon, not `src/games/generic` |
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
2. Read the reference file for each area you touch, and every row of [game-facts.md](references/game-facts.md) for the target game.
3. Read the target game's own shader, buffer and trace. **Never carry over the source game's values, registers, slots, formulas, encodings, scales or texture slots unread.** "Same as Attila" needs file and symbol for both games.
4. Ask of the feature: does the game already do this? The same effect computed twice is the most common waste ([ao-gi.md](references/ao-gi.md)).
5. A new TW game gets the whole package in its first round: HDR (already in every TW addon) plus everything Attila has. A "won't get" needs its proof in the commit (hash not in the dump, pass not in any trace), or it will be asked for later. The cost to avoid is the maintainer's hours of in-game testing: every check that can run offline runs before the build reaches them.
6. Order areas by what they need: the RT scene before anything that replaces the game's shadows; a reader census before any replacement; the render-scale audit before tuning effects at one scale.

## A new game in order

Details, exit criteria and the WH3 history in [bringup.md](references/bringup.md).

0. Offline census: shader names, engine values, the game-facts sheet, a parity table against Attila's page; a first build from the closest TW addon with only the trace and log rows, every feature off.
1. One data run, only when the maintainer allows it: frame traces in four scenes, on the maintainer's settings and on defaults, at 100% and at the game's own lower scales.
2. Offline from the traces: pass map, reader census by handle, capture candidates, pass diffs between scales and settings; the game-facts rows answered again from the traces.
3. Features in dependency order, each with its offline check green before the next.
4. The settings matrix offline: GI, paths, RR, 50%, FG, Reset mid-battle; each row's cost alone.
5. One bundled game run, its plan checked against a page dump of the maintainer's preset.
6. The maintainer's list: 10 lines or fewer.

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

- **Game runs** only when the maintainer allows them, per batch; the permission ends when the batch closes. Bundle what needs the game into one run. Back up the maintainer's `ReShade.ini`, `ReShadePreset.ini` and the game's preferences right before each install or run, never restore an older copy, compare hashes after.
- **Unproven work** ships as an Experimental row, off, with a player tooltip and one verdict line on the test list. Defaults change only from measured numbers. A section's More Options may hold as many Experimental rows as needed; the maintainer decides each row: on by default, in which presets, or left under More Options. Give them the numbers for that decision (cost, what it fixes, what it risks).
- **Look**: keep the developers' look; scale quality, distance and resolution; a look change is an Experimental row, off. No temporal dither on coverage (DLSS smears it). No flicker fix that dims or compresses highlights: fix the source. Recommended presets use PsychoV31; ask the maintainer for each game's PsychoV31 hue settings (Hue Clip's default is measured, see [game-facts.md](references/game-facts.md)).
- **Cost**: no per-frame counters or log lines, no per-frame CPU on the game's thread without a measured reason, hot callbacks return on one atomic.
- **Rows**: tooltips short and for players, naming nothing the game lacks; keys stay when rows move or saved values are lost.
- **Two ways**: a fix in shared code or a pattern goes to every addon that carries it; a new game's row goes back to Attila.
- **Process**: one implementer at a time on one tree, on a verified plan; read-only surveys may run beside it. The batch checklist lives in `HANDOFF.md`, ticked and committed after every step, so a stop or a compaction loses nothing.
- **Git**: one commit per item with cause and numbers, pushed to the fork's `atn-mods` right away, never upstream. No tags (the maintainer tags). No Nexus text.
- **Builds**: never mix addons built before and after an upstream merge in one game folder. No DevKit left in a TW game folder.
- **Licences**: licence text for anything shipped; no code from mods without a licence.

## Checks by kind of change

**Shared code** (anything under `twattila/` or `twrome2/` that another addon includes): build every addon that includes it, 32-bit too. Attila, Rome II and Thrones are only meaningful from `build32`. After a shared edit:

```text
cmake --build build --config Release --target renodx-twtroy.addon64 renodx-twpharaohdynasties.addon64 renodx-twwarhammer.addon64 renodx-twwarhammer2.addon64 renodx-twwarhammer3.addon64 renodx-twshogun2completeedition.addon64 renodx-twshogun2completeedition_dlss.addon64 renodx-rysesonofrome.addon64 renodx-rysesonofrome_dlss.addon64 renodx-expeditionsrome_dlss.addon64 renodx-talesofarise_dlss.addon64
cmake --build build32 --config Release --target renodx-twattila.addon32 renodx-twrome2.addon32 renodx-twthronesofbritannia.addon32
```

A behaviour one game needs goes behind a parameter that defaults to today's value.

**Helper or `dlaa_bridge.h`**: build into a new folder passed as the first argument of `src\games\twattila\dlaa_helper\build.ps1` (`build\tw_dlaa_helperN`; without it the script writes over `build\tw_dlaa_helper`), never over an old one. A change to the bridge's layout bumps `VERSION` and keeps `OLDEST_VERSION` readable; `Prepare` publishes only addon-owned fields. Run the full harness by mode name, `fg_bridge_test` and the `tw_helper_lifetime`, `tw_gpu_completion` suites. Addon and helper ship together.

**New replacement shader**: run [scripts/cb_audit.py](scripts/cb_audit.py) with `python -I`, naming the game (`cb_audit.py twwarhammer3`), and read each line of part 2. Then a WARP test against the game's bytes with a control that must fail. The script reads the decompiler's pack under `bin/`, which isn't in the repo.

**New `command_action::Register`**: without callback data use `[]<typename Context>`; a `<typename T, typename Context>` lambda is dropped silently. [scripts/scan_register.py](scripts/scan_register.py) must print nothing. Add a one-shot "applied" log line. A `.bypass` nearby needs its state back in `finalize_callback`. Every `Register` and `Install` has its `Unregister` and `Uninstall` in `DetachAddon`.

**New callback on the sun draws**: say where in the run order it lands and why. Registration order is run order.

**New caster family or capture row**: its line in the census of [rt-scene.md](references/rt-scene.md), with which pass draws it, its budget, its overflow answer, a log line and a harness stress mode.

**Replacing or skipping a game pass** (AO, GI, shadow maps, fog march, SSR): readers by resource handle in traces at 100% and 50%; every term the output multiplies; per-frame or kept across frames; never switch a render stage's engine value back on live; exactly one method, enforced at load and at present. See [ao-gi.md](references/ao-gi.md).

**A ray feature or row**: write down what it switches on (RR, NRD, the terrain wait, grass capture) and measure its fixed cost alone. A cheap feature must not pull in the costly ones.

**Anything below 100%**: the scale checklist of [render-scale-dlss.md](references/render-scale-dlss.md) at 100, 67, 50 and 33%, and a cold start at the low scale.

**Motion vectors**: a census of what each G-buffer VS writes to velocity; anything the game rewrites within a frame is read before the rewrite or marked stale.

**New work on the game's device** (side draws, compute, stream output, queries): targets kept, stream output without PS, compute at frame end, no CPU wait on the GPU; an off row and a Flush candidate per batch. Statics on the game's device are released at `destroy_device`; the addon never makes a device and never calls the game's device off its thread. See [helper-fg-stability.md](references/helper-fg-stability.md).

**New rows**: check the key and label set; hidden when without effect in this game, and a shared row made visible by a new table entry counts as a new row. Dry-run every preset button in a page dump.

**Patch scripts**: written to a file, not a heredoc (backslashes collapse, raw NUL bytes end up in sources). Text searches treat a file with a NUL as binary and skip it.

## Testing without the game

Inventory and traps in [testing.md](references/testing.md). In short:

- In the repo: `test/tw_*/run.ps1`, x86 and amd64, after any change they cover.
- Outside it, in the maintainer's `tools/tw`: `all_modes.sh` (compare by mode name), `rt_harness`, `fg_child/fg_bridge_test`, `warp_tests`, `page_dump`, the sun patch test, the scale and census scripts.
- The harness covers the bridge, the helper and part of the D3D11 capture. It doesn't run a game's passes or its CPU cost at game scale: say so in the report.
- A pass counts only with a "ready" or "applied" line; a run that left no new log is invalid.
- What only the game can prove goes on the test list.

## Asking the maintainer for a test

At most 10 lines, the most important first, grouped by scene. Each line: scene, exact row name and value, what to look for, which file to send. Start with a control (rows off looks like the last version), compare on and off inside one run, and prefer lines the build reports itself (applied lines, Record Profile, Frame Trace, Time Each Pass). Check every line against a page dump of the maintainer's current preset: the row shows, the debug view is made, the key exists. Never ask what an offline check or an allowed agent run can show. Format in [testing.md](references/testing.md).

## Finishing a round

Full list in [release.md](references/release.md).

1. Ask the double-work question again: is anything now computed twice?
2. List the source game's code you compared, by file and symbol.
3. For a bug found on the way: is it compiled from shared code (fixed for all, rebuild), or the same pattern in `twattila/` (fix there and in the addons that carry it, with a README line each)? Check the source game without the game: `build32`, harness, WARP or offline test, numeric identity on a refactor, key diff on the page.
4. One independent review of the round's diff, then one agent for its fixes: every close so far found real bugs.
5. One commit per item, the `HANDOFF.md` checklist ticked in the same commit, pushed to the fork.
6. Report per game: done, not done, what the maintainer tests. State what wasn't run.
7. Every bug the maintainer found in game becomes a row in the right reference file, with the offline check that would have caught it. Fix or delete a row that turned out wrong in the same commit.
