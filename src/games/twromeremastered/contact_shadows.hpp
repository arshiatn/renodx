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
#include <mutex>

#include <embed/shaders.h>
#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/feature_settings.hpp"
#include "./dlaa.hpp"
#include "./ray_tracing.hpp"

// Screen-space shadows (Graphics (Experimental), off; tools\tw\romeremastered\plan_1114.md item 7): Bend Studio's
// screen-space shadows (Apache 2.0, Copyright 2023 Sony Interactive Entertainment; twattila's sss_setup and sss_bend as
// they are) on the sun. After the shadow mask's resolve (CSMResolvePS, its bindings still set): the march over the
// scene's depth (dlaa.hpp keeps it, CSMResolve's t1) toward the sun (the frame constants PS b8, c2) with the main camera,
// its occlusion (1 shadowed) multiplying the mask's red by 1 - it (ray_tracing.hpp's multiply draw): soldiers, weapons and
// small details get contact shadows the cascades are too coarse for. sss_setup reads Attila's layout: the camera's
// projection at c5 and view-projection at c9 (column-major registers: the rows' transposes) and the sun's travel
// direction; c2 points toward the sun, so the view-projection goes in negated (the setup's -direction x it is then the
// sun's screen position). Softer Away from the Caster as Attila's: the extended march (sss_bend_extended_find) finds each
// shadowed pixel's caster (the depth's view positions by the jittered projection's inverse at c17) and writes its penumbra,
// sss_soft widens the shadow by it. The Debug View (sss_debug) over the tone mapper's output (addon.cpp's tone mapper
// hook, with the rays'). In the addon's own context state; the game's is put back after.
namespace twromeremastered::contact_shadows {

using Microsoft::WRL::ComPtr;

// The rows (the graphics page's Bend Screen-Space Shadows, feature_settings::AppendSss): Bend's march and Softness.
static twattila::feature_settings::SssSliders rows;

namespace internal {

constexpr UINT DISPATCH_COUNT = 8;
// The frame constants' float where the direction toward the sun starts (c2.x).
constexpr float SUN_DIRECTION = 8.f;

// sss.hlsli's sss_settings.
struct ShaderSettings {
  float width = 0.f;
  float height = 0.f;
  float inverse_width = 0.f;
  float inverse_height = 0.f;
  float thickness = 0.f;
  float samples = 0.f;
  float contrast = 4.f;
  float edge_threshold = 0.02f;
  float hard_samples = 4.f;
  float fade_samples = 8.f;
  float ignore_edges = 0.f;
  float precision_offset = 0.f;
  float sample_along_ray = 0.f;
  float softness = 0.f;
  float debug_view = 0.f;
  float mirror_edges = 0.f;
  float sun_direction = SUN_DIRECTION;
  float render_width = 0.f;
  float render_height = 0.f;
  float foliage_light = 0.f;
  float foliage_bit = 0.f;
  std::array<float, 3> padding = {};
};
static_assert(sizeof(ShaderSettings) == 96);

// sss_setup's and the extended march's camera_VS_PS: c5-c8 the projection, c9-c12 the view-projection, c17-c20 the
// projection's inverse.
struct CameraConstants {
  std::array<float, 20> unused = {};
  std::array<float, 16> projection = {};
  std::array<float, 16> view_projection = {};
  std::array<float, 16> unused_after = {};
  std::array<float, 16> inverse_projection = {};
};

struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> setup;
  ComPtr<ID3D11ComputeShader> bend;
  ComPtr<ID3D11ComputeShader> blur;  // Softness (sss_blur)
  // Softer Away from the Caster: the march finding each pixel's caster (sss_bend_extended_find), the widening (sss_soft).
  ComPtr<ID3D11ComputeShader> bend_extended;
  ComPtr<ID3D11ComputeShader> soft;
  ComPtr<ID3D11ComputeShader> debug;  // the Debug View (sss_debug)
  ComPtr<ID3D11Buffer> settings;
  ComPtr<ID3D11Buffer> extended_settings;  // sss_extended (b4)
  twattila::screen_space_shadows::internal::ExtendedSettings uploaded_extended = {};
  ComPtr<ID3D11Buffer> camera;
  std::array<ComPtr<ID3D11Buffer>, DISPATCH_COUNT> dispatch_indexes;  // sss_bend's b3: which of the list's dispatches it is
  ComPtr<ID3D11Buffer> arguments;  // the dispatches' group counts (sss_setup u0)
  ComPtr<ID3D11UnorderedAccessView> arguments_uav;
  ComPtr<ID3D11Buffer> parameters;  // the light's coordinate and the dispatches' offsets (sss_setup u1, sss_bend t1)
  ComPtr<ID3D11UnorderedAccessView> parameters_uav;
  ComPtr<ID3D11ShaderResourceView> parameters_srv;
  ComPtr<ID3D11SamplerState> border;  // off screen: the far depth
  ShaderSettings uploaded = {};
  // The occlusion and the occlusion after Softness or Softer Away from the Caster (R8, the depth's size); the penumbra
  // (R16F) and the Debug View's picture (the tone mapper's output's format) made when first needed.
  UINT width = 0;
  UINT height = 0;
  ComPtr<ID3D11UnorderedAccessView> occlusion_uav;
  ComPtr<ID3D11ShaderResourceView> occlusion_srv;
  ComPtr<ID3D11UnorderedAccessView> soft_uav;
  ComPtr<ID3D11ShaderResourceView> soft_srv;
  ComPtr<ID3D11UnorderedAccessView> penumbra_uav;
  ComPtr<ID3D11ShaderResourceView> penumbra_srv;
  ComPtr<ID3D11Texture2D> debug_texture;
  ComPtr<ID3D11UnorderedAccessView> debug_uav;
  uint64_t drawn_frame = UINT64_MAX;  // dlaa's frame the march last ran in
  bool softened = false;              // then into soft_srv
  uint64_t debug_frame = UINT64_MAX;  // the Debug View's, once a frame
  bool failed = false;
};

struct State {
  std::mutex mutex;
  Objects objects;  // on the game's device
};
static State state;

// The objects on `device` and the occlusion at `width` x `height` (made again when either changes); false: not available.
// The state's lock held.
inline bool Prepare(ID3D11Device* device, UINT width, UINT height) {
  auto& objects = state.objects;
  if (objects.device.Get() != device) {
    objects = {.device = device};
    ComPtr<ID3D11Device1> device1;
    const D3D_FEATURE_LEVEL feature_level = device->GetFeatureLevel();
    const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = sizeof(ShaderSettings), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC camera_desc = {.ByteWidth = sizeof(CameraConstants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC extended_desc = {.ByteWidth = sizeof(twattila::screen_space_shadows::internal::ExtendedSettings),
                                             .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC arguments_desc = {
        .ByteWidth = DISPATCH_COUNT * 12,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
        .MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
    };
    const D3D11_UNORDERED_ACCESS_VIEW_DESC arguments_view = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = DISPATCH_COUNT * 3, .Flags = D3D11_BUFFER_UAV_FLAG_RAW},
    };
    const D3D11_BUFFER_DESC parameters_desc = {
        .ByteWidth = (1 + DISPATCH_COUNT) * 16,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
        .StructureByteStride = 16,
    };
    const D3D11_SAMPLER_DESC border_desc = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
        .AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
        .AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
        .AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .BorderColor = {1.f, 1.f, 1.f, 1.f},
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    objects.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                     || FAILED(device1->CreateDeviceContextState(0, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                                 objects.private_state.GetAddressOf()))
                     || FAILED(device->CreateComputeShader(__sss_setup.data(), __sss_setup.size(), nullptr, objects.setup.GetAddressOf()))
                     || FAILED(device->CreateComputeShader(__sss_bend.data(), __sss_bend.size(), nullptr, objects.bend.GetAddressOf()))
                     || FAILED(device->CreateComputeShader(__sss_blur.data(), __sss_blur.size(), nullptr, objects.blur.GetAddressOf()))
                     || FAILED(device->CreateComputeShader(__sss_bend_extended_find.data(), __sss_bend_extended_find.size(), nullptr,
                                                           objects.bend_extended.GetAddressOf()))
                     || FAILED(device->CreateComputeShader(__sss_soft.data(), __sss_soft.size(), nullptr, objects.soft.GetAddressOf()))
                     || FAILED(device->CreateComputeShader(__sss_debug.data(), __sss_debug.size(), nullptr, objects.debug.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&settings_desc, nullptr, objects.settings.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&extended_desc, nullptr, objects.extended_settings.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&camera_desc, nullptr, objects.camera.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&arguments_desc, nullptr, objects.arguments.GetAddressOf()))
                     || FAILED(device->CreateUnorderedAccessView(objects.arguments.Get(), &arguments_view, objects.arguments_uav.GetAddressOf()))
                     || FAILED(device->CreateBuffer(&parameters_desc, nullptr, objects.parameters.GetAddressOf()))
                     || FAILED(device->CreateUnorderedAccessView(objects.parameters.Get(), nullptr, objects.parameters_uav.GetAddressOf()))
                     || FAILED(device->CreateShaderResourceView(objects.parameters.Get(), nullptr, objects.parameters_srv.GetAddressOf()))
                     || FAILED(device->CreateSamplerState(&border_desc, objects.border.GetAddressOf()));
    for (UINT index = 0; !objects.failed && index < DISPATCH_COUNT; ++index) {
      const std::array<UINT, 4> value = {index, 0, 0, 0};
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = value.data()};
      objects.failed = FAILED(device->CreateBuffer(&desc, &data, objects.dispatch_indexes[index].GetAddressOf()));
    }
  }
  if (objects.failed) return false;
  if (objects.width == width && objects.height == height) return true;
  objects.width = 0;
  objects.occlusion_uav.Reset();
  objects.occlusion_srv.Reset();
  objects.soft_uav.Reset();
  objects.soft_srv.Reset();
  objects.penumbra_uav.Reset();
  objects.penumbra_srv.Reset();
  objects.debug_texture.Reset();
  objects.debug_uav.Reset();
  if (!twattila::screen_space_shadows::internal::MakeTexture(device, width, height, DXGI_FORMAT_R8_UNORM, &objects.occlusion_uav, &objects.occlusion_srv)
      || !twattila::screen_space_shadows::internal::MakeTexture(device, width, height, DXGI_FORMAT_R8_UNORM, &objects.soft_uav, &objects.soft_srv)) {
    return false;
  }
  objects.width = width;
  objects.height = height;
  return true;
}

inline void OnDestroyDevice(reshade::api::device* device) {
  ray_tracing::internal::ReleaseObjects(device);
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(state.mutex);
  if (state.objects.device.Get() == native) state.objects = {};
}

// The march and the multiply (Apply; test/tw_romer_rays): the scene's `depth` (standard depth, the mask's size) with
// `camera` toward the sun of `frame_constants` (c2), into the mask's red (`mask`).
inline void Draw(ID3D11DeviceContext1* native, ID3D11ShaderResourceView* depth, const dlaa::internal::Camera& camera,
                 ID3D11Buffer* frame_constants, ID3D11RenderTargetView* mask) {
  ComPtr<ID3D11Resource> depth_resource;
  depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Resource> mask_resource;
  mask->GetResource(mask_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  ComPtr<ID3D11Texture2D> mask_texture;
  if (FAILED(depth_resource.As(&depth_texture)) || FAILED(mask_resource.As(&mask_texture))) return;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  D3D11_TEXTURE2D_DESC mask_desc = {};
  depth_texture->GetDesc(&depth_desc);
  mask_texture->GetDesc(&mask_desc);
  if (depth_desc.Width != mask_desc.Width || depth_desc.Height != mask_desc.Height || depth_desc.SampleDesc.Count != 1) return;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!ray_tracing::internal::MakeObjects(device.Get())) return;
  const std::unique_lock lock(state.mutex);
  auto& objects = state.objects;
  if (!Prepare(device.Get(), depth_desc.Width, depth_desc.Height)) return;
  // Bend's rows as Attila's (feature_settings::SssSettings); of the extended march's, Softer Away from the Caster alone
  // (Auto Thickness has no row; banners and torches are not installed here).
  const twattila::screen_space_shadows::Settings rows_settings = twattila::feature_settings::SssSettings(rows);
  namespace attila_sss = twattila::screen_space_shadows::internal;
  const bool soft = rows_settings.sun_disk > 0.f
                    && (objects.penumbra_uav
                        || attila_sss::MakeTexture(device.Get(), depth_desc.Width, depth_desc.Height, DXGI_FORMAT_R16_FLOAT,
                                                   &objects.penumbra_uav, &objects.penumbra_srv));
  const bool softened = soft || rows_settings.softness > 0.f;
  const ShaderSettings settings = {
      .width = static_cast<float>(depth_desc.Width),
      .height = static_cast<float>(depth_desc.Height),
      .inverse_width = 1.f / static_cast<float>(depth_desc.Width),
      .inverse_height = 1.f / static_cast<float>(depth_desc.Height),
      .thickness = rows_settings.thickness,
      .samples = rows_settings.samples,
      .contrast = rows_settings.contrast,
      .edge_threshold = rows_settings.edge_threshold,
      .hard_samples = rows_settings.hard_samples,
      // Within the samples left after the hard ones (Bend reads past them otherwise), as Attila's.
      .fade_samples = std::min(rows_settings.fade_samples, rows_settings.samples - rows_settings.hard_samples),
      .ignore_edges = rows_settings.ignore_edges ? 1.f : 0.f,
      .precision_offset = rows_settings.precision_offset ? 1.f : 0.f,
      .sample_along_ray = rows_settings.sample_along_ray ? 1.f : 0.f,
      .softness = rows_settings.softness,
      .debug_view = static_cast<float>(rows_settings.debug_view),
      .mirror_edges = rows_settings.mirror_edges ? 1.f : 0.f,
      .render_width = static_cast<float>(depth_desc.Width),
      .render_height = static_cast<float>(depth_desc.Height),
  };
  if (std::memcmp(&settings, &objects.uploaded, sizeof(ShaderSettings)) != 0) {
    native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &settings, 0, 0);
    objects.uploaded = settings;
  }
  const attila_sss::ExtendedSettings extended_settings = {
      .sun_tangent = (soft ? std::tan(rows_settings.sun_disk * (3.14159265f / 360.f)) : 0.f),
      .soft_radius = attila_sss::SOFT_RADIUS_2160 * static_cast<float>(depth_desc.Height) / 2160.f,
  };
  if (std::memcmp(&extended_settings, &objects.uploaded_extended, sizeof(extended_settings)) != 0) {
    native->UpdateSubresource(objects.extended_settings.Get(), 0, nullptr, &extended_settings, 0, 0);
    objects.uploaded_extended = extended_settings;
  }
  CameraConstants camera_constants = {
      .projection = dlaa::internal::Transposed(camera.projection),
      .view_projection = dlaa::internal::Transposed(camera.jittered_view_projection),
  };
  for (float& value : camera_constants.view_projection) value = -value;
  // The jittered projection's inverse (the depth as drawn): the view-projection's (in doubles) x the view.
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) {
      double sum = 0.;
      for (size_t k = 0; k < 4; ++k) sum += camera.inverse_rows[(row * 4) + k] * camera.view[(k * 4) + column];
      camera_constants.inverse_projection[(column * 4) + row] = static_cast<float>(sum);
    }
  }
  native->UpdateSubresource(objects.camera.Get(), 0, nullptr, &camera_constants, 0, 0);
  {
    const talesofarise::dlss::ContextStateScope scope(native, objects.private_state.Get());
    // The dispatch list for this camera and sun, the occlusion cleared (Bend leaves the sky's pixels), Bend's dispatches;
    // the unordered access views set before the views of what they wrote.
    const std::array<ID3D11Buffer*, 3> setup_constants = {objects.camera.Get(), frame_constants, objects.settings.Get()};
    const std::array<ID3D11UnorderedAccessView*, 2> setup_outputs = {objects.arguments_uav.Get(), objects.parameters_uav.Get()};
    native->CSSetShader(objects.setup.Get(), nullptr, 0);
    native->CSSetConstantBuffers(0, 3, setup_constants.data());
    native->CSSetUnorderedAccessViews(0, 2, setup_outputs.data(), nullptr);
    native->Dispatch(1, 1, 1);
    constexpr std::array<FLOAT, 4> ZEROS = {};
    native->ClearUnorderedAccessViewFloat(objects.occlusion_uav.Get(), ZEROS.data());
    if (soft) native->ClearUnorderedAccessViewFloat(objects.penumbra_uav.Get(), ZEROS.data());
    // Softer Away from the Caster: the extended march, each pixel's penumbra too (u1; b0's inverse projection, b4).
    const std::array<ID3D11UnorderedAccessView*, 2> bend_outputs = {objects.occlusion_uav.Get(), soft ? objects.penumbra_uav.Get() : nullptr};
    const std::array<ID3D11ShaderResourceView*, 2> bend_inputs = {depth, objects.parameters_srv.Get()};
    native->CSSetShader((soft ? objects.bend_extended : objects.bend).Get(), nullptr, 0);
    native->CSSetUnorderedAccessViews(0, 2, bend_outputs.data(), nullptr);
    native->CSSetShaderResources(0, 2, bend_inputs.data());
    native->CSSetSamplers(0, 1, objects.border.GetAddressOf());
    native->CSSetConstantBuffers(4, 1, objects.extended_settings.GetAddressOf());
    for (UINT index = 0; index < DISPATCH_COUNT; ++index) {
      native->CSSetConstantBuffers(3, 1, objects.dispatch_indexes[index].GetAddressOf());
      native->DispatchIndirect(objects.arguments.Get(), index * 12);
    }
    // The occlusion smoothed along surfaces: by each pixel's penumbra (sss_soft, Softness its least), else by Softness
    // (sss_blur); their settings at b2 and b4 as above, t3 sss_setup's parameters (X30: the depth at infinity).
    if (softened) {
      const std::array<ID3D11UnorderedAccessView*, 2> blur_outputs = {objects.soft_uav.Get(), nullptr};
      const std::array<ID3D11ShaderResourceView*, 4> blur_inputs = {depth, objects.occlusion_srv.Get(), soft ? objects.penumbra_srv.Get() : nullptr,
                                                                    objects.parameters_srv.Get()};
      native->CSSetShader((soft ? objects.soft : objects.blur).Get(), nullptr, 0);
      native->CSSetUnorderedAccessViews(0, 2, blur_outputs.data(), nullptr);
      native->CSSetShaderResources(0, 4, blur_inputs.data());
      native->Dispatch((depth_desc.Width + 7) / 8, (depth_desc.Height + 7) / 8, 1);
    }
    // Nothing of the game's or of the occlusion kept bound in the addon's state.
    constexpr std::array<ID3D11UnorderedAccessView*, 2> NO_OUTPUTS = {};
    constexpr std::array<ID3D11ShaderResourceView*, 4> NO_INPUTS = {};
    constexpr std::array<ID3D11Buffer*, 5> NO_CONSTANTS = {};
    native->CSSetUnorderedAccessViews(0, 2, NO_OUTPUTS.data(), nullptr);
    native->CSSetShaderResources(0, 4, NO_INPUTS.data());
    native->CSSetConstantBuffers(0, 5, NO_CONSTANTS.data());
  }
  ray_tracing::internal::DrawMultiply(native, mask, (softened ? objects.soft_srv : objects.occlusion_srv).Get(), true);
  objects.drawn_frame = camera.frame;
  objects.softened = softened;
}

