/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <d3d9.h>
#include <wrl/client.h>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

#include "../../utils/device_proxy.hpp"
#include "../../utils/state.hpp"
#include "../twattila/rt_core.hpp"
#include "./bridge.hpp"
#include "./bridge_replay.hpp"
#include "./sun_shaders.h"

// R.U.S.E.'s ray-traced ground shadows (tools/nontw/ruse/plan.md 10b A5, G1): the TW helper traces the sun through the
// scene the draw replay captured (rt_core.hpp, a clip-space host on the DX11 proxy's device: InstallReplayedScene). The
// ground's sun shadow is a screen-space mask: the pre-pass (the 32 terrain patches and the camera pass 0x88292FC6) draws
// it, StretchRect copies it into a texture whose red the 110 terrain and 94 water pixel shaders read by screen position
// and multiply into the sun's colour alone. Right after that copy, at the main pass's first draw: the pre-pass's depth
// (INTZ) into the bridge's shared depth, the last frame's draws into the scene in this frame's camera (CaptureScene), the
// trace, and the traced visibility into the mask's red by minimum (the darker of the game's shadow and the rays').
namespace ruse::ray_tracing {

using Microsoft::WRL::ComPtr;

struct State {
  bool ground = false;  // the Ray-Traced Ground Shadows row (restart; with the anti-aliasing's helper)
  bool failed = false;
  // This frame's pre-pass (the camera pass's target and depth), where its copy went (an IDirect3DTexture9), the sun.
  uint32_t camera_frame = UINT32_MAX;
  uint32_t mask_frame = UINT32_MAX;
  uint32_t traced_frame = UINT32_MAX;
  uint32_t sun_frame = UINT32_MAX;
  uint64_t prepass_target = 0;
  uint64_t prepass_depth = 0;
  uint64_t ground_mask = 0;
  std::array<float, 3> toward_sun = {0.f, 0.f, 1.f};
  reshade::api::command_queue* proxy_queue = nullptr;  // the proxy's (its present's): rt_core's command list
  ComPtr<IDirect3DPixelShader9> depth_copy;
  ComPtr<IDirect3DPixelShader9> shadows;
  bridge::SharedTexture visibility;  // R32F: the traced sun visibility for the D3D9 pass
  ComPtr<ID3D11UnorderedAccessView> visibility_uav;
  ComPtr<ID3D11ShaderResourceView> depth_view;  // the bridge's shared depth
  ComPtr<ID3D11ComputeShader> copy;
};
inline State state;

inline const twattila::rt_scene::Host HOST = {
    .render_size = [] { return std::array<UINT, 2>{bridge::state.width, bridge::state.height}; },
    .guides_written = [] { return false; },
    // The captures' clip positions go back to the world with the trace's camera: CaptureScene draws them in it.
    .capture_camera = [](ID3D11DeviceContext*, twattila::rt_scene::Camera*) { return true; },
    // No ReShade draw event reaches rt_core (CaptureReplayed); set, the replay shaders stream out as the draws' own.
    .scene_draw = [](ID3D11DeviceContext*, twattila::rt_scene::SceneShader*) { return false; },
    // World coordinates near 2e6: the captures as offsets from the eye (rt_vertices with RT_VERTICES_RELATIVE).
    .clip_relative = true,
};

// The camera pass's draw: this frame's pre-pass target and depth.
inline void OnCameraPass() {
  state.camera_frame = bridge::state.frame;
  state.prepass_target = bridge::state.render_target;
  state.prepass_depth = bridge::state.depth_stencil;
}

// StretchRect of the pre-pass target: the ground mask the main pass reads.
inline bool OnCopyTextureRegion(reshade::api::command_list*, reshade::api::resource source, uint32_t, const reshade::api::subresource_box*,
                                reshade::api::resource dest, uint32_t, const reshade::api::subresource_box*, reshade::api::filter_mode) {
  if (state.camera_frame == bridge::state.frame && source.handle == state.prepass_target) {
    state.ground_mask = dest.handle;
    state.mask_frame = bridge::state.frame;
  }
  return false;
}

// The trace (once a frame) and the visibility into the ground mask, before the main pass draws with it.
inline void TraceGround(reshade::api::command_list* cmd_list) {
  auto& bridge = bridge::state;
  auto* proxy_reshade = renodx::utils::device_proxy::proxy_device_reshade;
  if (state.failed || !bridge.ready || bridge.failed || proxy_reshade == nullptr || state.proxy_queue == nullptr || !bridge.camera_valid
      || !bridge.intz_depths.contains(state.prepass_depth)) {
    return;
  }
  auto* host = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());
  auto* proxy = reinterpret_cast<ID3D11Device*>(proxy_reshade->get_native());
  ComPtr<ID3D11DeviceContext> context;
  proxy->GetImmediateContext(context.GetAddressOf());
  if (state.shadows == nullptr) {
    state.failed = true;
    ComPtr<IDirect3DDevice9Ex> host_ex;
    const bool made = SUCCEEDED(host->QueryInterface(IID_PPV_ARGS(host_ex.GetAddressOf())))
                      && SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(__ground_depth.data()), state.depth_copy.ReleaseAndGetAddressOf()))
                      && SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(__ground_shadows.data()), state.shadows.ReleaseAndGetAddressOf()))
                      && bridge::MakeShared(proxy, host_ex.Get(), DXGI_FORMAT_R32_FLOAT, D3DFMT_R32F, &state.visibility,
                                            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS)
                      && SUCCEEDED(proxy->CreateUnorderedAccessView(state.visibility.proxy.Get(), nullptr, state.visibility_uav.ReleaseAndGetAddressOf()))
                      && SUCCEEDED(proxy->CreateShaderResourceView(bridge.depth.proxy.Get(), nullptr, state.depth_view.ReleaseAndGetAddressOf()))
                      && SUCCEEDED(proxy->CreateComputeShader(__rt_ground_copy.data(), __rt_ground_copy.size(), nullptr, state.copy.ReleaseAndGetAddressOf()));
    if (!made) {
      state.shadows.Reset();
      reshade::log::message(reshade::log::level::warning, "[R.U.S.E.] Ray-traced ground shadows: their objects weren't made: off");
      return;
    }
    state.failed = false;
  }
  ComPtr<IDirect3DTexture9> prepass_depth;
  ComPtr<IDirect3DSurface9> mask_surface;
  if (FAILED(reinterpret_cast<IDirect3DSurface9*>(state.prepass_depth)->GetContainer(IID_PPV_ARGS(prepass_depth.GetAddressOf())))
      || FAILED(reinterpret_cast<IDirect3DTexture9*>(state.ground_mask)->GetSurfaceLevel(0, mask_surface.GetAddressOf()))) {
    return;
  }
  const std::array<IDirect3DSurface9*, 1> depth_target = {bridge.depth.host_surface.Get()};
  const std::array<IDirect3DBaseTexture9*, 1> depth_source = {prepass_depth.Get()};
  bridge::DrawQuad(host, depth_target, state.depth_copy.Get(), depth_source, {});
  bridge::WaitHost(host);

  // The camera: the camera pass's inverse view-projection (rows acting on column vectors), its inverse in doubles
  // (Gauss-Jordan), the eye where the view-projection sends w to 0 (the inverse's third column).
  const float* inverse = bridge.camera.data() + 16;
  std::array<std::array<double, 8>, 4> rows = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) rows[row][column] = inverse[(row * 4) + column];
    rows[row][4 + row] = 1.0;
  }
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) {
      if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) pivot = row;
    }
    if (std::abs(rows[pivot][column]) < 1e-300) return;
    std::swap(rows[column], rows[pivot]);
    const double scale = rows[column][column];
    for (double& value : rows[column]) value /= scale;
    for (int row = 0; row < 4; ++row) {
      if (row == column) continue;
      const double factor = rows[row][column];
      for (int other = 0; other < 8; ++other) rows[row][other] -= factor * rows[column][other];
    }
  }
  std::array<double, 16> view_projection = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) view_projection[(row * 4) + column] = rows[row][4 + column];
  }
  const double eye_w = inverse[14];
  if (std::abs(eye_w) < 1e-30) return;
  const std::array<double, 3> eye = {inverse[2] / eye_w, inverse[6] / eye_w, inverse[10] / eye_w};
  twattila::rt_scene::Camera camera = {};
  for (size_t index = 0; index < 16; ++index) {
    camera.view_projection[index] = static_cast<float>(view_projection[index]);
    camera.inverse_view_projection[index] = inverse[index];
  }
  // Host::clip_relative's rows: a clip position's offset from the eye (the inverse's row less its w row x the eye).
  for (size_t axis = 0; axis < 3; ++axis) {
    camera.position[axis] = static_cast<float>(eye[axis]);
    for (size_t k = 0; k < 4; ++k) {
      camera.relative_inverse[(axis * 4) + k] = static_cast<float>(inverse[(axis * 4) + k] - (inverse[12 + k] * eye[axis]));
    }
    camera.relative_inverse[12 + axis] = camera.position[axis];
  }
  camera.relative_inverse[15] = 1.f;

  twattila::rt_scene::WriteSun(context.Get(), state.toward_sun);
  replay::CaptureScene(state.proxy_queue->get_immediate_command_list(), view_projection);
  if (!twattila::rt_scene::Trace(context.Get(), state.depth_view.Get(), camera)) return;
  ID3D11ShaderResourceView* visibility = twattila::rt_scene::Visibilities()[0];
  if (visibility == nullptr) return;
  ID3D11UnorderedAccessView* output = state.visibility_uav.Get();
  context->CSSetShader(state.copy.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, &visibility);
  context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
  context->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
  ID3D11ShaderResourceView* no_view = nullptr;
  ID3D11UnorderedAccessView* no_output = nullptr;
  context->CSSetShaderResources(0, 1, &no_view);
  context->CSSetUnorderedAccessViews(0, 1, &no_output, nullptr);
  context->CSSetShader(nullptr, nullptr, 0);
  bridge::WaitProxy(proxy, context.Get());

  const std::array<IDirect3DSurface9*, 1> mask_target = {mask_surface.Get()};
  const std::array<IDirect3DBaseTexture9*, 1> traced = {state.visibility.host.Get()};
  bridge::DrawQuad(host, mask_target, state.shadows.Get(), traced, {},
                   {{D3DRS_ALPHABLENDENABLE, TRUE}, {D3DRS_SEPARATEALPHABLENDENABLE, FALSE}, {D3DRS_BLENDOP, D3DBLENDOP_MIN},
                    {D3DRS_SRCBLEND, D3DBLEND_ONE}, {D3DRS_DESTBLEND, D3DBLEND_ONE}, {D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED}});
}

