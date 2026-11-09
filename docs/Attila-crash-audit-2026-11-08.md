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
- Log SHA-256: `E89B04A9BC0AC5BBC17420E130109<48A06B8ECD4A6DEAF9AD0A8B6B5C53444FB`.
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


### F5 failed acknowledgement (2026-11-09, GPT-6 Astra)
Prepare now returns immediately when the preceding helper acknowledgement fails, before retiring fences, replacing textures or changing the published generation. The x86/x64 production-header test injects a stale completion and checks preserved width, generation and shared color handle. All 14 consumer builds pass. No game run.


### F6 staged shared-resource publication (2026-11-09, GPT-6 Astra)
Prepare checks every remote-handle transfer, stages shared fields locally, and publishes only after textures and both fence handles exist. Failure rolls back transferred handles in the helper process and releases partial local resources. Tests inject first/second/fence transfer failures, check unchanged published fields and stable helper handle counts, and exercise successful publication. x86/x64 failure regressions and all 14 consumer builds pass; the success case also passes x86. No game run.


### F4/F8 sampler ownership (2026-11-09, GPT-6 Astra)
Sampler slots now own original COM references and are scoped to each device. References replaced in slot callbacks are released after unlocking. Duplicate sampler-init notifications preserve existing owned clones; teardown restores originals and clears each device cache. Final-release callbacks no longer acquire slot locks, and nested addon-callback suppression is preserved. WARP tests pass x86/x64 for originals surviving game release, clone identity preservation, two-device isolation, tracking restoration, per-device cleanup and shutdown. All 14 shared consumers built. No game run.


### F7 GPU-completion watchdog (2026-11-09, GPT-6 Astra)

Each submitted request now retains its fence generation and target completion independently of CPU acknowledgement. The watcher stays alive after helper exit to catch late in-flight publication, detects lack of fence progress (20 seconds during initial setup, 5 seconds after acknowledgement/for steady work), and recovers outstanding dependencies only after confirmed helper termination. Initial, steady and deferred GPU waits use the same ownership. Deferred shared writes keep their dependency even after a CPU-side failure. No per-frame CPU GPU-completion wait is added.

The actual production-header WARP tests pass x86/x64 for a healthy delayed submission, acknowledged-but-unsubmitted work, helper exit and stall in all three wait paths, and late publication after exit. The existing three-generation shutdown and transfer-rollback tests pass too. All 14 consumer builds pass. Recovery remains best effort if the driver/device or process termination fails; the timeout is not a guarantee against a driver TDR. No game was launched and the original battle freeze is not proven resolved.

Final review also moved F6's staged Shared block to checked heap allocation rather than the game render thread's stack.


## Independent 64-bit helper review (2026-11-09)

### Scope and evidence

This is a source review, not a repair or a reproduced crash diagnosis. Four additional findings are recorded below: two high-priority GPU error-handling defects and two medium-priority frame-generation correctness defects. No P0/emergency defect is established, and none is proven to explain the original battle crash.

Reviewed the saved helper sources in D:/Dev/renodx/src/games/twattila/dlaa_helper, with the Attila caller and shared protocol. Original checkout HEAD: 0a6cd493; its working-tree status was clean. All seven helper .cpp/.hpp files have identical text in the repair worktree at c314b0a6 after normalizing line endings. References below are relative to src/games/twattila/dlaa_helper unless stated otherwise. The report is appended in the repair worktree so that the completed F1-F8 record stays in the same file. No source changes, builds, test execution, game launches, commits, pushes, or installed-file changes were made during this review.

The earlier bridge tests use a disposable simulated helper. Their success does not validate these production helper backends. F1-F8 remain completed code repairs; the new findings are open. This review concentrates on CPU/GPU synchronization, submission failure paths, and frame-generation state transitions; it is not an exhaustive audit of every RT shader, vendor library, or geometry algorithm.

### F9 — P1: GPU wait failures are treated as completed work

**Confidence:** high, directly visible in control flow. Runtime failure has not been reproduced here.

**Locations:** rt_d3d12.hpp:2214-2217; rr_d3d12.hpp:250-253; upscale_d3d12.hpp:364-367; fg_d3d12.hpp:615-618; fg_proxy_d3d12.hpp:541-544. Callers include RT Trace at 1029-1032 and 1093, RR Record at 259-268, and both FG Present implementations at 399-401 / 258-260.

