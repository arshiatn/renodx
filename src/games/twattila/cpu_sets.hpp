/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Keep Render Core Free (Experimental, off; addon.cpp's row): the game's other threads kept off the CPU core the render
// thread runs on, so the core's second logical CPU (its SMT sibling) is free for it. Scheduling only: nothing the game
// computes changes, replays stay the same. Only SetThreadSelectedCpuSets on the game's own threads, always inside the CPUs
// the game may use: its process default CPU sets (Windows' or the player's own rule, such as a CPU set tool's), else its
// affinity. The process default, affinity masks and threads with their own rules are never changed. The game's own
// threads: the render thread and the threads that start in the game's modules (the host names them); the driver's,
// audio, overlays' and Windows' pool threads are left alone (an XAudio2 crash 11 s after an apply that moved them all).
#pragma once

#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/log.hpp"

namespace twattila::cpu_sets {

// One logical CPU of GetSystemCpuSetInformation (CoreIndex is the same for a core's logical CPUs).
struct CpuSet {
  ULONG id = 0;
  WORD group = 0;
  BYTE logical = 0;
  BYTE core = 0;
  BYTE efficiency = 0;
  // LastLevelCacheIndex (one per CCD on a Ryzen with two).
  BYTE cache = 0;
  // False when Windows gave it to other processes alone.
  bool available = true;
};

struct Plan {
  std::vector<ULONG> render_sets;
  std::vector<ULONG> worker_sets;
  BYTE render_core = 0;
  // Why nothing is applied; empty: apply.
  std::string_view stand_down;
};

// Presents of the render thread counted for its core.
constexpr uint32_t SAMPLE_PRESENTS = 64;

// The render thread gets its core's logical CPUs inside the base, every other thread the rest of the base. The base: the
// available CPUs in `default_sets` (empty: all) and in `affinity` (bit = logical CPU). `runs`: the render thread's presents
// on each logical CPU of the group; its core is the one with the most (ties: the lowest core index) among the cores with
// a second logical CPU in the base, core 0 (CoreIndex 0: most of the PC's interrupts) only when no other has one.
inline Plan PlanSets(std::span<const CpuSet> system, std::span<const ULONG> default_sets, uint64_t affinity,
                     std::span<const uint32_t, 64> runs) {
  Plan plan;
  if (system.empty()) {
    plan.stand_down = "no CPU set information from Windows";
    return plan;
  }
  if (std::ranges::any_of(system, [&](const CpuSet& cpu) { return cpu.group != system.front().group; })) {
    plan.stand_down = "more than one processor group";
    return plan;
  }
  std::vector<CpuSet> base;
  for (const CpuSet& cpu : system) {
    const bool in_default = default_sets.empty() || std::ranges::find(default_sets, cpu.id) != default_sets.end();
    if (cpu.available && in_default && cpu.logical < 64 && ((affinity >> cpu.logical) & 1) != 0) {
      base.push_back(cpu);
    }
  }
  if (base.empty()) {
    plan.stand_down = "no CPU inside the game's CPU set and affinity";
    return plan;
  }
  if (std::ranges::any_of(base, [&](const CpuSet& cpu) { return cpu.efficiency != base.front().efficiency; })) {
    plan.stand_down = "the game's CPU cores are of different kinds (efficiency classes)";
    return plan;
  }
  // Two CCDs: the render core's pick would be frozen on one of them (a CPU set of one CCD lets this apply).
  if (std::ranges::any_of(base, [&](const CpuSet& cpu) { return cpu.cache != base.front().cache; })) {
    plan.stand_down = "the game's CPUs span more than one L3 cache (CCD)";
    return plan;
  }
  std::array<uint32_t, 256> core_runs = {};
  std::array<uint32_t, 256> core_cpus = {};
  int cores = 0;
  uint32_t base_runs = 0;
  for (const CpuSet& cpu : base) {
    if (core_cpus[cpu.core]++ == 0) {
      ++cores;
    }
    core_runs[cpu.core] += runs[cpu.logical];
    base_runs += runs[cpu.logical];
  }
  if (cores < 4) {
    plan.stand_down = "fewer than 4 CPU cores for the game";
    return plan;
  }
  if (base_runs == 0) {
    plan.stand_down = "the render thread ran on none of the game's CPUs";
    return plan;
  }
  int render_core = -1;
  for (int core = 1; core < static_cast<int>(core_cpus.size()); ++core) {
    if (core_cpus[core] >= 2 && (render_core < 0 || core_runs[core] > core_runs[render_core])) {
      render_core = core;
    }
  }
  if (render_core < 0 && core_cpus[0] >= 2) {
    render_core = 0;
  }
  if (render_core < 0) {
    plan.stand_down = "no second logical CPU (SMT) on the game's CPU cores";
    return plan;
  }
  plan.render_core = static_cast<BYTE>(render_core);
  for (const CpuSet& cpu : base) {
    if (cpu.core == plan.render_core) {
      plan.render_sets.push_back(cpu.id);
    } else {
      plan.worker_sets.push_back(cpu.id);
    }
  }
  std::ranges::sort(plan.render_sets);
  std::ranges::sort(plan.worker_sets);
  return plan;
}

// This PC's logical CPUs for the game's process; empty when Windows gives none.
inline std::vector<CpuSet> SystemCpuSets() {
  ULONG length = 0;
  GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
  // SYSTEM_CPU_SET_INFORMATION holds a DWORD64: 8-byte aligned storage.
  std::vector<uint64_t> buffer((length + 7) / 8);
  if (length == 0
      || GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()), length, &length, GetCurrentProcess(), 0) == FALSE) {
    return {};
  }
  std::vector<CpuSet> system;
  for (ULONG offset = 0; offset + sizeof(DWORD) * 2 <= length;) {
    const auto* entry = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(reinterpret_cast<const std::byte*>(buffer.data()) + offset);
    if (entry->Size == 0) break;
    if (entry->Type == CpuSetInformation) {
      system.push_back({
          .id = entry->CpuSet.Id,
          .group = entry->CpuSet.Group,
          .logical = entry->CpuSet.LogicalProcessorIndex,
          .core = entry->CpuSet.CoreIndex,
          .efficiency = entry->CpuSet.EfficiencyClass,
          .cache = entry->CpuSet.LastLevelCacheIndex,
          .available = entry->CpuSet.Allocated == 0 || entry->CpuSet.AllocatedToTargetProcess != 0,
      });
    }
    offset += entry->Size;
  }
  return system;
}

