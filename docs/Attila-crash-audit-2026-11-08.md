# Total War: Attila / RenoDX — crash audit and implementation handoff

Date: 8 November 2026 — revised after independent source recheck  
Status: **Diagnosis only. No fixes applied.**

## Decision summary

The tester now reports that the same battle passed without the mod, after repeatedly failing with it. That is strong evidence of a failure that depends on the RenoDX installation in this setup. Treat this as our bug investigation, rather than dismissing it as an unavoidable 32-bit limitation.

The revised audit contains **eight actionable findings**. The six original findings survived a fresh review of their callers, ownership, cleanup and failure paths. F3 is downgraded from P1 to P2 because its triggering data has not been observed. Two additional findings cover incomplete GPU-wait recovery (F7) and borrowed sampler lifetime (F8). None has yet been demonstrated to be the first cause of this particular battle's freeze.

The distinction matters: the original freeze and the later crash during graphics-device recovery may have different causes. Fixing recovery must not be presented as proof that the initial GPU failure has been fixed.

| ID | Priority | Finding | Relationship to this incident |
| --- | --- | --- | --- |
| F1 | P1 | Helper/watchdog and IPC outlive add-on unload | The log demonstrates the relevant helper-start and unload sequence; exact crash attribution remains unproven. |
| F2 | P1 | Two draw callbacks survive in process-shared storage after unload | The log demonstrates unload/reload; execution of a stale callback is not recorded. |
| F3 | P2 | Motion-recording float loops can fail to terminate on extreme input | Source-level robustness defect; object motion ran, but no offending position has been captured. |
| F4 | P2 | Biased sampler clones lose ownership on duplicate initialization and lack teardown cleanup | Confirmed code defects; the log does not establish that texture bias was active. |
| F5 | P2 | Resource reconfiguration continues after a failed prerequisite reply | Confirmed error-path defect; no matching timeout is recorded in this log. |
| F6 | P2 | Partial resource-set construction leaks handles duplicated into the helper | Confirmed error-path defect; no matching allocation failure is recorded in this log. |
| F7 | P1 | CPU acknowledgements retire GPU-wait recovery too early | Confirmed recovery gap; requires an incomplete fence and helper failure/stall. Neither is established by this log. |
| F8 | P1 | Texture-bias restoration can reuse a sampler after its last reference is released | Confirmed ownership gap under a valid D3D11 usage sequence; texture bias is not established as active here. |

P1 means an unsafe lifetime or synchronization condition deserves early repair. P2 means a concrete defect to repair with more restricted or currently unobserved triggers. Both categories can have severe consequences. Priority is not evidence that the condition occurred in this incident.

### What the recheck changed

| Original finding | Recheck verdict | Counterargument checked |
| --- | --- | --- |
| F1 | Retained; shutdown scope expanded to the startup worker | No owning bridge destructor, helper shutdown, watchdog join or module pin supplies the missing lifetime protection. Job kill-on-close does not help when the job handle is never closed on unload. |
| F2 | Retained | Shared event-handler removal does not erase consumer registrations. The two missing callbacks have distinct lambda identities; another callback's unregister does not remove them. |
| F3 | Retained as P2 | The finite check elsewhere in `motion.hlsli` protects calculated motion vectors, not the position passed to `MotionRecord`. The three caller families provide no sufficient range guard. |
| F4 | Retained | D3D11 deduplication returns the same object, but each successful creation still owns a reference. Overwriting a clone record loses that reference even when no new unique sampler was allocated. |
| F5 | Retained; consequence narrowed | Later `Request()` calls reject `bridge.failed`, so this is not proof of successful rendering after failure. The erroneous allocation/publication in `Prepare()` still occurs. |
| F6 | Retained; growth claim bounded | The helper records handles only on an accepted generation. A failed attempt leaks its partial set; because the bridge then fails closed, this is not a demonstrated leak every frame. |

The review also checked for a replacement explanation in the helper's ray-tracing failure flag: `request_failed_` is atomic, so it is **not** an additional plain-bool data-race finding. No blanket claim of thread unsafety is justified from that accessor.

## Scope and evidence

### What was inspected

