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
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../twattila/dxbc.hpp"
#include "../twattila/rt_core.hpp"
#include "../talesofarise_dlss/constant_mirror.hpp"
#include "./contact_shadows.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/upscaling.hpp"

// Ray tracing (Experimental): Expeditions: Rome's scene through the Total War helper (twattila/rt_core.hpp;
// renodx-tw-dlaa64.exe next to the addon, its own D3D12 device with DXR), in metres (Unreal's centimetres / 100).
// The scene: Unreal 4.23's depth prepass into the scene depth (no target, depth written; the shadow maps' and custom
// depth's buffers stay out), streamed out through the game's own vertex shaders (those with bone indices, ATTRIBUTE3
// uint4, or a vertex buffer compute writes, the skin cache's: every frame). Alpha-tested materials (rt_core's
// SceneCoverage: a prepass pixel shader that discards and samples t0 at a texture coordinate, its alpha the mask) stream
// out that coordinate too; those that sample t0 otherwise (tiled masks) stay out. At the sun's deferred light draw (the light rectangle vs
// 0x7C21DA11, FDeferredLightUniformStruct at ps b1, View at ps b0, both read from the game's uploads:
// constant_mirror.hpp): the G-buffer guides (0x7A1E0D44), the trace (the scene depth at t1, the sun's direction), then
// the traced sun into the light attenuation (t8, 0x7A1E0D45: the darker of the game's and the traced) and the traced
// ambient into the screen-space AO (t7, 0x7A1E0D46: in its place, never on top of it, ATN 2026-11-16). With the rays' AO
// the trace comes earlier, at Unreal's AO passes (OnGameAo: the last sun draw's G-buffer and sun, this frame's View),
// which it leaves out, and the traced ambient goes into the AO at the base pass AO (192686A0), its first reader: the
// baked light takes ours too, one AO method (ATN 2026-11-16). The reflections replace the screen-space reflections'
// draw (0x7A1E0D47 into its target; the reflection captures and the sky fill the rest, which Specular Occlusion keeps only
// where the lobe is open, also over the game's own reflections), their light from the scene
// colour the temporal pass receives (KeepColor). The light bounce (path traced when on) right after the sun's draw: the
// scene colour then (direct sun light) is the prepared light (0x7A1E0D48, a texel per 2 x 2 pixels; 1 x 1 or 4 x 4 by
// Ray-Traced Light Bounce Resolution), the helper traces the light arriving at each texel, resolved over frames (0x7A1E0D49) and added to the scene colour times the diffuse
// colour (0x7A1E0D4A; Unreal's sky light and reflections follow in their own pass). Its shaders' code is kept from the
// game's start: Install only when ray tracing is on at launch.
namespace expeditionsrome::dlss::ray_tracing {

using Microsoft::WRL::ComPtr;

// The screen-space reflections' pixel shader (the game's SSR quality).
constexpr uint32_t REFLECTION_SHADER = 0xE36ED33E;

namespace internal {

// Unreal 4.23's View: WorldToClip (rows 4-7), WorldCameraOrigin (63), the scale the light pass's output takes (129.y: the
// sun's PS C38C91CD ends o0.xyz = cb0[129].yyy x its light).
constexpr uint32_t VIEW_WORLD_TO_CLIP = 4;
constexpr uint32_t VIEW_WORLD_CAMERA_ORIGIN = 63;
constexpr uint32_t VIEW_LIGHT_OUTPUT_SCALE = 129;
// The sky light (Path-Traced Ambient's sky): SkyLightColor (162) and SkyIrradianceEnvironmentMap (163-169: Unreal's
// GetSkySHDiffuse), as the sky light passes shaders/skylight00-02 read them.
constexpr uint32_t VIEW_SKY_LIGHT_COLOR = 162;
constexpr uint32_t VIEW_SKY_IRRADIANCE = 163;
constexpr uint32_t VIEW_ROWS = VIEW_SKY_IRRADIANCE + 7;
// The sky light passes and the slot their Path-Traced Ambient switch reads (shared.h ExrPathAmbient).
constexpr std::array<uint32_t, 3> SKY_LIGHT_SHADERS = {0x6C9A1DD0, 0x62008428, 0x5E77BB26};
constexpr UINT PATH_AMBIENT_SLOT = 120;
// FDeferredLightUniformStruct: ShadowedBits (c2.x), the light's colour (c4.xyz), the direction toward the light (c5.xyz);
// bound 144 - 256 bytes.
constexpr uint32_t LIGHT_SHADOWED_BITS = 2;
constexpr uint32_t LIGHT_COLOR = 4;
constexpr uint32_t LIGHT_DIRECTION = 5;
constexpr UINT MIN_LIGHT_BYTES = 144;
constexpr UINT MAX_LIGHT_BYTES = 256;
constexpr uint32_t LIGHT_RECTANGLE_SHADER = 0x7C21DA11;
// The base pass AO (BasePassAO, ps 192686A0: the scene colour so far, emissive and the baked light, x lerp(1, AO, the
// static fraction)), the first reader of the AO; Unreal's AO passes before it: the setups 73FDC01C / FB43A251, the levels
// 2491478D / A45907DE, the final pass ao00 31A1C68C / ao01 BD381E21 (run_epic_173834: 0.97 ms at 4K DLAA with the rays).
constexpr uint32_t BASE_PASS_AO_SHADER = 0x192686A0;
constexpr std::array<uint32_t, 6> GAME_AO_SHADERS = {0x73FDC01C, 0xFB43A251, 0x2491478D, 0xA45907DE, 0x31A1C68C, 0xBD381E21};
constexpr double CENTIMETRES = 100.0;
// The light bounce: the most frames its resolve averages (its texels: Ray-Traced Light Bounce Resolution's 1, 2 or 4
// render pixels each way).
constexpr float BOUNCE_FRAMES = 24.f;

// The sun for the helper: toward it (world, unit length) and its light on a diffuse surface (WriteSun's texel 2).
struct Sun {
  std::array<float, 3> toward = {};
  std::array<float, 4> light = {};
};

struct State {
  std::mutex mutex;
  twattila::rt_scene::Settings settings;
  ID3D11Resource* scene_depth = nullptr;  // the sun draw's t1 (compared only)
  std::array<UINT, 2> render_size = {};
  uint64_t frame = 0;
  uint64_t guides_frame = UINT64_MAX;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11VertexShader> fullscreen;
  ComPtr<ID3D11ComputeShader> guides;
  ComPtr<ID3D11PixelShader> shadow;
  ComPtr<ID3D11PixelShader> ambient;
  ComPtr<ID3D11PixelShader> reflection;
  ComPtr<ID3D11BlendState> multiply_blue;
  ComPtr<ID3D11BlendState> occlude_alpha;  // alpha only: a + (1 - a) x the shader's
  ComPtr<ID3D11Buffer> guides_constants;
  ComPtr<ID3D11Texture2D> attenuation_copy;
  ComPtr<ID3D11ShaderResourceView> attenuation_copy_srv;
  std::unordered_map<ID3D11Resource*, ComPtr<ID3D11RenderTargetView>> targets;  // the light attenuation's, the AO's
  bool failed = false;
  // The light bounce: its strength, shaders and textures (the bounce size), the two resolves (and their surfaces)
  // taking turns, this and last frame's camera.
  float bounce_strength = 1.f;
  ComPtr<ID3D11PixelShader> prepare;
  ComPtr<ID3D11ComputeShader> resolve;
  ComPtr<ID3D11PixelShader> apply;
  ComPtr<ID3D11BlendState> add;
  ComPtr<ID3D11Buffer> bounce_constants;
  std::unordered_map<ID3D11Resource*, ComPtr<ID3D11ShaderResourceView>> scene_views;
  std::array<UINT, 2> bounce_size = {};
  twattila::dlaa_bridge::Texture prepared;
  ComPtr<ID3D11RenderTargetView> prepared_rtv;
  twattila::dlaa_bridge::Texture traced;
  std::array<twattila::dlaa_bridge::Texture, 2> resolved;
  std::array<twattila::dlaa_bridge::Texture, 2> resolved_positions;
  uint32_t resolved_index = 0;
  uint64_t resolved_frame = UINT64_MAX;
  uint64_t shadowed_frame = UINT64_MAX;  // the rays' sun shadows' last draw
  // Path-Traced Ambient: the last frame the light bounce added the paths' sky, and the sky light passes' switch (1 x 1).
  uint64_t sky_frame = UINT64_MAX;
  ComPtr<ID3D11ShaderResourceView> path_ambient_switch;
  std::array<float, 16> view_projection = {};
  std::array<float, 16> previous_view_projection = {};
  // The trace at Unreal's AO passes (OnGameAo): the last sun draw's scene depth and G-buffer A - C (their views), sun and
  // frame; the frame it was tried there, the frame it traced there, the frame the traced ambient went into the AO.
  std::array<ComPtr<ID3D11ShaderResourceView>, 4> kept_inputs;
  Sun kept_sun;
  uint64_t kept_frame = UINT64_MAX;
  uint64_t early_tried_frame = UINT64_MAX;
  uint64_t early_frame = UINT64_MAX;
  uint64_t ambient_frame = UINT64_MAX;
};
static State state;

// A vertex shader's code: Unreal's bone indices (ATTRIBUTE3, uint) in its input signature.
inline bool Skinned(std::span<const uint8_t> code) {
  constexpr uint32_t COMPONENT_UINT32 = 1;
  const twattila::dxbc::Program program = twattila::dxbc::Read(code);
  twattila::dxbc::SignatureElement bones;
  return !program.tokens.empty()
         && twattila::dxbc::FindSignatureElement(code, program, "ISGN", [](const twattila::dxbc::SignatureElement& element, uint32_t) {
              return element.name == "ATTRIBUTE" && element.index == 3 && element.component_type == COMPONENT_UINT32;
            }, &bones);
}

// A prepass draw into the scene depth (no target, depth written): its pixel shader's coverage (rt_core's
// SceneCoverage) and its vertex shader's skinning; false: not in the scene.
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
  if (!twattila::rt_scene::SceneCoverage(native, shader)) return false;
  // Skinned: bone indices, or a vertex buffer compute writes (the skin cache).
  ComPtr<ID3D11Buffer> vertices;
  UINT stride = 0;
  UINT offset = 0;
  native->IAGetVertexBuffers(0, 1, vertices.GetAddressOf(), &stride, &offset);
  D3D11_BUFFER_DESC vertices_desc = {};
  if (vertices) vertices->GetDesc(&vertices_desc);
  shader->dynamic = twattila::rt_scene::SkinnedVertexShader(shader->hash) || (vertices_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
  return true;
}

// The device's own objects (once); false: not available.
inline bool PrepareDevice(ID3D11Device* device) {
  if (state.device.Get() == device) return !state.failed;
  state.device = device;
  state.targets.clear();
  state.attenuation_copy.Reset();
  state.attenuation_copy_srv.Reset();
  state.scene_views.clear();
  state.bounce_size = {};
  ComPtr<ID3D11Device1> device1;
  const auto feature_level = device->GetFeatureLevel();
  const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
  D3D11_BLEND_DESC multiply_desc = {};
  multiply_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ZERO, .DestBlend = D3D11_BLEND_SRC_COLOR,
                                   .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ZERO, .DestBlendAlpha = D3D11_BLEND_ONE,
                                   .BlendOpAlpha = D3D11_BLEND_OP_ADD, .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_BLUE};
  D3D11_BLEND_DESC add_desc = {};
  add_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ONE, .DestBlend = D3D11_BLEND_ONE, .BlendOp = D3D11_BLEND_OP_ADD,
                              .SrcBlendAlpha = D3D11_BLEND_ZERO, .DestBlendAlpha = D3D11_BLEND_ONE, .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                              .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN
                                                       | D3D11_COLOR_WRITE_ENABLE_BLUE};
  D3D11_BLEND_DESC occlude_desc = {};
  occlude_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ZERO, .DestBlend = D3D11_BLEND_ONE, .BlendOp = D3D11_BLEND_OP_ADD,
                                  .SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA, .DestBlendAlpha = D3D11_BLEND_ONE,
                                  .BlendOpAlpha = D3D11_BLEND_OP_ADD, .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA};
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC bounce_desc = {.ByteWidth = 96, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  constexpr uint8_t SWITCH_ON = 255;
  const D3D11_TEXTURE2D_DESC switch_desc = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8_UNORM,
                                            .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA switch_data = {.pSysMem = &SWITCH_ON, .SysMemPitch = 1};
  ComPtr<ID3D11Texture2D> switch_texture;
  state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                 || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                             nullptr, state.private_state.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr,
                                                      state.fullscreen.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateComputeShader(__0x7A1E0D44.data(), __0x7A1E0D44.size(), nullptr, state.guides.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__0x7A1E0D45.data(), __0x7A1E0D45.size(), nullptr, state.shadow.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__0x7A1E0D46.data(), __0x7A1E0D46.size(), nullptr, state.ambient.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__0x7A1E0D47.data(), __0x7A1E0D47.size(), nullptr,
                                                     state.reflection.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBlendState(&multiply_desc, state.multiply_blue.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBlendState(&occlude_desc, state.occlude_alpha.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&constants_desc, nullptr, state.guides_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__0x7A1E0D48.data(), __0x7A1E0D48.size(), nullptr, state.prepare.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateComputeShader(__0x7A1E0D49.data(), __0x7A1E0D49.size(), nullptr, state.resolve.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreatePixelShader(__0x7A1E0D4A.data(), __0x7A1E0D4A.size(), nullptr, state.apply.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBlendState(&add_desc, state.add.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateBuffer(&bounce_desc, nullptr, state.bounce_constants.ReleaseAndGetAddressOf()))
                 || FAILED(device->CreateTexture2D(&switch_desc, &switch_data, switch_texture.GetAddressOf()))
                 || FAILED(device->CreateShaderResourceView(switch_texture.Get(), nullptr, state.path_ambient_switch.ReleaseAndGetAddressOf()));
  if (state.failed) renodx::utils::log::w("[Rome ray tracing] Could not create its shaders or states.");
  return !state.failed;
}

// A render target view of `resource` (its typed format); null: none.
inline ID3D11RenderTargetView* Target(ID3D11Device* device, ID3D11Resource* resource, DXGI_FORMAT format) {
  auto& target = state.targets[resource];
  if (!target) {
    const D3D11_RENDER_TARGET_VIEW_DESC desc = {.Format = format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
    if (FAILED(device->CreateRenderTargetView(resource, &desc, target.GetAddressOf()))) state.targets.erase(resource);
  }
  const auto found = state.targets.find(resource);
  return (found == state.targets.end()) ? nullptr : found->second.Get();
}

// A full-screen draw of `pixel_shader` into `target` over `size` (the render pixels when 0), in the addon's own context
// state; `constants` with their ranges when given.
inline void FullScreen(ID3D11DeviceContext1* context, ID3D11RenderTargetView* target, ID3D11BlendState* blend, ID3D11PixelShader* pixel_shader,
                       std::span<ID3D11ShaderResourceView* const> views, std::span<ID3D11Buffer* const> constants = {},
                       std::span<const UINT> first_constants = {}, std::span<const UINT> constant_counts = {},
                       std::array<UINT, 2> size = {}) {
  const talesofarise::dlss::ContextStateScope scope(context, state.private_state.Get());
  if (size[0] == 0) size = state.render_size;
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(size[0]), .Height = static_cast<float>(size[1]), .MaxDepth = 1.f};
  context->OMSetRenderTargets(1, &target, nullptr);
  context->OMSetBlendState(blend, nullptr, UINT_MAX);
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(state.fullscreen.Get(), nullptr, 0);
  context->PSSetShader(pixel_shader, nullptr, 0);
  context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  if (!constants.empty()) {
    context->PSSetConstantBuffers1(0, static_cast<UINT>(constants.size()), constants.data(),
                                   first_constants.empty() ? nullptr : first_constants.data(),
                                   constant_counts.empty() ? nullptr : constant_counts.data());
  }
  context->Draw(3, 0);
}

// A texture of the bounce size (shader resource and unordered access, or render target when `rtv`); false: not made.
inline bool MakeBounceTexture(ID3D11Device* device, DXGI_FORMAT format, twattila::dlaa_bridge::Texture* texture,
                              ComPtr<ID3D11RenderTargetView>* rtv = nullptr) {
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = state.bounce_size[0],
      .Height = state.bounce_size[1],
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = format,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE | (rtv != nullptr ? D3D11_BIND_RENDER_TARGET : D3D11_BIND_UNORDERED_ACCESS)),
  };
  *texture = {};
  return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->texture.GetAddressOf()))
         && SUCCEEDED(device->CreateShaderResourceView(texture->texture.Get(), nullptr, texture->srv.GetAddressOf()))
         && (rtv != nullptr ? SUCCEEDED(device->CreateRenderTargetView(texture->texture.Get(), nullptr, rtv->ReleaseAndGetAddressOf()))
                            : SUCCEEDED(device->CreateUnorderedAccessView(texture->texture.Get(), nullptr, texture->uav.GetAddressOf())));
}

// The View's rows as last uploaded (`buffer` bound from constant `first`, `count` of them): to the sky light's where the
// range holds them, else to the light output scale's; 0: fewer, or not read.
inline UINT ReadView(ID3D11Buffer* buffer, UINT first, UINT count, std::array<std::array<float, 4>, VIEW_ROWS>* view) {
  if (count <= VIEW_LIGHT_OUTPUT_SCALE) return 0;
  const UINT rows = std::min(VIEW_ROWS, count);
  talesofarise::dlss::constants::Watch(buffer, (first + rows) * 16);
  return talesofarise::dlss::constants::Read(buffer, uint64_t{first} * 16, std::as_writable_bytes(std::span(*view).first(rows))) ? rows : 0;
}

// The trace: the camera (the View's rows `view`), `sun` and Path-Traced Ambient's sky to the helper, the guides from
// G-buffer A - C, the trace from the scene depth (`inputs`: the depth, then A - C); false: not traced.
inline bool TraceScene(ID3D11DeviceContext1* context, std::span<const std::array<float, 4>> view, const Sun& sun,
                       const std::array<ID3D11ShaderResourceView*, 4>& inputs, const twattila::rt_scene::Settings& settings, uint64_t frame) {
  // Unreal's WorldToClip (row vectors, centimetres) as rows acting on metres.
  std::array<float, 16> view_projection = {};
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) {
      view_projection[(row * 4) + column] = static_cast<float>(view[VIEW_WORLD_TO_CLIP + column][row] * (column < 3 ? CENTIMETRES : 1.0));
    }
  }
  const auto& origin = view[VIEW_WORLD_CAMERA_ORIGIN];
  const twattila::rt_scene::Camera camera = {
      .view_projection = view_projection,
      .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
      .position = {static_cast<float>(origin[0] / CENTIMETRES), static_cast<float>(origin[1] / CENTIMETRES),
                   static_cast<float>(origin[2] / CENTIMETRES)},
  };
  twattila::rt_scene::WriteSun(context, sun.toward, sun.light);
  auto& bridge = twattila::dlaa_bridge::bridge;
  // Path-Traced Ambient's sky (dlaa_helper/rt_path.hlsli SkyLight, OpenSky): Unreal's sky light toward +x, -x, +y, -y,
  // +z, -z (GetSkySHDiffuse x SkyLightColor x View 129.y, as skylight00-02 light the diffuse colour) into the sun
  // texture's texels 3-8, w 1 (texel 4's: the sky's scale, the game's units), texel 6's w 0 (Unreal's up is z: no
  // stretch of y, which Attila's sun light shaders have). Without it, or without the View's sky rows: zeros (no sky).
  std::array<std::array<float, 4>, 6> sky = {};
  if (settings.path_ambient && view.size() == VIEW_ROWS && bridge.rt_sun.texture) {
    for (size_t side = 0; side < sky.size(); ++side) {
      std::array<float, 3> normal = {};
      normal[side / 2] = (side % 2 == 0) ? 1.f : -1.f;
      const std::array<float, 4> linear = {normal[0], normal[1], normal[2], 1.f};
      const std::array<float, 4> quadratic = {normal[0] * normal[1], normal[1] * normal[2], normal[2] * normal[2], normal[2] * normal[0]};
      const float band = (normal[0] * normal[0]) - (normal[1] * normal[1]);
      for (size_t channel = 0; channel < 3; ++channel) {
        float irradiance = view[VIEW_SKY_IRRADIANCE + 6][channel] * band;
        for (size_t component = 0; component < 4; ++component) {
          irradiance += (view[VIEW_SKY_IRRADIANCE + channel][component] * linear[component])
                        + (view[VIEW_SKY_IRRADIANCE + 3 + channel][component] * quadratic[component]);
        }
        sky[side][channel] = std::max(irradiance, 0.f) * view[VIEW_SKY_LIGHT_COLOR][channel] * view[VIEW_LIGHT_OUTPUT_SCALE][1];
      }
      sky[side][3] = (side == 3) ? 0.f : 1.f;
    }
  }
  if (bridge.rt_sun.texture) {
    const D3D11_BOX sky_texels = {.left = 3, .top = 0, .front = 0, .right = 9, .bottom = 1, .back = 1};
    context->UpdateSubresource(bridge.rt_sun.texture.Get(), 0, &sky_texels, sky.data(), static_cast<UINT>(sizeof(sky)), 0);
  }
  // The guides for the reflections, the paths and Ray Reconstruction.
  if (talesofarise::upscaling::Guides() && bridge.guides) {
    const talesofarise::dlss::ContextStateScope scope(context, state.private_state.Get());
    const std::array<UINT, 4> size = {state.render_size[0], state.render_size[1], 0, 0};
    context->UpdateSubresource(state.guides_constants.Get(), 0, nullptr, size.data(), 0, 0);
    const std::array<ID3D11UnorderedAccessView*, 4> outputs = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(),
                                                               bridge.normals.uav.Get(), bridge.roughness.uav.Get()};
    context->CSSetShader(state.guides.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 3, inputs.data() + 1);
    context->CSSetUnorderedAccessViews(0, static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
    context->CSSetConstantBuffers(0, 1, state.guides_constants.GetAddressOf());
    context->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
    const std::unique_lock lock(state.mutex);
    state.guides_frame = frame;
  }
  if (!twattila::rt_scene::Trace(context, inputs[0], camera)) return false;
  state.view_projection = view_projection;
  return true;
}