Every listed WaitFor returns void. If the fence is incomplete, it registers an event and waits five seconds (RT/RR/upscaling) or ten seconds (FG), ignoring both the registration result and the wait result. It never verifies the requested fence value afterward. Event creation is also unchecked in these classes. Consequently timeout, failed event registration, or a failed wait all allow the caller to proceed as though the GPU completed.

Concrete failure sequence: hold a queue behind an unsatisfied dependency, wrap to a previously submitted slot, let WaitFor time out, then reach allocator Reset and overwrite/reuse slot data while the previous submission remains outstanding. Close/ReleaseFeature and history cleanup similarly release resources after the same ineffective completion gate. RT's newer WaitFor(trace_value_) is useful in the success case but inherits this defect.

An additional consequence is a stale event notification: a registration whose wait timed out can later wake another wait using the same event, even though the newer target has not completed. A successful wait alone is insufficient; the target fence must be rechecked. Device-removal sentinel values also need explicit failure handling rather than ordinary numeric completion.

Microsoft documents allocator Reset during outstanding GPU execution as undefined behavior: [ID3D12CommandAllocator::Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset).

**Impact:** resource lifetime violations, corrupted GPU work, or device removal after an existing stall/API failure. This may amplify an initial problem; it does not prove what caused the first freeze. F7's external watchdog is not a substitute for this gate: helper waits and watchdog expiry are independent, and the helper has no guarantee it is terminated before its timed-out wait returns.

**Proposed repair:** make completion waits return a checked outcome; validate event creation, registration, wait result, actual fence progress, and device removal. Stop recording, allocator reuse, feature destruction and resource recycling on failure. Establish a terminal helper/backend failure path with ownership retained until completion or safe teardown. Preserve game-fence recovery ordering; never fake GPU completion while the old producer can still access the resources.

**Required regression:** delay completion past the deadline on a healthy software device; inject event creation/registration/wait failures; deliver an older registration's event during a newer wait. Assert that no allocator reset, mapped-data overwrite, or resource/feature release happens before the exact target completes. Include both successful delayed completion and device-removal controls.

### F10 — P1: Frame generation submits command lists despite recording failures

**Confidence:** high. Distinct from F9: it also occurs when the wait completed successfully.

**Locations:** fg_d3d12.hpp:399-401 and 494-500; fg_proxy_d3d12.hpp:258-260 and 403-409.

Both FG implementations ignore the HRESULT from allocator Reset, command-list Reset, and command-list Close, then unconditionally call ExecuteCommandLists and advance shared/local completion bookkeeping. A failed Reset can leave an unusable recording state; a failed Close means a valid executable list has not been established. The RT/RR implementations already have more explicit recording checks, making the FG omission especially clear.

**Trigger:** an allocator/list reset failure or a recording validation/resource failure surfaced by Close. This is an error-path defect; it does not imply every normal FG frame is invalid.

**Impact:** invalid submission, dropped work or device removal, with the helper subsequently reporting request completion through the normal main.cpp answer path. Advancing bookkeeping does not prove that the intended copy/evaluation ran. Microsoft describes command-list submission validation and device-removal cases in [ExecuteCommandLists](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists).

**Proposed repair:** check each recording stage and submit only a successfully closed list. Mark the backend/request failed, retire the failed slot safely, and choose a deliberate failure acknowledgement/recovery path. Check queue Wait/Signal HRESULTs as part of that path so failure is not reported as a successfully scheduled dependency. Only use an ordered signal-only fallback when the queue/device is healthy and ownership permits it.

**Required regression:** inject Reset and Close failures independently after a successful wait for both DLSS-G and proxy FG. Assert no invalid list submission, no successful evaluation report, and a defined outcome for the game's outstanding fence. Include recovery/reinitialization or explicit feature disablement.

### F11 — P2: DLSS-G keeps the wrong color space after an HDR/SDR switch

**Confidence:** high for the state-machine defect; visual output was not tested.

