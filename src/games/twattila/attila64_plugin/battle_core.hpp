/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <tuple>
#include <type_traits>

#include "loader_api.h"
#include "plugin_block.h"

// ROW_CPU_PLACEMENT's battle part, Keep Battle Core Free: while the game's battle step runs beside the drawing, one
// whole core (both logical CPUs) is taken out of the process's default CPU sets and the step's thread is selected onto
// it, so nothing shares the step's core. Scheduling only: no game memory, no game code, no floating-point state.
// - Never widens: what is written is the CPUs the game had minus the core; every release puts back what was read.
// - Never fights another tool: a changed default or affinity ends it for the session (YIELDED), and a thread with a CPU
//   rule of its own is left alone, at the select and at the clear (the 32-bit add-on's cpu_sets.hpp has the same rules).
// - The step's thread changes between steps; the previous one is cleared lazily, at the next step's start, before the
//   new one is selected: the hook keeps no frame around the step (a tail call), so nothing runs at its end.
// - A watchdog timer releases the core when no step came for release_after_ms (pause, menus, the campaign).
// Departure from cpu_sets.hpp: that file never changes the process default (its first version moved every thread and an
// audio thread of the 32-bit game crashed); here the default is narrowed by two CPUs for the threads that follow it.
//
// Its render part, Keep Render Core Free: in the same reserve the next whole core of the same cache goes to the render
// thread (the thread the loader starts the game on), with its second logical CPU idle too. It has its own state and
// error (render_core_status): a refused select, a rule of another tool on that thread, a thread that ended or is mostly
// idle end only the render part, and its two CPUs go back to the default. What ends the battle part ends both.
namespace twattila::attila64_plugin::battle_core {

inline constexpr size_t MAX_CPUS = 64;  // one processor group; more is CORE_UNSUPPORTED
// Ours: the measured case is 8 cores for the game (16 logical CPUs); with fewer, the drawing would lose too much.
inline constexpr uint32_t MIN_CORES = 8;
// Ours: the render core keeps this many whole cores of its cache below it for the drawing's task threads, which also
// keeps it off the cache's first cores (the graphics driver's interrupt work). Unmeasured on smaller caches: off there.
inline constexpr uint32_t RENDER_POOL_CORES = 4;
inline constexpr uint32_t CHANGE_LINES = 16;  // the reserve / release lines logged per session
inline constexpr size_t LINE_CAPACITY = 256;
inline constexpr DWORD STEP_THREAD_ACCESS = THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_LIMITED_INFORMATION | SYNCHRONIZE;
inline constexpr char FAILED_FORMAT[] = "renodx_attila64: battle core: stopped: Windows refused (%s, error %lu)";

// The tests shorten these. 2 s is about ten times the 90th-percentile step interval of a big battle.
static DWORD release_after_ms = 2000;
static DWORD watchdog_ms = 250;
// The render thread's handle rights (a test leaves one out), and how long after a reserve it must have been busy for
// half of the time: the loader's interface does not say which thread starts the game.
static DWORD render_handle_access = STEP_THREAD_ACCESS;
static DWORD render_busy_after_ms = 1000;

struct IdList {
  std::array<ULONG, MAX_CPUS> ids = {};
  ULONG count = 0;
};

// One level-3 cache: the logical CPUs that share it (by LogicalProcessorIndex) and its size.
struct L3Cache {
  KAFFINITY logical_cpus = 0;
  DWORD bytes = 0;
};

struct Pick {
  CoreState state = CORE_NO_SMT;        // CORE_ACTIVE: `ids` is the core; CORE_WAITING: not asked for
  std::array<ULONG, 2> ids = {};        // its CPU set ids
  std::array<uint8_t, 2> logical = {};  // its logical CPUs, the lower first
  uint8_t cores = 0;                    // the whole two-CPU cores of the picked class the game may use
  uint8_t scheduling_class = 0;
  uint32_t l3_mb = 0;
};

struct Picks {
  Pick battle;  // or the reason neither part is used
  Pick render;  // or CORE_NO_CORE; CORE_WITH_BATTLE with a reason in `battle`
  IdList keep;  // the allowed CPUs without both cores
};

struct State {
  std::shared_mutex mutex;
  Block* shared = nullptr;         // both set at the first reserve, for the watchdog
  const LoaderApi* api = nullptr;
  PTP_TIMER timer = nullptr;       // made once, kept for the process (never closed, never waited for)
  CoreState core_state = CORE_WAITING;
  bool reserved = false;           // the process default is the one written: the watchdog runs, a release puts it back
  uint32_t mask_at_reserve = 0;    // the parts asked for at the reserve, without a render part that ended for the session
  std::array<ULONG, 2> battle_ids = {};
  IdList original;                 // the process default as read; count 0: there was none
  IdList written;                  // what the reserve wrote
  KAFFINITY affinity_at_reserve = 0;
  HANDLE step_thread = nullptr;    // the thread selected onto the core; nullptr: none (or it has its own rule)
  DWORD step_tid = 0;              // the last step's thread
  uint64_t last_step_ms = 0;
  uint32_t changes_logged = 0;
  bool own_rule_logged = false;
  // The render part. The thread is captured once and kept for the process.
  HANDLE render_thread = nullptr;
  DWORD render_tid = 0;
  CoreState render_state = CORE_NO_THREAD;
  std::array<ULONG, 2> render_ids = {};
  uint64_t render_busy_cpu_100ns = 0;  // the thread's CPU time and the tick count at the reserve
  uint64_t render_busy_start_ms = 0;
  bool render_busy_checked = false;
  std::array<bool, CORE_STATE_COUNT> render_logged = {};  // each outcome's line, once per session
  bool render_clear_logged = false;
  // The reserve's tables, used only under the lock: the step runs on a game thread whose stack size is not ours.
  std::array<SYSTEM_CPU_SET_INFORMATION, MAX_CPUS> system = {};
  IdList allowed;
  std::array<L3Cache, MAX_CPUS> l3 = {};
  alignas(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) std::array<uint8_t, 16384> caches = {};
};
static State state;
// The plugin is never unloaded and nothing may run at process exit: the timer's thread can still be in a callback.
static_assert(std::is_trivially_destructible_v<State>);

// What another program changed since the reserve. Its default wins: ours is gone and is not put back.
enum Drift : uint8_t { DRIFT_NONE, DRIFT_AFFINITY, DRIFT_DEFAULT };

[[nodiscard]] inline bool SameIds(std::span<const ULONG> left, std::span<const ULONG> right) noexcept {
  return left.size() == right.size()
         && std::ranges::all_of(left, [&](ULONG id) { return std::ranges::find(right, id) != right.end(); });
}

// The core for the battle step among the CPUs the game may use (`allowed_ids`), or why there is none.
// - Class: the highest EfficiencyClass allowed, so never an efficiency core. Where every CPU reports one class, the
//   two-CPU rule below still leaves the one-CPU efficiency cores out.
// - Cache (ours: AMD reports one class on every core): the last-level cache with the biggest level-3 size, the 3D
//   V-Cache half of a two-cache CPU; then the one with more whole cores, then the lower index. Without sizes they
//   count as equal.
// - Core: in that cache, the highest core whose two logical CPUs are both allowed and not parked. The highest, because
//   the graphics driver's interrupt work sits on the first cores (measured on the test PC).
// A core counts only as two logical CPUs: the gain measured is the step's idle sibling.
// The render core (`wanted & CORE_RENDER`) is the next core by the same rule (the first without `wanted & CORE_BATTLE`),
// used only with RENDER_POOL_CORES more of them below it; so it is in the battle core's cache and class.
[[nodiscard]] inline Picks PickCores(std::span<const SYSTEM_CPU_SET_INFORMATION> system, std::span<const ULONG> allowed_ids,
                                     std::span<const L3Cache> l3, uint32_t wanted) noexcept {
  if (system.size() > MAX_CPUS || std::ranges::any_of(system, [](const SYSTEM_CPU_SET_INFORMATION& entry) {
        return entry.Size != sizeof(entry) || entry.CpuSet.Group != 0;
      })) {
    return {.battle = {.state = CORE_UNSUPPORTED}, .render = {.state = CORE_WITH_BATTLE}};
  }
  // Masks by LogicalProcessorIndex (below 64 in one group).
  uint64_t allowed = 0;
  uint64_t parked = 0;
  uint8_t top_class = 0;
  for (const SYSTEM_CPU_SET_INFORMATION& entry : system) {
    const uint64_t bit = 1ull << entry.CpuSet.LogicalProcessorIndex;
    if (entry.CpuSet.Parked != 0) {
      parked |= bit;
    }
    if (std::ranges::find(allowed_ids, entry.CpuSet.Id) == allowed_ids.end()) continue;
    allowed |= bit;
    top_class = std::max(top_class, entry.CpuSet.EfficiencyClass);
  }
  // Each entry's core, and the CPUs of the whole cores: two logical CPUs, both allowed, of the top class.
  std::array<uint64_t, MAX_CPUS> core_cpus = {};
  uint64_t whole = 0;
  for (size_t index = 0; index < system.size(); ++index) {
    for (const SYSTEM_CPU_SET_INFORMATION& other : system) {
      if (other.CpuSet.CoreIndex == system[index].CpuSet.CoreIndex) {
        core_cpus[index] |= 1ull << other.CpuSet.LogicalProcessorIndex;
      }
    }
    if (std::popcount(core_cpus[index]) == 2 && (core_cpus[index] & ~allowed) == 0
        && system[index].CpuSet.EfficiencyClass == top_class) {
      whole |= core_cpus[index];
    }
  }

  // (level-3 bytes, whole-core CPUs, lower index first) of the best cache so far.
  int cache_index = -1;
  std::tuple<DWORD, int, int> cache_rank = {};
  for (const SYSTEM_CPU_SET_INFORMATION& entry : system) {
    const uint64_t bit = 1ull << entry.CpuSet.LogicalProcessorIndex;
    if ((allowed & bit) == 0 || entry.CpuSet.EfficiencyClass != top_class) continue;
    if (entry.CpuSet.LastLevelCacheIndex == cache_index) continue;
    DWORD bytes = 0;
    for (const L3Cache& cache : l3) {
      if ((cache.logical_cpus & bit) != 0) {
        bytes = cache.bytes;
      }
    }
    uint64_t whole_here = 0;
    for (const SYSTEM_CPU_SET_INFORMATION& other : system) {
      if (other.CpuSet.LastLevelCacheIndex == entry.CpuSet.LastLevelCacheIndex) {
        whole_here |= whole & (1ull << other.CpuSet.LogicalProcessorIndex);
      }
    }
    const std::tuple<DWORD, int, int> rank = {bytes, std::popcount(whole_here), -int{entry.CpuSet.LastLevelCacheIndex}};
    if (cache_index < 0 || rank > cache_rank) {
      cache_index = entry.CpuSet.LastLevelCacheIndex;
      cache_rank = rank;
    }
  }

  // The picked cache's whole cores that are not parked (by entry index), and the highest of them outside `taken`.
  const auto free_core = [&](size_t index) {
    return system[index].CpuSet.LastLevelCacheIndex == cache_index && (core_cpus[index] & ~whole) == 0
           && (core_cpus[index] & parked) == 0;
  };
  const auto highest_core = [&](uint64_t taken) {
    int best = -1;
    for (size_t index = 0; index < system.size(); ++index) {
      if (!free_core(index) || (core_cpus[index] & taken) != 0) continue;
      if (best < 0 || system[index].CpuSet.CoreIndex > system[best].CpuSet.CoreIndex) {
        best = static_cast<int>(index);
      }
    }
    return best;
  };
  const int first = highest_core(0);
  if (first < 0) return {.battle = {.state = CORE_NO_SMT}, .render = {.state = CORE_WITH_BATTLE}};
  const auto cores = static_cast<uint8_t>(std::popcount(whole) / 2);
  if (cores < MIN_CORES) return {.battle = {.state = CORE_FEW_CORES, .cores = cores}, .render = {.state = CORE_WITH_BATTLE}};

  const auto core_pick = [&](int index) {
    Pick pick = {
        .state = CORE_ACTIVE,
        .logical = {static_cast<uint8_t>(std::countr_zero(core_cpus[index])),
                    static_cast<uint8_t>(std::bit_width(core_cpus[index]) - 1)},
        .cores = cores,
        .scheduling_class = system[index].CpuSet.SchedulingClass,
        .l3_mb = std::get<0>(cache_rank) >> 20,
    };
    for (const SYSTEM_CPU_SET_INFORMATION& entry : system) {
      for (size_t slot = 0; slot < pick.ids.size(); ++slot) {
        if (entry.CpuSet.LogicalProcessorIndex == pick.logical[slot]) {
          pick.ids[slot] = entry.CpuSet.Id;
        }
      }
    }
    return pick;
  };
  Picks picks = {.battle = {.state = CORE_WAITING}, .render = {.state = CORE_WAITING}};
  uint64_t taken = 0;
  if ((wanted & CORE_BATTLE) != 0) {
    picks.battle = core_pick(first);
    taken = core_cpus[first];
  }
  if ((wanted & CORE_RENDER) != 0) {
    picks.render.state = CORE_NO_CORE;
    if (const int render = highest_core(taken); render >= 0) {
      uint64_t below = 0;
      for (size_t index = 0; index < system.size(); ++index) {
        if (free_core(index) && system[index].CpuSet.CoreIndex < system[render].CpuSet.CoreIndex) {
          below |= core_cpus[index];
        }
      }
      if (std::popcount(below) / 2 >= static_cast<int>(RENDER_POOL_CORES)) {
        picks.render = core_pick(render);
      }
    }
  }
  for (const ULONG id : allowed_ids) {
    if (std::ranges::find(picks.battle.ids, id) == picks.battle.ids.end()
        && std::ranges::find(picks.render.ids, id) == picks.render.ids.end()) {
      picks.keep.ids[picks.keep.count++] = id;
    }
  }
  return picks;
}

// The render states that stay for the session: the reserve then ignores the render part's bit. (CORE_NO_THREAD ends
// only when the thread is captured, at the game's start.)
[[nodiscard]] constexpr bool StaysForSession(CoreState render_state) noexcept {
  return render_state == CORE_OFF_INI || render_state == CORE_NO_THREAD || render_state == CORE_YIELDED
         || render_state == CORE_FAILED;
}

// The parts asked for now: the bits this build knows, without a render part that ended for the session.
[[nodiscard]] static uint32_t WantedParts(const State& state, const Block& shared) noexcept {
  const uint32_t wanted = shared.core_wanted.load(std::memory_order_relaxed) & (CORE_BATTLE | CORE_RENDER);
  return StaysForSession(state.render_state) ? (wanted & ~CORE_RENDER) : wanted;
}

// A thread's CPU time so far (kernel + user) in 100 ns; false when Windows refused.
[[nodiscard]] static bool ReadCpuTime(HANDLE thread, uint64_t* cpu_100ns) noexcept {
  FILETIME created;
  FILETIME exited;
  FILETIME kernel;
  FILETIME user;
  if (GetThreadTimes(thread, &created, &exited, &kernel, &user) == FALSE) return false;
  *cpu_100ns = ((uint64_t{kernel.dwHighDateTime} << 32) | kernel.dwLowDateTime)
               + ((uint64_t{user.dwHighDateTime} << 32) | user.dwLowDateTime);
  return true;
}

// Takes a thread's selected CPU sets away only while they are still exactly `ids` and it still runs: another tool's
// rule stays. True when cleared; an ended thread or a refused clear is not a failure of the feature.
static bool ClearIfOurs(HANDLE thread, std::span<const ULONG> ids) noexcept {
  std::array<ULONG, MAX_CPUS> selected;
  ULONG count = 0;
  return WaitForSingleObject(thread, 0) == WAIT_TIMEOUT
         && GetThreadSelectedCpuSets(thread, selected.data(), MAX_CPUS, &count) != FALSE
         && SameIds(std::span<const ULONG>(selected).first(count), ids)
         && SetThreadSelectedCpuSets(thread, nullptr, 0) != FALSE;
}

// The one writer of the battle part's status: this module's state and the block's core_status (never the render word).
static void Publish(State* state, CoreState next, uint32_t logical_a, uint32_t logical_b, uint32_t cores,
                    DWORD error) noexcept {
  state->core_state = next;
  state->shared->core_status.store((uint64_t{next} << CORE_STATE_SHIFT)
                                       | (uint64_t{logical_a & CORE_FIELD_MASK} << CORE_LOGICAL_A_SHIFT)
                                       | (uint64_t{logical_b & CORE_FIELD_MASK} << CORE_LOGICAL_B_SHIFT)
                                       | (uint64_t{cores & CORE_FIELD_MASK} << CORE_ALLOWED_SHIFT)
                                       | ((uint64_t{error} & CORE_ERROR_MASK) << CORE_ERROR_SHIFT),
                                   std::memory_order_relaxed);
}

// The one writer of the render part's status: this module's state and the block's render_core_status. A state that
// stays for the session also leaves the mask the live-change check compares with, so entering it releases nothing.
static void PublishRender(State* state, CoreState next, uint32_t logical_a, uint32_t logical_b, DWORD error) noexcept {
  state->render_state = next;
  if (StaysForSession(next)) {
    state->mask_at_reserve &= ~CORE_RENDER;
  }
  state->shared->render_core_status.store((uint32_t{next} << CORE_STATE_SHIFT)
                                              | ((logical_a & CORE_FIELD_MASK) << CORE_LOGICAL_A_SHIFT)
                                              | ((logical_b & CORE_FIELD_MASK) << CORE_LOGICAL_B_SHIFT)
                                              | (std::min<uint32_t>(error, RENDER_ERROR_LARGE) << RENDER_ERROR_SHIFT),
                                          std::memory_order_relaxed);
}

// The render part is not used or stopped: published, and its line (once per session for each outcome) goes into `line`
// for the caller to log after the unlock.
static void RenderOutcome(State* state, CoreState outcome, DWORD error, std::span<char> line) noexcept {
  PublishRender(state, outcome, 0, 0, error);
  if (state->render_logged[outcome]) return;
  state->render_logged[outcome] = true;
  const char* text = nullptr;
  switch (outcome) {
    case CORE_NO_CORE:    text = "not used: no free core next to the battle core"; break;
    case CORE_OWN_RULE:   text = "not used: the render thread has its own CPU rule"; break;
    case CORE_OFF_INI:    text = "off in ReShade.ini (RenderCore=0)"; break;
    case CORE_NO_THREAD:  text = "not used: the render thread has ended"; break;
    case CORE_NOT_RENDER: text = "not used: the render thread was mostly idle"; break;
    case CORE_YIELDED:    text = "stopped: another program changed the render thread's CPU sets (left as they are)"; break;
    default:              text = "stopped: Windows refused"; break;
  }
  std::snprintf(line.data(), line.size(), "renodx_attila64: render core: %s (render thread %lu, error %lu)", text,
                state->render_tid, error);
}

// A reserve or release line just formatted into `line`: kept for the first CHANGE_LINES, then one line says that the
// rest is not logged, then none.
static void CapChangeLine(State* state, std::span<char> line) noexcept {
  if (state->changes_logged > CHANGE_LINES) {
    line[0] = '\0';
    return;
  }
  ++state->changes_logged;
  if (state->changes_logged > CHANGE_LINES) {
    std::snprintf(line.data(), line.size(), "renodx_attila64: further battle core changes not logged");
  }
}

// A failed read counts as a changed default: with our own list written, the 64-id buffer can only be too small after
// another program's write.
[[nodiscard]] static Drift ReadDrift(const State& state) noexcept {
  std::array<ULONG, MAX_CPUS> now;
  ULONG count = 0;
  if (GetProcessDefaultCpuSets(GetCurrentProcess(), now.data(), MAX_CPUS, &count) == FALSE
      || !SameIds(std::span<const ULONG>(now).first(count), std::span(state.written.ids).first(state.written.count))) {
    return DRIFT_DEFAULT;
  }
  DWORD_PTR process_affinity = 0;
  DWORD_PTR system_affinity = 0;
  if (GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity) == FALSE
      || process_affinity != state.affinity_at_reserve) {
    return DRIFT_AFFINITY;
  }
  return DRIFT_NONE;
}

