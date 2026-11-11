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
#include <mutex>
#include <shared_mutex>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./frame_history.hpp"

// The screen-space reflections' depth pyramid (ssr_hiz.hlsli's hierarchical march): on the game's reflection draw
// (ssr00 / ssr01, its replaced shaders' on_draw), once a frame, the depth it reads (t3, the scene's depth) copied into
// an R32 texture's first level and each further level the nearest of the one before (compute, the compute stage's
// bindings put back after), bound at the draw's t10 and unbound after it. With Reflection Rays Half / Quarter or the
// Reflection Denoiser: the draw also writes each pixel's hit (ssr_hiz.hlsli: its reflected distance and confidence) into
// a target the addon binds as its third, and after the draw ssr_resolve fills the pixels not traced this frame and
// blends the reflections with the last frame's (the reflected point reprojected), into the draw's target. D3D11.
namespace twattila::ssr_hiz {

using Microsoft::WRL::ComPtr;

namespace internal {

constexpr UINT DEPTH_SLOT = 3;   // the reflection shaders' gbuffer_channel_4_sampler: the scene's depth
constexpr UINT PYRAMID_SLOT = 10;
constexpr float HISTORY_WEIGHT = 0.85f;

// ssr_resolve.cs's ssr_resolve.
struct ResolveSettings {
  std::array<float, 2> size = {};
  std::array<float, 2> inverse_size = {};
  float denoise = 0.f;
  float reset = 1.f;
  float history_weight = 0.f;
  float padding = 0.f;
  std::array<float, 2> texture_inverse_size = {};
  std::array<float, 2> padding2 = {};
};

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> copy;
  ComPtr<ID3D11ComputeShader> reduce;
  // The depth's size: the pyramid (every level for the draw, each level alone for building it).
  UINT width = 0;
  UINT height = 0;
  ComPtr<ID3D11ShaderResourceView> pyramid;
  std::vector<ComPtr<ID3D11ShaderResourceView>> level_views;
  std::vector<ComPtr<ID3D11UnorderedAccessView>> level_uavs;
  ComPtr<ID3D11ComputeShader> resolve;
  ComPtr<ID3D11Buffer> resolve_settings;
  ComPtr<ID3D11SamplerState> linear_clamp;
  // The reflection target's size and format: the hit target, this frame's copy, the result, the histories (written this
  // frame and the last in turn) and the cameras (this frame's for the next, the last one's).
  UINT target_width = 0;
  UINT target_height = 0;
  DXGI_FORMAT target_format = DXGI_FORMAT_UNKNOWN;
  ComPtr<ID3D11RenderTargetView> hit_rtv;
  ComPtr<ID3D11ShaderResourceView> hit_srv;
  ComPtr<ID3D11Texture2D> reflection_copy;
  ComPtr<ID3D11ShaderResourceView> reflection_copy_srv;
  ComPtr<ID3D11Texture2D> result;
  ComPtr<ID3D11UnorderedAccessView> result_uav;
  std::array<ComPtr<ID3D11Texture2D>, 2> histories;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> history_srvs;
  ComPtr<ID3D11Buffer> last_camera;
  ComPtr<ID3D11Buffer> previous_camera;
};

struct Runtime {
  std::shared_mutex mutex;
  bool enabled = false;
  bool denoise = false;
  int rays = 0;  // 0 every pixel, 1 half, 2 a quarter
  uint64_t frame = 0;
  uint64_t built_frame = UINT64_MAX;
  uint64_t cleared_frame = UINT64_MAX;
  uint64_t resolved_frame = UINT64_MAX;
  size_t history_index = 0;  // the one written this frame
  bool history_valid = false;
  bool reset = true;
  // The game's targets while the hit target is bound with them (put back after the draw).
  bool bound = false;
  std::array<ComPtr<ID3D11RenderTargetView>, 2> game_targets;
  ComPtr<ID3D11DepthStencilView> game_depth;
  DeviceObjects objects;
};

static Runtime runtime;

// The pyramid for a `width` x `height` depth; false: not available.
inline bool Prepare(ID3D11Device* device, UINT width, UINT height) {
  auto& objects = runtime.objects;
  if (objects.device.Get() != device) {
    objects = {};
    objects.device = device;
    const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = sizeof(ResolveSettings), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SAMPLER_DESC sampler_desc = {
        .Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
        .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    const bool made =
        SUCCEEDED(device->CreateComputeShader(__ssr_hiz_copy.data(), __ssr_hiz_copy.size(), nullptr, objects.copy.GetAddressOf()))
        && SUCCEEDED(device->CreateComputeShader(__ssr_hiz_reduce.data(), __ssr_hiz_reduce.size(), nullptr,
                                                 objects.reduce.GetAddressOf()))
        && SUCCEEDED(device->CreateComputeShader(__ssr_resolve.data(), __ssr_resolve.size(), nullptr,
                                                 objects.resolve.GetAddressOf()))
        && SUCCEEDED(device->CreateBuffer(&settings_desc, nullptr, objects.resolve_settings.GetAddressOf()))
        && SUCCEEDED(device->CreateSamplerState(&sampler_desc, objects.linear_clamp.GetAddressOf()));
    if (!made) {
      renodx::utils::log::w("[Total War] Reflections: could not create the depth pyramid's shaders.");
      objects = {};
      objects.device = device;
      return false;
    }
  }
  if (!objects.copy) return false;
  if (objects.width == width && objects.height == height) return true;
  objects.width = 0;
  objects.pyramid.Reset();
  objects.level_views.clear();
  objects.level_uavs.clear();
  UINT levels = 1;
  while ((std::max(width, height) >> levels) != 0) ++levels;
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = width,
      .Height = height,
      .MipLevels = levels,
      .ArraySize = 1,
      .Format = DXGI_FORMAT_R32_FLOAT,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
  };
  ComPtr<ID3D11Texture2D> texture;
  bool made = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()))
              && SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, objects.pyramid.GetAddressOf()));
  for (UINT level = 0; made && level < levels; ++level) {
    const D3D11_SHADER_RESOURCE_VIEW_DESC view = {
        .Format = DXGI_FORMAT_R32_FLOAT,
        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MostDetailedMip = level, .MipLevels = 1},
    };
    const D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {
        .Format = DXGI_FORMAT_R32_FLOAT,
        .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MipSlice = level},
    };
    made = SUCCEEDED(device->CreateShaderResourceView(texture.Get(), &view, objects.level_views.emplace_back().GetAddressOf()))
           && SUCCEEDED(device->CreateUnorderedAccessView(texture.Get(), &uav, objects.level_uavs.emplace_back().GetAddressOf()));
  }
  if (!made) {
    renodx::utils::log::w("[Total War] Reflections: could not create the depth pyramid (", width, " x ", height, ").");
    objects.pyramid.Reset();
    objects.level_views.clear();
    objects.level_uavs.clear();
    return false;
  }
  objects.width = width;
  objects.height = height;
  return true;
}