// Each scene draw (D3D9): the sun from the first pixel shader this frame that holds it (sun_shaders.h: the light's
// direction, so toward the sun its negative), and the trace at the main pass's first draw after the mask's copy.
inline void OnSceneDraw(reshade::api::command_list* cmd_list) {
  auto& bridge = bridge::state;
  if (state.sun_frame != bridge.frame) {
    const uint32_t pixel_shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
    const auto found = std::ranges::lower_bound(SUN_DIRECTION_SHADERS, pixel_shader, {}, &SunRegister::hash);
    if (found != SUN_DIRECTION_SHADERS.end() && found->hash == pixel_shader) {
      const float* direction = bridge.pixel_registers.data() + (found->register_index * 4);
      const float length = std::sqrt((direction[0] * direction[0]) + (direction[1] * direction[1]) + (direction[2] * direction[2]));
      if (length > 1e-6f) {
        state.toward_sun = {-direction[0] / length, -direction[1] / length, -direction[2] / length};
        state.sun_frame = bridge.frame;
      }
    }
  }
  if (state.mask_frame != bridge.frame || state.traced_frame == bridge.frame || bridge.depth_stencil == state.prepass_depth) return;
  state.traced_frame = bridge.frame;
  TraceGround(cmd_list);
}

// The game's present: rt_core's new frame.
inline void OnPresent() {
  twattila::rt_scene::Update({.sun_shadows = state.ground});
}

// The proxy's device (rt_core's objects, ours on it) or the game's (its shaders, the mask) going.
inline void OnDestroyDevice(reshade::api::device* device) {
  twattila::rt_scene::OnDestroyDevice(device);
  if (device->get_api() == reshade::api::device_api::d3d11) {
    state.visibility = {};
    state.visibility_uav.Reset();
    state.depth_view.Reset();
    state.copy.Reset();
    state.shadows.Reset();
    state.proxy_queue = nullptr;
  } else if (device->get_api() == reshade::api::device_api::d3d9) {
    state.depth_copy.Reset();
    state.shadows.Reset();
    state.visibility = {};
    state.ground_mask = 0;
    state.prepass_depth = 0;
    state.prepass_target = 0;
  }
  state.failed = false;
}

}  // namespace ruse::ray_tracing
