/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../expeditionsrome_dlss/constant_mirror.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/rt_core.hpp"

// Ray tracing (Experimental): Shogun 2's scene through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe
// next to the addon, its own D3D12 device with DXR). The scene: every G-buffer draw (rt0-2 RGBA16F, depth written,
// unblended; leaves alpha-tested against their diffuse alpha: rt_core's SceneCoverage), streamed out through the game's
// own vertex shaders (those with bone inputs every frame). The camera: camera_VS_PS (view_projection c10-c13 as
// uploaded, jittered as the scene was drawn; the position c0; the render size g_viewport_dimensions c38) from the
// game's uploads (expeditionsrome_dlss/constant_mirror.hpp). At the frame's first sun light draw (sunlight00-03, the
// addon's): the sun (lighting_VS_PS c0, from the sun), the G-buffer guides when the reflections or paths want them
// (rt_guides) and the trace (its readable depth); the traced sun and ambient visibility at t40 / t41 for those draws
// (shaders/rt_sun.hlsli: the darker of them and the game's shadow mask and AO). The traced reflections replace the
// screen-space march's hit in its resolve (ssrresolve00, twattila/rt_reflection.hlsli; the march and its blurs left
// out), whose lit scene is the helper's last frame's colour for the next frame (KeepColor). The local lights' shadows:
// the point lights the frame draws (pointlight00 / 01: point_light_PS b2) for the next frame's trace (rt_core picks the
// nearest and brightest), its lit bits and positions bound for the point light draws (t42 / b12,
// twattila/rt_local_lights.hlsli). The Light Bounce's trace goes through rays (path traced when on).
// Installed when an effect is on at launch.
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
// The point light draws (pointlight00 / 01: point_light_PS at b2, its light's position c0, colour c1.xyz, reach c1.w).
constexpr std::array<uint32_t, 2> POINT_LIGHT_SHADERS = {0x5CBF4FB6, 0x57139F8D};

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
  uint64_t frame = 0;
  uint64_t guides_frame = UINT64_MAX;  // the frame the guides were written in
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> guides;
  bool failed = false;
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
  bool reflections = false;
  bool guides = false;
  {
    const std::unique_lock lock(state.mutex);
    if (state.depth_size != std::array<UINT, 2>{depth_texture_desc.Width, depth_texture_desc.Height}) return false;
    reflections = state.settings.reflections;
    guides = reflections || state.settings.path_tracing;
  }
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!twattila::dlaa_bridge::Prepare(device.Get(), depth_texture_desc.Width, depth_texture_desc.Height, guides, true,
                                      DXGI_FORMAT_UNKNOWN, reflections)
      || !twattila::rt_scene::SceneCoverage(native, shader)) {
    return false;
  }
  shader->dynamic = twattila::rt_scene::SkinnedVertexShader(shader->hash);
  return true;
}

// The G-buffer guides for the reflections and paths from a sun draw's G-buffer (the three slots before its depth: normal,
// colour, constants), once a frame, in the addon's own context state.
// The addon's objects on the context's device (made again when it changes); false: not available. Hold state.mutex.
inline bool PrepareObjects(ID3D11DeviceContext* native) {
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (state.device == device) return !state.failed;
  ComPtr<ID3D11Device1> device1;
  const auto feature_level = device->GetFeatureLevel();
  const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
  state.device = device;
  state.private_state.Reset();
  state.guides.Reset();
  state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                 || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                             nullptr, state.private_state.GetAddressOf()))
                 || FAILED(device->CreateComputeShader(__rt_guides.data(), __rt_guides.size(), nullptr, state.guides.GetAddressOf()));
  return !state.failed;
}

inline void WriteGuides(ID3D11DeviceContext* native, UINT depth_slot) {
  auto& bridge = twattila::dlaa_bridge::bridge;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer;
  for (UINT index = 0; index < gbuffer.size(); ++index) native->PSGetShaderResources(depth_slot - 3 + index, 1, gbuffer[index].GetAddressOf());
  ComPtr<ID3D11DeviceContext1> native1;
  if (!bridge.guides || !gbuffer[0] || !gbuffer[1] || !gbuffer[2] || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
    return;
  }
  {
    const std::unique_lock lock(state.mutex);
    if (state.guides_frame == state.frame || !PrepareObjects(native)) return;
    state.guides_frame = state.frame;
  }
  const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
  ID3D11ShaderResourceView* views[] = {gbuffer[0].Get(), gbuffer[1].Get(), gbuffer[2].Get()};
  ID3D11UnorderedAccessView* written[] = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(), bridge.normals.uav.Get(),
                                          bridge.roughness.uav.Get()};
  native->CSSetShader(state.guides.Get(), nullptr, 0);
  native->CSSetShaderResources(0, 3, views);
  native->CSSetUnorderedAccessViews(0, 4, written, nullptr);
  native->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
}

