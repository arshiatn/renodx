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
#include <mutex>
#include <shared_mutex>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./buffer_writes.hpp"

// Shadow Map Size above the game's highest (row, Experimental): the sun's shadow maps at twice their size. The game's code
// holds no size to change (no immediates, no table: wh3_batch6/p7/size_code.txt, size_table.txt), so the size goes in where
// D3D sees it, and every reader of it follows (the run of 2026-11-10 for the numbers, the shader dump's bytes for the readers):
// - the maps: two 4096 x 4096 [4] R32_TYPELESS depth arrays (t_dynamic_objects_shadowmap_array, t_static_objects_shadowmap_array)
//   at the highest Shadows setting, made twice as wide and high at their creation (create_resource). A lower setting makes
//   other sizes and is left alone.
// - their writers: every draw into them, known by the bound depth, not by its shader (13 caster vertex shaders in the traces,
//   some 25 shadow techniques in the exe): the game's 4096 viewport and a 4096 scissor rect become the maps' size while one
//   of the maps is the depth target, and go back when another target is bound with the game's viewport still in place.
// - shadowmap_PS's g_vHardShadowBufferSize (4096, 4096, 1 / 4096, 1 / 4096; all 454 readers take it from there: `ld` at
//   uv x size in the fog, gather weights and offsets in the sun's mask and the water): the buffer is 640 bytes in all 454
//   and no other constant buffer of the dump is, so it is known by its size and written where the game uploads it.
// - static_shadowmap_CB's buffer_size (C883E3FA, the kept maps' copy) and the `x 2` of the two half-size copies (800F7F7F,
//   D5DCC85F) are in those three pixel shaders: our copies of them read the map's own size (shaders/shadowcopy*).
// Not reached: what the game's own code computes from its 4096 (the sample bias, the maps' texel snapping, culling by a
// caster's size in texels): each stays right for a 4096 map, so shadows may hold a little more bias than they need.
namespace twwarhammer3::shadow_map_size {

constexpr uint32_t GAME_SIZE = 4096;
constexpr uint32_t SCALE = 2;
constexpr uint32_t SLICES = 4;
constexpr uint64_t CONSTANTS_BYTES = 640;

static float row = 0.f;  // 0: the game's maps; 1: twice their size, from the next time the game makes them
// The maps made larger and still alive (their resources, by address).
static std::shared_mutex maps_mutex;
static std::vector<uint64_t> maps;
static std::atomic_uint32_t live_maps = 0;
static buffer_writes::Buffers constants;
static std::atomic_bool constants_logged = false;
// The context whose viewport this thread last made larger: only that one is ever made smaller again.
static thread_local ID3D11DeviceContext* scaled_context = nullptr;

// One of the game's cascade arrays at `size`.
inline bool IsMap(const reshade::api::resource_desc& desc, uint32_t size) {
  return desc.type == reshade::api::resource_type::texture_2d && desc.texture.width == size && desc.texture.height == size
         && desc.texture.depth_or_layers == SLICES && desc.texture.format == reshade::api::format::r32_typeless
         && (desc.usage & reshade::api::resource_usage::depth_stencil) != 0;
}

// g_vHardShadowBufferSize (the buffer's first four floats) at the larger maps' size while it holds the game's.
inline bool WriteSize(uint8_t* shadow_constants) {
  std::array<float, 4> size = {};
  std::memcpy(size.data(), shadow_constants, sizeof(size));
  if (size[0] != static_cast<float>(GAME_SIZE) || size[1] != static_cast<float>(GAME_SIZE)) return false;
  constexpr float LARGER = static_cast<float>(GAME_SIZE * SCALE);
  size = {LARGER, LARGER, 1.f / LARGER, 1.f / LARGER};
  std::memcpy(shadow_constants, size.data(), sizeof(size));
  return true;
}

// Whether the context draws into one of the larger maps.
inline bool DrawsIntoMap(ID3D11DeviceContext* native) {
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
  native->OMGetRenderTargets(0, nullptr, depth.GetAddressOf());
  if (!depth) return false;
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;
  depth->GetResource(resource.GetAddressOf());
  const std::shared_lock lock(maps_mutex);
  return std::ranges::find(maps, reinterpret_cast<uint64_t>(resource.Get())) != maps.end();
}

// The context's first viewport and scissor rect from the game's size to the larger maps' (`larger`), or back.
inline void Rescale(ID3D11DeviceContext* native, bool larger) {
  const uint32_t from = (larger ? GAME_SIZE : GAME_SIZE * SCALE);
  const uint32_t to = (larger ? GAME_SIZE * SCALE : GAME_SIZE);
  UINT viewport_count = 1;
  D3D11_VIEWPORT viewport = {};
  native->RSGetViewports(&viewport_count, &viewport);
  if (viewport_count != 0 && viewport.TopLeftX == 0.f && viewport.TopLeftY == 0.f && viewport.Width == static_cast<float>(from)
      && viewport.Height == static_cast<float>(from)) {
    viewport.Width = static_cast<float>(to);
    viewport.Height = static_cast<float>(to);
    native->RSSetViewports(1, &viewport);
    scaled_context = (larger ? native : nullptr);
  }
  UINT rect_count = 1;
  D3D11_RECT rect = {};
  native->RSGetScissorRects(&rect_count, &rect);
  if (rect_count != 0 && rect.left == 0 && rect.top == 0 && rect.right == static_cast<LONG>(from) && rect.bottom == static_cast<LONG>(from)) {
    rect.right = static_cast<LONG>(to);
    rect.bottom = static_cast<LONG>(to);
    native->RSSetScissorRects(1, &rect);
  }
}

// After the game bound targets: its 4096 viewport fits no larger map, and a viewport made larger fits no other target.
inline void TargetsBound(ID3D11DeviceContext* native) {
  if (live_maps.load(std::memory_order_relaxed) == 0) return;
  if (DrawsIntoMap(native)) {
    Rescale(native, true);
  } else if (scaled_context == native) {
    Rescale(native, false);
    scaled_context = nullptr;
  }
}

// After the game set a viewport or a scissor rect of `width` x `height`.
inline void AreaBound(ID3D11DeviceContext* native, float width, float height) {
  if (live_maps.load(std::memory_order_relaxed) == 0 || width != static_cast<float>(GAME_SIZE) || height != static_cast<float>(GAME_SIZE)) return;
  if (DrawsIntoMap(native)) {
    Rescale(native, true);
  }
}

static bool OnCreateResource(reshade::api::device*, reshade::api::resource_desc& desc, reshade::api::subresource_data*, reshade::api::resource_usage) {
  if (row == 0.f || !IsMap(desc, GAME_SIZE)) return false;
  desc.texture.width = GAME_SIZE * SCALE;
  desc.texture.height = GAME_SIZE * SCALE;
  return true;
}

static void OnInitResource(reshade::api::device*, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*,
                           reshade::api::resource_usage, reshade::api::resource resource) {
  buffer_writes::Add(&constants, CONSTANTS_BYTES, desc, resource);
  if (!IsMap(desc, GAME_SIZE * SCALE)) return;
  const std::unique_lock lock(maps_mutex);
  if (std::ranges::find(maps, resource.handle) != maps.end()) return;
  maps.push_back(resource.handle);
  if (live_maps.fetch_add(1, std::memory_order_relaxed) == 0) {
    renodx::utils::log::i(std::format("[Warhammer III] Shadow Map Size: the game's shadow maps made at {} x {} [{}].", GAME_SIZE * SCALE, GAME_SIZE * SCALE, SLICES));
  }
}

static void OnDestroyResource(reshade::api::device*, reshade::api::resource resource) {
  buffer_writes::Remove(&constants, resource);
  if (live_maps.load(std::memory_order_relaxed) == 0) return;
  const std::unique_lock lock(maps_mutex);
  if (std::erase(maps, resource.handle) != 0) {
    live_maps.fetch_sub(1, std::memory_order_relaxed);
  }
}

static void OnMapBufferRegion(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t, reshade::api::map_access access, void** data) {
  buffer_writes::Mapped(&constants, live_maps.load(std::memory_order_relaxed) != 0, resource, offset, access, data);
}

static void LogConstants(const char* how) {
  if (!constants_logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i(std::format("[Warhammer III] Shadow Map Size: the shared shadow constants say {} ({}).", GAME_SIZE * SCALE, how));
  }
}

static void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  const bool written = buffer_writes::Unmapped(&constants, resource, [](uint8_t* shadow_constants) {
    // The maps gone since the map: the game's size stays.
    return live_maps.load(std::memory_order_relaxed) != 0 && WriteSize(shadow_constants);
  });
  if (written) {
    LogConstants("map");
  }
}

