/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include <embed/shaders.h>

#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/frame_history.hpp"
#include "../twattila/light_bounce.h"

// Light Bounce (GT-VBGI, Experimental): after Ryse's tiled lighting (cs 0x897232B5: t0 linear depth, t1 normals, t3
// albedo, b0 its camera, u1 the HDR scene it lit), the light bouncing off what the camera sees onto each pixel, added to
// that HDR scene x the pixel's albedo. The Total War games' passes (twattila/light_bounce_*.hlsli, game-neutral) on
// Ryse's G-buffer (light_bounce.hlsli): the prepare (a copy of the lit scene as the light; at 1 / scale of the
// resolution) and its levels, the trace (the visibility bitmask), the resolve with the last frames' bounce (the camera
// kept for the next frame), the optional denoiser, then the apply into the scene, additively. In the addon's own context
// state; the game's is put back after.
namespace rysesonofrome::light_bounce {

using Microsoft::WRL::ComPtr;
using twattila::frame_history::Target;

namespace internal {

// The views at t40-t48: the light copy, the history (then the apply's bounce), the G-buffer (normals t42, albedo t43,
// t44 unused, depth t45), the prepared light and geometry, the resolve's last frame counts.
constexpr UINT VIEWS_SLOT = 40;
constexpr size_t LIGHT_VIEW = 0;
constexpr size_t HISTORY_VIEW = 1;
constexpr size_t NORMAL_VIEW = 2;
constexpr size_t ALBEDO_VIEW = 3;
constexpr size_t DEPTH_VIEW = 5;
constexpr size_t PREPARED_VIEW = 6;
constexpr size_t FRAMES_VIEW = 8;
constexpr UINT VIEW_COUNT = 9;
// The constants at b10-b12: the last frame's camera, this frame's (the tiled lighting's PER_BATCH), the Light Bounce's.
constexpr UINT CONSTANTS_SLOT = 10;
constexpr UINT PREPARED_LEVELS = 6;

struct PreparedTarget {
  ComPtr<ID3D11Texture2D> texture;
  std::array<ComPtr<ID3D11RenderTargetView>, PREPARED_LEVELS> level_rtvs;
  std::array<ComPtr<ID3D11ShaderResourceView>, PREPARED_LEVELS> level_srvs;
  ComPtr<ID3D11ShaderResourceView> srv;
};

struct Objects {
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> prepare;
  ComPtr<ID3D11PixelShader> levels;
  ComPtr<ID3D11PixelShader> trace;
  ComPtr<ID3D11PixelShader> resolve;
  ComPtr<ID3D11PixelShader> denoise;
  ComPtr<ID3D11PixelShader> apply;
  ComPtr<ID3D11BlendState> add_blend;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ComPtr<ID3D11Buffer> constants;
  LightBounceConstants uploaded = {};
  ComPtr<ID3D11Buffer> previous_camera;
  // The scene's copy and its target (its size and format); the prepared textures and histories (1 / scale of it).
  ComPtr<ID3D11Resource> scene;
  D3D11_TEXTURE2D_DESC scene_desc = {};
  UINT bounce_scale = 0;
  ComPtr<ID3D11RenderTargetView> scene_target;
  ComPtr<ID3D11Texture2D> light_copy;
  ComPtr<ID3D11ShaderResourceView> light_copy_srv;
  PreparedTarget prepared_light;
  PreparedTarget prepared_geometry;
  UINT prepared_levels = 0;
  Target traced;
  std::array<Target, 2> history;
  std::array<Target, 2> history_frames;
  size_t history_index = 0;
  Target denoised;
};
static Objects objects;

inline bool CreateShaders(ID3D11Device* device) {
  if (objects.device.Get() == device) return !objects.failed;
  objects = {};
  objects.device = device;
  ComPtr<ID3D11Device1> device1;
  const auto feature_level = device->GetFeatureLevel();
  const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
  D3D11_BLEND_DESC add_desc = {};
  add_desc.RenderTarget[0] = {
      .BlendEnable = TRUE,
      .SrcBlend = D3D11_BLEND_ONE,
      .DestBlend = D3D11_BLEND_ONE,
      .BlendOp = D3D11_BLEND_OP_ADD,
      .SrcBlendAlpha = D3D11_BLEND_ZERO,
      .DestBlendAlpha = D3D11_BLEND_ONE,
      .BlendOpAlpha = D3D11_BLEND_OP_ADD,
      .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE,
  };
  const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE};
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = sizeof(LightBounceConstants), .Usage = D3D11_USAGE_DEFAULT,
                                            .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const auto pixel = [&](std::span<const uint8_t> code, ComPtr<ID3D11PixelShader>* shader) {
    return SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
  };
  objects.failed =
      FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
      || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                  nullptr, objects.private_state.GetAddressOf()))
      || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, objects.full_screen.GetAddressOf()))
      || !pixel(__light_bounce_prepare, &objects.prepare) || !pixel(__light_bounce_levels, &objects.levels)
      || !pixel(__light_bounce_trace, &objects.trace) || !pixel(__light_bounce_resolve, &objects.resolve)
      || !pixel(__light_bounce_denoise, &objects.denoise) || !pixel(__light_bounce_apply, &objects.apply)
      || FAILED(device->CreateBlendState(&add_desc, objects.add_blend.GetAddressOf()))
      || FAILED(device->CreateRasterizerState(&rasterizer_desc, objects.rasterizer.GetAddressOf()))
      || FAILED(device->CreateBuffer(&constants_desc, nullptr, objects.constants.GetAddressOf()));
  return !objects.failed;
}

