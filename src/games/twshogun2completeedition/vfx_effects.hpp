/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/hash.hpp"

// Fire & glow draws told apart by their effect, for their own brightness (Big Fires, Campfires) and the VFX
// effect test (Advanced: find an effect, set the slider it follows; ReShade.ini [Shogun 2 VFX effects]). A particle
// draw's effect constants (particle_volumetric.hlsl scripted_effect_info_constants_VS; the game ships no effect data)
// give an ID that stays the same between sessions. The game writes them with Map / Unmap (or UpdateSubresource) of
// constant buffers: read then once the buffer was seen at a particle draw, read back once before that.
namespace twshogun2::vfx_effects {

struct Effect {
  uint32_t id = 0;
  float emissive_min = 0.f;  // the particles' own light (negative lighting), random between these
  float emissive_max = 0.f;
  bool on_screen = false;  // drawn in the last frame
};

// The slider an effect follows.
enum class Kind : uint8_t {
  FIRE_AND_GLOW,
  BIG_FIRE,
  CAMPFIRE,
};

// Glow meshes (rigidglowcolour / rigidglowtexture) have no effect constants.
constexpr uint32_t GLOW_MESH_COLOR = 1;
constexpr uint32_t GLOW_MESH_TEXTURE = 2;

namespace internal {

constexpr uint32_t CONSTANTS_SIZE = 23 * 16;
// The ID: start / end colours (c0-c3), fade / thickness / emissive, animation and rotation (c16-c20), what every
// effect sets. Left out: the mid colours and their offsets (the remaster's colour curve: effects without one keep the
// previous effect's values, so v30's IDs changed with what else was on screen, ATN), the wind, fade with height.
constexpr uint32_t COLOURS_END = 4 * 16;
constexpr uint32_t DEFINITION_OFFSET = 16 * 16;
constexpr uint32_t DEFINITION_END = 21 * 16;
constexpr size_t LIGHTING_MIN = (16 * 4) + 3;  // float index of g_fade_info_thickness_and_lighting_min.w
constexpr size_t LIGHTING_MAX = (17 * 4) + 3;

constexpr char CONFIG_SECTION[] = "Shogun 2 VFX effects";

struct State {
  std::shared_mutex mutex;
  std::unordered_map<uint64_t, Effect> buffer_effects;  // constant buffers seen at particle draws (id 0: unread)
  std::unordered_map<uint64_t, const uint8_t*> mapped;
  std::vector<Effect> effects;  // drawn, first seen first: the test's effect number - 1
  std::unordered_set<uint32_t> drawn;  // this frame
  std::unordered_map<uint32_t, Kind> kinds;  // not listed: Fire & Glow
  uint32_t read_backs = 0;
};

static State state;
// Set at the first particle draw that needs the effect: until then a Map costs one atomic load.
static std::atomic<bool> tracking = false;
// mapped is not empty: each Unmap checks it before the lock.
static std::atomic<bool> any_mapped = false;
// Set by the test's first Drawn: until then OnPresent does nothing.
static std::atomic<bool> testing = false;

inline Effect Identify(const uint8_t* constants) {
  uint32_t crc = renodx::utils::hash::UpdateCRC32(0xFFFFFFFFu, constants, COLOURS_END);
  crc = renodx::utils::hash::UpdateCRC32(crc, constants + DEFINITION_OFFSET, DEFINITION_END - DEFINITION_OFFSET);
  std::array<float, CONSTANTS_SIZE / 4> floats;
  std::memcpy(floats.data(), constants, CONSTANTS_SIZE);
  return {
      .id = renodx::utils::hash::FinalizeCRC32(crc),
      .emissive_min = std::max(0.f, -floats[LIGHTING_MIN]),
      .emissive_max = std::max(0.f, -floats[LIGHTING_MAX]),
  };
}

// UpdateSubresource of a buffer seen at a particle draw (immediate or deferred context).
inline void Updated(reshade::api::resource resource, const void* data, uint64_t offset, uint64_t size) {
  if (!tracking.load(std::memory_order_relaxed) || data == nullptr || offset != 0 || size < CONSTANTS_SIZE) return;
  const std::unique_lock lock(state.mutex);
  const auto found = state.buffer_effects.find(resource.handle);
  if (found == state.buffer_effects.end()) return;
  found->second = Identify(static_cast<const uint8_t*>(data));
}

}  // namespace internal

// Default kinds (ATN's VFX effect test, 2026-10-15: burning buildings, castles and walls; campfires), and ReShade.ini's
// lists (hex IDs), which replace a kind's defaults once set.
constexpr std::array<std::pair<uint32_t, Kind>, 5> DEFAULT_KINDS = {{
    {GLOW_MESH_COLOR, Kind::BIG_FIRE},
    {0xFC75D250, Kind::BIG_FIRE},
    {0xDCB6C2CB, Kind::BIG_FIRE},
    {0xFF972FE7, Kind::BIG_FIRE},
    {0x73B8E4C8, Kind::CAMPFIRE},
}};
constexpr std::array<std::pair<Kind, const char*>, 2> CONFIG_KEYS = {{
    {Kind::BIG_FIRE, "BigFires"},
    {Kind::CAMPFIRE, "Campfires"},
}};

inline void LoadKinds() {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.kinds = {DEFAULT_KINDS.begin(), DEFAULT_KINDS.end()};
  for (const auto& [kind, key] : CONFIG_KEYS) {
    std::array<char, 1024> text = {};
    size_t size = text.size();
    if (!reshade::get_config_value(nullptr, internal::CONFIG_SECTION, key, text.data(), &size)) continue;
    std::erase_if(internal::state.kinds, [kind](const auto& entry) { return entry.second == kind; });
    const std::string_view list(text.data());
    for (size_t at = 0; at < list.size();) {
      const size_t end = std::min(list.find(' ', at), list.size());
      uint32_t id = 0;
      if (std::from_chars(list.data() + at, list.data() + end, id, 16).ec == std::errc()) {
        internal::state.kinds[id] = kind;
      }
      at = end + 1;
    }
  }
}

inline Kind KindOf(uint32_t id) {
  const std::shared_lock lock(internal::state.mutex);
  const auto found = internal::state.kinds.find(id);
  return (found == internal::state.kinds.end()) ? Kind::FIRE_AND_GLOW : found->second;
}

// The test's choice for an effect, saved to ReShade.ini.
inline void SetKind(uint32_t id, Kind kind) {
  const std::unique_lock lock(internal::state.mutex);
  if (kind == Kind::FIRE_AND_GLOW) {
    internal::state.kinds.erase(id);
  } else {
    internal::state.kinds[id] = kind;
  }
  for (const auto& [listed_kind, key] : CONFIG_KEYS) {
    std::string list;
    for (const auto& [listed_id, entry_kind] : internal::state.kinds) {
      if (entry_kind == listed_kind) list += std::format("{}{:08X}", list.empty() ? "" : " ", listed_id);
    }
    reshade::set_config_value(nullptr, internal::CONFIG_SECTION, key, list.c_str());
  }
}

inline void OnMapBufferRegion(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t,
                              reshade::api::map_access access, void** data) {
  if (!internal::tracking.load(std::memory_order_relaxed) || data == nullptr || *data == nullptr || offset != 0
      || access == reshade::api::map_access::read_only) {
    return;
  }
  {
    const std::shared_lock lock(internal::state.mutex);
    if (!internal::state.buffer_effects.contains(resource.handle)) return;
  }
  const std::unique_lock lock(internal::state.mutex);
  internal::state.mapped[resource.handle] = static_cast<const uint8_t*>(*data);
  internal::any_mapped.store(true, std::memory_order_relaxed);
}

inline void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  if (!internal::any_mapped.load(std::memory_order_relaxed)) return;
  const std::unique_lock lock(internal::state.mutex);
  const auto found = internal::state.mapped.find(resource.handle);
  if (found == internal::state.mapped.end()) return;
  internal::state.buffer_effects[resource.handle] = internal::Identify(found->second);
  internal::state.mapped.erase(found);
  internal::any_mapped.store(!internal::state.mapped.empty(), std::memory_order_relaxed);
}