// The game's update of the shadow constants, whole or past the size, replaced by the same with the larger maps' size.
inline bool UpdateWithSize(ID3D11DeviceContext* native, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  if (live_maps.load(std::memory_order_relaxed) == 0) return false;
  const bool sent = buffer_writes::Update<CONSTANTS_BYTES>(constants, native, data, resource, offset, size, 16, WriteSize);
  if (sent) {
    LogConstants("update");
  }
  return sent;
}

static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  if (live_maps.load(std::memory_order_relaxed) == 0 || !constants.known.contains(resource.handle)) return false;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
  reinterpret_cast<ID3D11Device*>(device->get_native())->GetImmediateContext(immediate.GetAddressOf());
  return UpdateWithSize(immediate.Get(), data, resource, offset, size);
}

static bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  return UpdateWithSize(reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()), data, resource, offset, size);
}

static void OnBindRenderTargetsAndDepthStencil(reshade::api::command_list* cmd_list, uint32_t, const reshade::api::resource_view*, reshade::api::resource_view) {
  if (live_maps.load(std::memory_order_relaxed) == 0) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  TargetsBound(native);
}

static void OnBindViewports(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, const reshade::api::viewport* viewports) {
  if (live_maps.load(std::memory_order_relaxed) == 0 || first != 0 || count == 0) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  AreaBound(native, viewports[0].width, viewports[0].height);
}

