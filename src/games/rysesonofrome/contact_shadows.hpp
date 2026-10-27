/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstring>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/native_state.hpp"

// Contact shadows (Experimental): before Ryse's tiled lighting (cs 0x897232B5: t0 linear depth, t4 the shadow mask
// array, t32 its light list, b0 its camera), the sun's shadow mask gets short screen-space shadows toward the sun: which
// slice and channels are the sun's from the light list (0x52C5E001), then a full-screen pass (0x52C5E002 / 0x52C5E003)
// max-blended into slice 0 of the mask (1 = shadow). In the addon's own context state; the game's is put back after.
namespace rysesonofrome::contact_shadows {

using Microsoft::WRL::ComPtr;

struct Settings {
  float length = 0.5f;     // metres
  float thickness = 0.2f;  // metres
  float strength = 1.f;    // 0: off
  float frame = 0.f;       // the noise's frame
};

namespace internal {

struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> find_sun;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11Buffer> sun;
  ComPtr<ID3D11UnorderedAccessView> sun_uav;
  ComPtr<ID3D11ShaderResourceView> sun_srv;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11BlendState> max_blend;
  ComPtr<ID3D11RasterizerState> no_cull;
  ComPtr<ID3D11Resource> mask;  // the shadow mask array its slice-0 target is of
  ComPtr<ID3D11RenderTargetView> mask_target;
  Settings uploaded = {.strength = -1.f};
  bool failed = false;
};
static Objects objects;

inline bool Create(ID3D11Device* device) {
  if (objects.device.Get() == device) return !objects.failed;
  objects = {};
  objects.device = device;
  ComPtr<ID3D11Device1> device1;
  const auto feature_level = device->GetFeatureLevel();
  const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
  const D3D11_BUFFER_DESC sun_desc = {
      .ByteWidth = 8,
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
  };
  D3D11_UNORDERED_ACCESS_VIEW_DESC sun_uav_desc = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
  sun_uav_desc.Buffer.NumElements = 2;
  D3D11_SHADER_RESOURCE_VIEW_DESC sun_srv_desc = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  sun_srv_desc.Buffer.NumElements = 2;
  const D3D11_BUFFER_DESC constants_desc = {
      .ByteWidth = sizeof(Settings),
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
  };
  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0] = {
      .BlendEnable = TRUE,
      .SrcBlend = D3D11_BLEND_ONE,
      .DestBlend = D3D11_BLEND_ONE,
      .BlendOp = D3D11_BLEND_OP_MAX,
      .SrcBlendAlpha = D3D11_BLEND_ONE,
      .DestBlendAlpha = D3D11_BLEND_ONE,
      .BlendOpAlpha = D3D11_BLEND_OP_MAX,
      .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
  };
  const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE};
  objects.failed =
      FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
      || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                  nullptr, objects.private_state.GetAddressOf()))
      || FAILED(device->CreateComputeShader(__0x52C5E001.data(), __0x52C5E001.size(), nullptr, objects.find_sun.GetAddressOf()))
      || FAILED(device->CreateVertexShader(__0x52C5E002.data(), __0x52C5E002.size(), nullptr, objects.vertex_shader.GetAddressOf()))
      || FAILED(device->CreatePixelShader(__0x52C5E003.data(), __0x52C5E003.size(), nullptr, objects.pixel_shader.GetAddressOf()))
      || FAILED(device->CreateBuffer(&sun_desc, nullptr, objects.sun.GetAddressOf()))
      || FAILED(device->CreateUnorderedAccessView(objects.sun.Get(), &sun_uav_desc, objects.sun_uav.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(objects.sun.Get(), &sun_srv_desc, objects.sun_srv.GetAddressOf()))
      || FAILED(device->CreateBuffer(&constants_desc, nullptr, objects.constants.GetAddressOf()))
      || FAILED(device->CreateBlendState(&blend_desc, objects.max_blend.GetAddressOf()))
      || FAILED(device->CreateRasterizerState(&rasterizer_desc, objects.no_cull.GetAddressOf()));
  return !objects.failed;
}

}  // namespace internal

