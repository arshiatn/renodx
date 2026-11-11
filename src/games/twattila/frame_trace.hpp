/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"

// Debug (ReShade.ini [renodx] FrameTrace=1, read at start): one frame, 600 presents after the DLSS render scale began,
// every draw, dispatch (direct and indirect), clear and copy with its shaders, targets, viewport, inputs and the camera's
// screen / viewport sizes (the camera at b0, its registers from Install) into tw_frame_trace.txt next to the exe, with
// the anti-aliasing's marks (tone mapper, DLSS). For the render scale audit (tools\tw\scale_audit.py): which passes read
// the 3D image's part of a display-size texture as if it filled it. Its events are registered only for the traced frame.
namespace twattila::frame_trace {

// `count` values of a buffer from `offset` bytes on, read back (it waits for the GPU: for traced frames); empty: past the
// buffer's end or no read here (a deferred context).
template <typename T>
std::vector<T> ReadBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, UINT offset, UINT count) {
  const UINT bytes = count * static_cast<UINT>(sizeof(T));
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (offset + bytes > desc.ByteWidth) return {};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_STAGING,
                                          .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
  if (FAILED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()))) return {};
  const D3D11_BOX box = {.left = offset, .top = 0, .front = 0, .right = offset + bytes, .bottom = 1, .back = 1};
  context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, buffer, 0, &box);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
  const auto* values = static_cast<const T*>(mapped.pData);
  std::vector<T> result(values, values + count);
  context->Unmap(staging.Get(), 0);
  return result;
}

namespace internal {

using Microsoft::WRL::ComPtr;

constexpr uint32_t MAX_LINES = 20000;
constexpr uint32_t SCALED_PRESENTS = 600;

struct State {
  std::mutex mutex;
  bool wanted = false;      // the ini asked for one trace this run
  uint32_t scaled_presents = 0;
  std::atomic_bool tracing = false;
  std::string text;
  uint32_t lines = 0;
  // Attila's camera_VS_PS: g_screen_size, g_viewport_dimensions.
  uint32_t camera_screen_register = 27;
  uint32_t camera_viewport_register = 29;
  std::vector<uint32_t> value_hashes;  // Install's: a draw of these shaders, once each in a trace, its constants too
  std::vector<uint32_t> values_written;
  bool indirect_values_written = false;
};
static State state;

inline std::string DescribeResource(ID3D11Resource* resource) {
  if (resource == nullptr) return "-";
  if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    return std::format("{} {}x{}{}{} f{}", static_cast<const void*>(resource), desc.Width, desc.Height,
                       (desc.ArraySize > 1 ? std::format(" [{}]", desc.ArraySize) : std::string()),
                       (desc.SampleDesc.Count > 1 ? std::format(" ms{}", desc.SampleDesc.Count) : std::string()),
                       static_cast<int>(desc.Format));
  }
  return std::format("{} buffer", static_cast<const void*>(resource));
}

template <typename View>
std::string DescribeView(View* view) {
  if (view == nullptr) return "-";
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  return DescribeResource(resource.Get());
}

inline ID3D11DeviceContext* NativeContext(reshade::api::command_list* cmd_list) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  return context;
}

inline void Append(const std::string& line) {
  const std::lock_guard lock(state.mutex);
  if (state.lines++ >= MAX_LINES) return;
  state.text += line;
  state.text += '\n';
}

// The camera at b0, read back; empty if b0 is not camera-sized.
inline std::string DescribeCamera(ID3D11DeviceContext* context, ID3D11Buffer* buffer) {
  if (buffer == nullptr) return "";
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (desc.ByteWidth > 4096) return "";
  const UINT screen = state.camera_screen_register * 4;
  const UINT viewport = state.camera_viewport_register * 4;
  const auto c = ReadBuffer<float>(context, buffer, 0, std::max(screen, viewport) + 4);
  if (c.empty()) return "";
  return std::format(" | camera screen {}x{} viewport {}x{}", c[screen], c[screen + 1], c[viewport], c[viewport + 1]);
}

template <typename GetViews>
std::string DescribeInputs(GetViews get_views) {
  std::array<ID3D11ShaderResourceView*, 24> views = {};
  get_views(static_cast<UINT>(views.size()), views.data());
  std::string inputs;
  for (uint32_t slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    inputs += std::format(" t{} {}", slot, DescribeView(views[slot]));
    views[slot]->Release();
  }
  return inputs;
}

