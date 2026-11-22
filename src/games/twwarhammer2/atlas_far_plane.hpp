/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The rays replace the shadow maps' sun shadow through the atlas's view (Warhammer's sun builds, Warhammer II's software /
// hardware pcf and gather 4.1 ones): where the rays' shadow is bound at t49 a 1 x 1 texture of 1 (the far plane, a cleared
// map's value) at the atlas's slot makes every read lit, gather4_c and the game's comparison sampler, or sample_l / sample /
// gather4 and the shader's own test with our point CLAMP sampler at the same slot (the game's address mode unchecked: a
// border of 0 would shadow every read; the sampler reads the atlas alone in every sun build), so their patched shadows
// (screen_space_shadows.hpp Patch, each x the rays' visibility) give the rays' shadow alone. A build Patch left as it is
// gets no t49 and keeps the game's maps. The game's view and sampler are bound again after the draw (finalize: the Light
// Bounce draws the sun draw itself and bypasses). Install after the screen-space shadows' sun draw hooks (they bind t49),
// before the Light Bounce's. WARP: test/tw_warhammer_shaders part 29.

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
  ID3D11Device* device = nullptr;  // the stand-in's (compared only; kept after a failed one: not tried again)
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> far_plane;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> clamp;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> game_view;  // bound again after the draw
  Microsoft::WRL::ComPtr<ID3D11SamplerState> game_sampler;
  bool logged = false;
};
inline State state;
inline UINT atlas_slot = 1;  // Install's

// OWN_SAMPLER: ours at the atlas's sampler slot too (the builds that read it through a plain sampler).
template <bool OWN_SAMPLER>
constexpr auto ON_SUN_DRAW = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
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
    state.clamp.Reset();
    state.device = device.Get();
    const float far_plane = 1.f;
    const D3D11_TEXTURE2D_DESC desc = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = &far_plane, .SysMemPitch = 4};
    const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
                                             .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .ComparisonFunc = D3D11_COMPARISON_NEVER,
                                             .MaxLOD = D3D11_FLOAT32_MAX};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, state.far_plane.GetAddressOf()))
        || FAILED(device->CreateSamplerState(&sampler_desc, state.clamp.GetAddressOf()))) {
      state.far_plane.Reset();
      renodx::utils::log::w("[Total War] The rays replace the shadow maps: could not create the atlas's stand-in (the game's maps stay).");
    }
  }
  if (!state.far_plane) return {};
  native->PSGetShaderResources(atlas_slot, 1, state.game_view.ReleaseAndGetAddressOf());
  native->PSSetShaderResources(atlas_slot, 1, state.far_plane.GetAddressOf());
  if constexpr (OWN_SAMPLER) {
    native->PSGetSamplers(atlas_slot, 1, state.game_sampler.ReleaseAndGetAddressOf());
    native->PSSetSamplers(atlas_slot, 1, state.clamp.GetAddressOf());
  }
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: the rays replace the shadow maps: the sun draws' shadow atlas read as lit (the rays shadow).");
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    const std::unique_lock drawn_lock(state.mutex);
    if (drawn_native != nullptr) {
      drawn_native->PSSetShaderResources(atlas_slot, 1, state.game_view.GetAddressOf());
      if constexpr (OWN_SAMPLER) drawn_native->PSSetSamplers(atlas_slot, 1, state.game_sampler.GetAddressOf());
    }
    state.game_view.Reset();
    state.game_sampler.Reset();
  }};
};

// Only our stand-in's device (nullptr: the addon's end, Uninstall).
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.far_plane.Reset();
  state.clamp.Reset();
  state.device = nullptr;
}

}  // namespace internal

// The sun draws reading the atlas at t`slot` alone through s`slot`: `comparison_shaders` with the game's comparison sampler
// (gather4_c), `sampled_shaders` with ours (sample_l / sample / gather4).
inline void Install(std::span<const uint32_t> comparison_shaders, std::span<const uint32_t> sampled_shaders, UINT slot) {
  internal::atlas_slot = slot;
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  for (const uint32_t shader_hash : comparison_shaders) {
    renodx::utils::command_action::Register(
        internal::ON_SUN_DRAW<false>, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  for (const uint32_t shader_hash : sampled_shaders) {
    renodx::utils::command_action::Register(
        internal::ON_SUN_DRAW<true>, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::ON_SUN_DRAW<false>);
  renodx::utils::command_action::Unregister(internal::ON_SUN_DRAW<true>);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  internal::OnDestroyDevice(nullptr);
}

}  // namespace twwarhammer2::atlas_far_plane
