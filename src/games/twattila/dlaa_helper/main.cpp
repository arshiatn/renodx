/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War DLAA helper (64-bit, renodx-tw-dlaa64.exe next to the addon): NVIDIA's DLSS runs only in 64-bit
// processes and Attila, Rome II and Thrones are 32-bit. The addon starts it with its process id; it opens the addon's
// shared memory and events (dlaa_bridge.h), makes a D3D11 device on the game's GPU, and for each request: its GPU waits
// for the addon's fence value, NVIDIA's DLAA runs on the shared textures (talesofarise_dlss/ngx_backend.hpp), the fence
// is signalled one higher (also after a failure, so the game never waits for nothing). Ray Reconstruction, when the
// addon asks with its guides, runs through D3D12 (rr_d3d12.hpp; nvngx_dlssd.dll next to nvngx_dlss.dll). Ray tracing
// requests (bridge v4: the addon's rt_scene.hpp) run on another D3D12 device (rt_d3d12.hpp). Frame generation requests
// (bridge v5: the addon's frame_generation.hpp, one per game present) present through Streamline's DLSS Frame Generation
// into a child window over the game's (fg_d3d12.hpp), or (bridge v23) through FSR's or XeSS's (fg_proxy_d3d12.hpp). AMD's FSR
// and Intel's XeSS (bridge v22) run on D3D12 too, in DLSS's place (upscale_d3d12.hpp). It quits with the game. It answers addons from bridge version OLDEST_VERSION on.
// Build (x64): see twattila/README.md.
#include <windows.h>

#include <d3d11_4.h>
#include <dbghelp.h>
#include <dxgi1_6.h>
#include <io.h>
#include <nsight_graphics_sdk/NGFX_GPUTrace_D3D12.h>
#include <nsight_graphics_sdk/NGFX_GraphicsCapture_D3D12.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <format>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../talesofarise_dlss/ngx_backend.hpp"
#include "../dlaa_bridge.h"
#include "./fg_d3d12.hpp"
#include "./fg_proxy_d3d12.hpp"
#include "./rr_d3d12.hpp"
#include "./rt_d3d12.hpp"
#if defined(RENODX_RECORDER)
#include "./rt_serve.h"
#endif
#include "./upscale_d3d12.hpp"

using Microsoft::WRL::ComPtr;

// ReShade's exports (this process stands in for ReShade): the backend's log lines into renodx-tw-dlaa64.log.
static FILE* log_file = nullptr;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(HMODULE, uint32_t) { return true; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(HMODULE) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  if (log_file == nullptr) return;
  std::fprintf(log_file, "%s\n", message);
  std::fflush(log_file);
}

// With the time: this log's other lines have none, and the game's log and Windows' events are read beside these.
static void LogWithTime(const char* message) {
  if (log_file == nullptr) return;
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  std::fprintf(log_file, "%02d:%02d:%02d %s\n", now.wHour, now.wMinute, now.wSecond, message);
  std::fflush(log_file);
}

// A small dump beside the log, the last one only (`fault` null: the calling thread's place); false: none written.
static std::array<wchar_t, MAX_PATH> dump_path = {};
static bool WriteDump(EXCEPTION_POINTERS* fault) {
  const HANDLE file = CreateFileW(dump_path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  MINIDUMP_EXCEPTION_INFORMATION where = {.ThreadId = GetCurrentThreadId(), .ExceptionPointers = fault, .ClientPointers = FALSE};
  const bool dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithThreadInfo,
                                        (fault != nullptr) ? &where : nullptr, nullptr, nullptr)
                      != FALSE;
  CloseHandle(file);
  return dumped;
}

// The CRT's ends that skip OnUnhandledFault (its __fastfail): an uncaught C++ exception (std::terminate on the main
// thread; on another thread its terminate's abort), a pure virtual call, an invalid CRT parameter. Its line and a dump,
// then the process ends with `exit_code` (dlaa_bridge.h HELPER_EXIT_*), which the addon logs. Nothing allocated (a
// std::bad_alloc may be why); a second thread that gets here waits for the first's end.
[[noreturn]] static void StopOnCrtEnd(const char* why, uint32_t exit_code) {
  static std::atomic<bool> stopping = false;
  if (stopping.exchange(true)) Sleep(INFINITE);
  std::array<char, 512> line = {};
  std::snprintf(line.data(), line.size(), "[TW DLAA helper] Stopped: %s (exit code 0x%08X).", why, exit_code);
  try {
    if (const std::exception_ptr thrown = std::current_exception(); thrown != nullptr) std::rethrow_exception(thrown);
  } catch (const std::exception& thrown) {
    std::snprintf(line.data(), line.size(), "[TW DLAA helper] Stopped: %s: %s (exit code 0x%08X).", why, thrown.what(), exit_code);
  } catch (...) {
    // No-op: an exception that is no std::exception has no text.
  }
  LogWithTime(line.data());
  LogWithTime(WriteDump(nullptr) ? "[TW DLAA helper] renodx-tw-dlaa64.dmp written." : "[TW DLAA helper] No dump.");
  TerminateProcess(GetCurrentProcess(), exit_code);
  for (;;) Sleep(INFINITE);
}

// A fault nothing handled: a line in the log and a small dump beside it.
static LONG WINAPI OnUnhandledFault(EXCEPTION_POINTERS* fault) {
  const bool dumped = WriteDump(fault);
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     static_cast<LPCWSTR>(fault->ExceptionRecord->ExceptionAddress), &module);
  std::array<char, MAX_PATH> module_path = {};
  if (module != nullptr) GetModuleFileNameA(module, module_path.data(), MAX_PATH);
  std::array<char, 512> line = {};
  std::snprintf(line.data(), line.size(), "[TW DLAA helper] Fault %08lX at %s+0x%llX, %s.", fault->ExceptionRecord->ExceptionCode, module_path.data(),
                reinterpret_cast<uint64_t>(fault->ExceptionRecord->ExceptionAddress) - reinterpret_cast<uint64_t>(module),
                dumped ? "renodx-tw-dlaa64.dmp written" : "no dump");
  LogWithTime(line.data());
  return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  // Windows' own DXGI and Direct3D before anything calls them (build.ps1 delay-loads them): ReShade's dxgi.dll / d3d11.dll
  // / d3d12.dll in the game's folder stay out of this process (its add-on load failed here, and its "errors loading
  // add-ons" banner showed on frame generation's window; a DevKit there crashed the helper).
  for (const wchar_t* library : {L"dxgi.dll", L"d3d11.dll", L"d3d12.dll"}) LoadLibraryExW(library, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  int argument_count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments == nullptr || argument_count < 2) return static_cast<int>(twattila::dlaa_bridge::HELPER_EXIT_COMMAND_LINE);
  const DWORD game_id = std::wcstoul(arguments[1], nullptr, 10);

  std::wstring folder(MAX_PATH, L'\0');
  folder.resize(GetModuleFileNameW(nullptr, folder.data(), MAX_PATH));
  folder = folder.substr(0, folder.find_last_of(L"\\/"));
  const std::wstring log_path = folder + L"\\renodx-tw-dlaa64.log";
  // Started by the addon's watcher after the helper stopped, as "<game pid> rescue <LUID low> <LUID high>" and a
  // "<fence handle> <value>" for each set the game's GPU may still wait on, oldest first (dlaa_bridge.hpp; the handles
  // are the game's process's, taken from it here): the fences freed from this process, where ReShade counts no devices,
  // and its line after the stopped helper's.
  if (argument_count >= 7 && std::wcscmp(arguments[2], L"rescue") == 0) {
    using namespace twattila::dlaa_bridge;
    _wfopen_s(&log_file, log_path.c_str(), L"a");
    // The first step that failed, as the exit code (the addon logs it) and with its value here; the other sets are still
    // signalled.
    uint32_t failed_step = HELPER_EXIT_DONE;
    std::string why;
    const auto fail = [&](uint32_t step, std::string what) {
      if (failed_step != HELPER_EXIT_DONE) return;
      failed_step = step;
      why = std::move(what);
    };
    // SYNCHRONIZE: a set's wait also ends with the game.
    const HANDLE game = OpenProcess(PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, game_id);
    if (game == nullptr) {
      fail(HELPER_EXIT_RESCUE_GAME, std::format("the game's process: error {}", GetLastError()));
    }
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    if (const HRESULT made = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())); FAILED(made)) {
      fail(HELPER_EXIT_RESCUE_FACTORY, std::format("DXGI factory 0x{:08X}", static_cast<uint32_t>(made)));
    } else if (const HRESULT found = factory->EnumAdapterByLuid(
                   LUID{.LowPart = std::wcstoul(arguments[3], nullptr, 10), .HighPart = std::wcstol(arguments[4], nullptr, 10)},
                   IID_PPV_ARGS(adapter.GetAddressOf()));
               FAILED(found)) {
      fail(HELPER_EXIT_RESCUE_ADAPTER, std::format("the game's GPU 0x{:08X}", static_cast<uint32_t>(found)));
    } else if (const HRESULT created = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.GetAddressOf()));
               FAILED(created)) {
      fail(HELPER_EXIT_RESCUE_DEVICE, std::format("D3D12 device 0x{:08X}", static_cast<uint32_t>(created)));
    }
    // Woken by each move of a set's fence (auto-reset: RescueSet reads the fence again).
    const HANDLE moved = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (moved == nullptr) {
      fail(HELPER_EXIT_RESCUE_SIGNAL, std::format("its event, error {}", GetLastError()));
    }
    bool game_ended = false;
    for (int index = 5; device && moved != nullptr && !game_ended && index + 1 < argument_count; index += 2) {
      const int set = (index - 5) / 2;
      ComPtr<ID3D12Fence> fence;
      HANDLE shared_fence = nullptr;
      if (!DuplicateHandle(game, reinterpret_cast<HANDLE>(_wcstoui64(arguments[index], nullptr, 10)), GetCurrentProcess(), &shared_fence, 0,
                           FALSE, DUPLICATE_SAME_ACCESS)) {
        fail(HELPER_EXIT_RESCUE_HANDLE, std::format("set {}: its fence's handle, error {}", set, GetLastError()));
        continue;
      }
      if (const HRESULT opened = device->OpenSharedHandle(shared_fence, IID_PPV_ARGS(fence.GetAddressOf())); FAILED(opened)) {
        fail(HELPER_EXIT_RESCUE_FENCE, std::format("set {}: its fence 0x{:08X}", set, static_cast<uint32_t>(opened)));
        continue;
      }
      const uint64_t value = _wcstoui64(arguments[index + 1], nullptr, 10);
      HRESULT error = S_OK;
      if (twattila::dlaa_helper::RescueSet(fence.Get(), moved, game, value, failed_step == HELPER_EXIT_DONE,
                                           [&](uint64_t completed) {
                                             LogWithTime(std::format("[TW DLAA helper] Rescue: set {}: the game's GPU did not move for 2 s (its fence at {}, "
                                                                     "wanted {}); waiting until it gets there or the game ends.",
                                                                     set, completed, value)
                                                             .c_str());
                                           },
                                           &error)) {
        continue;
      }
      game_ended = (error == S_OK);
      if (!game_ended) {
        fail(HELPER_EXIT_RESCUE_SIGNAL, std::format("set {}: its fence at {}, wanted {} (0x{:08X})", set, fence->GetCompletedValue(), value,
                                                    static_cast<uint32_t>(error)));
      }
    }
    if (game_ended) {
      LogWithTime("[TW DLAA helper] The helper had stopped: the game ended before its GPU got to what it waited for.");
    } else if (failed_step == HELPER_EXIT_DONE) {
      LogWithTime("[TW DLAA helper] The helper had stopped: what the game's GPU waited for from it is signalled.");
    } else {
      LogWithTime(std::format("[TW DLAA helper] The helper had stopped: what the game's GPU waited for from it could not be signalled ({}; exit code {}).",
                              why, failed_step)
                      .c_str());
    }
    return static_cast<int>(failed_step);
  }
