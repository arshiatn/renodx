/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <embed/shaders.h>

#include "../twattila/screen_space_shadows.hpp"
#include "./native_state.hpp"

// Screen-space shadows (Experimental): Bend Studio's screen-space shadows (Apache 2.0, Copyright 2023 Sony Interactive
// Entertainment; the Total War games' passes twattila/sss_bend.cs_5_0.hlsl and sss_blur.cs_5_0.hlsl as they are, their
// core twattila/bend_sss_gpu.hlsli) on an Unreal game's sun, in place of the game's own contact
// shadows, before the game's sun reads its shadowing: the scene depth as 1 - device Z (host_sss_depth: the standard depth,
// far 1, those passes take), the dispatch list for the View's camera and the game's sun (each game's host_sss_setup, on the
// GPU as the game's constants are; it also writes the View's render size into the settings, copied into the constant
// buffer sss_bend and sss_blur read), Bend's dispatches (indirect) into the occlusion (1 - the shadow term), with Softness
// smoothed along surfaces (sss_blur). The game's adapter puts 1 - the occlusion into its sun's shadowing. In the add-on's
// own context state (the game's bindings stay); D3D11.
namespace talesofarise::ue_screen_space_shadows {

using Microsoft::WRL::ComPtr;

struct Inputs {
  ID3D11ShaderResourceView* depth = nullptr;  // the scene depth (device Z, reversed: the sky 0)
  dlss::ConstantBinding sun;                  // the game's host_sss_setup's b0: where it finds the sun
  dlss::ConstantBinding view;                 // the View uniform buffer (its b1)
  float render_share = 1.f;                   // the render size over the output's (DLSS below 100%: below 1)
};

namespace internal {

namespace attila_sss = twattila::screen_space_shadows::internal;
constexpr UINT DISPATCH_COUNT = attila_sss::DISPATCH_COUNT;

struct Objects {
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> depth;
  ComPtr<ID3D11ComputeShader> setup;
  ComPtr<ID3D11ComputeShader> bend;
  ComPtr<ID3D11ComputeShader> blur;
  // sss.hlsli's sss_settings: the add-on's (render size 0, host_sss_setup's b3), the setup's with the render size (raw)
  // and its copy (sss_bend's and sss_blur's b2).
  ComPtr<ID3D11Buffer> base_settings;
  ComPtr<ID3D11Buffer> written_settings;
  ComPtr<ID3D11UnorderedAccessView> written_settings_uav;
  ComPtr<ID3D11Buffer> settings;
  attila_sss::ShaderSettings uploaded = {};
  ComPtr<ID3D11Buffer> arguments;
  ComPtr<ID3D11UnorderedAccessView> arguments_uav;
  ComPtr<ID3D11Buffer> parameters;
  ComPtr<ID3D11UnorderedAccessView> parameters_uav;
  ComPtr<ID3D11ShaderResourceView> parameters_srv;
  std::array<ComPtr<ID3D11Buffer>, DISPATCH_COUNT> dispatch_indexes;
  ComPtr<ID3D11SamplerState> border;
  // The scene depth's size: Bend's depth, the occlusion and its softened copy.
  UINT width = 0;
  UINT height = 0;
  ComPtr<ID3D11UnorderedAccessView> depth_uav;
  ComPtr<ID3D11ShaderResourceView> depth_srv;
  ComPtr<ID3D11UnorderedAccessView> occlusion_uav;
  ComPtr<ID3D11ShaderResourceView> occlusion_srv;
  ComPtr<ID3D11UnorderedAccessView> soft_uav;
  ComPtr<ID3D11ShaderResourceView> soft_srv;
};
static Objects objects;

}  // namespace internal

// This frame's occlusion of the sun (R8 at the scene depth's size: 1 - Bend's shadow term, 0 where it found none) from
// `inputs` with `settings`, or null (off or not available). The game's bindings on `context` stay.
inline ID3D11ShaderResourceView* Compute(ID3D11DeviceContext* context, const Inputs& inputs,
                                         const twattila::screen_space_shadows::Settings& settings) {
  using internal::objects;
  namespace attila_sss = internal::attila_sss;
  ComPtr<ID3D11DeviceContext1> context1;
  if (!settings.enabled || inputs.depth == nullptr || !inputs.sun.buffer || !inputs.view.buffer
      || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
    return nullptr;
  }
  ComPtr<ID3D11Resource> depth_resource;
  inputs.depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return nullptr;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  if (depth_desc.SampleDesc.Count != 1) return nullptr;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());

