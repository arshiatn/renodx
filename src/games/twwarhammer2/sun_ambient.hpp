/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <climits>
#include <mutex>
#include <shared_mutex>
#include <span>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../twattila/dlaa_bridge.hpp"
#include "../twattila/frame_history.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/rt_scene.hpp"

// The AO the sun draws read, for the SSR resolve (Warhammer's and Warhammer II's environment_specular_light cs50_resolve):
// the sun draws put the cubes' light into the scene with that AO (x lerp(1, AO, 1 - gloss)), and the resolve takes the
// scene's share off again, so both must see one AO. With the rays the sun draws read the add-on's in place of the game's
// SSAO (the rays' combine, the AO filter's): t_ssao at each row's ao_slot, after the hooks that bind theirs (Install after
// twattila::ao_filter's, before the Light Bounce's), and its sampler, kept at the main view's sun draws while the rays run
// and let go at present, bound at the resolve's dispatch by BindCompute.
namespace twwarhammer2::sun_ambient {

namespace internal {

struct State {
  std::shared_mutex mutex;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
};
inline State state;

constexpr auto OnSunDraw = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!twattila::dlaa_bridge::bridge.ray_tracing) return {};
  auto* native = twattila::rt_scene::internal::NativeContext(command.cmd_list);
  if (native == nullptr || !twattila::frame_history::MainViewport(native)) return {};
  const auto& draw = command.template GetCallbackData<twattila::light_bounce::SunDraw>();
  const std::unique_lock lock(state.mutex);
  native->PSGetShaderResources(draw.ao_slot, 1, state.view.ReleaseAndGetAddressOf());
  native->PSGetSamplers(draw.ao_slot, 1, state.sampler.ReleaseAndGetAddressOf());
  return {};
};

// The game's device going away: what was kept from it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const std::unique_lock lock(state.mutex);
  state.view.Reset();
  state.sampler.Reset();
}

}  // namespace internal

// The sun draws with an ao_slot (kept for as long as it is installed).
inline void Install(std::span<const twattila::light_bounce::SunDraw> draws) {
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  for (const auto& draw : draws) {
    if (draw.ao_slot == UINT_MAX) continue;
    renodx::utils::command_action::Register(
        internal::OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &draw);
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunDraw);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  const std::unique_lock lock(internal::state.mutex);
  internal::state.view.Reset();
  internal::state.sampler.Reset();
}

// Every present: the frame's sun draws keep theirs again.
inline void OnPresent() {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.view.Reset();
  internal::state.sampler.Reset();
}

// The sun draws' AO at the compute stage's `slot` (and their sampler at `sampler_slot`; UINT_MAX: none). False: none kept
// this frame, nothing bound.
inline bool BindCompute(ID3D11DeviceContext* native, UINT slot, UINT sampler_slot = UINT_MAX) {
  const std::shared_lock lock(internal::state.mutex);
  if (!internal::state.view) return false;
  native->CSSetShaderResources(slot, 1, internal::state.view.GetAddressOf());
  if (sampler_slot != UINT_MAX) {
    native->CSSetSamplers(sampler_slot, 1, internal::state.sampler.GetAddressOf());
  }
  return true;
}

}  // namespace twwarhammer2::sun_ambient
