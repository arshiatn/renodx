# TW helper lifetime regression

```powershell
./test/tw_helper_lifetime/run.ps1 -Architecture x86
./test/tw_helper_lifetime/run.ps1 -Architecture amd64
```

Requires Visual Studio Build Tools and Clang. The test includes the production bridge (`dlaa_bridge.hpp`) and creates a software D3D11 device as the game's. Its own executable, copied as `renodx-tw-dlaa64.exe`, is the fake helper: the PID-based IPC, a software D3D12 queue, bridge v30's fence reading, and the helper's rescue mode with the production `SignalAbandoned`. It does not load a game, the real helper, ReShade or an installed add-on. Generated executables stay in `build/astra_helper_test_<architecture>`. A run takes about 75 s (it waits out the watcher's 30 s limit twice).

Checks:

- Start: missing helper cleaned up; an occupied mapping, request / done event or cluster mapping refused with the old session's contents untouched; injected resume, watcher-handle and stop-event failures refused with the child ended; a failed job assignment still runs the helper.
- Prepare: a stale acknowledgement stops it before any change; a failed handle transfer at 8 places of a full set (guides, ray tracing, reflections, frames, fence) closes the helper's handles again and publishes nothing; a helper that ends while a set is transferred (at a texture, at the fence) shows as the stopped helper with its log, in one line and with no rollback warnings; a complete set is published field by field, and `ready`, `rt_device`, `rt_compact_ok` and `status` written by the helper in the middle of Prepare survive it; a smaller set and `Release(true)` clear the handles they do not have.
- Reload: 20 start / unload cycles (natural and forced helper exit, with and without a set), the watcher thread really ended at each, a second Shutdown harmless, the process handle count unchanged.
- A helper that stops before it is ready: the status names its own reason and its log; one warning, none in 100 more frames of Prepare / Request / BeforeSharedWrites.
- Healthy: the answer first and the GPU work 100 ms later completes; 32 s with nothing asked ends nothing and starts no process; an unload with a wait still queued returns at once and starts no recovery.
- Failures, each with an older set's wait still queued: the helper exits after its answer (first, steady and deferred request), never answers, or answers and never does its GPU work (30 s). The game's queue must get past every wait, one recovery process must have run, once, and at most the failure's own lines are logged.

Limits: the software adapter, not a vendor GPU; the fake helper, not the real one (its rescue mode in `dlaa_helper/main.cpp` is mirrored, only `SignalAbandoned` is shared code); no ReShade, so the add-on reference counting that the design avoids is not exercised. A helper ended with a set's fence open leaves that fence at all ones on the software adapter, so in the two cases that end a live helper the current set is freed by that, not by the recovery; the older set and the three exit cases are freed by the recovery's signals.