// Right after the sun's deferred light draw: the light bounce from its scene colour (the draw's target), traced,
// resolved and added to it.
template <typename Context>
void OnSunLit(Context& command, const void*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (!target) return;
  ComPtr<ID3D11Resource> scene;
  target->GetResource(scene.GetAddressOf());
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  auto& scene_view = state.scene_views[scene.Get()];
  if (!scene_view && FAILED(device->CreateShaderResourceView(scene.Get(), nullptr, scene_view.GetAddressOf()))) {
    state.scene_views.erase(scene.Get());
    return;
  }
  uint64_t frame = 0;
  float strength = 1.f;
  UINT scale = 2;
  {
    const std::unique_lock lock(state.mutex);
    frame = state.frame;
    strength = state.bounce_strength;
    scale = 1u << std::min(static_cast<UINT>(state.settings.light_bounce_resolution), 2u);
  }
  // The bounce's textures at the render size's and the resolution's (made again when either changes).
  const std::array<UINT, 2> bounce_size = {(state.render_size[0] + scale - 1) / scale, (state.render_size[1] + scale - 1) / scale};
  if (state.bounce_size != bounce_size) {
    state.bounce_size = bounce_size;
    state.resolved_frame = UINT64_MAX;
    if (!MakeBounceTexture(device.Get(), DXGI_FORMAT_R11G11B10_FLOAT, &state.prepared, &state.prepared_rtv)
        || !MakeBounceTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, &state.traced)
        || !std::ranges::all_of(state.resolved, [&](auto& texture) {
             return MakeBounceTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, &texture);
           })
        || !std::ranges::all_of(state.resolved_positions, [&](auto& texture) {
             return MakeBounceTexture(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, &texture);
           })) {
      state.bounce_size = {};
      return;
    }
  }
  // The prepare's and the apply's constants (the buffer's whole 96 bytes: a constant buffer is written whole).
  struct ApplyConstants {
    std::array<uint32_t, 4> sizes;
    std::array<float, 4> settings;
    std::array<float, 16> padding;
  };
  const std::array<uint32_t, 4> sizes = {bounce_size[0], bounce_size[1], state.render_size[0], state.render_size[1]};
  const ApplyConstants prepare_constants = {.sizes = sizes, .settings = {0.f, static_cast<float>(scale)}, .padding = {}};
  context->UpdateSubresource(state.bounce_constants.Get(), 0, nullptr, &prepare_constants, 0, 0);
  const std::array<ID3D11ShaderResourceView*, 1> scene_views = {scene_view.Get()};
  const std::array<ID3D11Buffer*, 1> constant_buffers = {state.bounce_constants.Get()};
  FullScreen(context1.Get(), state.prepared_rtv.Get(), nullptr, state.prepare.Get(), scene_views, constant_buffers, {}, {}, bounce_size);
  if (!twattila::rt_scene::internal::TraceLightBounce(context, state.prepared.texture.Get(), bounce_size[0], bounce_size[1], scale,
                                            state.traced.texture.Get())) {
    return;
  }
  auto& bridge = twattila::dlaa_bridge::bridge;
  const uint32_t previous = state.resolved_index;
  const uint32_t current = previous ^ 1u;
  {
    struct ResolveConstants {
      std::array<float, 16> previous_view_projection;
      std::array<uint32_t, 4> sizes;
      std::array<float, 4> settings;
    };
    const ResolveConstants constants = {
        .previous_view_projection = state.previous_view_projection,
        .sizes = sizes,
        .settings = {BOUNCE_FRAMES, (state.resolved_frame + 1 == frame) ? 1.f : 0.f, static_cast<float>(scale)},
    };
    const talesofarise::dlss::ContextStateScope scope(context1.Get(), state.private_state.Get());
    context->UpdateSubresource(state.bounce_constants.Get(), 0, nullptr, &constants, 0, 0);
    const std::array<ID3D11ShaderResourceView*, 4> views = {state.traced.srv.Get(), bridge.rt_positions.srv.Get(),
                                                            state.resolved[previous].srv.Get(), state.resolved_positions[previous].srv.Get()};
    const std::array<ID3D11UnorderedAccessView*, 2> outputs = {state.resolved[current].uav.Get(), state.resolved_positions[current].uav.Get()};
    context->CSSetShader(state.resolve.Get(), nullptr, 0);
    context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    context->CSSetUnorderedAccessViews(0, static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
    context->CSSetConstantBuffers(0, 1, state.bounce_constants.GetAddressOf());
    context->Dispatch((bounce_size[0] + 7) / 8, (bounce_size[1] + 7) / 8, 1);
  }
  state.resolved_index = current;
  {
    const std::unique_lock lock(state.mutex);
    state.resolved_frame = frame;
  }
  state.previous_view_projection = state.view_projection;
  // The G-buffer the light read (its t2 - t4).
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer;
  {
    std::array<ID3D11ShaderResourceView*, 3> pointers = {};
    context->PSGetShaderResources(2, static_cast<UINT>(pointers.size()), pointers.data());
    for (size_t index = 0; index < pointers.size(); ++index) gbuffer[index].Attach(pointers[index]);
  }
  if (!gbuffer[0] || !gbuffer[1] || !gbuffer[2]) return;
  const ApplyConstants apply_constants = {.sizes = sizes, .settings = {strength, static_cast<float>(scale)}, .padding = {}};
  context->UpdateSubresource(state.bounce_constants.Get(), 0, nullptr, &apply_constants, 0, 0);
  const std::array<ID3D11ShaderResourceView*, 6> views = {state.resolved[current].srv.Get(), state.resolved_positions[current].srv.Get(),
                                                          bridge.rt_positions.srv.Get(), gbuffer[0].Get(), gbuffer[1].Get(), gbuffer[2].Get()};
  FullScreen(context1.Get(), target.Get(), state.add.Get(), state.apply.Get(), views, constant_buffers);
  // Path-Traced Ambient: this bounce brought the paths' sky (rt_core's path_ambient_on, once they learned the game's
  // light): this frame's sky light passes leave Unreal's diffuse sky light out (OnSkyLight).
  bool paths_sky = false;
  {
    auto& runtime = twattila::rt_scene::internal::runtime;
    const std::unique_lock runtime_lock(runtime.mutex);
    paths_sky = runtime.path_ambient_on;
  }
  if (paths_sky) {
    const std::unique_lock lock(state.mutex);
    state.sky_frame = frame;
  }
}

