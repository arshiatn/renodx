/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

#if !defined(_WIN64)
#include <algorithm>
#include <array>
#include <format>
#include <string>

#include "../../utils/log.hpp"
#include "./worker_pool.hpp"
#endif

// Pending Sweep trace (Attila, 32-bit): the pending list sweep (a pass over one group's pending lists, one call each) counted
// for a bench. Engine code (engine_code.hpp) puts a stub at the sweep's entry only while RENODX_TW_WORKER_TRACE was set at the
// start (never for a player); the stub calls `Arm` with the list being swept. Inside a trace window of the worker pool `Arm`
// copies the call's entry count into a fixed array (no formatting, no log call in game code); `Update` prints the frame's
// records at the next present.
namespace twattila::pending_sweep {

#if !defined(_WIN64)
namespace internal {

constexpr size_t MAX_CALLS = 4096;
// The histogram's lower bounds (a sweep goes in the last bucket whose bound it reaches; the first bucket is the empty ones) and the size
// from which a split sweep would pay (design 4.4).
constexpr std::array<uint32_t, 7> BUCKET_LIMITS = {1, 16, 64, 256, 1024, 2048, 8192};
constexpr uint32_t MIN_ENTRIES = 2048;

struct Call {
  uintptr_t object = 0;     // the list being swept
  uint32_t entries = 0;     // its entry count when the sweep began
};

static std::array<Call, MAX_CALLS> calls;
static uint32_t call_count = 0;  // the owner thread's (Arm after the owner check, Update)
static uint32_t dropped = 0;     // calls over MAX_CALLS
static std::atomic<uint32_t> off_owner = 0;  // calls on other threads, counted while a window is open, not recorded

}  // namespace internal

// The stub's target (`__fastcall`: ecx = the pending list being swept, its entry count at +0x18). The sweep runs on whichever
// thread the game's code reached it on; the trace belongs to the present thread, the pool's owner.
inline void __fastcall Arm(void* object) {
  using namespace internal;
  if (!worker_pool::Tracing()) return;
  // The pool's owner word, read the way the pool's own users read it: no new pool API for one comparison.
  if (worker_pool::internal::owner.load(std::memory_order_relaxed) != GetCurrentThreadId()) {
    off_owner.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (call_count == MAX_CALLS) {
    ++dropped;
    return;
  }
  calls[call_count++] = {.object = reinterpret_cast<uintptr_t>(object),
                         .entries = *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(object) + 0x18)};
}

// Every present on the present thread, after the pool's Update (which counts the window's frames): prints and clears what the
// frame's sweeps left, one line numbered with the pool's trace window.
inline void Update() {
  using namespace internal;
  const uint32_t elsewhere = off_owner.exchange(0, std::memory_order_relaxed);
  if (call_count == 0 && dropped == 0 && elsewhere == 0) return;
  uint64_t entries = 0;
  std::string list;
  for (uint32_t index = 0; index < call_count; ++index) {
    entries += calls[index].entries;
    list += std::format(" {:x}:{}", calls[index].object, calls[index].entries);
  }
  uint64_t entries_in_large = 0;  // entries of the sweeps with at least MIN_ENTRIES
  std::array<uint32_t, BUCKET_LIMITS.size() + 1> histogram = {};
  for (uint32_t index = 0; index < call_count; ++index) {
    const uint32_t count = calls[index].entries;
    entries_in_large += (count >= MIN_ENTRIES ? count : 0);
    ++histogram[std::upper_bound(BUCKET_LIMITS.begin(), BUCKET_LIMITS.end(), count) - BUCKET_LIMITS.begin()];
  }
  std::string buckets;
  for (size_t bucket = 0; bucket < histogram.size(); ++bucket) {
    buckets += std::format(" {}{}:{}", (bucket == 0 ? 0 : BUCKET_LIMITS[bucket - 1]), (bucket == BUCKET_LIMITS.size() ? "+" : ""), histogram[bucket]);
  }
  renodx::utils::log::i(std::format(
      "[Total War] Pending Sweep Trace window {}: {} sweeps, {} entries, {} on other threads, {} over the limit; object:entries{}",
      worker_pool::TraceWindow(), call_count, entries, elsewhere, dropped, list));
  renodx::utils::log::i(std::format("[Total War] Pending Sweep Histogram window {}: sweeps by entry count from:{}; {:.0f}% of the entries are in sweeps of {}+",
                                    worker_pool::TraceWindow(), buckets,
                                    (entries == 0 ? 0.0 : 100.0 * static_cast<double>(entries_in_large) / static_cast<double>(entries)), MIN_ENTRIES));
  call_count = 0;
  dropped = 0;
}
#endif

}  // namespace twattila::pending_sweep