#if defined(RENODX_RECORDER)
  // The recorder build's player: "<any> play <recording folder> [<dump folder>]" (below, once Nsight Graphics' activity is up);
  // the helper's own log held shut until then (its open below fails instead of emptying it: the player logs into its own).
  const std::vector<std::wstring> play_arguments(arguments, arguments + argument_count);
  const bool play = play_arguments.size() >= 4 && play_arguments[2] == L"play";
  // The resident: "<any> serve <recording>\frame_<N> <control folder> [--nsight] [--no-dred]" (rt_serve.h), its log resident.log there.
  const bool serve = play_arguments.size() >= 5 && play_arguments[2] == L"serve";
  const HANDLE kept_log = (play || serve) ? CreateFileW(log_path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
#endif
  LocalFree(arguments);
  // The last helper's log kept (a second helper in the same run, a game started again after a crash): its last lines and
  // DRED report.
  MoveFileExW(log_path.c_str(), (folder + L"\\renodx-tw-dlaa64.old.log").c_str(), MOVEFILE_REPLACE_EXISTING);
  _wfopen_s(&log_file, log_path.c_str(), L"w");
  // stderr into the log (a /SUBSYSTEM:WINDOWS exe has none): a failed assert's expression, file and line (below). The
  // play and resident modes hold this log shut (kept_log, above), so their stderr stays unset; they reopen log_file into
  // their own logs (below).
  if (log_file != nullptr) {
    _dup2(_fileno(log_file), 2);
  }
  LogWithTime(std::format("[TW DLAA helper] Started (game {}).", game_id).c_str());
  wcsncpy_s(dump_path.data(), dump_path.size(), (folder + L"\\renodx-tw-dlaa64.dmp").c_str(), _TRUNCATE);
  SetUnhandledExceptionFilter(OnUnhandledFault);
  // The terminate handler is per thread (this one's); another thread's uncaught exception reaches abort, whose handler is
  // process-wide, as are the last two.
  std::set_terminate([] { StopOnCrtEnd("an uncaught C++ exception", twattila::dlaa_bridge::HELPER_EXIT_UNCAUGHT_EXCEPTION); });
  std::signal(SIGABRT, [](int) {
    StopOnCrtEnd("an abort (a failed assert: its text above; another thread's uncaught C++ exception, or the CRT's)",
                 twattila::dlaa_bridge::HELPER_EXIT_ABORT);
  });
  // A failed assert (no NDEBUG here) writes to stderr (the log) and aborts into the handler above: in a /SUBSYSTEM:WINDOWS
  // exe the UCRT's default is a task-modal Abort / Retry / Ignore box, its thread blocked behind a fullscreen game until a click.
  _set_error_mode(_OUT_TO_STDERR);
  _set_purecall_handler([] { StopOnCrtEnd("a pure virtual call", twattila::dlaa_bridge::HELPER_EXIT_PURE_CALL); });
  _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    StopOnCrtEnd("an invalid CRT parameter", twattila::dlaa_bridge::HELPER_EXIT_INVALID_PARAMETER);
  });
  // Nsight Graphics' GPU Trace or Graphics Capture in this process (started by it, or by a game it started): its frame
  // boundaries (`run`, below) reach it once its activity is initialized here. Without Nsight Graphics nothing is injected
  // and they do nothing.
  if (NGFX_ActivityType injected = NGFX_ActivityType_COUNT; NGFX_GetInjectedActivity(&injected) == NGFX_Result_Success) {
    NGFX_Result initialized = NGFX_Result_NotImplemented;
    switch (injected) {
      case NGFX_ActivityType_GraphicsCapture: {
        NGFX_GraphicsCapture_InitializeActivity_D3D12_Params capture = {.version = NGFX_GraphicsCapture_InitializeActivity_D3D12_Params_VER};
        initialized = NGFX_GraphicsCapture_InitializeActivity_D3D12(&capture);
        break;
      }
      case NGFX_ActivityType_GPUTrace: {
        NGFX_GPUTrace_InitializeActivity_D3D12_Params trace = {.version = NGFX_GPUTrace_InitializeActivity_D3D12_Params_VER};
        initialized = NGFX_GPUTrace_InitializeActivity_D3D12(&trace);
        break;
      }
      default:
        break;
    }
    LogWithTime(std::format("[TW DLAA helper] Nsight Graphics activity {}: frame boundaries {} (result {}).", static_cast<int>(injected),
                            (initialized == NGFX_Result_Success ? "on" : "off"), static_cast<int>(initialized))
                    .c_str());
  }
#if defined(RENODX_RECORDER)
  // The player: a recording's requests on a scene of its own on the first adapter, no game (rt_d3d12.hpp PlayRecording; Nsight
  // Graphics' frame boundary before each trace); its dump <recording>\play by default, its log renodx-tw-play.log there.
  if (play) {
    if (log_file != nullptr) fclose(log_file);
    if (kept_log != INVALID_HANDLE_VALUE) CloseHandle(kept_log);
    // Its log in its dump folder, beside its outputs.
    const std::filesystem::path recording = play_arguments[3];
    const std::filesystem::path dump = (play_arguments.size() >= 5) ? std::filesystem::path(play_arguments[4]) : recording / L"play";
    std::error_code made;
    std::filesystem::create_directories(dump, made);
    _wfopen_s(&log_file, (dump / L"renodx-tw-play.log").c_str(), L"w");
    ComPtr<IDXGIFactory4> play_factory;
    ComPtr<IDXGIAdapter1> play_adapter;
    auto player = std::make_unique<twattila::dlaa_helper::RtScene12>();
    std::string report;
    const bool played = SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(play_factory.GetAddressOf())))
                        && SUCCEEDED(play_factory->EnumAdapters1(0, play_adapter.GetAddressOf())) && player->Initialize(play_adapter.Get(), folder, false)
                        && player->PlayRecording(recording, dump,
                                                 [&] {
                                                   NGFX_FrameBoundary_D3D12_Params boundary = {.version = NGFX_FrameBoundary_D3D12_Params_VER,
                                                                                               .commandQueue = player->Queue()};
                                                   NGFX_FrameBoundary_D3D12(&boundary);
                                                 },
                                                 &report);
    if (!played && report.empty()) report = player->Status();
    if (const std::string reports = player->TakeReports(); !reports.empty()) ReShadeLogMessage(nullptr, 0, reports.c_str());
    LogWithTime(std::format("[TW player] {}: {}", played ? "Played" : "FAILED", report).c_str());
    return played ? 0 : 1;
  }
  if (serve) {
    if (log_file != nullptr) fclose(log_file);
    if (kept_log != INVALID_HANDLE_VALUE) CloseHandle(kept_log);
    _wfopen_s(&log_file, (std::filesystem::path(play_arguments[4]) / L"resident.log").c_str(), L"w");
    twattila::dlaa_helper::serve::Resident::Options options = {
        .recording = play_arguments[3],
        .control = play_arguments[4],
        .helper_folder = folder,
        .log = [](const std::string& line) { LogWithTime(line.c_str()); },
        .frame_boundary =
            [](ID3D12CommandQueue* queue) {
              NGFX_FrameBoundary_D3D12_Params boundary = {.version = NGFX_FrameBoundary_D3D12_Params_VER, .commandQueue = queue};
              NGFX_FrameBoundary_D3D12(&boundary);
            },
    };
    if (std::string why; !twattila::dlaa_helper::serve::Resident::ParseFlags(std::span(play_arguments).subspan(5), &options, &why)) {
      LogWithTime(("[TW resident] FAILED: " + why).c_str());
      return 1;
    }
    twattila::dlaa_helper::serve::Resident resident(std::move(options));
    return resident.Serve();
  }
