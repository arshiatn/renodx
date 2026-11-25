/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/settings.hpp"

// Pass dump (test only; the D3D11 pass speed round, d3d11_pass_speed.md 3.2): the inputs and outputs of the add-on's own
// D3D11 passes, for an offline replay (test/tw_d3d11_replay) that must give the same bytes. Off unless
// RENODX_TW_PASS_DUMP names a folder: without it Install registers nothing. pass_timing::Install calls Install /
// Uninstall. Not test_hooks.hpp's: its define-before-include order (RENODX_TEST_HOOKS_MEMORY / PROFILE) must not be
// pulled in from here.
// - Trigger: the file renodx_pass_dump_now next to the exe ("<n>[ <label>]", test_hooks' set key pass_dump writes it),
//   read every 30 presents and deleted: the next n frames with an own-pass event go to <folder>\<label>\frame_<present>\.
// - Each event (a pass site's Begin / Pass, at a game command or our own) keeps the game state bound on the native
//   context then (every stage's shader bytes, views, constant buffers with their D3D11.1 offsets, samplers, UAVs; OM,
//   RS, IA and the host draw's arguments), each resource it reads copied then: our passes write through the native
//   context, which no ReShade event sees, so nothing tells a copy is still current (review R-M1); identical bytes are
//   stored once (by a hash). The site's own resources (Resource) are copied at the call and again at End; its CPU state
//   (Value) and constant buffer bytes (Bytes) are kept as they are.
// - Shader bytes: every pipeline's code at init_pipeline (the code the device made: a replacement's or a patch's), by
//   its native object, while enabled; the input layouts' elements too.
// - Copies: CopyResource into staging (typeless for depth: R24G8 carries depth and stencil) at the event, read one present
//   after the frame (Map on the render thread: the immediate context is not free-threaded), hashed and written by a
//   worker. At most 512 MB of staging in flight (the oldest read first when more is needed). x86: each subresource read
//   at once, above 16 MB in strips of rows (depth whole), each Map only with a free block twice its size.
// - Guards: under 150 GB free on the folder's disk stops the dump; less than 2 GB of the video memory budget left skips
//   the frame (a GPU reset ends the 64-bit port's session); a deferred context is skipped. Multisampled resources are
//   named, not copied.
// - A frame folder holds blobs\, manifest.json and uses.tsv (lab\dead_outputs.py's columns; rtv / dsv kinds), then
//   done.txt, then the log line "pass_dump: frame <present> done" (no Uninstall needed: the port's AddonUninit never
//   runs). Without done.txt a folder is void.
// - While enabled, every 30 presents: "pass_dump: camera yaw <deg> pitch <deg> pos <x> <y> <z>" from the camera
//   constants a site passes (Camera), for the capture script's camera turn.
namespace twattila::pass_dump {

namespace internal {

using Microsoft::WRL::ComPtr;

constexpr uint32_t POLL_PRESENTS = 30;
constexpr size_t POOL_BYTES = size_t{512} << 20;
constexpr size_t STRIP_BYTES = size_t{16} << 20;
constexpr size_t BACKLOG_BYTES = size_t{2} << 30;
constexpr uint64_t FREE_DISK_BYTES = uint64_t{150} << 30;
constexpr uint64_t VIDEO_MEMORY_MARGIN = uint64_t{2} << 30;
constexpr const wchar_t* MARKER = L"renodx_pass_dump_now";

// One subresource's bytes, rows packed (row_bytes each: a block row for compressed formats).
struct Subresource {
  UINT index = 0;
  UINT width = 1;
  UINT height = 1;
  UINT depth = 1;
  UINT row_bytes = 0;
  UINT rows = 1;
  std::vector<uint8_t> data;
  std::string blob;  // the worker's file name in blobs\ (by the bytes' hash)
};

// A resource copied at one point (or CPU bytes: a site's constants, a shader's code).
struct Copy {
  uint64_t id = 0;   // the native resource (the same at every copy of it in the frame)
  std::string kind;  // buffer, texture1d / 2d / 3d, bytes, shader
  std::string desc;  // JSON: the resource's desc fields
  ComPtr<ID3D11Resource> staging;
  std::string staging_key;
  size_t staging_bytes = 0;
  bool multisampled = false;  // named only
  std::vector<Subresource> subresources;
};

struct Frame {
  uint64_t present = 0;
  std::filesystem::path folder;
  std::string label;
  std::vector<std::unique_ptr<Copy>> copies;  // the worker fills each subresource's blob
  std::string events;                         // JSON objects, comma separated, in order
  std::string values;                         // frame-level Values (outside any event)
  std::string settings;                       // every setting's key and value at the frame's end (render thread)
  std::string uses;                           // uses.tsv rows
  std::unordered_map<uint64_t, size_t> shaders;  // copy index of each shader's code by its native object
  size_t event_count = 0;
  uint32_t spans = 0;
  size_t bytes = 0;
};

struct OpenEvent {
  std::string name;
  std::string host;  // JSON
  std::vector<std::pair<std::string, ComPtr<ID3D11Resource>>> resources;
  std::string before;
  std::string values;
  std::string bytes;
};

struct State {
  std::shared_mutex mutex;
  std::filesystem::path root;
  std::filesystem::path exe_folder;
  uint64_t presents = 0;
  uint32_t armed = 0;
  std::string label = "dump";
  std::shared_ptr<Frame> frame;      // being recorded
  std::shared_ptr<Frame> finishing;  // recorded, read at the next present
  bool open = false;
  OpenEvent event;
  std::unordered_map<ID3D11Resource*, size_t> captured;  // this capture's copies (an event's begin, its end)
  std::deque<Copy*> in_flight;
  size_t in_flight_bytes = 0;
  std::unordered_map<std::string, std::vector<ComPtr<ID3D11Resource>>> free_staging;
  size_t free_staging_bytes = 0;
  // Every pipeline's code and every input layout's elements, by native object.
  std::unordered_map<uint64_t, std::vector<uint8_t>> shaders;
  std::unordered_map<uint64_t, std::string> layouts;  // JSON
  // The last draw / dispatch event's arguments (JSON).
  std::string draw = "null";
  bool logged_deferred = false;
  // Camera line: the staging copy waiting to be read.
  ComPtr<ID3D11Buffer> camera_staging;
  bool camera_pending = false;
  // Replay checks (test/tw_d3d11_replay): the constant bytes Bytes must see, and what it didn't.
  std::unordered_map<std::string, std::vector<uint8_t>> expected;
  std::vector<std::string> mismatches;
  // The worker.
  std::mutex queue_mutex;
  std::condition_variable queue_wake;
  std::deque<std::shared_ptr<Frame>> queue;
  size_t queued_bytes = 0;
  bool stopping = false;
  std::atomic_bool worker_idle_exit = false;
  std::thread worker;
};
static State state;
static std::atomic_bool enabled = false;    // RENODX_TW_PASS_DUMP set (Install)
static std::atomic_bool recording = false;  // a frame is being recorded: every site and event tests this first
static std::atomic_bool checking = false;   // the replay's expected bytes are set
static std::atomic_bool camera_due = false;

// Raw 32-bit words of a desc (D3D11 descs hold no pointers: the same layout on x86 and x64), as a JSON array.
template <typename T>
std::string Words(const T& value) {
  static_assert(sizeof(T) % 4 == 0);
  std::array<uint32_t, sizeof(T) / 4> words = {};
  std::memcpy(words.data(), &value, sizeof(T));
  std::string text = "[";
  for (size_t index = 0; index < words.size(); ++index) text += std::format("{}{}", (index == 0 ? "" : ","), words[index]);
  return text + "]";
}

inline std::string Quoted(std::string_view text) {
  std::string quoted = "\"";
  for (const char character : text) {
    if (character == '"' || character == '\\') quoted += '\\';
    if (static_cast<unsigned char>(character) < 0x20) {
      quoted += std::format("\\u{:04x}", static_cast<int>(character));
      continue;
    }
    quoted += character;
  }
  return quoted + "\"";
}

inline uint64_t Hash(std::span<const uint8_t> bytes) {
  uint64_t hash = 0xCBF29CE484222325ull;
  size_t at = 0;
  for (; at + 8 <= bytes.size(); at += 8) {
    uint64_t word = 0;
    std::memcpy(&word, bytes.data() + at, 8);
    hash = (hash ^ word) * 0x100000001B3ull;
    hash ^= hash >> 29;
  }
  for (; at < bytes.size(); ++at) hash = (hash ^ bytes[at]) * 0x100000001B3ull;
  return hash;
}

inline void Log(const std::string& text) { renodx::utils::log::i(std::format("[Total War] pass_dump: {}", text)); }

// The largest free block of the address space (x86's Map guard).
inline size_t LargestFreeBlock() {
  size_t largest = 0;
  MEMORY_BASIC_INFORMATION info = {};
  for (auto* address = static_cast<uint8_t*>(nullptr); VirtualQuery(address, &info, sizeof(info)) == sizeof(info);
       address = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize) {
    if (info.State == MEM_FREE) largest = std::max(largest, static_cast<size_t>(info.RegionSize));
    if (static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize < address) break;
  }
  return largest;
}

// `copy`'s staging read: its subresources' rows packed into their data; the staging back to the free pool.
inline bool Read(ID3D11DeviceContext* native, Copy* copy) {
  bool read = true;
  for (auto& subresource : copy->subresources) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(native->Map(copy->staging.Get(), subresource.index, D3D11_MAP_READ, 0, &mapped))) {
      read = false;
      continue;
    }
    subresource.data.resize(size_t{subresource.row_bytes} * subresource.rows * subresource.depth);
    for (UINT slice = 0; slice < subresource.depth; ++slice) {
      for (UINT row = 0; row < subresource.rows; ++row) {
        std::memcpy(subresource.data.data() + ((size_t{slice} * subresource.rows) + row) * subresource.row_bytes,
                    static_cast<const uint8_t*>(mapped.pData) + (size_t{slice} * mapped.DepthPitch) + (size_t{row} * mapped.RowPitch),
                    subresource.row_bytes);
      }
    }
    native->Unmap(copy->staging.Get(), subresource.index);
  }
  if (state.free_staging_bytes + copy->staging_bytes <= POOL_BYTES) {
    state.free_staging[copy->staging_key].push_back(copy->staging);
    state.free_staging_bytes += copy->staging_bytes;
  }
  copy->staging.Reset();
  state.in_flight_bytes -= copy->staging_bytes;
  return read;
}

