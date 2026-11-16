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

#include <include/reshade.hpp>

#include "../talesofarise_dlss/constant_mirror.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../talesofarise_dlss/rt_shadows.hpp"
#include "../talesofarise_dlss/upscaling.hpp"
#include "../twattila/rt_core.hpp"

// Ray Tracing in the Helper (Experimental; off: rt_shadows.hpp's rays on the add-on's own D3D12 device): Tales of
// Arise's scene through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe, its own D3D12 device), in
// metres (Unreal's centimetres / 100). The scene: the depth prepass's draws into the scene depth (no target, depth
// written): the opaque prepass's vertex shaders (rt_shadows::DEPTH_SHADERS; the characters' dynamic) and the masked
// prepass's (rt_core's SceneCoverage: an alpha test at a texture coordinate as drawn, a dithered fade solid, a mask read
// at a scaled coordinate left out; the characters' by rt_shadows::SKINNED_MASKED_SHADERS dynamic). The trace at Bandai's
// sun shadow mask (cs 0x2084C889: the scene depth at t0, ScreenToShadow at b0 c28-c30, the View at b1): the camera
// (TranslatedWorldToClip c0-c3 and PreViewTranslation c62 in metres), the direction toward the sun (0x7A1E0D12's
// TowardSun, here on the CPU); then the traced sun at t40 for sunshadow00 (its texture's top-left the render pixels it
// loads) and the traced ambient copied to the scene depth's size for the lighting passes' t41 (rt_ambient.hlsli reads it
// by UV), in rt_shadows' place. Step 1 of the move (ATN 2026-11-16): the sun shadows and the AO.
namespace talesofarise::dlss::ray_tracing {

using Microsoft::WRL::ComPtr;

namespace internal {

// UE 4.21's View (the sun shadow mask's b1, 0x7A1E0D12's rows): TranslatedWorldToClip c0-c3, ScreenToWorld c44-c46,
// PreViewTranslation c62, ViewSizeAndInvSize c122. Its ScreenToShadow at b0 c28-c30.
constexpr uint32_t VIEW_SCREEN_TO_WORLD = 44;
constexpr uint32_t VIEW_PRE_VIEW_TRANSLATION = 62;
constexpr uint32_t VIEW_SIZE = 122;
constexpr uint32_t VIEW_ROWS = VIEW_SIZE + 1;
constexpr uint32_t SCREEN_TO_SHADOW = 28;
constexpr double CENTIMETRES = 100.0;

struct State {
  std::mutex mutex;
  twattila::rt_scene::Settings settings;
  ID3D11Resource* scene_depth = nullptr;  // the sun shadow mask's t0 (compared only)
  std::array<UINT, 2> render_size = {};
  uint64_t frame = 0;
  uint64_t ambient_frame = UINT64_MAX;  // the traced ambient's copy
  ComPtr<ID3D11Texture2D> ambient;      // at the scene depth's size, the render pixels at its top-left
  ComPtr<ID3D11ShaderResourceView> ambient_srv;
};
inline State state;

// A depth prepass draw into the scene depth (no target, depth written): an opaque prepass vertex shader's, or a masked
// one's by its pixel shader's coverage; false: not in the scene.
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(1, target.GetAddressOf(), depth_view.GetAddressOf());
  if (target || !depth_view) return false;
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_reference = 0;
  native->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_reference);
  D3D11_DEPTH_STENCIL_DESC depth_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL};
  if (depth_state) depth_state->GetDesc(&depth_desc);
  if (!depth_desc.DepthEnable || depth_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ALL) return false;
  {
    const std::unique_lock lock(state.mutex);
    if (depth_resource.Get() != state.scene_depth) return false;
  }
  const auto depth_shader = std::ranges::find(rt_shadows::DEPTH_SHADERS, shader->hash, &rt_shadows::DepthShader::hash);
  if (depth_shader != rt_shadows::DEPTH_SHADERS.end()) {
    shader->dynamic = depth_shader->dynamic;
    return true;
  }
  ComPtr<ID3D11PixelShader> pixel_shader;
  native->PSGetShader(pixel_shader.GetAddressOf(), nullptr, nullptr);
  if (!pixel_shader || !twattila::rt_scene::SceneCoverage(native, shader)) return false;
  shader->dynamic = std::ranges::find(rt_shadows::SKINNED_MASKED_SHADERS, shader->hash) != rt_shadows::SKINNED_MASKED_SHADERS.end();
  return true;
}

