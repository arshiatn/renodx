/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <intrin.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/data.hpp"
#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "../../utils/settings.hpp"
#include "../twrome2/engine_tweaks.hpp"
#include "./frame_trace.hpp"
#include "./native_hook_lifetime.hpp"
#include "./object_motion.hpp"

// Attila's Graphics (Experimental) in empire.retail.dll, found by byte patterns: the unit, tree, grass and animation
// packing hooks, each scaled live by its slider. Rome II: the unit and dead unit hooks (its own patterns and offsets; trees and
// animation packing are Attila's code). Thrones of Britannia: Attila's code. Units have their own distances (not the
// LOD pick's, not the battle_entity_* tweaks), and so do the campaign map's trees.
namespace twattila::graphics_hooks {

enum class Game : uint8_t {
  ATTILA = 0,
  THRONES = 1,  // Attila's patterns; the per-soldier update's frame 4 bytes larger
  ROME2 = 2,
};

// The sliders (1: the game's).
struct Scales {
  // Imposter distances: foot soldiers, cavalry, elephants, artillery (the entity sizes small to huge).
  std::array<float, 4> unit_detail = {1.f, 1.f, 1.f, 1.f};
  float dead_units = 1.f;        // dead soldiers' imposter distance, x the living ones'
  float unit_animation = 1.f;    // shared pose distances
  float facial_animation = 1.f;  // facial animation distance
  float hand_animation = 1.f;    // hand animation distance
  float unit_variety = 1.f;      // no-variant distance
  float footstep_effects = 1.f;  // footstep effect distance
  float campaign_trees = 1.f;    // the tree LOD's distances
  float grass_distance = 1.f;    // grass and plants: their LOD distances, the scrub's cells and thinning
  float grass_density = 1.f;     // grass and plants: less thinning (more of the map's) within the same distance
  // The soldiers' shadow distance (16 below): 0 while nothing reads their 3D shadows (Attila: the rays' shadows in place of
  // the sun's shadow map, its casters skipped). The game then drops only the stretched or 3D shadow of the small and medium
  // soldiers its rule at +0x441f49 lets go; the others' shadow items stay (the Unit Detail 8x trace: 20 draws into the map).
  float unit_shadows = 1.f;
  bool big_battle_fix = false;   // unit detail and animation in until the frame's first pack holds every item
  // Small Soldiers as Sprites: a soldier is a sprite once it would be fewer than this many pixels tall on screen (0: the
  // game's distances), by `focal_pixels`, the main view's vertical focal length in output pixels (0: unknown, no cap).
  float sprite_pixels = 0.f;
  float focal_pixels = 0.f;
};

namespace internal {

// float* __thiscall UnitDistances(EntityDisplay* display, float* out, uint32_t quality, ... 3 more) (+0x43c160): the
// battle units' 18 distances for the Unit Detail setting, every frame; the display copies `out` (+0x234). The names
// are the game's override tweaks (ENTITY_LOD_OVERRIDE). At the highest Unit Detail:
//   0-3   imposter distance (units turn into flat sprites, dead ones too): small 90, medium 110, large 200, huge 1000 m
//   4-7   imposter fade width (a random switch within it per unit): a quarter of 0-3
//   8-11  shared pose distance (soldiers copy each other's animation beyond it): 40, 40, 60, 500 m
//   12    no-variant distance (100), 13 facial animation (10), 14 hand animation (10), 15 footstep effects (25),
//   16    unit shadows (10000 at every Unit Detail, through entity_display_shadow_max_shadow_distance, default 10000;
//         10000 too with entity_display_enable_imposter_shadows off, that value's only reader, +0x43c70d), 17 a cap
//         (unlimited above the lowest Unit Detail).
// Index 16's reader (+0x441eb0, called by the per-soldier update at +0x45a80d): a soldier's shadow record, its shadow
// stretched along the sun (+0x441a20, entity_display_shadow_extrusion_multiplier) and its shadow LOD. Past index 16
// (+0x441f49) a small or medium soldier skips the stretch, and its 3D shadow where its state's +0x10 or its model's
// +0xb0 is set (+0x30 cleared; +0x31 set with the first).
constexpr std::string_view UNIT_DISTANCES =
    "83 EC 40 80 7C 24 54 00 F3 0F 10 05 ?? ?? ?? ?? 56 57 8B F9 C7 44 24 28 00 00 80 3F F3 0F 11 44 24 30 74";
// Rome II's (+0x4497e0): the same arguments and 18 distances (its display copies them to +0x214).
constexpr std::string_view UNIT_DISTANCES_ROME2 =
    "83 EC 2C 80 7C 24 40 00 F3 0F 10 25 ?? ?? ?? ?? 56 F3 0F 11 64 24 2C 74";

// float __thiscall ImposterDistance(EntityDisplay* display, uint32_t size, float random) (+0x48a8f0): where a soldier
// turns into a sprite: the imposter distance for its size + (random - 0.5) x the fade width (the 18 above). Its only
// caller is the per-soldier update (+0x4567e0, the call +0x456d20; dead soldiers too): its third argument, the
// soldier's display state, sits above the frame the call's two arguments were pushed onto; bit 0 of its flags (+0xe4)
// is set for the living (+0x456b43: the living fade with ENTITY_TRACKER_ALPHA_CHANGE_SPEED, the dead with
// dead_time_alpha_fade_out_seconds). Dead soldiers get Dead Unit Detail x the distance. The update compares it with the
// soldier's distance to the camera in metres (+0x456a50 its square root, +0x456dbf the test: farther is a sprite).
constexpr std::string_view IMPOSTER_DISTANCE =
    "8B 44 24 04 83 F8 03 77 ?? FF 24 85 ?? ?? ?? ?? F3 0F 10 89 38 02 00 00 F3 0F 10 91 48 02 00 00";
// The call: mov eax, [esi + 8]; movss xmm0, [esi + 0xb4]; push ecx; mov ecx, [esi + 0xa8]; movss [esp], xmm0;
// push eax; call ImposterDistance. Its return address: the match + 29.
constexpr std::string_view IMPOSTER_DISTANCE_CALL =
    "8B 46 08 F3 0F 10 86 B4 00 00 00 51 8B 8E A8 00 00 00 F3 0F 11 04 24 50 E8";
constexpr size_t IMPOSTER_DISTANCE_CALL_RETURN = 29;
// From the return address: it, the two arguments, the caller's frame (0x144 + esi, edi pushed), its return address,
// then its arguments (the soldier's display state third).
constexpr size_t CALLER_SOLDIER = 12 + 0x144 + 8 + 4 + 8;
constexpr size_t CALLER_SOLDIER_THRONES = 12 + 0x148 + 8 + 4 + 8;  // its update (+0x45c880): sub esp, 0x148
constexpr size_t SOLDIER_FLAGS = 0xe4;
// Rome II's: the display's distances 0x20 lower, the per-soldier update (+0x464430) with a 0x1d0 frame, the call
// (+0x464960) with the display state's fields 0xc lower, the alive flag at +0xd8.
constexpr std::string_view IMPOSTER_DISTANCE_ROME2 =
    "8B 44 24 04 83 F8 03 77 ?? FF 24 85 ?? ?? ?? ?? F3 0F 10 89 18 02 00 00 F3 0F 10 91 28 02 00 00";
constexpr std::string_view IMPOSTER_DISTANCE_CALL_ROME2 =
    "8B 46 08 F3 0F 10 86 A8 00 00 00 51 8B 8E 9C 00 00 00 F3 0F 11 04 24 50 E8";
constexpr size_t CALLER_SOLDIER_ROME2 = 12 + 0x1d0 + 8 + 4 + 8;
constexpr size_t SOLDIER_FLAGS_ROME2 = 0xd8;
constexpr uint8_t SOLDIER_ALIVE = 1;
// A soldier's height by its size (ImposterDistance's 0-3; past 3 the game takes 0's), metres: estimates for Small
// Soldiers as Sprites (a foot soldier, a rider, an elephant, a gun with its crew), not measured.
constexpr std::array<float, 4> SOLDIER_HEIGHTS = {1.8f, 2.6f, 4.f, 3.f};

// The soldiers' bone matrices go to animation textures (pages) of 512 x 672 texels (6 or 10, made by +0x184d800).
// bool __thiscall PackAnimations(Manager* pages, ... 2, List* items, uint32_t* next, uint32_t end) (+0x1893cc0) packs
// the model manager's soldier items [*next, end) into them, page by page, *next past the last one that fit (an item
// whose pose was packed in this call takes that pose's rows). The items: one list (+0xd8: count +0xdc, items +0xe0) for
// every pass, sorted by pass (begin +0xe4 and end +0x140 per pass kind, 23 kinds), so a pose drawn by the main pass and
// the four sun cascades is one item per pass. The soldiers' draw for a pass (+0x17fb8a0, from the manager's render of a
// pass +0x17fa310) first runs the frame's first pack (+0x186be70, once a frame by +0xb60: the call FIRST_PACK_CALL):
// every pass's items [0, count), whichever pass draws soldiers first (in ATN's battles the sun's cascades; the 8x trace's
// "0 to 418030 of 418030" counts those items, not soldiers). Then each pass draws the part of its range the pages hold
// and packs and draws the rest (+0x17fbb80). Items left over by the first pack keep an older frame's offset in the passes
// that don't pack again: huge stretched shadows jumping around in big fights (ATN, 8x unit sizes; v12's taller pages made
// soldiers vanish: most likely the 32-bit game's address space). Big Battle Fix: while items are left over there, unit
// detail and animation distances come in (fewer 3D, individually animated soldiers), then slowly back out. Before every
// pack, object motion records the soldiers drawn since the last one (object_motion.hpp FlushUnitRecords: their bones are
// gone after it).
constexpr std::string_view PACK_ANIMATIONS =
    "51 53 8B 5C 24 1C 55 8B 6C 24 1C 56 57 8B F1 39 5D 00 0F 84 ?? ?? ?? ?? 8B 46 04 B9";
// lea ecx, [esi + 0xbd0]; push dword ptr [esp + 0x1c]; call PackAnimations. Its return address: the match + 15.
constexpr std::string_view FIRST_PACK_CALL = "8D 8E D0 0B 00 00 FF 74 24 1C E8";
constexpr size_t FIRST_PACK_CALL_RETURN = 15;
constexpr float MIN_BATTLE_BUDGET = 0.09998233f;
constexpr float BATTLE_BUDGET_RECOVERY = 1.002f;  // per frame: about 1.13x a second at 60 fps
constexpr uint32_t BATTLE_BUDGET_HOLD_FRAMES = 180;  // frames without left-over soldiers before it recovers

// void __thiscall DrawTree(ModelManager* manager, uint32_t model, const float* position, ... 5 more) (+0x17ee130, a
// vtable method): a tree instance's LOD by its distance (with tree_lodding_method: projected and x tree_lod_scaler),
// against the model's LOD entries (count +0x28, list +0x2c, distance squared +0x38), then draws it. The campaign map's
// trees go through it, not the LOD pick. The hook scales the model's LOD distances (per model: the game's values kept,
// a value the game sets again is the new base).
constexpr std::string_view DRAW_TREE =
    "8B 44 24 04 83 EC 28 53 8B 5C 24 34 55 56 57 8B F9 3B 47 1C 73 08 8B 57 20 8B 34 82 EB 02 33 F6 B9 ?? ?? ?? ?? E8";
// void __thiscall DrawScrub(ModelManager* manager, uint32_t model, const float* transform, ... 3 more) (+0x17f9800,
// the vtable method before DrawTree): a grass or plant instance of the scrub (ScrubUpdate), its distance squared plus
// its bias (the 4th argument's +0x10) against the model's LOD entries (as DrawTree's; ATN debug9: the grass clumps' last
// one ~100 m, the circle the grass ends at), then draws it. The hook scales the model's LOD distances (as DrawTree's).
constexpr std::string_view DRAW_SCRUB =
    "83 EC 4C 8B 44 24 50 53 55 8B E9 56 57 89 6C 24 10 3B 05 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? 3B 45 1C 73 08 8B 4D 20 8B "
    "1C 81";
// void __thiscall ScrubUpdate(Scrub* scrub) (+0x17fb510): every frame, the grass cells within +0x80 (int: the tweak
// scrub_lod_distance_default, 200 m) of the camera, each instance with a bias (its random byte / 255 x +0x84, 200.f;
// squared, added to its distance squared) that thins the grass out with distance: near the camera half the map's grass
// is drawn (the grass clumps' last LOD, 100 m, against a bias up to 200 m). The hook writes both x Grass Distance, the
// bias / Grass Density (2x: all of it near the camera; the game's values kept).
// +0x80 has two readers (both in ScrubUpdate, without scrub_node_always_render): the quadtree query (+0x1898800) of the
// box camera +- float(+0x80) (a cell is left out with its subtree when its bounds, which hold its children's, miss the
// box), and the cell loop (+0x1897a30, per cell) taking a record (18 bytes: position first, +0x10 its slot in the scrub's
// models) to DrawScrub only while its distance squared <= float(+0x80 squared as an int). DrawScrub then takes the same
// distance squared (the same floats, the sum in another order) + (manager +0xc34 + the record's bias, which is >= 0) as
// its key: a record past every enabled LOD (+0x44) of its model draws nothing. The scrub's models: +0x90 the manager,
// +0x98 their count, +0x9c the manager's model index per slot (filled by +0x1875fc0, a slot a byte).
constexpr std::string_view SCRUB_UPDATE =
    "83 EC 4C 53 55 8B E9 56 57 89 6C 24 14 80 7D 14 00 0F 84 ?? ?? ?? ?? 83 BD 88 00 00 00 00 0F 84 ?? ?? ?? ?? E8 ?? "
    "?? ?? ?? 80 BD 8C 00 00 00 01";
constexpr size_t SCRUB_CELLS = 0x80;
constexpr size_t SCRUB_BIAS = 0x84;
constexpr size_t SCRUB_MANAGER = 0x90;
constexpr size_t SCRUB_MODEL_COUNT = 0x98;
constexpr size_t SCRUB_MODELS = 0x9c;
constexpr uint32_t MAX_SCRUB_MODELS = 256;
constexpr size_t MANAGER_LOD_OFFSET = 0xc34;  // a signed square, added to every key (+0x188afc0)
constexpr size_t MAX_MODEL_LODS = 8;

// __thiscall through __fastcall: `this` in ecx, edx unused, the rest on the stack (callee pops).
using UnitDistances = float*(__fastcall*)(void* display, void* unused_edx, float* out, uint32_t quality, uint32_t second,
                                          uint32_t third, uint32_t fourth);
using ImposterDistance = float(__fastcall*)(void* display, void* unused_edx, uint32_t size, float random);
using PackAnimations = bool(__fastcall*)(void* pages, void* unused_edx, uint32_t first, uint32_t second, void* soldiers,
                                         uint32_t* next, uint32_t end);
using DrawTree = void*(__fastcall*)(uint8_t* manager, void* unused_edx, uint32_t model, const float* position,
                                    uint32_t third, uint32_t fourth, uint32_t fifth, uint32_t sixth, uint32_t seventh);
using DrawScrub = void(__fastcall*)(uint8_t* manager, void* unused_edx, uint32_t model, const float* transform,
                                    uint32_t third, uint32_t fourth, uint32_t fifth);
using ScrubUpdate = void(__fastcall*)(uint8_t* scrub, void* unused_edx);

struct ModelLods {
  uint32_t count = 0;
  std::array<float, MAX_MODEL_LODS> game = {};       // the game's distances squared
  std::array<uint32_t, MAX_MODEL_LODS> written = {};  // the scaled ones the addon wrote
};

// The scrub's cells and bias as the game set them and as the addon wrote them.
struct ScrubValues {
  const uint8_t* scrub = nullptr;
  int32_t game_cells = 0;
  float game_bias = 0.f;
  int32_t written_cells = 0;
  float written_bias = 0.f;
};

static UnitDistances original_unit_distances = nullptr;
static DrawTree original_draw_tree = nullptr;
static DrawScrub original_draw_scrub = nullptr;
static ScrubUpdate original_scrub_update = nullptr;
static ImposterDistance original_imposter_distance = nullptr;
static uint8_t* imposter_distance_return = nullptr;
static size_t caller_soldier = CALLER_SOLDIER;  // the game's (set at install)
static size_t soldier_flags = SOLDIER_FLAGS;
static std::atomic<float> dead_scale = 1.f;
// Small Soldiers as Sprites: the distance at which a soldier 1 m tall is the limit's pixels tall (0: off).
static std::atomic<float> sprite_distance_scale = 0.f;
static std::vector<renodx::utils::detour::Function> hooks;
static tw::native_hooks::Lifetime hook_lifetime;
static std::atomic<bool> hooks_active = false;
static std::array<std::atomic<float>, 4> imposter_scales;
static std::atomic<float> animation_scale = 1.f;
static std::atomic<float> facial_scale = 1.f;
static std::atomic<float> hand_scale = 1.f;
static std::atomic<float> variety_scale = 1.f;
static std::atomic<float> footstep_scale = 1.f;
static std::atomic<float> unit_shadow_scale = 1.f;
static std::atomic<float> tree_distance_squared_scale = 1.f;
static std::atomic<float> grass_scale = 1.f;
static std::atomic<float> grass_density = 1.f;
static renodx::utils::data::ParallelFlatHashMap<uintptr_t, ModelLods, std::shared_mutex> scaled_models;
static std::atomic<bool> models_scaled = false;  // a model's LOD distances were scaled once (until then 1x: no lookups)
// The models this thread scaled, by address (direct-mapped): the scrub's cells draw their instances model after model in
// turn (ATN's grass profile: a one-model cache missed there and the map's lock took 0.53 ms a frame). An entry holds what
// the addon wrote; a model whose LOD distances all still hold it at the same scale needs nothing, as the map's path
// finds too (every distance the addon's: nothing to write, nothing new to keep).
struct CachedModel {
  const uint8_t* model = nullptr;
  float scale = 1.f;
  uint32_t count = 0;
  std::array<uint32_t, MAX_MODEL_LODS> written = {};
};
constexpr size_t CACHED_MODELS = 64;
static thread_local std::array<CachedModel, CACHED_MODELS> cached_models;
static ScrubValues scrub_values;  // the scrub update's thread
// Grass Trace (ATN: grass still ends ~100 m with v64's fade patch and every grass slider up): one frame of the scrub
// (from one ScrubUpdate to the next) into ReShade.log: its cells and bias, every DrawScrub call by its distance (10 m
// bins, the last 400 m and more) and whether one of its model's LODs takes it, the first models' LOD distances.
struct GrassTrace {
  std::atomic<int> state = 0;  // 0 idle, 1 asked (the next update starts it), 2 tracing
  std::mutex mutex;
  std::array<uint32_t, 41> calls = {};
  std::array<uint32_t, 41> picked = {};
  std::vector<std::pair<const uint8_t*, std::string>> models;
};
static GrassTrace grass_trace;
static PackAnimations original_pack_animations = nullptr;
static uint8_t* first_pack_return = nullptr;
static std::atomic<bool> big_battle_fix = false;
// x the unit detail and animation distances (Big Battle Fix), 1 without overflow.
static std::atomic<float> battle_budget = 1.f;
static uint32_t frames_without_overflow = 0;  // the first pack's thread (the render thread)

static bool __fastcall OnPackAnimations(void* pages, void* unused_edx, uint32_t first, uint32_t second, void* soldiers,
                                        uint32_t* next, uint32_t end) {
  if (!hooks_active.load(std::memory_order_relaxed)) return original_pack_animations(pages, unused_edx, first, second, soldiers, next, end);
  // The soldiers drawn since the last pack: their motion's bones recorded while the pages still hold them.
  object_motion::FlushUnitRecords();
  const uint32_t start = (next != nullptr ? *next : end);
  const bool packed = original_pack_animations(pages, unused_edx, first, second, soldiers, next, end);
  const auto* return_slot = static_cast<const uint8_t*>(_AddressOfReturnAddress());
  uint8_t* return_address;
  std::memcpy(&return_address, return_slot, sizeof(return_address));
  if (frame_trace::Tracing()) {
    frame_trace::Mark(std::format("soldiers' bones packed ({}): items {} to {} of {}",
                                  (return_address == first_pack_return ? "the frame's first pack, every pass's items" : "a pass's rest"),
                                  start, (next != nullptr ? *next : end), end));
  }
  if (return_address != first_pack_return || next == nullptr || end <= start) return packed;
  const uint32_t fitted = std::min(*next, end) - start;
  const uint32_t total = end - start;
  float budget = battle_budget.load(std::memory_order_relaxed);
  if (!big_battle_fix.load(std::memory_order_relaxed)) {
    budget = 1.f;
  } else if (fitted < total) {
    // About the area's share that fit (soldiers ~ distance squared), a little more.
    budget = std::max(MIN_BATTLE_BUDGET, budget * std::sqrt(static_cast<float>(fitted) / total) * 0.97f);
    frames_without_overflow = 0;
  } else if (++frames_without_overflow > BATTLE_BUDGET_HOLD_FRAMES) {
    budget = std::min(1.f, budget * BATTLE_BUDGET_RECOVERY);
  }
  battle_budget.store(budget, std::memory_order_relaxed);
  return packed;
}

static float* __fastcall OnUnitDistances(void* display, void* unused_edx, float* out, uint32_t quality, uint32_t second,
                                         uint32_t third, uint32_t fourth) {
  if (!hooks_active.load(std::memory_order_relaxed)) return original_unit_distances(display, unused_edx, out, quality, second, third, fourth);
  float* distances = original_unit_distances(display, unused_edx, out, quality, second, third, fourth);
  const auto scale = [&](size_t first, size_t last, const std::atomic<float>& factor) {
    for (size_t index = first; index <= last; ++index) {
      distances[index] = std::min(distances[index] * factor.load(std::memory_order_relaxed), FLT_MAX);
    }
  };
  const float budget = battle_budget.load(std::memory_order_relaxed);
  // 0-3 only: the fade widths (4-7) stay the game's.
  for (size_t index = 0; index < imposter_scales.size(); ++index) {
    distances[index] = std::min(distances[index] * imposter_scales[index].load(std::memory_order_relaxed) * budget,
                                FLT_MAX);
  }
  scale(8, 11, animation_scale);
  scale(12, 12, variety_scale);
  scale(13, 13, facial_scale);
  scale(14, 14, hand_scale);
  for (const size_t index : {8, 9, 10, 11, 13, 14}) {
    distances[index] *= budget;
  }
  scale(15, 15, footstep_scale);
  scale(16, 16, unit_shadow_scale);
  return distances;
}

static float __fastcall OnImposterDistance(void* display, void* unused_edx, uint32_t size, float random) {
  if (!hooks_active.load(std::memory_order_relaxed)) return original_imposter_distance(display, unused_edx, size, random);
  float distance = original_imposter_distance(display, unused_edx, size, random);
  // Small Soldiers as Sprites: no farther than where a soldier of its size is the limit's pixels tall, with the game's
  // spread around it (its random switch within the fade width; random 0.5 gives the distance itself).
  if (const float sprite_scale = sprite_distance_scale.load(std::memory_order_relaxed); sprite_scale > 0.f) {
    const float spread = distance - original_imposter_distance(display, unused_edx, size, 0.5f);
    const float height = SOLDIER_HEIGHTS[size < SOLDIER_HEIGHTS.size() ? size : 0];
    distance = std::min(distance, height * sprite_scale + spread);
  }
  const float dead = dead_scale.load(std::memory_order_relaxed);
  if (dead == 1.f) return distance;
  // Only from the per-soldier update's call, whose frame layout is known.
  const auto* return_slot = static_cast<const uint8_t*>(_AddressOfReturnAddress());
  uint8_t* return_address;
  std::memcpy(&return_address, return_slot, sizeof(return_address));
  if (return_address != imposter_distance_return) return distance;
  const uint8_t* soldier;
  std::memcpy(&soldier, return_slot + caller_soldier, sizeof(soldier));
  if (soldier == nullptr) return distance;
  const bool is_dead = (soldier[soldier_flags] & SOLDIER_ALIVE) == 0;
  return (is_dead ? distance * dead : distance);
}

// The manager's model `index` (its list +0x20, count +0x1c): its LOD distances (squared) x `scale` (squared), the game's
// values kept per model (DrawTree's and DrawScrub's). Returns the model (null: none at `index`).
static uint8_t* ScaleModelLods(uint8_t* manager, uint32_t index, float scale) {
  uint32_t model_count;
  std::memcpy(&model_count, manager + 0x1c, sizeof(model_count));
  uint8_t* model = nullptr;
  if (index < model_count) {
    uint8_t** models;
    std::memcpy(&models, manager + 0x20, sizeof(models));
    model = models[index];
  }
  if (model != nullptr && (scale != 1.f || models_scaled.load(std::memory_order_relaxed))) {
    uint32_t count;
    std::memcpy(&count, model + 0x28, sizeof(count));
    count = std::min<uint32_t>(count, MAX_MODEL_LODS);
    uint8_t** entries;
    std::memcpy(&entries, model + 0x2c, sizeof(entries));
    const auto key = reinterpret_cast<uintptr_t>(model);
    auto& cached = cached_models[((key >> 4) ^ (key >> 12)) % CACHED_MODELS];
    const auto holds_written = [&] {
      for (uint32_t index = 0; index < count; ++index) {
        uint32_t bits;
        std::memcpy(&bits, entries[index] + 0x38, sizeof(bits));
        if (bits != cached.written[index]) return false;
      }
      return true;
    };
    if (cached.model == model && cached.scale == scale && cached.count == count && holds_written()) return model;
    models_scaled.store(true, std::memory_order_relaxed);
    const auto scaled = [&](float game) {
      return std::bit_cast<uint32_t>(game < FLT_MAX ? std::min(game * scale, FLT_MAX) : game);
    };
    // Most misses find the model as another thread (or this one before an eviction) left it: nothing to write, under the
    // entry's shared lock.
    std::array<uint32_t, MAX_MODEL_LODS> written = {};
    bool holds_scaled = false;
    scaled_models.if_contains(key, [&](const auto& entry) {
      const auto& [model_key, model_lods] = entry;
      holds_scaled = (model_lods.count == count);
      for (uint32_t index = 0; holds_scaled && index < count; ++index) {
        uint32_t bits;
        std::memcpy(&bits, entries[index] + 0x38, sizeof(bits));
        holds_scaled = (bits == model_lods.written[index] && bits == scaled(model_lods.game[index]));
      }
      written = model_lods.written;
    });
    if (holds_scaled) {
      cached = {.model = model, .scale = scale, .count = count, .written = written};
      return model;
    }
    // The entry read, the LODs written and the entry updated under the entry's lock: as three steps, two threads scaling
    // one model at once (its first scaling, or its first after a slider change) could take each other's written value for
    // the game's and scale the model twice.
    const auto scale_lods = [&](ModelLods* lods) {
      const bool count_changed = (lods->count != count);
      lods->count = count;
      for (uint32_t index = 0; index < count; ++index) {
        uint32_t bits;
        std::memcpy(&bits, entries[index] + 0x38, sizeof(bits));
        // Anything but the addon's last value is the game's (its setup again, or a new model at the same address).
        if (count_changed || bits != lods->written[index]) {
          lods->game[index] = std::bit_cast<float>(bits);
        }
        lods->written[index] = scaled(lods->game[index]);
        if (lods->written[index] != bits) {
          std::memcpy(entries[index] + 0x38, &lods->written[index], sizeof(bits));
        }
      }
      written = lods->written;
    };
    scaled_models.lazy_emplace_l(
        key,
        [&](auto& entry) {
          auto& [model_key, model_lods] = entry;
          scale_lods(&model_lods);
        },
        [&](const auto& construct) {
          ModelLods model_lods;
          scale_lods(&model_lods);
          construct(key, model_lods);
        });
    cached = {.model = model, .scale = scale, .count = count, .written = written};
  }
  return model;
}

static void* __fastcall OnDrawTree(uint8_t* manager, void* unused_edx, uint32_t model, const float* position,
                                   uint32_t third, uint32_t fourth, uint32_t fifth, uint32_t sixth, uint32_t seventh) {
  if (!hooks_active.load(std::memory_order_relaxed)) return original_draw_tree(manager, unused_edx, model, position, third, fourth, fifth, sixth, seventh);
  ScaleModelLods(manager, model, tree_distance_squared_scale.load(std::memory_order_relaxed));
  return original_draw_tree(manager, unused_edx, model, position, third, fourth, fifth, sixth, seventh);
}

// A DrawScrub call while tracing: its distance and LOD as DrawScrub finds them (the camera from +0x34's view, vtable
// +0x28, and its position, vtable +0x3c; the distance squared plus +0xc34 and the call's bias, its 4th argument's +0x10).
static void TraceScrub(uint8_t* manager, uint32_t index, const float* transform, uint32_t params) {
  using GetView = uint8_t*(__fastcall*)(uint8_t* self, void* unused_edx);
  using GetPosition = const float*(__fastcall*)(uint8_t* self, void* unused_edx, float* out);
  uint8_t* provider;
  std::memcpy(&provider, manager + 0x34, sizeof(provider));
  if (provider == nullptr || transform == nullptr || params == 0) return;
  void** vtable;
  std::memcpy(&vtable, provider, sizeof(vtable));
  uint8_t* view = reinterpret_cast<GetView>(vtable[0x28 / 4])(provider, nullptr);
  if (view == nullptr) return;
  std::memcpy(&vtable, view, sizeof(vtable));
  std::array<float, 16> buffer = {};
  const float* camera = reinterpret_cast<GetPosition>(vtable[0x3c / 4])(view, nullptr, buffer.data());
  if (camera == nullptr) return;
  const float x = transform[3] - camera[0];
  const float y = transform[7] - camera[1];
  const float z = transform[11] - camera[2];
  float extra;
  float bias;
  std::memcpy(&extra, manager + 0xc34, sizeof(extra));
  std::memcpy(&bias, reinterpret_cast<const uint8_t*>(params) + 0x10, sizeof(bias));
  const float key = x * x + y * y + z * z + extra + bias;
  uint32_t model_count;
  std::memcpy(&model_count, manager + 0x1c, sizeof(model_count));
  uint8_t* model = nullptr;
  if (index < model_count) {
    uint8_t** models;
    std::memcpy(&models, manager + 0x20, sizeof(models));
    model = models[index];
  }
  bool picked = false;
  std::string lods;
  if (model != nullptr) {
    uint32_t count;
    std::memcpy(&count, model + 0x28, sizeof(count));
    uint8_t** entries;
    std::memcpy(&entries, model + 0x2c, sizeof(entries));
    for (uint32_t entry = 0; entry < std::min<uint32_t>(count, MAX_MODEL_LODS); ++entry) {
      float distance;
      std::memcpy(&distance, entries[entry] + 0x38, sizeof(distance));
      const bool enabled = entries[entry][0x44] != 0;
      picked |= (enabled && distance > key);
      lods += std::format("{}{:.0f}{}", lods.empty() ? "" : " / ", std::sqrt(distance), enabled ? "" : " (off)");
    }
  }
  const auto bin = std::min<size_t>(static_cast<size_t>(std::sqrt(x * x + y * y + z * z) / 10.f), 40);
  const std::unique_lock lock(grass_trace.mutex);
  ++grass_trace.calls[bin];
  grass_trace.picked[bin] += picked ? 1 : 0;
  if (model != nullptr && grass_trace.models.size() < 16
      && std::ranges::find(grass_trace.models, model, &std::pair<const uint8_t*, std::string>::first)
             == grass_trace.models.end()) {
    grass_trace.models.emplace_back(model, std::format("model {} (+0xc34 {:.0f}, bias {:.0f}): LODs {}", index, extra,
                                                       bias, lods));
  }
}

static void __fastcall OnDrawScrub(uint8_t* manager, void* unused_edx, uint32_t model, const float* transform,
                                   uint32_t third, uint32_t fourth, uint32_t fifth) {
  if (!hooks_active.load(std::memory_order_relaxed)) return original_draw_scrub(manager, unused_edx, model, transform, third, fourth, fifth);
  const float scale = grass_scale.load(std::memory_order_relaxed);
  ScaleModelLods(manager, model, scale * scale);
  if (grass_trace.state.load(std::memory_order_relaxed) == 2) TraceScrub(manager, model, transform, fourth);
  original_draw_scrub(manager, unused_edx, model, transform, third, fourth, fifth);
}

// The bias x the scale (squared by the game, as the LOD distances), so the grass thins out as the game's, farther away.
static void __fastcall OnScrubUpdate(uint8_t* scrub, void* unused_edx) {
  if (!hooks_active.load(std::memory_order_relaxed)) return original_scrub_update(scrub, unused_edx);
  const float scale = grass_scale.load(std::memory_order_relaxed);
  int32_t cells;
  float bias;
  std::memcpy(&cells, scrub + SCRUB_CELLS, sizeof(cells));
  std::memcpy(&bias, scrub + SCRUB_BIAS, sizeof(bias));
  auto& known = scrub_values;
  // Anything but the addon's last value is the game's (a new battle's scrub, or the game set it again).
  if (scrub != known.scrub || cells != known.written_cells) known.game_cells = cells;
  if (scrub != known.scrub || bias != known.written_bias) known.game_bias = bias;
  known.scrub = scrub;
  known.written_cells = static_cast<int32_t>(std::lround(static_cast<float>(known.game_cells) * scale));
  known.written_bias = known.game_bias * scale / grass_density.load(std::memory_order_relaxed);
  // The cells no farther than 1 m past the scrub models' farthest enabled LOD (scaled here first, as their draws would):
  // a record past it has no LOD, so neither it nor a cell wholly past it is visited (SCRUB_UPDATE: the same set drawn).
  uint8_t* manager;
  uint32_t slot_count;
  const uint32_t* slots;
  std::memcpy(&manager, scrub + SCRUB_MANAGER, sizeof(manager));
  std::memcpy(&slot_count, scrub + SCRUB_MODEL_COUNT, sizeof(slot_count));
  std::memcpy(&slots, scrub + SCRUB_MODELS, sizeof(slots));
  float farthest = 0.f;  // distance squared; a NaN LOD never takes a record, an infinite one leaves the cells as they are
  for (uint32_t slot = 0; manager != nullptr && slots != nullptr && slot < std::min(slot_count, MAX_SCRUB_MODELS); ++slot) {
    const uint8_t* model = ScaleModelLods(manager, slots[slot], scale * scale);
    if (model == nullptr) continue;
    uint32_t count;
    uint8_t** entries;
    std::memcpy(&count, model + 0x28, sizeof(count));
    std::memcpy(&entries, model + 0x2c, sizeof(entries));
    for (uint32_t lod = 0; lod < count; ++lod) {
      float distance;
      std::memcpy(&distance, entries[lod] + 0x38, sizeof(distance));
      if (entries[lod][0x44] != 0 && distance > farthest) {
        farthest = distance;
      }
    }
  }
  double reach = 0.0;  // metres (0: no scrub model with an enabled LOD found, the cells left as they are)
  if (farthest > 0.f) {
    float lod_offset;
    std::memcpy(&lod_offset, manager + MANAGER_LOD_OFFSET, sizeof(lod_offset));
    reach = std::sqrt(static_cast<double>(farthest) - std::min(lod_offset, 0.f));
    if (const double radius = std::ceil(reach) + 1.0; radius < known.written_cells) {
      known.written_cells = static_cast<int32_t>(radius);
    }
  }
  std::memcpy(scrub + SCRUB_CELLS, &known.written_cells, sizeof(cells));
  std::memcpy(scrub + SCRUB_BIAS, &known.written_bias, sizeof(bias));
  // Grass Trace: the last frame's draws out, or this frame's started.
  if (grass_trace.state.load() == 2) {
    const std::unique_lock lock(grass_trace.mutex);
    renodx::utils::log::i("[Total War] Grass trace: cells ", known.written_cells, " (game ", known.game_cells,
                          ", scrub LODs end at ", std::round(reach), " m), bias x ", known.written_bias, " (game ",
                          known.game_bias, ")");
    for (size_t bin = 0; bin < grass_trace.calls.size(); ++bin) {
      if (grass_trace.calls[bin] == 0) continue;
      renodx::utils::log::i("[Total War]   ", bin * 10, (bin == 40 ? "+ m" : " m"), ": ", grass_trace.calls[bin],
                            " draws, ", grass_trace.picked[bin], " with a LOD");
    }
    for (const auto& [model, text] : grass_trace.models) renodx::utils::log::i("[Total War]   ", text);
    grass_trace.state = 0;
  } else if (grass_trace.state.load() == 1) {
    const std::unique_lock lock(grass_trace.mutex);
    grass_trace.calls = {};
    grass_trace.picked = {};
    grass_trace.models.clear();
    grass_trace.state = 2;
  }
  original_scrub_update(scrub, unused_edx);
}

}  // namespace internal

// Grass Trace: one frame of the scrub into ReShade.log (from the next scrub update).
inline void TraceGrass() { internal::grass_trace.state = 1; }

// Every present: hooks / finds once empire.retail.dll is loaded (the result appended to `status`), then the sliders.
// Rome II: its unit and dead unit hooks only.
inline void Update(const Scales& scales, bool* installed, renodx::utils::settings::Setting* status,
                   Game game = Game::ATTILA) {
  const bool rome2 = (game == Game::ROME2);
  for (size_t index = 0; index < internal::imposter_scales.size(); ++index) {
    internal::imposter_scales[index].store(scales.unit_detail[index], std::memory_order_relaxed);
  }
  internal::dead_scale.store(scales.dead_units, std::memory_order_relaxed);
  internal::sprite_distance_scale.store(
      (scales.sprite_pixels > 0.f && scales.focal_pixels > 0.f) ? scales.focal_pixels / scales.sprite_pixels : 0.f,
      std::memory_order_relaxed);
  internal::animation_scale.store(scales.unit_animation, std::memory_order_relaxed);
  internal::facial_scale.store(scales.facial_animation, std::memory_order_relaxed);
  internal::hand_scale.store(scales.hand_animation, std::memory_order_relaxed);
  internal::variety_scale.store(scales.unit_variety, std::memory_order_relaxed);
  internal::footstep_scale.store(scales.footstep_effects, std::memory_order_relaxed);
  internal::unit_shadow_scale.store(scales.unit_shadows, std::memory_order_relaxed);
  internal::big_battle_fix.store(scales.big_battle_fix, std::memory_order_relaxed);
  internal::tree_distance_squared_scale.store(scales.campaign_trees * scales.campaign_trees, std::memory_order_relaxed);
  internal::grass_scale.store(scales.grass_distance, std::memory_order_relaxed);
  internal::grass_density.store(std::max(scales.grass_density, 0.010001118f), std::memory_order_relaxed);
  if (!*installed && !internal::hooks.empty()) {
    *installed = true;
    internal::hooks_active.store(true, std::memory_order_relaxed);
    return;
  }
  if (!*installed) {
    HMODULE module = GetModuleHandleW(L"empire.retail.dll");
    if (module == nullptr) return;
    *installed = true;
    const auto sections = tw::engine_tweaks::ModuleSections(module);
    std::string missing;
    const auto add = [&](std::string_view pattern, void** original, void* replacement, std::string_view name) {
      uint8_t* target = tw::engine_tweaks::FindPattern(sections.code, pattern);
      if (target == nullptr) {
        missing += std::format("{}{}", missing.empty() ? "" : ", ", name);
        return;
      }
      *original = target;
      internal::hooks.emplace_back(original, replacement, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE);
    };
    add(rome2 ? internal::UNIT_DISTANCES_ROME2 : internal::UNIT_DISTANCES,
        reinterpret_cast<void**>(&internal::original_unit_distances), reinterpret_cast<void*>(&internal::OnUnitDistances),
        "units");
    if (uint8_t* call = tw::engine_tweaks::FindPattern(
            sections.code, rome2 ? internal::IMPOSTER_DISTANCE_CALL_ROME2 : internal::IMPOSTER_DISTANCE_CALL);
        call != nullptr) {
      internal::imposter_distance_return = call + internal::IMPOSTER_DISTANCE_CALL_RETURN;
      internal::caller_soldier = (rome2                    ? internal::CALLER_SOLDIER_ROME2
                                  : game == Game::THRONES ? internal::CALLER_SOLDIER_THRONES
                                                          : internal::CALLER_SOLDIER);
      internal::soldier_flags = (rome2 ? internal::SOLDIER_FLAGS_ROME2 : internal::SOLDIER_FLAGS);
      add(rome2 ? internal::IMPOSTER_DISTANCE_ROME2 : internal::IMPOSTER_DISTANCE,
          reinterpret_cast<void**>(&internal::original_imposter_distance),
          reinterpret_cast<void*>(&internal::OnImposterDistance), "dead units");
    } else {
      missing += std::format("{}dead units", missing.empty() ? "" : ", ");
    }
    if (!rome2) {
      add(internal::DRAW_TREE, reinterpret_cast<void**>(&internal::original_draw_tree),
          reinterpret_cast<void*>(&internal::OnDrawTree), "trees");
      add(internal::DRAW_SCRUB, reinterpret_cast<void**>(&internal::original_draw_scrub),
          reinterpret_cast<void*>(&internal::OnDrawScrub), "grass");
      add(internal::SCRUB_UPDATE, reinterpret_cast<void**>(&internal::original_scrub_update),
          reinterpret_cast<void*>(&internal::OnScrubUpdate), "grass cells");
      if (uint8_t* call = tw::engine_tweaks::FindPattern(sections.code, internal::FIRST_PACK_CALL); call != nullptr) {
        internal::first_pack_return = call + internal::FIRST_PACK_CALL_RETURN;
        add(internal::PACK_ANIMATIONS, reinterpret_cast<void**>(&internal::original_pack_animations),
            reinterpret_cast<void*>(&internal::OnPackAnimations), "big battles");
      } else {
        missing += std::format("{}big battles", missing.empty() ? "" : ", ");
      }
    }
    std::string result = (missing.empty() ? (rome2 ? "units and dead units hooked" : "units, dead units, big battles, trees and grass hooked")
                                           : std::format("not found: {}", missing));
    if (!internal::hooks.empty()) {
      try {
        internal::hook_lifetime.Install(internal::hooks);
        internal::hooks_active.store(true, std::memory_order_relaxed);
      } catch (const std::exception& error) {
        internal::hooks.clear();
        result = std::format("hooks failed: {}", error.what());
      }
    }
    status->label += "; " + result;
    renodx::utils::log::i("[Total War] Graphics: ", result);
  }
}

// Addon detach: the hooks removed.
inline void Uninstall() {
  internal::hooks_active.store(false, std::memory_order_relaxed);
  if (internal::hooks.empty()) return;
  try {
    internal::hook_lifetime.Uninstall(&internal::hooks);
  } catch (const std::exception& error) {
    renodx::utils::log::e("[Total War] Graphics hooks could not be removed; addon retained until process exit: ", error.what());
  }
}

// Shadow Map Size: the shadow setup (+0x17665e0; Thrones of Britannia has the same code) picks the sun's shadow map
// size by the Shadows option, Max: 2048 (4 cascades), High: 2048 (3), Medium: 1024 (2), each as two immediates, the
// width and the height (mov dword ptr [esp + 0x10 / 0x14], size), copied to +0x19c4 / +0x19c8 and made into the
// textures. All six are the game's values x the slider; it reads them when it builds its shadows (at start and when
// the Shadows option changes).
namespace shadow_map {

namespace internal {
// From Max's width to Medium's height.
constexpr std::string_view PATTERN =
    "C7 44 24 10 ?? ?? ?? ?? 8B 44 24 10 89 87 ?? ?? 00 00 C7 44 24 14 ?? ?? ?? ?? 8B 44 24 14 89 87 ?? ?? 00 00 "
    "0F 94 C0 0F B6 C0 50 C7 87 94 00 00 00 00 04 00 00 C7 87 98 00 00 00 04 00 00 00 C7 87 84 00 00 00 02 00 00 00 "
    "E8 ?? ?? ?? ?? 83 C4 04 EB ?? C7 44 24 10 ?? ?? ?? ?? C7 44 24 14 ?? ?? ?? ?? C7 87 98 00 00 00 03 00 00 00 "
    "C7 87 84 00 00 00 01 00 00 00 EB ?? C7 44 24 10 ?? ?? ?? ?? C7 44 24 14 ?? ?? ?? ??";
constexpr std::array<size_t, 6> SIZE_OFFSETS = {4, 22, 87, 95, 125, 133};  // Max, High, Medium: width, height
constexpr std::array<float, 6> GAME_SIZES = {2048.f, 2048.f, 2048.f, 2048.f, 1024.f, 1024.f};
constexpr size_t CODE_SIZE = 137;
static uint8_t* code = nullptr;
static float written = 1.f;  // the scale last written
}  // namespace internal

// The game's sizes x `scale` (every present: the code is written only when it changes).
inline void Set(float scale) {
  if (internal::code == nullptr || scale == internal::written) return;
  internal::written = scale;
  DWORD protection;
  if (VirtualProtect(internal::code, internal::CODE_SIZE, PAGE_EXECUTE_READWRITE, &protection) == FALSE) return;
  for (size_t index = 0; index < internal::SIZE_OFFSETS.size(); ++index) {
    const float size = internal::GAME_SIZES[index] * scale;
    std::memcpy(internal::code + internal::SIZE_OFFSETS[index], &size, sizeof(size));
  }
  VirtualProtect(internal::code, internal::CODE_SIZE, protection, &protection);
  FlushInstructionCache(GetCurrentProcess(), internal::code, internal::CODE_SIZE);
}

// At the game's device creation, before it builds its shadows: finds the code and sets `scale`.
inline void Install(float scale) {
  if (internal::code == nullptr) {
    HMODULE module = GetModuleHandleW(L"empire.retail.dll");
    if (module == nullptr) return;
    internal::code = tw::engine_tweaks::FindPattern(tw::engine_tweaks::ModuleSections(module).code, internal::PATTERN);
    // Sizes another mod changed already: left alone (scaled again they would be too big).
    for (size_t index = 0; internal::code != nullptr && index < internal::SIZE_OFFSETS.size(); ++index) {
      float size;
      std::memcpy(&size, internal::code + internal::SIZE_OFFSETS[index], sizeof(size));
      if (size != internal::GAME_SIZES[index]) internal::code = nullptr;
    }
    renodx::utils::log::i("[Total War] Graphics: shadow map size ",
                          internal::code != nullptr ? "found" : "not found, or changed by another mod");
  }
  Set(scale);
}

// Addon detach: the game's sizes back.
inline void Uninstall() {
  Set(1.f);
}

}  // namespace shadow_map

}  // namespace twattila::graphics_hooks
