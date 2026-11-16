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
#include <memory>
#include <mutex>
#include <span>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/directx.hpp"
#include "./constant_mirror.hpp"
#include "./rt_outside_view_uv_gs.h"
#include "./shadow_skip.hpp"
#include "./upscaling.hpp"
#include "../twwarhammer3/rt_outside_view_gs.h"
#include "../talesofarise_dlss/matrix.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/rt_core.hpp"

// Ray tracing (Experimental): Ryse's scene through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe
// next to the addon, its own D3D12 device with DXR). The scene: every opaque draw into the G-buffer (three RGBA16F
// targets and the depth, depth written, unblended; too many vertex shader permutations to list; alpha-tested ones with
// their UV) streamed out through its own vertex shader (those with bone inputs every frame); the camera they were drawn with is VS PER_FRAME (b2, a
// range of a bigger buffer: rows c0-c3 the jittered view-projection, c6 the camera), read from the game's uploads
// (constant_mirror.hpp). Before the tiled lighting (cs 0x897232B5: t0 linear depth, b0 its
// PER_BATCH: the far plane's corners from the camera, the camera, the sun) each pixel's world position and the sun go
// to the helper (0x52C5E004), with the G-buffer guides when the reflections or paths want them (0x52C5E005), and the
// frame is traced. The sun's visibility then goes into the sun's shadow mask with the contact shadows, the ambient
// visibility into the lighting's directional occlusion (contact_shadows.hpp), the traced reflections take the
// screen-space ones' place at the lighting (t6; their trace, ps 0xE0B7E115, left out), the Light Bounce's trace goes
// through rays (light_bounce.hpp; path traced when on). With Path-Traced Ambient the paths' sky is the game's widest
// probe (rt_probe_sky, the sun texture's ambient cube) and, while the paths bring it, the lighting's diffuse probes (t34)
// are a black cube: its diffuse ambient comes from the probes alone (the decompile's lightType 1 loop), so the Light
// Bounce's apply adds the paths' light in its place. The game's HDR scene at the end of the frame is the helper's
// last frame's colour (KeepColor, 0x52C5E006; kept apart, the bridge's colour is the upscaler's input after it). The
// bridge (upscaling.hpp) is the upscaler's output size while the helper upscales: the rays at the render size, its
// textures' top-left. Its scene shaders' code is kept from the game's start: Install only when ray tracing is on at
// launch.
namespace rysesonofrome::ray_tracing {

using Microsoft::WRL::ComPtr;

namespace internal {

struct State {
  std::mutex mutex;
  twattila::rt_scene::Camera camera;  // the G-buffer's, last captured
  bool camera_found = false;
  twattila::rt_scene::Settings settings;  // the addon's (Update)
  uint64_t frame = 0;
  uint64_t guides_frame = UINT64_MAX;  // the frame the guides were written in
  std::array<UINT, 2> render_size = {};  // the G-buffer's depth's, last seen
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> positions;
  ComPtr<ID3D11ComputeShader> guides;
  ComPtr<ID3D11ComputeShader> color;
  // KeepColor's (RGBA16F at the render size), into the bridge's colour before the next trace; the traced reflections'
  // top-left at the render size while the bridge is bigger (the lighting samples t6 over the whole texture).
  ComPtr<ID3D11Texture2D> kept_color;
  ComPtr<ID3D11UnorderedAccessView> kept_color_uav;
  ComPtr<ID3D11Texture2D> reflections_copy;
  ComPtr<ID3D11ShaderResourceView> reflections_copy_srv;
  // Specular occlusion without the traced reflections: merged into the game's screen-space ones (rt_specular_merge).
  ComPtr<ID3D11ComputeShader> specular_merge;
  ComPtr<ID3D11SamplerState> linear_sampler;
  ComPtr<ID3D11Texture2D> merged;
  ComPtr<ID3D11UnorderedAccessView> merged_uav;
  ComPtr<ID3D11ShaderResourceView> merged_srv;
  // The lighting's screen-space reflections (t6) while the traced ones are bound in their place, and whether they were
  // at its last dispatch (the next screen-space trace is left out then).
  ComPtr<ID3D11ShaderResourceView> game_reflections;
  bool reflections_bound = false;
  bool reflecting = false;
  // Path-Traced Ambient: the sky's pass, the black cube bound at the lighting's t34 and the game's put back after it.
  ComPtr<ID3D11ComputeShader> probe_sky;
  ComPtr<ID3D11ShaderResourceView> no_probes;
  ComPtr<ID3D11ShaderResourceView> game_probes;
  bool probes_bound = false;
  // The lighting's PER_BATCH (b0) with SSDOParams.y 0 while the traced reflections or specular occlusion are bound (its
  // SSDO reflection occlusion left out), copied from the game's at each dispatch; the game's binding put back after it.
  ComPtr<ID3D11Buffer> neutral_batch;
  ComPtr<ID3D11Buffer> zero;
  ComPtr<ID3D11Buffer> game_batch;
  UINT game_batch_first = 0;
  UINT game_batch_count = 0;
  bool batch_bound = false;
  bool failed = false;
};
static State state;
// Settings::casters_out_of_view as Update last set it (SceneDraw's depth-only draws return at once without it).
static std::atomic_bool casters_out_of_view = false;
// The tiled lighting's PER_BATCH: 8 float4 (c5 SSDOParams: x the lights', y the reflections', z the ambient's occlusion).
constexpr UINT BATCH_BYTES = 8 * 16;
constexpr UINT SSDO_REFLECTION_OFFSET = (5 * 16) + 4;

// `texture` as an RGBA16F texture of `size` with `uav` and / or `srv` (null: none), made again when the size changes;
// false: it could not be made.
inline bool EnsureTexture(ID3D11Device* device, const std::array<UINT, 2>& size, ComPtr<ID3D11Texture2D>* texture,
                          ComPtr<ID3D11UnorderedAccessView>* uav, ComPtr<ID3D11ShaderResourceView>* srv) {
  D3D11_TEXTURE2D_DESC desc = {};
  if (*texture) (*texture)->GetDesc(&desc);
  if (*texture && desc.Width == size[0] && desc.Height == size[1]) return true;
  texture->Reset();
  if (uav != nullptr) uav->Reset();
  if (srv != nullptr) srv->Reset();
  desc = {
      .Width = size[0],
      .Height = size[1],
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = ((uav != nullptr) ? UINT{D3D11_BIND_UNORDERED_ACCESS} : 0u) | ((srv != nullptr) ? UINT{D3D11_BIND_SHADER_RESOURCE} : 0u),
  };
  if (FAILED(device->CreateTexture2D(&desc, nullptr, texture->GetAddressOf()))
      || (uav != nullptr && FAILED(device->CreateUnorderedAccessView(texture->Get(), nullptr, uav->GetAddressOf())))
      || (srv != nullptr && FAILED(device->CreateShaderResourceView(texture->Get(), nullptr, srv->GetAddressOf())))) {
    texture->Reset();
    return false;
  }
  return true;
}

// A sun map's draw (Host::cascade_target; which maps: shadow_skip::SunMap).
inline bool SquareMap(const D3D11_TEXTURE2D_DESC& desc) {
  return desc.Width == desc.Height && desc.ArraySize == 1;
}

// An opaque G-buffer draw (three RGBA16F targets and the depth, depth written, unblended): the bridge prepared for the
// depth's size (upscaling.hpp; the render size), its pixel shader's alpha test (rt_core's SceneCoverage: leaves against their diffuse alpha), its vertex
// shader skinned or not. With Shadow Casters Out of View also a sun caster's (depth only into one of the sun's maps the game
// draws every frame, shadow_skip::SunMap: the cascades and the characters' own, not the far map it keeps): rt_core's cascade
// capture, every frame, its shadow vertex shader with the main view's matrix in PER_FRAME (Host::cascade_view_slot / _row) and
// a triangle in the view collapsed (the G-buffer's draw has it): Warhammer III's rt_outside_view for the solid casters (ps
// 0x78B445B5), rt_outside_view_uv for the alpha-tested ones (ps 0x9112241F: the diffuse map's alpha at TEXCOORD0).
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  std::array<ID3D11RenderTargetView*, 4> targets = {};
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth_view.GetAddressOf());
  const bool depth_only = targets[0] == nullptr;
  bool gbuffer = depth_view && targets[3] == nullptr;
  for (size_t index = 0; index < 3; ++index) {
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    if (targets[index] != nullptr) targets[index]->GetDesc(&desc);
    gbuffer = gbuffer && targets[index] != nullptr && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
  }
  for (auto* target : targets) {
    if (target != nullptr) target->Release();
  }
  if (depth_only && depth_view && casters_out_of_view.load(std::memory_order_relaxed)) {
    ComPtr<ID3D11Resource> map;
    depth_view->GetResource(map.GetAddressOf());
    if (!rysesonofrome::shadow_skip::SunMap(reinterpret_cast<uintptr_t>(map.Get()))) return false;
    shader->cascade_keep = __rt_outside_view;
    shader->cascade_keep_uv = __rys_outside_view_uv;
    shader->dynamic = true;
    return twattila::rt_scene::SceneCoverage(native, shader);
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
  {
    const std::unique_lock lock(state.mutex);
    state.render_size = {depth_texture_desc.Width, depth_texture_desc.Height};
  }
  if (!upscaling::PrepareBridge(device.Get(), depth_texture_desc.Width, depth_texture_desc.Height)
      || !twattila::rt_scene::SceneCoverage(native, shader)) {
    return false;
  }
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
    .cascade_view_slot = 2,
    .cascade_view_row = 0,
    .cascade_target = internal::SquareMap,
    .depth_at_render_size = true,
};

// Before the tiled lighting's dispatch (its bindings set): the pixels' world positions and the sun to the helper, the
// guides from its G-buffer (t1-t3: normals, specular, diffuse) when wanted, then this frame's trace; true: traced.
inline bool Trace(ID3D11DeviceContext* context) {
  auto& state = internal::state;
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || !twattila::dlaa_bridge::bridge.ray_tracing || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
    return false;
  }
  twattila::rt_scene::Camera camera;
  std::array<UINT, 2> size;
  bool keep_color = false;
  bool path_ambient = false;
  {
    const std::unique_lock lock(state.mutex);
    if (!state.camera_found) return false;
    camera = state.camera;
    size = state.render_size;
    keep_color = (state.settings.reflections || state.settings.path_tracing) && state.kept_color;
    path_ambient = state.settings.path_ambient;
  }
  ComPtr<ID3D11ShaderResourceView> depth;
  ComPtr<ID3D11Buffer> per_batch;
  context->CSGetShaderResources(0, 1, depth.GetAddressOf());
  context->CSGetConstantBuffers(0, 1, per_batch.GetAddressOf());
  if (!depth || !per_batch) return false;
  // Path-Traced Ambient's sky from the lighting's lights (t32) and diffuse probes (t34); unbound: none.
  ComPtr<ID3D11ShaderResourceView> light_list;
  ComPtr<ID3D11ShaderResourceView> diffuse_probes;
  if (path_ambient) {
    context->CSGetShaderResources(32, 1, light_list.GetAddressOf());
    context->CSGetShaderResources(34, 1, diffuse_probes.GetAddressOf());
  }
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (state.device != device) {
    ComPtr<ID3D11Device1> device1;
    const auto feature_level = device->GetFeatureLevel();
    state.device = device;
    state.private_state.Reset();
    state.positions.Reset();
    state.guides.Reset();
    state.color.Reset();
    state.specular_merge.Reset();
    state.linear_sampler.Reset();
    state.probe_sky.Reset();
    state.no_probes.Reset();
    state.neutral_batch.Reset();
    state.zero.Reset();
    const D3D11_SAMPLER_DESC sampler_desc = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    // A black cube (one layer: the lighting's resIndex clamps to it) for Path-Traced Ambient.
    const std::array<uint64_t, 6> black = {};
    std::array<D3D11_SUBRESOURCE_DATA, 6> black_faces = {};
    for (auto& face : black_faces) face = {.pSysMem = &black[0], .SysMemPitch = 8};
    const D3D11_TEXTURE2D_DESC cube_desc = {
        .Width = 1,
        .Height = 1,
        .MipLevels = 1,
        .ArraySize = 6,
        .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_IMMUTABLE,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE,
    };
    D3D11_SHADER_RESOURCE_VIEW_DESC cube_view = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY};
    cube_view.TextureCubeArray = {.MostDetailedMip = 0, .MipLevels = 1, .First2DArrayFace = 0, .NumCubes = 1};
    ComPtr<ID3D11Texture2D> cube;
    const D3D11_BUFFER_DESC batch_desc = {.ByteWidth = internal::BATCH_BYTES, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const std::array<float, 4> zeros = {};
    const D3D11_SUBRESOURCE_DATA zero_data = {.pSysMem = zeros.data()};
    const D3D11_BUFFER_DESC zero_desc = {.ByteWidth = sizeof(zeros), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(0, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                               state.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__0x52C5E004.data(), __0x52C5E004.size(), nullptr, state.positions.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__0x52C5E005.data(), __0x52C5E005.size(), nullptr, state.guides.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__0x52C5E006.data(), __0x52C5E006.size(), nullptr, state.color.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rt_specular_merge.data(), __rt_specular_merge.size(), nullptr,
                                                         state.specular_merge.GetAddressOf()))
                   || FAILED(device->CreateSamplerState(&sampler_desc, state.linear_sampler.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rt_probe_sky.data(), __rt_probe_sky.size(), nullptr, state.probe_sky.GetAddressOf()))
                   || FAILED(device->CreateTexture2D(&cube_desc, black_faces.data(), cube.GetAddressOf()))
                   || FAILED(device->CreateShaderResourceView(cube.Get(), &cube_view, state.no_probes.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&batch_desc, nullptr, state.neutral_batch.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&zero_desc, &zero_data, state.zero.GetAddressOf()));
  }
  if (state.failed) return false;
  auto& bridge = twattila::dlaa_bridge::bridge;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer;
  if (upscaling::CurrentUse().guides && bridge.guides) {
    for (UINT index = 0; index < gbuffer.size(); ++index) context->CSGetShaderResources(1 + index, 1, gbuffer[index].GetAddressOf());
  }
  const bool write_guides = gbuffer[0] && gbuffer[1] && gbuffer[2];
  twattila::dlaa_bridge::BeforeSharedWrites(context);
  {
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    // Every trace: without Path-Traced Ambient its texels written empty (an older sky never stays).
    ID3D11ShaderResourceView* sky_inputs[] = {light_list.Get(), diffuse_probes.Get()};
    ID3D11UnorderedAccessView* sky_output[] = {bridge.rt_sun.uav.Get()};
    ID3D11SamplerState* sky_samplers[] = {state.linear_sampler.Get()};
    context->CSSetShader(state.probe_sky.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, sky_inputs);
    context->CSSetUnorderedAccessViews(0, 1, sky_output, nullptr);
    context->CSSetSamplers(0, 1, sky_samplers);
    context->Dispatch(1, 1, 1);
    ID3D11ShaderResourceView* inputs[] = {depth.Get(), nullptr};
    ID3D11UnorderedAccessView* outputs[] = {bridge.rt_positions.uav.Get(), bridge.rt_sun.uav.Get()};
    ID3D11Buffer* constants[] = {per_batch.Get()};
    context->CSSetShader(state.positions.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, inputs);
    context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
    context->CSSetConstantBuffers(0, 1, constants);
    context->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
    if (write_guides) {
      ID3D11ShaderResourceView* views[] = {gbuffer[0].Get(), gbuffer[1].Get(), gbuffer[2].Get()};
      ID3D11UnorderedAccessView* written[] = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(), bridge.normals.uav.Get(),
                                              bridge.roughness.uav.Get()};
      context->CSSetShader(state.guides.Get(), nullptr, 0);
      context->CSSetShaderResources(0, 3, views);
      context->CSSetUnorderedAccessViews(0, 4, written, nullptr);
      context->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
    }
  }
  // Last frame's light (KeepColor) where the helper's rays read it; the upscaler's input takes that texture after.
  D3D11_TEXTURE2D_DESC kept = {};
  if (keep_color) state.kept_color->GetDesc(&kept);
  if (keep_color && bridge.color.texture && kept.Width == size[0] && kept.Height == size[1]) {
    const D3D11_BOX box = {.right = size[0], .bottom = size[1], .back = 1};
    context->CopySubresourceRegion(bridge.color.texture.Get(), 0, 0, 0, 0, state.kept_color.Get(), 0, &box);
    bridge.color_square_root = false;
  }
  if (write_guides) {
    const std::unique_lock lock(state.mutex);
    state.guides_frame = state.frame;
  }
  return twattila::rt_scene::Trace(context, nullptr, camera);
}

