/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <span>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../expeditionsrome_dlss/constant_mirror.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/rt_core.hpp"

// Ray tracing (Experimental): Shogun 2's scene through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe
// next to the addon, its own D3D12 device with DXR). The scene: every G-buffer draw (rt0-2 RGBA16F, depth written,
// unblended; leaves alpha-tested against their diffuse alpha: rt_core's SceneCoverage), streamed out through the game's
// own vertex shaders (those with bone inputs every frame). The camera: camera_VS_PS (view_projection c10-c13 as
// uploaded, jittered as the scene was drawn; the position c0; the render size g_viewport_dimensions c38) from the
// game's uploads (expeditionsrome_dlss/constant_mirror.hpp). At the frame's first sun light draw (sunlight00-03, the
// addon's): the sun (lighting_VS_PS c0, from the sun) and the trace (its readable depth); the traced sun and ambient
// visibility at t40 / t41 for those draws (shaders/rt_sun.hlsli: the darker of them and the game's shadow mask and
// AO). The Light Bounce's trace goes through rays (path traced when on). Installed when an effect is on at launch.
namespace twshogun2::ray_tracing {

using Microsoft::WRL::ComPtr;

// A sun light draw (the addon's sunlight00-03): its readable depth's slot and its lighting_VS_PS's.
struct SunDraw {
  uint32_t shader_hash = 0;
  UINT depth_slot = 0;
  UINT lighting_slot = 0;
};
constexpr std::array<SunDraw, 4> SUN_DRAWS = {{
    {.shader_hash = 0xED1BED93, .depth_slot = 10, .lighting_slot = 2},
    {.shader_hash = 0x5496DE73, .depth_slot = 10, .lighting_slot = 2},
    {.shader_hash = 0xC3E0E939, .depth_slot = 12, .lighting_slot = 3},
    {.shader_hash = 0xD5FDDE8F, .depth_slot = 12, .lighting_slot = 3},
}};

namespace internal {

// camera_VS_PS through g_render_target_dimensions (c40): view_projection c10-c13, g_viewport_dimensions c38.
constexpr uint32_t CAMERA_ROWS = 41;
constexpr uint32_t CAMERA_VIEW_PROJECTION = 10;
constexpr uint32_t CAMERA_VIEWPORT = 38;
// The traced visibilities' slots (shaders/rt_sun.hlsli).
constexpr UINT VISIBILITY_SLOT = 40;

struct State {
  std::mutex mutex;
  twattila::rt_scene::Settings settings;
  std::array<UINT, 2> render_size = {};
  std::array<UINT, 2> depth_size = {};  // the sun draws' depth's: the main view's (other views' G-buffers stay out)
};
static State state;

// The main view's camera last uploaded in `buffer` (camera_VS_PS); false: none yet.
inline bool Camera(ID3D11Buffer* buffer, twattila::rt_scene::Camera* camera) {
  expeditionsrome::dlss::constants::Watch(buffer, CAMERA_ROWS * 16);
  std::array<std::array<float, 4>, CAMERA_ROWS> rows = {};
  if (!expeditionsrome::dlss::constants::Read(buffer, 0, std::as_writable_bytes(std::span(rows)))) return false;
  std::array<float, 16> view_projection = {};
  for (size_t row = 0; row < 4; ++row) std::copy_n(rows[CAMERA_VIEW_PROJECTION + row].begin(), 4, view_projection.begin() + (row * 4));
  *camera = {
      .view_projection = view_projection,
      .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
      .position = {rows[0][0], rows[0][1], rows[0][2]},
  };
  const std::unique_lock lock(state.mutex);
  state.render_size = {static_cast<UINT>(rows[CAMERA_VIEWPORT][0]), static_cast<UINT>(rows[CAMERA_VIEWPORT][1])};
  return true;
}

// A G-buffer draw (rt0-2 RGBA16F and the depth, depth written, unblended) of the main view (its depth the sun draws'
// size): the bridge prepared at that size, the pixel shader's alpha test, its vertex shader skinned or not.
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  std::array<ID3D11RenderTargetView*, 3> targets = {};
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth_view.GetAddressOf());
  bool gbuffer = static_cast<bool>(depth_view);
  for (auto* target : targets) {
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    if (target != nullptr) target->GetDesc(&desc);
    gbuffer = gbuffer && target != nullptr && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (target != nullptr) target->Release();
  }
  if (!gbuffer) return false;
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_reference = 0;
  native->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_reference);
  D3D11_DEPTH_STENCIL_DESC depth_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL};
  if (depth_state) depth_state->GetDesc(&depth_desc);
  ComPtr<ID3D11BlendState> blend_state;
  std::array<float, 4> blend_factor = {};
  UINT sample_mask = 0;
  native->OMGetBlendState(blend_state.GetAddressOf(), blend_factor.data(), &sample_mask);
  D3D11_BLEND_DESC blend_desc = {};
  if (blend_state) blend_state->GetDesc(&blend_desc);
  if (!depth_desc.DepthEnable || depth_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ALL || blend_desc.RenderTarget[0].BlendEnable) {
    return false;
  }
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  D3D11_TEXTURE2D_DESC depth_texture_desc = {};
  if (FAILED(depth_resource.As(&depth_texture))) return false;
  depth_texture->GetDesc(&depth_texture_desc);
  {
    const std::unique_lock lock(state.mutex);
    if (state.depth_size != std::array<UINT, 2>{depth_texture_desc.Width, depth_texture_desc.Height}) return false;
  }
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!twattila::dlaa_bridge::Prepare(device.Get(), depth_texture_desc.Width, depth_texture_desc.Height, false, true)
      || !twattila::rt_scene::SceneCoverage(native, shader)) {
    return false;
  }
  shader->dynamic = twattila::rt_scene::SkinnedVertexShader(shader->hash);
  return true;
}