  if (objects.device != device) {
    objects = {.device = device};
    const auto compute = [&](const auto& code, ComPtr<ID3D11ComputeShader>* shader) {
      return SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
    };
    const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = sizeof(attila_sss::ShaderSettings), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC written_desc = {.ByteWidth = sizeof(attila_sss::ShaderSettings), .Usage = D3D11_USAGE_DEFAULT,
                                            .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                            .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_UNORDERED_ACCESS_VIEW_DESC written_view = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = sizeof(attila_sss::ShaderSettings) / 4, .Flags = D3D11_BUFFER_UAV_FLAG_RAW},
    };
    const D3D11_BUFFER_DESC arguments_desc = {
        .ByteWidth = internal::DISPATCH_COUNT * 12,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
        .MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
    };
    const D3D11_UNORDERED_ACCESS_VIEW_DESC arguments_view = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = internal::DISPATCH_COUNT * 3, .Flags = D3D11_BUFFER_UAV_FLAG_RAW},
    };
    const D3D11_BUFFER_DESC parameters_desc = {
        .ByteWidth = (1 + internal::DISPATCH_COUNT) * 16,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
        .StructureByteStride = 16,
    };
    // Off screen: the far depth (Bend reads past the edges on purpose).
    const D3D11_SAMPLER_DESC border_desc = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
        .AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
        .AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
        .AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .BorderColor = {1.f, 1.f, 1.f, 1.f},
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    objects.failed = !dlss::MakeContextState(device.Get(), &objects.private_state)
                     || !compute(__host_sss_depth, &objects.depth) || !compute(__host_sss_setup, &objects.setup)
                     || !compute(__sss_bend, &objects.bend) || !compute(__sss_blur, &objects.blur)
                     || FAILED(device->CreateBuffer(&settings_desc, nullptr, objects.base_settings.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&settings_desc, nullptr, objects.settings.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&written_desc, nullptr, objects.written_settings.GetAddressOf()))
                     || FAILED(device->CreateUnorderedAccessView(objects.written_settings.Get(), &written_view,
                                                                 objects.written_settings_uav.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&arguments_desc, nullptr, objects.arguments.GetAddressOf()))
                     || FAILED(device->CreateUnorderedAccessView(objects.arguments.Get(), &arguments_view, objects.arguments_uav.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&parameters_desc, nullptr, objects.parameters.GetAddressOf()))
                     || FAILED(device->CreateUnorderedAccessView(objects.parameters.Get(), nullptr, objects.parameters_uav.GetAddressOf()))
                     || FAILED(device->CreateShaderResourceView(objects.parameters.Get(), nullptr, objects.parameters_srv.GetAddressOf()))
                     || FAILED(device->CreateSamplerState(&border_desc, objects.border.GetAddressOf()));
    // sss_bend's b3: which of the list's dispatches it is.
    for (UINT index = 0; !objects.failed && index < internal::DISPATCH_COUNT; ++index) {
      const std::array<UINT, 4> value = {index, 0, 0, 0};
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = value.data()};
      objects.failed = FAILED(device->CreateBuffer(&desc, &data, objects.dispatch_indexes[index].GetAddressOf()));
    }
  }
  if (objects.failed) return nullptr;
  if (objects.width != depth_desc.Width || objects.height != depth_desc.Height) {
    objects.width = 0;
    if (!attila_sss::MakeTexture(device.Get(), depth_desc.Width, depth_desc.Height, DXGI_FORMAT_R32_FLOAT, &objects.depth_uav, &objects.depth_srv)
        || !attila_sss::MakeTexture(device.Get(), depth_desc.Width, depth_desc.Height, DXGI_FORMAT_R8_UNORM, &objects.occlusion_uav,
                                    &objects.occlusion_srv)
        || !attila_sss::MakeTexture(device.Get(), depth_desc.Width, depth_desc.Height, DXGI_FORMAT_R8_UNORM, &objects.soft_uav, &objects.soft_srv)) {
      return nullptr;
    }
    objects.width = depth_desc.Width;
    objects.height = depth_desc.Height;
  }

  // Below 100% (O-SS5, as Attila's screen_space_shadows.hpp): sss_bend marches one sample a screen pixel between the
  // render texels, so the samples are the row's in screen pixels and the surface thickness the row's (Bend's window is
  // a share of the depth left, no pixel size in it); the bilinear threshold / share stays (it classifies neighbouring
  // render texels).
  const float share = std::clamp(inputs.render_share, 0.01f, 1.f);
  const float samples = std::max(std::round(settings.samples), settings.hard_samples + 4.f);
  const attila_sss::ShaderSettings shader_settings = {
      .width = static_cast<float>(objects.width),
      .height = static_cast<float>(objects.height),
      .inverse_width = 1.f / static_cast<float>(objects.width),
      .inverse_height = 1.f / static_cast<float>(objects.height),
      .thickness = settings.thickness,
      .samples = samples,
      .contrast = settings.contrast,
      .edge_threshold = settings.edge_threshold / share,
      .hard_samples = settings.hard_samples,
      // Within the samples left after the hard ones (Bend reads past them otherwise).
      .fade_samples = std::min(settings.fade_samples, samples - settings.hard_samples),
      .ignore_edges = (settings.ignore_edges ? 1.f : 0.f),
      .precision_offset = (settings.precision_offset ? 1.f : 0.f),
      .sample_along_ray = (settings.sample_along_ray ? 1.f : 0.f),
      .softness = settings.softness,
      .mirror_edges = (settings.mirror_edges ? 1.f : 0.f),
  };
  if (std::memcmp(&shader_settings, &objects.uploaded, sizeof(shader_settings)) != 0) {
    context->UpdateSubresource(objects.base_settings.Get(), 0, nullptr, &shader_settings, 0, 0);
    objects.uploaded = shader_settings;
  }

  const dlss::ContextStateScope scope(context1.Get(), objects.private_state.Get());
  const UINT groups_x = (objects.width + 7) / 8;
  const UINT groups_y = (objects.height + 7) / 8;
  context1->CSSetShader(objects.depth.Get(), nullptr, 0);
  context1->CSSetShaderResources(0, 1, &inputs.depth);
  context1->CSSetUnorderedAccessViews(0, 1, objects.depth_uav.GetAddressOf(), nullptr);
  context1->Dispatch(groups_x, groups_y, 1);

  for (const auto& [slot, binding] : {std::pair{0u, &inputs.sun}, std::pair{1u, &inputs.view}}) {
    // A whole-buffer binding reads back as offset 0 and 4096 constants, more than a small buffer holds.
    if (binding->first == 0) {
      context1->CSSetConstantBuffers(slot, 1, binding->buffer.GetAddressOf());
    } else {
      context1->CSSetConstantBuffers1(slot, 1, binding->buffer.GetAddressOf(), &binding->first, &binding->count);
    }
  }
  context1->CSSetConstantBuffers(3, 1, objects.base_settings.GetAddressOf());
  const std::array<ID3D11UnorderedAccessView*, 3> setup_outputs = {objects.arguments_uav.Get(), objects.parameters_uav.Get(),
                                                                   objects.written_settings_uav.Get()};
  context1->CSSetUnorderedAccessViews(0, 3, setup_outputs.data(), nullptr);
  context1->CSSetShader(objects.setup.Get(), nullptr, 0);
  context1->Dispatch(1, 1, 1);

  // The setup's outputs unbound before they are read (a view still bound for writing reads as null), the occlusion
  // cleared (Bend leaves the pixels it skips, the sky, as they were), Bend's dispatches.
  const std::array<ID3D11UnorderedAccessView*, 3> bend_outputs = {objects.occlusion_uav.Get(), nullptr, nullptr};
  context1->CSSetUnorderedAccessViews(0, 3, bend_outputs.data(), nullptr);
  context1->CopyResource(objects.settings.Get(), objects.written_settings.Get());
  constexpr std::array<FLOAT, 4> ZEROS = {};
  context1->ClearUnorderedAccessViewFloat(objects.occlusion_uav.Get(), ZEROS.data());
  const std::array<ID3D11ShaderResourceView*, 2> bend_inputs = {objects.depth_srv.Get(), objects.parameters_srv.Get()};
  context1->CSSetShaderResources(0, 2, bend_inputs.data());
  context1->CSSetConstantBuffers(2, 1, objects.settings.GetAddressOf());
  context1->CSSetSamplers(0, 1, objects.border.GetAddressOf());
  context1->CSSetShader(objects.bend.Get(), nullptr, 0);
  for (UINT index = 0; index < internal::DISPATCH_COUNT; ++index) {
    context1->CSSetConstantBuffers(3, 1, objects.dispatch_indexes[index].GetAddressOf());
    context1->DispatchIndirect(objects.arguments.Get(), index * 12);
  }
  if (!(settings.softness > 0.f)) return objects.occlusion_srv.Get();

  // Softness: its output bound before the occlusion it reads; t3 the setup's parameters (X30: sss_blur's depth at
  // infinity, the light coordinate's z).
  context1->CSSetUnorderedAccessViews(0, 1, objects.soft_uav.GetAddressOf(), nullptr);
  const std::array<ID3D11ShaderResourceView*, 4> blur_inputs = {objects.depth_srv.Get(), objects.occlusion_srv.Get(), nullptr,
                                                                objects.parameters_srv.Get()};
  context1->CSSetShaderResources(0, 4, blur_inputs.data());
  context1->CSSetShader(objects.blur.Get(), nullptr, 0);
  context1->Dispatch(groups_x, groups_y, 1);
  return objects.soft_srv.Get();
}

// The game's device is going away: the add-on's objects on it with it.
inline void Release(ID3D11Device* device) {
  if (internal::objects.device.Get() == device) {
    internal::objects = {};
  }
}

}  // namespace talesofarise::ue_screen_space_shadows