- Repository: `D:\Dev\renodx`.
- On-disk HEAD: `0a6cd493b5683b1ff9278252f1421fa3d1fe74f2`.
- Tracked unstaged and staged filename diffs were empty when inspected. Unsaved editor buffers are outside this snapshot.
- Log: `C:\Users\arshi\Downloads\ReShade.log`, 155,518 bytes.
- Log SHA-256: `E89B04A9BC0AC5BBC17420E13010948A06B8ECD4A6DEAF9AD0A8B6B5C53444FB`.
- Both supplied conversation screenshots, including the new statement that the battle passed without the mod.
- Attila's add-on, motion shaders, anti-aliasing, helper bridge, helper request processing, sampler bias and directly relevant shared callback infrastructure.
- Historical source snapshot `18cbf26eaad0a504aa07dfba7259b4f50e5e51d4`, dated 3 November 2026 at 09:38, to check that findings were not introduced only by later source changes.

The log identifies ReShade `6.8.0.2156` / 32-bit, RenoDX `v0.2026.1103.1004` / API 18, an RTX 5090 with driver `621.1`, and 3840×2160 output upgraded to RGBA16F. The version string does **not** identify the exact source commit. The historical snapshot is corroboration, not a verified reconstruction of the tester's DLL. The actual add-on/helper binaries and their hashes were not supplied.

No repository files, game files or settings were modified. No game, build, harness, GPU test or installation was started. This report is the only artifact created or revised. The recheck read the source again, compared relevant historical code and checked Microsoft's API contracts and ReShade 6.8's unload path. A read-only CPU binary32 arithmetic calculation corroborated F3's counter-stall example; it was not a compiled-shader or game reproduction. Unsaved editor buffers were not accessed or saved.

### Recorded sequence

| Log lines | Time | Observation |
| --- | --- | --- |
| 769–771 | 16:12:01–03 | The DLAA helper starts and native-resolution DLAA becomes active. |
| 772 | 16:12:04 | Object motion reports use of NVIDIA UAV overlap. |
| 773–780 | 16:31:42 | The game leaves fullscreen state and destroys its runtime, swapchain and graphics device. |
| 781–783 | 16:31:42.714 | RenoDX unloads and unregisters. |
| 784 | 16:31:42.714 | ReShade reports an inconsistent native device reference count of **13**. |
| 786–791 | 16:31:42.730–732 | A new device is created and RenoDX loads again. |
| 933–942 | 16:31:42 | The new 4K swapchain/runtime is initialized. |
| 1173–1185 | 16:31:45 | Depth handling resumes while effects finish compiling; the log then ends. |

The file contains no `std::bad_alloc` exception, crash stack, device-removal reason or GPU timeout event. The screenshot's mention of dumps and three GPU errors is a tester report; those artifacts were not provided. Device recreation is consistent with recovery after a GPU problem, but does not by itself establish its cause.

## F1 — P1: Helper lifetime is not tied to explicit add-on unload

**Code:** [helper launch and detached watchdog](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:375), [PID-based IPC creation](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:343), [bridge Release](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:1060), [DetachAddon](D:/Dev/renodx/src/games/twattila/addon.cpp:1956), [helper event loop](D:/Dev/renodx/src/games/twattila/dlaa_helper/main.cpp:511).

### Defect and trigger

`Start()` launches a helper and detaches a thread blocked on the helper process at bridge lines 391–394. Its callback uses code and `bridge` state belonging to the add-on DLL. There is no bridge shutdown/destructor that cancels and joins this thread before DLL unload.

The bridge also owns raw process, job, event, mapping and rescue-fence handles and mapped views. `Release()` releases graphics objects, but does not close/unmap that infrastructure or stop the helper. `DetachAddon()` supplies no missing shutdown step.

The trigger is: **start DLAA, then explicitly unload the add-on while the game process remains alive**. That sequence appears in the supplied log. A later helper exit can return the detached thread into unloaded code or destroyed state, including its mutex.

### Additional consequence: two helpers can share the same protocol objects

IPC names use only the game process ID. A newly loaded copy of the add-on opens the existing mapping/events, zeros the shared block and can launch a second helper. The previous helper is still waiting on those same auto-reset events. Both helpers can consume requests and write responses, although resource handles were duplicated into only the new helper's process.

Generation reuse makes this worse: if the old helper last accepted generation 1, a reloaded add-on can reset the shared block and publish generation 1 again. The old helper can then skip reopening resources and answer using its old textures/fence. The new add-on can wait on a different fence.

**Confidence:** Confirmed lifecycle defect, also present in the historical snapshot. The log does not show a second helper launch or the final faulting stack, so those particular consequences remain unobserved here. This finding concerns recovery, not the trigger of the first freeze.

### Proposed repair boundary

