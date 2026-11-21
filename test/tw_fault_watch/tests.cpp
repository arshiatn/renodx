// The Total War add-ons' fault and abort lines (twattila/device_loss.hpp: OnException, OnAbort, LogStall's exception
// line) without a device or a window: which exceptions the vectored handler logs (a hardware fault in this image, once),
// which it only keeps (the frame thread's, anywhere, for the stall line), which it leaves alone (software codes, other
// threads' faults outside this image), and that it always lets the search go on.
#include <windows.h>
#include <csignal>
#include <cstdio>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "src/games/twattila/device_loss.hpp"

namespace loss = twattila::device_loss;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

// output_encoding's preset switch reads the settings rows (none here).
extern "C" __declspec(dllexport) const char* const NAME = "tw_fault_watch";

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}
// Install reads [Total War] StallDump: not set.
extern "C" __declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char*, const char*, char*, size_t*) {
  return false;
}
static size_t Logged(const std::string& part) {
  const std::unique_lock lock(log_mutex);
  size_t count = 0;
  for (const std::string& line : log_lines) {
    if (line.find(part) != std::string::npos) ++count;
  }
  return count;
}
static std::string LineWith(const std::string& part) {
  const std::unique_lock lock(log_mutex);
  for (const std::string& line : log_lines) {
    if (line.find(part) != std::string::npos) return line;
  }
  return {};
}

// The handler as Windows calls it, for a made-up record.
static LONG Filter(DWORD code, uintptr_t address, ULONG_PTR access = 0, ULONG_PTR target = 0) {
  EXCEPTION_RECORD record = {.ExceptionCode = code, .ExceptionAddress = reinterpret_cast<void*>(address), .NumberParameters = 2};
  record.ExceptionInformation[0] = access;
  record.ExceptionInformation[1] = target;
  CONTEXT context = {};
  EXCEPTION_POINTERS pointers = {.ExceptionRecord = &record, .ContextRecord = &context};
  return loss::OnException(&pointers);
}

// A call: clang's __try covers faults in called functions, not in its own instructions.
__declspec(noinline) static void WriteTo(volatile int* address) {
  *address = 1;
}
// A game's frame thread that presents, faults in this image, and whose handler works for a while (a crash reporter).
static DWORD WINAPI FaultingFrameThread(void*) {
  loss::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
  __try {
    WriteTo(nullptr);
  } __except (Sleep(3000), EXCEPTION_EXECUTE_HANDLER) {
  }
  return 0;
}
// A frame thread that presents and then waits.
static DWORD WINAPI WaitingFrameThread(void*) {
  loss::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
  Sleep(1500);
  return 0;
}

