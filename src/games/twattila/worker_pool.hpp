/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

#if !defined(_WIN64)
#include <emmintrin.h>
#include <float.h>
#include <xmmintrin.h>

#include <charconv>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "../../utils/log.hpp"
#include "./cpu_sets.hpp"
#endif

// The shared worker pool (Attila, 32-bit): a few threads that the render thread (the owner: the thread of Update, the present
// thread) hands one job at a time to. Every user (the table below) has a path that runs alone: a user gets no workers when the
// pool is empty, when a job is open, when its limit is 1 or when it is not on the owner thread, and then it does the whole job
// itself, so a user reached inside another user's job (or the pool stopped) never needs the pool.
//
// A job: Start wakes workers 1 to n (the render thread is 0), the owner does its own part, Join waits. The users:
//
//   User            Join     How it calls                                  Units
//   DRAW_RECORDING  ALL      Run, at its 3 sites                           the planner's chunks (worker w runs chunk w)
//   EFFECT_UPDATE   ENTERED  Start at the walk's call, Join at its end     claims of G = 1 (the effect update)
//   ANIMATION_ROWS  ENTERED  Run, one job per fill call                    512-item blocks
//   PENDING_SWEEP   ENTERED  Run inside Arm                                1024-entry blocks
//   RING_FILL       ENTERED  Run, min(K, n / 384)                          64-record chunks
//
// ALL: pre-partitioned work. Every woken worker runs, and Join waits for all of them; a Run whose Start was refused runs the
// workers' parts on the owner, one after the other, so an ALL job always completes. For that, an ALL work function never waits
// for another worker (only for what the owner publishes, property (a)). ENTERED: claimed units. The owner claims until none is left,
// and Join waits only for the workers already inside: a worker that wakes late finds the job closed and skips it. What a work
// function may do:
//  (a) wait in the middle of a job (a bounded spin, then an atomic wait) on a word the owner publishes between Start and Join,
//      as long as the owner publishes it before it calls Join (Join waits for every worker that is inside);
//  (b) while a job is open across game code on the owner thread (the effect update's Start .. Join), every user's Available()
//      is 0, so any user the game's code reaches in between runs alone;
//  (c) in an ALL job, claim shared units (an atomic counter) before its own partition: every woken worker runs, so the units
//      are drained even when the owner's part has none left.
//
// Why a late worker is safe: Start publishes the generation in `open`, a worker counts itself in `entered`, then reads `open`
// (both sides seq_cst, a Dekker pair): either it sees the job closed and skips it, or Join sees it inside and waits for it.
// After Join no worker touches the user's memory. The pool's SFENCE after the work function covers write-combined stores for
// every user (the ring fill keeps its own sfence before the release increment of each chunk).
//
// Trace: a job with `trace` only copies one record into a fixed array at Join (no formatting, no log call inside game code);
// Update prints the records, and the refusals counted while Settings::trace is on, at the next present.
//
// Per-user limits are batch-only: RENODX_TW_WORKER_LIMITS=recording:4,effects:8,animation:2,sweep:4,ring:4 (threads, the
// render thread included, 1 to 16; 1 = the render thread alone). RENODX_TW_POOL_SPIN_US (default 3) is how long an idle worker
// spins for its next job before it waits. Both are read with GetEnvironmentVariableA at the first present and again whenever
// Settings::env_generation changes (the bench sets the variables, then bumps the generation), at an Update with no job open;
// one line says what was applied, and an entry that does not parse is said once. ArmTrace(frames) opens one trace window with
// a header line (time and the pool's values) for a bench trace. No CPU pinning of our own: each pool thread gets the sets
// cpu_sets published (Keep Render Core Free) and nothing else. x86 only: on x64 everything is an empty stub.
namespace twattila::worker_pool {

// Plus the render thread = 16 threads.
constexpr uint32_t MAX_WORKERS = 15;

enum class User : uint8_t { DRAW_RECORDING, EFFECT_UPDATE, ANIMATION_ROWS, PENDING_SWEEP, RING_FILL, COUNT };
enum class JoinRule : uint8_t { ALL, ENTERED };

struct Job {
  // Worker 1 to workers; the render thread is 0.
  void (*work)(void* context, uint32_t worker) = nullptr;
  void* context = nullptr;
  User user = User::DRAW_RECORDING;
  JoinRule join = JoinRule::ALL;
  // One-frame trace only: Join keeps a record (each worker's CPU and entry time) that Update prints at the next present.
  bool trace = false;
};

struct JoinResult {
  // QPC ticks spent in Join.
  uint64_t wait_ticks = 0;
  // Workers that ran the job.
  uint32_t ran = 0;
};

struct Settings {
  // The "Worker Threads" row, the render thread included (2 to 16).
  uint32_t threads = 4;
  // Any pool user's stage wakes workers.
  bool in_use = false;
  // A trace frame is running: calls that were refused (a job open, not the owner) are counted for the trace lines.
  bool trace = false;
  // The add-on's env generation: the limits and the spin are read again when it changes.
  uint32_t env_generation = 0;
};

#if !defined(_WIN64)

inline uint64_t Qpc() {
  LARGE_INTEGER counter = {};
  QueryPerformanceCounter(&counter);
  return static_cast<uint64_t>(counter.QuadPart);
}

static uint64_t qpc_frequency = [] {
  LARGE_INTEGER frequency = {};
  QueryPerformanceFrequency(&frequency);
  return static_cast<uint64_t>(frequency.QuadPart);
}();

namespace internal {

constexpr uint32_t WORKER_STACK_BYTES = 512 * 1024;
constexpr uint32_t DEFAULT_SPIN_US = 3;
constexpr size_t USER_COUNT = static_cast<size_t>(User::COUNT);
constexpr size_t MAX_RECORDS = 1024;
// The user names of RENODX_TW_WORKER_LIMITS and of the trace lines, in the order of User.
constexpr std::array<std::string_view, USER_COUNT> USER_KEYS = {"recording", "effects", "animation", "sweep", "ring"};

struct alignas(64) Worker {
  std::atomic<uint32_t> ticket{0};
  HANDLE thread = nullptr;
};

// The open job: written by Start before it opens the job, read by a worker only while it is counted in `entered` and has seen
// the job open, so Join (which waits for `entered`) is always after the last read.
struct alignas(64) Current {
  void (*work)(void* context, uint32_t worker) = nullptr;
  void* context = nullptr;
  uint32_t workers = 0;
  uint32_t x87 = 0;
  uint32_t sse = 0;
  JoinRule join = JoinRule::ALL;
  User user = User::DRAW_RECORDING;
  bool trace = false;
  uint64_t trace_start = 0;
};

// What Join keeps of a traced job: Update prints it.
struct TraceRecord {
  User user = User::DRAW_RECORDING;
  uint32_t workers = 0;
  uint32_t ran = 0;
  uint64_t wait_ticks = 0;
  // Per worker: processor group << 8 | number, and the ticks from Start to its entry plus 1 (0: it did not run).
  std::array<uint16_t, MAX_WORKERS> cpu = {};
  std::array<uint32_t, MAX_WORKERS> entry = {};
};

#if defined(TWATTILA_WORKER_POOL_CONTROLS)
// Test switches: a worker held between waking and counting itself in (a late worker), and a thread creation that fails.
struct Controls {
  bool fail_thread_creation = false;
  // The worker held (0: none) waits until `release_late` is set; `late_parked` counts the times it was held.
  uint32_t late_worker = 0;
  std::atomic<bool> release_late{true};
  std::atomic<uint32_t> late_parked{0};
  // A worker told to stop waits here until it is cleared (Stop stays inside its wait for the threads).
  std::atomic<bool> hold_exit{false};
};
static Controls controls;
static uint32_t thread_attempts = 0;
#endif

static std::array<Worker, MAX_WORKERS> pool;
// Threads created (never fewer while in use) and the target row's worker count: `ready` of them may be used.
static uint32_t created = 0;
static uint32_t target = 0;
static std::atomic<uint32_t> ready = 0;
// The target a worker thread could not be created for (no retry until the target changes).
static uint32_t failed_target = ~0u;
// The generation of cpu_sets' published sets the pool threads were given (all ones: none yet).
static uint32_t given_generation = ~0u;
static uint32_t generation = 0;
// The env generation the limits and the spin were read at (all ones: not yet).
static uint32_t env_generation_applied = ~0u;
// The entries and values that did not parse and were said already.
static std::vector<std::string> bad_said;
// A trace window: presents left, and the header line still to be written; the first present's time for it.
static std::atomic<uint32_t> trace_frames = 0;
static bool trace_header_pending = false;
static uint64_t first_update_ticks = 0;
static bool open_logged = false;
static bool other_open_logged = false;
static bool pinned = false;
// The most threads, the render thread included, each user may use.
static std::array<uint32_t, USER_COUNT> limits = {};
// QPC ticks an idle worker spins: one 32-bit word, so a read on a worker is never torn by an Update that changes it.
static std::atomic<uint32_t> spin_ticks = 0;
static std::atomic<DWORD> owner = 0;
// The thread that started the open job: only it may close it.
static std::atomic<DWORD> starter = 0;
static std::atomic<bool> stopping = false;
static std::atomic<bool> counting = false;
alignas(64) static Current current;
// The job's generation while it is open (0: closed).
alignas(64) static std::atomic<uint32_t> open = 0;
// Workers between waking and leaving the job (the skipped ones too).
alignas(64) static std::atomic<uint32_t> entered = 0;
// Workers that finished the work function.
alignas(64) static std::atomic<uint32_t> done = 0;
// A job is open, or Stop is stopping the threads.
alignas(64) static std::atomic<bool> busy = false;
// Refused calls per user since the last trace line (counted only while Settings::trace is on).
static std::array<std::atomic<uint32_t>, USER_COUNT> refused = {};
static std::array<uint32_t, MAX_WORKERS> trace_cpu = {};
static std::array<uint64_t, MAX_WORKERS> trace_entry = {};
static std::array<TraceRecord, MAX_RECORDS> records;
static uint32_t record_count = 0;
static uint32_t record_drops = 0;

// A worker: spins for its next ticket, then waits on it; counts itself in and runs the job only if it is still open (a late
// worker skips a closed job); takes the starting thread's x87 and SSE control words every job (game code on a worker may have
// changed its own and not restored it).
static DWORD WINAPI WorkerMain(void* context) {
  const uint32_t index = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context));
  Worker& self = pool[index - 1];
  uint32_t last = 0;
  for (;;) {
    uint32_t ticket = self.ticket.load(std::memory_order_acquire);
    if (ticket == last) {
      const uint64_t start = Qpc();
      while (ticket == last && Qpc() - start < spin_ticks.load(std::memory_order_relaxed)) {
        _mm_pause();
        ticket = self.ticket.load(std::memory_order_acquire);
      }
      while (ticket == last) {
        self.ticket.wait(last);
        ticket = self.ticket.load(std::memory_order_acquire);
      }
    }
    last = ticket;
    if (stopping.load(std::memory_order_acquire)) {
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
      while (controls.hold_exit.load(std::memory_order_acquire)) Sleep(1);
#endif
      return 0;
    }
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
    if (controls.late_worker == index) {
      controls.late_parked.fetch_add(1, std::memory_order_relaxed);
      while (!controls.release_late.load(std::memory_order_acquire)) Sleep(1);
    }
#endif
    entered.fetch_add(1, std::memory_order_seq_cst);
    if (open.load(std::memory_order_seq_cst) == ticket) {
      unsigned int ignored = 0;
      __control87_2(current.x87, _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC, &ignored, nullptr);
      __control87_2(current.sse, _MCW_EM | _MCW_RC | _MCW_DN, nullptr, &ignored);
      if (current.trace) {
        PROCESSOR_NUMBER processor = {};
        GetCurrentProcessorNumberEx(&processor);
        trace_cpu[index - 1] = (static_cast<uint32_t>(processor.Group) << 8) | processor.Number;
        trace_entry[index - 1] = Qpc();
      }
      current.work(current.context, index);
      _mm_sfence();
      const uint32_t finished = done.fetch_add(1, std::memory_order_release) + 1;
      if (current.join == JoinRule::ALL && finished == current.workers) {
        done.notify_one();
      }
    }
    // The owner waits on `entered` only after it closed the job: a worker that leaves last wakes it only if it sees the closed
    // job (the same Dekker pair, leaving side).
    if (entered.fetch_sub(1, std::memory_order_seq_cst) == 1 && open.load(std::memory_order_seq_cst) != ticket) {
      entered.notify_one();
    }
  }
}

}  // namespace internal

