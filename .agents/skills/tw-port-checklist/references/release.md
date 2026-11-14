# Closing a round

## 1. Before the report

1. The double-work question: is anything now computed twice? Update the table in [ao-gi.md](ao-gi.md).
2. The source game's code compared, by file and symbol.
3. Bugs found on the way: shared code (fixed for all, rebuild) or the same pattern in `twattila/` (fix there and in every addon that carries it, a README line each). Check the source game without the game: `build32`, harness, WARP, numeric identity on a refactor, a key diff on the page.
4. Every consumer built: every target in `SKILL.md`'s build lines, x64 and `build32`; every `test/tw_*` the change touches on x86 and amd64; `all_modes.sh` by name after a helper change.
5. One independent review of the merged round's diff on the normal model, then one agent for its fixes (`game-port-method`'s Agents and tools: Fable only when the maintainer asks). Every close so far found real bugs (5, 6, 5 and 9). Once over the merged tree at the end, not per merge (each merge's review cost more than it found).
6. Page dumps before and after: only the intended rows, labels, tooltips and the date differ.
7. Parity: [parity_check.py](../scripts/parity_check.py) over every TW game prints no open row (the Nothing dropped rules in `game-port-method` and [SKILL.md](../SKILL.md)); a row it prints is done now, or gets its ledger status and proof, or a `HANDOFF.md` checkbox with an owner.
8. Every agent report of the round read to its end: each not-done, later, listed or found line is a checkbox with an owner before the report goes to the maintainer.
9. The HDR-only snapshot check for every HDR add-on released: the checklist in `game-port-method`'s [hdr-only-snapshot.md](../../game-port-method/references/hdr-only-snapshot.md). TW's tools for it: the add-on's target built alone (step 4's lines name each); a page dump from `tools/tw/page_dump/dump_any.py <letter> <tag> [tree]`, whose file must start `== helper absent` (items 3 to 5: `== states` for page 1 of 2 and the note, `== buttons` for every preset, `== sliders`, `== first start without hdr`); `test/tw_device_loss` for the output modes. Shogun 2's dump (`page_dump_s2_body.h`) has neither section: read its code for items 2 to 4.

## 2. Release and install

- Version the addon and the README; a README line per change, `New:` and `Fixed:` first words in the players' notes.
- Rebuild every shipped addon and helper at HEAD right before stamping: an add-on embeds its build date, so any rebuild changes its hash, and builds older than the last code commit shipped four times in one day. The install tool below refuses a build older than its sources' last commit (its game's folder, the helper's, every file they include) or with uncommitted changes in them.
- Release builds are stamped (`game-port-method`'s Builds rule): install and zip the stamped copies.
- A helper unchanged this round keeps its earlier stamp: the install tool copies that stamped copy into the new stamped folder itself.
- `release_zip.py` names the helper folder; Attila players get the slim zip from `player_zip.py`. Compare the new zip's entries with the last one (a licence file once dropped out). Both put nvngx_dlss, dlssd and dlssg 310.9.16+ with NVIDIA's licence beside the helper (from the maintainer's Streamline SDK, `tools/tw/ngx_dlls.py`'s `SOURCE`) and refuse a zip without them: twattila_v145 / v146.zip had no nvngx_dlss.dll. `python -I tools/tw/ngx_dlls.py <zip>` checks an existing zip.
- Install only when the maintainer asked for it, with `tools/tw/install/tw_install.py --helper N attila=158 wh3=57 ...` (no flag: a dry run that writes nothing; `--stamp`; `--install`), never a per-round copy. It refuses a running game or helper (dead processes with 0 threads ignored), a game folder without nvngx_dlss, dlssd and dlssg 310.9.16+, a DevKit, a folder file that is no stamped copy of ours; backs up the maintainer's ini, preset and preferences fresh (`game-port-method`'s Game runs rule); renames old files aside (`.in-use-<ts>`), never deletes them (a dead process may pin them); checks the hashes after. Its Builds and Git rules hold here too.

## 3. Report and hand-off

- Commits, the `HANDOFF.md` checklist, the report per game and each in-game bug as a row: `game-port-method`'s Process, Git and Releasing rules. In TW the row goes into the reference file of its area here. A row that turned out wrong is fixed or deleted in the same commit.