// What a draw reads and how it blends and tests, for the trace's next line: vertex buffers (slot, stride, whether the CPU
// writes it), other stages' shaders, each target's write mask and blend, stencil.
inline std::string DescribeDrawState(reshade::api::command_list* cmd_list, ID3D11DeviceContext* context) {
  std::string text = " | state";
  for (const auto [name, stage] : {std::pair{"gs", reshade::api::pipeline_stage::geometry_shader}, std::pair{"hs", reshade::api::pipeline_stage::hull_shader},
                                   std::pair{"ds", reshade::api::pipeline_stage::domain_shader}}) {
    if (const uint32_t hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, stage); hash != 0) text += std::format(" {} {:08X}", name, hash);
  }
  std::array<ID3D11Buffer*, 4> buffers = {};
  std::array<UINT, 4> strides = {};
  std::array<UINT, 4> offsets = {};
  context->IAGetVertexBuffers(0, static_cast<UINT>(buffers.size()), buffers.data(), strides.data(), offsets.data());
  for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
    if (buffers[slot] == nullptr) continue;
    D3D11_BUFFER_DESC desc = {};
    buffers[slot]->GetDesc(&desc);
    text += std::format(" vb{} stride {}{}", slot, strides[slot], (desc.Usage == D3D11_USAGE_DYNAMIC ? " dynamic" : ""));
    buffers[slot]->Release();
  }
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  context->IAGetPrimitiveTopology(&topology);
  text += std::format(" topology {}", static_cast<int>(topology));
  ComPtr<ID3D11BlendState> blend;
  context->OMGetBlendState(blend.GetAddressOf(), nullptr, nullptr);
  D3D11_BLEND_DESC blend_desc = {};
  if (blend) {
    blend->GetDesc(&blend_desc);
    for (uint32_t slot = 0; slot < 8; ++slot) {
      const auto& target = blend_desc.RenderTarget[blend_desc.IndependentBlendEnable != FALSE ? slot : 0];
      text += std::format(" m{}={:x}{}", slot, target.RenderTargetWriteMask, (target.BlendEnable != FALSE ? "b" : ""));
    }
  } else {
    text += " m=default";
  }
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_ref = 0;
  context->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_ref);
  D3D11_DEPTH_STENCIL_DESC depth_desc = {};
  if (depth_state) depth_state->GetDesc(&depth_desc);
  if (depth_desc.StencilEnable != FALSE) text += std::format(" stencil ref {} read {:x} write {:x}", stencil_ref, depth_desc.StencilReadMask, depth_desc.StencilWriteMask);
  // Which faces the draw shows (D3D11_CULL_MODE: 1 both, 2 culls the front, 3 the back): a mesh the picture hides by its facing still stands in the rays.
  ComPtr<ID3D11RasterizerState> rasterizer;
  context->RSGetState(rasterizer.GetAddressOf());
  if (rasterizer) {
    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer->GetDesc(&rasterizer_desc);
    text += std::format(" cull {}{} bias {}", static_cast<int>(rasterizer_desc.CullMode), (rasterizer_desc.FrontCounterClockwise != FALSE ? " ccw" : ""), rasterizer_desc.DepthBias);
  }
  return text;
}

// The first floats of a draw's constant buffers (VS and PS, b0 to b3).
inline std::string DescribeConstants(ID3D11DeviceContext* context) {
  std::string text;
  const auto read = [&](const char* stage, std::array<ID3D11Buffer*, 4> buffers) {
    for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
      if (buffers[slot] == nullptr) continue;
      D3D11_BUFFER_DESC desc = {};
      buffers[slot]->GetDesc(&desc);
      text += std::format("\n  {} b{} {} bytes:", stage, slot, desc.ByteWidth);
      for (const float value : ReadBuffer<float>(context, buffers[slot], 0, std::min(desc.ByteWidth / 4, 64u))) text += std::format(" {}", value);
      buffers[slot]->Release();
    }
  };
  std::array<ID3D11Buffer*, 4> buffers = {};
  context->VSGetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
  read("vs", buffers);
  buffers = {};
  context->PSGetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
  read("ps", buffers);
  return text;
}

