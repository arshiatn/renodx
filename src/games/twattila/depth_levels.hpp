/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <shared_mutex>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"

// Ambient occlusion Depth Levels for the Visibility Bitmask's passes of every Total War game: the AO's depth at 1/2 to
// 1/32 of its size (depth_levels.cs_5_0.hlsl, one dispatch a level), bound by the caller at SLOT for its pass, where
// depth_levels.hlsli reads them: samples far from the pixel read a level whose texels are about 1/8 of their distance,
// so they stay in the texture cache (the cost of many directions and samples). D3D11.
namespace twattila::depth_levels {

using Microsoft::WRL::ComPtr;

constexpr UINT SLOT = 41;

namespace internal {

constexpr UINT LEVELS = 5;

// Made on the game's device, again when it changes (the levels again when the depth's size does).
struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3D11ComputeShader> shader;
  UINT depth_width = 0;
  UINT depth_height = 0;
  ComPtr<ID3D11Texture2D> texture;
  std::array<ComPtr<ID3D11UnorderedAccessView>, LEVELS> level_writes;
  std::array<ComPtr<ID3D11ShaderResourceView>, LEVELS> level_views;
  ComPtr<ID3D11ShaderResourceView> view;  // every level
};

struct Runtime {
  std::shared_mutex mutex;
  DeviceObjects objects;
  std::array<UINT, 2> used_size = {};  // SetUsedSize's
};

static Runtime runtime;

}  // namespace internal

// Only the top-left `size` of the depth holds anything (zero: all of it): a game that reads a bigger texture below 100% (Warhammer III).
inline void SetUsedSize(std::array<UINT, 2> size) {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.used_size = size;
}

// The levels of `depth` (the view the pass reads its depth from), built now on `context` (its compute shader, t0-t1 and
// u0 put back after): the view of every level, or null (none: the pass reads the depth only).
inline ComPtr<ID3D11ShaderResourceView> Build(ID3D11DeviceContext* context, ID3D11ShaderResourceView* depth) {
  ComPtr<ID3D11Resource> depth_resource;
  depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return nullptr;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());

  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& objects = runtime.objects;
  if (objects.device != device) {
    objects = {};
    objects.device = device;
    objects.failed = FAILED(device->CreateComputeShader(__depth_levels.data(), __depth_levels.size(), nullptr,
                                                        objects.shader.GetAddressOf()));
    if (objects.failed) {
      renodx::utils::log::w("[Total War] Ambient occlusion: could not create its depth levels' shader.");
    }
  }
  if (objects.failed) return nullptr;
  if (objects.depth_width != depth_desc.Width || objects.depth_height != depth_desc.Height) {
    objects.texture.Reset();
    objects.view.Reset();
    objects.level_writes = {};
    objects.level_views = {};
    objects.depth_width = depth_desc.Width;
    objects.depth_height = depth_desc.Height;
    const D3D11_TEXTURE2D_DESC levels_desc = {
        .Width = std::max(1u, (depth_desc.Width + 1) / 2),
        .Height = std::max(1u, (depth_desc.Height + 1) / 2),
        .MipLevels = internal::LEVELS,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R32G32_UINT,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
    };
    bool made = SUCCEEDED(device->CreateTexture2D(&levels_desc, nullptr, objects.texture.GetAddressOf()))
                && SUCCEEDED(device->CreateShaderResourceView(objects.texture.Get(), nullptr,
                                                              objects.view.GetAddressOf()));
    for (UINT level = 0; made && level < internal::LEVELS; ++level) {
      const D3D11_UNORDERED_ACCESS_VIEW_DESC write_desc = {
          .Format = levels_desc.Format,
          .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
          .Texture2D = {.MipSlice = level},
      };
      const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {
          .Format = levels_desc.Format,
          .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
          .Texture2D = {.MostDetailedMip = level, .MipLevels = 1},
      };
      made = SUCCEEDED(device->CreateUnorderedAccessView(objects.texture.Get(), &write_desc,
                                                         objects.level_writes[level].GetAddressOf()))
             && SUCCEEDED(device->CreateShaderResourceView(objects.texture.Get(), &view_desc,
                                                           objects.level_views[level].GetAddressOf()));
    }
    if (!made) {
      renodx::utils::log::w("[Total War] Ambient occlusion: could not create its depth levels (", depth_desc.Width,
                            " x ", depth_desc.Height, ").");
      objects.texture.Reset();
      objects.view.Reset();
    }
  }
  if (!objects.view) return nullptr;

  ComPtr<ID3D11ComputeShader> game_shader;
  context->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  std::array<ID3D11ShaderResourceView*, 2> game_views = {};
  context->CSGetShaderResources(0, static_cast<UINT>(game_views.size()), game_views.data());
  ComPtr<ID3D11UnorderedAccessView> game_write;
  context->CSGetUnorderedAccessViews(0, 1, game_write.GetAddressOf());

  context->CSSetShader(objects.shader.Get(), nullptr, 0);
  D3D11_TEXTURE2D_DESC levels_desc = {};
  objects.texture->GetDesc(&levels_desc);
  const std::array<UINT, 2> used_levels = {runtime.used_size[0] == 0 ? levels_desc.Width : std::min((runtime.used_size[0] + 1) / 2, levels_desc.Width),
                                           runtime.used_size[1] == 0 ? levels_desc.Height : std::min((runtime.used_size[1] + 1) / 2, levels_desc.Height)};
  for (UINT level = 0; level < internal::LEVELS; ++level) {
    ID3D11UnorderedAccessView* write[] = {objects.level_writes[level].Get()};
    context->CSSetUnorderedAccessViews(0, 1, write, nullptr);
    ID3D11ShaderResourceView* inputs[] = {depth, (level == 0) ? nullptr : objects.level_views[level - 1].Get()};
    context->CSSetShaderResources(0, 2, inputs);
    const UINT width = std::max(1u, used_levels[0] >> level);
    const UINT height = std::max(1u, used_levels[1] >> level);
    context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
  }

  context->CSSetShader(game_shader.Get(), nullptr, 0);
  context->CSSetUnorderedAccessViews(0, 1, game_write.GetAddressOf(), nullptr);
  context->CSSetShaderResources(0, static_cast<UINT>(game_views.size()), game_views.data());
  for (auto* view : game_views) {
    if (view != nullptr) view->Release();
  }
  return objects.view;
}

// The game's device is going away: the levels on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (runtime.objects.device.Get() != native) return;
  runtime.objects = {};
}

}  // namespace twattila::depth_levels
