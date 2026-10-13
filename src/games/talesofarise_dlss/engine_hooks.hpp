/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <detours.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/detour.hpp"
#include "./code_patch.hpp"

// Character, outline and cutscene settings the engine has no console variable for: hooks and small patches in the
// game's code, found by byte patterns of this exe. A hooked function's start comes from the exe's unwind data; struct
// offsets are the game's reflection data's (the generated property params, offset at +0x28). Installed while the
// addon loads (during the game's device creation, before any level runs). Code another mod (Arise-SDK) already
// changed is left alone: where it has a live console variable (sdk.*), the setting writes that instead. Every
// setting keeps the game's behaviour at its game value. Thanks to emoose: these do what his Arise-SDK settings do,
// found again in this exe from scratch (no Arise-SDK code or byte patterns).
namespace talesofarise::engine_hooks {

struct Option {
  const wchar_t* name;  // the graphics setting's name
  float game;           // the game's own behaviour
  float value;          // read by the hooks; written by the graphics settings
  const wchar_t* arise_sdk_cvar = nullptr;  // Arise-SDK's live setting for the same thing, when its hooks are in place
  bool available = false;
  std::string details = "not installed";  // where it was installed, or why not
};
enum : uint8_t {
  NPC_DISTANCE,
  MONSTER_DISTANCE,
  CHARACTER_DETAIL_DISTANCE,
  NEVER_HIDE_CHARACTERS,
  OUTLINE_DISTANCE,
  SKIT_RESOLUTION,
  SMOOTH_CUTSCENES,
};
static std::array<Option, 7> options = {{
    {.name = L"Tales.NPCDistance", .game = 3100.f, .value = 3100.f},  // the game's NPC despawn distance
    {.name = L"Tales.MonsterDistance", .game = 1.f, .value = 1.f, .arise_sdk_cvar = L"sdk.MonsterDistanceMultiplier"},
    {.name = L"Tales.CharacterDetailDistance", .game = 1.f, .value = 1.f, .arise_sdk_cvar = L"sdk.CharaLODMultiplier"},
    {.name = L"Tales.NeverHideCharacters", .game = 0.f, .value = 0.f},
    {.name = L"Tales.OutlineDistance", .game = 6000.f, .value = 6000.f, .arise_sdk_cvar = L"sdk.MinStageEdgeBaseDistance"},
    // Percent of the screen's pixel count; 0: the game's sizes.
    {.name = L"Tales.SkitResolution", .game = 0.f, .value = 0.f, .arise_sdk_cvar = L"sdk.CutsceneScreenPercentage"},
    {.name = L"Tales.SmoothCutscenes", .game = 0.f, .value = 0.f},
}};
// The game's output size (width << 32 | height; 0 without a swap chain), for the skit panels.
static std::atomic<uint64_t> screen_size = 0;

inline float Value(uint8_t option) {
  return std::atomic_ref(options[option].value).load(std::memory_order_relaxed);
}

// The NPC distance as a factor of the game's: every NPC distance (spawn, despawn, camera fade in and out) is scaled by
// it, so their gaps keep the game's proportions. 1 or less: the game's.
inline float NpcScale() {
  return Value(NPC_DISTANCE) / options[NPC_DISTANCE].game;
}

// APFNpcManager::InitsDistances(this, bool), once per level, sets the manager's distances from the game's data:
// FPFNpcSpawnSettingsData (SpawnDistance +0, DespawnDistance +4) and FPFNpcCameraSettingsData (CameraFarFadeIn
// +0x1C, CameraFarFadeOut +0x24), normal and high.
constexpr std::array<size_t, 2> NPC_SPAWN_SETTINGS = {0x338, 0x374};
constexpr std::array<size_t, 2> NPC_CAMERA_SETTINGS = {0x344, 0x380};
using NpcManagerDistances = void (*)(uint8_t* manager, bool high);
static NpcManagerDistances npc_manager_distances = nullptr;
static void OnNpcManagerDistances(uint8_t* manager, bool high) {
  npc_manager_distances(manager, high);
  const float scale = NpcScale();
  if (scale <= 1.f) return;
  for (const size_t field : {NPC_SPAWN_SETTINGS[0], NPC_SPAWN_SETTINGS[0] + 4, NPC_SPAWN_SETTINGS[1], NPC_SPAWN_SETTINGS[1] + 4,
                             NPC_CAMERA_SETTINGS[0] + 0x1C, NPC_CAMERA_SETTINGS[0] + 0x24,
                             NPC_CAMERA_SETTINGS[1] + 0x1C, NPC_CAMERA_SETTINGS[1] + 0x24}) {
    *reinterpret_cast<float*>(manager + field) *= scale;
  }
}

// APFNpcWalkSystem::CreateSpawnData(this, DefineID, Params): walking NPCs despawn at Params.DespawnMaxDistance.
struct WalkSpawnParams {
  int32_t max_spawn_count;
  float spawn_span;
  float despawn_max_distance;
};
using WalkSpawnData = void (*)(void* system, const void* define_id, const WalkSpawnParams* params);
static WalkSpawnData walk_spawn_data = nullptr;
static void OnWalkSpawnData(void* system, const void* define_id, const WalkSpawnParams* params) {
  const float scale = NpcScale();
  if (scale <= 1.f) {
    walk_spawn_data(system, define_id, params);
    return;
  }
  WalkSpawnParams scaled = *params;
  scaled.despawn_max_distance *= scale;
  walk_spawn_data(system, define_id, &scaled);
}

// AEncountGroup::IsSpawnRangePlayer(this, AreaInfo): a group's monsters spawn while the player is within its
// SpawnAreaInfo (+0x410; Range, HalfHeight); its BattleAreaInfo goes through here too, unchanged.
struct EncountAreaInfo {
  float range;
  float half_height;
};
constexpr size_t SPAWN_AREA_INFO = 0x410;
using SpawnRange = bool (*)(uint8_t* group, const EncountAreaInfo* area);
static SpawnRange spawn_range = nullptr;
static bool OnSpawnRange(uint8_t* group, const EncountAreaInfo* area) {
  const float multiplier = Value(MONSTER_DISTANCE);
  if (multiplier == options[MONSTER_DISTANCE].game || area != reinterpret_cast<const EncountAreaInfo*>(group + SPAWN_AREA_INFO)) {
    return spawn_range(group, area);
  }
  const EncountAreaInfo scaled = {.range = area->range * multiplier, .half_height = area->half_height * multiplier};
  return spawn_range(group, &scaled);
}

// UAchCharacterBuildComponent::SetCulling(this, bool), when a character is built: off-screen and occlusion culling.
using SetCulling = void (*)(void* component, bool culling);
static SetCulling set_culling = nullptr;
static void OnSetCulling(void* component, bool culling) {
  set_culling(component, culling && Value(NEVER_HIDE_CHARACTERS) == options[NEVER_HIDE_CHARACTERS].game);
}

// FSceneView::EndFinalPostprocessSettings(this, InitOptions), every frame: FinalPostProcessSettings (+0xD60)
// .StageEdgeBaseDistance_TO14 (+0x700), how far the cel-shading outlines reach; raised to the outline distance.
constexpr size_t STAGE_EDGE_BASE_DISTANCE = 0xD60 + 0x700;
using EndFinalPostprocess = void* (*)(uint8_t* view, void* init_options);
static EndFinalPostprocess end_final_postprocess = nullptr;
static void* OnEndFinalPostprocess(uint8_t* view, void* init_options) {
  void* result = end_final_postprocess(view, init_options);
  if (const float distance = Value(OUTLINE_DISTANCE); distance != options[OUTLINE_DISTANCE].game) {
    auto* edge = reinterpret_cast<float*>(view + STAGE_EDGE_BASE_DISTANCE);
    *edge = std::max(*edge, distance);
  }
  return result;
}

// The NPC and monster LOD code copies their LOD distances (FAchCharacterLODData) with memcpy, then counts the ones
// the camera is beyond; the call goes through a stub to CopyLodDistances, which multiplies them all.
static void* CopyLodDistances(void* destination, const void* source, size_t size) {
  std::memcpy(destination, source, size);
  const float multiplier = Value(CHARACTER_DETAIL_DISTANCE);
  if (multiplier == options[CHARACTER_DETAIL_DISTANCE].game) return destination;
  auto* distances = static_cast<float*>(destination);
  for (size_t index = 0; index < size / sizeof(float); ++index) distances[index] *= multiplier;
  return destination;
}

// UKismetRenderingLibrary::execCreateRenderTarget2D (Blueprint's CreateRenderTarget2D, the skit panels' targets) sets
// SizeX / SizeY (+0xC0 / +0xC4), then calls UTexture::UpdateResource (vtable +0x248); that call goes through a stub to
// OnRenderTargetCreated, which first gives a target smaller than the screen the screen's pixel count (times the skit
// resolution) in its own shape. Their scene captures also get TAA and motion blur (SCENE_CAPTURE_SHOW_FLAGS).
static void OnRenderTargetCreated(uint8_t* render_target) {
  constexpr size_t SIZE_X = 0xC0;
  constexpr size_t TEXTURE_UPDATE_RESOURCE = 0x248;
  auto* size = reinterpret_cast<int32_t*>(render_target + SIZE_X);  // SizeX, SizeY
  const float percent = Value(SKIT_RESOLUTION);
  const uint64_t screen = screen_size.load(std::memory_order_relaxed);
  if (percent != options[SKIT_RESOLUTION].game && screen != 0 && size[0] > 0 && size[1] > 0) {
    const double scale = std::clamp(percent, 1.f, 400.f) / 100.0;
    const double screen_area = static_cast<double>(screen >> 32) * static_cast<double>(screen & 0xFFFFFFFF) * scale * scale;
    if (const double area = static_cast<double>(size[0]) * size[1]; screen_area > area) {
      // Multiples of 4: Unreal quantizes a view's size, an odd one would be upscaled.
      const double enlarge = std::sqrt(screen_area / area);
      size[0] = (static_cast<int32_t>(std::ceil(size[0] * enlarge)) + 3) & ~3;
      size[1] = (static_cast<int32_t>(std::ceil(size[1] * enlarge)) + 3) & ~3;
    }
  }
  using UpdateResource = void (*)(uint8_t* texture);
  const auto* vtable = *reinterpret_cast<const UpdateResource* const*>(render_target);
  vtable[TEXTURE_UPDATE_RESOURCE / sizeof(UpdateResource)](render_target);
}

// ULevelSequence and UActorSequence (cutscenes): after one loads (vtable slot 0x10), its MovieScene (+0x348) plays
// WithSubFrames (EvaluationType +0xC8: FrameLocked 0, WithSubFrames 1), at the display's frame rate instead of in its
// own frames. Their constructors: mov [rbx + 0x348], reg; lea rax, [vtable]; mov [rbx], rax.
constexpr std::array<std::string_view, 2> SEQUENCE_VTABLES = {
    "48 89 BB 48 03 00 00 48 8D 05 ?? ?? ?? ?? 48 89 03 48 89 BB 50 03 00 00",
    "48 89 AB 48 03 00 00 48 8D 05 ?? ?? ?? ?? 48 89 03 44 8D 4D 01",
};
constexpr size_t SEQUENCE_VTABLE_DISPLACEMENT = 10;
constexpr size_t SEQUENCE_POST_LOAD = 0x10;
using PostLoad = void (*)(uint8_t* sequence);
static std::array<PostLoad, SEQUENCE_VTABLES.size()> sequence_post_load = {};
template <size_t SEQUENCE>
static void OnSequencePostLoad(uint8_t* sequence) {
  constexpr size_t MOVIE_SCENE = 0x348;
  constexpr size_t EVALUATION_TYPE = 0xC8;
  constexpr uint8_t WITH_SUB_FRAMES = 1;
  sequence_post_load[SEQUENCE](sequence);
  if (Value(SMOOTH_CUTSCENES) == options[SMOOTH_CUTSCENES].game) return;
  uint8_t* movie_scene = *reinterpret_cast<uint8_t**>(sequence + MOVIE_SCENE);
  if (movie_scene != nullptr) movie_scene[EVALUATION_TYPE] = WITH_SUB_FRAMES;
}
constexpr std::array<PostLoad, SEQUENCE_VTABLES.size()> SEQUENCE_POST_LOAD_HOOKS = {&OnSequencePostLoad<0>, &OnSequencePostLoad<1>};

struct Hook {
  uint8_t option;
  std::string_view pattern;  // anywhere in the function
  void** original;
  void* replacement;
};
// After the LOD distances' copy: mov edi, [rsp + 0x28]; jmp; ...; test edi, edi; jle (the call is the 5 bytes before).
constexpr std::string_view LOD_DISTANCES_COPIED = "8B 7C 24 28 EB 05 48 8B F3 8B FB 48 8B AC 24 ?? ?? 00 00 85 FF 7E";
// The CSM distance clamps r.Shadow.DistanceScale to 2 (minss xmm6, [2.0]); removed, as it changes nothing at 2 or less.
constexpr std::string_view SUN_DISTANCE_LIMIT = "F3 0F 5D 35 ?? ?? ?? ?? 40 84 ED 74 ?? F3 0F 10 86 ?? ?? 00 00";
// The NPC camera fade defaults: UPFNpcCameraFadeComponent's constructor (mov dword ptr [rbx + field], imm: camera far
// fade 2800 / 2900, far fade 3600 / 3800) and FPFNpcCameraSettingsData's (mov dword ptr [rdx + field], imm: 2800 /
// 2900). Each pattern holds a fade-in and a fade-out immediate.
struct FadeDefaults {
  std::string_view pattern;
  size_t fade_in;
  size_t fade_out;
};
constexpr std::array<FadeDefaults, 3> NPC_FADE_DEFAULTS = {{
    {"C7 83 18 02 00 00 00 00 2F 45 C7 83 20 02 00 00 00 40 35 45 C7 83 28 02", 6, 16},
    {"C7 83 38 02 00 00 00 00 61 45 C7 83 3C 02 00 00 00 80 6D 45", 6, 16},
    {"C7 42 1C 00 00 2F 45 C7 42 24 00 40 35 45", 3, 10},
}};
// mov rdx, [rax] (the vtable); mov [rax + 0xC0], r15d; mov [rax + 0xC4], r14d; call qword ptr [rdx + 0x248]; mov dl, 1
constexpr std::string_view RENDER_TARGET_SIZE = "48 8B 10 44 89 B8 C0 00 00 00 44 89 B0 C4 00 00 00 FF ?? 48 02 00 00 B2 01";
constexpr size_t RENDER_TARGET_UPDATE = 17;  // the call, 6 bytes
// The scene capture constructor clears ShowFlags.TemporalAA and MotionBlur (and dword ptr [rbx + 0x2B8], ~0x2008);
// with the skit resolution it sets them (or dword ptr [rbx + 0x2B8], 0x2008: the same length, so a constructor
// running mid-write still runs one of the two). Followed by unpcklps (two other constructors clear them too).
constexpr std::string_view SCENE_CAPTURE_SHOW_FLAGS = "81 A3 B8 02 00 00 F7 DF FF FF 0F 14 CA";
static std::vector<renodx::utils::detour::Function> hooks;
static code_patch::CodePatch lod_distances_call;
static code_patch::CodePatch sun_distance_limit;
static std::array<code_patch::CodePatch, NPC_FADE_DEFAULTS.size() * 2> npc_fade_defaults;  // in, out
static float npc_fade_scale = 1.f;  // the NPC scale the fade defaults hold
static code_patch::CodePatch render_target_update;
static code_patch::CodePatch scene_capture_show_flags;
static std::array<code_patch::CodePatch, SEQUENCE_VTABLES.size()> sequence_post_load_slots;

// A jump to `target` (mov rax, target; jmp rax) within a rel32 call's reach of `site` (Detours' region near it).
inline uint8_t* JumpStub(const uint8_t* site, const void* target) {
  DWORD region_size = 0;
  auto* stub = static_cast<uint8_t*>(DetourAllocateRegionWithinJumpBounds(site, &region_size));
  if (stub == nullptr) return nullptr;
  std::array<uint8_t, 12> jump = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
  const auto address = reinterpret_cast<uint64_t>(target);
  std::memcpy(jump.data() + 2, &address, sizeof(address));
  std::memcpy(stub, jump.data(), jump.size());
  FlushInstructionCache(GetCurrentProcess(), stub, jump.size());
  return stub;
}

inline void Install() {
  const auto fail = [](uint8_t option, std::string reason) {
    options[option].available = false;
    options[option].details = std::move(reason);
  };
  const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
  for (auto& option : options) {
    option.available = true;
    option.details.clear();
  }

  const std::array<Hook, 5> wanted = {{
      {.option = NPC_DISTANCE,
       .pattern = "C7 45 ?? 00 00 2F 45 C7 45 ?? 00 40 35 45 C7 45 ?? CD CC 4C 3E 66 0F 7F 45 ?? E8",
       .original = reinterpret_cast<void**>(&npc_manager_distances),
       .replacement = reinterpret_cast<void*>(&OnNpcManagerDistances)},
      {.option = NPC_DISTANCE,
       .pattern = "48 81 EC B8 00 00 00 F2 41 0F 10 00",
       .original = reinterpret_cast<void**>(&walk_spawn_data),
       .replacement = reinterpret_cast<void*>(&OnWalkSpawnData)},
      {.option = MONSTER_DISTANCE,
       .pattern = "0F 2F 07 0F 87 ?? ?? ?? ?? F3 0F 10 5F 04 49 8B D0 F3 0F 58 DB",
       .original = reinterpret_cast<void**>(&spawn_range),
       .replacement = reinterpret_cast<void*>(&OnSpawnRange)},
      {.option = NEVER_HIDE_CHARACTERS,
       .pattern = "0F B6 81 FC 01 00 00 4C 8B E9 88 91 FD 01 00 00 B9 01 00 00 00 3A C1",
       .original = reinterpret_cast<void**>(&set_culling),
       .replacement = reinterpret_cast<void*>(&OnSetCulling)},
      {.option = OUTLINE_DISTANCE,
       .pattern = "48 8B D9 0F 29 74 24 30 48 81 C1 C8 1B 00 00 0F 29 7C 24 20 E8",
       .original = reinterpret_cast<void**>(&end_final_postprocess),
       .replacement = reinterpret_cast<void*>(&OnEndFinalPostprocess)},
  }};
  std::array<uint8_t*, wanted.size()> starts = {};
  for (size_t index = 0; index < wanted.size(); ++index) {
    const auto& hook = wanted[index];
    std::string where;
    const uint8_t* site = code_patch::FindPattern(hook.pattern, &where);
    // The function's start: its unwind entry, following the chained entries of split functions.
    DWORD64 image_base = 0;
    const RUNTIME_FUNCTION* entry = (site == nullptr ? nullptr : RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(site), &image_base, nullptr));
    while (entry != nullptr) {
      if ((entry->UnwindData & 1) != 0) {
        entry = reinterpret_cast<const RUNTIME_FUNCTION*>(image_base + (entry->UnwindData & ~DWORD{1}));
        continue;
      }
      const auto* unwind = reinterpret_cast<const uint8_t*>(image_base + entry->UnwindData);
      if (((unwind[0] >> 3) & UNW_FLAG_CHAININFO) == 0) {
        starts[index] = reinterpret_cast<uint8_t*>(image_base + entry->BeginAddress);
        break;
      }
      entry = reinterpret_cast<const RUNTIME_FUNCTION*>(unwind + 4 + (((unwind[2] + 1) & ~1) * sizeof(uint16_t)));
    }
    const uint8_t* start = starts[index];
    if (start == nullptr) {
      fail(hook.option, std::format("code not found ({})", where));
      continue;
    }
    // A jump in the first instruction (after a REX prefix): another mod's hook.
    const size_t first = ((start[0] & 0xF0) == 0x40 ? 1 : 0);
    if (start[first] == 0xE9 || (start[first] == 0xFF && start[first + 1] == 0x25)) {
      starts[index] = nullptr;
      fail(hook.option, "already hooked by another mod (Arise-SDK?)");
    }
  }
  // An option needs all its hooks.
  hooks.clear();
  for (size_t index = 0; index < wanted.size(); ++index) {
    const auto& hook = wanted[index];
    auto& option = options[hook.option];
    if (starts[index] == nullptr || !option.available) continue;
    *hook.original = starts[index];
    hooks.emplace_back(hook.original, hook.replacement);
    option.details += std::format("{}hook exe+0x{:X}", (option.details.empty() ? "" : ", "), starts[index] - image);
  }
  if (!hooks.empty()) {
    try {
      renodx::utils::detour::Install(hooks);
    } catch (const std::exception& error) {
      for (auto& function : hooks) *function.original = nullptr;
      for (const auto& hook : wanted) {
        if (options[hook.option].available) fail(hook.option, std::format("hooks failed: {}", error.what()));
      }
      hooks.clear();
    }
  }

