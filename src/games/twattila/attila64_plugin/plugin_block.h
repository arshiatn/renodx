/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// The block the 64-bit Attila plugin (renodx_attila64.dll, a loader plugin that hooks game code) shares with the
// Attila add-on in the same process: what the plugin applied, which hooks the add-on wants, and the add-on's callbacks.
// The add-on finds it with GetModuleHandleW(MODULE_NAME) + GetProcAddress(BLOCK_EXPORT). Append-only like dlaa_bridge.h:
// new fields at the end with VERSION + 1, never reordered or reused; Callbacks the same. The v1 plugin reads neither
// Callbacks::version nor bytes (one layout); a later plugin will call a member added after v1 only when the add-on's
// `bytes` covers it. Layout version 1 is frozen (2026-11-25).
//
// The add-on's side of the callback protocol (the plugin's side is hooks.hpp's CallbackScope):
// - Register: take a load reference on the add-on's own module, compare_exchange `callbacks` nullptr -> its Callbacks
//   (a lost exchange: another add-on has it; release the reference), then set the `hooks_wanted` bits.
// - Unregister: clear the `hooks_wanted` bits, `callbacks.store(nullptr)` (seq_cst), then seq_cst loads of
//   `calls_in_flight` until it is 0. The first 0 ends the wait: a hook that enters later still counts, but it loads
//   nullptr and calls nothing. Both operations must be seq_cst: a release store may pass the following load (on x86 a
//   plain mov), and a hook could then load the old pointer after the wait. Bound the wait (1 s, Sleep(0) / Sleep(1));
//   drained -> release the module reference; not drained -> keep it for the process. At process-exit detach
//   (lpReserved != nullptr) skip the wait and keep the reference.
// - Never unregister from inside a callback, or while holding a lock a callback takes: the wait would count the caller
//   itself and end only at its bound, not drained.
// - Every Callbacks member is non-null: the plugin calls them without a check.
// - Callbacks run on the game's threads, possibly several at once, with the Windows calling convention; they may nest
//   (the pack hook can run inside a measured pass's original). None runs while a game original runs inside its call.
// - pass_end runs only when `callbacks` holds the same pointer as at its pass_begin. A re-registration at the same
//   address (the same static Callbacks) looks the same, so pass_begin writes the add-on's registration epoch into
//   PassMeasure::reserved and the add-on ignores a pass_end whose epoch is not the current one.
namespace twattila::attila64_plugin {

inline constexpr uint32_t VERSION = 1;
inline constexpr uint32_t OLDEST_VERSION = 1;  // the oldest block the add-on reads

inline constexpr wchar_t MODULE_NAME[] = L"renodx_attila64.dll";
inline constexpr char BLOCK_EXPORT[] = "renodx_attila64_block";

enum Status : uint32_t {
  STATUS_LOADING,
  STATUS_READY,
  STATUS_OFF,          // the [Attila64Plugin] Enabled=0 kill switch
  STATUS_WRONG_GAME,   // another game build
  STATUS_WRONG_LOADER  // loader interface, game range or the plugin's file name not as written for
};

// One per feature; bit (1 << row) in rows_applied, rows_refused, rows_changed and hooks_wanted. Append only.
// Reserved, no value yet: ROW_CPU_PLACEMENT (ini key CpuPlacement), for "Keep Battle Core Free".
enum Row : uint32_t {
  ROW_BONE_FLUSH,  // before the game packs its animation textures: the add-on flushes its unit records
  ROW_PASS_DRAW,   // each scene node's draw into a view: the add-on may skip the game's shadow-map work
  ROW_COUNT
};

constexpr uint32_t RowBit(Row row) { return 1u << row; }

enum PassAction : uint32_t { PASS_DRAW, PASS_SKIP, PASS_DRAW_MEASURED };

// The add-on's state for one measured call, kept on the plugin hook's stack between pass_begin and pass_end (zeroed
// before pass_begin). Fixed size forever: another layout means new callback members in a new Callbacks version.
struct PassMeasure {
  uint32_t sun_binds_before = 0;
  uint32_t other_binds_before = 0;
  uint32_t sun_at_start = 0;
  uint32_t reserved = 0;  // the add-on's registration epoch (see the protocol above)
};

// The add-on's, immutable while published. Called from the game's threads; none may throw.
struct Callbacks {
  uint32_t version = VERSION;  // VERSION the add-on was built with
  uint32_t bytes = sizeof(Callbacks);
  void (*before_pack)() noexcept = nullptr;
  PassAction (*pass_begin)(uint32_t pass_kind, PassMeasure* measure) noexcept = nullptr;
  void (*pass_end)(uint32_t pass_kind, const PassMeasure& measure) noexcept = nullptr;
};

struct Block {
  uint32_t version = VERSION;
  uint32_t bytes = sizeof(Block);
  std::atomic<uint32_t> status = STATUS_LOADING;  // stored last at init (release): the other fields final once not LOADING
  uint32_t rows_applied = 0;
  uint32_t rows_refused = 0;
  std::atomic<uint32_t> rows_changed = 0;     // set at game start (after READY) when a site's bytes changed after us
  std::atomic<uint32_t> hooks_wanted = 0;     // the add-on's; each hook reads it relaxed before any callback work
  // Plugin hooks inside the guard: raised before `callbacks` is loaded, lowered after the last call into it.
  std::atomic<uint32_t> calls_in_flight = 0;
  std::atomic<const Callbacks*> callbacks = nullptr;  // the add-on's
};

// The bytes a block of a given version has (an older plugin's ends earlier).
constexpr size_t BlockBytes(uint32_t version) { return version >= 1 ? sizeof(Block) : 0; }

using BlockExportFn = Block* (*)();

// [Attila64Plugin] in <exe dir>\ReShade.ini, read once at the plugin's init (diagnosis and kill switch, by hand).
inline constexpr wchar_t INI_SECTION[] = L"Attila64Plugin";
inline constexpr wchar_t INI_ENABLED[] = L"Enabled";
inline constexpr int ENABLED_DEFAULT = 1;
inline constexpr const wchar_t* ROW_INI_KEYS[ROW_COUNT] = {L"BoneFlush", L"PassDraw"};  // install the row's hook at all
inline constexpr int ROW_INSTALL_DEFAULT = 1;

static_assert(std::is_standard_layout_v<PassMeasure> && sizeof(PassMeasure) == 16);
static_assert(std::is_standard_layout_v<Callbacks> && std::is_standard_layout_v<Block>);
static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<const Callbacks*>::is_always_lock_free);
static_assert(sizeof(std::atomic<uint32_t>) == 4);
static_assert(offsetof(Block, version) == 0);
static_assert(offsetof(Block, bytes) == 4);
static_assert(offsetof(Block, status) == 8);
static_assert(offsetof(Block, rows_applied) == 12);
static_assert(offsetof(Block, rows_refused) == 16);
static_assert(offsetof(Block, rows_changed) == 20);
static_assert(offsetof(Block, hooks_wanted) == 24);
static_assert(offsetof(Block, calls_in_flight) == 28);
static_assert(offsetof(Block, callbacks) == 32);
static_assert(offsetof(Callbacks, version) == 0);
static_assert(offsetof(Callbacks, bytes) == 4);
// The block is shared only inside the 64-bit game; 32-bit builds include this header for PassMeasure / PassAction.
static_assert(sizeof(void*) != 8 || (sizeof(Block) == 40 && BlockBytes(1) == 40), "v1: 40 bytes in the 64-bit build");
static_assert(sizeof(void*) != 8 || (offsetof(Callbacks, before_pack) == 8 && offsetof(Callbacks, pass_begin) == 16
                                     && offsetof(Callbacks, pass_end) == 24 && sizeof(Callbacks) == 32));

}  // namespace twattila::attila64_plugin
