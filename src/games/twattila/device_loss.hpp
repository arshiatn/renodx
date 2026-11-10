/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <minidumpapiset.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <format>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../mods/swapchain.hpp"
#include "../../utils/log.hpp"
#include "./output_encoding.hpp"

// A Total War game after a GPU reset. It builds a new device and swap chain for the same window, in one of two orders:
// - The old device alive (WH3, ATN's log 2026-11-05): a chain whose back buffer's view is bound or held keeps the
//   window, CreateSwapChain answers 0x80070005. The game then asks for a blt chain, which would work, but the swapchain
//   mod turned that into flip too, and the game hung (WARP: test/tw_device_loss).
// - The old device destroyed first (Attila, a player's log 2026-11-03): ReShade unloads the addon with the game's last
//   device and loads it again with the new one, so only the destroy's part runs in the old image.
// Nothing here changes a frame without a removed device.
namespace twattila::device_loss {

// A test shortens it.
static uint64_t no_frame_limit_ms = 90000;
// A frame that does not come is sampled at 20, 40 and 60 s: one sample cannot tell a hang from a long job, nor whether
// the frame thread ever left the code it was first seen in.
static uint64_t stall_log_ms = 20000;  // a test shortens it
constexpr uint32_t STALL_SAMPLES = 3;
// A stall's first sample can also write every thread's place to a file in the game's folder, the last stall's over the
// one before. Only once a device was lost in this process, or where ReShade.ini asks ([Total War] StallDump=1): a long
// load is a stall too, and a game that only loads gets the one line.
constexpr uint32_t STALL_DUMPS = 3;
#if defined(_WIN64)
constexpr size_t STALL_DUMP_STACK_BYTES = 256 * 1024;
constexpr size_t STALL_DUMP_BYTES = 32 * 1024 * 1024;
#else
// A 32-bit game can be near the end of its address space.
constexpr size_t STALL_DUMP_STACK_BYTES = 64 * 1024;
constexpr size_t STALL_DUMP_BYTES = 8 * 1024 * 1024;
#endif
static bool stall_dump_always = false;
// Set in the process's environment at a lost device: the image that comes with the game's next device (Attila's order)
// is another one.
constexpr const wchar_t* DEVICE_LOST_MARK = L"RENODX_TW_DEVICE_LOST";
constexpr DWORD WATCH_POLL_MS = 1000;
constexpr DWORD WATCH_STOP_MS = 2000;

// Not owned: from init_device to destroy_device. A single-threaded device (Attila, Rome II, Thrones, Shogun 2) is only
// asked on the thread that presents.
struct TrackedDevice {
  ID3D11Device* device = nullptr;
  bool single_threaded = false;
};

static std::shared_mutex devices_mutex;
static std::vector<TrackedDevice> devices;
static std::vector<ID3D11Device*> removed_logged;  // under devices_mutex
static std::atomic_bool removal_check_wanted = false;
static std::atomic_bool presented = false;
static std::atomic_bool awaiting_frame = false;  // a device was lost and no frame has come since
static bool after_loss = false;
static bool resizing = false;  // the next create_swapchain is a ResizeBuffers
static bool keep_game_desc = false;
static reshade::api::swapchain_desc game_desc = {};
static HWND native_flip_window = nullptr;  // the game itself asked for a flip chain for it
static std::atomic<HWND> game_window = nullptr;
static std::atomic_uint64_t last_present_tick = 0;
static std::atomic_uint32_t present_thread = 0;
static HANDLE watch_thread = nullptr;
static HANDLE watch_stop = nullptr;  // that thread's own: a watch that could not be waited for keeps its event

static bool GameInFront() {
  DWORD process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &process);
  return process == GetCurrentProcessId();
}

// 0x887A0006: hung, the game's own work (ours within it). 0x887A0007: reset, another's work. devices_mutex held.
static void LogRemoved(ID3D11Device* device, HRESULT reason) {
  if (std::ranges::find(removed_logged, device) != removed_logged.end()) return;
  removed_logged.push_back(device);
  SetEnvironmentVariableW(DEVICE_LOST_MARK, L"1");
  renodx::utils::log::w(std::format("[Total War] GPU reset: the game's device removed (0x{:08X}), last frame {} ms ago, the game's window {}.",
                                    static_cast<uint32_t>(reason), GetTickCount64() - last_present_tick.load(std::memory_order_relaxed),
                                    GameInFront() ? "in front" : "not in front"));
}