**Locations:** fg_d3d12.hpp:734-745 (PrepareWindow), 345-353 (Close); ../dlaa_bridge.hpp PresentFrame writes fg_sdr per request; ../addon.cpp:1334 sets sdr_output from swap_chain_encoding. For comparison, fg_proxy_d3d12.hpp:223-237 explicitly includes chain_sdr_ in its recreation condition.

PrepareWindow returns immediately when width, height and pixel format match, without comparing the requested SDR/HDR interpretation. The code itself supports SDR and HDR in the same R10G10B10A2 format. Close deliberately retains the swap chain, so merely reopening shared textures does not correct this.

Concrete sequence: start DLSS-G in HDR, then switch the addon's output to SDR at the same resolution and format. The helper receives fg_sdr = 1 but returns before changing color space; the HDR/PQ setting remains. SDR-to-HDR has the inverse stale-state problem. Furthermore, the color-space lambda only sets HDR; it does not explicitly restore SDR on an existing chain.

**Impact:** incorrect colors/brightness in the FG window after an output-mode change. This is not evidence of the battle CTD.

**Proposed repair:** track the applied SDR/HDR color-space state independently of size/format; update it on same-size mode changes and explicitly apply both directions. Check color-space support and SetColorSpace1 results. Preserve required vendor resize/presentation sequencing.

**Required regression:** exercise HDR -> SDR -> HDR with fixed R10G10B10A2 size/format and with resource-set reopen. Verify the requested color-space calls/state, then compare the FG window against the game's output in a separately authorized visual test.

### F12 — P2: Preset-saving threads can persist an older selection last

**Confidence:** high for the ordering defect; scheduling-dependent behavior was not reproduced.

**Locations:** fg_d3d12.hpp:374-377 and 661-663; static result storage at 952.

Each preset change starts a detached thread carrying that selection. SavePreset serializes driver-profile writes with a mutex, but mutex acquisition order does not preserve thread creation or user selection order. If the worker for selection A is delayed and the later worker for selection B acquires the mutex first, B is saved first and then overwritten by A. The UI's preset_ remains B, so subsequent frames do not enqueue a correction. The shared completion status is also not tied to a selection/generation.

**Impact:** the persisted driver preset can disagree with the user's final choice, particularly after quick changes. This is not a demonstrated GPU crash or object-use-after-free: SavePreset and its result storage are static, and that part of the lifetime design was considered.

**Proposed repair:** use an ordered/coalescing worker or a synchronized latest-request generation. A stale worker must not overwrite a newer selection; expose completion only for the current request. Keep the worker's shutdown ownership explicit.

**Required regression:** gate the older worker before it acquires the save lock, let the newer selection complete, then release the older worker. Verify the persisted preset remains the latest user selection and its result is the one reported. Use a fake persistence layer, not the user's real NVIDIA profile.

### Suggested next repair order and handoff

1. F9 first: define a reliable completion/failure contract and update all helper callers. Do not merely lengthen the timeout.
2. F10: checked FG recording/submission and a failure path consistent with F9 and the bridge watchdog.
3. F11: color-space state transitions, as an independent patch.
4. F12: ordered preset persistence, as an independent patch.

Before implementing, recheck the current source against these locations and preserve concurrent work. Make each repair independently reviewable. A helper change requires a fresh numbered helper build and a matched addon/helper test pair; do not overwrite an installed helper. Run targeted backend regressions, then applicable shared-consumer builds if shared headers change. Vendor-specific FG and the original Attila battle still require separately authorized hardware/game testing.

No fixes are authorized by this report. The user requested review and a report-file update only.


## Additional non-shader C++ review (2026-11-09)

### Scope, baseline and limitations

Reviewed surrounding Total War C++ implementation paths, excluding Attila's shader source: frame generation, frame history, depth levels, AO filtering/pass setup, SSR setup, object-motion resource setup, RT capture/cluster-worker ownership, frame tracing, pass timing, Reflex, engine/graphics hooks and the Detours transaction wrapper. This is a focused source review of those paths, not a complete audit of the repository or every function in those files.

Used the repaired c314b0a6 tree to avoid reporting the already-fixed F1-F8 defects from the older original checkout. The five files supporting the new findings below (frame_generation.hpp, depth_levels.hpp, graphics_hooks.hpp, twrome2/engine_tweaks.hpp and utils/detour.hpp) also match the original checkout's saved text after line-ending normalization. The report alone was updated. No code edits, test/build execution, games, installed binaries, commits or pushes were involved.

