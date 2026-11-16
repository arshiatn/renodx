/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/upscaling.hpp"
#include "../twattila/rt_core.hpp"
#include "./upscaling.hpp"

// Ray tracing (Experimental): Divinity's scene through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe
// next to the add-on, its own D3D12 device with DXR), a clip-space host as Ryse's: every opaque draw into the G-buffer
// (five targets, gBuffer1 R32F linear depth at rt1, the depth written; the 494 _D_ material pixel shaders are too many
// to list) streamed out through its own vertex shader (skinned ones, BLENDINDICES, every frame; alpha-tested _AT_ ones by
// rt_core's SceneCoverage with their UV), their camera the b12 PerView they drew with as uploaded (upscaling.hpp keeps
// every upload). At the sun's shadow filter (CascadedPCSS 0x8F114E47 at ShadowQuality 0, CascadedUnfiltered 0x7697084B,
// CascadedVSM 0x1C0323E3: after the G-buffer, SSAO and the shadow casters) or, with the game's shadows off, at the sun's
// light pass (0xCCCB7253): the pixels' world positions and the sun (d1_rt_inputs, b13 PerFrame bound there), the G-buffer
// guides when Ray Reconstruction or the paths want them (d1_rt_guides), and the trace. Ray-Traced Shadows replace the
// filter's mask (the filter left out; d1_rt_shadow): the sun pass and the forward materials that read it get the rays
// alone. Ray-Traced AO is drawn over the SSAO chain's result at the sun pass (its Base, t5; d1_rt_ambient): the sun pass
// and the forward materials (t3 in R1's trace) read the rays' AO alone. The scene's shaders are kept from the game's
// start: Install only when ray tracing is on at launch.
namespace divinity::ray_tracing {

using Microsoft::WRL::ComPtr;

// The sun's shadow filters (one a ShadowQuality: PCSS, unfiltered, VSM), each writing the RGBA8 mask the sun pass reads.
constexpr std::array<uint32_t, 3> SHADOW_FILTER_SHADERS = {0x8F114E47, 0x7697084B, 0x1C0323E3};
constexpr uint32_t SUN_SHADER = 0xCCCB7253;

namespace internal {

struct InputConstants {
  std::array<float, 16> inverse_view;
  std::array<float, 4> size;
  std::array<float, 4> projection;
  std::array<float, 4> jitter;
  std::array<float, 4> camera;
  std::array<float, 4> lens;
};

struct GuideConstants {
  std::array<std::array<float, 4>, 3> view;
  std::array<float, 4> size;
};

struct State {
  std::mutex mutex;
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  uint64_t traced_frame = UINT64_MAX;  // the last frame a trace was tried in
  uint64_t guides_frame = UINT64_MAX;
  std::array<UINT, 2> render_size = {};  // the G-buffer's, last seen
  // The SSAO chain's result and its view (Ray-Traced AO draws over it; the game's thread only).
  ComPtr<ID3D11Resource> occlusion;
  ComPtr<ID3D11RenderTargetView> occlusion_target;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> inputs;
  ComPtr<ID3D11ComputeShader> guides;
  ComPtr<ID3D11VertexShader> fullscreen;
  ComPtr<ID3D11PixelShader> shadow;
  ComPtr<ID3D11PixelShader> ambient;
  ComPtr<ID3D11SamplerState> linear_sampler;
  ComPtr<ID3D11Buffer> input_constants;
  ComPtr<ID3D11Buffer> guide_constants;
  ComPtr<ID3D11Buffer> ambient_constants;
  bool failed = false;
};
static State state;

// A G-buffer draw: five targets, gBuffer1 (R32F linear depth) at rt1, the depth written, rt0 unblended; its pixel shader's
// alpha test (rt_core's SceneCoverage), its vertex shader skinned or not.
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  std::array<ID3D11RenderTargetView*, 6> targets = {};
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth_view.GetAddressOf());
  D3D11_RENDER_TARGET_VIEW_DESC depth_target_desc = {};
  if (targets[1] != nullptr) targets[1]->GetDesc(&depth_target_desc);
  const bool gbuffer = depth_view && targets[4] != nullptr && targets[5] == nullptr && depth_target_desc.Format == DXGI_FORMAT_R32_FLOAT;
  ComPtr<ID3D11Resource> depth_target;
  if (gbuffer) targets[1]->GetResource(depth_target.GetAddressOf());
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
  if (!depth_desc.DepthEnable || depth_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ALL || blend_desc.RenderTarget[0].BlendEnable) return false;
  ComPtr<ID3D11Texture2D> depth_texture;
  D3D11_TEXTURE2D_DESC depth_texture_desc = {};
  if (FAILED(depth_target.As(&depth_texture))) return false;
  depth_texture->GetDesc(&depth_texture_desc);
  {
    const std::unique_lock lock(state.mutex);
    state.render_size = {depth_texture_desc.Width, depth_texture_desc.Height};
  }
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!talesofarise::upscaling::PrepareBridge(device.Get()) || !twattila::rt_scene::SceneCoverage(native, shader)) return false;
  shader->dynamic = twattila::rt_scene::SkinnedVertexShader(shader->hash);
  return true;
}

