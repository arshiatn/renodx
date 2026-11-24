#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "src/games/twattila/dlaa_helper/gpu_completion.hpp"
using namespace twattila::dlaa_helper;
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)
struct FenceControl {
  uint64_t completed = 0;
  HRESULT registration = E_FAIL;
  HRESULT signal_result = S_OK;
  int signals = 0;
  uint64_t GetCompletedValue() { return completed; }
  HRESULT SetEventOnCompletion(uint64_t, HANDLE) { return registration; }
  HRESULT Signal(uint64_t value) {
    ++signals;
    if (SUCCEEDED(signal_result)) completed = value;
    return signal_result;
  }
};
// A real fence whose registrations are counted (the rescue's wait must not spin on a stale wake).
struct CountingFence {
  ID3D12Fence* fence = nullptr;
  int registrations = 0;
  uint64_t GetCompletedValue() { return fence->GetCompletedValue(); }
  HRESULT SetEventOnCompletion(uint64_t value, HANDLE event) {
    ++registrations;
    return fence->SetEventOnCompletion(value, event);
  }
  HRESULT Signal(uint64_t value) { return fence->Signal(value); }
};
// CONTROL: the rescue's signalling before G23 2a (2 s for the whole set, polled, timed by the tick count).
template <typename Fence>
bool OldSignalAbandoned(Fence* fence, uint64_t value, DWORD timeout) {
  const ULONGLONG started = GetTickCount64();
  for (uint64_t completed = fence->GetCompletedValue(); completed < value; completed = fence->GetCompletedValue()) {
    if (GetTickCount64() - started >= timeout) return false;
    if ((completed & 1u) != 0 && FAILED(fence->Signal(completed + 1))) return false;
    Sleep(1);
  }
  return true;
}
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

  // A D3D11.4 fence (the helper's DLSS: its waits before NGX frees a feature, G23 2b): completion after a queued Signal,
  // and a value queued behind a Wait on a gate (another device's shared fence, as the game's) only once the gate opens.
  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11DeviceContext> context11;
  CHECK(SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                    device11.GetAddressOf(), nullptr, context11.GetAddressOf())));
  ComPtr<ID3D11Device5> device11_5;
  ComPtr<ID3D11DeviceContext4> context11_4;
  CHECK(SUCCEEDED(device11.As(&device11_5)) && SUCCEEDED(context11.As(&context11_4)));
  ComPtr<ID3D11Fence> marker;
  CHECK(SUCCEEDED(device11_5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&marker))));
  CHECK(SUCCEEDED(context11_4->Signal(marker.Get(), 1)));
  context11->Flush();
  CHECK(WaitForGpu(marker.Get(), event, 1, 5000));
  ComPtr<ID3D12Fence> gate;
  CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&gate))));
  HANDLE gate_handle = nullptr;
  CHECK(SUCCEEDED(device->CreateSharedHandle(gate.Get(), nullptr, GENERIC_ALL, nullptr, &gate_handle)));
  ComPtr<ID3D11Fence> gate11;
  const HRESULT gate_opened = device11_5->OpenSharedFence(gate_handle, IID_PPV_ARGS(&gate11));
  CloseHandle(gate_handle);
  CHECK(SUCCEEDED(gate_opened));
  CHECK(SUCCEEDED(context11_4->Wait(gate11.Get(), 1)));
  CHECK(SUCCEEDED(context11_4->Signal(marker.Get(), 2)));
  context11->Flush();
  CHECK(!WaitForGpu(marker.Get(), event, 2, 50));
  CHECK(marker->GetCompletedValue() == 1);
  CHECK(SUCCEEDED(gate->Signal(1)));
  CHECK(WaitForGpu(marker.Get(), event, 2, 5000));
  // The release wait over the same fence: done, so the helper's handler is not called.
  WaitForGpuBeforeRelease(marker.Get(), event, 2, 30, "DLSS");
  CHECK(unproven_calls == 1 && unproven_owner == "live");

  // The rescue (G23 2a): the game on a fence of its own, waiting for each of the rescue's even values and signalling the next
  // odd one `gaps[i]` ms later, until `stop`.
  std::atomic<bool> stop_game = false;
  const auto late_game = [&](ID3D12Fence* chain, std::vector<DWORD> gaps) {
    return std::thread([&stop_game, chain, gaps = std::move(gaps)] {
      const HANDLE reached = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      for (size_t step = 0; step < gaps.size(); ++step) {
        const uint64_t even = step * 2;
        if (FAILED(chain->SetEventOnCompletion(even, reached))) break;
        while (!stop_game.load() && chain->GetCompletedValue() < even) WaitForSingleObject(reached, 20);
        if (stop_game.load()) break;
        Sleep(gaps[step]);
        chain->Signal(even + 1);
      }
      CloseHandle(reached);
    });
  };
  const auto new_chain = [&] {
    ComPtr<ID3D12Fence> chain;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&chain));
    return chain;
  };
  const HANDLE moved = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  const HANDLE game_alive = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // stands in for the game's process handle
  CHECK(moved != nullptr && game_alive != nullptr);
  HRESULT error = E_FAIL;
  int lates = 0;
  const auto late = [&](uint64_t) { ++lates; };
  {
    // A GPU that moves every 500 ms (10 pairs, about 5 s) is never late; today's 2 s for the whole set gave up on it.
    ComPtr<ID3D12Fence> chain = new_chain();
    std::thread game = late_game(chain.Get(), std::vector<DWORD>(10, 500));
    started = GetTickCount64();
    CHECK(RescueSet(chain.Get(), moved, game_alive, 20, true, late, &error) && error == S_OK && lates == 0);
    CHECK(GetTickCount64() - started >= 4500);
    game.join();
    ComPtr<ID3D12Fence> control_chain = new_chain();
    std::thread control_game = late_game(control_chain.Get(), std::vector<DWORD>(10, 500));
    started = GetTickCount64();
    const bool control_freed = OldSignalAbandoned(control_chain.Get(), 20, 2000);
    const ULONGLONG control_ms = GetTickCount64() - started;
    stop_game = true;
    control_game.join();
    stop_game = false;
    CHECK(!control_freed && control_ms >= 2000 && control_ms < 3000);  // CONTROL
  }
  {
    // A GPU 3 s behind, then moving: with every set before it freed one late line and no limit; else the 2 s.
    ComPtr<ID3D12Fence> chain = new_chain();
    std::thread game = late_game(chain.Get(), {3000, 500, 500, 500});
    CHECK(RescueSet(chain.Get(), moved, game_alive, 8, true, late, &error) && error == S_OK && lates == 1);
    game.join();
    ComPtr<ID3D12Fence> bounded_chain = new_chain();
    std::thread bounded_game = late_game(bounded_chain.Get(), {3000, 500, 500, 500});
    started = GetTickCount64();
    const bool bounded_freed = RescueSet(bounded_chain.Get(), moved, game_alive, 8, false, late, &error);
    const ULONGLONG bounded_ms = GetTickCount64() - started;
    stop_game = true;
    bounded_game.join();
    stop_game = false;
    CHECK(!bounded_freed && error == HRESULT_FROM_WIN32(ERROR_TIMEOUT) && lates == 1 && bounded_ms >= 2000 && bounded_ms < 2900);
  }
  {
    // The game ends during the wait: false with S_OK at its end, not a timeout.
    ComPtr<ID3D12Fence> chain = new_chain();
    std::thread ending([&] {
      Sleep(300);
      SetEvent(game_alive);
    });
    started = GetTickCount64();
    CHECK(!SignalAbandoned(chain.Get(), moved, game_alive, 2, INFINITE, &error) && error == S_OK);
    const ULONGLONG ended_ms = GetTickCount64() - started;
    ending.join();
    CHECK(ended_ms >= 290 && ended_ms < 1000);
    ResetEvent(game_alive);
  }
  {
    // Failures: a fence at all ones needs no call; a failed registration and a failed Signal end the set with their error.
    FenceControl removed = {.completed = UINT64_MAX, .registration = S_OK};
    CHECK(SignalAbandoned(&removed, moved, game_alive, 2, INFINITE, &error) && error == S_OK && removed.signals == 0);
    FenceControl unregistered = {.completed = 0, .registration = E_FAIL};
    CHECK(!SignalAbandoned(&unregistered, moved, game_alive, 2, INFINITE, &error) && error == E_FAIL);
    FenceControl lost = {.completed = 1, .registration = S_OK, .signal_result = DXGI_ERROR_DEVICE_REMOVED};
    CHECK(!SignalAbandoned(&lost, moved, game_alive, 2, INFINITE, &error) && error == DXGI_ERROR_DEVICE_REMOVED && lost.signals == 1);
  }
  {
    // A stale wake (the event already set) costs one more registration at most: no spin.
    ComPtr<ID3D12Fence> chain = new_chain();
    CountingFence counted = {.fence = chain.Get()};
    std::thread game = late_game(chain.Get(), {100});
    SetEvent(moved);
    CHECK(SignalAbandoned(&counted, moved, game_alive, 2, INFINITE, &error) && error == S_OK && counted.registrations <= 2);
    game.join();
  }
  CloseHandle(game_alive);
  CloseHandle(moved);

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
            "without and with a handler, D3D11.4 fence (signal, value behind a gate's wait), removed device (control, late, "
            "real WARP removal), logged terminal child exit; the rescue (a moving GPU never late, CONTROL the old 2 s; 3 s late:"
            " one line and freed, bounded: ERROR_TIMEOUT; the game's end; failures; no spin)");
}
