/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "loader_api.h"
#include "plugin_block.h"
#include "sites.h"

// The plugin's hooks on game code: the shared block, the callback guard and the two hook bodies. The game calls them
// with its own (Linux) calling convention; they call the add-on's callbacks with the Windows one. The unit test runs
// this file as shipped.
namespace twattila::attila64_plugin {

static Block block;
// What to call for each site's original function (the loader's trampoline), by SiteLabel. Set at init right after the
// loader's hook made the site live: safe because no game code runs before the plugins' inits have returned.
static std::array<void*, LABEL_COUNT> originals = {};
// For the first-call lines; the loader's interface stays valid for the process.
static const LoaderApi* loader = nullptr;
static std::array<std::atomic_bool, LABEL_COUNT> first_call_logged = {};

// The game functions' prototypes, typed noexcept: a C++ exception in game code is unwound by the game's own unwinder,
// which cannot pass a frame of ours. The pack hook leaves no frame of ours while its original runs (a tail call); the
// pass hook does on measured calls only, and no frame above it catches (the generator's catch gate on its site). So the
// hook bodies need no landing pads, and every function they call is noexcept.
using PackFn = bool(ATTILA64_GAME_CALL*)(void* self, void* context, bool flag, void* list, uint32_t first,
                                         uint32_t* cursor, uint32_t count, uint32_t* written) noexcept;
using ViewDrawFn = void(ATTILA64_GAME_CALL*)(void* node, void* context, void* view) noexcept;
using ViewKindFn = uint32_t(ATTILA64_GAME_CALL*)(void* view) noexcept;

inline constexpr size_t NODE_ENABLED_OFFSET = 0x28;             // a byte; the game returns at once when it is 0
inline constexpr size_t VIEW_KIND_SLOT = 0x80 / sizeof(void*);  // the view's pass kind, a virtual

// The add-on's unregister stores nullptr into `callbacks`, then waits (bounded) until `calls_in_flight` is 0
// (plugin_block.h has its rules): with this increment-then-load (seq_cst on both sides) no callback starts after that
// wait ends. Two atomics instead of the style guide's std::shared_mutex, one of the two cases where that rule does not
// fit (the other: the scales, plugin_block.h), because of the bounded wait: an exclusive SRW acquire would hang at exit
// behind a thread that died inside a callback, or behind a lock a callback takes. Close it before calling the game's
// original.
struct CallbackScope {
  CallbackScope() noexcept {
    block.calls_in_flight.fetch_add(1);
    callbacks = block.callbacks.load();
  }
  ~CallbackScope() { block.calls_in_flight.fetch_sub(1); }
  CallbackScope(const CallbackScope&) = delete;
  CallbackScope& operator=(const CallbackScope&) = delete;

  const Callbacks* callbacks = nullptr;
};

// The calling site's own line (a row with several sites shows which of them the game called).
static void LogFirstCall(SiteLabel label) noexcept {
  if (first_call_logged[label].load(std::memory_order_relaxed) || first_call_logged[label].exchange(true)) return;
  loader->log("renodx_attila64: %s: first call (thread %lu)", LABEL_WORDS[label], GetCurrentThreadId());
}

// The hooks call only noexcept functions, so nothing can leave them by an exception; they are not marked noexcept
// themselves because clang cannot compile a guaranteed tail call out of a noexcept body (its terminate scope).

// ROW_BONE_FLUSH: the add-on's before_pack, then the game's pack with its own frame (a tail call).
static ATTILA64_GAME_CALL bool PackEntry(void* self, void* context, bool flag, void* list, uint32_t first,
                                         uint32_t* cursor, uint32_t count, uint32_t* written) {
  LogFirstCall(LABEL_PACK_ENTRY);
  if ((block.hooks_wanted.load(std::memory_order_relaxed) & RowBit(ROW_BONE_FLUSH)) != 0) {
    CallbackScope scope;
    if (scope.callbacks != nullptr) {
      scope.callbacks->before_pack();
    }
  }
  [[clang::musttail]] return reinterpret_cast<PackFn>(originals[LABEL_PACK_ENTRY])(self, context, flag, list, first,
                                                                                   cursor, count, written);
}

// ROW_PASS_DRAW, in the game's own order: a node the game would skip never reaches the add-on; then pass_begin decides.
// Only a measured call keeps this frame around the original, and its pass_end runs only with the same Callbacks pointer
// as its pass_begin: an add-on that unregistered, or registered another Callbacks, in between gets none (one that
// registered the same address again tells by its epoch in PassMeasure::reserved).
static ATTILA64_GAME_CALL void ViewDrawEntry(void* node, void* context, void* view) {
  const auto original = reinterpret_cast<ViewDrawFn>(originals[LABEL_VIEW_DRAW_ENTRY]);
  LogFirstCall(LABEL_VIEW_DRAW_ENTRY);
  if ((block.hooks_wanted.load(std::memory_order_relaxed) & RowBit(ROW_PASS_DRAW)) == 0
      || *(static_cast<const uint8_t*>(node) + NODE_ENABLED_OFFSET) == 0) {
    [[clang::musttail]] return original(node, context, view);
  }
  PassMeasure measure = {};
  uint32_t pass_kind = 0;
  PassAction action = PASS_DRAW;
  const Callbacks* begin_callbacks = nullptr;
  {
    CallbackScope scope;
    if (scope.callbacks != nullptr) {
      pass_kind = reinterpret_cast<ViewKindFn>((*static_cast<void* const* const*>(view))[VIEW_KIND_SLOT])(view);
      action = scope.callbacks->pass_begin(pass_kind, &measure);
      begin_callbacks = scope.callbacks;
    }
  }
  switch (action) {
    case PASS_SKIP:
      return;
    case PASS_DRAW_MEASURED:
      break;
    default:
      [[clang::musttail]] return original(node, context, view);
  }
  original(node, context, view);
  CallbackScope scope;
  if (scope.callbacks == begin_callbacks) {
    scope.callbacks->pass_end(pass_kind, measure);
  }
}

// Each site's hook, by SiteLabel (patcher_test checks that every site has one).
static const std::array<void*, LABEL_COUNT> REPLACEMENTS = {reinterpret_cast<void*>(&PackEntry),
                                                            reinterpret_cast<void*>(&ViewDrawEntry)};

}  // namespace twattila::attila64_plugin
