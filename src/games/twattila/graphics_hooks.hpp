/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <intrin.h>
#include <windows.h>
#include <xmmintrin.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>
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
// Its code from the entry to the no-LOD return (+0xba; the global the model index is first compared with wildcarded; the
// same bytes in Thrones of Britannia): the model, the camera (manager +0x34's view by vtable +0x28, GET_VIEW_CODE, and the
// view's position by its vtable +0x3c, GET_POSITION_CODE, into DrawScrub's frame), the key ((dy^2 + dx^2) + dz^2 +
// (+0xc34 + the bias)), then the first enabled LOD (+0x44) farther than the key (+0x38 > key). A record past every enabled
// LOD returns there, an index equal to that global or a model without LODs at +0x376 (DRAW_SCRUB_RETURN): reads, writes
// to its own frame and two getters only. OnDrawScrub then leaves the call out (ATN's G1 battle: ~106k of 118k a frame).
constexpr std::string_view DRAW_SCRUB_NO_LOD =
    "83 EC 4C 8B 44 24 50 53 55 8B E9 56 57 89 6C 24 10 3B 05 ?? ?? ?? ?? 0F 84 59 03 00 00 3B 45 1C 73 08 8B 4D 20 8B "
    "1C 81 EB 02 33 DB 8B 4D 34 8B 01 FF 50 28 8B 08 8B 51 3C 8D 4C 24 20 51 8B C8 FF D2 8B 7C 24 64 33 D2 8B 73 28 F3 "
    "0F 10 4F 0C F3 0F 10 57 1C F3 0F 10 47 2C F3 0F 5C 50 04 F3 0F 5C 08 F3 0F 5C 40 08 8B 44 24 6C F3 0F 59 D2 F3 0F "
    "59 C9 F3 0F 59 C0 F3 0F 58 D1 F3 0F 58 D0 F3 0F 10 85 34 0C 00 00 F3 0F 58 40 10 F3 0F 58 D0 85 F6 0F 84 DD 02 00 "
    "00 8B 4B 2C 0F 1F 40 00 8B 19 F3 0F 10 43 38 0F 2F C2 76 06 80 7B 44 00 75 12 42 83 C1 04 3B D6 72 E6 5F 5E 5D 5B "
    "83 C4 4C C2 14 00";
constexpr std::string_view DRAW_SCRUB = DRAW_SCRUB_NO_LOD.substr(0, 119);  // its first 40 bytes find it
constexpr std::string_view DRAW_SCRUB_RETURN = "5F 5E 5D 5B 83 C4 4C C2 14 00";
constexpr size_t DRAW_SCRUB_RETURN_OFFSET = 0x376;
constexpr size_t MANAGER_SCENE = 0x34;
constexpr size_t GET_VIEW_SLOT = 0x28 / 4;  // the scene's vtable
constexpr std::array<uint8_t, 4> GET_VIEW_CODE = {0x8D, 0x41, 0x48, 0xC3};  // lea eax, [ecx + 0x48]; ret
constexpr size_t SCENE_VIEW = 0x48;
constexpr size_t GET_POSITION_SLOT = 0x3c / 4;  // the view's vtable
// mov eax, [esp + 4]; the view's +0x8, +0xc, +0x10 to [eax], [eax + 4], [eax + 8]; ret 4
constexpr std::array<uint8_t, 24> GET_POSITION_CODE = {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x51, 0x08, 0x89, 0x10, 0x8B, 0x51, 0x0C,
                                                       0x8B, 0x49, 0x10, 0x89, 0x50, 0x04, 0x89, 0x48, 0x08, 0xC2, 0x04, 0x00};
constexpr size_t VIEW_POSITION = 0x8;
constexpr size_t DRAW_PARAMS_BIAS = 0x10;  // DrawScrub's 4th argument
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
// void __thiscall ScrubCell(Scrub* scrub, const Cell* cell, const float* camera, CellBlock entry, float radius_squared,
// uint32_t) (+0x1897a30, ret 0x40): the cell loop, one call a cell of the quadtree query (ScrubUpdate's TBB body +0x17db200:
// the cell list entry's +0x8 to +0x37 by value, then float(+0x80 squared as an int)). A record (+0x24, 18 bytes each, +0x20
// of them; its +0x10 byte its slot in the scrub's models) goes on to the grid, the bias, the transform and DrawScrub only
// while its distance squared <= radius_squared ([esp + 0x184]; with scrub_node_always_render, +0xc0130, every record).
// The same bytes in Thrones of Britannia; the next record (+0x42c) and the return (+0x44c) read at install.
constexpr std::string_view SCRUB_CELL =
    "81 EC 40 01 00 00 56 57 8B F9 8D 4C 24 08 89 BC 24 3C 01 00 00 E8 ?? ?? ?? ?? 8B 8C 24 4C 01 00 00 8B 41 20 89 84 24 "
    "28 01 00 00 85 C0 0F 84 ?? ?? ?? ?? 33 C0 89 84 24 2C 01 00 00 0F 1F 40 00 8B 71 24 B9 ?? ?? ?? ?? 03 F0 8B 84 24 50 "
    "01 00 00 89 B4 24 30 01 00 00 F3 0F 10 16 F3 0F 5C 10 F3 0F 10 46 04 F3 0F 5C 40 04 F3 0F 10 4E 08 F3 0F 5C 48 08 F3 "
    "0F 59 D2 F3 0F 59 C0 F3 0F 59 C9 F3 0F 58 D0 F3 0F 58 D1 F3 0F 11 94 24 40 01 00 00 E8 ?? ?? ?? ?? 84 C0 75 4B F3 0F "
    "10 84 24 84 01 00 00 0F 2F 84 24 40 01 00 00 0F 82";
constexpr std::string_view SCRUB_CELL_NEXT = "83 C0 12 83 AC 24 28 01 00 00 01";  // add eax, 18; sub [esp + 0x128], 1
constexpr size_t SCRUB_CELL_NEXT_OFFSET = 0x42c;
constexpr std::string_view SCRUB_CELL_RETURN = "C2 40 00";
constexpr size_t SCRUB_CELL_RETURN_OFFSET = 0x44c;
constexpr size_t CELL_COUNT = 0x20;
constexpr size_t CELL_RECORDS = 0x24;
constexpr size_t RECORD_SIZE = 18;
constexpr size_t RECORD_SLOT = 0x10;
// The cell list entry's +0x8 to +0x37, passed on as the game pushed it.
struct CellEntry {
  std::array<uint32_t, 12> values = {};
};

// __thiscall through __fastcall: `this` in ecx, edx unused, the rest on the stack (callee pops).
using UnitDistances = float*(__fastcall*)(void* display, void* unused_edx, float* out, uint32_t quality, uint32_t second,
                                          uint32_t third, uint32_t fourth);
