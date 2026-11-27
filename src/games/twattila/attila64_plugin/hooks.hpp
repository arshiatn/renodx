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
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

#include "battle_core.hpp"
#include "loader_api.h"
#include "model_lods.hpp"
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
// which cannot pass a frame of ours. The pack, step, model LOD, grass and campaign tree hooks leave no frame of ours
// while their originals run (a tail call); the pass hook does on measured calls only, and no frame above it catches
// (the generator's catch gate on its site); the unit LOD hook does while a scale of its own is active, and nothing its
// original calls throws (read from the game's code: one direct call and two virtual ones, each with one possible
// target). So the hook bodies need no landing pads, and every function they call is noexcept.
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
using GrassUpdateFn = void(ATTILA64_GAME_CALL*)(uint8_t* scrub) noexcept;
// `flag` is a whole register so that it reaches the game as it came.
using GrassDrawFn = void(ATTILA64_GAME_CALL*)(const uint8_t* node, const uint32_t* model, const float* transform,
                                              void* clip, const uint8_t* params, uintptr_t flag) noexcept;
// The cell's clip state: 48 bytes the game passes by value, on the stack.
struct CellClip {
  std::array<uint64_t, 6> state;
};
using GrassCellFn = void(ATTILA64_GAME_CALL*)(const uint8_t* scrub, const uint8_t* cell, const float* camera,
                                              CellClip clip,
                                              float radius_squared, uintptr_t flag) noexcept;
using CampaignTreeFn = void*(ATTILA64_GAME_CALL*)(const uint8_t* node, const uint32_t* model, void* position,
                                                  uintptr_t fourth, uintptr_t fifth, uintptr_t sixth, float first_float,
                                                  float second_float) noexcept;
using EffectWalkFn = void(ATTILA64_GAME_CALL*)(uint8_t* node, float first_float, float second_float) noexcept;

inline constexpr size_t NODE_ENABLED_OFFSET = 0x28;             // a byte; the game returns at once when it is 0
inline constexpr size_t VIEW_KIND_SLOT = 0x80 / sizeof(void*);  // the view's pass kind, a virtual
// Where the game's pack returns to from its second call per pass, which runs only with items left over after the
// frame's first pack (a big battle's rest).
inline constexpr uintptr_t REST_PACK_RETURN = 0x33f6d11;
inline constexpr uint32_t UNIT_LOD_SCALES = (1u << SCALE_MODEL_DETAIL) - 1u;  // scales_active bits 0-16
static_assert(SCALE_UNIT_LOD == 0, "the unit LOD scales start at bit 0 and at the table's entry 0");

