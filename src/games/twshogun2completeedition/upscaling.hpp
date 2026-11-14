/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <format>
#include <mutex>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/dlaa_bridge.hpp"
#include "../twattila/frame_generation.hpp"
#include "../twshogun2completeedition_dlss/hdr_link.hpp"

// The helper's upscaler (Experimental, the DLSS addon's Upscaler): the game's DLSS evaluation (hdr_link.hpp, from the
// DLSS addon's hook of it) through this addon's bridge to renodx-tw-dlaa64.exe (twattila/dlaa_bridge.hpp: one a
// process, the ray tracing's too) in place of the game's NGX: NVIDIA's DLSS (with Ray Reconstruction when the rays run),
// AMD's FSR or Intel's XeSS. The game's textures into the bridge's (upscale_inputs), its result into the game's DLSS
// output (upscale_output). Any frame it can't (the helper not up, no answer, no camera for FSR / XeSS) keeps the game's
// DLSS. Every Prepare of the bridge asks the same (PrepareBridge): another set would make all of its textures again.
// Frame generation (the DLSS addon's, twattila/frame_generation.hpp: the helper presents): the same inputs (the bridge's
// depth and motion) with either upscaler, the frame's camera, its HUD-less image after the tone mapper (KeepFrame).
namespace twshogun2::upscaling {

using Microsoft::WRL::ComPtr;

// What the bridge serves, set each present: the ray tracing (installed), its guides (reflections, paths, Ray
// Reconstruction) and reflections, the upscaler (hdr_link: 0 the game's DLSS, else dlaa_bridge::UPSCALER_* + 1), Ray
// Reconstruction (0 off, else its NGX preset + 1; with the rays only) and frame generation (its frames' format:
// frame_generation::Format, unknown while off; its backend).
struct BridgeUse {
  bool ray_tracing = false;
  bool guides = false;
  bool reflections = false;
  uint32_t upscaler = 0;
  uint32_t ray_reconstruction = 0;
  DXGI_FORMAT frame_format = DXGI_FORMAT_UNKNOWN;
  uint32_t frame_backend = twattila::dlaa_bridge::FG_BACKEND_DLSS;
  uint32_t debug_view = 0;  // the Upscaling Debug View: 1 the motion the upscaler gets
};

namespace internal {

// upscale_inputs' / upscale_output's b0.
struct ShaderSettings {
  float square_root = 0.f;
  std::array<float, 2> motion_scale = {1.f, 1.f};
  float unused = 0.f;
  std::array<uint32_t, 2> render_size = {};
  uint32_t debug_view = 0;
  uint32_t unused_view = 0;
};

// A game texture's view, made again when the texture changes.
template <typename ViewType>
struct GameView {
  ComPtr<ID3D11Resource> resource;
  ComPtr<ViewType> view;
};

struct State {
  std::mutex mutex;
  BridgeUse use;
  uint64_t frame = 0;
  uint64_t evaluated_frame = UINT64_MAX;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> inputs;
  ComPtr<ID3D11ComputeShader> output;
  ComPtr<ID3D11Buffer> settings;
  GameView<ID3D11ShaderResourceView> color;
  GameView<ID3D11ShaderResourceView> depth;
  GameView<ID3D11ShaderResourceView> motion;
  GameView<ID3D11UnorderedAccessView> target;
  bool failed = false;
  bool logged = false;
  // The last camera given (Ray Reconstruction's, FSR's and XeSS's matrices; row vectors, the projection without its
  // jitter) and whether there was one.
  bool camera = false;
  std::array<float, 16> world_to_view = {};
  std::array<float, 16> view_to_clip = {};
  // Frame generation's inputs of the frame (`generation_frame`), the last frame's view-projection (registers).
  twattila::frame_generation::Inputs generation;
  uint64_t generation_frame = UINT64_MAX;
  std::array<float, 16> previous_view_projection = {};
  bool previous_kept = false;
};
static State state;

// A typeless game format as its views read it.
inline DXGI_FORMAT ViewFormat(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R24G8_TYPELESS:        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS:     return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS:          return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS:          return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R16G16_TYPELESS:       return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:                                return format;
  }
}

}  // namespace internal

