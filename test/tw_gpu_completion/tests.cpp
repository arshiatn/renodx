#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include "src/games/twattila/dlaa_helper/gpu_completion.hpp"
using namespace twattila::dlaa_helper;
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)
struct FenceControl {
  uint64_t completed = 0;
  HRESULT registration = E_FAIL;
  uint64_t GetCompletedValue() { return completed; }
  HRESULT SetEventOnCompletion(uint64_t, HANDLE) { return registration; }
};
// What the helper exe installs (main.cpp), with the line in a file of the test's.
static const wchar_t* stop_log = nullptr;
static int unproven_calls = 0;
static std::string unproven_owner;
int wmain(int argc, wchar_t** argv) {
  if (argc > 2) {
    // The helper's side of a live device that never finishes: one line with the reason, then the exit, no unwinding.
    stop_log = argv[2];
    unproven_gpu_completion = [](const char* owner, uint64_t completed, uint64_t wanted) {
      FILE* log = nullptr;
      _wfopen_s(&log, stop_log, L"w");
      if (log != nullptr) {
        std::fprintf(log, "stopped: %s at %llu, waited for %llu\n", owner, completed, wanted);
        std::fclose(log);
      }
      TerminateProcess(GetCurrentProcess(), ERROR_DEVICE_HARDWARE_ERROR);
    };
    FenceControl fence = {.registration = S_OK};
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WaitForGpuBeforeRelease(&fence, event, 1, 10, "the test's owner");
    return 42;  // Must never resume resource destruction after the failure.
  }
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12CommandQueue> queue;
  CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
  CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
  CHECK(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
  CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
  D3D12_COMMAND_QUEUE_DESC desc = {};
  CHECK(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  CHECK(event != nullptr);
  // A live device: only the fence at the target is completion.
  CHECK(WaitForGpu(fence.Get(), event, 0, 20));
  CHECK(!WaitForGpu(fence.Get(), event, 1, 20));
  // An old timed-out registration wakes the next wait before its target.
  std::atomic<bool> producer_ok = true;
  std::thread producer([&] {
    Sleep(25);
    if (FAILED(queue->Signal(fence.Get(), 1))) producer_ok = false;
    Sleep(35);
    if (FAILED(queue->Signal(fence.Get(), 2))) producer_ok = false;
  });
  const bool completed = WaitForGpu(fence.Get(), event, 2, 2000);
  const auto value = fence->GetCompletedValue();
  producer.join();
  CHECK(completed && value >= 2 && producer_ok);
  // A failed registration and a missing or invalid event on a live device prove nothing.
  FenceControl control;
  CHECK(!WaitForGpu(&control, event, 1, 10));
  CHECK(!WaitForGpu(fence.Get(), nullptr, 3, 10));
  CHECK(!WaitForGpu(fence.Get(), INVALID_HANDLE_VALUE, 3, 10));
  // After a start that failed between the fence and its event (nothing submitted), and with no fence at all: nothing to
  // wait for.
  CHECK(WaitForGpu(fence.Get(), nullptr, 2, 10));
  CHECK(WaitForGpu(static_cast<ID3D12Fence*>(nullptr), nullptr, 1, 10));

  // In a game's process (no handler: Tales of Arise's add-on) an unproven wait before a release returns to its owner.
  ULONGLONG started = GetTickCount64();
  WaitForGpuBeforeRelease(fence.Get(), event, 3, 30, "no handler");
  CHECK(GetTickCount64() - started < 2000);
  // In the helper (a handler): called once for a live device over its time, with what it waited for; not for a done wait.
  unproven_gpu_completion = [](const char* owner, uint64_t completed, uint64_t wanted) {
    if (completed == 2 && wanted == 3) ++unproven_calls;
    unproven_owner = owner;
  };
  WaitForGpuBeforeRelease(fence.Get(), event, 2, 30, "done");
  CHECK(unproven_calls == 0 && unproven_owner.empty());
  WaitForGpuBeforeRelease(fence.Get(), event, 3, 30, "live");
  CHECK(unproven_calls == 1 && unproven_owner == "live");

  // A removed device (a GPU reset: its fence reads all ones) leaves nothing to wait for: every wait returns at once, also
  // for values never signalled, and the handler stays out of it, so the owner can make a new device.
  control.completed = UINT64_MAX;
  CHECK(WaitForGpu(&control, event, 1, 10));
  // A removal between the first read and the registration.
  struct RemovedAtRegistration {
    uint64_t completed = 0;
    uint64_t GetCompletedValue() { return completed; }
    HRESULT SetEventOnCompletion(uint64_t, HANDLE) {
      completed = UINT64_MAX;
      return DXGI_ERROR_DEVICE_REMOVED;
    }
  } removed_late;
  CHECK(WaitForGpu(&removed_late, event, 1, 10));
  ComPtr<ID3D12Device5> device5;
  CHECK(SUCCEEDED(device.As(&device5)));
  device5->RemoveDevice();
  CHECK(FAILED(device->GetDeviceRemovedReason()));
  CHECK(fence->GetCompletedValue() == UINT64_MAX);
  started = GetTickCount64();
  CHECK(WaitForGpu(fence.Get(), event, 1000, 5000));
  CHECK(WaitForGpu(fence.Get(), nullptr, 1000, 5000));
  WaitForGpuBeforeRelease(fence.Get(), event, 1000, 5000, "removed");
  CHECK(GetTickCount64() - started < 1000);
  CHECK(unproven_calls == 1 && unproven_owner == "live");
  unproven_gpu_completion = nullptr;

  // The helper's handler in a child: its line is written before the process ends with the helper's exit code.
  wchar_t path[MAX_PATH];
  CHECK(GetModuleFileNameW(nullptr, path, MAX_PATH));
  const std::wstring log_path = std::wstring(path) + L".stop.log";
  DeleteFileW(log_path.c_str());
  std::wstring command = L"\"" + std::wstring(path) + L"\" stop \"" + log_path + L"\"";
  STARTUPINFOW startup = {.cb = sizeof(startup)};
  PROCESS_INFORMATION process = {};
  CHECK(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
  CHECK(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
  DWORD exit_code = 0;
  CHECK(GetExitCodeProcess(process.hProcess, &exit_code));
  CHECK(exit_code == ERROR_DEVICE_HARDWARE_ERROR);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  FILE* log = nullptr;
  CHECK(_wfopen_s(&log, log_path.c_str(), L"r") == 0 && log != nullptr);
  char line[128] = {};
  CHECK(std::fgets(line, sizeof(line), log) != nullptr);
  std::fclose(log);
  CHECK(std::strcmp(line, "stopped: the test's owner at 0, waited for 1\n") == 0);
  CloseHandle(event);
  std::puts("PASS: WARP completion, timeout, stale wake, failed registration, missing/invalid event, no fence, release wait "
            "without and with a handler, removed device (control, late, real WARP removal), logged terminal child exit");
}
