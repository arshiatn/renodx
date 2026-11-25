/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "../../utils/log.hpp"
#include "./attila64_plugin/plugin_block.h"
#include "./object_motion.hpp"
#include "./rt_scene.hpp"
#include "./shadow_pass.hpp"

// The add-on's side of the 64-bit engine plugin (renodx_attila64.dll, a loader plugin on the 64-bit port that hooks the
// game's code): its block looked up, and the callbacks its hooks call, registered while a feature wants them. The
// protocol's rules are plugin_block.h's.
namespace twattila::attila64_plugin_link {

// What the lookup found, for the note rows. PENDING: not final yet; DETACHED: the add-on detached, no lookup until the
// next attach (neither has a note).
enum class Found : uint8_t {
  PENDING,
  DETACHED,
  NOT_INSTALLED,
  READY,
  OFF,            // turned off in ReShade.ini
  WRONG_GAME,     // another game build
  WRONG_LOADER,   // another loader version (a plugin it never started included)
  OTHER_VERSION,  // a block this add-on does not read
};

namespace internal {

namespace plugin = twattila::attila64_plugin;

// Lookup, Update and Uninstall one at a time (init_device, the present and the detach may run on different threads).
// Never taken by a callback, so Unregister's wait may run under it.
static std::shared_mutex mutex;
// The lookup's result, stored once final (release, after `block` and shadow_pass's install): readers load it with
// acquire.
static std::atomic<Found> found = Found::PENDING;
// Found READY: the plugin is pinned and never unloads, so the block stays valid for the process.
static plugin::Block* block = nullptr;
// A test's RENODX_TW_PLUGIN_WANT=<row bits>, read at the READY lookup: wanted at every present as well (F7's
// registration case: its harness has no feature that wants a row); never a player's.
static uint32_t test_wanted = 0;
// Under the mutex: the rows wanted now, whether CALLBACKS is registered, and the module reference it holds.
static uint32_t wanted = 0;
static bool registered = false;
static bool lost = false;  // another add-on holds the callbacks: not tried again until the detach
static bool registered_logged = false;
static HMODULE module_reference = nullptr;
// Written into each measured call's PassMeasure::reserved at pass_begin: a pass_end of another registration is ignored.
static std::atomic<uint32_t> epoch = 0;
// A callback caught an exception: logged once at the next present (the callbacks log only shadow_pass's learned-kind
// line).
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

static constexpr plugin::Callbacks CALLBACKS = {
    .before_pack = &BeforePack, .pass_begin = &PassBegin, .pass_end = &PassEnd};

// Under the mutex. The hooks see no callback after this returns, unless the wait ran out (then this add-on stays
// loaded). Never from a callback, or under a lock a callback takes (object_motion's, rt_core's): the wait would count
// the caller.
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
      renodx::utils::log::w(
          "[Total War] 64-bit Attila: the engine plugin's hooks did not leave the add-on within 1 s: it stays loaded");
      return;
    }
    Sleep(spins < 64 ? 0 : 1);
  }
  FreeLibrary(std::exchange(module_reference, nullptr));
}

}  // namespace internal

// The lookup's result (acquire: READY's block and shadow_pass's install are complete).
inline Found Result() {
  return internal::found.load(std::memory_order_acquire);
}

// With Result() READY: the rows the block offers (bit: attila64_plugin::Row), hooked at its init and not changed after
// (rows_changed may be set at the game's start, after the lookup: read each time).
inline uint32_t RowsOffered() {
  return internal::block->rows_applied & ~internal::block->rows_changed.load(std::memory_order_relaxed);
}

