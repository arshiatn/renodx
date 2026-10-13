/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <include/reshade.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../../utils/directx.hpp"
#include "../../utils/float16.hpp"
#include "../../utils/format.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"

// One-frame trace of one screen position (Advanced: "Trace this pixel"), to find which pass draws something and in
// which format (ATN: the sky bands in SDR too; turning shaders off in the devkit never removed it, as Unreal does not
// clear those pixels: the last frame's sky stays). During the next frame, before every draw, dispatch, clear and copy,
// the texel at that position (scaled to each target's size) is copied from the targets of the command before, so every
// command's result is known. Saved next to the exe as tales_pixel_trace.txt: each command that changed it, with its
// shaders, the target's format and size and its shader's input textures.
namespace talesofarise::pixel_trace {

using Microsoft::WRL::ComPtr;

constexpr size_t MAX_SAMPLES = 20000;  // 1x1 staging copies per trace
constexpr size_t NO_SAMPLE = std::numeric_limits<size_t>::max();
constexpr UINT TRACED_SLOTS = 8;
constexpr UINT TRACED_INPUTS = 16;

struct Target {
  ComPtr<ID3D11Texture2D> texture;
  UINT subresource = 0;
  D3D11_BOX texel = {};
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;  // the command's view of it
  std::string description;                   // slot, format, size, texel
  size_t before = NO_SAMPLE;
  size_t after = NO_SAMPLE;
};

struct Command {
  uint32_t index = 0;
  std::string description;  // kind and shaders
  std::string inputs;       // its shader's textures
  std::vector<Target> targets;
};

struct Sample {
  ComPtr<ID3D11Texture2D> staging;
  uint32_t size = 0;                    // the texel's bytes (the rest of `bytes` stays 0)
  std::array<uint8_t, 16> bytes = {};
};

struct State {
  std::shared_mutex mutex;
  bool requested = false;
  bool tracing = false;
  std::array<float, 2> requested_position = {};
  std::array<float, 2> position = {};  // 0-1 of each target's width and height
  std::vector<Command> commands;
  std::vector<Sample> samples;
  std::map<std::pair<ID3D11Texture2D*, UINT>, size_t> latest;  // each texel's last sample
  uint32_t command_count = 0;
  std::string status = "No trace yet.";
};
static State state;

inline std::string FormatName(DXGI_FORMAT format) {
  std::ostringstream name;
  ::operator<<(name, static_cast<reshade::api::format>(format));
  return name.str();
}

// The texel's channels in `format` (the command's view), or its bytes for formats without a reader here.
inline std::string Decode(DXGI_FORMAT format, const std::array<uint8_t, 16>& bytes) {
  const auto half = [&](size_t index) {
    uint16_t value = 0;
    std::memcpy(&value, bytes.data() + index * 2, sizeof(value));
    return renodx::utils::float16::Decode(value);
  };
  const auto single = [&](size_t index) {
    float value = 0;
    std::memcpy(&value, bytes.data() + index * 4, sizeof(value));
    return value;
  };
  uint32_t packed = 0;
  std::memcpy(&packed, bytes.data(), sizeof(packed));
  // 11- and 10-bit floats: 5 exponent bits, no sign.
  const auto small_float = [](uint32_t bits, uint32_t mantissa_bits) {
    const uint32_t exponent = bits >> mantissa_bits;
    const float mantissa = static_cast<float>(bits & ((1u << mantissa_bits) - 1u)) / static_cast<float>(1u << mantissa_bits);
    if (exponent == 0) return std::ldexp(mantissa, -14);
    if (exponent == 31) return std::numeric_limits<float>::infinity();
    return std::ldexp(1.f + mantissa, static_cast<int>(exponent) - 15);
  };
  std::vector<float> channels;
  switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      channels = {half(0), half(1), half(2), half(3)};
      break;
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_TYPELESS:
      channels = {half(0), half(1)};
      break;
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_TYPELESS:
      channels = {half(0)};
      break;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      channels = {single(0), single(1), single(2), single(3)};
      break;
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_TYPELESS:
      channels = {single(0), single(1)};
      break;
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
      channels = {single(0)};
      break;
    case DXGI_FORMAT_R11G11B10_FLOAT:
      channels = {small_float(packed & 0x7FFu, 6), small_float((packed >> 11) & 0x7FFu, 6), small_float(packed >> 22, 5)};
      break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      channels = {(packed & 0x3FFu) / 1023.f, ((packed >> 10) & 0x3FFu) / 1023.f, ((packed >> 20) & 0x3FFu) / 1023.f,
                  (packed >> 30) / 3.f};
      break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      channels = {bytes[0] / 255.f, bytes[1] / 255.f, bytes[2] / 255.f, bytes[3] / 255.f};
      break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
      channels = {bytes[2] / 255.f, bytes[1] / 255.f, bytes[0] / 255.f, bytes[3] / 255.f};
      break;
    case DXGI_FORMAT_R8G8_UNORM:
      channels = {bytes[0] / 255.f, bytes[1] / 255.f};
      break;
    case DXGI_FORMAT_R8_UNORM:
      channels = {bytes[0] / 255.f};
      break;
    default: {
      std::string hex = "bytes";
      for (const uint8_t byte : bytes) hex += std::format(" {:02X}", byte);
      return hex;
    }
  }
  std::string text = "(";
  for (size_t index = 0; index < channels.size(); ++index) {
    text += std::format("{}{:.6g}", (index == 0 ? "" : ", "), channels[index]);
  }
  return text + ")";
}