// Each present: what the bridge serves this frame.
inline void SetUse(const BridgeUse& use) {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.use = use;
  ++internal::state.frame;
}

// Whether Ray Reconstruction is asked for (the sun draws then write the G-buffer guides).
inline bool Reconstructing() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.use.upscaler == twattila::dlaa_bridge::UPSCALER_DLSS + 1 && internal::state.use.ray_reconstruction != 0;
}

// Whether the helper upscaled the last frame (its colour then the bridge's: the ray tracing's last frame's light).
inline bool Upscaled() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.evaluated_frame + 1 >= internal::state.frame && internal::state.evaluated_frame != UINT64_MAX;
}

// The bridge at `width` x `height` (the display size) for what it serves; false: not available.
inline bool PrepareBridge(ID3D11Device* device, UINT width, UINT height) {
  BridgeUse use;
  {
    const std::unique_lock lock(internal::state.mutex);
    use = internal::state.use;
  }
  const bool reconstruct = use.upscaler == twattila::dlaa_bridge::UPSCALER_DLSS + 1 && use.ray_reconstruction != 0;
  return twattila::dlaa_bridge::Prepare(device, width, height, use.guides || reconstruct, use.ray_tracing, use.frame_format,
                                        use.reflections, (use.upscaler != 0) ? use.upscaler - 1 : twattila::dlaa_bridge::UPSCALER_DLSS,
                                        use.frame_backend);
}

