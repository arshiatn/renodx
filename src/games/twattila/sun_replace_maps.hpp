/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The rays replace the per-frame shadow maps' sun shadow (always, one method: Pharaoh's, Troy's and Warhammer II's
// directionallight.hlsl, Rome II's): the sun draws reading the per-frame atlas through their atlas sampler (its only reads
// of that sampler) read it as lit where the rays' shadow is bound at t49, so their patched shadow (screen_space_shadows.hpp
// Patch, x the rays' visibility) is the rays' shadow alone. Compared by the sampler (sample_c / gather4_c): an ALWAYS
// sampler there. Compared by hand (Rome II's Shader Model 4.1 / 3.0 builds, gather4 / sample then `lt depth, map`): an
// empty map (FLT_MAX in every channel) at the atlas's view through a point sampler. Taken as the light itself (Rome II's
// hardware_pcf builds): a map of 1 there. The kept baked map stays the game's, and so do the atlas's other readers. The
// game's sampler (and view) are bound again after the draw (finalize: the Light Bounce draws the sun draw itself and
// bypasses). Install after the screen-space shadows' sun draw hooks (they bind t49), before the Light Bounce's. WARP on
// the game's bytes: tools\tw\pharaoh\sun_test, tools\tw\troy\sun_test and tools\tw\rome2\sun_test (P).

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cfloat>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./rt_scene.hpp"
#include "./screen_space_shadows.hpp"

namespace twattila::sun_replace_maps {

// How a sun draw reads the atlas through its sampler slot.
enum class AtlasRead : uint8_t {
  COMPARED,  // by the sampler (sample_c / gather4_c)
  DEPTH,     // by hand: `lt depth, map`
  LIGHT,     // the map's value taken as the light itself (hardware_pcf)
};

struct AtlasSunShader {
  uint32_t hash = 0u;
  UINT sampler_slot = 2;  // read for the atlas alone
  AtlasRead read = AtlasRead::COMPARED;
  UINT texture_slot = 0;  // the atlas's view (DEPTH / LIGHT)
};

namespace internal {

struct AtlasLit {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // the objects' (compared only)
  Microsoft::WRL::ComPtr<ID3D11SamplerState> always;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> point;            // DEPTH / LIGHT: no border can reach the 1x1 maps
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> empty_map;  // DEPTH's
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lit_map;    // LIGHT's
  Microsoft::WRL::ComPtr<ID3D11SamplerState> game_sampler;     // bound again after the draw
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> game_view;  // DEPTH / LIGHT: bound again after the draw
  bool logged = false;
  bool failure_logged = false;
};
inline AtlasLit atlas_lit;
inline const float* row = nullptr;  // the addon's row (0: off)
inline std::vector<AtlasSunShader> compared_shaders;  // the hash list Install's (each draw's hook holds its entry)

constexpr auto OnSunReplaceMaps = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
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
    state.point.Reset();
    state.empty_map.Reset();
    state.lit_map.Reset();
    state.device = nullptr;
    // Point: each comparison exactly 1.
    const D3D11_SAMPLER_DESC always_desc = {.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
                                            .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                            .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                            .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                            .ComparisonFunc = D3D11_COMPARISON_ALWAYS,
                                            .MaxLOD = D3D11_FLOAT32_MAX};
    const D3D11_SAMPLER_DESC point_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
                                           .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .ComparisonFunc = D3D11_COMPARISON_NEVER,
                                           .MaxLOD = D3D11_FLOAT32_MAX};
    // Every channel: the fetch4 builds compare all four of one sample.
    const auto create_map = [&](float value, ID3D11ShaderResourceView** view) {
      const std::array<float, 4> texel = {value, value, value, value};
      const D3D11_TEXTURE2D_DESC desc = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                         .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = texel.data(), .SysMemPitch = sizeof(texel)};
      Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
      return SUCCEEDED(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()))
             && SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, view));
    };
    if (FAILED(device->CreateSamplerState(&always_desc, state.always.GetAddressOf()))
        || FAILED(device->CreateSamplerState(&point_desc, state.point.GetAddressOf()))
        || !create_map(FLT_MAX, state.empty_map.GetAddressOf()) || !create_map(1.f, state.lit_map.GetAddressOf())) {
      if (!std::exchange(state.failure_logged, true)) {
        renodx::utils::log::e("[Total War] Rays Replace Shadow Maps: its samplers or maps not created; the sun draws keep the game's shadow maps.");
      }
      state.always.Reset();
      state.point.Reset();
      state.empty_map.Reset();
      state.lit_map.Reset();
      return {};
    }
    state.device = device.Get();
  }
  const auto& shader = command.template GetCallbackData<T>();
  native->PSGetSamplers(shader.sampler_slot, 1, state.game_sampler.ReleaseAndGetAddressOf());
  if (shader.read == AtlasRead::COMPARED) {
    native->PSSetSamplers(shader.sampler_slot, 1, state.always.GetAddressOf());
  } else {
    native->PSGetShaderResources(shader.texture_slot, 1, state.game_view.ReleaseAndGetAddressOf());
    const auto& map = (shader.read == AtlasRead::DEPTH ? state.empty_map : state.lit_map);
    native->PSSetShaderResources(shader.texture_slot, 1, map.GetAddressOf());
    native->PSSetSamplers(shader.sampler_slot, 1, state.point.GetAddressOf());
  }
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: Rays Replace Shadow Maps: the sun draws' per-frame shadow maps read as lit (the rays shadow).");
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    const auto& drawn_shader = drawn.template GetCallbackData<T>();
    auto& drawn_state = atlas_lit;
    const std::unique_lock drawn_lock(drawn_state.mutex);
    if (drawn_native != nullptr) {
      drawn_native->PSSetSamplers(drawn_shader.sampler_slot, 1, drawn_state.game_sampler.GetAddressOf());
      if (drawn_shader.read != AtlasRead::COMPARED) {
        drawn_native->PSSetShaderResources(drawn_shader.texture_slot, 1, drawn_state.game_view.GetAddressOf());
      }
    }
    drawn_state.game_sampler.Reset();
    drawn_state.game_view.Reset();
  }};
};

// Only our objects' device (nullptr: the addon's end, Uninstall).
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = atlas_lit;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.always.Reset();
  state.point.Reset();
  state.empty_map.Reset();
  state.lit_map.Reset();
  state.device = nullptr;
}

}  // namespace internal

// `atlas_sun_shaders`: static storage (each draw's hook holds its entry); `replace_row`: the addon's row (0: off).
inline void Install(std::span<const AtlasSunShader> atlas_sun_shaders, const float* replace_row) {
  internal::row = replace_row;
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  for (const auto& shader : atlas_sun_shaders) {
    renodx::utils::command_action::Register(
        internal::OnSunReplaceMaps, {.shader_hash = shader.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &shader);
  }
}

// The sun draws comparing the atlas by the sampler at `sampler_slot`.
inline void Install(std::span<const uint32_t> atlas_sun_shaders, const float* replace_row, UINT sampler_slot = 2) {
  internal::compared_shaders.clear();
  for (const uint32_t shader_hash : atlas_sun_shaders) {
    internal::compared_shaders.push_back({.hash = shader_hash, .sampler_slot = sampler_slot});
  }
  Install(internal::compared_shaders, replace_row);
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunReplaceMaps);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  internal::OnDestroyDevice(nullptr);
}

}  // namespace twattila::sun_replace_maps
