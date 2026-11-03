# Total War port lessons

Stable rules from the Attila work, for any TW port. Each row: the mistake, how to avoid it, how to check it without the game. Hashes in brackets are commits with the cause. Files are under `src/games/twattila` unless named. "Harness" means the maintainer's `tools/tw/rt_harness`, outside the repo; `RENODX_TW_*` and `HARNESS_*` are its switches.

## 1. Facts that differ per game

Found when reading WH3 against Attila. Each one looks portable and isn't.

| Assumption | What the code really does | Check |
|---|---|---|
| A scene table line finds leaf UVs itself | `SceneCoverage` runs only for hosts with `scene_draw` (`rt_core.hpp`). A table host must set `.foliage`, `.uv_semantic`, `.uv_semantic_index` by hand, or leaves are solid cards | Read the leaf VS and PS bytes; RT Debug View with Scene Test |
| The clip mask is PS t0's alpha | `rt_core.hpp` reads t0 (t1 for `far_tree`). A game's grass may test another slot, with t0 a virtual-texture page. Read which texture the `discard` tests | Disassemble the PS, read the bound textures in a trace |
| Billboards go in the scene | Clip-space cards built from `cb0[0].xz - position` face the camera, so their shadow turns with the camera | Read the VS basis before listing it |
| The final colour target is sqrt-encoded | Attila's target is; another game's tone mapper may end in gamma 2.2. A prepare pass must decode what that game's pass encoded | Read the last lines of the tone mapper; WARP run with a known ramp |
| Guides registered anywhere are seen | Registration order is run order: guides registered after `InstallSunDraws` run after the trace that reads `guides_written`, so it stays false every frame | Read the install order against the trace's read |
| `InstallShadowsOnly` hides any game's shadow maps | It takes no shader; it learns the map from a square depth texture bound at the sun draw. A game whose sun PS reads a screen mask instead binds none, so it can't work there | Read the sun draw's bindings in a trace |
| `.dynamic = false` limits capture cost | Any draw with a D3D11_USAGE_DYNAMIC vertex buffer is captured each frame regardless | Log Details |
| Any capture list works | `rt_core.hpp` returns on anything but a triangle list, and has no HS / DS handling: tessellated meshes stream nothing | Stream-out test on WARP with the real bytes; a "left out" log line |
| Depth is reversed as in Attila | Read it: TAA's nearest-depth test tells the direction, and it decides inversion flags for FSR, XeSS, FG | TAA CS bytes |
| Tweak names exist in today's exe | Lists rot. Lookup needs a NUL-preceded whole string (`twrome2/engine_tweaks.hpp`) | Scan the exe offline for each name, count what resolves |
| A tweak name that resolves is a value | `Find` also lands on plain words passed to other calls, and on a preference's flag byte instead of its value. Writing there corrupts data or faults. List names from the value constructors' own call sites, not from strings | Replay `Find` offline, check each address against the constructor that built it |
| A factor changes any tweak | A value of 0 stays 0. WH3's shadow splits are 0 and the game then uses a table, so they need `.absolute`. A cap of 0 can also mean "no cap": read the reader's early exit before setting a slider's minimum | Read the reader's branch on the value |
| An at-start test can stay when its names don't exist | `StartAtStart` retries, and the addon's present path waits for it, so a missing name holds every slider back | Grep the list against the replay; a list with no at-start test skips the thread |
| Shown rows have effect | A hidden row's labels still clamp the saved value (`graphics_page.hpp`); an `EmptyLine` has no `shown`, so a hidden section can leave a stray title | Key and label diff; read `utils/settings.hpp` |

## 2. Checks a port tends to miss

1. **Square root for presets L, M and RR.** They flatten darks on linear HDR: feed sqrt, square back (`aa_dlaa_prepare.cs`). Test with the input's real range first (`HARNESS_RR_IMAGE`, `HARNESS_SR=12`): pre-tone-map input isn't post-tone-map input.
2. **HUD-less frame holds everything but UI.** Flares and vignette drawn after the tone mapper belong in it ([f3b2cc91], `OnOutputEffectDrawn` in `anti_aliasing.hpp`). A pass that also runs after the UI must not feed it.
3. **Ray Reconstruction only while something is ray-traced** (`ray_reconstruction && ray_tracing`, `rt_scene.hpp` `Configure`).
4. **`Release(true)` on leaving DLSS,** or the bridge's textures stay.
5. **No sticky "camera found".** Require a camera seen this frame, or a scale change keeps DLSS on with a stale camera and the wrong render size.
6. **`main_view`, `frame_history::render_width` and `tonemapped` must be set by a host running below 100%.** Otherwise `MainViewport` is always true and `RenderSize` the display's, and UI 3D views can enter the scene.
7. **Game's shadow maps under RT shadows** are double work; say so if they can't be skipped.
8. **No raw control bytes in sources.** A NUL makes `grep` call the file binary, and agents go blind (`rg -a`, then fix it).