// A removed device lets go of its swap chain's window: its context's bindings and D3D11's late destruction keep it.
static void DropBindings(ID3D11Device* device) {
  ID3D11DeviceContext* context = nullptr;
  device->GetImmediateContext(&context);
  context->ClearState();
  context->Flush();
  context->Release();
}

// Where the frame thread stands when frames stopped: its instruction and the code addresses on its stack, nearest first (no
// unwinding, so some are stale; nothing that can take a lock while it is suspended). With `dump`, every thread's
// registers and stack go to renodx-stall.dmp beside the game's exe, for a debugger.
static void LogStall(uint64_t age_ms, bool dump) {
  const DWORD frame_thread = present_thread.load(std::memory_order_relaxed);
  const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, frame_thread);
  if (thread == nullptr) return;
  static std::array<uintptr_t, 4096> stack;
  SIZE_T stack_bytes = 0;
  CONTEXT context = {.ContextFlags = CONTEXT_CONTROL};
  SuspendThread(thread);
  const bool stopped = GetThreadContext(thread, &context) != FALSE;
#if defined(_WIN64)
  const uintptr_t instruction = context.Rip;
  const uintptr_t stack_top = context.Rsp;
#else
  const uintptr_t instruction = context.Eip;
  const uintptr_t stack_top = context.Esp;
#endif
  if (stopped) {
    // A stack shorter than the array is read to its end.
    MEMORY_BASIC_INFORMATION pages = {};
    if (VirtualQuery(reinterpret_cast<const void*>(stack_top), &pages, sizeof(pages)) != 0) {
      const size_t wanted = std::min<size_t>(sizeof(stack), reinterpret_cast<uintptr_t>(pages.BaseAddress) + pages.RegionSize - stack_top);
      ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(stack_top), stack.data(), wanted, &stack_bytes);
    }
  }
  ResumeThread(thread);
  CloseHandle(thread);
  if (!stopped) return;
  // "module+0xoffset" for an address in a module's code, else empty.
  const auto code_place = [](uintptr_t address) {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &module) == FALSE) return std::string();
    MEMORY_BASIC_INFORMATION page = {};
    constexpr DWORD EXECUTABLE = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (VirtualQuery(reinterpret_cast<const void*>(address), &page, sizeof(page)) == 0 || (page.Protect & EXECUTABLE) == 0) return std::string();
    std::array<char, MAX_PATH> path = {};
    GetModuleFileNameA(module, path.data(), MAX_PATH);
    const std::string file = path.data();
    return std::format("{}+0x{:X}", file.substr(file.find_last_of('\\') + 1), address - reinterpret_cast<uintptr_t>(module));
  };
  std::string line = std::format("[Total War] No frame for {} s, the game's window {}, the frame thread at: {}", age_ms / 1000,
                                 GameInFront() ? "in front" : "not in front", code_place(instruction));
  int added = 0;
  for (size_t word = 0; word < stack_bytes / sizeof(uintptr_t) && added < 32; ++word) {
    if (const std::string place = code_place(stack[word]); !place.empty()) {
      line += " " + place;
      ++added;
    }
  }
  renodx::utils::log::w(line);
