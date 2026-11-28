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
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"
#include "./attila64_plugin/plugin_block.h"
#include "./attila64_plugin/tweaks.h"
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
  PLUGINS_OFF,    // not loaded: ATTILA_NO_PLUGINS or ATTILA_PLAIN is set (the loader then loads no plugin)
  NOT_LOADED,     // in <exe dir>\plugins\ but not loaded (the loader's log says why)
};

// Keep Battle Core Free's, Keep Render Core Free's and Faster Battle Step's defaults: the plugin's initial core_wanted
// bits.
inline constexpr bool BATTLE_CORE_DEFAULT =
    (attila64_plugin::CORE_WANTED_DEFAULT & attila64_plugin::CORE_BATTLE) != 0;
inline constexpr bool RENDER_CORE_DEFAULT =
    (attila64_plugin::CORE_WANTED_DEFAULT & attila64_plugin::CORE_RENDER) != 0;
inline constexpr bool BATTLE_STEP_DEFAULT =
    (attila64_plugin::CORE_WANTED_DEFAULT & attila64_plugin::CORE_STEP) != 0;

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
// Under the mutex: Keep Battle Core Free's and Keep Render Core Free's core_wanted bits as this add-on last wrote (or,
// for the render row's first present at its default, left) them (none yet: nullopt).
static std::optional<bool> battle_core_written;
static std::optional<bool> render_core_written;
// Under the mutex: Effects on All Cores' stop (0 Off, 1 Automatic, 2 On) as this add-on last wrote the two effect bits
// (none yet: nullopt).
static std::optional<uint32_t> effects_written;
// Under the mutex: Faster Battle Step's CORE_STEP bit as this add-on last wrote it (none yet: nullopt).
static std::optional<bool> battle_step_written;
// Under the mutex: Multithreaded Collisions' CORE_SPLIT bit as this add-on last wrote it (none yet: nullopt).
static std::optional<bool> contact_split_written;
// The loader's effect value (tweaks.h): the plugin checks its guards with it at the game's start (PK9 7.3). Its slot
// is looked up by name for the verdict only; no Tweak is bound to it, so the plugin's walk hook stays its one writer.
inline constexpr std::string_view EFFECTS_SLOT = "VFX_PARALLEL_UPDATER";
static_assert(std::ranges::any_of(plugin::TWEAKS, [](const plugin::TweakEntry& entry) {
  return entry.loader_writes && entry.name == EFFECTS_SLOT;
}), "tweaks.h lists the loader's effect value");
// The rows whose refusal raises the "could not turn on every engine option" note: all but the CPU row (refused, its rows
// hide and the plugin's log says why).
inline constexpr uint32_t KNOWN_ROWS = ((1u << plugin::ROW_COUNT) - 1u) & ~plugin::RowBit(plugin::ROW_CPU_PLACEMENT);

// plugin_block.h's ini names in ReShade's narrow form (the plugin reads them as UTF-16; they are ASCII).
template <size_t N>
constexpr std::array<char, N> Narrow(const wchar_t (&wide)[N]) {
  std::array<char, N> narrow = {};
  for (size_t index = 0; index < N; ++index) narrow[index] = static_cast<char>(wide[index]);
  return narrow;
}
inline constexpr auto VALUES_SECTION = Narrow(plugin::INI_VALUES_SECTION);
inline constexpr auto SOL_FRAME_CAP_KEY = Narrow(plugin::INI_SOL_FRAME_CAP);
inline constexpr auto SHADOW_MAP_SIZE_KEY = Narrow(plugin::INI_SHADOW_MAP_SIZE);
inline constexpr auto SHADOW_CASCADES_KEY = Narrow(plugin::INI_SHADOW_CASCADES);
inline constexpr auto FASTER_GAME_CODE_KEY = Narrow(plugin::INI_FASTER_GAME_CODE);
// The scales_active bits of each scale row's hooks: set only while the row is offered (a refused row's hook may stay
// in, the plugin's main.cpp).
inline constexpr std::array<std::pair<plugin::Row, uint32_t>, 5> ROW_SCALES = {{
    {plugin::ROW_UNIT_LOD, (1u << plugin::SCALE_MODEL_DETAIL) - (1u << plugin::SCALE_UNIT_LOD)},
    {plugin::ROW_MODEL_DETAIL, 1u << plugin::SCALE_MODEL_DETAIL},
    // ROW_GRASS_CELLS reads 19 and 20 as well, but cuts only what ROW_GRASS's hooks reached: no bits of its own (a
    // refused grass row leaves its cell hook without work).
    {plugin::ROW_GRASS,
     (1u << plugin::SCALE_GRASS_LOD) | (1u << plugin::SCALE_GRASS_RADIUS) | (1u << plugin::SCALE_GRASS_DENSITY)},
    {plugin::ROW_CAMPAIGN_TREES, 1u << plugin::SCALE_CAMPAIGN_TREES},
    {plugin::ROW_SPRITES, (1u << plugin::SCALE_SPRITE_DISTANCE) | (1u << plugin::SCALE_DEAD_UNITS)},
}};

