/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <utility>

#include "../../utils/log.hpp"
#include "./attila64_plugin/plugin_block.h"
#include "./object_motion.hpp"
#include "./rt_scene.hpp"
#include "./shadow_pass.hpp"

// The add-on's side of the 64-bit engine plugin (renodx_attila64.dll, a loader plugin on the 64-bit port that hooks the
// game's code): its block found, and the callbacks its hooks call, registered while a feature wants them. The protocol's
// rules are plugin_block.h's.
namespace twattila::attila64_plugin_link {

// What the lookup found, for the note row (READY: the block is kept).
enum class Found : uint8_t {
  NOT_INSTALLED,
  READY,
  OFF,            // turned off in ReShade.ini
  WRONG_GAME,     // another game build
  WRONG_LOADER,   // another loader, or the plugin's file renamed
  OTHER_VERSION,  // a block this add-on does not read
};

namespace internal {

namespace plugin = twattila::attila64_plugin;

// Found READY: the plugin is pinned and never unloads, so the block stays valid for the process.
static plugin::Block* block = nullptr;
// The present thread's: the rows wanted now, whether CALLBACKS is registered, and the module reference it holds.
static uint32_t wanted = 0;
static bool registered = false;
static bool lost = false;  // another add-on holds the callbacks: not tried again until the detach
static bool registered_logged = false;
static HMODULE module_reference = nullptr;
// Written into each measured call's PassMeasure::reserved at pass_begin: a pass_end of another registration is ignored.
static std::atomic<uint32_t> epoch = 0;
// A callback caught an exception (logged once at the next present: no logging inside a callback).
static std::atomic_bool callback_failed = false;
static bool callback_failure_logged = false;

static void BeforePack() noexcept {
  try {
    // The soldiers drawn since the last pack: their motion's bones recorded and their batched corners made while the
    // pages still hold them (both return at once off the immediate context's thread or with nothing pending).
    object_motion::FlushUnitRecords();
    rt_scene::FlushUnitCaptures();
  } catch (...) {
    callback_failed.store(true, std::memory_order_relaxed);
  }
}

static plugin::PassAction PassBegin(uint32_t pass_kind, plugin::PassMeasure* measure) noexcept {
  try {
    if (!shadow_pass::Counting()) return plugin::PASS_DRAW;
    measure->reserved = epoch.load(std::memory_order_relaxed);
    return shadow_pass::PassBegin(pass_kind, measure);
  } catch (...) {
    callback_failed.store(true, std::memory_order_relaxed);
    return plugin::PASS_DRAW;
  }
}

static void PassEnd(uint32_t pass_kind, const plugin::PassMeasure& measure) noexcept {
  try {
    if (measure.reserved != epoch.load(std::memory_order_relaxed)) return;
    shadow_pass::PassEnd(pass_kind, measure);
  } catch (...) {
    callback_failed.store(true, std::memory_order_relaxed);
  }
}

static constexpr plugin::Callbacks CALLBACKS = {.before_pack = &BeforePack, .pass_begin = &PassBegin, .pass_end = &PassEnd};

// The hooks see no callback after this returns, unless the wait ran out (then this add-on stays loaded). Never from a
// callback, or under a lock a callback takes (object_motion's, rt_core's): the wait would count the caller.
inline void Unregister() {
  if (!registered) return;
  registered = false;
  wanted = 0;
  block->hooks_wanted.store(0);
  // seq_cst store, then seq_cst loads (plugin_block.h): the first 0 ends the wait, a hook entering later loads nullptr.
  block->callbacks.store(nullptr);
  const ULONGLONG start = GetTickCount64();
  for (uint32_t spins = 0; block->calls_in_flight.load() != 0; ++spins) {
    if (GetTickCount64() - start >= 1000) {
      renodx::utils::log::w("[Total War] 64-bit Attila: the engine plugin's hooks did not leave the add-on within 1 s: it stays loaded");
      return;
    }
    Sleep(spins < 64 ? 0 : 1);
  }
  FreeLibrary(std::exchange(module_reference, nullptr));
}

}  // namespace internal

// The block (once a lookup is final, at the latest the first present: the loader loads its plugins before the game's
// code runs). Not READY: why, for the note row.
inline Found Find() {
  namespace plugin = internal::plugin;
  const HMODULE module = GetModuleHandleW(plugin::MODULE_NAME);
  if (module == nullptr) return Found::NOT_INSTALLED;
  const auto block_export = reinterpret_cast<plugin::BlockExportFn>(GetProcAddress(module, plugin::BLOCK_EXPORT));
  plugin::Block* block = (block_export != nullptr ? block_export() : nullptr);
  if (block == nullptr || block->version < plugin::OLDEST_VERSION || block->bytes < plugin::BlockBytes(plugin::OLDEST_VERSION)) {
    return Found::OTHER_VERSION;
  }
  switch (block->status.load(std::memory_order_acquire)) {
    case plugin::STATUS_READY:
      internal::block = block;
      return Found::READY;
    case plugin::STATUS_OFF:
      return Found::OFF;
    case plugin::STATUS_WRONG_GAME:
      return Found::WRONG_GAME;
    case plugin::STATUS_WRONG_LOADER:
      return Found::WRONG_LOADER;
    default:
      return Found::OTHER_VERSION;
  }
}

// The rows the READY block offers (bit: attila64_plugin::Row): hooked at its init and unchanged at the game's start.
inline uint32_t RowsOffered() {
  return internal::block->rows_applied & ~internal::block->rows_changed.load(std::memory_order_relaxed);
}

// Every present on the port with the block READY (the loader's drawing thread): the rows whose hooks are wanted now (bit:
// attila64_plugin::Row). The callbacks are registered while any is wanted.
inline void Update(uint32_t wanted) {
  using internal::block;
  if (internal::callback_failed.load(std::memory_order_relaxed) && !internal::callback_failure_logged) {
    internal::callback_failure_logged = true;
    renodx::utils::log::e("[Total War] 64-bit Attila: an engine plugin callback failed (its work skipped for that call)");
  }
  wanted &= RowsOffered();
  if (wanted == internal::wanted) return;
  if (wanted == 0) {
    internal::Unregister();
    return;
  }
  if (!internal::registered) {
    if (internal::lost) return;
    // The plugin may call in while the add-on is asked to unload: a load reference of its own until the drain.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&internal::CALLBACKS),
                            &internal::module_reference)) {
      internal::lost = true;
      renodx::utils::log::e("[Total War] 64-bit Attila: no module reference for the engine plugin's callbacks (", GetLastError(), ")");
      return;
    }
    // A new epoch for each registration, seeded from the clock: an add-on loaded again at the same address starts elsewhere.
    const uint32_t last_epoch = internal::epoch.load(std::memory_order_relaxed);
    internal::epoch.store((last_epoch == 0 ? (GetTickCount() | 1u) : last_epoch + 1), std::memory_order_relaxed);
    const internal::plugin::Callbacks* expected = nullptr;
    if (!block->callbacks.compare_exchange_strong(expected, &internal::CALLBACKS)) {
      FreeLibrary(std::exchange(internal::module_reference, nullptr));
      internal::lost = true;
      renodx::utils::log::w("[Total War] 64-bit Attila: another add-on holds the engine plugin's callbacks: left to it");
      return;
    }
    internal::registered = true;
    if (!std::exchange(internal::registered_logged, true)) {
      renodx::utils::log::i("[Total War] 64-bit Attila: engine plugin callbacks registered (rows wanted ", wanted, ")");
    }
  }
  block->hooks_wanted.store(wanted);
  internal::wanted = wanted;
}

// Addon detach (after the present's last Update; never at process exit, when the loader lock holds and threads are gone:
// DllMain does not detach then).
inline void Uninstall() {
  internal::Unregister();
  internal::lost = false;
  internal::registered_logged = false;
  internal::callback_failed = false;
  internal::callback_failure_logged = false;
}

}  // namespace twattila::attila64_plugin_link