// Opens one trace window of `frames` presents: the next Update writes a header line (time, the row, the ready workers, the
// limits, the spin, the env generation), and Tracing() is true until the window ends. A bench trace of any user arms it.
inline void ArmTrace(uint32_t frames) {
  using namespace internal;
  trace_header_pending = true;
  trace_frames.store(frames, std::memory_order_relaxed);
}

// A trace window is open: users set Job::trace from it (and their own trace output).
inline bool Tracing() { return internal::trace_frames.load(std::memory_order_relaxed) != 0; }

// Workers this user may use now (0: run alone). 0 when the caller is not the owner (pool threads and game threads), when a job
// is open or when the pool is empty; otherwise the ready workers, up to the user's limit less the render thread. While a trace
// runs, a 0 for a job open or another thread is counted for the trace lines.
inline uint32_t Available(User user) {
  using namespace internal;
  if (GetCurrentThreadId() != owner.load(std::memory_order_relaxed) || busy.load(std::memory_order_relaxed)) {
    if (counting.load(std::memory_order_relaxed)) {
      refused[static_cast<size_t>(user)].fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
  }
  return std::min(ready.load(std::memory_order_relaxed), limits[static_cast<size_t>(user)] - 1);
}

// Opens a job for workers 1 to `workers` (at most Available(job.user)). False (nothing started, the caller runs alone): not the
// owner thread or a job already open (counted for the trace), or no workers asked for or ready.
inline bool Start(const Job& job, uint32_t workers) {
  using namespace internal;
  if (GetCurrentThreadId() != owner.load(std::memory_order_relaxed) || busy.exchange(true, std::memory_order_acquire)) {
    if (counting.load(std::memory_order_relaxed)) {
      refused[static_cast<size_t>(job.user)].fetch_add(1, std::memory_order_relaxed);
    }
    return false;
  }
  if (workers == 0 || workers > ready.load(std::memory_order_relaxed)) {
    busy.store(false, std::memory_order_release);
    return false;
  }
  starter.store(GetCurrentThreadId(), std::memory_order_relaxed);
  current = {.work = job.work,
             .context = job.context,
             .workers = workers,
             .join = job.join,
             .user = job.user,
             .trace = job.trace,
             .trace_start = (job.trace ? Qpc() : 0)};
  __control87_2(0, 0, &current.x87, &current.sse);
  done.store(0, std::memory_order_relaxed);
  if (job.trace) {
    std::fill_n(trace_entry.begin(), workers, 0);
  }
  generation = (generation + 1 == 0 ? 1 : generation + 1);
  open.store(generation, std::memory_order_seq_cst);
  for (uint32_t i = 0; i < workers; ++i) {
    pool[i].ticket.store(generation, std::memory_order_release);
    pool[i].ticket.notify_one();
  }
  return true;
}

// Waits for the open job and closes it: ALL waits for every woken worker, then both rules close the job and wait for the workers
// that are still inside. A no-op when no job is open or the caller is not the thread that started it. After it no worker touches
// the job's memory. A traced job leaves a record for Update.
inline JoinResult Join() {
  using namespace internal;
  if (!busy.load(std::memory_order_relaxed) || GetCurrentThreadId() != starter.load(std::memory_order_relaxed)) return {};
  const uint64_t start = Qpc();
  // A short spin, then a wait, until `word` equals `value` (no timeout: a worker can't be stopped inside the game's code). The
  // first load is seq_cst: the close of the job and this load are the owner's half of the Dekker pair.
  const auto wait_until = [](std::atomic<uint32_t>& word, uint32_t value) {
    const uint64_t wait_start = Qpc();
    const uint64_t spin_limit = qpc_frequency * 2 / 1000;
    uint32_t seen = word.load(std::memory_order_seq_cst);
    while (seen != value && Qpc() - wait_start < spin_limit) {
      _mm_pause();
      seen = word.load(std::memory_order_seq_cst);
    }
    while (seen != value) {
      word.wait(seen);
      seen = word.load(std::memory_order_seq_cst);
    }
  };
  if (current.join == JoinRule::ALL) {
    wait_until(done, current.workers);
  }
  // Do not weaken this store (seq_cst) or the first `entered` load in wait_until: they are the owner's half of the Dekker pair
  // with the worker's `entered` add and `open` load, and no test on x86 can catch a weaker order.
  open.store(0, std::memory_order_seq_cst);
  wait_until(entered, 0);
  const JoinResult result = {.wait_ticks = Qpc() - start, .ran = done.load(std::memory_order_acquire)};
  if (current.trace && record_count < MAX_RECORDS) {
    TraceRecord& record = records[record_count++];
    record = {.user = current.user, .workers = current.workers, .ran = result.ran, .wait_ticks = result.wait_ticks};
    for (uint32_t i = 0; i < current.workers; ++i) {
      record.cpu[i] = static_cast<uint16_t>(trace_cpu[i]);
      record.entry[i] = (trace_entry[i] == 0 ? 0 : static_cast<uint32_t>(trace_entry[i] - current.trace_start) + 1);
    }
  } else if (current.trace) {
    ++record_drops;
  }
  // No job: a `busy` that Stop holds later never matches a stale starter.
  starter.store(0, std::memory_order_relaxed);
  busy.store(false, std::memory_order_release);
  return result;
}

// Start, the owner's own part, Join. The part runs even when Start was refused: safe for ENTERED jobs, where units are claimed.
// A refused ALL job runs the workers' parts on this thread after its own, so it always completes.
template <typename Part>
inline JoinResult Run(const Job& job, uint32_t workers, Part&& part) {
  const bool started = Start(job, workers);
  part();
  if (!started && job.join == JoinRule::ALL) {
    for (uint32_t worker = 1; worker <= workers; ++worker) {
      job.work(job.context, worker);
    }
  }
  return (started ? Join() : JoinResult{});
}

// Ends the threads and joins them (Update with no user on, detach; never under the loader lock). Users then run alone, so it
// does not depend on any site coming out. A job still open is joined first (the starting thread only). One open on another
// thread keeps the pool as it is (its owner joins it later) and pins the module, so a thread that is still inside the work
// function never runs in unmapped code. Stop holds `busy` while it ends the threads: a Start meanwhile is refused.
inline void Stop() {
  using namespace internal;
  Join();
  if (busy.exchange(true, std::memory_order_acquire)) {
    if (created > 0 && !pinned) {
      pinned = true;
      HMODULE module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&WorkerMain),
                         &module);
      renodx::utils::log::w("[Total War] Worker pool: stopped with a job open on another thread; the pool stays and the module is pinned.");
    }
    return;
  }
  ready.store(0, std::memory_order_relaxed);
  target = 0;
  failed_target = ~0u;
  given_generation = ~0u;
  if (created > 0) {
    stopping.store(true, std::memory_order_release);
    std::array<HANDLE, MAX_WORKERS> handles = {};
    for (uint32_t i = 0; i < created; ++i) {
      handles[i] = pool[i].thread;
      pool[i].ticket.fetch_add(1, std::memory_order_release);
      pool[i].ticket.notify_one();
    }
    WaitForMultipleObjects(created, handles.data(), TRUE, INFINITE);
    for (uint32_t i = 0; i < created; ++i) {
      CloseHandle(handles[i]);
      pool[i].thread = nullptr;
    }
    created = 0;
    stopping.store(false, std::memory_order_release);
  }
  busy.store(false, std::memory_order_release);
}