// This frame's pyramid from `depth_view`, the compute stage's bindings put back after.
inline void Build(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth_view) {
  auto& objects = runtime.objects;
  ComPtr<ID3D11ComputeShader> game_shader;
  native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  ComPtr<ID3D11ShaderResourceView> game_view;
  native->CSGetShaderResources(0, 1, game_view.GetAddressOf());
  ComPtr<ID3D11UnorderedAccessView> game_uav;
  native->CSGetUnorderedAccessViews(0, 1, game_uav.GetAddressOf());

  // The view off before the level is written, the written level bound before it is read.
  ID3D11ShaderResourceView* no_view = nullptr;
  for (size_t level = 0; level < objects.level_uavs.size(); ++level) {
    native->CSSetShaderResources(0, 1, &no_view);
    native->CSSetUnorderedAccessViews(0, 1, objects.level_uavs[level].GetAddressOf(), nullptr);
    if (level == 0) {
      native->CSSetShader(objects.copy.Get(), nullptr, 0);
      native->CSSetShaderResources(0, 1, &depth_view);
    } else {
      native->CSSetShader(objects.reduce.Get(), nullptr, 0);
      native->CSSetShaderResources(0, 1, objects.level_views[level - 1].GetAddressOf());
    }
    const UINT width = std::max(objects.width >> level, 1u);
    const UINT height = std::max(objects.height >> level, 1u);
    native->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
  }
  ID3D11UnorderedAccessView* no_uav = nullptr;
  native->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);

  native->CSSetShader(game_shader.Get(), nullptr, 0);
  native->CSSetShaderResources(0, 1, game_view.GetAddressOf());
  native->CSSetUnorderedAccessViews(0, 1, game_uav.GetAddressOf(), nullptr);
}