// Bend's Debug View over the tone mapper's output `target` (the occlusion's size, RGBA16F): `frame`'s occlusion as the
// picture (sss_debug), once a frame and only in one the march ran in; false: not drawn.
inline bool DrawDebug(ID3D11DeviceContext1* native, ID3D11Texture2D* target, uint64_t frame) {
  D3D11_TEXTURE2D_DESC desc = {};
  target->GetDesc(&desc);
  const std::unique_lock lock(state.mutex);
  auto& objects = state.objects;
  if (objects.drawn_frame != frame || objects.debug_frame == frame || desc.Width != objects.width || desc.Height != objects.height
      || desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1
      || (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS)) {
    return false;
  }
  if (!objects.debug_uav) {
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    D3D11_TEXTURE2D_DESC debug_desc = desc;
    debug_desc.Usage = D3D11_USAGE_DEFAULT;
    debug_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    debug_desc.CPUAccessFlags = 0;
    debug_desc.MiscFlags = 0;
    const D3D11_UNORDERED_ACCESS_VIEW_DESC view = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
    if (FAILED(device->CreateTexture2D(&debug_desc, nullptr, objects.debug_texture.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(objects.debug_texture.Get(), &view, objects.debug_uav.GetAddressOf()))) {
      objects.debug_texture.Reset();
      objects.debug_uav.Reset();
      return false;
    }
  }
  {
    const talesofarise::dlss::ContextStateScope scope(native, objects.private_state.Get());
    const std::array<ID3D11ShaderResourceView*, 2> views = {(objects.softened ? objects.soft_srv : objects.occlusion_srv).Get(),
                                                            objects.parameters_srv.Get()};
    native->CSSetShader(objects.debug.Get(), nullptr, 0);
    native->CSSetUnorderedAccessViews(0, 1, objects.debug_uav.GetAddressOf(), nullptr);
    native->CSSetShaderResources(0, 2, views.data());
    native->CSSetConstantBuffers(2, 1, objects.settings.GetAddressOf());
    native->Dispatch((objects.width + 7) / 8, (objects.height + 7) / 8, 1);
    constexpr std::array<ID3D11ShaderResourceView*, 2> NO_INPUTS = {};
    ID3D11UnorderedAccessView* const no_output = nullptr;
    ID3D11Buffer* const no_constants = nullptr;
    native->CSSetUnorderedAccessViews(0, 1, &no_output, nullptr);
    native->CSSetShaderResources(0, 2, NO_INPUTS.data());
    native->CSSetConstantBuffers(2, 1, &no_constants);
  }
  native->CopyResource(target, objects.debug_texture.Get());
  objects.debug_frame = frame;
  return true;
}

}  // namespace internal