Implement explicit, idempotent bridge shutdown from `AddonUninit`/`DetachAddon`, outside `DllMain`: stop new requests; settle or safely rescue outstanding GPU waits; stop the helper through a defined shutdown protocol; cancel/join the watchdog; then close owned handles, unmap views and reset state. Keep rescue objects alive until all users have stopped. If a replacement helper can coexist temporarily, use distinct session identity so it cannot consume the previous session's IPC.

ReShade 6.8 calls `AddonUninit` before `FreeLibrary` for normally loaded add-ons, so there is an appropriate teardown entry point. Its unregistering of ReShade events does not stop an add-on's own threads. [ReShade 6.8 add-on manager](https://raw.githubusercontent.com/crosire/reshade/v6.8.0/source/addon_manager.cpp)

**Additional worker lifetime hole found on recheck:** [StartAtStart](D:/Dev/renodx/src/games/twattila/engine_tests.hpp:439) detaches another thread and sets `done = true` before its lambda and captured `options` vector finish destruction. [DetachAddon](D:/Dev/renodx/src/games/twattila/addon.cpp:1972) waits only for that flag. On an early unload it can observe `done` and unload while the thread still runs its final cleanup. Retain and join this worker as well, outside the loader lock. This narrow startup/unload race does not explain the reported freeze after approximately twenty minutes; it is an additional correction needed to make the unload invariant complete.

Do not put a blocking thread join into `DllMain`; Microsoft documents loader-lock hazards there. [DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)

**Future validation:** Repeated load/start/unload with the parent process alive; helper exit during unload; pending request during unload; failed helper startup. Require zero surviving helper/watchdog instances and handle/view counts returning to baseline after each cycle.

## F2 — P1: Draw callbacks remain registered after their DLL unloads

**Code:** [campaign registration](D:/Dev/renodx/src/games/twattila/addon.cpp:1929), [water registration](D:/Dev/renodx/src/games/twattila/addon.cpp:1944), [DetachAddon](D:/Dev/renodx/src/games/twattila/addon.cpp:1956), [shared callback records](D:/Dev/renodx/src/utils/command_action.hpp:192), [callback removal](D:/Dev/renodx/src/utils/command_action.hpp:694), [infrastructure detach](D:/Dev/renodx/src/utils/command_action.hpp:769), [shared module removal](D:/Dev/renodx/src/utils/cross_addon.hpp:303).

### Defect and trigger

Attachment directly registers `OnCampaignDraw` and `water_effects::OnWaterDraw`. `DetachAddon()` never unregisters either callback. `command_action` retains callback identities and executable thunks in process-shared maps. Detaching its event infrastructure does not purge these game-specific records, and removing the final module does not destroy the process-shared data.

On reload at a different DLL base, the new callback identity differs. Registration therefore does not remove the old entry. A later matching shader can invoke code at an address belonging to the unloaded image. Another live RenoDX consumer can also expose stale registrations before the original DLL returns. Same-address reload can conceal the problem.

**Impact:** Calls into unloaded code or duplicate callbacks with stale/incompatible state.

**Confidence:** Confirmed registration/teardown mismatch. Both omissions exist in the historical snapshot; the older water callback was local to `addon.cpp`. The supplied sequence reaches unload/reload, but does not prove that a stale thunk was called or that the DLL moved.

### Proposed repair boundary

Unregister both callbacks before the command infrastructure detaches. Check direct registrations in the affected binary entry points for the same omission. Keep the repair at the ownership boundary; do not assume a general shared-map reset is safe while other add-ons use it.

**Future validation:** Check callback counts/identities before attachment and after detachment; keep another consumer alive; force a different DLL load address on reload; assert no registry entry or pipeline copy contains a thunk from the unloaded module.

## F3 — P2: Motion-recording loops can fail to terminate on extreme positions

**Code:** [MotionRecord](D:/Dev/renodx/src/games/twattila/motion.hlsli:129), [unit caller](D:/Dev/renodx/src/games/twattila/motion_unit_records.hlsli:60), [rigid caller](D:/Dev/renodx/src/games/twattila/motion_rigid.cs_5_0.hlsl:85), [capture caller](D:/Dev/renodx/src/games/twattila/motion_capture.ps_5_0.hlsl:26).

### Defect and trigger

`MotionRecord()` computes floating-point cell bounds from world positions, then iterates using float counters at lines 136 and 138. Neither the position nor the resulting bounds have a finite/range check or a fixed iteration bound.

For positive infinity, incrementing the counter leaves it infinite. A large **finite** binary32 cell coordinate such as `16777216.0f` (`2^24`) can also remain unchanged when `1.0f` is added. In either case the loop condition can remain true indefinitely. Breaking the inner slot search does not exit the enclosing coordinate loop.