// The CPU set IDs a process's or thread's Get*CpuSets call returns, sorted; nullopt when the call fails.
inline std::optional<std::vector<ULONG>> ReadSets(BOOL(WINAPI* read)(HANDLE, PULONG, ULONG, PULONG), HANDLE owner) {
  std::vector<ULONG> sets(64);
  ULONG count = 0;
  while (read(owner, sets.data(), static_cast<ULONG>(sets.size()), &count) == FALSE) {
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || count <= sets.size()) return std::nullopt;
    sets.resize(count);
  }
  sets.resize(count);
  std::ranges::sort(sets);
  return sets;
}

// Milliseconds between the worker's reads of the game's CPU set and affinity (the tests' shorter): a new rule of the
// player's wins within one. Every CHECK_EVERY-th read is a check (the render thread, a plan, new threads).
static DWORD tick_ms = 1000;
constexpr int CHECK_EVERY = 5;
constexpr int SNAPSHOT_EVERY = 6;

// A thread's start address: Windows has no Win32 call for it, only this documented read-only query of ntdll
// (ThreadQuerySetWin32StartAddress); nullptr (or the tests' seam): the game's threads can't be told apart, it stands down.
constexpr ULONG THREAD_QUERY_SET_WIN32_START_ADDRESS = 9;
static auto* query_thread_information = reinterpret_cast<LONG(WINAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*)>(
    GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
// The game's modules as GetModuleHandleW takes them (nullptr: the exe), set by Update when the worker starts.
static std::span<const wchar_t* const> game_module_names;
constexpr std::string_view NOT_THE_GAMES = "is not the game's";

// One hand-off without a lock: while samples < SAMPLE_PRESENTS only the presenting thread (Update) writes runs and
// render_thread; at SAMPLE_PRESENTS only the worker reads them, and only it sets samples back to 0 (release / acquire on
// samples orders the rest).
static std::array<uint32_t, 64> runs = {};
static std::atomic<uint32_t> samples = 0;
static std::atomic<DWORD> render_thread = 0;
// Since the worker's last check: any present, a present of the measured render thread.
static std::atomic<bool> presented = false;
static std::atomic<bool> render_thread_presented = false;

// A thread given CPU sets and the sets given: only one still holding exactly these is cleared (someone else's rule of the
// very same CPUs, set after ours, is cleared with it). The open handle keeps the thread's ID from being reused.
struct Given {
  DWORD id = 0;
  HANDLE thread = nullptr;
  std::vector<ULONG> sets;
};

// The worker: the game's CPU set and affinity every tick_ms and a check every CHECK_EVERY-th (the render thread, a plan,
// new threads) until `stop`, then ours cleared.
static DWORD WINAPI WorkerLoop(void* stop) {
  std::vector<Given> given;
  bool applied = false;
  bool on_logged = false;
  Plan applied_plan;
  std::vector<ULONG> applied_default;
  uint64_t applied_affinity = 0;
  std::string applied_cpus;
  // The base at the check before: the first apply waits until it holds for two checks in a row (a CPU set tool may set the
  // game's rule a moment after its start).
  std::optional<std::vector<ULONG>> previous_default;
  uint64_t previous_affinity = 0;
  std::string last_note;
  int ticks = 0;
  int checks_since_apply = 0;
  // The first thread snapshot after an apply found none of the game's threads to move.
  bool nothing_to_move = false;

  // Exactly ours: a thread whose sets someone changed since keeps theirs. How many were reset.
  const auto clear_given = [&given] {
    int reset = 0;
    for (const Given& thread : given) {
      if (thread.thread == nullptr) continue;
      if (WaitForSingleObject(thread.thread, 0) == WAIT_TIMEOUT && ReadSets(GetThreadSelectedCpuSets, thread.thread) == thread.sets
          && SetThreadSelectedCpuSets(thread.thread, nullptr, 0) != FALSE) {
        ++reset;
      }
      CloseHandle(thread.thread);
    }
    given.clear();
    return reset;
  };
  const auto note = [&last_note](std::string reason) {
    if (reason == last_note) return;
    renodx::utils::log::i(std::format("[Total War] Keep Render Core Free: stands down: {}.", reason));
    last_note = std::move(reason);
  };
  // Only while samples is SAMPLE_PRESENTS or the render thread is out of Update: Update leaves runs alone until samples is 0.
  const auto measure_again = [] {
    runs.fill(0);
    render_thread.store(0, std::memory_order_relaxed);
    samples.store(0, std::memory_order_release);
  };
  const auto stand_down_applied = [&](std::string_view change) {
    clear_given();
    applied = false;
    last_note.clear();
    measure_again();
    renodx::utils::log::i(std::format("[Total War] Keep Render Core Free: stands down: {}; ours cleared, measuring again.", change));
  };
  // `sets` given to the thread unless it ended, is another process's (a reused ID), is not the game's (when `game_modules`
  // is given: started outside them, or a start that can't be read; this worker is moved as the game's, as in the tests) or
  // has its own rule (selected CPU sets not ours, or an affinity other than the process's); empty when given, else why
  // not. Recorded before the call: an exception leaves no set unrecorded and no handle open.
  const auto give = [&given](DWORD thread_id, const std::vector<ULONG>& sets, DWORD_PTR process_mask,
                             const std::vector<HMODULE>* game_modules) -> std::string_view {
    given.push_back({.id = thread_id, .sets = sets});
    const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, thread_id);
    given.back().thread = thread;
    std::string_view refused;
    GROUP_AFFINITY group_affinity = {};
    void* start = nullptr;
    HMODULE start_module = nullptr;
    if (thread == nullptr || GetProcessIdOfThread(thread) != GetCurrentProcessId() || WaitForSingleObject(thread, 0) != WAIT_TIMEOUT) {
      refused = "has ended";
    } else if (game_modules != nullptr && thread_id != GetCurrentThreadId()
               && (query_thread_information == nullptr
                   || query_thread_information(thread, THREAD_QUERY_SET_WIN32_START_ADDRESS, &start, sizeof(start), nullptr) < 0
                   || GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                         static_cast<LPCWSTR>(start), &start_module)
                          == FALSE
                   || std::ranges::find(*game_modules, start_module) == game_modules->end())) {
      refused = NOT_THE_GAMES;
    } else if (const auto selected = ReadSets(GetThreadSelectedCpuSets, thread);
               !selected || !selected->empty() || GetThreadGroupAffinity(thread, &group_affinity) == FALSE || group_affinity.Mask != process_mask) {
      refused = "has its own CPU rule";
    } else if (SetThreadSelectedCpuSets(thread, sets.data(), static_cast<ULONG>(sets.size())) == FALSE) {
      refused = "was refused its CPUs by Windows";
    }
    if (!refused.empty()) {
      if (thread != nullptr) {
        CloseHandle(thread);
      }
      given.pop_back();
    }
    return refused;
  };

  while (WaitForSingleObject(stop, tick_ms) == WAIT_TIMEOUT) {
    try {
      DWORD_PTR process_mask = 0;
      DWORD_PTR system_mask = 0;
      const auto default_sets = ReadSets(GetProcessDefaultCpuSets, GetCurrentProcess());
      if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) == FALSE || !default_sets) continue;
      // A 32-bit process's mask covers 32 logical CPUs: one nobody narrowed stands for all of them.
      const uint64_t affinity = (process_mask == system_mask ? ~0ull : static_cast<uint64_t>(process_mask));
      // Every tick: a new rule (the player's or Windows') clears ours at once.
      if (applied && (*default_sets != applied_default || affinity != applied_affinity)) {
        stand_down_applied("the game's CPU set or affinity changed");
        continue;
      }
      if (++ticks % CHECK_EVERY != 0) continue;
      const bool any_present = presented.exchange(false, std::memory_order_relaxed);
      const bool render_present = render_thread_presented.exchange(false, std::memory_order_relaxed);
      const bool stable = (previous_default == default_sets && previous_affinity == affinity);
      previous_default = default_sets;
      previous_affinity = affinity;

      if (applied) {
        if (any_present && !render_present) {
          stand_down_applied("the game presents from another thread");
          continue;
        }
      } else {
        // Never every thread instead (the driver's, audio's).
        if (query_thread_information == nullptr || game_module_names.empty()) {
          note("cannot tell the game's threads apart");
          continue;
        }
        if (nothing_to_move) continue;
        if (samples.load(std::memory_order_acquire) < SAMPLE_PRESENTS) continue;
        // Presents counted under another base, or a plan that stands down: measured again (the reason may pass, such as
        // a render thread counted outside a CPU set changed since).
        if (!stable) {
          measure_again();
          continue;
        }
        const std::vector<CpuSet> system = SystemCpuSets();
        Plan plan = PlanSets(system, *default_sets, affinity, runs);
        if (!plan.stand_down.empty()) {
          note(std::string(plan.stand_down));
          measure_again();
          continue;
        }
        // Only read while applied: filled before the render thread's sets, so nothing after them can throw.
        applied_cpus.clear();
        for (const CpuSet& cpu : system) {
          if (std::ranges::binary_search(plan.render_sets, cpu.id)) {
            applied_cpus += std::format("{}{}", (applied_cpus.empty() ? "" : ", "), cpu.logical);
          }
        }
        applied_default = *default_sets;
        // The render thread first (wherever it started): without its sets nobody moves. Measured again when it can't
        // have them: it may have ended (a loading screen's thread) or have its own rule.
        if (const std::string_view refused = give(render_thread.load(std::memory_order_relaxed), plan.render_sets, process_mask, nullptr);
            !refused.empty()) {
          note(std::format("the render thread {}", refused));
          measure_again();
          continue;
        }
        applied = true;
        on_logged = false;
        applied_plan = std::move(plan);
        applied_affinity = affinity;
        last_note.clear();
        checks_since_apply = 0;
      }

      // Threads not given sets yet (new ones too) get theirs unless they have their own rule; ended ones let go. A thread
      // snapshot lists every thread of the PC (tens of ms of CPU with thousands): each check for the first SNAPSHOT_EVERY
      // after an apply (the game's start-up threads), then every SNAPSHOT_EVERY-th.
      if (++checks_since_apply > SNAPSHOT_EVERY && checks_since_apply % SNAPSHOT_EVERY != 1) continue;
      std::erase_if(given, [](const Given& thread) {
        if (thread.thread == nullptr || WaitForSingleObject(thread.thread, 0) != WAIT_OBJECT_0) return false;
        CloseHandle(thread.thread);
        return true;
      });
      const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      if (snapshot == INVALID_HANDLE_VALUE) continue;
      const std::unique_ptr<void, decltype(&CloseHandle)> snapshot_closer(snapshot, &CloseHandle);
      std::vector<HMODULE> game_modules;
      for (const wchar_t* name : game_module_names) {
        if (const HMODULE module = GetModuleHandleW(name); module != nullptr) {
          game_modules.push_back(module);
        }
      }
      const DWORD process = GetCurrentProcessId();
      const DWORD self = GetCurrentThreadId();
      int others = 0;
      int left_alone = 0;
      int not_games = 0;
      THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
      for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process) continue;
        if (std::ranges::any_of(given, [&entry](const Given& thread) { return thread.id == entry.th32ThreadID; })) continue;
        const std::string_view refused = give(entry.th32ThreadID, applied_plan.worker_sets, process_mask, &game_modules);
        if (refused == NOT_THE_GAMES) {
          ++not_games;
        } else if (!refused.empty()) {
          ++left_alone;
        } else if (entry.th32ThreadID != self) {
          // This worker is moved too, but isn't one of the game's threads the render core is kept free of.
          ++others;
        }
      }
      if (!on_logged) {
        // Only the render thread on its core with nobody moved off it: worse than off. Stays down until the row is off.
        if (others == 0) {
          clear_given();
          applied = false;
          nothing_to_move = true;
          note("none of the game's threads to move off the render core; ours cleared until the row is off and on again");
          continue;
        }
        on_logged = true;
        renodx::utils::log::i(std::format(
            "[Total War] Keep Render Core Free: on: the render thread (thread {}) on logical CPUs {} (one core), {} of the game's "
            "threads on the other {} logical CPUs of {}, {} left alone (their own CPU rules), {} not the game's.",
            render_thread.load(std::memory_order_relaxed), applied_cpus, others, applied_plan.worker_sets.size(),
            (default_sets->empty() ? "the game's affinity" : "the game's CPU set"), left_alone, not_games));
      }
    } catch (...) {
      // No-op: the next check goes on from here.
    }
  }
  try {
    const int reset = clear_given();
    if (applied) {
      renodx::utils::log::i(std::format("[Total War] Keep Render Core Free: off: ours cleared ({} threads).", reset));
    }
  } catch (...) {
    // No-op: only a failed allocation; what is left stays until the thread ends.
  }
  measure_again();
  return 0;
}