static void OnBindScissorRects(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, const reshade::api::rect* rects) {
  if (live_maps.load(std::memory_order_relaxed) == 0 || first != 0 || count == 0) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  AreaBound(native, static_cast<float>(rects[0].right - rects[0].left), static_cast<float>(rects[0].bottom - rects[0].top));
}

// At the addon's attach, once its settings are read. With the row off then, nothing is registered: no call of the game's
// passes by here, and turning the row on needs a restart anyway (the maps are made at the game's start).
static void Install() {
  if (row == 0.f) return;
  reshade::register_event<reshade::addon_event::create_resource>(OnCreateResource);
  reshade::register_event<reshade::addon_event::init_resource>(OnInitResource);
  reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
  reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindRenderTargetsAndDepthStencil);
  reshade::register_event<reshade::addon_event::bind_viewports>(OnBindViewports);
  reshade::register_event<reshade::addon_event::bind_scissor_rects>(OnBindScissorRects);
}

static void Uninstall() {
  reshade::unregister_event<reshade::addon_event::create_resource>(OnCreateResource);
  reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResource);
  reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);
  reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindRenderTargetsAndDepthStencil);
  reshade::unregister_event<reshade::addon_event::bind_viewports>(OnBindViewports);
  reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(OnBindScissorRects);
  buffer_writes::Clear(&constants);
  // The maps stay the game's own objects, as large as they were made; an image kept loaded tracks none.
  const std::unique_lock lock(maps_mutex);
  maps.clear();
  live_maps.store(0, std::memory_order_relaxed);
}

}  // namespace twwarhammer3::shadow_map_size