using ImposterDistance = float(__fastcall*)(void* display, void* unused_edx, uint32_t size, float random);
using PackAnimations = bool(__fastcall*)(void* pages, void* unused_edx, uint32_t first, uint32_t second, void* soldiers,
                                         uint32_t* next, uint32_t end);
using DrawTree = void*(__fastcall*)(uint8_t* manager, void* unused_edx, uint32_t model, const float* position,
                                    uint32_t third, uint32_t fourth, uint32_t fifth, uint32_t sixth, uint32_t seventh);
using DrawScrub = void(__fastcall*)(uint8_t* manager, void* unused_edx, uint32_t model, const float* transform,
                                    uint32_t third, const uint8_t* params, uint32_t fifth);
using ScrubUpdate = void(__fastcall*)(uint8_t* scrub, void* unused_edx);
using ScrubCell = void(__fastcall*)(uint8_t* scrub, void* unused_edx, const uint8_t* cell, const float* camera, CellEntry entry,
                                    float radius_squared, uint32_t last);

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
  // Per slot of the scrub's models, the cell radius its records need (as the cells': ceil(sqrt(the farthest enabled LOD -
  // min(+0xc34, 0))) + 1 m; 0: no enabled LOD; INT32_MAX: no model, or not below written_cells). slot_count 0: none known.
  std::array<int32_t, MAX_SCRUB_MODELS> slot_radius = {};
  uint32_t slot_count = 0;
};

static UnitDistances original_unit_distances = nullptr;
static DrawTree original_draw_tree = nullptr;
static DrawScrub original_draw_scrub = nullptr;
static ScrubUpdate original_scrub_update = nullptr;
static ScrubCell original_scrub_cell = nullptr;
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
static bool draw_scrub_read = false;  // DrawScrub's code is DRAW_SCRUB_NO_LOD and DRAW_SCRUB_RETURN (set at install)
// The camera getters DrawScrub calls, as OnScrubUpdate found them every frame with the code read (null: not found): each
// pointer stands alone, so two separate atomics.
static std::atomic<const void*> verified_get_view = nullptr;
static std::atomic<const void*> verified_get_position = nullptr;
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

// Grass Dump (tools/tw/grass_replay: phase 0 of grass placed on the GPU): one frame of the scrub, its input and what the
// game builds from it, into tw_grass_dump_<n>.bin next to the exe. Asked, the next scrub update is timed alone (DUMP_TIMING);
// the one after it (DUMP_SCENE) first writes the scene (the scrub's and its manager's fields, the code the cell loop and
// DrawScrub run with the values they read through absolute addresses, the angle table, the byte grid, the scrub models with
// their LODs and parts, every quadtree node with its records), then every cell loop and DrawScrub call while it runs
// (DUMP_CALLS); the grass draws' instance rows follow for two frames (DUMP_ROWS, DUMP_ROWS_NEXT: a frame's grass may be
// drawn after the next update began). The file: "TWGD", its version, then chunks (a 4-byte tag, a 4-byte size, the fields;
// pointers 8 bytes) that grass_replay.py reads.
constexpr int DUMP_IDLE = 0;
constexpr int DUMP_TIMING = 1;
constexpr int DUMP_SCENE = 2;
constexpr int DUMP_CALLS = 3;
constexpr int DUMP_ROWS = 4;
constexpr int DUMP_ROWS_NEXT = 5;
constexpr uint32_t DUMP_VERSION = 1;
constexpr uint32_t DUMP_PRESENTS = 240;  // presents without the next scrub update: the dump ends (or is given up)
constexpr size_t DUMP_SCRUB_BYTES = 0x120;
constexpr size_t DUMP_MANAGER_BYTES = 0xc40;
constexpr size_t DUMP_MODEL_BYTES = 0x40;
constexpr size_t DUMP_LOD_BYTES = 0x48;
constexpr size_t DUMP_PART_BYTES = 0x100;
constexpr size_t DUMP_PARAMS_BYTES = 0x38;    // the cell loop's draw parameters (+0x1b8f60 sets 0x35 bytes)
constexpr size_t DUMP_TABLE_BYTES = 256 * 8;  // the (cos, sin) table: an angle byte's 256 entries
constexpr uint32_t DUMP_MAX_NODES = 1u << 18;
constexpr uint32_t DUMP_MAX_PARTS = 64;
constexpr uint64_t DUMP_MAX_GRID = 64u << 20;
constexpr uint32_t DUMP_MAX_CELL_RECORDS = 1u << 22;
constexpr size_t CELL_LOOP_BYTES = SCRUB_CELL_RETURN_OFFSET + 3;
constexpr size_t DRAW_SCRUB_BYTES = 0x380;  // to its last return (+0x37d)
// ScrubUpdate's quadtree (+0x88; its root +0x14, as the query +0x1898800 takes it), a node 0x3c bytes: bounds +0x4 to
// +0x1b, the records' count +0x20 and list +0x24 (CELL_COUNT, CELL_RECORDS: a node is the cell loop's cell), its 4
// children from +0x2c (none: 0). The byte grid +0xa0 (its row shift +0xa8, offset +0xb0, unsigned clamp +0xb4) and the
// (cos, sin) table +0x118, as the cell loop reads them.
constexpr size_t SCRUB_QUADTREE = 0x88;
constexpr size_t QUADTREE_ROOT = 0x14;
constexpr size_t NODE_BYTES = 0x3c;
constexpr size_t NODE_CHILDREN = 0x2c;
constexpr size_t SCRUB_GRID = 0xa0;
constexpr size_t SCRUB_GRID_SHIFT = 0xa8;
constexpr size_t SCRUB_GRID_CLAMP = 0xb4;
constexpr size_t SCRUB_TABLE = 0x118;
// A scrub model's LOD (DrawScrub): its parts' count +0x20 and list +0x24 (each part's draw at its vtable +0x10c), its
// attachments' count +0x2c and list +0x30.
constexpr size_t LOD_PART_COUNT = 0x20;
constexpr size_t LOD_PARTS = 0x24;
constexpr size_t LOD_ATTACHMENT_COUNT = 0x2c;
constexpr size_t LOD_ATTACHMENTS = 0x30;
constexpr size_t PART_DRAW_SLOT = 0x10c / 4;
// The values the cell loop and DrawScrub read through absolute addresses in their code: the instruction's bytes before the
// address, where it sits in the code, the value's offset from the address (a tweak's value at +0x48, +0xc0100 / +0xc0130).
struct CodeValue {
  size_t code_offset = 0;
  std::string_view opcode;
  size_t value_offset = 0;
  bool draw_scrub = false;  // DrawScrub's code (else the cell loop's)
};
constexpr std::array<CodeValue, 7> CODE_VALUES = {{
    {.code_offset = 0x43, .opcode = "\xB9", .value_offset = 0x48},    // scrub_node_always_render (a byte)
    {.code_offset = 0xcb, .opcode = "\xF3\x0F\x59\x0D"},              // the bias's 1 / 65025
    {.code_offset = 0xe5, .opcode = "\xB9", .value_offset = 0x48},    // grass_burn_override
    {.code_offset = 0xff, .opcode = "\x0F\x2E\x05"},                  // -1: no burn override
    {.code_offset = 0x117, .opcode = "\xF3\x0F\x59\x05"},             // the burn byte's 1 / 255
    {.code_offset = 0x214, .opcode = "\xF3\x0F\x10\x2D"},             // the sign mask
    {.code_offset = 0x11, .opcode = "\x3B\x05", .draw_scrub = true},  // the model index that draws nothing
}};