### F13 — P1: Failed frame-generation setup is reused as if initialization succeeded

**Confidence:** high; directly traced through two consecutive calls. No runtime fault injection performed.

**Locations:** src/games/twattila/frame_generation.hpp:276-301 (DrawHudless initialization and failure cleanup), 377-380 (subsequent resource use).

DrawHudless initializes its context state, shaders, sampler and injection buffer only when objects.device differs from the current device. If any creation fails, the error branch resets the objects but retains objects.device = device1, then returns false.

On the next call on the same device, the device comparison passes and initialization is skipped. Once a readable source and matching bridge target are available, the code calls UpdateSubresource with objects.injection == nullptr, swaps using a missing context state and draws with missing shaders. No later readiness check protects these uses.

**Trigger:** a one-off failure of any of the initial creation calls, followed by another eligible HUD-less capture on the same device. A transient allocation failure can therefore turn into persistent invalid API use instead of a clean fallback.

**Impact:** invalid D3D11 calls and broken frame-generation capture; a crash is possible depending on runtime/driver handling. This is not a proven explanation of the original battle CTD.

**Proposed repair:** publish the device/readiness state only after the entire object set succeeds, or maintain an explicit failed/not-ready state checked by every consumer. On failure, use a clean fallback; if retrying, retry the complete setup rather than using partial resources. Also protect the scRGB present path, which consumes the same context state and shaders.

**Required regression:** inject failure independently into each initial creation API; call DrawHudless twice on the same device. Verify the second call either successfully reconstructs the whole set or returns without resource updates/draws using missing objects. Then validate successful HUD-less capture and scRGB presentation. Do not test by deliberately submitting null-resource calls to the user's game.

### F14 — P1: Failed engine-hook removal is swallowed before live addon unload

**Confidence:** high for the failure path and ownership loss; a live-unload crash was not reproduced.

**Locations:** src/games/twattila/graphics_hooks.hpp:532-539; src/games/twrome2/engine_tweaks.hpp:646-654. Call chain: Attila addon.cpp DetachAddon at 1973-1974, then AddonUninit at 2012 onward. Transaction implementation: src/utils/detour.hpp, internal::Transaction and ApplyTransaction.

Both Uninstall functions catch any detour-removal exception, ignore it on the assumption that the game is closing, and unconditionally clear their hook records. These functions are also called from AddonUninit during explicit addon unload while the game can remain alive.

A failed removal transaction does not establish that the patches were removed. For example, a failure beginning the transaction leaves the existing hooks untouched; a failure queuing a detach invokes the wrapper's transaction rollback. Nevertheless, these callers discard the records and return normally. AddonUninit proceeds to unregister the addon and permits its module to unload. A later game call can then follow a surviving detour into unloaded addon code.

This is separate from F2's ReShade/pipeline callback unregistration: native executable detours are a different ownership mechanism.