// The frame's first sun light draw: the sun and the trace; every one: the traced visibilities bound (put back after).
constexpr auto OnSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& draw = command.template GetCallbackData<SunDraw>();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  twattila::rt_scene::Settings settings;
  {
    const std::unique_lock lock(state.mutex);
    settings = state.settings;
  }
  ComPtr<ID3D11ShaderResourceView> depth;
  std::array<ComPtr<ID3D11Buffer>, 2> buffers;  // camera_VS_PS, lighting_VS_PS
  native->PSGetShaderResources(draw.depth_slot, 1, depth.GetAddressOf());
  native->PSGetConstantBuffers(0, 1, buffers[0].GetAddressOf());
  native->PSGetConstantBuffers(draw.lighting_slot, 1, buffers[1].GetAddressOf());
  if (!depth || !buffers[0] || !buffers[1]) return {};
  {
    ComPtr<ID3D11Resource> depth_resource;
    depth->GetResource(depth_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> depth_texture;
    D3D11_TEXTURE2D_DESC depth_desc = {};
    if (SUCCEEDED(depth_resource.As(&depth_texture))) depth_texture->GetDesc(&depth_desc);
    const std::unique_lock lock(state.mutex);
    state.depth_size = {depth_desc.Width, depth_desc.Height};
  }
  // The sun (toward it) for this frame's trace (lighting_VS_PS c0 points from it).
  expeditionsrome::dlss::constants::Watch(buffers[1].Get(), 16);
  if (std::array<float, 4> sun = {}; twattila::dlaa_bridge::bridge.rt_sun.texture
                                     && expeditionsrome::dlss::constants::Read(buffers[1].Get(), 0, std::as_writable_bytes(std::span(sun)))) {
    if (const float length = std::sqrt((sun[0] * sun[0]) + (sun[1] * sun[1]) + (sun[2] * sun[2])); length > 0.f) {
      twattila::rt_scene::WriteSun(native, {-sun[0] / length, -sun[1] / length, -sun[2] / length});
    }
  }
  if (twattila::rt_scene::Camera camera; Camera(buffers[0].Get(), &camera)) twattila::rt_scene::Trace(native, depth.Get(), camera);
  const auto [sun, ambient] = twattila::rt_scene::Visibilities();
  const std::array<ID3D11ShaderResourceView*, 2> visibilities = {settings.sun_shadows ? sun : nullptr,
                                                                 settings.ambient_occlusion ? ambient : nullptr};
  if (visibilities[0] == nullptr && visibilities[1] == nullptr) return {};
  native->PSSetShaderResources(VISIBILITY_SLOT, static_cast<UINT>(visibilities.size()), visibilities.data());
  return {.post_callback = [](Context& command, const void*) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&native);
    constexpr std::array<ID3D11ShaderResourceView*, 2> NONE = {};
    if (native != nullptr) native->PSSetShaderResources(VISIBILITY_SLOT, static_cast<UINT>(NONE.size()), NONE.data());
  }};
};

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .camera = internal::Camera,
    .render_size = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.render_size;
    },
    .guides_written = [] { return false; },
    .capture_camera = [](ID3D11DeviceContext*, twattila::rt_scene::Camera*) { return true; },
    .scene_draw = internal::SceneDraw,
};

// Every present: the settings and a new frame.
inline void Update(const twattila::rt_scene::Settings& settings) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.settings = settings;
  }
  twattila::rt_scene::Update(settings);
}

inline void Install() {
  expeditionsrome::dlss::constants::Use(DLL_PROCESS_ATTACH);
  twattila::rt_scene::InstallScene({}, HOST);
  twattila::light_bounce::SetRayTracedTrace(&twattila::rt_scene::internal::TraceLightBounce);
  for (const auto& draw : SUN_DRAWS) {
    renodx::utils::command_action::Register(
        internal::OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunDraw);
  twattila::light_bounce::SetRayTracedTrace(nullptr);
  twattila::rt_scene::UninstallScene();
  expeditionsrome::dlss::constants::Use(DLL_PROCESS_DETACH);
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  twattila::rt_scene::OnDestroyDevice(device);
  twattila::dlaa_bridge::Release();
}

}  // namespace twshogun2::ray_tracing
