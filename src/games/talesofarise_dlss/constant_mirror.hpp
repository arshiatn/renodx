/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <windows.h>
#include <include/reshade.hpp>

#include "../../utils/log.hpp"

// CPU copy of the game's constant-buffer uploads (UpdateSubresource, Map/Unmap,
// initial data), limited to the buffer sizes the temporal pass binds and to the
// bytes it reads. It replaces the GPU readback wait. Map events do not say which
// context wrote, so the adapter trusts it only after it matched exact readbacks
// and keeps checking it without waiting.
namespace talesofarise::dlss::constants {

namespace internal {

constexpr uint32_t MAX_BUFFER_BYTES = 64 * 1024;
// Larger offsets suggest a ring buffer; its many small writes are not copied.
constexpr uint32_t MAX_NEEDED_BYTES = 16 * 1024;

struct Snapshot {
  std::vector<std::byte> bytes;  // first `needed` bytes of the buffer
  uint32_t valid_begin = 0;      // one valid byte range
  uint32_t valid_end = 0;
  const std::byte* mapping = nullptr;
};

inline std::mutex mutex;
inline std::unordered_map<ID3D11Buffer*, Snapshot> buffers;
// Watched buffers: ByteWidth (bits 0-31), D3D11_USAGE (32-39) and bytes read
// from the start (40-63). Usage keeps UpdateSubresource buffers apart from
// Map-written ones of the same size.
inline std::array<std::atomic<uint64_t>, 4> watched = {};
inline std::atomic<uint64_t> copied_bytes = 0;
// Watched buffers mapped on this thread; every other Unmap returns without locking.
inline thread_local std::array<ID3D11Buffer*, 4> open_maps = {};

constexpr uint64_t Key(uint32_t byte_width, D3D11_USAGE usage) {
  return byte_width | (uint64_t{static_cast<uint8_t>(usage)} << 32);
}

// D3D11 resources are not ReShade proxies; the handle is the game's pointer.
inline ID3D11Buffer* Buffer(reshade::api::resource resource) {
  return reinterpret_cast<ID3D11Buffer*>(resource.handle);
}

// Bytes to keep for a watched constant buffer, else 0.
inline uint32_t Needed(ID3D11Buffer* buffer) {
  if (buffer == nullptr) return 0;
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if ((desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0) return 0;
  const uint64_t key = Key(desc.ByteWidth, desc.Usage);
  for (const auto& entry : watched) {
    const uint64_t value = entry.load(std::memory_order_relaxed);
    if (value != 0 && (value & 0xFFFFFFFFFFull) == key) return std::min(static_cast<uint32_t>(value >> 40), desc.ByteWidth);
  }
  return 0;
}

inline void OnInitResource(reshade::api::device*, const reshade::api::resource_desc& desc,
                           const reshade::api::subresource_data* initial_data, reshade::api::resource_usage,
                           reshade::api::resource resource) {
  if (desc.type != reshade::api::resource_type::buffer || desc.buffer.size > MAX_BUFFER_BYTES
      || (desc.usage & reshade::api::resource_usage::constant_buffer) == 0) return;
  const uint32_t needed = Needed(Buffer(resource));
  if (needed == 0) return;
  const std::lock_guard lock(mutex);
  auto& snapshot = buffers[Buffer(resource)];
  snapshot = {.bytes = std::vector<std::byte>(needed)};
  if (initial_data != nullptr && initial_data->data != nullptr) {
    std::memcpy(snapshot.bytes.data(), initial_data->data, needed);
    snapshot.valid_end = needed;
    copied_bytes.fetch_add(needed, std::memory_order_relaxed);
  }
}

inline void OnMapBufferRegion(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t,
                              reshade::api::map_access access, void** data) {
  if (access == reshade::api::map_access::read_only) return;
  auto* buffer = Buffer(resource);
  const uint32_t needed = Needed(buffer);
  if (needed == 0) return;
  // Copied at unmap. A discard map renames the buffer; other maps keep its contents.
  const auto slot = std::ranges::find(open_maps, nullptr);
  const bool tracked = offset == 0 && data != nullptr && slot != open_maps.end();
  if (tracked) *slot = buffer;
  const std::lock_guard lock(mutex);
  auto& snapshot = buffers[buffer];
  snapshot.bytes.resize(needed);
  snapshot.mapping = tracked ? static_cast<const std::byte*>(*data) : nullptr;
  snapshot.valid_begin = 0;
  snapshot.valid_end = 0;
}

inline void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  const auto slot = std::ranges::find(open_maps, Buffer(resource));
  if (slot == open_maps.end()) return;
  *slot = nullptr;
  const std::lock_guard lock(mutex);
  const auto entry = buffers.find(Buffer(resource));
  if (entry == buffers.end() || entry->second.mapping == nullptr) return;
  auto& snapshot = entry->second;
  // Called before the native Unmap, while the pointer is still valid.
  std::memcpy(snapshot.bytes.data(), snapshot.mapping, snapshot.bytes.size());
  snapshot.mapping = nullptr;
  snapshot.valid_end = static_cast<uint32_t>(snapshot.bytes.size());
  copied_bytes.fetch_add(snapshot.bytes.size(), std::memory_order_relaxed);
}

// ReShade reports this for the immediate context only (deferred ones use update_buffer_region_command).
inline bool OnUpdateBufferRegion(reshade::api::device*, const void* data, reshade::api::resource resource,
                                 uint64_t offset, uint64_t size) {
  auto* buffer = Buffer(resource);
  const uint32_t needed = Needed(buffer);
  if (needed == 0) return false;
  const std::lock_guard lock(mutex);
  auto& snapshot = buffers[buffer];
  snapshot.bytes.resize(needed);
  snapshot.mapping = nullptr;
  if (offset >= needed) return false;
  const auto begin = static_cast<uint32_t>(offset);
  const auto end = static_cast<uint32_t>(size == UINT64_MAX ? needed : std::min<uint64_t>(offset + size, needed));
  if (data == nullptr) {
    snapshot.valid_begin = 0;
    snapshot.valid_end = 0;
    return false;
  }
  std::memcpy(snapshot.bytes.data() + begin, data, end - begin);
  copied_bytes.fetch_add(end - begin, std::memory_order_relaxed);
  // Keep one range: merge when touching, else keep only the new bytes.
  if (snapshot.valid_end > snapshot.valid_begin && begin <= snapshot.valid_end && end >= snapshot.valid_begin) {
    snapshot.valid_begin = std::min(snapshot.valid_begin, begin);
    snapshot.valid_end = std::max(snapshot.valid_end, end);
  } else {
    snapshot.valid_begin = begin;
    snapshot.valid_end = end;
  }
  return false;
}

// GPU-side copies into a watched buffer are not visible on the CPU.
inline void Invalidate(reshade::api::resource resource) {
  const std::lock_guard lock(mutex);
  const auto entry = buffers.find(Buffer(resource));
  if (entry == buffers.end()) return;
  entry->second.valid_begin = 0;
  entry->second.valid_end = 0;
}

inline bool OnCopyResource(reshade::api::command_list*, reshade::api::resource, reshade::api::resource destination) {
  Invalidate(destination);
  return false;
}

inline bool OnCopyBufferRegion(reshade::api::command_list*, reshade::api::resource, uint64_t,
                               reshade::api::resource destination, uint64_t, uint64_t) {
  Invalidate(destination);
  return false;
}

inline void OnDestroyResource(reshade::api::device*, reshade::api::resource resource) {
  const std::lock_guard lock(mutex);
  buffers.erase(Buffer(resource));
}

inline void OnDestroyDevice(reshade::api::device*) {
  const std::lock_guard lock(mutex);
  buffers.clear();
}

}  // namespace internal

// Keeps the first `needed_bytes` of every constant buffer like `buffer` from now on.
inline void Watch(ID3D11Buffer* buffer, uint32_t needed_bytes) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (desc.ByteWidth == 0 || desc.ByteWidth > internal::MAX_BUFFER_BYTES
      || needed_bytes == 0 || needed_bytes > internal::MAX_NEEDED_BYTES) return;
  const uint64_t key = internal::Key(desc.ByteWidth, desc.Usage);
  for (auto& entry : internal::watched) {
    uint64_t value = entry.load();
    while (value == 0 || (value & 0xFFFFFFFFFFull) == key) {
      const uint64_t needed = std::max<uint64_t>(value >> 40, needed_bytes);
      if (value != 0 && needed == (value >> 40)) return;
      if (entry.compare_exchange_weak(value, (needed << 40) | key)) {
        renodx::utils::log::i("[Tales AA constants] Copying uploads of ", desc.ByteWidth, "-byte constant buffers (usage ",
                              static_cast<uint32_t>(desc.Usage), ", CPU access ", renodx::utils::log::AsHex(desc.CPUAccessFlags),
                              "), first ", needed, " bytes.");
        return;
      }
    }
  }
}

