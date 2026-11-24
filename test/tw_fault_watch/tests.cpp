// The Total War add-ons' fault, unhandled-exception, overflow and abort lines (twattila/device_loss.hpp: OnException,
// OnUnhandled, the frame watch's overflow line, OnAbort, LogStall's exception lines) without a device or a window: which
// exceptions the vectored handler logs (a hardware fault in the add-on's code, up to 4 places), which it only keeps (the
// frame thread's, anywhere, for the stall line), which it leaves alone (software codes, other threads' faults outside its
// code), that it always lets the search go on, and that the top-level filter says an unhandled one once and passes it on.
#include <windows.h>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <format>
#include <mutex>
#include <regex>
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

// A handler as Windows calls it, for a record.
static LONG Call(LONG(WINAPI* handler)(EXCEPTION_POINTERS*), EXCEPTION_RECORD record) {
  CONTEXT context = {};
  EXCEPTION_POINTERS pointers = {.ExceptionRecord = &record, .ContextRecord = &context};
  return handler(&pointers);
}
// The vectored handler for a made-up fault.
static LONG Filter(DWORD code, uintptr_t address, ULONG_PTR access = 0, ULONG_PTR target = 0) {
  EXCEPTION_RECORD record = {.ExceptionCode = code, .ExceptionAddress = reinterpret_cast<void*>(address), .NumberParameters = 2};
  record.ExceptionInformation[0] = access;
  record.ExceptionInformation[1] = target;
  return Call(loss::OnException, record);
}

// The game's own top-level filter (its crash reporter), and one set after ours.
static int game_filter_calls = 0;
static LONG WINAPI GameFilter(EXCEPTION_POINTERS*) {
  ++game_filter_calls;
  return EXCEPTION_EXECUTE_HANDLER;
}
static LONG WINAPI LaterFilter(EXCEPTION_POINTERS*) {
  return EXCEPTION_CONTINUE_SEARCH;
}