// The resolve's textures for the reflection target `desc` (its size and format); false: not available.
inline bool PrepareResolve(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc) {
  auto& objects = runtime.objects;
  if (objects.target_width == desc.Width && objects.target_height == desc.Height && objects.target_format == desc.Format) {
    return true;
  }
  objects.target_width = 0;
  objects.hit_rtv.Reset();
  objects.hit_srv.Reset();
  objects.reflection_copy.Reset();
  objects.reflection_copy_srv.Reset();
  objects.result.Reset();
  objects.result_uav.Reset();
  objects.histories = {};
  objects.history_srvs = {};
  runtime.history_valid = false;
  const auto texture = [&](DXGI_FORMAT format, UINT bind, ComPtr<ID3D11Texture2D>* made) {
    const D3D11_TEXTURE2D_DESC texture_desc = {
        .Width = desc.Width,
        .Height = desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = bind,
    };
    return SUCCEEDED(device->CreateTexture2D(&texture_desc, nullptr, made->GetAddressOf()));
  };
  ComPtr<ID3D11Texture2D> hit;
  bool made = texture(DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, &hit)
              && SUCCEEDED(device->CreateRenderTargetView(hit.Get(), nullptr, objects.hit_rtv.GetAddressOf()))
              && SUCCEEDED(device->CreateShaderResourceView(hit.Get(), nullptr, objects.hit_srv.GetAddressOf()))
              && texture(desc.Format, D3D11_BIND_SHADER_RESOURCE, &objects.reflection_copy)
              && SUCCEEDED(device->CreateShaderResourceView(objects.reflection_copy.Get(), nullptr,
                                                            objects.reflection_copy_srv.GetAddressOf()))
              && texture(desc.Format, D3D11_BIND_UNORDERED_ACCESS, &objects.result)
              && SUCCEEDED(device->CreateUnorderedAccessView(objects.result.Get(), nullptr, objects.result_uav.GetAddressOf()));
  for (size_t index = 0; made && index < objects.histories.size(); ++index) {
    made = texture(desc.Format, D3D11_BIND_SHADER_RESOURCE, &objects.histories[index])
           && SUCCEEDED(device->CreateShaderResourceView(objects.histories[index].Get(), nullptr,
                                                         objects.history_srvs[index].GetAddressOf()));
  }
  if (!made) {
    renodx::utils::log::w("[Total War] Reflections: could not create the resolve's textures (format ",
                          static_cast<int>(desc.Format), ").");
    objects.hit_rtv.Reset();
    return false;
  }
  objects.target_width = desc.Width;
  objects.target_height = desc.Height;
  objects.target_format = desc.Format;
  return true;
}

