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
constexpr UINT MAX_LIGHTS = 32;              // the mask's bits (sss_lights)
constexpr UINT MAX_ENTRIES = 1 + (2 * MAX_LIGHTS * 8);  // the dispatch list: its header, two entries each of 8 a light
constexpr UINT LIST_LIGHTS = 128;            // rt_local_lights.hlsli's b9 arrays (dlaa_bridge.h RT_ALL_LIGHTS)
constexpr UINT LIGHT_LIST = (LIST_LIGHTS * 2) + 1;      // b9's float4s: positions, count, colours
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
  auto& lights = objects.lights;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!runtime.settings.enabled || runtime.settings.lights == 0 || objects.device.Get() != device.Get() || !objects.bend_extended[0]) {
    return false;
  }
  // Their objects on the screen-space shadows' device, made when first needed.
  if (!lights.shader) {
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
    bool made = SUCCEEDED(device->CreateComputeShader(__sss_lights.data(), __sss_lights.size(), nullptr, lights.shader.GetAddressOf()))
                && buffer(internal::MAX_DRAWS * 16, D3D11_BIND_SHADER_RESOURCE, STRUCTURED, 16, &lights.draws)
                && SUCCEEDED(device->CreateShaderResourceView(lights.draws.Get(), nullptr, lights.draws_srv.GetAddressOf()))
                && buffer(internal::MAX_ENTRIES * 16, READ_WRITE, STRUCTURED, 16, &lights.dispatches)
                && SUCCEEDED(device->CreateUnorderedAccessView(lights.dispatches.Get(), nullptr, lights.dispatches_uav.GetAddressOf()))
                && SUCCEEDED(device->CreateShaderResourceView(lights.dispatches.Get(), nullptr, lights.dispatches_srv.GetAddressOf()))
                && buffer(internal::MAX_LIGHTS * 16, READ_WRITE, STRUCTURED, 16, &lights.coordinates)
                && SUCCEEDED(device->CreateUnorderedAccessView(lights.coordinates.Get(), nullptr, lights.coordinates_uav.GetAddressOf()))
                && SUCCEEDED(device->CreateShaderResourceView(lights.coordinates.Get(), nullptr, lights.coordinates_srv.GetAddressOf()))
                && buffer(12, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
                          &lights.arguments)
                && SUCCEEDED(device->CreateUnorderedAccessView(lights.arguments.Get(), &arguments_view, lights.arguments_uav.GetAddressOf()))
                && buffer(internal::LIGHT_LIST * 16, D3D11_BIND_UNORDERED_ACCESS, STRUCTURED, 16, &lights.list)
                && SUCCEEDED(device->CreateUnorderedAccessView(lights.list.Get(), nullptr, lights.list_uav.GetAddressOf()))
                && buffer(internal::LIGHT_LIST * 16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, &lights.constants)
                && buffer(sizeof(screen_space_shadows::internal::ExtendedSettings), D3D11_BIND_CONSTANT_BUFFER, 0, 0, &lights.settings)
                && SUCCEEDED(device->CreateBuffer(&mode_desc, &mode_data, lights.mode.GetAddressOf()));
    for (size_t index = 0; made && index < lights.instances.size(); ++index) {
      made = buffer(internal::INSTANCE_BYTES, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, &lights.instances[index])
             && SUCCEEDED(device->CreateShaderResourceView(lights.instances[index].Get(), &instances_view, lights.instances_srv[index].GetAddressOf()));
    }
    if (!made) {
      renodx::utils::log::w("[Total War] Torch and Fire Shadows: could not create their shaders or buffers.");
      lights.shader.Reset();
      return false;
    }
  }

  // The frame's first: last frame's copies give this frame's lights, marched over this frame's depth (the sun draw's).
  if (lights.frame != runtime.frame) {
    lights.frame = runtime.frame;
    lights.current = 1 - lights.current;
    lights.records[lights.current].clear();
    lights.bytes[lights.current] = 0;
    const size_t last = 1 - lights.current;
    const auto& last_records = lights.records[last];
    const bool depth_ready = runtime.computed_frame == runtime.frame && lights.camera;
    if (!depth_ready && !std::exchange(internal::logged_order, true)) {
      renodx::utils::log::i("[Total War] Torch and Fire Shadows: a light draw came before the sun's: none that frame.");
    }
    lights.marched = depth_ready && !last_records.empty()
                     && (lights.mask_uav
                         || screen_space_shadows::internal::MakeTexture(device.Get(), objects.width, objects.height, DXGI_FORMAT_R32_UINT,
                                                                        &lights.mask_uav, &lights.mask_srv));
    if (lights.marched) {
      std::array<std::array<uint32_t, 4>, internal::MAX_DRAWS> draws = {};
      std::ranges::copy(last_records, draws.begin());
      native->UpdateSubresource(lights.draws.Get(), 0, nullptr, draws.data(), 0, 0);
      // The sun's march as it ran this frame (Auto Thickness, the banners' share), with the lights' pick.
      screen_space_shadows::internal::ExtendedSettings settings = objects.uploaded_extended;
      settings.sun_tangent = 0.f;
      settings.coloured = 0.f;
      settings.lights_wanted = std::min(runtime.settings.lights, internal::MAX_LIGHTS);
      settings.light_draws = static_cast<uint32_t>(last_records.size());
      settings.light_range = runtime.settings.light_range;
      if (std::memcmp(&settings, &lights.uploaded_settings, sizeof(settings)) != 0) {
        native->UpdateSubresource(lights.settings.Get(), 0, nullptr, &settings, 0, 0);
        lights.uploaded_settings = settings;
      }
      const ComputeState game_state(native);
      ComputeState::Bind(native, lights.shader.Get(), {lights.instances_srv[last].Get(), lights.draws_srv.Get()},
                         {lights.dispatches_uav.Get(), lights.coordinates_uav.Get(), lights.arguments_uav.Get(), lights.list_uav.Get()},
                         {lights.camera.Get(), nullptr, objects.settings.Get(), nullptr, lights.settings.Get()});
      native->Dispatch(1, 1, 1);
      const D3D11_BOX list = {.left = 0, .top = 0, .front = 0, .right = internal::LIGHT_LIST * 16, .bottom = 1, .back = 1};
      native->CopySubresourceRegion(lights.constants.Get(), 0, 0, 0, 0, lights.list.Get(), 0, &list);
      constexpr std::array<UINT, 4> ALL_LIT = {UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX};
      native->ClearUnorderedAccessViewUint(lights.mask_uav.Get(), ALL_LIT.data());
      ID3D11ShaderResourceView* thin = (settings.thin_light > 0.f) ? objects.thin_srv.Get() : nullptr;
      ComputeState::Bind(native, objects.bend_extended[screen_space_shadows::internal::ExtendedVariant(runtime.settings.auto_thickness, false)].Get(),
                         {objects.depth_srv.Get(), objects.parameters_srv.Get(), thin, nullptr, lights.dispatches_srv.Get(), lights.coordinates_srv.Get()},
                         {nullptr, nullptr, nullptr, lights.mask_uav.Get()},
                         {lights.camera.Get(), nullptr, objects.settings.Get(), lights.mode.Get(), lights.settings.Get()});
      native->CSSetSamplers(0, 1, objects.border.GetAddressOf());
      native->DispatchIndirect(lights.arguments.Get(), 0);
    }
  }

  auto& records = lights.records[lights.current];
  UINT& bytes = lights.bytes[lights.current];
  const UINT count = (instances.stride == 0) ? 0 : std::min(instances.count, (internal::INSTANCE_BYTES - bytes) / instances.stride);
  if (instances.buffer != nullptr && count != 0 && records.size() < internal::MAX_DRAWS) {
    const D3D11_BOX box = {.left = instances.first, .top = 0, .front = 0, .right = instances.first + (count * instances.stride), .bottom = 1, .back = 1};
    native->CopySubresourceRegion(lights.instances[lights.current].Get(), 0, bytes, 0, 0, instances.buffer, 0, &box);
    records.push_back({bytes, count, instances.stride, instances.position | (instances.colour << 16)});
    bytes += count * instances.stride;
  }
  if (!lights.marched) return false;
  native->PSSetShaderResources(internal::LIGHT_MASK_SLOT, 1, lights.mask_srv.GetAddressOf());
  native->PSSetConstantBuffers(internal::LIGHT_CONSTANTS_SLOT, 1, lights.constants.GetAddressOf());
  return true;
}

}  // namespace twattila::screen_space_lights
