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
bool helper_writes_during_transfer = false;
std::atomic<unsigned> processes_created = 0;
std::atomic<unsigned> warnings = 0;
HANDLE child_witness = nullptr;
BOOL WINAPI TestAssignProcessToJobObject(HANDLE job, HANDLE process);
DWORD WINAPI TestResumeThread(HANDLE thread);
BOOL WINAPI TestDuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, HANDLE* target,
                               DWORD access, BOOL inherit, DWORD options);
HANDLE WINAPI TestCreateEventW(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL state, LPCWSTR name);
BOOL WINAPI TestCreateProcessW(LPCWSTR application, LPWSTR command, LPSECURITY_ATTRIBUTES process_attributes,
                              LPSECURITY_ATTRIBUTES thread_attributes, BOOL inherit, DWORD flags, LPVOID environment,
                              LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION information);
}

#define AssignProcessToJobObject TestAssignProcessToJobObject
#define ResumeThread TestResumeThread
#define DuplicateHandle TestDuplicateHandle
#define CreateEventW TestCreateEventW
#define CreateProcessW TestCreateProcessW
#include "src/games/twattila/dlaa_bridge.hpp"
#undef AssignProcessToJobObject
#undef ResumeThread
#undef DuplicateHandle
#undef CreateEventW
#undef CreateProcessW
#include "src/games/twattila/dlaa_helper/gpu_completion.hpp"

// ReShade's log, counted: a failure is said once, not at every call after it.
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int level, const char*) {
  if (level <= 2) ++warnings;
}

namespace {
namespace bridge_api = twattila::dlaa_bridge;
using Microsoft::WRL::ComPtr;

// The fake helper's ways to fail, as a request's rt_request.
constexpr uint32_t HEALTHY = 996;          // answers, then its GPU work arrives late
constexpr uint32_t EXITS_AFTER_ANSWER = 997;  // answers, then exits before any GPU work
constexpr uint32_t STUCK_AFTER_ANSWER = 998;  // answers, stays alive, its GPU work never arrives
constexpr uint32_t SILENT = 995;           // never answers

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
  const bool transfer = source_process == GetCurrentProcess() && target_process != GetCurrentProcess()
                        && (options & DUPLICATE_CLOSE_SOURCE) != 0;
  if (transfer && helper_writes_during_transfer) {
    // The helper writes its own fields whenever it likes: here, in the middle of Prepare making a set.
    auto* shared = bridge_api::bridge.shared;
    shared->ready = 1;
    ++shared->rt_device;
    shared->rt_compact_ok = 77;
    strcpy_s(shared->status, "written by the helper during Prepare");
  }
  if (fail_transfer_at != 0 && transfer && ++transfer_calls == fail_transfer_at) {
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

BOOL WINAPI TestCreateProcessW(LPCWSTR application, LPWSTR command, LPSECURITY_ATTRIBUTES process_attributes,
                              LPSECURITY_ATTRIBUTES thread_attributes, BOOL inherit, DWORD flags, LPVOID environment,
                              LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION information) {
  ++processes_created;
  return CreateProcessW(application, command, process_attributes, thread_attributes, inherit, flags, environment, directory,
                        startup, information);
}

void CheckClosed() {
  const auto& bridge = bridge_api::bridge;
  Check(bridge.mapping == nullptr && bridge.shared == nullptr && bridge.cluster_mapping == nullptr && bridge.cluster_data == nullptr
            && bridge.request == nullptr && bridge.done == nullptr && bridge.process == nullptr && bridge.job == nullptr
            && bridge.watcher == nullptr && bridge.watcher_stop == nullptr && bridge.watchdog.recovery == nullptr
            && bridge.watchdog.retired.empty() && bridge.watchdog.waited.load() == 0 && !bridge.fence,
        "all IPC, process, thread and fence ownership released");
  if (child_witness != nullptr) {
    Check(WaitForSingleObject(child_witness, 0) == WAIT_OBJECT_0, "child process has actually terminated");
    CloseHandle(child_witness);
    child_witness = nullptr;
  }
}

ComPtr<ID3D12Device> MakeHelperDevice() {
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<ID3D12Device> device;
  Check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "helper DXGI factory");
  Check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))), "helper WARP adapter");
  Check(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))), "helper D3D12 device");
  return device;
}

