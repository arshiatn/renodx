# Release check: the HDR-only snapshot

The maintainer ships each HDR add-on as a snapshot on the renodx GitHub, which upstream builds alone. The full package (the helper, the DLSS add-on, the tools) is on the maintainer's GitHub and Nexus, linked from the add-on's page. So every HDR add-on must be right in the state upstream ships it: alone. Run this before every release of an HDR add-on, beside the engine's own release steps (TW: `tw-port-checklist`'s `release.md`).

## Checklist

1. **Builds alone** from the repo's normal targets (`cmake --build build --config Release --target renodx-<game>.addon64`, or `build32` and `.addon32` for a 32-bit game) in a checkout with no private tools: nothing under `tools/`, no watermark step, no file outside the repo, no generated header that only a private script writes. List every `../<folder>/` include of the add-on's folder (`grep -rhoE '\.\./[a-z0-9_]+/' src/games/<game> | sort -u`): each folder named goes into the snapshot with it, or the include goes.
2. **Works with the helper and any second add-on absent**: no `renodx-tw-dlaa64.exe` beside it, no `_dlss` add-on, no bridge. Read every path that starts or waits for the helper or reads the partner's shared data or ini keys (`cross_addon` blocks, `[<Game> DLSS]` sections): each must fall back to the game's own behaviour, never crash, never wait. A row that drives a game setting (render scale, an engine value) is gated on what uses the value being able to run.
3. **The page says so**: a red notice that the full download is needed (TW: `FULL_DOWNLOAD_NOTE` when `!dlaa_bridge::HelperInstalled()`), and the About line with GitHub (Downloads) and NexusMods buttons pointing at this game's pages.
4. **An SDR preset** (Rendering Mode SDR, Output SDR) and the **SDR first-start path**: on a player's first start with no saved settings on a display without HDR, the add-on applies the SDR preset (TW: `output_encoding`'s `first_start_without_hdr`).
5. **Every preset button and every Graphics page 1 of 2 row dry-run in a page dump in the helper-absent state**: what each button writes, which rows show in every page state, what each method slider writes; nothing writes a key the add-on lacks, nothing shows a row that needs the helper without its note.
6. **HDR and SDR output modes**: HDR10, scRGB where offered, SDR and Auto each decided right (TW: `test/tw_device_loss`'s output table), the SDR encode matching what the game's pass encoded (a WARP ramp); one in-game line on the test list for each mode the offline checks can't show.

## What a page dump can and can't show

TW page dumps (`tools/tw/page_dump`, outside the repo; [tw-port-checklist's release.md](../../tw-port-checklist/references/release.md) says how to run them) run every state with the helper absent: the dump exe has no `renodx-tw-dlaa64.exe` beside it and starts no bridge. The file's `== helper absent (HelperInstalled 0, bridge started 0)` line proves that state; `PRESENT` there means the file is not the HDR-only state. Its `== first start without hdr` section prints what the first-start SDR path writes, or `none`.

Not shown by any dump, so read in code or put on the test list:

- what a hook does at run time with a value and no helper (item 2's code read);
- a helper-present state (no fake shared memory), so a dump never shows the rows as the full package shows them;
- the swapchain and its output modes (item 6);
- every non-TW add-on (Ryse, Tales of Arise, Expeditions: Rome have no page-dump driver): for them items 3 to 5 are a code read until a driver exists. Shogun 2's HDR part (`page_dump_s2_body.h`) writes both sections since 2026-11-15.
- a global row's value across buttons: the dump's `ResetSettings(false)` keeps global rows, so after the SDR button every later button and `== first start without hdr` print `SwapChainEncoding 3 -> 2` that the code doesn't write (no first start writes Output: `output_encoding` already decided SDR).

As of 2026-11-15 (the snp_ round): all ten TW HDR add-ons have the SDR preset, the first-start SDR path (`output_encoding`'s `first_start_without_hdr`, each its own `ApplySdrPreset`), the full-download note on General (and in the Graphics page's Anti-Aliasing section, Shogun 2 aside: it has no such page) and GitHub (Downloads) in About. Ryse's, Tales of Arise's and Expeditions: Rome's HDR add-ons have none of these: a `HANDOFF.md` item before their next HDR snapshot.