// `resource`'s texel at the traced position, as `view_format` sees it; false for anything but a single-sample 2D
// texture.
inline bool MakeTarget(ID3D11Resource* resource, UINT mip, UINT slice, DXGI_FORMAT view_format, const std::string& slot,
                       Target* target) {
  ComPtr<ID3D11Texture2D> texture;
  if (resource == nullptr || FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) return false;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (desc.SampleDesc.Count > 1 || mip >= desc.MipLevels || slice >= desc.ArraySize) return false;
  const UINT width = std::max(desc.Width >> mip, 1u);
  const UINT height = std::max(desc.Height >> mip, 1u);
  const UINT x = std::min(static_cast<UINT>(state.position[0] * static_cast<float>(width)), width - 1);
  const UINT y = std::min(static_cast<UINT>(state.position[1] * static_cast<float>(height)), height - 1);
  target->format = (view_format != DXGI_FORMAT_UNKNOWN ? view_format : desc.Format);
  target->description = std::format("{} {} {}x{}{}{} at {},{}", slot, FormatName(target->format), width, height,
                                    (mip != 0 ? std::format(" mip {}", mip) : std::string()),
                                    (desc.ArraySize > 1 ? std::format(" slice {}", slice) : std::string()), x, y);
  target->texture = std::move(texture);
  target->subresource = D3D11CalcSubresource(mip, slice, desc.MipLevels);
  target->texel = {.left = x, .top = y, .front = 0, .right = x + 1, .bottom = y + 1, .back = 1};
  return true;
}

// A copy of the target's texel now, or NO_SAMPLE once MAX_SAMPLES are taken.
inline size_t Copy(ID3D11DeviceContext* context, const Target& target) {
  if (state.samples.size() >= MAX_SAMPLES) return NO_SAMPLE;
  D3D11_TEXTURE2D_DESC desc = {};
  target.texture->GetDesc(&desc);
  const D3D11_TEXTURE2D_DESC staging_desc = {
      .Width = 1,
      .Height = 1,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = desc.Format,
      .SampleDesc = {.Count = 1, .Quality = 0},
      .Usage = D3D11_USAGE_STAGING,
      .BindFlags = 0,
      .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
      .MiscFlags = 0,
  };
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  Sample sample = {.size = std::min<uint32_t>(reshade::api::format_row_pitch(static_cast<reshade::api::format>(desc.Format), 1), 16)};
  if (device == nullptr || FAILED(device->CreateTexture2D(&staging_desc, nullptr, sample.staging.GetAddressOf()))) {
    return NO_SAMPLE;
  }
  context->CopySubresourceRegion(sample.staging.Get(), 0, 0, 0, 0, target.texture.Get(), target.subresource, &target.texel);
  state.samples.push_back(std::move(sample));
  return state.samples.size() - 1;
}

inline ID3D11DeviceContext* NativeContext(reshade::api::command_list* cmd_list) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  return native;
}

