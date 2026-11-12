# Shaders, HDR output, graphics page, presets

## 1. Replaced shaders and decompiler constants

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| 3Dmigoto names the wrong constant: campaign's haze gone, UI swapped [e06a18da] | Audit every new replacement, re-read from bytecode | [cb_audit.py](../scripts/cb_audit.py) |
| 3Dmigoto drops a two-component operand: `add r0.yz, cb0[0].yyyy, -cb2[0].wwzw` came out with one constant for both (WH3 fog height) | Read decompile against disassembly where a line writes two components | `warp_tests/fog_wh3`: the game's shader against the build on random constants, every bit at the neutral setting; the raw decompile fails it |
| `rN.xy = scalar * ...` loses a component; `dot(c, c)` on a scalar drops the 2 (DOF in 7 games) | `dot(c.xx, c.xx)`, compare | `cb_audit` plus 24 random interpreter runs |
| Generated motion or RT VS: regenerating brings fixed lines back | Fix the decompile input | `cb_audit`'s generated VS part |
| Edits among decompiled lines: `r0.w` written before a line reads `r0.yzw` lost one colour | Keep each register's order; test with the same inputs by another route | WARP: half the rays against half the result |
| A pass whose result is added has no `max(0, ...)` to hide what another pass's clamp hid (0 / 0 in the cubes' share) | Guard the new output for NaN and infinity. fxc strips `isnan` and `ZeroNaN`: test NaN with a bit test | Read the adding pass's last line |
| A replacement needed only with a feature on | Keep the game's bytes when off: no `code` on the entry, CSSetShader ours in `on_draw`, the game's back in `on_drawn` | Default state needs no proof then |
| A pass at half the render's size: `v0.xy` isn't the render pixel, a quarter of the screen is read with no error (WH3's fog light) | Take the pixel as the shader loads its depth: `v0` x the target's inverse size x the screen size | WARP: a ramp at a half-size target; `v0.xy` as control must fail |
| WH1's trees silently wrong in 3Dmigoto | `RT_ASM=labels`, fxc's own disassembly | Compare test |
| A strength on one permutation of a family: others take another route (WH3 fog: half resolution through an upsample, others straight to the picture) | Put it where the traced passes meet; list what no trace shows | `names.tsv` family, each permutation's outputs |
| Hooks that never ran for days: `Register` without data plus `<typename T, typename Context>` [daec47ba] | Without data use `[]<typename Context>`; check an "applied" line | [scan_register.py](../scripts/scan_register.py) |
| `.bypass` skips later callbacks' post callbacks: t51 stayed bound, ambient gone [e5bf7dd3] | State back in `finalize_callback`, or swap plus `.replay` | Read each new `post_callback` under a bypass |
| Registration order is run order; addon order is by name (HDR first). Guides registered after `InstallSunDraws` ran after the trace that reads `guides_written`, so it stayed false | Comment the order at the install | Read the install order against the read |
| A value's writer gated on another feature: `rt_sun` ran only with RT sun shadows, so paths with the game's shadows had no sun | Grep every reader of what you gate | The settings' combinations in the page dump |
| Slider stops 0.99, 1.01, 0.01 fell on the other side of a gate after a constant changed [730dd54b] | Keep gates exact; test each stop | Read |
| Helper shaders named `*.cs_6_N.hlsl` get compiled by the addon's CMake glob | Plain `.hlsl` when they need defines | The build |
| A game's `*.hlsl` that wrap `../twattila/*`: edits reach both games | Remember both, build both | Build |
| Patches run in order: the render scale's patch changed sun shaders before the shadow patch saw them; SSS patched water first; Water Caustics never patched | `view_share::Repatched` carries the game's hash; log "patched" or "not as expected" per patch | WARP patch test (`make_sun_patch_test.py`) |
| A replaced shader loses the render scale's correction: torch light slid | Add both patches to each new replacement | `view_share_test.cpp` (WARP: the patch over a dump's shaders, `VIEW_SHARE_SHOW=<hash>`) |
| An HLSL array in a cbuffer pads each element to 16 bytes | The reflected size equals `sizeof` | Reflection check |
| In-place format upgrades lost clamps (Ryse) | View cloning for new mods; don't retrofit | Read |
| Debug edits put back with `Copy-Item` keep the old file time: the build skips the shader | Set the time after restoring; check the `.cso`'s time; look at the picture before the numbers | - |
| No raw control bytes in sources: a NUL makes `grep` call the file binary | `rg -a` to find them, then fix | - |

## 2. HDR output

Tone-map and LUT math belongs to `handle-sdr-tonemap-lut`; these are the TW-specific facts.

