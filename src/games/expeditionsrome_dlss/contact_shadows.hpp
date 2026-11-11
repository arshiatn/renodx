/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "./native_state.hpp"

// Contact shadows (Experimental): Unreal's own. The game's sun light shader already has them (a short screen-space
// march toward the light, 8 steps), but the game gives its sun a ContactShadowLength of 0. Before the sun's deferred
// light draw (the light rectangle vs 0x7C21DA11 with a light's constants at ps b1, UE 4.26 FDeferredLightUniformStruct),
// those constants are copied with the length set (cs 0x7A1E0D43) and the copy is bound for the draw; the game's binding
// comes back after it. The copy runs in the addon's own context state.
namespace expeditionsrome::dlss::contact_shadows {

using Microsoft::WRL::ComPtr;

// Unreal's ContactShadowLength for the sun: a share of the screen (0: the game's, none).
inline std::atomic<float> length = 0.f;
// The ray-traced sun shadows went into this draw's light attenuation (ray_tracing.hpp, just before): none of these.
inline std::atomic<bool> ray_traced = false;
// OnLightRectangle's vertex shader. UE 4.26 draws only directional lights as a rectangle; spot and point lights are
// geometry with other shaders.
constexpr uint32_t LIGHT_RECTANGLE_SHADER = 0x7C21DA11;

namespace internal {
// FDeferredLightUniformStruct is 144 bytes (160 in Unreal's buffer pool); this vertex shader's other passes bind a
// larger View at ps b1.
constexpr UINT MIN_LIGHT_BYTES = 144;
constexpr UINT COPY_BYTES = 256;

struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> copy_shader;
  ComPtr<ID3D11Buffer> written;  // the copy as the shader writes it (raw)
  ComPtr<ID3D11UnorderedAccessView> written_uav;
  ComPtr<ID3D11Buffer> light_copy;  // bound for the draw
  ComPtr<ID3D11Buffer> settings;
  float uploaded_length = -1.f;
  bool failed = false;
  // The game's binding, put back after the draw.
  ComPtr<ID3D11Buffer> game_light;
  UINT game_first_constant = 0;
  UINT game_constant_count = 0;
};
static Objects objects;

}  // namespace internal

constexpr auto OnLightRectangle = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using internal::objects;
  const float contact_length = length.load(std::memory_order_relaxed);
  if (contact_length <= 0.f || ray_traced.load(std::memory_order_relaxed)) return {};
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};

  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (objects.device != device) {
    objects = {.device = device};
    ComPtr<ID3D11Device1> device1;
    const auto feature_level = device->GetFeatureLevel();
    const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                                 ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                                 : 0;
    const D3D11_BUFFER_DESC written_desc = {
        .ByteWidth = internal::COPY_BYTES,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
    };
    D3D11_UNORDERED_ACCESS_VIEW_DESC written_uav_desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
    written_uav_desc.Buffer = {.FirstElement = 0, .NumElements = internal::COPY_BYTES / 4, .Flags = D3D11_BUFFER_UAV_FLAG_RAW};
    const D3D11_BUFFER_DESC light_copy_desc = {
        .ByteWidth = internal::COPY_BYTES,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    objects.failed =
        FAILED(device.As(&device1))
        || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                    nullptr, objects.private_state.GetAddressOf()))
        || FAILED(device->CreateComputeShader(__0x7A1E0D43.data(), __0x7A1E0D43.size(), nullptr, objects.copy_shader.GetAddressOf()))
        || FAILED(device->CreateBuffer(&written_desc, nullptr, objects.written.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(objects.written.Get(), &written_uav_desc, objects.written_uav.GetAddressOf()))
        || FAILED(device->CreateBuffer(&light_copy_desc, nullptr, objects.light_copy.GetAddressOf()))
        || FAILED(device->CreateBuffer(&settings_desc, nullptr, objects.settings.GetAddressOf()));
  }
  if (objects.failed) return {};

  objects.game_light.Reset();
  context1->PSGetConstantBuffers1(1, 1, objects.game_light.GetAddressOf(), &objects.game_first_constant,
                                  &objects.game_constant_count);
  if (!objects.game_light) return {};
  D3D11_BUFFER_DESC game_desc = {};
  objects.game_light->GetDesc(&game_desc);
  // The bytes the shader sees: the buffer from the first constant, at most the bound range.
  const UINT first_byte = std::min(objects.game_first_constant * 16, game_desc.ByteWidth);
  const UINT bound_bytes = std::min(game_desc.ByteWidth - first_byte, objects.game_constant_count * 16);
  if (bound_bytes < internal::MIN_LIGHT_BYTES || bound_bytes > internal::COPY_BYTES) {
    objects.game_light.Reset();
    return {};
  }
  if (contact_length != objects.uploaded_length) {
    const std::array<float, 4> settings = {contact_length};
    context->UpdateSubresource(objects.settings.Get(), 0, nullptr, settings.data(), 0, 0);
    objects.uploaded_length = contact_length;
  }
  {
    const ContextStateScope scope(context1.Get(), objects.private_state.Get());
    ID3D11Buffer* game_light = objects.game_light.Get();
    ID3D11Buffer* settings = objects.settings.Get();
    ID3D11UnorderedAccessView* written = objects.written_uav.Get();
    context1->CSSetShader(objects.copy_shader.Get(), nullptr, 0);
    context1->CSSetConstantBuffers1(0, 1, &game_light, &objects.game_first_constant, &objects.game_constant_count);
    context1->CSSetConstantBuffers(1, 1, &settings);
    context1->CSSetUnorderedAccessViews(0, 1, &written, nullptr);
    context1->Dispatch(1, 1, 1);
  }
  context->CopyResource(objects.light_copy.Get(), objects.written.Get());
  ID3D11Buffer* light_copy = objects.light_copy.Get();
  context->PSSetConstantBuffers(1, 1, &light_copy);
  return {.post_callback = [](Context& command, const void*) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    ComPtr<ID3D11DeviceContext1> context1;
    if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
    ID3D11Buffer* game_light = objects.game_light.Get();
    context1->PSSetConstantBuffers1(1, 1, &game_light, &objects.game_first_constant, &objects.game_constant_count);
    objects.game_light.Reset();
  }};
};

// A device is going away: the addon's objects on it with it.
inline void Release(ID3D11Device* device) {
  if (internal::objects.device.Get() == device) {
    internal::objects = {};
  }
}

}  // namespace expeditionsrome::dlss::contact_shadows