// The textures for this scene target and scale (made again when either changes).
inline bool CreateTextures(ID3D11DeviceContext* context, ID3D11Resource* scene, const D3D11_TEXTURE2D_DESC& scene_desc,
                           DXGI_FORMAT scene_format, UINT scale) {
  if (objects.scene.Get() == scene && objects.scene_desc.Width == scene_desc.Width && objects.scene_desc.Height == scene_desc.Height
      && objects.scene_desc.Format == scene_desc.Format && objects.bounce_scale == scale) {
    return static_cast<bool>(objects.scene_target);
  }
  auto* device = objects.device.Get();
  objects.scene = scene;
  objects.scene_desc = scene_desc;
  objects.bounce_scale = scale;
  objects.scene_target.Reset();
  D3D11_TEXTURE2D_DESC copy = scene_desc;
  copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  copy.MiscFlags = 0;
  const D3D11_SHADER_RESOURCE_VIEW_DESC copy_view = {.Format = scene_format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                     .Texture2D = {.MipLevels = 1}};
  const D3D11_RENDER_TARGET_VIEW_DESC target_view = {.Format = scene_format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
  const UINT width = (scene_desc.Width + scale - 1) / scale;
  const UINT height = (scene_desc.Height + scale - 1) / scale;
  objects.prepared_levels = std::min<UINT>(PREPARED_LEVELS, std::bit_width(std::max(width, height)));
  const auto make_prepared = [&](DXGI_FORMAT format, PreparedTarget* prepared) {
    *prepared = {};
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = width,
        .Height = height,
        .MipLevels = objects.prepared_levels,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };
    if (FAILED(device->CreateTexture2D(&desc, nullptr, prepared->texture.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(prepared->texture.Get(), nullptr, prepared->srv.GetAddressOf()))) {
      return false;
    }
    for (UINT level = 0; level < objects.prepared_levels; ++level) {
      const D3D11_RENDER_TARGET_VIEW_DESC level_target = {.Format = format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
                                                          .Texture2D = {.MipSlice = level}};
      const D3D11_SHADER_RESOURCE_VIEW_DESC level_view = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                          .Texture2D = {.MostDetailedMip = level, .MipLevels = 1}};
      if (FAILED(device->CreateRenderTargetView(prepared->texture.Get(), &level_target, prepared->level_rtvs[level].GetAddressOf()))
          || FAILED(device->CreateShaderResourceView(prepared->texture.Get(), &level_view, prepared->level_srvs[level].GetAddressOf()))) {
        return false;
      }
    }
    return true;
  };
  using twattila::frame_history::MakeTarget;
  if ((scene_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0
      || FAILED(device->CreateRenderTargetView(scene, &target_view, objects.scene_target.GetAddressOf()))
      || FAILED(device->CreateTexture2D(&copy, nullptr, objects.light_copy.ReleaseAndGetAddressOf()))
      || FAILED(device->CreateShaderResourceView(objects.light_copy.Get(), &copy_view, objects.light_copy_srv.ReleaseAndGetAddressOf()))
      || !make_prepared(DXGI_FORMAT_R11G11B10_FLOAT, &objects.prepared_light)
      || !make_prepared(DXGI_FORMAT_R32G32_UINT, &objects.prepared_geometry)
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.traced)
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.history[0])
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.history[1])
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16_FLOAT, &objects.history_frames[0])
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16_FLOAT, &objects.history_frames[1])
      || !MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.denoised)) {
    objects.scene_target.Reset();
    return false;
  }
  // No bounce and no history until the first trace.
  constexpr float NOTHING[4] = {0.f, 0.f, 0.f, 0.f};
  for (const Target* target : {&objects.history[0], &objects.history[1], &objects.history_frames[0], &objects.history_frames[1],
                               &objects.denoised}) {
    context->ClearRenderTargetView(target->rtv.Get(), NOTHING);
  }
  return true;
}

}  // namespace internal