// The command before's targets now hold its result; then this command's targets (their texels before it, unless a
// command before already wrote them).
inline void Record(ID3D11DeviceContext* context, std::string description, std::string inputs, std::vector<Target> targets) {
  if (!state.commands.empty()) {
    for (auto& target : state.commands.back().targets) {
      target.after = Copy(context, target);
      state.latest[{target.texture.Get(), target.subresource}] = target.after;
    }
  }
  for (auto& target : targets) {
    const auto key = std::make_pair(target.texture.Get(), target.subresource);
    const auto known = state.latest.find(key);
    target.before = (known != state.latest.end() ? known->second : Copy(context, target));
    state.latest[key] = target.before;
  }
  state.commands.push_back({.index = state.command_count, .description = std::move(description), .inputs = std::move(inputs),
                            .targets = std::move(targets)});
}

// The shader's input textures (t0-t15), for the log.
template <typename GetResources>
std::string Inputs(GetResources get_resources) {
  std::array<ID3D11ShaderResourceView*, TRACED_INPUTS> views = {};
  get_resources(views.data());
  std::string inputs;
  for (UINT slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    ComPtr<ID3D11ShaderResourceView> view;
    view.Attach(views[slot]);
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    view->GetDesc(&view_desc);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    std::string size = "buffer";
    if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource.As(&texture))) {
      D3D11_TEXTURE2D_DESC desc = {};
      texture->GetDesc(&desc);
      size = std::format("{}x{}{}{}", desc.Width, desc.Height, (desc.MipLevels > 1 ? std::format(" {} mips", desc.MipLevels) : std::string()),
                         (desc.ArraySize > 1 ? std::format(" {} slices", desc.ArraySize) : std::string()));
    } else if (ComPtr<ID3D11Texture3D> volume; SUCCEEDED(resource.As(&volume))) {
      D3D11_TEXTURE3D_DESC desc = {};
      volume->GetDesc(&desc);
      size = std::format("{}x{}x{}", desc.Width, desc.Height, desc.Depth);
    }
    inputs += std::format("{}t{} {} {}", (inputs.empty() ? "" : ", "), slot, FormatName(view_desc.Format), size);
  }
  return inputs;
}

inline void OnGraphics(reshade::api::command_list* cmd_list, const char* kind) {
  const std::unique_lock lock(state.mutex);
  if (!state.tracing) return;
  ++state.command_count;  // every command, also those without a traced target
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return;
  std::array<ID3D11RenderTargetView*, TRACED_SLOTS> views = {};
  context->OMGetRenderTargets(TRACED_SLOTS, views.data(), nullptr);
  std::vector<Target> targets;
  for (UINT slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    ComPtr<ID3D11RenderTargetView> view;
    view.Attach(views[slot]);
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    view->GetDesc(&desc);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    UINT mip = 0;
    UINT slice = 0;
    if (desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) {
      mip = desc.Texture2D.MipSlice;
    } else if (desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY) {
      mip = desc.Texture2DArray.MipSlice;
      slice = desc.Texture2DArray.FirstArraySlice;
    } else {
      continue;
    }
    if (Target target; MakeTarget(resource.Get(), mip, slice, desc.Format, std::format("RT{}", slot), &target)) {
      targets.push_back(std::move(target));
    }
  }
  if (targets.empty()) return;
  const auto vertex = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
  const auto pixel = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
  Record(context, std::format("{} vs 0x{:08X} ps 0x{:08X}", kind, vertex, pixel),
         Inputs([&](ID3D11ShaderResourceView** views) { context->PSGetShaderResources(0, TRACED_INPUTS, views); }),
         std::move(targets));
}

inline void OnCompute(reshade::api::command_list* cmd_list, const char* kind) {
  const std::unique_lock lock(state.mutex);
  if (!state.tracing) return;
  ++state.command_count;  // every command, also those without a traced target
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return;
  std::array<ID3D11UnorderedAccessView*, TRACED_SLOTS> views = {};
  context->CSGetUnorderedAccessViews(0, TRACED_SLOTS, views.data());
  std::vector<Target> targets;
  for (UINT slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    ComPtr<ID3D11UnorderedAccessView> view;
    view.Attach(views[slot]);
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    view->GetDesc(&desc);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    UINT mip = 0;
    UINT slice = 0;
    if (desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D) {
      mip = desc.Texture2D.MipSlice;
    } else if (desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2DARRAY) {
      mip = desc.Texture2DArray.MipSlice;
      slice = desc.Texture2DArray.FirstArraySlice;
    } else {
      continue;
    }
    if (Target target; MakeTarget(resource.Get(), mip, slice, desc.Format, std::format("u{}", slot), &target)) {
      targets.push_back(std::move(target));
    }
  }
  if (targets.empty()) return;
  const auto compute = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::compute_shader);
  Record(context, std::format("{} cs 0x{:08X}", kind, compute),
         Inputs([&](ID3D11ShaderResourceView** views) { context->CSGetShaderResources(0, TRACED_INPUTS, views); }),
         std::move(targets));
}

