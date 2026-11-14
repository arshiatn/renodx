/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Rays Replace Shadow Maps (Pharaoh's and Troy's directionallight.hlsl, Experimental, off; Warhammer II's through s1): the sun
// draws reading the per-frame atlas (g_tShadowBuffer_SM50 t3 through sShadowBuffer_SM50 s2, their only reads of s2) take every comparison
// there as lit (an ALWAYS sampler) where the rays' shadow is bound at t49, so their patched comparisons
// (screen_space_shadows.hpp Patch, each x the rays' visibility) give the rays' shadow alone. The kept baked map (t1 by s1)
// stays the game's, and so do the atlas's other readers. The game's sampler is bound again after the draw (finalize: the
// Light Bounce draws the sun draw itself and bypasses). Install after the screen-space shadows' sun draw hooks (they bind
// t49), before the Light Bounce's. WARP on the game's bytes: tools\tw\pharaoh\sun_test and tools\tw\troy\sun_test (P).

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
#include "./rt_scene.hpp"
#include "./screen_space_shadows.hpp"

namespace twattila::sun_replace_maps {

namespace internal {

struct AtlasLit {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // the sampler's (compared only)
  Microsoft::WRL::ComPtr<ID3D11SamplerState> always;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> game_sampler;  // bound again after the draw
  bool logged = false;
};
inline AtlasLit atlas_lit;
inline const float* row = nullptr;  // the addon's Rays Replace Shadow Maps row (0: off)
inline UINT atlas_sampler_slot = 2;  // Install's

constexpr auto OnSunReplaceMaps = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (row == nullptr || *row == 0.f || command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rays;
  native->PSGetShaderResources(screen_space_shadows::internal::MASK_SLOT, 1, rays.GetAddressOf());
  // With the rays' sun shadows on, t49 holds theirs alone (no screen-space mask beside them).
  if (!rays || !rt_scene::SunShadowsTraced()) return {};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = atlas_lit;
  const std::unique_lock lock(state.mutex);
  if (state.device != device.Get()) {
    state.always.Reset();
    state.device = nullptr;
    // Point: each comparison exactly 1.
    const D3D11_SAMPLER_DESC desc = {.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
                                     .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     .ComparisonFunc = D3D11_COMPARISON_ALWAYS,
                                     .MaxLOD = D3D11_FLOAT32_MAX};
    if (FAILED(device->CreateSamplerState(&desc, state.always.GetAddressOf()))) return {};
    state.device = device.Get();
  }
  native->PSGetSamplers(atlas_sampler_slot, 1, state.game_sampler.ReleaseAndGetAddressOf());
  native->PSSetSamplers(atlas_sampler_slot, 1, state.always.GetAddressOf());
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: Rays Replace Shadow Maps: the sun draws' per-frame shadow maps read as lit (the rays shadow).");
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    auto& drawn_state = atlas_lit;
    const std::unique_lock drawn_lock(drawn_state.mutex);
    if (drawn_native != nullptr) {
      drawn_native->PSSetSamplers(atlas_sampler_slot, 1, drawn_state.game_sampler.GetAddressOf());
    }
    drawn_state.game_sampler.Reset();
  }};
};

// Only our sampler's device (nullptr: the addon's end, Uninstall).
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = atlas_lit;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.always.Reset();
  state.device = nullptr;
}

}  // namespace internal

// `atlas_sun_shaders`: the sun draws reading the per-frame atlas through `sampler_slot` alone; `replace_row`: the addon's row
// (0: off).
inline void Install(std::span<const uint32_t> atlas_sun_shaders, const float* replace_row, UINT sampler_slot = 2) {
  internal::row = replace_row;
  internal::atlas_sampler_slot = sampler_slot;
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  for (const uint32_t shader_hash : atlas_sun_shaders) {
    renodx::utils::command_action::Register(
        internal::OnSunReplaceMaps, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunReplaceMaps);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  internal::OnDestroyDevice(nullptr);
}

}  // namespace twattila::sun_replace_maps
