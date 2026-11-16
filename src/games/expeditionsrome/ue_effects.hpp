/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/ue_light_bounce.hpp"
#include "../talesofarise_dlss/ue_screen_space_shadows.hpp"
#include "./ray_tracing.hpp"

// Bend's screen-space shadows and GT-VBGI (on by default since ATN's 2026-11-16; Tales of Arise's Unreal port of the Total War games' passes:
// talesofarise_dlss/ue_screen_space_shadows.hpp, ue_light_bounce.hpp) at the sun's deferred light draw (the light
// rectangle vs 0x7C21DA11: the View at ps b0, the light's FDeferredLightUniformStruct at ps b1, the scene depth at t1,
// G-buffer A-C at t2-t4, the light attenuation at t8). Bend before the draw, its occlusion into the attenuation's light
// function x per-object channel (ue_sss_apply), which the light multiplies into its surface shadow; Unreal's own
// contact shadows (contact_shadows.hpp) off while it runs. GT-VBGI after the draw, once a frame: from the scene colour
// as the sun left it (the draw's target), added into it x the diffuse colour. In the add-on's own context state.
namespace expeditionsrome::dlss::ue_effects {

using Microsoft::WRL::ComPtr;

struct Settings {
  twattila::screen_space_shadows::Settings shadows;  // off: none
  LightBounceConstants bounce = {};                   // strength 0: none
  bool denoise = false;
  float render_share = 1.f;  // the render size over the output's
};

namespace internal {

constexpr size_t DEPTH = 1, GBUFFER_A = 2, GBUFFER_B = 3, GBUFFER_C = 4, ATTENUATION = 8;

struct State {
  std::mutex mutex;
  Settings settings;
  uint64_t frame = 0;
  uint64_t bounce_frame = UINT64_MAX;
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> apply_shadows;
  ComPtr<ID3D11BlendState> multiply_blue;
  // The game's textures' views (each holds its texture; let go at a new render size, when Unreal makes them again): the
  // attenuation's render target, the scene colour's shader resource.
  std::array<UINT, 2> render_size = {};
  std::unordered_map<ID3D11Resource*, ComPtr<ID3D11RenderTargetView>> attenuation_targets;
  std::unordered_map<ID3D11Resource*, ComPtr<ID3D11ShaderResourceView>> scene_views;
};
static State state;

// The draw's View (ps b0) and light (ps b1) bindings, the latter a directional light's 144 - 256 bytes (this vertex
// shader's other passes bind a larger View there); false: not a light's draw.
inline bool LightConstants(ID3D11DeviceContext1* context, std::array<talesofarise::dlss::ConstantBinding, 2>* constants) {
  for (UINT slot = 0; slot < 2; ++slot) {
    auto& binding = (*constants)[slot];
    context->PSGetConstantBuffers1(slot, 1, binding.buffer.GetAddressOf(), &binding.first, &binding.count);
    if (!binding.buffer) return false;
  }
  D3D11_BUFFER_DESC light_desc = {};
  (*constants)[1].buffer->GetDesc(&light_desc);
  const UINT light_first = std::min((*constants)[1].first * 16, light_desc.ByteWidth);
  const UINT light_bytes = std::min(light_desc.ByteWidth - light_first, (*constants)[1].count * 16);
  return light_bytes >= ray_tracing::internal::MIN_LIGHT_BYTES && light_bytes <= ray_tracing::internal::MAX_LIGHT_BYTES;
}

// After the sun's light draw: GT-VBGI from its target (the scene colour), once a frame (a second directional light's
// draw would turn its history over again).
template <typename Context>
void OnSunLit(Context& command, const void*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  std::array<talesofarise::dlss::ConstantBinding, 2> constants;
  if (!target || !LightConstants(context1.Get(), &constants)) return;
  ComPtr<ID3D11Resource> scene;
  target->GetResource(scene.GetAddressOf());
  std::array<ComPtr<ID3D11ShaderResourceView>, GBUFFER_C + 1> inputs;
  {
    std::array<ID3D11ShaderResourceView*, GBUFFER_C + 1> pointers = {};
    context->PSGetShaderResources(0, static_cast<UINT>(pointers.size()), pointers.data());
    for (size_t index = 0; index < pointers.size(); ++index) {
      inputs[index].Attach(pointers[index]);
    }
  }
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const std::unique_lock lock(state.mutex);
  if (state.bounce_frame == state.frame || state.device != device) return;
  state.bounce_frame = state.frame;
  auto& scene_view = state.scene_views[scene.Get()];
  if (!scene_view && FAILED(device->CreateShaderResourceView(scene.Get(), nullptr, scene_view.GetAddressOf()))) {
    state.scene_views.erase(scene.Get());
    return;
  }
  talesofarise::ue_light_bounce::Apply(context, {
                                                    .light = scene_view.Get(),
                                                    .depth = inputs[DEPTH].Get(),
                                                    .gbuffer_a = inputs[GBUFFER_A].Get(),
                                                    .gbuffer_b = inputs[GBUFFER_B].Get(),
                                                    .gbuffer_c = inputs[GBUFFER_C].Get(),
                                                    .view = constants[0],
                                                    .render_size = state.render_size,
                                                },
                                       state.settings.bounce, state.settings.denoise, target.Get());
}

}  // namespace internal

// The sun's deferred light draw: Bend's occlusion into its light attenuation before it, GT-VBGI after it.
constexpr auto OnLightRectangle = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using internal::state;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  std::array<talesofarise::dlss::ConstantBinding, 2> constants;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))
      || !internal::LightConstants(context1.Get(), &constants)) {
    return {};
  }
  std::array<ComPtr<ID3D11ShaderResourceView>, internal::ATTENUATION + 1> inputs;
  {
    std::array<ID3D11ShaderResourceView*, internal::ATTENUATION + 1> pointers = {};
    context->PSGetShaderResources(0, static_cast<UINT>(pointers.size()), pointers.data());
    for (size_t index = 0; index < pointers.size(); ++index) {
      inputs[index].Attach(pointers[index]);
    }
  }
  D3D11_VIEWPORT viewport = {};
  UINT viewports = 1;
  context->RSGetViewports(&viewports, &viewport);
  const std::array<UINT, 2> render_size = {static_cast<UINT>(viewport.Width), static_cast<UINT>(viewport.Height)};
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());

  const std::unique_lock lock(state.mutex);
  const bool shadows = state.settings.shadows.enabled;
  const bool bounce = state.settings.bounce.strength > 0.f;
  if ((!shadows && !bounce) || viewports == 0 || render_size[0] == 0 || render_size[1] == 0 || !inputs[internal::DEPTH]) return {};
  if (state.device != device) {
    state.device = device;
    state.attenuation_targets.clear();
    state.scene_views.clear();
    D3D11_BLEND_DESC multiply_desc = {};
    multiply_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ZERO, .DestBlend = D3D11_BLEND_SRC_COLOR,
                                     .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ZERO, .DestBlendAlpha = D3D11_BLEND_ONE,
                                     .BlendOpAlpha = D3D11_BLEND_OP_ADD, .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_BLUE};
    state.failed = !talesofarise::dlss::MakeContextState(device.Get(), &state.private_state)
                   || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr,
                                                        state.full_screen.ReleaseAndGetAddressOf()))
                   || FAILED(device->CreatePixelShader(__ue_sss_apply.data(), __ue_sss_apply.size(), nullptr,
                                                       state.apply_shadows.ReleaseAndGetAddressOf()))
                   || FAILED(device->CreateBlendState(&multiply_desc, state.multiply_blue.ReleaseAndGetAddressOf()));
  }
  if (state.failed) return {};
  if (state.render_size != render_size) {
    state.render_size = render_size;
    state.attenuation_targets.clear();
    state.scene_views.clear();
  }

  // Bend's occlusion into the attenuation (Unreal's B8G8R8A8 target at least the render size; a light without shadows
  // binds a 1 x 1 white texture, left alone).
  if (shadows && inputs[internal::ATTENUATION]) {
    ComPtr<ID3D11Resource> attenuation;
    inputs[internal::ATTENUATION]->GetResource(attenuation.GetAddressOf());
    ComPtr<ID3D11Texture2D> attenuation_texture;
    D3D11_TEXTURE2D_DESC desc = {};
    if (SUCCEEDED(attenuation.As(&attenuation_texture))) {
      attenuation_texture->GetDesc(&desc);
    }
    auto& target = state.attenuation_targets[attenuation.Get()];
    if (desc.Width >= render_size[0] && desc.Height >= render_size[1] && desc.MipLevels == 1 && desc.SampleDesc.Count == 1
        && (desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0
        && (desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) && !target) {
      const D3D11_RENDER_TARGET_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_B8G8R8A8_UNORM, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
      device->CreateRenderTargetView(attenuation.Get(), &view_desc, target.GetAddressOf());
    }
    ID3D11ShaderResourceView* occlusion =
        target ? talesofarise::ue_screen_space_shadows::Compute(context, {.depth = inputs[internal::DEPTH].Get(),
                                                                         .sun = constants[1],
                                                                         .view = constants[0],
                                                                         .render_share = state.settings.render_share},
                                                                state.settings.shadows)
               : nullptr;
    if (occlusion != nullptr) {
      const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
      const D3D11_VIEWPORT render_viewport = {.Width = viewport.Width, .Height = viewport.Height, .MaxDepth = 1.f};
      ID3D11RenderTargetView* output = target.Get();
      context1->OMSetRenderTargets(1, &output, nullptr);
      context1->OMSetBlendState(state.multiply_blue.Get(), nullptr, UINT_MAX);
      context1->RSSetViewports(1, &render_viewport);
      context1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context1->VSSetShader(state.full_screen.Get(), nullptr, 0);
      context1->PSSetShader(state.apply_shadows.Get(), nullptr, 0);
      context1->PSSetShaderResources(0, 1, &occlusion);
      context1->Draw(3, 0);
    }
  }
  if (!bounce) return {};
  return {.post_callback = internal::OnSunLit<Context>};
};

// Every present, a new frame: Bend's settings (off: none), GT-VBGI's sliders (strength 0: none; the noise turns with the
// frame) and the render share.
inline void Update(const twattila::screen_space_shadows::Settings& shadows, const twattila::light_bounce::Sliders& gi, float render_share) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  ++state.frame;
  state.settings = {
      .shadows = shadows,
      .bounce = talesofarise::ue_light_bounce::ConstantsOf(gi, state.frame),
      .denoise = (gi.denoise != 0.f),
      .render_share = render_share,
  };
}

// The game's device is going away: the add-on's objects on it with it.
inline void Release(ID3D11Device* device) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  talesofarise::ue_screen_space_shadows::Release(device);
  talesofarise::ue_light_bounce::Release(device);
  if (state.device.Get() == device) {
    state.device.Reset();
    state.private_state.Reset();
    state.full_screen.Reset();
    state.apply_shadows.Reset();
    state.multiply_blue.Reset();
    state.render_size = {};
    state.attenuation_targets.clear();
    state.scene_views.clear();
    state.bounce_frame = UINT64_MAX;
    state.failed = false;
  }
}

}  // namespace expeditionsrome::dlss::ue_effects