// A clear or copy into `resource` (subresource `subresource`).
inline void OnWrite(reshade::api::command_list* cmd_list, ID3D11Resource* resource, UINT subresource, DXGI_FORMAT view_format,
                    const std::string& description) {
  const std::unique_lock lock(state.mutex);
  if (!state.tracing) return;
  ++state.command_count;  // every command, also those without a traced target
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  ComPtr<ID3D11Texture2D> texture;
  if (context == nullptr || resource == nullptr || FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) return;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  std::vector<Target> targets(1);
  if (!MakeTarget(resource, subresource % desc.MipLevels, subresource / desc.MipLevels, view_format, "dest", targets.data())) return;
  Record(context, description, {}, std::move(targets));
}

inline std::string SourceName(reshade::api::resource source) {
  ComPtr<ID3D11Texture2D> texture;
  auto* resource = reinterpret_cast<ID3D11Resource*>(source.handle);
  if (resource == nullptr || FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) return "a buffer or volume";
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  return std::format("{} {}x{}", FormatName(desc.Format), desc.Width, desc.Height);
}

inline bool OnDraw(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, uint32_t) {
  OnGraphics(cmd_list, "draw");
  return false;
}

inline bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  OnGraphics(cmd_list, "draw indexed");
  return false;
}

inline bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t) {
  OnCompute(cmd_list, "dispatch");
  return false;
}

inline bool OnIndirect(reshade::api::command_list* cmd_list, reshade::api::indirect_command type, reshade::api::resource,
                       uint64_t, uint32_t, uint32_t) {
  if (type == reshade::api::indirect_command::dispatch) {
    OnCompute(cmd_list, "dispatch indirect");
  } else {
    OnGraphics(cmd_list, "draw indirect");
  }
  return false;
}

inline bool OnClearRenderTarget(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4],
                                uint32_t, const reshade::api::rect*) {
  auto* view = reinterpret_cast<ID3D11RenderTargetView*>(rtv.handle);
  if (view == nullptr) return false;
  D3D11_RENDER_TARGET_VIEW_DESC desc = {};
  view->GetDesc(&desc);
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  UINT subresource = 0;
  if (desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) {
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(resource.As(&texture))) {
      D3D11_TEXTURE2D_DESC texture_desc = {};
      texture->GetDesc(&texture_desc);
      subresource = D3D11CalcSubresource(desc.Texture2D.MipSlice, 0, texture_desc.MipLevels);
    }
  }
  OnWrite(cmd_list, resource.Get(), subresource, desc.Format,
          std::format("clear render target to ({:g}, {:g}, {:g}, {:g})", color[0], color[1], color[2], color[3]));
  return false;
}

inline bool OnClearUnorderedAccess(reshade::api::command_list* cmd_list, reshade::api::resource_view uav, const float values[4],
                                   uint32_t, const reshade::api::rect*) {
  auto* view = reinterpret_cast<ID3D11UnorderedAccessView*>(uav.handle);
  if (view == nullptr) return false;
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
  view->GetDesc(&desc);
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  OnWrite(cmd_list, resource.Get(), 0, desc.Format,
          std::format("clear UAV to ({:g}, {:g}, {:g}, {:g})", values[0], values[1], values[2], values[3]));
  return false;
}

inline bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest) {
  OnWrite(cmd_list, reinterpret_cast<ID3D11Resource*>(dest.handle), 0, DXGI_FORMAT_UNKNOWN, "copy from " + SourceName(source));
  return false;
}

inline bool OnCopyTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t,
                                const reshade::api::subresource_box*, reshade::api::resource dest, uint32_t dest_subresource,
                                const reshade::api::subresource_box*, reshade::api::filter_mode) {
  OnWrite(cmd_list, reinterpret_cast<ID3D11Resource*>(dest.handle), dest_subresource, DXGI_FORMAT_UNKNOWN,
          "copy region from " + SourceName(source));
  return false;
}