// The DLSS addon's evaluation (hdr_link::Upscale) through the helper (frame generation alone: its inputs, the game's DLSS
// after); `guides_written`: the ray tracing's G-buffer guides this frame (Ray Reconstruction needs them). False: the game's
// DLSS runs.
inline bool Evaluate(const hdr_link::UpscaleRequest& request, bool guides_written) {
  auto* native = request.context;
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  BridgeUse use;
  {
    const std::unique_lock lock(internal::state.mutex);
    use = internal::state.use;
  }
  const bool generation = twattila::frame_generation::Wanted();
  ComPtr<ID3D11Texture2D> output;
  ComPtr<ID3D11DeviceContext1> native1;
  if ((use.upscaler == 0 && !generation) || native == nullptr || request.color == nullptr || request.depth == nullptr || request.motion == nullptr
      || request.output == nullptr || FAILED(request.output->QueryInterface(IID_PPV_ARGS(output.GetAddressOf())))
      || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
    return false;
  }
  D3D11_TEXTURE2D_DESC output_desc = {};
  output->GetDesc(&output_desc);
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!PrepareBridge(device.Get(), output_desc.Width, output_desc.Height)) return false;
  auto& bridge = twattila::dlaa_bridge::bridge;
  const bool nvidia_model = use.upscaler == twattila::dlaa_bridge::UPSCALER_DLSS + 1;
  const std::unique_lock lock(internal::state.mutex);
  auto& state = internal::state;
  if (state.device != device) {
    state.device = device;
    state.private_state.Reset();
    state.inputs.Reset();
    state.output.Reset();
    state.settings.Reset();
    state.color = {};
    state.depth = {};
    state.motion = {};
    state.target = {};
    ComPtr<ID3D11Device1> device1;
    const auto feature_level = device->GetFeatureLevel();
    const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                                 ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                                 : 0;
    const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = sizeof(internal::ShaderSettings), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                               nullptr, state.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__upscale_inputs.data(), __upscale_inputs.size(), nullptr, state.inputs.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__upscale_output.data(), __upscale_output.size(), nullptr, state.output.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&settings_desc, nullptr, state.settings.GetAddressOf()));
  }
  if (state.failed) return false;
  // The game's textures' views, again when they change.
  const auto shader_view = [&](ID3D11Resource* resource, internal::GameView<ID3D11ShaderResourceView>* cached) {
    if (cached->resource.Get() == resource) return cached->view.Get();
    cached->resource = resource;
    cached->view.Reset();
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc = {};
    if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) texture->GetDesc(&desc);
    const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = internal::ViewFormat(desc.Format), .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                       .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
    if (!texture || FAILED(device->CreateShaderResourceView(resource, &view_desc, cached->view.GetAddressOf()))) cached->view.Reset();
    return cached->view.Get();
  };
  auto* color = shader_view(request.color, &state.color);
  auto* depth = shader_view(request.depth, &state.depth);
  auto* motion = shader_view(request.motion, &state.motion);
  if (state.target.resource.Get() != request.output) {
    state.target.resource = request.output;
    state.target.view.Reset();
    const D3D11_UNORDERED_ACCESS_VIEW_DESC target_desc = {.Format = internal::ViewFormat(output_desc.Format), .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
    if (FAILED(device->CreateUnorderedAccessView(request.output, &target_desc, state.target.view.GetAddressOf()))) state.target.view.Reset();
  }
  if (!state.logged) {
    state.logged = true;
    renodx::utils::log::i(std::format("[Shogun 2] Helper upscaler {}: {}x{} -> {}x{}, motion scale {} {}, output format {}, views {} {} {} {}",
                                      use.upscaler - 1, request.render_width, request.render_height, output_desc.Width, output_desc.Height,
                                      request.motion_scale_x, request.motion_scale_y, static_cast<int>(output_desc.Format), color != nullptr,
                                      depth != nullptr, motion != nullptr, static_cast<bool>(state.target.view)));
  }
  if (color == nullptr || depth == nullptr || motion == nullptr || !state.target.view || request.render_width == 0
      || request.render_width > bridge.width || request.render_height > bridge.height) {
    return false;
  }
  // The camera's registers (column vectors): view c2-c5, projection c6-c9 without its jitter (its z column's x and y:
  // twattila/fg_camera.hpp's), position c0, near and far c34.
  constexpr size_t VIEW = 2 * 4;
  constexpr size_t PROJECTION = 6 * 4;
  constexpr size_t NEAR_FAR = 34 * 4;
  std::array<float, 16> view = {};
  std::array<float, 16> projection = {};
  std::copy_n(request.constants.begin() + VIEW, 16, view.begin());
  std::copy_n(request.constants.begin() + PROJECTION, 16, projection.begin());
  projection[2] = 0.f;
  projection[6] = 0.f;
  // Ray Reconstruction's, FSR's and XeSS's matrices (row vectors: the registers' transposes); the last camera kept for a
  // frame without one.
  if (request.camera) {
    for (size_t row = 0; row < 4; ++row) {
      for (size_t column = 0; column < 4; ++column) {
        state.world_to_view[(column * 4) + row] = view[(row * 4) + column];
        state.view_to_clip[(column * 4) + row] = projection[(row * 4) + column];
      }
    }
    state.camera = true;
  }
  if (use.upscaler != 0 && !nvidia_model && !state.camera) return false;
  const bool reconstruct = nvidia_model && use.ray_reconstruction != 0 && guides_written && bridge.guides && state.camera;
  // NVSDK_NGX_DLSS_Hint_Render_Preset L, M and Ray Reconstruction take the colour's square root; K, FSR and XeSS the light as it is.
  const bool square_root = nvidia_model && (reconstruct || request.preset == 12u || request.preset == 13u);
  bridge.color_square_root = square_root;
  const internal::ShaderSettings settings = {
      .square_root = square_root ? 1.f : 0.f,
      .motion_scale = {request.motion_scale_x, request.motion_scale_y},
      .render_size = {request.render_width, request.render_height},
      .debug_view = use.debug_view,
  };
  twattila::dlaa_bridge::BeforeSharedWrites(native);
  bool evaluated = false;
  {
    const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
    native->UpdateSubresource(state.settings.Get(), 0, nullptr, &settings, 0, 0);
    const std::array<ID3D11ShaderResourceView*, 3> sources = {color, depth, motion};
    const std::array<ID3D11UnorderedAccessView*, 3> destinations = {bridge.color.uav.Get(), bridge.depth.uav.Get(), bridge.motion.uav.Get()};
    native->CSSetShader(state.inputs.Get(), nullptr, 0);
    native->CSSetShaderResources(0, static_cast<UINT>(sources.size()), sources.data());
    native->CSSetUnorderedAccessViews(0, static_cast<UINT>(destinations.size()), destinations.data(), nullptr);
    native->CSSetConstantBuffers(0, 1, state.settings.GetAddressOf());
    native->Dispatch((request.render_width + 7) / 8, (request.render_height + 7) / 8, 1);
    native->ClearState();
    twattila::dlaa_bridge::RayReconstruction reconstruction = {.preset = (use.ray_reconstruction != 0) ? use.ray_reconstruction - 1 : 0u,
                                                               .world_to_view = state.world_to_view,
                                                               .view_to_clip = state.view_to_clip};
    evaluated = use.upscaler != 0
                && twattila::dlaa_bridge::Evaluate(native, request.render_width, request.render_height, request.jitter_x, request.jitter_y,
                                                   request.reset, request.preset, reconstruct ? &reconstruction : nullptr,
                                                   nvidia_model ? nullptr : &state.view_to_clip);
    if (evaluated) {
      // The Debug View's motion and the dimmed scene: the upscaler's inputs.
      const std::array<ID3D11ShaderResourceView*, 3> results = {bridge.output.srv.Get(), bridge.motion.srv.Get(), bridge.color.srv.Get()};
      ID3D11UnorderedAccessView* target = state.target.view.Get();
      native->CSSetShader(state.output.Get(), nullptr, 0);
      native->CSSetShaderResources(0, static_cast<UINT>(results.size()), results.data());
      native->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
      native->CSSetConstantBuffers(0, 1, state.settings.GetAddressOf());
      native->Dispatch((output_desc.Width + 7) / 8, (output_desc.Height + 7) / 8, 1);
    }
  }
  if (evaluated) state.evaluated_frame = state.frame;
  // Frame generation's camera: no jitter (its view-projection made again from the view and the projection without it).
  if (generation && request.camera) {
    std::array<float, 16> view_projection = {};
    for (size_t row = 0; row < 4; ++row) {
      for (size_t column = 0; column < 4; ++column) {
        for (size_t k = 0; k < 4; ++k) view_projection[(row * 4) + column] += projection[(row * 4) + k] * view[(k * 4) + column];
      }
    }
    state.generation = {
        .width = request.render_width,
        .height = request.render_height,
        .jitter_x = request.jitter_x,
        .jitter_y = request.jitter_y,
        .reset = request.reset,
        .near_plane = request.constants[NEAR_FAR],
        .far_plane = request.constants[NEAR_FAR + 1],
    };
    twattila::frame_generation::SetCamera(&state.generation, projection, view, view_projection,
                                           ((request.reset || !state.previous_kept) ? view_projection : state.previous_view_projection),
                                           {request.constants[0], request.constants[1], request.constants[2]});
    state.previous_view_projection = view_projection;
    state.previous_kept = true;
    state.generation_frame = state.frame;
  }
  return evaluated;
}

// After the tone mapper (tonemap00 / 01, the display size, before the UI; after the Debug View): frame generation's
// HUD-less image from its target and the frame's inputs.
inline void KeepFrame(reshade::api::command_list* cmd_list) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.generation_frame != internal::state.frame) return;
  internal::state.generation_frame = UINT64_MAX;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  if (ComPtr<ID3D11Texture2D> target; SUCCEEDED(resource.As(&target))) {
    twattila::frame_generation::KeepFrame(native, target.Get(), internal::state.generation);
  }
}

// The game's device is going away: the addon's objects on it with it, and the bridge's (after the ray tracing's).
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (const std::unique_lock lock(internal::state.mutex); internal::state.device.Get() == native) {
    auto& state = internal::state;
    state.device.Reset();
    state.private_state.Reset();
    state.inputs.Reset();
    state.output.Reset();
    state.settings.Reset();
    state.color = {};
    state.depth = {};
    state.motion = {};
    state.target = {};
    state.failed = false;
  }
  twattila::dlaa_bridge::Release();
}

}  // namespace twshogun2::upscaling
