// Standalone tests of the production bridge's IPC/process/thread lifetime. No game or vendor helper runs.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
unsigned fail_transfer_at = 0;
unsigned transfer_calls = 0;
bool fail_assign = false;
bool fail_resume = false;
bool fail_duplicate = false;
bool fail_stop_event = false;
HANDLE child_witness = nullptr;
BOOL WINAPI TestAssignProcessToJobObject(HANDLE job, HANDLE process);
DWORD WINAPI TestResumeThread(HANDLE thread);
BOOL WINAPI TestDuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, HANDLE* target,
                               DWORD access, BOOL inherit, DWORD options);
HANDLE WINAPI TestCreateEventW(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL state, LPCWSTR name);
}

#define AssignProcessToJobObject TestAssignProcessToJobObject
#define ResumeThread TestResumeThread
#define DuplicateHandle TestDuplicateHandle
#define CreateEventW TestCreateEventW
#include "src/games/twattila/dlaa_bridge.hpp"
#undef AssignProcessToJobObject
#undef ResumeThread
#undef DuplicateHandle
#undef CreateEventW

extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

namespace {
namespace bridge_api = twattila::dlaa_bridge;

void Check(bool passed, const char* message) {
  if (passed) return;
  std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", message, GetLastError());
  TerminateProcess(GetCurrentProcess(), 1);
  std::abort();
}

BOOL WINAPI TestAssignProcessToJobObject(HANDLE job, HANDLE process) {
  Check(child_witness == nullptr, "previous child witness closed");
  Check(DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &child_witness, SYNCHRONIZE, FALSE, 0),
        "retain child witness");
  return fail_assign ? FALSE : AssignProcessToJobObject(job, process);
}

DWORD WINAPI TestResumeThread(HANDLE thread) {
  return fail_resume ? static_cast<DWORD>(-1) : ResumeThread(thread);
}