inline void DrainInFlight(ID3D11DeviceContext* native, size_t room) {
  while (!state.in_flight.empty() && state.in_flight_bytes + room > POOL_BYTES) {
    Copy* oldest = state.in_flight.front();
    state.in_flight.pop_front();
    if (!Read(native, oldest)) Log(std::format("a copy of {:#x} could not be read", oldest->id));
  }
}

// x86: one subresource of `resource` read now, through a staging texture of that subresource alone (above STRIP_BYTES
// in strips of rows; depth and multisampled resources only whole, as D3D11 copies them).
inline bool ReadNow(ID3D11DeviceContext* native, ID3D11Resource* resource, D3D11_RESOURCE_DIMENSION dimension, Subresource* subresource,
                    DXGI_FORMAT staging_format, bool depth) {
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  const UINT whole_rows = subresource->rows;
  const size_t row_bytes = size_t{subresource->row_bytes} * subresource->depth;
  const UINT block = (whole_rows == 0) ? 1 : std::max(1u, subresource->height / whole_rows);
  const UINT strip_rows = (depth || dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                              ? whole_rows
                              : std::clamp(static_cast<UINT>(STRIP_BYTES / std::max<size_t>(row_bytes, 1)), 1u, whole_rows);
  subresource->data.resize(row_bytes * whole_rows);
  for (UINT first_row = 0; first_row < whole_rows; first_row += strip_rows) {
    const UINT rows = std::min(strip_rows, whole_rows - first_row);
    ComPtr<ID3D11Resource> staging;
    HRESULT made = E_FAIL;
    if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
      const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = subresource->row_bytes, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
      ComPtr<ID3D11Buffer> buffer;
      made = device->CreateBuffer(&staging_desc, nullptr, buffer.GetAddressOf());
      staging = buffer;
    } else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
      const D3D11_TEXTURE3D_DESC staging_desc = {.Width = subresource->width, .Height = subresource->height, .Depth = subresource->depth, .MipLevels = 1,
                                                 .Format = staging_format, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
      ComPtr<ID3D11Texture3D> texture;
      made = device->CreateTexture3D(&staging_desc, nullptr, texture.GetAddressOf());
      staging = texture;
    } else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D) {
      const D3D11_TEXTURE1D_DESC staging_desc = {.Width = subresource->width, .MipLevels = 1, .ArraySize = 1, .Format = staging_format,
                                                 .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
      ComPtr<ID3D11Texture1D> texture;
      made = device->CreateTexture1D(&staging_desc, nullptr, texture.GetAddressOf());
      staging = texture;
    } else {
      const D3D11_TEXTURE2D_DESC staging_desc = {.Width = subresource->width, .Height = std::min(subresource->height, rows * block), .MipLevels = 1,
                                                 .ArraySize = 1, .Format = staging_format, .SampleDesc = {.Count = 1},
                                                 .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
      ComPtr<ID3D11Texture2D> texture;
      made = device->CreateTexture2D(&staging_desc, nullptr, texture.GetAddressOf());
      staging = texture;
    }
    if (FAILED(made)) return false;
    if (strip_rows == whole_rows) {
      native->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, resource, subresource->index, nullptr);
    } else {
      const D3D11_BOX box = {.left = 0, .top = first_row * block, .front = 0, .right = subresource->width,
                             .bottom = std::min(subresource->height, (first_row + rows) * block), .back = 1};
      native->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, resource, subresource->index, &box);
    }
    if (LargestFreeBlock() < 2 * row_bytes * rows) return false;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(native->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    for (UINT slice = 0; slice < subresource->depth; ++slice) {
      for (UINT row = 0; row < rows; ++row) {
        std::memcpy(subresource->data.data() + ((size_t{slice} * whole_rows) + first_row + row) * subresource->row_bytes,
                    static_cast<const uint8_t*>(mapped.pData) + (size_t{slice} * mapped.DepthPitch) + (size_t{row} * mapped.RowPitch),
                    subresource->row_bytes);
      }
    }
    native->Unmap(staging.Get(), 0);
  }
  return true;
}

