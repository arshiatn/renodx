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

// A write map of a lighting buffer that has not been unmapped. A context is one thread's between a map and its unmap,
// and a buffer can be mapped on several deferred contexts at once: one pointer a thread.
struct OpenMap {
  DWORD thread = 0;
  uint8_t* data = nullptr;
};

static std::atomic_bool off = false;
static std::atomic_bool logged = false;
// The game's lighting buffers, from init_resource to destroy_resource, each with its open maps.
static renodx::utils::data::ParallelFlatHashMap<uint64_t, std::vector<OpenMap>, std::shared_mutex> lighting_buffers;
// Every buffer's unmap passes here: nothing is looked up while no map is open.
static std::atomic_uint32_t open_map_count = 0;

static void LogOnce(const char* how) {
  if (!logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i(std::format("[Warhammer III] First run: the game's Global Illumination flag off in its lighting constants ({}).", how));
  }
}

static void OnInitResource(reshade::api::device*, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*,
                           reshade::api::resource_usage, reshade::api::resource resource) {
  if (desc.type != reshade::api::resource_type::buffer || desc.buffer.size != LIGHTING_BYTES
      || (desc.usage & reshade::api::resource_usage::constant_buffer) == 0) {
    return;
  }
  lighting_buffers.lazy_emplace_l(
      resource.handle,
      // Made again at an address whose destroy was never seen: the old buffer's pointers are not this one's.
      [](auto& entry) {
        open_map_count.fetch_sub(static_cast<uint32_t>(entry.second.size()), std::memory_order_relaxed);
        entry.second.clear();
      },
      [&](const auto& construct) { construct(resource.handle, std::vector<OpenMap>()); });
}

static void OnDestroyResource(reshade::api::device*, reshade::api::resource resource) {
  lighting_buffers.erase_if(resource.handle, [](const auto& entry) {
    open_map_count.fetch_sub(static_cast<uint32_t>(entry.second.size()), std::memory_order_relaxed);
    return true;
  });
}

static void OnMapBufferRegion(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t, reshade::api::map_access access, void** data) {
  if (!off.load(std::memory_order_relaxed) || offset != 0 || data == nullptr || *data == nullptr || access == reshade::api::map_access::read_only) return;
  lighting_buffers.modify_if(resource.handle, [data](auto& entry) {
    auto& open_maps = entry.second;
    const DWORD thread = GetCurrentThreadId();
    // One pointer for a thread: a map whose unmap never came (a lost device) must not leave its old pointer for the
    // next unmap to write through.
    if (const auto own = std::ranges::find(open_maps, thread, &OpenMap::thread); own != open_maps.end()) {
      own->data = static_cast<uint8_t*>(*data);
    } else {
      open_maps.push_back({.thread = thread, .data = static_cast<uint8_t*>(*data)});
      open_map_count.fetch_add(1, std::memory_order_relaxed);
    }
  });
}

// Before the game's unmap: the pointer its map gave is still the buffer's memory.
static void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  if (open_map_count.load(std::memory_order_relaxed) == 0) return;
  bool written = false;
  lighting_buffers.modify_if(resource.handle, [&written](auto& entry) {
    auto& open_maps = entry.second;
    if (open_maps.empty()) return;
    const auto own = std::ranges::find(open_maps, GetCurrentThreadId(), &OpenMap::thread);
    if (own == open_maps.end()) {
      // Unmapped on another thread than mapped (or mapped before the option came on): which map this ends is unknown,
      // so no pointer of this buffer is written through any more.
      open_map_count.fetch_sub(static_cast<uint32_t>(open_maps.size()), std::memory_order_relaxed);
      open_maps.clear();
      return;
    }
    // Turned off since the map: the game's flag stays.
    if (off.load(std::memory_order_relaxed)) {
      constexpr uint32_t GI_OFF = 0u;
      std::memcpy(own->data + FLAG_OFFSET, &GI_OFF, sizeof(GI_OFF));
      written = true;
    }
    open_maps.erase(own);
    open_map_count.fetch_sub(1, std::memory_order_relaxed);
  });
  if (written) {
    LogOnce("map");
  }
}

// The game's update, whole or past the flag, replaced by the same with the flag 0 on its own context (no event).
static bool UpdateWithFlagOff(ID3D11DeviceContext* native, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  if (!off.load(std::memory_order_relaxed) || native == nullptr || data == nullptr || offset != 0 || size < FLAG_OFFSET + 4) return false;
  if (!lighting_buffers.contains(resource.handle)) return false;
  // Through ReShade's context the update would come back here.
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  std::array<uint8_t, LIGHTING_BYTES> patched = {};
  std::memcpy(patched.data(), data, static_cast<size_t>(std::min(size, LIGHTING_BYTES)));
  constexpr uint32_t GI_OFF = 0u;
  std::memcpy(patched.data() + FLAG_OFFSET, &GI_OFF, sizeof(GI_OFF));
  const D3D11_BOX part = {.left = 0, .top = 0, .front = 0, .right = static_cast<UINT>(std::min(size, LIGHTING_BYTES)), .bottom = 1, .back = 1};
  native->UpdateSubresource(reinterpret_cast<ID3D11Resource*>(resource.handle), 0, (size < LIGHTING_BYTES) ? &part : nullptr, patched.data(), 0, 0);
  LogOnce("update");
  return true;
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
  // An image kept loaded after the detach holds no buffer and no pointer.
  lighting_buffers.clear();
  open_map_count.store(0, std::memory_order_relaxed);
}

}  // namespace twwarhammer3::game_gi