  // Character detail: the memcpy call goes to a stub.
  lod_distances_call = {};
  if (uint8_t* site = code_patch::FindPattern(LOD_DISTANCES_COPIED, &lod_distances_call.details); site == nullptr) {
    fail(CHARACTER_DETAIL_DISTANCE, std::format("code not found ({})", lod_distances_call.details));
  } else {
    uint8_t* call = site - 5;
    int32_t memcpy_offset = 0;
    std::memcpy(&memcpy_offset, call + 1, sizeof(memcpy_offset));
    // Another mod's stub (mov rax, imm64) instead of memcpy.
    const uint8_t* memcpy_target = call + 5 + memcpy_offset;
    const uint8_t* stub = nullptr;
    if (call[0] != 0xE8 || (memcpy_target[0] == 0x48 && memcpy_target[1] == 0xB8)) {
      fail(CHARACTER_DETAIL_DISTANCE, "already redirected by another mod (Arise-SDK?)");
    } else if (stub = JumpStub(call, reinterpret_cast<const void*>(&CopyLodDistances)); stub == nullptr) {
      fail(CHARACTER_DETAIL_DISTANCE, "no memory near the code");
    } else {
      const auto stub_offset = static_cast<int32_t>(stub - (call + 5));
      lod_distances_call.address = call + 1;
      lod_distances_call.original.assign(call + 1, call + 5);
      lod_distances_call.replacement.resize(sizeof(stub_offset));
      std::memcpy(lod_distances_call.replacement.data(), &stub_offset, sizeof(stub_offset));
      if (code_patch::ApplyCodePatch(&lod_distances_call, true)) {
        options[CHARACTER_DETAIL_DISTANCE].details = lod_distances_call.details;
      } else {
        fail(CHARACTER_DETAIL_DISTANCE, "patch failed: " + lod_distances_call.details);
      }
    }
  }

