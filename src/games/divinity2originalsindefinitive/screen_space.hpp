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
#include <cstring>
#include <mutex>
#include <utility>

#include <embed/shaders.h>
#include <include/reshade.hpp>

#include "../twattila/feature_settings.hpp"
#include "./dlaa.hpp"

// The screen-space methods without rays (Graphics, Experimental, off until a game run shows them; divinity2.tsv's ssao_*
// row). GT-VBAO in the game's SSAO's place: PPSAO (B4AF6326, the perspective camera's, 4 draws) not drawn, its normals (t0,
// GBuffer1) and camera (b12, PerView) kept; then PPSAO_Reinterleave's draw (2CA00ABD) with ao_vbao in its pixel shader's
// place: its target the texture PPSAO_Blur smooths twice, as the game's AO, which PPApplyAO and ClusteredLightingWithSSAO
// read. PPSAO_Deinterleave stays (its slices unread then): it comes before any sign of the camera, and the orthographic
// camera's PPSAO (16BFFA78) reads them. Under Ray-Traced AO and Path-Traced Ambient the chain is theirs
// (ray_tracing.hpp: their hooks first): one method. Any view, the inventory's portrait too.
namespace divinity2::screen_space {

using Microsoft::WRL::ComPtr;

// GT-VBAO's rows (the graphics page's keys: ssao_mode, ssao_radius, ...).
struct AoSliders {
  float mode = 0.f;            // 1: GT-VBAO
  float radius = 1.f;          // metres
  float strength = 1.f;        // the visibility's exponent
  float thickness = 0.1f;      // metres
  float quality = 1.f;         // 8 x 2^this samples per side of a direction, at most (0-3)
  float directions = 1.f;      // 2^this directions (0-3)
  float auto_thickness = 0.f;  // 1: each occluder as thick as it is wide
};
static AoSliders ao;

namespace internal {

constexpr uint32_t SAO_SHADER = 0xB4AF6326;
constexpr uint32_t REINTERLEAVE_SHADER = 0x2CA00ABD;
constexpr UINT AO_SETTINGS_SLOT = 1;  // ao_vbao's settings
constexpr UINT AO_NORMALS_SLOT = 5;   // its GBuffer1
constexpr UINT PER_VIEW_SLOT = 12;    // PerView, as the game's

// ao_vbao.ps_5_0.hlsl's ao_settings.
struct AoConstants {
  float radius = 0.f;
  float strength = 0.f;
  float thickness = 0.f;
  float steps = 0.f;
  float slices = 0.f;
  float auto_thickness = 0.f;
  float frame = 0.f;
  float padding = 0.f;
};

struct State {
  std::mutex mutex;
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3D11PixelShader> vbao;
  ComPtr<ID3D11Buffer> ao_constants;
  AoConstants uploaded = {};
  uint64_t frame = 0;
  // PPSAO's normals and camera since the last reinterleave (null: the game's SSAO drew).
  ComPtr<ID3D11ShaderResourceView> normals;
  ComPtr<ID3D11Buffer> per_view;
  // What GT-VBAO's reinterleave changed, put back after its draw.
  bool replaced = false;
  ComPtr<ID3D11PixelShader> game_shader;
  ComPtr<ID3D11ShaderResourceView> game_view;
  std::array<ComPtr<ID3D11Buffer>, 2> game_constants;  // b1, b12
};
static State state;

// The objects on the game's `device` (made again when it changes); false: not available. state.mutex held.
inline bool MakeObjects(ID3D11Device* device) {
  if (state.device.Get() == device) return !state.failed;
  state.device = device;
  state.vbao.Reset();
  state.ao_constants.Reset();
  state.uploaded = {};
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = sizeof(AoConstants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  state.failed = FAILED(device->CreatePixelShader(__ao_vbao.data(), __ao_vbao.size(), nullptr, state.vbao.GetAddressOf()))
                 || FAILED(device->CreateBuffer(&constants_desc, nullptr, state.ao_constants.GetAddressOf()));
  return !state.failed;
}

}  // namespace internal

// PPSAO's draw (before it): not drawn under GT-VBAO (true: skipped), its normals and camera kept for the reinterleave.
inline bool SkipSao(reshade::api::command_list* cmd_list) {
  if (ao.mode < 1.f) return false;
  ID3D11DeviceContext* native = dlaa::internal::ImmediateContext(cmd_list);
  if (native == nullptr) return false;
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (!state.normals) {
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    if (!internal::MakeObjects(device.Get())) return false;
    native->PSGetShaderResources(0, 1, state.normals.GetAddressOf());
    native->PSGetConstantBuffers(internal::PER_VIEW_SLOT, 1, state.per_view.GetAddressOf());
    if (!state.normals || !state.per_view) {
      state.normals.Reset();
      state.per_view.Reset();
      return false;
    }
  }
  return true;
}

// PPSAO_Reinterleave's draw (before it), after PPSAO's skipped draws: ao_vbao in its pixel shader's place (the game's
// target, triangle, depth at t0 and sampler), PPSAO's normals at t5 and camera at b12, GT-VBAO's settings at b1.
inline void OnReinterleave(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = dlaa::internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (!state.normals) return;
  // The noise moves every frame only under a temporal upscaler (which settles it); else it stands still.
  const internal::AoConstants constants = {
      .radius = ao.radius,
      .strength = ao.strength,
      .thickness = ao.thickness,
      .steps = static_cast<float>(8u << static_cast<uint32_t>(std::clamp(ao.quality, 0.f, 3.f))),
      .slices = static_cast<float>(1u << static_cast<uint32_t>(std::clamp(ao.directions, 0.f, 3.f))),
      .auto_thickness = (ao.auto_thickness != 0.f) ? 1.f : 0.f,
      .frame = twattila::feature_settings::Upscaling(dlaa::sliders) ? static_cast<float>(state.frame % 64u) : 0.f,
  };
  if (std::memcmp(&constants, &state.uploaded, sizeof(constants)) != 0) {
    native->UpdateSubresource(state.ao_constants.Get(), 0, nullptr, &constants, 0, 0);
    state.uploaded = constants;
  }
  native->PSGetShader(state.game_shader.GetAddressOf(), nullptr, nullptr);
  native->PSGetShaderResources(internal::AO_NORMALS_SLOT, 1, state.game_view.GetAddressOf());
  native->PSGetConstantBuffers(internal::AO_SETTINGS_SLOT, 1, state.game_constants[0].GetAddressOf());
  native->PSGetConstantBuffers(internal::PER_VIEW_SLOT, 1, state.game_constants[1].GetAddressOf());
  native->PSSetShader(state.vbao.Get(), nullptr, 0);
  native->PSSetShaderResources(internal::AO_NORMALS_SLOT, 1, state.normals.GetAddressOf());
  native->PSSetConstantBuffers(internal::AO_SETTINGS_SLOT, 1, state.ao_constants.GetAddressOf());
  native->PSSetConstantBuffers(internal::PER_VIEW_SLOT, 1, state.per_view.GetAddressOf());
  state.normals.Reset();
  state.per_view.Reset();
  state.replaced = true;
}

// PPSAO_Reinterleave drawn: the game's bindings back after GT-VBAO's draw.
inline void OnReinterleaveDrawn(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = dlaa::internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (!std::exchange(state.replaced, false)) return;
  native->PSSetShader(state.game_shader.Get(), nullptr, 0);
  native->PSSetShaderResources(internal::AO_NORMALS_SLOT, 1, state.game_view.GetAddressOf());
  native->PSSetConstantBuffers(internal::AO_SETTINGS_SLOT, 1, state.game_constants[0].GetAddressOf());
  native->PSSetConstantBuffers(internal::PER_VIEW_SLOT, 1, state.game_constants[1].GetAddressOf());
  state.game_shader.Reset();
  state.game_view.Reset();
  state.game_constants = {};
}

// The frame's end: the noise's next frame; normals kept by a PPSAO no reinterleave followed let go.
inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*, uint32_t,
                      const reshade::api::rect*) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  ++state.frame;
  state.normals.Reset();
  state.per_view.Reset();
}

// The game's `device` is going away: the objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.device.Get() != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.device.Reset();
  state.failed = false;
  state.vbao.Reset();
  state.ao_constants.Reset();
  state.normals.Reset();
  state.per_view.Reset();
}

inline void Install() {
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
}

}  // namespace divinity2::screen_space