// The sun's deferred light draw: its camera and sun to the helper, the guides, the trace (unless traced at Unreal's AO
// passes: OnGameAo); then the traced sun into the light attenuation and the traced ambient into the AO (unless the base
// pass AO took it: OnBasePassAo); the light bounce after it (OnSunLit).
constexpr auto OnLightRectangle = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  contact_shadows::ray_traced.store(false, std::memory_order_relaxed);
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  twattila::rt_scene::Settings settings;
  {
    const std::unique_lock lock(state.mutex);
    settings = state.settings;
  }
  // The View (ps b0) and the light (ps b1, a directional light's 144 - 256 bytes) as last uploaded.
  std::array<ComPtr<ID3D11Buffer>, 2> buffers;
  std::array<UINT, 2> first_constants = {};
  std::array<UINT, 2> constant_counts = {};
  for (UINT slot = 0; slot < 2; ++slot) {
    context1->PSGetConstantBuffers1(slot, 1, buffers[slot].GetAddressOf(), &first_constants[slot], &constant_counts[slot]);
    if (!buffers[slot]) return {};
  }
  D3D11_BUFFER_DESC light_desc = {};
  buffers[1]->GetDesc(&light_desc);
  const UINT light_first = std::min(first_constants[1] * 16, light_desc.ByteWidth);
  const UINT light_bytes = std::min(light_desc.ByteWidth - light_first, constant_counts[1] * 16);
  if (light_bytes < MIN_LIGHT_BYTES || light_bytes > MAX_LIGHT_BYTES) return {};
  std::array<std::array<float, 4>, VIEW_ROWS> view = {};
  const UINT view_rows = ReadView(buffers[0].Get(), first_constants[0], constant_counts[0], &view);
  std::array<std::array<float, 4>, LIGHT_DIRECTION + 1> light = {};
  talesofarise::dlss::constants::Watch(buffers[1].Get(), light_first + static_cast<uint32_t>(sizeof(light)));
  if (view_rows == 0 || !talesofarise::dlss::constants::Read(buffers[1].Get(), light_first, std::as_writable_bytes(std::span(light)))) {
    return {};
  }
  uint32_t shadowed_bits = 0;
  std::memcpy(&shadowed_bits, light[LIGHT_SHADOWED_BITS].data(), 4);
  const auto& direction = light[LIGHT_DIRECTION];
  const float direction_length = std::sqrt((direction[0] * direction[0]) + (direction[1] * direction[1]) + (direction[2] * direction[2]));
  if (shadowed_bits == 0 || !(direction_length > 0.f)) return {};
  // The sun's diffuse light with its direction (WriteSun's texel 2, w 1), the paths' sun as twattila/rt_sun.cs_5_0.hlsl
  // writes it for the Total War games: the light lights the G-buffer's diffuse colour (the guides' base x (1 - metallic)) by
  // the colour times N.L times the shadow. C38C91CD's default lit: DiffuseColor x 0.318309873 (1 / pi) x LightColor (c4) x
  // its shadow and N.L, x View 129.y at its output.
  constexpr float INVERSE_PI = 0.318309873f;
  const float scale = INVERSE_PI * view[VIEW_LIGHT_OUTPUT_SCALE][1];
  const auto& color = light[LIGHT_COLOR];
  const Sun sun_now = {.toward = {direction[0] / direction_length, direction[1] / direction_length, direction[2] / direction_length},
                       .light = {color[0] * scale, color[1] * scale, color[2] * scale, 1.f}};

  // The scene depth (t1) and the G-buffer the light reads, the render size (the draw's viewport).
  std::array<ComPtr<ID3D11ShaderResourceView>, 9> inputs;
  {
    std::array<ID3D11ShaderResourceView*, 9> pointers = {};
    context->PSGetShaderResources(0, static_cast<UINT>(pointers.size()), pointers.data());
    for (size_t index = 0; index < pointers.size(); ++index) inputs[index].Attach(pointers[index]);
  }
  constexpr size_t DEPTH = 1, GBUFFER_A = 2, GBUFFER_B = 3, GBUFFER_C = 4, GBUFFER_E = 6, AMBIENT = 7, ATTENUATION = 8;
  if (!inputs[DEPTH] || !inputs[GBUFFER_A] || !inputs[GBUFFER_B] || !inputs[GBUFFER_C]) return {};
  ComPtr<ID3D11Resource> depth_resource;
  inputs[DEPTH]->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return {};
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  D3D11_VIEWPORT viewport = {};
  UINT viewports = 1;
  context->RSGetViewports(&viewports, &viewport);
  const std::array<UINT, 2> render_size = {static_cast<UINT>(viewport.Width), static_cast<UINT>(viewport.Height)};
  if (viewports == 0 || render_size[0] == 0 || render_size[1] == 0 || render_size[0] > depth_desc.Width || render_size[1] > depth_desc.Height) {
    return {};
  }
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  // The bridge as the upscaler and frame generation ask for it (upscaling.hpp: the swap chain's size).
  if (!PrepareDevice(device.Get()) || !talesofarise::upscaling::PrepareBridge(device.Get())) return {};
  uint64_t frame = 0;
  bool traced_early = false;
  bool ambient_written = false;
  // The scene colour's views of the last render size, the last kept inputs: each holds its texture, which Unreal makes
  // again at a new size (let go after the lock: the game's texture may go with its last view).
  std::unordered_map<ID3D11Resource*, ComPtr<ID3D11ShaderResourceView>> dropped_scene_views;
  std::array<ComPtr<ID3D11ShaderResourceView>, 4> kept_inputs = {inputs[DEPTH], inputs[GBUFFER_A], inputs[GBUFFER_B], inputs[GBUFFER_C]};
  {
    const std::unique_lock lock(state.mutex);
    state.scene_depth = depth_resource.Get();
    if (state.render_size != render_size) {
      state.render_size = render_size;
      state.targets.clear();
      dropped_scene_views.swap(state.scene_views);
    }
    frame = state.frame;
    // For the next frame's trace at Unreal's AO passes (OnGameAo); traced there this frame: not again.
    kept_inputs.swap(state.kept_inputs);
    state.kept_sun = sun_now;
    state.kept_frame = frame;
    traced_early = state.early_frame == frame;
    ambient_written = state.ambient_frame == frame;
  }
  if (!traced_early
      && !TraceScene(context1.Get(), std::span(view).first(view_rows), sun_now,
                     {inputs[DEPTH].Get(), inputs[GBUFFER_A].Get(), inputs[GBUFFER_B].Get(), inputs[GBUFFER_C].Get()}, settings, frame)) {
    return {};
  }

  const auto [sun, ambient] = twattila::rt_scene::Visibilities();
  // The traced sun: the light attenuation copied (its shader reads the game's), then scaled.
  if (settings.sun_shadows && sun != nullptr && inputs[ATTENUATION] && inputs[GBUFFER_E]) {
    ComPtr<ID3D11Resource> attenuation;
    inputs[ATTENUATION]->GetResource(attenuation.GetAddressOf());
    ComPtr<ID3D11Texture2D> attenuation_texture;
    D3D11_TEXTURE2D_DESC desc = {};
    if (SUCCEEDED(attenuation.As(&attenuation_texture))) attenuation_texture->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC copy_desc = {};
    if (state.attenuation_copy) state.attenuation_copy->GetDesc(&copy_desc);
    if (desc.Width != copy_desc.Width || desc.Height != copy_desc.Height || desc.Format != copy_desc.Format) {
      D3D11_TEXTURE2D_DESC made = desc;
      made.MipLevels = 1;
      made.ArraySize = 1;
      made.Usage = D3D11_USAGE_DEFAULT;
      made.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      made.CPUAccessFlags = 0;
      made.MiscFlags = 0;
      const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_B8G8R8A8_UNORM, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                         .Texture2D = {.MipLevels = 1}};
      state.attenuation_copy.Reset();
      state.attenuation_copy_srv.Reset();
      if (desc.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return {};
      if (FAILED(device->CreateTexture2D(&made, nullptr, state.attenuation_copy.GetAddressOf()))
          || FAILED(device->CreateShaderResourceView(state.attenuation_copy.Get(), &view_desc, state.attenuation_copy_srv.GetAddressOf()))) {
        state.attenuation_copy.Reset();
        return {};
      }
    }
    if (auto* target = Target(device.Get(), attenuation.Get(), DXGI_FORMAT_B8G8R8A8_UNORM); target != nullptr && desc.MipLevels == 1) {
      context->CopyResource(state.attenuation_copy.Get(), attenuation.Get());
      const std::array<ID3D11ShaderResourceView*, 5> views = {state.attenuation_copy_srv.Get(), inputs[DEPTH].Get(),
                                                              inputs[GBUFFER_B].Get(), inputs[GBUFFER_E].Get(), sun};
      const std::array<ID3D11Buffer*, 2> constant_buffers = {buffers[0].Get(), buffers[1].Get()};
      FullScreen(context1.Get(), target, state.multiply_blue.Get(), state.shadow.Get(), views, constant_buffers, first_constants, constant_counts);
      contact_shadows::ray_traced.store(true, std::memory_order_relaxed);
      const std::unique_lock lock(state.mutex);
      state.shadowed_frame = frame;
    }
  }
  // The traced ambient: in place of the AO where the game made one (not its 1 x 1 default; R8, its one channel the AO),
  // unless the base pass AO took it this frame (OnBasePassAo).
  if (settings.ambient_occlusion && !ambient_written && ambient != nullptr && inputs[AMBIENT]) {
    ComPtr<ID3D11Resource> occlusion;
    inputs[AMBIENT]->GetResource(occlusion.GetAddressOf());
    ComPtr<ID3D11Texture2D> occlusion_texture;
    D3D11_TEXTURE2D_DESC desc = {};
    if (SUCCEEDED(occlusion.As(&occlusion_texture))) occlusion_texture->GetDesc(&desc);
    if (desc.Width >= render_size[0] && desc.Height >= render_size[1] && desc.Format == DXGI_FORMAT_R8_UNORM) {
      if (auto* target = Target(device.Get(), occlusion.Get(), DXGI_FORMAT_R8_UNORM); target != nullptr) {
        const std::array<ID3D11ShaderResourceView*, 1> views = {ambient};
        FullScreen(context1.Get(), target, nullptr, state.ambient.Get(), views);
      }
    }
  }
  if (!settings.light_bounce) return {};
  return {.post_callback = OnSunLit<Context>};
};