// At each init_device until READY, final at the first present (`last`): the loader loads its plugins before the game's
// code runs, before the game's device. READY installs shadow_pass before the result is published, so no present's
// Update runs before the install is complete.
inline void Lookup(bool last) {
  namespace plugin = internal::plugin;
  const std::unique_lock lock(internal::mutex);
  if (internal::found.load(std::memory_order_relaxed) != Found::PENDING) return;
  Found outcome = Found::NOT_INSTALLED;
  const char* outcome_text = "not installed";
  plugin::Block* found_block = nullptr;
  if (const HMODULE module = GetModuleHandleW(plugin::MODULE_NAME); module != nullptr) {
    const auto block_export = reinterpret_cast<plugin::BlockExportFn>(GetProcAddress(module, plugin::BLOCK_EXPORT));
    found_block = (block_export != nullptr ? block_export() : nullptr);
    outcome = Found::OTHER_VERSION;
    outcome_text = "of another version";
    if (found_block != nullptr && found_block->version >= plugin::OLDEST_VERSION
        && found_block->bytes >= plugin::BlockBytes(plugin::OLDEST_VERSION)) {
      switch (found_block->status.load(std::memory_order_acquire)) {
        case plugin::STATUS_READY:
          outcome = Found::READY;
          outcome_text = "ready";
          break;
        case plugin::STATUS_OFF:
          outcome = Found::OFF;
          outcome_text = "off (ReShade.ini)";
          break;
        case plugin::STATUS_WRONG_GAME:
          outcome = Found::WRONG_GAME;
          outcome_text = "off (another game build)";
          break;
        case plugin::STATUS_WRONG_LOADER:
          outcome = Found::WRONG_LOADER;
          outcome_text = "off (another loader version)";
          break;
        case plugin::STATUS_LOADING:
          // Still loading at the first present: a loader that loaded the plugin but never called its init.
          outcome = Found::WRONG_LOADER;
          outcome_text = "not started by the loader (another loader version)";
          break;
        default:
          break;
      }
    }
  }
  if (outcome != Found::READY && !last) return;
  uint32_t rows = 0;
  if (outcome == Found::READY) {
    internal::block = found_block;
    rows = RowsOffered();
    std::array<char, 16> value = {};
    const DWORD length =
        GetEnvironmentVariableA("RENODX_TW_PLUGIN_WANT", value.data(), static_cast<DWORD>(value.size()));
    if (length != 0 && length < value.size()) {
      std::from_chars(value.data(), value.data() + length, internal::test_wanted);
    }
    shadow_pass::Install();
  }
  internal::found.store(outcome, std::memory_order_release);
  renodx::utils::log::i(
      std::format("[Total War] 64-bit Attila: engine plugin {}, rows offered {:#x}", outcome_text, rows));
  if (internal::test_wanted != 0) {
    renodx::utils::log::i(std::format(
        "[Total War] 64-bit Attila: rows {:#x} wanted from RENODX_TW_PLUGIN_WANT (tests only)", internal::test_wanted));
  }
}

// Every present with Result() READY (the loader's drawing thread): the rows whose hooks are wanted now (bit:
// attila64_plugin::Row). The callbacks are registered while any is wanted and offered.
inline void Update(uint32_t rows_wanted) {
  using internal::block;
  const std::unique_lock lock(internal::mutex);
  // A present still running while the add-on detaches: Uninstall came first (DETACHED), nothing is registered again.
  if (internal::found.load(std::memory_order_relaxed) != Found::READY) return;
  if (internal::callback_failed.load(std::memory_order_relaxed) && !internal::callback_failure_logged) {
    internal::callback_failure_logged = true;
    renodx::utils::log::e(
        "[Total War] 64-bit Attila: an engine plugin callback failed (its work skipped for that call)");
  }
  const uint32_t rows = (rows_wanted | internal::test_wanted) & RowsOffered();
  if (rows == internal::wanted) return;
  if (rows == 0) {
    internal::Unregister();
    return;
  }
  if (!internal::registered) {
    if (internal::lost) return;
    // The plugin may call in while the add-on is asked to unload: a load reference of its own until the drain.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&internal::CALLBACKS),
                            &internal::module_reference)) {
      internal::lost = true;
      renodx::utils::log::e("[Total War] 64-bit Attila: no module reference for the engine plugin's callbacks (",
                            GetLastError(), ")");
      return;
    }
    // A new epoch for each registration, seeded from the clock: an add-on loaded again at the same address starts
    // elsewhere.
    const uint32_t last_epoch = internal::epoch.load(std::memory_order_relaxed);
    internal::epoch.store((last_epoch == 0 ? (GetTickCount() | 1u) : last_epoch + 1), std::memory_order_relaxed);
    const internal::plugin::Callbacks* expected = nullptr;
    if (!block->callbacks.compare_exchange_strong(expected, &internal::CALLBACKS)) {
      FreeLibrary(std::exchange(internal::module_reference, nullptr));
      internal::lost = true;
      renodx::utils::log::w(
          "[Total War] 64-bit Attila: another add-on holds the engine plugin's callbacks: left to it");
      return;
    }
    internal::registered = true;
    if (!std::exchange(internal::registered_logged, true)) {
      renodx::utils::log::i(
          std::format("[Total War] 64-bit Attila: engine plugin callbacks registered (rows wanted {:#x})", rows));
    }
  }
  block->hooks_wanted.store(rows);
  internal::wanted = rows;
}

// Addon attach: the lookup runs again (an add-on kept loaded across a detach).
inline void Install() {
  const std::unique_lock lock(internal::mutex);
  internal::found.store(Found::PENDING, std::memory_order_relaxed);
}

// Addon detach (ReShade's unload at the last device's release, threads alive; our DllMain does not detach at process
// exit). Waits for a present inside Update; a present after it finds DETACHED: no lookup, nothing registered.
inline void Uninstall() {
  const std::unique_lock lock(internal::mutex);
  internal::Unregister();
  // The block stays: the plugin is pinned, and a reader that saw READY (a row's visibility, a note) may still read it.
  internal::found.store(Found::DETACHED, std::memory_order_relaxed);
  internal::test_wanted = 0;
  internal::lost = false;
  internal::registered_logged = false;
  internal::callback_failed = false;
  internal::callback_failure_logged = false;
}

}  // namespace twattila::attila64_plugin_link
