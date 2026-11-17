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
#include <unordered_map>
#include <utility>

#include <embed/shaders.h>
#include <include/reshade.hpp>

#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/ue_light_bounce.hpp"
#include "../talesofarise_dlss/ue_screen_space_shadows.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/lighting_settings.hpp"
#include "./dlaa.hpp"
#include "./ray_tracing.hpp"

// The screen-space methods without rays (Graphics, Experimental, off until a game run shows them; divinity2.tsv's ssao_*
// and light_bounce_* rows), on ray_tracing::lighting_rows.
// GT-VBAO in the game's SSAO's place: PPSAO (B4AF6326, the perspective camera's, 4 draws) not drawn, its normals (t0,
// GBuffer1) and camera (b12, PerView) kept; then PPSAO_Reinterleave's draw (2CA00ABD) with ao_vbao in its pixel shader's
// place: its target the texture PPSAO_Blur smooths twice, as the game's AO, which PPApplyAO and ClusteredLightingWithSSAO
// read. PPSAO_Deinterleave stays (its slices unread then): it comes before any sign of the camera, and the orthographic
// camera's PPSAO (16BFFA78) reads them. Under Ray-Traced AO and Path-Traced Ambient the chain is theirs
// (ray_tracing.hpp: their hooks first): one method. Any view, the inventory's portrait too.
// GT-VBGI, Global Illumination's method without rays (Strength above 0, the rays' bounce off): the shared passes
// (talesofarise_dlss/ue_light_bounce.hpp, the Total War games' GT-VBGI; bounce_gbuffer.hlsli reads Divinity 2's G-buffer)
// at the rays' place, ClusteredLightingWithSSAO drawn: from the scene it lit, added to it x each pixel's diffuse colour,
// once a frame, the main view's (the camera dlaa.hpp keeps, and the last frame's for the history).
// Bend's screen-space sun shadows (Shadows' method without rays): Bend Studio's passes through the same host's
// talesofarise_dlss/ue_screen_space_shadows.hpp (their wrappers here: ue_sss_depth on the G-buffer pass's D24S8 depth
// dlaa.hpp keeps, ue_sss_setup on PerView and PerFrame's sun) after the sun's shadow mask is drawn, the occlusion
// multiplied into its red (the sun's visibility ClusteredLightingWithSSAO reads): over the game's shadow maps, as
// Attila's. Under the rays' sun shadows the mask is theirs (its draw skipped: no Bend).
namespace divinity2::screen_space {

using Microsoft::WRL::ComPtr;

// Bend's rows (feature_settings::AppendSss' keys).
static twattila::feature_settings::SssSliders shadow_rows;

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
  float exact_depth = 0.f;
};

// bounce_gbuffer.hlsli's bounce_view.
struct BounceView {
  std::array<float, 12> view_rows = {};
  std::array<float, 8> projection_rows = {};
  std::array<float, 16> previous_view_projection = {};
  std::array<float, 4> eye = {};
  std::array<float, 4> previous_eye = {};
  std::array<float, 4> size = {};
};

struct State {
  std::mutex mutex;
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3D11PixelShader> vbao;
  ComPtr<ID3D11Buffer> ao_constants;
  AoConstants uploaded = {};
  ComPtr<ID3D11Buffer> bounce_view;
  // Bend's occlusion into the mask: the add-on's context state, the full-screen triangle, rt_write, a multiply blend.
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> write;
  ComPtr<ID3D11BlendState> multiply;
  ComPtr<ID3D11RasterizerState> no_cull;
  uint64_t frame = 0;
  // PPSAO's normals and camera since the last reinterleave (null: the game's SSAO drew).
  ComPtr<ID3D11ShaderResourceView> normals;
  ComPtr<ID3D11Buffer> per_view;
  // What GT-VBAO's reinterleave changed, put back after its draw.
  bool replaced = false;
  ComPtr<ID3D11PixelShader> game_shader;
  ComPtr<ID3D11ShaderResourceView> game_view;
  std::array<ComPtr<ID3D11Buffer>, 2> game_constants;  // b1, b12
  // GT-VBGI: the frame it last ran, that frame's camera (its view-projection without the jitter, its position), the
  // scene's views (each holds its texture; let go at a new size).
  uint64_t bounce_frame = UINT64_MAX;
  std::array<float, 16> previous_view_projection = {};
  std::array<float, 3> previous_eye = {};
  std::array<UINT, 2> bounce_size = {};
  std::unordered_map<ID3D11Resource*, ComPtr<ID3D11ShaderResourceView>> scene_views;
};
static State state;

