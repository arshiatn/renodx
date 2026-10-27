/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"

// Debug (ReShade.ini [renodx] FrameTrace=1, read at start): one frame, 600 presents after the DLSS render scale began,
// every draw, dispatch, clear and copy with its shaders, targets, viewport, inputs and the camera's screen / viewport
// sizes (camera_VS_PS c27 / c29 at b0) into tw_frame_trace.txt next to the exe, with the anti-aliasing's marks (tone
// mapper, DLSS). For the render scale audit (tools\tw\scale_audit.py): which passes read the 3D image's part of a
// display-size texture as if it filled it. Its events are registered only for the traced frame.
namespace twattila::frame_trace {

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
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  return context;
}

inline void Append(const std::string& line) {
  const std::lock_guard lock(state.mutex);
  if (state.lines++ >= MAX_LINES) return;
  state.text += line;
  state.text += '\n';
}

// The camera at b0 (Attila's camera_VS_PS: g_screen_size c27, g_viewport_dimensions c29), read back; empty if b0 is not
// camera-sized.
inline std::string DescribeCamera(ID3D11DeviceContext* context, ID3D11Buffer* buffer) {
  constexpr UINT CAMERA_SIZE = 30 * 16;
  if (buffer == nullptr) return "";
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (desc.ByteWidth < CAMERA_SIZE || desc.ByteWidth > 4096) return "";
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING,
                                          .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  if (FAILED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()))) return "";
  context->CopyResource(staging.Get(), buffer);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return "";
  const auto* c = static_cast<const float*>(mapped.pData);
  std::string text = std::format(" | camera screen {}x{} viewport {}x{}", c[27 * 4], c[27 * 4 + 1], c[29 * 4], c[29 * 4 + 1]);
  context->Unmap(staging.Get(), 0);
  return text;
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
  Append(std::format("{} {} vs {:08X} ps {:08X} |{} | vp {}x{} at {},{} |{}{}", kind, count,
                     renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader),
                     renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader),
                     outputs, viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY, inputs,
                     DescribeCamera(context, constants.Get())));
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

inline bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t x, uint32_t y, uint32_t z) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return false;
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
  Append(std::format("dispatch {}x{}x{} cs {:08X} |{} |{}{}", x, y, z,
                     renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::compute_shader),
                     outputs, inputs, DescribeCamera(context, constants.Get())));
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
    reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
    reshade::register_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);
  } else {
    reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
    reshade::unregister_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);
  }
}

}  // namespace internal

// At the addon's start: whether this run traces a frame.
inline void Install() {
  bool wanted = false;
  reshade::get_config_value(nullptr, "renodx", "FrameTrace", wanted);
  const std::lock_guard lock(internal::state.mutex);
  internal::state.wanted = wanted;
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
      start = true;
    }
  }
  if (start) {
    internal::UseEvents(true);
    state.tracing.store(true);
  }
}

}  // namespace twattila::frame_trace