// A call: clang's __try covers faults in called functions, not in its own instructions.
__declspec(noinline) static void WriteTo(volatile int* address) {
  *address = 1;
}
__declspec(noinline) static void ThrowFromImage() {
  throw std::runtime_error("thrown by this image");
}
static EXCEPTION_RECORD thrown_record = {};
static int KeepRecord(EXCEPTION_POINTERS* info) {
  thrown_record = *info->ExceptionRecord;
  return EXCEPTION_EXECUTE_HANDLER;
}
static void CatchThrown() {
  __try {
    ThrowFromImage();
  } __except (KeepRecord(GetExceptionInformation())) {
  }
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
// A frame thread that presents, faults, handles it at once and then waits.
static DWORD WINAPI HandledFrameThread(void*) {
  loss::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
  __try {
    WriteTo(nullptr);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  Sleep(2500);
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
  loss::stall_log_ms = 600000;
  SetUnhandledExceptionFilter(GameFilter);

  loss::Install();
  CHECK(loss::fault_handler != nullptr && !loss::unhandled_filter_set);
  CHECK(loss::own_image_begin == reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) && loss::own_image_end > loss::own_image_begin);
  CHECK(exe == loss::own_image_name.data());
  const uintptr_t inside = loss::own_image_begin + 0x1000;
  const auto outside = reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtClose"));
  CHECK(outside != 0);
  const DWORD main_thread = GetCurrentThreadId();

  // 1. Software codes in this image, on the frame thread: not logged, not kept, the search goes on.
  loss::present_thread = main_thread;
  for (const DWORD code : {0xE06D7363ul, 0x406D1388ul, static_cast<DWORD>(EXCEPTION_BREAKPOINT), static_cast<DWORD>(EXCEPTION_SINGLE_STEP),
                           static_cast<DWORD>(EXCEPTION_GUARD_PAGE), 0xC0000409ul}) {
    CHECK(Filter(code, inside) == EXCEPTION_CONTINUE_SEARCH);
  }
  try {
    throw std::runtime_error("a C++ exception thrown and caught");
  } catch (const std::exception&) {
  }
  CHECK(Logged("Fault") == 0 && loss::frame_fault_tick == 0);

  // 2. A fault another module handles itself (KernelBase's guarded read, as a game's own probe): on the frame thread kept
  // for the stall line, never logged; the floating-point and SSE codes kept too.
  void* reserved = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
  CHECK(reserved != nullptr);
  CHECK(IsBadReadPtr(reserved, 4) != FALSE);
  CHECK(Logged("Fault") == 0);
  CHECK(loss::frame_fault_code == EXCEPTION_ACCESS_VIOLATION && loss::frame_fault_tick != 0);
  CHECK(loss::frame_fault_address - loss::own_image_begin >= loss::own_image_end - loss::own_image_begin);
  for (const DWORD code : {0xC000008Cul, 0xC000008Dul, 0xC0000090ul, 0xC0000093ul, 0xC0000095ul, 0xC00002B4ul, 0xC00002B5ul}) {
    CHECK(Filter(code, outside) == EXCEPTION_CONTINUE_SEARCH && loss::frame_fault_code == code);
  }
  // Not the frame thread's, outside this image: neither.
  loss::present_thread = 0;
  loss::frame_fault_tick = 0;
  loss::frame_fault_code = 0;
  for (const DWORD code : {static_cast<DWORD>(EXCEPTION_ACCESS_VIOLATION), static_cast<DWORD>(EXCEPTION_ILLEGAL_INSTRUCTION),
                           static_cast<DWORD>(EXCEPTION_INT_DIVIDE_BY_ZERO), static_cast<DWORD>(EXCEPTION_STACK_OVERFLOW), 0xC0000091ul}) {
    CHECK(Filter(code, outside) == EXCEPTION_CONTINUE_SEARCH);
  }
  CHECK(IsBadReadPtr(reserved, 4) != FALSE);
  CHECK(Logged("Fault") == 0 && loss::frame_fault_tick == 0 && loss::frame_fault_code == 0 && loss::overflow_count == 0);
  VirtualFree(reserved, 0, MEM_RELEASE);

  // 3. A real fault in this image on the frame thread, its handler working: one first-chance line naming this image,
  // the thread and the write; the stall line names the exception it is handling from the dispatcher's record, on every
  // build. The first present put the top-level filter over the game's.
  DWORD faulting_thread_id = 0;
  const HANDLE frame_thread = CreateThread(nullptr, 0, FaultingFrameThread, nullptr, 0, &faulting_thread_id);
  CHECK(frame_thread != nullptr);
  Sleep(1000);
  CHECK(loss::unhandled_filter_set && loss::previous_filter == GameFilter);
  CHECK(Logged("Fault (first chance) 0xC0000005 at " + exe + "+0x") == 1);
  const std::string fault = LineWith("Fault (first chance) 0xC0000005");
  std::printf("%s\n", fault.c_str());
  CHECK(fault.find(std::format("(thread {}, the frame thread; write at 0x0).", faulting_thread_id)) != std::string::npos);
  loss::LogStall(1000, false);
  CHECK(Logged("No frame for 1 s") == 1);
  const std::string handling = LineWith("The frame thread is handling an exception: 0xC0000005 at " + exe + "+0x");
  CHECK(!handling.empty() && Logged("The frame thread is handling an exception") == 1);
  std::printf("%s\n", handling.c_str());
  CHECK(Logged("(handled; it went on)") == 0);
  loss::LogStall(2000, false);
  CHECK(LineWith("No frame for 2 s").find("; worker pool: effects") == std::string::npos);  // no job open: no words
#if !defined(_WIN64)
  // The open pool job's words ride on the stall line (x86: the pool exists only there).
  {
    namespace wp = twattila::worker_pool;
    static std::atomic<bool> release = false;
    // Each worker's thread id, and the fault line it formats for itself while the job is open (no allocation: FormatFault's).
    static std::array<std::atomic<DWORD>, 3> worker_ids = {};
    static std::array<std::array<char, 512>, 3> worker_lines = {};
    static EXCEPTION_RECORD worker_fault = {.ExceptionCode = EXCEPTION_ACCESS_VIOLATION, .NumberParameters = 2};
    worker_fault.ExceptionAddress = reinterpret_cast<void*>(inside + 0x40);
    worker_fault.ExceptionInformation[0] = 1;
    worker_fault.ExceptionInformation[1] = 0x20;
    wp::Update({.threads = 3, .in_use = true});
    CHECK(wp::Start({.work =
                         [](void*, uint32_t worker) {
                           loss::FormatFault(worker_lines[worker], "Fault (first chance)", worker_fault, loss::own_image_name.data(),
                                             loss::own_image_begin);
                           worker_ids[worker].store(GetCurrentThreadId());
                           while (!release.load()) Sleep(1);
                         },
                     .user = wp::User::EFFECT_UPDATE,
                     .join = wp::JoinRule::ENTERED},
                    2));
    for (int wait = 0; wait < 2000 && wp::internal::entered.load() != 2; ++wait) Sleep(1);
    for (int wait = 0; wait < 2000 && (worker_ids[1].load() == 0 || worker_ids[2].load() == 0); ++wait) Sleep(1);
    loss::LogStall(3000, false);
    CHECK(LineWith("No frame for 3 s").ends_with("; worker pool: effects, open, asked 2, entered 2, done 0"));
    // The pool records its threads' ids at CreateThread, and a fault on a worker names its number and the open job's user: the
    // gates' job_line (the bench's crash pin, rule 2) matches it.
    CHECK(worker_ids[1].load() == wp::internal::pool[0].id.load() && worker_ids[2].load() == wp::internal::pool[1].id.load());
    const std::regex job_line("crash on pool worker (\\d+) in (\\w+)'s job", std::regex::icase);
    for (uint32_t worker = 1; worker <= 2; ++worker) {
      const std::string line = worker_lines[worker].data();
      std::printf("%s\n", line.c_str());
      CHECK(line.find(std::format("at {}+0x1040 (thread {}, crash on pool worker {} in effects's job; write at 0x20).", exe, worker_ids[worker].load(), worker)) != std::string::npos);
      std::smatch matched;
      CHECK(std::regex_search(line, matched, job_line) && matched[1] == std::to_string(worker) && matched[2] == "effects");
    }
    // A pool thread with no job open (here the main thread entered as the pool's fourth slot): its number, no user.
    std::array<char, 512> main_line = {};
    wp::internal::pool[3].id.store(main_thread);
    loss::FormatFault(main_line, "Fault (first chance)", worker_fault, nullptr, 0);
    CHECK(std::string(main_line.data()).find(std::format("(thread {}, crash on pool worker 4 in effects's job; write at 0x20).", main_thread)) != std::string::npos);
    release = true;
    wp::Join();
    loss::FormatFault(main_line, "Fault (first chance)", worker_fault, nullptr, 0);
    CHECK(std::string(main_line.data()).find(std::format("(thread {}, pool worker 4, no job open; write at 0x20).", main_thread)) != std::string::npos);
    wp::internal::pool[3].id.store(0);
    wp::Stop();
    CHECK(wp::internal::pool[0].id.load() == 0 && wp::internal::pool[1].id.load() == 0);
    // Not a pool thread: the frame thread / another thread words stay.
    loss::FormatFault(main_line, "Fault (first chance)", worker_fault, nullptr, 0);
    CHECK(std::string(main_line.data()).find(std::format("(thread {}, another thread; write at 0x20).", main_thread)) != std::string::npos);
  }
#endif
  CHECK(WaitForSingleObject(frame_thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(frame_thread);
  const size_t handling_lines = Logged("The frame thread is handling an exception");

  // 4. The same fault handled at once, the thread then waiting: the same place is not said again, and the stall line
  // says it was handled (once), not that the thread is handling one.
  const HANDLE handled_thread = CreateThread(nullptr, 0, HandledFrameThread, nullptr, 0, nullptr);
  CHECK(handled_thread != nullptr);
  Sleep(500);
  loss::LogStall(5000, false);
  loss::LogStall(6000, false);
  CHECK(Logged("No frame for 6 s") == 1 && Logged("Fault (first chance)") == 1);
  const std::string handled = LineWith("(handled; it went on): 0xC0000005 at " + exe + "+0x");
  CHECK(!handled.empty() && Logged("(handled; it went on)") == 1);
  std::printf("%s\n", handled.c_str());
  CHECK(Logged("The frame thread is handling an exception") == handling_lines);
  CHECK(WaitForSingleObject(handled_thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(handled_thread);
  // A fault before the frame thread's last present: not named.
  const HANDLE waiting_thread = CreateThread(nullptr, 0, WaitingFrameThread, nullptr, 0, nullptr);
  CHECK(waiting_thread != nullptr);
  Sleep(200);
  loss::frame_fault_said = 0;
  loss::frame_fault_tick = 1;
  loss::LogStall(7000, false);
  CHECK(Logged("No frame for 7 s") == 1 && Logged("(handled; it went on)") == 1);
  CHECK(Logged("The frame thread is handling an exception") == handling_lines);
  CHECK(WaitForSingleObject(waiting_thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(waiting_thread);

  // 5. A page of the add-on's code outside its image: named like the image (fault line, places); 4 places.
  loss::present_thread = 0;
  auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READ));
  CHECK(page != nullptr);
  CHECK(loss::AddCodeRange(page, page + 0x1000, "test stubs"));
  CHECK(loss::CodePlace(reinterpret_cast<uintptr_t>(page) + 0x10) == "test stubs+0x10");
  CHECK(Filter(EXCEPTION_ACCESS_VIOLATION, reinterpret_cast<uintptr_t>(page) + 0x10, 0, 0x1234) == EXCEPTION_CONTINUE_SEARCH);
  CHECK(Logged(std::format("Fault (first chance) 0xC0000005 at test stubs+0x10 (thread {}, another thread; read at 0x1234).", main_thread)) == 1);
  // A render_mt part's page (engine_code, effect_update_sites and draw_recording_sites call this once each): the range's INFO line is
  // the gates' part_range, and a fault in the page is named by the same token and offset.
  loss::AddPartRange("B3b", "engine_stubs", reinterpret_cast<void*>(0x10), 0x10);
  const std::string range_line = LineWith("render_mt range:");
  std::printf("%s\n", range_line.c_str());
  const std::regex part_range("render_mt range: (\\w+) (?:code|stub) ([^\\s+]+)\\+0x([0-9A-Fa-f]+)\\.\\.\\+?0x([0-9A-Fa-f]+)");
  std::smatch range_match;
  CHECK(std::regex_search(range_line, range_match, part_range) && range_match[1] == "B3b" && range_match[2] == "engine_stubs"
        && range_match[3] == "0" && range_match[4] == "10");
  CHECK(loss::CodePlace(0x18) == "engine_stubs+0x8");
  CHECK(loss::AddCodeRange(reinterpret_cast<void*>(0x20), reinterpret_cast<void*>(0x30), "three"));
  CHECK(loss::AddCodeRange(reinterpret_cast<void*>(0x30), reinterpret_cast<void*>(0x40), "four"));
  CHECK(!loss::AddCodeRange(reinterpret_cast<void*>(0x40), reinterpret_cast<void*>(0x50), "five"));
  // All 4 places taken: a warning says the page is not named, and no part_range line is made.
  loss::AddPartRange("C", "recording_page", reinterpret_cast<void*>(0x40), 0x10);
  CHECK(Logged("render_mt range: C page recording_page is not named in fault lines") == 1 && Logged("stub recording_page") == 0);

  // 6. Up to 4 places are said (a handled first chance, Detours' own, costs one), the floating-point codes too; a place
  // said once is not said again.
  CHECK(Filter(EXCEPTION_FLT_INVALID_OPERATION, inside + 0x10) == EXCEPTION_CONTINUE_SEARCH);
  CHECK(Logged(std::format("Fault (first chance) 0xC0000090 at {}+0x1010 (thread {}, another thread).", exe, main_thread)) == 1);
  CHECK(Filter(0xC00002B5, inside + 0x20) == EXCEPTION_CONTINUE_SEARCH);
  CHECK(Logged("Fault (first chance) 0xC00002B5") == 1);
  CHECK(Filter(EXCEPTION_INT_OVERFLOW, inside + 0x30) == EXCEPTION_CONTINUE_SEARCH);
  CHECK(Filter(EXCEPTION_FLT_INVALID_OPERATION, inside + 0x10) == EXCEPTION_CONTINUE_SEARCH);
  CHECK(Logged("Fault (first chance)") == 4);

  // 7. A stack overflow in the add-on's code, then an unhandled one anywhere: nothing on the overflowed stack, the frame
  // watch says each from its own; the unhandled one goes on to the game's filter. Without a watch the overflowed thread
  // does not wait; with one it is said before the handler returns (a crash reporter suspends the threads at once).
  uint64_t start_tick = GetTickCount64();
  CHECK(Filter(EXCEPTION_STACK_OVERFLOW, inside + 0x40) == EXCEPTION_CONTINUE_SEARCH);
  CHECK(GetTickCount64() - start_tick < 100);
  CHECK(loss::overflow_count == 1 && !loss::overflow_unhandled && Logged("Stack overflow") == 0);
  loss::watch_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  loss::watch_thread = CreateThread(nullptr, 0, loss::WatchLoop, loss::watch_stop, 0, nullptr);
  CHECK(loss::watch_thread != nullptr);
  Sleep(1500);
  CHECK(loss::watch_running && Logged(std::format("Stack overflow at {}+0x1040 (thread {}, another thread; first chance, in the add-on's code).", exe, main_thread)) == 1);
  start_tick = GetTickCount64();
  CHECK(Filter(EXCEPTION_STACK_OVERFLOW, inside + 0x50) == EXCEPTION_CONTINUE_SEARCH);
  CHECK(Logged(std::format("Stack overflow at {}+0x1050", exe)) == 1 && GetTickCount64() - start_tick < 400);
  start_tick = GetTickCount64();
  CHECK(Call(loss::OnUnhandled, {.ExceptionCode = EXCEPTION_STACK_OVERFLOW, .ExceptionAddress = reinterpret_cast<void*>(outside)}) == EXCEPTION_EXECUTE_HANDLER);
  CHECK(Logged("Stack overflow at ntdll.dll+0x") == 1 && Logged("; unhandled).") == 1 && GetTickCount64() - start_tick < 400);
  CHECK(game_filter_calls == 1 && Logged("Unhandled exception") == 0 && Logged("Stack overflow") == 3);
  std::printf("%s\n", LineWith("Stack overflow at ntdll.dll+0x").c_str());
  loss::StopWatch();

  // 8. An unhandled exception on another thread, in the game's code: one line naming the module, then the game's filter
  // (its result back to Windows). A C++ exception thrown by this image says so; one thrown elsewhere does not.
  EXCEPTION_RECORD crash = {.ExceptionCode = EXCEPTION_ACCESS_VIOLATION, .ExceptionAddress = reinterpret_cast<void*>(outside), .NumberParameters = 2};
  crash.ExceptionInformation[0] = 1;
  crash.ExceptionInformation[1] = 8;
  CHECK(Call(loss::OnUnhandled, crash) == EXCEPTION_EXECUTE_HANDLER && game_filter_calls == 2);
  const std::string unhandled = LineWith("Unhandled exception 0xC0000005 at ntdll.dll+0x");
  CHECK(unhandled.ends_with(std::format("(thread {}, another thread; write at 0x8).", main_thread)));
  std::printf("%s\n", unhandled.c_str());
  CatchThrown();
  CHECK(thrown_record.ExceptionCode == 0xE06D7363);
  loss::unhandled_logged = false;
  CHECK(Call(loss::OnUnhandled, thrown_record) == EXCEPTION_EXECUTE_HANDLER && game_filter_calls == 3);
  const std::string thrown = LineWith("Unhandled exception 0xE06D7363");
  CHECK(thrown.ends_with("; thrown by the add-on's code)."));
  std::printf("%s\n", thrown.c_str());
  EXCEPTION_RECORD foreign = thrown_record;
#if defined(_WIN64)
  foreign.ExceptionInformation[3] = 0x10000;
#else
  foreign.ExceptionInformation[2] = outside;
#endif
  loss::unhandled_logged = false;
  CHECK(Call(loss::OnUnhandled, foreign) == EXCEPTION_EXECUTE_HANDLER && game_filter_calls == 4);
  CHECK(Logged("Unhandled exception 0xE06D7363") == 2 && Logged("thrown by the add-on's code") == 1);
  // Once: the next one is passed on, not said.
  CHECK(Call(loss::OnUnhandled, crash) == EXCEPTION_EXECUTE_HANDLER && game_filter_calls == 5);
  CHECK(Logged("Unhandled exception") == 3);

  // 9. This image's abort (std::terminate's default ends there): one line, then abort goes on (here raise returns).
  std::raise(SIGABRT);
  CHECK(Logged("The add-on's code called abort") == 1);

  // 10. Uninstall: the handlers go and the game's filter is on top again.
  loss::Uninstall();
  CHECK(loss::fault_handler == nullptr && !loss::unhandled_filter_set);
  CHECK(std::signal(SIGABRT, SIG_DFL) == SIG_DFL);
  CHECK(SetUnhandledExceptionFilter(GameFilter) == GameFilter);
  CHECK(Logged("Another exception filter") == 0);
  // A filter set over ours stays on top, and the image stays loaded.
  loss::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
  CHECK(loss::unhandled_filter_set && loss::previous_filter == GameFilter);
  CHECK(SetUnhandledExceptionFilter(LaterFilter) == loss::OnUnhandled);
  loss::Uninstall();
  CHECK(Logged("Another exception filter was set over the add-on's: the add-on stays loaded.") == 1);
  // The kept image attached again does not put its filter over that one (a loop between the two into an overflow).
  CHECK(loss::unhandled_filter_set && loss::overflow_kept == nullptr && loss::overflow_logged == nullptr);
  loss::Install();
  loss::OnPresent(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
  CHECK(SetUnhandledExceptionFilter(GameFilter) == LaterFilter);
  loss::Uninstall();
  std::printf("PASS\n");
  return 0;
}