// The frame's first sun light draw: the sun, the guides and the trace; every one: the traced visibilities bound (put back
// after).
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
  if (settings.reflections || settings.path_tracing) WriteGuides(native, draw.depth_slot);
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

// A point light draw: its light (point_light_PS) kept for the next frame's trace; this frame's traced lights' bits and
// positions bound (put back after).
constexpr auto OnPointLightDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  ComPtr<ID3D11Buffer> point_light;
  native->PSGetConstantBuffers(2, 1, point_light.GetAddressOf());
  if (point_light) expeditionsrome::dlss::constants::Watch(point_light.Get(), 32);
  if (std::array<std::array<float, 4>, 2> rows = {};
      point_light && expeditionsrome::dlss::constants::Read(point_light.Get(), 0, std::as_writable_bytes(std::span(rows)))) {
    twattila::rt_scene::AddLocalLight({.light = {rows[0][0], rows[0][1], rows[0][2], rows[1][3]}, .brightness = std::max({rows[1][0], rows[1][1], rows[1][2]})});
  }
  if (!twattila::rt_scene::BindLocalLights(native)) return {};
  return {.post_callback = [](Context& command, const void*) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&native);
    if (native != nullptr) twattila::rt_scene::UnbindLocalLights(native);
  }};
};

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .camera = internal::Camera,
    .render_size = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.render_size;
    },
    .guides_written = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.guides_frame == internal::state.frame;
    },
    .capture_camera = [](ID3D11DeviceContext*, twattila::rt_scene::Camera*) { return true; },
    .scene_draw = internal::SceneDraw,
};

// Every present: the settings and a new frame.
inline void Update(const twattila::rt_scene::Settings& settings) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.settings = settings;
    ++internal::state.frame;
  }
  twattila::rt_scene::Update(settings);
}

// After the screen-space reflections' resolve (its t9: this frame's lit scene, with mips): the render pixels of its first
// level are the helper's colour, last frame's light for the next frame's reflections and paths.
inline void KeepColor(reshade::api::command_list* cmd_list) {
  std::array<UINT, 2> render_size;
  {
    const std::unique_lock lock(internal::state.mutex);
    if (!(internal::state.settings.reflections || internal::state.settings.path_tracing)) return;
    render_size = internal::state.render_size;
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (native == nullptr || !bridge.color.texture || render_size[0] == 0 || render_size[0] > bridge.width || render_size[1] > bridge.height) {
    return;
  }
  ComPtr<ID3D11ShaderResourceView> scene;
  native->CSGetShaderResources(9, 1, scene.GetAddressOf());
  if (!scene) return;
  ComPtr<ID3D11Resource> resource;
  scene->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC desc = {};
  if (FAILED(resource.As(&texture))) return;
  texture->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Width < render_size[0] || desc.Height < render_size[1]) return;
  const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = render_size[0], .bottom = render_size[1], .back = 1};
  native->CopySubresourceRegion(bridge.color.texture.Get(), 0, 0, 0, 0, resource.Get(), 0, &region);
}

inline void Install() {
  expeditionsrome::dlss::constants::Use(DLL_PROCESS_ATTACH);
  twattila::rt_scene::InstallScene({}, HOST);
  twattila::light_bounce::SetRayTracedTrace(&twattila::rt_scene::internal::TraceLightBounce);
  for (const auto& draw : SUN_DRAWS) {
    renodx::utils::command_action::Register(
        internal::OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
  for (const uint32_t shader_hash : POINT_LIGHT_SHADERS) {
    renodx::utils::command_action::Register(
        internal::OnPointLightDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunDraw);
  renodx::utils::command_action::Unregister(internal::OnPointLightDraw);
  twattila::light_bounce::SetRayTracedTrace(nullptr);
  twattila::rt_scene::UninstallScene();
  expeditionsrome::dlss::constants::Use(DLL_PROCESS_DETACH);
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.device.Reset();
    internal::state.private_state.Reset();
    internal::state.guides.Reset();
    internal::state.failed = false;
  }
  twattila::rt_scene::OnDestroyDevice(device);
  twattila::dlaa_bridge::Release();
}

}  // namespace twshogun2::ray_tracing
