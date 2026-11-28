/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The battle core (battle_core.hpp) without a game: the pick on synthetic CPU tables, and the live cases on this
// process's own CPU sets with its own "step" threads (reserve, the step's thread moving, threads with a rule of their
// own, the row's bit turned off and on, the watchdog's release, another program's change, the states that stay), and
// the render part with a "render" thread of its own (both cores in one reserve, each way it ends alone, the RenderCore
// key, the two bits switched live).
// The live cases need a machine the pick accepts (8 cores with two logical CPUs each); elsewhere they are left out and
// the last line says so. A write of the default that Windows refuses is made through this exe's own import slot.
// battle_core_test.exe; exit code = failures.

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
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
// `caches` caches of `cores` two-CPU cores each.
Table Caches(uint8_t caches, uint8_t cores) {
  Table table;
  for (uint8_t cache = 0; cache < caches; ++cache) {
    const auto first_logical = static_cast<uint8_t>(cache * cores * 2);
    AddCores(&table, first_logical, cores, 2, first_logical, 0);
  }
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
  CoreState state;  // the battle part's: its core, why neither part is used, or CORE_WAITING (not asked for)
  uint8_t logical_a = 0;
  uint8_t logical_b = 0;
  uint8_t cores = 0;
  uint32_t l3_mb = 0;
  uint32_t wanted = CORE_BATTLE;
  CoreState render = CORE_WAITING;  // where the battle part has no reason against both
  uint8_t render_a = 0;
  uint8_t render_b = 0;
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

// A thread for the render part: it captures itself as the render thread or runs a step, and waits idle or busy.
enum RenderCommand : uint8_t { RENDER_NOTHING, RENDER_CAPTURE, RENDER_STEP };
struct RenderThread {
  HANDLE go = CreateSemaphoreW(nullptr, 0, 64, nullptr);
  HANDLE done = CreateSemaphoreW(nullptr, 0, 64, nullptr);
  std::atomic<RenderCommand> command = RENDER_NOTHING;
  std::atomic_bool busy = false;
  std::atomic_bool quit = false;
  std::thread thread{[this] {
    while (!quit.load()) {
      if (WaitForSingleObject(go, (busy.load() ? 0 : INFINITE)) != WAIT_OBJECT_0) continue;
      switch (command.load()) {
        case RENDER_CAPTURE: battle_core::SetRenderThread(&shared, api); break;
        case RENDER_STEP:    battle_core::OnStepStart(&shared, api); break;
        default:             break;
      }
      ReleaseSemaphore(done, 1, nullptr);
    }
  }};

  void Run(RenderCommand next) {
    command = next;
    ReleaseSemaphore(go, 1, nullptr);
    WaitForSingleObject(done, INFINITE);
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
uint32_t RenderField(uint32_t shift) { return (shared.render_core_status.load() >> shift) & CORE_FIELD_MASK; }

bool WaitForState(CoreState wanted) {
  for (int tries = 0; tries < 300 && StatusField(CORE_STATE_SHIFT) != wanted; ++tries) {
    Sleep(10);
  }
  return StatusField(CORE_STATE_SHIFT) == wanted;
}

bool WaitForRender(CoreState wanted) {
  for (int tries = 0; tries < 300 && RenderField(CORE_STATE_SHIFT) != wanted; ++tries) {
    Sleep(10);
  }
  return RenderField(CORE_STATE_SHIFT) == wanted;
}

int LinesWith(const char* text) {
  const std::shared_lock lock(log_mutex);
  return static_cast<int>(std::ranges::count_if(log_lines, [&](const std::string& line) { return line.find(text) != std::string::npos; }));
}

// The watchdog logs after its work is done and its lock is gone: a line of its own says that all of it is visible.
bool WaitForLine(const char* text) {
  for (int tries = 0; tries < 300 && LinesWith(text) == 0; ++tries) {
    Sleep(10);
  }
  return LinesWith(text) != 0;
}

// This exe's import of SetProcessDefaultCpuSets is swapped for this (battle_core.hpp is compiled into the exe, so its
// writes come here too): while `before_refused_write` is set, the next write runs it and is refused as Windows would.
decltype(&SetProcessDefaultCpuSets) real_set_process_default = nullptr;
std::function<void()> before_refused_write;
BOOL WINAPI RefusableSetProcessDefault(HANDLE process, const ULONG* ids, ULONG count) {
  if (!before_refused_write) return real_set_process_default(process, ids, count);
  std::exchange(before_refused_write, nullptr)();
  SetLastError(ERROR_ACCESS_DENIED);
  return FALSE;
}

bool Reserved() {
  const std::scoped_lock lock(battle_core::state.mutex);
  return battle_core::state.reserved;
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
  const uint32_t both = CORE_BATTLE | CORE_RENDER;
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
      // The render core beside the battle core.
      {"render: a rule for logical 0-15", TwoCaches(), 0xffff, TwoCacheSizes(96, 32), CORE_ACTIVE, 14, 15, 8, 96, both, CORE_ACTIVE, 12, 13},
      {"render: one cache, 8 cores", OneCache(8, 2), every, one_96, CORE_ACTIVE, 14, 15, 8, 96, both, CORE_ACTIVE, 12, 13},
      {"render: two caches 96 + 32 MB, no rule", TwoCaches(), every, TwoCacheSizes(96, 32), CORE_ACTIVE, 14, 15, 16, 96, both,
       CORE_ACTIVE, 12, 13},
      {"render: two equal caches, no rule", TwoCaches(), every, TwoCacheSizes(32, 32), CORE_ACTIVE, 14, 15, 16, 32, both,
       CORE_ACTIVE, 12, 13},
      {"render: two caches 32 + 96 MB: beside the battle core", TwoCaches(), every, TwoCacheSizes(32, 96), CORE_ACTIVE, 30, 31, 16,
       96, both, CORE_ACTIVE, 28, 29},
      {"render: two caches of 6 cores: exactly 4 below", Caches(2, 6), every, {}, CORE_ACTIVE, 10, 11, 12, 0, both, CORE_ACTIVE, 8, 9},
      {"render: two caches of 4 cores: 2 below, none", Caches(2, 4), every, {}, CORE_ACTIVE, 6, 7, 8, 0, both, CORE_NO_CORE},
      {"render: four caches of 3 cores: none", Caches(4, 3), every, {}, CORE_ACTIVE, 4, 5, 12, 0, both, CORE_NO_CORE},
      {"render: four caches of 4 cores: none", Caches(4, 4), every, {}, CORE_ACTIVE, 6, 7, 16, 0, both, CORE_NO_CORE},
      {"render: 8 performance cores (class 1) + 16 efficiency cores", Hybrid(2, 1), every, {}, CORE_ACTIVE, 14, 15, 8, 0, both,
       CORE_ACTIVE, 12, 13},
      {"render: 8 performance + 16 efficiency cores, one class reported", Hybrid(2, 0), every, {}, CORE_ACTIVE, 14, 15, 8, 0, both,
       CORE_ACTIVE, 12, 13},
      {"render: no rule without logical 12 and 13: the next whole core", TwoCaches(), 0xffffcfff, TwoCacheSizes(96, 32),
       CORE_ACTIVE, 14, 15, 15, 96, both, CORE_ACTIVE, 10, 11},
      {"render: the core below the battle core parked: the next (5 below)", With(TwoCaches(), 0x3000, 0), 0xffff,
       TwoCacheSizes(96, 32), CORE_ACTIVE, 14, 15, 8, 96, both, CORE_ACTIVE, 10, 11},
      {"render: parked cores below do not count: none", With(TwoCaches(), 0x03ff, 0), 0xffff, TwoCacheSizes(96, 32), CORE_ACTIVE,
       14, 15, 8, 96, both, CORE_NO_CORE},
      {"render: 2 cores of the big cache + 6 of the other: none", TwoCaches(), 0x0fff000f, TwoCacheSizes(96, 32), CORE_ACTIVE, 2, 3,
       8, 96, both, CORE_NO_CORE},
      {"render: 6 cores: neither part", OneCache(6, 2), every, {}, CORE_FEW_CORES, 0, 0, 0, 0, both},
      {"render: the rule without logical 12 and 13: 7 cores, neither part", TwoCaches(), 0xcfff, TwoCacheSizes(96, 32),
       CORE_FEW_CORES, 0, 0, 0, 0, both},
      {"render: one logical CPU each: neither part", OneCache(8, 1), every, {}, CORE_NO_SMT, 0, 0, 0, 0, both},
      {"render: more than 64 logical CPUs: neither part", too_many, every, {}, CORE_UNSUPPORTED, 0, 0, 0, 0, both},
      // The render part alone takes the battle core's place.
      {"render alone: the highest core", TwoCaches(), 0xffff, TwoCacheSizes(96, 32), CORE_WAITING, 0, 0, 0, 0, CORE_RENDER,
       CORE_ACTIVE, 14, 15},
      {"render alone, a cache of 4 cores: 3 below, none", Caches(2, 4), every, {}, CORE_WAITING, 0, 0, 0, 0, CORE_RENDER, CORE_NO_CORE},
      {"render alone, 6 cores: neither part", OneCache(6, 2), every, {}, CORE_FEW_CORES, 0, 0, 0, 0, CORE_RENDER},
  };
  for (const PickCase& test : cases) {
    std::vector<ULONG> allowed_ids;
    uint8_t top_class = 0;
    for (const SYSTEM_CPU_SET_INFORMATION& entry : test.table) {
      if (((test.allowed_logical >> (entry.CpuSet.LogicalProcessorIndex % 64)) & 1) == 0) continue;
      allowed_ids.push_back(entry.CpuSet.Id);
      top_class = std::max(top_class, entry.CpuSet.EfficiencyClass);
    }
    const battle_core::Picks picks = battle_core::PickCores(test.table, allowed_ids, test.l3, test.wanted);
    const battle_core::Pick& pick = picks.battle;
    // A reason against the battle part is a reason against both.
    const bool neither = pick.state != CORE_ACTIVE && pick.state != CORE_WAITING;
    const bool as_expected = pick.state == test.state && pick.logical[0] == test.logical_a && pick.logical[1] == test.logical_b
                             && (test.state != CORE_ACTIVE || pick.cores == test.cores)
                             && pick.l3_mb == test.l3_mb
                             && picks.render.state == (neither ? CORE_WITH_BATTLE : test.render)
                             && picks.render.logical[0] == test.render_a && picks.render.logical[1] == test.render_b;
    Expect(as_expected, test.name);
    if (!as_expected) {
      std::printf("  got state %u, logical %u + %u, cores %u, L3 %u MB; render state %u, logical %u + %u\n", pick.state,
                  pick.logical[0], pick.logical[1], pick.cores, pick.l3_mb, picks.render.state, picks.render.logical[0],
                  picks.render.logical[1]);
    }
    // Over every table: a picked core is two allowed logical CPUs, both of one core that has no third, of the highest
    // class allowed (never an efficiency core beside performance cores), not parked; without a pick, no ids.
    const auto entry_of = [&](ULONG id) {
      return std::ranges::find_if(test.table, [&](const SYSTEM_CPU_SET_INFORMATION& entry) { return entry.CpuSet.Id == id; })->CpuSet;
    };
    // The cores of a cache that could be picked, below a core index.
    const auto free_cores_below = [&](uint8_t cache, uint8_t core_index) {
      int cores = 0;
      for (const SYSTEM_CPU_SET_INFORMATION& entry : test.table) {
        const auto of_core = [&](const SYSTEM_CPU_SET_INFORMATION& other) { return other.CpuSet.CoreIndex == entry.CpuSet.CoreIndex; };
        const bool usable = std::ranges::all_of(test.table, [&](const SYSTEM_CPU_SET_INFORMATION& other) {
          return !of_core(other)
                 || (other.CpuSet.Parked == 0 && std::ranges::find(allowed_ids, other.CpuSet.Id) != allowed_ids.end());
        });
        if (entry.CpuSet.LogicalProcessorIndex == entry.CpuSet.CoreIndex && entry.CpuSet.LastLevelCacheIndex == cache
            && entry.CpuSet.CoreIndex < core_index && entry.CpuSet.EfficiencyClass == top_class
            && std::ranges::count_if(test.table, of_core) == 2 && usable) {
          ++cores;
        }
      }
      return cores;
    };
    std::vector<ULONG> parts(picks.keep.ids.begin(), picks.keep.ids.begin() + picks.keep.count);
    for (const battle_core::Pick* part : {&picks.battle, &picks.render}) {
      if (part->state != CORE_ACTIVE) {
        Expect(part->ids[0] == 0 && part->ids[1] == 0 && part->logical[0] == 0 && part->logical[1] == 0, "pick: no ids without a core");
        continue;
      }
      parts.insert(parts.end(), part->ids.begin(), part->ids.end());
      const auto first = entry_of(part->ids[0]);
      const auto second = entry_of(part->ids[1]);
      const auto core_cpus = std::ranges::count_if(
          test.table, [&](const SYSTEM_CPU_SET_INFORMATION& entry) { return entry.CpuSet.CoreIndex == first.CoreIndex; });
      Expect(part->ids[0] != part->ids[1] && std::ranges::find(allowed_ids, part->ids[0]) != allowed_ids.end()
                 && std::ranges::find(allowed_ids, part->ids[1]) != allowed_ids.end(),
             "pick: both CPUs are allowed");
      Expect(first.CoreIndex == second.CoreIndex && core_cpus == 2 && first.LogicalProcessorIndex == part->logical[0]
                 && second.LogicalProcessorIndex == part->logical[1] && part->logical[0] < part->logical[1],
             "pick: one whole core with two logical CPUs");
      Expect(first.EfficiencyClass == top_class && second.EfficiencyClass == top_class && first.Parked == 0 && second.Parked == 0,
             "pick: the highest class allowed, not parked");
    }
    // What is kept and the cores are apart and together the allowed CPUs (never more); nothing is kept without a pick.
    Expect(neither ? (picks.keep.count == 0) : (Sorted(parts) == Sorted(allowed_ids)),
           "pick: the kept CPUs and the cores are the allowed CPUs, each once");
    if (picks.render.state == CORE_ACTIVE) {
      const auto render = entry_of(picks.render.ids[0]);
      Expect(free_cores_below(render.LastLevelCacheIndex, render.CoreIndex) >= static_cast<int>(battle_core::RENDER_POOL_CORES),
             "pick: 4 cores of its cache stay below the render core");
      if (pick.state == CORE_ACTIVE) {
        const auto battle = entry_of(pick.ids[0]);
        Expect(render.LastLevelCacheIndex == battle.LastLevelCacheIndex && render.CoreIndex < battle.CoreIndex,
               "pick: the render core is in the battle core's cache, below it");
      }
    }
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

  // The import slot is found by its name: the lookup table beside each DLL's slots holds the names.
  auto* const image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  const IMAGE_DATA_DIRECTORY& imports =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(image + reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew)
          ->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (const auto* dll = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image + imports.VirtualAddress);
       real_set_process_default == nullptr && dll->OriginalFirstThunk != 0; ++dll) {
    const auto* name = reinterpret_cast<const IMAGE_THUNK_DATA*>(image + dll->OriginalFirstThunk);
    for (auto* slot = reinterpret_cast<IMAGE_THUNK_DATA*>(image + dll->FirstThunk); name->u1.AddressOfData != 0; ++name, ++slot) {
      if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal)
          || std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image + name->u1.AddressOfData)->Name,
                         "SetProcessDefaultCpuSets")
                 != 0) {
        continue;
      }
      real_set_process_default = reinterpret_cast<decltype(real_set_process_default)>(slot->u1.Function);
      DWORD protection = 0;
      VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection);
      slot->u1.Function = reinterpret_cast<ULONGLONG>(&RefusableSetProcessDefault);
      VirtualProtect(slot, sizeof(*slot), protection, &protection);
      break;
    }
  }
  const bool write_swapped = real_set_process_default != nullptr;
  Expect(write_swapped, "seam: this exe's import of SetProcessDefaultCpuSets is swapped");

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

    // ---- the render part ----
    battle_core::render_busy_after_ms = 600000;
    RenderThread render;
    const HANDLE render_handle = render.thread.native_handle();
    const DWORD render_tid = GetThreadId(render_handle);
    const auto released = [] {
      for (int tries = 0; tries < 300 && Reserved(); ++tries) {
        Sleep(10);
      }
      return !Reserved();
    };
    // A session as after the plugin's init with this RenderCore key: nothing reserved, no render thread, an empty log.
    // The bits start without CORE_RENDER (the build's default holds it), so the key 1 shows.
    const auto new_session = [&](int render_core_key) {
      battle_core::release_after_ms = 0;
      released();
      battle_core::release_after_ms = 600000;
      Sleep(2 * battle_core::watchdog_ms);  // the release's line
      {
        const std::scoped_lock lock(battle_core::state.mutex);
        if (battle_core::state.render_thread != nullptr) {
          CloseHandle(battle_core::state.render_thread);
        }
        battle_core::state.render_thread = nullptr;
        battle_core::state.render_tid = 0;
        battle_core::state.render_state = CORE_NO_THREAD;
        battle_core::state.render_logged = {};
        battle_core::state.render_clear_logged = false;
        battle_core::state.core_state = CORE_WAITING;
        battle_core::state.changes_logged = 0;
        battle_core::state.own_rule_logged = false;
      }
      SetThreadSelectedCpuSets(render_handle, nullptr, 0);
      SetProcessDefault(every_id);
      shared.core_wanted.store(CORE_WANTED_DEFAULT & ~CORE_RENDER);
      shared.core_status.store(uint64_t{CORE_WAITING} << CORE_STATE_SHIFT);
      shared.render_core_status.store(0);
      {
        const std::scoped_lock lock(log_mutex);
        log_lines.clear();
      }
      battle_core::ApplyRenderCoreKey(&shared, render_core_key);
    };
    const auto step = [&](size_t thread) { last_error_kept = threads[thread].Step() && last_error_kept; };
    const auto capture_and_step = [&] {
      render.Run(RENDER_CAPTURE);
      step(0);
    };
    // The battle part off and on again: a release, then a new reserve.
    const auto reserve_again = [&] {
      shared.core_wanted.fetch_and(~CORE_BATTLE);
      step(0);
      shared.core_wanted.fetch_or(CORE_BATTLE);
      step(0);
    };
    const auto cpu_of = [&](ULONG id) {
      return std::ranges::find_if(cpus, [&](const SYSTEM_CPU_SET_INFORMATION& entry) { return entry.CpuSet.Id == id; })->CpuSet;
    };
    const char* const yield_line =
        "render core: stopped: another program changed the render thread's CPU sets, left as they are";
    const char* const refused_line = "render core: stopped: Windows refused";
    const char* const kept_line = " keeps the CPU sets it has";
    const char* const ended_line = "render core: not used: the render thread has ended";
    const char* const refused_write_line = "battle core: stopped: Windows refused (the CPU set write, error 5)";
    const auto end_thread = [](RenderThread* ending) {
      ending->command = RENDER_NOTHING;
      ending->quit = true;
      ReleaseSemaphore(ending->go, 1, nullptr);
      ending->thread.join();
    };

    // The key 1 sets the bit; the thread is captured once; one reserve takes both cores.
    new_session(1);
    Expect(shared.core_wanted.load() == (CORE_WANTED_DEFAULT | CORE_RENDER) && RenderField(CORE_STATE_SHIFT) == CORE_NO_THREAD,
           "key: 1 sets the render part's bit; no thread yet");
    render.Run(RENDER_CAPTURE);
    render.Run(RENDER_CAPTURE);
    battle_core::SetRenderThread(&shared, api);
    char render_thread_line[64];
    std::snprintf(render_thread_line, sizeof(render_thread_line), "renodx_attila64: render thread %lu", render_tid);
    Expect(RenderField(CORE_STATE_SHIFT) == CORE_WAITING && LinesWith("renodx_attila64: render thread ") == 1
               && LinesWith(render_thread_line) == 1 && battle_core::state.render_tid == render_tid,
           "capture: the first thread, once, with its line");
    step(0);
    const std::array<ULONG, 2> render_core = battle_core::state.render_ids;
    const std::vector<ULONG> render_core_ids = Sorted({render_core.begin(), render_core.end()});
    const std::vector<ULONG> kept_both = without(kept, render_core);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_ACTIVE
               && RenderField(CORE_LOGICAL_A_SHIFT) == logical_of(render_core[0])
               && RenderField(CORE_LOGICAL_B_SHIFT) == logical_of(render_core[1]) && RenderField(RENDER_ERROR_SHIFT) == 0,
           "render: the status has the render core's logical CPUs");
    Expect(Selected(render_handle) == render_core_ids && ProcessDefault() == kept_both && kept_both.size() + 4 == every_id.size()
               && battle_core::state.battle_ids == core && holders(core) == 1,
           "render: the render thread holds its core, the default is what it was minus both cores");
    Expect(cpu_of(render_core[0]).CoreIndex == cpu_of(render_core[1]).CoreIndex
               && cpu_of(render_core[0]).CoreIndex < cpu_of(core[0]).CoreIndex
               && cpu_of(render_core[0]).LastLevelCacheIndex == cpu_of(core[0]).LastLevelCacheIndex
               && cpu_of(render_core[0]).EfficiencyClass == cpu_of(core[0]).EfficiencyClass,
           "render: one core of the battle core's cache and class, below it");
    char render_piece[96];
    std::snprintf(render_piece, sizeof(render_piece), ", render core: logical %u + %u (render thread %lu), scheduling class ",
                  logical_of(render_core[0]), logical_of(render_core[1]), render_tid);
    Expect(LinesWith("renodx_attila64: battle core: logical ") == 1 && LinesWith(render_piece) == 1 && LinesWith("render core:") == 1,
           "render: one reserve line with both cores");

    // The render thread runs a step: it is never the step's thread, and nothing is logged.
    render.Run(RENDER_STEP);
    Expect(Selected(render_handle) == render_core_ids && Selected(handle_of(0)) == Sorted(core_ids) && holders(core) == 1
               && LinesWith("own CPU rule") == 0,
           "render: the render thread's own step leaves both threads where they are");
    // The bits live: an unknown one changes nothing; the render part's bit either way is a release, and the next step
    // reserves what is asked for.
    shared.core_wanted.fetch_xor(CORE_STEP);
    step(0);
    Sleep(4 * battle_core::watchdog_ms);
    shared.core_wanted.fetch_xor(CORE_STEP);
    step(0);
    Sleep(4 * battle_core::watchdog_ms);
    Expect(RenderField(CORE_STATE_SHIFT) == CORE_ACTIVE && ProcessDefault() == kept_both && LinesWith("released") == 0,
           "bits: an unknown bit toggled releases neither core");
    shared.core_wanted.fetch_and(~CORE_RENDER);
    step(0);
    // The watchdog may release first; then this step has already reserved the battle core alone.
    Expect(WaitForLine("battle core: released (turned off)") && RenderField(CORE_STATE_SHIFT) == CORE_WAITING
               && Selected(render_handle).empty()
               && ((StatusField(CORE_STATE_SHIFT) == CORE_WAITING && ProcessDefault() == every_id && holders(core) == 0)
                   || (StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && ProcessDefault() == kept && holders(core) == 1)),
           "bits: the render part turned off, both released");
    step(0);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_WAITING
               && Selected(render_handle).empty() && ProcessDefault() == kept && holders(core) == 1,
           "bits: the step after reserves the battle core alone");
    shared.core_wanted.fetch_or(CORE_RENDER);
    step(0);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_WAITING && ProcessDefault() == every_id
               && LinesWith("battle core: released (turned on)") == 1,
           "bits: the render part turned on, released");
    step(0);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_ACTIVE
               && Selected(render_handle) == render_core_ids && ProcessDefault() == kept_both,
           "bits: the step after reserves both");
    // Turned off while no step comes, and the steps stopping: the watchdog releases both.
    shared.core_wanted.fetch_and(~CORE_RENDER);
    Expect(WaitForRender(CORE_WAITING) && released() && Selected(render_handle).empty() && ProcessDefault() == every_id,
           "bits: the render part turned off, released by the watchdog");
    shared.core_wanted.fetch_or(CORE_RENDER);
    step(0);
    battle_core::release_after_ms = 100;
    Expect(WaitForState(CORE_WAITING) && WaitForRender(CORE_WAITING) && Selected(render_handle).empty()
               && ProcessDefault() == every_id && holders(core) == 0,
           "threshold: no step, both released, the render thread cleared");
    battle_core::release_after_ms = 600000;

    // The render part alone (the key absent leaves the bits as they are): it takes the battle core's place, and no
    // step thread is selected.
    new_session(-1);
    Expect(shared.core_wanted.load() == (CORE_WANTED_DEFAULT & ~CORE_RENDER) && RenderField(CORE_STATE_SHIFT) == CORE_NO_THREAD,
           "key: absent leaves the bits");
    shared.core_wanted.store(CORE_RENDER);
    capture_and_step();
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_WAITING && RenderField(CORE_STATE_SHIFT) == CORE_ACTIVE
               && battle_core::state.render_ids == core && Selected(render_handle) == Sorted(core_ids) && holders(core) == 0
               && ProcessDefault() == kept && Reserved(),
           "render alone: the highest core, no step thread selected");
    Expect(LinesWith("renodx_attila64: render core: logical ") == 1 && LinesWith("battle core: logical") == 0,
           "render alone: its own reserve line");
    // Another program changes the render thread's CPU sets: its sets stay, the render part ends for the session, and
    // the default is every allowed CPU again (still ours) until the release.
    SetThreadSelectedCpuSets(render_handle, own_rule.data(), 1);
    Expect(WaitForLine(yield_line) && RenderField(CORE_STATE_SHIFT) == CORE_YIELDED && Selected(render_handle) == own_rule
               && ProcessDefault() == every_id && Reserved() && StatusField(CORE_STATE_SHIFT) == CORE_WAITING,
           "render alone: yielded, its CPUs are back in the default");
    step(0);
    Sleep(4 * battle_core::watchdog_ms);
    Expect(Reserved() && LinesWith("released") == 0, "render alone: a state that stays releases nothing by itself");
    battle_core::release_after_ms = 100;
    Expect(released() && ProcessDefault() == every_id && RenderField(CORE_STATE_SHIFT) == CORE_YIELDED
               && Selected(render_handle) == own_rule,
           "render alone: the release leaves the yielded state and the other program's sets");
    battle_core::release_after_ms = 600000;
    step(0);
    Expect(!Reserved() && RenderField(CORE_STATE_SHIFT) == CORE_YIELDED && ProcessDefault() == every_id && LinesWith(yield_line) == 1,
           "render alone: yielded stays, its bit reserves nothing");

    // The same with both parts: only the render part ends, without a release; it stays ended over a new reserve.
    new_session(1);
    capture_and_step();
    SetThreadSelectedCpuSets(render_handle, own_rule.data(), 1);
    Expect(WaitForLine(yield_line) && RenderField(CORE_STATE_SHIFT) == CORE_YIELDED && Selected(render_handle) == own_rule
               && ProcessDefault() == kept && StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && holders(core) == 1
               && LinesWith("released") == 0,
           "yield: the render part alone ends, the battle core stays");
    reserve_again();
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_YIELDED
               && Selected(render_handle) == own_rule && ProcessDefault() == kept && LinesWith(yield_line) == 1,
           "yield: stays over a new reserve, the thread is not selected again");

    // Windows refuses the select (a handle without the right): the render part fails alone with its own error, and
    // is not tried again.
    battle_core::render_handle_access = THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
    new_session(1);
    capture_and_step();
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_FAILED
               && RenderField(RENDER_ERROR_SHIFT) == ERROR_ACCESS_DENIED && Selected(render_handle).empty()
               && ProcessDefault() == kept && holders(core) == 1 && LinesWith(refused_line) == 1 && LinesWith(", error 5)") == 1,
           "refused: the render part fails with its own error, the battle core is reserved");
    reserve_again();
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_FAILED && ProcessDefault() == kept
               && LinesWith(refused_line) == 1,
           "refused: stays, no second select");
    battle_core::render_handle_access = battle_core::STEP_THREAD_ACCESS;
    {
      const std::scoped_lock lock(battle_core::state.mutex);
      char line[battle_core::LINE_CAPACITY];
      battle_core::RenderOutcome(&battle_core::state, CORE_FAILED, ERROR_TIMEOUT, line);
    }
    Expect(RenderField(RENDER_ERROR_SHIFT) == RENDER_ERROR_LARGE && RenderField(CORE_STATE_SHIFT) == CORE_FAILED,
           "refused: an error above 254 is stored as 255");

    // Another program changes the default: both parts end, and the render thread is cleared. The reserve here is the
    // render thread's own step: no step thread is selected.
    new_session(1);
    render.Run(RENDER_CAPTURE);
    render.Run(RENDER_STEP);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_ACTIVE && holders(core) == 0
               && Selected(render_handle) == render_core_ids && ProcessDefault() == kept_both,
           "render: its own step reserves both cores and selects only the render thread");
    SetProcessDefault(foreign_default);
    Expect(WaitForState(CORE_YIELDED) && WaitForRender(CORE_WITH_BATTLE) && Selected(render_handle).empty()
               && ProcessDefault() == foreign_default,
           "yield: a changed default ends both, the render thread is cleared");
    // The same while the render thread has another program's sets: left alone, with a line.
    new_session(1);
    capture_and_step();
    {
      const std::scoped_lock lock(battle_core::state.mutex);
      SetThreadSelectedCpuSets(render_handle, own_rule.data(), 1);
      SetProcessDefault(foreign_default);
    }
    Expect(WaitForLine(kept_line) && StatusField(CORE_STATE_SHIFT) == CORE_YIELDED && RenderField(CORE_STATE_SHIFT) == CORE_WITH_BATTLE
               && Selected(render_handle) == own_rule && LinesWith(yield_line) == 0,
           "yield: a render thread with another program's sets is left alone at the release");

    // A clear that Windows refuses (the handle swapped for one without the right) is a line, not a failure.
    new_session(1);
    capture_and_step();
    {
      HANDLE without_set = nullptr;
      DuplicateHandle(GetCurrentProcess(), render_handle, GetCurrentProcess(), &without_set,
                      THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0);
      const std::scoped_lock lock(battle_core::state.mutex);
      CloseHandle(battle_core::state.render_thread);
      battle_core::state.render_thread = without_set;
    }
    shared.core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
    step(0);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_WAITING && RenderField(CORE_STATE_SHIFT) == CORE_WAITING
               && Selected(render_handle) == render_core_ids && ProcessDefault() == every_id && LinesWith(kept_line) == 1
               && LinesWith("Windows refused") == 0,
           "release: a refused clear of the render thread is logged and is not a failure");
    // The same when Windows refuses the read of the render thread's sets (a handle without that right).
    new_session(1);
    capture_and_step();
    HANDLE without_query = nullptr;
    DuplicateHandle(GetCurrentProcess(), render_handle, GetCurrentProcess(), &without_query,
                    THREAD_SET_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0);
    {
      // The bits under the same lock: a watchdog tick between the two would end the render part for the refused read.
      const std::scoped_lock lock(battle_core::state.mutex);
      CloseHandle(battle_core::state.render_thread);
      battle_core::state.render_thread = without_query;
      shared.core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
    }
    step(0);
    // A failed duplicate would leave a null handle, which gets the same line from the wait that fails on it.
    Expect(without_query != nullptr && WaitForLine(kept_line) && StatusField(CORE_STATE_SHIFT) == CORE_WAITING
               && RenderField(CORE_STATE_SHIFT) == CORE_WAITING
               && Selected(render_handle) == render_core_ids && ProcessDefault() == every_id && LinesWith(kept_line) == 1
               && LinesWith("Windows refused") == 0,
           "release: a refused read of the render thread's sets is logged and is not a failure");
    // A render thread that ended while it held its core: the release says that, not that it keeps its sets.
    new_session(1);
    {
      RenderThread ended;
      ended.Run(RENDER_CAPTURE);
      step(0);
      const bool held = RenderField(CORE_STATE_SHIFT) == CORE_ACTIVE;
      {
        // Under the lock: the watchdog sees the ended thread only together with the bits.
        const std::scoped_lock lock(battle_core::state.mutex);
        end_thread(&ended);
        shared.core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
      }
      step(0);
      Expect(held && WaitForLine(ended_line) && StatusField(CORE_STATE_SHIFT) == CORE_WAITING
                 && RenderField(CORE_STATE_SHIFT) == CORE_NO_THREAD && ProcessDefault() == every_id && LinesWith(ended_line) == 1
                 && LinesWith(kept_line) == 0,
             "release: a render thread that ended is recorded as ended, with no line about its sets");
    }

    if (write_swapped) {
      // A step whose reserve is to reach the armed write; true when it was refused there. Disarmed either way: a
      // reserve that missed the write would have the next case's reset refused.
      const auto refused_step = [&] {
        step(0);
        return !std::exchange(before_refused_write, nullptr);
      };
      // Windows refuses the reserve's write of the default: the battle part fails for the session with no second try,
      // the default stays, and the render thread (selected before the write) is cleared again.
      new_session(1);
      render.Run(RENDER_CAPTURE);
      bool selected_at_write = false;
      before_refused_write = [&] { selected_at_write = Selected(render_handle) == render_core_ids; };
      const bool refused_once = refused_step();
      step(0);
      Expect(refused_once && selected_at_write && StatusField(CORE_STATE_SHIFT) == CORE_FAILED
                 && (shared.core_status.load() >> CORE_ERROR_SHIFT) == ERROR_ACCESS_DENIED
                 && RenderField(CORE_STATE_SHIFT) == CORE_WITH_BATTLE && !Reserved() && Selected(render_handle).empty()
                 && holders(core) == 0 && ProcessDefault() == every_id && LinesWith(refused_write_line) == 1
                 && LinesWith("render core:") == 0,
             "refused write: the battle part fails once, the default stays, the render thread is cleared");
      // With another program's sets on the render thread by then: they stay, with the line.
      new_session(1);
      render.Run(RENDER_CAPTURE);
      before_refused_write = [&] { SetThreadSelectedCpuSets(render_handle, own_rule.data(), 1); };
      const bool refused_with_own_rule = refused_step();
      Expect(refused_with_own_rule && StatusField(CORE_STATE_SHIFT) == CORE_FAILED
                 && RenderField(CORE_STATE_SHIFT) == CORE_WITH_BATTLE
                 && Selected(render_handle) == own_rule && ProcessDefault() == every_id && LinesWith(refused_write_line) == 1
                 && LinesWith(kept_line) == 1,
             "refused write: a render thread with another program's sets is left alone, with its line");
      // With a render thread that ended by then: recorded as ended, not as a thread that keeps its sets.
      new_session(1);
      RenderThread ended;
      ended.Run(RENDER_CAPTURE);
      before_refused_write = [&] { end_thread(&ended); };
      const bool refused_with_ended = refused_step();
      if (!refused_with_ended) {
        end_thread(&ended);
      }
      Expect(refused_with_ended && StatusField(CORE_STATE_SHIFT) == CORE_FAILED
                 && RenderField(CORE_STATE_SHIFT) == CORE_NO_THREAD
                 && ProcessDefault() == every_id && LinesWith(refused_write_line) == 1 && LinesWith(ended_line) == 1
                 && LinesWith(kept_line) == 0,
             "refused write: a render thread that ended is recorded as ended, with no line about its sets");
    }

    // No render thread, and one that ended before the reserve: the battle core as without the render part.
    new_session(1);
    step(0);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_NO_THREAD && ProcessDefault() == kept
               && holders(core) == 1 && LinesWith("render") == 0,
           "no thread: nothing captured, the battle core alone");
    new_session(1);
    std::thread([] { battle_core::SetRenderThread(&shared, api); }).join();
    step(0);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_NO_THREAD && ProcessDefault() == kept
               && LinesWith("render core: not used: the render thread has ended") == 1,
           "no thread: a render thread that ended, the battle core alone");

    // A render thread with a rule of its own (selected sets, then an affinity): left alone, and looked at again at
    // the next reserve.
    new_session(1);
    render.Run(RENDER_CAPTURE);
    SetThreadSelectedCpuSets(render_handle, own_rule.data(), 1);
    step(0);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && RenderField(CORE_STATE_SHIFT) == CORE_OWN_RULE
               && Selected(render_handle) == own_rule && ProcessDefault() == kept
               && LinesWith("render core: not used: the render thread has its own CPU rule") == 1,
           "own rule: the render thread's sets stay, the default keeps the render core's CPUs");
    SetThreadSelectedCpuSets(render_handle, nullptr, 0);
    SetThreadAffinityMask(render_handle, found_affinity & (~found_affinity + 1));
    reserve_again();
    Expect(RenderField(CORE_STATE_SHIFT) == CORE_OWN_RULE && Selected(render_handle).empty() && ProcessDefault() == kept
               && LinesWith("has its own CPU rule") == 1,
           "own rule: an affinity of its own (logged once)");
    SetThreadAffinityMask(render_handle, found_affinity);
    reserve_again();
    Expect(RenderField(CORE_STATE_SHIFT) == CORE_ACTIVE && Selected(render_handle) == render_core_ids && ProcessDefault() == kept_both,
           "own rule: gone at the next reserve, the render core is used");

    // The busy check: an idle "render" thread loses the core at the first tick after the threshold (its sets cleared,
    // its CPUs back in the default); a busy one at the next reserve gets and keeps it.
    battle_core::render_busy_after_ms = 200;
    new_session(1);
    capture_and_step();
    Expect(WaitForLine("render core: not used: the render thread was mostly idle") && RenderField(CORE_STATE_SHIFT) == CORE_NOT_RENDER
               && Selected(render_handle).empty() && ProcessDefault() == kept && StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE
               && LinesWith("released") == 0,
           "busy: an idle thread is not the render thread, the battle core stays");
    // The rule reads the thread's CPU time, and its two CPUs stay open to other processes: the busy thread outranks
    // normal work there, and copies of this test take turns (each picks the same render core).
    const HANDLE busy_turn = CreateMutexW(nullptr, FALSE, L"Local\\renodx_attila64_battle_core_test_busy");
    WaitForSingleObject(busy_turn, INFINITE);
    SetThreadPriority(render_handle, THREAD_PRIORITY_TIME_CRITICAL);
    render.busy = true;
    render.Run(RENDER_NOTHING);
    reserve_again();
    Sleep(3 * battle_core::render_busy_after_ms);
    Expect(RenderField(CORE_STATE_SHIFT) == CORE_ACTIVE && Selected(render_handle) == render_core_ids && ProcessDefault() == kept_both
               && LinesWith("mostly idle") == 1,
           "busy: looked at again at the next reserve, a busy thread keeps the core");
    render.busy = false;
    SetThreadPriority(render_handle, THREAD_PRIORITY_NORMAL);
    ReleaseMutex(busy_turn);
    CloseHandle(busy_turn);
    battle_core::render_busy_after_ms = 600000;

    // RenderCore=0: off for the process. Nothing is captured, and the bit set later reserves no render core.
    new_session(0);
    Expect((shared.core_wanted.load() & CORE_RENDER) == 0 && RenderField(CORE_STATE_SHIFT) == CORE_OFF_INI, "key: 0 is off");
    render.Run(RENDER_CAPTURE);
    render.Run(RENDER_CAPTURE);
    shared.core_wanted.fetch_or(CORE_RENDER);
    step(0);
    Expect(RenderField(CORE_STATE_SHIFT) == CORE_OFF_INI && battle_core::state.render_thread == nullptr
               && LinesWith("renodx_attila64: render thread ") == 0
               && LinesWith("render core: off in ReShade.ini, RenderCore=0 (render thread 0, error 0)") == 1 && StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE
               && ProcessDefault() == kept && Selected(render_handle).empty(),
           "key: 0 captures nothing and never places, even with the bit set later");

    // A step thread that ended: the next one is selected, nothing fails.
    new_session(-1);
    {
      StepThread ended;
      last_error_kept = ended.Step() && last_error_kept;
      ended.quit = true;
      ReleaseSemaphore(ended.go, 1, nullptr);
      ended.thread.join();
    }
    step(1);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && holders(core) == 1 && LinesWith("Windows refused") == 0,
           "thread: the step's thread ended, the next one holds the core");
    // Turned off at a step while another program's default is there: a yield, the default left as it is.
    {
      const std::scoped_lock lock(battle_core::state.mutex);
      SetProcessDefault(foreign_default);
      shared.core_wanted.fetch_and(~CORE_BATTLE);
    }
    step(1);
    Expect(WaitForState(CORE_YIELDED) && ProcessDefault() == foreign_default && WaitForLine("CPU sets (left as they are)")
               && LinesWith("released") == 0,
           "off: with a changed default it is a yield, nothing is put back");
    // No default and an affinity of the process's own before the reserve: the affinity's CPUs minus the core are
    // written, and the release leaves no default again.
    new_session(-1);
    SetProcessDefault({});
    SetProcessAffinityMask(GetCurrentProcess(), found_affinity & (found_affinity - 1));
    step(0);
    const ULONG first_cpu = *std::ranges::find_if(every_id, [&](ULONG id) {
      return logical_of(id) == static_cast<uint32_t>(std::countr_zero(found_affinity));
    });
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_ACTIVE && ProcessDefault() == without(kept, std::span(&first_cpu, 1)),
           "affinity: with no default, the affinity's CPUs minus the core are written");
    shared.core_wanted.fetch_and(~CORE_BATTLE);
    step(0);
    Expect(StatusField(CORE_STATE_SHIFT) == CORE_WAITING && ProcessDefault().empty(), "affinity: the release leaves no default again");
    SetProcessAffinityMask(GetCurrentProcess(), found_affinity);

    new_session(-1);
    battle_core::render_busy_after_ms = 1000;
    end_thread(&render);

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