Units, rigid records and cloth/sail/rope captures can call this routine. The supplied log proves object motion ran, so the subsystem is relevant. It does **not** prove that any object supplied extreme coordinates. Normal battle positions are far smaller; the concrete trigger requires an outlier/nonfinite value, for example from an invalid transient object or record.

**Impact:** Nonterminating GPU work and a possible GPU watchdog timeout/device removal. This is a credible initial-hang mechanism, not an identified root cause of this battle.

**Confidence:** Confirmed source-level progress defect for the stated inputs. The loop is unchanged in the historical snapshot. No affected GPU invocation or compiled-shader reproduction was captured during this audit.

**Recheck:** Explicit binary32 rounding at battle scale gives `position = 16777216`, `low = high = 16777216`, and `float(low + 1) = 16777216`. The counter does not progress. This supports the arithmetic claim, but does not establish how the shipped shader was compiled or whether Attila ever supplies that position. The priority is reduced from P1 to P2 on that basis. Malformed-input protection is justified; calling it the battle's GPU hang would be premature.

### Proposed repair boundary

Reject nonfinite and unsupported-range positions before conversion. Replace float-controlled loops with explicitly bounded integer-offset iteration over the required neighboring cells, derived from the existing reach/cell-size invariant. Preserve ordinary and negative-coordinate behavior. A finite check alone does not fix the large finite case.

**Future validation:** Ordinary, negative and boundary positions; campaign scale; NaN, infinities, `2^24` and larger finite values. Verify valid placement output stays equivalent and every malformed input completes without device removal. Cover all three caller families, then repeat the reported battle under a separately approved diagnostic build.

## F4 — P2: Biased sampler clone ownership is lost on reinitialization and teardown

**Code:** [owned clone creation](D:/Dev/renodx/src/games/twattila/texture_lod_bias.hpp:81), [cache overwrite](D:/Dev/renodx/src/games/twattila/texture_lod_bias.hpp:173), [sampler-event removal](D:/Dev/renodx/src/games/twattila/anti_aliasing.hpp:1570), [device cleanup](D:/Dev/renodx/src/games/twattila/anti_aliasing.hpp:1627), [ReShade initialization event](D:/Dev/renodx/external/reshade/source/d3d11/d3d11_device.cpp:1224), [add-on sampler ownership](D:/Dev/renodx/external/reshade/source/d3d11/d3d11_impl_device.cpp:184).

### Defect A: repeated initialization overwrites an owned clone

D3D11 can return the same sampler interface for an identical sampler descriptor. ReShade emits `init_sampler` for each successful creation, while destruction tracking fires when the object is actually destroyed. [Microsoft sampler documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createsamplerstate)

`OnInitSampler()` uses `insert_or_assign` with a new record whose clone is zero. If an existing record already owns a biased clone, this overwrites its only ownership record without calling `destroy_sampler`. A later biased binding acquires another clone reference. This can repeat during a session without add-on unload.

### Defect B: there is no cache/device teardown

Clones are stored as plain handles. Uninstall unregisters sampler callbacks without releasing remaining clones; device destruction does not clean the cache or reset its remembered sampler slots/tracking. Destruction of the map itself does not release the COM references represented by those raw handles.

**Trigger:** Below-native rendering with texture bias active, followed by duplicate sampler creation or add-on/device teardown.

**Impact:** Leaked sampler ownership, retained device references and stale per-device cache state if the DLL stays loaded across replacement.

**Confidence:** Both defects are confirmed and present in the historical snapshot. The log reports native-resolution DLAA and does not demonstrate this bias path was active. **Do not identify these clones as the source of the logged reference count of 13.**

Ordinary destruction of every original sampler while callbacks remain installed can clean its corresponding clone. That favorable path does not cover duplicate initialization or explicit uninstall with live samplers. Native-resolution operation sets the bias to zero in [OnPresent](D:/Dev/renodx/src/games/twattila/anti_aliasing.hpp:534), which makes F4 and F8 weaker explanations of the supplied session unless the user changed render scale later.

### Proposed repair boundary

Preserve an existing cache record on duplicate initialization. Track ownership per device and explicitly release every clone on device cleanup/uninstall while its device is valid. Restore/reset relevant sampler bindings and clear slots/tracking before callback removal. Account for D3D11's sampler deduplication rather than treating every init event as a new object identity.

**Future validation:** Repeated identical game sampler creation after clone caching; bias changes; original sampler destruction; explicit uninstall; device replacement with another consumer alive. Count acquired/released references as well as unique objects—deduplication can hide leaks from object counts alone.

