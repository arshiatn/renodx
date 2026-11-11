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
#include <chrono>
#include <format>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
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
  SMOOTH_SUN,
  DAY_SPEED,
  SUN_SHADOW_LAYERS,
};
static std::array<Option, 10> options = {{
    {.name = L"Tales.NPCDistance", .game = 3100.f, .value = 3100.f},  // the game's NPC despawn distance
    {.name = L"Tales.MonsterDistance", .game = 1.f, .value = 1.f, .arise_sdk_cvar = L"sdk.MonsterDistanceMultiplier"},
    {.name = L"Tales.CharacterDetailDistance", .game = 1.f, .value = 1.f, .arise_sdk_cvar = L"sdk.CharaLODMultiplier"},
    {.name = L"Tales.NeverHideCharacters", .game = 0.f, .value = 0.f, .arise_sdk_cvar = L"sdk.CharaDisableCull"},
    {.name = L"Tales.OutlineDistance", .game = 6000.f, .value = 6000.f, .arise_sdk_cvar = L"sdk.MinStageEdgeBaseDistance"},
    // Percent of the screen's pixel count; 0: the game's sizes.
    {.name = L"Tales.SkitResolution", .game = 0.f, .value = 0.f, .arise_sdk_cvar = L"sdk.CutsceneScreenPercentage"},
    {.name = L"Tales.SmoothCutscenes", .game = 0.f, .value = 0.f, .arise_sdk_cvar = L"sdk.CutsceneAllowSubframes"},
    {.name = L"Tales.SmoothSun", .game = 0.f, .value = 0.f},
    {.name = L"Tales.DaySpeed", .game = 1.f, .value = 1.f},  // the time of day's speed, times the game's (0: stopped)
    {.name = L"Tales.SunShadowLayers", .game = 0.f, .value = 0.f},  // the sun's shadow cascades; 0: the light's own
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

// Smooth sun: Bandai's sky Blueprint moves the sun in bursts: every 7.2 s (6 minutes of game time, ATN's overlay) it
// turns it quickly to the next position, then holds it, although its world time runs every frame (AWorldTimeActor::Tick,
// exe+0x5FD930, sends it to the Blueprint every frame; DLSS v73's tick interval test changed nothing).
// The light: each new direction reaches the renderer through FLightSceneProxy::SetTransform(proxy, LightToWorld,
// Position) (LightToWorld at +0x70, its inverse +0x30, Position +0xB0; LightType +0x140, 0: directional; both
// arguments 16-byte aligned: it loads them with movaps, v74 crashed on unaligned ones). For directional lights the hook
// keeps the newest transform as the target and passes the one shown so far; every present (the render thread, between
// frames: D3D11 has no RHI thread in UE 4.21) moves it on. The directional proxies' destructor (their vtable's first
// slot) drops them, so no freed proxy is written.
// The sun disk: the sky shader reads the sun's direction from a material parameter collection (e.g. cb2[4] of ps
// 0x11039AD5; v76's material instance hook found none), which the Blueprint sets in the same bursts through
// UMaterialParameterCollectionInstance::SetVectorParameterValue(instance, Name, Value) (game thread; a change marks the
// instance for its render update at the end of the frame). A unit-length vector within 3 degrees of a smoothed light's
// direction (either way) is smoothed the same way: the hook passes the shown one, and after every world time tick (game
// thread, every frame where the time of day runs) it is set again. The instances' destructor (their vtable's first
// slot) drops them.
// The shadows: Unreal snaps each cascade's shadow map to a grid of 4-texel steps in light space, anchored at the world
// origin (FProjectedShadowInfo::SetupWholeSceneProjection, exe+0x1ACC450: Fmod of WorldToFace(center) by 8 / resolution;
// its comment: stable under camera translation and rotation only). A turning light swings that grid around the world
// origin, kilometres away, so the scene slides across it by texels every frame: the shadows shimmer all the time (ATN).
// FDirectionalLightSceneProxy::GetViewDependentWholeSceneProjectedShadowInitializer (vtable slot 20) is hooked: each
// cascade keeps an anchor that follows the camera (the view origin, FSceneView +0x60C, as GetShadowSplitBounds
// exe+0x23C8AF0 reads it) in whole steps of 1/64 of the cascade's size (a multiple of the snap step for any resolution
// from 512), which leaves the grid where it is, and WorldToLight (+0x10) gets a translation (light-space y, z: minus
// half the anchor's position modulo that step; the snapping and the shadow matrices both add it) that anchors the grid
// there. Camera moves keep the grid as before; a turn now swings it around the camera (v78: around the cascade's
// center, hundreds of metres ahead with Tales' one sun shadow map), so shadow edges near the camera barely crawl. The
// shadow window moves by under 64 texels.
// Both: the game turns the sun for about 2.2 s, then holds it about 5 s (ATN). A burst (the first change after half a
// second without one) starts the shown motion from where it is toward where the previous burst ended, spread over the
// time since the previous burst: an even speed (aiming at the moving target would speed up for 2.2 s, then slow down),
// one burst behind. Changes without such gaps start the next motion once the shown one is done. Turns above 10 degrees
// (time skips, cutscenes, new areas) are not smoothed.
constexpr size_t LIGHT_PROXY_LIGHT_TO_WORLD = 0x70;
constexpr size_t LIGHT_PROXY_TYPE = 0x140;
constexpr uint8_t DIRECTIONAL_LIGHT = 0;
constexpr float WORLD_MAX = 2097152.f;  // a directional light's position: -direction x WORLD_MAX, w 0 (GetLightPosition)
constexpr float DEGREES = 57.2957795f;  // per radian
// FMatrix (rows: the light's X, Y, Z axes, then its location) and FVector4, aligned as the game's.
struct alignas(16) Matrix : std::array<float, 16> {};
struct alignas(16) Vector4 : std::array<float, 4> {};
// The game's bursts of changes and the shown motion between them.
struct Burst {
  double start = 0.;
  double duration = 0.;  // 0: at the target
  double last_update = 0.;
  double last_burst = 0.;

  // A change at `now`; true when it starts a motion (a burst, or the shown motion done). A motion lasts 1-15 s, times
  // how many times slower the day passes (12 min steps at 100x slower).
  bool Update(double now) {
    const bool starts = now - last_update > 0.5 || duration == 0.;
    if (starts) {
      const double longest = 15. / std::max(Value(DAY_SPEED), .0001f);
      start = now;
      duration = std::clamp(now - last_burst, std::min(1., longest), longest);
      last_burst = now;
    }
    last_update = now;
    return starts;
  }

  // The day's speed changed: the motion and the time since the last burst stretched by `ratio` (the old speed over the
  // new), so the shown motion keeps its place and the next burst's motion fits the new steps.
  void Rescale(double now, double ratio) {
    if (duration > 0.) {
      const double progress = (now - start) / duration;
      duration *= ratio;
      start = now - (progress * duration);
    }
    last_burst = now - ((now - last_burst) * ratio);
  }

  // A turn shown at once.
  void Snap(double now) {
    duration = 0.;
    last_update = now;
    last_burst = now;
  }

  // How far the shown motion is at `now` (0-1; done: duration 0).
  float Progress(double now) {
    const float t = static_cast<float>(std::min((now - start) / duration, 1.));
    if (t >= 1.f) duration = 0.;
    return t;
  }
};
struct SunTransform {
  uint8_t* proxy = nullptr;  // null: unused
  Matrix from = {};
  Matrix aim = {};  // where the shown motion goes: the target when it started
  Matrix to = {};   // the newest target
  Matrix shown = {};
  Burst burst;
  // The game's steps, for the overlay: bursts since the light was added (or the last snap), the second one's time,
  // the light's direction where the previous burst ended, that burst's turn in degrees.
  double first_burst = 0.;
  uint32_t bursts = 0;
  std::array<float, 3> burst_end = {};
  float step = 0.f;
};
// A parameter collection's vector that follows the sun.
struct SunVector {
  uint8_t* instance = nullptr;  // UMaterialParameterCollectionInstance; null: unused
  uint64_t name = 0;            // the parameter's FName
  Vector4 from = {};
  Vector4 aim = {};
  Vector4 to = {};
  Vector4 shown = {};
  Burst burst;
};
struct SmoothSun {
  std::shared_mutex mutex;
  std::array<SunTransform, 4> lights;
  std::array<SunVector, 8> vectors;
};
static SmoothSun smooth_sun;
using SetTransform = void (*)(uint8_t* proxy, const float* light_to_world, const float* position);
static SetTransform set_transform = nullptr;
using SetCollectionVector = bool (*)(uint8_t* instance, uint64_t name, const float* value);
static SetCollectionVector set_collection_vector = nullptr;
using ActorTick = void (*)(uint8_t* actor, float delta_seconds);
static std::array<ActorTick, 2> world_time_tick = {};
using Destructor = void* (*)(uint8_t* object, uint32_t flags);
static Destructor directional_proxy_destructor = nullptr;
static Destructor collection_instance_destructor = nullptr;
using CascadeInitializer = bool (*)(uint8_t* proxy, const uint8_t* view, int32_t cascade, bool precomputed_lighting, uint8_t* initializer);
static CascadeInitializer cascade_initializer = nullptr;
// FWholeSceneProjectedShadowInitializer: PreShadowTranslation +0 (minus the cascade's center), WorldToLight +0x10 (rows),
// Scales +0x50 (1, 1 / radius, 1 / radius).
constexpr size_t INITIALIZER_WORLD_TO_LIGHT = 0x10;
constexpr size_t INITIALIZER_SCALES = 0x50;
constexpr size_t VIEW_ORIGIN = 0x60C;  // FSceneView
// Sun shadow layers (test): GetNumShadowMappedCascades (exe+0x23C61F0) takes the light's DynamicShadowCascades (proxy
// +0x194, the component's +0x3A4) with built lighting (the cvar r.Shadow.UnbuiltNumWholeSceneDynamicShadowCascades
// without: changed nothing, DLSS v79). Raised here for the next frame; the light's own comes back at 0.
constexpr size_t LIGHT_PROXY_CASCADES = 0x194;
struct LightCascades {
  uint8_t* proxy = nullptr;  // null: unused
  int32_t cascades = 0;      // the light's own
};
static std::array<LightCascades, 4> light_cascades;  // under smooth_sun.mutex
struct CascadeAnchor {
  std::array<double, 3> position = {};
  uint64_t used = 0;  // 0: unused
};
// Per cascade index, a few anchors: the closest one is used (the skit panels render other views in the same frame).
static std::array<std::array<CascadeAnchor, 4>, 8> cascade_anchors;  // under smooth_sun.mutex
static uint64_t cascade_anchor_uses = 0;

inline double Seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline float Dot3(const float* a, const float* b) {
  return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

// The shown rotation between two light transforms: each axis interpolated, then made orthonormal again (the game's
// bursts are about a degree); the location interpolated.
inline Matrix InterpolateLight(const Matrix& from, const Matrix& to, float t) {
  Matrix result = {};
  std::array<std::array<float, 3>, 3> axes = {};
  for (size_t row = 0; row < 3; ++row) {
    for (size_t column = 0; column < 3; ++column) {
      axes[row][column] = from[(row * 4) + column] + ((to[(row * 4) + column] - from[(row * 4) + column]) * t);
    }
  }
  const auto normalize = [](std::array<float, 3>* v) {
    const float length = std::sqrt(Dot3(v->data(), v->data()));
    for (float& component : *v) component /= length;
  };
  normalize(&axes[0]);
  const float along = Dot3(axes[1].data(), axes[0].data());
  for (size_t column = 0; column < 3; ++column) axes[1][column] -= axes[0][column] * along;
  normalize(&axes[1]);
  axes[2] = {(axes[0][1] * axes[1][2]) - (axes[0][2] * axes[1][1]), (axes[0][2] * axes[1][0]) - (axes[0][0] * axes[1][2]),
             (axes[0][0] * axes[1][1]) - (axes[0][1] * axes[1][0])};
  for (size_t row = 0; row < 3; ++row) {
    for (size_t column = 0; column < 3; ++column) result[(row * 4) + column] = axes[row][column];
  }
  for (size_t column = 0; column < 3; ++column) result[12 + column] = from[12 + column] + ((to[12 + column] - from[12 + column]) * t);
  result[15] = 1.f;
  return result;
}

// Where the shown motion is at `now` (the target once it is done).
inline Matrix ShownLight(SunTransform* light, double now) {
  if (light->burst.duration == 0.) return light->shown;
  return InterpolateLight(light->from, light->aim, light->burst.Progress(now));
}

// The same for a direction: interpolated, with the aim's length and w.
inline Vector4 ShownVector(SunVector* vector, double now) {
  if (vector->burst.duration == 0.) return vector->shown;
  const float t = vector->burst.Progress(now);
  Vector4 result = vector->aim;
  for (size_t column = 0; column < 3; ++column) result[column] = vector->from[column] + ((vector->aim[column] - vector->from[column]) * t);
  const float scale = std::sqrt(Dot3(vector->aim.data(), vector->aim.data()) / Dot3(result.data(), result.data()));
  for (size_t column = 0; column < 3; ++column) result[column] *= scale;
  return result;
}

inline Vector4 DirectionalLightPosition(const Matrix& light_to_world) {
  return {{-light_to_world[0] * WORLD_MAX, -light_to_world[1] * WORLD_MAX, -light_to_world[2] * WORLD_MAX, 0.f}};
}

static void OnSetTransform(uint8_t* proxy, const float* light_to_world, const float* position) {
  if (proxy[LIGHT_PROXY_TYPE] != DIRECTIONAL_LIGHT) {
    set_transform(proxy, light_to_world, position);
    return;
  }
  const bool smooth = Value(SMOOTH_SUN) != options[SMOOTH_SUN].game;
  Matrix target;
  std::memcpy(target.data(), light_to_world, sizeof(target));
  const double now = Seconds();
  std::optional<Matrix> shown;
  {
    const std::unique_lock lock(smooth_sun.mutex);
    auto light = std::ranges::find(smooth_sun.lights, proxy, &SunTransform::proxy);
    if (!smooth) {
      if (light != smooth_sun.lights.end()) *light = {};  // the game's transform from now on
    } else if (light == smooth_sun.lights.end()) {
      light = std::ranges::find(smooth_sun.lights, static_cast<uint8_t*>(nullptr), &SunTransform::proxy);
      if (light != smooth_sun.lights.end()) {
        // What the proxy shows now (set when it was added); a burst now, the game's 5 s before it.
        *light = {.proxy = proxy, .burst = {.last_update = now - 1., .last_burst = now - 5.}};
        std::memcpy(light->shown.data(), proxy + LIGHT_PROXY_LIGHT_TO_WORLD, sizeof(Matrix));
        light->aim = light->shown;
        light->to = light->shown;
      }
    }
    if (smooth && light != smooth_sun.lights.end()) {
      const Matrix current = ShownLight(&*light, now);
      if (!(Dot3(current.data(), target.data()) > 0.985f)) {  // beyond ~10 degrees (or an unset or broken transform)
        light->shown = target;
        light->aim = target;
        light->burst.Snap(now);
        light->bursts = 0;
      } else if (light->burst.Update(now)) {
        light->from = current;
        light->aim = light->to;
        const std::array<float, 3> end = {light->to[0], light->to[1], light->to[2]};
        if (light->bursts > 0) light->step = std::acos(std::clamp(Dot3(end.data(), light->burst_end.data()), -1.f, 1.f)) * DEGREES;
        if (light->bursts == 1) light->first_burst = now;  // timed from the second: the first may be the light's creation
        ++light->bursts;
        light->burst_end = end;
      }
      light->to = target;
      light->shown = ShownLight(&*light, now);
      shown = light->shown;
    }
  }
  if (!shown.has_value()) {
    set_transform(proxy, light_to_world, position);
    return;
  }
  set_transform(proxy, shown->data(), DirectionalLightPosition(*shown).data());
}

static bool OnCascadeInitializer(uint8_t* proxy, const uint8_t* view, int32_t cascade, bool precomputed_lighting, uint8_t* initializer) {
  if (!cascade_initializer(proxy, view, cascade, precomputed_lighting, initializer)) return false;
  {
    const std::unique_lock lock(smooth_sun.mutex);
    auto* cascades = reinterpret_cast<int32_t*>(proxy + LIGHT_PROXY_CASCADES);
    auto own = std::ranges::find(light_cascades, proxy, &LightCascades::proxy);
    if (const auto layers = static_cast<int32_t>(Value(SUN_SHADOW_LAYERS)); layers > 0) {
      if (own == light_cascades.end()) {
        own = std::ranges::find(light_cascades, static_cast<uint8_t*>(nullptr), &LightCascades::proxy);
        if (own != light_cascades.end()) *own = {.proxy = proxy, .cascades = *cascades};
      }
      if (own != light_cascades.end()) *cascades = std::max(own->cascades, layers);
    } else if (own != light_cascades.end()) {
      *cascades = own->cascades;
      *own = {};
    }
  }
  if (Value(SMOOTH_SUN) == options[SMOOTH_SUN].game || cascade < 0 || static_cast<size_t>(cascade) >= cascade_anchors.size()) return true;
  const auto* pre_shadow_translation = reinterpret_cast<const float*>(initializer);
  auto* world_to_light = reinterpret_cast<float*>(initializer + INITIALIZER_WORLD_TO_LIGHT);
  const float inverse_radius = reinterpret_cast<const float*>(initializer + INITIALIZER_SCALES)[1];
  if (!(inverse_radius > 0.f)) return true;
  const double radius = 1. / inverse_radius;
  const double step = radius / 64.;
  // Light-space axis `axis` of a world vector (back to world: the rotation's transpose).
  const auto light = [&](const std::array<double, 3>& v, size_t axis) {
    return (v[0] * world_to_light[axis]) + (v[1] * world_to_light[4 + axis]) + (v[2] * world_to_light[8 + axis]);
  };
  const auto* view_origin = reinterpret_cast<const float*>(view + VIEW_ORIGIN);
  std::array<double, 3> center = {view_origin[0], view_origin[1], view_origin[2]};
  if (!std::isfinite(center[0] + center[1] + center[2])) {
    center = {-pre_shadow_translation[0], -pre_shadow_translation[1], -pre_shadow_translation[2]};
  }
  const std::unique_lock lock(smooth_sun.mutex);
  // The closest anchor within 4 radii, else the longest unused one starts at the camera (a new area or a cut: one jump).
  const auto distance = [&](const CascadeAnchor& candidate) {
    const std::array<double, 3> offset = {center[0] - candidate.position[0], center[1] - candidate.position[1], center[2] - candidate.position[2]};
    return (candidate.used == 0 ? INFINITY : (offset[0] * offset[0]) + (offset[1] * offset[1]) + (offset[2] * offset[2]));
  };
  auto& anchors = cascade_anchors[cascade];
  auto* anchor = &*std::ranges::min_element(anchors, {}, distance);
  if (!(distance(*anchor) <= 16. * radius * radius)) {
    anchor = &*std::ranges::min_element(anchors, {}, &CascadeAnchor::used);
    anchor->position = center;
  }
  anchor->used = ++cascade_anchor_uses;
  const std::array<double, 3> offset = {center[0] - anchor->position[0], center[1] - anchor->position[1], center[2] - anchor->position[2]};
  // Toward the camera in whole steps across the light (the grid stays), any distance along it.
  const std::array<double, 3> move = {light(offset, 0), light(offset, 1) - std::fmod(light(offset, 1), step),
                                      light(offset, 2) - std::fmod(light(offset, 2), step)};
  for (size_t axis = 0; axis < 3; ++axis) {
    anchor->position[axis] += (move[0] * world_to_light[axis * 4]) + (move[1] * world_to_light[(axis * 4) + 1]) + (move[2] * world_to_light[(axis * 4) + 2]);
  }
  for (size_t axis = 1; axis < 3; ++axis) {
    world_to_light[12 + axis] += static_cast<float>(-std::fmod(light(anchor->position, axis), step) / 2.);
  }
  return true;
}

static void* OnDirectionalProxyDestroyed(uint8_t* proxy, uint32_t flags) {
  {
    const std::unique_lock lock(smooth_sun.mutex);
    for (auto& light : smooth_sun.lights) {
      if (light.proxy == proxy) light = {};
    }
    for (auto& own : light_cascades) {
      if (own.proxy == proxy) own = {};
    }
  }
  return directional_proxy_destructor(proxy, flags);
}

static bool OnSetCollectionVector(uint8_t* instance, uint64_t name, const float* value) {
  const bool smooth = Value(SMOOTH_SUN) != options[SMOOTH_SUN].game;
  Vector4 target;
  std::memcpy(target.data(), value, sizeof(target));
  const double now = Seconds();
  Vector4 shown = target;
  {
    const std::unique_lock lock(smooth_sun.mutex);
    auto vector = std::ranges::find_if(smooth_sun.vectors, [&](const SunVector& candidate) {
      return candidate.instance == instance && candidate.name == name;
    });
    const float length = std::sqrt(Dot3(target.data(), target.data()));
    float along = 0.f;  // the closest smoothed light's |cosine|
    for (const auto& light : smooth_sun.lights) {
      if (smooth && light.proxy != nullptr) along = std::max(along, std::abs(Dot3(light.to.data(), target.data())) / length);
    }
    // A direction (unit length: colors rarely are) within 3 degrees of a smoothed light's, either way.
    if (!smooth || !(std::abs(length - 1.f) < 0.02f && along > 0.9986f)) {
      if (vector != smooth_sun.vectors.end()) *vector = {};
    } else if (vector == smooth_sun.vectors.end()) {
      // The first change shown as it is; the next burst is smoothed.
      vector = std::ranges::find(smooth_sun.vectors, static_cast<uint8_t*>(nullptr), &SunVector::instance);
      if (vector != smooth_sun.vectors.end()) {
        *vector = {.instance = instance, .name = name, .aim = target, .to = target, .shown = target, .burst = {.last_update = now, .last_burst = now}};
      }
    } else if (target != vector->to) {
      const Vector4 current = ShownVector(&*vector, now);
      if (!(Dot3(current.data(), target.data()) > 0.985f * length * std::sqrt(Dot3(current.data(), current.data())))) {
        vector->shown = target;
        vector->aim = target;
        vector->burst.Snap(now);
      } else if (vector->burst.Update(now)) {
        vector->from = current;
        vector->aim = vector->to;
      }
      vector->to = target;
      vector->shown = ShownVector(&*vector, now);
      shown = vector->shown;
    } else {
      shown = vector->shown;
    }
  }
  return set_collection_vector(instance, name, shown.data());
}

static void* OnCollectionInstanceDestroyed(uint8_t* instance, uint32_t flags) {
  {
    const std::unique_lock lock(smooth_sun.mutex);
    for (auto& vector : smooth_sun.vectors) {
      if (vector.instance == instance) vector = {};
    }
  }
  return collection_instance_destructor(instance, flags);
}

// Day speed: TWorldTimeManager (the first global if set, else the second) advances its time by DeltaSeconds x its rate
// (+0x68; 50: a day in 28.8 min) every world tick (exe+0x5FE290); the rate is multiplied by the setting. A rate the
// game sets meanwhile (its Blueprint setters) becomes the game's. When the rate changes, the sun's motions are
// stretched to it at once.
constexpr size_t WORLD_TIME_RATE = 0x68;
// Both world time ticks: mov rdx, [first]; mov rcx, [second]; test rdx, rdx; mov rax, rcx; cmovne rax, rdx.
constexpr std::string_view WORLD_TIME_MANAGER = "48 8B 15 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 85 D2 48 8B C1 48 0F 45 C2";
static std::array<uint8_t* const*, 2> world_time_manager = {};
struct TimeRate {
  float game = 0.f;
  float written = 0.f;
};
static TimeRate time_rate;  // game thread

// After every world time tick (game thread): the smoothed vectors one frame further (the game's back with the setting
// off). Before it: the day's rate.
template <size_t CLASS>
static void OnWorldTimeTick(uint8_t* actor, float delta_seconds) {
  const double now = Seconds();
  if (uint8_t* manager = (world_time_manager[0] == nullptr ? nullptr : (*world_time_manager[0] != nullptr ? *world_time_manager[0] : *world_time_manager[1])); manager != nullptr) {
    auto* rate = reinterpret_cast<float*>(manager + WORLD_TIME_RATE);
    if (*rate != time_rate.written) time_rate.game = *rate;
    if (const float written = time_rate.game * Value(DAY_SPEED); written != time_rate.written) {
      if (written > 0.f && time_rate.written > 0.f) {
        const double ratio = static_cast<double>(time_rate.written) / written;
        const std::unique_lock lock(smooth_sun.mutex);
        for (auto& light : smooth_sun.lights) {
          light.burst.Rescale(now, ratio);
          light.bursts = 0;  // the overlay's step time starts again
        }
        for (auto& vector : smooth_sun.vectors) vector.burst.Rescale(now, ratio);
      }
      time_rate.written = written;
    }
    *rate = time_rate.written;
  }
  world_time_tick[CLASS](actor, delta_seconds);
  const bool smooth = Value(SMOOTH_SUN) != options[SMOOTH_SUN].game;
  std::array<SunVector, std::tuple_size_v<decltype(SmoothSun::vectors)>> moved = {};
  size_t count = 0;
  {
    const std::unique_lock lock(smooth_sun.mutex);
    for (auto& vector : smooth_sun.vectors) {
      if (vector.instance == nullptr || (smooth && vector.burst.duration == 0.)) continue;
      vector.shown = (smooth ? ShownVector(&vector, now) : vector.to);
      moved[count++] = vector;
      if (!smooth) vector = {};
    }
  }
  // Outside the lock; instances are destroyed on this thread (garbage collection), not meanwhile.
  for (size_t index = 0; index < count; ++index) set_collection_vector(moved[index].instance, moved[index].name, moved[index].shown.data());
}

// Every present: the shown light transforms one frame further toward their targets (at once with the setting off).
inline void UpdateSun() {
  if (set_transform == nullptr) return;
  const bool smooth = Value(SMOOTH_SUN) != options[SMOOTH_SUN].game;
  const double now = Seconds();
  std::array<std::pair<uint8_t*, Matrix>, std::tuple_size_v<decltype(SmoothSun::lights)>> moved = {};
  size_t count = 0;
  {
    const std::unique_lock lock(smooth_sun.mutex);
    for (auto& light : smooth_sun.lights) {
      if (light.proxy == nullptr || (light.burst.duration == 0. && smooth)) continue;
      light.shown = (smooth ? ShownLight(&light, now) : light.to);
      moved[count++] = {light.proxy, light.shown};
      if (!smooth) light = {};
    }
  }
  for (size_t index = 0; index < count; ++index) {
    const auto& [proxy, shown] = moved[index];
    set_transform(proxy, shown.data(), DirectionalLightPosition(shown).data());
  }
}

// For the overlay: the game's sun steps (the first light with a few) and the sky values following the sun.
inline std::string SunSteps() {
  const std::shared_lock lock(smooth_sun.mutex);
  const auto followed = std::ranges::count_if(smooth_sun.vectors, [](const SunVector& vector) { return vector.instance != nullptr; });
  const auto light = std::ranges::find_if(smooth_sun.lights, [](const SunTransform& candidate) { return candidate.proxy != nullptr && candidate.bursts > 2; });
  return std::format("Sun steps: {}; {} sky values follow the sun",
                     (light == smooth_sun.lights.end()
                          ? std::string("not enough seen yet")
                          : std::format("every {:.3f} s (average of {}), the last one {:.3f} degrees",
                                        (light->burst.last_burst - light->first_burst) / (light->bursts - 2), light->bursts - 2, light->step)),
                     followed);
}

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
// FDirectionalLightSceneProxy's constructor: lea rax, [vtable]; xor edx, edx; mov [rdi], rax; movzx eax, byte ptr
// [rbx + 0x380] (the component's bUsedAsAtmosphereSunLight); and al, 1; mov [rdi + 0x178], al.
constexpr std::string_view DIRECTIONAL_PROXY_VTABLE = "48 8D 05 ?? ?? ?? ?? 33 D2 48 89 07 0F B6 83 80 03 00 00 24 01 88 87 78 01 00 00";
// UMaterialParameterCollectionInstance's constructor: lea rax, [vtable]; mov [rbx], rax; xor eax, eax; then its
// fields (+0x40, +0x48, +0x60, +0x68; mov dword ptr [rbx + 0x70], -1).
constexpr std::string_view COLLECTION_INSTANCE_VTABLE = "48 8D 05 ?? ?? ?? ?? 48 89 03 33 C0 48 89 43 40 48 89 43 48 48 89 43 60 48 89 43 68 C7 43 70 FF FF FF FF";
struct VtableSlot {
  std::string_view pattern;  // starts with lea reg, [vtable] (displacement at +3)
  size_t slot;
  void** original;
  void* replacement;
};
// The directional light proxies' destructor and cascade initializer, the collection instances' destructor.
static std::array<code_patch::CodePatch, 3> sun_vtable_slots;

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

  const std::array<Hook, 9> wanted = {{
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
      // FLightSceneProxy::SetTransform: LightToWorld to +0x70, then its inverse.
      {.option = SMOOTH_SUN,
       .pattern = "48 83 EC 68 0F 28 02 48 8B C2 0F 11 41 70 4C 8B C9 0F 28 4A 10 0F 11 89 80 00 00 00 0F 28 42 20 0F 11 81 90 00 00 00",
       .original = reinterpret_cast<void**>(&set_transform),
       .replacement = reinterpret_cast<void*>(&OnSetTransform)},
      // UMaterialParameterCollectionInstance::SetVectorParameterValue: its collection's (+0x30) vector parameters
      // (+0x48, 0x28 each) searched for the name.
      {.option = SMOOTH_SUN,
       .pattern = "48 89 5C 24 18 48 89 54 24 10 57 48 83 EC 30 48 8B 41 30 4D 8B D0 48 8B D9 4C 63 40 50",
       .original = reinterpret_cast<void**>(&set_collection_vector),
       .replacement = reinterpret_cast<void*>(&OnSetCollectionVector)},
      // AWorldTimeActor::Tick: AActor::Tick, then the manager's hour (+0x5C) against the one sent (+0x328).
      {.option = SMOOTH_SUN,
       .pattern = "40 57 48 83 EC 30 48 8B F9 E8 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 85 D2 48 8B C1 48 0F 45 C2 "
                  "48 85 C0 0F 84 ?? ?? ?? ?? 48 85 D2 0F 29 74 24 20 48 8B C1 48 0F 45 C2 F3 0F 10 70 5C 0F 2E B7 28 03 00 00",
       .original = reinterpret_cast<void**>(&world_time_tick[0]),
       .replacement = reinterpret_cast<void*>(&OnWorldTimeTick<0>)},
      // The game's subclass: the same, keeping DeltaSeconds (xmm7) for its own counters.
      {.option = SMOOTH_SUN,
       .pattern = "40 57 48 83 EC 40 0F 29 74 24 30 48 8B F9 0F 29 7C 24 20 0F 28 F9 E8 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 48 8B 0D "
                  "?? ?? ?? ?? 48 85 D2 48 8B C1 48 0F 45 C2 48 85 C0 0F 84 ?? ?? ?? ?? 48 85 D2 48 8B C1 48 0F 45 C2 "
                  "F3 0F 10 70 5C 0F 2E B7 28 03 00 00",
       .original = reinterpret_cast<void**>(&world_time_tick[1]),
       .replacement = reinterpret_cast<void*>(&OnWorldTimeTick<1>)},
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
    hooks.emplace_back(hook.original, hook.replacement, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE);
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

  // Day speed: the world time manager's globals, read by the world time ticks (Smooth sun movement's hooks).
  world_time_manager = {};
  if (const auto sites = code_patch::FindPatterns(WORLD_TIME_MANAGER); sites.empty() || world_time_tick[0] == nullptr) {
    fail(DAY_SPEED, (sites.empty() ? "world time manager not found" : "needs the world time hooks (Smooth sun movement's)"));
  } else {
    for (size_t index = 0; index < world_time_manager.size(); ++index) {
      int32_t displacement = 0;
      std::memcpy(&displacement, sites[0] + 3 + (index * 7), sizeof(displacement));
      world_time_manager[index] = reinterpret_cast<uint8_t* const*>(sites[0] + 7 + (index * 7) + displacement);
    }
    options[DAY_SPEED].details = std::format("world time manager globals exe+0x{:X}, exe+0x{:X}",
                                               reinterpret_cast<const uint8_t*>(world_time_manager[0]) - image,
                                               reinterpret_cast<const uint8_t*>(world_time_manager[1]) - image);
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

  // Smooth sun: the vtable slots, or no smoothing (nothing freed must be written).
  const std::array<VtableSlot, sun_vtable_slots.size()> vtable_slots = {{
      {DIRECTIONAL_PROXY_VTABLE, 0, reinterpret_cast<void**>(&directional_proxy_destructor), reinterpret_cast<void*>(&OnDirectionalProxyDestroyed)},
      {DIRECTIONAL_PROXY_VTABLE, 20, reinterpret_cast<void**>(&cascade_initializer), reinterpret_cast<void*>(&OnCascadeInitializer)},
      {COLLECTION_INSTANCE_VTABLE, 0, reinterpret_cast<void**>(&collection_instance_destructor), reinterpret_cast<void*>(&OnCollectionInstanceDestroyed)},
  }};
  for (size_t index = 0; index < vtable_slots.size(); ++index) {
    auto& slot = sun_vtable_slots[index];
    slot = {};
    if (!options[SMOOTH_SUN].available) continue;
    const auto& wanted_slot = vtable_slots[index];
    const uint8_t* site = code_patch::FindPattern(wanted_slot.pattern, &slot.details);
    if (site == nullptr) {
      fail(SMOOTH_SUN, std::format("vtable {} not found ({})", index, slot.details));
      continue;
    }
    int32_t displacement = 0;
    std::memcpy(&displacement, site + 3, sizeof(displacement));
    auto* vtable = reinterpret_cast<uint8_t* const*>(site + 7 + displacement);
    if (vtable[wanted_slot.slot] < image || vtable[wanted_slot.slot] >= image + nt->OptionalHeader.SizeOfImage) {
      fail(SMOOTH_SUN, std::format("vtable slot {} already changed by another mod", index));
      continue;
    }
    *wanted_slot.original = vtable[wanted_slot.slot];
    slot.address = reinterpret_cast<uint8_t*>(const_cast<uint8_t**>(&vtable[wanted_slot.slot]));
    slot.original.assign(slot.address, slot.address + sizeof(void*));
    const auto hook = reinterpret_cast<uintptr_t>(wanted_slot.replacement);
    slot.replacement.resize(sizeof(hook));
    std::memcpy(slot.replacement.data(), &hook, sizeof(hook));
    if (code_patch::ApplyCodePatch(&slot, true)) {
      options[SMOOTH_SUN].details += std::format(", vtable slot {} {}", index, slot.details);
    } else {
      fail(SMOOTH_SUN, "patch failed: " + slot.details);
    }
  }
  if (!options[SMOOTH_SUN].available) {
    for (auto& slot : sun_vtable_slots) code_patch::ApplyCodePatch(&slot, false);
    // Leave the light and the sky to the game (the hooks pass everything through at the game's value).
    options[SMOOTH_SUN].value = options[SMOOTH_SUN].game;
    fail(SUN_SHADOW_LAYERS, "needs Smooth sun movement's hooks");
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
  for (auto& slot : sun_vtable_slots) code_patch::ApplyCodePatch(&slot, false);
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