## 3. Capture and scene

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| One mesh captured twice: depth prefill plus G-buffer by one VS paid the share twice; hidden and shown instances raced in the dedupe (grass flicker) [78c0f619] | Capture only with a colour target bound (foliage with rows and thin only), checked against the game's multi-target passes | Group draws by vs, ps, targets in a frame trace |
| Same effect computed twice: screen-space under its RT replacement, two traces, copied passes | Ask at each round's end | Read the settings' gates; trace |
| Rows drawn under the game's triangle list streamed nothing; meshes placed by stale texels: giant shapes, long lines [28832b16] | Point list for one-vertex-per-instance draws | Harness D3D11 side, read back |
| Scenery re-keyed every frame (instance streams repacked): rebuilt all the time | Key by geometry; BLAS once, rows each draw | "new captures" log, harness |
| First-come shares: units or walls starved by draw order [da53d23a]; capture count ran out at 256 [eae66fca, 64c6938c] | Per-kind shares; log "left out", "without room" | Log Details; harness stress modes |
| Fixed caps dropping kept instances flicker with no error (262,144 TLAS) [6ad8a2c2] | Grow on a worker at 3/4; step x4/3 (5/4 grew twice) [c7ffc569] | `RENODX_TW_TEST_ROOM`, stress |
| Room growing at a battle's start: shadows flicker in the first seconds | Start at half the limit on big GPUs [965c89cd] | Harness |
| Dead entries crowd out live ones: a particle draw holds its whole list, nearly all without opacity | Count only what has opacity [85b0c873] | Log counts |
| Smoke as Mega Geometry clusters: trace 3.6 to 194 ms | Regular sorted BLAS, own TLAS, own ray [6ad8a2c2, 321434ab, 965c89cd] | `HARNESS_SMOKE_STRESS` |
| Past smoke's room, whichever came last gives way: burning houses' shadows pulse | Smallest give way [6875b28f, f8606d9a]; a second lit shader must be found too [2da1e65f] | Harness |
| LOD switch: old copy stays and darkens the new for 16 frames; gaps while moving [6fe88cde, 6902136f] | Drop drawn-out copies at once; place every frame while the camera moves | Harness scene test |
| Dedupe key held a camera-distance shrink: grass doubled while moving | Key on x / z, nothing camera-driven | Harness |
| Terrain captured with the game's camera-distance height push: black circle under the camera [bad338fa] | Capture version without the push. Clip-space captures inherit jitter and any push | Compare RT ground vs depth in the Debug View |
| Scene captures that aren't the world: UI's small 3D views, sky domes blocking every ray | `main_view` gate; dome rule in `rt_core.hpp` | Trace viewports; Log Details |
| Garbage triangles spreading scene bounds: trace 1000 ms | Helper NaNs them, logs the shader | "garbage triangles" log |
| Shadows and light reach only patched sun shaders; forward passes that light themselves stay lit under RT-only | List readers (the log names them), `InstallShadowReaders` | Read the dump: who samples the shadow map |
| Patches run in order: the render scale's patch changed sun shaders before the shadow patch saw them; SSS patched water first, Water Caustics never patched | `view_share::Repatched` carries the game's hash; log "patched" / "not as expected" | WARP patch test (`tools/tw/sun_attila`, `make_sun_patch_test.py`) |
| Replaced shaders lose the render scale's correction: torch light slid | Add both patches to each new replacement | `scale_audit patch_test` |
| Unit cut-outs solid in the scene: black beards and leaves [78c0f619] | List of clipping PS, `RT_SOLID_CUT_OUT` | Dump scan for `discard` |
| Grass forced to 2-state micromaps: whole fields black; OMM-opaque parts never reach the candidate loop | Grass stays 4-state plus alpha; step nearest hits | Harness |
| Cluster templates without a per-triangle geometry index: garbage GeometryIndex | Zero-filled buffer | Harness candidate debug |
| BVH order changing with the same input moves shadows (sway rows packed by thread order) [b11f567c] | Keep order deterministic: a ray past MAX_CANDIDATES depends on it | Harness: frame-to-frame pixel count |
| Rays start wrong: own blade [b11f567c], own surface [8e044106], riverbed for water, 3 mm helmets [6902136f]; terminator facets trust the depth normal [db34e39d] | Per-surface offsets; ask the triangle when edge-on | Harness plate checks |
| Screen-edge pixels: normal from past the view: black first and last columns [5a995075, 88270d2b], border fade cuts reflections [97e0fe87] | Use the neighbour inside; test at the edges | Harness edge modes |
| Campaign world ~1/20 of a battle's: every metre constant wrong (placement cells: jitter) | Scale everything world-sized on the campaign (`PlacementScale`) | Read camera near/far in a trace |
| Nothing marks foliage in stencil or G-buffer | Don't assume; depth copies around the prefill | Trace, dump |
| Foliage fix via an extra target at SV_Target3 wrote nothing, fought kept targets | Don't | - |
| Water refraction read by several shader names | Pass the slots as parameters, find every name | Dump; `OnReflectionDraw`'s t6, t8 are Attila's |