// Unreal's AO passes (GAME_AO_SHADERS) under the rays' AO: the trace at the first of them this frame, from the last
// frame's sun draw's scene depth and G-buffer when this pass reads the same depth and G-buffer A (t0, t1; Unreal 4.23
// draws its decals before its AO: the G-buffer is complete), its sun and this frame's View (ps b1). Traced: these passes
// left out, the base pass AO takes ours (OnBasePassAo). Not: the game's AO, the trace at the sun draw as before.
constexpr auto OnGameAo = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  std::array<ComPtr<ID3D11ShaderResourceView>, 4> kept;
  Sun sun;
  {
    const std::unique_lock lock(state.mutex);
    if (!state.settings.ambient_occlusion) return {};
    if (state.early_frame == state.frame) return {.bypass = true};
    if (state.early_tried_frame == state.frame || state.kept_frame + 1 != state.frame || !state.kept_inputs[0]) return {};
    state.early_tried_frame = state.frame;
    settings = state.settings;
    frame = state.frame;
    kept = state.kept_inputs;
    sun = state.kept_sun;
  }
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  for (UINT slot = 0; slot < 2; ++slot) {
    ComPtr<ID3D11ShaderResourceView> bound;
    context->PSGetShaderResources(slot, 1, bound.GetAddressOf());
    ComPtr<ID3D11Resource> bound_resource;
    ComPtr<ID3D11Resource> kept_resource;
    if (bound) bound->GetResource(bound_resource.GetAddressOf());
    kept[slot]->GetResource(kept_resource.GetAddressOf());
    if (!bound_resource || bound_resource != kept_resource) return {};
  }
  ComPtr<ID3D11Buffer> view_buffer;
  UINT first_constant = 0;
  UINT constant_count = 0;
  context1->PSGetConstantBuffers1(1, 1, view_buffer.GetAddressOf(), &first_constant, &constant_count);
  std::array<std::array<float, 4>, VIEW_ROWS> view = {};
  const UINT view_rows = view_buffer ? ReadView(view_buffer.Get(), first_constant, constant_count, &view) : 0;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (view_rows == 0 || !PrepareDevice(device.Get()) || !talesofarise::upscaling::PrepareBridge(device.Get())
      || !TraceScene(context1.Get(), std::span(view).first(view_rows), sun, {kept[0].Get(), kept[1].Get(), kept[2].Get(), kept[3].Get()},
                     settings, frame)) {
    return {};
  }
  const std::unique_lock lock(state.mutex);
  state.early_frame = frame;
  return {.bypass = true};
};

