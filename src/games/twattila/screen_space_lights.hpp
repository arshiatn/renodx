/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <embed/shaders.h>

#include "../../utils/log.hpp"
#include "./screen_space_shadows.hpp"

// Torch and Fire Shadows (Experimental): the screen-space shadows' march (sss_bend_extended's light mode) from the game's
// point lights, into the lit bits the ray-traced point light shadows give (rt_local_lights.hlsli: t42, b9), which the
// replaced light shaders read unchanged. rt_scene.hpp's light draw hook hands each light draw's instances over when no
// rays trace them: copied on the GPU (POSITION2: position and reach, POSITION1: colour); at the frame's first light draw
// last frame's copies give the lights (sss_lights: the rays' pick, the brightest and nearest, each once) and each one's
// Bend dispatches over the rectangle its reach covers, all marched in one indirect dispatch over this frame's depth (the
// sun draw's copy, screen_space_shadows.hpp: the light draws come after it). Positions one frame late, matched within
// the hook's 0.25 m. D3D11.
namespace twattila::screen_space_lights {

using Microsoft::WRL::ComPtr;

// The Torch and Fire Shadows row is shown where rt_scene.hpp's InstallPointLights hooks the game's light draws.
static bool installed = false;

namespace internal {

constexpr UINT INSTANCE_BYTES = 512 * 1024;  // a frame's light instances (rt_scene.hpp's readback's)
constexpr UINT MAX_DRAWS = 256;              // sss_lights' records
constexpr UINT MAX_LIGHTS = 32;              // rt_local_lights.hlsli's
constexpr UINT MAX_ENTRIES = 1 + (2 * MAX_LIGHTS * 8);  // the dispatch list: its header, two entries each of 8 a light
constexpr UINT LIGHT_LIST = (MAX_LIGHTS * 2) + 1;       // b9's float4s: positions, count, colours
constexpr UINT LIGHT_MASK_SLOT = 42;
constexpr UINT LIGHT_CONSTANTS_SLOT = 9;
static bool logged_order = false;

}  // namespace internal

// Whether the light draws take these shadows now (the screen-space shadows on with lights asked for).
inline bool Wanted() {
  const std::shared_lock lock(screen_space_shadows::internal::runtime.mutex);
  const auto& settings = screen_space_shadows::internal::runtime.settings;
  return settings.enabled && settings.lights != 0;
}

// A light draw's instances: `buffer` from byte `first` on, `count` of them `stride` apart, each one's position and reach
// at `position` bytes in, its colour at `colour`.
struct Instances {
  ID3D11Buffer* buffer = nullptr;
  UINT first = 0;
  UINT count = 0;
  UINT stride = 0;
  UINT position = 0;
  UINT colour = 0;
};

// A light draw (rt_scene.hpp's hook, no rays tracing them): its instances copied for the next frame; at the frame's
// first, this frame's lights from last frame's copies and their lit bits; they and the lights bound for it (t42, b9:
// rt_scene's UnbindLocalLights after). False: none this frame (nothing bound).
inline bool OnLightDraw(ID3D11DeviceContext* native, const Instances& instances) {
  using screen_space_shadows::internal::ComputeState;
  auto& runtime = screen_space_shadows::internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& objects = runtime.objects;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!runtime.settings.enabled || runtime.settings.lights == 0 || objects.device.Get() != device.Get() || !objects.bend_extended) return false;
  // Their objects on the screen-space shadows' device, made when first needed.
  if (!objects.lights_shader) {
    const auto buffer = [&](UINT bytes, UINT bind, UINT misc, UINT stride, ComPtr<ID3D11Buffer>* made) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind, .MiscFlags = misc, .StructureByteStride = stride};
      return SUCCEEDED(device->CreateBuffer(&desc, nullptr, made->GetAddressOf()));
    };
    constexpr UINT STRUCTURED = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    constexpr UINT READ_WRITE = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    const D3D11_UNORDERED_ACCESS_VIEW_DESC arguments_view = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = 3, .Flags = D3D11_BUFFER_UAV_FLAG_RAW},
    };
    const D3D11_SHADER_RESOURCE_VIEW_DESC instances_view = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX,
        .BufferEx = {.FirstElement = 0, .NumElements = internal::INSTANCE_BYTES / 4, .Flags = D3D11_BUFFEREX_SRV_FLAG_RAW},
    };
    constexpr std::array<UINT, 4> LIGHT_MODE = {0, 1, 0, 0};  // sss_bend's b3: no sun dispatch, light mode
    const D3D11_BUFFER_DESC mode_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA mode_data = {.pSysMem = LIGHT_MODE.data()};
    bool made = SUCCEEDED(device->CreateComputeShader(__sss_lights.data(), __sss_lights.size(), nullptr, objects.lights_shader.GetAddressOf()))
                && buffer(internal::MAX_DRAWS * 16, D3D11_BIND_SHADER_RESOURCE, STRUCTURED, 16, &objects.light_draws)
                && SUCCEEDED(device->CreateShaderResourceView(objects.light_draws.Get(), nullptr, objects.light_draws_srv.GetAddressOf()))
                && buffer(internal::MAX_ENTRIES * 16, READ_WRITE, STRUCTURED, 16, &objects.light_dispatches)
                && SUCCEEDED(device->CreateUnorderedAccessView(objects.light_dispatches.Get(), nullptr, objects.light_dispatches_uav.GetAddressOf()))
                && SUCCEEDED(device->CreateShaderResourceView(objects.light_dispatches.Get(), nullptr, objects.light_dispatches_srv.GetAddressOf()))
                && buffer(internal::MAX_LIGHTS * 16, READ_WRITE, STRUCTURED, 16, &objects.light_coordinates)
                && SUCCEEDED(device->CreateUnorderedAccessView(objects.light_coordinates.Get(), nullptr, objects.light_coordinates_uav.GetAddressOf()))
                && SUCCEEDED(device->CreateShaderResourceView(objects.light_coordinates.Get(), nullptr, objects.light_coordinates_srv.GetAddressOf()))
                && buffer(12, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
                          &objects.light_arguments)
                && SUCCEEDED(device->CreateUnorderedAccessView(objects.light_arguments.Get(), &arguments_view, objects.light_arguments_uav.GetAddressOf()))
                && buffer(internal::LIGHT_LIST * 16, D3D11_BIND_UNORDERED_ACCESS, STRUCTURED, 16, &objects.light_list)
                && SUCCEEDED(device->CreateUnorderedAccessView(objects.light_list.Get(), nullptr, objects.light_list_uav.GetAddressOf()))
                && buffer(internal::LIGHT_LIST * 16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, &objects.light_constants)
                && buffer(sizeof(screen_space_shadows::internal::ExtendedSettings), D3D11_BIND_CONSTANT_BUFFER, 0, 0, &objects.light_settings)
                && SUCCEEDED(device->CreateBuffer(&mode_desc, &mode_data, objects.light_mode.GetAddressOf()));
    for (size_t index = 0; made && index < objects.light_instances.size(); ++index) {
      made = buffer(internal::INSTANCE_BYTES, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, &objects.light_instances[index])
             && SUCCEEDED(device->CreateShaderResourceView(objects.light_instances[index].Get(), &instances_view, objects.light_instances_srv[index].GetAddressOf()));
    }
    if (!made) {
      renodx::utils::log::w("[Total War] Torch and Fire Shadows: could not create their shaders or buffers.");
      objects.lights_shader.Reset();
      return false;
    }
  }

  // The frame's first: last frame's copies give this frame's lights, marched over this frame's depth (the sun draw's).
  if (objects.light_frame != runtime.frame) {
    objects.light_frame = runtime.frame;
    objects.light_current = 1 - objects.light_current;
    objects.light_records[objects.light_current].clear();
    objects.light_bytes[objects.light_current] = 0;
    const size_t last = 1 - objects.light_current;
    const auto& last_records = objects.light_records[last];
    const bool depth_ready = runtime.computed_frame == runtime.frame && objects.light_camera;
    if (!depth_ready && !std::exchange(internal::logged_order, true)) {
      renodx::utils::log::i("[Total War] Torch and Fire Shadows: a light draw came before the sun's: none that frame.");
    }
    objects.lights_marched = depth_ready && !last_records.empty()
                             && (objects.light_mask_uav
                                 || screen_space_shadows::internal::MakeTexture(device.Get(), objects.width, objects.height, DXGI_FORMAT_R32_UINT,
                                                                                &objects.light_mask_uav, &objects.light_mask_srv));
    if (objects.lights_marched) {
      std::array<std::array<uint32_t, 4>, internal::MAX_DRAWS> draws = {};
      std::ranges::copy(last_records, draws.begin());
      native->UpdateSubresource(objects.light_draws.Get(), 0, nullptr, draws.data(), 0, 0);
      // The sun's march as it ran this frame (Auto Thickness, the banners' share), with the lights' pick.
      screen_space_shadows::internal::ExtendedSettings settings = objects.uploaded_extended;
      settings.sun_tangent = 0.f;
      settings.coloured = 0.f;
      settings.lights_wanted = std::min(runtime.settings.lights, internal::MAX_LIGHTS);
      settings.light_draws = static_cast<uint32_t>(last_records.size());
      if (std::memcmp(&settings, &objects.uploaded_light_settings, sizeof(settings)) != 0) {
        native->UpdateSubresource(objects.light_settings.Get(), 0, nullptr, &settings, 0, 0);
        objects.uploaded_light_settings = settings;
      }
      const ComputeState game_state(native);
      ComputeState::Bind(native, objects.lights_shader.Get(), {objects.light_instances_srv[last].Get(), objects.light_draws_srv.Get()},
                         {objects.light_dispatches_uav.Get(), objects.light_coordinates_uav.Get(), objects.light_arguments_uav.Get(), objects.light_list_uav.Get()},
                         {objects.light_camera.Get(), nullptr, objects.settings.Get(), nullptr, objects.light_settings.Get()});
      native->Dispatch(1, 1, 1);
      const D3D11_BOX list = {.left = 0, .top = 0, .front = 0, .right = internal::LIGHT_LIST * 16, .bottom = 1, .back = 1};
      native->CopySubresourceRegion(objects.light_constants.Get(), 0, 0, 0, 0, objects.light_list.Get(), 0, &list);
      constexpr std::array<UINT, 4> ALL_LIT = {UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX};
      native->ClearUnorderedAccessViewUint(objects.light_mask_uav.Get(), ALL_LIT.data());
      ID3D11ShaderResourceView* thin = (settings.thin_light > 0.f) ? objects.thin_srv.Get() : nullptr;
      ComputeState::Bind(native, objects.bend_extended.Get(),
                         {objects.depth_srv.Get(), objects.parameters_srv.Get(), thin, nullptr, objects.light_dispatches_srv.Get(), objects.light_coordinates_srv.Get()},
                         {nullptr, nullptr, nullptr, objects.light_mask_uav.Get()},
                         {objects.light_camera.Get(), nullptr, objects.settings.Get(), objects.light_mode.Get(), objects.light_settings.Get()});
      native->CSSetSamplers(0, 1, objects.border.GetAddressOf());
      native->DispatchIndirect(objects.light_arguments.Get(), 0);
    }
  }

  auto& records = objects.light_records[objects.light_current];
  UINT& bytes = objects.light_bytes[objects.light_current];
  const UINT count = (instances.stride == 0) ? 0 : std::min(instances.count, (internal::INSTANCE_BYTES - bytes) / instances.stride);
  if (instances.buffer != nullptr && count != 0 && records.size() < internal::MAX_DRAWS) {
    const D3D11_BOX box = {.left = instances.first, .top = 0, .front = 0, .right = instances.first + (count * instances.stride), .bottom = 1, .back = 1};
    native->CopySubresourceRegion(objects.light_instances[objects.light_current].Get(), 0, bytes, 0, 0, instances.buffer, 0, &box);
    records.push_back({bytes, count, instances.stride, instances.position | (instances.colour << 16)});
    bytes += count * instances.stride;
  }
  if (!objects.lights_marched) return false;
  native->PSSetShaderResources(internal::LIGHT_MASK_SLOT, 1, objects.light_mask_srv.GetAddressOf());
  native->PSSetConstantBuffers(internal::LIGHT_CONSTANTS_SLOT, 1, objects.light_constants.GetAddressOf());
  return true;
}

}  // namespace twattila::screen_space_lights
