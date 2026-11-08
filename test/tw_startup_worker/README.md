# Total War startup-worker lifetime regression

Run from PowerShell with Visual Studio Build Tools, Clang and the repository dependencies installed:

```powershell
./test/tw_startup_worker/run.ps1 -Architecture x86
./test/tw_startup_worker/run.ps1 -Architecture amd64
```

This standalone test includes the real `engine_tests.hpp`. It loads no game, ReShade DLL or GPU device and changes no game settings. An option with no tweaks allows completion without writing memory. A fake logging export arms a thread-local destructor that blocks after the worker publishes `done`; unloading must wait until that destructor is released and the native thread exits. It also checks empty tests, cancellation against an absent DLL, restarting a pending worker, repeated stop, and process handle counts across 100 cycles.

Build outputs stay in `build/astra_worker_test_<architecture>`. This is separate from the runtime ReShade test suite and requires no game installation. Real add-on unload/reload remains a user-run check listed in each affected game's README.