## F5 — P2: Prepare ignores a failed outstanding-request handshake

**Code:** [Prepare prerequisite](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:490), [Answer failure](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:411), [new generation publication](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:647).

### Defect and trigger

When a steady request is pending, `Prepare()` calls `Answer()` but ignores its result. `Answer()` can fail because the helper exited or timed out and marks the bridge failed. Nevertheless `Prepare()` continues replacing textures, shared fields and the rescue fence, publishes a new generation and can return success.

The trigger is resource reconfiguration—such as a size or method change—while a request is pending and its acknowledgement fails. A timed-out helper may still be alive and accessing the old protocol state. The function has lost the prerequisite that made rewriting it safe. With a dead helper it still attempts an unusable new resource set.

**Confidence:** Confirmed error-path defect, also present in the historical snapshot. The supplied log records no corresponding timeout, so it does not prove this path caused the incident.

The recheck confirms that [Request](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:681) subsequently returns false when `bridge.failed` is set. Keep the finding scoped to a broken preparation contract and unsafe protocol mutation after a failed prerequisite; do not claim that the failure flag is ignored everywhere.

### Proposed repair boundary

Return failure immediately when the required acknowledgement fails. Do not mutate/publish the next generation until the old session is known stopped or a defined recovery protocol has completed. Coordinate this with F1 rather than merely allocating a replacement set after failure.

**Future validation:** Delay the helper beyond the acknowledgement timeout while requesting reconfiguration; separately exit the helper at that point. Require failure to propagate and shared generation, published handles and rescue fence to remain unchanged until safe shutdown/recovery.

## F6 — P2: Failed resource-set creation leaks handles in the helper process

**Code:** [handle duplication](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:401), [per-texture publication](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:527), [construction and failure](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:612), [helper accepted-handle tracking](D:/Dev/renodx/src/games/twattila/dlaa_helper/main.cpp:537).

### Defect and trigger

Each successful texture creation immediately duplicates a handle into the helper and writes that handle into shared memory. If a later texture, view or fence creation fails, `Prepare()` only calls `Fail()`. It does not close the duplicates already created in the other process or roll back the partial publication.

The generation increments only after the complete chain succeeds. The helper therefore never records those partial duplicates in its accepted generation's `set_handles`, which is what its normal cleanup closes. `Fail()` prevents later normal preparation, and `Release()` only drops local graphics references. The remote handles can retain the partial allocations until the helper exits.

In addition, `ShareWithHelper()` ignores `DuplicateHandle()` failure. Texture creation reports success based on local allocation/shared-handle creation even if transfer failed, allowing a zero recipient handle to reach a supposedly successful set. Fence and rescue-handle error handling also needs to participate in the transaction.

**Impact:** Allocation failure can retain successful partial allocations, worsen memory pressure and leave a failed shared-resource session instead of a clean fallback.

**Confidence:** Confirmed ownership/error-path defects, present in the historical snapshot. No matching texture-allocation failure is recorded in this incident's log.

This is bounded to the resources successfully created before a failed attempt, plus any retained previous set. The failed flag blocks normal repeated preparation, so this evidence does not establish continuous per-frame growth.

### Proposed repair boundary

Construct a complete temporary resource set, including checked recipient-handle transfers. Publish shared fields/generation only after success. On any failure, release all local ownership and explicitly reclaim every duplicate in its recipient process.

Do not call local `CloseHandle` on a numeric handle that belongs to the helper. Also account for `DUPLICATE_CLOSE_SOURCE` closing the source even when duplication fails. [DuplicateHandle contract](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle)

**Future validation:** Inject failure at every allocation/view/shared-handle/duplication/fence step. Require no published partial generation and no residual recipient handles or retained allocations. Exercise initial creation and replacement of an existing successful set.

## F7 — P1, new: GPU-wait rescue tracks the CPU reply instead of GPU completion

**Code:** [watchdog](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:391), [Answer clears waiting](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:418), [wait scheduling](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:694), [deferred wait](D:/Dev/renodx/src/games/twattila/dlaa_bridge.hpp:731), [helper queues work and acknowledges](D:/Dev/renodx/src/games/twattila/dlaa_helper/main.cpp:676), [worker executes it later](D:/Dev/renodx/src/games/twattila/dlaa_helper/main.cpp:285).

### Defect and trigger

`bridge.waiting` is the watchdog's only record of the value it should rescue. It is set only for steady requests, then cleared by `Answer()` when the CPU event and `shared.done_value` indicate a reply. That reply is not evidence that the GPU reached the fence value.