Microsoft documents that detour operations take effect at transaction commit, and abort reverses pending detach operations: [DetourTransactionBegin](https://github.com/microsoft/Detours/wiki/DetourTransactionBegin), [DetourTransactionAbort](https://github.com/microsoft/Detours/wiki/DetourTransactionAbort).

**Impact:** possible crash after explicit unload/reload if hook removal fails; hook tracking is also lost, preventing an ordinary retry. This finding is conditional on removal failure, not a claim that normal detachment always fails.

**Proposed repair:** retain hook ownership until removal is confirmed and log/propagate the failure. Define a safe live-unload strategy that prevents unloading executable code still reachable through hooks (for example, explicit module retention when the host cannot cancel unload), together with a retry/quiescence policy. AddonUninit returns void, so merely returning early is not proof that the host will keep the DLL loaded. Distinguish live unload from process termination.

**Required regression:** in a disposable hooked test process, inject transaction-begin, detach and commit failures. Verify hook records survive, the module remains loaded while any detour can target it, and the original function remains callable. Verify successful removal and repeated unload/reload separately.

### F15 — P2: One failed depth-level allocation disables rebuilding at that resolution

**Confidence:** high; directly visible cache-state transition.

**Locations:** src/games/twattila/depth_levels.hpp:91-97 and 127-134 (Build).

Build decides whether to allocate the depth-level texture and views solely from the cached width/height. It writes the new dimensions before allocation. On failure, it resets texture and the aggregate view but leaves those dimensions recorded. Every later call with the same device and dimensions skips allocation and returns nullptr, even after memory becomes available.

If allocation fails after some per-level views were created, level_writes/level_views are also retained by the failure branch. Their COM references can keep the partially constructed texture alive despite objects.texture.Reset(), until a size/device change clears the arrays.

**Trigger:** a transient failure creating the texture, aggregate SRV, or a per-level view during initial setup or a resolution change.

**Impact:** the AO depth-level optimization remains unavailable for the current resolution, and partial allocations can remain retained. This review did not inspect shader fallback behavior, so it does not claim AO itself disappears or that this alone causes a crash.

**Proposed repair:** stage the entire texture/view set and commit dimensions only on success, or explicitly invalidate dimensions/readiness on failure. Release all partial views. Retry in a bounded/backoff manner, or report a deliberate disabled state with an explicit recovery mechanism.

**Required regression:** fail each allocation/view-creation step once, then allow a successful call at the exact same dimensions. Verify successful recovery, complete view ownership, and no retained partial texture after a failed construction.

### Follow-up order and review boundaries

For repairs, prioritize F13 and F14 alongside the helper's F9/F10 safety work. F15 is a separate recovery/ownership fix. Keep the existing F11/F12 frame-generation correctness findings in the same backlog.

Potential issues involving cluster-worker teardown, frame-trace event cleanup, binding restoration and partial frame-history targets were examined but are not promoted to verified findings in this pass: some depend on host/runtime cleanup or are protected by caller-side reset logic. Do not treat their mention as confirmed additional defects.

This appendix adds three source-level findings, bringing the new open review backlog to F9-F15. It does not reopen the completed F1-F8 repairs, prove the original freeze's cause, or authorize implementation.

### F9 repair completed (2026-11-09, GPT-6 Astra)
All five D3D12 helper backends now verify the exact fence target, registration, event handle, deadline and removal sentinel. Failure ends the helper process without unwinding GPU owners; the existing bridge watchdog can recover only after process exit. Queue Wait/Signal failures use the same terminal contract, preventing an unsignalled local fence from being mistaken for completion. Event creation is checked. This deliberately replaces in-process recovery on these fatal synchronization paths with helper exit; recovery is still best effort, not a guarantee against TDR.
Validation: test/tw_gpu_completion/run.ps1 -Architecture amd64 passes real WARP delayed completion, timeout, an old registration waking a newer wait, plus controlled registration failure, removal sentinel, invalid/missing event and a disposable child proving no return after failure. Production helper built into build/tw_dlaa_helper90 with available vendor SDKs. No game or installed binary changed. Actual vendor evaluation and the original battle remain untested. User authorized implementation after the review; the earlier review-only restrictions describe that review phase.

### F10 repair completed (2026-11-09, GPT-6 Astra)
Both FG backends now check allocator Reset, command-list Reset and every Close before continuing. Failure uses F9's terminal helper exit, leaving game-fence rescue to the existing watchdog after process exit; no invalid list is submitted or normal success acknowledgement sent. Queue dependencies already use F9's checked contract. Validation: the shared failure gate's disposable child test passes for failed HRESULT; both production FG paths compile in helper91. Source review verifies the checks precede ExecuteCommandLists. Actual vendor FG Reset/Close fault injection has not been run; the child test proves the terminal gate rather than vendor API behavior. No game/driver profile/installed binary changed.

### F11 repair completed (2026-11-09, GPT-6 Astra)
DLSS-G tracks the successfully applied SDR/HDR state separately from dimensions/format. Same-size R10 mode changes now explicitly apply either SDR gamma/BT.709 or HDR PQ/BT.2020. New and resized chains check color-space support and SetColorSpace1; failure disables this backend rather than claiming the state applied. Existing resize sequencing is preserved. Validation: production helper92 build and source-path review (same-size changes, resize, new chain, Close/Open retaining a chain). Hardware/display visual verification remains required: fixed-resolution HDR -> SDR -> HDR and resource reopen. No shaders, game or installed files changed.
