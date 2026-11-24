// Standalone tests of the production bridge's IPC/process/thread lifetime. No game or vendor helper runs.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {
unsigned fail_transfer_at = 0;
unsigned transfer_calls = 0;
unsigned end_helper_at_transfer = 0;
bool fail_assign = false;
bool fail_resume = false;
bool fail_duplicate = false;
bool fail_stop_event = false;
bool helper_writes_during_transfer = false;
// The bridge's next exit-code read: STILL_ACTIVE (a helper not gone 5 s after the watcher ended it); the one after a
// fake_still_active read, or the next one: failed.
std::atomic<bool> fake_still_active = false;
std::atomic<bool> fail_exit_read = false;
std::atomic<unsigned> processes_created = 0;
std::atomic<unsigned> warnings = 0;
HANDLE child_witness = nullptr;
// The rescue's (the watcher's thread assigns it to the helper's job): read by the test's thread.
std::atomic<HANDLE> rescue_witness = nullptr;
BOOL WINAPI TestAssignProcessToJobObject(HANDLE job, HANDLE process);
DWORD WINAPI TestResumeThread(HANDLE thread);
BOOL WINAPI TestDuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, HANDLE* target,
                               DWORD access, BOOL inherit, DWORD options);
HANDLE WINAPI TestCreateEventW(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL state, LPCWSTR name);
BOOL WINAPI TestCreateProcessW(LPCWSTR application, LPWSTR command, LPSECURITY_ATTRIBUTES process_attributes,
                              LPSECURITY_ATTRIBUTES thread_attributes, BOOL inherit, DWORD flags, LPVOID environment,
                              LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION information);
BOOL WINAPI TestGetExitCodeProcess(HANDLE process, LPDWORD code);
}

#define AssignProcessToJobObject TestAssignProcessToJobObject
#define ResumeThread TestResumeThread
#define DuplicateHandle TestDuplicateHandle
#define CreateEventW TestCreateEventW
#define CreateProcessW TestCreateProcessW
#define GetExitCodeProcess TestGetExitCodeProcess
#include "src/games/twattila/dlaa_bridge.hpp"
#undef AssignProcessToJobObject
#undef ResumeThread
#undef DuplicateHandle
#undef CreateEventW
#undef CreateProcessW
#undef GetExitCodeProcess
#include "src/games/twattila/dlaa_helper/gpu_completion.hpp"

// The add-on's name, which utils/settings.hpp's overlay title reads (an add-on defines it in its addon.cpp).
extern "C" __declspec(dllexport) const char* const NAME = "TW helper lifetime test";

// ReShade's log, counted: a failure is said once, not at every call after it. The watcher's line with the helper's exit
// code is counted apart (its thread's, beside the game thread's line for the same end), the last one kept, and the
// recovery's last line too.
std::atomic<unsigned> exit_lines = 0;
std::mutex exit_line_mutex;
std::string last_exit_line;
std::string last_rescue_line;
std::atomic<unsigned> still_waits_lines = 0;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int level, const char* message) {
  const std::unique_lock lock(exit_line_mutex);
  if (std::strstr(message, "the helper ended (") != nullptr || std::strstr(message, "the helper did not end within 5 s") != nullptr) {
    last_exit_line = message;
    ++exit_lines;
    return;
  }
  if (std::strstr(message, "what the game's GPU waited for from it") != nullptr) {
    last_rescue_line = message;
  }
  if (std::strstr(message, "its rescue still waits for the game's GPU") != nullptr) {
    ++still_waits_lines;
  }
  if (level <= 2) ++warnings;
}

// The watcher's exit line for an end, within 2 s: one more than `before`, with the exit code `code` and whether the
// watcher ended the helper.
bool ExitLogged(unsigned before, const char* code, bool by_watcher) {
  const ULONGLONG until = GetTickCount64() + 2000;
  while (exit_lines.load() == before && GetTickCount64() < until) Sleep(1);
  const std::unique_lock lock(exit_line_mutex);
  return exit_lines.load() == before + 1 && last_exit_line.find(code) != std::string::npos
         && (last_exit_line.find("ended by the watcher") != std::string::npos) == by_watcher;
}

