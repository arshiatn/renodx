# TW helper lifetime regression

```powershell
./test/tw_helper_lifetime/run.ps1 -Architecture x86
./test/tw_helper_lifetime/run.ps1 -Architecture amd64
```

Requires Visual Studio Build Tools and Clang. The test includes the production bridge and creates a software D3D11 device. Its disposable child executable implements the PID-based IPC handshake and a software D3D12 queue; it does not load a game, vendor helper, or installed add-on. Generated executables stay in `build/astra_helper_test_<architecture>`.

Checks: missing helper cleanup; preservation of an occupied mapping's contents; rejection of occupied request/done/cluster objects; injected job-assignment, watcher-stop-event, watcher-handle-retention, and resume failures; actual child termination on failure; 20 natural/forced-exit reload cycles; watcher native thread completion; repeated shutdown; stable process handle count. Win32 calls alone are fault-injected; the bridge's Start/StopSession/Shutdown implementations are used unchanged.

The GPU cases queue actual software-device waits on three fence generations (two retired, one current) with the CPU pending flag clear. A delayed fake helper signals all three in the normal drain case; a stalled helper never signals, forcing shutdown recovery. Both require the game queue tail to complete and exact fence values without device removal. This covers shutdown recovery on WARP, not vendor GPU teardown or a game's battle stability. Manual unload/reload checks are listed in the consumer READMEs.

The fake helper uses a D3D12 queue, matching the real RT helper. Each generation interleaves game signals 1/3 and waits 2/4 with helper waits 1/3 and signals 2/4. A two-D3D11-device WARP probe stalls even without the bridge; it is not used as the RT-helper queue model. Recovery uses CPU fence signals after helper exit, so recovery cannot itself queue behind the GPU dependency.

F7 cases execute actual Request/Answer/BeforeSharedWrites with a helper that acknowledges but either exits or stalls before GPU submission. Initial, steady and deferred paths must reach exact fence completion and game queue tail without device removal. A healthy delayed D3D12 submission must retire watchdog ownership without killing the helper. A separate late-publication case registers a generation after helper exit and requires recovery. F5/F6 cases verify stale acknowledgement rejection, failed handle-transfer rollback and successful generation publication.