// The scrub: the scene node that draws a battle's grass and plants. Each frame it takes the cells of its quadtree
// within `radius` metres of the camera; a cell's records (18 bytes each: a position, a slot of the scrub's models, a
// random byte) within that radius go to the model node's draw, each with a bias (its random byte / 255 x `bias`,
// squared, added to its distance squared) that thins the grass out with distance. The draw takes the first enabled LOD
// of the model whose distance squared is above that key; a record past every enabled LOD draws nothing.
inline constexpr size_t SCRUB_RADIUS = 0xd0;      // an int, metres
inline constexpr size_t SCRUB_BIAS = 0xd4;        // a float
inline constexpr size_t SCRUB_NODE = 0xe8;        // the model node its records are drawn through
inline constexpr size_t SCRUB_SLOT_COUNT = 0xf4;  // a uint32_t
inline constexpr size_t SCRUB_SLOTS = 0xf8;       // per slot the model's index in the node, a uint32_t
inline constexpr uint32_t MAX_SCRUB_SLOTS = 256;  // a record's slot is a byte
inline constexpr size_t CELL_RECORD_COUNT = 0x24;
inline constexpr size_t CELL_RECORDS = 0x28;
inline constexpr size_t RECORD_BYTES = 18;
inline constexpr size_t RECORD_SLOT = 0x10;
// The model node: its scene, and a signed square the draw adds to every key.
inline constexpr size_t NODE_SCENE = 0x50;
inline constexpr size_t NODE_LOD_OFFSET = 0xe98;
inline constexpr size_t TRANSFORM_POSITION_X = 0xc;  // y and z 0x10 and 0x20 after it (a 4 x 3 matrix)
inline constexpr size_t PARAMS_BIAS = 0x18;
// The draw's camera: the scene's camera getter (a virtual) returns the camera inside the scene, and that camera's
// position getter (a virtual) copies three floats of it. The no-LOD return below reads them itself, and only while both
// functions hold exactly this code (read every frame) and the scene's and the camera's tables point at them.
inline constexpr size_t SCENE_CAMERA_SLOT = 0x58 / sizeof(void*);
inline constexpr size_t CAMERA_POSITION_SLOT = 0x80 / sizeof(void*);
inline constexpr size_t SCENE_CAMERA = 0x80;
inline constexpr size_t CAMERA_POSITION = 0x10;
inline constexpr uintptr_t GET_SCENE_CAMERA = 0x30b97b0;
inline constexpr uintptr_t GET_CAMERA_POSITION = 0x74ff50;
// sub rdi, -0x80; mov rax, rdi; ret
inline constexpr std::array<uint8_t, 8> GET_SCENE_CAMERA_CODE = {0x48, 0x83, 0xef, 0x80, 0x48, 0x89, 0xf8, 0xc3};
// The camera's +0x10, +0x14 and +0x18 through xmm0 to [rdi], [rdi + 4] and [rdi + 8]; mov rax, rdi; ret
inline constexpr std::array<uint8_t, 33> GET_CAMERA_POSITION_CODE = {
    0xf3, 0x0f, 0x10, 0x46, 0x10, 0xf3, 0x0f, 0x11, 0x07, 0xf3, 0x0f, 0x10, 0x46, 0x14, 0xf3, 0x0f, 0x11,
    0x47, 0x04, 0xf3, 0x0f, 0x10, 0x46, 0x18, 0xf3, 0x0f, 0x11, 0x47, 0x08, 0x48, 0x89, 0xf8, 0xc3};
inline constexpr uint32_t GRASS_SCALES =
    (1u << SCALE_GRASS_LOD) | (1u << SCALE_GRASS_RADIUS) | (1u << SCALE_GRASS_DENSITY);
// The scales that move where grass can be drawn at all: only these cut a cell's radius, or the scrub's.
inline constexpr uint32_t GRASS_REACH_SCALES = (1u << SCALE_GRASS_LOD) | (1u << SCALE_GRASS_RADIUS);

