/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The battle core (battle_core.hpp) without a game: the pick on synthetic CPU tables, and the live cases on this
// process's own CPU sets with its own "step" threads (reserve, the step's thread moving, threads with a rule of their
// own, the row's bit turned off and on, the watchdog's release, another program's change, the states that stay).
// The live cases need a machine the pick accepts (8 cores with two logical CPUs each); elsewhere they are left out and
// the last line says so.
// battle_core_test.exe; exit code = failures.

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "../battle_core.hpp"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::test;

inline constexpr DWORD MEGABYTE = 1u << 20;
inline constexpr DWORD ERROR_MARKER = 0x1234;  // each step thread's last error before the step

// ---- synthetic tables ----

using Table = std::vector<SYSTEM_CPU_SET_INFORMATION>;

ULONG IdOf(uint32_t logical) { return 0x100 + logical; }

// `cores` cores from `first_logical` on, each with `threads` logical CPUs; a core's index is its first logical CPU's
// (0, 2, 4 ... with two each, as Windows numbers them).
void AddCores(Table* table, uint8_t first_logical, uint8_t cores, uint8_t threads, uint8_t cache, uint8_t efficiency) {
  for (uint8_t core = 0; core < cores; ++core) {
    for (uint8_t thread = 0; thread < threads; ++thread) {
      SYSTEM_CPU_SET_INFORMATION entry = {};
      entry.Size = sizeof(entry);
      entry.Type = CpuSetInformation;
      entry.CpuSet.LogicalProcessorIndex = static_cast<BYTE>(first_logical + (core * threads) + thread);
      entry.CpuSet.Id = IdOf(entry.CpuSet.LogicalProcessorIndex);
      entry.CpuSet.CoreIndex = static_cast<BYTE>(first_logical + (core * threads));
      entry.CpuSet.LastLevelCacheIndex = cache;
      entry.CpuSet.EfficiencyClass = efficiency;
      table->push_back(entry);
    }
  }
}

// Two 8-core halves with a level-3 cache each (megabytes).
Table TwoCaches() {
  Table table;
  AddCores(&table, 0, 8, 2, 0, 0);
  AddCores(&table, 16, 8, 2, 16, 0);
  return table;
}
std::vector<battle_core::L3Cache> TwoCacheSizes(DWORD first_mb, DWORD second_mb) {
  return {{.logical_cpus = 0xffff, .bytes = first_mb * MEGABYTE}, {.logical_cpus = 0xffff0000, .bytes = second_mb * MEGABYTE}};
}
// 8 performance cores with two logical CPUs (or one), then 16 one-CPU efficiency cores; one cache.
Table Hybrid(uint8_t performance_threads, uint8_t performance_class) {
  Table table;
  AddCores(&table, 0, 8, performance_threads, 0, performance_class);
  AddCores(&table, static_cast<uint8_t>(8 * performance_threads), 16, 1, 0, 0);
  return table;
}
Table OneCache(uint8_t cores, uint8_t threads) {
  Table table;
  AddCores(&table, 0, cores, threads, 0, 0);
  return table;
}
Table With(Table table, uint64_t parked_logical, uint64_t other_group_logical) {
  for (SYSTEM_CPU_SET_INFORMATION& entry : table) {
    const uint64_t bit = 1ull << entry.CpuSet.LogicalProcessorIndex;
    entry.CpuSet.Parked = ((parked_logical & bit) != 0) ? 1 : 0;
    entry.CpuSet.Group = ((other_group_logical & bit) != 0) ? 1 : 0;
  }
  return table;
}

struct PickCase {
  const char* name;
  Table table;
  uint64_t allowed_logical;  // the CPUs the game may use
  std::vector<battle_core::L3Cache> l3;
  CoreState state;
  uint8_t logical_a = 0;
  uint8_t logical_b = 0;
  uint8_t cores = 0;
  uint32_t l3_mb = 0;
};

// ---- live cases ----

Block shared;
LoaderApi api;

struct StepThread {
  HANDLE go = CreateSemaphoreW(nullptr, 0, 64, nullptr);
  HANDLE done = CreateSemaphoreW(nullptr, 0, 64, nullptr);
  std::atomic_bool quit = false;
  std::atomic<DWORD> error_after = 0;
  std::thread thread{[this] {
    while (WaitForSingleObject(go, INFINITE) == WAIT_OBJECT_0 && !quit.load()) {
      SetLastError(ERROR_MARKER);
      battle_core::OnStepStart(&shared, api);
      error_after = GetLastError();
      ReleaseSemaphore(done, 1, nullptr);
    }
  }};