## 4. Memory and lifetime (helper, D3D12, shared handles)

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| `Close` of shared textures without a wait: #921, a real removal at RR to AA Off [00862f09] | Wait on its own fence before any Close | Debug layer `RENODX_TW_D3D12_DEBUG`, `RENODX_TW_TEST_DEVICE_REMOVAL`, `HARNESS_SWITCH` |
| A held removed device blocks every new device in the process; Streamline never lets go | `MakeOwnDevice` (factory, no singleton) for RR, FSR, XeSS; retry each second | `singleton_test`, harness |
| Removed device leaves shared fences at all ones: every wait passes, RR and MFG "broken", fps "better" | New fence set in `dlaa_bridge::Prepare` | `RENODX_TW_TEST_REMOVAL` |
| After a removal, "N failed" log lines are a result, not a cause; "no page fault" rules out nothing; DRED labels can be stale | Read in order; reading freed memory hangs with no fault | Debug layer |
| Trace recorded over the last trace: the real freeze | `WaitFor(trace_value_)` before recording. Anything relying on "the same slot" needs a real wait: cadence changes with settings | Harness; needs sway or bakes on |
| Helper GPU work after the game's signal runs beside the D3D11 frame and hangs | Inside the request's list, bounded per request | Harness bakes stress |
| Request overflow (256 meshes + a bake): a failed Close leaves its slot dead for good | Room for a full request; failure = new scene, 4th turns off | `leaf_storm` mode |
| Idle release freed what was just opened: first RT start after 10 s built twice [23344f8f] | Opening counts as use | `HARNESS_RELEASE` |
| Never stop Streamline: `slShutdown` then RR + RT start hung the helper | `slFreeResources`; `Shutdown1` from idle RR crashes the next evaluation | `fg_bridge_test`, `tw_fg_test` |
| RT off keeps 15 GB; effects off keep textures [625742fc, 80239089] | Idle release 10 s, RT objects 15 s, effects free on off, `Release(true)` on leaving DLSS | Helper log "release" lines; Windows' memory counters |
| Shared texture opened twice in a process stays in VRAM (+195 MiB a RR cycle) | D3D11 opens at the set's first DLSS request | `fg_bridge_test` memory phases |
| Handle never closed: +32 MiB per set | Check every new shared texture's handle | Harness memory per phase |
| Slow API in the first request pushed past the 5 s answer limit | Worker thread | Helper log |
| Root UAVs have no bounds checks | `BUFFER_AT_OFFSET` clamps | Harness |
| 32-bit address space (peak 3158 of 4096 MB) and ReShade's screenshot crash are 32-bit only | Don't take Attila's RAM caps as needed in a 64-bit game | - |
| Addon and bridge layout: arrays grown in place, `OLDEST_VERSION`, padding | Addon and helper ship together, bump; the release zip names the helper folder | Build all addons |
| Shared header change breaks other games silently [9b062cc7, 6f6cf5e2, 8cb71cec] | Build every TW target and 32-bit, Shogun 2, Tales, Expeditions, Ryse after shared edits | The builds |