namespace {
namespace bridge_api = twattila::dlaa_bridge;
using Microsoft::WRL::ComPtr;

// The fake helper's ways to fail, as a request's rt_request.
constexpr uint32_t HEALTHY = 996;          // answers, then its GPU work arrives late
constexpr uint32_t EXITS_AFTER_ANSWER = 997;  // answers, then exits before any GPU work
constexpr uint32_t STUCK_AFTER_ANSWER = 998;  // answers, stays alive, its GPU work never arrives
constexpr uint32_t SILENT = 995;           // never answers
// Answers with the request's own value and signals nothing, as a helper whose fence went (a GPU reset, not reopened):
// from then on it reads the fence as all ones.
constexpr uint32_t FENCELESS = 994;

void Check(bool passed, const char* message) {
  if (passed) return;
  std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", message, GetLastError());
  TerminateProcess(GetCurrentProcess(), 1);
  std::abort();
}

// The software context's ID3D11DeviceContext4::Signal / Wait (slots 147 / 148 of d3d11_3.h's ID3D11DeviceContext4Vtbl),
// counted and failed on the bridge's fence when asked; everything else passes through.
using FenceCall = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext4*, ID3D11Fence*, UINT64);
constexpr size_t SIGNAL_SLOT = 147;
constexpr size_t WAIT_SLOT = 148;
FenceCall original_signal = nullptr;
FenceCall original_wait = nullptr;
unsigned fence_signals = 0;  // the bridge's fence's
unsigned fence_waits = 0;
unsigned probe_calls = 0;    // any fence's
bool fail_next_signal = false;
bool fail_next_wait = false;
HRESULT STDMETHODCALLTYPE TestSignal(ID3D11DeviceContext4* context, ID3D11Fence* fence, UINT64 value) {
  ++probe_calls;
  if (fence == bridge_api::bridge.fence.Get()) {
    ++fence_signals;
    if (std::exchange(fail_next_signal, false)) return E_FAIL;
  }
  return original_signal(context, fence, value);
}
HRESULT STDMETHODCALLTYPE TestWait(ID3D11DeviceContext4* context, ID3D11Fence* fence, UINT64 value) {
  ++probe_calls;
  if (fence == bridge_api::bridge.fence.Get()) {
    ++fence_waits;
    if (std::exchange(fail_next_wait, false)) return E_FAIL;
  }
  return original_wait(context, fence, value);
}

// The software fence's ID3D11Fence::CreateSharedHandle (slot 7 of d3d11_3.h's ID3D11FenceVtbl), counted, the
// `fail_share_at`-th call failed (0: none).
using ShareCall = HRESULT(STDMETHODCALLTYPE*)(ID3D11Fence*, const SECURITY_ATTRIBUTES*, DWORD, LPCWSTR, HANDLE*);
constexpr size_t SHARE_SLOT = 7;
ShareCall original_share = nullptr;
unsigned fail_share_at = 0;
unsigned share_calls = 0;
HRESULT STDMETHODCALLTYPE TestCreateSharedHandle(ID3D11Fence* fence, const SECURITY_ATTRIBUTES* attributes, DWORD access, LPCWSTR name,
                                                 HANDLE* handle) {
  if (++share_calls == fail_share_at) {
    *handle = nullptr;
    return E_OUTOFMEMORY;
  }
  return original_share(fence, attributes, access, name, handle);
}

// `object`'s vtable entry `slot` replaced by `hook`; the entry it had.
void* PatchSlot(void* object, size_t slot, void* hook) {
  void** vtable = *static_cast<void***>(object);
  DWORD protection = 0;
  Check(VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &protection) != FALSE, "vtable writable");
  void* original = std::exchange(vtable[slot], hook);
  VirtualProtect(&vtable[slot], sizeof(void*), protection, &protection);
  return original;
}

