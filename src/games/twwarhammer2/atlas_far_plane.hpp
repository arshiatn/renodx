/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The rays replace the shadow maps' sun shadow through the atlas's view (Warhammer's sun builds, Warhammer II's software /
// hardware pcf ones): where the rays' shadow is bound at t49 a 1 x 1 texture of 1 (the far plane, a cleared map's value) at
// the atlas's slot makes every read lit, gather4_c and comparison sampler or sample_l / sample and the shader's own test
// alike (the sampler's address mode must not be a border of 0), so their patched shadows (screen_space_shadows.hpp Patch,
// each x the rays' visibility) give the rays' shadow alone. The game's view is bound again after the draw (finalize: the
// Light Bounce draws the sun draw itself and bypasses). Install after the screen-space shadows' sun draw hooks (they bind
// t49), before the Light Bounce's. WARP: test/tw_warhammer_shaders part 29.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <utility>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/screen_space_shadows.hpp"

namespace twwarhammer2::atlas_far_plane {

namespace internal {

struct State {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // the stand-in's (compared only)
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> far_plane;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> game_view;  // bound again after the draw
  bool logged = false;
};
inline State state;
inline UINT atlas_slot = 1;  // Install's

constexpr auto OnSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rays;
  native->PSGetShaderResources(twattila::screen_space_shadows::internal::MASK_SLOT, 1, rays.GetAddressOf());
  // With the rays' sun shadows on, t49 holds theirs alone (Bend's are off under them).
  if (!rays || !twattila::rt_scene::SunShadowsTraced()) return {};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  const std::unique_lock lock(state.mutex);
  if (state.device != device.Get()) {
    state.far_plane.Reset();
    state.device = nullptr;
    const float far_plane = 1.f;
    const D3D11_TEXTURE2D_DESC desc = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = &far_plane, .SysMemPitch = 4};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, state.far_plane.GetAddressOf()))) {
      return {};
    }
    state.device = device.Get();
  }
  native->PSGetShaderResources(atlas_slot, 1, state.game_view.ReleaseAndGetAddressOf());
  native->PSSetShaderResources(atlas_slot, 1, state.far_plane.GetAddressOf());
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: the rays replace the shadow maps: the sun draws' shadow atlas read as lit (the rays shadow).");
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    const std::unique_lock drawn_lock(state.mutex);
    if (drawn_native != nullptr) drawn_native->PSSetShaderResources(atlas_slot, 1, state.game_view.GetAddressOf());
    state.game_view.Reset();
  }};
};

// Only our stand-in's device (nullptr: the addon's end, Uninstall).
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.far_plane.Reset();
  state.device = nullptr;
}

}  // namespace internal

// `sun_shaders`: the sun draws reading the atlas at `slot` alone.
inline void Install(std::span<const uint32_t> sun_shaders, UINT slot) {
  internal::atlas_slot = slot;
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  for (const uint32_t shader_hash : sun_shaders) {
    renodx::utils::command_action::Register(
        internal::OnSunDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunDraw);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  internal::OnDestroyDevice(nullptr);
}

}  // namespace twwarhammer2::atlas_far_plane