// The base pass AO (BASE_PASS_AO_SHADER) after the trace at Unreal's AO passes (OnGameAo, which left them out): the traced
// ambient into its AO (t1, R8) first; white without it.
constexpr auto OnBasePassAo = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  uint64_t frame = 0;
  {
    const std::unique_lock lock(state.mutex);
    if (state.early_frame != state.frame) return {};
    frame = state.frame;
  }
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  ComPtr<ID3D11ShaderResourceView> occlusion_view;
  context->PSGetShaderResources(1, 1, occlusion_view.GetAddressOf());
  if (!occlusion_view) return {};
  ComPtr<ID3D11Resource> occlusion;
  occlusion_view->GetResource(occlusion.GetAddressOf());
  ComPtr<ID3D11Texture2D> occlusion_texture;
  D3D11_TEXTURE2D_DESC desc = {};
  if (SUCCEEDED(occlusion.As(&occlusion_texture))) occlusion_texture->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_R8_UNORM || desc.Width < state.render_size[0] || desc.Height < state.render_size[1]) return {};
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  auto* target = Target(device.Get(), occlusion.Get(), DXGI_FORMAT_R8_UNORM);
  if (target == nullptr) return {};
  if (auto* ambient = twattila::rt_scene::Visibilities()[1]; ambient != nullptr) {
    FullScreen(context1.Get(), target, nullptr, state.ambient.Get(), std::span(&ambient, 1));
  } else {
    constexpr std::array<float, 4> WHITE = {1.f, 1.f, 1.f, 1.f};
    context->ClearRenderTargetView(target, WHITE.data());
  }
  const std::unique_lock lock(state.mutex);
  state.ambient_frame = frame;
  return {};
};