// The direction toward the sun (unit length) from the shadow map's ScreenToShadow rows and the View's ScreenToWorld
// rows (their xyz), as 0x7A1E0D12's TowardSun (sun_direction.hlsli); false: none (a degenerate matrix).
inline bool TowardSun(std::span<const std::array<float, 4>, 3> screen_to_shadow, std::span<const std::array<float, 4>, 3> screen_to_world,
                      std::array<float, 3>* toward_sun) {
  using Vector = std::array<double, 3>;
  const auto cross = [](const Vector& a, const Vector& b) -> Vector {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
  };
  const auto dot = [](const Vector& a, const Vector& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); };
  std::array<Vector, 3> rows;
  for (size_t row = 0; row < 3; ++row) rows[row] = {screen_to_world[row][0], screen_to_world[row][1], screen_to_world[row][2]};
  const double determinant = dot(rows[0], cross(rows[1], rows[2]));
  if (!(std::abs(determinant) > 0.0)) return false;
  // The inverse's columns (k0 - k2), then the shadow map's x, y and z axes in world space.
  const std::array<Vector, 3> inverse = {cross(rows[1], rows[2]), cross(rows[2], rows[0]), cross(rows[0], rows[1])};
  std::array<Vector, 3> axes = {};
  for (size_t axis = 0; axis < 3; ++axis) {
    for (size_t row = 0; row < 3; ++row) {
      for (size_t component = 0; component < 3; ++component) {
        axes[axis][component] += inverse[row][component] / determinant * screen_to_shadow[row][axis];
      }
    }
  }
  Vector toward = cross(axes[0], axes[1]);
  if (dot(toward, axes[2]) > 0.0) toward = {-toward[0], -toward[1], -toward[2]};
  const double length = std::sqrt(dot(toward, toward));
  if (!(length > 0.0) || !std::isfinite(length)) return false;
  *toward_sun = {static_cast<float>(toward[0] / length), static_cast<float>(toward[1] / length), static_cast<float>(toward[2] / length)};
  return true;
}

// The camera from the View's TranslatedWorldToClip (rows c0-c3, row vectors on centimetres) and PreViewTranslation
// (c62): rows acting on metres.
inline twattila::rt_scene::Camera ViewCamera(std::span<const std::array<float, 4>> view) {
  const auto& translation = view[VIEW_PRE_VIEW_TRANSLATION];
  std::array<float, 16> view_projection = {};
  for (size_t row = 0; row < 4; ++row) {
    double offset = view[3][row];
    for (size_t column = 0; column < 3; ++column) {
      view_projection[(row * 4) + column] = static_cast<float>(view[column][row] * CENTIMETRES);
      offset += static_cast<double>(translation[column]) * view[column][row];
    }
    view_projection[(row * 4) + 3] = static_cast<float>(offset);
  }
  return {
      .view_projection = view_projection,
      .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
      .position = {static_cast<float>(-translation[0] / CENTIMETRES), static_cast<float>(-translation[1] / CENTIMETRES),
                   static_cast<float>(-translation[2] / CENTIMETRES)},
  };
}

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .render_size = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.render_size;
    },
    // The reflections' and the paths' G-buffer guides: none yet (step 1).
    .guides_written = [] { return false; },
    .capture_camera = [](ID3D11DeviceContext*, twattila::rt_scene::Camera*) { return true; },
    .scene_draw = internal::SceneDraw,
    .characters_dynamic = true,
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