## 5. DLSS, jitter, motion, render scale

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| L, M, RR flatten darks on linear HDR (always auto-exposed) | Square root in, square out | `HARNESS_RR_IMAGE`, `HARNESS_RR_JITTER`, `HARNESS_SR=12` with the input's darkness (a graded shot is too bright) |
| Jitter or motion sign or scale wrong: looks sharp when still | Prove by pairs, right vs wrong (FSR 0.081 vs 0.18-0.24, XeSS 0.097, DLSS K 0.106). A proof at 100% says nothing below 100% | Harness jitter sign pairs |
| A gate for one feature took another's: TAA and T2x lost jitter for twelve versions [5693b7cb, dfb7bbfd] | After any change around jitter, list every AA method's jitter | Read `anti_aliasing.hpp` per method |
| Render scale active without DLSS running: picture stuck top-left when the helper or files are missing | Gate scale on DLSS really running. A game's own scale slider has no such gate | Read the gate |
| A game's own scale may be top-left in a display-size texture | `Evaluate` and motion scale need the viewport's size, not the texture's; a viewport == screen filter must go, with a new main-view test | Read a trace at 50% |
| Below 100%, bugs by name: scissor left scaled (flare, markers), flare occlusion, grass depth in split uv form (`Kind::SPLIT`), AO blur and water refraction read whole, refraction under three names, SSS tolerances by the render share, SSS far fade on the backdrop, SSS debug view through DLSS, bloom share, depth for markers stretched | Run the same list per game | `scale_audit patch_test`; a trace at 50% |
| Effects at output resolution: what really follows it | Read each | `HANDOFF.md` |
| Motion cost: SO left bound through game's draws stalled ~10 us per unit draw [3ac1b1ad]; second prelude [840c5433]; buffer copies between draws 1.24 ms; synchronous helper round trip 0.25 ms | Keep targets, SO only, no PS, compute at frame end; async requests. Don't port object motion if the game's velocity is real | Toggle within one run |
| Motion "for everything" assumed | Look: which draws write velocity, which don't. Flags, sails, ropes, water, heat haze are the usual gaps | Trace by script; Motion Debug View in game |
| Post-TAA 3D effects draw after DLSS's frame | They stay outside DLSS: their own resolution and depth | Read the trace |
| Camera in a shared constant buffer: `Upload` reads mapped memory at unmap, mutex per 1-2 KB buffer map | Keep hook costs measured | Debug timing in one run, then remove |
| In-game TAA must stay on when DLSS hooks its pass; other AA modes never run that CS | Tooltip and a status line when not evaluated | Read |
| `reset` only on a skipped frame | Attila's checks for cuts | Read |
| RR and reflection guides come from the game's own G-buffer rules, not Attila's | Read that game's dielectric rule (`rr_guides`); `guides_written` true when guides ran | Harness for shape only |
| RR left alone: ~20% of AO noise stays | Raw AO under RR, histories kept [8fff25ae] | `HARNESS_RR` |

## 6. Frame generation

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| Streamline takes no scRGB: restart stopgap, then convert [6de22557] | Auto / HDR10, or the conversion pass (`fg_hdr10`) | `fg_bridge_test` |
| SDR is 10-bit too: "10-bit = HDR10" turned the window red [582065c9] | Send the colour space | `fg_output_runs.sh` needs the game; `sl.log` |
| ReShade screenshots never show the helper's window: a bug only there looks fine | Display capture or the log | - |
| HUD-less must hold everything but UI [f3b2cc91]; taking the tone mapper's output before the final pass counted all pixels as UI | Keep output after the last scene pass; passes that don't run every frame fall back | Read; FG Debug View |
| The swap chain shader's constants aren't at b13 in every game: WH3's `shared.h` has b10, and a HUD-less image drawn with b13 came out black | Read the game's `shared.h`; `frame_generation::Install`'s `injection_slot` | `wh3_fg_keep_test` (WARP, built shaders) |
| Resize: off and on in one frame races Present: stale frames, a zero buffer count | Two presents off | `tw_fg_test` |
| Frozen window when textures no longer fit or open fails; window before a working Present; 60 failed in a row | Hide it, say why [f3b2cc91, 6f4a8ea0] | `fg_bridge_test` |
| Memory: FG off keeps 590-605 MiB under Streamline, +64 MiB per on / off | Free resources | Helper log |
| FG presets = driver profile, not Streamline; read once at load; Global VSync caps it | DRS key on the helper exe | - |
| No status line: nobody knows why no frames | Reason text [78c0f619] | `fg_bridge_test` |
| Frame Rate Limit, Reflex, Dynamic rows shown where they don't apply | Rows per method | Read the page |
| FG's Debug View left on while the menu is open | Off while menu open [c8b5dde6] | Read |