  // NPC fade defaults: found now, written by Update.
  for (size_t index = 0; index < NPC_FADE_DEFAULTS.size(); ++index) {
    const auto& fade = NPC_FADE_DEFAULTS[index];
    auto& fade_in = npc_fade_defaults[index * 2];
    auto& fade_out = npc_fade_defaults[(index * 2) + 1];
    fade_in = {};
    fade_out = {};
    uint8_t* site = code_patch::FindPattern(fade.pattern, &fade_in.details);
    if (site == nullptr) {
      if (options[NPC_DISTANCE].available) fail(NPC_DISTANCE, std::format("fade defaults {} not found ({})", index, fade_in.details));
      continue;
    }
    for (const auto& [patch, offset] : {std::pair{&fade_in, fade.fade_in}, std::pair{&fade_out, fade.fade_out}}) {
      patch->address = site + offset;
      patch->original.assign(patch->address, patch->address + sizeof(float));
    }
  }
  npc_fade_scale = 1.f;

  // Skit panels: their UpdateResource call becomes nop; call stub.
  render_target_update = {};
  if (uint8_t* site = code_patch::FindPattern(RENDER_TARGET_SIZE, &render_target_update.details); site == nullptr) {
    fail(SKIT_RESOLUTION, std::format("code not found ({}; changed by another mod, Arise-SDK?)", render_target_update.details));
  } else if (uint8_t* call = site + RENDER_TARGET_UPDATE; const uint8_t* stub = JumpStub(call, reinterpret_cast<const void*>(&OnRenderTargetCreated))) {
    const auto stub_offset = static_cast<int32_t>(stub - (call + 6));
    render_target_update.address = call;
    render_target_update.original.assign(call, call + 6);
    render_target_update.replacement = {0x90, 0xE8, 0, 0, 0, 0};
    std::memcpy(render_target_update.replacement.data() + 2, &stub_offset, sizeof(stub_offset));
    if (code_patch::ApplyCodePatch(&render_target_update, true)) {
      options[SKIT_RESOLUTION].details = render_target_update.details;
    } else {
      fail(SKIT_RESOLUTION, "patch failed: " + render_target_update.details);
    }
  } else {
    fail(SKIT_RESOLUTION, "no memory near the code");
  }
  scene_capture_show_flags = {};
  if (uint8_t* site = code_patch::FindPattern(SCENE_CAPTURE_SHOW_FLAGS, &scene_capture_show_flags.details); site != nullptr) {
    scene_capture_show_flags.address = site;
    scene_capture_show_flags.original.assign(site, site + 10);
    scene_capture_show_flags.replacement = {0x81, 0x8B, 0xB8, 0x02, 0x00, 0x00, 0x08, 0x20, 0x00, 0x00};
  } else if (options[SKIT_RESOLUTION].available) {
    options[SKIT_RESOLUTION].details += std::format(", scene capture anti-aliasing not found ({})", scene_capture_show_flags.details);
  }

