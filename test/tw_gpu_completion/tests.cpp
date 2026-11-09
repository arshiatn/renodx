#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <thread>
#include <string>
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
int main(int argc, char**) {
  if (argc > 1) {
    FenceControl fence;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    RequireGpuCompletion(&fence, event, 1, 10);
    return 42; // Must never resume caller/resource destruction after failure.
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
  FenceControl control;
  CHECK(!WaitForGpu(&control, event, 1, 10));
  control.completed = UINT64_MAX;
  CHECK(!WaitForGpu(&control, event, 1, 10));
  CHECK(!WaitForGpu(fence.Get(), nullptr, 3, 10));
  CHECK(!WaitForGpu(fence.Get(), INVALID_HANDLE_VALUE, 3, 10));
  wchar_t path[MAX_PATH];
  CHECK(GetModuleFileNameW(nullptr, path, MAX_PATH));
  std::wstring command = L"\"" + std::wstring(path) + L"\" child";
  STARTUPINFOW startup = {.cb = sizeof(startup)};
  PROCESS_INFORMATION process = {};
  CHECK(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
  CHECK(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
  DWORD exit_code = 0;
  CHECK(GetExitCodeProcess(process.hProcess, &exit_code));
  CHECK(exit_code == ERROR_DEVICE_HARDWARE_ERROR);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  // Retire registrations before closing the test event.
  CHECK(SUCCEEDED(fence->Signal(3)));
  CloseHandle(event);
  std::puts("PASS: WARP completion, timeout, stale wake, registration failure, removed fence, missing/invalid event, terminal child exit");
}