// At the tiled lighting's dispatch, after Trace: Path-Traced Ambient's black probes (t34) while the paths bring the sky
// (their bounce traced the frame before: a frame without it keeps the game's ambient); the traced reflections in place of
// its screen-space ones (t6: the same lerp over the probes, reflected light by the share of rays that met something);
// without them the traced specular occlusion merged into the screen-space ones (rt_specular_merge: the probes' share times
// it). Either way the lighting's own SSDO reflection occlusion (SSDOParams.y on the probes' share) is left out: ours
// replaces it (else the probes' share is occluded twice). The game's put back after the dispatch (RestoreLightingInputs).
inline void BindLightingInputs(ID3D11DeviceContext* context) {
  auto& state = internal::state;
  const auto traced = twattila::rt_scene::Reflections(context);
  bool paths_bring_sky = false;
  {
    auto& runtime = twattila::rt_scene::internal::runtime;
    const std::unique_lock runtime_lock(runtime.mutex);
    paths_bring_sky = runtime.path_ambient_on && runtime.bounced_frame + 1 >= runtime.frame;
  }
  const std::unique_lock lock(state.mutex);
  if (paths_bring_sky && state.no_probes && !state.probes_bound) {
    state.game_probes.Reset();
    context->CSGetShaderResources(34, 1, state.game_probes.GetAddressOf());
    ID3D11ShaderResourceView* black = state.no_probes.Get();
    context->CSSetShaderResources(34, 1, &black);
    state.probes_bound = true;
  }
  state.reflecting = traced[0] != nullptr && state.settings.reflections;
  const bool occluding = !state.reflecting && traced[1] != nullptr && state.settings.specular_occlusion && state.specular_merge;
  ComPtr<ID3D11DeviceContext1> context1;
  if ((!state.reflecting && !occluding) || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  ComPtr<ID3D11ShaderResourceView> game;
  context->CSGetShaderResources(6, 1, game.GetAddressOf());
  ID3D11ShaderResourceView* bound = traced[0];
  const auto& bridge = twattila::dlaa_bridge::bridge;
  if (occluding) {
    if (!internal::EnsureTexture(state.device.Get(), state.render_size, std::addressof(state.merged), std::addressof(state.merged_uav),
                                 std::addressof(state.merged_srv))) {
      return;
    }
    {
      const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
      ID3D11ShaderResourceView* inputs[] = {game.Get(), traced[1]};
      ID3D11UnorderedAccessView* outputs[] = {state.merged_uav.Get()};
      ID3D11SamplerState* samplers[] = {state.linear_sampler.Get()};
      context->CSSetShader(state.specular_merge.Get(), nullptr, 0);
      context->CSSetShaderResources(0, 2, inputs);
      context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
      context->CSSetSamplers(0, 1, samplers);
      context->Dispatch((state.render_size[0] + 7) / 8, (state.render_size[1] + 7) / 8, 1);
    }
    bound = state.merged_srv.Get();
  } else if (bridge.width != state.render_size[0] || bridge.height != state.render_size[1]) {
    // A bridge bigger than the render (the helper upscales): its top-left alone.
    ComPtr<ID3D11Resource> resource;
    bound->GetResource(resource.GetAddressOf());
    if (!internal::EnsureTexture(state.device.Get(), state.render_size, std::addressof(state.reflections_copy), nullptr,
                                 std::addressof(state.reflections_copy_srv))) {
      state.reflecting = false;
      return;
    }
    const D3D11_BOX box = {.right = state.render_size[0], .bottom = state.render_size[1], .back = 1};
    context->CopySubresourceRegion(state.reflections_copy.Get(), 0, 0, 0, 0, resource.Get(), 0, &box);
    bound = state.reflections_copy_srv.Get();
  }
  state.game_reflections = game;
  context->CSSetShaderResources(6, 1, &bound);
  state.reflections_bound = true;
  // PER_BATCH with SSDOParams.y 0 (a copy of the game's 8 float4 at its bound range): the ambient's and the lights'
  // occlusion (z, x) stay the game's.
  ComPtr<ID3D11Buffer> batch;
  UINT first = 0;
  UINT count = 0;
  context1->CSGetConstantBuffers1(0, 1, batch.GetAddressOf(), &first, &count);
  D3D11_BUFFER_DESC batch_desc = {};
  if (batch) batch->GetDesc(&batch_desc);
  const UINT offset = first * 16;
  if (!batch || !state.neutral_batch || offset + internal::BATCH_BYTES > batch_desc.ByteWidth) return;
  const D3D11_BOX source = {.left = offset, .right = offset + internal::BATCH_BYTES, .bottom = 1, .back = 1};
  context->CopySubresourceRegion(state.neutral_batch.Get(), 0, 0, 0, 0, batch.Get(), 0, &source);
  const D3D11_BOX one_float = {.right = 4, .bottom = 1, .back = 1};
  context->CopySubresourceRegion(state.neutral_batch.Get(), 0, internal::SSDO_REFLECTION_OFFSET, 0, 0, state.zero.Get(), 0, &one_float);
  ID3D11Buffer* neutral = state.neutral_batch.Get();
  context->CSSetConstantBuffers(0, 1, &neutral);
  state.game_batch = batch;
  state.game_batch_first = first;
  state.game_batch_count = count;
  state.batch_bound = true;
}

inline void RestoreLightingInputs(ID3D11DeviceContext* context) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.probes_bound) {
    ID3D11ShaderResourceView* probes = state.game_probes.Get();
    context->CSSetShaderResources(34, 1, &probes);
    state.game_probes.Reset();
    state.probes_bound = false;
  }
  if (state.batch_bound) {
    // As the game bound it: the whole buffer (first 0, its size or more) or its range.
    ID3D11Buffer* game_batch = state.game_batch.Get();
    D3D11_BUFFER_DESC batch_desc = {};
    game_batch->GetDesc(&batch_desc);
    ComPtr<ID3D11DeviceContext1> context1;
    if (state.game_batch_first == 0 && state.game_batch_count * 16 >= batch_desc.ByteWidth) {
      context->CSSetConstantBuffers(0, 1, &game_batch);
    } else if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
      context1->CSSetConstantBuffers1(0, 1, &game_batch, &state.game_batch_first, &state.game_batch_count);
    }
    state.game_batch.Reset();
    state.batch_bound = false;
  }
  if (!state.reflections_bound) return;
  ID3D11ShaderResourceView* game = state.game_reflections.Get();
  context->CSSetShaderResources(6, 1, &game);
  state.game_reflections.Reset();
  state.reflections_bound = false;
}