// The objects on the game's `device` (made again when it changes); false: not available. state.mutex held.
inline bool MakeObjects(ID3D11Device* device) {
  if (state.device.Get() == device) return !state.failed;
  state.device = device;
  state.vbao.Reset();
  state.ao_constants.Reset();
  state.bounce_view.Reset();
  state.private_state.Reset();
  state.full_screen.Reset();
  state.write.Reset();
  state.multiply.Reset();
  state.no_cull.Reset();
  state.uploaded = {};
  state.bounce_frame = UINT64_MAX;
  state.scene_views.clear();
  const D3D11_BUFFER_DESC ao_desc = {.ByteWidth = sizeof(AoConstants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC view_desc = {.ByteWidth = sizeof(BounceView), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  // The mask x (1 - the occlusion), red only (the mask is R8).
  D3D11_BLEND_DESC multiply_desc = {};
  multiply_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ZERO, .DestBlend = D3D11_BLEND_INV_SRC_COLOR,
                                   .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ZERO, .DestBlendAlpha = D3D11_BLEND_ONE,
                                   .BlendOpAlpha = D3D11_BLEND_OP_ADD, .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED};
  const D3D11_RASTERIZER_DESC no_cull_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE};
  state.failed = FAILED(device->CreatePixelShader(__ao_vbao.data(), __ao_vbao.size(), nullptr, state.vbao.GetAddressOf()))
                 || FAILED(device->CreateBuffer(&ao_desc, nullptr, state.ao_constants.GetAddressOf()))
                 || FAILED(device->CreateBuffer(&view_desc, nullptr, state.bounce_view.GetAddressOf()))
                 || !talesofarise::dlss::MakeContextState(device, &state.private_state)
                 || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, state.full_screen.GetAddressOf()))
                 || FAILED(device->CreatePixelShader(__rt_write.data(), __rt_write.size(), nullptr, state.write.GetAddressOf()))
                 || FAILED(device->CreateBlendState(&multiply_desc, state.multiply.GetAddressOf()))
                 || FAILED(device->CreateRasterizerState(&no_cull_desc, state.no_cull.GetAddressOf()));
  return !state.failed;
}

// Bend this frame (the rays' sun shadows skip the mask's draw itself).
inline bool ShadowsWanted() {
  return shadow_rows.enabled != 0.f;
}

// GT-VBGI this frame: Strength above 0 with the rays' bounce off.
inline bool BounceWanted() {
  return ray_tracing::lighting_rows.light_bounce_strength > 0.f && ray_tracing::sliders.light_bounce == 0.f;
}

}  // namespace internal

