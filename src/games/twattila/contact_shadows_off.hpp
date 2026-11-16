/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// One method for contact shadows (Pharaoh's and Troy's directionallight.hlsl): the sun PS's own march toward the sun
// (lighting_VS_PS's g_contact_shadows, an int at byte 4 in all their sun draws, its march gated by one if_nz; its result
// min'ed into the shadow) goes in a sun draw that ours shadow: Bend or the rays, whose mask screen_space_shadows' OnSunDraw
// (registered before this) bound at t49 for that draw. The draw reads a copy of its lighting constants with the flag 0, the
// game's bound again after it (finalize: the Light Bounce draws the sun draw itself and bypasses). Without our mask the game's
// buffer, flag and bytes stay. Install after the screen-space shadows' sun draw hooks, before the Light Bounce's (it draws
// the sun draw itself with the constants bound then). WARP on the game's bytes with the traces' constants:
// tools\tw\pharaoh\sun_test and tools\tw\troy\sun_test (v51).
// A game whose flag sits elsewhere (Warhammer III's sun mask: sss_screen_space_shadows_enabled, byte 28 of b3) or whose
// own condition decides (the rays over kept maps, or Bend) calls BindWithoutFlag / BindGameConstants around that draw
// itself, with Install({}) for the device's end.

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <utility>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./light_bounce.hpp"
#include "./screen_space_shadows.hpp"

namespace twattila::contact_shadows_off {

namespace internal {

constexpr UINT CONTACT_SHADOWS_OFFSET = 4;
struct ContactShadowsOff {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // the copy's (compared only)
  Microsoft::WRL::ComPtr<ID3D11Buffer> lighting;
  UINT lighting_bytes = 0;
  Microsoft::WRL::ComPtr<ID3D11Buffer> zeros;
  Microsoft::WRL::ComPtr<ID3D11Buffer> game_lighting;  // bound again after the draw
  UINT game_slot = 0;
  std::atomic_bool logged = false;
};
inline ContactShadowsOff contact_shadows_off;

}  // namespace internal

// The draw about to run reads a copy of its pixel shader's constant buffer at `slot` with the uint at `offset` 0 (the
// game's contact-march flag: Pharaoh's and Troy's g_contact_shadows at byte 4 of their lighting constants, Warhammer III's
// sss_screen_space_shadows_enabled at byte 28 of its sun mask's b3); the game's buffer is kept for BindGameConstants,
// which the caller runs after the draw. False: nothing changed (no buffer bound there, too small, or no copy).
inline bool BindWithoutFlag(ID3D11DeviceContext* native, UINT slot, UINT offset) {
  Microsoft::WRL::ComPtr<ID3D11Buffer> game_lighting;
  native->PSGetConstantBuffers(slot, 1, game_lighting.GetAddressOf());
  if (!game_lighting) return false;
  D3D11_BUFFER_DESC desc = {};
  game_lighting->GetDesc(&desc);
  if (desc.ByteWidth < offset + sizeof(uint32_t)) return false;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = internal::contact_shadows_off;
  const std::unique_lock lock(state.mutex);
  if (state.device != device.Get() || state.lighting_bytes != desc.ByteWidth) {
    state.lighting.Reset();
    state.lighting_bytes = 0;
    state.device = nullptr;
    constexpr std::array<uint32_t, 4> ZEROS = {};
    const D3D11_BUFFER_DESC copy = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC zeros = {.ByteWidth = sizeof(ZEROS), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA zeros_data = {.pSysMem = ZEROS.data()};
    if (FAILED(device->CreateBuffer(&copy, nullptr, state.lighting.GetAddressOf()))
        || FAILED(device->CreateBuffer(&zeros, &zeros_data, state.zeros.ReleaseAndGetAddressOf()))) {
      state.lighting.Reset();
      return false;
    }
    state.lighting_bytes = desc.ByteWidth;
    state.device = device.Get();
  }
  // A constant buffer takes no update of a part: the game's whole, then the flag from the buffer of zeros.
  native->CopyResource(state.lighting.Get(), game_lighting.Get());
  const D3D11_BOX flag = {.left = 0, .top = 0, .front = 0, .right = sizeof(uint32_t), .bottom = 1, .back = 1};
  native->CopySubresourceRegion(state.lighting.Get(), 0, offset, 0, 0, state.zeros.Get(), 0, &flag);
  native->PSSetConstantBuffers(slot, 1, state.lighting.GetAddressOf());
  state.game_lighting = std::move(game_lighting);
  state.game_slot = slot;
  return true;
}

// After the draw BindWithoutFlag prepared: the game's buffer at its slot again (`native` null: only let go of it).
inline void BindGameConstants(ID3D11DeviceContext* native) {
  auto& state = internal::contact_shadows_off;
  const std::unique_lock lock(state.mutex);
  if (native != nullptr) {
    native->PSSetConstantBuffers(state.game_slot, 1, state.game_lighting.GetAddressOf());
  }
  state.game_lighting.Reset();
}

namespace internal {

constexpr auto OnSunContactShadows = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> our_shadow;
  native->PSGetShaderResources(screen_space_shadows::internal::MASK_SLOT, 1, our_shadow.GetAddressOf());
  if (!our_shadow) return {};
  const auto& sun_draw = command.template GetCallbackData<T>();
  if (!BindWithoutFlag(native, sun_draw.lighting_slot, CONTACT_SHADOWS_OFFSET)) return {};
  if (!contact_shadows_off.logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i(std::format("[Total War] First run: the game's contact shadows off in sun draw {:08X} (ours shadow it).", sun_draw.shader_hash));
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    BindGameConstants(drawn_native);
  }};
};

// Only the copy's device (nullptr: the addon's end, Uninstall): another device's end between a sun draw and its finalize
// must leave the game's buffer to bind again.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = contact_shadows_off;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.lighting.Reset();
  state.lighting_bytes = 0;
  state.zeros.Reset();
  state.device = nullptr;
}

}  // namespace internal

// `sun_draws`: every sun draw whose lighting_VS_PS (at its lighting_slot) holds g_contact_shadows at byte 4; static storage
// (each draw's hook holds its entry). Empty: only the device's end, for a game calling BindWithoutFlag itself.
inline void Install(std::span<const light_bounce::SunDraw> sun_draws) {
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  for (const auto& sun_draw : sun_draws) {
    renodx::utils::command_action::Register(
        internal::OnSunContactShadows, {.shader_hash = sun_draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &sun_draw);
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunContactShadows);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  internal::OnDestroyDevice(nullptr);
}

}  // namespace twattila::contact_shadows_off