inline bool OnUpdateBufferRegion(reshade::api::device*, const void* data, reshade::api::resource resource,
                                 uint64_t offset, uint64_t size) {
  internal::Updated(resource, data, offset, size);
  return false;
}

inline bool OnUpdateBufferRegionCommand(reshade::api::command_list*, const void* data, reshade::api::resource resource,
                                        uint64_t offset, uint64_t size) {
  internal::Updated(resource, data, offset, size);
  return false;
}

// Present: the test's effects on screen in the last frame.
inline void OnPresent() {
  if (!internal::testing.load(std::memory_order_relaxed)) return;
  const std::unique_lock lock(internal::state.mutex);
  for (auto& effect : internal::state.effects) {
    effect.on_screen = internal::state.drawn.contains(effect.id);
  }
  internal::state.drawn.clear();
}

// A fire & glow draw (glow meshes: GLOW_MESH_COLOR / _TEXTURE), for the test.
inline void Drawn(const Effect& effect) {
  internal::testing.store(true, std::memory_order_relaxed);
  const std::unique_lock lock(internal::state.mutex);
  internal::state.drawn.insert(effect.id);
  for (const auto& known : internal::state.effects) {
    if (known.id == effect.id) return;
  }
  internal::state.effects.push_back(effect);
}

// A particle draw's effect (its constant buffer at the vertex shader's slot), ID 0 if unknown.
inline Effect DrawnParticles(reshade::api::command_list* cmd_list, uint32_t slot) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  context->VSGetConstantBuffers(slot, 1, buffer.GetAddressOf());
  if (buffer == nullptr) return {};
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (desc.ByteWidth < internal::CONSTANTS_SIZE) return {};
  const auto handle = reinterpret_cast<uint64_t>(static_cast<ID3D11Resource*>(buffer.Get()));

  Effect effect;
  bool seen = false;
  {
    const std::shared_lock lock(internal::state.mutex);
    if (const auto found = internal::state.buffer_effects.find(handle); found != internal::state.buffer_effects.end()) {
      effect = found->second;
      seen = true;
    }
  }
  if (!seen) {
    const std::unique_lock lock(internal::state.mutex);
    effect = internal::state.buffer_effects.try_emplace(handle).first->second;
    internal::tracking.store(true, std::memory_order_relaxed);
  }

  // Not written since it was first seen: read back once (the immediate context only; this waits for the GPU).
  if (effect.id == 0 && context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()))) return {};
    context->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
    effect = internal::Identify(static_cast<const uint8_t*>(mapped.pData));
    context->Unmap(staging.Get(), 0);

    const std::unique_lock lock(internal::state.mutex);
    internal::state.buffer_effects[handle] = effect;
    ++internal::state.read_backs;
  }
  return effect;
}

// The test's effect number (1: the first drawn) as its ID, 0 if none yet.
inline uint32_t IdOf(int number) {
  const std::shared_lock lock(internal::state.mutex);
  if (number < 1 || number > static_cast<int>(internal::state.effects.size())) return 0;
  return internal::state.effects[number - 1].id;
}

inline std::vector<Effect> Effects() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.effects;
}

inline uint32_t ReadBacks() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.read_backs;
}

}  // namespace twshogun2::vfx_effects