inline bool OnResolve(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t,
                      const reshade::api::subresource_box*, reshade::api::resource dest, uint32_t dest_subresource, uint32_t,
                      uint32_t, uint32_t, reshade::api::format) {
  OnWrite(cmd_list, reinterpret_cast<ID3D11Resource*>(dest.handle), dest_subresource, DXGI_FORMAT_UNKNOWN,
          "resolve from " + SourceName(source));
  return false;
}

inline void Events(bool attach) {
  if (attach) {
    reshade::register_event<reshade::addon_event::draw>(OnDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnIndirect);
    reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTarget);
    reshade::register_event<reshade::addon_event::clear_unordered_access_view_float>(OnClearUnorderedAccess);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
    reshade::register_event<reshade::addon_event::resolve_texture_region>(OnResolve);
  } else {
    reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnIndirect);
    reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTarget);
    reshade::unregister_event<reshade::addon_event::clear_unordered_access_view_float>(OnClearUnorderedAccess);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
    reshade::unregister_event<reshade::addon_event::resolve_texture_region>(OnResolve);
  }
}

// Overlay: trace the next frame at `x`, `y` (0-1 of the screen).
inline void Request(float x, float y) {
  const std::unique_lock lock(state.mutex);
  if (state.tracing) return;
  state.requested = true;
  state.requested_position = {x, y};
  state.status = "Waiting for the next frame...";
}

inline std::string Status() {
  const std::shared_lock lock(state.mutex);
  return state.status;
}

// Stops tracing without a result (unload).
inline void Stop() {
  const std::unique_lock lock(state.mutex);
  if (!state.tracing) return;
  Events(false);
  state.tracing = false;
  state.commands.clear();
  state.samples.clear();
  state.latest.clear();
}

// At each Present (before it): starts a requested trace for the next frame, or ends the running one and saves it.
inline void OnPresent(reshade::api::command_queue* queue) {
  const std::unique_lock lock(state.mutex);
  if (!state.tracing) {
    if (!state.requested) return;
    state.requested = false;
    state.tracing = true;
    state.position = state.requested_position;
    state.commands.clear();
    state.samples.clear();
    state.latest.clear();
    state.command_count = 0;
    Events(true);
    return;
  }
  Events(false);
  state.tracing = false;
  ID3D11DeviceContext* context = NativeContext(queue->get_immediate_command_list());
  if (context != nullptr && !state.commands.empty()) {
    for (auto& target : state.commands.back().targets) target.after = Copy(context, target);
  }
  for (auto& sample : state.samples) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (context == nullptr || FAILED(context->Map(sample.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;
    std::memcpy(sample.bytes.data(), mapped.pData, sample.size);
    context->Unmap(sample.staging.Get(), 0);
    sample.staging.Reset();
  }
  std::array<wchar_t, MAX_PATH> exe = {};
  GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
  const auto path = std::filesystem::path(exe.data()).parent_path() / "tales_pixel_trace.txt";
  std::ofstream file(path);
  file << std::format("Pixel trace at {:.1f}% x {:.1f}% of each target ({} commands, {} with traced targets, {} texel copies{}).\n",
                      state.position[0] * 100.f, state.position[1] * 100.f, state.command_count, state.commands.size(),
                      state.samples.size(), (state.samples.size() >= MAX_SAMPLES ? ", limit reached: later values unknown" : ""))
       << "Each line: command number, what it is (shader hashes), the target it changed, its texel before -> after, and the\n"
       << "shader's input textures.\n\n";
  size_t changed = 0;
  for (const auto& command : state.commands) {
    for (const auto& target : command.targets) {
      if (target.before == NO_SAMPLE || target.after == NO_SAMPLE) continue;
      const auto& before = state.samples[target.before].bytes;
      const auto& after = state.samples[target.after].bytes;
      if (before == after) continue;
      ++changed;
      file << std::format("#{} {} | {} | {} -> {}", command.index, command.description, target.description,
                          Decode(target.format, before), Decode(target.format, after));
      if (!command.inputs.empty()) file << " | " << command.inputs;
      file << "\n";
    }
  }
  state.commands.clear();
  state.samples.clear();
  state.latest.clear();
  state.status = std::format("Saved {} changes of the pixel to {}.", changed, path.string());
  renodx::utils::log::i("[Tales pixel trace] ", state.status);
}

}  // namespace talesofarise::pixel_trace