struct GrassDump {
  std::atomic<int> state = DUMP_IDLE;
  std::mutex mutex;
  std::ofstream file;
  std::filesystem::path path;
  uint32_t sequence = 0;           // this run's dumps (each its own file: a second shows what changed in the battle)
  const uint8_t* scrub = nullptr;  // the dumped scrub (its cell loop calls only)
  int64_t clean_ticks = 0;         // the timed update's (QueryPerformanceCounter)
  int64_t dump_ticks = 0;          // the dumped update's, its calls written while it ran
  uint32_t presents = 0;           // since the state last changed
  uint32_t nodes = 0;
  uint64_t records = 0;
  uint32_t cells = 0;
  uint32_t draws = 0;
  uint32_t row_draws = 0;
  bool truncated = false;                                 // a cap reached (nodes, a cell's records, the grid)
  std::vector<std::pair<uintptr_t, uint32_t>> rows_read;  // this window's instance buffers and first bytes read
};
static GrassDump grass_dump;
// The cell loop's and DrawScrub's code as found, before their hooks (the dump's copy of the bytes they run).
static const uint8_t* cell_loop_code = nullptr;
static const uint8_t* draw_scrub_code = nullptr;

// Whether `size` bytes from `at` can be read (committed, neither guard nor no-access pages): the dump follows the game's
// pointers by the layouts above, read from the code, not proven for every battle.
static bool Readable(const void* at, size_t size) {
  if (at == nullptr) return false;
  auto address = reinterpret_cast<uintptr_t>(at);
  const uintptr_t end = address + size;
  if (end < address) return false;
  while (address < end) {
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
    address = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
  }
  return true;
}

// A dump chunk: its tag, then its fields in order (the size is set when it is written).
struct DumpChunk {
  std::vector<uint8_t> bytes;

  explicit DumpChunk(std::string_view tag) : bytes(8) { std::memcpy(bytes.data(), tag.data(), 4); }

  template <typename T>
  DumpChunk& Add(const T& value) {
    const auto* at = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), at, at + sizeof(T));
    return *this;
  }

  DumpChunk& Pointer(const void* value) { return Add<uint64_t>(reinterpret_cast<uintptr_t>(value)); }

  // `size` bytes from `at` (zeros for null): the game's own arguments, readable by contract.
  DumpChunk& Raw(const void* at, size_t size) {
    const size_t start = bytes.size();
    bytes.resize(start + size);
    if (at != nullptr) std::memcpy(bytes.data() + start, at, size);
    return *this;
  }

  // A 4-byte flag (1: readable), then `size` bytes from `at` (zeros when not readable).
  DumpChunk& Copy(const void* at, size_t size) {
    const bool readable = Readable(at, size);
    Add<uint32_t>(readable ? 1 : 0);
    const size_t start = bytes.size();
    bytes.resize(start + size);
    if (readable) std::memcpy(bytes.data() + start, at, size);
    return *this;
  }

  // Its fields, then `tail_size` bytes from `tail` (game memory written as it is).
  void Write(std::ofstream* file, const void* tail = nullptr, size_t tail_size = 0) {
    const auto size = static_cast<uint32_t>(bytes.size() - 8 + tail_size);
    std::memcpy(bytes.data() + 4, &size, sizeof(size));
    file->write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (tail_size != 0) file->write(static_cast<const char*>(tail), static_cast<std::streamsize>(tail_size));
  }
};

// The calling thread's x87 control word (_control87's form; 0 on x64) and MXCSR: how the cell loop's floats round.
static std::array<uint32_t, 2> FloatControl() {
  unsigned int x87 = 0;
#if defined(_M_IX86)
  __control87_2(0, 0, &x87, nullptr);
#endif
  return {x87, _mm_getcsr()};
}

// The dump's last chunks and the file closed; its lock held.
static void FinishGrassDump(std::string_view reason) {
  auto& dump = grass_dump;
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  DumpChunk time("TIME");
  time.Add<int64_t>(frequency.QuadPart).Add(dump.clean_ticks).Add(dump.dump_ticks).Add(dump.nodes).Add(dump.records)
      .Add(dump.cells).Add(dump.draws).Add(dump.row_draws).Add<uint32_t>(dump.truncated ? 1 : 0);
  time.Write(&dump.file);
  DumpChunk("END_").Write(&dump.file);
  dump.file.close();
  renodx::utils::log::i("[Total War] Grass dump: ", dump.nodes, " nodes, ", dump.records, " records, ", dump.cells,
                        " cells visited, ", dump.draws, " DrawScrub calls, ", dump.row_draws, " grass draws' rows",
                        (dump.truncated ? " (a cap reached)" : ""), ", update ",
                        static_cast<double>(dump.clean_ticks) * 1000.0 / static_cast<double>(frequency.QuadPart),
                        " ms; ", reason, ": ", dump.path.string());
  dump.state = DUMP_IDLE;
}