// At Bandai's sun shadow mask: the camera and the sun to the helper, the trace; the traced sun at t40 (rt_shadows::Unbind
// after the mask), the traced ambient copied for Ambient. False: not traced (nothing bound).
inline bool Trace(ID3D11DeviceContext* context) {
  using internal::state;
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  {
    const std::unique_lock lock(state.mutex);
    settings = state.settings;
    frame = state.frame;
  }
  ComPtr<ID3D11DeviceContext1> context1;
  if (!(settings.sun_shadows || settings.ambient_occlusion) || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return false;
  ComPtr<ID3D11ShaderResourceView> depth;
  context->CSGetShaderResources(0, 1, depth.GetAddressOf());
  std::array<ComPtr<ID3D11Buffer>, 2> buffers;
  std::array<UINT, 2> first_constants = {};
  std::array<UINT, 2> constant_counts = {};
  for (UINT slot = 0; slot < 2; ++slot) {
    context1->CSGetConstantBuffers1(slot, 1, buffers[slot].GetAddressOf(), &first_constants[slot], &constant_counts[slot]);
  }
  if (!depth || !buffers[0] || !buffers[1] || constant_counts[0] <= internal::SCREEN_TO_SHADOW + 2 || constant_counts[1] < internal::VIEW_ROWS) {
    return false;
  }
  std::array<std::array<float, 4>, 3> screen_to_shadow = {};
  std::array<std::array<float, 4>, internal::VIEW_ROWS> view = {};
  talesofarise::dlss::constants::Watch(buffers[0].Get(), (first_constants[0] + internal::SCREEN_TO_SHADOW + 3) * 16);
  talesofarise::dlss::constants::Watch(buffers[1].Get(), (first_constants[1] + internal::VIEW_ROWS) * 16);
  std::array<float, 3> toward_sun = {};
  if (!talesofarise::dlss::constants::Read(buffers[0].Get(), uint64_t{first_constants[0] + internal::SCREEN_TO_SHADOW} * 16,
                                           std::as_writable_bytes(std::span(screen_to_shadow)))
      || !talesofarise::dlss::constants::Read(buffers[1].Get(), uint64_t{first_constants[1]} * 16, std::as_writable_bytes(std::span(view)))
      || !internal::TowardSun(screen_to_shadow, std::span(view).subspan<internal::VIEW_SCREEN_TO_WORLD, 3>(), &toward_sun)) {
    return false;
  }
  ComPtr<ID3D11Resource> depth_resource;
  depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return false;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  const std::array<UINT, 2> render_size = {static_cast<UINT>(view[internal::VIEW_SIZE][0]), static_cast<UINT>(view[internal::VIEW_SIZE][1])};
  if (render_size[0] == 0 || render_size[1] == 0 || render_size[0] > depth_desc.Width || render_size[1] > depth_desc.Height) return false;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (!talesofarise::upscaling::PrepareBridge(device.Get())) return false;
  // The traced ambient's copy at the scene depth's size (made again when it changes).
  D3D11_TEXTURE2D_DESC ambient_desc = {};
  if (state.ambient) state.ambient->GetDesc(&ambient_desc);
  if (settings.ambient_occlusion && (ambient_desc.Width != depth_desc.Width || ambient_desc.Height != depth_desc.Height)) {
    const D3D11_TEXTURE2D_DESC desc = {.Width = depth_desc.Width, .Height = depth_desc.Height, .MipLevels = 1, .ArraySize = 1,
                                       .Format = DXGI_FORMAT_R8_UNORM, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    state.ambient_srv.Reset();
    if (FAILED(device->CreateTexture2D(&desc, nullptr, state.ambient.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateShaderResourceView(state.ambient.Get(), nullptr, state.ambient_srv.GetAddressOf()))) {
      state.ambient.Reset();
      state.ambient_srv.Reset();
    }
  }
  {
    const std::unique_lock lock(state.mutex);
    state.scene_depth = depth_resource.Get();
    state.render_size = render_size;
  }
  twattila::rt_scene::WriteSun(context, toward_sun);
  if (!twattila::rt_scene::Trace(context, depth.Get(), internal::ViewCamera(view))) return false;
  const auto [sun, ambient] = twattila::rt_scene::Visibilities();
  if (settings.sun_shadows && sun != nullptr) context->CSSetShaderResources(rt_shadows::VISIBILITY_SLOT, 1, &sun);
  if (settings.ambient_occlusion && ambient != nullptr && state.ambient) {
    ComPtr<ID3D11Resource> traced;
    ambient->GetResource(traced.GetAddressOf());
    const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = render_size[0], .bottom = render_size[1], .back = 1};
    context->CopySubresourceRegion(state.ambient.Get(), 0, 0, 0, 0, traced.Get(), 0, &region);
    const std::unique_lock lock(state.mutex);
    state.ambient_frame = frame;
  }
  return true;
}

// This frame's traced ambient for the lighting passes (t41), or null (off, or not traced this frame).
inline ComPtr<ID3D11ShaderResourceView> Ambient() {
  const std::unique_lock lock(internal::state.mutex);
  if (!internal::state.settings.ambient_occlusion || internal::state.ambient_frame != internal::state.frame) return nullptr;
  return internal::state.ambient_srv;
}

// The traced ambient reached last frame's lighting: Bandai's SSAO (before this frame's trace) may leave the AO to it.
inline bool AmbientTracedLastFrame() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.settings.ambient_occlusion && internal::state.ambient_frame + 1 == internal::state.frame;
}

inline void Install() {
  twattila::rt_scene::InstallScene({}, HOST);
}

inline void Uninstall() {
  twattila::rt_scene::UninstallScene();
}

// The game's device is going away: the addon's objects on it with it.
inline void Release(reshade::api::device* device) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.scene_depth = nullptr;
    internal::state.ambient_frame = UINT64_MAX;
  }
  internal::state.ambient_srv.Reset();
  internal::state.ambient.Reset();
  twattila::rt_scene::OnDestroyDevice(device);
}

}  // namespace talesofarise::dlss::ray_tracing
