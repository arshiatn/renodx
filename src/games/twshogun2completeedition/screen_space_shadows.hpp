/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <mutex>
#include <span>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/light_bounce.hpp"
#include "./shared.h"

// Attila's SSS Softness and Debug View (Experimental; the DLSS addon's ScreenSpaceShadowsSoftness / DebugView) for Shogun 2's
// own screen-space shadows (sss00 / sss01, Bend's, and Remastered CE's builds), whose pass writes the sun's mask with the
// cascades in it. With either on, the pass gets this module's texture at u1: shaders/sss.hlsli keeps the cascades there and
// gives Bend 1 for them, so the mask holds Bend's shadows alone. At the first sun light draw after it, sss_soft_mask puts the
// darker of the cascades and Bend's shadows (softened by Softness, on the same surface) back into the mask, from a copy of it
// that the tone mappers show with the Debug View (shaders/tonemap.hlsli). Softer Away from the Caster (ScreenSpaceShadows-
// DistanceSoftness) also swaps in the pass's extended version (sss_distance*: Bend's FindHit), which writes each shadowed
// pixel's penumbra there for sss_soft_mask. All off: nothing bound, the game's pass.
namespace twshogun2::screen_space_shadows {

using Microsoft::WRL::ComPtr;

namespace internal {

struct State {
  std::mutex mutex;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> shader;
  ComPtr<ID3D11ComputeShader> distance_cascades;  // sss00's extended version
  ComPtr<ID3D11ComputeShader> distance_plain;     // sss01's
  ComPtr<ID3D11ComputeShader> game_pass;          // the pass's own (or Light Through Foliage's), put back after it
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11Texture2D> split;  // R8G8B8A8: the cascades (r), the Screen Edge share (b)
  ComPtr<ID3D11UnorderedAccessView> split_target;
  ComPtr<ID3D11ShaderResourceView> split_view;
  ComPtr<ID3D11Texture2D> bend;  // the mask's copy: Bend's shadows alone
  ComPtr<ID3D11ShaderResourceView> bend_view;
  D3D11_TEXTURE2D_DESC mask_desc = {};
  ComPtr<ID3D11UnorderedAccessView> mask;   // the pass's u0
  ComPtr<ID3D11ShaderResourceView> depth;   // the pass's depth (Bend's)
  float softness = 0.f;
  bool distance = false;
  bool split_bound = false;   // during the pass
  bool written = false;       // a pass wrote the split since the last sun draw
  bool debug_bound = false;   // at a tone mapper
  bool debug_ready = false;   // this frame's copy made
  bool failed = false;
};
inline State state;

inline void ReleaseObjects() {
  state.device.Reset();
  state.private_state.Reset();
  state.shader.Reset();
  state.distance_cascades.Reset();
  state.distance_plain.Reset();
  state.game_pass.Reset();
  state.constants.Reset();
  state.split.Reset();
  state.split_target.Reset();
  state.split_view.Reset();
  state.bend.Reset();
  state.bend_view.Reset();
  state.mask_desc = {};
  state.mask.Reset();
  state.depth.Reset();
  state.written = false;
  state.debug_ready = false;
  state.failed = false;
}

// The objects on the device, the textures at the mask's size and format (`format`: its UAV's); false: none.
inline bool PrepareObjects(ID3D11DeviceContext* native, const D3D11_TEXTURE2D_DESC& mask_desc, DXGI_FORMAT format) {
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
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                               nullptr, state.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_soft_mask.data(), __sss_soft_mask.size(), nullptr, state.shader.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_distance_cascades.data(), __sss_distance_cascades.size(), nullptr,
                                                         state.distance_cascades.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_distance.data(), __sss_distance.size(), nullptr, state.distance_plain.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&constants_desc, nullptr, state.constants.GetAddressOf()));
  }
  if (state.failed) return false;
  if (state.split && state.mask_desc.Width == mask_desc.Width && state.mask_desc.Height == mask_desc.Height
      && state.mask_desc.Format == mask_desc.Format) {
    return true;
  }
  state.split.Reset();
  state.split_target.Reset();
  state.split_view.Reset();
  state.bend.Reset();
  state.bend_view.Reset();
  state.mask_desc = {};
  const D3D11_TEXTURE2D_DESC split_desc = {.Width = mask_desc.Width, .Height = mask_desc.Height, .MipLevels = 1, .ArraySize = 1,
                                           .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
  const D3D11_TEXTURE2D_DESC bend_desc = {.Width = mask_desc.Width, .Height = mask_desc.Height, .MipLevels = 1, .ArraySize = 1,
                                          .Format = mask_desc.Format, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                          .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SHADER_RESOURCE_VIEW_DESC bend_view_desc = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                          .Texture2D = {.MipLevels = 1}};
  if (FAILED(state.device->CreateTexture2D(&split_desc, nullptr, state.split.GetAddressOf()))
      || FAILED(state.device->CreateUnorderedAccessView(state.split.Get(), nullptr, state.split_target.GetAddressOf()))
      || FAILED(state.device->CreateShaderResourceView(state.split.Get(), nullptr, state.split_view.GetAddressOf()))
      || FAILED(state.device->CreateTexture2D(&bend_desc, nullptr, state.bend.GetAddressOf()))
      || FAILED(state.device->CreateShaderResourceView(state.bend.Get(), &bend_view_desc, state.bend_view.GetAddressOf()))) {
    state.split.Reset();
    return false;
  }
  state.mask_desc = mask_desc;
  return true;
}