// DUMP_SCENE's update, before the game's: the file opened and the scene written (GrassDump), then DUMP_CALLS.
static void StartGrassDump(const uint8_t* scrub) {
  auto& dump = grass_dump;
  const std::unique_lock lock(dump.mutex);
  std::wstring exe(MAX_PATH, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  dump.path = std::filesystem::path(exe).parent_path() / std::format("tw_grass_dump_{}.bin", ++dump.sequence);
  dump.file.open(dump.path, std::ios::binary | std::ios::trunc);
  if (!dump.file) {
    renodx::utils::log::e("[Total War] Grass dump: ", dump.path.string(), " could not be written");
    dump.state = DUMP_IDLE;
    return;
  }
  dump.file.write("TWGD", 4);
  dump.file.write(reinterpret_cast<const char*>(&DUMP_VERSION), sizeof(DUMP_VERSION));
  dump.scrub = scrub;
  dump.nodes = 0;
  dump.records = 0;
  dump.cells = 0;
  dump.draws = 0;
  dump.row_draws = 0;
  dump.truncated = false;
  const auto pointer_at = [](const uint8_t* at) {
    const uint8_t* value = nullptr;
    if (Readable(at, sizeof(value))) std::memcpy(&value, at, sizeof(value));
    return value;
  };
  const auto u32_at = [](const uint8_t* at) {
    uint32_t value = 0;
    if (Readable(at, sizeof(value))) std::memcpy(&value, at, sizeof(value));
    return value;
  };
  const auto& known = scrub_values;
  const uint8_t* manager = pointer_at(scrub + SCRUB_MANAGER);
  // DrawScrub's camera as OnDrawScrub takes it (the scene's view, its getters' code read): NaN when not known.
  std::array<float, 3> camera = {NAN, NAN, NAN};
  if (verified_get_view.load(std::memory_order_relaxed) != nullptr && manager != nullptr) {
    const uint8_t* scene = pointer_at(manager + MANAGER_SCENE);
    if (scene != nullptr && Readable(scene + SCENE_VIEW + VIEW_POSITION, sizeof(camera))) {
      std::memcpy(camera.data(), scene + SCENE_VIEW + VIEW_POSITION, sizeof(camera));
    }
  }
  const auto control = FloatControl();
  DumpChunk head("HEAD");
  head.Pointer(GetModuleHandleW(L"empire.retail.dll")).Pointer(scrub).Pointer(manager).Add(known.game_cells)
      .Add(known.game_bias).Add(known.written_cells).Add(known.written_bias)
      .Add(grass_scale.load(std::memory_order_relaxed)).Add(grass_density.load(std::memory_order_relaxed))
      .Add(control[0]).Add(control[1]).Add(camera).Add<uint32_t>(draw_scrub_read ? 1 : 0).Add(known.slot_count);
  for (uint32_t slot = 0; slot < known.slot_count; ++slot) head.Add(known.slot_radius[slot]);
  head.Write(&dump.file);
  DumpChunk("SCRB").Copy(scrub, DUMP_SCRUB_BYTES).Write(&dump.file);
  DumpChunk("MNGR").Pointer(manager).Copy(manager, DUMP_MANAGER_BYTES).Write(&dump.file);
  DumpChunk("CODE").Add<uint32_t>(0).Pointer(cell_loop_code).Copy(cell_loop_code, CELL_LOOP_BYTES).Write(&dump.file);
  DumpChunk("CODE").Add<uint32_t>(1).Pointer(draw_scrub_code).Copy(draw_scrub_code, DRAW_SCRUB_BYTES).Write(&dump.file);
  if constexpr (sizeof(void*) == 4) {
    for (size_t index = 0; index < CODE_VALUES.size(); ++index) {
      const auto& value = CODE_VALUES[index];
      const uint8_t* code = (value.draw_scrub ? draw_scrub_code : cell_loop_code);
      const uint8_t* address = nullptr;
      if (code != nullptr && Readable(code + value.code_offset, value.opcode.size() + 4)
          && std::memcmp(code + value.code_offset, value.opcode.data(), value.opcode.size()) == 0) {
        address = pointer_at(code + value.code_offset + value.opcode.size());
      }
      const bool readable = (address != nullptr && Readable(address + value.value_offset, 4));
      DumpChunk("CVAL").Add(static_cast<uint32_t>(index)).Pointer(address).Add<uint32_t>(readable ? 1 : 0)
          .Add(readable ? u32_at(address + value.value_offset) : 0u).Write(&dump.file);
    }
  }
  const uint8_t* table = pointer_at(scrub + SCRUB_TABLE);
  DumpChunk("TABL").Pointer(table).Copy(table, DUMP_TABLE_BYTES).Write(&dump.file);
  // The grid's bytes the cell loop can index: (clamp << shift) + clamp at most, written as the game holds them.
  const uint8_t* grid = pointer_at(scrub + SCRUB_GRID);
  const uint32_t clamp = u32_at(scrub + SCRUB_GRID_CLAMP);
  const uint64_t grid_size = (static_cast<uint64_t>(clamp) << (u32_at(scrub + SCRUB_GRID_SHIFT) & 31)) + clamp + 1;
  const bool grid_written = (grid_size <= DUMP_MAX_GRID && Readable(grid, static_cast<size_t>(grid_size)));
  dump.truncated |= (grid_size > DUMP_MAX_GRID);
  DumpChunk("GRID").Pointer(grid).Add(grid_size).Add<uint32_t>(grid_written ? 1 : 0)
      .Write(&dump.file, grid, grid_written ? static_cast<size_t>(grid_size) : 0);
  // The scrub models (a slot's model as DrawScrub finds it: the manager's list +0x20, its count +0x1c), their LODs and parts,
  // each model once.
  const uint8_t* slots = pointer_at(scrub + SCRUB_MODELS);
  const uint8_t* models = (manager != nullptr ? pointer_at(manager + 0x20) : nullptr);
  const uint32_t model_count = (manager != nullptr ? u32_at(manager + 0x1c) : 0);
  std::vector<const uint8_t*> written_models;
  for (uint32_t slot = 0; slot < known.slot_count; ++slot) {
    const uint32_t index = (slots != nullptr ? u32_at(slots + (slot * 4)) : UINT32_MAX);
    const uint8_t* model = (models != nullptr && index < model_count ? pointer_at(models + (index * 4)) : nullptr);
    DumpChunk("MODL").Add(slot).Add(index).Pointer(model).Copy(model, DUMP_MODEL_BYTES).Write(&dump.file);
    if (model == nullptr || !Readable(model, DUMP_MODEL_BYTES) || std::ranges::find(written_models, model) != written_models.end()) continue;
    written_models.push_back(model);
    const uint32_t lod_count = std::min<uint32_t>(u32_at(model + 0x28), MAX_MODEL_LODS);
    const uint8_t* lods = pointer_at(model + 0x2c);
    for (uint32_t lod_index = 0; lod_index < lod_count; ++lod_index) {
      const uint8_t* lod = pointer_at(lods + (lod_index * 4));
      DumpChunk("LOD_").Pointer(model).Add(lod_index).Pointer(lod).Copy(lod, DUMP_LOD_BYTES).Write(&dump.file);
      if (!Readable(lod, DUMP_LOD_BYTES)) continue;
      const uint32_t part_count = std::min(u32_at(lod + LOD_PART_COUNT), DUMP_MAX_PARTS);
      const uint8_t* parts = pointer_at(lod + LOD_PARTS);
      for (uint32_t part_index = 0; part_index < part_count; ++part_index) {
        const uint8_t* part = pointer_at(parts + (part_index * 4));
        const uint8_t* vtable = (part != nullptr ? pointer_at(part) : nullptr);
        const uint8_t* draw = (vtable != nullptr ? pointer_at(vtable + (PART_DRAW_SLOT * 4)) : nullptr);
        DumpChunk("PART").Pointer(model).Add(lod_index).Add(part_index).Pointer(part).Pointer(vtable).Pointer(draw)
            .Copy(part, DUMP_PART_BYTES).Write(&dump.file);
      }
      if (const uint32_t attachments = std::min(u32_at(lod + LOD_ATTACHMENT_COUNT), DUMP_MAX_PARTS); attachments != 0) {
        DumpChunk("ATCH").Pointer(model).Add(lod_index).Add(attachments)
            .Copy(pointer_at(lod + LOD_ATTACHMENTS), attachments * 4).Write(&dump.file);
      }
    }
  }
  // Every quadtree node from the root, its records as the game holds them.
  const uint8_t* quadtree = pointer_at(scrub + SCRUB_QUADTREE);
  std::vector<const uint8_t*> pending = {quadtree != nullptr ? pointer_at(quadtree + QUADTREE_ROOT) : nullptr};
  std::unordered_set<const uint8_t*> seen;
  while (!pending.empty()) {
    const uint8_t* node = pending.back();
    pending.pop_back();
    if (node == nullptr || !seen.insert(node).second) continue;
    if (dump.nodes == DUMP_MAX_NODES) {
      dump.truncated = true;
      break;
    }
    ++dump.nodes;
    uint32_t count = u32_at(node + CELL_COUNT);
    const uint8_t* records = pointer_at(node + CELL_RECORDS);
    if (count > DUMP_MAX_CELL_RECORDS || !Readable(records, size_t{count} * RECORD_SIZE)) {
      dump.truncated |= (count != 0);
      count = 0;
    }
    dump.records += count;
    DumpChunk("NODE").Pointer(node).Copy(node, NODE_BYTES).Pointer(records).Add(count)
        .Write(&dump.file, records, size_t{count} * RECORD_SIZE);
    if (!Readable(node, NODE_BYTES)) continue;
    for (size_t child = 0; child < 4; ++child) pending.push_back(pointer_at(node + NODE_CHILDREN + (child * 4)));
  }
  dump.presents = 0;
  dump.state = DUMP_CALLS;
}

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
static void TraceScrub(uint8_t* manager, uint32_t index, const float* transform, const uint8_t* params) {
  using GetView = uint8_t*(__fastcall*)(uint8_t* self, void* unused_edx);
  using GetPosition = const float*(__fastcall*)(uint8_t* self, void* unused_edx, float* out);
  uint8_t* provider;
  std::memcpy(&provider, manager + MANAGER_SCENE, sizeof(provider));
  if (provider == nullptr || transform == nullptr || params == nullptr) return;
  void** vtable;
  std::memcpy(&vtable, provider, sizeof(vtable));
  uint8_t* view = reinterpret_cast<GetView>(vtable[GET_VIEW_SLOT])(provider, nullptr);
  if (view == nullptr) return;
  std::memcpy(&vtable, view, sizeof(vtable));
  std::array<float, 16> buffer = {};
  const float* camera = reinterpret_cast<GetPosition>(vtable[GET_POSITION_SLOT])(view, nullptr, buffer.data());
  if (camera == nullptr) return;
  const float x = transform[3] - camera[0];
  const float y = transform[7] - camera[1];
  const float z = transform[11] - camera[2];
  float extra;
  float bias;
  std::memcpy(&extra, manager + MANAGER_LOD_OFFSET, sizeof(extra));
  std::memcpy(&bias, params + DRAW_PARAMS_BIAS, sizeof(bias));
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
                                   uint32_t third, const uint8_t* params, uint32_t fifth) {
  if (!hooks_active.load(std::memory_order_relaxed)) return original_draw_scrub(manager, unused_edx, model, transform, third, params, fifth);
  const float scale = grass_scale.load(std::memory_order_relaxed);
  const uint8_t* lods = ScaleModelLods(manager, model, scale * scale);
  if (grass_trace.state.load(std::memory_order_relaxed) == 2) TraceScrub(manager, model, transform, params);
  // A record past every enabled LOD of its model: DrawScrub would draw nothing and change nothing (DRAW_SCRUB_NO_LOD), so
  // it isn't called. Its key as DrawScrub's: the camera as the getters it would call return it (the same functions as
  // OnScrubUpdate's, their code read), the floats in its order. `picked`: the LOD DrawScrub takes (-1: none, -2: not known).
  int32_t picked = -2;
  std::array<float, 3> camera = {NAN, NAN, NAN};
  if (const void* get_view = verified_get_view.load(std::memory_order_relaxed);
      get_view != nullptr && lods != nullptr && params != nullptr) {
    uint8_t* scene;
    void** scene_vtable;
    std::memcpy(&scene, manager + MANAGER_SCENE, sizeof(scene));
    std::memcpy(&scene_vtable, scene, sizeof(scene_vtable));
    if (scene_vtable[GET_VIEW_SLOT] == get_view) {
      void** view_vtable;
      std::memcpy(&view_vtable, scene + SCENE_VIEW, sizeof(view_vtable));
      if (view_vtable[GET_POSITION_SLOT] == verified_get_position.load(std::memory_order_relaxed)) {
        float lod_offset;
        float bias;
        std::memcpy(camera.data(), scene + SCENE_VIEW + VIEW_POSITION, sizeof(camera));
        std::memcpy(&lod_offset, manager + MANAGER_LOD_OFFSET, sizeof(lod_offset));
        std::memcpy(&bias, params + DRAW_PARAMS_BIAS, sizeof(bias));
        const float dx = transform[3] - camera[0];
        const float dy = transform[7] - camera[1];
        const float dz = transform[11] - camera[2];
        float distance_squared = dy * dy + dx * dx;
        distance_squared = distance_squared + dz * dz;
        const float key = distance_squared + (lod_offset + bias);
        uint32_t count;
        uint8_t** entries;
        std::memcpy(&count, lods + 0x28, sizeof(count));
        std::memcpy(&entries, lods + 0x2c, sizeof(entries));
        picked = -1;
        for (uint32_t lod = 0; lod < count && picked < 0; ++lod) {
          float distance;
          std::memcpy(&distance, entries[lod] + 0x38, sizeof(distance));
          if (distance > key && entries[lod][0x44] != 0) picked = static_cast<int32_t>(lod);
        }
      }
    }
  }
  // Grass Dump: every call the cell loop makes, as it passes it.
  if (grass_dump.state.load(std::memory_order_relaxed) == DUMP_CALLS && transform != nullptr) {
    DumpChunk chunk("DRAW");
    chunk.Pointer(manager).Add(model).Raw(transform, 12 * sizeof(float)).Add(third).Raw(params, DUMP_PARAMS_BYTES)
        .Add(fifth).Add(camera).Add(picked).Add<uint32_t>(GetCurrentThreadId());
    const std::unique_lock lock(grass_dump.mutex);
    if (grass_dump.state == DUMP_CALLS) {
      chunk.Write(&grass_dump.file);
      ++grass_dump.draws;
    }
  }
  if (picked == -1) return;
  original_draw_scrub(manager, unused_edx, model, transform, third, params, fifth);
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
  float lod_offset = 0.f;
  if (manager != nullptr) {
    std::memcpy(&lod_offset, manager + MANAGER_LOD_OFFSET, sizeof(lod_offset));
  }
  // Per slot too (OnScrubCell): a cell of grass alone then stops at the grass's reach, though far plants elsewhere keep the
  // scrub's cells (ATN's G1 battle: 1600 m for 3 plants, 88% of the records visited past 310 m, 6 of them drawn).
  known.slot_count = (manager != nullptr && slots != nullptr) ? std::min(slot_count, MAX_SCRUB_MODELS) : 0;
  float farthest = 0.f;  // distance squared; a NaN LOD never takes a record, an infinite one leaves the cells as they are
  for (uint32_t slot = 0; slot < known.slot_count; ++slot) {
    known.slot_radius[slot] = INT32_MAX;
    const uint8_t* model = ScaleModelLods(manager, slots[slot], scale * scale);
    if (model == nullptr) continue;
    uint32_t count;
    uint8_t** entries;
    std::memcpy(&count, model + 0x28, sizeof(count));
    std::memcpy(&entries, model + 0x2c, sizeof(entries));
    float slot_farthest = -std::numeric_limits<float>::infinity();
    for (uint32_t lod = 0; lod < count; ++lod) {
      float distance;
      std::memcpy(&distance, entries[lod] + 0x38, sizeof(distance));
      if (entries[lod][0x44] == 0) continue;
      if (distance > farthest) farthest = distance;
      if (distance > slot_farthest) slot_farthest = distance;
    }
    // No enabled LOD past min(+0xc34, 0) (a key is never below it): its records draw nothing at any distance.
    if (const double reach_squared = static_cast<double>(slot_farthest) - std::min(lod_offset, 0.f); !(reach_squared > 0.0)) {
      known.slot_radius[slot] = 0;
    } else if (const double radius = std::ceil(std::sqrt(reach_squared)) + 1.0; radius < known.written_cells) {
      known.slot_radius[slot] = static_cast<int32_t>(radius);
    }
  }
  double reach = 0.0;  // metres (0: no scrub model with an enabled LOD found, the cells left as they are)
  if (farthest > 0.f) {
    reach = std::sqrt(static_cast<double>(farthest) - std::min(lod_offset, 0.f));
    if (const double radius = std::ceil(reach) + 1.0; radius < known.written_cells) {
      known.written_cells = static_cast<int32_t>(radius);
    }
  }
  std::memcpy(scrub + SCRUB_CELLS, &known.written_cells, sizeof(cells));
  std::memcpy(scrub + SCRUB_BIAS, &known.written_bias, sizeof(bias));
  // OnDrawScrub's early-out: the camera getters its manager's DrawScrub calls, their code read again every frame (other
  // code, such as another mod's: none found, every record goes to DrawScrub).
  const void* get_view = nullptr;
  const void* get_position = nullptr;
  uint8_t* scene = nullptr;
  if (draw_scrub_read && manager != nullptr) {
    std::memcpy(&scene, manager + MANAGER_SCENE, sizeof(scene));
  }
  void** scene_vtable = nullptr;
  if (scene != nullptr) {
    std::memcpy(&scene_vtable, scene, sizeof(scene_vtable));
  }
  if (scene_vtable != nullptr
      && std::memcmp(scene_vtable[GET_VIEW_SLOT], GET_VIEW_CODE.data(), GET_VIEW_CODE.size()) == 0) {
    void** view_vtable;
    std::memcpy(&view_vtable, scene + SCENE_VIEW, sizeof(view_vtable));
    if (view_vtable != nullptr
        && std::memcmp(view_vtable[GET_POSITION_SLOT], GET_POSITION_CODE.data(), GET_POSITION_CODE.size()) == 0) {
      get_view = scene_vtable[GET_VIEW_SLOT];
      get_position = view_vtable[GET_POSITION_SLOT];
    }
  }
  verified_get_view.store(get_view, std::memory_order_relaxed);
  verified_get_position.store(get_position, std::memory_order_relaxed);
  // Grass Trace: the last frame's draws out, or this frame's started.
  if (grass_trace.state.load() == 2) {
    const std::unique_lock lock(grass_trace.mutex);
    renodx::utils::log::i("[Total War] Grass trace: cells ", known.written_cells, " (game ", known.game_cells,
                          ", scrub LODs end at ", std::round(reach), " m), bias x ", known.written_bias, " (game ",
                          known.game_bias, "), DrawScrub past every LOD ", get_view != nullptr ? "skipped" : "called");
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
  // Grass Dump: the update after the ask timed alone, the next one written with its calls, the rows' windows moved on.
  const int dump_state = grass_dump.state.load(std::memory_order_relaxed);
  if (dump_state == DUMP_IDLE) return original_scrub_update(scrub, unused_edx);
  if (dump_state == DUMP_SCENE) StartGrassDump(scrub);
  if (dump_state >= DUMP_ROWS) {
    const std::unique_lock lock(grass_dump.mutex);
    if (grass_dump.state == DUMP_ROWS_NEXT) {
      FinishGrassDump("written");
    } else if (grass_dump.state == DUMP_ROWS) {
      grass_dump.state = DUMP_ROWS_NEXT;
      grass_dump.presents = 0;
      grass_dump.rows_read.clear();
    }
  }
  LARGE_INTEGER start;
  QueryPerformanceCounter(&start);
  original_scrub_update(scrub, unused_edx);
  LARGE_INTEGER end;
  QueryPerformanceCounter(&end);
  if (dump_state != DUMP_TIMING && dump_state != DUMP_SCENE) return;
  const std::unique_lock lock(grass_dump.mutex);
  if (dump_state == DUMP_TIMING && grass_dump.state == DUMP_TIMING) {
    grass_dump.clean_ticks = end.QuadPart - start.QuadPart;
    grass_dump.state = DUMP_SCENE;
    grass_dump.presents = 0;
  } else if (dump_state == DUMP_SCENE && grass_dump.state == DUMP_CALLS) {
    grass_dump.dump_ticks = end.QuadPart - start.QuadPart;
    grass_dump.state = DUMP_ROWS;
    grass_dump.presents = 0;
    grass_dump.rows_read.clear();
  }
}

// A cell's records within the radius its records' slots need (OnScrubUpdate's, the same proof as the scrub's cells: a
// record farther than its model's has no LOD): the cell loop sends the same records on, the others fail its distance
// test before the grid, the bias, the transform and the DrawScrub call. The workers' (OnScrubUpdate wrote the radii before
// the game's ScrubUpdate started them; its parallel_for returns after the last cell).
static void __fastcall OnScrubCell(uint8_t* scrub, void* unused_edx, const uint8_t* cell, const float* camera, CellEntry entry,
                                   float radius_squared, uint32_t last) {
  const auto& known = scrub_values;
  const float game_radius_squared = radius_squared;
  if (hooks_active.load(std::memory_order_relaxed) && cell != nullptr && scrub == known.scrub && known.slot_count != 0) {
    uint32_t count;
    const uint8_t* records;
    std::memcpy(&count, cell + CELL_COUNT, sizeof(count));
    std::memcpy(&records, cell + CELL_RECORDS, sizeof(records));
    int32_t radius = 0;
    for (uint32_t record = 0; record < count && radius < known.written_cells; ++record) {
      const uint8_t slot = records[(record * RECORD_SIZE) + RECORD_SLOT];
      radius = std::max(radius, (slot < known.slot_count) ? known.slot_radius[slot] : INT32_MAX);
    }
    if (radius < known.written_cells) {
      radius_squared = std::min(radius_squared, static_cast<float>(int64_t{radius} * radius));
    }
  }
  // Grass Dump: the call as the game made it, the radius it goes on with, the thread's float rounding.
  if (grass_dump.state.load(std::memory_order_acquire) == DUMP_CALLS && scrub == grass_dump.scrub) {
    const auto control = FloatControl();
    DumpChunk chunk("CALL");
    chunk.Pointer(cell).Raw(camera, 3 * sizeof(float)).Add(entry).Add(game_radius_squared).Add(radius_squared).Add(last)
        .Add(control[0]).Add(control[1]).Add<uint32_t>(GetCurrentThreadId());
    const std::unique_lock lock(grass_dump.mutex);
    if (grass_dump.state == DUMP_CALLS) {
      chunk.Write(&grass_dump.file);
      ++grass_dump.cells;
    }
  }
  original_scrub_cell(scrub, unused_edx, cell, camera, entry, radius_squared, last);
}

}  // namespace internal

// Grass Trace: one frame of the scrub into ReShade.log (from the next scrub update).
inline void TraceGrass() { internal::grass_trace.state = 1; }

// Grass Dump: the next frames' scrub into tw_grass_dump_<n>.bin next to the exe (internal::GrassDump).
inline void DumpGrass() {
  int idle = internal::DUMP_IDLE;
  if (!internal::grass_dump.state.compare_exchange_strong(idle, internal::DUMP_TIMING)) {
    renodx::utils::log::i("[Total War] Grass dump: one is already being written");
    return;
  }
  const std::unique_lock lock(internal::grass_dump.mutex);
  internal::grass_dump.presents = 0;
  renodx::utils::log::i("[Total War] Grass dump asked (from the next grass update)");
}

// Whether the grass draws' rows are read now (one atomic: the grass draws' callback returns on it).
inline bool GrassDumpReadsRows() {
  const int state = internal::grass_dump.state.load(std::memory_order_relaxed);
  return state == internal::DUMP_ROWS || state == internal::DUMP_ROWS_NEXT;
}

// A grass draw's arguments as the game gave them.
struct GrassDraw {
  uint32_t vertex_shader = 0;
  bool indexed = false;
  uint32_t count = 0;  // an instance's indices (or vertices)
  uint32_t instances = 0;
  uint32_t first = 0;  // the first index (or vertex)
  int32_t base_vertex = 0;
  uint32_t first_instance = 0;
};

// A grass draw while the dump reads rows: its mesh's and its instances' buffers as bound, and its instance rows read back
// (a GPU wait, for the dump's frames only) once per instance range of a window (the prefill and the G-buffer share them).
inline void AddGrassDumpRows(ID3D11DeviceContext* context, const GrassDraw& draw) {
  auto& dump = internal::grass_dump;
  if (!GrassDumpReadsRows() || context == nullptr) return;
  std::array<ID3D11Buffer*, 2> buffers = {};
  std::array<UINT, 2> strides = {};
  std::array<UINT, 2> offsets = {};
  context->IAGetVertexBuffers(0, 2, buffers.data(), strides.data(), offsets.data());
  Microsoft::WRL::ComPtr<ID3D11Buffer> mesh;
  Microsoft::WRL::ComPtr<ID3D11Buffer> instances;
  mesh.Attach(buffers[0]);
  instances.Attach(buffers[1]);
  Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  context->IAGetIndexBuffer(index_buffer.GetAddressOf(), &index_format, &index_offset);
  D3D11_BUFFER_DESC desc = {};
  if (instances != nullptr) instances->GetDesc(&desc);
  const uint32_t start = offsets[1] + (draw.first_instance * strides[1]);
  const auto range = std::make_pair(reinterpret_cast<uintptr_t>(instances.Get()), start);
  bool read_before = false;
  {
    const std::unique_lock lock(dump.mutex);
    read_before = std::ranges::find(dump.rows_read, range) != dump.rows_read.end();
    if (!read_before) dump.rows_read.push_back(range);
  }
  std::vector<uint8_t> rows;
  if (!read_before && instances != nullptr && strides[1] != 0) {
    rows = frame_trace::ReadBuffer<uint8_t>(context, instances.Get(), start, draw.instances * strides[1]);
  }
  // The camera the draw places its grass with (camera_VS_PS c0, camera_position): the scrub's of which frame.
  std::array<float, 4> camera = {NAN, NAN, NAN, NAN};
  Microsoft::WRL::ComPtr<ID3D11Buffer> camera_buffer;
  context->VSGetConstantBuffers(0, 1, camera_buffer.GetAddressOf());
  if (camera_buffer != nullptr) {
    if (const auto values = frame_trace::ReadBuffer<float>(context, camera_buffer.Get(), 0, 4); values.size() == camera.size()) {
      std::ranges::copy(values, camera.begin());
    }
  }
  const std::unique_lock lock(dump.mutex);
  if (dump.state != internal::DUMP_ROWS && dump.state != internal::DUMP_ROWS_NEXT) return;
  internal::DumpChunk chunk("ROWS");
  chunk.Add<uint32_t>(dump.state == internal::DUMP_ROWS ? 0 : 1).Add(dump.row_draws).Add(draw.vertex_shader)
      .Add<uint32_t>(draw.indexed ? 1 : 0).Add(draw.count).Add(draw.instances).Add(draw.first).Add(draw.base_vertex)
      .Add(draw.first_instance).Pointer(mesh.Get()).Add(offsets[0]).Add(strides[0]).Pointer(instances.Get())
      .Add(offsets[1]).Add(strides[1]).Add(desc.ByteWidth).Pointer(index_buffer.Get())
      .Add(static_cast<uint32_t>(index_format)).Add(index_offset).Add(camera).Add<uint32_t>(read_before ? 1 : 0)
      .Add(static_cast<uint32_t>(rows.size()));
  chunk.Write(&dump.file, rows.data(), rows.size());
  if (dump.row_draws++ == 0) renodx::utils::log::i("[Total War] Grass dump: the grass draws' rows read");
}

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
  // Grass Dump: no grass update for DUMP_PRESENTS presents (out of a battle, or it ended): written as it is, or given up.
  if (const int dump_state = internal::grass_dump.state.load(std::memory_order_relaxed);
      dump_state != internal::DUMP_IDLE && dump_state != internal::DUMP_CALLS) {
    const std::unique_lock lock(internal::grass_dump.mutex);
    if (internal::grass_dump.state != internal::DUMP_IDLE && internal::grass_dump.state != internal::DUMP_CALLS
        && ++internal::grass_dump.presents > internal::DUMP_PRESENTS) {
      if (internal::grass_dump.state >= internal::DUMP_ROWS) {
        internal::FinishGrassDump("the grass stopped updating, written");
      } else {
        internal::grass_dump.state = internal::DUMP_IDLE;
        renodx::utils::log::i("[Total War] Grass dump given up: no grass update (not in a battle)");
      }
    }
  }
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
      // OnDrawScrub's early-out only with DrawScrub's code as read, before the hook writes its entry.
      if (const auto* draw_scrub = reinterpret_cast<const char*>(internal::original_draw_scrub); draw_scrub != nullptr) {
        const auto matches = [](const char* code, std::string_view pattern) {
          return tw::engine_tweaks::FindPattern({std::string_view(code, (pattern.size() + 1) / 3)}, pattern) != nullptr;
        };
        internal::draw_scrub_read = matches(draw_scrub, internal::DRAW_SCRUB_NO_LOD)
                                    && matches(draw_scrub + internal::DRAW_SCRUB_RETURN_OFFSET, internal::DRAW_SCRUB_RETURN);
        internal::draw_scrub_code = reinterpret_cast<const uint8_t*>(draw_scrub);
        renodx::utils::log::i("[Total War] Graphics: grass records past every LOD ",
                              internal::draw_scrub_read ? "skip DrawScrub" : "go to DrawScrub (its code differs)");
      }
      add(internal::SCRUB_UPDATE, reinterpret_cast<void**>(&internal::original_scrub_update),
          reinterpret_cast<void*>(&internal::OnScrubUpdate), "grass cells");
      // The cell loop only with its record step and return as read (its arguments' layout).
      if (const auto* cell_loop = reinterpret_cast<const char*>(tw::engine_tweaks::FindPattern(sections.code, internal::SCRUB_CELL));
          cell_loop != nullptr
          && tw::engine_tweaks::FindPattern({std::string_view(cell_loop + internal::SCRUB_CELL_NEXT_OFFSET, 11)}, internal::SCRUB_CELL_NEXT) != nullptr
          && tw::engine_tweaks::FindPattern({std::string_view(cell_loop + internal::SCRUB_CELL_RETURN_OFFSET, 3)}, internal::SCRUB_CELL_RETURN) != nullptr) {
        internal::cell_loop_code = reinterpret_cast<const uint8_t*>(cell_loop);
        add(internal::SCRUB_CELL, reinterpret_cast<void**>(&internal::original_scrub_cell),
            reinterpret_cast<void*>(&internal::OnScrubCell), "grass cells' radius");
      } else {
        missing += std::format("{}grass cells' radius", missing.empty() ? "" : ", ");
      }
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
  if (internal::grass_dump.state.load() != internal::DUMP_IDLE) {
    const std::unique_lock lock(internal::grass_dump.mutex);
    if (internal::grass_dump.file.is_open()) internal::FinishGrassDump("the addon unloaded");
    internal::grass_dump.state = internal::DUMP_IDLE;
  }
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
// the Shadows option changes). Two Shadow Cascades Under Rays: Max's and High's cascade count (+0x98: 4, 3) and +0x84
// (2, 1) written as Medium's (2, 0: its own writes, right after the pattern, read the same at shadow build).
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
// Max's and High's: +0x98 and +0x84 (the game's values in the pattern); Medium's, and its two writes of them after the pattern
// (mov dword ptr [edi + 0x98], 2; mov dword ptr [edi + 0x84], 0): only where those follow are they written.
constexpr std::array<size_t, 4> CASCADE_OFFSETS = {59, 69, 105, 115};
constexpr std::array<uint32_t, 4> GAME_CASCADES = {4, 2, 3, 1};
constexpr std::array<uint32_t, 4> TWO_CASCADES = {2, 0, 2, 0};
constexpr std::array<uint8_t, 20> MEDIUM_CASCADES = {0xC7, 0x87, 0x98, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
                                                     0xC7, 0x87, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t* code = nullptr;
static float written = 1.f;              // the scale last written
static bool two_cascades_written = false;
}  // namespace internal

// The game's sizes x `scale`, and Max's and High's cascades as Medium's where `two_cascades` (every present: the code is
// written only when either changes).
inline void Set(float scale, bool two_cascades) {
  if (internal::code == nullptr) return;
  two_cascades = two_cascades && std::memcmp(internal::code + internal::CODE_SIZE, internal::MEDIUM_CASCADES.data(), internal::MEDIUM_CASCADES.size()) == 0;
  if (scale == internal::written && two_cascades == internal::two_cascades_written) return;
  internal::written = scale;
  internal::two_cascades_written = two_cascades;
  DWORD protection;
  if (VirtualProtect(internal::code, internal::CODE_SIZE, PAGE_EXECUTE_READWRITE, &protection) == FALSE) return;
  for (size_t index = 0; index < internal::SIZE_OFFSETS.size(); ++index) {
    const float size = internal::GAME_SIZES[index] * scale;
    std::memcpy(internal::code + internal::SIZE_OFFSETS[index], &size, sizeof(size));
  }
  const auto& cascades = two_cascades ? internal::TWO_CASCADES : internal::GAME_CASCADES;
  for (size_t index = 0; index < internal::CASCADE_OFFSETS.size(); ++index) {
    std::memcpy(internal::code + internal::CASCADE_OFFSETS[index], &cascades[index], sizeof(cascades[index]));
  }
  VirtualProtect(internal::code, internal::CODE_SIZE, protection, &protection);
  FlushInstructionCache(GetCurrentProcess(), internal::code, internal::CODE_SIZE);
}

// At the game's device creation, before it builds its shadows: finds the code and sets `scale` and `two_cascades`.
inline void Install(float scale, bool two_cascades) {
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
  Set(scale, two_cascades);
}

// Addon detach: the game's sizes and cascades back.
inline void Uninstall() {
  Set(1.f, false);
}

}  // namespace shadow_map

}  // namespace twattila::graphics_hooks