// `resource` copied now (once per capture): its index in the frame's copies, SIZE_MAX: none.
inline size_t Snapshot(ID3D11DeviceContext* native, ID3D11Resource* resource) {
  if (resource == nullptr) return SIZE_MAX;
  if (const auto found = state.captured.find(resource); found != state.captured.end()) return found->second;
  auto& frame = *state.frame;
  auto copy = std::make_unique<Copy>();
  copy->id = reinterpret_cast<uintptr_t>(resource);
  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  resource->GetType(&dimension);
  // Depth formats read through their typeless group (staging can't be a depth format; the bits are the same).
  const auto staging_format = [](DXGI_FORMAT format) {
    const auto typeless = static_cast<DXGI_FORMAT>(reshade::api::format_to_typeless(static_cast<reshade::api::format>(format)));
    switch (format) {
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
      case DXGI_FORMAT_D32_FLOAT:
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      case DXGI_FORMAT_D16_UNORM:
        return typeless;
      default:
        return format;
    }
  };
  const auto is_depth = [](DXGI_FORMAT format) {
    switch (format) {
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
      case DXGI_FORMAT_R24G8_TYPELESS:
      case DXGI_FORMAT_D32_FLOAT:
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      case DXGI_FORMAT_R32G8X24_TYPELESS:
      case DXGI_FORMAT_D16_UNORM:
        return true;
      default:
        return false;
    }
  };
  // A texture's subresources (mip-major within each array slice, D3D11's order) with their packed row sizes.
  const auto add_subresources = [&](DXGI_FORMAT format, UINT width, UINT height, UINT depth, UINT mips, UINT slices) {
    for (UINT slice = 0; slice < slices; ++slice) {
      for (UINT mip = 0; mip < mips; ++mip) {
        Subresource subresource = {.index = mip + (slice * mips), .width = std::max(1u, width >> mip), .height = std::max(1u, height >> mip),
                                   .depth = std::max(1u, depth >> mip)};
        subresource.row_bytes = reshade::api::format_row_pitch(static_cast<reshade::api::format>(format), subresource.width);
        if (subresource.row_bytes == 0) return false;
        subresource.rows = reshade::api::format_slice_pitch(static_cast<reshade::api::format>(format), subresource.row_bytes, subresource.height)
                           / subresource.row_bytes;
        copy->subresources.push_back(std::move(subresource));
      }
    }
    return true;
  };
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  bool copyable = true;
  switch (dimension) {
    case D3D11_RESOURCE_DIMENSION_BUFFER: {
      D3D11_BUFFER_DESC desc = {};
      static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
      copy->kind = "buffer";
      copy->desc = std::format(R"({{"ByteWidth":{},"Usage":{},"BindFlags":{},"CPUAccessFlags":{},"MiscFlags":{},"StructureByteStride":{}}})",
                               desc.ByteWidth, static_cast<int>(desc.Usage), desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags, desc.StructureByteStride);
      copy->subresources.push_back({.width = desc.ByteWidth, .row_bytes = desc.ByteWidth});
      copy->staging_key = std::format("b{}", desc.ByteWidth);
      break;
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
      D3D11_TEXTURE1D_DESC desc = {};
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      copy->kind = "texture1d";
      copy->desc = std::format(R"({{"Width":{},"MipLevels":{},"ArraySize":{},"Format":{},"Usage":{},"BindFlags":{},"CPUAccessFlags":{},"MiscFlags":{}}})",
                               desc.Width, desc.MipLevels, desc.ArraySize, static_cast<int>(desc.Format), static_cast<int>(desc.Usage), desc.BindFlags,
                               desc.CPUAccessFlags, desc.MiscFlags);
      format = staging_format(desc.Format);
      copyable = add_subresources(format, desc.Width, 1, 1, desc.MipLevels, desc.ArraySize);
      copy->staging_key = std::format("1:{}:{}:{}:{}", desc.Width, desc.MipLevels, desc.ArraySize, static_cast<int>(format));
      break;
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
      D3D11_TEXTURE2D_DESC desc = {};
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      copy->kind = "texture2d";
      copy->desc = std::format(
          R"({{"Width":{},"Height":{},"MipLevels":{},"ArraySize":{},"Format":{},"SampleCount":{},"SampleQuality":{},"Usage":{},"BindFlags":{},"CPUAccessFlags":{},"MiscFlags":{}}})",
          desc.Width, desc.Height, desc.MipLevels, desc.ArraySize, static_cast<int>(desc.Format), desc.SampleDesc.Count, desc.SampleDesc.Quality,
          static_cast<int>(desc.Usage), desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags);
      format = staging_format(desc.Format);
      copy->multisampled = desc.SampleDesc.Count > 1;
      copyable = !copy->multisampled && add_subresources(format, desc.Width, desc.Height, 1, desc.MipLevels, desc.ArraySize);
      copy->staging_key = std::format("2:{}:{}:{}:{}:{}", desc.Width, desc.Height, desc.MipLevels, desc.ArraySize, static_cast<int>(format));
      break;
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D: {
      D3D11_TEXTURE3D_DESC desc = {};
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      copy->kind = "texture3d";
      copy->desc = std::format(R"({{"Width":{},"Height":{},"Depth":{},"MipLevels":{},"Format":{},"Usage":{},"BindFlags":{},"CPUAccessFlags":{},"MiscFlags":{}}})",
                               desc.Width, desc.Height, desc.Depth, desc.MipLevels, static_cast<int>(desc.Format), static_cast<int>(desc.Usage),
                               desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags);
      format = staging_format(desc.Format);
      copyable = add_subresources(format, desc.Width, desc.Height, desc.Depth, desc.MipLevels, 1);
      copy->staging_key = std::format("3:{}:{}:{}:{}:{}", desc.Width, desc.Height, desc.Depth, desc.MipLevels, static_cast<int>(format));
      break;
    }
    default:
      copyable = false;
      break;
  }
  if (!copyable) copy->subresources.clear();
  for (const auto& subresource : copy->subresources) {
    copy->staging_bytes += size_t{subresource.row_bytes} * subresource.rows * subresource.depth;
  }
  frame.bytes += copy->staging_bytes;
  bool copied = copy->subresources.empty();
  if constexpr (sizeof(void*) == 4) {
    // x86: read at once (the address space has no room for a whole frame's staging).
    copied = true;
    for (auto& subresource : copy->subresources) {
      copied = copied && ReadNow(native, resource, dimension, &subresource, format, is_depth(format));
    }
  } else if (!copied) {
    DrainInFlight(native, copy->staging_bytes);
    auto& pool = state.free_staging[copy->staging_key];
    if (!pool.empty()) {
      copy->staging = pool.back();
      pool.pop_back();
      state.free_staging_bytes -= copy->staging_bytes;
    } else {
      HRESULT made = E_FAIL;
      switch (dimension) {
        case D3D11_RESOURCE_DIMENSION_BUFFER: {
          const D3D11_BUFFER_DESC desc = {.ByteWidth = copy->subresources[0].row_bytes, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
          ComPtr<ID3D11Buffer> buffer;
          made = device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf());
          copy->staging = buffer;
          break;
        }
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
          D3D11_TEXTURE1D_DESC desc = {};
          static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
          desc = {.Width = desc.Width, .MipLevels = desc.MipLevels, .ArraySize = desc.ArraySize, .Format = format, .Usage = D3D11_USAGE_STAGING,
                  .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
          ComPtr<ID3D11Texture1D> texture;
          made = device->CreateTexture1D(&desc, nullptr, texture.GetAddressOf());
          copy->staging = texture;
          break;
        }
        case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
          D3D11_TEXTURE2D_DESC desc = {};
          static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
          desc = {.Width = desc.Width, .Height = desc.Height, .MipLevels = desc.MipLevels, .ArraySize = desc.ArraySize, .Format = format,
                  .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
          ComPtr<ID3D11Texture2D> texture;
          made = device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf());
          copy->staging = texture;
          break;
        }
        case D3D11_RESOURCE_DIMENSION_TEXTURE3D: {
          D3D11_TEXTURE3D_DESC desc = {};
          static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
          desc = {.Width = desc.Width, .Height = desc.Height, .Depth = desc.Depth, .MipLevels = desc.MipLevels, .Format = format,
                  .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
          ComPtr<ID3D11Texture3D> texture;
          made = device->CreateTexture3D(&desc, nullptr, texture.GetAddressOf());
          copy->staging = texture;
          break;
        }
        default:
          break;
      }
      if (FAILED(made)) copy->staging.Reset();
    }
    if (copy->staging) {
      native->CopyResource(copy->staging.Get(), resource);
      state.in_flight.push_back(copy.get());
      state.in_flight_bytes += copy->staging_bytes;
      copied = true;
    }
  }
  if (!copied) {
    Log(std::format("{:#x} could not be copied: frame {} void", copy->id, frame.present));
    recording.store(false);
  }
  frame.copies.push_back(std::move(copy));
  state.captured[resource] = frame.copies.size() - 1;
  return frame.copies.size() - 1;
}

// CPU bytes kept as a copy (a site's constants, a shader's code).
inline size_t KeepBytes(std::string_view kind, const void* data, size_t size) {
  auto copy = std::make_unique<Copy>();
  copy->kind = kind;
  copy->desc = std::format(R"({{"bytes":{}}})", size);
  copy->subresources.push_back({.width = static_cast<UINT>(size), .row_bytes = static_cast<UINT>(size)});
  copy->subresources[0].data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
  state.frame->bytes += size;
  state.frame->copies.push_back(std::move(copy));
  return state.frame->copies.size() - 1;
}

inline std::string Index(size_t copy) { return (copy == SIZE_MAX) ? std::string("-1") : std::format("{}", copy); }

inline std::string ShaderCopy(ID3D11DeviceChild* shader) {
  if (shader == nullptr) return "-1";
  const auto id = reinterpret_cast<uintptr_t>(shader);
  auto& frame = *state.frame;
  if (const auto found = frame.shaders.find(id); found != frame.shaders.end()) return Index(found->second);
  const auto code = state.shaders.find(id);
  if (code == state.shaders.end()) return "-2";  // made before the dump was enabled, or the add-on's own (native)
  frame.shaders[id] = KeepBytes("shader", code->second.data(), code->second.size());
  return Index(frame.shaders[id]);
}

// The game state bound on `native` now (JSON), every resource it names copied.
inline std::string HostState(ID3D11DeviceContext* native) {
  ComPtr<ID3D11DeviceContext1> native1;
  native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf()));
  const auto view_resource = [&](ID3D11View* view) {
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    return Snapshot(native, resource.Get());
  };
  std::string text = "{";
  const auto stage = [&](std::string_view key, ID3D11DeviceChild* shader, auto get_views, auto get_buffers, auto get_buffers1, auto get_samplers) {
    text += std::format(R"({}"{}":{{"shader":{},"srv":[)", (text.size() > 1 ? "," : ""), key, ShaderCopy(shader));
    std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views = {};
    (native->*get_views)(0, static_cast<UINT>(views.size()), views.data());
    bool first = true;
    for (UINT slot = 0; slot < views.size(); ++slot) {
      if (views[slot] == nullptr) continue;
      D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
      views[slot]->GetDesc(&desc);
      text += std::format("{}[{},{},{}]", (first ? "" : ","), slot, Index(view_resource(views[slot])), Words(desc));
      views[slot]->Release();
      first = false;
    }
    text += R"(],"cb":[)";
    std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers = {};
    std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> first_constants = {};
    std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> constant_counts = {};
    if (native1) {
      (native1.Get()->*get_buffers1)(0, static_cast<UINT>(buffers.size()), buffers.data(), first_constants.data(), constant_counts.data());
    } else {
      (native->*get_buffers)(0, static_cast<UINT>(buffers.size()), buffers.data());
    }
    first = true;
    for (UINT slot = 0; slot < buffers.size(); ++slot) {
      if (buffers[slot] == nullptr) continue;
      text += std::format("{}[{},{},{},{}]", (first ? "" : ","), slot, Index(Snapshot(native, buffers[slot])), first_constants[slot], constant_counts[slot]);
      buffers[slot]->Release();
      first = false;
    }
    text += R"(],"sampler":[)";
    std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers = {};
    (native->*get_samplers)(0, static_cast<UINT>(samplers.size()), samplers.data());
    first = true;
    for (UINT slot = 0; slot < samplers.size(); ++slot) {
      if (samplers[slot] == nullptr) continue;
      D3D11_SAMPLER_DESC desc = {};
      samplers[slot]->GetDesc(&desc);
      text += std::format("{}[{},{}]", (first ? "" : ","), slot, Words(desc));
      samplers[slot]->Release();
      first = false;
    }
    text += "]";
  };
  const auto uavs = [&](std::span<ID3D11UnorderedAccessView*> list, UINT first_slot) {
    std::string items;
    for (UINT index = 0; index < list.size(); ++index) {
      if (list[index] == nullptr) continue;
      D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
      list[index]->GetDesc(&desc);
      items += std::format("{}[{},{},{}]", (items.empty() ? "" : ","), first_slot + index, Index(view_resource(list[index])), Words(desc));
      list[index]->Release();
    }
    return "[" + items + "]";
  };
  {
    ComPtr<ID3D11VertexShader> shader;
    native->VSGetShader(shader.GetAddressOf(), nullptr, nullptr);
    stage("vs", shader.Get(), &ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::VSGetConstantBuffers,
          &ID3D11DeviceContext1::VSGetConstantBuffers1, &ID3D11DeviceContext::VSGetSamplers);
    text += "}";
  }
  {
    ComPtr<ID3D11HullShader> shader;
    native->HSGetShader(shader.GetAddressOf(), nullptr, nullptr);
    stage("hs", shader.Get(), &ID3D11DeviceContext::HSGetShaderResources, &ID3D11DeviceContext::HSGetConstantBuffers,
          &ID3D11DeviceContext1::HSGetConstantBuffers1, &ID3D11DeviceContext::HSGetSamplers);
    text += "}";
  }
  {
    ComPtr<ID3D11DomainShader> shader;
    native->DSGetShader(shader.GetAddressOf(), nullptr, nullptr);
    stage("ds", shader.Get(), &ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::DSGetConstantBuffers,
          &ID3D11DeviceContext1::DSGetConstantBuffers1, &ID3D11DeviceContext::DSGetSamplers);
    text += "}";
  }
  {
    ComPtr<ID3D11GeometryShader> shader;
    native->GSGetShader(shader.GetAddressOf(), nullptr, nullptr);
    stage("gs", shader.Get(), &ID3D11DeviceContext::GSGetShaderResources, &ID3D11DeviceContext::GSGetConstantBuffers,
          &ID3D11DeviceContext1::GSGetConstantBuffers1, &ID3D11DeviceContext::GSGetSamplers);
    text += "}";
  }
  {
    ComPtr<ID3D11PixelShader> shader;
    native->PSGetShader(shader.GetAddressOf(), nullptr, nullptr);
    stage("ps", shader.Get(), &ID3D11DeviceContext::PSGetShaderResources, &ID3D11DeviceContext::PSGetConstantBuffers,
          &ID3D11DeviceContext1::PSGetConstantBuffers1, &ID3D11DeviceContext::PSGetSamplers);
    text += "}";
  }
  {
    ComPtr<ID3D11ComputeShader> shader;
    native->CSGetShader(shader.GetAddressOf(), nullptr, nullptr);
    stage("cs", shader.Get(), &ID3D11DeviceContext::CSGetShaderResources, &ID3D11DeviceContext::CSGetConstantBuffers,
          &ID3D11DeviceContext1::CSGetConstantBuffers1, &ID3D11DeviceContext::CSGetSamplers);
    std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> list = {};
    native->CSGetUnorderedAccessViews(0, static_cast<UINT>(list.size()), list.data());
    text += R"(,"uav":)" + uavs(list, 0) + "}";
  }
  // OM.
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets = {};
  ID3D11DepthStencilView* depth = nullptr;
  native->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), &depth);
  text += R"(,"om":{"rtv":[)";
  bool first = true;
  for (UINT slot = 0; slot < targets.size(); ++slot) {
    if (targets[slot] == nullptr) continue;
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    targets[slot]->GetDesc(&desc);
    text += std::format("{}[{},{},{}]", (first ? "" : ","), slot, Index(view_resource(targets[slot])), Words(desc));
    targets[slot]->Release();
    first = false;
  }
  text += R"(],"dsv":)";
  if (depth != nullptr) {
    D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
    depth->GetDesc(&desc);
    text += std::format("[{},{}]", Index(view_resource(depth)), Words(desc));
    depth->Release();
  } else {
    text += "null";
  }
  {
    std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> list = {};
    native->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, static_cast<UINT>(list.size()), list.data());
    text += R"(,"uav":)" + uavs(list, 0);
  }
  ComPtr<ID3D11BlendState> blend;
  std::array<FLOAT, 4> blend_factor = {};
  UINT sample_mask = 0;
  native->OMGetBlendState(blend.GetAddressOf(), blend_factor.data(), &sample_mask);
  D3D11_BLEND_DESC blend_desc = {};
  if (blend) blend->GetDesc(&blend_desc);
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_ref = 0;
  native->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_ref);
  D3D11_DEPTH_STENCIL_DESC depth_desc = {};
  if (depth_state) depth_state->GetDesc(&depth_desc);
  text += std::format(R"(,"blend":{},"blend_factor":[{},{},{},{}],"sample_mask":{},"depth_stencil":{},"stencil_ref":{}}})",
                      (blend ? Words(blend_desc) : "null"), blend_factor[0], blend_factor[1], blend_factor[2], blend_factor[3], sample_mask,
                      (depth_state ? Words(depth_desc) : "null"), stencil_ref);
  // RS.
  std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
  UINT viewport_count = static_cast<UINT>(viewports.size());
  native->RSGetViewports(&viewport_count, viewports.data());
  std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors = {};
  UINT scissor_count = static_cast<UINT>(scissors.size());
  native->RSGetScissorRects(&scissor_count, scissors.data());
  ComPtr<ID3D11RasterizerState> rasterizer;
  native->RSGetState(rasterizer.GetAddressOf());
  D3D11_RASTERIZER_DESC rasterizer_desc = {};
  if (rasterizer) rasterizer->GetDesc(&rasterizer_desc);
  text += R"(,"rs":{"viewports":[)";
  for (UINT index = 0; index < viewport_count; ++index) {
    const auto& viewport = viewports[index];
    text += std::format("{}[{},{},{},{},{},{}]", (index == 0 ? "" : ","), viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height,
                        viewport.MinDepth, viewport.MaxDepth);
  }
  text += R"(],"scissors":[)";
  for (UINT index = 0; index < scissor_count; ++index) {
    const auto& scissor = scissors[index];
    text += std::format("{}[{},{},{},{}]", (index == 0 ? "" : ","), scissor.left, scissor.top, scissor.right, scissor.bottom);
  }
  text += std::format(R"(],"state":{}}})", (rasterizer ? Words(rasterizer_desc) : "null"));
  // IA.
  ComPtr<ID3D11InputLayout> layout;
  native->IAGetInputLayout(layout.GetAddressOf());
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  native->IAGetPrimitiveTopology(&topology);
  std::string layout_text = "null";
  if (layout) {
    const auto found = state.layouts.find(reinterpret_cast<uintptr_t>(layout.Get()));
    layout_text = (found == state.layouts.end() ? std::string(R"("unknown")") : found->second);
  }
  text += std::format(R"(,"ia":{{"layout":{},"topology":{},"vb":[)", layout_text, static_cast<int>(topology));
  std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertex_buffers = {};
  std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides = {};
  std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> offsets = {};
  native->IAGetVertexBuffers(0, static_cast<UINT>(vertex_buffers.size()), vertex_buffers.data(), strides.data(), offsets.data());
  first = true;
  for (UINT slot = 0; slot < vertex_buffers.size(); ++slot) {
    if (vertex_buffers[slot] == nullptr) continue;
    text += std::format("{}[{},{},{},{}]", (first ? "" : ","), slot, Index(Snapshot(native, vertex_buffers[slot])), strides[slot], offsets[slot]);
    vertex_buffers[slot]->Release();
    first = false;
  }
  ID3D11Buffer* index_buffer = nullptr;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  native->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
  if (index_buffer != nullptr) {
    text += std::format(R"(],"ib":[{},{},{}]}})", Index(Snapshot(native, index_buffer)), static_cast<int>(index_format), index_offset);
    index_buffer->Release();
  } else {
    text += R"(],"ib":null})";
  }
  return text + R"(,"draw":)" + state.draw + "}";
}

