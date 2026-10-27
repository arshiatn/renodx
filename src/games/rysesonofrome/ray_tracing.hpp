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
#include <span>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/directx.hpp"
#include "../rysesonofrome_dlss/constant_mirror.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/rt_core.hpp"

// Ray tracing (Experimental): Ryse's scene through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe
// next to the addon, its own D3D12 device with DXR). The scene: every opaque draw into the G-buffer (three RGBA16F
// targets and the depth, depth written, unblended; too many vertex shader permutations to list) streamed out through
// its own vertex shader (those with bone inputs every frame); the camera they were drawn with is VS PER_FRAME (b2, a
// range of a bigger buffer: rows c0-c3 the jittered view-projection, c6 the camera), read from the game's uploads
// (rysesonofrome_dlss/constant_mirror.hpp). Before the tiled lighting (cs 0x897232B5: t0 linear depth, b0 its
// PER_BATCH: the far plane's corners from the camera, the camera, the sun) each pixel's world position and the sun go
// to the helper (0x52C5E004) and the frame is traced. The sun's visibility then goes into the sun's shadow mask with the
// contact shadows (contact_shadows.hpp), the Light Bounce's trace goes through rays (light_bounce.hpp; path traced when
// on). Its scene shaders' code is kept from the game's start: Install only when ray tracing is on at launch.
namespace rysesonofrome::ray_tracing {

using Microsoft::WRL::ComPtr;

namespace internal {

struct State {
  std::mutex mutex;
  twattila::rt_scene::Camera camera;  // the G-buffer's, last captured
  bool camera_found = false;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> positions;
  bool failed = false;
};
static State state;

// An opaque G-buffer draw (three RGBA16F targets and the depth, depth written, unblended): the bridge prepared at the
// depth's size, its vertex shader skinned or not.
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  std::array<ID3D11RenderTargetView*, 4> targets = {};
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth_view.GetAddressOf());
  bool gbuffer = depth_view && targets[3] == nullptr;
  for (size_t index = 0; index < 3; ++index) {
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    if (targets[index] != nullptr) targets[index]->GetDesc(&desc);
    gbuffer = gbuffer && targets[index] != nullptr && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
  }
  for (auto* target : targets) {
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
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!twattila::dlaa_bridge::Prepare(device.Get(), depth_texture_desc.Width, depth_texture_desc.Height, false, true)) return false;
  shader->dynamic = twattila::rt_scene::SkinnedVertexShader(shader->hash);
  return true;
}

// The draw's camera: VS PER_FRAME (b2) as last uploaded.
inline bool CaptureCamera(ID3D11DeviceContext* native, twattila::rt_scene::Camera* camera) {
  ComPtr<ID3D11DeviceContext1> native1;
  if (FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return false;
  ComPtr<ID3D11Buffer> per_frame;
  UINT first_constant = 0;
  UINT constant_count = 0;
  native1->VSGetConstantBuffers1(2, 1, per_frame.GetAddressOf(), &first_constant, &constant_count);
  if (!per_frame || constant_count < 7) return false;
  const uint64_t offset = uint64_t{first_constant} * 16;
  rysesonofrome::dlss::constants::Watch(per_frame.Get(), static_cast<uint32_t>(offset + (7 * 16)));
  std::array<std::array<float, 4>, 7> rows = {};
  if (!rysesonofrome::dlss::constants::Read(per_frame.Get(), offset, std::as_writable_bytes(std::span(rows)))) return false;
  std::array<float, 16> view_projection = {};
  for (size_t row = 0; row < 4; ++row) std::copy_n(rows[row].begin(), 4, view_projection.begin() + (row * 4));
  *camera = {
      .view_projection = view_projection,
      .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
      .position = {rows[6][0], rows[6][1], rows[6][2]},
  };
  const std::unique_lock lock(state.mutex);
  state.camera = *camera;
  state.camera_found = true;
  return true;
}

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .render_size = [] { return std::array<UINT, 2>{twattila::dlaa_bridge::bridge.width, twattila::dlaa_bridge::bridge.height}; },
    .guides_written = [] { return false; },
    .capture_camera = internal::CaptureCamera,
    .scene_draw = internal::SceneDraw,
};

// Before the tiled lighting's dispatch (its bindings set): the pixels' world positions and the sun to the helper, then
// this frame's trace; true: traced.
inline bool Trace(ID3D11DeviceContext* context) {
  auto& state = internal::state;
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || !twattila::dlaa_bridge::bridge.ray_tracing || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
    return false;
  }
  twattila::rt_scene::Camera camera;
  {
    const std::unique_lock lock(state.mutex);
    if (!state.camera_found) return false;
    camera = state.camera;
  }
  ComPtr<ID3D11ShaderResourceView> depth;
  ComPtr<ID3D11Buffer> per_batch;
  context->CSGetShaderResources(0, 1, depth.GetAddressOf());
  context->CSGetConstantBuffers(0, 1, per_batch.GetAddressOf());
  if (!depth || !per_batch) return false;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (state.device != device) {
    ComPtr<ID3D11Device1> device1;
    const auto feature_level = device->GetFeatureLevel();
    state.device = device;
    state.private_state.Reset();
    state.positions.Reset();
    state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(0, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                               state.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__0x52C5E004.data(), __0x52C5E004.size(), nullptr, state.positions.GetAddressOf()));
  }
  if (state.failed) return false;
  auto& bridge = twattila::dlaa_bridge::bridge;
  {
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    ID3D11ShaderResourceView* inputs[] = {depth.Get()};
    ID3D11UnorderedAccessView* outputs[] = {bridge.rt_positions.uav.Get(), bridge.rt_sun.uav.Get()};
    ID3D11Buffer* constants[] = {per_batch.Get()};
    context->CSSetShader(state.positions.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, inputs);
    context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
    context->CSSetConstantBuffers(0, 1, constants);
    context->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
  }
  return twattila::rt_scene::Trace(context, nullptr, camera);
}

inline void Install() {
  rysesonofrome::dlss::constants::Use(DLL_PROCESS_ATTACH);
  twattila::rt_scene::InstallScene({}, HOST);
}

inline void Uninstall() {
  twattila::rt_scene::UninstallScene();
  rysesonofrome::dlss::constants::Use(DLL_PROCESS_DETACH);
}

// The game's device is going away: the addon's objects on it with it.
inline void Release() {
  auto& state = internal::state;
  state.device.Reset();
  state.private_state.Reset();
  state.positions.Reset();
  state.failed = false;
  twattila::dlaa_bridge::Release();
}

}  // namespace rysesonofrome::ray_tracing
