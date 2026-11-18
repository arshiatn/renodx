/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Keep Render Core Free (Experimental, off; addon.cpp's row): the game's other threads kept off the CPU core the render
// thread runs on, so the core's second logical CPU (its SMT sibling) is free for it. Scheduling only: nothing the game
// computes changes, replays stay the same. Only SetThreadSelectedCpuSets on the game's own threads, always inside the CPUs
// the game may use: its process default CPU sets (Windows' or the player's own rule, such as a CPU set tool's), else its
// affinity. The process default, affinity masks and threads with their own rules are never changed.
#pragma once

#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
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
// on each logical CPU of the group; its core is the one with the most (ties: the lowest core index).
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
  std::array<uint32_t, 256> core_runs = {};
  std::array<uint32_t, 256> core_cpus = {};
  int cores = 0;
  for (const CpuSet& cpu : base) {
    if (core_cpus[cpu.core]++ == 0) ++cores;
    core_runs[cpu.core] += runs[cpu.logical];
  }
  if (cores < 4) {
    plan.stand_down = "fewer than 4 CPU cores for the game";
    return plan;
  }
  const auto most_runs = std::ranges::max_element(core_runs);
  if (*most_runs == 0) {
    plan.stand_down = "the render thread ran on none of the game's CPUs";
    return plan;
  }
  plan.render_core = static_cast<BYTE>(most_runs - core_runs.begin());
  if (core_cpus[plan.render_core] < 2) {
    plan.stand_down = "no second logical CPU (SMT) on the render thread's core";
    return plan;
  }
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

// Milliseconds between the worker's checks (the tests' shorter).
static DWORD check_ms = 5000;
constexpr int SNAPSHOT_EVERY = 6;

// Written by the render thread (Update) until SAMPLE_PRESENTS, then read by the worker: `samples` hands `runs` over
// (release / acquire), and only the worker sets it back to 0.
static std::array<uint32_t, 64> runs = {};
static std::atomic<uint32_t> samples = 0;
static std::atomic<DWORD> render_thread = 0;
// Since the worker's last check: any present, a present of the measured render thread.
static std::atomic<bool> presented = false;
static std::atomic<bool> render_thread_presented = false;

// A thread given CPU sets and the sets given: only one still holding exactly these is cleared. The open handle keeps the
// thread's ID from being reused.
struct Given {
  DWORD id = 0;
  HANDLE thread = nullptr;
  std::vector<ULONG> sets;
};

// The worker: a check every check_ms until `stop` (the base, the render thread, new threads), then ours cleared.
static DWORD WINAPI WorkerLoop(void* stop) {
  std::vector<Given> given;
  bool applied = false;
  Plan applied_plan;
  std::vector<ULONG> applied_default;
  uint64_t applied_affinity = 0;
  // The base at the check before: the first apply waits until it holds for two checks in a row (a CPU set tool may set the
  // game's rule a moment after its start).
  std::optional<std::vector<ULONG>> previous_default;
  uint64_t previous_affinity = 0;
  std::string_view last_note;
  int checks_since_apply = 0;

  // Exactly ours: a thread whose sets someone changed since keeps theirs.
  const auto clear_given = [&given] {
    for (const Given& thread : given) {
      if (WaitForSingleObject(thread.thread, 0) == WAIT_TIMEOUT && ReadSets(GetThreadSelectedCpuSets, thread.thread) == thread.sets) {
        SetThreadSelectedCpuSets(thread.thread, nullptr, 0);
      }
      CloseHandle(thread.thread);
    }
    given.clear();
  };
  const auto note = [&last_note](std::string_view reason) {
    if (reason == last_note) return;
    last_note = reason;
    renodx::utils::log::i(std::format("[Total War] Keep Render Core Free: stands down: {}.", reason));
  };
  // Only while samples is SAMPLE_PRESENTS or the render thread is out of Update: Update leaves runs alone until samples is 0.
  const auto measure_again = [] {
    runs.fill(0);
    render_thread.store(0, std::memory_order_relaxed);
    samples.store(0, std::memory_order_release);
  };

  while (WaitForSingleObject(stop, check_ms) == WAIT_TIMEOUT) {
    try {
      DWORD_PTR process_mask = 0;
      DWORD_PTR system_mask = 0;
      const auto default_sets = ReadSets(GetProcessDefaultCpuSets, GetCurrentProcess());
      if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) == FALSE || !default_sets) continue;
      // A 32-bit process's mask covers 32 logical CPUs: one nobody narrowed stands for all of them.
      const uint64_t affinity = (process_mask == system_mask ? ~0ull : static_cast<uint64_t>(process_mask));
      const bool any_present = presented.exchange(false, std::memory_order_relaxed);
      const bool render_present = render_thread_presented.exchange(false, std::memory_order_relaxed);
      const bool stable = (previous_default == default_sets && previous_affinity == affinity);
      previous_default = default_sets;
      previous_affinity = affinity;

      // A thread's own rule: selected CPU sets not given by us, or an affinity other than the process's.
      const auto own_rule = [process_mask](HANDLE thread) {
        GROUP_AFFINITY group_affinity = {};
        const auto selected = ReadSets(GetThreadSelectedCpuSets, thread);
        return !selected || !selected->empty() || GetThreadGroupAffinity(thread, &group_affinity) == FALSE
               || group_affinity.Mask != process_mask;
      };

      const bool first_apply = !applied;
      if (applied) {
        std::string_view change;
        if (*default_sets != applied_default || affinity != applied_affinity) {
          change = "the game's CPU set or affinity changed";
        } else if (any_present && !render_present) {
          change = "the game presents from another thread";
        }
        if (!change.empty()) {
          clear_given();
          applied = false;
          last_note = {};
          measure_again();
          renodx::utils::log::i(std::format("[Total War] Keep Render Core Free: stands down: {}; ours cleared, measuring again.", change));
          continue;
        }
      } else {
        if (samples.load(std::memory_order_acquire) < SAMPLE_PRESENTS) continue;
        // Presents counted under another base, or a plan that stands down: measured again (the reason may pass, such as
        // a render thread counted outside a CPU set changed since).
        if (!stable) {
          measure_again();
          continue;
        }
        Plan plan = PlanSets(SystemCpuSets(), *default_sets, affinity, runs);
        if (!plan.stand_down.empty()) {
          note(plan.stand_down);
          measure_again();
          continue;
        }
        const HANDLE render = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, render_thread.load(std::memory_order_relaxed));
        if (render == nullptr) continue;
        const bool render_own_rule = own_rule(render);
        CloseHandle(render);
        if (render_own_rule) {
          note("the render thread has its own CPU rule");
          continue;
        }
        applied = true;
        applied_plan = std::move(plan);
        applied_default = *default_sets;
        applied_affinity = affinity;
        last_note = {};
        checks_since_apply = 0;
      }

      // Threads not given sets yet (new ones too) get theirs unless they have their own rule; ended ones let go. A thread
      // snapshot lists every thread of the PC (tens of ms of CPU with thousands): each check for the first SNAPSHOT_EVERY
      // after an apply (the game's start-up threads), then every SNAPSHOT_EVERY-th.
      if (++checks_since_apply > SNAPSHOT_EVERY && checks_since_apply % SNAPSHOT_EVERY != 1) continue;
      std::erase_if(given, [](const Given& thread) {
        if (WaitForSingleObject(thread.thread, 0) != WAIT_OBJECT_0) return false;
        CloseHandle(thread.thread);
        return true;
      });
      const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      if (snapshot == INVALID_HANDLE_VALUE) continue;
      const DWORD process = GetCurrentProcessId();
      const DWORD render_id = render_thread.load(std::memory_order_relaxed);
      int others = 0;
      int left_alone = 0;
      THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
      for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process) continue;
        if (std::ranges::any_of(given, [&entry](const Given& thread) { return thread.id == entry.th32ThreadID; })) continue;
        const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ThreadID);
        if (thread == nullptr) continue;
        const std::vector<ULONG>& sets = (entry.th32ThreadID == render_id ? applied_plan.render_sets : applied_plan.worker_sets);
        if (own_rule(thread) || SetThreadSelectedCpuSets(thread, sets.data(), static_cast<ULONG>(sets.size())) == FALSE) {
          ++left_alone;
          CloseHandle(thread);
          continue;
        }
        given.push_back({.id = entry.th32ThreadID, .thread = thread, .sets = sets});
        if (entry.th32ThreadID != render_id) ++others;
      }
      CloseHandle(snapshot);
      if (first_apply) {
        std::string render_cpus;
        for (const CpuSet& cpu : SystemCpuSets()) {
          if (std::ranges::binary_search(applied_plan.render_sets, cpu.id)) {
            render_cpus += std::format("{}{}", (render_cpus.empty() ? "" : ", "), cpu.logical);
          }
        }
        renodx::utils::log::i(std::format(
            "[Total War] Keep Render Core Free: on: the render thread (thread {}) on CPU core {} (logical CPUs {}), {} other threads on "
            "the other {} logical CPUs of {}, {} left alone (their own CPU rules).",
            render_id, applied_plan.render_core, render_cpus, others, applied_plan.worker_sets.size(),
            (default_sets->empty() ? "the game's affinity" : "the game's CPU set"), left_alone));
      }
    } catch (...) {
      // No-op: the next check goes on from here.
    }
  }
  if (applied) {
    renodx::utils::log::i(std::format("[Total War] Keep Render Core Free: off: ours cleared ({} threads).", given.size()));
  }
  clear_given();
  measure_again();
  return 0;
}