## 7. Shaders and decompiler constants

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| 3Dmigoto names the wrong constant: campaign's haze gone, UI swapped [e06a18da] | Audit every new replacement, re-read from bytecode | [cb_audit.py](../scripts/cb_audit.py) |
| `rN.xy = scalar * ...` loses a component; `dot(c,c)` on a scalar drops the 2 (DOF in 7 games) | `dot(c.xx, c.xx)`, compare | cb_audit + interpreter 24 random runs |
| Generated motion or RT VS: regenerating brings fixed lines back | Fix the decompile input | cb_audit's generated VS part |
| WH1's trees silently wrong in 3Dmigoto | `RT_ASM=labels`, fxc's own disassembly | Compare test |
| Hooks that never ran: `Register` without data plus `<typename T, typename Context>` dropped silently, for days [daec47ba] | Without data use `[]<typename Context>`; check an "applied" line | [scan_register.py](../scripts/scan_register.py) |
| `.bypass` skips later callbacks' post callbacks: t51 stayed bound, ambient gone [e5bf7dd3] | State back in `finalize_callback`, or swap + `.replay` | Read each new `post_callback` under Light Bounce's bypass |
| Registration order = run order; addon order (HDR first by name) | Comment it at the install | Read |
| Sun direction and camera layout assumed to be Attila's: SSS lines, shadows 180 degrees off | Name them per draw (`sun_draw_layouts.py`) | WARP `sss_scene` |
| Game's ambient has a scale: PT AO took all of it ("cooked") | Replace only the diffuse, at the game's scale; read the shader's formula | Patch test; A/B brightness in game |
| Slider stops 0.99 / 1.01 / 0.01 fell on the other side of a gate after a constant changed [730dd54b] | Keep gates exact; test each stop | Read |
| Helper shaders named `*.cs_6_N.hlsl` get compiled by the addon's CMake glob | Plain `.hlsl` when they need defines | The build |
| A game's `*.hlsl` that wrap `../twattila/*`: edits reach both games | Remember both, build both | Build |
| In-place format upgrades lost clamps (Ryse) | View cloning for new mods; don't retrofit | Read |
| Extended's fire and bloom caps differ per game | Read the game's own | Read |
| 3Dmigoto drops a two-component operand: `add r0.yz, cb0[0].yyyy, -cb2[0].wwzw` came out with one constant for both (WH3 fog height) | Read decompile against disassembly where a line writes two components | `warp_tests/fog_wh3`: game's shader against the build on random constants, every bit at the neutral setting; the raw decompile fails it |
| A strength on one permutation of a family: others may take another route (WH3 fog: half-resolution through an upsample, others with alpha 0 straight to the picture [I]) | Put it where the traced passes meet; list what no trace shows | `names.tsv` family, each permutation's outputs |
| Decompiled fog or sky: horizon row as vertical lines through fog (8-bit low-res sky cube) | Fog & Sky Smoothing only with the cause proven in that game | Contrast-stretch a shot |