// Specular Occlusion without the ray-traced reflections, right after the game's screen-space reflections' draw: their
// target's alpha raised where the specular lobe is closed (0x7A1E0D47 with no reflections bound), so the reflection
// captures and the sky fill less there.
template <typename Context>
void OnReflected(Context& command, const void*) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return;
  auto* open = twattila::rt_scene::Reflections(context)[1];
  ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (open == nullptr || !target) return;
  const std::array<ID3D11ShaderResourceView*, 2> views = {nullptr, open};
  FullScreen(context1.Get(), target.Get(), state.occlude_alpha.Get(), state.reflection.Get(), views);
}

// The screen-space reflections' draw: the traced reflections into its target in its place (left out), with Specular
// Occlusion on the captures' share; the game's when not traced this frame (Specular Occlusion alone: OnReflected).
constexpr auto OnReflection = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  ComPtr<ID3D11DeviceContext1> context1;
  if (context == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) return {};
  bool reflections = false;
  bool occlusion = false;
  {
    const std::unique_lock lock(state.mutex);
    if (state.failed || !state.private_state) return {};
    reflections = state.settings.reflections;
    occlusion = state.settings.specular_occlusion;
  }
  if (!reflections && !occlusion) return {};
  if (!reflections) return {.post_callback = OnReflected<Context>};
  const auto traced = twattila::rt_scene::Reflections(context);
  ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (traced[0] == nullptr || !target) return {};
  const std::array<ID3D11ShaderResourceView*, 2> views = {traced[0], (occlusion ? traced[1] : nullptr)};
  FullScreen(context1.Get(), target.Get(), nullptr, state.reflection.Get(), views);
  return {.bypass = true};
};