#if defined(_WIN64)
  // A fault's thread is inside ntdll's exception dispatcher, with the fault's registers right above that call and its
  // record 0x4F0 further (Windows 10 and 11). A game's own crash handler can work there for minutes: ATN's "hang" of
  // 2026-11-06 was Warhammer III compressing its crash report.
  const auto dispatcher = reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "KiUserExceptionDispatcher"));
  constexpr size_t RECORD_OFFSET = 0x4F0;
  for (size_t word = 0; (word + 1) * sizeof(uintptr_t) + RECORD_OFFSET + sizeof(EXCEPTION_RECORD) <= stack_bytes; ++word) {
    if (dispatcher == 0 || stack[word] - dispatcher >= 0x80) continue;
    CONTEXT fault = {};
    EXCEPTION_RECORD record = {};
    std::memcpy(&fault, &stack[word + 1], sizeof(fault));
    std::memcpy(&record, reinterpret_cast<const uint8_t*>(&stack[word + 1]) + RECORD_OFFSET, sizeof(record));
    if ((fault.ContextFlags & ~0xFFul) != CONTEXT_AMD64 || record.NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS) continue;
    const auto fault_address = reinterpret_cast<uintptr_t>(record.ExceptionAddress);
    const std::string place = code_place(fault_address);
    renodx::utils::log::w(std::format(
        "[Total War] The frame thread is handling an exception: 0x{:08X} at {} (a crash, not a wait: the game's crash handler or a debugger has it; the fault's registers are at 0x{:X} on that thread's stack).",
        static_cast<uint32_t>(record.ExceptionCode), place.empty() ? std::format("0x{:X}", fault_address) : place,
        stack_top + (word + 1) * sizeof(uintptr_t)));
    break;
  }
#endif
  if (!dump) return;

  // The dump is written by hand. MiniDumpWriteDump stops every other thread of its own process and then allocates and
  // loads: with one of them stopped inside the heap or the loader it never returns, and a long load would become a
  // hang. Here one thread at a time is stopped, for its registers and a copy of its stack into memory made before, and
  // each stack goes to the file once its thread runs again: the dump never holds more than one of them in memory.
  std::vector<DWORD> thread_ids = {frame_thread};
  if (const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snapshot != INVALID_HANDLE_VALUE) {
    THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
    for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
      if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId() && entry.th32ThreadID != frame_thread) {
        thread_ids.push_back(entry.th32ThreadID);
      }
    }
    CloseHandle(snapshot);
  }
  struct StoppedThread {
    DWORD id = 0;
    uintptr_t environment = 0;  // its TEB
    CONTEXT context = {};
    uintptr_t stack_start = 0;
    RVA stack_offset = 0;  // in the file
    size_t stack_size = 0;
  };
  std::vector<StoppedThread> threads;
  threads.reserve(thread_ids.size());

  // The modules' places, read as another process would read them (no loader lock).
  struct LoadedModule {
    MINIDUMP_MODULE module = {};
    std::wstring path;
  };
  std::vector<LoadedModule> modules;
  static std::array<HMODULE, 2048> handles;
  DWORD handle_bytes = 0;
#if !defined(_WIN64)
  // A 32-bit game on 64-bit Windows: its system modules are named in System32 and come from SysWOW64, where a
  // debugger has to look.
  std::array<wchar_t, MAX_PATH> system_folder = {};
  std::array<wchar_t, MAX_PATH> loaded_from_folder = {};
  const size_t system_folder_length = GetSystemDirectoryW(system_folder.data(), MAX_PATH);
  const bool redirected = GetSystemWow64DirectoryW(loaded_from_folder.data(), MAX_PATH) != 0;