// After the tiled lighting's dispatch on `context` (its bindings still set): the bounce added to its HDR scene.
// `constants`: the Light Bounce's (strength 0: off), `denoise`: the resolved bounce smoothed along surfaces before the
// apply.
// The trace through rays (ray_tracing.hpp: twattila/rt_core.hpp's TraceLightBounce, path traced when on): the prepared
// light (its first level's top-left `width` x `height`, a texel each `scale` scene pixels) in, the bounce into `traced`;
// false: not this frame (the screen-space trace then). Null: never.
inline bool (*ray_traced_trace)(ID3D11DeviceContext* context, ID3D11Texture2D* prepared_light, UINT width, UINT height, UINT scale,
                                ID3D11Texture2D* traced) = nullptr;

inline void Apply(ID3D11DeviceContext* context, const LightBounceConstants& constants, bool denoise) {
  using internal::objects;
  ComPtr<ID3D11DeviceContext1> context1;
  if (!(constants.strength > 0.f) || context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
    return;
  }
  std::array<ComPtr<ID3D11ShaderResourceView>, 4> gbuffer;  // depth, normals, specular, albedo (t0-t3)
  {
    std::array<ID3D11ShaderResourceView*, 4> pointers = {};
    context->CSGetShaderResources(0, 4, pointers.data());
    for (size_t index = 0; index < gbuffer.size(); ++index) gbuffer[index].Attach(pointers[index]);
  }
  ComPtr<ID3D11UnorderedAccessView> scene_view;
  ComPtr<ID3D11Buffer> camera;
  context->CSGetUnorderedAccessViews(1, 1, scene_view.GetAddressOf());
  context->CSGetConstantBuffers(0, 1, camera.GetAddressOf());
  if (!gbuffer[0] || !gbuffer[1] || !gbuffer[3] || !scene_view || !camera) return;
  ComPtr<ID3D11Resource> scene;
  scene_view->GetResource(scene.GetAddressOf());
  ComPtr<ID3D11Texture2D> scene_texture;
  if (FAILED(scene.As(&scene_texture))) return;
  D3D11_TEXTURE2D_DESC scene_desc = {};
  scene_texture->GetDesc(&scene_desc);
  D3D11_UNORDERED_ACCESS_VIEW_DESC scene_view_desc = {};
  scene_view->GetDesc(&scene_view_desc);
  if (scene_desc.MipLevels != 1 || scene_desc.ArraySize != 1 || scene_desc.SampleDesc.Count != 1) return;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const auto scale = static_cast<UINT>(constants.scale);
  if (!internal::CreateShaders(device.Get())
      || !internal::CreateTextures(context, scene.Get(), scene_desc, scene_view_desc.Format, scale)) {
    return;
  }
  if (std::memcmp(&objects.uploaded, &constants, sizeof(LightBounceConstants)) != 0) {
    context->UpdateSubresource(objects.constants.Get(), 0, nullptr, &constants, 0, 0);
    objects.uploaded = constants;
  }
  // The first frame: its own camera as the last one.
  if (!objects.previous_camera) twattila::frame_history::KeepCamera(device.Get(), context, camera.Get(), std::addressof(objects.previous_camera));

  const talesofarise::dlss::ContextStateScope scope(context1.Get(), objects.private_state.Get());
  context->CopyResource(objects.light_copy.Get(), scene.Get());
  std::array<ID3D11ShaderResourceView*, internal::VIEW_COUNT> inputs = {};
  inputs[internal::NORMAL_VIEW] = gbuffer[1].Get();
  inputs[internal::ALBEDO_VIEW] = gbuffer[3].Get();
  inputs[internal::DEPTH_VIEW] = gbuffer[0].Get();
  ID3D11Buffer* buffers[] = {objects.previous_camera.Get(), camera.Get(), objects.constants.Get()};
  context->PSSetConstantBuffers(internal::CONSTANTS_SLOT, 3, buffers);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(objects.full_screen.Get(), nullptr, 0);
  context->RSSetState(objects.rasterizer.Get());
  const D3D11_VIEWPORT trace_viewport = {
      .Width = std::ceil(static_cast<float>(scene_desc.Width) / scale),
      .Height = std::ceil(static_cast<float>(scene_desc.Height) / scale),
      .MaxDepth = 1.f,
  };
  context->RSSetViewports(1, &trace_viewport);

  // The prepare (the scene's copy as the light; the last history for Multi-Bounce), then the levels.
  inputs[internal::LIGHT_VIEW] = objects.light_copy_srv.Get();
  inputs[internal::HISTORY_VIEW] = objects.history[objects.history_index].srv.Get();
  context->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, inputs.data());
  ID3D11RenderTargetView* prepared[] = {objects.prepared_light.level_rtvs[0].Get(), objects.prepared_geometry.level_rtvs[0].Get()};
  context->OMSetRenderTargets(2, prepared, nullptr);
  context->PSSetShader(objects.prepare.Get(), nullptr, 0);
  context->Draw(3, 0);
  D3D11_TEXTURE2D_DESC prepared_desc = {};
  objects.prepared_light.texture->GetDesc(&prepared_desc);
  context->PSSetShader(objects.levels.Get(), nullptr, 0);
  for (UINT level = 1; level < objects.prepared_levels; ++level) {
    ID3D11RenderTargetView* level_targets[] = {objects.prepared_light.level_rtvs[level].Get(),
                                               objects.prepared_geometry.level_rtvs[level].Get()};
    context->OMSetRenderTargets(2, level_targets, nullptr);
    ID3D11ShaderResourceView* above[] = {objects.prepared_light.level_srvs[level - 1].Get(),
                                         objects.prepared_geometry.level_srvs[level - 1].Get()};
    context->PSSetShaderResources(internal::VIEWS_SLOT + internal::PREPARED_VIEW, 2, above);
    const D3D11_VIEWPORT level_viewport = {
        .Width = static_cast<FLOAT>(std::max(1u, prepared_desc.Width >> level)),
        .Height = static_cast<FLOAT>(std::max(1u, prepared_desc.Height >> level)),
        .MaxDepth = 1.f,
    };
    context->RSSetViewports(1, &level_viewport);
    context->Draw(3, 0);
  }
  context->RSSetViewports(1, &trace_viewport);

  // The trace, the resolve into the other history (and its frame counts), the denoiser; each target set before its
  // input, so no texture is bound as both.
  const auto pass = [&](const Target& target, ID3D11PixelShader* shader, const Target* frames = nullptr) {
    ID3D11RenderTargetView* targets[] = {target.rtv.Get(), (frames != nullptr) ? frames->rtv.Get() : nullptr};
    context->OMSetRenderTargets((frames != nullptr) ? 2 : 1, targets, nullptr);
    context->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, inputs.data());
    context->PSSetShader(shader, nullptr, 0);
    context->Draw(3, 0);
  };
  const size_t previous_index = objects.history_index;
  objects.history_index ^= 1;
  inputs[internal::LIGHT_VIEW] = nullptr;
  inputs[internal::HISTORY_VIEW] = objects.history[previous_index].srv.Get();
  inputs[internal::PREPARED_VIEW] = objects.prepared_light.srv.Get();
  inputs[internal::PREPARED_VIEW + 1] = objects.prepared_geometry.srv.Get();
  if (ray_traced_trace == nullptr
      || !ray_traced_trace(context, objects.prepared_light.texture.Get(), static_cast<UINT>(trace_viewport.Width),
                           static_cast<UINT>(trace_viewport.Height), scale, objects.traced.texture.Get())) {
    pass(objects.traced, objects.trace.Get());
  }
  inputs[internal::LIGHT_VIEW] = objects.traced.srv.Get();
  inputs[internal::FRAMES_VIEW] = objects.history_frames[previous_index].srv.Get();
  pass(objects.history[objects.history_index], objects.resolve.Get(), &objects.history_frames[objects.history_index]);
  inputs[internal::FRAMES_VIEW] = nullptr;
  if (denoise) {
    inputs[internal::LIGHT_VIEW] = objects.history[objects.history_index].srv.Get();
    inputs[internal::HISTORY_VIEW] = nullptr;
    pass(objects.denoised, objects.denoise.Get());
  }

  // The apply: over the whole scene at its size, added (the bounce x albedo x strength).
  inputs[internal::LIGHT_VIEW] = nullptr;
  inputs[internal::HISTORY_VIEW] = (denoise ? objects.denoised.srv.Get() : objects.history[objects.history_index].srv.Get());
  inputs[internal::PREPARED_VIEW] = nullptr;
  inputs[internal::PREPARED_VIEW + 1] = nullptr;
  ID3D11RenderTargetView* scene_target = objects.scene_target.Get();
  context->OMSetRenderTargets(1, &scene_target, nullptr);
  context->OMSetBlendState(objects.add_blend.Get(), nullptr, 0xFFFFFFFF);
  context->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, inputs.data());
  context->PSSetShader(objects.apply.Get(), nullptr, 0);
  const D3D11_VIEWPORT scene_viewport = {
      .Width = static_cast<float>(scene_desc.Width),
      .Height = static_cast<float>(scene_desc.Height),
      .MaxDepth = 1.f,
  };
  context->RSSetViewports(1, &scene_viewport);
  context->Draw(3, 0);
  // This frame's camera for the next trace.
  twattila::frame_history::KeepCamera(device.Get(), context, camera.Get(), std::addressof(objects.previous_camera));
}

// The game's device is going away: the addon's objects on it with it.
inline void Release() {
  internal::objects = {};
}

}  // namespace rysesonofrome::light_bounce