// A scrub's radius and bias as the game set them and as the update hook wrote them, and per slot the radius its
// records need. Written by the scrub's update hook before the game's update starts the cell loop on its task threads,
// read by the cell hook there.
struct ScrubState {
  uint64_t updated = 0;  // ScrubStates::updates at its last update; 0: never used
  int32_t game_radius = 0;
  uint32_t game_bias = 0;  // a float's bits
  int32_t written_radius = 0;
  uint32_t written_bias = 0;
  // Per slot: ceil(sqrt(the model's farthest enabled LOD - min(the node's offset, 0))) + 1 m; 0: no enabled LOD;
  // INT32_MAX: no model, or not below written_radius. slot_count 0: none known.
  uint32_t slot_count = 0;
  std::array<int32_t, MAX_SCRUB_SLOTS> slot_radius = {};
};
// Each scrub's own state, by its address: with one state for all, a second scrub updating in turn with the first would
// have our written radius and bias taken for the game's. An entry is claimed at a scrub's first update. Entries are
// not freed (nothing tells when the game frees a scrub); a new scrub with none unused takes the one longest without an
// update among those ScrubStateFree allows. With none the scrub keeps the game's radius and bias, and one log line
// says so.
// Not told apart (it needs both floats' exact bits): a new scrub at a freed one's address whose own radius or bias
// is the value written last there, or the game setting such a value again; that value is taken for ours. No field of
// a scrub is known to stay for its life and differ between scrubs, and a wrong guess there would take our values for
// the game's.
inline constexpr size_t MAX_SCRUBS = 64;
inline constexpr uint64_t STALE_SCRUB_UPDATES = 1u << 20;
struct ScrubStates {
  std::shared_mutex mutex;
  uint64_t updates = 0;                                // every update hook call that ran its body
  std::array<const uint8_t*, MAX_SCRUBS> scrubs = {};  // beside the states: the cell hook's search reads these alone
  std::array<ScrubState, MAX_SCRUBS> states = {};
};
static ScrubStates scrub_states;
static_assert(std::is_trivially_destructible_v<ScrubStates>);
static std::atomic_bool scrub_table_full_logged = false;
// Whether a new scrub may take this state (the caller holds the lock). A never used one; else one without an update
// for STALE_SCRUB_UPDATES update calls (a scrub the game draws is updated every frame) whose scrub cannot come back
// holding a value of ours: its last update left the game's own values (1x), or its memory holds neither value written
// last (the game set both again, or the scrub is gone: other memory, or none). A scrub that still holds a written
// value may be alive and idle; with its state gone its next update would take our values for the game's and scale
// them again, so it is never taken.
static_assert(SCRUB_BIAS == SCRUB_RADIUS + sizeof(int32_t));
static bool ScrubStateFree(size_t index) noexcept {
  const ScrubState& state = scrub_states.states[index];
  if (state.updated == 0) return true;
  if (scrub_states.updates - state.updated <= STALE_SCRUB_UPDATES) return false;
  if (state.written_radius == state.game_radius && state.written_bias == state.game_bias) return true;
  // The scrub may be freed: its page is asked for first (committed, no guard page, readable), and the read is the
  // system's, which fails where a read of ours would fault. No answer or no read: not a scrub any more.
  const uint8_t* values = scrub_states.scrubs[index] + SCRUB_RADIUS;
  MEMORY_BASIC_INFORMATION page = {};
  std::array<uint32_t, 2> held = {};  // the radius, the bias
  SIZE_T bytes_read = 0;
  if (VirtualQuery(values, &page, sizeof(page)) == 0 || page.State != MEM_COMMIT
      || (page.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
      || ReadProcessMemory(GetCurrentProcess(), values, held.data(), sizeof(held), &bytes_read) == 0
      || bytes_read != sizeof(held)) {
    return true;
  }
  return held[0] != static_cast<uint32_t>(state.written_radius) && held[1] != state.written_bias;
}
// The hooks that write game memory run their bodies from their row's first scale that is not neutral on, for the
// process: back at 1x they write the game's values x 1, the game's bits (the 32-bit add-on's models_scaled).
static std::atomic_bool grass_scaled = false;
static std::atomic_bool campaign_trees_scaled = false;
// Both camera getters hold their code (the update hook reads it every frame: the loader and other plugins write
// game code after our init too).
static std::atomic_bool camera_getters_read = false;
// The grass draw's code from its entry to its no-LOD return is the code that was read (FNV-1a, 32 bits). Set at init,
// before the site's hook goes in, and again at game start (the loader and other plugins write game code after our
// init too); the hook's own early return stands on it, as the 32-bit add-on's draw_scrub_read.
inline constexpr size_t GRASS_DRAW_NO_LOD_BYTES = 0x110;
inline constexpr uint32_t GRASS_DRAW_NO_LOD_HASH = 0xd7a250e2;
inline constexpr const char* GRASS_DRAW_CODE_LINE =
    "renodx_attila64: grass draw: the game's code is not the code that was read, every draw goes to the game";
static std::atomic_bool grass_draw_code_read = false;
// Shared with the tests, which pin the same hash on the game file.
static uint32_t Fnv1a(const uint8_t* bytes, size_t count) noexcept {
  uint32_t hash = 0x811c9dc5;
  for (size_t index = 0; index < count; ++index) {
    hash = (hash ^ bytes[index]) * 0x01000193;
  }
  return hash;
}

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

// A scale as its hook applies it: neutral without its bit in scales_active.
static float ActiveScale(Scale scale) noexcept {
  return (((block.scales_active.load(std::memory_order_relaxed) & (1u << scale)) != 0)
              ? block.scales[scale].load(std::memory_order_relaxed)
              : NEUTRAL_SCALES[scale]);
}

// Whether the grass row's two hooks run their bodies. A hook cannot be taken out again: when the row's second hook
// was refused its first one stays in, so both test the row itself, whatever the add-on's scales say.
static bool GrassRowRuns() noexcept {
  if ((block.rows_applied & RowBit(ROW_GRASS)) == 0) return false;
  if (grass_scaled.load(std::memory_order_relaxed)) return true;
  if ((block.scales_active.load(std::memory_order_relaxed) & GRASS_SCALES) == 0) return false;
  grass_scaled.store(true, std::memory_order_relaxed);
  return true;
}

// ROW_GRASS, the scrub's update: the radius x Grass Distance and the bias x Grass Distance / Grass Density (the game
// squares the bias, as the LOD distances are squares, so the grass thins out as the game's, farther away; at 2x density
// all of it is drawn near the camera), each from the game's value: anything but the last value written is the game's
// (a new battle's scrub, or the game set it again). The 32-bit add-on's OnScrubUpdate (graphics_hooks.hpp); ours: the
// radius is cut to the models' reach only while a reach scale is active, so that 1x is the game's radius. Then the
// game's update with its own frame.
static ATTILA64_GAME_CALL void GrassUpdateEntry(uint8_t* scrub) {
  LogFirstCall(LABEL_GRASS_UPDATE_ENTRY);
  if (GrassRowRuns()) {
    camera_getters_read.store(
        std::memcmp(reinterpret_cast<const void*>(GET_SCENE_CAMERA), GET_SCENE_CAMERA_CODE.data(),
                    GET_SCENE_CAMERA_CODE.size()) == 0
            && std::memcmp(reinterpret_cast<const void*>(GET_CAMERA_POSITION), GET_CAMERA_POSITION_CODE.data(),
                           GET_CAMERA_POSITION_CODE.size()) == 0,
        std::memory_order_relaxed);
    const float radius_scale = ActiveScale(SCALE_GRASS_RADIUS);
    const float lod_scale = ActiveScale(SCALE_GRASS_LOD);
    int32_t radius = 0;
    uint32_t bias = 0;
    const uint8_t* node = nullptr;
    uint32_t slot_count = 0;
    const uint32_t* slots = nullptr;
    std::memcpy(&radius, scrub + SCRUB_RADIUS, sizeof(radius));
    std::memcpy(&bias, scrub + SCRUB_BIAS, sizeof(bias));
    std::memcpy(&node, scrub + SCRUB_NODE, sizeof(node));
    std::memcpy(&slot_count, scrub + SCRUB_SLOT_COUNT, sizeof(slot_count));
    std::memcpy(&slots, scrub + SCRUB_SLOTS, sizeof(slots));
    float lod_offset = 0.f;
    if (node != nullptr) {
      std::memcpy(&lod_offset, node + NODE_LOD_OFFSET, sizeof(lod_offset));
    }

    // This scrub's entry; else, of those that may be taken, the one longest without an update (a never used one
    // first). Under a lock of its own: the game's update is called from here when there is none, with no local of
    // ours alive.
    size_t entry = MAX_SCRUBS;
    bool first_update = false;
    {
      const std::scoped_lock lock(scrub_states.mutex);
      ++scrub_states.updates;
      entry = static_cast<size_t>(std::ranges::find(scrub_states.scrubs, scrub) - scrub_states.scrubs.begin());
      first_update = (entry == MAX_SCRUBS);
      for (size_t index = 0; first_update && index < MAX_SCRUBS; ++index) {
        if ((entry == MAX_SCRUBS || scrub_states.states[index].updated < scrub_states.states[entry].updated)
            && ScrubStateFree(index)) {
          entry = index;
        }
      }
      if (entry != MAX_SCRUBS) {
        scrub_states.scrubs[entry] = scrub;
        scrub_states.states[entry].updated = scrub_states.updates;
      }
    }
    if (entry == MAX_SCRUBS) {
      if (!scrub_table_full_logged.exchange(true)) {
        loader->log("renodx_attila64: grass: more than %zu grass areas at once, the rest keep the game's radius",
                    MAX_SCRUBS);
      }
      [[clang::musttail]] return reinterpret_cast<GrassUpdateFn>(originals[LABEL_GRASS_UPDATE_ENTRY])(scrub);
    }
    const std::scoped_lock lock(scrub_states.mutex);
    ScrubState& known = scrub_states.states[entry];
    if (first_update || radius != known.written_radius) {
      known.game_radius = radius;
    }
    if (first_update || bias != known.written_bias) {
      known.game_bias = bias;
    }
    known.written_radius = static_cast<int32_t>(std::lround(static_cast<float>(known.game_radius) * radius_scale));
    known.written_bias = std::bit_cast<uint32_t>(std::bit_cast<float>(known.game_bias) * radius_scale
                                                 / ActiveScale(SCALE_GRASS_DENSITY));
    // Per slot the radius its records need, from the model's LODs scaled here first (as its draws would): a record
    // farther than its model's farthest enabled LOD has no LOD (a key is never below its distance squared + min(the
    // node's offset, 0): the bias is a square), so the cell hook leaves it out. A NaN LOD never takes a record; an
    // infinite one leaves the radius as it is.
    known.slot_count = ((node != nullptr && slots != nullptr) ? std::min(slot_count, MAX_SCRUB_SLOTS) : 0);
    float farthest = 0.f;  // distance squared, over every slot
    for (uint32_t slot = 0; slot < known.slot_count; ++slot) {
      known.slot_radius[slot] = INT32_MAX;
      const uint8_t* model = model_lods::Scale(node, slots[slot], lod_scale, &block.notices);
      if (model == nullptr) continue;
      uint32_t lod_count = 0;
      const uint8_t* const* lods = nullptr;
      std::memcpy(&lod_count, model + model_lods::MODEL_LOD_COUNT, sizeof(lod_count));
      std::memcpy(&lods, model + model_lods::MODEL_LODS, sizeof(lods));
      float slot_farthest = -std::numeric_limits<float>::infinity();
      for (uint32_t lod = 0; lod < lod_count; ++lod) {
        if (lods[lod][model_lods::LOD_ENABLED] == 0) continue;
        float distance_squared = 0.f;
        std::memcpy(&distance_squared, lods[lod] + model_lods::LOD_DISTANCE_SQUARED, sizeof(distance_squared));
        farthest = std::max(farthest, distance_squared);
        slot_farthest = std::max(slot_farthest, distance_squared);
      }
      if (const double reach_squared = static_cast<double>(slot_farthest) - std::min(lod_offset, 0.f);
          !(reach_squared > 0.0)) {
        known.slot_radius[slot] = 0;
      } else if (const double slot_radius = std::ceil(std::sqrt(reach_squared)) + 1.0;
                 slot_radius < known.written_radius) {
        known.slot_radius[slot] = static_cast<int32_t>(slot_radius);
      }
    }
    // The cells no farther than 1 m past the farthest enabled LOD of any slot: a cell wholly past it holds no record
    // with a LOD, so the same set is drawn.
    if ((block.scales_active.load(std::memory_order_relaxed) & GRASS_REACH_SCALES) != 0 && farthest > 0.f) {
      if (const double reach = std::ceil(std::sqrt(static_cast<double>(farthest) - std::min(lod_offset, 0.f))) + 1.0;
          reach < known.written_radius) {
        known.written_radius = static_cast<int32_t>(reach);
      }
    }
    if (known.written_radius != radius) {
      std::memcpy(scrub + SCRUB_RADIUS, &known.written_radius, sizeof(radius));
    }
    if (known.written_bias != bias) {
      std::memcpy(scrub + SCRUB_BIAS, &known.written_bias, sizeof(bias));
    }
  }
  [[clang::musttail]] return reinterpret_cast<GrassUpdateFn>(originals[LABEL_GRASS_UPDATE_ENTRY])(scrub);
}

// ROW_GRASS, a grass or plant instance's draw: its model's LOD distances x Grass Distance squared (model_lods.hpp).
// Then the game's own pick, before the game: an instance past every enabled LOD of its model draws nothing there (the
// game's code from its entry to that return only reads, calls the two camera getters and writes its own frame), so its
// call is left out, as the 32-bit add-on's OnDrawScrub does. The key in the game's order: (dy^2 + dx^2) + dz^2, plus
// (the instance's bias + the node's offset); the first LOD with distance > key that is enabled is taken.
static ATTILA64_GAME_CALL void GrassDrawEntry(const uint8_t* node, const uint32_t* model, const float* transform,
                                              void* clip, const uint8_t* params, uintptr_t flag) {
  const auto original = reinterpret_cast<GrassDrawFn>(originals[LABEL_GRASS_DRAW_ENTRY]);
  LogFirstCall(LABEL_GRASS_DRAW_ENTRY);
  if (!GrassRowRuns()) {
    [[clang::musttail]] return original(node, model, transform, clip, params, flag);
  }
  const uint8_t* scaled_model = model_lods::Scale(node, *model, ActiveScale(SCALE_GRASS_LOD), &block.notices);
  if (scaled_model != nullptr && grass_draw_code_read.load(std::memory_order_relaxed)
      && camera_getters_read.load(std::memory_order_relaxed)) {
    const uint8_t* scene = nullptr;
    std::memcpy(&scene, node + NODE_SCENE, sizeof(scene));
    const uint8_t* camera = scene + SCENE_CAMERA;
    if ((*reinterpret_cast<const uintptr_t* const*>(scene))[SCENE_CAMERA_SLOT] == GET_SCENE_CAMERA
        && (*reinterpret_cast<const uintptr_t* const*>(camera))[CAMERA_POSITION_SLOT] == GET_CAMERA_POSITION) {
      uint32_t lod_count = 0;
      std::memcpy(&lod_count, scaled_model + model_lods::MODEL_LOD_COUNT, sizeof(lod_count));
      // The game reads the instance's bias only for a model with LODs.
      if (lod_count == 0) return;
      const uint8_t* const* lods = nullptr;
      std::array<float, 3> camera_position = {};
      float bias = 0.f;
      float lod_offset = 0.f;
      std::memcpy(&lods, scaled_model + model_lods::MODEL_LODS, sizeof(lods));
      std::memcpy(camera_position.data(), camera + CAMERA_POSITION, sizeof(camera_position));
      std::memcpy(&bias, params + PARAMS_BIAS, sizeof(bias));
      std::memcpy(&lod_offset, node + NODE_LOD_OFFSET, sizeof(lod_offset));
      const float dx = transform[TRANSFORM_POSITION_X / sizeof(float)] - camera_position[0];
      const float dy = transform[(TRANSFORM_POSITION_X + 0x10) / sizeof(float)] - camera_position[1];
      const float dz = transform[(TRANSFORM_POSITION_X + 0x20) / sizeof(float)] - camera_position[2];
      float distance_squared = (dy * dy) + (dx * dx);
      distance_squared = (dz * dz) + distance_squared;
      const float key = (bias + lod_offset) + distance_squared;
      bool picked = false;
      for (uint32_t lod = 0; lod < lod_count && !picked; ++lod) {
        float lod_distance_squared = 0.f;
        std::memcpy(&lod_distance_squared, lods[lod] + model_lods::LOD_DISTANCE_SQUARED, sizeof(lod_distance_squared));
        picked = (lod_distance_squared > key && lods[lod][model_lods::LOD_ENABLED] != 0);
      }
      if (!picked) return;
    }
  }
  [[clang::musttail]] return original(node, model, transform, clip, params, flag);
}

// ROW_GRASS_CELLS, one cell of the scrub's cell loop: the radius (squared) its records are tested against, cut to the
// largest radius the cell's records' slots need (the update hook's, the same proof): the loop sends the same records
// on, the others fail its distance test before their transform and their draw call. Only while the grass row is in
// (its update hook fills the states), a reach scale is active and the update hook holds this scrub's state; speed
// only. The 32-bit add-on's OnScrubCell. Then the game's loop.
static ATTILA64_GAME_CALL void GrassCellEntry(const uint8_t* scrub, const uint8_t* cell, const float* camera,
                                              CellClip clip, float radius_squared, uintptr_t flag) {
  LogFirstCall(LABEL_GRASS_CELL_ENTRY);
  if ((block.rows_applied & RowBit(ROW_GRASS)) != 0
      && (block.scales_active.load(std::memory_order_relaxed) & GRASS_REACH_SCALES) != 0 && cell != nullptr) {
    uint32_t record_count = 0;
    const uint8_t* records = nullptr;
    std::memcpy(&record_count, cell + CELL_RECORD_COUNT, sizeof(record_count));
    std::memcpy(&records, cell + CELL_RECORDS, sizeof(records));
    const std::shared_lock lock(scrub_states.mutex);
    const auto entry = static_cast<size_t>(std::ranges::find(scrub_states.scrubs, scrub) - scrub_states.scrubs.begin());
    if (entry != MAX_SCRUBS && scrub_states.states[entry].slot_count != 0) {
      const ScrubState& known = scrub_states.states[entry];
      int32_t radius = 0;
      for (uint32_t record = 0; record < record_count && radius < known.written_radius; ++record) {
        const uint8_t slot = records[(record * RECORD_BYTES) + RECORD_SLOT];
        radius = std::max(radius, ((slot < known.slot_count) ? known.slot_radius[slot] : INT32_MAX));
      }
      if (radius < known.written_radius) {
        radius_squared = std::min(radius_squared, static_cast<float>(int64_t{radius} * radius));
      }
    }
  }
  [[clang::musttail]] return reinterpret_cast<GrassCellFn>(originals[LABEL_GRASS_CELL_ENTRY])(scrub, cell, camera, clip,
                                                                                             radius_squared, flag);
}

// ROW_CAMPAIGN_TREES: a campaign map tree's draw picks its LOD against its model's entries, not through the model LOD
// pick: the model's LOD distances x the add-on's scale (the slider squared; model_lods.hpp), then the game's draw with
// its own frame. The 32-bit add-on's OnDrawTree.
static ATTILA64_GAME_CALL void* CampaignTreeEntry(const uint8_t* node, const uint32_t* model, void* position,
                                                  uintptr_t fourth, uintptr_t fifth, uintptr_t sixth, float first_float,
                                                  float second_float) {
  LogFirstCall(LABEL_CAMPAIGN_TREE_ENTRY);
  if (!campaign_trees_scaled.load(std::memory_order_relaxed)
      && (block.scales_active.load(std::memory_order_relaxed) & (1u << SCALE_CAMPAIGN_TREES)) != 0) {
    campaign_trees_scaled.store(true, std::memory_order_relaxed);
  }
  if (campaign_trees_scaled.load(std::memory_order_relaxed)) {
    model_lods::Scale(node, *model, ActiveScale(SCALE_CAMPAIGN_TREES), &block.notices);
  }
  [[clang::musttail]] return reinterpret_cast<CampaignTreeFn>(originals[LABEL_CAMPAIGN_TREE_ENTRY])(
      node, model, position, fourth, fifth, sixth, first_float, second_float);
}

// ROW_PARALLEL_EFFECTS: the walk over a scene node's effect types, once per node and frame on the game's main thread
// (twice for a node the sky's environment bake draws again). Then the game's walk with its own frame.
static ATTILA64_GAME_CALL void EffectWalkEntry(uint8_t* node, float first_float, float second_float) {
  LogFirstCall(LABEL_EFFECT_WALK_ENTRY);
  [[clang::musttail]] return reinterpret_cast<EffectWalkFn>(originals[LABEL_EFFECT_WALK_ENTRY])(node, first_float,
                                                                                             second_float);
}

// Each hook site's hook, by SiteLabel; a patch site has none (patcher_test checks that each label holds its own).
static const std::array<void*, LABEL_COUNT> REPLACEMENTS = [] {
  std::array<void*, LABEL_COUNT> hooks = {};
  hooks[LABEL_PACK_ENTRY] = reinterpret_cast<void*>(&PackEntry);
  hooks[LABEL_VIEW_DRAW_ENTRY] = reinterpret_cast<void*>(&ViewDrawEntry);
  hooks[LABEL_STEP_ENTRY] = reinterpret_cast<void*>(&StepTaskEntry);
  hooks[LABEL_UNIT_LOD_ENTRY] = reinterpret_cast<void*>(&UnitLodEntry);
  hooks[LABEL_MODEL_LOD_ENTRY] = reinterpret_cast<void*>(&ModelLodEntry);
  hooks[LABEL_GRASS_UPDATE_ENTRY] = reinterpret_cast<void*>(&GrassUpdateEntry);
  hooks[LABEL_GRASS_DRAW_ENTRY] = reinterpret_cast<void*>(&GrassDrawEntry);
  hooks[LABEL_GRASS_CELL_ENTRY] = reinterpret_cast<void*>(&GrassCellEntry);
  hooks[LABEL_CAMPAIGN_TREE_ENTRY] = reinterpret_cast<void*>(&CampaignTreeEntry);
  hooks[LABEL_EFFECT_WALK_ENTRY] = reinterpret_cast<void*>(&EffectWalkEntry);
  return hooks;
}();

}  // namespace twattila::attila64_plugin