// After the shadow mask's resolve (CSMResolvePS; its bindings still set): this frame's screen-space shadows into its red.
inline void Apply(reshade::api::command_list* cmd_list) {
  if (rows.enabled == 0.f || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  dlaa::internal::Camera camera;
  const ComPtr<ID3D11ShaderResourceView> depth = dlaa::SceneDepth();
  ComPtr<ID3D11Buffer> frame_constants;
  native->PSGetConstantBuffers(8, 1, frame_constants.GetAddressOf());
  ComPtr<ID3D11RenderTargetView> mask;
  native->OMGetRenderTargets(1, mask.GetAddressOf(), nullptr);
  ComPtr<ID3D11DeviceContext1> native1;
  if (!depth || !frame_constants || !mask || !dlaa::MainCamera(&camera) || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
    return;
  }
  internal::Draw(native1.Get(), depth.Get(), camera, frame_constants.Get(), mask.Get());
}

// After the tone mapper (addon.cpp's tone mapper hook, with the rays' Debug View): Bend's Debug View over its output.
inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  if (rows.enabled == 0.f || rows.debug_view == 0.f || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  dlaa::internal::Camera camera;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  ComPtr<ID3D11DeviceContext1> native1;
  if (!target_view || !dlaa::MainCamera(&camera) || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  if (ComPtr<ID3D11Texture2D> target; SUCCEEDED(resource.As(&target))) internal::DrawDebug(native1.Get(), target.Get(), camera.frame);
}

inline void Install() {
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
}

}  // namespace twromeremastered::contact_shadows
