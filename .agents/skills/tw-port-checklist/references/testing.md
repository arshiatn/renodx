# Testing: offline, game runs, the maintainer's list

## 1. What to run without the game

**In the repo** (`test/tw_*/run.ps1`, x86 and amd64): `tw_depth_levels`, `tw_device_loss`, `tw_fg_initialization`, `tw_game_gi`, `tw_gpu_completion`, `tw_helper_lifetime`, `tw_indirect_counts`, `tw_kept_buffers`, `tw_motion_record`, `tw_native_context`, `tw_native_hook_lifetime`, `tw_particle_history`, `tw_preset_writer`, `tw_psychov35`, `tw_sampler_lifetime`, `tw_screen_space_shadows`, `tw_scrub_lods`, `tw_shadow_map_size`, `tw_startup_worker`, `tw_texture_lod_bias`, `tw_unit_pack_rounds`, `tw_unit_shadows`. Run each suite a change touches; a new behaviour gets a case.

**In the maintainer's `tools/tw`** (outside the repo):

| Tool | Proves | Doesn't prove |
|---|---|---|
| `all_modes.sh` and `rt_harness` | The bridge, the helper, part of the D3D11 capture; stress per family (`far_cards_20`, `far_cards_48`, `leaf_storm`, `removal`, `switching_removal`, `release_*`, `bounces_*`); GI of no view (`pedestal_gi`; `HARNESS_PEDESTAL_TIMES` prints the bounce's GPU ms, slow frames named); a torch's bit near its own pole (`HARNESS_TORCH` with `HARNESS_LIGHT_FLICKER`: changed bits, isolated and steady dark spots); RR asked in a folder without nvngx_dlssd.dll (`HARNESS_RR_FOLLOW`: the trace's RT_RECONSTRUCTED from `bridge.reconstructing`). The harness's guard-wrapped copies (`all_modes_v64.sh`, `torch_v64.sh`) stop when a TW game starts | A game's own passes, real melee, burning houses, cloth; the helper's CPU cost at game scale |
| `fg_child/fg_bridge_test` | FG per backend at 2x and 6x, colour-space changes, memory phases, the module list with a ReShade `dxgi.dll` beside the helper | The game's pacing |
| `warp_tests/*` | A game shader's bytes against ours on WARP: patches, replacements, stream output, level chains, scale copies | GPU hangs (WARP never hangs), real scene content |
| `page_dump` | An addon's settings as data with ImGui and ReShade faked: rows, what shows in every state, what buttons and method sliders write; `games_compare.py` puts two games' (or versions') rows, defaults and a button's result side by side | Whether the hook behind a row runs; rows behind install flags the dump doesn't set (`dump_w` sets only the scene table: sprite and far-tree rows and WH3's torch rows don't show); a new game needs its own `dump_x.cpp` |
| `fxc //nologo //dumpbin x.cso` (Git Bash doubles the slashes), the decompiler dump with `names.tsv` | What a game's own shader reads and tests: a material byte's bits, slots, `SV_Position` at o0 | The values at run time |
| `sun_attila/make_sun_patch_test.py`, `warp_tests/sun_wh3` | The sun patch lands on the sun term; the ambient formula | - |
| `scale_audit.py`, `scale_scan.py`, `wh3_batch5/p9_ndc_trace.py` | Candidates for below-100% bugs | Nothing passes by itself: read each hit |
| Census scripts (`wh3_batch3/b3_cull.py`, `wh3_batch3/b5_census.py`, cut-out and proxy scans) | Families, cut-outs, culling laws from traces | Draws no trace holds |
| [cb_audit.py](../scripts/cb_audit.py), [scan_register.py](../scripts/scan_register.py), [static_checks.py](../scripts/static_checks.py) | Decompiler constants; dropped `Register` lambdas; a host whose tone mapper never draws the Debug View; `&x` into a `ComPtr<T>*` callee that reads it | A wrong member of the right buffer; a hook that runs and draws wrong |
| `check_logs.py`, `install/tw_install.py`, `ngx_dlls.py` | Known-bad lines in a run's logs; an install of an old build, into a folder without NVIDIA's three DLLs, over a file that is no stamped copy of ours; a zip's helper without the three | What no log line says; anything at run time |

Rules:

- Compare `all_modes.sh` runs by mode name against the last log, not by count. Run it in the background beside compiling, not beside another GPU job, and don't copy a helper into its folder until it ends.
- A pass counts only with a "ready" or "applied" line: a build that failed PSO creation once "passed". A run without a new log is invalid (an old log was once copied, a second game failed silently).
- Give WARP tests the game's real ranges, and a control that must fail.
- Scale a WARP formula check's tolerance with the largest summed term, not a fixed epsilon [f306ff2d].
- A texture read checked against a game PS's pixel: bilinear weights have 8 bits an axis and the PS's inputs come interpolated (a constant 1-2 ulps off), so a smooth texture may differ by 2/256 of its largest texel step times the decode's slope. For a 4-ulp check give the texture plateaus where it is read [df95f06e].
- WARP's bilinear weights at one texel coordinate differ with the texture's size: a picture-in-a-bigger-texture scale test at a share that isn't a power of two (33%) with reads off the texel centres (a distortion) misses by a weight step even with the coordinates 7.6e-6 texels apart (Pharaoh's SSR apply: 2,112 values). Check such a read with a point sampler and its coordinates with a probe (`warp_tests/pharaoh/apply_share_v62`) [7224b347].
- The frame trace logs every draw and dispatch in its own callback, also the ones another callback bypasses: a skipped chain still shows. Prove a skip by the add-on's skip-path line. Its options `blend_hashes`, `vertex_input_hashes`, `instances_apart` (`twattila/frame_trace.hpp`, 7e945569) give a PS's blend desc, a VS's buffer slots with strides and element counts, and index x instance counts apart in one run.
- Pack searches: `mmap.find` on literal names takes minutes over 75 GB; a regex with a `{0,120}` prefix ran 30 minutes with no output.
- The helper's status line's GPU ms are one request's (the last answered), not a mean, and the "Ray tracing with" line comes 3 s after a settings change: read high values against the settings changes around them (a 19-40 ms bounce "spike" was a held Rays setting between two changes). Means and parts come from Record Ray Tracing Profile (`tw_rt_profile_helper.csv`).
- Say in the report what the harness can't show.

## 2. Game runs

- Permission and backups: the Game runs rule in [SKILL.md](../SKILL.md). It covers the scripts that start a game too (`run_game`, `soak`, `fg_run`, `fg_mem_test`, `shot_run3`, `install`, `overlay_test`, `profile_run2`).
- Run on the maintainer's current ini and preset (the proof levels in [SKILL.md](../SKILL.md)); a "0 of 8" once held only for one ini.
- Never two GPU jobs at once. Before each start, Committed Bytes below about 60 GB (dead processes from resets hold theirs until a reboot, [helper-fg-stability.md](helper-fg-stability.md)).
- The game folder holds plain stamped copies before a run: Pharaoh's addon and a DevKit were symlinks into `build/Release` (as of 2026-11), so any build would have swapped the addon mid-run and the DevKit broke the no-DevKit rule. `ls -l` the folder first.
- **Driving a run**: `test_scripts/run_game.ps1` (`-Game`, `-Shot s`, `-Profile s`, `-Set "s:key=value;..."`, `-Env`, `-Keys "W:6.4"`, `-Campaign`) sets `RENODX_TW_SHOT`, `RENODX_TW_PROFILE`, `RENODX_TW_SET` (`twattila/test_hooks.hpp`) and closes the game through its window; `wh3_batch3/check_run.py` proves the run ran.
- Steam started elevated (the maintainer's SteamAdminStart task) passes that to the game and blocks input: `(Get-Process steam).Path` empty means elevated; `steam.exe -shutdown`, then `-silent`; don't touch the task.
- Start forms: `game_startup_mode replay "<full path>"` (WH3 crashes on a bare name), `campaign_load "<save>"`, `battle_benchmark "<xml>"` (WH3). Try them before building a plan on one.
- A guessed start argument can crash the game before any frame: Pharaoh's `game_startup_mode campaign <benchmark>` did (0xC0000005); its help lists `battle|campaign|... <xml>` and no replay or campaign_load. One try per guessed form, and say it was a guess. A plain `game_startup_mode campaign` crashed the same way (both a null read at Pharaoh.exe+0x47C81A, read from the crash_report minidump's exception stream): Pharaoh's campaign is reached only from the menu (Load), so campaign traces need the maintainer's load and the `renodx_trace_now` marker.
- Rome II crashes before any frame from the command line, also without the add-on: `game_startup_mode battle <xml>` at Empire.Retail.dll+0x276899, `campaign_load <save>` at +0x103661. Its traces, like Pharaoh's campaign's, come from the maintainer's own menu starts while `rome2/atn_trace_watch.ps1` (`-Game`, `-Process`, `-Runs` for other games) drops the marker every few seconds and moves each trace into a run folder.
- A desktop capture (`CopyFromScreen`) of an HDR flip-model game window returned one stale frame (Pharaoh's loading screen, three shots alike): take pictures with ReShade's screenshot or `RENODX_TW_SHOT`. ReShade's screenshot key is each ini's `KeyScreenshot`, a virtual-key code (44 Print Screen; Rome II and WH2 112, F1; Attila, Pharaoh and WH3 6, a mouse button, as of 2026-11): read it before asking for shots.
- After an unclean exit Rome II's mod manager asks about the mod pack (window class `mod_question_dlg`), and SendInput's Enter doesn't answer it: close every start cleanly (CloseMainWindow, kill only after 60 s); a run script detects the class and stops at once (`rome2_data_run.ps1`).
- Fullscreen Pharaoh draws no frame while another window is in front ("No frame for 20 s"), and the app keeps the foreground: run windowed at full size (`gfx_fullscreen false`, restored from that run's backup).
- `RENODX_TW_SET` is read at launch; its timed changes fire only while frames are drawn and pile up otherwise. Time them from the battle's first-frames line, and place each trace marker after the log shows the value set.
- In-game MSAA turns the addon's AA off, and the frame trace hangs off the AA frame: an MSAA start gives no trace until the trace has its own path. With it on, DLSS, the rays and FG stay off too (Rome II round 2): an install or a run that needs them sets the game's AA preference off (`gfx_aa 0`) only with the maintainer's OK, from a fresh backup.
- A game exe with the "Run as administrator" compatibility flag (`RUNASADMIN` under `AppCompatFlags\Layers`, HKCU or HKLM) raises a UAC prompt no agent can answer: Rome Remastered's start failed "canceled by the user" (2026-11-14). Read both keys for the exe while planning a run; only the maintainer may clear the flag or start the game.
- Crashed starts leave `Get-Process` entries with 0 threads until a reboot (six Rome2.exe after the command-line crashes): count only processes with threads, as `tw_install.py` and `atn_trace_watch.ps1` do.
- An agent's data run (start, traces, logs, quit) runs at medium effort: it collects; the reading stays offline.
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
- A unit or grass row's cost shows only with the units or the grass on screen: Unit Detail A/Bs in a replay whose camera held few soldiers changed nothing. Name the scene the maintainer measured (their heavy 8x battle), the camera on the units.
- Prefer what the build reports itself: "applied" lines, Record Ray Tracing Profile (30 s), Frame Trace, Time Each Pass, a pixel trace. Then the maintainer only sends `ReShade.log`, the helper log and the ini.
- Dry-run each line, and every agent run's plan, against a page dump of the maintainer's current preset: the row shows, the debug view is made (an AO view without the AO row is magenta), the key exists in this game, and every log line the plan relies on exists in the build (a helper once didn't log placed instances).
- Never ask what an offline check or an allowed agent run can show; never repeat an unchanged item.
- A longer scripted list belongs to agent runs only.

## 4. Reading the maintainer's evidence

- Copy each set of logs, shots and inis into the round's folder in `tools/tw` before reading, named by run. Then `python -I tools/tw/check_logs.py <folder>` (the data-run scripts and `atn_trace_watch.ps1` print it at their end): known-bad lines in `ReShade.log`, the helper's log and the run's notes (RR unavailable, "no main camera" repeating, a `RENODX_TW_SET` key not applied, a trace not taken, exceptions, removed devices, NOT RUN, a game that ended by itself), exit 1 on any. Pharaoh's run B61 held "Ray Reconstruction unavailable" at 16:28; it was read at 19:55.
- A crash or hang: the game's `crash_report` folder first, then the device-end line, then event 153 ([helper-fg-stability.md](helper-fg-stability.md)).
- A profile: segment it by its setting-change log lines before reading numbers; a profile's "states" were once the maintainer stepping through Upscaling Mode.
- A paused pair 30 s apart differed 4 to 5% around the units with nothing changed (banners fade after the pause starts): run the control before blaming a row.
- A brighter picture with rays multiplying is not the rays: one effect at a time, the other shadow method off. RT Shadows on turns Bend off.
- Plant Wind Sway above 1 pulses 13 to 38% of the field dark once a second (Attila only): keep 1 when judging shadows.
- Slow frames report GPU times late: read the last status line (`last_status.py`).

## 5. Profiling

- PresentMon CPU busy and GPU wait before any CPU or GPU work (facts in [helper-fg-stability.md](helper-fg-stability.md) section 5).
- The helper's GPU time from per-process GPU Engine counters.
- Record Ray Tracing Profile for 30 s with ReShade's menu closed (FG is off under the menu); read `answer_wait_ms`, `present_pacing_ms`, `cpu_record_parts`.
- Time Each Pass for one frame; A/B inside one run (`RENODX_TW_SET`).
- A CPU question needs a sampled profile, a GPU question one with GPU events: `wpr -start CPU -filemode` (GPU: `-start GPU -start CPU`), then `wpr -stop <file>.etl`, in the maintainer's admin terminal; read it with xperf or WPA (`Windows Kits\10\Windows Performance Toolkit`) by module offsets. PresentMon alone guesses.
- An A/B measures a change only where its code is on the critical path: heapfix's sort, 6-7x faster, changed nothing in a GPU-bound replay (54.45 against 54.35 fps) and was 3.6% slower with 4 threads in a CPU-bound one, where the render thread never waited on it. Find the waiting thread in a profile before building the A/B.

## 6. Other traps

| Trap | How to avoid |
|---|---|
| Reports without a check | Numbers from the run, else "not checked" |
| A fix proves half | Keep the repro after the first fix |
| Debug layer | Needs Graphics Tools; deterministic for lifetime bugs (`RENODX_TW_D3D12_DEBUG`) |
| A lambda with `CHECK` must return int | Exit 3 with no output otherwise |
| Script corruption; a German decimal comma in `-f` | InvariantCulture; read before running |
| PowerShell variable names ignore case: `$start` overwrote a `[string]$Start` parameter (rome2_data_run.ps1, then again romeremastered_data_run.ps1 with this row in place) | One spelling per name in a script; no local shadowing a parameter. `tools/tw/ps_param_shadow.ps1 <scripts>` lists every assignment to a parameter under another spelling (exit 1); run it on every new run script before its first start |
| An own compute pass in a replaced pass's `on_draw` runs before the mod's push: the settings register holds what the last replaced pass pushed | Bind the settings at the register `shared.h` names; write one value of it out to check it arrives |
| A plan built on one start form or one trace | Two forms or two traces before planning |
| A WARP sun test's comparison sampler on an RGBA32F shadow map returned 0 everywhere | Shadow maps as R32_FLOAT (the game's D32 view), `sun_scale_v61` |
| PowerShell `-Filter "*.addon32.*"` matches plain `.addon32` files too | Never delete by it; old files are renamed aside ([release.md](release.md)) |
| Bitdefender quarantined freshly built unsigned test exes (mingw builds, Gen:Variant.Draftor) | A new test goes into an existing `test/tw_*` suite or the harness, not a new exe; a flagged exe: stop building it, never evade or exclude, tell the maintainer |