BOOL WINAPI TestDuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, HANDLE* target,
                               DWORD access, BOOL inherit, DWORD options) {
  if (fail_transfer_at != 0 && source_process == GetCurrentProcess() && target_process != GetCurrentProcess()
      && (options & DUPLICATE_CLOSE_SOURCE) != 0 && ++transfer_calls == fail_transfer_at) {
    CloseHandle(source); // Match DUPLICATE_CLOSE_SOURCE even on failure.
    *target = nullptr;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  return fail_duplicate ? FALSE : DuplicateHandle(source_process, source, target_process, target, access, inherit, options);
}

HANDLE WINAPI TestCreateEventW(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL state, LPCWSTR name) {
  return (fail_stop_event && name == nullptr) ? nullptr : CreateEventW(attributes, manual, state, name);
}

void CheckClosed() {
  const auto& bridge = bridge_api::bridge;
  Check(bridge.mapping == nullptr && bridge.shared == nullptr && bridge.cluster_mapping == nullptr && bridge.cluster_data == nullptr
            && bridge.request == nullptr && bridge.done == nullptr && bridge.process == nullptr && bridge.job == nullptr
            && bridge.watcher == nullptr && bridge.watcher_stop == nullptr && bridge.rescue_fence == nullptr,
        "all IPC, process and thread ownership released");
  if (child_witness != nullptr) {
    Check(WaitForSingleObject(child_witness, 0) == WAIT_OBJECT_0, "child process has actually terminated");
    CloseHandle(child_witness);
    child_witness = nullptr;
  }
}
}

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--missing") != 0) {
    // The fake helper uses the real PID-based IPC contract and remains alive until its request or owning job ends.
    const std::wstring name = L"Local\\RenoDX_TW_DLAA_" + std::to_wstring(std::strtoul(argv[1], nullptr, 10));
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    HANDLE done = OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L"_done").c_str());
    HANDLE request = OpenEventW(SYNCHRONIZE, FALSE, (name + L"_request").c_str());
    Check(mapping != nullptr && done != nullptr && request != nullptr, "helper opens IPC");
    auto* shared = static_cast<bridge_api::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(bridge_api::Shared)));
    Check(shared != nullptr, "helper maps shared block");
    shared->ready = 1;
    SetEvent(done);
    Check(WaitForSingleObject(request, 15000) == WAIT_OBJECT_0, "natural-exit request before timeout");
    if (shared->rt_request == 997 || shared->rt_request == 998) {
      const bool stalled = shared->rt_request == 998;
      shared->done_value = shared->value + 1;
      shared->evaluated = 1;
      SetEvent(done); // Deliberately acknowledge without any GPU submission.
      Sleep(stalled ? 15000 : 100);
      return 0;
    }
    if (shared->rt_request == 999 || shared->rt_request == 996) {
      Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
      Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
      Microsoft::WRL::ComPtr<ID3D12Device> device;
      Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
      Check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "helper DXGI factory");
      Check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))), "helper WARP adapter");
      Check(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))), "helper D3D12 device");
      D3D12_COMMAND_QUEUE_DESC desc = {};
      desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
      Check(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))), "helper independent queue");
      const bool single_request = shared->rt_request == 996;
      if (single_request) {
        shared->done_value = shared->value + 1;
        shared->evaluated = 1;
        SetEvent(done);
      }
      Sleep(100);  // CPU acknowledgement can precede worker GPU submission.
      const std::array<uint64_t, 3> inputs = single_request
          ? std::array<uint64_t, 3>{shared->fence, 0, 0}
          : std::array<uint64_t, 3>{shared->color, shared->depth, shared->motion};
      for (uint64_t handle : inputs) {
        if (handle == 0) continue;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        Check(SUCCEEDED(device->OpenSharedHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle)),
                                               IID_PPV_ARGS(fence.GetAddressOf()))), "helper opens retired generation");
        for (uint64_t value : {2u, 4u}) {
          if (single_request && value == 4) break;
          Check(SUCCEEDED(queue->Wait(fence.Get(), value - 1)), "helper waits for game input");
          Check(SUCCEEDED(queue->Signal(fence.Get(), value)), "helper signals completion");
        }
      }
      WaitForSingleObject(request, 15000);  // Parent owns termination after GPU completion.
    }
    return 0;
  }
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                  device.GetAddressOf(), nullptr, nullptr)), "create software D3D11 device");
  if (argc > 1) {
    Check(!bridge_api::internal::Start(device.Get()), "missing executable must fail startup");
    CheckClosed();
    bridge_api::Shutdown();
    CheckClosed();
    std::puts("PASS: missing helper cleans partial IPC");
    return 0;
  }
  const std::wstring name = L"Local\\RenoDX_TW_DLAA_" + std::to_wstring(GetCurrentProcessId());
  HANDLE occupied = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(bridge_api::Shared), name.c_str());
  Check(occupied != nullptr, "occupy old session mapping");
  auto* old_shared = static_cast<bridge_api::Shared*>(MapViewOfFile(occupied, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(bridge_api::Shared)));
  Check(old_shared != nullptr, "map old session");
  old_shared->generation = 12345;
  Check(!bridge_api::internal::Start(device.Get()), "old session must not be reused");
  Check(old_shared->generation == 12345, "old session contents unchanged");
  CheckClosed();
  bridge_api::Shutdown();
  UnmapViewOfFile(old_shared);
  CloseHandle(occupied);
  for (const wchar_t* suffix : {L"_request", L"_done", L"_clusters"}) {
    const std::wstring object_name = name + suffix;
    occupied = (suffix == std::wstring(L"_clusters"))
                   ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, object_name.c_str())
                   : CreateEventW(nullptr, FALSE, FALSE, object_name.c_str());
    Check(occupied != nullptr, "occupy old session object");
    Check(!bridge_api::internal::Start(device.Get()), "occupied object must reject startup");
    CheckClosed();
    bridge_api::Shutdown();
    CloseHandle(occupied);
  }
  for (bool* failure : {&fail_assign, &fail_resume, &fail_duplicate, &fail_stop_event}) {
    *failure = true;
    Check(!bridge_api::internal::Start(device.Get()), "injected launch failure rejected");
    CheckClosed();
    bridge_api::Shutdown();
    *failure = false;
  }
  Check(bridge_api::internal::Start(device.Get()), "start for failed acknowledgement");
  Check(WaitForSingleObject(bridge_api::bridge.done, 5000) == WAIT_OBJECT_0, "failed-ack helper ready");
  bridge_api::bridge.pending = true;
  bridge_api::bridge.value = 1;
  bridge_api::bridge.width = 123;
  bridge_api::bridge.shared->generation = 456;
  bridge_api::bridge.shared->color = 789;
  bridge_api::bridge.shared->done_value = 0;
  SetEvent(bridge_api::bridge.done);
  Check(!bridge_api::Prepare(device.Get(), 32, 32), "reject stale acknowledgement");
  Check(bridge_api::bridge.failed && bridge_api::bridge.width == 123
        && bridge_api::bridge.shared->generation == 456 && bridge_api::bridge.shared->color == 789,
        "failed acknowledgement preserves resource generation before allocation");
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: failed acknowledgement stops Prepare before resource mutation");
  for (unsigned failure : {1u, 2u, 5u}) {
    Check(bridge_api::internal::Start(device.Get()), "start transfer rollback test");
    Check(WaitForSingleObject(bridge_api::bridge.done, 5000) == WAIT_OBJECT_0, "rollback helper ready");
    auto& state = bridge_api::bridge;
    state.shared->generation = 321;
    state.shared->color = 654;
    DWORD helper_before = 0, helper_after = 0;
    Check(GetProcessHandleCount(state.process, &helper_before), "helper handles before failed transfer");
    fail_transfer_at = failure;
    transfer_calls = 0;
    Check(!bridge_api::Prepare(device.Get(), 16, 16), "injected transfer fails Prepare");
    fail_transfer_at = 0;
    Check(transfer_calls == failure, "reached requested transfer failure");
    Check(GetProcessHandleCount(state.process, &helper_after), "helper handles after rollback");
    Check(helper_before == helper_after, "remote handles rolled back without leak");
    Check(state.shared->generation == 321 && state.shared->color == 654 && state.width == 0
          && !state.color.texture && !state.fence, "partial resources discarded without publication");
    bridge_api::Shutdown();
    CheckClosed();
  }
  std::puts("PASS: first/second/fence transfer failures roll back remote ownership and publication");
  Check(bridge_api::internal::Start(device.Get()), "start successful publication test");
  Check(WaitForSingleObject(bridge_api::bridge.done, 5000) == WAIT_OBJECT_0, "publication helper ready");
  Check(bridge_api::Prepare(device.Get(), 16, 16), "complete resource setup publishes successfully");
  Check(bridge_api::bridge.width == 16 && bridge_api::bridge.shared->generation == 1
        && bridge_api::bridge.shared->color != 0 && bridge_api::bridge.shared->fence != 0
        && bridge_api::bridge.rescue_fence != nullptr, "complete generation has texture and both fence handles");
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: successful Prepare publishes complete generation");
  DWORD before = 0;
  DWORD after = 0;
  Check(GetProcessHandleCount(GetCurrentProcess(), &before), "initial handle count");
  for (int iteration = 0; iteration < 20; ++iteration) {
    Check(bridge_api::internal::Start(device.Get()), "start a fresh session");
    auto& bridge = bridge_api::bridge;
    Check(WaitForSingleObject(bridge.done, 5000) == WAIT_OBJECT_0 && bridge.shared->ready == 1, "fake helper ready");
    Check(bridge.shared->generation == 0, "fresh session has no old generation");
    HANDLE watcher_witness = nullptr;
    Check(DuplicateHandle(GetCurrentProcess(), bridge.watcher, GetCurrentProcess(), &watcher_witness,
                          SYNCHRONIZE, FALSE, 0), "retain watcher witness");
    if (iteration % 2 == 0) {
      SetEvent(bridge.request);
      Check(WaitForSingleObject(child_witness, 5000) == WAIT_OBJECT_0, "natural helper exit");
      // Watcher remains alive until shutdown to cover late in-flight fence publication.
    }
    bridge_api::Shutdown();
    Check(WaitForSingleObject(watcher_witness, 0) == WAIT_OBJECT_0, "shutdown joined actual watcher exit");
    CloseHandle(watcher_witness);
    CheckClosed();
    bridge_api::Shutdown();
    CheckClosed();
  }
  Check(GetProcessHandleCount(GetCurrentProcess(), &after), "final handle count");
  Check(before == after, "repeated reload does not leak handles");
  std::printf("PASS: mapping/event collisions; job/resume/watcher failures; 20 reloads, natural/forced exit, joined watcher, idempotence; handles %lu -> %lu\n", before, after);
  Microsoft::WRL::ComPtr<ID3D11Device5> device5;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
  device->GetImmediateContext(context.GetAddressOf());
  Check(SUCCEEDED(device.As(&device5)) && SUCCEEDED(context.As(&context4)), "game shared fence support");
  Check(bridge_api::internal::Start(device.Get()), "start healthy watchdog control");
  Check(WaitForSingleObject(bridge_api::bridge.done, 5000) == WAIT_OBJECT_0, "healthy helper ready");
  Check(bridge_api::Prepare(device.Get(), 16, 16), "healthy resources");
  Check(bridge_api::internal::Request(context.Get(), bridge_api::EVALUATE, [](bridge_api::Shared* shared) {
    shared->rt_request = 996;
  }), "healthy acknowledged request");
  context->Flush();
  const ULONGLONG healthy_until = GetTickCount64() + 3000;
  bool retired = false;
  while (GetTickCount64() < healthy_until) {
    {
      const std::shared_lock lock(bridge_api::bridge.watchdog.mutex);
      retired = bridge_api::bridge.watchdog.fences.empty();
    }
    if (retired) break;
    Sleep(1);
  }
  Check(retired && bridge_api::bridge.fence->GetCompletedValue() == 2
        && !bridge_api::bridge.watchdog.failed.load()
        && WaitForSingleObject(bridge_api::bridge.process, 0) == WAIT_TIMEOUT,
        "healthy GPU completion retires ownership without killing helper");
  bridge_api::Shutdown(); CheckClosed();
  std::puts("PASS: healthy acknowledged work completes and retires without false watchdog failure");
  for (bool stall : {false, true}) {
    for (unsigned mode : {0u, 1u, 2u}) {
      Check(bridge_api::internal::Start(device.Get()), "start watchdog test");
      Check(WaitForSingleObject(bridge_api::bridge.done, 5000) == WAIT_OBJECT_0, "watchdog helper ready");
      Check(bridge_api::Prepare(device.Get(), 16, 16), "watchdog resource generation");
      bridge_api::bridge.steady[bridge_api::EVALUATE] = mode == 1;
      const auto kind = mode == 2 ? bridge_api::PRESENT_FRAME : bridge_api::EVALUATE;
      Check(bridge_api::internal::Request(context.Get(), kind, [&](bridge_api::Shared* shared) {
        shared->rt_request = stall ? 998 : 997;
      }), "initial/steady/deferred request acknowledged");
      if (bridge_api::bridge.pending) Check(bridge_api::internal::Answer(), "consume steady CPU acknowledgement");
      if (mode == 2) bridge_api::BeforeSharedWrites(context.Get());
      Check(bridge_api::bridge.waiting.load() == 0, "CPU pending marker cleared before GPU recovery");
      Microsoft::WRL::ComPtr<ID3D11Fence> tail;
      Check(SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&tail))), "watchdog tail");
      Check(SUCCEEDED(context4->Signal(tail.Get(), 1)), "watchdog tail signal");
      context->Flush();
      const ULONGLONG until = GetTickCount64() + 8000;
      while (tail->GetCompletedValue() == 0 && GetTickCount64() < until) Sleep(1);
      Check(tail->GetCompletedValue() == 1 && bridge_api::bridge.fence->GetCompletedValue() == 2,
            "watchdog releases actual GPU wait without device removal");
      Check(bridge_api::bridge.watchdog.failed.load(), "watchdog reports helper failure");
      Check(WaitForSingleObject(bridge_api::bridge.process, 0) == WAIT_OBJECT_0, "helper exited before recovery");
      if (!stall && mode == 0) {
        Microsoft::WRL::ComPtr<ID3D11Fence> late;
        Check(SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&late))), "late generation");
        Check(SUCCEEDED(context4->Signal(late.Get(), 1)), "late input"); context->Flush();
        Check(SUCCEEDED(context4->Wait(late.Get(), 2)), "late wait");
        Check(SUCCEEDED(context4->Signal(tail.Get(), 2)), "late tail"); context->Flush();
        {
          const std::unique_lock lock(bridge_api::bridge.watchdog.mutex);
          bridge_api::bridge.watchdog.fences.push_back({.fence = late, .value = 2, .last_progress = GetTickCount64()});
        }
        const ULONGLONG late_until = GetTickCount64() + 3000;
        while (tail->GetCompletedValue() < 2 && GetTickCount64() < late_until) Sleep(1);
        Check(tail->GetCompletedValue() == 2 && late->GetCompletedValue() == 2, "late publication after helper exit recovered");
      }
      bridge_api::Shutdown(); CheckClosed();
      std::printf("PASS: watchdog mode=%u stalled=%d, CPU ack before GPU work\n", mode, stall);
    }
  }
  for (bool stall : {false, true}) {
    Check(bridge_api::internal::Start(device.Get()), "start helper for queued GPU work");
    auto& bridge = bridge_api::bridge;
    Check(WaitForSingleObject(bridge.done, 5000) == WAIT_OBJECT_0, "GPU test helper ready");
    std::array<Microsoft::WRL::ComPtr<ID3D11Fence>, 3> generations;
    std::array<uint64_t*, 3> handles = {&bridge.shared->color, &bridge.shared->depth, &bridge.shared->motion};
    for (size_t index = 0; index < generations.size(); ++index) {
      Check(SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(generations[index].GetAddressOf()))),
            "create fence generation");
      bridge.fence = generations[index];
      bridge.completion_value = 4;
      HANDLE shared = nullptr;
      Check(SUCCEEDED(bridge.fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &shared)), "share fence generation");
      *handles[index] = bridge_api::internal::ShareWithHelper(shared);
      Check(*handles[index] != 0, "duplicate fence into helper");
      for (uint64_t value : {1u, 3u}) {
        Check(SUCCEEDED(context4->Signal(bridge.fence.Get(), value)), "queue game input signal");
        context->Flush();
        Check(SUCCEEDED(context4->Wait(bridge.fence.Get(), value + 1)), "queue game GPU dependency");
      }
      if (index == 0) {
        bridge_api::internal::RetireFence();  // Prepare retains the old generation before replacing it.
        bridge.fence.Reset();
      } else if (index == 1) {
        bridge_api::Release();               // Device/resource release also retains uncompleted work.
      }
    }
    Check(bridge.retired_fences.size() == 2 && bridge.waiting.load() == 0, "old generations retained without CPU pending flag");
    Microsoft::WRL::ComPtr<ID3D11Fence> game_finished;
    Check(SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(game_finished.GetAddressOf()))), "game tail fence");
    Check(SUCCEEDED(context4->Signal(game_finished.Get(), 1)), "game signal after all helper waits");
    context->Flush();
    if (!stall) {
      bridge.shared->rt_request = 999;
      SetEvent(bridge.request);
    }
    const ULONGLONG started = GetTickCount64();
    bridge_api::Shutdown();
    const ULONGLONG elapsed = GetTickCount64() - started;
    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (game_finished->GetCompletedValue() == 0 && GetTickCount64() < deadline) Sleep(1);
    std::fprintf(stderr, "GPU case stall=%d elapsed=%llu tail=%llu generations=%llu/%llu/%llu\n", stall, elapsed, game_finished->GetCompletedValue(), generations[0]->GetCompletedValue(), generations[1]->GetCompletedValue(), generations[2]->GetCompletedValue());
    Check(game_finished->GetCompletedValue() == 1, "game GPU progresses past every retired/current helper wait");
    for (const auto& generation : generations) Check(generation->GetCompletedValue() == 4, "completion without device removal");
    CheckClosed();
    Check(bridge.retired_fences.empty(), "retired ownership released");
    Check(stall ? elapsed >= 5000 : elapsed < 5000, "drain precedes forced recovery");
    std::printf("PASS: three GPU fence generations, %s, game queue completed, shutdown %llu ms\n",
                stall ? "stalled helper recovered" : "acknowledged work drained", elapsed);
  }
}
