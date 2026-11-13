/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <type_traits>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./frame_history.hpp"
#include "./light_bounce.hpp"

// AO filter: the ambient occlusion's Temporal Smoothing and Denoiser for the games whose AO ao_pass.hpp does not draw
// (Troy, Pharaoh, Warhammer I-III: their own passes and line sweeps). Before the frame's first sun light draw of the main
// view, the AO texture the sun draws read (SunDraw::ao_slot) goes through ao_filter_resolve (the draw's G-buffer depth
// and camera, last frame's history and camera) and ao_filter_denoise into the addon's texture at the display's size,
// which the frame's sun draws read in its place. The game's compute bindings are put back after.
namespace twattila::ao_filter {

using Microsoft::WRL::ComPtr;

struct Settings {
  bool temporal = false;
  bool edge_fix = false;
  bool denoise = false;
};

namespace internal {

struct ShaderSettings {
  float width = 0.f;
  float height = 0.f;
  float inverse_width = 0.f;
  float inverse_height = 0.f;
  float history = 0.f;
  float edge_fix = 0.f;
  float denoise = 0.f;
  float padding = 0.f;
  float texture_inverse_width = 0.f;
  float texture_inverse_height = 0.f;
  float padding2[2] = {};
};

struct Texture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

// Made on the game's device, again when it changes; the textures again when the display's size does.
struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3D11ComputeShader> resolve;
  ComPtr<ID3D11ComputeShader> resolve_msaa;
  ComPtr<ID3D11ComputeShader> denoise;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11Buffer> settings;
  ShaderSettings uploaded = {};
  ComPtr<ID3D11Buffer> camera;           // the sun draw's in Attila's layout
  ComPtr<ID3D11Buffer> previous_camera;  // last frame's (frame_history::KeepCamera)
  UINT width = 0;
  UINT height = 0;
  std::array<UINT, 2> filtered_size = {};  // the 3D size of the last filtered frame
  std::array<Texture, 2> history;  // the result, its distance, frames (RGBA16F)
  size_t history_index = 0;        // this frame's
  Texture output;                  // the sun draws' AO (RGBA16F)
};

struct Runtime {
  std::shared_mutex mutex;
  Settings settings;
  uint64_t frame = 0;
  UINT display_width = 0;
  UINT display_height = 0;
  uint64_t filtered_frame = UINT64_MAX;
  ID3D11ShaderResourceView* game_ao = nullptr;  // this frame's (compared only)
  DeviceObjects objects;
};

static Runtime runtime;

inline bool Enabled() {
  return runtime.settings.temporal || runtime.settings.denoise;
}

// The shaders on `device`, the textures at `width` x `height`. Under the lock.
inline bool Prepare(ID3D11Device* device, UINT width, UINT height) {
  auto& objects = runtime.objects;
  if (objects.device.Get() != device) {
    objects = {};
    objects.device = device;
    const D3D11_SAMPLER_DESC sampler = {
        .Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
        .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    const D3D11_BUFFER_DESC settings = {.ByteWidth = sizeof(ShaderSettings), .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    objects.failed =
        FAILED(device->CreateComputeShader(__ao_filter_resolve.data(), __ao_filter_resolve.size(), nullptr, objects.resolve.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__ao_filter_resolve_ms.data(), __ao_filter_resolve_ms.size(), nullptr,
                                              objects.resolve_msaa.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__ao_filter_denoise.data(), __ao_filter_denoise.size(), nullptr, objects.denoise.GetAddressOf()))
        || FAILED(device->CreateSamplerState(&sampler, objects.sampler.GetAddressOf()))
        || FAILED(device->CreateBuffer(&settings, nullptr, objects.settings.GetAddressOf()));
    if (objects.failed) renodx::utils::log::w("[Total War] AO filter: could not create its shaders: off.");
  }
  if (objects.failed) return false;
  if (objects.output.texture && objects.width == width && objects.height == height) return true;
  objects.history = {};
  objects.output = {};
  objects.width = width;
  objects.height = height;
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = width,
      .Height = height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
  };
  for (Texture* texture : {&objects.history[0], &objects.history[1], &objects.output}) {
    if (FAILED(device->CreateTexture2D(&desc, nullptr, texture->texture.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(texture->texture.Get(), nullptr, texture->srv.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(texture->texture.Get(), nullptr, texture->uav.GetAddressOf()))) {
      renodx::utils::log::w("[Total War] AO filter: could not create its textures (", width, " x ", height, ").");
      objects.output = {};
      return false;
    }
  }
  runtime.filtered_frame = UINT64_MAX;
  return true;
}

// This frame's filtered AO from the game's (`game_ao`), the G-buffer depth (`depth`; `msaa`: multisampled) and the
// camera in Attila's layout, `sun_mapping` (null: none) the sun draw's cb_ssao at b3; the compute stage's bindings put
// back after. Under the lock.
inline void Filter(ID3D11DeviceContext* native, ID3D11ShaderResourceView* game_ao, ID3D11ShaderResourceView* depth,
                   bool msaa, ID3D11Buffer* camera, ID3D11Buffer* sun_mapping) {
  auto& objects = runtime.objects;
  // Last frame's history only if it was made last frame at this 3D size (DLSS: the textures' top-left corner).
  const auto size = frame_history::RenderSize(objects.width, objects.height);
  const bool history = runtime.settings.temporal && runtime.filtered_frame + 1 == runtime.frame && size == objects.filtered_size;
  objects.filtered_size = size;
  const ShaderSettings settings = {
      .width = static_cast<float>(size[0]),
      .height = static_cast<float>(size[1]),
      .inverse_width = 1.f / static_cast<float>(size[0]),
      .inverse_height = 1.f / static_cast<float>(size[1]),
      .history = (history ? 1.f : 0.f),
      .edge_fix = (runtime.settings.edge_fix ? 1.f : 0.f),
      .denoise = (runtime.settings.denoise ? 1.f : 0.f),
      .texture_inverse_width = 1.f / static_cast<float>(objects.width),
      .texture_inverse_height = 1.f / static_cast<float>(objects.height),
  };
  if (std::memcmp(&settings, &objects.uploaded, sizeof(ShaderSettings)) != 0) {
    native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &settings, 0, 0);
    objects.uploaded = settings;
  }

  ComPtr<ID3D11ComputeShader> game_shader;
  native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  std::array<ID3D11ShaderResourceView*, 3> game_views = {};
  native->CSGetShaderResources(0, 3, game_views.data());
  ID3D11UnorderedAccessView* game_uav = nullptr;
  native->CSGetUnorderedAccessViews(0, 1, &game_uav);
  std::array<ID3D11Buffer*, 3> game_constants = {};
  native->CSGetConstantBuffers(0, 3, game_constants.data());
  ID3D11SamplerState* game_sampler = nullptr;
  native->CSGetSamplers(0, 1, &game_sampler);
  ComPtr<ID3D11Buffer> game_mapping;
  if (sun_mapping != nullptr) {
    native->CSGetConstantBuffers(3, 1, game_mapping.GetAddressOf());
    native->CSSetConstantBuffers(3, 1, &sun_mapping);
  }

  const UINT previous_index = objects.history_index;
  objects.history_index = 1 - objects.history_index;
  const std::array<ID3D11Buffer*, 3> constants = {camera, objects.previous_camera.Get(), objects.settings.Get()};
  native->CSSetConstantBuffers(0, 3, constants.data());
  native->CSSetSamplers(0, 1, objects.sampler.GetAddressOf());
  const auto pass = [&](ID3D11ComputeShader* shader, ID3D11ShaderResourceView* history_view, ID3D11UnorderedAccessView* output) {
    constexpr UINT KEEP_COUNT = UINT_MAX;
    ID3D11UnorderedAccessView* no_uav = nullptr;
    native->CSSetUnorderedAccessViews(0, 1, &no_uav, &KEEP_COUNT);
    const std::array<ID3D11ShaderResourceView*, 3> views = {game_ao, depth, history_view};
    native->CSSetShaderResources(0, 3, views.data());
    native->CSSetUnorderedAccessViews(0, 1, &output, &KEEP_COUNT);
    native->CSSetShader(shader, nullptr, 0);
    native->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
  };
  pass((msaa ? objects.resolve_msaa : objects.resolve).Get(), objects.history[previous_index].srv.Get(),
       objects.history[objects.history_index].uav.Get());
  pass(objects.denoise.Get(), objects.history[objects.history_index].srv.Get(), objects.output.uav.Get());
  frame_history::KeepCamera(objects.device.Get(), native, camera, std::addressof(objects.previous_camera));

  constexpr UINT KEEP_COUNT = UINT_MAX;
  native->CSSetShader(game_shader.Get(), nullptr, 0);
  native->CSSetUnorderedAccessViews(0, 1, &game_uav, &KEEP_COUNT);
  native->CSSetShaderResources(0, 3, game_views.data());
  native->CSSetConstantBuffers(0, 3, game_constants.data());
  native->CSSetSamplers(0, 1, &game_sampler);
  if (sun_mapping != nullptr) native->CSSetConstantBuffers(3, 1, game_mapping.GetAddressOf());
  for (auto* view : game_views) {
    if (view != nullptr) view->Release();
  }
  if (game_uav != nullptr) game_uav->Release();
  for (auto* buffer : game_constants) {
    if (buffer != nullptr) buffer->Release();
  }
  if (game_sampler != nullptr) game_sampler->Release();
}

}  // namespace internal

// A sun draw (the pass's light_bounce::SunDraw): at the frame's first of the main view its AO filtered; then each reads
// the addon's in place of the game's.
constexpr auto OnSunDraw = []<typename T, typename Context>(
                               Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
    if (!frame_history::MainViewport(native)) return {};
    auto& runtime = internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    if (!internal::Enabled()) return {};
    const auto& sun_draw = command.template GetCallbackData<T>();
    if (sun_draw.ao_slot == UINT_MAX) return {};
    ComPtr<ID3D11ShaderResourceView> game_ao;
    native->PSGetShaderResources(sun_draw.ao_slot, 1, game_ao.GetAddressOf());
    if (!game_ao) return {};
    if (runtime.filtered_frame != runtime.frame) {
      // The main view's: its G-buffer depth at the display's size.
      ComPtr<ID3D11ShaderResourceView> depth;
      native->PSGetShaderResources(sun_draw.gbuffer_slot + 3, 1, depth.GetAddressOf());
      ComPtr<ID3D11Buffer> game_camera;
      native->PSGetConstantBuffers(sun_draw.camera_slot, 1, game_camera.GetAddressOf());
      if (!depth || !game_camera) return {};
      ComPtr<ID3D11Resource> depth_resource;
      depth->GetResource(depth_resource.GetAddressOf());
      ComPtr<ID3D11Texture2D> depth_texture;
      D3D11_TEXTURE2D_DESC depth_desc = {};
      if (FAILED(depth_resource.As(&depth_texture))) return {};
      depth_texture->GetDesc(&depth_desc);
      if (depth_desc.Width != runtime.display_width || depth_desc.Height != runtime.display_height) return {};
      ComPtr<ID3D11Device> device;
      native->GetDevice(device.GetAddressOf());
      if (!internal::Prepare(device.Get(), depth_desc.Width, depth_desc.Height)) return {};
      auto* camera = frame_history::AttilaCamera(device.Get(), native, game_camera.Get(), sun_draw.camera_layout,
                                                 std::addressof(runtime.objects.camera));
      ComPtr<ID3D11Buffer> sun_mapping;
      if (sun_draw.ssao_constants_slot != UINT_MAX) native->PSGetConstantBuffers(sun_draw.ssao_constants_slot, 1, sun_mapping.GetAddressOf());
      internal::Filter(native, game_ao.Get(), depth.Get(), depth_desc.SampleDesc.Count > 1, camera, sun_mapping.Get());
      runtime.filtered_frame = runtime.frame;
      runtime.game_ao = game_ao.Get();
    }
    if (game_ao.Get() == runtime.game_ao) {
      native->PSSetShaderResources(sun_draw.ao_slot, 1, runtime.objects.output.srv.GetAddressOf());
    }
    return {};
  }
};

// Every present: the settings and the main view's size (the display's).
inline void Update(reshade::api::swapchain* swapchain, const Settings& settings) {
  reshade::api::resource_desc display = {};
  if (settings.temporal || settings.denoise) {
    display = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
  }
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.settings = settings;
  // Ray Reconstruction or NRD denoises it: the AO's own smoothing and denoiser leave its noise to them.
  if (dlaa_bridge::bridge.reconstructing || dlaa_bridge::bridge.nrd_ambient) {
    internal::runtime.settings.temporal = false;
    internal::runtime.settings.denoise = false;
  }
  // Off: its memory back (made again with its next pass).
  if (!internal::Enabled() && internal::runtime.objects.device) {
    internal::runtime.objects = {};
  }
  internal::runtime.display_width = display.texture.width;
  internal::runtime.display_height = display.texture.height;
  ++internal::runtime.frame;
}

// The game's sun draws (kept for as long as it is installed); those with an ao_slot are filtered.
inline void Install(std::span<const light_bounce::SunDraw> draws) {
  for (const auto& draw : draws) {
    if (draw.ao_slot == UINT_MAX) continue;
    renodx::utils::command_action::Register(
        OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &draw);
  }
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.objects.device.Get() != native) return;
  internal::runtime.objects = {};
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(OnSunDraw);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.objects = {};
}

}  // namespace twattila::ao_filter