// The sky light passes (shaders/skylight00-02): Path-Traced Ambient's switch at t120 while this frame's light bounce
// brought the paths' sky (OnSunLit; a frame without it keeps Unreal's diffuse sky light), unbound in a finalize callback.
constexpr auto OnSkyLight = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  ComPtr<ID3D11ShaderResourceView> path_ambient_switch;
  {
    const std::unique_lock lock(state.mutex);
    if (state.sky_frame != state.frame) return {};
    path_ambient_switch = state.path_ambient_switch;
  }
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  if (context == nullptr || !path_ambient_switch) return {};
  context->PSSetShaderResources(PATH_AMBIENT_SLOT, 1, path_ambient_switch.GetAddressOf());
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_context = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&post_context);
    if (post_context == nullptr) return;
    ID3D11ShaderResourceView* const none = nullptr;
    post_context->PSSetShaderResources(PATH_AMBIENT_SLOT, 1, &none);
  }};
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
    .capture_camera = [](ID3D11DeviceContext*, twattila::rt_scene::Camera*) { return true; },
    .scene_draw = internal::SceneDraw,
    .skinned_code = internal::Skinned,
    .characters_dynamic = true,
};

// Every present: the settings (the addon's; the light bounce's strength) and a new frame.
inline void Update(const twattila::rt_scene::Settings& settings, float bounce_strength) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.settings = settings;
    internal::state.bounce_strength = bounce_strength;
    ++internal::state.frame;
  }
  twattila::rt_scene::Update(settings);
}