#endif
  if (K32EnumProcessModules(GetCurrentProcess(), handles.data(), sizeof(handles), &handle_bytes) != FALSE) {
    for (size_t index = 0; index < std::min<size_t>(handle_bytes / sizeof(HMODULE), handles.size()); ++index) {
      MODULEINFO info = {};
      std::array<wchar_t, MAX_PATH> path = {};
      if (K32GetModuleInformation(GetCurrentProcess(), handles[index], &info, sizeof(info)) == FALSE) continue;
      K32GetModuleFileNameExW(GetCurrentProcess(), handles[index], path.data(), MAX_PATH);
      LoadedModule loaded = {.path = path.data()};
#if !defined(_WIN64)
      if (redirected && _wcsnicmp(loaded.path.c_str(), system_folder.data(), system_folder_length) == 0 && loaded.path[system_folder_length] == L'\\') {
        loaded.path = loaded_from_folder.data() + loaded.path.substr(system_folder_length);
      }
#endif
      loaded.module.BaseOfImage = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
      loaded.module.SizeOfImage = info.SizeOfImage;
      // What a debugger matches the file on disk by.
      IMAGE_DOS_HEADER dos = {};
      IMAGE_NT_HEADERS headers = {};
      if (ReadProcessMemory(GetCurrentProcess(), info.lpBaseOfDll, &dos, sizeof(dos), nullptr) != FALSE && dos.e_magic == IMAGE_DOS_SIGNATURE
          && ReadProcessMemory(GetCurrentProcess(), static_cast<const uint8_t*>(info.lpBaseOfDll) + dos.e_lfanew, &headers, sizeof(headers), nullptr) != FALSE
          && headers.Signature == IMAGE_NT_SIGNATURE) {
        loaded.module.TimeDateStamp = headers.FileHeader.TimeDateStamp;
        loaded.module.CheckSum = headers.OptionalHeader.CheckSum;
      }
      modules.push_back(std::move(loaded));
    }
  }

  // The file: header, directory (filled in last), the stacks as they are read, then each stream's data where its entry
  // points.
  std::array<wchar_t, MAX_PATH> exe = {};
  GetModuleFileNameW(nullptr, exe.data(), MAX_PATH);
  std::wstring path = exe.data();
  path = path.substr(0, path.find_last_of(L'\\') + 1) + L"renodx-stall.dmp";
  const HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) {
    renodx::utils::log::w("[Total War] The stalled threads' places could not be written to renodx-stall.dmp beside the game's exe.");
    return;
  }
  // Closed on every way out, a thrown one (no memory) too.
  const std::unique_ptr<void, BOOL(WINAPI*)(HANDLE)> output_owner(output, CloseHandle);
  constexpr ULONG32 STREAMS = 5;
  const MINIDUMP_HEADER header = {.Signature = MINIDUMP_SIGNATURE,
                                  .Version = MINIDUMP_VERSION,
                                  .NumberOfStreams = STREAMS,
                                  .StreamDirectoryRva = sizeof(MINIDUMP_HEADER),
                                  .TimeDateStamp = static_cast<ULONG32>(std::time(nullptr))};
  std::array<MINIDUMP_DIRECTORY, STREAMS> directory = {};
  DWORD written = 0;
  bool saved = WriteFile(output, &header, sizeof(header), &written, nullptr) != FALSE
               && WriteFile(output, directory.data(), sizeof(directory), &written, nullptr) != FALSE;
  constexpr RVA STACKS_START = sizeof(header) + sizeof(directory);
  RVA file_end = STACKS_START;

  static std::array<uint8_t, STALL_DUMP_STACK_BYTES> one_stack;
  // ntdll's THREAD_BASIC_INFORMATION (class 0): a debugger wants each thread's environment block.
  struct ThreadBasics {
    LONG exit_status;
    void* environment;
    void* process_id;
    void* thread_id;
    uintptr_t affinity;
    LONG priority;
    LONG base_priority;
  };
  using QueryThread = LONG(NTAPI*)(HANDLE, int, void*, ULONG, ULONG*);
  const auto query_thread = reinterpret_cast<QueryThread>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
  for (const DWORD id : thread_ids) {
    const HANDLE other = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
    if (other == nullptr) continue;
    StoppedThread stopped_thread = {.id = id};
    if (ThreadBasics basics = {}; query_thread != nullptr && query_thread(other, 0, &basics, sizeof(basics), nullptr) >= 0) {
      stopped_thread.environment = reinterpret_cast<uintptr_t>(basics.environment);
    }
    stopped_thread.context.ContextFlags = CONTEXT_FULL;
    SIZE_T copied = 0;
    bool known = false;
    if (SuspendThread(other) != static_cast<DWORD>(-1)) {
      // Until the resume: system calls only (the stopped thread may hold the heap's or the loader's lock).
      known = GetThreadContext(other, &stopped_thread.context) != FALSE;
      if (known && file_end - STACKS_START < STALL_DUMP_BYTES) {
#if defined(_WIN64)
        stopped_thread.stack_start = stopped_thread.context.Rsp;
#else
        stopped_thread.stack_start = stopped_thread.context.Esp;
#endif
        // The stack's committed pages from the stack pointer up.
        uintptr_t end = stopped_thread.stack_start;
        MEMORY_BASIC_INFORMATION pages = {};
        void* allocation = nullptr;
        while (end - stopped_thread.stack_start < one_stack.size() && VirtualQuery(reinterpret_cast<const void*>(end), &pages, sizeof(pages)) != 0
               && pages.State == MEM_COMMIT && (pages.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0
               && (allocation == nullptr || pages.AllocationBase == allocation)) {
          allocation = pages.AllocationBase;
          end = reinterpret_cast<uintptr_t>(pages.BaseAddress) + pages.RegionSize;
        }
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(stopped_thread.stack_start), one_stack.data(),
                          std::min<size_t>(end - stopped_thread.stack_start, one_stack.size()), &copied);
      }
      ResumeThread(other);
    }
    CloseHandle(other);
    if (!known) continue;
    stopped_thread.stack_offset = file_end;
    stopped_thread.stack_size = copied;
    if (copied != 0) {
      saved = saved && WriteFile(output, one_stack.data(), static_cast<DWORD>(copied), &written, nullptr) != FALSE && written == copied;
      file_end += static_cast<RVA>(copied);
    }
    threads.push_back(stopped_thread);
  }

  // What follows the stacks, small: made in memory, its places counted from the stacks' end.
  std::vector<uint8_t> streams;
  const auto append = [&streams, file_end](const void* data, size_t size) {
    const auto offset = file_end + static_cast<RVA>(streams.size());
    streams.insert(streams.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    return offset;
  };
  // A length in bytes, the text, a terminator.
  const auto append_text = [&append](const std::wstring& text) {
    const auto length = static_cast<ULONG32>(text.size() * sizeof(wchar_t));
    const RVA offset = append(&length, sizeof(length));
    append(text.c_str(), length + sizeof(wchar_t));
    return offset;
  };

  SYSTEM_INFO processor = {};
  GetNativeSystemInfo(&processor);
  RTL_OSVERSIONINFOW windows = {.dwOSVersionInfoSize = sizeof(windows)};
  using GetVersion = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
  if (const auto get_version = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")); get_version != nullptr) {
    get_version(&windows);
  }
  MINIDUMP_SYSTEM_INFO system_info = {};
#if defined(_WIN64)
  system_info.ProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
#else
  system_info.ProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
#endif
  system_info.ProcessorLevel = processor.wProcessorLevel;
  system_info.ProcessorRevision = processor.wProcessorRevision;
  system_info.NumberOfProcessors = static_cast<UCHAR>(std::min<DWORD>(processor.dwNumberOfProcessors, 255));
  system_info.ProductType = VER_NT_WORKSTATION;
  system_info.MajorVersion = windows.dwMajorVersion;
  system_info.MinorVersion = windows.dwMinorVersion;
  system_info.BuildNumber = windows.dwBuildNumber;
  system_info.PlatformId = VER_PLATFORM_WIN32_NT;
  system_info.CSDVersionRva = append_text(L"");
  directory[0] = {.StreamType = SystemInfoStream, .Location = {.DataSize = sizeof(system_info), .Rva = append(&system_info, sizeof(system_info))}};

  const MINIDUMP_MISC_INFO process = {.SizeOfInfo = sizeof(MINIDUMP_MISC_INFO), .Flags1 = MINIDUMP_MISC1_PROCESS_ID, .ProcessId = GetCurrentProcessId()};
  directory[1] = {.StreamType = MiscInfoStream, .Location = {.DataSize = sizeof(process), .Rva = append(&process, sizeof(process))}};

  for (LoadedModule& loaded : modules) {
    loaded.module.ModuleNameRva = append_text(loaded.path);
  }
  const auto module_count = static_cast<ULONG32>(modules.size());
  directory[2] = {.StreamType = ModuleListStream,
                  .Location = {.DataSize = static_cast<ULONG32>(sizeof(module_count) + modules.size() * sizeof(MINIDUMP_MODULE)),
                               .Rva = append(&module_count, sizeof(module_count))}};
  for (const LoadedModule& loaded : modules) {
    append(&loaded.module, sizeof(loaded.module));
  }

  // Each thread's registers, then the two lists that point at them and at the stacks.
  std::vector<MINIDUMP_THREAD> thread_list;
  std::vector<MINIDUMP_MEMORY_DESCRIPTOR> memory_list;
  for (const StoppedThread& stopped_thread : threads) {
    const MINIDUMP_MEMORY_DESCRIPTOR stack_memory = {
        .StartOfMemoryRange = stopped_thread.stack_start,
        .Memory = {.DataSize = static_cast<ULONG32>(stopped_thread.stack_size), .Rva = stopped_thread.stack_offset}};
    thread_list.push_back({.ThreadId = stopped_thread.id,
                           .Teb = stopped_thread.environment,
                           .Stack = stack_memory,
                           .ThreadContext = {.DataSize = sizeof(CONTEXT), .Rva = append(&stopped_thread.context, sizeof(CONTEXT))}});
    if (stopped_thread.stack_size != 0) {
      memory_list.push_back(stack_memory);
    }
  }
  const auto thread_count = static_cast<ULONG32>(thread_list.size());
  directory[3] = {.StreamType = ThreadListStream,
                  .Location = {.DataSize = static_cast<ULONG32>(sizeof(thread_count) + thread_list.size() * sizeof(MINIDUMP_THREAD)),
                               .Rva = append(&thread_count, sizeof(thread_count))}};
  append(thread_list.data(), thread_list.size() * sizeof(MINIDUMP_THREAD));
  const auto memory_count = static_cast<ULONG32>(memory_list.size());
  directory[4] = {.StreamType = MemoryListStream,
                  .Location = {.DataSize = static_cast<ULONG32>(sizeof(memory_count) + memory_list.size() * sizeof(MINIDUMP_MEMORY_DESCRIPTOR)),
                               .Rva = append(&memory_count, sizeof(memory_count))}};
  append(memory_list.data(), memory_list.size() * sizeof(MINIDUMP_MEMORY_DESCRIPTOR));

  saved = saved && WriteFile(output, streams.data(), static_cast<DWORD>(streams.size()), &written, nullptr) != FALSE && written == streams.size()
          && SetFilePointer(output, sizeof(MINIDUMP_HEADER), nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER
          && WriteFile(output, directory.data(), sizeof(directory), &written, nullptr) != FALSE && written == sizeof(directory);
  if (saved) {
    renodx::utils::log::w(std::format("[Total War] Every thread's place when the frames stopped ({} threads, {} KB) is in renodx-stall.dmp beside the game's exe.",
                                      threads.size(), (file_end + streams.size()) / 1024));
  } else {
    renodx::utils::log::w("[Total War] The stalled threads' places could not be written to renodx-stall.dmp beside the game's exe.");
  }
}

// Nothing thrown may leave this thread: it is a raw one, and an exception out of it ends the game. A 32-bit game near
// the end of its address space can fail any allocation here (a line's text, the dump's lists).
static DWORD WINAPI WatchLoop(void* stop) {
  bool front_known = false;
  bool last_front = false;
  uint64_t stalled_frame = 0;
  uint32_t stall_samples = 0;
  uint32_t stall_dumps = 0;
  uint64_t awaited_ms = 0;
  while (WaitForSingleObject(stop, WATCH_POLL_MS) == WAIT_TIMEOUT) {
    try {
      {
        const std::unique_lock lock(devices_mutex);
        for (const TrackedDevice& tracked : devices) {
          if (tracked.single_threaded) {
            removal_check_wanted.store(true, std::memory_order_relaxed);
            continue;
          }
          if (const HRESULT reason = tracked.device->GetDeviceRemovedReason(); FAILED(reason)) {
            LogRemoved(tracked.device, reason);
          }
        }
      }
      if (const uint64_t frame = last_present_tick.load(std::memory_order_relaxed); frame != 0) {
        if (const bool front = GameInFront(); !front_known || front != last_front) {
          if (front_known) {
            renodx::utils::log::i("[Total War] The game's window: ", front ? "in front" : "not in front", ".");
          }
          front_known = true;
          last_front = front;
        }
        if (frame != stalled_frame) {
          stalled_frame = frame;
          stall_samples = 0;
        }
        if (const uint64_t age = GetTickCount64() - frame; stall_samples < STALL_SAMPLES && age >= stall_log_ms * (stall_samples + 1)) {
          ++stall_samples;
          // A minimised game may just not be drawing.
          const bool dump = stall_samples == 1 && stall_dumps < STALL_DUMPS
                            && (stall_dump_always || GetEnvironmentVariableW(DEVICE_LOST_MARK, nullptr, 0) != 0)
                            && IsIconic(game_window.load(std::memory_order_relaxed)) == FALSE;
          if (dump) {
            ++stall_dumps;
          }
          LogStall(age, dump);
        }
      }
      if (!awaiting_frame.load(std::memory_order_relaxed)) continue;
      if (presented.load(std::memory_order_relaxed)) {
        awaiting_frame.store(false, std::memory_order_relaxed);
        awaited_ms = 0;
        renodx::utils::log::i("[Total War] GPU reset: the game draws again.");
        continue;
      }
      // A game minimised or behind another program draws nothing and is not hung: only the time its window is in front, or
      // does not answer Windows (its ghost is in front then, not the game), counts.
      const HWND window = game_window.load(std::memory_order_relaxed);
      if (IsHungAppWindow(window) != FALSE || (GameInFront() && IsIconic(window) == FALSE)) {
        awaited_ms += WATCH_POLL_MS;
      }
      if (awaited_ms >= no_frame_limit_ms) {
        renodx::utils::log::e(std::format("[Total War] GPU reset: no frame for {} s after the game's new device, the game is closed.",
                                          no_frame_limit_ms / 1000));
        TerminateProcess(GetCurrentProcess(), 1);
      }
    } catch (...) {
      // No-op: the next poll goes on from here.
    }
  }
  return 0;
}

// Before the addon can go; a device's last destroy or the addon's detach, whichever comes first. The wait is bounded:
// under the loader's lock (the addon's detach) a thread told to stop is out of this code at once but cannot finish
// ending until the detach returns, and waiting for that would hang the game's quit. Such a thread keeps its handles.
static void StopWatch() {
  if (watch_thread == nullptr) return;
  SetEvent(watch_stop);
  if (WaitForSingleObject(watch_thread, WATCH_STOP_MS) == WAIT_OBJECT_0) {
    CloseHandle(watch_thread);
    CloseHandle(watch_stop);
  }
  watch_thread = nullptr;
}

static void OnInitDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  const std::unique_lock lock(devices_mutex);
  devices.push_back({.device = native, .single_threaded = (native->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0});
  if (watch_thread == nullptr) {
    watch_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    watch_thread = CreateThread(nullptr, 0, WatchLoop, watch_stop, 0, nullptr);
  }
}

// The references on the game's device as its destroy begins (the first of the addon's handlers) and once the last of them has
// run: the difference is what the addon held and let go; what is left is the game's, ReShade's and whatever the addon still
// holds. ReShade's own count ("Reference count ... is inconsistent (N)") comes after it unloaded the addon, with no line of
// ours beside it: WH3's quit of 2026-11-10 said 851 and nothing could say whose. One line a device's end.
static ULONG references_at_destroy = 0;

static void BeforeDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  native->AddRef();
  references_at_destroy = native->Release();
}