// The open event's end: its resources copied again, its JSON added to the frame.
inline void CloseEvent(ID3D11DeviceContext* native) {
  if (!state.open) return;
  state.open = false;
  state.captured.clear();
  std::string after;
  for (const auto& [name, resource] : state.event.resources) {
    after += std::format("{}{}:{}", (after.empty() ? "" : ","), Quoted(name), Index(Snapshot(native, resource.Get())));
  }
  state.captured.clear();
  auto& frame = *state.frame;
  frame.events += std::format(R"({}{{"index":{},"name":{},"host":{},"before":{{{}}},"after":{{{}}},"values":{{{}}},"bytes":{{{}}}}})",
                              (frame.event_count == 0 ? "" : ",\n"), frame.event_count, Quoted(state.event.name), state.event.host, state.event.before,
                              after, state.event.values, state.event.bytes);
  ++frame.event_count;
  state.event = {};
}

// The worker: each frame's blobs (once per hash), manifest.json, uses.tsv, done.txt, the log line.
inline void WriteFrame(const std::shared_ptr<Frame>& frame) {
  std::error_code error;
  std::filesystem::create_directories(frame->folder / "blobs", error);
  std::unordered_set<std::string> written;
  for (const auto& copy : frame->copies) {
    for (auto& subresource : copy->subresources) {
      if (subresource.data.empty() && subresource.row_bytes != 0) continue;
      subresource.blob = std::format("{:016x}_{}.bin", Hash(subresource.data), subresource.data.size());
      if (written.insert(subresource.blob).second) {
        std::ofstream(frame->folder / "blobs" / subresource.blob, std::ios::binary | std::ios::trunc)
            .write(reinterpret_cast<const char*>(subresource.data.data()), static_cast<std::streamsize>(subresource.data.size()));
      }
      std::vector<uint8_t>().swap(subresource.data);
    }
  }
  std::string copies;
  for (size_t index = 0; index < frame->copies.size(); ++index) {
    const auto& copy = *frame->copies[index];
    std::string subresources;
    for (const auto& subresource : copy.subresources) {
      subresources += std::format(R"({}{{"index":{},"width":{},"height":{},"depth":{},"row_bytes":{},"rows":{},"blob":{}}})",
                                  (subresources.empty() ? "" : ","), subresource.index, subresource.width, subresource.height, subresource.depth,
                                  subresource.row_bytes, subresource.rows, Quoted(subresource.blob));
    }
    copies += std::format(R"({}{{"id":"{:#x}","kind":"{}","desc":{},"multisampled":{},"subresources":[{}]}})", (index == 0 ? "" : ",\n"), copy.id, copy.kind,
                          copy.desc, (copy.multisampled ? "true" : "false"), subresources);
  }
  const std::string manifest = std::format(
      "{{\"version\":1,\"present\":{},\"label\":{},\"pointer_bits\":{},\"settings\":{{{}}},\"values\":{{{}}},\n\"copies\":[\n{}\n],\n\"events\":[\n{}\n]}}\n",
      frame->present, Quoted(frame->label), sizeof(void*) * 8, frame->settings, frame->values, copies, frame->events);
  std::ofstream(frame->folder / "manifest.json.tmp", std::ios::binary | std::ios::trunc) << manifest;
  std::filesystem::rename(frame->folder / "manifest.json.tmp", frame->folder / "manifest.json", error);
  std::ofstream(frame->folder / "uses.tsv", std::ios::binary | std::ios::trunc) << "# frame\tspan\tstem\tkind\tslot\tresource\n" << frame->uses;
  std::ofstream(frame->folder / "done.txt", std::ios::binary | std::ios::trunc) << std::format("{} events, {} copies\n", frame->event_count, frame->copies.size());
  Log(std::format("frame {} done ({} events, {} copies, {})", frame->present, frame->event_count, frame->copies.size(), frame->folder.string()));
}

inline void Work() {
  while (true) {
    std::shared_ptr<Frame> frame;
    {
      std::unique_lock lock(state.queue_mutex);
      state.queue_wake.wait(lock, [] { return state.stopping || !state.queue.empty(); });
      if (state.queue.empty()) break;
      frame = state.queue.front();
      state.queue.pop_front();
    }
    WriteFrame(frame);
    const std::unique_lock lock(state.queue_mutex);
    state.queued_bytes -= frame->bytes;
  }
  state.worker_idle_exit.store(true);
}

// The video memory budget's room for the copies (a GPU reset ends the port's session): `why` when not.
inline bool VideoMemoryRoom(ID3D11DeviceContext* native, std::string* why) {
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIAdapter3> adapter3;
  if (FAILED(device.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf())) || FAILED(adapter.As(&adapter3))) {
    *why = "no IDXGIAdapter3";
    return false;
  }
  DXGI_QUERY_VIDEO_MEMORY_INFO local = {};
  DXGI_QUERY_VIDEO_MEMORY_INFO non_local = {};
  adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
  adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &non_local);
  if (local.CurrentUsage + VIDEO_MEMORY_MARGIN > local.Budget || non_local.CurrentUsage + (2 * POOL_BYTES) > non_local.Budget) {
    *why = std::format("video memory: local {} of {} MB, non-local {} of {} MB", local.CurrentUsage >> 20, local.Budget >> 20,
                       non_local.CurrentUsage >> 20, non_local.Budget >> 20);
    return false;
  }
  return true;
}

