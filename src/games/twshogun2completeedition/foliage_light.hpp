/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <mutex>
#include <span>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/light_bounce.hpp"

// Light Through Foliage (Experimental; the DLSS addon's SSSFoliageLight, Attila's Light Through Foliage): Shogun 2's
// screen-space shadows (sss00 / sss01 and Remastered CE's builds) march its depth buffer itself. With the row above 0
// their depth is a copy (sss_foliage_depth: R32F, made in the addon's own context state before their dispatch) whose
// foliage pixels are negative, and the pass runs as its light-caster version (sss_foliage_cascades / sss_foliage:
// Bend's BEND_SSS_LIGHT_CASTERS, that share as CasterLight; the game's shader put back after): the sun comes through
// the shadows grass and leaves cast. Off: nothing changes (the passes' own versions keep no light casters). The foliage bit: the G-buffer constants' (the sun light draws' third G-buffer texture, kept from
// the frame before: the same texture, this frame's G-buffer in it by the shadow pass).
namespace twshogun2::foliage_light {

using Microsoft::WRL::ComPtr;

namespace internal {

struct State {
  std::mutex mutex;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> shader;
  ComPtr<ID3D11ComputeShader> cascades_pass;  // sss00's light-caster version
  ComPtr<ID3D11ComputeShader> plain_pass;     // sss01's
  ComPtr<ID3D11ComputeShader> game_pass;      // the pass's own, put back after it
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  ComPtr<ID3D11UnorderedAccessView> target;
  UINT width = 0;
  UINT height = 0;
  ComPtr<ID3D11ShaderResourceView> constants;   // the sun draws' G-buffer constants
  ComPtr<ID3D11ShaderResourceView> game_depth;  // the shadow pass's own, put back after it
  UINT depth_slot = 0;
  bool failed = false;
  bool on = false;
};
static State state;

// The objects on the device let go (`state.mutex` held).
inline void ReleaseObjects() {
  state.device.Reset();
  state.private_state.Reset();
  state.shader.Reset();
  state.cascades_pass.Reset();
  state.plain_pass.Reset();
  state.game_pass.Reset();
  state.texture.Reset();
  state.view.Reset();
  state.target.Reset();
  state.width = 0;
  state.height = 0;
  state.constants.Reset();
  state.game_depth.Reset();
  state.failed = false;
}

// A sun light draw: its G-buffer constants (gbuffer_slot + 2) kept while the row is on.
constexpr auto OnSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& draw = command.template GetCallbackData<twattila::light_bounce::SunDraw>();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  const std::unique_lock lock(state.mutex);
  if (!state.on) return {};
  state.constants.Reset();
  native->PSGetShaderResources(draw.gbuffer_slot + 2, 1, state.constants.GetAddressOf());
  return {};
};

// The objects on the device for a depth of that size; false: none.
inline bool PrepareObjects(ID3D11DeviceContext* native, UINT width, UINT height) {
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (state.device != device) {
    ReleaseObjects();
    state.device = device;
    ComPtr<ID3D11Device1> device1;
    const auto feature_level = device->GetFeatureLevel();
    const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                                 ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                                 : 0;
    state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                               nullptr, state.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_foliage_depth.data(), __sss_foliage_depth.size(), nullptr,
                                                         state.shader.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_foliage_cascades.data(), __sss_foliage_cascades.size(), nullptr,
                                                         state.cascades_pass.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_foliage.data(), __sss_foliage.size(), nullptr, state.plain_pass.GetAddressOf()));
  }
  if (state.failed) return false;
  if (state.width == width && state.height == height && state.texture) return true;
  state.texture.Reset();
  state.view.Reset();
  state.target.Reset();
  state.width = 0;
  state.height = 0;
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
  if (FAILED(device->CreateTexture2D(&desc, nullptr, state.texture.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(state.texture.Get(), nullptr, state.view.GetAddressOf()))
      || FAILED(device->CreateUnorderedAccessView(state.texture.Get(), nullptr, state.target.GetAddressOf()))) {
    state.texture.Reset();
    return false;
  }
  state.width = width;
  state.height = height;
  return true;
}

}  // namespace internal

// The screen-space shadow pass (its depth at `depth_slot`: 3 with the cascades, 0 without): with the row on, the
// foliage's depth made and bound there, the pass's light-caster version bound.
inline void OnShadowPass(reshade::api::command_list* cmd_list, UINT depth_slot, float foliage_light) {
  auto& state = internal::state;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(state.mutex);
  state.on = foliage_light > 0.f;
  if (!state.on || native == nullptr || !state.constants) return;
  ComPtr<ID3D11ShaderResourceView> depth;
  native->CSGetShaderResources(depth_slot, 1, depth.GetAddressOf());
  ComPtr<ID3D11DeviceContext1> native1;
  if (!depth || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  ComPtr<ID3D11Resource> resource;
  depth->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC desc = {};
  if (FAILED(resource.As(&texture))) return;
  texture->GetDesc(&desc);
  if (!internal::PrepareObjects(native, desc.Width, desc.Height)) return;
  {
    const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
    ID3D11ShaderResourceView* sources[] = {depth.Get(), state.constants.Get()};
    native->CSSetShader(state.shader.Get(), nullptr, 0);
    native->CSSetShaderResources(0, 2, sources);
    native->CSSetUnorderedAccessViews(0, 1, state.target.GetAddressOf(), nullptr);
    native->Dispatch((desc.Width + 7) / 8, (desc.Height + 7) / 8, 1);
    native->ClearState();
  }
  state.game_pass.Reset();
  native->CSGetShader(state.game_pass.GetAddressOf(), nullptr, nullptr);
  native->CSSetShader((depth_slot != 0) ? state.cascades_pass.Get() : state.plain_pass.Get(), nullptr, 0);
  native->CSSetShaderResources(depth_slot, 1, state.view.GetAddressOf());
  state.game_depth = std::move(depth);
  state.depth_slot = depth_slot;
}

// After the shadow pass: its own depth and shader put back.
inline void OnShadowPassDone(reshade::api::command_list* cmd_list) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (!state.game_depth) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native != nullptr) {
    native->CSSetShaderResources(state.depth_slot, 1, state.game_depth.GetAddressOf());
    native->CSSetShader(state.game_pass.Get(), nullptr, 0);
  }
  state.game_depth.Reset();
  state.game_pass.Reset();
}

// Before the Light Bounce's callbacks on the same sun draws (its own draw skips the later ones).
inline void Install(std::span<const twattila::light_bounce::SunDraw> sun_draws) {
  for (const auto& draw : sun_draws) {
    renodx::utils::command_action::Register(
        internal::OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunDraw);
}

inline void OnDestroyDevice(reshade::api::device*) {
  const std::unique_lock lock(internal::state.mutex);
  internal::ReleaseObjects();
}

}  // namespace twshogun2::foliage_light