static HANDLE worker = nullptr;
static HANDLE worker_stop = nullptr;
// The worker could not start: tried again only after the row was off.
static bool start_failed = false;

// The row's off and the addon's detach: the worker ends and clears ours. Outside DllMain (it waits for the worker).
inline void Stop() {
  if (worker == nullptr) return;
  SetEvent(worker_stop);
  WaitForSingleObject(worker, INFINITE);
  CloseHandle(worker);
  CloseHandle(worker_stop);
  worker = nullptr;
  worker_stop = nullptr;
}

// Every present, on the thread that presents (the render thread): `enabled` is the row, `game_modules` where the game's
// own threads start (GetModuleHandleW's names, nullptr: the exe; taken when the worker starts, not copied: the array and
// its names must outlive the worker, such as a static array of literals; empty: it stands down). On: the worker runs, and
// SAMPLE_PRESENTS presents in a row of one thread count where it runs; off: only this branch.
inline void Update(bool enabled, std::span<const wchar_t* const> game_modules) {
  if (!enabled) {
    start_failed = false;
    Stop();
    return;
  }
  if (start_failed) return;
  if (worker == nullptr) {
    game_module_names = game_modules;
    worker_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (worker_stop != nullptr) {
      worker = CreateThread(nullptr, 0, WorkerLoop, worker_stop, 0, nullptr);
    }
    if (worker == nullptr) {
      if (worker_stop != nullptr) {
        CloseHandle(worker_stop);
      }
      worker_stop = nullptr;
      start_failed = true;
      renodx::utils::log::w("[Total War] Keep Render Core Free: stands down: its thread could not start.");
      return;
    }
    SetThreadDescription(worker, L"RenoDX CPU sets");
  }
  const DWORD thread = GetCurrentThreadId();
  presented.store(true, std::memory_order_relaxed);
  if (const uint32_t sampled = samples.load(std::memory_order_acquire); sampled < SAMPLE_PRESENTS) {
    // A present from another thread starts the count again on it: a thread that stops presenting (a loading screen's)
    // never holds it. Two threads presenting in turn never finish it (no single render thread: nothing applies).
    uint32_t counted = sampled;
    if (sampled == 0 || thread != render_thread.load(std::memory_order_relaxed)) {
      runs.fill(0);
      render_thread.store(thread, std::memory_order_relaxed);
      counted = 0;
    }
    PROCESSOR_NUMBER processor = {};
    GetCurrentProcessorNumberEx(&processor);
    if (processor.Group == 0 && processor.Number < runs.size()) {
      ++runs[processor.Number];
    }
    samples.store(counted + 1, std::memory_order_release);
  } else if (thread == render_thread.load(std::memory_order_relaxed)) {
    render_thread_presented.store(true, std::memory_order_relaxed);
  }
}

}  // namespace twattila::cpu_sets