  // Cutscenes: both sequence classes' PostLoad slots, or neither.
  for (size_t sequence = 0; sequence < SEQUENCE_VTABLES.size(); ++sequence) {
    auto& slot = sequence_post_load_slots[sequence];
    slot = {};
    const uint8_t* site = code_patch::FindPattern(SEQUENCE_VTABLES[sequence], &slot.details);
    if (site == nullptr) {
      fail(SMOOTH_CUTSCENES, std::format("sequence {} not found ({})", sequence, slot.details));
      continue;
    }
    int32_t displacement = 0;
    std::memcpy(&displacement, site + SEQUENCE_VTABLE_DISPLACEMENT, sizeof(displacement));
    auto* vtable = reinterpret_cast<uint8_t* const*>(site + SEQUENCE_VTABLE_DISPLACEMENT + sizeof(displacement) + displacement);
    const uint8_t* post_load = vtable[SEQUENCE_POST_LOAD];
    if (post_load < image || post_load >= image + nt->OptionalHeader.SizeOfImage) {
      fail(SMOOTH_CUTSCENES, "already hooked by another mod (Arise-SDK?)");
      continue;
    }
    sequence_post_load[sequence] = reinterpret_cast<PostLoad>(vtable[SEQUENCE_POST_LOAD]);
    slot.address = reinterpret_cast<uint8_t*>(const_cast<uint8_t**>(&vtable[SEQUENCE_POST_LOAD]));
    slot.original.assign(slot.address, slot.address + sizeof(PostLoad));
    const auto hook = reinterpret_cast<uintptr_t>(SEQUENCE_POST_LOAD_HOOKS[sequence]);
    slot.replacement.resize(sizeof(hook));
    std::memcpy(slot.replacement.data(), &hook, sizeof(hook));
  }
  for (auto& slot : sequence_post_load_slots) {
    if (!options[SMOOTH_CUTSCENES].available) break;
    if (!code_patch::ApplyCodePatch(&slot, true)) fail(SMOOTH_CUTSCENES, "patch failed: " + slot.details);
  }
  if (options[SMOOTH_CUTSCENES].available) {
    options[SMOOTH_CUTSCENES].details = std::format("vtable slots {}, {}", sequence_post_load_slots[0].details,
                                                    sequence_post_load_slots[1].details);
  } else {
    for (auto& slot : sequence_post_load_slots) code_patch::ApplyCodePatch(&slot, false);
  }

