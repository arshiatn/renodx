---
name: game-port-method
description: "Engine-neutral RenoDX method for porting or bringing up any game add-on (Total War, Unreal Engine 4, CryEngine and others: Expeditions: Rome, Tales of Arise, Ryse: Son of Rome): proof levels for a game fact, the stop rule, the maintainer's rules (game runs only when allowed, fresh backups, Experimental rows, keep the developers' look, no temporal dither, no flicker fix that dims, every setting players run, Nothing dropped, HANDOFF checkboxes with owners, SHARED STEP, stale reports, parity ledger), measurement over manual testing (fixed scene, scripted bench, quiet lock, look verdicts, Nsight, WPR), the maintainer's test list, agents and tools (testbin, Bitdefender, GPU lock, build limits, worktree base, junctions, patch scripts, reviews, Fable only on request), the helper and bridge, Ray Reconstruction through NGX D3D12 in D3D11 games, the fast draw dispatch opt-in for command_action, UE4 live cvars, Expand Gamut, CryEngine ShaderCache.pak, shader sources, the HDR-only snapshot release check (HDR add-on alone, helper absent, red notice, GitHub and Nexus buttons, SDR preset, first start, page dump), one add-on or two (HDR plus _dlss, merge, cost per draw, cross-add-on settings, draw replay, WPR)."
argument-hint: "game, engine and what is being planned, ported or released"
---

# Game Port Method

How the maintainer's ports are planned and checked, whatever the engine. The Total War rounds learned it; it holds for every game. Engine facts live in the engine's own skill or in [references/engines.md](references/engines.md); this file holds only what is true in every engine. A rule that holds in a second engine moves here, not into two engine skills.

## Related skills

| Need | Use |
|---|---|
| A Total War game: bring-up order, Warscape facts, RT scene, helper internals, TW release tools | `tw-port-checklist`, on top of this skill |
| A new game folder, build targets, links | `setup-game-mod-dev` |
| Where the output goes, swapchain and proxy state | `swapchain-resource-analysis` |
| Tone mapper and LUT math in a proven pass | `handle-sdr-tonemap-lut` |

| File | Read when |
|---|---|
| [references/engines.md](references/engines.md) | Unreal Engine 4 (Expeditions: Rome, Tales of Arise), CryEngine (Ryse), where shader sources come from |
| [references/hdr-only-snapshot.md](references/hdr-only-snapshot.md) | Releasing any HDR add-on: the snapshot on the renodx GitHub that upstream builds alone |
| [references/addon-split.md](references/addon-split.md) | Deciding between one add-on and two (HDR plus `_dlss`): what each costs, how to measure it |

Before editing C++, read `AGENTS.md`, `src/games/AGENTS.md` and `docs/CXX_STYLE.md`.

## Before you start

1. `git status`, `git log` and the checklist in `HANDOFF.md`: an earlier attempt may have got further. Continue it, don't redo it.
2. A worktree's base first: `git merge-base --is-ancestor <atn-mods tip> HEAD`. Worktrees have been cut from `main`; reset only your own worktree.
3. Read the target game's own shader, buffer and code. Never carry over another game's values, registers, slots, formulas or offsets unread: "same as game X" needs file and symbol for both games.
4. Ask of every feature: does the game already do this? The same effect computed twice is the most common waste.
5. Order the work by what it needs, each piece's offline check green before the next.

## Proof levels for a game fact

Everything a port assumes about a game is one of these. Note the level in the commit.

| Level | What was seen | What may ship |
|---|---|---|
| A | Pass seen in a frame trace or capture, and its bytes read | Ships normally; still off by default if it changes the picture when wrong |
| B | In the shader dump with bytes read, not in any trace | Off by default, Experimental, a line on the in-game test list |
| C | Needs a value or hash nobody has seen | Left out, row hidden, a line on the test list |

Where the engine has no offline shader dump (a packed cache, code a protection hides), level B means "seen in a runtime dump" ([references/engines.md](references/engines.md)). A hash goes in only with its proof in the commit message: the shader's name or family, the bytes read, the trace line if any. A value read from an agent's game run counts as level A only if the run used the maintainer's ini and preset: agent settings once hid a game's GI from a whole round.