// A restart-only engine value a row writes, by its slot's index: the row's Tweak points `value` at `bits`, where
// engine_tweaks' Apply writes the slider's bits; the restart mirror copies them, clamped, into ReShade.ini.
struct RestartValue {
  uint32_t bits = 0;
  const plugin::TweakSlot* slot = nullptr;    // null: no row writes this slot
  const plugin::TweakEntry* entry = nullptr;  // its range (tweaks.h)
  bool ini_read = false;
  // The bits in ReShade.ini: as read at READY (absent: the game's default; text the plugin refuses: none), then as written.
  std::optional<uint32_t> mirrored;
};
// Under the mutex, filled by FindTweaks; kept across a detach (the Tweaks point into it until the next FindTweaks).
static std::vector<RestartValue> restart_values;
// FindTweaks has run (release: the Tweaks' values are final for readers that load it with acquire).
static std::atomic_bool tweaks_found = false;
static std::atomic_bool tweaks_refused = false;  // a value a row uses refused (another type, name or default)
static std::atomic_bool values_pending = false;
// Loader Frame Cap: the key as this start read it (the first READY present, before any write; the process's), the key
// as last read or written, and whether the row differs from this start's.
static std::optional<bool> sol_frame_cap_at_start;
static std::optional<int> sol_frame_cap_mirrored;
static std::atomic_bool sol_frame_cap_pending = false;
// Shadow Map Size the same way: the key as this start read it (a value the plugin refuses: 0, the game's sizes), the
// key as last read or written, and whether the row differs from this start's.
static std::optional<int> shadow_map_size_at_start;
static std::optional<int> shadow_map_size_mirrored;
static std::atomic_bool shadow_map_size_pending = false;
// Two Shadow Cascades Under Rays' key: as this start read it (as the plugin's GetPrivateProfileIntW reads it: exactly
// 1 writes), and as last read or written (a plain decimal number; other text: none, so the mirror writes it again).
static std::optional<bool> shadow_cascades_at_start;
static std::optional<int> shadow_cascades_mirrored;
// Faster Game CPU Code the same way: the key as this start read it (as the plugin's GetPrivateProfileIntW reads it), the
// key read since the lookup, the key as last read (a plain decimal number; other text: none, so the row writes it again)
// or written, and whether the row differs from this start's.
static std::optional<bool> game_code_at_start;
static bool game_code_read = false;
static std::optional<int> game_code_mirrored;
static std::atomic_bool game_code_pending = false;
// Under the mutex: the plugin's notices bits already logged.
static uint32_t notices_logged = 0;
// Under the mutex: the engine lanes (bit 1 << shift) that read ENGINE_OTHER_MOD, logged once per add-on load (a detach
// keeps them).
static uint32_t other_mod_logged = 0;
// Written into each measured call's PassMeasure::reserved at pass_begin: a pass_end of another registration is ignored.
static std::atomic<uint32_t> epoch = 0;
// A callback caught an exception: logged once at the next present (the callbacks log only shadow_pass's learned-kind
// line).
static std::atomic_bool callback_failed = false;
static bool callback_failure_logged = false;
// UnitFlushRunsHere's reads from a draw callback (no mutex there): the rows registered now (`wanted` as Update stored
// it, 0 from Unregister on) and the thread of the last BeforePack since this registration (none before its first). Two
// atomics, not one state under a lock: each alone makes the answer false (nothing registered, or the pack on another
// thread) and no invariant spans both. Both stores of the mirror come before hooks_wanted's (Update's and Unregister's):
// a draw decided after the bone flush stops never waits for it.
static std::atomic<uint32_t> registered_rows = 0;
static std::atomic<std::thread::id> pack_thread;
static_assert(std::atomic<std::thread::id>::is_always_lock_free);
// Under the mutex: Batched Unit Capture's flush line as last logged (bit 0 offered, 1 registered, 2 on the draw thread;
// none yet: nullopt).
static std::optional<uint32_t> unit_flush_logged;