// Ends a reserve, under the lock: what was read is put back only while the default is still the one written, the
// step's thread is cleared only while it still holds exactly the core, and `next` is published. A changed default or
// affinity makes a release a yield; a refused restore makes it a failure (both end the feature for the session). The
// line for the log goes into `line`: the caller logs it after the unlock. The render thread is cleared first, by the
// step thread's rule; a clear that did not happen is no failure (its line goes into `render_line`). The render part
// waits again after a release, and ends with the battle part after a yield or a failure.
static void Release(State* state, CoreState next, DWORD error, const char* reason, std::span<char> line,
                    std::span<char> render_line) noexcept {
  // Only stopped: the callback takes the lock, so waiting for it here would never end.
  if (state->timer != nullptr) {
    SetThreadpoolTimer(state->timer, nullptr, 0, 0);
  }
  if (state->render_state == CORE_ACTIVE && !ClearIfOurs(state->render_thread, state->render_ids)) {
    if (WaitForSingleObject(state->render_thread, 0) == WAIT_OBJECT_0) {
      RenderOutcome(state, CORE_NO_THREAD, ERROR_SUCCESS, render_line);
    } else if (!state->render_clear_logged) {
      state->render_clear_logged = true;
      std::snprintf(render_line.data(), render_line.size(),
                    "renodx_attila64: render core: the render thread %lu keeps the CPU sets it has", state->render_tid);
    }
  }
  const Drift drift = ReadDrift(*state);
  if (drift != DRIFT_DEFAULT
      && SetProcessDefaultCpuSets(GetCurrentProcess(),
                                  ((state->original.count == 0) ? nullptr : state->original.ids.data()),
                                  state->original.count)
             == FALSE) {
    next = CORE_FAILED;
    error = GetLastError();
    reason = "the CPU set restore";
  }
  if (state->step_thread != nullptr) {
    ClearIfOurs(state->step_thread, state->battle_ids);
    CloseHandle(state->step_thread);
    state->step_thread = nullptr;
  }
  state->step_tid = 0;
  if (next == CORE_WAITING && drift != DRIFT_NONE) {
    next = CORE_YIELDED;
  }
  state->reserved = false;
  Publish(state, next, 0, 0, 0, error);
  if (!StaysForSession(state->render_state)) {
    PublishRender(state, ((next == CORE_WAITING) ? CORE_WAITING : CORE_WITH_BATTLE), 0, 0, ERROR_SUCCESS);
  }
  switch (next) {
    case CORE_WAITING:
      std::snprintf(line.data(), line.size(), "renodx_attila64: battle core: released (%s)", reason);
      CapChangeLine(state, line);
      break;
    case CORE_YIELDED:
      std::snprintf(line.data(), line.size(), "renodx_attila64: battle core: stopped: another program changed the game's %s",
                    ((drift == DRIFT_DEFAULT) ? "CPU sets (left as they are)" : "affinity (CPU sets put back)"));
      break;
    default:
      std::snprintf(line.data(), line.size(), FAILED_FORMAT, reason, error);
      break;
  }
}