inline void TraceDraw(reshade::api::command_list* cmd_list, std::string_view kind, uint32_t count) {
  if (!state.tracing.load(std::memory_order_relaxed)) return;
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return;
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets = {};
  ID3D11DepthStencilView* depth = nullptr;
  context->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), &depth);
  std::string outputs;
  for (uint32_t slot = 0; slot < targets.size(); ++slot) {
    if (targets[slot] == nullptr) continue;
    outputs += std::format(" rt{} {}", slot, DescribeView(targets[slot]));
    targets[slot]->Release();
  }
  if (depth != nullptr) {
    // The depth test's comparison (0: off) and w: depth written (D3D11's default state without one bound).
    ComPtr<ID3D11DepthStencilState> depth_state;
    context->OMGetDepthStencilState(depth_state.GetAddressOf(), nullptr);
    D3D11_DEPTH_STENCIL_DESC depth_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL, .DepthFunc = D3D11_COMPARISON_LESS};
    if (depth_state) depth_state->GetDesc(&depth_desc);
    outputs += std::format(" ds {} z{}{}", DescribeView(depth), depth_desc.DepthEnable ? static_cast<int>(depth_desc.DepthFunc) : 0,
                           depth_desc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL ? "w" : "");
    depth->Release();
  }
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  const std::string inputs = DescribeInputs([&](UINT count, ID3D11ShaderResourceView** views) { context->PSGetShaderResources(0, count, views); });
  ComPtr<ID3D11Buffer> constants;
  context->PSGetConstantBuffers(0, 1, constants.GetAddressOf());
  if (!constants) context->VSGetConstantBuffers(0, 1, constants.GetAddressOf());
  const uint32_t vertex_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
  const uint32_t pixel_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
  // The trace's first indirect draw (the game's particles draw so) and Install's shaders, once each: their constants.
  bool values = false;
  {
    const std::lock_guard lock(state.mutex);
    if (kind.ends_with("indirect") && !state.indirect_values_written) {
      state.indirect_values_written = true;
      values = true;
    }
    for (const uint32_t hash : {vertex_hash, pixel_hash}) {
      if (std::ranges::find(state.value_hashes, hash) != state.value_hashes.end() && std::ranges::find(state.values_written, hash) == state.values_written.end()) {
        state.values_written.push_back(hash);
        values = true;
      }
    }
  }
  Append(std::format("{} {} vs {:08X} ps {:08X} |{} | vp {}x{} at {},{} |{}{}{}{}", kind, count, vertex_hash, pixel_hash,
                     outputs, viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY, inputs,
                     DescribeCamera(context, constants.Get()), DescribeDrawState(cmd_list, context), (values ? DescribeConstants(context) : std::string())));
}

inline bool OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t, uint32_t) {
  TraceDraw(cmd_list, "draw", vertex_count * instance_count);
  return false;
}

inline bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t,
                          int32_t, uint32_t) {
  TraceDraw(cmd_list, "indexed", index_count * instance_count);
  return false;
}

inline void TraceDispatch(reshade::api::command_list* cmd_list, std::string_view kind, uint32_t x, uint32_t y, uint32_t z) {
  if (!state.tracing.load(std::memory_order_relaxed)) return;
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return;
  std::array<ID3D11UnorderedAccessView*, 8> uavs = {};
  context->CSGetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data());
  std::string outputs;
  for (uint32_t slot = 0; slot < uavs.size(); ++slot) {
    if (uavs[slot] == nullptr) continue;
    outputs += std::format(" u{} {}", slot, DescribeView(uavs[slot]));
    uavs[slot]->Release();
  }
  const std::string inputs = DescribeInputs([&](UINT count, ID3D11ShaderResourceView** views) { context->CSGetShaderResources(0, count, views); });
  ComPtr<ID3D11Buffer> constants;
  context->CSGetConstantBuffers(0, 1, constants.GetAddressOf());
  Append(std::format("{} {}x{}x{} cs {:08X} |{} |{}{}", kind, x, y, z,
                     renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::compute_shader),
                     outputs, inputs, DescribeCamera(context, constants.Get())));
}

inline bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t x, uint32_t y, uint32_t z) {
  TraceDispatch(cmd_list, "dispatch", x, y, z);
  return false;
}

// D3D11's one command at `offset`, its numbers read from the GPU's buffer (0: no read here).
inline bool OnDrawOrDispatchIndirect(reshade::api::command_list* cmd_list, reshade::api::indirect_command type, reshade::api::resource buffer,
                                     uint64_t offset, uint32_t, uint32_t) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return false;
  auto arguments = ReadBuffer<uint32_t>(context, reinterpret_cast<ID3D11Buffer*>(buffer.handle), static_cast<UINT>(offset), 3);
  arguments.resize(3);
  if (type == reshade::api::indirect_command::dispatch) {
    TraceDispatch(cmd_list, "dispatch indirect", arguments[0], arguments[1], arguments[2]);
  } else {
    // Both draws' arguments start with the count per instance and the instances.
    TraceDraw(cmd_list, (type == reshade::api::indirect_command::draw_indexed ? "indexed indirect" : "draw indirect"), arguments[0] * arguments[1]);
  }
  return false;
}

inline bool OnClearRenderTargetView(reshade::api::command_list*, reshade::api::resource_view view, const float color[4],
                                    uint32_t, const reshade::api::rect*) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  Append(std::format("clear {} to ({}, {}, {}, {})", DescribeView(reinterpret_cast<ID3D11RenderTargetView*>(view.handle)),
                     color[0], color[1], color[2], color[3]));
  return false;
}

