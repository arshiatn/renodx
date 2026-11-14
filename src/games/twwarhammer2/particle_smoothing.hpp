/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <shared_mutex>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../twattila/particle_history.hpp"

// Smooth Half-Size Particles (Experimental): the game draws its lit particles (one indirect draw of F32116B4) into a half-size
// layer of its own against a half-size depth, the layer cleared to (0, 0, 0, 1) (its alpha the share of the scene left), and
// brings it up with deferred_alpha_composition's cross-bilateral upsample 21DE8D05 (t0 the layer, t1 the render's depth, t2 the
// layer's, the camera at b0; once a frame in all 26 traces). At that draw the layer smoothed over frames
// (twattila/particle_history.hpp on light_bounce.hlsli's rows, the game's own camera's) is bound at t0 for the game's upsample,
// the game's layer put back after it. The layer's part is the draw's viewport halved: below 100% the view sits in the top-left,
// its particles too (960 x 540 of 1920 x 1080 at DLSS Performance in the traces); the rest of the smoothed layer is copied from
// the game's, which the upsample's bilinear reads at the view's edge.
namespace twwarhammer2::particle_smoothing {

using Microsoft::WRL::ComPtr;
namespace particle_history = twattila::particle_history;

static float row = 0.f;

namespace internal {

constexpr uint32_t COMPOSITE_PS = 0x21DE8D05;

struct State {
  std::shared_mutex mutex;
  particle_history::History history;
  uint64_t frame = 0;    // counted at each present
  bool restart = true;   // the history starts again at the next resolve
  ComPtr<ID3D11ShaderResourceView> game_layer;  // the draw's t0, put back after it
  bool applied_logged = false;
};
static State state;

constexpr auto OnComposite = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (row == 0.f) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  // The layer, the render's depth, the layer's depth.
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> views;
  std::array<D3D11_TEXTURE2D_DESC, 3> descs = {};
  std::array<ComPtr<ID3D11Texture2D>, 3> textures;
  for (UINT slot = 0; slot < views.size(); ++slot) {
    native->PSGetShaderResources(slot, 1, views[slot].GetAddressOf());
    if (!views[slot]) return {};
    ComPtr<ID3D11Resource> resource;
    views[slot]->GetResource(resource.GetAddressOf());
    if (FAILED(resource.As(&textures[slot]))) return {};
    textures[slot]->GetDesc(&descs[slot]);
  }
  ComPtr<ID3D11Buffer> camera;
  native->PSGetConstantBuffers(0, 1, camera.GetAddressOf());
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  native->RSGetViewports(&viewport_count, &viewport);
  // Not the traced layer (half the render's depth, its own depth at its size, the history's format to copy from): the game's
  // upsample.
  if (!camera || viewport_count == 0 || descs[1].Width != 2 * descs[0].Width || descs[2].Width != descs[0].Width
      || (descs[0].Format != DXGI_FORMAT_R16G16B16A16_FLOAT && descs[0].Format != DXGI_FORMAT_R16G16B16A16_TYPELESS)) {
    return {};
  }
  const D3D11_VIEWPORT region = {.TopLeftX = viewport.TopLeftX / 2.f,
                                 .TopLeftY = viewport.TopLeftY / 2.f,
                                 .Width = viewport.Width / 2.f,
                                 .Height = viewport.Height / 2.f,
                                 .MaxDepth = 1.f};
  const std::unique_lock lock(state.mutex);
  const auto smoothed = particle_history::Resolve(native, {.colour = views[0].Get(), .depth = views[2].Get(), .camera = camera.Get(), .viewport = region},
                                                  state.frame, state.restart, &state.history);
  state.restart = false;
  if (smoothed[0] == nullptr) return {};
  // Outside the region the game's layer as it is (the resolve writes only the region).
  ComPtr<ID3D11Resource> history_colour;
  smoothed[0]->GetResource(history_colour.GetAddressOf());
  const UINT left = static_cast<UINT>(region.TopLeftX);
  const UINT top = static_cast<UINT>(region.TopLeftY);
  const UINT right = std::min(descs[0].Width, static_cast<UINT>(std::ceil(region.TopLeftX + region.Width)));
  const UINT bottom = std::min(descs[0].Height, static_cast<UINT>(std::ceil(region.TopLeftY + region.Height)));
  const std::array<D3D11_BOX, 4> outside = {{
      {.left = 0, .top = 0, .front = 0, .right = descs[0].Width, .bottom = top, .back = 1},
      {.left = 0, .top = bottom, .front = 0, .right = descs[0].Width, .bottom = descs[0].Height, .back = 1},
      {.left = 0, .top = top, .front = 0, .right = left, .bottom = bottom, .back = 1},
      {.left = right, .top = top, .front = 0, .right = descs[0].Width, .bottom = bottom, .back = 1},
  }};
  for (const auto& box : outside) {
    if (box.right > box.left && box.bottom > box.top) {
      native->CopySubresourceRegion(history_colour.Get(), 0, box.left, box.top, 0, textures[0].Get(), 0, &box);
    }
  }
  state.game_layer = views[0];
  native->PSSetShaderResources(0, 1, &smoothed[0]);
  if (!state.applied_logged) {
    state.applied_logged = true;
    renodx::utils::log::i("[Warhammer II] Smooth Half-Size Particles: the first upsample smoothed (", region.Width, " x ", region.Height,
                          " of the half layer).");
  }
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&post_native);
    const std::unique_lock post_lock(state.mutex);
    post_native->PSSetShaderResources(0, 1, state.game_layer.GetAddressOf());
    state.game_layer.Reset();
  }};
};

}  // namespace internal

inline void Install() {
  renodx::utils::command_action::Register(
      internal::OnComposite, {.shader_hash = internal::COMPOSITE_PS, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnComposite);
}

// Every present: the frame counted; while the row is off the histories' memory back and the history started again.
inline void OnPresent() {
  const std::unique_lock lock(internal::state.mutex);
  ++internal::state.frame;
  if (row == 0.f) {
    if (internal::state.history.width != 0 || internal::state.history.failed_width != 0) {
      particle_history::ReleaseHistories(&internal::state.history);
    }
    internal::state.restart = true;
  }
}

inline void OnDestroyDevice(reshade::api::device*) {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.history = {};
  internal::state.game_layer.Reset();
  internal::state.restart = true;
}

}  // namespace twwarhammer2::particle_smoothing