The clearest source-level counterexample is steady ray tracing: the helper copies a request into `rt_work.pending`, writes `done_value = value + 1` and signals the CPU event before the worker has necessarily submitted any GPU commands. An acknowledgement can therefore arrive while the required GPU signal does not even exist yet.

A concrete failing order is:

1. A steady request queues a game-side GPU wait for value V.
2. The helper queues the work and acknowledges V; its worker has not submitted the signal.
3. Preparation for a changed resource set consumes that acknowledgement, clearing `waiting`, and replaces the old rescue-fence handle.
4. The helper exits before submitting the old work. The watchdog sees zero and has no retained old-fence/value pair to rescue.

If the old fence remains below V, work behind the old GPU wait cannot proceed. A later request, completed signal or device removal might avoid the stall in other executions; this is a missing recovery guarantee, not a claim that every helper exit hangs. D3D11's GPU wait depends on the actual fence value, not the separate CPU event. [Microsoft D3D11 Wait contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext4-wait)

The same coverage gap exists for nonsteady and deferred waits: lines 699, 723 and 740 enqueue waits without registering them in `waiting`. The process watcher also handles exit only once; it does not monitor an acknowledged request whose worker remains alive but stops progressing.

**Confidence:** Confirmed wait-accounting/recovery gap, present in the historical snapshot. A runtime hang has not been reproduced. The log does not establish that ray tracing or frame generation was active or that the helper exited. F7 can explain failed recovery from another problem; it does not identify the original problem.

### Proposed repair boundary

Track outstanding GPU dependencies by fence generation, retained fence ownership and value, separately from CPU request acknowledgement. Retire them only on actual GPU completion or a defined cancellation/recovery path. Cover steady, initial and deferred waits, helper exit during publication, and resource replacement while an older fence is outstanding. Integrate this with F1 so teardown cannot destroy the only rescue mechanism. Do not replace asynchronous execution with an unconditional per-frame CPU/GPU wait.

**Future validation:** Pause the RT worker after acknowledgement and before signal submission, then replace the resource set and terminate the helper. Independently exercise initial and deferred waits. Require every queued wait to complete or be safely abandoned under device loss, without relying on another game frame reaching `Answer()`. Also verify normal GPU ordering and frame pacing.

## F8 — P1, new: Sampler replacement can invalidate the original pointer saved for restoration

**Code:** [borrowed slot storage](D:/Dev/renodx/src/games/twattila/texture_lod_bias.hpp:55), [clone binding](D:/Dev/renodx/src/games/twattila/texture_lod_bias.hpp:103), [snapshot references immediately released](D:/Dev/renodx/src/games/twattila/texture_lod_bias.hpp:157), [destroy-event suppression](D:/Dev/renodx/src/games/twattila/texture_lod_bias.hpp:182).

### Defect and trigger

The module remembers original samplers as numeric pointer values in `slots.game`; it does not retain a reference to those originals. During `BindSlots()` it replaces them with biased clones while `addon_sampler = true`. `OnDestroySampler()` immediately returns under that flag, even when the sampler being destroyed is a game original rather than an add-on clone.

D3D11 holds a reference to a bound sampler, so a valid application can release its own creation reference and rely on the context to keep the sampler alive. [Microsoft PSSetSamplers contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetsamplers)

A concrete ownership failure is:

1. Bind an original anisotropic sampler while the view is unbiased; release the application's creation reference. Its context binding is the remaining owner.
2. Enter a biased main view. `BindSlots()` replaces that original with a clone, releasing the context's reference to the original.
3. If destruction is reported synchronously during this rebind, the `addon_sampler` guard suppresses cleanup, leaving the original's pointer in `slots.game`.
4. Leave the main view or disable tracking. Restoration passes the stale original pointer to `PSSetSamplers`.

This can produce a CPU use-after-free. If destruction is deferred and the normal callback clears the slot later, that particular stale-pointer sequence can be avoided, but the code still has no owned original with which to preserve the game's logical binding. It relies on the game retaining an extra reference and on callback timing, neither of which it enforces.

**Confidence:** Confirmed ownership gap under the stated valid API usage; runtime timing and Attila's sampler-release pattern have not been captured. The implementation is unchanged in the historical snapshot. Like F4, this requires texture bias and is not supported as the cause of the logged native-DLAA session.

### Proposed repair boundary

