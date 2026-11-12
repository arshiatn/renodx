# Closing a round

## 1. Before the report

1. The double-work question: is anything now computed twice? Update the table in [ao-gi.md](ao-gi.md).
2. The source game's code compared, by file and symbol.
3. Bugs found on the way: shared code (fixed for all, rebuild) or the same pattern in `twattila/` (fix there and in every addon that carries it, a README line each). Check the source game without the game: `build32`, harness, WARP, numeric identity on a refactor, a key diff on the page.
4. Every consumer built: every target in `SKILL.md`'s build lines, x64 and `build32`; every `test/tw_*` the change touches on x86 and amd64; `all_modes.sh` by name after a helper change.
5. One independent review of the round's diff, then one agent for its fixes. Every close so far found real bugs (5, 6, 5 and 9).
6. Page dumps before and after: only the intended rows, labels, tooltips and the date differ.

## 2. Release and install

- Version the addon and the README; a README line per change, `New:` and `Fixed:` first words in the players' notes.
- Release builds are stamped by the maintainer's private step. Never rebuild after stamping: install and zip the stamped copies.
- `release_zip.py` names the helper folder; Attila players get the slim zip from `player_zip.py`. Compare the new zip's entries with the last one (a licence file once dropped out).
- Install only when the maintainer asked for it, with the game closed and the backups of the Game runs rule in [SKILL.md](../SKILL.md); after, the installed files' hashes equal the stamped copies'. Its Builds and Git rules hold here too.

## 3. Report and hand-off

- Commits and the `HANDOFF.md` checklist as in [SKILL.md](../SKILL.md)'s Process and Git rules.
- Report per game: done, not done, what the maintainer tests (in the format of [testing.md](testing.md)). State what wasn't run, built or installed.
- Each bug the maintainer finds in game becomes a row in the right reference file, with the offline check that would have caught it. A row that turned out wrong is fixed or deleted in the same commit.