static void BeforePack() noexcept {
  pack_thread.store(std::this_thread::get_id(), std::memory_order_relaxed);
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
  // Before the hooks stop: a batch decided from here on waits for no flush (Update's rows-0 branch and Uninstall end
  // here; a mirror kept would batch soldiers whose flush never comes, their shadows missing).
  registered_rows.store(0, std::memory_order_relaxed);
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
  // Drained: no BeforePack runs now, so the next registration answers false until its own first one.
  pack_thread.store(std::thread::id(), std::memory_order_relaxed);
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

// With Result() READY: the block has v2's fields. A v1 block (an older plugin) offers its v1 rows only.
inline bool BlockV2() {
  return internal::block->version >= 2 && internal::block->bytes >= internal::plugin::BlockBytes(2);
}

// The plugin offers the bone flush before each pack (a v1 block too: BONE_FLUSH is its row 0). Batched Unit Capture's
// row shows with it (rt_scene Host::unit_flush_offered); the port's Update asks for the row while the batch is wanted,
// and Update keeps it only while offered. Not whether the flush is registered: that follows from the batch wanted.
inline bool UnitFlushOffered() {
  namespace plugin = internal::plugin;
  return Result() == Found::READY && (RowsOffered() & plugin::RowBit(plugin::ROW_BONE_FLUSH)) != 0;
}

// The bone flush runs before each pack on this thread: BONE_FLUSH registered now and the last BeforePack on this
// thread. Batched Unit Capture's batch decision on the draw thread (rt_scene Host::flushes_unit_captures on the port):
// batched draws wait for this flush, so before this registration's first BeforePack, or with the pack on another
// thread, nothing is batched. Lock-free (a draw callback).
inline bool UnitFlushRunsHere() {
  namespace plugin = internal::plugin;
  return (internal::registered_rows.load(std::memory_order_relaxed) & plugin::RowBit(plugin::ROW_BONE_FLUSH)) != 0
         && internal::pack_thread.load(std::memory_order_relaxed) == std::this_thread::get_id();
}

// With Result() READY and BlockV2(): the rows the plugin tried (bit: attila64_plugin::Row), final at its READY. Rows
// switched off in the ini are not tried.
inline uint32_t RowsTried() {
  return internal::block->rows_tried;
}

// With Result() READY: the battle part's core_status (attila64_plugin::CoreState and its fields); 0 for a v1 block (it
// has no such field) or a plugin that did not build the part.
inline uint64_t BattleCoreStatus() {
  return BlockV2() ? internal::block->core_status.load(std::memory_order_relaxed) : 0;
}

// With Result() READY: the render part's render_core_status (attila64_plugin::CoreState and its fields, its own error
// byte); 0 for a v1 block or a plugin that did not build the part.
inline uint32_t RenderCoreStatus() {
  return BlockV2() ? internal::block->render_core_status.load(std::memory_order_relaxed) : 0;
}

// With Result() READY and BlockV2(): the render part's core_wanted bit as the plugin reads it now (the row's, or the
// plugin's own RenderCore key while the row was not changed).
inline bool RenderCoreWanted() {
  return (internal::block->core_wanted.load(std::memory_order_relaxed) & internal::plugin::CORE_RENDER) != 0;
}

// With Result() READY: an engine row's lane of engine_status (attila64_plugin::EngineState) at `shift`
// (ENGINE_STEP_SHIFT, ENGINE_SPLIT_SHIFT); 0 for a v1 block or a plugin that did not build the row.
inline uint32_t EngineLaneState(uint32_t shift) {
  if (!BlockV2()) return 0;
  return (internal::block->engine_status.load(std::memory_order_relaxed) >> shift) & internal::plugin::ENGINE_LANE_MASK;
}

// With Result() READY and BlockV2(): an engine option the plugin could not turn on: a known row tried but not offered
// (its site refused, or changed after it), or a value a row uses refused (known once FindTweaks has run). A value this
// game build lacks (ABSENT) hides its row without this.
inline bool OptionsRefused() {
  return (RowsTried() & internal::KNOWN_ROWS & ~RowsOffered()) != 0
         || internal::tweaks_refused.load(std::memory_order_relaxed);
}

// With Result() READY: the loader's effect slot checked OK at the game's start (its guards and flag read as expected),
// so the plugin's walk hook may act on Effects on All Cores' bits. False before the plugin's check (TWEAKS_READY,
// acquire: the slots are final), on a v1 block, or with Tweaks=0.
inline bool EffectsSlotOk() {
  namespace plugin = internal::plugin;
  if (!BlockV2() || internal::block->tweaks_status.load(std::memory_order_acquire) != plugin::TWEAKS_READY) return false;
  return std::ranges::any_of(std::span(internal::block->tweaks, internal::block->tweak_count),
                             [](const plugin::TweakSlot& slot) {
                               return slot.name != nullptr && slot.name == internal::EFFECTS_SLOT
                                      && slot.verdict == plugin::VERDICT_OK;
                             });
}

// FindTweaks has run since the lookup: the Tweaks' values are final (acquire).
inline bool TweaksFound() {
  return internal::tweaks_found.load(std::memory_order_acquire);
}

// The restart mirror's pending states, for the "Restart the game now" notes: a restart-only value the next start would
// read other than this start did, Loader Frame Cap's, Shadow Map Size's and Faster Game CPU Code's.
inline bool ValuesPending() {
  return internal::values_pending.load(std::memory_order_relaxed);
}
inline bool SolFrameCapPending() {
  return internal::sol_frame_cap_pending.load(std::memory_order_relaxed);
}
inline bool ShadowMapSizePending() {
  return internal::shadow_map_size_pending.load(std::memory_order_relaxed);
}
inline bool GameCodePending() {
  return internal::game_code_pending.load(std::memory_order_relaxed);
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
  const HMODULE module = GetModuleHandleW(plugin::MODULE_NAME);
  if (module != nullptr) {
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
  if (module == nullptr) {
    // The loader loads no plugin while either variable is set, even to nothing (it tests getenv for non-null): the
    // size query is 0 only for a variable that is not there.
    std::array<wchar_t, MAX_PATH> exe_path = {};
    const DWORD exe_length = GetModuleFileNameW(nullptr, exe_path.data(), static_cast<DWORD>(exe_path.size()));
    std::error_code error;
    if (GetEnvironmentVariableW(L"ATTILA_NO_PLUGINS", nullptr, 0) != 0
        || GetEnvironmentVariableW(L"ATTILA_PLAIN", nullptr, 0) != 0) {
      outcome = Found::PLUGINS_OFF;
      outcome_text = "not loaded: plugins switched off (ATTILA_NO_PLUGINS / ATTILA_PLAIN)";
    } else if (exe_length != 0 && exe_length < exe_path.size()
               && std::filesystem::exists(
                   std::filesystem::path(exe_path.data()).parent_path() / L"plugins" / plugin::MODULE_NAME, error)) {
      outcome = Found::NOT_LOADED;
      outcome_text = "in plugins\\ but not loaded (attila64_log.txt says why)";
    }
  }
  uint32_t rows = 0;
  if (outcome == Found::READY) {
    internal::block = found_block;
    rows = RowsOffered();
    if (!BlockV2()) {
      outcome_text = "ready (block v1: an older plugin, its first rows only)";
    }
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
  // Its own line: the start checks read the line above to its end.
  if (outcome == Found::READY && BlockV2()) {
    renodx::utils::log::i(
        std::format("[Total War] 64-bit Attila: engine plugin rows tried {:#x}", RowsTried()));
  }
  if (internal::test_wanted != 0) {
    renodx::utils::log::i(std::format(
        "[Total War] 64-bit Attila: rows {:#x} wanted from RENODX_TW_PLUGIN_WANT (tests only)", internal::test_wanted));
  }
}

// Every present with Result() READY (the loader's drawing thread): the rows whose hooks are wanted now (bit:
// attila64_plugin::Row), whether Keep Battle Core Free and Keep Render Core Free are on, Effects on All Cores' stop
// (0 Off, 1 Automatic, 2 On), whether Faster Battle Step is on and whether Multithreaded Collisions is on (nullopt:
// its row hidden, the plugin's bit left alone; tw_unit_shadows' calls pass none: Off, the plugin's default and nullopt).
// The callbacks are registered while any row is wanted and offered.
inline void Update(uint32_t rows_wanted, bool battle_core, bool render_core, uint32_t effects_stop = 0,
                   bool battle_step = BATTLE_STEP_DEFAULT, std::optional<bool> contact_split = std::nullopt) {
  using internal::block;
  namespace plugin = internal::plugin;
  const std::unique_lock lock(internal::mutex);
  // A present still running while the add-on detaches: Uninstall came first (DETACHED), nothing is registered again.
  if (internal::found.load(std::memory_order_relaxed) != Found::READY) return;
  // Only the row's own bit changes (the plugin's other parts keep theirs): at the first present after READY and after
  // every change of the row, a preset slot's load included. The plugin applies it at its next battle step or watchdog
  // tick.
  // The line is logged while the plugin offers the bit's row (`row`).
  const auto write_core_bit = [&](uint32_t bit, bool on, plugin::Row row, const char* name) {
    if (on) {
      block->core_wanted.fetch_or(bit, std::memory_order_relaxed);
    } else {
      block->core_wanted.fetch_and(~bit, std::memory_order_relaxed);
    }
    if ((RowsOffered() & plugin::RowBit(row)) != 0) {
      renodx::utils::log::i(std::format("[Total War] 64-bit Attila: {} {}", name, (on ? "on" : "off")));
    }
  };
  if (BlockV2() && internal::battle_core_written != battle_core) {
    write_core_bit(plugin::CORE_BATTLE, battle_core, plugin::ROW_CPU_PLACEMENT, "Keep Battle Core Free");
    internal::battle_core_written = battle_core;
  }
  // The render row's first present writes only a row off its default: at its default the bit stays the plugin's (its
  // own RenderCore=1 included) until the row changes. With RenderCore=0 (OFF_INI: off for the process) nothing is
  // written.
  if (BlockV2() && internal::render_core_written != render_core) {
    const bool off_in_ini =
        ((RenderCoreStatus() >> plugin::CORE_STATE_SHIFT) & plugin::CORE_FIELD_MASK) == plugin::CORE_OFF_INI;
    if (!off_in_ini && (internal::render_core_written.has_value() || render_core != RENDER_CORE_DEFAULT)) {
      write_core_bit(plugin::CORE_RENDER, render_core, plugin::ROW_CPU_PLACEMENT, "Keep Render Core Free");
    }
    internal::render_core_written = render_core;
  }
  // Effects on All Cores: only bits 4-5 change, each stop change one fetch_and or fetch_or, so the walk hook never reads
  // a stop between the old and the new one (src: PK9 7.5's six transitions; the clear first for a pair the add-on never
  // writes, CORE_EFFECTS_AUTO alone, which reads as Off). From the first present on the add-on owns the bits, Off
  // included (7.5: the plugin's EffectsMode key holds only until then; it is the way in for an A/B without the add-on).
  if (const uint32_t stop = std::min(effects_stop, 2u); BlockV2() && internal::effects_written != stop) {
    // Per stop (plugin_block.h): Off neither bit, Automatic both, On CORE_EFFECTS alone.
    constexpr std::array<uint32_t, 3> STOP_BITS = {0u, plugin::CORE_EFFECTS | plugin::CORE_EFFECTS_AUTO,
                                                   plugin::CORE_EFFECTS};
    constexpr std::array<const char*, 3> STOP_NAMES = {"Off", "Automatic", "On"};
    const uint32_t target = STOP_BITS[stop];
    const uint32_t bits_now =
        block->core_wanted.load(std::memory_order_relaxed) & (plugin::CORE_EFFECTS | plugin::CORE_EFFECTS_AUTO);
    if ((bits_now & ~target) != 0) {
      block->core_wanted.fetch_and(~(bits_now & ~target), std::memory_order_relaxed);
    }
    if ((target & ~bits_now) != 0) {
      block->core_wanted.fetch_or(target & ~bits_now, std::memory_order_relaxed);
    }
    // While the hook can act on the bits: its row offered and the loader's slot OK.
    if ((RowsOffered() & plugin::RowBit(plugin::ROW_PARALLEL_EFFECTS)) != 0 && EffectsSlotOk()) {
      renodx::utils::log::i(std::format("[Total War] 64-bit Attila: Effects on All Cores {}", STOP_NAMES[stop]));
    }
    internal::effects_written = stop;
  }
  // Faster Battle Step: from the first present on the add-on owns CORE_STEP, Off included, as Effects on All Cores owns
  // its bits (src: PK9 7.5 via rp94_ MUST-1; ours for this bit, the PK8 design names no other rule: its BattleStep key
  // 1 is the way in for a start without the add-on, and 0 leaves the site out for the process, lane OFF_INI). The
  // plugin reads the bit at the hook's next call.
  if (BlockV2() && internal::battle_step_written != battle_step) {
    write_core_bit(plugin::CORE_STEP, battle_step, plugin::ROW_BATTLE_STEP, "Faster Battle Step");
    internal::battle_step_written = battle_step;
  }
  // Multithreaded Collisions: CORE_SPLIT is written only while the row shows (src: PK2P P1a via rp1a_ S1, a hidden row
  // writes no bit; deviation: from W-A3's form above, which writes its hidden default, because the batch's arms set
  // ContactSplit=1 / 0 in the plugin's ini with the add-on loaded, and a hidden Off would clear their bit). Then, as
  // PK8 Stage B 3.7 has it, fetch_or / fetch_and while the plugin offers ROW_CONTACT_SPLIT (a site left out or changed:
  // nothing to switch). The plugin reads the bit at the hook's next call.
  if (contact_split.has_value() && BlockV2() && (RowsOffered() & plugin::RowBit(plugin::ROW_CONTACT_SPLIT)) != 0
      && internal::contact_split_written != contact_split) {
    write_core_bit(plugin::CORE_SPLIT, *contact_split, plugin::ROW_CONTACT_SPLIT, "Multithreaded Collisions");
    internal::contact_split_written = contact_split;
  }
  if (internal::callback_failed.load(std::memory_order_relaxed) && !internal::callback_failure_logged) {
    internal::callback_failure_logged = true;
    renodx::utils::log::e(
        "[Total War] 64-bit Attila: an engine plugin callback failed (its work skipped for that call)");
  }
  // Batched Unit Capture's flush as the presents so far left it (a registration below shows at the next present), once
  // per change: offered, registered, and UnitFlushRunsHere as the batch decision reads it (the port's present runs on
  // its draw thread).
  const bool flush_offered = UnitFlushOffered();
  const bool flush_registered = (internal::wanted & plugin::RowBit(plugin::ROW_BONE_FLUSH)) != 0;
  const bool flush_here = UnitFlushRunsHere();
  if (const uint32_t flush_state = (flush_offered ? 1u : 0u) | (flush_registered ? 2u : 0u) | (flush_here ? 4u : 0u);
      internal::unit_flush_logged != flush_state) {
    internal::unit_flush_logged = flush_state;
    renodx::utils::log::i(std::format(
        "[Total War] 64-bit Attila: unit flush offered {:d}, registered {:d}, on the draw thread {:d}", flush_offered,
        flush_registered, flush_here));
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
  internal::registered_rows.store(rows, std::memory_order_relaxed);
  block->hooks_wanted.store(rows);
  internal::wanted = rows;
}

// Every present with Result() READY until it returns true (the loader's drawing thread, before the values' Update):
// each Tweak of `options` gets the value the plugin checked, matched by name (whichever side is newer). Live: the game's
// own value; restart-only: an add-on cell holding the game's default (the restart mirror's). Another verdict, another
// type than the add-on writes, a range this add-on does not know, or a name the plugin does not list: no value, and then
// none for its whole option (its row hides). False while the plugin's check is pending; true without values on a v1 block or with Tweaks=0.
inline bool FindTweaks(std::span<tw::engine_tweaks::Option* const> options) {
  using internal::block;
  namespace plugin = internal::plugin;
  const std::unique_lock lock(internal::mutex);
  if (internal::found.load(std::memory_order_relaxed) != Found::READY) return false;
  const uint32_t status = (BlockV2() ? block->tweaks_status.load(std::memory_order_acquire) : plugin::TWEAKS_NONE);
  if (status == plugin::TWEAKS_PENDING) return false;
  const std::span<const plugin::TweakSlot> slots(block->tweaks, (status == plugin::TWEAKS_READY ? block->tweak_count : 0u));
  internal::restart_values.assign(slots.size(), {});
  std::string refused;
  for (tw::engine_tweaks::Option* option : options) {
    for (tw::engine_tweaks::Tweak& tweak : option->tweaks) {
      tweak.value = nullptr;
      const auto slot = std::ranges::find_if(slots, [&tweak](const plugin::TweakSlot& candidate) {
        return candidate.name != nullptr && tweak.name == candidate.name;
      });
      if (slot == slots.end() || slot->verdict == plugin::VERDICT_ABSENT) continue;
      // engine_tweaks writes a flag's byte, an int32 or a float.
      bool same_type = (slot->type == plugin::TWEAK_FLOAT);
      if (tweak.flag) {
        same_type = (slot->type == plugin::TWEAK_BOOL);
      } else if (tweak.integer) {
        same_type = (slot->type == plugin::TWEAK_INT || slot->type == plugin::TWEAK_UINT);
      }
      auto entry = plugin::TWEAKS.end();
      if (slot->restart_only != 0) {
        entry = std::ranges::find_if(plugin::TWEAKS, [&tweak](const plugin::TweakEntry& known) {
          return known.restart_only && tweak.name == known.name;
        });
      }
      // A restart-only value also needs this add-on's range for it, of the slot's type (the mirror clamps by it).
      if (slot->verdict != plugin::VERDICT_OK || !same_type
          || (slot->restart_only != 0 && (entry == plugin::TWEAKS.end() || entry->type != slot->type))) {
        refused += std::format("{}{}", (refused.empty() ? "" : ", "), tweak.name);
        continue;
      }
      if (slot->restart_only == 0) {
        tweak.value = reinterpret_cast<void*>(static_cast<uintptr_t>(slot->value_address));
        continue;
      }
      internal::RestartValue& restart = internal::restart_values[static_cast<size_t>(slot - slots.begin())];
      restart = {.bits = slot->default_bits, .slot = &*slot, .entry = &*entry};
      tweak.value = &restart.bits;
    }
    // An option missing a value gets none (its row hides): nothing applied part-way while the player cannot see it. Its
    // restart cells go too, so the mirror neither writes their default over a saved key nor raises a note for them.
    if (std::ranges::any_of(option->tweaks, [](const tw::engine_tweaks::Tweak& tweak) { return tweak.value == nullptr; })) {
      for (tw::engine_tweaks::Tweak& tweak : option->tweaks) {
        const auto cell = std::ranges::find(internal::restart_values, tweak.value,
                                            [](internal::RestartValue& restart) -> void* { return &restart.bits; });
        if (cell != internal::restart_values.end()) {
          *cell = {};
        }
        tweak.value = nullptr;
      }
    }
  }
  internal::tweaks_refused.store(!refused.empty(), std::memory_order_relaxed);
  internal::tweaks_found.store(true, std::memory_order_release);
  // The log's side of the "could not turn on every engine option" note.
  if (BlockV2() && OptionsRefused()) {
    renodx::utils::log::w(std::format("[Total War] 64-bit Attila: engine options not turned on: rows {:#x}{}{}",
                                      RowsTried() & internal::KNOWN_ROWS & ~RowsOffered(),
                                      (refused.empty() ? "" : ", values "), refused));
  }
  return true;
}

// Every present with Result() READY on a v2 block, after the values' Update (the loader's drawing thread): the next
// start's restart-only values, Loader Frame Cap (`sol_frame_cap`), Shadow Map Size (`shadow_map_size`) and Faster Game
// CPU Code (`faster_game_code`; these two while their row is offered: a hidden row's saved key stays) into ReShade.ini
// [Attila64Plugin.Values], which the plugin reads at its init. A value is clamped to its slider's range first
// (tweaks.h's, as the plugin checks: a hand-edited ReShade.ini may hold any slider value). Each key is read once, then
// written only when it differs, with one log line per write (ReShade saves the file about 1 s later, at a present).
// Pending: a value whose next start differs from this start's
// (the bits it applied, else the default; a key it refused or did not apply: always), and each of the three rows against
// the key this start read.
inline void MirrorRestartValues(bool sol_frame_cap, int shadow_map_size, bool faster_game_code) {
  namespace plugin = internal::plugin;
  const std::unique_lock lock(internal::mutex);
  if (internal::found.load(std::memory_order_relaxed) != Found::READY || !BlockV2()) return;
  bool pending = false;
  // The cells of this READY's find only: Uninstall keeps the last ones (the add-on's detach still reads the Tweaks' values
  // in them after it), so before the next find they are not walked.
  const bool values_found = internal::tweaks_found.load(std::memory_order_relaxed);
  for (internal::RestartValue& restart : internal::restart_values) {
    if (!values_found || restart.slot == nullptr) continue;
    const uint32_t low = restart.entry->low_bits;
    const uint32_t high = restart.entry->high_bits;
    uint32_t bits = restart.bits;
    switch (restart.entry->type) {
      case plugin::TWEAK_FLOAT:
        // A NaN to the low end.
        if (!(std::bit_cast<float>(bits) >= std::bit_cast<float>(low))) {
          bits = low;
        } else if (std::bit_cast<float>(bits) > std::bit_cast<float>(high)) {
          bits = high;
        }
        break;
      case plugin::TWEAK_INT:
        bits = static_cast<uint32_t>(
            std::clamp(static_cast<int32_t>(bits), static_cast<int32_t>(low), static_cast<int32_t>(high)));
        break;
      default:
        bits = std::clamp(bits, low, high);
        break;
    }
    if (!std::exchange(restart.ini_read, true)) {
      restart.mirrored = restart.slot->default_bits;
      std::array<char, 16> text = {};
      size_t size = text.size();
      if (reshade::get_config_value(nullptr, internal::VALUES_SECTION.data(), restart.slot->name, text.data(), &size)) {
        // The plugin reads exactly 8 hex digits.
        uint32_t parsed = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + 8, parsed, 16);
        const bool eight_hex_digits = (size == 9 && error == std::errc() && end == text.data() + 8);
        restart.mirrored = (eight_hex_digits ? std::optional<uint32_t>(parsed) : std::nullopt);
      }
    }
    const uint32_t this_start =
        (restart.slot->restart_state == plugin::RESTART_APPLIED ? restart.slot->written_bits : restart.slot->default_bits);
    if (restart.mirrored != bits) {
      reshade::set_config_value(nullptr, internal::VALUES_SECTION.data(), restart.slot->name,
                                std::format("{:08x}", bits).c_str());
      restart.mirrored = bits;
      renodx::utils::log::i(std::format("[Total War] 64-bit Attila: engine value {}: {:#x} at the next start (this start: {:#x})",
                                        restart.slot->name, bits, this_start));
    }
    pending = pending || bits != this_start || restart.slot->restart_state == plugin::RESTART_OUT_OF_RANGE
              || restart.slot->restart_state == plugin::RESTART_NOT_DX11;
  }
  internal::values_pending.store(pending, std::memory_order_relaxed);
  if (!internal::sol_frame_cap_mirrored.has_value()) {
    int key = 0;  // absent: off, as the plugin reads it
    reshade::get_config_value(nullptr, internal::VALUES_SECTION.data(), internal::SOL_FRAME_CAP_KEY.data(), key);
    internal::sol_frame_cap_mirrored = key;
    if (!internal::sol_frame_cap_at_start.has_value()) internal::sol_frame_cap_at_start = (key == 1);
  }
  const bool cap_at_start = *internal::sol_frame_cap_at_start;
  if (const int key = (sol_frame_cap ? 1 : 0); internal::sol_frame_cap_mirrored != key) {
    reshade::set_config_value(nullptr, internal::VALUES_SECTION.data(), internal::SOL_FRAME_CAP_KEY.data(), key);
    internal::sol_frame_cap_mirrored = key;
    renodx::utils::log::i(std::format("[Total War] 64-bit Attila: engine value {}: {:#x} at the next start (this start: {:#x})",
                                      internal::SOL_FRAME_CAP_KEY.data(), key, (cap_at_start ? 1 : 0)));
  }
  internal::sol_frame_cap_pending.store(sol_frame_cap != cap_at_start, std::memory_order_relaxed);
  bool shadow_pending = false;
  if ((RowsOffered() & plugin::RowBit(plugin::ROW_SHADOW_MAP)) != 0) {
    if (!internal::shadow_map_size_mirrored.has_value()) {
      int key = 0;  // absent: the game's sizes
      reshade::get_config_value(nullptr, internal::VALUES_SECTION.data(), internal::SHADOW_MAP_SIZE_KEY.data(), key);
      internal::shadow_map_size_mirrored = key;
      if (!internal::shadow_map_size_at_start.has_value()) {
        internal::shadow_map_size_at_start = ((key >= 0 && key <= plugin::SHADOW_MAP_SIZE_STEPS) ? key : 0);
      }
    }
    // This start's sizes: the key's, unless the plugin saw the route as not DX11 and wrote nothing (seen on a
    // restart-only value with a key: NOT_DX11; without such a key, the key's).
    const bool not_dx11 = values_found && std::ranges::any_of(internal::restart_values, [](const internal::RestartValue& restart) {
      return restart.slot != nullptr && restart.slot->restart_state == plugin::RESTART_NOT_DX11;
    });
    const int size_at_start = (not_dx11 ? 0 : *internal::shadow_map_size_at_start);
    const int size = std::clamp(shadow_map_size, 0, plugin::SHADOW_MAP_SIZE_STEPS);
    if (internal::shadow_map_size_mirrored != size) {
      reshade::set_config_value(nullptr, internal::VALUES_SECTION.data(), internal::SHADOW_MAP_SIZE_KEY.data(), size);
      internal::shadow_map_size_mirrored = size;
      renodx::utils::log::i(std::format("[Total War] 64-bit Attila: engine value {}: {:#x} at the next start (this start: {:#x})",
                                        internal::SHADOW_MAP_SIZE_KEY.data(), size, size_at_start));
    }
    shadow_pending = (size != size_at_start);
  }
  internal::shadow_map_size_pending.store(shadow_pending, std::memory_order_relaxed);
  bool code_pending = false;
  if ((RowsOffered() & plugin::RowBit(plugin::ROW_GAME_CODE)) != 0) {
    if (!std::exchange(internal::game_code_read, true)) {
      // As the plugin reads it (GetPrivateProfileIntW): absent or empty, the default (on); else a leading decimal or
      // 0x hex number, none (text): 0. Other hand edits (`+1`, a number past int, leading spaces) may read otherwise
      // here; they are not plain decimal numbers, so they stay unmirrored and the row's 0 / 1 is written back.
      bool on_in_file = (plugin::FASTER_GAME_CODE_DEFAULT != 0);
      internal::game_code_mirrored = plugin::FASTER_GAME_CODE_DEFAULT;
      std::array<char, 16> text = {};
      size_t size = text.size();
      if (reshade::get_config_value(nullptr, internal::VALUES_SECTION.data(), internal::FASTER_GAME_CODE_KEY.data(),
                                    text.data(), &size)) {
        const std::string_view value(text.data());
        const bool hex = value.starts_with("0x") || value.starts_with("0X");
        const std::string_view digits = value.substr(hex ? 2 : 0);
        int number = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number, (hex ? 16 : 10));
        if (!value.empty()) {
          on_in_file = (error == std::errc() && number != 0);
        }
        const bool plain_decimal = (!hex && error == std::errc() && end == digits.data() + digits.size());
        internal::game_code_mirrored = (plain_decimal ? std::optional<int>(number) : std::nullopt);
      }
      if (!internal::game_code_at_start.has_value()) {
        internal::game_code_at_start = on_in_file;
      }
    }
    const bool code_at_start = *internal::game_code_at_start;
    if (const int key = (faster_game_code ? 1 : 0); internal::game_code_mirrored != key) {
      reshade::set_config_value(nullptr, internal::VALUES_SECTION.data(), internal::FASTER_GAME_CODE_KEY.data(), key);
      internal::game_code_mirrored = key;
      renodx::utils::log::i(std::format("[Total War] 64-bit Attila: engine value {}: {:#x} at the next start (this start: {:#x})",
                                        internal::FASTER_GAME_CODE_KEY.data(), key, (code_at_start ? 1 : 0)));
    }
    code_pending = (faster_game_code != code_at_start);
  }
  internal::game_code_pending.store(code_pending, std::memory_order_relaxed);
}

// Every present with Result() READY on a v2 block, beside MirrorRestartValues: Two Shadow Cascades Under Rays for the
// next start into ReShade.ini [Attila64Plugin.Values] ShadowCascades, which the plugin reads at its init. 1 only while
// the plugin offers the shadow map row and `two_cascades` (the row on and the rays' shadows on) holds; 0 at once when
// that ends, so a start without the rays' shadows gets the game's cascades back. Read once (as Faster Game CPU Code's
// key above: text that is no plain decimal number stays unmirrored and is written back as 0 / 1), then written only
// when it differs, with one log line per write.
// ours: 0 also while the plugin does not offer the row (ShadowMapSize leaves its key alone then), because the key
// depends on the rays, not only on the row: a stale 1 would give the next start that offers the row 2 cascades
// without them.
inline void MirrorShadowCascades(bool two_cascades) {
  namespace plugin = internal::plugin;
  const std::unique_lock lock(internal::mutex);
  if (internal::found.load(std::memory_order_relaxed) != Found::READY || !BlockV2()) return;
  if (!internal::shadow_cascades_mirrored.has_value()) {
    bool written_at_start = false;  // absent or empty: the game's cascades
    internal::shadow_cascades_mirrored = 0;
    std::array<char, 16> text = {};
    size_t size = text.size();
    if (reshade::get_config_value(nullptr, internal::VALUES_SECTION.data(), internal::SHADOW_CASCADES_KEY.data(),
                                  text.data(), &size)) {
      const std::string_view value(text.data());
      const bool hex = value.starts_with("0x") || value.starts_with("0X");
      const std::string_view digits = value.substr(hex ? 2 : 0);
      int number = 0;
      const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number, (hex ? 16 : 10));
      written_at_start = (error == std::errc() && number == 1);
      const bool plain_decimal = (!hex && error == std::errc() && end == digits.data() + digits.size());
      if (!value.empty()) {
        internal::shadow_cascades_mirrored = (plain_decimal ? std::optional<int>(number) : std::nullopt);
      }
    }
    if (!internal::shadow_cascades_at_start.has_value()) {
      internal::shadow_cascades_at_start = written_at_start;
    }
  }
  const int key = ((two_cascades && (RowsOffered() & plugin::RowBit(plugin::ROW_SHADOW_MAP)) != 0) ? 1 : 0);
  if (internal::shadow_cascades_mirrored != key) {
    reshade::set_config_value(nullptr, internal::VALUES_SECTION.data(), internal::SHADOW_CASCADES_KEY.data(), key);
    internal::shadow_cascades_mirrored = key;
    renodx::utils::log::i(std::format("[Total War] 64-bit Attila: engine value {}: {:#x} at the next start (this start: {:#x})",
                                      internal::SHADOW_CASCADES_KEY.data(), key,
                                      (*internal::shadow_cascades_at_start ? 1 : 0)));
  }
}

// Every present with Result() READY on a v2 block (the loader's drawing thread): the scale rows' multipliers for the
// plugin's hooks (attila64_plugin::Scale), the changed ones stored, then scales_active: a scale's bit while it is not
// neutral and its row is offered (a clear bit: the hook leaves the game's call as it is). One log line when the bits
// change, one when the plugin raises a new notice (sticky bits), and one per add-on load for each engine lane
// that reads ENGINE_OTHER_MOD. The scales stay at the add-on's detach.
inline void PublishScales(const std::array<float, internal::plugin::SCALE_COUNT>& scales) {
  using internal::block;
  namespace plugin = internal::plugin;
  const std::unique_lock lock(internal::mutex);
  if (internal::found.load(std::memory_order_relaxed) != Found::READY || !BlockV2()) return;
  uint32_t rows_scales = 0;
  for (const auto& [row, bits] : internal::ROW_SCALES) {
    if ((RowsOffered() & plugin::RowBit(row)) != 0) {
      rows_scales |= bits;
    }
  }
  uint32_t active = 0;
  for (uint32_t index = 0; index < plugin::SCALE_COUNT; ++index) {
    // Small Soldiers as Sprites: a distance scale that is not finite and above 0 cannot move a soldier (the plugin's
    // handler and the 32-bit hook act only above 0; +inf keeps the game's distance), so it is sent as its neutral 0
    // with its bit clear: no handler call per soldier.
    const bool sprite_off = (index == plugin::SCALE_SPRITE_DISTANCE
                             && !(scales[index] > 0.f && scales[index] <= std::numeric_limits<float>::max()));
    const float scale = (sprite_off ? 0.f : scales[index]);
    if (block->scales[index].load(std::memory_order_relaxed) != scale) {
      block->scales[index].store(scale, std::memory_order_relaxed);
    }
    // Exact: the sliders' defaults give the neutral bits (1x, 1 / 1^2); a NaN counts as active.
    if (scale != plugin::NEUTRAL_SCALES[index]) {
      active |= 1u << index;
    }
  }
  active &= rows_scales;
  if (block->scales_active.load(std::memory_order_relaxed) != active) {
    block->scales_active.store(active, std::memory_order_relaxed);
    renodx::utils::log::i(std::format("[Total War] 64-bit Attila: engine scales active {:#x}", active));
  }
  if (const uint32_t notices = block->notices.load(std::memory_order_relaxed);
      (notices & ~internal::notices_logged) != 0) {
    internal::notices_logged = notices;
    renodx::utils::log::i(std::format(
        "[Total War] 64-bit Attila: engine plugin notices {:#x} (rest pack: {}, model table full: {})", notices,
        ((notices & plugin::NOTICE_REST_PACK) != 0 ? "yes" : "no"),
        ((notices & plugin::NOTICE_MODEL_TABLE_FULL) != 0 ? "yes" : "no")));
  }
  // A lane carries no address: the loader's log has the changed places.
  for (const auto& [shift, line] : {
           std::pair{plugin::ENGINE_STEP_SHIFT,
                     "[Total War] 64-bit Attila: Faster Battle Step cannot run: another mod changed the game code it "
                     "needs (an Engine Performance Fix update?); attila64_log.txt in the game folder says where."},
           std::pair{plugin::ENGINE_SPLIT_SHIFT,
                     "[Total War] 64-bit Attila: Multithreaded Collisions cannot run: another mod changed the game "
                     "code it needs (an Engine Performance Fix update?); attila64_log.txt in the game folder says "
                     "where."},
       }) {
    if ((internal::other_mod_logged & (1u << shift)) == 0 && EngineLaneState(shift) == plugin::ENGINE_OTHER_MOD) {
      internal::other_mod_logged |= 1u << shift;
      renodx::utils::log::i(line);
    }
  }
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
  internal::battle_core_written.reset();
  internal::render_core_written.reset();
  internal::effects_written.reset();
  internal::battle_step_written.reset();
  internal::contact_split_written.reset();
  internal::callback_failed = false;
  internal::callback_failure_logged = false;
  internal::unit_flush_logged.reset();
  // The next READY finds the values again (a kept image's Tweaks too) and reads the keys again; this start's frame cap,
  // shadow map and game code keys stay.
  internal::tweaks_found = false;
  internal::tweaks_refused = false;
  internal::values_pending = false;
  internal::sol_frame_cap_mirrored.reset();
  internal::sol_frame_cap_pending = false;
  internal::shadow_map_size_mirrored.reset();
  internal::shadow_map_size_pending = false;
  internal::shadow_cascades_mirrored.reset();
  internal::game_code_read = false;
  internal::game_code_mirrored.reset();
  internal::game_code_pending = false;
  internal::notices_logged = 0;
}

}  // namespace twattila::attila64_plugin_link