// The game's screen-space reflection trace (ps 0xE0B7E115, before the lighting): left out while the traced reflections
// take its place (they did at the last lighting). True: draw.
inline bool OnReflectionTrace() {
  const std::unique_lock lock(internal::state.mutex);
  return !(internal::state.reflecting && internal::state.settings.reflections);
}

// After the game copies its HDR scene into an R11G11B10 target at the render size (ps 0x3BA5B162; its last such copy in
// a frame is the scene before post, which the next frame's screen-space reflections read): kept, last frame's light for
// the next frame's reflections and paths (Trace puts it in the bridge's colour).
inline void KeepColor(ID3D11DeviceContext* context) {
  auto& state = internal::state;
  std::array<UINT, 2> size;
  {
    const std::unique_lock lock(state.mutex);
    if (!(state.settings.reflections || state.settings.path_tracing) || state.failed || !state.color) return;
    size = state.render_size;
  }
  ComPtr<ID3D11DeviceContext1> context1;
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11ShaderResourceView> scene;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  context->PSGetShaderResources(0, 1, scene.GetAddressOf());
  if (!target || !scene || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  ComPtr<ID3D11Resource> target_resource;
  target->GetResource(target_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target_texture;
  D3D11_TEXTURE2D_DESC desc = {};
  if (FAILED(target_resource.As(&target_texture))) return;
  target_texture->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_R11G11B10_FLOAT || desc.Width != size[0] || desc.Height != size[1]
      || !internal::EnsureTexture(state.device.Get(), size, std::addressof(state.kept_color), std::addressof(state.kept_color_uav), nullptr)) {
    return;
  }
  const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
  ID3D11ShaderResourceView* inputs[] = {scene.Get()};
  ID3D11UnorderedAccessView* outputs[] = {state.kept_color_uav.Get()};
  context->CSSetShader(state.color.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, inputs);
  context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
  context->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
}

// Every present: the settings and a new frame.
inline void Update(const twattila::rt_scene::Settings& settings) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.settings = settings;
    ++internal::state.frame;
  }
  internal::casters_out_of_view.store(settings.casters_out_of_view, std::memory_order_relaxed);
  twattila::rt_scene::Update(settings);
}

