// Standalone regression against the production header; no game, GPU or ReShade DLL is loaded.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

extern "C" __declspec(dllexport) const char* const NAME = "Startup worker lifetime test";
#include "src/games/twattila/engine_tests.hpp"

namespace {
HANDLE cleanup_entered = nullptr;
HANDLE cleanup_resume = nullptr;
std::atomic<bool> block_cleanup = false;
std::atomic<bool> cleanup_exited = false;

struct Cleanup {
  bool armed = false;
  ~Cleanup() {
    if (!armed) return;
    SetEvent(cleanup_entered);
    WaitForSingleObject(cleanup_resume, INFINITE);
    cleanup_exited = true;
  }
};

void Check(bool passed, const char* message) {
  if (passed) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  TerminateProcess(GetCurrentProcess(), 1);
  std::abort();
}
}  // namespace

extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {
  thread_local Cleanup cleanup;
  cleanup.armed = block_cleanup.load();
}

int main() {
  namespace worker = twattila::engine_tests;
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  std::atomic<bool> done = false;
  std::atomic<bool> stop = false;
  std::vector<worker::EngineTest> tests;

  worker::StartAtStart(nullptr, &tests, &done, &stop);
  Check(done && worker::at_start_thread == nullptr, "empty tests must not start a thread");
  worker::StopAtStart(&stop);
  worker::StopAtStart(&stop);

  // An empty option changes no memory. Trace arms thread-local cleanup, after which the real worker publishes done.
  tests.push_back({.at_start = true});
  cleanup_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  cleanup_resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Check(cleanup_entered != nullptr && cleanup_resume != nullptr, "create cleanup gates");
  const HANDLE shutdown_begin = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  const HANDLE shutdown_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Check(shutdown_begin != nullptr && shutdown_ready != nullptr, "create shutdown gates");
  std::atomic<bool> stopped = false;
  // A new Windows thread cannot start during another thread's loader-lock TLS cleanup.
  // Start the coordinator before deliberately blocking that cleanup.
  std::thread shutdown([&] {
    SetEvent(shutdown_ready);
    WaitForSingleObject(shutdown_begin, INFINITE);
    worker::StopAtStart(&stop);
    stopped = true;
  });
  Check(WaitForSingleObject(shutdown_ready, 5000) == WAIT_OBJECT_0, "shutdown coordinator is running");
  block_cleanup = true;
  worker::StartAtStart(nullptr, &tests, &done, &stop);
  Check(WaitForSingleObject(cleanup_entered, 5000) == WAIT_OBJECT_0, "reach worker thread cleanup");
  Check(done, "the production progress flag precedes thread exit");
  Check(WaitForSingleObject(worker::at_start_thread, 0) == WAIT_TIMEOUT, "worker is still alive after done");
  SetEvent(shutdown_begin);
  const ULONGLONG deadline = GetTickCount64() + 5000;
  while (!stop && GetTickCount64() < deadline) Sleep(1);
  Check(stop, "shutdown entered the production stop path");
  Sleep(100);
  Check(!stopped, "unload must wait through thread cleanup");
  SetEvent(cleanup_resume);
  shutdown.join();
  Check(cleanup_exited && stopped && worker::at_start_thread == nullptr, "unload joins and closes the worker");
  block_cleanup = false;
  CloseHandle(cleanup_entered);
  CloseHandle(cleanup_resume);
  CloseHandle(shutdown_begin);
  CloseHandle(shutdown_ready);

  DWORD handles_before = 0;
  DWORD handles_after = 0;
  Check(GetProcessHandleCount(GetCurrentProcess(), &handles_before), "initial handle count");
  for (int iteration = 0; iteration < 100; ++iteration) {
    worker::StartAtStart(L"renodx-test-no-such-module.dll", &tests, &done, &stop);
    Check(!done && worker::at_start_thread != nullptr, "unresolved module keeps the worker pending");
    // Start must also join a prior worker, rather than overwrite its handle.
    worker::StartAtStart(L"renodx-test-no-such-module.dll", &tests, &done, &stop);
    worker::StopAtStart(&stop);
    Check(done && worker::at_start_thread == nullptr, "cancellation completes before returning");
    worker::StopAtStart(&stop);
  }
  Check(GetProcessHandleCount(GetCurrentProcess(), &handles_after), "final handle count");
  Check(handles_after == handles_before, "repeated start/restart/stop must not leak handles");
  std::printf("PASS: empty, done-before-exit, cancellation, restart, idempotent stop; 100 cycles; handles %lu -> %lu\n",
              handles_before, handles_after);
}