// The draw's camera: its VS b12 (PerView) as last uploaded (the jitter in, as it drew).
inline bool CaptureCamera(ID3D11DeviceContext* native, twattila::rt_scene::Camera* camera) {
  ComPtr<ID3D11Buffer> per_view;
  native->VSGetConstantBuffers(12, 1, per_view.GetAddressOf());
  std::array<float, 16> view_projection = {};
  std::array<float, 3> position = {};
  if (!per_view || !upscaling::UploadedView(per_view.Get(), &view_projection, &position)) return false;
  *camera = {
      .view_projection = view_projection,
      .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
      .position = position,
  };
  return true;
}

// The device's own objects (once a device); false: not available.
inline bool PrepareDevice(ID3D11Device* device) {
  if (state.device.Get() == device) return !state.failed;
  state.device = device;
  const D3D11_SAMPLER_DESC sampler_desc = {
      .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
      .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
      .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
      .ComparisonFunc = D3D11_COMPARISON_NEVER,
      .MaxLOD = D3D11_FLOAT32_MAX,
  };
  const D3D11_BUFFER_DESC input_desc = {.ByteWidth = sizeof(InputConstants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC guide_desc = {.ByteWidth = sizeof(GuideConstants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC ambient_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  state.failed = !talesofarise::dlss::MakeContextState(device, &state.private_state)
                 || FAILED(device->CreateComputeShader(__d1_rt_inputs.data(), __d1_rt_inputs.size(), nullptr, state.inputs.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateComputeShader(__d1_rt_guides.data(), __d1_rt_guides.size(), nullptr, state.guides.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, state.fullscreen.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__d1_rt_shadow.data(), __d1_rt_shadow.size(), nullptr, state.shadow.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__d1_rt_ambient.data(), __d1_rt_ambient.size(), nullptr, state.ambient.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateSamplerState(&sampler_desc, state.linear_sampler.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&input_desc, nullptr, state.input_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&guide_desc, nullptr, state.guide_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&ambient_desc, nullptr, state.ambient_constants.ReleaseAndGetAddressOf()));
  if (state.failed) renodx::utils::log::w("[Divinity] Ray tracing: its shaders or states could not be made: off.");
  return !state.failed;
}

// A full-screen draw of `pixel_shader` into `target` (its whole `size`), in the add-on's own context state.
inline void FullScreen(ID3D11DeviceContext1* context, ID3D11RenderTargetView* target, std::array<UINT, 2> size, ID3D11PixelShader* pixel_shader,
                       ID3D11ShaderResourceView* view, ID3D11Buffer* constants) {
  const talesofarise::dlss::ContextStateScope scope(context, state.private_state.Get());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(size[0]), .Height = static_cast<float>(size[1]), .MaxDepth = 1.f};
  context->OMSetRenderTargets(1, &target, nullptr);
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(state.fullscreen.Get(), nullptr, 0);
  context->PSSetShader(pixel_shader, nullptr, 0);
  context->PSSetShaderResources(0, 1, &view);
  context->PSSetSamplers(0, 1, state.linear_sampler.GetAddressOf());
  if (constants != nullptr) context->PSSetConstantBuffers(0, 1, &constants);
  context->Draw(3, 0);
}

// This frame's trace (one try a frame), at a pass that binds the game's linear depth at `depth_slot` and PerFrame at b13:
// the positions and the sun (d1_rt_inputs), then rt_core's Trace.
inline void Trace(ID3D11DeviceContext* context, ID3D11DeviceContext1* context1, UINT depth_slot) {
  if (!twattila::dlaa_bridge::bridge.ray_tracing) return;
  upscaling::Camera camera;
  std::array<float, 2> jitter_ndc;
  if (!upscaling::MainCamera(&camera, &jitter_ndc)) return;
  std::array<UINT, 2> size;
  {
    const std::unique_lock lock(state.mutex);
    if (state.traced_frame == state.frame) return;
    state.traced_frame = state.frame;
    size = state.render_size;
  }
  ComPtr<ID3D11ShaderResourceView> depth;
  ComPtr<ID3D11Buffer> per_frame;
  context->PSGetShaderResources(depth_slot, 1, depth.GetAddressOf());
  context->PSGetConstantBuffers(13, 1, per_frame.GetAddressOf());
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (!depth || !per_frame || size[0] == 0 || !PrepareDevice(device.Get())) return;
  // The jittered view-projection (as the depth and the captures were drawn).
  std::array<float, 16> view_projection = camera.view_projection;
  for (size_t column = 0; column < 4; ++column) {
    view_projection[column] += jitter_ndc[0] * view_projection[12 + column];
    view_projection[4 + column] += jitter_ndc[1] * view_projection[12 + column];
  }
  const InputConstants inputs = {
      .inverse_view = talesofarise::frame_generation::Inverse(camera.view),
      .size = {static_cast<float>(size[0]), static_cast<float>(size[1]), 1.f / static_cast<float>(size[0]), 1.f / static_cast<float>(size[1])},
      .projection = {camera.projection[10], camera.projection[11], camera.info[0], 0.f},
      .jitter = {jitter_ndc[0], jitter_ndc[1], 0.f, 0.f},
      .camera = camera.position,
      .lens = {camera.projection[0], camera.projection[5], camera.projection[2], camera.projection[6]},
  };
  auto& bridge = twattila::dlaa_bridge::bridge;
  twattila::dlaa_bridge::BeforeSharedWrites(context);
  {
    const talesofarise::dlss::ContextStateScope scope(context1, state.private_state.Get());
    context->UpdateSubresource(state.input_constants.Get(), 0, nullptr, &inputs, 0, 0);
    ID3D11ShaderResourceView* views[] = {depth.Get()};
    ID3D11UnorderedAccessView* outputs[] = {bridge.rt_positions.uav.Get(), bridge.rt_sun.uav.Get()};
    ID3D11Buffer* constants[] = {state.input_constants.Get(), per_frame.Get()};
    context->CSSetShader(state.inputs.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, views);
    context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
    context->CSSetConstantBuffers(0, 2, constants);
    context->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
  }
  twattila::rt_scene::Trace(context, nullptr, {
                                                  .view_projection = view_projection,
                                                  .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
                                                  .position = {camera.position[0], camera.position[1], camera.position[2]},
                                              });
}

// The sun's shadow filter: this frame's trace, then the traced sun into its mask in its place (the filter left out).
constexpr auto OnShadowFilter = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  Trace(context, context1.Get(), 0);
  bool sun_shadows = false;
  std::array<UINT, 2> size;
  {
    const std::unique_lock lock(state.mutex);
    sun_shadows = state.settings.sun_shadows;
    size = state.render_size;
  }
  auto* sun = twattila::rt_scene::Visibilities()[0];
  ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (!sun_shadows || sun == nullptr || !target) return {};
  ComPtr<ID3D11Resource> resource;
  target->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> mask;
  D3D11_TEXTURE2D_DESC desc = {};
  if (FAILED(resource.As(&mask))) return {};
  mask->GetDesc(&desc);
  if (desc.Width != size[0] || desc.Height != size[1]) return {};
  FullScreen(context1.Get(), target.Get(), size, state.shadow.Get(), sun, nullptr);
  return {.bypass = true};
};

// The sun's light pass: this frame's trace if the shadow filter didn't run (the game's shadows off); the G-buffer guides
// (its t1-t4) when Ray Reconstruction wants them (it reads them at the tone mapper); the traced ambient over the SSAO
// chain's result it reads (t5; only while the game's SSAO is on: its shaderProperties.x samples it).
constexpr auto OnSunLight = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  Trace(context, context1.Get(), 1);
  bool ambient_occlusion = false;
  std::array<UINT, 2> size;
  uint64_t frame = 0;
  {
    const std::unique_lock lock(state.mutex);
    ambient_occlusion = state.settings.ambient_occlusion;
    size = state.render_size;
    frame = state.frame;
  }
  if (size[0] == 0 || !state.private_state) return {};
  auto& bridge = twattila::dlaa_bridge::bridge;
  upscaling::Camera camera;
  std::array<float, 2> jitter_ndc;
  if (talesofarise::upscaling::Guides() && bridge.guides && upscaling::MainCamera(&camera, &jitter_ndc)) {
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> gbuffer;
    for (UINT index = 0; index < gbuffer.size(); ++index) context->PSGetShaderResources(1 + index, 1, gbuffer[index].GetAddressOf());
    if (gbuffer[0] && gbuffer[1] && gbuffer[2] && gbuffer[3]) {
      const GuideConstants guide_constants = {
          .view = {{{camera.view[0], camera.view[1], camera.view[2], 0.f},
                    {camera.view[4], camera.view[5], camera.view[6], 0.f},
                    {camera.view[8], camera.view[9], camera.view[10], 0.f}}},
          .size = {static_cast<float>(size[0]), static_cast<float>(size[1]), 0.f, 0.f},
      };
      twattila::dlaa_bridge::BeforeSharedWrites(context);
      {
        const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
        context->UpdateSubresource(state.guide_constants.Get(), 0, nullptr, &guide_constants, 0, 0);
        ID3D11ShaderResourceView* views[] = {gbuffer[0].Get(), gbuffer[1].Get(), gbuffer[2].Get(), gbuffer[3].Get()};
        ID3D11UnorderedAccessView* written[] = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(), bridge.normals.uav.Get(),
                                                bridge.roughness.uav.Get()};
        context->CSSetShader(state.guides.Get(), nullptr, 0);
        context->CSSetShaderResources(0, 4, views);
        context->CSSetUnorderedAccessViews(0, 4, written, nullptr);
        context->CSSetConstantBuffers(0, 1, state.guide_constants.GetAddressOf());
        context->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
      }
      const std::unique_lock lock(state.mutex);
      state.guides_frame = frame;
    }
  }
  auto* ambient = twattila::rt_scene::Visibilities()[1];
  ComPtr<ID3D11ShaderResourceView> occlusion_view;
  context->PSGetShaderResources(5, 1, occlusion_view.GetAddressOf());
  if (!ambient_occlusion || ambient == nullptr || !occlusion_view) return {};
  ComPtr<ID3D11Resource> resource;
  occlusion_view->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> occlusion;
  D3D11_TEXTURE2D_DESC desc = {};
  if (FAILED(resource.As(&occlusion))) return {};
  occlusion->GetDesc(&desc);
  if ((desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 || desc.Width == 0 || desc.Height == 0) return {};
  // One view kept: the chain's result is the same texture every frame (a new one after a resize lets the old go).
  if (state.occlusion != resource) {
    state.occlusion = resource;
    state.occlusion_target.Reset();
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    const D3D11_RENDER_TARGET_VIEW_DESC target_desc = {.Format = desc.Format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
    if (FAILED(device->CreateRenderTargetView(resource.Get(), &target_desc, state.occlusion_target.GetAddressOf()))) {
      state.occlusion.Reset();
      return {};
    }
  }
  // The ambient texture is the bridge's size, the render at its top-left.
  const std::array<float, 4> texel = {static_cast<float>(size[0]) / static_cast<float>(desc.Width) / static_cast<float>(bridge.width),
                                      static_cast<float>(size[1]) / static_cast<float>(desc.Height) / static_cast<float>(bridge.height), 0.f, 0.f};
  context->UpdateSubresource(state.ambient_constants.Get(), 0, nullptr, texel.data(), 0, 0);
  FullScreen(context1.Get(), state.occlusion_target.Get(), {desc.Width, desc.Height}, state.ambient.Get(), ambient, state.ambient_constants.Get());
  return {};
};

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .render_size = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.render_size;
    },
    .guides_written = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.guides_frame == internal::state.frame;
    },
    .capture_camera = internal::CaptureCamera,
    .scene_draw = internal::SceneDraw,
    .characters_dynamic = true,
    .depth_at_render_size = true,
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

inline void Install() {
  twattila::rt_scene::InstallScene({}, HOST);
  for (const uint32_t shader_hash : SHADOW_FILTER_SHADERS) {
    renodx::utils::command_action::Register(internal::OnShadowFilter, {.shader_hash = shader_hash,
                                                                       .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  renodx::utils::command_action::Register(internal::OnSunLight, {.shader_hash = SUN_SHADER,
                                                                 .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnShadowFilter);
  renodx::utils::command_action::Unregister(internal::OnSunLight);
  twattila::rt_scene::UninstallScene();
}

// The game's device is going away: the add-on's objects on it with it (the bridge's after: upscaling.hpp).
inline void Release(reshade::api::device* device) {
  auto& state = internal::state;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (state.device.Get() == native) {
    state.device.Reset();
    state.private_state.Reset();
    state.inputs.Reset();
    state.guides.Reset();
    state.fullscreen.Reset();
    state.shadow.Reset();
    state.ambient.Reset();
    state.linear_sampler.Reset();
    state.input_constants.Reset();
    state.guide_constants.Reset();
    state.ambient_constants.Reset();
    state.occlusion.Reset();
    state.occlusion_target.Reset();
    state.failed = false;
  }
  twattila::rt_scene::OnDestroyDevice(device);
}

}  // namespace divinity::ray_tracing