static void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  native->AddRef();
  renodx::utils::log::i(std::format("[Total War] References on the game's device: {} as its end began, {} after the addon's releases.",
                                    references_at_destroy, native->Release()));
  bool last_device = false;
  {
    const std::unique_lock lock(devices_mutex);
    // Destroyed before the game's new swap chain (Attila's order): the same step as BeforeSwapchainMod's, here.
    if (std::erase_if(devices, [native](const TrackedDevice& tracked) { return tracked.device == native; }) != 0) {
      if (const HRESULT reason = native->GetDeviceRemovedReason(); FAILED(reason)) {
        LogRemoved(native, reason);
        DropBindings(native);
      }
    }
    std::erase(removed_logged, native);
    last_device = devices.empty();
  }
  if (last_device) StopWatch();
}

static void OnDestroySwapchain(reshade::api::swapchain*, bool resize) {
  resizing = resize;
}

static void OnInitSwapchain(reshade::api::swapchain*, bool) {
  resizing = false;
}

// Registered before the swapchain mod: the old chain goes with its context's bindings and a Flush (D3D11 destroys late),
// and a new chain's output is decided.
static bool BeforeSwapchainMod(reshade::api::device_api, reshade::api::swapchain_desc& desc, void* hwnd) {
  auto* window = static_cast<HWND>(hwnd);
  game_window.store(window, std::memory_order_relaxed);
  {
    const std::unique_lock lock(devices_mutex);
    const auto removed = std::ranges::remove_if(devices, [](const TrackedDevice& tracked) {
      const HRESULT reason = tracked.device->GetDeviceRemovedReason();
      if (reason == S_OK) return false;
      LogRemoved(tracked.device, reason);
      DropBindings(tracked.device);
      return true;
    });
    if (!removed.empty()) {
      devices.erase(removed.begin(), removed.end());
      after_loss = true;
      presented.store(false, std::memory_order_relaxed);
      awaiting_frame.store(true, std::memory_order_relaxed);
    }
  }
  if (!std::exchange(resizing, false)) {
    if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD) {
      native_flip_window = window;
    }
    output_encoding::Decide(window);
  }
  // A blt request for a window the game itself gave a flip chain: the game's fallback after 0x80070005. A game that
  // always asks for blt (Attila) has no such request to tell apart, and keeps its upgrade.
  keep_game_desc = after_loss && desc.present_mode == DXGI_SWAP_EFFECT_DISCARD && window == native_flip_window;
  if (keep_game_desc) game_desc = desc;
  return false;
}