// The helper's exe in its rescue mode ("<game pid> rescue <LUID low> <LUID high>" and a "<handle> <value>" for each set,
// as dlaa_helper/main.cpp), on the software adapter: the game's handles taken from it, the production signalling.
int RescueStub(int argc, char** argv) {
  const HANDLE game = OpenProcess(PROCESS_DUP_HANDLE, FALSE, std::strtoul(argv[1], nullptr, 10));
  const ComPtr<ID3D12Device> device = MakeHelperDevice();
  bool freed = game != nullptr;
  for (int index = 5; index + 1 < argc; index += 2) {
    ComPtr<ID3D12Fence> fence;
    HANDLE shared_fence = nullptr;
    freed = DuplicateHandle(game, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::strtoull(argv[index], nullptr, 10))),
                            GetCurrentProcess(), &shared_fence, 0, FALSE, DUPLICATE_SAME_ACCESS)
            && SUCCEEDED(device->OpenSharedHandle(shared_fence, IID_PPV_ARGS(fence.GetAddressOf())))
            && twattila::dlaa_helper::SignalAbandoned(fence.Get(), std::strtoull(argv[index + 1], nullptr, 10), 2000) && freed;
  }
  return freed ? 0 : 1;
}

// The fake helper: the real PID-based IPC contract, a software D3D12 queue, and bridge v30's reading of the set's fence
// at each request and between them. It ends with its job or by its parent.
int HelperStub(char** argv) {
  const std::wstring name = L"Local\\RenoDX_TW_DLAA_" + std::to_wstring(std::strtoul(argv[1], nullptr, 10));
  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
  HANDLE done = OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L"_done").c_str());
  HANDLE request = OpenEventW(SYNCHRONIZE, FALSE, (name + L"_request").c_str());
  Check(mapping != nullptr && done != nullptr && request != nullptr, "helper opens IPC");
  auto* shared = static_cast<bridge_api::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(bridge_api::Shared)));
  Check(shared != nullptr, "helper maps shared block");
  if (GetEnvironmentVariableW(L"RENODX_TW_TEST_STUB_DIES", nullptr, 0) != 0) {
    strcpy_s(shared->status, "stub: no device");
    return 1;
  }
  shared->ready = 1;
  SetEvent(done);
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence;
  uint32_t opened_generation = 0;
  for (;;) {
    const DWORD woken = WaitForSingleObject(request, 100);
    if (fence) {
      shared->fence_generation = opened_generation;
      shared->fence_completed = fence->GetCompletedValue();
    }
    if (woken != WAIT_OBJECT_0) continue;
    if (shared->generation != 0 && shared->color == 0) {
      // Another anti-aliasing took over (Release): answered, nothing signalled.
      shared->evaluated = 0;
      shared->done_value = shared->value;
      SetEvent(done);
      continue;
    }
    const uint32_t mode = shared->rt_request;
    if (mode != HEALTHY && mode != EXITS_AFTER_ANSWER && mode != STUCK_AFTER_ANSWER && mode != SILENT) return 0;
    if (shared->generation != opened_generation) {
      if (!device) {
        device = MakeHelperDevice();
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))), "helper queue");
      }
      fence.Reset();
      Check(SUCCEEDED(device->OpenSharedHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(shared->fence)),
                                               IID_PPV_ARGS(fence.GetAddressOf()))), "helper opens the set's fence");
      opened_generation = shared->generation;
    }
    if (mode == SILENT) continue;
    const uint64_t value = shared->value;
    shared->evaluated = 1;
    shared->done_value = value + 1;
    SetEvent(done);  // The CPU answer comes before the GPU work, as the real helper's queued ray tracing.
    if (mode == EXITS_AFTER_ANSWER) {
      Sleep(100);
      return 0;
    }
    if (mode == HEALTHY) {
      Sleep(100);
      Check(SUCCEEDED(queue->Wait(fence.Get(), value)), "helper waits for game input");
      Check(SUCCEEDED(queue->Signal(fence.Get(), value + 1)), "helper signals completion");
    }
  }
}

struct Game {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Device5> device5;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11DeviceContext4> context4;
  uint64_t tail_value = 0;
  ComPtr<ID3D11Fence> tail;