enum class Traced : uint8_t {
  SUN_SHADOWS,
  LIGHT_BOUNCE,  // path traced or not
};

// Whether the rays' `effect` reached the scene within the last half second: the game's own method stays off meanwhile
// (Unreal's screen-space GI under the light bounce; its far sun cascades under the sun shadows with Two Shadow Cascades
// Under Rays).
inline bool TracedRecently(Traced effect) {
  constexpr uint64_t RECENT_FRAMES = 30;
  const std::unique_lock lock(internal::state.mutex);
  const uint64_t traced = (effect == Traced::SUN_SHADOWS ? internal::state.shadowed_frame : internal::state.resolved_frame);
  return traced != UINT64_MAX && traced + RECENT_FRAMES >= internal::state.frame;
}

// At the temporal pass: its scene colour (`color`, the render pixels at its top-left) is the helper's last frame's
// colour for the next trace's reflections and paths.
inline void KeepColor(ID3D11DeviceContext* context, ID3D11Texture2D* color) {
  auto& bridge = twattila::dlaa_bridge::bridge;
  std::array<UINT, 2> render_size;
  {
    const std::unique_lock lock(internal::state.mutex);
    if (!(internal::state.settings.reflections || internal::state.settings.path_tracing)) return;
    render_size = internal::state.render_size;
  }
  if (!bridge.color.texture || color == nullptr || render_size[0] == 0 || render_size[0] > bridge.width || render_size[1] > bridge.height) return;
  D3D11_TEXTURE2D_DESC desc = {};
  color->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Width < render_size[0] || desc.Height < render_size[1]) return;
  const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = render_size[0], .bottom = render_size[1], .back = 1};
  context->CopySubresourceRegion(bridge.color.texture.Get(), 0, 0, 0, 0, color, 0, &region);
}

// Before the contact shadows' callback on the same draw (it rebinds the light's constants).
inline void Install() {
  twattila::rt_scene::InstallScene({}, HOST);
  renodx::utils::command_action::Register(internal::OnLightRectangle, {.shader_hash = internal::LIGHT_RECTANGLE_SHADER,
                                                                       .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  renodx::utils::command_action::Register(internal::OnBasePassAo, {.shader_hash = internal::BASE_PASS_AO_SHADER,
                                                                   .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  for (const uint32_t game_ao_shader : internal::GAME_AO_SHADERS) {
    renodx::utils::command_action::Register(internal::OnGameAo, {.shader_hash = game_ao_shader,
                                                                 .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  renodx::utils::command_action::Register(internal::OnReflection, {.shader_hash = REFLECTION_SHADER,
                                                                   .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  for (const uint32_t sky_light_shader : internal::SKY_LIGHT_SHADERS) {
    renodx::utils::command_action::Register(internal::OnSkyLight, {.shader_hash = sky_light_shader,
                                                                   .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnLightRectangle);
  renodx::utils::command_action::Unregister(internal::OnBasePassAo);
  renodx::utils::command_action::Unregister(internal::OnGameAo);
  renodx::utils::command_action::Unregister(internal::OnReflection);
  renodx::utils::command_action::Unregister(internal::OnSkyLight);
  twattila::rt_scene::UninstallScene();
}

// The game's device is going away: the addon's objects on it with it.
inline void Release(reshade::api::device* device) {
  auto& state = internal::state;
  // The kept views let go after the lock (the game's texture may go with its last view).
  std::array<ComPtr<ID3D11ShaderResourceView>, 4> dropped_inputs;
  {
    const std::unique_lock lock(state.mutex);
    state.scene_depth = nullptr;
    dropped_inputs.swap(state.kept_inputs);
  }
  if (state.device.Get() == reinterpret_cast<ID3D11Device*>(device->get_native())) {
    state.device.Reset();
    state.private_state.Reset();
    state.targets.clear();
    state.attenuation_copy.Reset();
    state.attenuation_copy_srv.Reset();
    state.scene_views.clear();
    state.prepared = {};
    state.prepared_rtv.Reset();
    state.traced = {};
    state.resolved = {};
    state.resolved_positions = {};
    state.bounce_size = {};
    state.path_ambient_switch.Reset();
    state.failed = false;
  }
  twattila::rt_scene::OnDestroyDevice(device);
  twattila::dlaa_bridge::OnDestroyDevice(reinterpret_cast<ID3D11Device*>(device->get_native()));
}

}  // namespace expeditionsrome::dlss::ray_tracing