// Every watchdog_ms while the default is the one written (context: the State): releases when the steps stopped or a
// row was switched, and yields when another program changed the default or the affinity. Then the render part alone:
// it ends when another program changed the render thread's CPU sets, or when that thread is not the busy one.
static VOID CALLBACK Watchdog(PTP_CALLBACK_INSTANCE /*instance*/, PVOID context, PTP_TIMER /*timer*/) noexcept {
  auto* watched = static_cast<State*>(context);
  char line[LINE_CAPACITY] = "";
  char render_line[LINE_CAPACITY] = "";
  const LoaderApi* api = nullptr;
  {
    const std::scoped_lock lock(watched->mutex);
    if (!watched->reserved) return;
    api = watched->api;
    const uint64_t now_ms = GetTickCount64();
    const uint32_t wanted = WantedParts(*watched, *watched->shared);
    const char* reason = nullptr;
    if (now_ms - watched->last_step_ms >= release_after_ms) {
      reason = "no battle step";
    } else if (wanted != watched->mask_at_reserve) {
      reason = (((watched->mask_at_reserve & ~wanted) != 0) ? "turned off" : "turned on");
    } else if (ReadDrift(*watched) != DRIFT_NONE) {
      reason = "the CPU sets changed back";  // a yield unless they did: Release reads again and words it
    }
    if (reason != nullptr) {
      Release(watched, CORE_WAITING, ERROR_SUCCESS, reason, line, render_line);
    } else if (watched->render_state == CORE_ACTIVE) {
      // Ends the render part alone: its two CPUs go back into the default, which is still the one written (read just
      // above). A refused write ends both parts.
      const auto drop = [&](CoreState outcome, DWORD error) {
        RenderOutcome(watched, outcome, error, render_line);
        IdList wider = watched->written;
        for (const ULONG id : watched->render_ids) {
          wider.ids[wider.count++] = id;
        }
        if (SetProcessDefaultCpuSets(GetCurrentProcess(), wider.ids.data(), wider.count) == FALSE) {
          Release(watched, CORE_FAILED, GetLastError(), "the CPU set write", line, render_line);
        } else {
          watched->written = wider;
        }
      };
      std::array<ULONG, MAX_CPUS> selected;
      ULONG count = 0;
      uint64_t cpu_100ns = 0;
      if (GetThreadSelectedCpuSets(watched->render_thread, selected.data(), MAX_CPUS, &count) == FALSE) {
        const DWORD error = GetLastError();
        if (WaitForSingleObject(watched->render_thread, 0) == WAIT_OBJECT_0) {
          drop(CORE_NO_THREAD, ERROR_SUCCESS);
        } else {
          drop(CORE_FAILED, error);
        }
      } else if (!SameIds(std::span<const ULONG>(selected).first(count), watched->render_ids)) {
        drop(CORE_YIELDED, ERROR_SUCCESS);
      } else if (!watched->render_busy_checked && now_ms - watched->render_busy_start_ms >= render_busy_after_ms) {
        // Once per reserve. The render thread is busy nearly all of a battle (99 % measured); another thread would
        // hold a whole core for nothing. Below half: not it, until the next reserve looks again.
        watched->render_busy_checked = true;
        if (ReadCpuTime(watched->render_thread, &cpu_100ns)
            && (cpu_100ns - watched->render_busy_cpu_100ns) * 2 < (now_ms - watched->render_busy_start_ms) * 10000) {
          ClearIfOurs(watched->render_thread, watched->render_ids);
          drop(CORE_NOT_RENDER, ERROR_SUCCESS);
        }
      }
    }
  }
  if (line[0] != '\0') {
    api->log("%s", line);
  }
  if (render_line[0] != '\0') {
    api->log("%s", render_line);
  }
}