BOOL WINAPI TestAssignProcessToJobObject(HANDLE job, HANDLE process) {
  // The helper's first, then its rescue's (in the same job, G23 2c).
  if (child_witness != nullptr) {
    HANDLE witness = nullptr;
    Check(rescue_witness.load() == nullptr
              && DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &witness,
                                 SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0),
          "retain rescue witness");
    rescue_witness.store(witness);
    return AssignProcessToJobObject(job, process);
  }
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
  if (end_helper_at_transfer != 0 && transfer && ++transfer_calls == end_helper_at_transfer) {
    // The helper ends in the middle of Prepare making a set: this transfer and every later one fails by itself.
    TerminateProcess(target_process, 1);
    WaitForSingleObject(target_process, 5000);
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

BOOL WINAPI TestGetExitCodeProcess(HANDLE process, LPDWORD code) {
  if (fake_still_active.exchange(false)) {
    *code = STILL_ACTIVE;
    return TRUE;
  }
  if (fail_exit_read.exchange(false)) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return GetExitCodeProcess(process, code);
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
  // The watcher ended it (the unload) or it ended by itself; the end of a terminated process can come a moment later.
  if (const HANDLE rescue = rescue_witness.exchange(nullptr); rescue != nullptr) {
    Check(WaitForSingleObject(rescue, 5000) == WAIT_OBJECT_0, "the rescue has ended");
    CloseHandle(rescue);
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
// as dlaa_helper/main.cpp), on the software adapter: the game's handles taken from it, the production sets (RescueSet;
// its late line is the helper's log's, which the stub has not). RENODX_TW_TEST_RESCUE_REMOVE_MS: its device removed
// that many ms after its start (a GPU reset during its wait).
int RescueStub(int argc, char** argv) {
  const HANDLE game = OpenProcess(PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, std::strtoul(argv[1], nullptr, 10));
  const ComPtr<ID3D12Device> device = MakeHelperDevice();
  if (char remove_ms[16] = {}; GetEnvironmentVariableA("RENODX_TW_TEST_RESCUE_REMOVE_MS", remove_ms, sizeof(remove_ms)) != 0) {
    std::thread([device, milliseconds = std::strtoul(remove_ms, nullptr, 10)] {
      Sleep(milliseconds);
      ComPtr<ID3D12Device5> removable;
      if (SUCCEEDED(device.As(&removable))) removable->RemoveDevice();
    }).detach();
  }
  const HANDLE moved = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  bool freed = game != nullptr && moved != nullptr;
  for (int index = 5; game != nullptr && moved != nullptr && index + 1 < argc; index += 2) {
    ComPtr<ID3D12Fence> fence;
    HANDLE shared_fence = nullptr;
    HRESULT error = E_FAIL;
    if (DuplicateHandle(game, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::strtoull(argv[index], nullptr, 10))),
                        GetCurrentProcess(), &shared_fence, 0, FALSE, DUPLICATE_SAME_ACCESS)
        && SUCCEEDED(device->OpenSharedHandle(shared_fence, IID_PPV_ARGS(fence.GetAddressOf())))
        && twattila::dlaa_helper::RescueSet(fence.Get(), moved, game, std::strtoull(argv[index + 1], nullptr, 10), freed,
                                            [](uint64_t) {}, &error)) continue;
    // The game ended: nothing left to free.
    if (error == S_OK) break;
    freed = false;
  }
  return static_cast<int>(freed ? bridge_api::HELPER_EXIT_DONE : bridge_api::HELPER_EXIT_RESCUE_SIGNAL);
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
    return static_cast<int>(bridge_api::HELPER_EXIT_NO_D3D11);
  }
  shared->ready = 1;
  SetEvent(done);
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence;
  uint32_t opened_generation = 0;
  bool fenceless = false;
  for (;;) {
    const DWORD woken = WaitForSingleObject(request, 100);
    if (fence) {
      shared->fence_generation = opened_generation;
      shared->fence_completed = (fenceless ? UINT64_MAX : fence->GetCompletedValue());
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
    if (mode != HEALTHY && mode != EXITS_AFTER_ANSWER && mode != STUCK_AFTER_ANSWER && mode != SILENT && mode != FENCELESS) return 0;
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
    if (mode == FENCELESS) {
      fenceless = true;
      shared->evaluated = 0;
      shared->done_value = value;
      SetEvent(done);
      continue;
    }
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
  // The context's Signal and Wait through the test's: the interface's calls must reach them, and still do their work.
  original_signal = reinterpret_cast<FenceCall>(PatchSlot(game.context4.Get(), SIGNAL_SLOT, reinterpret_cast<void*>(&TestSignal)));
  original_wait = reinterpret_cast<FenceCall>(PatchSlot(game.context4.Get(), WAIT_SLOT, reinterpret_cast<void*>(&TestWait)));
  {
    ComPtr<ID3D11Fence> probe;
    Check(SUCCEEDED(game.device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&probe))), "probe fence");
    Check(SUCCEEDED(game.context4->Signal(probe.Get(), 1)) && SUCCEEDED(game.context4->Wait(probe.Get(), 1)) && probe_calls == 2,
          "Signal and Wait reach slots 147 and 148");
    game.context->Flush();
    const ULONGLONG until = GetTickCount64() + 2000;
    while (probe->GetCompletedValue() < 1 && GetTickCount64() < until) Sleep(1);
    Check(probe->GetCompletedValue() == 1, "the patched Signal still signals");
    ComPtr<ID3D11Fence> shared_probe;
    Check(SUCCEEDED(game.device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&shared_probe))), "shared probe fence");
    original_share = reinterpret_cast<ShareCall>(PatchSlot(shared_probe.Get(), SHARE_SLOT, reinterpret_cast<void*>(&TestCreateSharedHandle)));
    HANDLE probe_handle = nullptr;
    Check(SUCCEEDED(shared_probe->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &probe_handle)) && probe_handle != nullptr
          && share_calls == 1, "CreateSharedHandle reaches slot 7 and still shares");
    CloseHandle(probe_handle);
  }

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
    // The helper's last set (here its numbers only) goes too: an empty set, which it read.
    Check(bridge.shared->generation == 322 && bridge.shared->color == 0 && bridge.shared->fence == 0 && bridge.shared->rt_positions == 0
          && bridge.width == 0 && !bridge.color.texture && !bridge.fence && bridge.watchdog.recovery == nullptr,
          "partial resources discarded, the helper's last set replaced by an empty one");
    Check(bridge.failed == (failure == 26u), "a texture's failure is tried again, the fence's turns the bridge off");
    bridge_api::Shutdown();
    CheckClosed();
  }
  std::puts("PASS: transfer failures across a full set (guides, ray tracing, reflections, frames, fence) roll back remote ownership and publication");

  // The helper ends while Prepare transfers a set (a texture's handle, then the fence's): said as the stopped helper with
  // its own status and its log, once, and nothing is closed in a process that is gone (no rollback warnings).
  for (unsigned ending : {20u, 26u}) {
    StartReady(game, "start helper-ends-in-Prepare test");
    const unsigned warnings_before_end = warnings.load();
    const unsigned exits_before_end = exit_lines.load();
    end_helper_at_transfer = ending;
    transfer_calls = 0;
    Check(!bridge_api::Prepare(device, 16, 16, true, true, DXGI_FORMAT_R10G10B10A2_UNORM, true), "a helper that ends in Prepare fails it");
    end_helper_at_transfer = 0;
    Check(transfer_calls >= ending, "reached the transfer the helper ended at");
    Check(bridge.failed && bridge.status.find("the helper stopped") != std::string::npos
          && bridge.status.find("renodx-tw-dlaa64.log") != std::string::npos, "the ended helper is named, with its log");
    Check(warnings.load() == warnings_before_end + 1, "one line for a helper that ended in Prepare, none for its handles");
    Check(ExitLogged(exits_before_end, "exit code 0x00000001", false), "the watcher logs the ended helper's own exit code, once");
    Check(bridge.width == 0 && !bridge.color.texture && !bridge.fence && bridge.watchdog.recovery == nullptr, "nothing kept of the set");
    bridge_api::Shutdown();
    CheckClosed();
  }
  std::puts("PASS: a helper that ends while Prepare transfers a set shows as the stopped helper, with one log line");

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
  const unsigned exits_before_stop = exit_lines.load();
  Check(bridge_api::internal::Start(device), "start a helper that stops at once");
  SetEnvironmentVariableW(L"RENODX_TW_TEST_STUB_DIES", nullptr);
  Check(WaitForSingleObject(bridge.process, 5000) == WAIT_OBJECT_0, "helper stopped at start");
  const ULONGLONG noticed_until = GetTickCount64() + 2000;
  while (!bridge.watchdog.failed.load() && GetTickCount64() < noticed_until) Sleep(1);
  Check(ExitLogged(exits_before_stop, "exit code 0x00000004: no D3D11.4 device", false), "its exit code and meaning logged by the watcher");
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
  const HANDLE rescue_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  Check(rescue_event != nullptr, "the test's rescue event");
  {
    HRESULT error = E_FAIL;
    HANDLE left_handle = nullptr;
    ComPtr<ID3D12Fence> left_fence12;
    Check(SUCCEEDED(left_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &left_handle))
          && SUCCEEDED(MakeHelperDevice()->OpenSharedHandle(left_handle, IID_PPV_ARGS(left_fence12.GetAddressOf())))
          && twattila::dlaa_helper::SignalAbandoned(left_fence12.Get(), rescue_event, GetCurrentProcess(), 4, 2000, &error),
        "free the wait the unload left");
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
    const unsigned exits_before = exit_lines.load();
    processes_created = 0;
    // The silent helper's end read as not gone after the watcher's 5 s, then the rescue's exit code not read.
    fake_still_active = (test.mode == SILENT);
    fail_exit_read = (test.mode == SILENT);
    Check(RequestAs(game, test.kind, test.mode), "request sent");
    if (test.mode == SILENT) {
      Check(!bridge_api::internal::Answer(200) && bridge.status.find("did not answer a steady request in 200 ms") != std::string::npos,
            "no answer within the wait, said as a timeout");
    }
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
    // A helper that ended by itself: its own code; one the watcher ended: the watcher's.
    if (test.mode == EXITS_AFTER_ANSWER) {
      Check(ExitLogged(exits_before, "exit code 0x00000000: ended normally", false), "the helper's own end logged once");
    } else if (test.mode == SILENT) {
      Check(ExitLogged(exits_before, "the helper did not end within 5 s after the watcher ended it.", false), "STILL_ACTIVE said as not ended");
      Check(!fake_still_active && !fail_exit_read, "both exit-code reads were the bridge's");
      const std::unique_lock lock(exit_line_mutex);
      Check(last_rescue_line.find("was not freed (the rescue's exit code not read (error 5))") != std::string::npos,
            "the rescue's unread exit code said as such, not as the addon's end");
    } else {
      Check(ExitLogged(exits_before, "exit code 0xE0000010: ended by the addon", true), "the watcher's end logged once");
    }
    game.QueueTail();
    Check(game.TailWithin(2000), "no wait queued after the failure");
    Check(processes_created.load() == 1, "no second recovery");
    bridge_api::Shutdown();
    CheckClosed();
    std::printf("PASS: %s: both sets' waits freed by the recovery process, logged once\n", test.name);
  }

  // Another D3D11 device of the process ends (a probe's, an overlay's) while the game's GPU waits on the helper: the set,
  // its recovery and the wait stay, so a helper that stops later is still freed (Shogun 2, Divinity, Rome Remastered,
  // Ryse and Expeditions released the set at any device's end, and the older set's wait was then nobody's).
  StartReady(game, "start other-device test");
  {
    Check(bridge_api::Prepare(device, 16, 16), "the set");
    Check(RequestAs(game, bridge_api::EVALUATE, STUCK_AFTER_ANSWER), "a request whose GPU work never arrives");
    const ComPtr<ID3D11Fence> first_fence = bridge.fence;
    const HANDLE recovery = bridge.watchdog.recovery;
    const uint64_t waited = bridge.watchdog.waited.load();
    {
      ComPtr<ID3D11Device> other;
      Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, other.GetAddressOf(),
                                        nullptr, nullptr)), "another device");
      bridge_api::OnDestroyDevice(other.Get());
    }
    Check(bridge.width == 16 && bridge.device && bridge.fence == first_fence && bridge.watchdog.recovery == recovery
          && bridge.watchdog.waited.load() == waited && waited != 0, "another device's end leaves the set, its recovery and its wait");
    Check(bridge_api::Prepare(device, 32, 32), "a second set");
    Check(RequestAs(game, bridge_api::EVALUATE, STUCK_AFTER_ANSWER), "the helper closes the first set's fence");
    processes_created = 0;
    TerminateProcess(bridge.process, 1);
    game.QueueTail();
    Check(game.TailWithin(8000), "the game's GPU gets past the first set's wait");
    Check(first_fence->GetCompletedValue() == 2 && processes_created.load() == 1, "freed by the recovery");
    bridge_api::OnDestroyDevice(device);
    Check(bridge.width == 0 && !bridge.device && !bridge.fence && bridge.watchdog.recovery == nullptr && bridge.watchdog.retired.empty(),
          "the bridge's own device's end releases it");
  }
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: another device's end leaves the set and its recovery; the bridge's own device's end releases it");

  // A helper whose fence went answers a steady request with its own value and signals nothing, while the game's GPU
  // already waits for the next value: no answer, so the watcher ends the helper and the recovery frees the wait (the
  // helper reads the fence as all ones, which the stuck test never counts).
  StartReady(game, "start fence-less answer test");
  {
    Check(bridge_api::Prepare(device, 16, 16), "the set");
    Check(RequestAs(game, bridge_api::EVALUATE, HEALTHY), "a request that makes the next one steady");
    Check(RequestAs(game, bridge_api::EVALUATE, FENCELESS) && bridge.pending, "a steady request, its wait queued");
    processes_created = 0;
    Check(!RequestAs(game, bridge_api::EVALUATE, FENCELESS), "nothing evaluated by a fence-less helper");
    game.QueueTail();
    Check(game.TailWithin(8000), "the game's GPU gets past the steady request's wait");
    // The ended helper's fence (all ones on the software adapter) can free the wait before the watcher starts the
    // recovery: up to 5 s for it.
    for (const ULONGLONG until = GetTickCount64() + 5000; processes_created.load() == 0 && GetTickCount64() < until;) Sleep(1);
    Check(bridge.watchdog.failed.load() && bridge.failed && processes_created.load() == 1, "the fence-less answer is no answer: one recovery");
    Check(bridge.status.find("answered a steady request with 3, wanted 4") != std::string::npos, "said as a low answer, with both values");
  }
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: a fence-less answer to a steady request turns the bridge off and the recovery frees its wait");

  // The game's own Signal or Wait on the set's fence fails: off at once. A failed signal asks the helper nothing (it would
  // wait for a value that never comes); after a failed wait (the deferred one in Request, BeforeSharedWrites') no signal
  // of the game's follows, which could land before the helper's and be moved back by it.
  for (int failure = 0; failure < 3; ++failure) {
    StartReady(game, "start failed fence call test");
    Check(bridge_api::Prepare(device, 16, 16), "the set");
    Check(RequestAs(game, (failure == 0 ? bridge_api::EVALUATE : bridge_api::PRESENT_FRAME), HEALTHY), "a first request");
    const uint64_t asked = bridge.shared->value;
    const unsigned signals = fence_signals;
    if (failure == 0) {
      fail_next_signal = true;
    } else {
      fail_next_wait = true;
    }
    if (failure == 2) {
      bridge_api::BeforeSharedWrites(game.context.Get());
      Check(bridge.failed && !fail_next_wait, "a failed wait before the shared writes turns the bridge off");
    }
    Check(!RequestAs(game, bridge_api::EVALUATE, HEALTHY) && bridge.failed, "a failed signal or wait turns the bridge off");
    Check(!fail_next_signal && !fail_next_wait, "the failure was injected");
    Check(fence_signals == signals + (failure == 0 ? 1u : 0u) && bridge.shared->value == asked, "nothing signalled or asked after it");
    game.QueueTail();
    Check(game.TailWithin(3000), "nothing left waiting");
    bridge_api::Shutdown();
    CheckClosed();
  }
  std::puts("PASS: a failed Signal or Wait of the game's on the set's fence turns the bridge off, with no signal or request after it");

  // The fence's second handle, the recovery's, can't be made: nothing would free the set's waits if the helper stopped, so
  // Prepare fails, and what the helper got of the set (its fence too) is closed there again.
  StartReady(game, "start recovery handle test");
  {
    DWORD helper_before = 0;
    DWORD helper_after = 0;
    Check(GetProcessHandleCount(bridge.process, &helper_before), "helper handles before the set");
    share_calls = 0;
    fail_share_at = 2;
    Check(!bridge_api::Prepare(device, 16, 16, true, true, DXGI_FORMAT_R10G10B10A2_UNORM, true), "a fence without a recovery handle fails Prepare");
    fail_share_at = 0;
    Check(share_calls == 2, "the recovery's handle was the one refused");
    Check(GetProcessHandleCount(bridge.process, &helper_after) && helper_before == helper_after, "the helper's handles closed again");
    Check(bridge.failed && bridge.watchdog.recovery == nullptr && !bridge.fence && bridge.width == 0 && bridge.shared->fence == 0,
          "off, nothing kept of the set");
  }
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: a set whose fence has no recovery handle is refused and rolled back");

  // A fence-less answer gives back the game's own (odd) value: the game's next value is odd still, the helper's even, as
  // the recovery's SignalAbandoned expects.
  StartReady(game, "start odd value test");
  {
    Check(bridge_api::Prepare(device, 16, 16), "the set");
    Check(!RequestAs(game, bridge_api::EVALUATE, FENCELESS) && bridge.value == 1, "a fence-less answer: the game's own value back");
    Check(RequestAs(game, bridge_api::EVALUATE, HEALTHY) && bridge.shared->value == 3 && bridge.value == 4, "the game's next value is odd");
    game.QueueTail();
    Check(game.TailWithin(3000), "the helper's even value reached");
  }
  bridge_api::Shutdown();
  CheckClosed();
  std::puts("PASS: the game's fence values stay odd after a fence-less answer");

  // The rescue (G23 2a / 2c): the game's GPU held before the helper's request by a gate (another device's shared fence,
  // as a GPU seconds behind), the helper gone after its answer. The rescue, in the helper's job, waits with no time limit
  // until the GPU gets there (past the watcher's 15 s one line), the unload ends it or its own device goes.
  {
    const ComPtr<ID3D12Device> gate_device = MakeHelperDevice();
    ComPtr<ID3D12Fence> gate;
    ComPtr<ID3D11Fence> gate11;
    HANDLE gate_handle = nullptr;
    Check(SUCCEEDED(gate_device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&gate)))
              && SUCCEEDED(gate_device->CreateSharedHandle(gate.Get(), nullptr, GENERIC_ALL, nullptr, &gate_handle))
              && SUCCEEDED(game.device5->OpenSharedFence(gate_handle, IID_PPV_ARGS(&gate11))),
          "the gate");
    CloseHandle(gate_handle);
    uint64_t gate_value = 0;
    const auto rescue_logged = [](const char* text) {
      for (const ULONGLONG until = GetTickCount64() + 3000; GetTickCount64() < until; Sleep(1)) {
        const std::unique_lock lock(exit_line_mutex);
        if (last_rescue_line.find(text) != std::string::npos) return true;
      }
      return false;
    };
    struct RescueCase {
      const char* name;
      DWORD gate_ms;             // 0: not opened while the rescue runs
      const wchar_t* remove_ms;  // the rescue's device removed
    };
    for (const RescueCase& test : {RescueCase{"a GPU 6 s behind: freed", 6000, nullptr},
                                   RescueCase{"a GPU 40 s behind: one line at 15 s, then freed", 40000, nullptr},
                                   RescueCase{"a GPU that never gets there: the unload ends the rescue at once", 0, nullptr},
                                   RescueCase{"the rescue's device removed during its wait: it ends", 0, L"3000"}}) {
      StartReady(game, "start rescue wait test");
      Check(bridge_api::Prepare(device, 16, 16), "the set");
      Check(SUCCEEDED(game.context4->Wait(gate11.Get(), ++gate_value)), "the game's GPU held by the gate");
      {
        const std::unique_lock lock(exit_line_mutex);
        last_rescue_line.clear();
      }
      const unsigned still_waits_before = still_waits_lines.load();
      SetEnvironmentVariableW(L"RENODX_TW_TEST_RESCUE_REMOVE_MS", test.remove_ms);
      processes_created = 0;
      Check(RequestAs(game, bridge_api::EVALUATE, EXITS_AFTER_ANSWER), "answered, then the helper ends");
      const ComPtr<ID3D11Fence> set_fence = bridge.fence;
      const uint64_t waited = bridge.watchdog.waited.load();
      game.QueueTail();
      for (const ULONGLONG until = GetTickCount64() + 5000; rescue_witness.load() == nullptr && GetTickCount64() < until;) Sleep(1);
      const ULONGLONG rescue_started = GetTickCount64();
      SetEnvironmentVariableW(L"RENODX_TW_TEST_RESCUE_REMOVE_MS", nullptr);
      Check(rescue_witness.load() != nullptr && processes_created.load() == 1, "one rescue");
      // Its own handle in the helper's job (not this process's job: the test may run in one).
      BOOL in_job = FALSE;
      while (IsProcessInJob(rescue_witness.load(), bridge.job, &in_job) && !in_job && GetTickCount64() - rescue_started < 1000) Sleep(1);
      Check(in_job, "the rescue is in the helper's job");
      if (test.gate_ms != 0) {
        Sleep(test.gate_ms);
        Check(WaitForSingleObject(rescue_witness.load(), 0) == WAIT_TIMEOUT, "the rescue still waits for the game's GPU");
        Check(SUCCEEDED(gate->Signal(gate_value)) && game.TailWithin(3000), "the game's GPU gets past the helper's wait");
        Check(WaitForSingleObject(rescue_witness.load(), 2000) == WAIT_OBJECT_0 && rescue_logged("was freed."), "freed, and said so");
      } else if (test.remove_ms != nullptr) {
        Check(WaitForSingleObject(rescue_witness.load(), 4500) == WAIT_OBJECT_0 && GetTickCount64() - rescue_started >= 2500,
              "the rescue ends with its device's removal");
        Check(rescue_logged("was freed."), "a removed device: nothing left to free");
      } else {
        Sleep(5000);
        const ULONGLONG stopping = GetTickCount64();
        bridge_api::Shutdown();
        Check(GetTickCount64() - stopping < 2000 && rescue_logged("the unload ended the rescue"), "the unload ends a waiting rescue at once");
      }
      Check(still_waits_lines.load() - still_waits_before == ((test.gate_ms > 15000) ? 1u : 0u), "one line past 15 s, none before");
      Check(processes_created.load() == 1, "no second rescue");
      bridge_api::Shutdown();
      CheckClosed();
      // What the rescue did not free (the unload's, the removed device's): the gate opened, the set's value signalled.
      HANDLE set_handle = nullptr;
      ComPtr<ID3D12Fence> set_fence12;
      HRESULT error = E_FAIL;
      Check(SUCCEEDED(gate->Signal(gate_value)) && SUCCEEDED(set_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &set_handle))
                && SUCCEEDED(gate_device->OpenSharedHandle(set_handle, IID_PPV_ARGS(set_fence12.GetAddressOf())))
                && twattila::dlaa_helper::SignalAbandoned(set_fence12.Get(), rescue_event, GetCurrentProcess(), waited, 5000, &error),
            "free what the rescue left");
      CloseHandle(set_handle);
      game.QueueTail();
      Check(game.TailWithin(3000), "queue clean for the next case");
      std::printf("PASS: rescue: %s\n", test.name);
    }
  }
  CloseHandle(rescue_event);

  // Textures that could not be made: tried again at another size or after 30 s, three times a session, a line each. Last:
  // a 32-bit process may find no room afterwards for a later case's 512 MB ray tracing texture.
  StartReady(game, "start texture retry test");
  {
    const unsigned warnings_before_retry = warnings.load();
    const auto fail_texture = [&](UINT size) {
      fail_transfer_at = 1;
      transfer_calls = 0;
      const bool prepared = bridge_api::Prepare(device, size, size);
      fail_transfer_at = 0;
      return prepared;
    };
    Check(!fail_texture(16) && !bridge.failed && bridge.texture_failures == 1, "a texture that cannot be shared: tried again later");
    Check(!fail_texture(16) && transfer_calls == 0 && bridge.texture_failures == 1, "the same size waits");
    bridge.texture_retry_ms = GetTickCount64();
    Check(bridge_api::Prepare(device, 16, 16) && bridge.width == 16, "the same size again after the wait");
    Check(bridge_api::Prepare(device, 32, 32), "a set the next failure frees in the helper");
    Check(!fail_texture(48) && bridge.texture_failures == 2 && bridge.shared->color == 0, "the next size tried, the helper's set freed");
    Check(!fail_texture(64) && bridge.texture_failures == 3 && !bridge.failed, "the third try");
    Check(!fail_texture(80) && bridge.texture_failures == 3 && bridge.failed, "then off for the session");
    Check(warnings.load() == warnings_before_retry + 4, "one line for each failure");
  }
  bridge_api::Shutdown();
  CheckClosed();
  Check(bridge.texture_failures == 0, "a new session tries again");
  std::puts("PASS: textures that could not be made are tried again three times a session, the helper's last set freed each time");
  return 0;
}
