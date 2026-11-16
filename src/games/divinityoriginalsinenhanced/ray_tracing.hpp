/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>

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
// every upload). At DownsampleDepth (0xEB741AE5, SSAO's half-size depth from gBuffer1: after the G-buffer, before SSAO and
// the sun's shadow casters) or, where it isn't drawn, at the sun's shadow filter (CascadedPCSS 0x8F114E47 at ShadowQuality
// 0, CascadedUnfiltered 0x7697084B, CascadedVSM 0x1C0323E3) or the sun's light pass (0xCCCB7253): the pixels' world
// positions and the sun (d1_rt_inputs, b13 PerFrame bound there) and the trace; the G-buffer guides when Ray
// Reconstruction or the paths want them (d1_rt_guides, at the sun pass). Ray-Traced Shadows replace the
// filter's mask (the filter left out; d1_rt_shadow): the sun pass and the forward materials that read it get the rays
// alone. Ray-Traced AO is drawn over the SSAO chain's result at the sun pass (its Base, t5; d1_rt_ambient): the sun pass
// and the forward materials (t3 in R1's trace) read the rays' AO alone. Global Illumination (the rays' One Bounce or the
// paths' ReSTIR GI; Expeditions' route, as Divinity 2's): the scene before DeferredFog (0xEDA7F8A1, its t1: the sun and
// every local light in) prepared, traced, resolved over frames and added to it times each pixel's diffuse colour
// (d1_rt_bounce_apply). Path-Traced Ambient (Divinity 2's route for a host without rt_core's ambient cube): the sun pass's
// sky and ground ambient into the paths' sky (d1_rt_sky: the bridge's sun texels 3-8); once the paths have learned the
// game's light, the sun pass draws with PerFrame's ambient zeroed and SSAO's result open (1): the paths bring that light
// and its occlusion. The scene's shaders are kept from the game's start: Install only when ray tracing is on at launch.
namespace divinity::ray_tracing {

using Microsoft::WRL::ComPtr;

// Global Illumination's strength (the graphics page's light_bounce_strength; 0: off): the rays' light bounce (One Bounce)
// or the paths' (ReSTIR GI), and with them Path-Traced Ambient.
static float light_bounce_strength = 0.f;
// Skip the Game's Shadow Maps (Experimental, off): under Ray-Traced Shadows, this frame traced before the casters (at
// DownsampleDepth), nothing is drawn into the sun's shadow atlas (R1 T1: 705 draws, 0.331 ms of GPU); its only reader
// there is the shadow filter the rays replace.
static float skip_shadow_maps = 0.f;

// The sun's shadow filters (one a ShadowQuality: PCSS, unfiltered, VSM), each writing the RGBA8 mask the sun pass reads.
constexpr std::array<uint32_t, 3> SHADOW_FILTER_SHADERS = {0x8F114E47, 0x7697084B, 0x1C0323E3};
constexpr uint32_t SUN_SHADER = 0xCCCB7253;
// DownsampleDepth: gBuffer1 at t0, the G-buffer complete (R1 T1: its 256 draws all before it, none after).
constexpr uint32_t DOWNSAMPLE_DEPTH_SHADER = 0xEB741AE5;
// DeferredSSAO and DeferredSSAOBlurH, then DeferredSSAOBlurV, which writes the half-size texture the sun pass (t5) and the
// forward surfaces read (R1 T1: no reader between it and the sun pass).
constexpr std::array<uint32_t, 2> SSAO_SHADERS = {0x385DCF3C, 0x0524EF64};
constexpr uint32_t SSAO_BLUR_SHADER = 0xB887004F;
// DeferredFog: its t1 the scene lit by the sun and every local light (R1 T1: after the point and spot lights, before the
// forward surfaces).
constexpr uint32_t FOG_SHADER = 0xEDA7F8A1;

namespace internal {

// The most frames the bounce's resolve averages (Expeditions' value).
constexpr float BOUNCE_FRAMES = 24.f;
// b13 PerFrame (R1: 128 bytes; the sun pass reads its first 4 registers).
constexpr UINT PER_FRAME_BYTES = 128;

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

// The light bounce's textures (the game's thread).
struct Bounce {
  std::array<UINT, 2> size = {};
  twattila::dlaa_bridge::Texture prepared;
  ComPtr<ID3D11RenderTargetView> prepared_rtv;
  twattila::dlaa_bridge::Texture traced;
  std::array<twattila::dlaa_bridge::Texture, 2> resolved;
  std::array<twattila::dlaa_bridge::Texture, 2> resolved_positions;
  uint32_t resolved_index = 0;
  uint64_t resolved_frame = UINT64_MAX;
  std::array<float, 16> previous_view_projection = {};
  // The scene (DeferredFog's t1) and its render target (a new one when the game's texture changes).
  ComPtr<ID3D11Resource> scene;
  ComPtr<ID3D11RenderTargetView> scene_target;
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
  // The G-buffer the sun pass read (gBuffer1 linear depth, gBuffer2 normals, gBuffer3 diffuse), the bounce's at the fog.
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer;
  uint64_t gbuffer_frame = UINT64_MAX;
  // The game's b13 while the sun pass draws with PerFrame without its ambient (Path-Traced Ambient).
  ComPtr<ID3D11Buffer> kept_frame;
  Bounce bounce;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> inputs;
  ComPtr<ID3D11ComputeShader> guides;
  ComPtr<ID3D11ComputeShader> sky;
  ComPtr<ID3D11VertexShader> fullscreen;
  ComPtr<ID3D11PixelShader> shadow;
  ComPtr<ID3D11PixelShader> ambient;
  ComPtr<ID3D11PixelShader> bounce_prepare;
  ComPtr<ID3D11ComputeShader> bounce_resolve;
  ComPtr<ID3D11PixelShader> bounce_apply;
  ComPtr<ID3D11SamplerState> linear_sampler;
  ComPtr<ID3D11BlendState> add;
  ComPtr<ID3D11Buffer> input_constants;
  ComPtr<ID3D11Buffer> guide_constants;
  ComPtr<ID3D11Buffer> ambient_constants;
  ComPtr<ID3D11Buffer> sky_settings;
  ComPtr<ID3D11Buffer> bounce_constants;  // 96 bytes: the resolve's (the most), the prepare's and the apply's
  // d1_rt_sky's PerFrame without the game's ambient (8 x float4) and the same as a constant buffer.
  ComPtr<ID3D11Buffer> without_ambient;
  ComPtr<ID3D11UnorderedAccessView> without_ambient_uav;
  ComPtr<ID3D11Buffer> frame_without_ambient;
  bool failed = false;
};
static State state;
// The sun's shadow atlas (the shadow filter's t1; R1: R32F 2048 x 4096, its casters' render target), learned at the
// filter's draw and held so its address stays its own; the draws while it is the bound render target are skipped under
// Skip the Game's Shadow Maps.
static ComPtr<ID3D11Resource> shadow_atlas;
static std::atomic<uintptr_t> shadow_atlas_address = 0;
static std::atomic_bool skipping_casters = false;

// Render targets bound: the sun's shadow atlas under Skip the Game's Shadow Maps with this frame's traced sun skips the
// draws into it until the next bind.
inline void OnBindTargets(reshade::api::command_list*, uint32_t count, const reshade::api::resource_view* targets, reshade::api::resource_view) {
  bool skip = false;
  const uintptr_t atlas = shadow_atlas_address.load(std::memory_order_relaxed);
  if (skip_shadow_maps != 0.f && atlas != 0 && count != 0 && targets[0].handle != 0) {
    ComPtr<ID3D11Resource> resource;
    reinterpret_cast<ID3D11View*>(targets[0].handle)->GetResource(resource.GetAddressOf());
    if (reinterpret_cast<uintptr_t>(resource.Get()) == atlas) {
      bool sun_shadows = false;
      {
        const std::unique_lock lock(state.mutex);
        sun_shadows = state.settings.sun_shadows;
      }
      skip = sun_shadows && twattila::rt_scene::Visibilities()[0] != nullptr;
    }
  }
  skipping_casters.store(skip, std::memory_order_relaxed);
}

inline bool OnCasterDraw(reshade::api::command_list*, uint32_t, uint32_t, uint32_t, uint32_t) {
  return skipping_casters.load(std::memory_order_relaxed);
}

inline bool OnCasterDrawIndexed(reshade::api::command_list*, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  return skipping_casters.load(std::memory_order_relaxed);
}

inline bool OnCasterDrawIndirect(reshade::api::command_list*, reshade::api::indirect_command type, reshade::api::resource, uint64_t, uint32_t,
                                 uint32_t) {
  return (type == reshade::api::indirect_command::draw || type == reshade::api::indirect_command::draw_indexed)
         && skipping_casters.load(std::memory_order_relaxed);
}

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
  const D3D11_BUFFER_DESC bounce_desc = {.ByteWidth = 96, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC frame_desc = {.ByteWidth = PER_FRAME_BYTES, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC without_desc = {.ByteWidth = PER_FRAME_BYTES, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  const D3D11_UNORDERED_ACCESS_VIEW_DESC without_view = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                         .Buffer = {.FirstElement = 0, .NumElements = PER_FRAME_BYTES / 16}};
  D3D11_BLEND_DESC add_desc = {};
  add_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ONE, .DestBlend = D3D11_BLEND_ONE, .BlendOp = D3D11_BLEND_OP_ADD,
                              .SrcBlendAlpha = D3D11_BLEND_ZERO, .DestBlendAlpha = D3D11_BLEND_ONE, .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                              .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE};
  state.failed = !talesofarise::dlss::MakeContextState(device, &state.private_state)
                 || FAILED(device->CreateComputeShader(__d1_rt_inputs.data(), __d1_rt_inputs.size(), nullptr, state.inputs.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateComputeShader(__d1_rt_guides.data(), __d1_rt_guides.size(), nullptr, state.guides.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateComputeShader(__d1_rt_sky.data(), __d1_rt_sky.size(), nullptr, state.sky.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, state.fullscreen.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__d1_rt_shadow.data(), __d1_rt_shadow.size(), nullptr, state.shadow.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__d1_rt_ambient.data(), __d1_rt_ambient.size(), nullptr, state.ambient.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__rt_bounce_prepare.data(), __rt_bounce_prepare.size(), nullptr, state.bounce_prepare.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateComputeShader(__rt_bounce_resolve.data(), __rt_bounce_resolve.size(), nullptr, state.bounce_resolve.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__d1_rt_bounce_apply.data(), __d1_rt_bounce_apply.size(), nullptr, state.bounce_apply.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateSamplerState(&sampler_desc, state.linear_sampler.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBlendState(&add_desc, state.add.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&input_desc, nullptr, state.input_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&guide_desc, nullptr, state.guide_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&ambient_desc, nullptr, state.ambient_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&ambient_desc, nullptr, state.sky_settings.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&bounce_desc, nullptr, state.bounce_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&without_desc, nullptr, state.without_ambient.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateUnorderedAccessView(state.without_ambient.Get(), &without_view, state.without_ambient_uav.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&frame_desc, nullptr, state.frame_without_ambient.ReleaseAndGetAddressOf()));
  if (state.failed) renodx::utils::log::w("[Divinity] Ray tracing: its shaders or states could not be made: off.");
  return !state.failed;
}

// A full-screen draw of `pixel_shader` into `target` (its whole `size`; `blend` null: none) with `views` at t0 on, in the
// add-on's own context state.
inline void FullScreen(ID3D11DeviceContext1* context, ID3D11RenderTargetView* target, std::array<UINT, 2> size, ID3D11PixelShader* pixel_shader,
                       std::span<ID3D11ShaderResourceView* const> views, ID3D11Buffer* constants, ID3D11BlendState* blend = nullptr) {
  const talesofarise::dlss::ContextStateScope scope(context, state.private_state.Get());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(size[0]), .Height = static_cast<float>(size[1]), .MaxDepth = 1.f};
  context->OMSetRenderTargets(1, &target, nullptr);
  context->OMSetBlendState(blend, nullptr, 0xFFFFFFFF);
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(state.fullscreen.Get(), nullptr, 0);
  context->PSSetShader(pixel_shader, nullptr, 0);
  context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  context->PSSetSamplers(0, 1, state.linear_sampler.GetAddressOf());
  if (constants != nullptr) context->PSSetConstantBuffers(0, 1, &constants);
  context->Draw(3, 0);
}

// A texture of `format` at `size`, read by views, written by a render target (`rtv`) or unordered access.
inline bool MakeTexture(ID3D11Device* device, std::array<UINT, 2> size, DXGI_FORMAT format, twattila::dlaa_bridge::Texture* texture,
                        ComPtr<ID3D11RenderTargetView>* rtv = nullptr) {
  *texture = {};
  const D3D11_TEXTURE2D_DESC desc = {.Width = size[0], .Height = size[1], .MipLevels = 1, .ArraySize = 1, .Format = format, .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE | (rtv != nullptr ? D3D11_BIND_RENDER_TARGET : D3D11_BIND_UNORDERED_ACCESS))};
  return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->texture.GetAddressOf()))
         && SUCCEEDED(device->CreateShaderResourceView(texture->texture.Get(), nullptr, texture->srv.GetAddressOf()))
         && (rtv != nullptr ? SUCCEEDED(device->CreateRenderTargetView(texture->texture.Get(), nullptr, rtv->ReleaseAndGetAddressOf()))
                            : SUCCEEDED(device->CreateUnorderedAccessView(texture->texture.Get(), nullptr, texture->uav.GetAddressOf())));
}

// The paths bring the sky this frame: Path-Traced Ambient on, the last light bounce's paths had learned the game's light
// (rt_core's path_ambient_on) and this frame was traced (at DownsampleDepth, before SSAO and the sun pass).
inline bool PathsBringSky() {
  {
    const std::unique_lock lock(state.mutex);
    if (!state.settings.path_ambient) return false;
  }
  {
    auto& runtime = twattila::rt_scene::internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    if (!runtime.path_ambient_on) return false;
  }
  return twattila::rt_scene::Visibilities()[0] != nullptr;
}

// A draw's render target if it is half the G-buffer's size (SSAO's), else null.
inline ComPtr<ID3D11RenderTargetView> HalfSizeTarget(ID3D11DeviceContext* context) {
  ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (!target) return nullptr;
  ComPtr<ID3D11Resource> resource;
  target->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC desc = {};
  if (FAILED(resource.As(&texture))) return nullptr;
  texture->GetDesc(&desc);
  std::array<UINT, 2> size;
  {
    const std::unique_lock lock(state.mutex);
    size = state.render_size;
  }
  const auto half = [](UINT part, UINT whole) { return part * 2 == whole || part * 2 == whole + 1; };
  return (half(desc.Width, size[0]) && half(desc.Height, size[1])) ? target : nullptr;
}

// The game's immediate context of a draw's `cmd_list` and its ID3D11DeviceContext1; false: none.
inline bool NativeContext(reshade::api::command_list* cmd_list, ID3D11DeviceContext** context, ComPtr<ID3D11DeviceContext1>* context1) {
  *context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(context);
  return *context != nullptr && SUCCEEDED((*context)->QueryInterface(IID_PPV_ARGS(context1->ReleaseAndGetAddressOf())));
}

// This frame's trace (once a frame), at a pass that binds the game's linear depth (gBuffer1: R32F at the G-buffer's size)
// at `depth_slot` and PerFrame at b13: the positions and the sun (d1_rt_inputs), then rt_core's Trace.
inline void Trace(ID3D11DeviceContext* context, ID3D11DeviceContext1* context1, UINT depth_slot) {
  if (!twattila::dlaa_bridge::bridge.ray_tracing) return;
  upscaling::Camera camera;
  std::array<float, 2> jitter_ndc;
  if (!upscaling::MainCamera(&camera, &jitter_ndc)) return;
  std::array<UINT, 2> size;
  {
    const std::unique_lock lock(state.mutex);
    if (state.traced_frame == state.frame) return;
    size = state.render_size;
  }
  ComPtr<ID3D11ShaderResourceView> depth;
  ComPtr<ID3D11Buffer> per_frame;
  context->PSGetShaderResources(depth_slot, 1, depth.GetAddressOf());
  context->PSGetConstantBuffers(13, 1, per_frame.GetAddressOf());
  if (!depth || !per_frame || size[0] == 0) return;
  D3D11_SHADER_RESOURCE_VIEW_DESC depth_view_desc = {};
  depth->GetDesc(&depth_view_desc);
  ComPtr<ID3D11Resource> depth_resource;
  depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  if (FAILED(depth_resource.As(&depth_texture))) return;
  depth_texture->GetDesc(&depth_desc);
  if (depth_view_desc.Format != DXGI_FORMAT_R32_FLOAT || depth_desc.Width != size[0] || depth_desc.Height != size[1]) return;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (!PrepareDevice(device.Get())) return;
  {
    const std::unique_lock lock(state.mutex);
    state.traced_frame = state.frame;
  }
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

// DownsampleDepth: this frame's trace, before SSAO and the sun's shadow casters.
constexpr auto OnDownsampleDepth = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  ID3D11DeviceContext* context = nullptr;
  ComPtr<ID3D11DeviceContext1> context1;
  if (NativeContext(command.cmd_list, &context, &context1)) Trace(context, context1.Get(), 0);
  return {};
};

// The sun's shadow filter: this frame's trace (if not yet), then the traced sun into its mask in its place (the filter
// left out).
constexpr auto OnShadowFilter = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  ID3D11DeviceContext* context = nullptr;
  ComPtr<ID3D11DeviceContext1> context1;
  if (!NativeContext(command.cmd_list, &context, &context1)) return {};
  // The atlas it reads (t1), for Skip the Game's Shadow Maps from the next frame on.
  {
    ComPtr<ID3D11ShaderResourceView> atlas_view;
    context->PSGetShaderResources(1, 1, atlas_view.GetAddressOf());
    ComPtr<ID3D11Resource> atlas;
    if (atlas_view) atlas_view->GetResource(atlas.GetAddressOf());
    if (atlas && reinterpret_cast<uintptr_t>(atlas.Get()) != shadow_atlas_address.load(std::memory_order_relaxed)) {
      shadow_atlas = atlas;
      shadow_atlas_address.store(reinterpret_cast<uintptr_t>(atlas.Get()), std::memory_order_relaxed);
    }
  }
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
  FullScreen(context1.Get(), target.Get(), size, state.shadow.Get(), std::span(&sun, 1), nullptr);
  return {.bypass = true};
};

// The sun's light pass: this frame's trace if not yet (DownsampleDepth and the shadow filter not drawn); the G-buffer
// guides (its t1-t4) when Ray Reconstruction or the paths want them (read at the tone mapper and the bounce); with Global
// Illumination its G-buffer kept for the bounce and Path-Traced Ambient's sky (d1_rt_sky), this draw without the game's
// ambient while the paths bring the sky (PerFrame's copy at b13, the game's bound again after the draw); the traced
// ambient over the SSAO chain's result it reads (t5; only while the game's SSAO is on: its shaderProperties.x samples it).
constexpr auto OnSunLight = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  ID3D11DeviceContext* context = nullptr;
  ComPtr<ID3D11DeviceContext1> context1;
  if (!NativeContext(command.cmd_list, &context, &context1)) return {};
  Trace(context, context1.Get(), 1);
  bool ambient_occlusion = false;
  bool light_bounce = false;
  bool path_ambient = false;
  std::array<UINT, 2> size;
  uint64_t frame = 0;
  {
    const std::unique_lock lock(state.mutex);
    ambient_occlusion = state.settings.ambient_occlusion;
    light_bounce = state.settings.light_bounce;
    path_ambient = state.settings.path_ambient;
    size = state.render_size;
    frame = state.frame;
  }
  if (size[0] == 0 || !state.private_state) return {};
  renodx::utils::command_action::CallbackResult<Context> result = {};
  auto& bridge = twattila::dlaa_bridge::bridge;
  ComPtr<ID3D11Buffer> per_frame;
  context->PSGetConstantBuffers(13, 1, per_frame.GetAddressOf());
  D3D11_BUFFER_DESC per_frame_desc = {};
  if (per_frame) per_frame->GetDesc(&per_frame_desc);
  if (light_bounce && per_frame_desc.ByteWidth >= PER_FRAME_BYTES && bridge.rt_sun.uav) {
    {
      const std::unique_lock lock(state.mutex);
      for (UINT index = 0; index < state.gbuffer.size(); ++index) context->PSGetShaderResources(1 + index, 1, state.gbuffer[index].ReleaseAndGetAddressOf());
      state.gbuffer_frame = frame;
    }
    const bool paths_bring_sky = PathsBringSky();
    const std::array<float, 4> sky_settings = {path_ambient ? 1.f : 0.f, 0.f, 0.f, 0.f};
    twattila::dlaa_bridge::BeforeSharedWrites(context);
    {
      const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
      context->UpdateSubresource(state.sky_settings.Get(), 0, nullptr, sky_settings.data(), 0, 0);
      ID3D11Buffer* constants[] = {per_frame.Get(), state.sky_settings.Get()};
      ID3D11UnorderedAccessView* written[] = {bridge.rt_sun.uav.Get(), state.without_ambient_uav.Get()};
      context->CSSetShader(state.sky.Get(), nullptr, 0);
      context->CSSetConstantBuffers(0, 2, constants);
      context->CSSetUnorderedAccessViews(0, 2, written, nullptr);
      context->Dispatch(1, 1, 1);
    }
    if (paths_bring_sky) {
      context->CopyResource(state.frame_without_ambient.Get(), state.without_ambient.Get());
      state.kept_frame = per_frame;
      context->PSSetConstantBuffers(13, 1, state.frame_without_ambient.GetAddressOf());
      result.post_callback = [](Context& drawn, const void*) {
        ID3D11DeviceContext* drawn_context = nullptr;
        ComPtr<ID3D11DeviceContext1> drawn_context1;
        if (NativeContext(drawn.cmd_list, &drawn_context, &drawn_context1)) drawn_context->PSSetConstantBuffers(13, 1, state.kept_frame.GetAddressOf());
        state.kept_frame.Reset();
      };
    }
  }
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
  if (!ambient_occlusion || ambient == nullptr || !occlusion_view) return result;
  ComPtr<ID3D11Resource> resource;
  occlusion_view->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> occlusion;
  D3D11_TEXTURE2D_DESC desc = {};
  if (FAILED(resource.As(&occlusion))) return result;
  occlusion->GetDesc(&desc);
  if ((desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 || desc.Width == 0 || desc.Height == 0) return result;
  // One view kept: the chain's result is the same texture every frame (a new one after a resize lets the old go).
  if (state.occlusion != resource) {
    state.occlusion = resource;
    state.occlusion_target.Reset();
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    const D3D11_RENDER_TARGET_VIEW_DESC target_desc = {.Format = desc.Format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
    if (FAILED(device->CreateRenderTargetView(resource.Get(), &target_desc, state.occlusion_target.GetAddressOf()))) {
      state.occlusion.Reset();
      return result;
    }
  }
  // The ambient texture is the bridge's size, the render at its top-left.
  const std::array<float, 4> texel = {static_cast<float>(size[0]) / static_cast<float>(desc.Width) / static_cast<float>(bridge.width),
                                      static_cast<float>(size[1]) / static_cast<float>(desc.Height) / static_cast<float>(bridge.height), 0.f, 0.f};
  context->UpdateSubresource(state.ambient_constants.Get(), 0, nullptr, texel.data(), 0, 0);
  FullScreen(context1.Get(), state.occlusion_target.Get(), {desc.Width, desc.Height}, state.ambient.Get(), std::span(&ambient, 1),
             state.ambient_constants.Get());
  return result;
};

// DeferredSSAO and its first blur (before them): not drawn while the paths bring the sky (true: skipped).
constexpr auto OnSsao = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  ID3D11DeviceContext* context = nullptr;
  ComPtr<ID3D11DeviceContext1> context1;
  if (!NativeContext(command.cmd_list, &context, &context1) || !HalfSizeTarget(context)) return {};
  return {.bypass = PathsBringSky()};
};

// DeferredSSAOBlurV (before it): while the paths bring the sky its target open (1: their blocked rays are the
// occlusion), the blur skipped.
constexpr auto OnSsaoBlur = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  ID3D11DeviceContext* context = nullptr;
  ComPtr<ID3D11DeviceContext1> context1;
  if (!NativeContext(command.cmd_list, &context, &context1)) return {};
  const ComPtr<ID3D11RenderTargetView> target = HalfSizeTarget(context);
  if (!target || !PathsBringSky()) return {};
  constexpr std::array<float, 4> OPEN = {1.f, 1.f, 1.f, 1.f};
  context->ClearRenderTargetView(target.Get(), OPEN.data());
  return {.bypass = true};
};

// DeferredFog (before its draw; t1 the scene lit by the sun and every local light): the light bounce from it
// (Expeditions' route: prepared, traced by the rays or the paths, resolved over frames) added to it, times each pixel's
// diffuse colour and the strength.
constexpr auto OnFog = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  ID3D11DeviceContext* context = nullptr;
  ComPtr<ID3D11DeviceContext1> context1;
  if (!NativeContext(command.cmd_list, &context, &context1)) return {};
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  std::array<UINT, 2> render_size;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer;
  {
    const std::unique_lock lock(state.mutex);
    settings = state.settings;
    frame = state.frame;
    render_size = state.render_size;
    if (state.gbuffer_frame == frame) gbuffer = state.gbuffer;
  }
  upscaling::Camera camera;
  std::array<float, 2> jitter_ndc;
  ComPtr<ID3D11ShaderResourceView> scene_view;
  context->PSGetShaderResources(1, 1, scene_view.GetAddressOf());
  if (!settings.light_bounce || light_bounce_strength <= 0.f || !gbuffer[0] || !gbuffer[1] || !gbuffer[2] || !scene_view || state.failed
      || !state.bounce_apply || !upscaling::MainCamera(&camera, &jitter_ndc)) {
    return {};
  }
  ComPtr<ID3D11Resource> scene;
  scene_view->GetResource(scene.GetAddressOf());
  ComPtr<ID3D11Texture2D> scene_texture;
  D3D11_TEXTURE2D_DESC scene_desc = {};
  if (FAILED(scene.As(&scene_texture))) return {};
  scene_texture->GetDesc(&scene_desc);
  if (scene_desc.Width != render_size[0] || scene_desc.Height != render_size[1] || (scene_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) return {};
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  auto& bounce = state.bounce;
  const UINT scale = 1u << std::min(static_cast<UINT>(settings.light_bounce_resolution), 2u);
  const std::array<UINT, 2> size = {(render_size[0] + scale - 1) / scale, (render_size[1] + scale - 1) / scale};
  // The bounce's textures at the render size's and the resolution's (made again when either changes).
  if (bounce.size != size) {
    bounce.size = size;
    bounce.resolved_frame = UINT64_MAX;
    if (!MakeTexture(device.Get(), size, DXGI_FORMAT_R11G11B10_FLOAT, &bounce.prepared, &bounce.prepared_rtv)
        || !MakeTexture(device.Get(), size, DXGI_FORMAT_R16G16B16A16_FLOAT, &bounce.traced)
        || !std::ranges::all_of(bounce.resolved, [&](auto& texture) { return MakeTexture(device.Get(), size, DXGI_FORMAT_R16G16B16A16_FLOAT, &texture); })
        || !std::ranges::all_of(bounce.resolved_positions,
                                [&](auto& texture) { return MakeTexture(device.Get(), size, DXGI_FORMAT_R32G32B32A32_FLOAT, &texture); })) {
      bounce.size = {};
      return {};
    }
  }
  if (bounce.scene != scene) {
    bounce.scene = scene;
    if (FAILED(device->CreateRenderTargetView(scene.Get(), nullptr, bounce.scene_target.ReleaseAndGetAddressOf()))) {
      bounce.scene.Reset();
      return {};
    }
  }
  // The prepare's, the resolve's and the apply's constants (the buffer's whole 96 bytes: a constant buffer is written whole).
  struct Constants {
    std::array<uint32_t, 4> sizes;
    std::array<float, 4> settings;
    std::array<float, 16> rows;
  };
  const std::array<uint32_t, 4> sizes = {size[0], size[1], render_size[0], render_size[1]};
  const Constants prepare_constants = {.sizes = sizes, .settings = {0.f, static_cast<float>(scale)}};
  context->UpdateSubresource(state.bounce_constants.Get(), 0, nullptr, &prepare_constants, 0, 0);
  ID3D11ShaderResourceView* scene_srv = scene_view.Get();
  FullScreen(context1.Get(), bounce.prepared_rtv.Get(), size, state.bounce_prepare.Get(), std::span(&scene_srv, 1), state.bounce_constants.Get());
  if (!twattila::rt_scene::internal::TraceLightBounce(context, bounce.prepared.texture.Get(), size[0], size[1], scale, bounce.traced.texture.Get())) {
    return {};
  }
  auto& bridge = twattila::dlaa_bridge::bridge;
  const uint32_t previous = bounce.resolved_index;
  const uint32_t current = previous ^ 1u;
  {
    // The resolve's: last frame's camera (rows acting on column vectors, without the jitter), the sizes, its settings.
    struct ResolveConstants {
      std::array<float, 16> previous_view_projection;
      std::array<uint32_t, 4> sizes;
      std::array<float, 4> settings;
    };
    const ResolveConstants constants = {
        .previous_view_projection = bounce.previous_view_projection,
        .sizes = sizes,
        .settings = {BOUNCE_FRAMES, (bounce.resolved_frame + 1 == frame) ? 1.f : 0.f, static_cast<float>(scale)},
    };
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    context->UpdateSubresource(state.bounce_constants.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11ShaderResourceView* views[] = {bounce.traced.srv.Get(), bridge.rt_positions.srv.Get(), bounce.resolved[previous].srv.Get(),
                                         bounce.resolved_positions[previous].srv.Get()};
    ID3D11UnorderedAccessView* outputs[] = {bounce.resolved[current].uav.Get(), bounce.resolved_positions[current].uav.Get()};
    context->CSSetShader(state.bounce_resolve.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 4, views);
    context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
    context->CSSetConstantBuffers(0, 1, state.bounce_constants.GetAddressOf());
    context->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
  }
  bounce.resolved_index = current;
  bounce.resolved_frame = frame;
  bounce.previous_view_projection = camera.view_projection;
  const Constants apply_constants = {
      .sizes = sizes,
      .settings = {light_bounce_strength, static_cast<float>(scale)},
      .rows = {camera.view[0], camera.view[1], camera.view[2], 0.f, camera.view[4], camera.view[5], camera.view[6], 0.f, camera.view[8],
               camera.view[9], camera.view[10], 0.f},
  };
  context->UpdateSubresource(state.bounce_constants.Get(), 0, nullptr, &apply_constants, 0, 0);
  const std::array<ID3D11ShaderResourceView*, 6> views = {bounce.resolved[current].srv.Get(), bounce.resolved_positions[current].srv.Get(),
                                                          bridge.rt_positions.srv.Get(),       gbuffer[1].Get(),
                                                          gbuffer[2].Get(),                    gbuffer[0].Get()};
  FullScreen(context1.Get(), bounce.scene_target.Get(), render_size, state.bounce_apply.Get(), views, state.bounce_constants.Get(), state.add.Get());
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
  renodx::utils::command_action::Register(internal::OnDownsampleDepth, {.shader_hash = DOWNSAMPLE_DEPTH_SHADER,
                                                                        .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  for (const uint32_t shader_hash : SHADOW_FILTER_SHADERS) {
    renodx::utils::command_action::Register(internal::OnShadowFilter, {.shader_hash = shader_hash,
                                                                       .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  renodx::utils::command_action::Register(internal::OnSunLight, {.shader_hash = SUN_SHADER,
                                                                 .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  for (const uint32_t shader_hash : SSAO_SHADERS) {
    renodx::utils::command_action::Register(internal::OnSsao, {.shader_hash = shader_hash,
                                                               .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  renodx::utils::command_action::Register(internal::OnSsaoBlur, {.shader_hash = SSAO_BLUR_SHADER,
                                                                 .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  renodx::utils::command_action::Register(internal::OnFog, {.shader_hash = FOG_SHADER,
                                                            .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(internal::OnBindTargets);
  reshade::register_event<reshade::addon_event::draw>(internal::OnCasterDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(internal::OnCasterDrawIndexed);
  reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(internal::OnCasterDrawIndirect);
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnDownsampleDepth);
  renodx::utils::command_action::Unregister(internal::OnShadowFilter);
  renodx::utils::command_action::Unregister(internal::OnSunLight);
  renodx::utils::command_action::Unregister(internal::OnSsao);
  renodx::utils::command_action::Unregister(internal::OnSsaoBlur);
  renodx::utils::command_action::Unregister(internal::OnFog);
  reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(internal::OnBindTargets);
  reshade::unregister_event<reshade::addon_event::draw>(internal::OnCasterDraw);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(internal::OnCasterDrawIndexed);
  reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(internal::OnCasterDrawIndirect);
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
    state.sky.Reset();
    state.fullscreen.Reset();
    state.shadow.Reset();
    state.ambient.Reset();
    state.bounce_prepare.Reset();
    state.bounce_resolve.Reset();
    state.bounce_apply.Reset();
    state.linear_sampler.Reset();
    state.add.Reset();
    state.input_constants.Reset();
    state.guide_constants.Reset();
    state.ambient_constants.Reset();
    state.sky_settings.Reset();
    state.bounce_constants.Reset();
    state.without_ambient.Reset();
    state.without_ambient_uav.Reset();
    state.frame_without_ambient.Reset();
    state.occlusion.Reset();
    state.occlusion_target.Reset();
    state.gbuffer = {};
    state.kept_frame.Reset();
    state.bounce = {};
    state.failed = false;
    internal::shadow_atlas_address.store(0, std::memory_order_relaxed);
    internal::shadow_atlas.Reset();
  }
  twattila::rt_scene::OnDestroyDevice(device);
}

}  // namespace divinity::ray_tracing