  sun_distance_limit = {};
  if (uint8_t* site = code_patch::FindPattern(SUN_DISTANCE_LIMIT, &sun_distance_limit.details); site != nullptr) {
    sun_distance_limit.address = site;
    sun_distance_limit.original.assign(site, site + 8);
    sun_distance_limit.replacement.assign(8, 0x90);
    code_patch::ApplyCodePatch(&sun_distance_limit, true);
  }
}

// The code that follows a setting: the skit panels' anti-aliasing and the NPC fade defaults (only immediates change: a
// thread reading one mid-write gets an odd distance, never other instructions).
inline void Update() {
  if (const bool skits = options[SKIT_RESOLUTION].available && Value(SKIT_RESOLUTION) != options[SKIT_RESOLUTION].game;
      scene_capture_show_flags.address != nullptr && scene_capture_show_flags.applied != skits) {
    code_patch::ApplyCodePatch(&scene_capture_show_flags, skits);
  }
  const float scale = std::max(NpcScale(), 1.f);
  if (!options[NPC_DISTANCE].available || scale == npc_fade_scale) return;
  for (auto& patch : npc_fade_defaults) {
    code_patch::ApplyCodePatch(&patch, false);
    if (scale == 1.f) continue;
    float distance = 0.f;
    std::memcpy(&distance, patch.original.data(), sizeof(distance));
    distance *= scale;
    patch.replacement.resize(sizeof(distance));
    std::memcpy(patch.replacement.data(), &distance, sizeof(distance));
    code_patch::ApplyCodePatch(&patch, true);
  }
  npc_fade_scale = scale;
}

inline void Uninstall() {
  for (auto& slot : sequence_post_load_slots) code_patch::ApplyCodePatch(&slot, false);
  code_patch::ApplyCodePatch(&scene_capture_show_flags, false);
  code_patch::ApplyCodePatch(&render_target_update, false);
  for (auto& patch : npc_fade_defaults) code_patch::ApplyCodePatch(&patch, false);
  code_patch::ApplyCodePatch(&lod_distances_call, false);
  code_patch::ApplyCodePatch(&sun_distance_limit, false);
  if (!hooks.empty()) {
    try {
      renodx::utils::detour::Uninstall(hooks);
    } catch (const std::exception&) {
      // No-op: the game is closing; nothing else can be done.
    }
    hooks.clear();
  }
  for (auto& option : options) option.available = false;
}

}  // namespace talesofarise::engine_hooks