**Stop a piece when** a hash isn't in the dump, the fix needs a guessed offset or register, two attempts failed at the same offline check, the piece has eaten more than twice its size with no green check, or shared code would change another game's behaviour without a parameter. Then revert only that piece, hide its row, leave a `NOT DONE: what, why, smallest step` line in `HANDOFF.md`, and go on. With a feature unproven, the game must behave exactly as without it.

## The maintainer's rules

- **Game runs** only when the maintainer allows them, per batch; the permission ends when the batch closes. A standing OK covers only what it names (the TW skill lists the ones given). Bundle what needs the game into one run. Back up the maintainer's `ReShade.ini`, `ReShadePreset.ini`, the game's preferences and the replays and saves a run can write right before each install or run, never restore an older copy (a stale restore lost newer preferences, a preset and a replay), compare hashes after. Restores move aside only files the run created inside its own window.
- **Unproven work** ships as an Experimental row, off, with a player tooltip and one verdict line on the test list. Defaults change only from measured numbers. A section's More Options may hold as many Experimental rows as needed; the maintainer decides each row: on by default, in which presets, or left under More Options. Give them the numbers for that decision (cost, what it fixes, what it risks). The maintainer's standing rule (2026-11-15): a row that is faster in the bench and gives the same picture (bit-exact or identical by its test) goes on by default and loses "(Experimental)"; a row that may lower quality goes to the maintainer as a list with its numbers. RenoDX writes every row's value to the ini, so a new default reaches only players who never saved the row: check with `git merge-base --is-ancestor <the key's first commit> <the last release>` whether the key shipped.
- **Look**: keep the developers' look; scale quality, distance, resolution and update rates, keep the artists' tuning (blur, colour, roughness); a look change is an Experimental row, off. No temporal dither on coverage: DLSS smears alpha or dissolve that changes every frame (Shogun 2's Grass Fade); use stable per-object thresholds. No flicker fix that dims or compresses highlights (Ryse's Highlight Stability was rejected): fix the source (geometry, motion vectors, jitter, specular aliasing). Recommended uses PsychoV35 in every HDR add-on: a new game takes `twattila/psycho35_settings.hpp` with its own `HUE_CLIP_DEFAULT`, measured, not copied; ask the maintainer for its hue settings.
- **Cost**: no per-frame counters or log lines, no per-frame CPU on the game's thread without a measured reason, hot callbacks return on one atomic; one-frame traces behind a button.
- **Rows**: tooltips short and for players, naming nothing the game lacks, no dev notes; keys stay when rows move or saved values are lost.
- **Every setting**: players run every shader model, quality level and preset. A pass or permutation the maintainer's settings never draw is still fixed and ported when it can be proven offline (WARP against the game's bytes: level B, Experimental or a test line); skip it only with the one trace it needs or a WON'T GET proof.
- **Two ways**: a fix in shared code or a pattern goes to every add-on that carries it.
- **Process**: one implementer at a time on one tree, on a verified plan; read-only surveys may run beside it. Parallel rounds each get their own worktree and game folder: check its base, give each agent its own scratch-file prefix (they share one scratchpad), merge one branch at a time, then verify the merged tree once (every target, the test suites, the harness, page dumps) before stamping. The batch checklist lives in `HANDOFF.md`, ticked in the commit of every step, so a stop or a compaction loses nothing.
- **Nothing dropped**: every "not done", "later", "listed", "needs" or "found" line of an agent's report, and every line of an analysis report's ranked list (profiles, Nsight, censuses), becomes its own `HANDOFF.md` checkbox with an owner (an agent, the maintainer's decision, or a named trace) in the commit that merges or records the report. "Later" and "listed" are not states; "too big" needs a measured number. Read every report to its end before relaying it.
- **SHARED STEP**: a piece several games need is named SHARED STEP in the report and gets one owner for all games. Before starting other agents, write it into every game's parity ledger as SHARED STEP with that owner, or they build it twice (2026-11-14: one skip built by two agents).
- **Stale reports**: a brief that starts from a report carries every line it owns and names who owns the rest, each line checked first against `git log` and `HANDOFF.md`: a report can predate its own fix (2026-11-14: three fixes done the night before went into a brief again).
- **Parity ledger**: where a reference add-on of the same engine exists (Attila for Total War), the new game's ledger lists the reference's whole package, every row and feature HAVE, DONE, NEEDS A TRACE (scene named) or WON'T GET (proof). Per-game plans stop at their own end; only the ledger check sees the whole package. A new game gets the whole package in its first round.
- **Git**: one commit per item with cause and numbers, pushed to the fork's `atn-mods` right away, never upstream. No tags (the maintainer tags). No Nexus text until the maintainer asks.
- **Builds**: never mix add-ons built before and after an upstream merge in one game folder. Release builds are stamped by the maintainer's private step; never rebuild after stamping.
- **Licences**: licence text for anything shipped; no code from mods without a licence (Arise-SDK is GPL-3.0: findings only, never its code).

## Measurement over manual testing

The maintainer's in-game hours are the cost to avoid. Every check that can run offline runs before a build reaches them.

- **One fixed scene per game** for costs and frame times: a replay, benchmark or save with a scripted camera move, the same every run (Attila: `tools/tw/bench/constantinople_bench.ps1`). Run it once after all of a batch's optimization rounds, each round's candidates as variants, with a named baseline build.
- **A quiet lock** while it measures: while `tools/tw/quiet_run.lock` exists nothing else starts (no build, no test exe, no WARP, no long script). Test exes once ran inside a measurement window and spoiled two starts.
- **Look verdicts are scored**: screenshots off and on against a reference, in one run (pause freezes the picture; settings changed while paused show no change). The quality scorer's scene has no fog, reflections or water (smoke only through its `smoke` feature, 300 particles in the sun's way): fog, reflection and water changes need their own WARP or harness check, no score.
- **Tools by question**: GPU passes by the add-on's own pass timer and frame trace; the CPU by PresentMon and a sampled WPR profile (`wpr` only through the maintainer's elevated tasks `RenoDX_WPR_Start` / `RenoDX_WPR_Stop`, never elevate another way); Nsight Graphics where the game is 64-bit and its protection allows attaching (it doesn't attach to 32-bit D3D11). Never work around a game's protection to attach a tool or read code. An A/B measures a change only where its code is on the critical path: find the waiting thread first.
- **Read the number the change moves**: in a CPU-bound view a GPU saving leaves the fps alone (GPU busy, the pass timer and the helper's profile show it: a torch shadow pick saved 1.7 ms of GPU, fps unchanged). A CPU saving that only turns into CPU wait (CPU busy -1.8 ms, frame time unchanged) means another thread or wait limits the frame.
- **The harness predicts the direction, not the frame**: a render-list sort, an instance gather and a 128-byte memcpy path won in the harness and lost or did nothing in game; D3D11 per-draw side work cost more in game than in the harness (batched unit capture: GPU busy -1.7 to -2.2 ms in game against 0.7 ms predicted; a reuse record -0.22 against -0.37). Only the bench decides a default.
- **Profile a statically found site before porting it**: render-list pushes found by reading the x64 code got 0 samples in Pharaoh and Shogun 2 battles (WPR, 2026-11-15).
- **What the maintainer still tests**: look verdicts and what only a long session shows.
- **A run counts** only with a "ready" or "applied" line in a new log: a build that failed PSO creation once "passed", an old log was once copied.

## The maintainer's test list

- 10 lines or fewer, the most important first, grouped by scene so one start covers each scene.
- Each line: scene; exact row name and value; what to look for; which file to send. First line a control: rows off looks like the last version.
- Every Experimental row gets one A/B line naming the decision it drives. A row's cost shows only with its content on screen: name the scene and camera.
- Prefer what the build reports itself ("applied" lines, profiles, frame traces, pixel traces): then the maintainer only sends the logs and the ini.
- Dry-run each line, and every agent run's plan, against a page dump of the maintainer's current preset: the row shows, the key exists in this game, every log line the plan relies on exists in the build.
- Never ask what an offline check or an allowed agent run can show; never repeat an unchanged item. A longer scripted list belongs to agent runs only.

## Agents and tools

- **Standalone test exes** are allowed, built only in `tools/tw/testbin`, which the maintainer excluded in Bitdefender (nothing downloaded goes there). Bitdefender quarantines fresh unsigned exes elsewhere: stop that path and tell the maintainer; never evade detection or touch its settings.
- **Load limits**: start a build only under 45 GB Committed Bytes, with `-j 3`; one GPU job at a time through `tools/tw/gpu_job.lock`, taken atomically, deleted only by its owner.
- **Worktrees** link `bin`, `build\downloads` and `external\*` into the main checkout as junctions: delete the links alone before any recursive delete of a worktree.
- **Patch scripts** are written to a file with the Write tool, not a shell heredoc (backslashes collapse, raw NUL bytes end up in sources). Text searches skip a file with a NUL as binary: find such files with `rg -a`, then fix them.
- **Reviews, bug hunts and planning** run on the normal model: one independent review of a round's merged tree, then one agent for its fixes. Fable only when the maintainer asks for it, or after asking them, for a problem that can't be solved otherwise or a really big change: it costs far more.
- **Data-collecting agent runs** (start, traces, logs, quit), where the maintainer allows them, run at medium effort: they collect; the reading stays offline.

## The helper and bridge

The TW helper (`renodx-tw-dlaa64.exe`, `src/games/twattila/dlaa_helper/`) also serves non-TW add-ons (Ryse, Expeditions: Rome, Shogun 2) through `twattila/dlaa_bridge.h`. Its internals are in `tw-port-checklist`'s `helper-fg-stability.md`.

- **Newest only**: every add-on on the bridge builds against today's `dlaa_bridge.h` and ships with the helper built from the same commit, never an older copy. An add-on built against an older bridge is rebuilt and shipped with the matching helper.
- **Ray Reconstruction in a D3D11 game goes through NGX D3D12** (`twattila/dlaa_helper/rr_d3d12.hpp` in the helper; Tales' in-process `talesofarise_dlss/rr_d3d12.hpp` is unused since its helper route): NVIDIA's D3D11 RR followed by a GPU wait (or a shared-fence wait) gave black output or an access violation in nvwgf2umx. D3D11 Super Resolution is fine.

## The fast draw dispatch

`utils/command_action_fast.hpp` keeps, per D3D11 command list, which `command_action` callbacks each bound pipeline brings, instead of looking them up on every draw. `command_action`'s registry is one per process, shared by every add-on, so the fast path is on only while **every** module attached to `command_action` opted in: one module without it (the DevKit, an older partner add-on) turns it off for all, and the log says so.

- Any add-on that uses `command_action` (through `mods::shader` or `command_action::Use`) calls `command_action::fast::Use(DLL_PROCESS_ATTACH)` right after that attach, and `fast::Use(DLL_PROCESS_DETACH)` just before its detach. TW add-ons with resource upgrades call `twattila::draw_dispatch::Use` instead (it adds the resource upgrades' gate). Since 2026-11-15 every TW add-on and Ryse, Tales and Expeditions (one add-on each) opt in; a DevKit next to the game still turns it off.
- A new command-wide callback (`command_action::Register` without a shader hash) gets a `command_action::fast::SetGate` that repeats its own first early return (cleared before its `Unregister`), or a comment saying why it runs on every draw; a gate whose per-pipeline answer changes after the pipeline's init calls `fast::Invalidate()`.
- Check: `test/tw_draw_dispatch` (x86 and amd64) and the replay oracle in `tools/tw/draw_redesign` (fast against `--legacy`) show 0 differences.

## Releasing

Every HDR add-on passes the HDR-only snapshot check before its release: [references/hdr-only-snapshot.md](references/hdr-only-snapshot.md). Then the engine's own release steps (TW: `tw-port-checklist`'s `release.md`), then the report per game: done, not done, what the maintainer tests, what wasn't run, built or installed. Each bug the maintainer finds in game becomes a row in the right skill file, with the offline check that would have caught it.