Retain each original sampler while the module substitutes a clone and may later restore that original. Release retained ownership on logical slot replacement, tracking shutdown and device/add-on teardown. Combine this design with F4's clone ownership and per-device cleanup. Do not just remove `addon_sampler`: destruction can re-enter while `slots.mutex` is held and deadlock. Arrange release and callback handling so re-entry is safe, and handle clone/original aliasing caused by sampler deduplication.

**Future validation:** A real D3D11 harness should bind, release the caller's reference, switch biased/unbiased views, disable tracking and tear down the device. Verify the original remains valid and is restored, then all references are released. Include identical descriptors, repeated slots and bias changes; run with the debug layer where available.

## Claims that should not accompany these findings

- **“The initial freeze is definitely fixed by repairing unload.”** Unload happens after the observed freeze/recovery sequence. It can explain a secondary crash without explaining the first failure.
- **“This is merely the game's 32-bit limit.”** No address-space measurement, allocation exception or crash stack was supplied. Added resources can contribute to a limit, but that is not established here.
- **“A GPU error proves the mod is innocent.”** Faulty GPU commands, synchronization or shader behavior can produce driver timeouts.
- **“13 means thirteen leaked textures.”** ReShade observes an outstanding native device reference count. It does not identify the resources or owner. Its context cleanup and add-on unload precede this check.
- **“The later WH3 terrain Flush change fixes Attila.”** Attila's [scene table](D:/Dev/renodx/src/games/twattila/addon.cpp:1179) has no tessellated entries; that field defaults false. The [conditional Flush](D:/Dev/renodx/src/games/twattila/rt_core.hpp:1630) cannot execute for that table. This is an excluded explanation, not a recommended Attila fix.
- **“Release dropping local textures proves a GPU use-after-free.”** The helper owns opened references, and its generation-change path drains its worker before closing old handles. That isolated observation is insufficient; F1, F5 and F6 identify the concrete missing lifecycle/error handling instead.
- **“NVIDIA UAV overlap is the cause.”** The log proves it ran; no faulty overlap sequence was established by this audit.
- **“SCAC.fx explains the battle crash.”** Its compilation error already occurred at startup, well before this incident. It should be treated separately.

## Recommended implementation order — only after approval

The plan has two deliverables: repair the verified code defects, and establish what caused this battle's first freeze. The second deliverable cannot be closed merely by completing the first.

| Step | Scope and concrete result | Validation gate before moving on |
| --- | --- | --- |
| 0. Establish the reproduction baseline | Preserve the tester's exact binaries, helper log, dump/events, settings and replay/save. Match the logged release to a source commit if possible. Specify what remained installed in the successful control. | A documented reproduction and control configuration; missing evidence remains explicitly marked missing. This need not prevent independent code repairs. |
| 1. Remove stale callbacks (F2) | Add symmetric unregistration for campaign and water callbacks in Attila; inspect corresponding direct registrations in sibling entry points. Keep shared infrastructure intact for other consumers. | Load/unload/reload callback counts and pipeline copies return to baseline, including another live consumer and relocation. |
| 2. Make shutdown and wait recovery complete (F1, F7) | Own and join the bridge watcher and startup worker. Define helper stop/session identity. Track actual outstanding GPU waits per fence generation, including deferred waits. Close mappings, process/job/event handles and rescue objects only after their users stop. | Fault injection before/after CPU acknowledgement and GPU submission; reconfiguration, helper exit and unload all finish with no surviving worker/helper and no unsatisfied owned wait. Deliver lifetime and wait-accounting changes as separately reviewable patches. |
| 3. Make preparation transactional (F5, F6) | Propagate failed `Answer()` immediately. Stage and check all local resources and recipient handles, then publish one complete generation. Roll back ownership on every failure, including duplication/rescue-fence creation. Check startup API failures as part of the same ownership invariant. | Failure at each construction step leaves no partial published set or leaked local/remote ownership; a failed bridge returns a clean fallback. |
| 4. Bound motion recording (F3) | Add finite/range validation and fixed bounded cell iteration. Preserve all supported positive/negative positions and campaign scale. No arbitrary clamp that writes malformed transforms into the table. | Compare valid placement keys/output, then compile and exercise extreme/invalid inputs in an isolated shader harness. No GPU timeout; ordinary output unchanged. |
| 5. Correct sampler ownership (F8, F4) | Own originals while substituting clones; preserve clones on duplicate initialization; make device/slot cleanup explicit and re-entry safe. Wire cleanup into all consumers of this shared header. | D3D11 bind/release/restore test, duplicate-descriptor reference accounting, tracking off, uninstall and device replacement all pass without stale pointers, deadlock or leaked references. |
| 6. Build and reproduce the battle | Build all affected consumers; package a matched add-on/helper pair for testing. With separate permission to run the game, repeat the original battle and controlled feature variants, recording device-removal reasons and completion. | Source/harness gates pass and the previously failing battle repeatedly completes with the intended features. Report harness coverage separately from game coverage. |