// Every present on the owner thread, outside jobs, before the users' own Updates: prints what the traced jobs left, grows the
// pool to the row's size (never shrinks it while in use: extra threads sleep and are not offered), gives each thread cpu_sets'
// published sets, stops it when no user is on. A job still open here is joined (said once).
inline void Update(const Settings& settings) {
  using namespace internal;
  owner.store(GetCurrentThreadId(), std::memory_order_relaxed);
  if (first_update_ticks == 0) {
    first_update_ticks = Qpc();
  }
  // A job still open here is joined first (only the thread that started it can: another thread's stays open and is said once).
  if (busy.load(std::memory_order_acquire)) {
    Join();
    const bool still_open = busy.load(std::memory_order_acquire);
    bool& logged = (still_open ? other_open_logged : open_logged);
    if (!logged) {
      renodx::utils::log::w(still_open ? "[Total War] Worker pool: a job is open on another thread."
                                       : "[Total War] Worker pool: a job was still open at the present; joined.");
    }
    logged = true;
  }

  // The limits and the spin, at the first present and when the env generation changed; not while a job is open (it is tried
  // again at the next present).
  if (!busy.load(std::memory_order_acquire) && settings.env_generation != env_generation_applied) {
    env_generation_applied = settings.env_generation;
    limits.fill(MAX_WORKERS + 1);
    uint32_t microseconds = DEFAULT_SPIN_US;
    const auto read = [](const char* name) {
      std::string text(GetEnvironmentVariableA(name, nullptr, 0), '\0');
      text.resize(GetEnvironmentVariableA(name, text.data(), static_cast<DWORD>(text.size())));
      return text;
    };
    const auto parse = [](std::string_view text, uint32_t* value) {
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), *value);
      return error == std::errc() && end == text.data() + text.size();
    };
    // Said once each: the same entry in a later read is not said again.
    const auto complain = [](const std::string& what) {
      if (std::ranges::find(bad_said, what) == bad_said.end()) {
        bad_said.push_back(what);
        renodx::utils::log::w("[Total War] Worker pool: " + what + " does not parse; left out.");
      }
    };
    if (const std::string spin = read("RENODX_TW_POOL_SPIN_US"); !spin.empty() && !parse(spin, &microseconds)) {
      microseconds = DEFAULT_SPIN_US;
      complain("RENODX_TW_POOL_SPIN_US=" + spin);
    }
    spin_ticks.store(static_cast<uint32_t>(std::min<uint64_t>(qpc_frequency * microseconds / 1000000, ~0u)), std::memory_order_relaxed);
    // recording:4,effects:8,...
    const std::string list = read("RENODX_TW_WORKER_LIMITS");
    for (std::string_view rest = list; !rest.empty();) {
      const size_t comma = rest.find(',');
      const std::string_view entry = rest.substr(0, comma);
      rest = (comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1));
      const size_t colon = entry.find(':');
      const auto key = std::ranges::find(USER_KEYS, entry.substr(0, colon));
      uint32_t threads = 0;
      if (colon != std::string_view::npos && key != USER_KEYS.end() && parse(entry.substr(colon + 1), &threads)) {
        limits[static_cast<size_t>(key - USER_KEYS.begin())] = std::clamp<uint32_t>(threads, 1, MAX_WORKERS + 1);
      } else if (!entry.empty()) {
        complain("RENODX_TW_WORKER_LIMITS entry \"" + std::string(entry) + "\"");
      }
    }
    std::string applied = std::format("[Total War] Worker pool: limits and spin applied (env generation {}):", settings.env_generation);
    for (size_t user = 0; user < USER_COUNT; ++user) {
      applied += std::format(" {} {}", USER_KEYS[user], limits[user]);
    }
    renodx::utils::log::i(applied + std::format(", spin {} us.", microseconds));
  }

  // The trace lines of the last present's jobs, and the refusals counted since the last ones (not while a job is open: the
  // starting thread may be writing a record).
  if (!busy.load(std::memory_order_acquire)) {
    const double microseconds = 1e6 / static_cast<double>(qpc_frequency);
    for (uint32_t index = 0; index < record_count; ++index) {
      const TraceRecord& record = records[index];
      std::string line = std::format("[Total War] Worker pool trace ({}): {} of {} workers ran, join wait {:.1f} us",
                                     USER_KEYS[static_cast<size_t>(record.user)], record.ran, record.workers,
                                     static_cast<double>(record.wait_ticks) * microseconds);
      for (uint32_t i = 0; i < record.workers; ++i) {
        if (record.entry[i] == 0) continue;
        line += std::format(", worker {} on CPU {}:{} at +{:.1f} us", i + 1, record.cpu[i] >> 8, record.cpu[i] & 0xff,
                            static_cast<double>(record.entry[i] - 1) * microseconds);
      }
      renodx::utils::log::i(line);
    }
    if (record_drops != 0) {
      renodx::utils::log::w(std::format("[Total War] Worker pool trace: {} records dropped (the array is full).", record_drops));
    }
    record_count = 0;
    record_drops = 0;
  }
  std::string refusals;
  for (size_t user = 0; user < USER_COUNT; ++user) {
    if (const uint32_t count = refused[user].exchange(0, std::memory_order_relaxed); count != 0) {
      refusals += std::format(" {} {}", USER_KEYS[user], count);
    }
  }
  if (!refusals.empty()) {
    renodx::utils::log::i("[Total War] Worker pool trace: starts and Available() calls refused (a job open, or not the owner):" + refusals);
  }
  // A trace window: the header at its first present, one present counted off at each later one.
  if (trace_header_pending) {
    trace_header_pending = false;
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    std::string header = std::format(
        "[Total War] Worker pool trace window of {} presents at {:02}:{:02}:{:02}.{:03} ({:.1f} ms after the first present): threads {}, "
        "workers ready {} of {}, spin {} us, env generation {}, limits",
        trace_frames.load(std::memory_order_relaxed), now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        static_cast<double>(Qpc() - first_update_ticks) * 1000.0 / static_cast<double>(qpc_frequency), settings.threads,
        ready.load(std::memory_order_relaxed), created,
        static_cast<uint64_t>(spin_ticks.load(std::memory_order_relaxed)) * 1000000 / qpc_frequency, settings.env_generation);
    for (size_t user = 0; user < USER_COUNT; ++user) {
      header += std::format(" {} {}", USER_KEYS[user], limits[user]);
    }
    renodx::utils::log::i(header);
  } else if (trace_frames.load(std::memory_order_relaxed) != 0) {
    trace_frames.fetch_sub(1, std::memory_order_relaxed);
  }
  counting.store(settings.trace || trace_frames.load(std::memory_order_relaxed) != 0, std::memory_order_relaxed);

  if (!settings.in_use) {
    Stop();
    return;
  }
  // Growth and the CPU sets change `created` and the threads' handles, which Stop (another thread, at detach) also changes: only
  // with `busy`, else this present is skipped.
  if (busy.exchange(true, std::memory_order_acquire)) return;
  target = std::min(std::max<uint32_t>(settings.threads, 1) - 1, MAX_WORKERS);
  if (target != failed_target) {
    failed_target = ~0u;
  }
  for (; created < target && failed_target == ~0u; ++created) {
    pool[created].ticket.store(0, std::memory_order_relaxed);
    HANDLE thread = nullptr;
    bool fail = false;
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
    ++thread_attempts;
    fail = controls.fail_thread_creation;
#endif
    if (!fail) {
      thread = CreateThread(nullptr, WORKER_STACK_BYTES, WorkerMain, reinterpret_cast<void*>(static_cast<uintptr_t>(created + 1)),
                            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    }
    if (thread == nullptr) {
      renodx::utils::log::w("[Total War] Worker pool: a worker thread could not start; the users run alone.");
      failed_target = target;
      break;
    }
    SetThreadDescription(thread, L"RenoDX worker");
    SetThreadPriority(thread, GetThreadPriority(GetCurrentThread()));
    pool[created].thread = thread;
    given_generation = ~0u;
  }
  ready.store(std::min(created, target), std::memory_order_relaxed);

  // Keep Render Core Free's sets (none: cleared), copied only when their generation changed. Nothing else places the threads.
  if (created > 0 && cpu_sets::AppliedGeneration() != given_generation) {
    uint32_t applied_generation = 0;
    const std::vector<ULONG> sets = cpu_sets::AppliedWorkerSets(&applied_generation);
    for (uint32_t i = 0; i < created; ++i) {
      SetThreadSelectedCpuSets(pool[i].thread, (sets.empty() ? nullptr : sets.data()), static_cast<ULONG>(sets.size()));
    }
    given_generation = applied_generation;
  }
  busy.store(false, std::memory_order_release);
}

#else

template <typename Part>
inline JoinResult Run(const Job& job, uint32_t workers, Part&& part) {
  part();
  if (job.join == JoinRule::ALL) {
    for (uint32_t worker = 1; worker <= workers; ++worker) {
      job.work(job.context, worker);
    }
  }
  return {};
}
inline uint32_t Available(User) { return 0; }
inline bool Start(const Job&, uint32_t) { return false; }
inline JoinResult Join() { return {}; }
inline void Update(const Settings&) {}
inline void Stop() {}
inline void ArmTrace(uint32_t) {}
inline bool Tracing() { return false; }

#endif

}  // namespace twattila::worker_pool