int main() {
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  wchar_t exe_path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  const std::wstring exe_wide = wcsrchr(exe_path, L'\\') + 1;
  const std::string exe(exe_wide.begin(), exe_wide.end());

  loss::Install();
  CHECK(loss::fault_handler != nullptr);
  CHECK(loss::own_image_begin == reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) && loss::own_image_end > loss::own_image_begin);
  CHECK(exe == loss::own_image_name.data());
  const uintptr_t inside = loss::own_image_begin + 0x1000;
  const auto outside = reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtClose"));
  CHECK(outside != 0);

  // 1. Software codes in this image, on the frame thread: not logged, not kept, the search goes on.
  loss::present_thread = GetCurrentThreadId();
  for (const DWORD code : {0xE06D7363ul, 0x406D1388ul, static_cast<DWORD>(EXCEPTION_BREAKPOINT), static_cast<DWORD>(EXCEPTION_SINGLE_STEP),
                           static_cast<DWORD>(EXCEPTION_GUARD_PAGE), 0xC0000409ul}) {
    CHECK(Filter(code, inside) == EXCEPTION_CONTINUE_SEARCH);
  }
  try {
    throw std::runtime_error("a C++ exception thrown and caught");
  } catch (const std::exception&) {
  }
  CHECK(Logged("Fault 0x") == 0 && loss::frame_fault_tick == 0);

  // 2. A fault another module handles itself (KernelBase's guarded read, as a game's own probe): on the frame thread kept
  // for the stall line, never logged.
  void* reserved = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
  CHECK(reserved != nullptr);
  CHECK(IsBadReadPtr(reserved, 4) != FALSE);
  CHECK(Logged("Fault 0x") == 0);
  CHECK(loss::frame_fault_code == EXCEPTION_ACCESS_VIOLATION && loss::frame_fault_tick != 0);
  CHECK(loss::frame_fault_address - loss::own_image_begin >= loss::own_image_end - loss::own_image_begin);
  // Not the frame thread's, outside this image: neither.
  loss::present_thread = 0;
  loss::frame_fault_tick = 0;
  loss::frame_fault_code = 0;
  for (const DWORD code : {static_cast<DWORD>(EXCEPTION_ACCESS_VIOLATION), static_cast<DWORD>(EXCEPTION_ILLEGAL_INSTRUCTION),
                           static_cast<DWORD>(EXCEPTION_INT_DIVIDE_BY_ZERO), static_cast<DWORD>(EXCEPTION_STACK_OVERFLOW)}) {
    CHECK(Filter(code, outside) == EXCEPTION_CONTINUE_SEARCH);
  }
  CHECK(IsBadReadPtr(reserved, 4) != FALSE);
  CHECK(Logged("Fault 0x") == 0 && loss::frame_fault_tick == 0 && loss::frame_fault_code == 0);
  VirtualFree(reserved, 0, MEM_RELEASE);

  // 3. A real fault in this image on the frame thread, its handler working: one line naming this image, the thread and
  // the write, and the stall line names it on every build (once).
  DWORD faulting_thread_id = 0;
  const HANDLE frame_thread = CreateThread(nullptr, 0, FaultingFrameThread, nullptr, 0, &faulting_thread_id);
  CHECK(frame_thread != nullptr);
  Sleep(1000);
  CHECK(Logged("Fault 0xC0000005 at " + exe + "+0x") == 1);
  const std::string fault = LineWith("Fault 0xC0000005");
  std::printf("%s\n", fault.c_str());
  CHECK(fault.find(std::format("(thread {}, the frame thread; write at 0x0).", faulting_thread_id)) != std::string::npos);
  loss::LogStall(1000, false);
  const std::string stall = LineWith("The frame thread had an exception after its last frame: 0xC0000005 at " + exe + "+0x");
  CHECK(!stall.empty());
  std::printf("%s\n", stall.c_str());
  CHECK(Logged("No frame for 1 s") == 1);
  loss::LogStall(2000, false);
  CHECK(Logged("No frame for 2 s") == 1 && Logged("The frame thread had an exception after its last frame") == 1);
  // The note an add-on adds to the stall line.
  loss::stall_note = [] { return std::string("worker pool: a test note"); };
  loss::LogStall(3000, false);
  loss::stall_note = nullptr;
  CHECK(LineWith("No frame for 3 s").ends_with("; worker pool: a test note"));
  CHECK(WaitForSingleObject(frame_thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(frame_thread);
  // A fault before the frame thread's last present: not named.
  DWORD waiting_thread_id = 0;
  const HANDLE waiting_thread = CreateThread(nullptr, 0, WaitingFrameThread, nullptr, 0, &waiting_thread_id);
  CHECK(waiting_thread != nullptr);
  Sleep(200);
  loss::frame_fault_said = 0;
  loss::frame_fault_tick = 1;
  loss::LogStall(4000, false);
  CHECK(Logged("No frame for 4 s") == 1 && Logged("The frame thread had an exception after its last frame") == 1);
  CHECK(WaitForSingleObject(waiting_thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(waiting_thread);

  // 4. Once: a second fault in this image logs nothing more, and the search still goes on.
  CHECK(Filter(EXCEPTION_ACCESS_VIOLATION, inside, 8, inside) == EXCEPTION_CONTINUE_SEARCH);
  CHECK(Logged("Fault 0x") == 1);

  // 5. This image's abort (std::terminate's default ends there): one line, then abort goes on (here raise returns).
  std::raise(SIGABRT);
  CHECK(Logged("The add-on's code called abort") == 1);

  // 6. Uninstall: the handler and the abort handler go.
  loss::Uninstall();
  CHECK(loss::fault_handler == nullptr);
  CHECK(std::signal(SIGABRT, SIG_DFL) == SIG_DFL);
  std::printf("PASS\n");
  return 0;
}