#endif
  // RENODX_TW_REPLAYABLE (Nsight Graphics captures of the 4K harness, tools/tw/nsight): frames its replayer can make again.
  // It makes no cross-process shared fence and no second device from ID3D12DeviceFactory, so the ray tracing device opens
  // no shared fence (the addon's value is waited for and its next signalled here on D3D11, which Nsight does not capture;
  // ray tracing requests then run one at a time) and Ray Reconstruction stays off.
  const bool replayable = GetEnvironmentVariableW(L"RENODX_TW_REPLAYABLE", nullptr, 0) != 0;
  // RENODX_TW_SERIAL_REQUESTS (a test switch, tools/tw/nsight A32): every ray tracing request runs at once, as each kind's
  // first does, the shared fence kept: RENODX_TW_REPLAYABLE's one-at-a-time half alone (which half kept the helper's commit
  // bounded under Nsight's injection).
  const bool serial_requests = GetEnvironmentVariableW(L"RENODX_TW_SERIAL_REQUESTS", nullptr, 0) != 0;
  HANDLE replay_wait = nullptr;
  if (replayable) {
    replay_wait = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  }
  // A live GPU did not finish work whose resources are about to be freed (a removed device never gets here: a new one is
  // made below). Freeing them under it is not safe and nothing else can go on: the reason logged, then the process ends
  // without unwinding the GPU objects (the addon sees the exit).
  twattila::dlaa_helper::unproven_gpu_completion = [](const char* owner, uint64_t completed, uint64_t wanted) {
    LogWithTime(std::format("[TW DLAA helper] Stopped: the GPU did not finish {}'s work in time (its fence at {}, waited for {}; the "
                            "device was not removed). Its resources cannot be freed safely.",
                            owner, completed, wanted)
                    .c_str());
    static_assert(twattila::dlaa_bridge::HELPER_EXIT_GPU_UNFINISHED == ERROR_DEVICE_HARDWARE_ERROR, "the code earlier helpers ended with");
    TerminateProcess(GetCurrentProcess(), twattila::dlaa_bridge::HELPER_EXIT_GPU_UNFINISHED);
  };
#ifdef RENODX_TW_HELPER_FAULT_TEST
  // A test build only (build.ps1 with -DRENODX_TW_HELPER_FAULT_TEST added): RENODX_TW_HELPER_FAULT set, the dump's path on demand.
  if (GetEnvironmentVariableW(L"RENODX_TW_HELPER_FAULT", nullptr, 0) != 0) *static_cast<volatile int*>(nullptr) = 1;
#endif

  const std::wstring name = L"Local\\RenoDX_TW_DLAA_" + std::to_wstring(game_id);
  const auto not_found = [&](const char* what) {
    LogWithTime(std::format("[TW DLAA helper] The game's {} was not found (error {}).", what, GetLastError()).c_str());
    return static_cast<int>(twattila::dlaa_bridge::HELPER_EXIT_NOT_FOUND);
  };
  HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, game_id);
  if (game == nullptr) return not_found("process");
  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
  if (mapping == nullptr) return not_found("shared memory");
  HANDLE request = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (name + L"_request").c_str());
  if (request == nullptr) return not_found("request event");
  HANDLE done = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (name + L"_done").c_str());
  if (done == nullptr) return not_found("completion event");
  // The block as the addon's version made it (v2's ends before v3's fields): its version first.
  const auto map = [&](size_t bytes) {
    return static_cast<twattila::dlaa_bridge::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
  };
  auto* shared = map(twattila::dlaa_bridge::SharedBytes(twattila::dlaa_bridge::OLDEST_VERSION));
  const uint32_t version = (shared != nullptr ? shared->version : 0u);
  if (version >= 3 && shared != nullptr) {
    UnmapViewOfFile(shared);
    shared = map(twattila::dlaa_bridge::SharedBytes(version));
  }
  if (shared == nullptr || version < twattila::dlaa_bridge::OLDEST_VERSION || version > twattila::dlaa_bridge::VERSION) {
    LogWithTime(std::format("[TW DLAA helper] Shared memory unusable (the addon's bridge v{}, this helper's v{} to v{}; error {}).", version,
                            twattila::dlaa_bridge::OLDEST_VERSION, twattila::dlaa_bridge::VERSION, (shared == nullptr) ? GetLastError() : 0u)
                    .c_str());
    return static_cast<int>(twattila::dlaa_bridge::HELPER_EXIT_VERSION);
  }
  const auto set_status = [&](const char* status) {
    strncpy_s(shared->status, status, _TRUNCATE);
  };
  if (log_file == nullptr) {
    set_status("DLAA helper: its log renodx-tw-dlaa64.log could not be opened");
  }
  // The cluster sets' data (v16): the addon's second mapping (null: none, no compact draws).
  const uint8_t* cluster_data = nullptr;
  if (HANDLE clusters = (version >= 16) ? OpenFileMappingW(FILE_MAP_READ, FALSE, (name + L"_clusters").c_str()) : nullptr; clusters != nullptr) {
    cluster_data = static_cast<const uint8_t*>(MapViewOfFile(clusters, FILE_MAP_READ, 0, 0, twattila::dlaa_bridge::RT_CLUSTER_DATA_BYTES));
    CloseHandle(clusters);
  }

  // A device on the game's GPU (again after a GPU reset removed it: every request waits and signals on it).
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  const LUID luid = {.LowPart = shared->adapter_luid_low, .HighPart = shared->adapter_luid_high};
  constexpr D3D_FEATURE_LEVEL LEVELS[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  ComPtr<ID3D11Device1> device1;
  ComPtr<ID3D11Device5> device5;
  ComPtr<ID3D11DeviceContext4> context4;
  // The first failure (E_NOINTERFACE: a device without D3D11.4).
  const auto make_device = [&] {
    HRESULT made = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, LEVELS, 2, D3D11_SDK_VERSION,
                                     device.ReleaseAndGetAddressOf(), nullptr, context.ReleaseAndGetAddressOf());
    if (SUCCEEDED(made)) made = device.As(&device1);
    if (SUCCEEDED(made)) made = device.As(&device5);
    if (SUCCEEDED(made)) made = context.As(&context4);
    return made;
  };
  {
    const char* step = "factory";
    HRESULT made = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (SUCCEEDED(made)) {
      step = "adapter";
      made = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(adapter.GetAddressOf()));
    }
    if (SUCCEEDED(made)) {
      step = "device";
      made = make_device();
    }
    if (FAILED(made)) {
      const std::string why = std::format("DLAA helper: no D3D11.4 device on the game's GPU ({} 0x{:08X})", step, static_cast<uint32_t>(made));
      set_status(why.c_str());
      LogWithTime(why.c_str());
      return static_cast<int>(twattila::dlaa_bridge::HELPER_EXIT_NO_D3D11);
    }
  }
  // Only requests are answered through `done` (the addon reads `ready` before its first one).
  if (log_file != nullptr) {
    set_status("DLAA helper ready");
  }
  shared->ready = 1;

  talesofarise::dlss::Backend backend;
  bool super_resolution_ran = false;  // its feature is made (freed while Ray Reconstruction runs)
  bool super_resolution_opened = false;  // this set's textures tried on D3D11
  // Ray Reconstruction (v3 addons with its guides): its D3D12 device and the addon's objects opened there (a new device
  // after a GPU reset removed it).
  auto reconstruction = std::make_unique<twattila::dlaa_helper::RayReconstruction12>();
  bool reconstruction_open = false;
  // FSR or XeSS (v22 addons choosing one): opened with their first request of a set; NGX stays out of it (DLSS and Ray
  // Reconstruction are for NVIDIA's GPUs: their licence).
  auto upscaler = std::make_unique<twattila::dlaa_helper::Upscaler12>();
  bool upscaler_tried = false;
  bool upscaler_open = false;
  bool upscaler_live = false;
  // The game's view-space unit in metres (v33: the addon wrote it before it started this helper), FSR's
  // viewSpaceToMetersFactor for its upscaler and frame generation.
  const float metres_per_unit = (version >= 33) ? shared->metres_per_unit : 1.f;
  const auto other_upscaler = [&] { return version >= 22 && shared->upscaler != twattila::dlaa_bridge::UPSCALER_DLSS; };
  std::array<uint32_t, 7> upscale_said = {};
  // Ray tracing (v4 addons with it on): its D3D12 device and the addon's objects opened there. After a GPU reset removed
  // it, a new one with the Experimental options that build the scene in their own way paused until the game restarts; a
  // second removal leaves ray tracing off (ATN 2026-10-29: a hang, then three new devices lost at once, and Windows then
  // refused the helper new devices: frame generation's too). A request that could not be recorded takes a new scene too,
  // nothing paused; the fourth leaves it off (whatever made it fail would come back each time).
  auto scene = std::make_unique<twattila::dlaa_helper::RtScene12>();
  bool scene_open = false;
  // A scene whose own GPU work a request could not wait out on a live device (TimedOut: the game's GPU seconds behind):
  // kept until that work is done, since freeing it under the work would have to end the helper (and with it everything
  // until the game restarts). Ray tracing passes meanwhile; then a new scene, nothing paused. A stall now and then is
  // forgotten; the fourth with no more than STALL_MEMORY between them leaves ray tracing off (a GPU that keeps falling
  // behind would have its scene made and captured again without end).
  std::unique_ptr<twattila::dlaa_helper::RtScene12> stalled_scene;
  constexpr auto STALL_MEMORY = std::chrono::minutes(10);
  // Ray tracing's and Ray Reconstruction's devices let go after IDLE_RELEASE without a request of theirs (gigabytes of the
  // GPU's memory: 15 GB stayed after RT/PT was turned off, 7.5 GB with it never on, ATN 2026-11-01), opened again with
  // their next one (the addon then captures its scene again: rt_device). FSR's and XeSS's too. Frame generation's resources, swap chain and window
  // too, once its set has no textures of it: turned off in the settings, not ReShade's menu open (1.5 GB stayed; ~0.6 GB of
  // it, Streamline and its device, still does: FreeResources).
  constexpr auto IDLE_RELEASE = std::chrono::seconds(10);
  auto rt_used = std::chrono::steady_clock::now();
  auto rr_used = rt_used;
  auto fg_used = rt_used;
  auto upscale_used = rt_used;
  bool generation_resources = false;
  bool streamline_used = false;
  bool scene_idle = false;
  bool reconstruction_idle = false;
  bool scene_live = false;           // its device made (open or closed: a set without ray tracing closes it, its memory stays)
  bool reconstruction_live = false;
  uint32_t scene_losses = 0;
  uint32_t scene_failures = 0;
  uint32_t scene_stalls = 0;
  auto scene_stalled = rt_used;
  const auto scene_off = [&] { return scene_losses >= 2 || scene_failures >= 4 || scene_stalls >= 4; };
  constexpr uint32_t PAUSED_TRACE_FLAGS = twattila::dlaa_bridge::RT_GPU_MICROMAPS | twattila::dlaa_bridge::RT_SWAY_IN_VIEW;
  constexpr uint32_t PAUSED_PATH_FLAGS = twattila::dlaa_bridge::RT_PATH_SER | twattila::dlaa_bridge::RT_PATH_SHARC;
  constexpr const char* RT_OFF = "Ray tracing off after repeated failures (renodx-tw-dlaa64.log): restart the game";
  const auto scene_status = [&]() -> std::string {
    if (scene_off()) return RT_OFF;
    return stalled_scene ? "Ray tracing waits for the GPU to finish its last requests" : scene->Status();
  };
  // The handles the addon shared for the open set (its textures and fence, duplicated into this process), kept until the
  // next set: a new ray tracing device opens them again.
  std::vector<HANDLE> set_handles;
  // Frame generation (v5 addons with it on): Streamline's D3D12 device, its window and swap chain (made again after a GPU
  // reset removed the device). FSR's or XeSS's (v23 addons) in Streamline's place: one backend serves a set.
  auto generation = std::make_unique<twattila::dlaa_helper::FrameGeneration12>();
  auto proxy_generation = std::make_unique<twattila::dlaa_helper::ProxyFrameGeneration12>();
  uint32_t generation_backend = twattila::dlaa_bridge::FG_BACKEND_DLSS;
  std::array<bool, 3> generation_refused = {};  // backends Auto no longer tries
  // FSR's and XeSS's objects replaced while they still make their context or chain on their own thread (a cold start can
  // pass the answer's budget: upscale_d3d12.hpp): kept until that is done, as stalled_scene, then freed with a request or an
  // idle wake, so no request waits for it. A GPU reset's: freed at once (its making on the removed device ends soon; kept, the
  // device would refuse the new ones: MakeOwnDevice).
  std::vector<std::unique_ptr<twattila::dlaa_helper::Upscaler12>> parked_upscalers;
  std::vector<std::unique_ptr<twattila::dlaa_helper::ProxyFrameGeneration12>> parked_generations;
  const auto free_parked = [&] {
    std::erase_if(parked_upscalers, [](const auto& parked) { return !parked->Making() || parked->Lost(); });
    std::erase_if(parked_generations, [](const auto& parked) { return !parked->Making() || parked->Lost(); });
  };
  const auto new_upscaler = [&] {
    if (upscaler->Making() && !upscaler->Lost()) {
      parked_upscalers.push_back(std::move(upscaler));
    }
    upscaler = std::make_unique<twattila::dlaa_helper::Upscaler12>();
  };
  const auto new_proxy_generation = [&] {
    if (proxy_generation->Making() && !proxy_generation->Lost()) {
      parked_generations.push_back(std::move(proxy_generation));
    }
    proxy_generation = std::make_unique<twattila::dlaa_helper::ProxyFrameGeneration12>();
  };
  DXGI_ADAPTER_DESC1 adapter_description = {};
  adapter->GetDesc1(&adapter_description);
  // RENODX_TW_FAKE_VENDOR (tests): another's GPU, as in the addon.
  if (std::array<wchar_t, 16> fake = {}; GetEnvironmentVariableW(L"RENODX_TW_FAKE_VENDOR", fake.data(), static_cast<DWORD>(fake.size())) != 0) {
    adapter_description.VendorId = std::wcstoul(fake.data(), nullptr, 16);
  }
  bool generation_open = false;
  uint32_t opened_generation = 0;
  // Opened by a set's first DLSS request: a shared texture opened twice in this process (D3D11 and D3D12, or Ray Reconstruction
  // and frame generation) stays in video memory after both let it go, until the process ends, whichever the order or a new
  // D3D11 device (2026-11-03, tw_fg_test: 190 MiB with each Ray Reconstruction set, now 2; frame generation's depth and motion
  // still 64 MiB with each of its sets beside DLSS or Ray Reconstruction).
  ComPtr<ID3D11Texture2D> color;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11Texture2D> motion;
  ComPtr<ID3D11Texture2D> output;
  ComPtr<ID3D11Fence> fence;
  // Ray tracing requests of a kind (trace, Light Bounce, reflections) that ran last time are answered once copied (their
  // fence value is always the next; v16: with their new cluster sets' data, which the addon writes again for its next)
  // and recorded in order by a worker of their own while the game goes on; the rest, and anything that opens or closes
  // the scene, wait for the worker to be done.
  struct Work {
    std::unique_ptr<twattila::dlaa_bridge::Shared> shared;
    std::vector<uint8_t> sets;
  };
  struct RtWork {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Work> pending;
    std::array<bool, 3> ran = {};
    bool busy = false;
    bool stop = false;
  } rt_work;
  // A trace request's new cluster sets' data: the mapping's start up to the end of the last.
  const auto new_sets = [&](const twattila::dlaa_bridge::Shared& from) {
    size_t bytes = 0;
    for (uint32_t index = 0; version >= 16 && from.rt_request == 1 && index < std::min(from.rt_new_set_count, twattila::dlaa_bridge::RT_NEW_SETS); ++index) {
      const auto& set = from.rt_new_sets[index];
      bytes = std::max(bytes, size_t{set.offset} + (size_t{set.clusters} * 16) + (size_t{set.triangles} * 3));
    }
    return (cluster_data != nullptr) ? std::span(cluster_data, std::min<size_t>(bytes, twattila::dlaa_bridge::RT_CLUSTER_DATA_BYTES))
                                     : std::span<const uint8_t>();
  };