// Registered after the swapchain mod: the game's blt fallback stays as the game asked, 8-bit SDR, and the mod treats
// the chain as one it left alone (it would upgrade the views of back buffers that were not upgraded).
static bool AfterSwapchainMod(reshade::api::device_api, reshade::api::swapchain_desc& desc, void*) {
  if (!keep_game_desc) return false;
  keep_game_desc = false;
  desc = game_desc;
  renodx::mods::swapchain::upgraded_swapchain_desc.reset();
  output_encoding::Apply(output_encoding::SDR);
  renodx::utils::log::w("[Total War] GPU reset: the window is still taken, the game's own swap chain is used (SDR until the game restarts).");
  return true;
}

static void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  if (removal_check_wanted.load(std::memory_order_relaxed)) {
    removal_check_wanted.store(false, std::memory_order_relaxed);
    if (auto* device = swapchain->get_device(); device->get_api() == reshade::api::device_api::d3d11) {
      auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
      if (const HRESULT reason = native->GetDeviceRemovedReason(); FAILED(reason)) {
        const std::unique_lock lock(devices_mutex);
        LogRemoved(native, reason);
      }
    }
  }
  presented.store(true, std::memory_order_relaxed);
  last_present_tick.store(GetTickCount64(), std::memory_order_relaxed);
  present_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
}

// Before renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH): create_swapchain callbacks run in registration order.
static void Install() {
  reshade::get_config_value(nullptr, "Total War", "StallDump", stall_dump_always);
  reshade::register_event<reshade::addon_event::create_swapchain>(BeforeSwapchainMod);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(BeforeDestroyDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
}

// At the end of the addon's attach: after the swapchain mod's create_swapchain, and after every other destroy_device
// (a removed device's Flush comes once the addon's own objects on it are released).
static void InstallLast() {
  reshade::register_event<reshade::addon_event::create_swapchain>(AfterSwapchainMod);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
}

static void Uninstall() {
  reshade::unregister_event<reshade::addon_event::create_swapchain>(AfterSwapchainMod);
  reshade::unregister_event<reshade::addon_event::create_swapchain>(BeforeSwapchainMod);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(BeforeDestroyDevice);
  StopWatch();
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  // An image kept loaded after the detach tracks nothing. Not waited for: a watch that Windows ended inside the lock (the
  // process's end) never gives it back, and nothing reads the lists then.
  if (const std::unique_lock lock(devices_mutex, std::try_to_lock); lock.owns_lock()) {
    devices.clear();
    removed_logged.clear();
  }
}

}  // namespace twattila::device_loss