  // A signal queued after everything so far: reached only once the game's GPU got past every wait before it.
  void QueueTail() {
    Check(SUCCEEDED(context4->Signal(tail.Get(), ++tail_value)), "tail signal");
    context->Flush();
  }
  bool TailWithin(DWORD milliseconds) const {
    const ULONGLONG until = GetTickCount64() + milliseconds;
    while (tail->GetCompletedValue() < tail_value && GetTickCount64() < until) Sleep(1);
    return tail->GetCompletedValue() >= tail_value;
  }
};

void StartReady(const Game& game, const char* what) {
  Check(bridge_api::internal::Start(game.device.Get()), what);
  Check(WaitForSingleObject(bridge_api::bridge.done, 5000) == WAIT_OBJECT_0 && bridge_api::bridge.shared->ready == 1, "fake helper ready");
}

bool RequestAs(const Game& game, bridge_api::RequestKind kind, uint32_t mode) {
  return bridge_api::internal::Request(game.context.Get(), kind, [&](bridge_api::Shared* shared) { shared->rt_request = mode; });
}
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc > 2 && std::strcmp(argv[2], "rescue") == 0) return RescueStub(argc, argv);
  if (argc > 1 && std::strcmp(argv[1], "--missing") != 0) return HelperStub(argv);
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  Game game;
  Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                  game.device.GetAddressOf(), nullptr, game.context.GetAddressOf())), "create software D3D11 device");
  ID3D11Device* device = game.device.Get();
  if (argc > 1) {
    Check(!bridge_api::internal::Start(device), "missing executable must fail startup");
    CheckClosed();
    bridge_api::Shutdown();
    CheckClosed();
    std::puts("PASS: missing helper cleans partial IPC");
    return 0;
  }
  Check(SUCCEEDED(game.device.As(&game.device5)) && SUCCEEDED(game.context.As(&game.context4)), "game shared fence support");
  Check(SUCCEEDED(game.device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&game.tail))), "tail fence");
  auto& bridge = bridge_api::bridge;

  const std::wstring name = L"Local\\RenoDX_TW_DLAA_" + std::to_wstring(GetCurrentProcessId());
  HANDLE occupied = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(bridge_api::Shared), name.c_str());
  Check(occupied != nullptr, "occupy old session mapping");
  auto* old_shared = static_cast<bridge_api::Shared*>(MapViewOfFile(occupied, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(bridge_api::Shared)));
  Check(old_shared != nullptr, "map old session");
  old_shared->generation = 12345;
  Check(!bridge_api::internal::Start(device), "old session must not be reused");
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
    Check(!bridge_api::internal::Start(device), "occupied object must reject startup");
    CheckClosed();
    bridge_api::Shutdown();
    CloseHandle(occupied);
  }
  for (bool* failure : {&fail_resume, &fail_duplicate, &fail_stop_event}) {
    *failure = true;
    Check(!bridge_api::internal::Start(device), "injected launch failure rejected");
    CheckClosed();
    bridge_api::Shutdown();
    *failure = false;
  }
  // The job is a second way to end the helper with the game, not a condition to run it.
  fail_assign = true;
  StartReady(game, "start without a job");
  fail_assign = false;
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: mapping/event collisions and resume/watcher failures rejected; a helper without a job runs and ends with the unload");

  StartReady(game, "start for failed acknowledgement");
  bridge.pending = true;
  bridge.value = 1;
  bridge.width = 123;
  bridge.shared->generation = 456;
  bridge.shared->color = 789;
  bridge.shared->done_value = 0;
  SetEvent(bridge.done);
  Check(!bridge_api::Prepare(device, 32, 32), "reject stale acknowledgement");
  Check(bridge.failed && bridge.width == 123 && bridge.shared->generation == 456 && bridge.shared->color == 789,
        "failed acknowledgement preserves resource generation before allocation");
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: failed acknowledgement stops Prepare before resource mutation");

  // The guides, ray tracing, reflections and frame generation transfers too: 4 + 4 + 12 + 3 + 2, then the fence.
  for (unsigned failure : {1u, 2u, 5u, 9u, 20u, 23u, 25u, 26u}) {
    StartReady(game, "start transfer rollback test");
    bridge.shared->generation = 321;
    bridge.shared->color = 654;
    DWORD helper_before = 0;
    DWORD helper_after = 0;
    Check(GetProcessHandleCount(bridge.process, &helper_before), "helper handles before failed transfer");
    fail_transfer_at = failure;
    transfer_calls = 0;
    Check(!bridge_api::Prepare(device, 16, 16, true, true, DXGI_FORMAT_R10G10B10A2_UNORM, true), "injected transfer fails Prepare");
    fail_transfer_at = 0;
    Check(transfer_calls == failure, "reached requested transfer failure");
    Check(GetProcessHandleCount(bridge.process, &helper_after), "helper handles after rollback");
    Check(helper_before == helper_after, "remote handles rolled back without leak");
    Check(bridge.shared->generation == 321 && bridge.shared->color == 654 && bridge.shared->fence == 0 && bridge.shared->rt_positions == 0
          && bridge.width == 0 && !bridge.color.texture && !bridge.fence && bridge.watchdog.recovery == nullptr,
          "partial resources discarded without publication");
    bridge_api::Shutdown();
    CheckClosed();
  }
  std::puts("PASS: transfer failures across a full set (guides, ray tracing, reflections, frames, fence) roll back remote ownership and publication");

  // Prepare owns the set's handles and settings only. What the helper writes while Prepare makes the set stays.
  Check(bridge_api::internal::Start(device), "start publication test");
  Check(WaitForSingleObject(bridge.done, 5000) == WAIT_OBJECT_0, "publication helper ready");
  bridge.shared->ready = 0;  // as before the helper's device is up
  const uint32_t device_before = bridge.shared->rt_device;
  helper_writes_during_transfer = true;
  Check(bridge_api::Prepare(device, 16, 16, true, true, DXGI_FORMAT_R10G10B10A2_UNORM, true), "complete resource setup publishes successfully");
  helper_writes_during_transfer = false;
  Check(bridge.width == 16 && bridge.shared->generation == 1 && bridge.shared->color != 0 && bridge.shared->fence != 0
        && bridge.shared->rt_positions != 0 && bridge.shared->rt_reflection != 0 && bridge.shared->fg_frame != 0
        && bridge.shared->fg_format == DXGI_FORMAT_R10G10B10A2_UNORM && bridge.watchdog.recovery != nullptr,
        "complete generation has its textures and both fence handles");
  Check(bridge.shared->ready == 1 && bridge.shared->rt_device > device_before && bridge.shared->rt_compact_ok == 77
        && std::strcmp(bridge.shared->status, "written by the helper during Prepare") == 0,
        "helper-owned fields written during Prepare survive its publication");
  Check(bridge_api::Prepare(device, 32, 32), "smaller set");
  Check(bridge.shared->generation == 2 && bridge.shared->rt_positions == 0 && bridge.shared->diffuse_albedo == 0
        && bridge.shared->rt_reflection == 0 && bridge.shared->fg_frame == 0 && bridge.shared->color != 0,
        "a set without guides, ray tracing and frames clears their handles");
  bridge_api::Release(true);
  Check(bridge.shared->generation == 3 && bridge.shared->color == 0 && bridge.shared->fence == 0 && bridge.watchdog.recovery == nullptr,
        "another anti-aliasing: an empty set");
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: Prepare publishes complete sets field by field; the helper's own fields survive a Prepare they straddle");

  // D3D11 destroys released objects some time after the next flush: the count once it stays for half a second.
  const auto settled_handles = [&] {
    DWORD count = 0;
    DWORD last = 0;
    for (int same = 0, polls = 0; same < 5 && polls < 100; ++polls) {
      last = count;
      game.context->Flush();
      Sleep(100);
      Check(GetProcessHandleCount(GetCurrentProcess(), &count), "handle count");
      same = (count == last ? same + 1 : 0);
    }
    return count;
  };
  const DWORD before = settled_handles();
  for (int iteration = 0; iteration < 20; ++iteration) {
    StartReady(game, "start a fresh session");
    Check(bridge.shared->generation == 0, "fresh session has no old generation");
    HANDLE watcher_witness = nullptr;
    Check(DuplicateHandle(GetCurrentProcess(), bridge.watcher, GetCurrentProcess(), &watcher_witness,
                          SYNCHRONIZE, FALSE, 0), "retain watcher witness");
    if (iteration % 2 == 0) {
      SetEvent(bridge.request);
      Check(WaitForSingleObject(child_witness, 5000) == WAIT_OBJECT_0, "natural helper exit");
    }
    if (iteration % 4 == 1) Check(bridge_api::Prepare(device, 16, 16), "a set to release with the unload");
    bridge_api::Shutdown();
    Check(WaitForSingleObject(watcher_witness, 0) == WAIT_OBJECT_0, "shutdown joined actual watcher exit");
    CloseHandle(watcher_witness);
    CheckClosed();
    bridge_api::Shutdown();
    CheckClosed();
  }
  const DWORD after = settled_handles();
  Check(before == after, "repeated reload does not leak handles");
  std::printf("PASS: 20 reloads, natural/forced exit, joined watcher, idempotence; handles %lu -> %lu\n", before, after);

  // A helper that stops before it is ready: its own reason, said once, however often the game asks again.
  SetEnvironmentVariableW(L"RENODX_TW_TEST_STUB_DIES", L"1");
  Check(bridge_api::internal::Start(device), "start a helper that stops at once");
  SetEnvironmentVariableW(L"RENODX_TW_TEST_STUB_DIES", nullptr);
  Check(WaitForSingleObject(bridge.process, 5000) == WAIT_OBJECT_0, "helper stopped at start");
  const ULONGLONG noticed_until = GetTickCount64() + 2000;
  while (!bridge.watchdog.failed.load() && GetTickCount64() < noticed_until) Sleep(1);
  unsigned warnings_before = warnings.load();
  Check(!bridge_api::Prepare(device, 16, 16), "no set for a stopped helper");
  Check(bridge.failed && bridge.status.find("stub: no device") != std::string::npos
        && bridge.status.find("renodx-tw-dlaa64.log") != std::string::npos, "the stopped helper's own reason and its log are named");
  Check(warnings.load() == warnings_before + 1, "the stop is logged");
  for (int frame = 0; frame < 100; ++frame) {
    Check(!bridge_api::Prepare(device, 16, 16) && !RequestAs(game, bridge_api::EVALUATE, HEALTHY), "off after the stop");
    bridge_api::BeforeSharedWrites(game.context.Get());
  }
  Check(warnings.load() == warnings_before + 1, "a failure is logged once, not at every call");
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: a helper that stops at start shows its own reason; 100 more frames log nothing");

  // Healthy: the answer first, the GPU work 100 ms later; then half a minute with nothing asked. Nothing may end the helper.
  StartReady(game, "start healthy control");
  Check(bridge_api::Prepare(device, 16, 16), "healthy resources");
  Check(RequestAs(game, bridge_api::EVALUATE, HEALTHY), "healthy acknowledged request");
  game.QueueTail();
  Check(game.TailWithin(3000) && bridge.fence->GetCompletedValue() == 2, "healthy GPU completion");
  processes_created = 0;
  Sleep(32000);
  Check(!bridge.watchdog.failed.load() && WaitForSingleObject(bridge.process, 0) == WAIT_TIMEOUT && processes_created.load() == 0
        && bridge.shared->fence_completed == 2 && bridge.shared->fence_generation == bridge.shared->generation,
        "a helper with nothing outstanding is left alone for 32 s");
  // The unload with a live helper and a wait still queued: quick, no recovery process, no device, nothing waited for.
  bridge.steady[bridge_api::EVALUATE] = true;
  Check(RequestAs(game, bridge_api::EVALUATE, STUCK_AFTER_ANSWER), "a request left unfinished at the unload");
  const ComPtr<ID3D11Fence> left_fence = bridge.fence;
  const ULONGLONG unload_started = GetTickCount64();
  bridge_api::Shutdown();
  Check(GetTickCount64() - unload_started < 2000 && processes_created.load() == 0, "the unload neither waits for the GPU nor starts a recovery");
  CheckClosed();
  // In a game the device and its waits go with the unload; here the device stays, so its queue is freed by hand.
  {
    HANDLE left_handle = nullptr;
    ComPtr<ID3D12Fence> left_fence12;
    Check(SUCCEEDED(left_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &left_handle))
          && SUCCEEDED(MakeHelperDevice()->OpenSharedHandle(left_handle, IID_PPV_ARGS(left_fence12.GetAddressOf())))
          && twattila::dlaa_helper::SignalAbandoned(left_fence12.Get(), 4, 2000), "free the wait the unload left");
    CloseHandle(left_handle);
    game.QueueTail();
    Check(game.TailWithin(2000), "queue clean for the next case");
  }
  std::puts("PASS: healthy work completes; 32 s idle and the unload start no recovery");

  struct Case {
    const char* name;
    uint32_t mode;
    bool steady;
    bridge_api::RequestKind kind;
    DWORD freed_within;
  };
  // Stuck work is given 30 s by the watcher; the others are noticed at once.
  for (const Case& test : {Case{"exit after answer, first request", EXITS_AFTER_ANSWER, false, bridge_api::EVALUATE, 8000},
                           Case{"exit after answer, steady request", EXITS_AFTER_ANSWER, true, bridge_api::EVALUATE, 8000},
                           Case{"exit after answer, present (deferred wait)", EXITS_AFTER_ANSWER, false, bridge_api::PRESENT_FRAME, 8000},
                           Case{"no answer, steady request", SILENT, true, bridge_api::EVALUATE, 8000},
                           Case{"answer but GPU work never arrives", STUCK_AFTER_ANSWER, false, bridge_api::EVALUATE, 45000}}) {
    StartReady(game, "start failure case");
    // An older set first, its wait still queued when the next set is made: both are the recovery's.
    Check(bridge_api::Prepare(device, 16, 16), "first set");
    Check(RequestAs(game, bridge_api::EVALUATE, STUCK_AFTER_ANSWER), "first set's request answered without GPU work");
    const ComPtr<ID3D11Fence> first_fence = bridge.fence;
    Check(bridge_api::Prepare(device, 32, 32), "second set");
    Check(bridge.watchdog.retired.size() == 1 && bridge.watchdog.retired[0].value == 2, "the first set's unfinished wait is kept");
    bridge.steady[test.kind] = test.steady;
    warnings_before = warnings.load();
    processes_created = 0;
    Check(RequestAs(game, test.kind, test.mode), "request sent");
    if (test.mode == SILENT) Check(!bridge_api::internal::Answer(200), "no answer within the wait");
    if (test.kind == bridge_api::PRESENT_FRAME) bridge_api::BeforeSharedWrites(game.context.Get());
    game.QueueTail();
    Check(game.TailWithin(test.freed_within), "the game's GPU gets past every wait on the helper");
    // The first set's fence was closed by the helper, so its value is the recovery's. A helper ended with the second
    // set's fence open leaves that one at all ones (seen on the software adapter): its waits pass without the recovery.
    const uint64_t second_value = bridge.fence->GetCompletedValue();
    Check(first_fence->GetCompletedValue() == 2 && (second_value == 2 || (test.mode != EXITS_AFTER_ANSWER && second_value == UINT64_MAX)),
          "the fences are where the recovery or the ended helper left them");
    Check(bridge.watchdog.failed.load() && WaitForSingleObject(bridge.process, 1000) == WAIT_OBJECT_0, "the helper is gone");
    Check(processes_created.load() == 1, "one recovery process, once");
    // After the failure nothing waits again and nothing more is logged.
    Sleep(600);
    const unsigned warnings_after = warnings.load();
    for (int frame = 0; frame < 100; ++frame) {
      Check(!bridge_api::Prepare(device, 32, 32) && !RequestAs(game, bridge_api::EVALUATE, HEALTHY), "off after the failure");
      bridge_api::BeforeSharedWrites(game.context.Get());
    }
    Check(warnings.load() - warnings_after <= 1 && warnings_after - warnings_before <= 3, "the failure is logged once");
    game.QueueTail();
    Check(game.TailWithin(2000), "no wait queued after the failure");
    Check(processes_created.load() == 1, "no second recovery");
    bridge_api::Shutdown();
    CheckClosed();
    std::printf("PASS: %s: both sets' waits freed by the recovery process, logged once\n", test.name);
  }
  return 0;
}