// At the start of each battle step that runs beside the drawing, on the step's thread (from the hook, before the
// game's function): reserves at the first step, follows the step's thread, releases when a row was switched (the
// next step then reserves what is asked for). Changes nothing the game can see: the thread's last error is put back.
static void OnStepStart(Block* shared, const LoaderApi& api) noexcept {
  const DWORD last_error = GetLastError();
  // Formatted under the lock, logged after it: the loader's log may wait for its own lock.
  char change_line[LINE_CAPACITY] = "";
  char thread_line[LINE_CAPACITY] = "";
  char render_line[LINE_CAPACITY] = "";
  {
    const std::scoped_lock lock(state.mutex);
    state.last_step_ms = GetTickCount64();
    const uint32_t wanted = WantedParts(state, *shared);
    if (state.reserved && wanted != state.mask_at_reserve) {
      Release(&state, CORE_WAITING, ERROR_SUCCESS, (((state.mask_at_reserve & ~wanted) != 0) ? "turned off" : "turned on"),
              change_line, render_line);
    } else if (!state.reserved && state.core_state == CORE_WAITING && wanted != 0) {
      // The reserve. The tables are read again every time: rules and parking change. NO_SMT, FEW_CORES, UNSUPPORTED
      // and FAILED stay for the session (no second pick) and end the render part with the battle part.
      state.shared = shared;
      state.api = &api;
      const auto refuse = [&](CoreState gate, DWORD error, const char* failed_call) {
        Publish(&state, gate, 0, 0, 0, error);
        if (!StaysForSession(state.render_state)) {
          PublishRender(&state, CORE_WITH_BATTLE, 0, 0, ERROR_SUCCESS);
        }
        switch (gate) {
          case CORE_NO_SMT:
            std::snprintf(change_line, sizeof(change_line), "renodx_attila64: battle core: not used: no core with two logical CPUs");
            break;
          case CORE_FEW_CORES:
            std::snprintf(change_line, sizeof(change_line), "renodx_attila64: battle core: not used: fewer than %u cores", MIN_CORES);
            break;
          case CORE_UNSUPPORTED:
            std::snprintf(change_line, sizeof(change_line),
                          "renodx_attila64: battle core: not used: more than 64 logical CPUs, or processor groups");
            break;
          default:
            std::snprintf(change_line, sizeof(change_line), FAILED_FORMAT, failed_call, error);
            break;
        }
      };
      // A list that does not fit 64 entries is a bigger machine, not a failure.
      const auto refuse_read = [&](const char* failed_call) {
        const DWORD error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
          refuse(CORE_UNSUPPORTED, ERROR_SUCCESS, failed_call);
        } else {
          refuse(CORE_FAILED, error, failed_call);
        }
      };
      ULONG system_bytes = 0;
      ULONG default_count = 0;
      DWORD_PTR process_affinity = 0;
      DWORD_PTR system_affinity = 0;
      if (GetSystemCpuSetInformation(state.system.data(), sizeof(state.system), &system_bytes, GetCurrentProcess(), 0)
          == FALSE) {
        refuse_read("the CPU set list");
      } else if (GetProcessDefaultCpuSets(GetCurrentProcess(), state.original.ids.data(), MAX_CPUS, &default_count)
                 == FALSE) {
        refuse_read("the game's CPU sets");
      } else if (GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity) == FALSE) {
        refuse(CORE_FAILED, GetLastError(), "the game's affinity");
      } else {
        // Allowed: the default (none: every CPU), inside the affinity (a hard affinity wins over CPU sets), without
        // the CPUs another process holds and the real-time ones.
        const std::span system = std::span(state.system).first(system_bytes / sizeof(SYSTEM_CPU_SET_INFORMATION));
        const std::span original = std::span<const ULONG>(state.original.ids).first(default_count);
        state.allowed.count = 0;
        for (const SYSTEM_CPU_SET_INFORMATION& entry : system) {
          const bool in_default = original.empty() || std::ranges::find(original, entry.CpuSet.Id) != original.end();
          const bool in_affinity = ((process_affinity >> entry.CpuSet.LogicalProcessorIndex) & 1) != 0;
          const bool taken = entry.CpuSet.Allocated != 0 && entry.CpuSet.AllocatedToTargetProcess == 0;
          if (in_default && in_affinity && !taken && entry.CpuSet.RealTime == 0) {
            state.allowed.ids[state.allowed.count++] = entry.CpuSet.Id;
          }
        }
        // The level-3 sizes; when the read fails they count as equal.
        size_t l3_count = 0;
        DWORD cache_bytes = sizeof(state.caches);
        if (GetLogicalProcessorInformationEx(
                RelationCache, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(state.caches.data()), &cache_bytes)
            != FALSE) {
          for (DWORD offset = 0; offset < cache_bytes && l3_count < state.l3.size();) {
            const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(state.caches.data() + offset);
            if (info->Relationship == RelationCache && info->Cache.Level == 3 && info->Cache.GroupMask.Group == 0) {
              state.l3[l3_count++] = {.logical_cpus = info->Cache.GroupMask.Mask, .bytes = info->Cache.CacheSize};
            }
            offset += info->Size;
          }
        }
        const std::span allowed = std::span<const ULONG>(state.allowed.ids).first(state.allowed.count);
        const Picks picks = PickCores(system, allowed, std::span<const L3Cache>(state.l3).first(l3_count), wanted);
        // Before the render part: an outcome that stays for the session takes its bit out again.
        state.mask_at_reserve = wanted;
        if (picks.battle.state != CORE_ACTIVE && picks.battle.state != CORE_WAITING) {
          refuse(picks.battle.state, ERROR_SUCCESS, nullptr);
        } else {
          // The render thread first, so a refused select leaves its two CPUs in the default. Selected only when it
          // still runs and has no rule of its own (no selected sets but ours, the process's affinity).
          if (picks.render.state == CORE_NO_CORE) {
            RenderOutcome(&state, CORE_NO_CORE, ERROR_SUCCESS, render_line);
          } else if (picks.render.state == CORE_ACTIVE) {
            std::array<ULONG, MAX_CPUS> selected;
            ULONG selected_count = 0;
            GROUP_AFFINITY thread_affinity = {};
            if (WaitForSingleObject(state.render_thread, 0) != WAIT_TIMEOUT) {
              RenderOutcome(&state, CORE_NO_THREAD, ERROR_SUCCESS, render_line);
            } else if (GetThreadSelectedCpuSets(state.render_thread, selected.data(), MAX_CPUS, &selected_count) == FALSE
                       || (selected_count != 0
                           && !SameIds(std::span<const ULONG>(selected).first(selected_count), picks.render.ids))
                       || GetThreadGroupAffinity(state.render_thread, &thread_affinity) == FALSE
                       || thread_affinity.Mask != process_affinity) {
              RenderOutcome(&state, CORE_OWN_RULE, ERROR_SUCCESS, render_line);
            } else if (SetThreadSelectedCpuSets(state.render_thread, picks.render.ids.data(),
                                                static_cast<ULONG>(picks.render.ids.size()))
                       == FALSE) {
              RenderOutcome(&state, CORE_FAILED, GetLastError(), render_line);
            } else {
              state.render_ids = picks.render.ids;
              state.render_busy_start_ms = state.last_step_ms;
              state.render_busy_checked = !ReadCpuTime(state.render_thread, &state.render_busy_cpu_100ns);
              PublishRender(&state, CORE_ACTIVE, picks.render.logical[0], picks.render.logical[1], ERROR_SUCCESS);
            }
          }
          // What is written: the allowed CPUs without the cores now held.
          state.written = picks.keep;
          if (picks.render.state == CORE_ACTIVE && state.render_state != CORE_ACTIVE) {
            for (const ULONG id : picks.render.ids) {
              state.written.ids[state.written.count++] = id;
            }
          }
          if (SetProcessDefaultCpuSets(GetCurrentProcess(), state.written.ids.data(), state.written.count) == FALSE) {
            const DWORD error = GetLastError();
            if (state.render_state == CORE_ACTIVE) {
              ClearIfOurs(state.render_thread, state.render_ids);
            }
            refuse(CORE_FAILED, error, "the CPU set write");
          } else {
            state.reserved = true;
            state.original.count = default_count;
            state.battle_ids = picks.battle.ids;
            state.affinity_at_reserve = process_affinity;
            if (state.timer == nullptr) {
              state.timer = CreateThreadpoolTimer(&Watchdog, &state, nullptr);
            }
            if (state.timer == nullptr) {
              Release(&state, CORE_FAILED, GetLastError(), "the watchdog timer", change_line, render_line);
            } else {
              // Relative, in 100 ns.
              const int64_t due_100ns = -int64_t{watchdog_ms} * 10000;
              FILETIME due = {.dwLowDateTime = static_cast<DWORD>(due_100ns),
                              .dwHighDateTime = static_cast<DWORD>(due_100ns >> 32)};
              SetThreadpoolTimer(state.timer, &due, watchdog_ms, 0);
              // One line for the cores now held: the battle part's, then the render part's.
              int length = 0;
              if (picks.battle.state == CORE_ACTIVE) {
                Publish(&state, CORE_ACTIVE, picks.battle.logical[0], picks.battle.logical[1], picks.battle.cores,
                        ERROR_SUCCESS);
                length = std::snprintf(
                    change_line, sizeof(change_line),
                    "renodx_attila64: battle core: logical %u + %u, allowed cores %u, L3 %u MB, scheduling class %u",
                    picks.battle.logical[0], picks.battle.logical[1], picks.battle.cores, picks.battle.l3_mb,
                    picks.battle.scheduling_class);
              }
              if (state.render_state == CORE_ACTIVE) {
                length += std::snprintf(change_line + length, sizeof(change_line) - length, "%s",
                                        ((length == 0) ? "renodx_attila64: " : ", "));
                std::snprintf(change_line + length, sizeof(change_line) - length,
                              "render core: logical %u + %u (render thread %lu), scheduling class %u",
                              picks.render.logical[0], picks.render.logical[1], state.render_tid,
                              picks.render.scheduling_class);
              }
              if (length != 0) {
                CapChangeLine(&state, change_line);
              }
            }
          }
        }
      }
    }

    // The step's thread, at the reserve and whenever the step moved to another one: the previous thread is cleared
    // first, so two threads never hold the core. Selected only when it has no rule of its own (no selected sets, the
    // process's affinity); otherwise its steps run on the default and the core stays empty until the step moves on.
    // The render thread is never the step's thread: a step it runs stays where that thread is.
    if (state.core_state == CORE_ACTIVE && GetCurrentThreadId() != state.step_tid
        && GetCurrentThreadId() != state.render_tid) {
      if (state.step_thread != nullptr) {
        ClearIfOurs(state.step_thread, state.battle_ids);
        CloseHandle(state.step_thread);
        state.step_thread = nullptr;
      }
      state.step_tid = GetCurrentThreadId();
      ULONG selected_count = 0;
      GROUP_AFFINITY thread_affinity = {};
      DWORD_PTR process_affinity = 0;
      DWORD_PTR system_affinity = 0;
      // A fresh handle per thread: a thread id may be used again.
      HANDLE self = nullptr;
      if (GetThreadSelectedCpuSets(GetCurrentThread(), nullptr, 0, &selected_count) == FALSE || selected_count != 0
          || GetThreadGroupAffinity(GetCurrentThread(), &thread_affinity) == FALSE
          || GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity) == FALSE
          || thread_affinity.Mask != process_affinity) {
        if (!state.own_rule_logged) {
          state.own_rule_logged = true;
          std::snprintf(thread_line, sizeof(thread_line),
                        "renodx_attila64: battle core: a step thread has its own CPU rule: left alone");
        }
      } else if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, STEP_THREAD_ACCESS,
                                 FALSE, 0)
                     == FALSE
                 || SetThreadSelectedCpuSets(self, state.battle_ids.data(), static_cast<ULONG>(state.battle_ids.size()))
                        == FALSE) {
        const DWORD error = GetLastError();
        state.step_thread = self;  // Release closes it
        Release(&state, CORE_FAILED, error, "the step thread's CPU sets", thread_line, render_line);
      } else {
        state.step_thread = self;
      }
    }
  }
  if (change_line[0] != '\0') {
    api.log("%s", change_line);
  }
  if (thread_line[0] != '\0') {
    api.log("%s", thread_line);
  }
  if (render_line[0] != '\0') {
    api.log("%s", render_line);
  }
  SetLastError(last_error);
}

