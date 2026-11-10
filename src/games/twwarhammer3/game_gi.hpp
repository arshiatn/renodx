/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <shared_mutex>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/data.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./buffer_writes.hpp"

// The game's Global Illumination off while ours is on (Light Bounce, any method): the flag every reader takes,
// g_gi_enabled, goes to 0 where the game uploads it. lighting_VS_PS is 480 bytes in all 1228 shaders that have it,
// g_gi_enabled at 180 in all, and no other constant buffer in the dump is 480 bytes (v37_cbuffer_sizes.py). 1019 shaders
// read the flag; with it 0 they take the game's own path without GI.
// The game's engine value gi_enabled is never written: its GI stage makes its textures only while the value is not 0 (at
// its construction and at every graphics change), and runs whenever it is not 0. Set to 0 and back in a battle, the
// stage ran without textures (ATN's crash 2026-11-06: a read of null + 0x18 at Warhammer3.exe+0x1A45803).
namespace twwarhammer3::game_gi {

constexpr uint64_t LIGHTING_BYTES = 480;
constexpr uint64_t FLAG_OFFSET = 180;

static std::atomic_bool off = false;
static std::atomic_bool logged = false;
// The game's lighting buffers with their open maps (buffer_writes.hpp).
static buffer_writes::Buffers lighting;
static auto& lighting_buffers = lighting.known;
static auto& open_map_count = lighting.open_map_count;

static void LogOnce(const char* how) {
  if (!logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i(std::format("[Warhammer III] First run: the game's Global Illumination flag off in its lighting constants ({}).", how));
  }
}

static void OnInitResource(reshade::api::device*, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*,
                           reshade::api::resource_usage, reshade::api::resource resource) {
  buffer_writes::Add(&lighting, LIGHTING_BYTES, desc, resource);
}

static void OnDestroyResource(reshade::api::device*, reshade::api::resource resource) {
  buffer_writes::Remove(&lighting, resource);
}

static void OnMapBufferRegion(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t, reshade::api::map_access access, void** data) {
  buffer_writes::Mapped(&lighting, off.load(std::memory_order_relaxed), resource, offset, access, data);
}

static void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  const bool written = buffer_writes::Unmapped(&lighting, resource, [](uint8_t* constants) {
    // Turned off since the map: the game's flag stays.
    if (!off.load(std::memory_order_relaxed)) return false;
    constexpr uint32_t GI_OFF = 0u;
    std::memcpy(constants + FLAG_OFFSET, &GI_OFF, sizeof(GI_OFF));
    return true;
  });
  if (written) {
    LogOnce("map");
  }
}

// The game's update, whole or past the flag, replaced by the same with the flag 0.
static bool UpdateWithFlagOff(ID3D11DeviceContext* native, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  if (!off.load(std::memory_order_relaxed)) return false;
  const bool sent = buffer_writes::Update<LIGHTING_BYTES>(lighting, native, data, resource, offset, size, FLAG_OFFSET + 4, [](uint8_t* constants) {
    constexpr uint32_t GI_OFF = 0u;
    std::memcpy(constants + FLAG_OFFSET, &GI_OFF, sizeof(GI_OFF));
    return true;
  });
  if (sent) {
    LogOnce("update");
  }
  return sent;
}

static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  if (!off.load(std::memory_order_relaxed)) return false;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
  reinterpret_cast<ID3D11Device*>(device->get_native())->GetImmediateContext(immediate.GetAddressOf());
  return UpdateWithFlagOff(immediate.Get(), data, resource, offset, size);
}

static bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  return UpdateWithFlagOff(reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()), data, resource, offset, size);
}

static void Install() {
  reshade::register_event<reshade::addon_event::init_resource>(OnInitResource);
  reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
  reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);
}

static void Uninstall() {
  off.store(false, std::memory_order_relaxed);
  reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResource);
  reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);
  buffer_writes::Clear(&lighting);
}

}  // namespace twwarhammer3::game_gi