// Before the tiled lighting's dispatch (its bindings set): the contact shadows into the sun's shadow mask.
inline void Apply(reshade::api::command_list* cmd_list, const Settings& settings) {
  using internal::objects;
  if (settings.strength <= 0.f || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  ComPtr<ID3D11ShaderResourceView> depth;
  ComPtr<ID3D11ShaderResourceView> mask_view;
  ComPtr<ID3D11ShaderResourceView> lights;
  ComPtr<ID3D11Buffer> camera;
  context->CSGetShaderResources(0, 1, depth.GetAddressOf());
  context->CSGetShaderResources(4, 1, mask_view.GetAddressOf());
  context->CSGetShaderResources(32, 1, lights.GetAddressOf());
  context->CSGetConstantBuffers(0, 1, camera.GetAddressOf());
  if (!depth || !mask_view || !lights || !camera) return;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (!internal::Create(device.Get())) return;

  // The mask's slice 0 as a target (made again when the game's mask changes).
  ComPtr<ID3D11Resource> mask;
  mask_view->GetResource(mask.GetAddressOf());
  ComPtr<ID3D11Texture2D> mask_texture;
  if (FAILED(mask.As(&mask_texture))) return;
  D3D11_TEXTURE2D_DESC mask_desc = {};
  mask_texture->GetDesc(&mask_desc);
  if ((mask_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) return;
  if (objects.mask != mask) {
    D3D11_SHADER_RESOURCE_VIEW_DESC mask_view_desc = {};
    mask_view->GetDesc(&mask_view_desc);
    D3D11_RENDER_TARGET_VIEW_DESC target_desc = {
        .Format = mask_view_desc.Format,
        .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY,
    };
    target_desc.Texture2DArray = {.MipSlice = 0, .FirstArraySlice = 0, .ArraySize = 1};
    objects.mask_target.Reset();
    if (FAILED(device->CreateRenderTargetView(mask.Get(), &target_desc, objects.mask_target.GetAddressOf()))) return;
    objects.mask = mask;
  }
  if (std::memcmp(&settings, &objects.uploaded, sizeof(Settings)) != 0) {
    context->UpdateSubresource(objects.constants.Get(), 0, nullptr, &settings, 0, 0);
    objects.uploaded = settings;
  }

  const talesofarise::dlss::ContextStateScope scope(context1.Get(), objects.private_state.Get());
  ID3D11ShaderResourceView* light_list = lights.Get();
  ID3D11UnorderedAccessView* sun_output = objects.sun_uav.Get();
  context->CSSetShader(objects.find_sun.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, &light_list);
  context->CSSetUnorderedAccessViews(0, 1, &sun_output, nullptr);
  context->Dispatch(1, 1, 1);
  ID3D11UnorderedAccessView* no_output = nullptr;
  context->CSSetUnorderedAccessViews(0, 1, &no_output, nullptr);

  const D3D11_VIEWPORT viewport = {
      .Width = static_cast<float>(mask_desc.Width),
      .Height = static_cast<float>(mask_desc.Height),
      .MaxDepth = 1.f,
  };
  ID3D11ShaderResourceView* inputs[] = {depth.Get(), objects.sun_srv.Get()};
  ID3D11Buffer* constants[] = {camera.Get(), objects.constants.Get()};
  ID3D11RenderTargetView* target = objects.mask_target.Get();
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(objects.vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(objects.pixel_shader.Get(), nullptr, 0);
  context->PSSetShaderResources(0, 2, inputs);
  context->PSSetConstantBuffers(0, 2, constants);
  context->RSSetState(objects.no_cull.Get());
  context->RSSetViewports(1, &viewport);
  context->OMSetBlendState(objects.max_blend.Get(), nullptr, 0xFFFFFFFF);
  context->OMSetRenderTargets(1, &target, nullptr);
  context->Draw(3, 0);
}

// The game's device is going away: the addon's objects on it with it.
inline void Release() {
  internal::objects = {};
}

}  // namespace rysesonofrome::contact_shadows