// The reflection target `target` resolved (fill, denoiser) with the scene's depth and this frame's camera, the compute
// stage's bindings put back after.
inline void Resolve(ID3D11DeviceContext* native, ID3D11Resource* target, ID3D11ShaderResourceView* depth_view,
                    ID3D11Buffer* camera) {
  auto& objects = runtime.objects;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (runtime.resolved_frame != runtime.frame) {
    // A new frame: the last one's camera becomes the previous one, the histories swap.
    runtime.reset = !(runtime.history_valid && runtime.resolved_frame + 1 == runtime.frame && objects.last_camera);
    if (objects.last_camera) frame_history::KeepCamera(device.Get(), native, objects.last_camera.Get(), &objects.previous_camera);
    runtime.history_index ^= 1;
    runtime.resolved_frame = runtime.frame;
  }
  native->CopyResource(objects.reflection_copy.Get(), target);
  // The view's part of the target (DLSS below 100%: its top-left, the render size's share of the display, the scene
  // depth's size).
  std::array<UINT, 2> view = {objects.target_width, objects.target_height};
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  if (ComPtr<ID3D11Texture2D> depth_texture; SUCCEEDED(depth_resource.As(&depth_texture))) {
    D3D11_TEXTURE2D_DESC depth_desc = {};
    depth_texture->GetDesc(&depth_desc);
    const auto render = frame_history::RenderSize(depth_desc.Width, depth_desc.Height);
    view = {(objects.target_width * render[0] + depth_desc.Width - 1) / depth_desc.Width,
            (objects.target_height * render[1] + depth_desc.Height - 1) / depth_desc.Height};
  }
  const ResolveSettings settings = {
      .size = {static_cast<float>(view[0]), static_cast<float>(view[1])},
      .inverse_size = {1.f / static_cast<float>(view[0]), 1.f / static_cast<float>(view[1])},
      .denoise = (runtime.denoise ? 1.f : 0.f),
      .reset = ((runtime.reset || !objects.previous_camera) ? 1.f : 0.f),
      .history_weight = HISTORY_WEIGHT,
      .texture_inverse_size = {1.f / static_cast<float>(objects.target_width), 1.f / static_cast<float>(objects.target_height)},
  };
  native->UpdateSubresource(objects.resolve_settings.Get(), 0, nullptr, &settings, 0, 0);

  ComPtr<ID3D11ComputeShader> game_shader;
  native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  std::array<ID3D11ShaderResourceView*, 4> game_views = {};
  native->CSGetShaderResources(0, 4, game_views.data());
  ID3D11UnorderedAccessView* game_uav = nullptr;
  native->CSGetUnorderedAccessViews(0, 1, &game_uav);
  std::array<ID3D11Buffer*, 3> game_constants = {};
  native->CSGetConstantBuffers(0, 3, game_constants.data());
  ID3D11SamplerState* game_sampler = nullptr;
  native->CSGetSamplers(0, 1, &game_sampler);

  const std::array<ID3D11ShaderResourceView*, 4> views = {objects.reflection_copy_srv.Get(), objects.hit_srv.Get(), depth_view,
                                                          objects.history_srvs[runtime.history_index ^ 1].Get()};
  const std::array<ID3D11Buffer*, 3> constants = {camera, objects.previous_camera.Get(), objects.resolve_settings.Get()};
  constexpr std::array<ID3D11ShaderResourceView*, 4> NO_VIEWS = {};
  native->CSSetShader(objects.resolve.Get(), nullptr, 0);
  native->CSSetShaderResources(0, 4, NO_VIEWS.data());
  native->CSSetUnorderedAccessViews(0, 1, objects.result_uav.GetAddressOf(), nullptr);
  native->CSSetShaderResources(0, 4, views.data());
  native->CSSetConstantBuffers(0, 3, constants.data());
  native->CSSetSamplers(0, 1, objects.linear_clamp.GetAddressOf());
  native->Dispatch((objects.target_width + 7) / 8, (objects.target_height + 7) / 8, 1);
  ID3D11UnorderedAccessView* no_uav = nullptr;
  native->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);

  native->CSSetShader(game_shader.Get(), nullptr, 0);
  native->CSSetShaderResources(0, 4, game_views.data());
  native->CSSetUnorderedAccessViews(0, 1, &game_uav, nullptr);
  native->CSSetConstantBuffers(0, 3, game_constants.data());
  native->CSSetSamplers(0, 1, &game_sampler);
  for (auto* view : game_views) {
    if (view != nullptr) view->Release();
  }
  if (game_uav != nullptr) game_uav->Release();
  for (auto* buffer : game_constants) {
    if (buffer != nullptr) buffer->Release();
  }
  if (game_sampler != nullptr) game_sampler->Release();

  native->CopyResource(target, objects.result.Get());
  native->CopyResource(objects.histories[runtime.history_index].Get(), objects.result.Get());
  frame_history::KeepCamera(device.Get(), native, camera, &objects.last_camera);
  runtime.history_valid = true;
}

inline ID3D11DeviceContext* ImmediateContext(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return nullptr;
  return native;
}

}  // namespace internal