// The camera constants' line (camera_VS_PS: c0 its position, c13-c16 inv_view; either packing: the one whose
// translation is c0), yaw and pitch of its forward axis in degrees (y up).
inline std::string CameraLine(const float* constants) {
  const float* position = constants;
  const float* inv_view = constants + (13 * 4);
  const auto at_position = [&](const std::array<float, 3>& value) {
    return std::abs(value[0] - position[0]) + std::abs(value[1] - position[1]) + std::abs(value[2] - position[2])
           < 0.01f * (1.f + std::abs(position[0]) + std::abs(position[1]) + std::abs(position[2]));
  };
  std::array<float, 3> forward = {inv_view[8], inv_view[9], inv_view[10]};  // rows: c15
  if (!at_position({inv_view[12], inv_view[13], inv_view[14]}) && at_position({inv_view[3], inv_view[7], inv_view[11]})) {
    forward = {inv_view[2], inv_view[6], inv_view[10]};  // columns: the z column
  }
  const float length = std::sqrt((forward[0] * forward[0]) + (forward[1] * forward[1]) + (forward[2] * forward[2]));
  constexpr float DEGREES = 57.29577951f;
  const float yaw = std::atan2(forward[0], forward[2]) * DEGREES;
  const float pitch = (length > 0.f ? std::asin(std::clamp(forward[1] / length, -1.f, 1.f)) * DEGREES : 0.f);
  return std::format("camera yaw {:.2f} pitch {:.2f} pos {:.3f} {:.3f} {:.3f}", yaw, pitch, position[0], position[1], position[2]);
}