// PPSAO's draw (before it): not drawn under GT-VBAO (true: skipped), its normals and camera kept for the reinterleave.
inline bool SkipSao(reshade::api::command_list* cmd_list) {
  if (ray_tracing::lighting_rows.ssao_mode < 1.f) return false;
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
  const auto& rows = ray_tracing::lighting_rows;
  // The noise moves every frame only under a temporal upscaler (which settles it); else it stands still.
  const internal::AoConstants constants = {
      .radius = rows.ssao_radius,
      .strength = rows.ssao_strength,
      .thickness = rows.ssao_thickness,
      .steps = static_cast<float>(8u << static_cast<uint32_t>(std::clamp(rows.ssao_quality, 0.f, 3.f))),
      .slices = static_cast<float>(1u << static_cast<uint32_t>(std::clamp(rows.ssao_directions, 0.f, 3.f))),
      .auto_thickness = (rows.ssao_auto_thickness != 0.f) ? 1.f : 0.f,
      .frame = twattila::feature_settings::Upscaling(dlaa::sliders) ? static_cast<float>(state.frame % 64u) : 0.f,
      .exact_depth = (rows.ssao_exact_depth != 0.f) ? 1.f : 0.f,
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

// ClusteredLightingWithSSAO drawn (the main view's scene lit; its G-buffer still bound: t2 GBuffer1, t3 GBuffer2, t4
// GBuffer3, t6 LinearizeDepth's depth): GT-VBGI from the scene, added to it, once a frame.
inline void OnLit(reshade::api::command_list* cmd_list) {
  if (!internal::BounceWanted()) return;
  ID3D11DeviceContext* native = dlaa::internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  const ComPtr<ID3D11RenderTargetView> target = ray_tracing::internal::MainViewTarget(native);
  dlaa::internal::Camera camera;
  if (!target || !dlaa::MainCamera(&camera)) return;
  std::array<ComPtr<ID3D11ShaderResourceView>, 4> gbuffer;
  for (size_t index = 0; index < gbuffer.size(); ++index) {
    native->PSGetShaderResources(std::array<UINT, 4>{2, 3, 4, 6}[index], 1, gbuffer[index].GetAddressOf());
    if (!gbuffer[index]) return;
  }
  ComPtr<ID3D11Resource> scene;
  target->GetResource(scene.GetAddressOf());
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  const std::array<UINT, 2> size = dlaa::SceneSize();
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.bounce_frame == state.frame || !internal::MakeObjects(device.Get())) return;
  if (state.bounce_size != size) {
    state.bounce_size = size;
    state.scene_views.clear();
  }
  auto& scene_view = state.scene_views[scene.Get()];
  if (!scene_view && FAILED(device->CreateShaderResourceView(scene.Get(), nullptr, scene_view.GetAddressOf()))) {
    state.scene_views.erase(scene.Get());
    return;
  }
  // The last frame's camera where GT-VBGI ran then; else this frame's (no motion).
  const bool continued = state.bounce_frame + 1 == state.frame;
  state.bounce_frame = state.frame;
  if (!continued) {
    state.previous_view_projection = camera.view_projection;
    state.previous_eye = camera.position;
  }
  // The projection's rows 0 and 1 as the depth was drawn: the jitter (pixels, y up) x row 3 in clip units.
  internal::BounceView view = {
      .previous_view_projection = state.previous_view_projection,
      .eye = {camera.position[0], camera.position[1], camera.position[2], camera.planes[1]},
      .previous_eye = {state.previous_eye[0], state.previous_eye[1], state.previous_eye[2], 0.f},
      .size = {static_cast<float>(size[0]), static_cast<float>(size[1]), 0.f, 0.f},
  };
  std::copy_n(camera.view.begin(), 12, view.view_rows.begin());
  for (size_t row = 0; row < 2; ++row) {
    const float jitter = 2.f * camera.jitter[row] / static_cast<float>(camera.size[row]);
    for (size_t column = 0; column < 4; ++column) {
      view.projection_rows[(row * 4) + column] = camera.projection[(row * 4) + column] + (jitter * camera.projection[12 + column]);
    }
  }
  native->UpdateSubresource(state.bounce_view.Get(), 0, nullptr, &view, 0, 0);
  state.previous_view_projection = camera.view_projection;
  state.previous_eye = camera.position;

  const auto& rows = ray_tracing::lighting_rows;
  twattila::light_bounce::Sliders sliders = twattila::lighting_settings::LightBounceSliders(rows);
  talesofarise::ue_light_bounce::Apply(native,
                                       {
                                           .light = scene_view.Get(),
                                           .depth = gbuffer[3].Get(),
                                           .gbuffer_a = gbuffer[0].Get(),
                                           .gbuffer_b = gbuffer[2].Get(),
                                           .gbuffer_c = gbuffer[1].Get(),
                                           .view = {.buffer = state.bounce_view},
                                           .render_size = size,
                                       },
                                       talesofarise::ue_light_bounce::ConstantsOf(sliders, state.frame), sliders.denoise != 0.f, target.Get());
}

// A sun shadow mask drawn (the main view's: its target the render size; PerFrame at b13 and PerView at b12 as it read
// them): Bend's occlusion multiplied into its red.
inline void OnSunMaskDrawn(reshade::api::command_list* cmd_list) {
  if (!internal::ShadowsWanted()) return;
  ID3D11DeviceContext* native = dlaa::internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  const ComPtr<ID3D11RenderTargetView> target = ray_tracing::internal::MainViewTarget(native);
  const ComPtr<ID3D11ShaderResourceView> depth = dlaa::SceneDepth();
  talesofarise::dlss::ConstantBinding frame_constants;
  talesofarise::dlss::ConstantBinding view_constants;
  native->PSGetConstantBuffers(13, 1, frame_constants.buffer.GetAddressOf());
  native->PSGetConstantBuffers(internal::PER_VIEW_SLOT, 1, view_constants.buffer.GetAddressOf());
  ComPtr<ID3D11DeviceContext1> native1;
  if (!target || !depth || !frame_constants.buffer || !view_constants.buffer || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
    return;
  }
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  const std::array<UINT, 2> size = dlaa::SceneSize();
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (!internal::MakeObjects(device.Get())) return;
  ID3D11ShaderResourceView* occlusion = talesofarise::ue_screen_space_shadows::Compute(
      native, {.depth = depth.Get(), .sun = frame_constants, .view = view_constants}, twattila::feature_settings::SssSettings(shadow_rows));
  if (occlusion == nullptr) return;
  const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(size[0]), .Height = static_cast<float>(size[1]), .MaxDepth = 1.f};
  ID3D11RenderTargetView* output = target.Get();
  native1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  native1->VSSetShader(state.full_screen.Get(), nullptr, 0);
  native1->PSSetShader(state.write.Get(), nullptr, 0);
  native1->PSSetShaderResources(0, 1, &occlusion);
  native1->RSSetState(state.no_cull.Get());
  native1->RSSetViewports(1, &viewport);
  native1->OMSetBlendState(state.multiply.Get(), nullptr, 0xFFFFFFFF);
  native1->OMSetRenderTargets(1, &output, nullptr);
  native1->Draw(3, 0);
}

// The frame's end: the noise's next frame; normals kept by a PPSAO no reinterleave followed let go; whether the next
// frame wants dlaa.hpp's camera and depth (GT-VBGI, Bend).
inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*, uint32_t,
                      const reshade::api::rect*) {
  dlaa::screen_effects = internal::BounceWanted() || internal::ShadowsWanted();
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  ++state.frame;
  state.normals.Reset();
  state.per_view.Reset();
}

// The game's `device` is going away: the objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  talesofarise::ue_light_bounce::Release(native);
  talesofarise::ue_screen_space_shadows::Release(native);
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.device.Get() != native) return;
  state.device.Reset();
  state.failed = false;
  state.vbao.Reset();
  state.ao_constants.Reset();
  state.bounce_view.Reset();
  state.private_state.Reset();
  state.full_screen.Reset();
  state.write.Reset();
  state.multiply.Reset();
  state.no_cull.Reset();
  state.normals.Reset();
  state.per_view.Reset();
  state.scene_views.clear();
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