// Before the game's reflection draw (the depth it reads at `depth_slot`): this frame's pyramid (once) at its t10; with it
// off, none (the game's march). With the resolve: the hit target (cleared once a frame) bound as the third target.
// `march` false: no pixel of the draw marches (ray-traced reflections on all of them), neither.
inline bool OnReflectionDraw(reshade::api::command_list* cmd_list, UINT depth_slot = internal::DEPTH_SLOT, bool march = true) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return true;
  ID3D11ShaderResourceView* pyramid = nullptr;
  if (runtime.enabled && march) {
    ComPtr<ID3D11ShaderResourceView> depth_view;
    native->PSGetShaderResources(depth_slot, 1, depth_view.GetAddressOf());
    D3D11_SHADER_RESOURCE_VIEW_DESC depth_desc = {};
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC texture_desc = {};
    if (depth_view) {
      depth_view->GetDesc(&depth_desc);
      depth_view->GetResource(resource.GetAddressOf());
    }
    if (resource && SUCCEEDED(resource.As(&texture))) texture->GetDesc(&texture_desc);
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    if (depth_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && texture_desc.Width != 0
        && internal::Prepare(device.Get(), texture_desc.Width, texture_desc.Height)) {
      if (runtime.built_frame != runtime.frame) {
        internal::Build(native, depth_view.Get());
        runtime.built_frame = runtime.frame;
      }
      pyramid = runtime.objects.pyramid.Get();
    }
  }
  native->PSSetShaderResources(internal::PYRAMID_SLOT, 1, &pyramid);
  runtime.bound = false;
  if (pyramid != nullptr && (runtime.denoise || runtime.rays != 0)) {
    std::array<ID3D11RenderTargetView*, 2> targets = {};
    ID3D11DepthStencilView* depth = nullptr;
    native->OMGetRenderTargets(2, targets.data(), &depth);
    runtime.game_targets[0].Attach(targets[0]);
    runtime.game_targets[1].Attach(targets[1]);
    runtime.game_depth.Attach(depth);
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc = {};
    if (targets[0] != nullptr) targets[0]->GetResource(resource.GetAddressOf());
    if (resource && SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    if (desc.Width != 0 && desc.SampleDesc.Count == 1 && internal::PrepareResolve(device.Get(), desc)) {
      if (runtime.cleared_frame != runtime.frame) {
        constexpr FLOAT NOT_DRAWN[4] = {0.f, -2.f, 0.f, 0.f};
        native->ClearRenderTargetView(runtime.objects.hit_rtv.Get(), NOT_DRAWN);
        runtime.cleared_frame = runtime.frame;
      }
      const std::array<ID3D11RenderTargetView*, 3> bound = {targets[0], targets[1], runtime.objects.hit_rtv.Get()};
      native->OMSetRenderTargets(3, bound.data(), depth);
      runtime.bound = true;
    } else {
      runtime.game_targets = {};
      runtime.game_depth.Reset();
    }
  }
  return true;
}

// After it: the pyramid unbound (it is written again next frame); with the hit target, the game's targets back and the
// resolve.
inline void OnReflectionDrawn(reshade::api::command_list* cmd_list) {
  auto* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  ID3D11ShaderResourceView* no_view = nullptr;
  native->PSSetShaderResources(internal::PYRAMID_SLOT, 1, &no_view);
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.bound) return;
  runtime.bound = false;
  const std::array<ID3D11RenderTargetView*, 2> targets = {runtime.game_targets[0].Get(), runtime.game_targets[1].Get()};
  native->OMSetRenderTargets(2, targets.data(), runtime.game_depth.Get());
  ComPtr<ID3D11ShaderResourceView> depth_view;
  native->PSGetShaderResources(internal::DEPTH_SLOT, 1, depth_view.GetAddressOf());
  ComPtr<ID3D11Buffer> camera;
  native->PSGetConstantBuffers(0, 1, camera.GetAddressOf());
  ComPtr<ID3D11Resource> target;
  runtime.game_targets[0]->GetResource(target.GetAddressOf());
  if (depth_view && camera) internal::Resolve(native, target.Get(), depth_view.Get(), camera.Get());
  runtime.game_targets = {};
  runtime.game_depth.Reset();
}

// Every present: the settings (`rays`: 0 every pixel, 1 half, 2 a quarter), a new frame.
inline void Update(bool enabled, bool denoise, int rays) {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.enabled = enabled;
  internal::runtime.denoise = denoise;
  internal::runtime.rays = rays;
  // Off: its memory back (made again at the next reflection draw).
  if (!enabled && internal::runtime.objects.device) {
    internal::runtime.objects = {};
  }
  ++internal::runtime.frame;
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

}  // namespace twattila::ssr_hiz