// Arming: `text` "<n>[ <label>]".
inline void Arm(std::string_view text) {
  uint32_t frames = 0;
  size_t at = 0;
  while (at < text.size() && text[at] >= '0' && text[at] <= '9') frames = (frames * 10) + static_cast<uint32_t>(text[at++] - '0');
  std::string label(text.substr(std::min(at, text.size())));
  label.erase(0, label.find_first_not_of(" \t"));
  while (!label.empty() && (label.back() == '\r' || label.back() == '\n' || label.back() == ' ')) label.pop_back();
  const bool plain = std::ranges::all_of(label, [](char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')
           || character == '_' || character == '-';
  });
  if (!label.empty() && plain) state.label = label;
  state.armed += frames;
  Log(std::format("armed {} frames (label {}){}", frames, state.label, (plain ? "" : "; the label is not plain, the last one kept")));
}

// Each present (before the add-on's other present work): the recorded frame closed (read at the next present, then
// written by the worker), the marker polled, the camera line, the next frame started while armed.
inline void Present(ID3D11DeviceContext* native) {
  const std::unique_lock lock(state.mutex);
  ++state.presents;
  // Copies of a frame (read now) leave the in-flight list; a later frame's stay in it.
  const auto drop_in_flight = [](const Frame& frame) {
    std::erase_if(state.in_flight, [&](const Copy* copy) {
      return std::ranges::any_of(frame.copies, [&](const auto& owned) { return owned.get() == copy; });
    });
  };
  if (state.finishing) {
    for (const auto& copy : state.finishing->copies) {
      if (copy->staging) Read(native, copy.get());
    }
    drop_in_flight(*state.finishing);
    Log(std::format("frame {} copied ({} MB)", state.finishing->present, state.finishing->bytes >> 20));
    {
      const std::unique_lock queue_lock(state.queue_mutex);
      state.queued_bytes += state.finishing->bytes;
      state.queue.push_back(state.finishing);
    }
    state.queue_wake.notify_one();
    state.finishing.reset();
  }
  if (state.frame) {
    CloseEvent(native);
    if (recording.load() && state.frame->event_count != 0) {
      if (renodx::utils::settings::settings != nullptr) {
        for (const auto* setting : *renodx::utils::settings::settings) {
          if (setting->key.empty()) continue;
          state.frame->settings += std::format("{}{}:{}", (state.frame->settings.empty() ? "" : ","), Quoted(setting->key), setting->GetValue());
        }
      }
      state.finishing = state.frame;
      --state.armed;
    } else {
      // Nothing of ours ran (not counted), or void: its copies dropped.
      for (const auto& copy : state.frame->copies) {
        if (copy->staging) {
          copy->staging.Reset();
          state.in_flight_bytes -= copy->staging_bytes;
        }
      }
      drop_in_flight(*state.frame);
    }
    state.frame.reset();
    recording.store(false);
  }
  if (state.camera_pending) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (native->Map(state.camera_staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped) == S_OK) {
      Log(CameraLine(static_cast<const float*>(mapped.pData)));
      native->Unmap(state.camera_staging.Get(), 0);
      state.camera_pending = false;
    }
  }
  if (state.presents % POLL_PRESENTS == 0) {
    camera_due.store(!state.camera_pending);
    if (std::ifstream marker(state.exe_folder / MARKER, std::ios::binary); marker) {
      std::string text((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
      marker.close();
      std::error_code ignored;
      std::filesystem::remove(state.exe_folder / MARKER, ignored);
      Arm(text);
    }
  }
  // The next frame right away while armed (consecutive frames: the histories go on).
  if (state.armed == 0) return;
  std::error_code space_error;
  const auto space = std::filesystem::space(state.root, space_error);
  if (space_error || space.available < FREE_DISK_BYTES) {
    Log(std::format("stopped: {} GB free on the dump folder's disk (needs 150)", (space_error ? uintmax_t{0} : (space.available >> 30))));
    state.armed = 0;
    return;
  }
  {
    const std::unique_lock queue_lock(state.queue_mutex);
    if (state.queued_bytes > BACKLOG_BYTES) return;
  }
  if (std::string why; !VideoMemoryRoom(native, &why)) {
    Log(std::format("frame {} skipped: {}", state.presents, why));
    return;
  }
  state.frame = std::make_shared<Frame>();
  state.frame->present = state.presents;
  state.frame->label = state.label;
  state.frame->folder = state.root / state.label / std::format("frame_{}", state.presents);
  state.draw = "null";
  recording.store(true);
}

inline ID3D11DeviceContext* NativeContext(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  return native;
}

inline void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*, uint32_t,
                      const reshade::api::rect*) {
  if (queue->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(queue->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  Present(native);
}

inline void KeepDraw(reshade::api::command_list* cmd_list, std::string text) {
  if (!recording.load(std::memory_order_relaxed) || NativeContext(cmd_list) == nullptr) return;
  const std::unique_lock lock(state.mutex);
  state.draw = std::move(text);
}

inline bool OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) {
  if (recording.load(std::memory_order_relaxed)) {
    KeepDraw(cmd_list, std::format(R"({{"kind":"draw","vertex_count":{},"instance_count":{},"first_vertex":{},"first_instance":{}}})", vertex_count,
                                   instance_count, first_vertex, first_instance));
  }
  return false;
}

inline bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset,
                          uint32_t first_instance) {
  if (recording.load(std::memory_order_relaxed)) {
    KeepDraw(cmd_list,
             std::format(R"({{"kind":"indexed","index_count":{},"instance_count":{},"first_index":{},"vertex_offset":{},"first_instance":{}}})", index_count,
                         instance_count, first_index, vertex_offset, first_instance));
  }
  return false;
}