static HANDLE worker = nullptr;
static HANDLE worker_stop = nullptr;

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

// Every present, on the thread that presents (the render thread): `enabled` is the row. On: the worker runs, and the
// first SAMPLE_PRESENTS presents of one thread count where it runs; off: only this branch.
inline void Update(bool enabled) {
  if (!enabled) {
    Stop();
    return;
  }
  if (worker == nullptr) {
    worker_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (worker_stop == nullptr) return;
    worker = CreateThread(nullptr, 0, WorkerLoop, worker_stop, 0, nullptr);
    if (worker == nullptr) {
      CloseHandle(worker_stop);
      worker_stop = nullptr;
      return;
    }
  }
  const DWORD thread = GetCurrentThreadId();
  presented.store(true, std::memory_order_relaxed);
  if (const uint32_t sampled = samples.load(std::memory_order_acquire); sampled < SAMPLE_PRESENTS) {
    if (sampled == 0) render_thread.store(thread, std::memory_order_relaxed);
    if (thread != render_thread.load(std::memory_order_relaxed)) return;
    PROCESSOR_NUMBER processor = {};
    GetCurrentProcessorNumberEx(&processor);
    if (processor.Group == 0 && processor.Number < runs.size()) ++runs[processor.Number];
    samples.store(sampled + 1, std::memory_order_release);
  } else if (thread == render_thread.load(std::memory_order_relaxed)) {
    render_thread_presented.store(true, std::memory_order_relaxed);
  }
}

}  // namespace twattila::cpu_sets
