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
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/state.hpp"

// Debug (Advanced button, from the Shogun 2 DLSS addon): every draw, dispatch, copy and render target clear of one
// frame with its shaders, targets (all 8 and depth), viewport, scissor, inputs and Unreal's view size, into
// tales_frame_trace.txt next to the exe. For DLSS upscaling: at a render scale below 100%, which passes after the TAA
// run at the render size and which at the display size, where the game's spatial upscale is, and which formats they
// use. Its events are registered only for the traced frame.
namespace talesofarise::frame_trace {

namespace internal {

using Microsoft::WRL::ComPtr;

constexpr uint32_t MAX_LINES = 12000;
// UE 4.21's View uniform buffer: ViewSizeAndInvSize at c122, BufferSizeAndInvSize at c123 (the TAA's b1, README).
constexpr UINT VIEW_SIZE_CONSTANT = 122;
constexpr UINT BUFFER_SIZE_CONSTANT = 123;

struct State {
  std::mutex mutex;
  std::atomic_bool requested = false;
  std::atomic_bool tracing = false;
  std::string text;
  uint32_t lines = 0;
  std::string last_view;  // the last view sizes written
  std::unordered_set<uint32_t> saved_shaders;
  std::string status = "No frame trace yet.";
};
static State state;

inline std::filesystem::path ExecutableFolder() {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  return std::filesystem::path(exe).parent_path();
}

inline std::string DescribeResource(ID3D11Resource* resource) {
  if (resource == nullptr) return "-";
  if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    return std::format("{} {}x{}{}{} f{}", static_cast<const void*>(resource), desc.Width, desc.Height,
                       (desc.ArraySize > 1 ? std::format(" [{}]", desc.ArraySize) : std::string()),
                       (desc.MipLevels > 1 ? std::format(" {} mips", desc.MipLevels) : std::string()),
                       static_cast<int>(desc.Format));
  }
  if (ComPtr<ID3D11Texture3D> volume; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(volume.GetAddressOf())))) {
    D3D11_TEXTURE3D_DESC desc = {};
    volume->GetDesc(&desc);
    return std::format("{} {}x{}x{} f{}", static_cast<const void*>(resource), desc.Width, desc.Height, desc.Depth,
                       static_cast<int>(desc.Format));
  }
  return std::format("{} (buffer)", static_cast<const void*>(resource));
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

inline void Append(std::string line) {
  const std::lock_guard lock(state.mutex);
  if (state.lines++ >= MAX_LINES) return;
  state.text += line;
  state.text += '\n';
}

// Unreal's view and buffer size from the first bound constant buffer that holds a View uniform buffer (read back
// through a staging copy; a buffer counts when c122 / c123 are sizes with their inverses), else empty.
inline std::string DescribeUnrealView(ID3D11DeviceContext* context, const std::array<ID3D11Buffer*, 14>& buffers) {
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  for (ID3D11Buffer* buffer : buffers) {
    if (buffer == nullptr) continue;
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    if (desc.ByteWidth < (BUFFER_SIZE_CONSTANT + 1) * 16) continue;
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING,
                                            .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()))) continue;
    context->CopyResource(staging.Get(), buffer);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;
    const auto* view = static_cast<const float*>(mapped.pData) + VIEW_SIZE_CONSTANT * 4;
    const auto* size = static_cast<const float*>(mapped.pData) + BUFFER_SIZE_CONSTANT * 4;
    const auto is_size = [](const float* c) {
      return c[0] >= 1.f && c[1] >= 1.f && c[0] <= 16384.f && c[1] <= 16384.f && std::abs(c[0] * c[2] - 1.f) < 1e-3f
             && std::abs(c[1] * c[3] - 1.f) < 1e-3f;
    };
    std::string text;
    if (is_size(view) && is_size(size)) {
      text = std::format(" | view {}x{} buffer {}x{}", view[0], view[1], size[0], size[1]);
    }
    context->Unmap(staging.Get(), 0);
    if (!text.empty()) return text;
  }
  return "";
}

// Each traced shader's bytecode once, as tales_frame_trace_shaders\0x<hash>.cso next to the exe (when an addon keeps
// the game's shader code, as the DevKit does).
inline void SaveShader(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stage, uint32_t hash) {
  if (hash == 0u) return;
  {
    const std::lock_guard lock(state.mutex);
    if (!state.saved_shaders.insert(hash).second) return;
  }
  const auto* info = renodx::utils::state::GetBoundPipelineInfo(cmd_list, stage);
  if (info == nullptr) return;
  for (const auto& entry : info->shader_details) {
    if (entry.shader_hash != hash || entry.subobject_index >= info->details.subobjects.size()) continue;
    const auto* desc = static_cast<const reshade::api::shader_desc*>(info->details.subobjects[entry.subobject_index].data);
    if (desc == nullptr || desc->code_size == 0) return;
    const auto folder = ExecutableFolder() / L"tales_frame_trace_shaders";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / std::format("0x{:08X}.cso", hash), std::ios::binary | std::ios::trunc)
        .write(static_cast<const char*>(desc->code), static_cast<std::streamsize>(desc->code_size));
    return;
  }
}