inline bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t x, uint32_t y, uint32_t z) {
  if (recording.load(std::memory_order_relaxed)) {
    KeepDraw(cmd_list, std::format(R"({{"kind":"dispatch","x":{},"y":{},"z":{}}})", x, y, z));
  }
  return false;
}

// The code each pipeline was made with (a replacement's or a patch's: what runs), the input layouts' elements.
inline void OnInitPipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects,
                           reshade::api::pipeline pipeline) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    const auto& subobject = subobjects[index];
    switch (subobject.type) {
      case reshade::api::pipeline_subobject_type::vertex_shader:
      case reshade::api::pipeline_subobject_type::hull_shader:
      case reshade::api::pipeline_subobject_type::domain_shader:
      case reshade::api::pipeline_subobject_type::geometry_shader:
      case reshade::api::pipeline_subobject_type::pixel_shader:
      case reshade::api::pipeline_subobject_type::compute_shader: {
        const auto* desc = static_cast<const reshade::api::shader_desc*>(subobject.data);
        if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) break;
        const auto* code = static_cast<const uint8_t*>(desc->code);
        const std::unique_lock lock(state.mutex);
        state.shaders[pipeline.handle].assign(code, code + desc->code_size);
        break;
      }
      case reshade::api::pipeline_subobject_type::input_layout: {
        const auto* elements = static_cast<const reshade::api::input_element*>(subobject.data);
        std::string text = "[";
        for (uint32_t element = 0; element < subobject.count; ++element) {
          const auto& item = elements[element];
          text += std::format(R"({}{{"semantic":{},"index":{},"format":{},"slot":{},"offset":{},"stride":{},"step_rate":{}}})", (element == 0 ? "" : ","),
                              Quoted(item.semantic == nullptr ? "" : item.semantic), item.semantic_index, static_cast<int>(item.format), item.buffer_binding,
                              item.offset, item.stride, item.instance_step_rate);
        }
        const std::unique_lock lock(state.mutex);
        state.layouts[pipeline.handle] = text + "]";
        break;
      }
      default:
        break;
    }
  }
}

inline void OnDestroyPipeline(reshade::api::device* device, reshade::api::pipeline pipeline) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const std::unique_lock lock(state.mutex);
  state.shaders.erase(pipeline.handle);
  state.layouts.erase(pipeline.handle);
}

// The folder, the exe's, the worker (Install, and the replay's self test without ReShade).
inline void Start(const std::filesystem::path& root) {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  state.root = root;
  std::error_code ignored;
  std::filesystem::create_directories(root, ignored);
  state.exe_folder = std::filesystem::path(exe).parent_path();
  state.stopping = false;
  state.worker_idle_exit.store(false);
  state.worker = std::thread(Work);
  enabled.store(true);
}

// The worker stopped after the frames queued (the replay's self test; Uninstall).
inline void Stop() {
  enabled.store(false);
  recording.store(false);
  {
    const std::unique_lock lock(state.queue_mutex);
    state.stopping = true;
  }
  state.queue_wake.notify_one();
  if (state.worker.joinable()) state.worker.join();
}

}  // namespace internal

inline bool Recording() { return internal::recording.load(std::memory_order_relaxed); }

// An event at a pass site (its name "<module>.<pass>"), until End or the next Begin / present: the game state bound
// on `native` now with every resource it names, copied.
inline void Begin(ID3D11DeviceContext* native, const char* name) {
  using namespace internal;
  if (!recording.load(std::memory_order_relaxed) || native == nullptr) return;
  const std::unique_lock lock(state.mutex);
  if (state.frame == nullptr) return;
  if (native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
    if (!state.logged_deferred) Log("a deferred context's event skipped");
    state.logged_deferred = true;
    return;
  }
  CloseEvent(native);
  state.captured.clear();
  state.open = true;
  state.event = {.name = name};
  state.event.host = HostState(native);
}

inline void End(ID3D11DeviceContext* native) {
  if (!internal::recording.load(std::memory_order_relaxed) || native == nullptr) return;
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.frame == nullptr) return;
  internal::CloseEvent(native);
}

// Begin .. End around a scope.
class Pass {
 public:
  Pass(ID3D11DeviceContext* native, const char* name) : native_(native) { Begin(native, name); }
  ~Pass() { End(native_); }
  Pass(const Pass&) = delete;
  Pass& operator=(const Pass&) = delete;