#if defined(RENODX_RECORDER)
  // The bridge recorder (the recorder build; rt_d3d12.hpp SaveRecordingState): with RENODX_TW_RECORD=<folder> (relative: the
  // helper's) set when the game starts, a recording starts at the trace of frame RENODX_TW_RECORD_AT (or the first after it)
  // or at the first after renodx_record_now appears next to the helper (looked for every 30 traces, then deleted): the
  // snapshot, then the requests of RENODX_TW_RECORD_FRAMES frames (2) into <folder>\frame_<frame> (with
  // RENODX_TW_RECORD_HISTORIES=1 the last frame's histories too).
  const std::filesystem::path record_folder = [&] {
    std::array<wchar_t, MAX_PATH> value = {};
    if (GetEnvironmentVariableW(L"RENODX_TW_RECORD", value.data(), MAX_PATH) == 0) return std::filesystem::path();
    const std::filesystem::path given = value.data();
    return given.is_absolute() ? given : std::filesystem::path(folder) / given;
  }();
  const auto record_number = [](const wchar_t* name, uint32_t otherwise) {
    std::array<wchar_t, 32> value = {};
    return (GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size())) != 0) ? static_cast<uint32_t>(std::wcstoul(value.data(), nullptr, 10))
                                                                                                : otherwise;
  };
  struct Recorder {
    uint32_t at = UINT32_MAX;  // the frame a recording starts at (UINT32_MAX: none asked for)
    uint32_t frames = 2;
    uint32_t first = 0;
    uint32_t last = 0;
    const twattila::dlaa_helper::RtScene12* scene = nullptr;  // the recording's (null: none on its way)
    uint32_t traces = 0;
    uint32_t waits = 0;
  } recorder = {.at = record_number(L"RENODX_TW_RECORD_AT", UINT32_MAX), .frames = std::max(record_number(L"RENODX_TW_RECORD_FRAMES", 2), 1u)};
  const std::filesystem::path record_marker = std::filesystem::path(folder) / L"renodx_record_now";
  // A recorder's line into the helper's log and <folder>\recorder.log (kept when the helper's log is not: another process
  // holding it, a helper started again).
  const auto say = [&](const std::string& line) {
    LogWithTime(line.c_str());
    SYSTEMTIME now;
    GetLocalTime(&now);
    std::error_code ignored;
    std::filesystem::create_directories(record_folder, ignored);
    std::ofstream(record_folder / L"recorder.log", std::ios::app)
        << std::format("{:02}:{:02}:{:02} pid {} {}\n", now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId(), line);
  };
  const auto record = [&](const twattila::dlaa_bridge::Shared& from) {
    if (record_folder.empty()) return;
    const uint32_t frame = from.rt_frame;
    if (recorder.scene != nullptr) {
      if (recorder.scene != scene.get()) {
        say("[TW recorder] The recording stopped: a new ray tracing scene (a GPU reset, a failed request or 10 s idle).");
        recorder.scene = nullptr;
      } else if (frame > recorder.last || frame < recorder.first) {
        say(std::format("[TW recorder] {}.", scene->FinishRecording()));
        recorder.scene = nullptr;
      }
      return;
    }
    // The marker stays until a recording starts or is refused (a helper started again finds it too).
    if (++recorder.traces % 30 == 0 && recorder.at == UINT32_MAX && GetFileAttributesW(record_marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
      recorder.at = frame;
      say(std::format("[TW recorder] renodx_record_now found at frame {}.", frame));
    }
    if (recorder.at == UINT32_MAX || frame < recorder.at) return;
    std::string why;
    bool refused = false;
    const std::filesystem::path into = record_folder / std::format("frame_{}", frame);
    const bool saved = scene->SaveRecordingState(into, frame, &why, &refused);
    // Still waiting after ~30 s (a scene that keeps capturing): given up, logged.
    if (!saved && !refused && recorder.waits >= 1800) {
      refused = true;
      why = std::format("still {} after {} traces", why, recorder.waits);
    }
    if (saved || refused) {
      std::error_code ignored;
      std::filesystem::remove(record_marker, ignored);
    }
    if (!saved) {
      if (refused) {
        recorder.at = UINT32_MAX;
        recorder.waits = 0;
        say(std::format("[TW recorder] Not recorded at frame {}: {}.", frame, why));
      } else if (recorder.waits++ % 60 == 0) {
        say(std::format("[TW recorder] Frame {}: {}; tried again at the next trace.", frame, why));
      }
      return;
    }
    scene->RecordFrames(into, frame, frame + recorder.frames - 1, true, record_number(L"RENODX_TW_RECORD_HISTORIES", 0) != 0);
    recorder = {.frames = recorder.frames, .first = frame, .last = frame + recorder.frames - 1, .scene = scene.get(), .traces = recorder.traces};
    say(std::format("[TW recorder] Recording frames {} to {} into {} ({}).", frame, recorder.last, into.string(), why));
  };
#endif
  const auto run = [&](const twattila::dlaa_bridge::Shared& from, std::span<const uint8_t> sets) {
    if (version >= 7 && from.rt_request >= 2) return scene->Paths(from, version, from.rt_request == 3);
#if defined(RENODX_RECORDER)
    record(from);
#endif
    // A frame's ray tracing starts with its trace request, so the last frame's last submit is behind: Nsight Graphics'
    // frame boundary (the helper never presents, its default delimiter).
    NGFX_FrameBoundary_D3D12_Params boundary = {.version = NGFX_FrameBoundary_D3D12_Params_VER, .commandQueue = scene->Queue()};
    NGFX_FrameBoundary_D3D12(&boundary);
    return scene->Trace(from, version, sets);
  };
  const auto kind = [&](const twattila::dlaa_bridge::Shared& from) {
    return std::min<uint32_t>(version >= 7 ? from.rt_request : 1u, 3u) - 1;
  };
  // Its status for the addon, (v15) the garbage triangles its scene left out so far, the last such capture's key, and
  // (v16) whether it takes compact draws.
  const auto publish = [&] {
    strncpy_s(shared->rt_status, scene_status().c_str(), _TRUNCATE);
    if (const std::string reports = scene->TakeReports(); !reports.empty()) ReShadeLogMessage(nullptr, 0, reports.c_str());
    if (version >= 15) {
      const auto& left_out = scene->LeftOutSoFar();
      shared->rt_left_out = left_out.far_off + left_out.parts + left_out.others;
      shared->rt_left_out_key = left_out.key;
    }
    if (version >= 16) {
      shared->rt_compact_ok = (cluster_data != nullptr) ? scene->CompactState() : 0u;
      shared->rt_dynamic_uvs = scene->DynamicUvs();
    }
    if (version >= 23) shared->rt_nrd_off = scene->NrdOff();
    if (version >= 32) shared->rt_di_ok = scene->DiOk();
  };
  std::thread rt_worker([&] {
    std::unique_lock lock(rt_work.mutex);
    while (true) {
      rt_work.changed.wait(lock, [&] { return rt_work.stop || !rt_work.pending.empty(); });
      if (rt_work.pending.empty()) return;
      const Work work = std::move(rt_work.pending.front());
      rt_work.pending.pop_front();
      rt_work.busy = true;
      lock.unlock();
      const bool ran = run(*work.shared, work.sets);
      publish();
      lock.lock();
      rt_work.ran[kind(*work.shared)] = ran;
      rt_work.busy = false;
      rt_work.changed.notify_all();
    }
  });
  const auto rt_drain = [&] {
    std::unique_lock lock(rt_work.mutex);
    rt_work.changed.wait(lock, [&] { return rt_work.pending.empty() && !rt_work.busy; });
  };
  // A GPU fault or hang (Windows' timeout detection) removed the ray tracing's device, or a request could not be recorded:
  // where the GPU stopped logged (DRED), a new scene (opened by the caller after the first) and the addon told to capture
  // its scene again.
  const auto replace_lost_scene = [&] {
    rt_drain();
    // A failed request's reason (its skip text or Close's HRESULT) goes with the scene.
    const std::string why = scene->Status();
    const bool removed = scene->Removed();
    if (removed) {
      ReShadeLogMessage(nullptr, 0, scene->LostReport().c_str());
    }
    const bool stalled = !removed && scene->TimedOut();
    const auto stall = scene->LastStall();
    if (stalled) {
      stalled_scene = std::move(scene);
    }
    scene = std::make_unique<twattila::dlaa_helper::RtScene12>();
    scene_open = false;
    {
      const std::unique_lock lock(rt_work.mutex);
      rt_work.ran = {};
    }
    if (version >= 15) ++shared->rt_device;
    if (stalled) {
      const auto now = std::chrono::steady_clock::now();
      if (now - scene_stalled > STALL_MEMORY) {
        scene_stalls = 0;
      }
      scene_stalled = now;
      ++scene_stalls;
      const std::string values = std::format("its fence at {}, waited for {}, stall {} of 4", stall.completed, stall.wanted, scene_stalls);
      if (scene_off()) {
        LogWithTime(std::format("[TW ray tracing] Off until the game restarts (the GPU did not finish its requests in time, four times in a row; {}).", values)
                        .c_str());
      } else {
        LogWithTime(std::format("[TW ray tracing] The GPU did not finish its last requests in time ({}; its device was not removed): ray tracing "
                                "passes until it has, then starts again.",
                                values)
                        .c_str());
      }
      return;
    }
    if (!removed) {
      ++scene_failures;
      if (scene_off()) {
        ReShadeLogMessage(nullptr, 0, std::format("[TW ray tracing] Off until the game restarts (a fourth failed request: {}).", why).c_str());
      } else {
        ReShadeLogMessage(nullptr, 0, std::format("[TW ray tracing] Started again after a failed request ({}).", why).c_str());
      }
      return;
    }
    ReShadeLogMessage(nullptr, 0, (++scene_losses == 1)
                                      ? "[TW ray tracing] Started again on a new device, with GPU Micromap Baking and the Experimental Swaying "
                                        "Shadows Range, Off-Screen Sway, Shader Execution Reordering and SHaRC Radiance Cache paused until the game "
                                        "restarts."
                                      : "[TW ray tracing] Off until the game restarts (a second GPU reset).");
  };
  // Ray tracing's idle release destroys its scene on this thread, off the request loop: the scene's device and its gigabytes
  // took 70 to 101 ms inside the answer to the DLSS request that found it idle, a frame's hitch in the game (2026-11-16
  // audit A1g). Joined before a scene is made again and at the end.
  std::thread scene_release;
  const auto wait_scene_release = [&] {
    if (scene_release.joinable()) scene_release.join();
  };
  const auto ms_since = [](std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  };
  // Each open that did its work gets one line with its time (not a replay's): how close a cold shader cache runs to the
  // addon's wait for a set's first answer.
  const auto open_scene = [&] {
    wait_scene_release();
    const bool details = (shared->rt_flags & twattila::dlaa_bridge::RT_DETAILS) != 0;
    scene_live = !scene_off();
    const auto started = std::chrono::steady_clock::now();
    const bool initialized = scene_live && !stalled_scene && scene->Initialize(adapter.Get(), folder, details, other_upscaler());
    const auto initialize_ms = ms_since(started);
    scene_open = initialized && scene->Open(*shared, version, replayable);
    // From here on: a set's first request is an evaluation, and an open can take longer than the idle time by itself.
    rt_used = std::chrono::steady_clock::now();
    if (scene_open && !replayable) {
      const auto open_ms = ms_since(started);
      LogWithTime(std::format("[TW DLAA helper] Ray tracing opened in {} ms (device and pipelines {} ms, the set {} ms).", open_ms,
                              initialize_ms, open_ms - initialize_ms)
                      .c_str());
    }
  };
  // The open set's objects in Ray Reconstruction and frame generation (their handles stay open: set_handles), the latter on a
  // new device when a GPU reset removed its own.
  std::string reconstruction_refusal;
  const auto open_reconstruction = [&] {
    if (replayable) return;
    reconstruction_live = true;
    const auto started = std::chrono::steady_clock::now();
    reconstruction_open = reconstruction->Initialize(adapter.Get(), folder)
                          && reconstruction->Open({shared->color, shared->depth, shared->motion, shared->output, shared->diffuse_albedo,
                                                   shared->specular_albedo, shared->normals, shared->roughness},
                                                  shared->fence, (version >= 17) ? shared->rr_hit_distance : 0u,
                                                  (version >= 25) ? std::array{shared->rr_extra[0], shared->rr_extra[1], shared->rr_extra[2]}
                                                                  : std::array<uint64_t, twattila::dlaa_helper::EXTRA_INPUTS>{});
    rr_used = std::chrono::steady_clock::now();
    // A refused one is asked again each second: its refusal is said once.
    if (reconstruction_open) {
      LogWithTime(std::format("[TW DLAA helper] Ray Reconstruction opened in {} ms.", ms_since(started)).c_str());
    } else if (reconstruction->Status() != reconstruction_refusal) {
      reconstruction_refusal = reconstruction->Status();
      ReShadeLogMessage(nullptr, 0, ("[TW DLAA helper] " + reconstruction_refusal).c_str());
    }
  };
  // Streamline stopped after a GPU reset (both start again, Streamline first), Ray Reconstruction's object made again before
  // it (fg_bridge_test 2026-11-16): NGX's shutdown in that object's destructor after Streamline's stop faults in nvngx_dlssd,
  // and with Streamline left running its next evaluation crashes (2026-11-03). A removed device of Ray Reconstruction's goes
  // that way (kept, no new device could be made: MakeOwnDevice); after Streamline's own loss its live object stays
  // unreleased instead: that shutdown before Streamline's stop leaves Streamline's removed device held (also with
  // FreeResources first). Both removed at once: no order found (the first way, Streamline's device then held).
  const auto stop_streamline = [&] {
    generation->Shutdown();
    generation = std::make_unique<twattila::dlaa_helper::FrameGeneration12>();
    streamline_used = false;
  };
  const auto generation_status = [&] {
    return (generation_backend == twattila::dlaa_bridge::FG_BACKEND_DLSS) ? generation->Status().c_str() : proxy_generation->Status().c_str();
  };
  const auto generation_presented = [&] {
    return (generation_backend == twattila::dlaa_bridge::FG_BACKEND_DLSS) ? generation->Presented() : proxy_generation->Presented();
  };
  const auto generation_lost = [&] {
    return (generation_backend == twattila::dlaa_bridge::FG_BACKEND_DLSS) ? generation->Lost() : proxy_generation->Lost();
  };
  const auto free_generation = [&] {
    if (generation_backend == twattila::dlaa_bridge::FG_BACKEND_DLSS) {
      generation->FreeResources();
    } else {
      proxy_generation->FreeResources();
    }
  };
  const auto open_generation = [&] {
    using namespace twattila::dlaa_bridge;
    const auto started = std::chrono::steady_clock::now();
    const bool streamline_lost = generation->Lost();
    if (streamline_lost) {
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] A GPU reset removed frame generation's device: Streamline started again.");
      reconstruction->Close();
      reconstruction.release();
      reconstruction = std::make_unique<twattila::dlaa_helper::RayReconstruction12>();
      stop_streamline();
    }
    // The player's choice alone (DLSS only on NVIDIA's GPUs, else as Auto), or Auto's order.
    const bool nvidia = adapter_description.VendorId == 0x10DE;
    const uint32_t wanted = (version >= 23 ? shared->fg_backend : FG_BACKEND_DLSS);
    std::array<uint32_t, 3> order = {FG_BACKEND_FSR, FG_BACKEND_XESS, FG_BACKEND_FSR};
    size_t count = 2;
    if (wanted != FG_BACKEND_AUTO && (wanted != FG_BACKEND_DLSS || nvidia || version < 23)) {
      order[0] = wanted;
      count = 1;
    } else if (nvidia) {
      order = {FG_BACKEND_DLSS, FG_BACKEND_FSR, FG_BACKEND_XESS};
      count = 3;
    } else if (adapter_description.VendorId == 0x8086) {
      order = {FG_BACKEND_XESS, FG_BACKEND_FSR, FG_BACKEND_FSR};
    }
    generation_open = false;
    for (size_t index = 0; index < count && !generation_open; ++index) {
      const uint32_t backend = order[index];
      if (generation_refused[backend]) continue;
      // Another backend's window and swap chain gone first; its memory with them (FSR's or XeSS's chain still being made:
      // parked, FreeResources would wait for it here).
      if (generation_resources && generation_backend != backend) {
        if (generation_backend != FG_BACKEND_DLSS && proxy_generation->Making()) {
          new_proxy_generation();
        } else {
          free_generation();
        }
      }
      generation_backend = backend;
      if (backend == FG_BACKEND_DLSS) {
        streamline_used = true;
        generation_open = generation->Initialize(adapter.Get(), folder) && generation->Open(*shared, version);
      } else {
        if (proxy_generation->Lost() || proxy_generation->Backend() != backend) {
          if (proxy_generation->Lost()) ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] A GPU reset removed frame generation's device: a new one.");
          new_proxy_generation();
        }
        generation_open = proxy_generation->Initialize(adapter.Get(), folder, backend) && proxy_generation->Open(*shared);
      }
      if (!generation_open) {
        // Sticky inside FSR's or XeSS's object, not a new device refused for a while after GPU resets; Streamline's is its own.
        generation_refused[backend] = backend != FG_BACKEND_DLSS && proxy_generation->Failed();
        ReShadeLogMessage(nullptr, 0, generation_status());
      }
    }
    // The other's textures of the last set let go.
    if (generation_backend == FG_BACKEND_DLSS) {
      proxy_generation->Close();
    } else {
      generation->Close();
    }
    // FSR's and XeSS's swap chain is still being made on its own thread (Making): their time is the start's only.
    if (generation_open && !replayable) {
      constexpr std::array<const char*, 3> BACKEND_NAMES = {"DLSS", "FSR", "XeSS"};
      LogWithTime(std::format("[TW DLAA helper] Frame generation ({}) {} in {} ms.", BACKEND_NAMES[generation_backend],
                              (generation_backend == FG_BACKEND_DLSS ? "opened" : "started"), ms_since(started))
                      .c_str());
    }
    strncpy_s(shared->fg_status, generation_status(), _TRUNCATE);
    if (version >= 13) shared->fg_presented = generation_presented();
    shared->fg_failed = (generation_open ? 0u : 1u);
    if (version >= 23) shared->fg_running = generation_backend;
    if (streamline_lost && reconstruction_open) open_reconstruction();
  };
  // A release's line in the log: how long it stalled the requests (an answer later than 5 s turns DLSS off) and this
  // process's video memory before and after.
  ComPtr<IDXGIAdapter3> memory_adapter;
  adapter.As(&memory_adapter);
  const auto video_memory_mb = [&] {
    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    if (memory_adapter) memory_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    return info.CurrentUsage / (1024 * 1024);
  };
  const auto logged_release = [&](const char* what, const auto& release) {
    const auto started = std::chrono::steady_clock::now();
    const uint64_t before = video_memory_mb();
    release();
    const auto took = ms_since(started);
    ReShadeLogMessage(nullptr, 0, std::format("{} ({} ms; this process's video memory {} -> {} MB).", what, took, before, video_memory_mb()).c_str());
  };
  // Lets go what had no request for IDLE_RELEASE up to `as_of`: a request's start (so nothing it opens or asks for is let
  // go by it, also after an open longer than IDLE_RELEASE), else now.
  const auto release_idle = [&](std::chrono::steady_clock::time_point as_of) {
    if (scene_live && version >= 15 && as_of - rt_used > IDLE_RELEASE) {
      rt_drain();
      wait_scene_release();
      scene_release = std::thread([released = std::move(scene), &logged_release]() mutable {
        logged_release("[TW ray tracing] Off for 10 s: its memory released, beside the requests (it starts again when turned on)",
                       [&] { released.reset(); });
      });
      scene = std::make_unique<twattila::dlaa_helper::RtScene12>();
      scene_open = false;
      scene_live = false;
      scene_idle = true;
      {
        const std::unique_lock lock(rt_work.mutex);
        rt_work.ran = {};
      }
      ++shared->rt_device;
    }
    if (reconstruction_live && as_of - rr_used > IDLE_RELEASE) {
      reconstruction_live = false;
      if (streamline_used) {
        // NGX stays: its shutdown, also of another device, crashes Streamline's next evaluation (2026-11-03, fg_bridge_test). The
        // feature's memory goes with it (FreeMemOnReleaseFeature).
        logged_release("[TW DLAA helper] Ray Reconstruction off for 10 s: its feature released", [&] { reconstruction->ReleaseFeature(); });
      } else {
        logged_release("[TW DLAA helper] Ray Reconstruction off for 10 s: its memory released",
                       [&] { reconstruction = std::make_unique<twattila::dlaa_helper::RayReconstruction12>(); });
        reconstruction_open = false;
        reconstruction_idle = true;
      }
    }
    if (upscaler_live && as_of - upscale_used > IDLE_RELEASE) {
      logged_release("[TW DLAA helper] FSR / XeSS off for 10 s: their memory released", new_upscaler);
      upscaler_tried = false;
      upscaler_open = false;
      upscaler_live = false;
    }
    // FSR's or XeSS's chain still being made: let go at a later wake, once made (FreeResources would wait for it here).
    const bool generation_making = generation_backend != twattila::dlaa_bridge::FG_BACKEND_DLSS && proxy_generation->Making();
    if (generation_resources && as_of - fg_used > IDLE_RELEASE && !generation_making) {
      logged_release("[TW DLAA helper] Frame generation off for 10 s: its memory released", free_generation);
      generation_resources = false;
    }
  };
  // Whose device a GPU reset removed, once each, also with no request coming (the game stuck: ATN's freeze left no line here).
  // 0x887A0006: hung, its own work. 0x887A0007: reset, another's work.
  std::array<HRESULT, 5> removed_logged = {};
  auto last_request = std::chrono::steady_clock::now();
  const auto log_removed_devices = [&] {
    const std::array<std::pair<const char*, HRESULT>, 5> reasons = {{
        {"its D3D11", device->GetDeviceRemovedReason()},
        {"ray tracing's", scene->RemovedReason()},
        {"Ray Reconstruction's", reconstruction->RemovedReason()},
        {"FSR's / XeSS's", upscaler->RemovedReason()},
        {"frame generation's", (generation_backend == twattila::dlaa_bridge::FG_BACKEND_DLSS) ? generation->RemovedReason() : proxy_generation->RemovedReason()},
    }};
    for (size_t index = 0; index < reasons.size(); ++index) {
      const auto& [whose, reason] = reasons[index];
      if (reason == removed_logged[index]) continue;
      removed_logged[index] = reason;
      if (SUCCEEDED(reason)) continue;
      LogWithTime(std::format("[TW DLAA helper] A GPU reset removed {} device (0x{:08X}), last request {} ms ago.", whose, static_cast<uint32_t>(reason),
                              ms_since(last_request))
                      .c_str());
    }
  };
  // v30: this set's fence as read now, for the addon's watcher (a request's work that the GPU never finishes shows there
  // as a value that stays behind; the game's thread may be blocked by then, so also read each second without a request).
  const auto publish_fence = [&] {
    if (version < 30) return;
    shared->fence_generation = opened_generation;
    shared->fence_completed = (fence ? fence->GetCompletedValue() : UINT64_MAX);
  };
  const HANDLE waits[] = {request, game};
  // With no requests coming (DLSS turned off) the wait still wakes for release_idle.
  const auto next_request = [&] {
    DWORD woken = WaitForMultipleObjects(2, waits, FALSE, 1000);
    for (; woken == WAIT_TIMEOUT; woken = WaitForMultipleObjects(2, waits, FALSE, 1000)) {
      log_removed_devices();
      publish_fence();
      release_idle(std::chrono::steady_clock::now());
      free_parked();
    }
    return woken;
  };
  auto refused_tried = std::chrono::steady_clock::now();
  uint32_t exit_code = twattila::dlaa_bridge::HELPER_EXIT_DONE;
  DWORD woken = WAIT_OBJECT_0;
  while ((woken = next_request()) == WAIT_OBJECT_0) {
    log_removed_devices();
    last_request = std::chrono::steady_clock::now();
    // A GPU reset removed the D3D11 device every request waits and signals on: a new one, this set's fence opened again on
    // it (its handles stay open: set_handles; the textures with the next DLSS request). None: the helper ends, which the
    // addon's watcher reports (else every request would pass unanswered for good).
    if (FAILED(device->GetDeviceRemovedReason())) {
      backend.Reset();
      super_resolution_ran = false;
      super_resolution_opened = false;
      color.Reset();
      depth.Reset();
      motion.Reset();
      output.Reset();
      fence.Reset();
      context4.Reset();
      device5.Reset();
      device1.Reset();
      context.Reset();
      device.Reset();
      if (const HRESULT made = make_device(); FAILED(made)) {
        LogWithTime(std::format("[TW DLAA helper] A GPU reset removed its D3D11 device and no new one could be made (0x{:08X}): the helper ends.",
                                static_cast<uint32_t>(made))
                        .c_str());
        exit_code = twattila::dlaa_bridge::HELPER_EXIT_D3D11_LOST;
        break;
      }
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] A GPU reset removed its D3D11 device: a new one.");
      if (shared->generation == opened_generation && shared->color != 0) {
        if (const HRESULT opened = device5->OpenSharedFence(reinterpret_cast<HANDLE>(shared->fence), IID_PPV_ARGS(fence.GetAddressOf()));
            FAILED(opened)) {
          fence.Reset();
          LogWithTime(std::format("[TW DLAA helper] The set's fence could not be opened on the new D3D11 device (0x{:08X}).", static_cast<uint32_t>(opened))
                          .c_str());
        }
      }
    }
    // The stalled scene's work is done, or its device removed since (held, it would refuse the new devices below).
    const bool scene_resumes = stalled_scene && stalled_scene->Idle();
    if (scene_resumes) {
      stalled_scene.reset();
    }
    free_parked();
    // A GPU reset removed one's device: it goes at once, whatever the set (no new device can be made while it is held:
    // MakeOwnDevice), a new one on this set (a new set usually follows: the shared fence reads all ones, dlaa_bridge::Prepare).
    if (reconstruction->Lost()) {
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] A GPU reset removed Ray Reconstruction's device: a new one.");
      const bool same_set = shared->generation == opened_generation;
      const bool restart_streamline = streamline_used && generation_open && generation_backend == twattila::dlaa_bridge::FG_BACKEND_DLSS && same_set;
      reconstruction = std::make_unique<twattila::dlaa_helper::RayReconstruction12>();
      if (streamline_used) {
        stop_streamline();
      }
      if (restart_streamline) open_generation();
      if (reconstruction_open && same_set) open_reconstruction();
    }
    if (upscaler->Lost()) {
      ReShadeLogMessage(nullptr, 0, "[TW DLAA helper] A GPU reset removed FSR's / XeSS's device: a new one.");
      new_upscaler();
      upscaler_tried = false;
    }
    const bool new_set = shared->generation != opened_generation;
    // A new set's first request: its time to the answer (its opens' lines come first), what the addon's first wait for a
    // set allows 20 s for. Said after the answer, so the line costs the game nothing.
    const bool log_first_answer = new_set && shared->color != 0 && !replayable;
    if (new_set) {
      rt_drain();
      // The addon's new textures and fence, opened; the last set's handles closed, this set's kept (set_handles).
      for (const HANDLE handle : set_handles) CloseHandle(handle);
      set_handles.clear();
      const auto keep = [&](std::initializer_list<uint64_t> handles) {
        for (const uint64_t handle : handles) {
          if (handle != 0) set_handles.push_back(reinterpret_cast<HANDLE>(handle));
        }
      };
      keep({shared->color, shared->depth, shared->motion, shared->output, shared->fence});
      if (version >= 3) keep({shared->diffuse_albedo, shared->specular_albedo, shared->normals, shared->roughness});
      if (version >= 4) keep({shared->rt_vertices, shared->rt_positions, shared->rt_sun, shared->rt_visibility, shared->rt_ambient});
      if (version >= 5) keep({shared->fg_frame, shared->fg_hudless});
      if (version >= 6) keep({shared->rt_atlas});
      if (version >= 7) keep({shared->rt_light, shared->rt_bounce});
      if (version >= 8) keep({shared->rt_reflection, shared->rt_specular});
      if (version >= 9) keep({shared->rt_light_mask});
      if (version >= 11) keep({shared->rt_fog});
      if (version >= 17) keep({shared->rr_hit_distance});
      if (version >= 25) keep({shared->rr_extra[0], shared->rr_extra[1], shared->rr_extra[2]});
      if (version >= 18) keep({shared->rt_fog_lights});
      if (version >= 19) keep({shared->rt_tint});
      if (version >= 32) keep({shared->rt_light_surface, shared->rt_di_light[0], shared->rt_di_light[1]});
      color.Reset();
      depth.Reset();
      motion.Reset();
      output.Reset();
      fence.Reset();
      super_resolution_opened = false;
      upscaler->Close();
      upscaler_tried = false;
      upscaler_open = false;
      // The guides serve Ray Reconstruction and the paths.
      reconstruction_open = false;
      if (version >= 3 && shared->diffuse_albedo != 0 && !reconstruction_idle && !other_upscaler()) {
        open_reconstruction();
      } else {
        reconstruction->Close();
      }
      if (scene->Lost()) replace_lost_scene();
      scene_open = false;
      if (version >= 4 && shared->rt_vertices != 0 && !scene_idle) {
        open_scene();
        if (!scene_open) strncpy_s(shared->rt_status, scene_status().c_str(), _TRUNCATE);
      } else {
        scene->Close();
      }
      generation_open = false;
      if (version >= 5 && shared->fg_frame != 0) {
        open_generation();
      } else {
        generation->Close();
        proxy_generation->Close();
      }
      opened_generation = shared->generation;
      if (shared->color == 0) {
        // Another anti-aliasing took over (dlaa_bridge::Release): no request follows, so DLSS's feature goes now and the
        // flush lets D3D11 destroy what the closed textures held.
        backend.Reset();
        super_resolution_ran = false;
        context->ClearState();
        context->Flush();
        if (ComPtr<IDXGIDevice3> dxgi_device; SUCCEEDED(device.As(&dxgi_device))) dxgi_device->Trim();
        set_status("DLSS off");
      } else if (FAILED(device5->OpenSharedFence(reinterpret_cast<HANDLE>(shared->fence), IID_PPV_ARGS(fence.GetAddressOf())))) {
        set_status("DLAA helper: the shared textures could not be opened");
        fence.Reset();
      }
    }
    if (scene_resumes) {
      if (!scene_open && version >= 4 && shared->rt_vertices != 0 && !scene_idle) {
        open_scene();
      }
      // What the addon captured meanwhile reached no scene: again.
      if (version >= 15) ++shared->rt_device;
      if (!scene_off()) {
        ReShadeLogMessage(nullptr, 0, "[TW ray tracing] Started again: the GPU finished its last requests.");
      }
    }
    publish_fence();
    // A GPU reset removed frame generation's device: a new one on this set (until then requests pass without it).
    if (generation_open && generation_lost()) open_generation();
    const auto now = std::chrono::steady_clock::now();
    // A device refused while another's removed one is held (frame generation's: MakeOwnDevice): asked again each second,
    // a next set may not come (until then requests pass without them).
    if (now - refused_tried > std::chrono::seconds(1)) {
      refused_tried = now;
      if (other_upscaler()) {
        upscaler_tried = upscaler_tried && upscaler_open;
      } else if (shared->ray_reconstruction != 0 && !reconstruction_open && !reconstruction_idle) {
        open_reconstruction();
      }
    }
    if (shared->rt_request != 0) {
      rt_used = now;
      if (scene_idle) {
        scene_idle = false;
        open_scene();
      }
    }
    if (shared->ray_reconstruction != 0) {
      rr_used = now;
      reconstruction_live = true;
      if (reconstruction_idle) {
        reconstruction_idle = false;
        open_reconstruction();
      }
    }
    if (shared->fg_frame != 0) {
      fg_used = now;
      generation_resources = true;
    }
    release_idle(last_request);
    bool evaluated = false;
    bool answered = false;
    // The upscaler's request when it changes (one line, never a frame's): which one, its preset by NVIDIA's number (11 K, 12 L,
    // 13 M; Ray Reconstruction's own), the sizes. A run's log then says what Auto chose.
    if (fence && shared->color != 0 && !(version >= 5 && shared->fg_request != 0) && !(version >= 4 && shared->rt_request != 0)) {
      const bool reconstructs = shared->ray_reconstruction != 0;
      const std::array<uint32_t, 7> asked = {(version >= 22 ? shared->upscaler : 0u), (reconstructs ? 1u : 0u),
                                             (reconstructs ? shared->ray_reconstruction_preset : shared->preset),
                                             shared->width, shared->height, shared->output_width, shared->output_height};
      if (asked != upscale_said) {
        upscale_said = asked;
        LogWithTime(std::format("[TW DLAA helper] Upscaling: {}, preset {}, {} x {} to {} x {}.",
                                (other_upscaler() ? std::format("upscaler {}", asked[0]) : std::string(reconstructs ? "Ray Reconstruction" : "DLSS")),
                                asked[2], asked[3], asked[4], asked[5], asked[6])
                        .c_str());
      }
    }
    if (fence && version >= 5 && shared->fg_request != 0) {
      // Frame generation (D3D12): its copy queue waits for the addon's value and signals the next.
      const bool proxied = generation_backend != twattila::dlaa_bridge::FG_BACKEND_DLSS;
      if (generation_open) {
        evaluated = proxied ? proxy_generation->Present(shared, metres_per_unit) : generation->Present(shared);
      } else {
        context4->Wait(fence.Get(), shared->value);
        context4->Signal(fence.Get(), shared->value + 1);
        context->Flush();
      }
      strncpy_s(shared->fg_status, generation_status(), _TRUNCATE);
      if (version >= 13) shared->fg_presented = generation_presented();
      // A backend gave up on this GPU or driver (a GPU reset is not that: a new device, above): Auto's next, or the addon stops asking.
      if (generation_open && !generation_lost() && (proxied ? proxy_generation->Failed() : generation->Failed())) {
        ReShadeLogMessage(nullptr, 0, generation_status());
        generation_refused[generation_backend] = true;
        open_generation();
      }
    } else if (fence && version >= 4 && shared->rt_request != 0) {
      // Ray tracing (D3D12): its queue waits for the addon's value and signals the next (v7: request 2 the Light Bounce,
      // v8: request 3 the reflections).
      // A kind that ran last time goes to the worker and is answered at once: the addon's GPU then waits for its value
      // before the addon signals the next (the fence's order kept). Otherwise it runs here, after the worker's.
      // A GPU fault or hang removed its device (ATN 2026-10-28: a freeze, then no ray tracing until the game restarted): a
      // new device on this set's textures after the first (the addon makes a new set at once: the fence the lost device
      // shared reads all ones, dlaa_bridge::Prepare).
      if (scene_open && scene->Lost()) {
        replace_lost_scene();
        open_scene();
      }
      // After a GPU reset the paused Experimental options stay off (the addon writes them again each request).
      if (scene_losses != 0) {
        shared->rt_flags &= ~PAUSED_TRACE_FLAGS;
        if (version >= 8) shared->rt_path_flags &= ~PAUSED_PATH_FLAGS;
        if (version >= 18) shared->rt_sway_range = 0.f;
        if (version >= 31) shared->rt_grass_range = 0.f;
      }
      bool queued = false;
      if (scene_open) {
        const std::unique_lock lock(rt_work.mutex);
        if (rt_work.ran[kind(*shared)] && !replayable && !serial_requests) {
          auto copy = std::make_unique<twattila::dlaa_bridge::Shared>();
          std::memcpy(copy.get(), shared, twattila::dlaa_bridge::SharedBytes(version));
          const auto sets = new_sets(*shared);
          rt_work.pending.push_back({.shared = std::move(copy), .sets = {sets.begin(), sets.end()}});
          queued = true;
        }
      }
      if (queued) {
        rt_work.changed.notify_all();
        shared->evaluated = 1;
        shared->done_value = shared->value + 1;
        SetEvent(done);
        answered = true;
      } else {
        rt_drain();
        // Replayable: the scene opened no shared fence, so the addon's value is waited for here, and its next signalled
        // below once the scene's work is done (Trace and Paths wait for it then).
        if (replayable && scene_open) {
          twattila::dlaa_helper::WaitForGpu(fence.Get(), replay_wait, shared->value, 5000);
        }
        evaluated = scene_open && run(*shared, new_sets(*shared));
        if (scene_open) {
          const std::unique_lock lock(rt_work.mutex);
          rt_work.ran[kind(*shared)] = evaluated;
        } else {
          context4->Wait(fence.Get(), shared->value);
        }
        if (!scene_open || replayable) {
          context4->Signal(fence.Get(), shared->value + 1);
          context->Flush();
        }
        publish();
      }
    } else if (fence && other_upscaler()) {
      // FSR or XeSS (D3D12): its queue waits for the addon's value and signals the next.
      upscale_used = now;
      if (!upscaler_tried) {
        upscaler_tried = true;
        if (upscaler->Backend() != shared->upscaler) new_upscaler();
        upscaler_live = true;
        upscaler_open = upscaler->Initialize(adapter.Get(), folder, shared->upscaler)
                        && upscaler->Open({shared->color, shared->depth, shared->motion, shared->output}, shared->fence);
      }
      if (std::exchange(super_resolution_ran, false)) backend.Reset();
      reconstruction->ReleaseFeature();
      twattila::dlaa_helper::Request upscale = {
          .value = shared->value,
          .width = shared->width,
          .height = shared->height,
          .output_width = shared->output_width,
          .output_height = shared->output_height,
          .jitter_x = shared->jitter_x,
          .jitter_y = shared->jitter_y,
          .reset = (shared->reset != 0),
          .sharpness = shared->fsr_sharpness,
          .metres_per_unit = metres_per_unit,
      };
      std::copy_n(shared->view_to_clip, 16, upscale.view_to_clip.begin());
      evaluated = upscaler_open && upscaler->Evaluate(upscale);
      if (!upscaler_open) {
        context4->Wait(fence.Get(), shared->value);
        context4->Signal(fence.Get(), shared->value + 1);
        context->Flush();
      }
      set_status(upscaler->Status().c_str());
    } else if (fence && reconstruction_open && shared->ray_reconstruction != 0) {
      // Ray Reconstruction (D3D12): its queue waits for the addon's value and signals the next.
      twattila::dlaa_helper::Request reconstruct = {
          .value = shared->value,
          .width = shared->width,
          .height = shared->height,
          .output_width = shared->output_width,
          .output_height = shared->output_height,
          .jitter_x = shared->jitter_x,
          .jitter_y = shared->jitter_y,
          .reset = (shared->reset != 0),
          .preset = shared->ray_reconstruction_preset,
          .hit_distance = version >= 17 && shared->rr_hit_distance_written != 0,
          .extra_inputs = (version >= 25) ? shared->rr_extra_written : 0u,
      };
      std::copy_n(shared->world_to_view, 16, reconstruct.world_to_view.begin());
      std::copy_n(shared->view_to_clip, 16, reconstruct.view_to_clip.begin());
      if (std::exchange(super_resolution_ran, false)) backend.Reset();
      evaluated = reconstruction->Evaluate(reconstruct);
      set_status(reconstruction->Status().c_str());
      if (version >= 25) shared->rr_refused = 0;
    } else if (fence) {
      reconstruction->ReleaseFeature();
      super_resolution_ran = true;
      // Asked for Ray Reconstruction that couldn't start: DLSS instead, and the addon told (its rays then denoise without RR).
      if (version >= 25) shared->rr_refused = (shared->ray_reconstruction != 0) ? 1u : 0u;
      const auto open = [&](uint64_t handle, ComPtr<ID3D11Texture2D>* texture) {
        return SUCCEEDED(device1->OpenSharedResource1(reinterpret_cast<HANDLE>(handle), IID_PPV_ARGS(texture->GetAddressOf())));
      };
      if (!std::exchange(super_resolution_opened, true)
          && !(open(shared->color, &color) && open(shared->depth, &depth) && open(shared->motion, &motion) && open(shared->output, &output))) {
        output.Reset();
      }
      context4->Wait(fence.Get(), shared->value);
      if (output) {
        // src: DLSS Programming Guide (31 March 2026) 3.9, p. 25: the renderer's exposure as a 1x1 texture, "Only
        // supported by Presets J and K"; 3.10, p. 27: that parameter "is the preferred method", AutoExposure its
        // stand-in. So the addon's exposure (0: none), AutoExposure only without one.
        const int auto_exposure = (shared->dlss_exposure > 0.f ? 0 : NVSDK_NGX_DLSS_Feature_Flags_AutoExposure);
        evaluated = backend.Evaluate(context.Get(), {
                                                        .color = color.Get(),
                                                        .depth = depth.Get(),
                                                        .motion_vectors = motion.Get(),
                                                        .output = output.Get(),
                                                        .width = shared->width,
                                                        .height = shared->height,
                                                        .jitter_x = shared->jitter_x,
                                                        .jitter_y = shared->jitter_y,
                                                        .exposure = shared->dlss_exposure,
                                                        .reset = (shared->reset != 0),
                                                        .output_width = shared->output_width,
                                                        .output_height = shared->output_height,
                                                        .mode = (shared->output_width > shared->width
                                                                     ? talesofarise::dlss::Mode::DLSS_CUSTOM
                                                                     : talesofarise::dlss::Mode::DLAA),
                                                        .preset = shared->preset,
                                                        .create_flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR
                                                                        | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes
                                                                        | auto_exposure,
                                                    });
        set_status(backend.Status());
      } else {
        set_status("DLAA helper: the shared textures could not be opened");
      }
      context4->Signal(fence.Get(), shared->value + 1);
      context->Flush();
    }
    if (!answered) {
      shared->evaluated = (evaluated ? 1 : 0);
      // The fence value it signalled (none without a fence).
      shared->done_value = (fence ? shared->value + 1 : shared->value);
      SetEvent(done);
    }
    if (log_first_answer) {
      LogWithTime(std::format("[TW DLAA helper] A new set's first request answered in {} ms.", ms_since(last_request)).c_str());
    }
  }
  // WAIT_OBJECT_0 + 1: the game ended. WAIT_OBJECT_0: left above with its line.
  if (woken != WAIT_OBJECT_0 && woken != WAIT_OBJECT_0 + 1) {
    LogWithTime(std::format("[TW DLAA helper] The request wait failed (0x{:X}, error {}): the helper ends.", woken, GetLastError()).c_str());
    exit_code = twattila::dlaa_bridge::HELPER_EXIT_REQUEST_WAIT;
  }
  {
    const std::unique_lock lock(rt_work.mutex);
    rt_work.stop = true;
  }
  rt_work.changed.notify_all();
  rt_worker.join();
  wait_scene_release();
  // The game and its window are gone: nothing to wait for on FSR's or XeSS's swap chain, nor for a parked one's making.
  proxy_generation.release();
  for (auto& parked : parked_upscalers) {
    parked.release();
  }
  for (auto& parked : parked_generations) {
    parked.release();
  }
  // Their work can only finish with the game's GPU, which is gone: no wait for it.
  stalled_scene.release();
  backend.Abandon();
  return static_cast<int>(exit_code);
}
