---
name: tw-port-checklist
description: "RenoDX checklist for porting features between Total War (TW) addons, mainly from Attila (src/games/twattila) to WH3 (src/games/twwarhammer3) and the other TW games: the dlaa_helper process and bridge, DLSS, FSR, XeSS, Ray Reconstruction, frame generation, Reflex, RT capture and scene tables, render scale, replaced shaders, the graphics page and presets. Use when porting, reviewing or debugging a TW feature, adding a hash or a new row, or editing shared code under twattila or twrome2."
argument-hint: "target game folder, feature or phase, and which Attila code it comes from"
---

# Total War Port Checklist

Bring a feature from one Total War addon to another without repeating what it cost the first time. Attila carries most of the shared code (`src/games/twattila`, `src/games/twrome2`); other TW addons compile it directly.

Before editing C++, read `AGENTS.md`, `src/games/AGENTS.md` and `docs/CXX_STYLE.md`. This skill adds what those can't know.

## Related skills and scope

| Need | Use |
|---|---|
| Porting or checking a TW feature, its capture, helper use, rows | This skill and [references/lessons.md](references/lessons.md) |
| A new game folder, DevKit, build targets, links | `setup-game-mod-dev` |
| Proving where the output goes, swapchain and proxy state | `swapchain-resource-analysis` |
| Tone mapper and LUT math in a proven pass | `handle-sdr-tonemap-lut` |

## Before you start

1. `git status` and `git log`: an earlier attempt may have got further. Continue it, don't redo it.
2. Read the rows of [references/lessons.md](references/lessons.md) for the subsystem you touch. Each row is a mistake already made once: how to avoid it, how to check it without the game.
3. Read the target game's own shader, buffer and trace. **Never carry over the source game's values, registers, slots, formulas, encodings or texture slots unread.** "Same as Attila" needs file and symbol for both games.
4. Ask of the feature: does the game already do this? The same effect computed twice is the most common waste (see lessons section 3).

## Proof levels for a game fact

Everything a port assumes about a game is one of these. Note the level in the commit.

| Level | What was seen | What may ship |
|---|---|---|
| A | Pass seen in a frame trace, and its bytes read | Ships normally; still off by default if it changes the picture when wrong |
| B | In the shader dump with bytes read, not in any trace | Off by default, Experimental, a line on the in-game test list |
| C | Needs a value or hash nobody has seen | Left out, row hidden, a line on the test list |

A hash goes in only with its proof in the commit message: the shader's family name, the bytes read, the trace line if any.

**Stop a piece when** a hash isn't in the dump, the fix needs a guessed offset or register, two attempts failed at the same offline check, the piece has eaten more than twice its size with no green check, or shared code would change another game's behaviour without a parameter. Then revert only that piece, hide its row, leave a `NOT DONE: what, why, smallest step` line in `HANDOFF.md`, and go on to the next piece. With a feature unproven, the game must behave exactly as without it.

## Checks by kind of change

**Shared code** (anything under `twattila/` or `twrome2/` that another addon includes): build every addon that includes it, 32-bit too. Attila, Rome II and Thrones are only meaningful from `build32`. After a shared edit:

```text
cmake --build build --config Release --target renodx-twtroy.addon64 renodx-twpharaohdynasties.addon64 renodx-twwarhammer.addon64 renodx-twwarhammer2.addon64 renodx-twwarhammer3.addon64 renodx-twshogun2completeedition.addon64 renodx-twshogun2completeedition_dlss.addon64 renodx-rysesonofrome.addon64 renodx-rysesonofrome_dlss.addon64 renodx-expeditionsrome_dlss.addon64 renodx-talesofarise_dlss.addon64
cmake --build build32 --config Release --target renodx-twattila.addon32 renodx-twrome2.addon32 renodx-twthronesofbritannia.addon32
```

A behaviour one game needs goes behind a parameter that defaults to today's value.

**Helper or `dlaa_bridge.h`**: build into a new `build\tw_dlaa_helperN` with `src\games\twattila\dlaa_helper\build.ps1`, never over an old one. Addon and helper ship together.

**New replacement shader**: run [scripts/cb_audit.py](scripts/cb_audit.py) with `python -I`, naming the game (`cb_audit.py twwarhammer3`), and read each line of part 2. 3Dmigoto can name the wrong constant, and a lost constant may only show on one effect. The script reads the decompiler's pack under `bin/`, which isn't in the repo.

**New `command_action::Register`**: without callback data use `[]<typename Context>`; a `<typename T, typename Context>` lambda is dropped silently. [scripts/scan_register.py](scripts/scan_register.py) must print nothing. Add a one-shot "applied" log line, since a hook that never ran looks like a hook that did nothing. A `.bypass` nearby needs its state back in `finalize_callback`.

**New callback on the sun draws**: say where in the run order it lands and why. Registration order is run order.

**New rows**: check the key and label set; hidden when without effect in this game, and a shared row made visible by a new table entry counts as a new row. Tooltips are short, for players, name nothing the game lacks. Keys stay when rows move, or saved values are lost.

**Look**: scale quality, distance and resolution, not the developers' look; a look change is an Experimental row. No per-frame live counters. No temporal dither on coverage, since DLSS smears it. No flicker fix that dims or compresses highlights: fix the source. Licence text for anything shipped, and no code from mods without a licence.

**Patch scripts**: written to a file, not a heredoc (backslashes collapse, raw NUL bytes end up in sources). Text searches treat a file with a NUL as binary and skip it.

## Testing without the game

The harness and test scripts live outside the repo, in the maintainer's `tools/tw` folder: `all_modes.sh`, `rt_harness`, `fg_child/fg_bridge_test`, `sun_attila/make_sun_patch_test.py`, `warp_tests`, `page_dump` (an addon's settings as data with ImGui and ReShade faked: rows, what shows in every state, what buttons and method sliders write; diff before against after for any page change).

- Compare `all_modes.sh` runs by mode name against the last log, not by count. Run it in the background beside compiling, not beside another GPU job, and don't copy a helper into its folder until it ends.
- The harness covers the bridge and the helper, and only part of the D3D11 capture. Say so in the report: it says nothing about a game's own passes.
- A pass counts only with a "ready" or "applied" line. A run that left no new log is invalid.
- Games start only when the maintainer asks. So do the scripts that start one (`run_game`, `soak`, `fg_run`, `fg_mem_test`, `shot_run3`, `install`, `overlay_test`, `profile_run2`).
- What only the game can prove goes on the test list as: what to do, what to look for.

## Finishing a round

1. Ask the double-work question again: is anything now computed twice?
2. List the source game's code you compared, by file and symbol.
3. For a bug found on the way: is it compiled from shared code (fixed for all, rebuild), or the same pattern in `twattila/` (fix there and in the addons that carry it, with a README line each)? Check the source game without the game: `build32`, harness, WARP or offline test, numeric identity on a refactor, key diff on the page.
4. One commit per item with cause and numbers, pushed to the fork only. Tick the checklist in `HANDOFF.md` in the same commit.
5. Report per game: done, not done, what the maintainer tests. State what wasn't run.