| Mistake | Rule | Check |
|---|---|---|
| The hand-off from WH2 and WH3's per-map Reinhard broke at mid grey | Match its value and its log-log slope at mid grey | Curve plot |
| Per-channel input to the game's SDR curve made white bloom spots | Keep the game's curve as a grade on a max-channel (Neutwo) compressed copy, then undo the scalar (`twattila/tone_mapper.hlsli`) | WARP ramps |
| Attila's bloom high pass caps luminance at 25: a flat halo in HDR | Extend it logarithmically; WH3 has no cap | Read the bloom PS |
| Extended's fire came out flat salmon | Its shoulder rolls off per channel above paper white | Fire in a ramp test |
| A Hue Clip default copied from another game | Default from the OkLab hue error of PsychoV35 against that game's SDR clip (Attila 40, WH3 100); sky blue bends twice as far | `test/tw_psychov35`: ramps never above peak or NaN, other modes 0 pixels different, other shaders the same bytes |
| PsychoV35 shipped as the default | First an Experimental Rendering Mode; Recommended took it (Musa's V35) only after the maintainer's test, now in every HDR addon; no new per-frame CPU | Page dump |
| The SDR preset set PsychoV31 | It sets Rendering Mode SDR | Page dump |
| SDR output re-encoded the gamma 2.0 frame as 2.2: darks brighter | Decode what that game's pass encoded | WARP ramp |
| WH3's lens blood reached 9.5 in FP16 | Clamp additive overlays at paper white | Read the overlay PS |

## 3. Graphics page and presets

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| Keys renamed or removed lose saved values | Page moves and renames only; every setting keeps its key (`graphics_page.hpp`) | Diff keys against the last zip's |
| A hidden row's labels still clamp the saved value; an `EmptyLine` has no `shown`, so a hidden section leaves a stray title | Read `utils/settings.hpp`; hidden DLSS on AMD: places differ from values | Key and label diff; `RENODX_TW_FAKE_VENDOR` |
| A row hidden at a default later changed: its value was already saved | Reset it once on a revision key, not only the default | Old ini through `page_dump` |
| Two ImGui widgets with one id act as one | Unique label; section in the id | Read |
| `Page::Take` matches by key or label: a setting without either isn't found; remaining rows go under the last section | Give them a label | Read |
| `Page::Defaults` puts everything in preset defaults: Shadow Map Size got reset | Keep such keys out | Read |
| A page's Reset reads defaults when the page is built | Set a game's own default before `graphics_page::Apply` | `page_dump`'s buttons list |
| Reset must bring back the game's own effects (SSAO, SSR, water, shadows) | List Reset's changes explicitly | The preset table |
| A preset sets a hidden row: where no hook reads it, it still runs (fog shadows traced for nothing) | Gate the setting on the hook being installed | `SettingsOf` for each key the preset writes |
| A preset or method assumes a replacing effect: RTAO over GT-VBAO where it multiplies | A binding says which; texts say what's true here | Every button in a page dump |
| Presets against method rows | Presets show through; method sliders hold nothing; RT and PT turn RR on only on NVIDIA | Read |
| A new game's RT/PT preset and defaults written by hand drift from Attila's (WH3 kept 2 bounces, no glow, 10 m bounce range) | One list for every game, `graphics_page::RAYS_PRESET` (the maintainer's Attila settings); `UpdateSettings` skips keys a game lacks, so a game's own rows go in it too. A new game takes it as the defaults of the rows the methods leave (WH3's `addon.cpp`); the methods stay off | `games_compare.py` on both games' dumps: after RT/PT only per-game rows (hue, game passes) differ |
| A row that drives a game's setting (e.g. its render scale) | Gate it on what uses the value being able to run (helper up, the pass there to replace): a failed helper or the game's AA off gave the game's own 50% upscale with Full Detail's cost | Page dump with the helper absent |
| Rows without effect shown [599604a6] | Hide by what's installed | `AppendSettings(..., false, false, ...)` |
| Gamma Correction, SDR preset on an SDR monitor at first start [e06a18da, 3dbf1ce6] | Gamma in all presets; a first-start check | Read |
| Add-on running alone [9a909c13, 6875b28f] | Red notice, GitHub button; a missing helper must not crash | The path taken with the helper absent |
| Tooltips with dev notes, trace names, other games; DLSS-only wording once FSR and XeSS exist [43141539] | Short, player-facing | Grep tooltip lines, multi-line text too |
| Per-game hue defaults copied | Ask the maintainer; don't copy Attila's Highlight values | Read |
| Recommended and Reset presets | Recommended in PsychoV35 (`twattila/psycho35_settings.hpp`, the game's own `HUE_CLIP_DEFAULT`); Reset keeps its default where Extended exists, Recommended's mode where it doesn't | Page dump |
| Live counters, temporary timers | One-frame traces only; debug work behind Log Details; remove timers before a zip | Read per-frame paths |
| Status lines lag 3 s | "Test: x set to" is exact | - |
| Credits and licence texts for everything shipped (NRD, SHaRC, OMM, FSR, XeSS, SSSR, CMAA2, DLSS, ACES, PsychoV35) | About rows from `twattila/credits.hpp` in every addon that ships the code; the licence text next to vendored code; the repack adds the licence set to every zip; no code from a mod without a licence | Read the zip |
