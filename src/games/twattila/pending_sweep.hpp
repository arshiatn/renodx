/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

#if !defined(_WIN64)
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
  renodx::utils::log::i(std::format(
      "[Total War] Pending Sweep Trace window {}: {} sweeps, {} entries, {} on other threads, {} over the limit; object:entries{}",
      worker_pool::TraceWindow(), call_count, entries, elsewhere, dropped, list));
  call_count = 0;
  dropped = 0;
}
#endif

}  // namespace twattila::pending_sweep
