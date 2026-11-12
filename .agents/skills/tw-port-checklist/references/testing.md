# Testing: offline, game runs, the maintainer's list

## 1. What to run without the game

**In the repo** (`test/tw_*/run.ps1`, x86 and amd64): `tw_depth_levels`, `tw_device_loss`, `tw_fg_initialization`, `tw_game_gi`, `tw_gpu_completion`, `tw_helper_lifetime`, `tw_motion_record`, `tw_native_hook_lifetime`, `tw_particle_history`, `tw_preset_writer`, `tw_psychov35`, `tw_sampler_lifetime`, `tw_screen_space_shadows`, `tw_shadow_map_size`, `tw_startup_worker`, `tw_texture_lod_bias`, `tw_unit_pack_rounds`. Run each suite a change touches; a new behaviour gets a case.

**In the maintainer's `tools/tw`** (outside the repo):

| Tool | Proves | Doesn't prove |
|---|---|---|
| `all_modes.sh` and `rt_harness` | The bridge, the helper, part of the D3D11 capture; stress per family (`far_cards_20`, `far_cards_48`, `leaf_storm`, `removal`, `switching_removal`, `release_*`, `bounces_*`) | A game's own passes, real melee, burning houses, cloth; the helper's CPU cost at game scale |
| `fg_child/fg_bridge_test` | FG per backend at 2x and 6x, colour-space changes, memory phases, the module list with a ReShade `dxgi.dll` beside the helper | The game's pacing |
| `warp_tests/*` | A game shader's bytes against ours on WARP: patches, replacements, stream output, level chains, scale copies | GPU hangs (WARP never hangs), real scene content |
| `page_dump` | An addon's settings as data with ImGui and ReShade faked: rows, what shows in every state, what buttons and method sliders write; `games_compare.py` puts two games' (or versions') rows, defaults and a button's result side by side | Whether the hook behind a row runs; rows behind install flags the dump doesn't set (`dump_w` sets only the scene table: sprite and far-tree rows and WH3's torch rows don't show); a new game needs its own `dump_x.cpp` |
| `fxc //nologo //dumpbin x.cso` (Git Bash doubles the slashes), the decompiler dump with `names.tsv` | What a game's own shader reads and tests: a material byte's bits, slots, `SV_Position` at o0 | The values at run time |
| `sun_attila/make_sun_patch_test.py`, `warp_tests/sun_wh3` | The sun patch lands on the sun term; the ambient formula | - |
| `scale_audit.py`, `scale_scan.py`, `wh3_batch5/p9_ndc_trace.py` | Candidates for below-100% bugs | Nothing passes by itself: read each hit |
| Census scripts (`wh3_batch3/b3_cull.py`, `wh3_batch3/b5_census.py`, cut-out and proxy scans) | Families, cut-outs, culling laws from traces | Draws no trace holds |
| [cb_audit.py](../scripts/cb_audit.py), [scan_register.py](../scripts/scan_register.py) | Decompiler constants; dropped `Register` lambdas | A wrong member of the right buffer |

Rules:

- Compare `all_modes.sh` runs by mode name against the last log, not by count. Run it in the background beside compiling, not beside another GPU job, and don't copy a helper into its folder until it ends.
- A pass counts only with a "ready" or "applied" line: a build that failed PSO creation once "passed". A run without a new log is invalid (an old log was once copied, a second game failed silently).
- Give WARP tests the game's real ranges, and a control that must fail.
- Say in the report what the harness can't show.

## 2. Game runs

- Permission and backups: the Game runs rule in [SKILL.md](../SKILL.md). It covers the scripts that start a game too (`run_game`, `soak`, `fg_run`, `fg_mem_test`, `shot_run3`, `install`, `overlay_test`, `profile_run2`).
- Run on the maintainer's current ini and preset (the proof levels in [SKILL.md](../SKILL.md)); a "0 of 8" once held only for one ini.
- Never two GPU jobs at once.
- **Driving a run**: `test_scripts/run_game.ps1` (`-Game`, `-Shot s`, `-Profile s`, `-Set "s:key=value;..."`, `-Env`, `-Keys "W:6.4"`, `-Campaign`) sets `RENODX_TW_SHOT`, `RENODX_TW_PROFILE`, `RENODX_TW_SET` (`twattila/test_hooks.hpp`) and closes the game through its window; `wh3_batch3/check_run.py` proves the run ran.
- Steam started elevated (the maintainer's SteamAdminStart task) passes that to the game and blocks input: `(Get-Process steam).Path` empty means elevated; `steam.exe -shutdown`, then `-silent`; don't touch the task.
- Start forms: `game_startup_mode replay "<full path>"` (WH3 crashes on a bare name), `campaign_load "<save>"`, `battle_benchmark "<xml>"` (WH3). Try them before building a plan on one.
- The games read the mouse through DirectInput: synthetic clicks never land. Keys by SendInput scan codes after `SetForegroundWindow`; settings through `ReShade.ini` or `RENODX_TW_SET`.
- Delete `ReShade.log` before waiting on a line (the old log holds it); in WH3 wait for a new "First run: sun light draw", not for the helper.
- Replays end at their own length, then a still results scene: a 20-minute soak held 6 minutes of live battle.
- A plan's list of what test material lacks is a guess until the game's packs are read: WH3's benchmark XMLs held a burning siege.

## 3. The maintainer's test list

- 10 lines or fewer, the most important first.
- Grouped by scene, so one start per scene.
- Each line: scene; exact row name and value; what to look for; which file to send.
- First line a control: rows off looks like the last version.
- On and off inside one run: pause freezes the picture, and settings changed while paused show no change; each paused run freezes a random wind phase.
- Every Experimental row gets one A/B line naming the decision it drives.
- Prefer what the build reports itself: "applied" lines, Record Ray Tracing Profile (30 s), Frame Trace, Time Each Pass, a pixel trace. Then the maintainer only sends `ReShade.log`, the helper log and the ini.
- Dry-run each line, and every agent run's plan, against a page dump of the maintainer's current preset: the row shows, the debug view is made (an AO view without the AO row is magenta), the key exists in this game, and every log line the plan relies on exists in the build (a helper once didn't log placed instances).
- Never ask what an offline check or an allowed agent run can show; never repeat an unchanged item.
- A longer scripted list belongs to agent runs only.

## 4. Reading the maintainer's evidence

- Copy each set of logs, shots and inis into the round's folder in `tools/tw` before reading, named by run.
- A crash or hang: the game's `crash_report` folder first, then the device-end line, then event 153 ([helper-fg-stability.md](helper-fg-stability.md)).
- A profile: segment it by its setting-change log lines before reading numbers; a profile's "states" were once the maintainer stepping through Upscaling Mode.
- A paused pair 30 s apart differed 4 to 5% around the units with nothing changed (banners fade after the pause starts): run the control before blaming a row.
- A brighter picture with rays multiplying is not the rays: one effect at a time, the other shadow method off. RT Shadows on turns Bend off.
- Plant Wind Sway above 1 pulses 13 to 38% of the field dark once a second (Attila only): keep 1 when judging shadows.
- Slow frames report GPU times late: read the last status line (`last_status.py`).

## 5. Profiling

- PresentMon (GPU busy) first: a CPU-bound scene hides GPU savings.
- The helper's GPU time from per-process GPU Engine counters.
- Record Ray Tracing Profile for 30 s with ReShade's menu closed (FG is off under the menu); read `answer_wait_ms`, `present_pacing_ms`, `cpu_record_parts`.
- Time Each Pass for one frame; xperf for CPU; A/B inside one run (`RENODX_TW_SET`).

## 6. Other traps

| Trap | How to avoid |
|---|---|
| Reports without a check | Numbers from the run, else "not checked" |
| A fix proves half | Keep the repro after the first fix |
| Debug layer | Needs Graphics Tools; deterministic for lifetime bugs (`RENODX_TW_D3D12_DEBUG`) |
| A lambda with `CHECK` must return int | Exit 3 with no output otherwise |
| Script corruption; a German decimal comma in `-f` | InvariantCulture; read before running |
| An own compute pass in a replaced pass's `on_draw` runs before the mod's push: the settings register holds what the last replaced pass pushed | Bind the settings at the register `shared.h` names; write one value of it out to check it arrives |
| A plan built on one start form or one trace | Two forms or two traces before planning |
| PowerShell `-Filter "*.addon32.*"` matches plain `.addon32` files too | Never delete by it; a dead game process pins the addon: rename aside |