// Bytes [byte_offset, byte_offset + destination.size()) of the latest upload.
inline bool Read(ID3D11Buffer* buffer, uint64_t byte_offset, std::span<std::byte> destination) {
  const std::lock_guard lock(internal::mutex);
  const auto entry = internal::buffers.find(buffer);
  if (entry == internal::buffers.end()) return false;
  const auto& snapshot = entry->second;
  if (snapshot.mapping != nullptr || byte_offset < snapshot.valid_begin
      || byte_offset + destination.size() > snapshot.valid_end) return false;
  std::memcpy(destination.data(), snapshot.bytes.data() + byte_offset, destination.size());
  return true;
}

inline uint64_t CopiedBytes() {
  return internal::copied_bytes.load(std::memory_order_relaxed);
}

inline void Use(DWORD reason) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      reshade::register_event<reshade::addon_event::init_resource>(internal::OnInitResource);
      reshade::register_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
      reshade::register_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
      reshade::register_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
      reshade::register_event<reshade::addon_event::copy_resource>(internal::OnCopyResource);
      reshade::register_event<reshade::addon_event::copy_buffer_region>(internal::OnCopyBufferRegion);
      reshade::register_event<reshade::addon_event::destroy_resource>(internal::OnDestroyResource);
      reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
      break;
    case DLL_PROCESS_DETACH:
      reshade::unregister_event<reshade::addon_event::init_resource>(internal::OnInitResource);
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
      reshade::unregister_event<reshade::addon_event::copy_resource>(internal::OnCopyResource);
      reshade::unregister_event<reshade::addon_event::copy_buffer_region>(internal::OnCopyBufferRegion);
      reshade::unregister_event<reshade::addon_event::destroy_resource>(internal::OnDestroyResource);
      reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
      {
        const std::lock_guard lock(internal::mutex);
        internal::buffers.clear();
      }
      for (auto& entry : internal::watched) entry.store(0);
      break;
  }
}

}  // namespace talesofarise::dlss::constants
