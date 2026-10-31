/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <span>
#include <type_traits>
#include <utility>

#include "../../utils/command_action.hpp"

// What the addon's passes (light_bounce.hpp, ao_pass.hpp) keep from frame to frame: textures they draw into and read
// back, and a copy of the camera constants for the next frame's reprojection; and the game's draw issued again.
namespace twattila::frame_history {

using Microsoft::WRL::ComPtr;

// A depth buffer's format: its typeless group (a copy's format) and the format that reads its depth; unknown: neither.
inline std::pair<DXGI_FORMAT, DXGI_FORMAT> DepthFormats(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
      return {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS};
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
      return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT};
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      return {DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS};
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
      return {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM};
    default:
      return {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN};
  }
}

// A texture the passes draw into and then read.
struct Target {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D11ShaderResourceView> srv;
};

inline bool MakeTarget(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, Target* target) {
  *target = {};
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = width,
      .Height = height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = format,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
  };
  return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, target->texture.GetAddressOf()))
         && SUCCEEDED(device->CreateRenderTargetView(target->texture.Get(), nullptr, target->rtv.GetAddressOf()))
         && SUCCEEDED(device->CreateShaderResourceView(target->texture.Get(), nullptr, target->srv.GetAddressOf()));
}

// The 3D image's size when DLSS draws it below the display's (anti_aliasing.hpp sets it every present; 0: not scaled):
// the passes that work on the game's screen-size textures keep to their top-left corner of that size.
inline std::atomic<uint32_t> render_width = 0;
inline std::atomic<uint32_t> render_height = 0;

// Effects at Output Resolution (anti_aliasing.hpp sets it every present): DLSS's render share while it draws below 100%
// with the option on, else 1. The effects' own resolutions follow it (their divisors x it), so they keep the detail
// they have at 100%.
inline std::atomic<float> effect_share = 1.f;
// Frames left of the campaign map: its border, shroud and sea draws set it (Attila's addon), each present takes one.
inline std::atomic<uint32_t> campaign_frames = 0;

// The steps of 2 between full and `effect_share` (nearest; 0 at 1): what a divisor of 2^n drops by; `dimensions` 2 for
// counts per area (the reflections' rays: Half, Quarter).
inline float EffectSteps(float dimensions = 1.f) {
  return std::round(dimensions * std::log2(1.f / std::clamp(effect_share.load(std::memory_order_relaxed), 0.01f, 1.f)));
}

// The render size for a display of `width` x `height` at `scale`: a whole number of the display's aspect units (width
// and height over their gcd: 16 x 9 at 3840 x 2160), so the render aspect is exactly the display's. The games' motion
// vectors take their aspect from the viewport: with each length rounded (custom 99%: 3802 x 2138) static pixels got up
// to 0.6 px of motion toward the screen edges, a flicker (ATN, Shogun 2 v15). Displays without fine units (gcd below
// 40) round each length.
inline std::array<uint32_t, 2> AspectSize(uint32_t width, uint32_t height, float scale) {
  const uint32_t units = std::gcd(width, height);
  if (units < 40) {
    return {std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(width) * scale))),
            std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(height) * scale)))};
  }
  const uint32_t count = std::clamp(static_cast<uint32_t>(std::lround(static_cast<float>(units) * scale)), 1u, units);
  return {width / units * count, height / units * count};
}

// The main view's tone mapper drew this frame (anti_aliasing.hpp; false at each present): later draws are other views'
// (the battle portrait's 3D, the campaign's small views).
inline std::atomic_bool tonemapped = false;

// A sun light draw is the main view's: before its tone mapper, at the render size with DLSS (the display's at 100%).
// Without the anti-aliasing module running, every draw is.
inline bool MainViewport(ID3D11DeviceContext* context) {
  if (tonemapped.load(std::memory_order_relaxed)) return false;
  const uint32_t width = render_width.load(std::memory_order_relaxed);
  if (width == 0) return true;
  D3D11_VIEWPORT viewport = {};
  UINT count = 1;
  context->RSGetViewports(&count, &viewport);
  return count != 0 && viewport.Width == static_cast<float>(width)
         && viewport.Height == static_cast<float>(render_height.load(std::memory_order_relaxed));
}

// This frame's 3D size for a display of `width` x `height`.
inline std::array<UINT, 2> RenderSize(UINT width, UINT height) {
  const uint32_t scaled_width = render_width.load(std::memory_order_relaxed);
  const uint32_t scaled_height = render_height.load(std::memory_order_relaxed);
  if (scaled_width == 0 || scaled_width > width || scaled_height == 0 || scaled_height > height) return {width, height};
  return {scaled_width, scaled_height};
}

// A run of bytes of a game's camera constants at their place in Attila's camera_VS_PS (c0-c29), for the games whose
// layout differs (Warhammer III).
struct CameraRun {
  UINT destination = 0;
  UINT source = 0;
  UINT bytes = 0;
};

// The camera constants in Attila's layout for the addon's shaders: `camera` itself without `runs`, else its runs copied
// into `*remapped` (made once).
inline ID3D11Buffer* AttilaCamera(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* camera,
                                  std::span<const CameraRun> runs, ComPtr<ID3D11Buffer>* remapped) {
  if (runs.empty() || camera == nullptr) return camera;
  if (!*remapped) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = 30 * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(device->CreateBuffer(&desc, nullptr, remapped->GetAddressOf()))) return camera;
  }
  for (const CameraRun& run : runs) {
    const D3D11_BOX box = {.left = run.source, .top = 0, .front = 0, .right = run.source + run.bytes, .bottom = 1, .back = 1};
    context->CopySubresourceRegion(remapped->Get(), 0, run.destination, 0, 0, camera, 0, &box);
  }
  return remapped->Get();
}

// `camera` (the game's camera constants) copied into `*previous`, made again when its size changes: the camera of
// this frame for the next one.
inline void KeepCamera(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* camera,
                       ComPtr<ID3D11Buffer>* previous) {
  D3D11_BUFFER_DESC camera_desc = {};
  camera->GetDesc(&camera_desc);
  D3D11_BUFFER_DESC previous_desc = {};
  if (*previous) (*previous)->GetDesc(&previous_desc);
  if (previous_desc.ByteWidth != camera_desc.ByteWidth) {
    const D3D11_BUFFER_DESC copy_desc = {
        .ByteWidth = camera_desc.ByteWidth,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    previous->Reset();
    device->CreateBuffer(&copy_desc, nullptr, previous->GetAddressOf());
  }
  if (*previous) context->CopyResource(previous->Get(), camera);
}

// Draws again what the game's command drew, with what is bound now.
template <typename Arguments>
void DrawAgain(ID3D11DeviceContext* context, const Arguments& arguments) {
  if constexpr (std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>) {
    context->DrawInstanced(arguments.vertex_count, arguments.instance_count, arguments.first_vertex, arguments.first_instance);
  } else {
    context->DrawIndexedInstanced(arguments.index_count, arguments.instance_count, arguments.first_index,
                                  arguments.vertex_offset, arguments.first_instance);
  }
}

}  // namespace twattila::frame_history