// The first sun light draw after the pass: the mask copied, then the cascades and Bend's (softened) shadows put back into it.
constexpr auto OnMaskSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(state.mutex);
  if (!state.written || native == nullptr) return {};
  state.written = false;
  ComPtr<ID3D11DeviceContext1> native1;
  ComPtr<ID3D11Resource> mask_resource;
  state.mask->GetResource(mask_resource.GetAddressOf());
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  native->RSGetViewports(&viewport_count, &viewport);
  if (viewport_count == 0 || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return {};
  const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
  native->CopyResource(state.bend.Get(), mask_resource.Get());
  native->UpdateSubresource(state.constants.Get(), 0, nullptr, std::array{viewport.Width, viewport.Height, state.softness,
                                                                         state.distance ? SSS_MOST_PENUMBRA_2160 * viewport.Height / 2160.f : 0.f}.data(), 0, 0);
  ID3D11ShaderResourceView* sources[] = {state.depth.Get(), state.bend_view.Get(), state.split_view.Get()};
  native->CSSetShader(state.shader.Get(), nullptr, 0);
  native->CSSetConstantBuffers(0, 1, state.constants.GetAddressOf());
  native->CSSetShaderResources(0, 3, sources);
  native->CSSetUnorderedAccessViews(0, 1, state.mask.GetAddressOf(), nullptr);
  native->Dispatch((static_cast<UINT>(viewport.Width) + 7) / 8, (static_cast<UINT>(viewport.Height) + 7) / 8, 1);
  native->ClearState();
  state.debug_ready = true;
  return {};
};

}  // namespace internal

// The screen-space shadow pass (Bend's depth at `depth_slot`: 3 with the cascades, 0 without; after foliage_light's): with
// Softness (render pixels), the Debug View or Softer Away from the Caster (the sun's degrees) on, the split texture bound at
// u1; with the last, the pass's extended version too.
inline void OnShadowPass(reshade::api::command_list* cmd_list, UINT depth_slot, float softness, float debug_view, float distance) {
  auto& state = internal::state;
  if (softness <= 0.f && debug_view <= 0.f && distance <= 0.f) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  const std::unique_lock lock(state.mutex);
  ComPtr<ID3D11UnorderedAccessView> mask;
  native->CSGetUnorderedAccessViews(0, 1, mask.GetAddressOf());
  ComPtr<ID3D11ShaderResourceView> depth;
  native->CSGetShaderResources(depth_slot, 1, depth.GetAddressOf());
  if (!mask || !depth) return;
  ComPtr<ID3D11Resource> resource;
  mask->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC desc = {};
  D3D11_UNORDERED_ACCESS_VIEW_DESC mask_view_desc = {};
  mask->GetDesc(&mask_view_desc);
  if (FAILED(resource.As(&texture))) return;
  texture->GetDesc(&desc);
  if (desc.SampleDesc.Count != 1 || desc.MipLevels != 1 || desc.ArraySize != 1 || !internal::PrepareObjects(native, desc, mask_view_desc.Format)) {
    return;
  }
  native->CSSetUnorderedAccessViews(1, 1, state.split_target.GetAddressOf(), nullptr);
  state.mask = std::move(mask);
  state.depth = std::move(depth);
  state.softness = softness;
  state.distance = distance > 0.f;
  state.split_bound = true;
  state.game_pass.Reset();
  if (state.distance) {
    native->CSGetShader(state.game_pass.GetAddressOf(), nullptr, nullptr);
    native->CSSetShader((depth_slot != 0) ? state.distance_cascades.Get() : state.distance_plain.Get(), nullptr, 0);
  }
}

// After the pass (before foliage_light's): the split texture unbound, written; the pass's shader put back.
inline void OnShadowPassDone(reshade::api::command_list* cmd_list) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (!state.split_bound) return;
  state.split_bound = false;
  state.written = true;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  ID3D11UnorderedAccessView* none = nullptr;
  native->CSSetUnorderedAccessViews(1, 1, &none, nullptr);
  if (state.distance) native->CSSetShader(state.game_pass.Get(), nullptr, 0);
  state.game_pass.Reset();
}

// A tone mapper's draw: with the Debug View on, this frame's copy and split bound at t60 / t61; unbound after it.
inline void OnTonemap(reshade::api::command_list* cmd_list, float debug_view) {
  auto& state = internal::state;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(state.mutex);
  if (debug_view <= 0.f || !state.debug_ready || native == nullptr) return;
  ID3D11ShaderResourceView* views[] = {state.bend_view.Get(), state.split_view.Get()};
  native->PSSetShaderResources(60, 2, views);
  state.debug_bound = true;
}

inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  state.debug_ready = false;
  if (!state.debug_bound) return;
  state.debug_bound = false;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  ID3D11ShaderResourceView* none[] = {nullptr, nullptr};
  if (native != nullptr) native->PSSetShaderResources(60, 2, none);
}

inline void Install(std::span<const twattila::light_bounce::SunDraw> sun_draws) {
  for (const auto& draw : sun_draws) {
    renodx::utils::command_action::Register(
        internal::OnMaskSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnMaskSunDraw);
}

inline void OnDestroyDevice(reshade::api::device*) {
  const std::unique_lock lock(internal::state.mutex);
  internal::ReleaseObjects();
}

}  // namespace twshogun2::screen_space_shadows