template <typename GetViews>
std::string DescribeInputs(GetViews get_views) {
  std::array<ID3D11ShaderResourceView*, 16> views = {};
  get_views(views.data());
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
    outputs += std::format(" ds {}", DescribeView(depth));
    depth->Release();
  }
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  D3D11_RECT scissor = {};
  UINT scissor_count = 1;
  context->RSGetScissorRects(&scissor_count, &scissor);
  const std::string inputs = DescribeInputs([&](ID3D11ShaderResourceView** views) { context->PSGetShaderResources(0, 16, views); });
  // The view sizes once per change (they stay for most of a pass).
  std::array<ID3D11Buffer*, 14> constants = {};
  context->PSGetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
  std::string view = DescribeUnrealView(context, constants);
  if (view.empty()) {
    for (auto*& buffer : constants) {
      if (buffer != nullptr) buffer->Release();
      buffer = nullptr;
    }
    context->VSGetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
    view = DescribeUnrealView(context, constants);
  }
  for (auto* buffer : constants) {
    if (buffer != nullptr) buffer->Release();
  }
  {
    const std::lock_guard lock(state.mutex);
    if (view == state.last_view) {
      view.clear();
    } else {
      state.last_view = view;
    }
  }
  const uint32_t vertex_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
  const uint32_t pixel_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
  SaveShader(cmd_list, reshade::api::pipeline_stage::vertex_shader, vertex_hash);
  SaveShader(cmd_list, reshade::api::pipeline_stage::pixel_shader, pixel_hash);
  Append(std::format("{} {} vs {:08X} ps {:08X} |{} | vp {}x{} at {},{}{} |{}{}", kind, count, vertex_hash, pixel_hash,
                     outputs, viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY,
                     (scissor_count != 0 ? std::format(" scissor {},{}-{},{}", scissor.left, scissor.top, scissor.right, scissor.bottom)
                                         : std::string()),
                     inputs, view));
}

inline bool OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t,
                   uint32_t) {
  TraceDraw(cmd_list, "draw", vertex_count * instance_count);
  return false;
}

inline bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count,
                          uint32_t, int32_t, uint32_t) {
  TraceDraw(cmd_list, "indexed", index_count * instance_count);
  return false;
}

inline void TraceDispatch(reshade::api::command_list* cmd_list, const std::string& kind) {
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
  const std::string inputs = DescribeInputs([&](ID3D11ShaderResourceView** views) { context->CSGetShaderResources(0, 16, views); });
  std::array<ID3D11Buffer*, 14> constants = {};
  context->CSGetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
  const std::string view = DescribeUnrealView(context, constants);
  for (auto* buffer : constants) {
    if (buffer != nullptr) buffer->Release();
  }
  const uint32_t compute_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::compute_shader);
  SaveShader(cmd_list, reshade::api::pipeline_stage::compute_shader, compute_hash);
  Append(std::format("{} cs {:08X} |{} |{}{}", kind, compute_hash, outputs, inputs, view));
}

inline bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t x, uint32_t y, uint32_t z) {
  TraceDispatch(cmd_list, std::format("dispatch {}x{}x{}", x, y, z));
  return false;
}

inline bool OnIndirect(reshade::api::command_list* cmd_list, reshade::api::indirect_command type, reshade::api::resource,
                       uint64_t, uint32_t, uint32_t) {
  if (type == reshade::api::indirect_command::dispatch) {
    TraceDispatch(cmd_list, "dispatch indirect");
  } else {
    TraceDraw(cmd_list, "draw indirect", 0);
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

// The traced frame's events, registered only for it (otherwise no cost per draw).
inline void UseEvents(bool use) {
  if (use) {
    reshade::register_event<reshade::addon_event::draw>(OnDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnIndirect);
    reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
  } else {
    reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnIndirect);
    reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
  }
}

}  // namespace internal

inline void Request() {
  internal::state.requested.store(true);
}

inline std::string Status() {
  const std::lock_guard lock(internal::state.mutex);
  return internal::state.status;
}

// Present: ends a traced frame (writes the file), starts a requested one.
inline void OnPresent() {
  auto& state = internal::state;
  if (state.tracing.exchange(false)) {
    internal::UseEvents(false);
    const auto path = internal::ExecutableFolder() / L"tales_frame_trace.txt";
    const std::lock_guard lock(state.mutex);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << state.text;
    state.status = std::format("Frame trace: {} lines saved to {}", state.lines, path.string());
    state.text.clear();
  }
  if (state.requested.exchange(false)) {
    {
      const std::lock_guard lock(state.mutex);
      state.text =
          "One frame: draws, dispatches, clears and copies in order (vs / ps / cs hashes, targets, viewport, scissor,\n"
          "inputs; Unreal's view and buffer size when they change). Formats: DXGI numbers (10 RGBA16F, 24 RGB10A2,\n"
          "26 R11G11B10F, 28 RGBA8, 29 RGBA8 sRGB, 87 BGRA8).\n";
      state.lines = 0;
      state.last_view.clear();
      state.saved_shaders.clear();
    }
    internal::UseEvents(true);
    state.tracing.store(true);
  }
}

// Unload: a frame being traced is dropped.
inline void Stop() {
  if (internal::state.tracing.exchange(false)) {
    internal::UseEvents(false);
  }
}

}  // namespace talesofarise::frame_trace