// At the plugin's init, when the CPU row's hook is in: the [Attila64Plugin] RenderCore key. -1 (absent) leaves the
// render part's bit as the build has it, 0 turns the part off for the process (the add-on's row cannot turn it on),
// any other value sets the bit (a run without the add-on). The render word is built from here on.
static void ApplyRenderCoreKey(Block* shared, int key) noexcept {
  const std::scoped_lock lock(state.mutex);
  state.shared = shared;
  if (key == 0) {
    shared->core_wanted.fetch_and(~CORE_RENDER, std::memory_order_relaxed);
    PublishRender(&state, CORE_OFF_INI, 0, 0, ERROR_SUCCESS);
    return;
  }
  if (key != -1) {
    shared->core_wanted.fetch_or(CORE_RENDER, std::memory_order_relaxed);
  }
  PublishRender(&state, CORE_NO_THREAD, 0, 0, ERROR_SUCCESS);
}

// At the game's start, on the thread the loader then runs the game's main on: the render thread. Captured once and
// kept for the process (that thread ends only at exit); nothing is captured with RenderCore=0.
static void SetRenderThread(Block* shared, const LoaderApi& api) noexcept {
  char line[LINE_CAPACITY] = "";
  {
    const std::scoped_lock lock(state.mutex);
    state.shared = shared;
    if (state.render_state == CORE_OFF_INI) {
      RenderOutcome(&state, CORE_OFF_INI, ERROR_SUCCESS, line);
    } else if (state.render_thread == nullptr) {
      // A second call keeps the first thread.
      HANDLE self = nullptr;
      if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, render_handle_access, FALSE,
                          0)
          != FALSE) {
        state.render_thread = self;
        state.render_tid = GetCurrentThreadId();
        PublishRender(&state, CORE_WAITING, 0, 0, ERROR_SUCCESS);
        std::snprintf(line, sizeof(line), "renodx_attila64: render thread %lu", state.render_tid);
      } else {
        std::snprintf(line, sizeof(line), "renodx_attila64: render thread not captured (error %lu)", GetLastError());
      }
    }
  }
  if (line[0] != '\0') {
    api.log("%s", line);
  }
}

}  // namespace twattila::attila64_plugin::battle_core