inline bool OnCopyResource(reshade::api::command_list*, reshade::api::resource source, reshade::api::resource dest) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  Append(std::format("copy {} -> {}", DescribeResource(reinterpret_cast<ID3D11Resource*>(source.handle)),
                     DescribeResource(reinterpret_cast<ID3D11Resource*>(dest.handle))));
  return false;
}

inline bool OnCopyTextureRegion(reshade::api::command_list*, reshade::api::resource source, uint32_t,
                                const reshade::api::subresource_box*, reshade::api::resource dest, uint32_t,
                                const reshade::api::subresource_box*, reshade::api::filter_mode) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  Append(std::format("copy region {} -> {}", DescribeResource(reinterpret_cast<ID3D11Resource*>(source.handle)),
                     DescribeResource(reinterpret_cast<ID3D11Resource*>(dest.handle))));
  return false;
}

inline bool OnResolveTextureRegion(reshade::api::command_list*, reshade::api::resource source, uint32_t,
                                   const reshade::api::subresource_box*, reshade::api::resource dest, uint32_t, uint32_t,
                                   uint32_t, uint32_t, reshade::api::format) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  Append(std::format("resolve {} -> {}", DescribeResource(reinterpret_cast<ID3D11Resource*>(source.handle)),
                     DescribeResource(reinterpret_cast<ID3D11Resource*>(dest.handle))));
  return false;
}

inline void UseEvents(bool use) {
  if (use) {
    reshade::register_event<reshade::addon_event::draw>(OnDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);
    reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
    reshade::register_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);
  } else {
    reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);
    reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
    reshade::unregister_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);
  }
}

}  // namespace internal

// At the addon's start: whether this run traces a frame; the camera's g_screen_size and g_viewport_dimensions registers;
// shaders whose first draw in a trace also lists its constants.
inline void Install(uint32_t camera_screen_register = 27, uint32_t camera_viewport_register = 29, std::vector<uint32_t> value_hashes = {}) {
  bool wanted = false;
  reshade::get_config_value(nullptr, "renodx", "FrameTrace", wanted);
  const std::lock_guard lock(internal::state.mutex);
  internal::state.wanted = wanted;
  internal::state.camera_screen_register = camera_screen_register;
  internal::state.camera_viewport_register = camera_viewport_register;
  internal::state.value_hashes = std::move(value_hashes);
}

// A mark in the traced frame (the anti-aliasing's tone mapper end, DLSS).
// A trace of the next frame given to OnPresent as scaled (debug: on demand).
inline void StartSoon() {
  const std::lock_guard lock(internal::state.mutex);
  internal::state.wanted = true;
  internal::state.scaled_presents = internal::SCALED_PRESENTS - 1;
}

// Whether a trace is still to come this run (FrameTrace).
inline bool Wanted() {
  const std::lock_guard lock(internal::state.mutex);
  return internal::state.wanted;
}

// Whether this frame is traced (marks that cost something to build are made only then).
inline bool Tracing() {
  return internal::state.tracing.load(std::memory_order_relaxed);
}

inline void Mark(std::string_view text) {
  if (internal::state.tracing.load(std::memory_order_relaxed)) internal::Append(std::string("MARK ") + std::string(text));
}

// Every present (`scaled`: the DLSS render scale ran this frame): ends a traced frame (writes the file); starts one
// after SCALED_PRESENTS scaled presents.
inline void OnPresent(bool scaled) {
  auto& state = internal::state;
  if (state.tracing.exchange(false)) {
    internal::UseEvents(false);
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    const auto path = std::filesystem::path(exe).parent_path() / L"tw_frame_trace.txt";
    const std::lock_guard lock(state.mutex);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << state.text;
    renodx::utils::log::i("[Total War] Frame trace: ", state.lines, " lines saved to ", path.string());
    state.text.clear();
    return;
  }
  bool start = false;
  {
    const std::lock_guard lock(state.mutex);
    if (!state.wanted) return;
    state.scaled_presents = scaled ? state.scaled_presents + 1 : 0;
    if (state.scaled_presents >= internal::SCALED_PRESENTS) {
      state.wanted = false;
      state.text = "One frame at the DLSS render scale: draws, dispatches, copies (vs / ps / cs, targets, viewport, inputs, camera).\n";
      state.lines = 0;
      state.values_written.clear();
      state.indirect_values_written = false;
      start = true;
    }
  }
  if (start) {
    internal::UseEvents(true);
    state.tracing.store(true);
  }
}

}  // namespace twattila::frame_trace
