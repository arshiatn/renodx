# Helper, frame generation, stability, cost

The helper (`renodx-tw-dlaa64.exe`, `src/games/twattila/dlaa_helper/`) is a standalone D3D12 process for DLSS, RR, FG, FSR, XeSS and the ray tracing, reached through `twattila/dlaa_bridge.h` in shared memory (the addon's side: `dlaa_bridge.hpp`). It isn't a ReShade addon; it shares a few headers with the addons.

**Take the newest helper and bridge.** Most freezes, crashes and limits of the Attila and WH3 rounds were fixed in the helper and the bridge. A new or older TW game, and any non-TW addon on the bridge, builds against today's `dlaa_bridge.h` and ships with the helper built from the same commit, never an older copy.

- The helper answers addons of bridge `OLDEST_VERSION` to `VERSION` (20 to 31 as of 2026-11) and refuses others ("Shared memory unusable (another version?)" in its log). The addon's half of a fix (`Prepare`'s new fence set, owned fields only) reaches only a rebuilt addon: an addon built against an older bridge is rebuilt and shipped with the matching helper.
- What newer helpers fixed: a held removed device, shared fences after a loss and the GPU reset classes ([section 2](#2-crashes-hangs-and-gpu-resets)); ReShade kept out of the helper, request overflow, requests answered off the game's thread on workers ([section 3](#3-helper-memory-and-lifetime)); TLAS room and the capture count ([rt-scene.md](rt-scene.md#2-families-where-drawn-budget-overflow-answer)).

## 1. First-build stability checklist

Before any test round of a new game or a big change:

1. From an unmodded `ReShade.log` (the maintainer's, or the data run's first start): device flags, flip or blt chain, probe devices and chains, fullscreen, bitness. Write them in the commit.
2. Wire the shared `device_loss` (Install before the swapchain mod, InstallLast at the end of `AttachAddon`, Uninstall at detach) and `output_encoding`; `light_bounce` installs `device_end`.
3. Every `Register` and `Install` paired with its `Unregister` and `Uninstall`; `engine_tweaks::Uninstall(options)`; native hooks behind `tw::native_hooks::Lifetime`; no detached threads, joined in `AddonUninit`; `DllMain` returns TRUE.
4. Every static game-device object released at `destroy_device`; no game-device calls off its thread; no device made by the addon.
5. Engine values: the render-stage trap in [game-facts.md](game-facts.md); detach switches nothing back (`engine_tweaks::Uninstall` in `twrome2/engine_tweaks.hpp` keeps the game's own values for the next image).
6. In-frame side work: targets kept, stream output without PS, compute at frame end, no GPU readback waits; an off row and a Flush candidate per batch.
7. Hot callbacks return on one atomic; no counters, no per-frame logs; log once per failure streak.
8. A helper or bridge change: the steps under **Helper or `dlaa_bridge.h`** in [SKILL.md](../SKILL.md)'s checks.
9. Every target in `SKILL.md`'s build lines built, `build32` too; every `test/tw_*` on x86 and amd64.
10. Profiling ready before the first run: Record Ray Tracing Profile, Time Each Pass, Frame Trace.

## 2. Crashes, hangs and GPU resets

| Mistake | Rule | Check |
|---|---|---|
| A "freeze" or "hang" was a crash: the game's crash handler compressing a 141 MB report on the frame thread; 60 runs blamed the helper while the game's own D3D11 device hung | Before any theory read the game's `crash_report` folder (`cdb -z`), `ReShade.log`'s device-end line (removed reason, last frame's age, 32-bit free address space) and the count of System-log event 153 (`nvlddmkm`); `renodx-stall.dmp` for stalls [97e36a3a, 4630d647, 087aa313] | `test/tw_device_loss` |
| CreateSwapChain failed twice after a GPU reset (34 of 48 WH3 crash reports): the removed device's views held the window; our swapchain mod made the game's blt fallback flip [cb588e93, 6b06cc9c] | ClearState and Flush removed devices before the new chain; keep the blt fallback SDR; FG drops its kept back buffer at `destroy_swapchain` (a held SRV failed ResizeBuffers) | `warp_tests/swap_loss`, `test/tw_fg_initialization`; only a real reset proves it |
| A white picture after a device loss with Windows HDR off: the output decided once | Decide the output for every new swap chain (not ResizeBuffers); HDR10 or scRGB falls to SDR only when HDR is known off on every display; log a refusal once, ask again at the next output change [be4ac169, 03c7fa06] | `test/tw_device_loss` output table; `fg_bridge_test` HDR10, SDR, HDR10 |
| 13, 92, 851 references left on the game's device; line-sweep AO bound a removed device's buffer on the new context [288da43b, 9ab8343c] | Release every static at `destroy_device`; drop it when the context's device changes (own the device, don't compare addresses); no D3D Release in process-exit destructors [29ab381a] | The end line: "References on the game's device: N ... M" |
| Unload became real, and every leftover fatal: callbacks, failed hook removal, workers, Graphics scales compounding after reload [893bfdf0, 85615df0, c0f6439f, 0ec6f600, 2c70e7f4] | Every registration undone in `DetachAddon`; threads joined in `AddonUninit`, never in `DllMain`; once-only log lines reappearing prove a fresh image | `test/tw_helper_lifetime`, `test/tw_startup_worker`, `test/tw_native_hook_lifetime` |
| The addon made a device: a D3D11 device on the game's thread deadlocked; a D3D12 device in `AddonUninit` re-entered ReShade's addon loading [a891b919, 08d69552] | ReShade hooks every create call: recovery runs in a second helper process | Rescue cases in `test/tw_helper_lifetime` |
| A watcher used the game's fences off-thread on a single-threaded device | The watcher reads only shared memory and atomics; the removed reason is asked on the presenting thread once a second | `test/tw_device_loss` records calls per thread |
| The helper treated a fence at all ones as failure and ended itself at every GPU reset; shared RR code could end Tales [bb72ea36, bf3618c9, f0c461d7, 4dd59a56] | A removed device means done; a timeout drops the request, not the backend; end a process only where memory can't be freed safely, only in the helper, after a flushed log line naming owner and fence values | `test/tw_gpu_completion`; harness `removal`, `removal_sway`, `switching_removal`, `switching_debug_layer`, `release_*`; helper built with `RENODX_TW_TEST_GPU_WAITS` |
| Watchdogs and failure paths: `GetTickCount64` counts sleep; `Fail()` logged every frame; a failed rescue retried every 50 ms; `bad_alloc` in a raw thread terminates | Count polls (stuck = 120 x 250 ms); one recovery, no retry; log on transition; allocation-free failure paths; the 90 s no-frame limit counts only foreground time | `test/tw_helper_lifetime` |
| A whole-struct copy-back erased the helper's ready flag: DLSS, RT and FG silently never started [e0218e39] | `Prepare` publishes only addon-owned fields, generation last; 64-bit fields, `VERSION`, `OLDEST_VERSION`, `offsetof` per version | Helper writes during `Prepare` survive (`test/tw_helper_lifetime`) |
| GPU reset classes: a trace recorded over the last trace; helper work after the game's signal; WH3's own device hung on tree compute plus our tessellated stream output in one batch [a6e4dbb6] | First ask whose device was removed: the debug layer and DRED see only D3D12 (the helper), never the game's D3D11 device, which has no breadcrumbs. Every new in-frame compute or stream-output batch gets an off row and a Flush candidate. Prove with 6 runs or more per state, the control first, counting event 153; bisect with fence signals at chosen hashes and take a cure as the proof | No offline check: WARP never hangs |
| Each driver reset leaves a dead process with its commit until a reboot: 100 runs filled the page file, then "No memory" changed the scene and runs proved nothing | Count resets; the maintainer reboots after a batch with resets | Committed Bytes |
| One clean run or 0 of 5 clears nothing: the same path gave 0 of 5 and 3 of 6 | 6 runs at least per state; read the feature-off control and the rays-off control together | Event 153 count |
| A held removed D3D12 device blocks every new device in the process; Streamline never lets go | `MakeOwnDevice` (factory, no singleton) for RR, FSR, XeSS; retry each second | Harness `removal`, `switching_removal` |
| A removed device leaves shared fences at all ones: every wait passes, RR and MFG look "broken", fps "better" | A new fence set in `dlaa_bridge::Prepare`; the helper treats all ones as done | `RENODX_TW_TEST_REMOVAL` |
| After a removal, "N failed" lines are a result, not a cause; "no page fault" rules out nothing; DRED labels can be stale | Read in order; reading freed memory hangs with no fault | Debug layer `RENODX_TW_D3D12_DEBUG` |

## 3. Helper memory and lifetime

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| `Close` of shared textures without a wait: a real removal at RR to AA Off [00862f09] | Wait on its own fence before any Close | Debug layer, `RENODX_TW_TEST_DEVICE_REMOVAL`, `HARNESS_SWITCH` |
| A trace recorded over the last trace: the real freeze | `WaitFor(trace_value_)` before recording; anything relying on "the same slot" needs a real wait | Harness with sway or bakes on |
| Helper GPU work after the game's signal runs beside the D3D11 frame and hangs | Inside the request's list, bounded per request | Harness bakes stress |
| Request overflow (256 meshes plus a bake): a failed Close leaves its slot dead for good | Room for a full request; failure means a new scene, the 4th turns off | `leaf_storm` |
| Idle release freed what was just opened: the first RT start after 10 s built twice [23344f8f] | Opening counts as use | `HARNESS_RELEASE` |
| `slShutdown` then RR plus RT start hung the helper | `slFreeResources`; `Shutdown1` from idle RR crashes the next evaluation | `fg_bridge_test` |
| RT off kept 15 GB; effects off kept textures [625742fc, 80239089] | Idle release 10 s, RT objects 15 s, effects free when off, `Release(true)` on leaving DLSS | Helper "release" lines; memory counters |
| A shared texture opened twice in a process stays in VRAM (+195 MiB per RR cycle); a handle never closed (+32 MiB per set) | D3D11 opens at the set's first DLSS request; check every new shared texture's handle | `fg_bridge_test` memory phases |
| A slow API in the first request passed the 5 s answer limit (`dlaa_bridge::Answer`; a later answer turns DLSS off) | Worker thread (`rt_worker` in `dlaa_helper/main.cpp`) | Helper log |
| Pharaoh v52 crashed at every game exit: a destroy handler called `device->get_api()`, and `DetachAddon` calls the same handler with nullptr [d3013b0b, 97840ddd] | nullptr means the addon is ending: skip the device checks, drop the state | Read each destroy handler's `DetachAddon` call |
| `&member` (a WRL `ComPtr`) handed to a `ComPtr<T>*` parameter releases what it holds first (`ComPtrRef::operator T*`): helper107-108's `GrowBuffer(&sway_...)` made and freed 16 MiB every frame (~1.3 ms GPU paging, ~2.4 ms on the RT worker); every addon's `KeepCamera` / `AttilaCamera` made a D3D11 buffer each call [02a05144, 20b91319] | `std::addressof(member)` for any `ComPtr<T>*` that must keep the object; `&member` only as an out-parameter meant to drop it | [scripts/static_checks.py](../scripts/static_checks.py) (comptr: `&x` into a `ComPtr<T>*` callee that reads it before writing; `--rev 20b91319^` lists the 18 old sites, `02a05144^` the sway's 3 of them alone; SHaRC's and the micromap bake's are known); `RENODX_TW_TEST_BUFFERS=1`: each request's report counts `Buffer()`'s new buffers (`rt_harness/buffers_v65.sh`); `test/tw_kept_buffers` |
| Room growth by `CreateCommittedResource` (up to 2.5 GB) stalled requests | On a worker at 3/4 [c12d17c7] | Harness |
| Root UAVs have no bounds checks | `BUFFER_AT_OFFSET` clamps | Harness |
| A 64-bit ReShade `dxgi.dll` beside the helper loaded our addons into it: a CRT fault each start, the DevKit crashed it, ReShade's banner on FSR FG's window | The helper loads `dxgi`, `d3d11`, `d3d12` from System32 first (delay-loaded) [3bd71292]; addons keep `DllMain` TRUE with `AddonInit` failing in a foreign host | `fg_bridge_test` module list with a ReShade in the helper's folder |
| 32-bit: address space (peak 3158 of 4096 MB), mapped or dynamic textures (Animated Soldier Limit made soldiers invisible), `HeapAlloc` aligning to 8 [bc0ead39], ReShade's screenshot `bad_alloc` | Cast pointers via `uintptr_t`; don't take Attila's RAM caps as needed in a 64-bit game | `build32` and an x86 run of every `test/tw_*` |

## 4. Frame generation

| Mistake or trap | How to avoid | Verify without the game |
|---|---|---|
| Streamline takes no scRGB [6de22557] | Auto or HDR10, or the conversion pass (`fg_hdr10`) | `fg_bridge_test` |
| SDR is 10-bit too: "10-bit means HDR10" turned the window red [582065c9] | Send the colour space | `sl.log` |
| A colour-space refusal made fatal ended DLSS-G for the helper's lifetime [03c7fa06] | Log once, ask again at the next output change | `fg_bridge_test` HDR10, SDR, HDR10 |
| ReShade screenshots never show the helper's window | Display capture or the log | - |
| HUD-less must hold everything but UI; taking the tone mapper's output before the final pass counted all pixels as UI [f3b2cc91] | Keep output after the last scene pass (`OnOutputEffectDrawn`); flares and vignette after the tone mapper belong in it; a pass that also runs after the UI must not feed it; passes that don't run every frame fall back | FG Debug View |
| The swap-chain shader's constants aren't at b13 in every game | The game's `shared.h`; `frame_generation::Install`'s `injection_slot` | `wh3_fg_keep_test` (WARP, built shaders) |
| Resize: off and on in one frame races Present | Two presents off; drop the kept back buffer at `destroy_swapchain` | `test/tw_fg_initialization` |
| A frozen window when textures no longer fit or open fails; an early return in a present path while the helper presents [be916e1c] | Show the window only after a Present that worked; hide it after 60 failed Presents in a row and say why [f3b2cc91, 6f4a8ea0]; send the off request | `fg_bridge_test` |
| FG off kept 590 to 605 MiB under Streamline, +64 MiB per on and off | Free resources | Helper log |
| FG presets are the driver profile, not Streamline: read once at load; Global VSync caps it | DRS key 0x10E41DF1 on the helper exe | - |
| FG is off under ReShade's menu, so the status line showed what ran before the menu opened [f3c4d3c5] | Status says so (`graphics_page.hpp`, `frame_generation.hpp`'s `overlay_open`) | Read the status line's `on_draw` |
| Backends present at most FSR 2x, XeSS the GPU's, DLSS the driver's; asking more asks for the most | Rows per backend maximum [3bd71292] | `fg_bridge_test` at 2x and 6x per backend |
| A 22 ms wait on the game's thread was FG's pacing, not the helper [5797d2a6] | Read `present_pacing_ms` before optimising | Record Ray Tracing Profile (30 s) with the menu closed |
| No status line: nobody knows why no frames | Reason text [78c0f619] | `fg_bridge_test` |
| Frame Rate Limit, Reflex, Dynamic rows where they don't apply; FG's Debug View left on under the menu [c8b5dde6] | Rows per method; off while the menu is open | Page dump |

## 5. CPU and GPU cost (RTX 5090)

- On the game's device: a side draw unbinding targets about 1.7 ms a frame; with PS UAV writes about +2 ms; targets kept with no PS or stream output about 0.1 ms; a moving draw (about 40 D3D11 calls) about 1.2 us; `CopySubresourceRegion` between about 140 draws 1.24 ms GPU; stream output left bound through game draws about 10 us a draw.
- WH3's terrain-count `GetData` spin cost 14 to 21 ms of CPU a frame; read a frame late from six query sets it is 0.001 ms (Experimental row). A re-begun query still open reads the old answer. A CPU number that looks like the biggest cost may be a wait: the no-wait path had the same fps and a worse p99. A/B before fixing.
- WH3's GPU-culled grass gave the rays its indirect numbers through a waiting read-back (1.2 ms a frame where it is drawn). Grass Without Waiting (Experimental) takes the probe's (an earlier frame's copy read with DO_NOT_WAIT): 0.011 ms against the wait's 3.8 on WARP, its shadows a frame or two late [796033c8]; `test/tw_indirect_counts` runs the addon's CaptureIndirect. The form without a lag (the capture as an indirect draw onto emptied room sized from the probe, or the numbers copied on the GPU for the helper) changes the compact capture's room and the helper's cluster counts: a bridge change.
- A hook that grew a map lookup and two locks per `PSSetSamplers` regressed: back to one atomic [29ab381a].
- Helper at 4K: DLAA 1.74 ms, RR 8.3 ms, FG 2.5 to 3.75 ms, one 4K RGBA32F copy 0.16 ms. The helper's CPU records a trace in 2.5 to 3 ms in game against 0.16 ms in the harness: the harness hides CPU cost at game scale. NVAPI PTLAS updates stalled 0.3 to 0.8 s for 4 to 5 s after BLAS compaction [d721f596, c4c5e623].
- Measure before CPU work: PresentMon CPU busy and GPU wait, and both processes' GPU Engine counters (PresentMon leaves out the helper's GPU); then a profile ([testing.md](testing.md) section 5). WH3's 4K siege was CPU-bound (GPU 13.4 to 14.6 of 28 to 31 ms), so GPU savings didn't show as fps; Attila's 4K views with RR at DLAA are often bound by the helper's GPU (about 9.75 ms of GPU wait), so CPU savings don't. Grass CPU is the game's own, per drawn instance (Attila's scrub writes a row per instance); our hooks cost about 0.45 us a draw.
