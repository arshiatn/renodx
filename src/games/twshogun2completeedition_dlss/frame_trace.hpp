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
#include <span>
#include <string>
#include <string_view>

#include <nvsdk_ngx_helpers.h>
#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/state.hpp"

// Debug (overlay button): every draw, dispatch, copy and render target clear of one frame with its shaders, targets
// (all 8), viewport and inputs, and where the game's DLSS runs (with the masks it gets), into shogun2_frame_trace.txt
// next to the exe. For the render scale work: which passes draw the 3D image at the display size. Its events are
// registered only for the traced frame.
// Capture (v16, overlay button): what the next CAPTURE_FRAMES DLSS evaluations get (color, motion vectors, depth: their
// render subrect, raw texels), for the bushes and tree trunks that flicker with jitter (ATN debug14).
namespace twshogun2::frame_trace {

namespace internal {

using Microsoft::WRL::ComPtr;

constexpr uint32_t MAX_LINES = 8000;
constexpr int CAPTURE_FRAMES = 4;

struct State {
  std::mutex mutex;
  std::atomic_bool requested = false;
  std::atomic_bool tracing = false;
  std::string text;
  uint32_t lines = 0;
  std::string last_outputs;  // the previous draw's targets
  std::string last_camera;   // the last camera constants written
  std::string status;
  std::atomic_int captures_left = 0;  // DLSS evaluations still to capture
};
static State state;

inline std::filesystem::path ExecutableFolder() {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  return std::filesystem::path(exe).parent_path();
}

inline std::string DescribeResource(ID3D11Resource* resource) {
  if (resource == nullptr) return "-";
  D3D11_TEXTURE2D_DESC desc = {};
  if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
    texture->GetDesc(&desc);
    return std::format("{} {}x{}{} f{}", static_cast<const void*>(resource), desc.Width, desc.Height,
                       (desc.ArraySize > 1 ? std::format(" [{}]", desc.ArraySize) : std::string()),
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

// The camera constants (camera.hlsl camera_VS_PS, 44 float4: bound at b0 by most of the game's shaders), read back
// through a staging copy: the buffer and a hash of all of it, time_in_sec / prev_time_in_sec c35.xy, screen_size c37,
// g_viewport_dimensions c38, g_viewport_origin c39, g_render_target_dimensions c40, g_camera_jitter c42.zw,
// g_prev_camera_jitter c43.xy, the projection's jitter terms (projection [2][0] / [2][1]: c6.z, c7.z). Empty if b0 is
// not that buffer.
inline std::string DescribeCamera(ID3D11DeviceContext* context, ID3D11Buffer* buffer) {
  constexpr UINT CAMERA_SIZE = 44 * 16;
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
  uint32_t hash = 2166136261u;  // FNV-1a
  for (const uint8_t byte : std::span(static_cast<const uint8_t*>(mapped.pData), CAMERA_SIZE)) {
    hash = (hash ^ byte) * 16777619u;
  }
  std::string text = std::format(
      " | camera {} #{:08X}: screen {}x{} viewport {}x{} at {},{} rt {}x{} jitter ({:.6f}, {:.6f}) prev ({:.6f}, "
      "{:.6f}) projection jitter ({:.6f}, {:.6f}) time {:.4f} prev {:.4f} (time - prev {:.9g})",
      static_cast<const void*>(buffer), hash, c[37 * 4], c[37 * 4 + 1], c[38 * 4], c[38 * 4 + 1], c[39 * 4],
      c[39 * 4 + 1], c[40 * 4], c[40 * 4 + 1], c[42 * 4 + 2], c[42 * 4 + 3], c[43 * 4], c[43 * 4 + 1], c[6 * 4 + 2],
      c[7 * 4 + 2], c[35 * 4], c[35 * 4 + 1], c[35 * 4] - c[35 * 4 + 1]);
  context->Unmap(staging.Get(), 0);
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
    outputs += std::format(" ds {}", DescribeView(depth));
    depth->Release();
  }
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  std::array<ID3D11ShaderResourceView*, 16> views = {};
  context->PSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  std::string inputs;
  for (uint32_t slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    inputs += std::format(" t{} {}", slot, DescribeView(views[slot]));
    views[slot]->Release();
  }
  // The camera constants once per run of draws into the same targets, and whenever they change (v16: do the trees
  // draw with other ones?).
  ComPtr<ID3D11Buffer> constants;
  context->VSGetConstantBuffers(0, 1, constants.GetAddressOf());
  std::string camera = DescribeCamera(context, constants.Get());
  {
    const std::lock_guard lock(state.mutex);
    const bool new_targets = (outputs != state.last_outputs);
    state.last_outputs = outputs;
    if (new_targets || camera != state.last_camera) {
      state.last_camera = camera;
    } else {
      camera.clear();
    }
  }
  Append(std::format("{} {} vs {:08X} ps {:08X} |{} | vp {}x{} at {},{} |{}{}", kind, count,
                     renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader),
                     renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader),
                     outputs, viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY, inputs, camera));
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
  std::array<ID3D11ShaderResourceView*, 16> views = {};
  context->CSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  std::string inputs;
  for (uint32_t slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    inputs += std::format(" t{} {}", slot, DescribeView(views[slot]));
    views[slot]->Release();
  }
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
  Append(std::format("clear {} to ({}, {}, {}, {})",
                               DescribeView(reinterpret_cast<ID3D11RenderTargetView*>(view.handle)), color[0],
                               color[1], color[2], color[3]));
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
  Append(std::format("copy region {} -> {}",
                               DescribeResource(reinterpret_cast<ID3D11Resource*>(source.handle)),
                               DescribeResource(reinterpret_cast<ID3D11Resource*>(dest.handle))));
  return false;
}

// The traced frame's events, registered only for it (otherwise no cost per draw).
inline void UseEvents(bool use) {
  if (use) {
    reshade::register_event<reshade::addon_event::draw>(OnDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
  } else {
    reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
  }
}

}  // namespace internal

inline void Request() {
  internal::state.requested.store(true);
}

// Files next to the exe: shogun2_dlss_capture_<frame>_<color / motion / depth>.bin (header: width, height, DXGI
// format, bytes per texel, uint32 each; then the rows) and shogun2_dlss_capture.txt (each frame's parameters).
inline void RequestCapture() {
  internal::state.captures_left.store(internal::CAPTURE_FRAMES);
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
    const auto path = internal::ExecutableFolder() / L"shogun2_frame_trace.txt";
    const std::lock_guard lock(state.mutex);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << state.text;
    state.status = std::format("Trace: {} lines saved to {}", state.lines, path.string());
    state.text.clear();
  }
  if (state.requested.exchange(false)) {
    {
      const std::lock_guard lock(state.mutex);
      state.text = "One frame: draws, dispatches and copies in order (vs / ps / cs hashes, targets, viewport, inputs).\n";
      state.lines = 0;
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

// The game's DLSS evaluation (game_dlss's callback).
inline void OnEvaluate(ID3D11DeviceContext* context, const NVSDK_NGX_D3D11_DLSS_Eval_Params& eval) {
  auto& state = internal::state;
  if (state.tracing.load(std::memory_order_relaxed)) {
    internal::Append(std::format(
        "DLSS evaluate: color {} output {} depth {} motion vectors {} exposure {} particle mask {} bias mask {} "
        "transparency mask {} | render subrect {}x{} jitter ({}, {}) MV scale ({}, {}) reset {}",
        internal::DescribeResource(eval.Feature.pInColor), internal::DescribeResource(eval.Feature.pInOutput),
        internal::DescribeResource(eval.pInDepth), internal::DescribeResource(eval.pInMotionVectors),
        internal::DescribeResource(eval.pInExposureTexture), internal::DescribeResource(eval.pInIsParticleMask),
        internal::DescribeResource(eval.pInBiasCurrentColorMask), internal::DescribeResource(eval.pInTransparencyMask),
        eval.InRenderSubrectDimensions.Width, eval.InRenderSubrectDimensions.Height, eval.InJitterOffsetX,
        eval.InJitterOffsetY, eval.InMVScaleX, eval.InMVScaleY, eval.InReset));
  }

  const int captures_left = state.captures_left.load();
  if (captures_left <= 0 || context == nullptr) return;
  state.captures_left.store(captures_left - 1);
  const int frame = internal::CAPTURE_FRAMES - captures_left;
  const auto folder = internal::ExecutableFolder();
  const uint32_t width = eval.InRenderSubrectDimensions.Width;
  const uint32_t height = eval.InRenderSubrectDimensions.Height;
  // One input's render subrect, raw (a staging copy of the whole texture; depth-stencil copies cannot take a box).
  auto save = [&](ID3D11Resource* resource, std::string_view name) -> std::string {
    internal::ComPtr<ID3D11Texture2D> texture;
    if (resource == nullptr || FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
      return std::format("{} -", name);
    }
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    uint32_t texel_size = 0;
    switch (desc.Format) {
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R32G8X24_TYPELESS:
        texel_size = 8;
        break;
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16_TYPELESS:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32_TYPELESS:
      case DXGI_FORMAT_R24G8_TYPELESS:
      case DXGI_FORMAT_R11G11B10_FLOAT:
        texel_size = 4;
        break;
      default:
        return std::format("{} f{} not saved", name, static_cast<int>(desc.Format));
    }
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    internal::ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    internal::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()))) {
      return std::format("{} staging failed", name);
    }
    context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return std::format("{} map failed", name);
    const std::array<uint32_t, 4> header = {std::min(width, desc.Width), std::min(height, desc.Height),
                                            static_cast<uint32_t>(desc.Format), texel_size};
    std::ofstream file(folder / std::format("shogun2_dlss_capture_{}_{}.bin", frame, name),
                       std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    for (uint32_t row = 0; row < header[1]; ++row) {
      file.write(static_cast<const char*>(mapped.pData) + (static_cast<size_t>(row) * mapped.RowPitch),
                 static_cast<std::streamsize>(header[0]) * texel_size);
    }
    context->Unmap(staging.Get(), 0);
    return std::format("{} {}x{} f{}", name, header[0], header[1], static_cast<int>(desc.Format));
  };
  const std::string color = save(eval.Feature.pInColor, "color");
  const std::string motion = save(eval.pInMotionVectors, "motion");
  const std::string depth = save(eval.pInDepth, "depth");
  const auto mode = (frame == 0 ? std::ios::trunc : std::ios::app);
  std::ofstream(folder / L"shogun2_dlss_capture.txt", std::ios::binary | mode) << std::format(
      "frame {}: render subrect {}x{} jitter ({}, {}) MV scale ({}, {}) reset {} | {} | {} | {}\n", frame, width, height,
      eval.InJitterOffsetX, eval.InJitterOffsetY, eval.InMVScaleX, eval.InMVScaleY, eval.InReset, color, motion, depth);
  if (captures_left == 1) {
    const std::lock_guard lock(state.mutex);
    state.status = std::format("DLSS inputs: {} frames saved to {}", internal::CAPTURE_FRAMES,
                               (folder / L"shogun2_dlss_capture_*").string());
  }
}

}  // namespace twshogun2::frame_trace