inline void Install() {
  rysesonofrome::dlss::constants::Use(DLL_PROCESS_ATTACH);
  twattila::rt_scene::InstallScene({}, HOST);
}

inline void Uninstall() {
  twattila::rt_scene::UninstallScene();
  rysesonofrome::dlss::constants::Use(DLL_PROCESS_DETACH);
}

// The game's device is going away: the addon's objects on it with it (the bridge's after: graphics.hpp).
inline void Release() {
  auto& state = internal::state;
  state.device.Reset();
  state.private_state.Reset();
  state.positions.Reset();
  state.guides.Reset();
  state.color.Reset();
  state.kept_color.Reset();
  state.kept_color_uav.Reset();
  state.reflections_copy.Reset();
  state.reflections_copy_srv.Reset();
  state.specular_merge.Reset();
  state.linear_sampler.Reset();
  state.merged.Reset();
  state.merged_uav.Reset();
  state.merged_srv.Reset();
  state.game_reflections.Reset();
  state.reflections_bound = false;
  state.reflecting = false;
  state.probe_sky.Reset();
  state.no_probes.Reset();
  state.game_probes.Reset();
  state.probes_bound = false;
  state.neutral_batch.Reset();
  state.zero.Reset();
  state.game_batch.Reset();
  state.batch_bound = false;
  state.failed = false;
}

}  // namespace rysesonofrome::ray_tracing