  // One battle step's start on this thread; true when the thread's last error came through unchanged.
  bool Step() {
    ReleaseSemaphore(go, 1, nullptr);
    WaitForSingleObject(done, INFINITE);
    return error_after.load() == ERROR_MARKER;
  }
};

std::vector<ULONG> Sorted(std::vector<ULONG> ids) {
  std::ranges::sort(ids);
  return ids;
}

std::vector<ULONG> ProcessDefault() {
  std::array<ULONG, 256> ids = {};
  ULONG count = 0;
  GetProcessDefaultCpuSets(GetCurrentProcess(), ids.data(), static_cast<ULONG>(ids.size()), &count);
  return Sorted({ids.begin(), ids.begin() + count});
}

void SetProcessDefault(const std::vector<ULONG>& ids) {
  SetProcessDefaultCpuSets(GetCurrentProcess(), (ids.empty() ? nullptr : ids.data()), static_cast<ULONG>(ids.size()));
}

std::vector<ULONG> Selected(HANDLE thread) {
  std::array<ULONG, 256> ids = {};
  ULONG count = 0;
  GetThreadSelectedCpuSets(thread, ids.data(), static_cast<ULONG>(ids.size()), &count);
  return Sorted({ids.begin(), ids.begin() + count});
}

uint32_t StatusField(uint32_t shift) { return static_cast<uint32_t>(shared.core_status.load() >> shift) & CORE_FIELD_MASK; }

bool WaitForState(CoreState wanted) {
  for (int tries = 0; tries < 300 && StatusField(CORE_STATE_SHIFT) != wanted; ++tries) {
    Sleep(10);
  }
  return StatusField(CORE_STATE_SHIFT) == wanted;
}

int LinesWith(const char* text) {
  return static_cast<int>(std::ranges::count_if(log_lines, [&](const std::string& line) { return line.find(text) != std::string::npos; }));
}

// The test's reset of a state that stays for the session.
void BackToWaiting() {
  const std::scoped_lock lock(battle_core::state.mutex);
  battle_core::state.core_state = CORE_WAITING;
  shared.core_status.store(uint64_t{CORE_WAITING} << CORE_STATE_SHIFT);
}

}  // namespace

