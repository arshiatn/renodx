/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <shared_mutex>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/data.hpp"
#include "../../utils/directx.hpp"

// A value in one of the game's constant buffers, rewritten where the game uploads it (game_gi.hpp: the lighting constants'
// Global Illumination flag; shadow_map_size.hpp: the shadow constants' map size). The buffers are known by their byte size,
// which must be that constant buffer's alone in the shader dump; a map's pointer is written just before the game's unmap,
// an update is sent again with the value changed.
namespace twwarhammer3::buffer_writes {

// A write map that has not been unmapped. A context is one thread's between a map and its unmap, and a buffer can be mapped
// on several deferred contexts at once: one pointer a thread.
struct OpenMap {
  DWORD thread = 0;
  uint8_t* data = nullptr;
};

// The game's buffers of one size, from init_resource to destroy_resource, each with its open maps.
struct Buffers {
  renodx::utils::data::ParallelFlatHashMap<uint64_t, std::vector<OpenMap>, std::shared_mutex> known;
  // Every buffer's unmap passes by: nothing is looked up while no map is open.
  std::atomic_uint32_t open_map_count = 0;
};

inline void Add(Buffers* buffers, uint64_t bytes, const reshade::api::resource_desc& desc, reshade::api::resource resource) {
  if (desc.type != reshade::api::resource_type::buffer || desc.buffer.size != bytes
      || (desc.usage & reshade::api::resource_usage::constant_buffer) == 0) {
    return;
  }
  buffers->known.lazy_emplace_l(
      resource.handle,
      // Made again at an address whose destroy was never seen: the old buffer's pointers are not this one's.
      [buffers](auto& entry) {
        buffers->open_map_count.fetch_sub(static_cast<uint32_t>(entry.second.size()), std::memory_order_relaxed);
        entry.second.clear();
      },
      [&](const auto& construct) { construct(resource.handle, std::vector<OpenMap>()); });
}

inline void Remove(Buffers* buffers, reshade::api::resource resource) {
  buffers->known.erase_if(resource.handle, [buffers](const auto& entry) {
    buffers->open_map_count.fetch_sub(static_cast<uint32_t>(entry.second.size()), std::memory_order_relaxed);
    return true;
  });
}

// The game's map of a known buffer, kept while `active`.
inline void Mapped(Buffers* buffers, bool active, reshade::api::resource resource, uint64_t offset, reshade::api::map_access access, void** data) {
  if (!active || offset != 0 || data == nullptr || *data == nullptr || access == reshade::api::map_access::read_only) return;
  buffers->known.modify_if(resource.handle, [buffers, data](auto& entry) {
    auto& open_maps = entry.second;
    const DWORD thread = GetCurrentThreadId();
    // One pointer for a thread: a map whose unmap never came (a lost device) must not leave its old pointer for the
    // next unmap to write through.
    if (const auto own = std::ranges::find(open_maps, thread, &OpenMap::thread); own != open_maps.end()) {
      own->data = static_cast<uint8_t*>(*data);
    } else {
      open_maps.push_back({.thread = thread, .data = static_cast<uint8_t*>(*data)});
      buffers->open_map_count.fetch_add(1, std::memory_order_relaxed);
    }
  });
}

// Before the game's unmap: the pointer its map gave is still the buffer's memory. `write(bytes)` runs on this thread's
// pointer and says whether it wrote; true: it did.
template <typename Write>
bool Unmapped(Buffers* buffers, reshade::api::resource resource, Write write) {
  if (buffers->open_map_count.load(std::memory_order_relaxed) == 0) return false;
  bool written = false;
  buffers->known.modify_if(resource.handle, [buffers, &written, &write](auto& entry) {
    auto& open_maps = entry.second;
    if (open_maps.empty()) return;
    const auto own = std::ranges::find(open_maps, GetCurrentThreadId(), &OpenMap::thread);
    if (own == open_maps.end()) {
      // Unmapped on another thread than mapped (or mapped before the option came on): which map this ends is unknown,
      // so no pointer of this buffer is written through any more.
      buffers->open_map_count.fetch_sub(static_cast<uint32_t>(open_maps.size()), std::memory_order_relaxed);
      open_maps.clear();
      return;
    }
    written = write(own->data);
    open_maps.erase(own);
    buffers->open_map_count.fetch_sub(1, std::memory_order_relaxed);
  });
  return written;
}

// The game's update of a known buffer, whole or as far as `needed` bytes, replaced by the same with `write(bytes)` done on a
// copy, on its own context (no event). False: left to the game (also when `write` wrote nothing).
template <size_t BYTES, typename Write>
bool Update(const Buffers& buffers, ID3D11DeviceContext* native, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size,
            uint64_t needed, Write write) {
  if (native == nullptr || data == nullptr || offset != 0 || size < needed) return false;
  if (!buffers.known.contains(resource.handle)) return false;
  std::array<uint8_t, BYTES> patched = {};
  std::memcpy(patched.data(), data, static_cast<size_t>(std::min<uint64_t>(size, BYTES)));
  if (!write(patched.data())) return false;
  // Through ReShade's context the update would come back here.
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  const D3D11_BOX part = {.left = 0, .top = 0, .front = 0, .right = static_cast<UINT>(std::min<uint64_t>(size, BYTES)), .bottom = 1, .back = 1};
  native->UpdateSubresource(reinterpret_cast<ID3D11Resource*>(resource.handle), 0, (size < BYTES) ? &part : nullptr, patched.data(), 0, 0);
  return true;
}

// An image kept loaded after the detach holds no buffer and no pointer.
inline void Clear(Buffers* buffers) {
  buffers->known.clear();
  buffers->open_map_count.store(0, std::memory_order_relaxed);
}

}  // namespace twwarhammer3::buffer_writes