## 8. UI and presets

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| Keys renamed or removed lose saved values | Page moves and renames only; every setting keeps its key (`graphics_page.hpp`) | Diff keys vs the last zip's |
| Saved value outside new labels | Hidden row keeps the clamp; hidden DLSS on AMD: places differ from values | Read; `RENODX_TW_FAKE_VENDOR` |
| Two ImGui widgets with one id act as one | Unique label; section in the id | Read |
| `Page::Take` matches by key or label: a setting without either isn't found; remaining rows go under the last section | Give them a label | Read |
| `Page::Defaults` puts everything in preset defaults: Shadow Map Size got reset | Keep such keys out | Read |
| Reset must bring back the game's own effects (SSAO, SSR, water) | List Reset's changes explicitly | Read the preset table |
| A preset sets a hidden row: where no hook reads the result it still runs (fog shadows traced for nothing) | Gate the setting on the hook being installed | Read `SettingsOf` for each key the preset writes |
| A preset or method assumes a replacing effect: ray-traced AO over GT-VBAO where it multiplies instead of replacing | A binding says which; texts say what's true here | Press each button in a dump of the settings |
| Presets vs method rows: presets must show through; RT/PT turns RR on only on NVIDIA | Method sliders hold nothing | Read |
| RR with nothing ray-traced | Gate | Read |
| Rows without effect shown (Reflections, Light Through Leaves where the scene has none) [599604a6] | Hide by what's installed | `AppendSettings(..., false, false, ...)` |
| Gamma Correction, SDR preset on an SDR monitor at first start [e06a18da, 3dbf1ce6] | Gamma in all presets; first-start check | Read |
| Add-on running alone [9a909c13, 6875b28f] | Red notice, GitHub button; a missing helper must not crash | Read the path taken with the helper absent |
| Tooltips with dev notes, trace names, other games; DLSS-only wording once FSR and XeSS exist [43141539] | Short, player-facing | Grep tooltip lines, multi-line text too |
| Per-game hue defaults copied | Don't copy Attila's Highlight values | Read |
| Recommended in PsychoV31; Reset keeps its default where Extended exists | Keep | Read |
| Live counters, temporary timers | One-frame traces only; debug work behind Log Details; remove timers before a zip | Read per-frame paths |
| Status lines lag 3 s; "Test: x set to" is exact | - | - |
| Credits and licence texts for everything shipped (NRD, SHaRC, OMM, FSR, XeSS, SSSR) | About plus `licenses\` | Read the zip |

## 9. Testing traps

| Trap | How to avoid |
|---|---|
| Pause freezes the picture; settings changed while paused show no change; each paused run freezes a random wind phase | Never compare across paused runs; compare inside one run |
| Plant Wind Sway above 1: the field pulses 13-38% dark pixels once a second; looks like "all shadows gone" | Keep 1 when judging shadows |
| Old settings prove nothing: a "0 of 8" held only for one ini | The player's current ini |
| A run that didn't run: old log copied, second game failed silently, 4 runs looked clean | Mark runs without a new log INVALID |
| A "pass" where the code didn't run: a build failed PSO and "passed"; hooks never ran | Check the ready or applied line |
| Harness tests bridge and helper, and only part of the D3D11 capture; no real melee, burning houses, cloth in replays | Say what wasn't run |
| Harness darkness: a graded screenshot is too bright to show RR's flat darks | Use the input's real range |
| Slow frames report GPU times late | Read the last status line (`last_status.py`) |
| Debug layer needs Graphics Tools; deterministic for lifetime bugs | `RENODX_TW_D3D12_DEBUG` |
| A lambda with `CHECK` must return int: exit 3, no output | - |
| Script corruption; a German decimal comma in `-f` | InvariantCulture; read before running |
| DevKit in a TW folder loads into the helper and crashes it | Frame trace or capture marks instead |
| Reports without a check | Numbers from the run, else "not checked" |
| A fix proves half | Keep the repro after the first fix |

## 10. Process

| Rule | Why |
|---|---|
| One implementer at a time on one tree; verified plans first; one independent review at the end, then one agent for its fixes | Parallel editing workflows burned far more than serial ones and lost work. Read-only surveys may run beside |
| After a stop: `git log` + `git status` first; continue an earlier attempt | A workflow at step 6 was restarted as if at 1 |
| Checklist in `HANDOFF.md`, tick and commit per step, push the fork | Compaction lost plans |
| Tag only when final | A tag can't be force-pushed; zip tools don't overwrite |
| `build32` for Attila, Rome II, Thrones after shared edits | Rounds shipped useless builds without it |
| No brightness-compressing flicker fixes; no temporal dither on coverage | Judged by the picture; DLSS smears |
| Keep artist intent: scale steps, distance, resolution; look changes in Experimental | Same |
| Round report per game: done, not done, what gets tested; double-work check before it | Without it nobody knows what shipped |
| PowerShell `-Filter "*.addon32.*"` matches plain files too: never delete by it. Heredocs eat backslashes | Both lost work |
| A dead game process pins the addon: rename aside, not delete by wildcard | Only in a game folder |