int wmain() {
  // ---- the pick ----
  const uint64_t every = ~0ull;
  Table two_groups = OneCache(8, 2);
  Table too_many = OneCache(64, 1);
  too_many.push_back(too_many.back());
  const std::vector<battle_core::L3Cache> one_96 = {{.logical_cpus = 0xffff, .bytes = 96 * MEGABYTE}};
  const std::vector<PickCase> cases = {
      {"two caches 96 + 32 MB, a rule for logical 0-15", TwoCaches(), 0xffff, TwoCacheSizes(96, 32), CORE_ACTIVE, 14, 15, 8, 96},
      {"two caches 96 + 32 MB, no rule", TwoCaches(), every, TwoCacheSizes(96, 32), CORE_ACTIVE, 14, 15, 16, 96},
      {"two caches 32 + 32 MB, no rule: the first", TwoCaches(), every, TwoCacheSizes(32, 32), CORE_ACTIVE, 14, 15, 16, 32},
      {"two caches 32 + 96 MB: the bigger one", TwoCaches(), every, TwoCacheSizes(32, 96), CORE_ACTIVE, 30, 31, 16, 96},
      {"two equal caches, 6 whole cores in the first: the one with more", TwoCaches(), 0xffff0fff, TwoCacheSizes(32, 32),
       CORE_ACTIVE, 30, 31, 14, 32},
      {"two caches, no sizes read: equal, the first", TwoCaches(), every, {}, CORE_ACTIVE, 14, 15, 16, 0},
      {"two caches, the second parked", With(TwoCaches(), 0xffff0000, 0), every, TwoCacheSizes(96, 32), CORE_ACTIVE, 14, 15, 16, 96},
      {"the rule leaves logical 14 and 15 out: the next whole core", TwoCaches(), 0xffff3fff, TwoCacheSizes(96, 32),
       CORE_ACTIVE, 12, 13, 15, 96},
      {"the rule leaves logical 15 out: the next whole core", TwoCaches(), 0xffff7fff, TwoCacheSizes(96, 32), CORE_ACTIVE, 12,
       13, 15, 96},
      {"the last core parked: the next", With(TwoCaches(), 0xc000, 0), 0xffff, TwoCacheSizes(96, 32), CORE_ACTIVE, 12, 13, 8, 96},
      {"one of the last core's CPUs parked: the next", With(TwoCaches(), 0x8000, 0), 0xffff, TwoCacheSizes(96, 32),
       CORE_ACTIVE, 12, 13, 8, 96},
      {"the rule leaves a 7-core half: few cores", TwoCaches(), 0x3fff, TwoCacheSizes(96, 32), CORE_FEW_CORES},
      {"one cache, 8 cores", OneCache(8, 2), every, one_96, CORE_ACTIVE, 14, 15, 8, 96},
      {"6 cores", OneCache(6, 2), every, {}, CORE_FEW_CORES},
      {"4 cores", OneCache(4, 2), every, {}, CORE_FEW_CORES},
      {"8 performance cores (class 1) + 16 efficiency cores", Hybrid(2, 1), every, {}, CORE_ACTIVE, 14, 15, 8, 0},
      {"8 performance + 16 efficiency cores, one class reported", Hybrid(2, 0), every, {}, CORE_ACTIVE, 14, 15, 8, 0},
      {"performance cores with one logical CPU each", Hybrid(1, 1), every, {}, CORE_NO_SMT},
      {"8 cores, one logical CPU each", OneCache(8, 1), every, {}, CORE_NO_SMT},
      {"a rule for one logical CPU of each core", TwoCaches(), 0x55555555, TwoCacheSizes(96, 32), CORE_NO_SMT},
      {"the biggest cache without a whole core", TwoCaches(), 0xffff5555, TwoCacheSizes(96, 32), CORE_NO_SMT},
      {"nothing allowed", TwoCaches(), 0, TwoCacheSizes(96, 32), CORE_NO_SMT},
      {"a second processor group", With(two_groups, 0, 0xff00), every, {}, CORE_UNSUPPORTED},
      {"more than 64 logical CPUs", too_many, every, {}, CORE_UNSUPPORTED},
  };
  for (const PickCase& test : cases) {
    std::vector<ULONG> allowed_ids;
    uint8_t top_class = 0;
    for (const SYSTEM_CPU_SET_INFORMATION& entry : test.table) {
      if (((test.allowed_logical >> (entry.CpuSet.LogicalProcessorIndex % 64)) & 1) == 0) continue;
      allowed_ids.push_back(entry.CpuSet.Id);
      top_class = std::max(top_class, entry.CpuSet.EfficiencyClass);
    }
    const battle_core::Pick pick = battle_core::PickBattleCore(test.table, allowed_ids, test.l3);
    const bool as_expected = pick.state == test.state && pick.logical[0] == test.logical_a && pick.logical[1] == test.logical_b
                             && (test.state != CORE_ACTIVE || pick.cores == test.cores)
                             && pick.l3_mb == test.l3_mb;
    Expect(as_expected, test.name);
    if (!as_expected) {
      std::printf("  got state %u, logical %u + %u, cores %u, L3 %u MB\n", pick.state, pick.logical[0], pick.logical[1], pick.cores,
                  pick.l3_mb);
    }
    // Over every table: a picked core is two allowed logical CPUs, both of one core that has no third, of the highest
    // class allowed (never an efficiency core beside performance cores), not parked; without a pick, no ids.
    if (pick.state != CORE_ACTIVE) {
      Expect(pick.ids[0] == 0 && pick.ids[1] == 0, "pick: no ids without a core");
      continue;
    }
    const auto entry_of = [&](ULONG id) {
      return *std::ranges::find_if(test.table, [&](const SYSTEM_CPU_SET_INFORMATION& entry) { return entry.CpuSet.Id == id; });
    };
    const auto first = entry_of(pick.ids[0]).CpuSet;
    const auto second = entry_of(pick.ids[1]).CpuSet;
    const auto core_cpus = std::ranges::count_if(
        test.table, [&](const SYSTEM_CPU_SET_INFORMATION& entry) { return entry.CpuSet.CoreIndex == first.CoreIndex; });
    Expect(pick.ids[0] != pick.ids[1] && std::ranges::find(allowed_ids, pick.ids[0]) != allowed_ids.end()
               && std::ranges::find(allowed_ids, pick.ids[1]) != allowed_ids.end(),
           "pick: both CPUs are allowed");
    Expect(first.CoreIndex == second.CoreIndex && core_cpus == 2 && first.LogicalProcessorIndex == pick.logical[0]
               && second.LogicalProcessorIndex == pick.logical[1] && pick.logical[0] < pick.logical[1],
           "pick: one whole core with two logical CPUs");
    Expect(first.EfficiencyClass == top_class && second.EfficiencyClass == top_class && first.Parked == 0 && second.Parked == 0,
           "pick: the highest class allowed, not parked");
  }

  // ---- the live cases ----
  api = MakeApi("", 0);
  std::array<SYSTEM_CPU_SET_INFORMATION, 256> machine = {};
  ULONG machine_bytes = 0;
  DWORD_PTR found_affinity = 0;
  DWORD_PTR system_affinity = 0;
  const std::vector<ULONG> found_default = ProcessDefault();
  if (GetSystemCpuSetInformation(machine.data(), sizeof(machine), &machine_bytes, GetCurrentProcess(), 0) == FALSE
      || GetProcessAffinityMask(GetCurrentProcess(), &found_affinity, &system_affinity) == FALSE) {
    std::printf("FAIL: this machine's CPU sets could not be read (error %lu)\n", GetLastError());
    return failures + 1;
  }
  const std::span cpus = std::span(machine).first(machine_bytes / sizeof(SYSTEM_CPU_SET_INFORMATION));
  // Every CPU this process may use, the first logical CPU of each core, and the first four whole cores.
  std::vector<ULONG> every_id;
  std::vector<ULONG> one_per_core;
  std::vector<ULONG> four_cores;
  for (const SYSTEM_CPU_SET_INFORMATION& entry : cpus) {
    if (entry.CpuSet.Group != 0 || ((found_affinity >> entry.CpuSet.LogicalProcessorIndex) & 1) == 0) continue;
    every_id.push_back(entry.CpuSet.Id);
    if (entry.CpuSet.CoreIndex == entry.CpuSet.LogicalProcessorIndex) {
      one_per_core.push_back(entry.CpuSet.Id);
    }
    if (entry.CpuSet.CoreIndex < 8) {
      four_cores.push_back(entry.CpuSet.Id);
    }
  }
  std::ranges::sort(every_id);
  const auto logical_of = [&](ULONG id) {
    return uint32_t{std::ranges::find_if(cpus, [&](const SYSTEM_CPU_SET_INFORMATION& entry) { return entry.CpuSet.Id == id; })
                        ->CpuSet.LogicalProcessorIndex};
  };
  const auto without = [](std::vector<ULONG> ids, std::span<const ULONG> removed) {
    std::erase_if(ids, [&](ULONG id) { return std::ranges::find(removed, id) != removed.end(); });
    return ids;
  };

  battle_core::watchdog_ms = 50;
  battle_core::release_after_ms = 600000;
  std::array<StepThread, 3> threads;
  const auto holders = [&](std::span<const ULONG> ids) {
    return std::ranges::count_if(threads, [&](StepThread& thread) {
      return Selected(thread.thread.native_handle()) == Sorted({ids.begin(), ids.end()});
    });
  };
  const auto handle_of = [&](size_t index) { return static_cast<HANDLE>(threads[index].thread.native_handle()); };

  // The reserve, with a default of this process's own: every CPU it may use.
  SetProcessDefault(every_id);
  bool last_error_kept = threads[0].Step();
  const bool live = StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE;
  if (live) {
    const std::array<ULONG, 2> core = battle_core::state.battle_ids;
    const std::vector<ULONG> core_ids = {core.begin(), core.end()};
    const std::vector<ULONG> kept = without(every_id, core);
    Expect(ProcessDefault() == kept && kept.size() + 2 == every_id.size(), "reserve: the default is what it was minus the core");
    Expect(Selected(handle_of(0)) == Sorted(core_ids) && holders(core) == 1, "reserve: the step's thread holds the core");
    Expect(StatusField(CORE_LOGICAL_A_SHIFT) == logical_of(core[0]) && StatusField(CORE_LOGICAL_B_SHIFT) == logical_of(core[1])
               && StatusField(CORE_ALLOWED_SHIFT) >= battle_core::MIN_CORES && (shared.core_status.load() >> CORE_ERROR_SHIFT) == 0
               && shared.render_core_status.load() == 0,
           "reserve: the status has the core's logical CPUs and the cores; the render word is untouched");
    Expect(LinesWith("renodx_attila64: battle core: logical ") == 1 && LinesWith("allowed cores") == 1, "reserve: its line");

    // The step moves between threads: the previous one is cleared first.
    last_error_kept = threads[1].Step() && last_error_kept;
    Expect(Selected(handle_of(0)).empty() && Selected(handle_of(1)) == Sorted(core_ids) && holders(core) == 1,
           "thread: the step moved, the previous thread is cleared");
    last_error_kept = threads[1].Step() && last_error_kept;
    Expect(Selected(handle_of(1)) == Sorted(core_ids) && ProcessDefault() == kept, "thread: the same thread again, nothing changes");

    // A thread with selected CPU sets of its own: not selected, and its sets stay.
    const std::vector<ULONG> own_rule = {every_id.front()};
    SetThreadSelectedCpuSets(handle_of(2), own_rule.data(), 1);
    last_error_kept = threads[2].Step() && last_error_kept;
    Expect(Selected(handle_of(2)) == own_rule && Selected(handle_of(1)).empty() && holders(core) == 0
               && StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && LinesWith("a step thread has its own CPU rule: left alone") == 1,
           "own rule: the thread is left alone at the select, the core stays reserved");
    last_error_kept = threads[0].Step() && last_error_kept;
    Expect(Selected(handle_of(2)) == own_rule && Selected(handle_of(0)) == Sorted(core_ids), "own rule: untouched when the step moves on");
    // Another tool gives the step's thread a rule after the select: left alone at the clear.
    SetThreadSelectedCpuSets(handle_of(0), own_rule.data(), 1);
    last_error_kept = threads[1].Step() && last_error_kept;
    Expect(Selected(handle_of(0)) == own_rule && Selected(handle_of(1)) == Sorted(core_ids) && holders(core) == 1,
           "own rule: a rule set after the select is left alone at the clear");
    SetThreadSelectedCpuSets(handle_of(0), nullptr, 0);
    SetThreadSelectedCpuSets(handle_of(2), nullptr, 0);
    // A thread with an affinity of its own.
    SetThreadAffinityMask(handle_of(0), found_affinity & (~found_affinity + 1));
    last_error_kept = threads[0].Step() && last_error_kept;
    Expect(Selected(handle_of(0)).empty() && holders(core) == 0 && LinesWith("has its own CPU rule") == 1,
           "own rule: a thread with its own affinity is left alone (logged once)");
    SetThreadAffinityMask(handle_of(0), found_affinity);
    last_error_kept = threads[1].Step() && last_error_kept;
    Expect(holders(core) == 1, "own rule: the next thread holds the core again");

    // The bits: another part's bit changes nothing, here and at the watchdog.
    shared.core_wanted.fetch_xor(CORE_STEP);
    last_error_kept = threads[1].Step() && last_error_kept;
    Sleep(4 * battle_core::watchdog_ms);
    shared.core_wanted.fetch_xor(CORE_STEP);
    last_error_kept = threads[1].Step() && last_error_kept;
    Sleep(4 * battle_core::watchdog_ms);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && ProcessDefault() == kept && holders(core) == 1 && LinesWith("released") == 0,
           "bits: an unknown bit toggled releases nothing");

    // Turned off live at a step, then on again.
    shared.core_wanted.fetch_and(~CORE_BATTLE);
    last_error_kept = threads[0].Step() && last_error_kept;
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_WAITING && ProcessDefault() == every_id && holders(core) == 0
               && LinesWith("battle core: released (turned off)") == 1,
           "off: released at the next step, the default put back, the thread cleared");
    last_error_kept = threads[0].Step() && last_error_kept;
    shared.core_wanted.store(CORE_RENDER);
    last_error_kept = threads[1].Step() && last_error_kept;
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_WAITING && ProcessDefault() == every_id && holders(core) == 0
               && shared.render_core_status.load() == 0 && LinesWith("battle core: logical") == 1,
           "bits: the render bit alone reserves nothing");
    shared.core_wanted.store(CORE_BATTLE);
    last_error_kept = threads[1].Step() && last_error_kept;
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && ProcessDefault() == kept && holders(core) == 1, "on: reserved again");
    // Turned off while no step comes: the watchdog releases.
    shared.core_wanted.fetch_and(~CORE_BATTLE);
    Expect(WaitForState(CORE_WAITING) && ProcessDefault() == every_id && holders(core) == 0, "off: released by the watchdog");
    shared.core_wanted.store(CORE_BATTLE);

    // The steps stop: released after the threshold, with a default and without one.
    last_error_kept = threads[0].Step() && last_error_kept;
    battle_core::release_after_ms = 100;
    Expect(WaitForState(CORE_WAITING) && ProcessDefault() == every_id && holders(core) == 0
               && LinesWith("battle core: released (no battle step)") == 1,
           "threshold: no step, released and the default put back");
    battle_core::release_after_ms = 600000;
    SetProcessDefault({});
    last_error_kept = threads[0].Step() && last_error_kept;
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && ProcessDefault() == kept, "no default: every CPU minus the core is written");
    battle_core::release_after_ms = 100;
    Expect(WaitForState(CORE_WAITING) && ProcessDefault().empty(), "no default: the release leaves none again");
    battle_core::release_after_ms = 600000;
    SetProcessDefault(every_id);

    // The log's cap: 16 reserve / release lines, then one line, then none.
    for (int cycle = 0; cycle < 8; ++cycle) {
      last_error_kept = threads[0].Step() && last_error_kept;
      shared.core_wanted.fetch_and(~CORE_BATTLE);
      last_error_kept = threads[0].Step() && last_error_kept;
      shared.core_wanted.store(CORE_BATTLE);
    }
    Expect(LinesWith("battle core: logical") + LinesWith("battle core: released") == static_cast<int>(battle_core::CHANGE_LINES)
               && LinesWith("further battle core changes not logged") == 1,
           "log: 16 change lines, then one that says so");

    // Another program changes the default: it is kept, the feature stops for the session.
    last_error_kept = threads[0].Step() && last_error_kept;
    std::vector<ULONG> foreign_default = kept;
    foreign_default.pop_back();
    SetProcessDefault(foreign_default);
    Expect(WaitForState(CORE_YIELDED) && ProcessDefault() == foreign_default && holders(core) == 0
               && LinesWith("another program changed the game's CPU sets") == 1,
           "yield: a changed default is left as it is");
    last_error_kept = threads[1].Step() && last_error_kept;
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_YIELDED && ProcessDefault() == foreign_default && holders(core) == 0,
           "yield: stays for the session");
    BackToWaiting();
    SetProcessDefault(every_id);

    // The states that stay: one pick, one line, whatever the CPU sets become.
    SetProcessDefault(one_per_core);
    last_error_kept = threads[0].Step() && last_error_kept;
    SetProcessDefault(every_id);
    last_error_kept = threads[0].Step() && last_error_kept;
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_NO_SMT && ProcessDefault() == every_id && holders(core) == 0
               && LinesWith("not used: no core with two logical CPUs") == 1,
           "gate: no core with two logical CPUs, no second pick");
    BackToWaiting();
    SetProcessDefault(four_cores);
    last_error_kept = threads[0].Step() && last_error_kept;
    Expect(ProcessDefault() == Sorted(four_cores), "gate: few cores, the default untouched");
    SetProcessDefault(every_id);
    last_error_kept = threads[0].Step() && last_error_kept;
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_FEW_CORES && ProcessDefault() == every_id && LinesWith("not used: fewer than 8 cores") == 1,
           "gate: few cores, no second pick");
    BackToWaiting();

    // Another program changes the affinity: our default is still ours and is put back, then the feature stops.
    last_error_kept = threads[0].Step() && last_error_kept;
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && ProcessDefault() == kept, "affinity: reserved");
    SetProcessAffinityMask(GetCurrentProcess(), found_affinity & (found_affinity - 1));
    Expect(WaitForState(CORE_YIELDED) && ProcessDefault() == every_id && holders(core) == 0
               && LinesWith("another program changed the game's affinity") == 1,
           "yield: a changed affinity, the default put back");
    SetProcessAffinityMask(GetCurrentProcess(), found_affinity);

    Expect(last_error_kept, "every step left the thread's last error as it was");
  }

  SetProcessDefault(found_default);
  for (StepThread& thread : threads) {
    thread.quit = true;
    ReleaseSemaphore(thread.go, 1, nullptr);
    thread.thread.join();
  }
  std::printf("%s: %d checks, %d failure(s)%s\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures,
              (live ? "" : "; the live cases were LEFT OUT: this machine has no core the pick accepts"));
  return failures;
}