 private:
  ID3D11DeviceContext* native_;
};

// One of the open event's own resources ("<module>.<name>"): copied now and again at its end.
inline void Resource(ID3D11DeviceContext* native, const char* name, ID3D11Resource* resource) {
  using namespace internal;
  if (!recording.load(std::memory_order_relaxed) || resource == nullptr) return;
  const std::unique_lock lock(state.mutex);
  if (!state.open) return;
  state.captured.clear();
  state.event.before += std::format("{}{}:{}", (state.event.before.empty() ? "" : ","), Quoted(name), Index(Snapshot(native, resource)));
  state.captured.clear();
  state.event.resources.emplace_back(name, resource);
}

inline void Resource(ID3D11DeviceContext* native, const char* name, ID3D11View* view) {
  if (!internal::recording.load(std::memory_order_relaxed) || view == nullptr) return;
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  Resource(native, name, resource.Get());
}

// A CPU value of the open event (else of the frame) that feeds a constant buffer or a branch.
inline void Value(const char* name, double value) {
  using namespace internal;
  if (!recording.load(std::memory_order_relaxed)) return;
  const std::unique_lock lock(state.mutex);
  if (state.frame == nullptr) return;
  std::string& values = (state.open ? state.event.values : state.frame->values);
  values += std::format("{}{}:{}", (values.empty() ? "" : ","), Quoted(name), value);
}

// Constant bytes a site builds (`name`, before it uploads them): kept with the open event. False: the replay's expected
// bytes differ (test/tw_d3d11_replay's check before a dispatch), the pass must not run. True in the game.
[[nodiscard]] inline bool Bytes(const char* name, const void* data, size_t size) {
  using namespace internal;
  if (checking.load(std::memory_order_relaxed)) {
    const std::unique_lock lock(state.mutex);
    if (const auto found = state.expected.find(name); found != state.expected.end()) {
      const auto* bytes = static_cast<const uint8_t*>(data);
      if (found->second.size() != size || !std::equal(found->second.begin(), found->second.end(), bytes)) {
        size_t word = 0;
        while (word * 4 < size && word * 4 < found->second.size() && std::memcmp(found->second.data() + word * 4, bytes + word * 4, 4) == 0) ++word;
        state.mismatches.push_back(std::format("{}: word {} ({} bytes, {} expected)", name, word, size, found->second.size()));
        return false;
      }
    }
  }
  if (!recording.load(std::memory_order_relaxed)) return true;
  const std::unique_lock lock(state.mutex);
  if (state.frame == nullptr || !state.open) return true;
  state.event.bytes += std::format("{}{}:{}", (state.event.bytes.empty() ? "" : ","), Quoted(name), Index(KeepBytes("bytes", data, size)));
  return true;
}

// The camera constants (camera_VS_PS, Attila's layout) for the camera line, while enabled, every 30 presents.
inline void Camera(ID3D11DeviceContext* native, ID3D11Buffer* camera) {
  using namespace internal;
  if (!camera_due.load(std::memory_order_relaxed) || camera == nullptr) return;
  const std::unique_lock lock(state.mutex);
  if (!camera_due.exchange(false)) return;
  D3D11_BUFFER_DESC desc = {};
  camera->GetDesc(&desc);
  if (desc.ByteWidth < 17 * 16) return;
  D3D11_BUFFER_DESC staging_desc = {};
  if (state.camera_staging) state.camera_staging->GetDesc(&staging_desc);
  if (staging_desc.ByteWidth != desc.ByteWidth) {
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    const D3D11_BUFFER_DESC made_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    state.camera_staging.Reset();
    if (FAILED(device->CreateBuffer(&made_desc, nullptr, state.camera_staging.GetAddressOf()))) return;
  }
  native->CopyResource(state.camera_staging.Get(), camera);
  state.camera_pending = true;
}

// OwnPass's end (pass_timing.hpp): the pass's bindings as uses.tsv rows (span: the mark's number in the frame; stem:
// its name, the shader's embed name; compute: the CS stage's views and UAVs, else the PS stage's views and OM's targets).
inline void OwnPassDone(ID3D11DeviceContext* native, const char* name, bool compute) {
  using namespace internal;
  if (!recording.load(std::memory_order_relaxed) || native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  const std::unique_lock lock(state.mutex);
  if (state.frame == nullptr) return;
  auto& frame = *state.frame;
  const uint32_t span = frame.spans++;
  const auto row = [&](std::string_view kind, std::string_view slot, ID3D11View* view) {
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    frame.uses += std::format("{}\t{}\t{}\t{}\t{}\t{:#x}\n", frame.present, span, name, kind, slot, reinterpret_cast<uintptr_t>(resource.Get()));
    view->Release();
  };
  std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views = {};
  if (compute) {
    native->CSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> uavs = {};
    native->CSGetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data());
    for (UINT slot = 0; slot < uavs.size(); ++slot) {
      if (uavs[slot] != nullptr) row("uav", std::format("u{}:0", slot), uavs[slot]);
    }
  } else {
    native->PSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets = {};
    ID3D11DepthStencilView* depth = nullptr;
    native->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), &depth);
    for (UINT slot = 0; slot < targets.size(); ++slot) {
      if (targets[slot] != nullptr) row("rtv", std::format("o{}:0", slot), targets[slot]);
    }
    if (depth != nullptr) row("dsv", "oDepth:0", depth);
  }
  for (UINT slot = 0; slot < views.size(); ++slot) {
    if (views[slot] != nullptr) row("srv", std::format("t{}:0", slot), views[slot]);
  }
}

// RENODX_TW_PASS_DUMP=<folder>: the dump's events registered (pass_timing::Install, before the add-on's other draw and
// present events); unset: nothing.
inline void Install() {
  const DWORD size = GetEnvironmentVariableW(L"RENODX_TW_PASS_DUMP", nullptr, 0);
  if (size == 0 || internal::enabled.load()) return;
  std::wstring folder(size, L'\0');
  folder.resize(GetEnvironmentVariableW(L"RENODX_TW_PASS_DUMP", folder.data(), size));
  internal::Start(folder);
  reshade::register_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::register_event<reshade::addon_event::draw>(internal::OnDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(internal::OnDrawIndexed);
  reshade::register_event<reshade::addon_event::dispatch>(internal::OnDispatch);
  reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  reshade::register_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  internal::Log(std::format("on: {} ({}-bit), marker {}", internal::state.root.string(), sizeof(void*) * 8,
                            (internal::state.exe_folder / internal::MARKER).string()));
}

// The worker is stopped and waited for at most 2 s (frames still queued then are lost: the folder has no done.txt).
inline void Uninstall() {
  if (!internal::enabled.load()) return;
  reshade::unregister_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::unregister_event<reshade::addon_event::draw>(internal::OnDraw);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(internal::OnDrawIndexed);
  reshade::unregister_event<reshade::addon_event::dispatch>(internal::OnDispatch);
  reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  reshade::unregister_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  internal::enabled.store(false);
  internal::recording.store(false);
  {
    const std::unique_lock lock(internal::state.queue_mutex);
    internal::state.stopping = true;
  }
  internal::state.queue_wake.notify_one();
  // AddonUninit can run under the loader lock (DLL unload): a join could wait forever there.
  for (int wait = 0; wait < 200 && !internal::state.worker_idle_exit.load(); ++wait) Sleep(10);
  if (internal::state.worker.joinable()) internal::state.worker.detach();
}

}  // namespace twattila::pass_dump
