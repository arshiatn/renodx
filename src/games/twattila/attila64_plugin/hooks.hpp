/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cstddef>
#include <cstdint>

#include "battle_core.hpp"
#include "loader_api.h"
#include "plugin_block.h"
#include "sites.h"

// The plugin's hooks on game code: the shared block, the callback guard and the hook bodies. The game calls them with
// its own (Linux) calling convention; they call the add-on's callbacks and the plugin's own code with the Windows one.
// The unit test runs this file as shipped.
namespace twattila::attila64_plugin {

static Block block;
// What to call for each site's original function (the loader's trampoline), by SiteLabel. Set at init right after the
// loader's hook made the site live: safe because no game code runs before the plugins' inits have returned.
static std::array<void*, LABEL_COUNT> originals = {};
// For the first-call lines; the loader's interface stays valid for the process.
static const LoaderApi* loader = nullptr;
static std::array<std::atomic_bool, LABEL_COUNT> first_call_logged = {};

// The game functions' prototypes, typed noexcept: a C++ exception in game code is unwound by the game's own unwinder,
// which cannot pass a frame of ours. The pack, step and model LOD hooks leave no frame of ours while their originals
// run (a tail call); the pass hook does on measured calls only, and no frame above it catches (the generator's catch
// gate on its site); the unit LOD hook does while a scale of its own is active, and nothing its original calls throws
// (read from the game's code: one direct call and two virtual ones, each with one possible target). So the hook
// bodies need no landing pads, and every function they call is noexcept.
using PackFn = bool(ATTILA64_GAME_CALL*)(void* self, void* context, bool flag, void* list, uint32_t first,
                                         uint32_t* cursor, uint32_t count, uint32_t* written) noexcept;
using ViewDrawFn = void(ATTILA64_GAME_CALL*)(void* node, void* context, void* view) noexcept;
using ViewKindFn = uint32_t(ATTILA64_GAME_CALL*)(void* view) noexcept;
using StepTaskFn = void*(ATTILA64_GAME_CALL*)(void* task) noexcept;
// The unit LOD table is returned through `distances` (18 floats, also the return value). The four settings are whole
// registers here so that each reaches the game as it came (it reads 32 and 8 bits of them).
using UnitLodFn = float*(ATTILA64_GAME_CALL*)(float* distances, void* display, uintptr_t quality, uintptr_t second,
                                              uintptr_t third, uintptr_t fourth) noexcept;
using ModelLodFn = void*(ATTILA64_GAME_CALL*)(void* entry, void* next_lod, float distance_squared) noexcept;

inline constexpr size_t NODE_ENABLED_OFFSET = 0x28;             // a byte; the game returns at once when it is 0
inline constexpr size_t VIEW_KIND_SLOT = 0x80 / sizeof(void*);  // the view's pass kind, a virtual
// Where the game's pack returns to from its second call per pass, which runs only with items left over after the
// frame's first pack (a big battle's rest).
inline constexpr uintptr_t REST_PACK_RETURN = 0x33f6d11;
inline constexpr uint32_t UNIT_LOD_SCALES = (1u << SCALE_MODEL_DETAIL) - 1u;  // scales_active bits 0-16
static_assert(SCALE_UNIT_LOD == 0, "the unit LOD scales start at bit 0 and at the table's entry 0");

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

// ROW_BONE_FLUSH: the add-on's before_pack, then the game's pack with its own frame (a tail call). A rest pack leaves
// its sticky notice first (the 32-bit Big Battle Fix tells the two packs apart the same way, by the return address:
// graphics_hooks.hpp OnPackAnimations).
static ATTILA64_GAME_CALL bool PackEntry(void* self, void* context, bool flag, void* list, uint32_t first,
                                         uint32_t* cursor, uint32_t count, uint32_t* written) {
  LogFirstCall(LABEL_PACK_ENTRY);
  if (reinterpret_cast<uintptr_t>(__builtin_return_address(0)) == REST_PACK_RETURN
      && (block.notices.load(std::memory_order_relaxed) & NOTICE_REST_PACK) == 0) {
    block.notices.fetch_or(NOTICE_REST_PACK, std::memory_order_relaxed);
  }
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

// ROW_CPU_PLACEMENT: the battle step's task, run beside the drawing on one of the game's task threads. The placement
// (battle_core.hpp) reads core_wanted itself and ignores hooks_wanted; then the game's step with its own frame (a tail
// call), so no local with a destructor here.
static ATTILA64_GAME_CALL void* StepTaskEntry(void* task) {
  LogFirstCall(LABEL_STEP_ENTRY);
  battle_core::OnStepStart(&block, *loader);
  [[clang::musttail]] return reinterpret_cast<StepTaskFn>(originals[LABEL_STEP_ENTRY])(task);
}

// The scale rows. Each hook first tests its own bits of scales_active: with none set the game's call goes on as it
// came (a tail call with the incoming registers, no frame of ours), so 1x is the game's result by construction. With
// some set, only the values whose own bit is set change.

// ROW_UNIT_LOD: the game's 18 unit LOD distances, then entries 0-16 x their scales (17, the cap, stays). The 32-bit
// add-on's OnUnitDistances (graphics_hooks.hpp) is the same `min(distance x scale, FLT_MAX)` per entry; there every
// entry is multiplied once any row is active, here only the entries whose scale is not neutral.
static ATTILA64_GAME_CALL float* UnitLodEntry(float* distances, void* display, uintptr_t quality, uintptr_t second,
                                              uintptr_t third, uintptr_t fourth) {
  const auto original = reinterpret_cast<UnitLodFn>(originals[LABEL_UNIT_LOD_ENTRY]);
  LogFirstCall(LABEL_UNIT_LOD_ENTRY);
  const uint32_t active = block.scales_active.load(std::memory_order_relaxed) & UNIT_LOD_SCALES;
  if (active == 0) {
    [[clang::musttail]] return original(distances, display, quality, second, third, fourth);
  }
  float* filled = original(distances, display, quality, second, third, fourth);
  for (uint32_t index = SCALE_UNIT_LOD; index < SCALE_MODEL_DETAIL; ++index) {
    if ((active & (1u << index)) != 0) {
      filled[index] = std::min(filled[index] * block.scales[index].load(std::memory_order_relaxed), FLT_MAX);
    }
  }
  return filled;
}

// ROW_MODEL_DETAIL: the LOD pick of trees and other objects (not battle units) compares the camera's distance squared
// with each LOD's; the add-on's scale is 1 / slider squared, so they keep their detail farther away (the 32-bit
// add-on's stub at the same function: engine_code.hpp MODEL_DETAIL_STUB). Then the game's pick with its own frame.
static ATTILA64_GAME_CALL void* ModelLodEntry(void* entry, void* next_lod, float distance_squared) {
  LogFirstCall(LABEL_MODEL_LOD_ENTRY);
  if ((block.scales_active.load(std::memory_order_relaxed) & (1u << SCALE_MODEL_DETAIL)) != 0) {
    distance_squared *= block.scales[SCALE_MODEL_DETAIL].load(std::memory_order_relaxed);
  }
  [[clang::musttail]] return reinterpret_cast<ModelLodFn>(originals[LABEL_MODEL_LOD_ENTRY])(entry, next_lod,
                                                                                           distance_squared);
}

// Each hook site's hook, by SiteLabel; a patch site has none (patcher_test checks that each label holds its own).
static const std::array<void*, LABEL_COUNT> REPLACEMENTS = [] {
  std::array<void*, LABEL_COUNT> hooks = {};
  hooks[LABEL_PACK_ENTRY] = reinterpret_cast<void*>(&PackEntry);
  hooks[LABEL_VIEW_DRAW_ENTRY] = reinterpret_cast<void*>(&ViewDrawEntry);
  hooks[LABEL_STEP_ENTRY] = reinterpret_cast<void*>(&StepTaskEntry);
  hooks[LABEL_UNIT_LOD_ENTRY] = reinterpret_cast<void*>(&UnitLodEntry);
  hooks[LABEL_MODEL_LOD_ENTRY] = reinterpret_cast<void*>(&ModelLodEntry);
  return hooks;
}();

}  // namespace twattila::attila64_plugin