Steps 1 and 4 can be reviewed independently of the bridge design. F1/F7 and F4/F8 share lifetime invariants, so agree on their ownership model before applying small local patches that might conflict. Keep each logical repair separately reviewable and retain the reproducer after the first successful run.

The changes are not isolated to Attila: shared headers are consumed by Rome II, Thrones, Troy, Pharaoh Dynasties, Warhammer I–III and other targets listed in the checklist; the sampler header is also directly used by the Shogun 2 DLSS add-on. Follow the full consumer build matrix in the [Total War checklist](D:/Dev/renodx/.agents/skills/tw-port-checklist/SKILL.md), including **32-bit Attila, Rome II and Thrones**. If helper/protocol code changes, build in a new helper directory and distribute the matched add-on/helper pair. The current Attila README says v140/helper89 was built but not run in game; do not substitute helper89 for the tester's unknown helper and call that a controlled baseline.

Do not modify global CMake presets, vendored ReShade or other dependency code to implement these fixes. Follow `AGENTS.md`, `docs/CXX_STYLE.md` and `.agents/skills/tw-port-checklist/SKILL.md`. Preserve existing unsaved work and avoid overwriting installed binaries in use. No commit, deployment, game launch or fix is authorized by this diagnostic report itself.

## Evidence needed to attribute and close the original freeze

Preserve the original artifacts before another run overwrites them:

- Matching `renodx-tw-dlaa64.log`, crash dump(s), and Windows GPU events around 16:31:42.
- The tester's exact add-on/helper binaries and hashes, settings and relevant game graphics settings.
- The replay/save, battle and camera/actions required to reproduce the failure.
- The exact contents of “without the mod”: whether ReShade and the same effects/settings remained installed, plus repeated outcomes for that control.

For a later approved diagnostic build, establish whose device first fails—the game's D3D11 device or a helper device—and record its removal reason and last completed work. Existing repository diagnostics may be reused after confirming their compatibility with Attila; D3D12-only evidence does not identify a fault in the game's D3D11 stream.

Then compare the same battle with the complete original configuration and controlled variants that isolate object motion, helper-based AA and ray effects. Change one variable per comparison and record completion, reset events and memory/handle growth. The repository checklist recommends repeated runs because one pass does not clear an intermittent reset.

**Completion criteria:** no stale callbacks, surviving add-on workers/helpers or leaked ownership across unload/reload; every outstanding GPU wait covered through completion or defined recovery; original samplers remain valid across substitution/restoration; bounded shader completion for malformed motion inputs; clean failure rollback; and repeated successful completion of the previously failing battle with the intended features enabled. Until that last step, the report establishes bugs to repair, not a proven resolution of the initial freeze.


## F1 repair completed (2026-11-09, GPT-6 Astra)

Startup worker native-exit joining was shipped separately in 0ec6f600; helper watcher/process/IPC ownership is now repaired. All 11 bridge users shut it down from AddonUninit after detaching callbacks. Shutdown retains outstanding fence generations, drains normal work, and performs ordered CPU fence recovery after confirmed helper exit if needed. Recovery is best effort when device creation, shared-handle opening, helper termination, or the device itself fails; those paths log failure rather than claim successful recovery.

Validation: x86/x64 production-header regressions pass missing executable and IPC collision cases, launch/watcher failure injection, 20 reload cycles with stable handles, and three outstanding D3D11/D3D12 WARP fence generations (normal delayed completion and stalled helper recovery). Final x86 handles 186/186, x64 166/166. All 14 Release consumers built successfully. No game or installed binary was changed. Manual unload/reload and the original battle reproduction remain necessary. F7 mid-run helper death/stall is not closed by this shutdown repair. F3-F8 remain open; F2 was already fixed in 893bfdf0.


### F3 bounded motion placement (2026-11-09, GPT-6 Astra)
MotionRecord now rejects nonfinite, invalid-scale and out-of-range cells before integer conversion, then visits at most nine cells with integer offsets. This prevents float increment stalls at extreme coordinates. The WARP regression runs the actual shader for 52 normal/boundary/negative/campaign/invalid/extreme cases and compares recorded cells with the old calculation for supported inputs. All 14 consumer builds passed. No game was launched; the original freeze is still unproven. Test: test/tw_motion_record/run.ps1.
