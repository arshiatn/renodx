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
#include <cstring>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>

#include "../../utils/device_proxy.hpp"
#include "../../utils/state.hpp"
#include "../twattila/rt_core.hpp"
#include "./bridge.hpp"
#include "./ambient_shaders.h"
#include "./bridge_replay.hpp"
#include "./patched_shaders.h"
#include "./sun_shaders.h"

// R.U.S.E.'s ray-traced ground shadows (tools/nontw/ruse/plan.md 10b A5, G1): the TW helper traces the sun through the
// scene the draw replay captured (rt_core.hpp, a clip-space host on the DX11 proxy's device: InstallReplayedScene). The
// ground's sun shadow is a screen-space mask: the pre-pass (the 32 terrain patches and the camera pass 0x88292FC6) draws
// it, StretchRect copies it into a texture whose red the 110 terrain and 94 water pixel shaders read by screen position
// and multiply into the sun's colour alone. Right after that copy, at the main pass's first draw: the pre-pass's depth
// (INTZ) into the bridge's shared depth, the last frame's draws into the scene in this frame's camera (CaptureScene), the
// trace, and the traced visibility into the mask's red in place of the game's shadow-map term (the pre-pass writes only
// that term there; a frame without a trace keeps the game's).
// Objects (O1) shade with the game's shadow maps alone: their pixels have no depth before the main pass, so last frame's
// main depth (kept at the bloom) goes into this frame's view over the ground's and the rays start from both; the object
// shaders (patched_shaders.h) read the traced visibility at s15 by screen position in place of their shadow-map factor
// (its alpha 1), an alpha-0 texel (the game's factor) when untraced. The ground's ambient occlusion (AO) comes from the
// same trace: the terrain shaders read it at s14 into their ambient.
// Screen-space ground shadows (G2): Bend Studio's march (Attila's sss_setup / sss_bend) over the same depth, into the
// mask by minimum with the game's shadow maps (off under the rays: one method a shadow). The ground's ambient occlusion
// without rays: GT-VBAO
// (ground_vbao.cs_5_0) over the same depth into the same occlusion texture, without the helper too (the bridge's
// shared textures alone: MakeObjects). The ground's bounced light: GT-VBGI (ground_bounce.cs_5_0, the same march) with
// last frame's scene as the light, which the terrain shaders add to their ambient, with the occlusion in the same s14
// texel (s12, the one other sampler the patches could take, is the game's uniDiversityMap in 23 shaders). Water
// reflections: a screen-space march (water_reflections.cs_5_0) over the same depth with last frame's main depth in it,
// its hits read in last frame's scene; the water shaders blend it over their cube map by its share at s13.
// The rays' bounce and reflections (plan.md 11c): the patched terrain, object and water shaders also write guides into
// three more render targets of the main pass (oC1 albedo, oC2 world normal, oC3 the water's surface: D3D9 takes four
// targets, one blend state, a write mask each); at the next trace they go where this frame's start depth was then, into
// the helper's guides, and the water's surface into the start depth (the water writes no depth). The helper's Light
// Bounce (its rays, or path tracing with the game's ambient replaced by the paths' sky) over last frame's scene as the
// light becomes the s14 texel; its reflections (a mirror on the water) the s13 texel.
namespace ruse::ray_tracing {

// The rays' ambient range in world units: 2 m if a unit is a centimetre (eye heights near 67,000 on the B0 trace; the
// T-run checks).
constexpr float AMBIENT_RANGE = 200.f;
// GT-VBAO's radius and its samples' thickness, world units (the hollows and slopes of terrain: ten times the rays'; the
// T-run checks).
constexpr float VBAO_RADIUS = 2000.f;
constexpr float VBAO_THICKNESS = 500.f;
// The bounced light's strength (1: the scene's light as it was).
constexpr float BOUNCE_STRENGTH = 1.f;
// The water reflections' reach and the thickness a depth sample stands for, world units (1 km and 5 m if a unit is a
// centimetre; the T-run checks).
constexpr float REFLECTION_REACH = 100000.f;
constexpr float REFLECTION_THICKNESS = 500.f;
// The rays' Light Bounce: a texel each BOUNCE_SCALE x BOUNCE_SCALE pixels, paths a texel, their reach past the first
// surface in world units (10 m if a unit is a centimetre; the T-run checks).
constexpr UINT BOUNCE_SCALE = 2;
constexpr uint32_t BOUNCE_RAYS = 2;
constexpr float BOUNCE_RANGE = 1000.f;

using Microsoft::WRL::ComPtr;

struct State {
  bool ground = false;  // the Ray-Traced Ground Shadows row (restart; with the anti-aliasing's helper)
  bool objects = false;  // the Ray-Traced Object Shadows row (restart; with the ground's)
  bool ambient = false;  // the Ray-Traced Ground Ambient Occlusion row (restart; with the ground's)
  bool bend = false;  // the Screen-Space Ground Shadows row (restart; with the anti-aliasing's bridge; off under `ground`)
  bool vbao = false;  // the Screen-Space Ground Ambient Occlusion row (restart; no helper needed)
  bool bounce = false;  // the Ground Light Bounce row (restart; with the anti-aliasing's bridge)
  bool reflections = false;  // the Water Reflections row (restart; with the anti-aliasing's bridge)
  uint32_t traced_bounce = 0;  // the Ground Light Bounce row's rays (restart; with the ground's): 1 rays, 2 path tracing
  bool traced_reflections = false;  // the Water Reflections row's rays (restart; with the ground's)
  bool guides = false;  // either: the guide targets and the helper's guides
  bool failed = false;
  bool tracing = false;  // the last trace's answer, and its changes logged this start (a few: never a line a frame)
  uint32_t changes_logged = 0;
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
  uint32_t visibility_frame = UINT32_MAX;  // the frame whose rays it holds (in place of the game's shadow maps then)
  ComPtr<ID3D11ShaderResourceView> depth_view;  // the bridge's shared depth
  ComPtr<ID3D11ComputeShader> copy;
  // O1: last frame's main depth with its camera (the inverse view-projection) and jitter (NDC), the rays' start depth
  // (R32 typeless: float to trace, uint to scatter by minimum), the object shaders' s15 when untraced.
  uint32_t kept_frame = UINT32_MAX;
  uint32_t bound_frame = UINT32_MAX;
  std::array<float, 16> kept_inverse = {};
  std::array<float, 2> kept_jitter = {};
  ComPtr<ID3D11Texture2D> kept_depth;
  ComPtr<ID3D11ShaderResourceView> kept_depth_view;
  ComPtr<ID3D11Texture2D> start_depth;
  ComPtr<ID3D11ShaderResourceView> start_depth_view;
  ComPtr<ID3D11UnorderedAccessView> start_depth_uav;
  ComPtr<ID3D11ComputeShader> reproject;
  ComPtr<ID3D11Buffer> reprojection;
  ComPtr<IDirect3DTexture9> unshadowed;
  // AO: the traced ambient occlusion for the D3D9 pass (R32F), and the frame it holds.
  bridge::SharedTexture occlusion;
  uint32_t occlusion_frame = UINT32_MAX;
  ComPtr<ID3D11ComputeShader> vbao_shader;
  ComPtr<ID3D11Buffer> vbao_settings;  // ground_vbao's and ground_bounce's: projection, size, range, to last frame
  // The bounced light (s14 with the occlusion: x it, yzw the light) and the water reflections (s13): last frame's scene
  // (the bridge's colour, from its bloom) read by both, their textures for the D3D9 pass (RGBA16F), the frames they hold;
  // the rays' occlusion for the bounce's texel.
  ComPtr<ID3D11ShaderResourceView> light_view;
  ComPtr<ID3D11ShaderResourceView> occlusion_view;
  ComPtr<ID3D11ComputeShader> bounce_shader;
  bridge::SharedTexture ground_light;
  uint32_t bounce_frame = UINT32_MAX;
  ComPtr<ID3D11ComputeShader> reflection_shader;
  ComPtr<ID3D11Buffer> reflection_settings;  // water_reflections': the eye-relative matrices, size, projection, range
  bridge::SharedTexture reflection;
  uint32_t reflection_frame = UINT32_MAX;
  ComPtr<IDirect3DTexture9> unlit;  // s13 / the bounce's s14 when untraced: (1, 0, 0, 0), the game's shading
  // The guide targets (render targets 1-3 from the trace until the bloom: `guides_frame`'s main pass, bound beside a
  // frame-sized scene target), what the gather reads of them, and the frame the helper's guides hold; last frame's eye.
  bridge::SharedTexture guide_albedo;  // B8G8R8A8: oC1
  bridge::SharedTexture guide_normal;  // RGBA16F: oC2
  bridge::SharedTexture guide_water;   // RGBA16F: oC3
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> guide_views;
  ComPtr<ID3D11ComputeShader> guides_shader;
  ComPtr<ID3D11Buffer> guide_settings;
  uint32_t guides_frame = UINT32_MAX;
  bool guides_closed = true;
  bool guides_bound = false;
  uint32_t gathered_frame = UINT32_MAX;
  std::array<double, 3> kept_eye = {};
  // The rays' Light Bounce: its prepared light in (R11G11B10F at the bounce's texels), its bounce out (RGBA16F), into the
  // s14 texel; the reflections into the s13 texel; the game's ambient (last frame's, gamma 2.2) for Path-Traced Ambient.
  ComPtr<ID3D11Texture2D> prepared_light;
  ComPtr<ID3D11UnorderedAccessView> prepared_light_uav;
  ComPtr<ID3D11Texture2D> traced_light;
  ComPtr<ID3D11ShaderResourceView> traced_light_view;
  ComPtr<ID3D11ComputeShader> bounce_light_shader;
  ComPtr<ID3D11Buffer> bounce_light_settings;
  ComPtr<ID3D11ComputeShader> texels_shader;
  ComPtr<ID3D11ComputeShader> reflection_texels_shader;
  ComPtr<ID3D11Buffer> texel_settings;
  std::array<float, 3> ambient_colour = {};
  uint32_t ambient_frame = UINT32_MAX;
  // G2: sss_setup's dispatch list (camera_VS_PS b0, lighting_VS_PS b1, sss_settings b2), sss_bend's eight indirect
  // dispatches (b3 each one's index) into the occlusion, ground_bend's minimum into the visibility.
  struct Bend {
    ComPtr<ID3D11ComputeShader> setup;
    ComPtr<ID3D11ComputeShader> march;
    ComPtr<ID3D11ComputeShader> combine;
    ComPtr<ID3D11Buffer> camera;
    ComPtr<ID3D11Buffer> lighting;
    ComPtr<ID3D11Buffer> settings;
    std::array<ComPtr<ID3D11Buffer>, 8> dispatches;
    ComPtr<ID3D11Buffer> arguments;
    ComPtr<ID3D11UnorderedAccessView> arguments_uav;
    ComPtr<ID3D11Buffer> parameters;
    ComPtr<ID3D11UnorderedAccessView> parameters_uav;
    ComPtr<ID3D11ShaderResourceView> parameters_srv;
    ComPtr<ID3D11Texture2D> occlusion;
    ComPtr<ID3D11UnorderedAccessView> occlusion_uav;
    ComPtr<ID3D11ShaderResourceView> occlusion_srv;
    ComPtr<ID3D11SamplerState> border;
  } bend_objects;
};
inline State state;

// sss.hlsli's sss_settings (Attila's screen_space_shadows.hpp ShaderSettings): Attila's defaults, no extras.
struct BendSettings {
  float width = 0.f;
  float height = 0.f;
  float inverse_width = 0.f;
  float inverse_height = 0.f;
  float thickness = 0.005f;
  float samples = 240.f;
  float contrast = 4.f;
  float edge_threshold = 0.02f;
  float hard_samples = 4.f;
  float fade_samples = 8.f;
  float ignore_edges = 0.f;
  float precision_offset = 0.f;
  float sample_along_ray = 0.f;
  float softness = 0.f;
  float debug_view = 0.f;
  float mirror_edges = 0.f;
  float sun_direction = 0.f;  // lighting_VS_PS's float the sun's travel direction starts at
  float render_width = 0.f;
  float render_height = 0.f;
  float foliage_light = 0.f;
  float foliage_bit = 0.f;
  std::array<float, 3> padding = {};
};

// Any row that shades the ground or the water from the main pass's first draw (ShadeGround).
inline bool Shading() {
  return state.ground || state.bend || state.vbao || state.bounce || state.reflections;
}

inline const twattila::rt_scene::Host HOST = {
    .render_size = [] { return std::array<UINT, 2>{bridge::state.width, bridge::state.height}; },
    .guides_written = [] { return state.gathered_frame == bridge::state.frame; },
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

// The ground's sun shadows once a frame (the rays, Bend's march or both) into the ground mask, before the main pass draws
// with it; true when the shared visibility holds this frame's.
inline bool ShadeGround(reshade::api::command_list* cmd_list) {
  auto& bridge = bridge::state;
  auto* proxy_reshade = renodx::utils::device_proxy::proxy_device_reshade;
  if (state.failed || bridge.failed || proxy_reshade == nullptr || (state.ground && state.proxy_queue == nullptr)
      || !bridge.camera_valid || !bridge.intz_depths.contains(state.prepass_depth)) {
    return false;
  }
  auto* host = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());
  // The anti-aliasing's bridge once it runs; without it (GT-VBAO alone) its shared textures and passes only, tried once.
  if (!bridge.ready) {
    if (bridge.enabled || !state.vbao) return false;
    if (!bridge::MakeObjects(host, __bridge_quad, __bridge_prepare, __bridge_writeback)) {
      state.failed = true;
      reshade::log::message(reshade::log::level::warning, "[R.U.S.E.] Ground ambient occlusion: the shared textures weren't made: off");
      return false;
    }
  }
  auto* proxy = reinterpret_cast<ID3D11Device*>(proxy_reshade->get_native());
  ComPtr<ID3D11DeviceContext> context;
  proxy->GetImmediateContext(context.GetAddressOf());
  if (state.shadows == nullptr) {
    state.failed = true;
    ComPtr<IDirect3DDevice9Ex> host_ex;
    bool made = SUCCEEDED(host->QueryInterface(IID_PPV_ARGS(host_ex.GetAddressOf())))
                      && SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(__ground_depth.data()), state.depth_copy.ReleaseAndGetAddressOf()))
                      && SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(__ground_shadows.data()), state.shadows.ReleaseAndGetAddressOf()))
                      && bridge::MakeShared(proxy, host_ex.Get(), D3DFMT_R32F, &state.visibility, true)
                      && SUCCEEDED(proxy->CreateShaderResourceView(bridge.depth.proxy.Get(), nullptr, state.depth_view.ReleaseAndGetAddressOf()))
                      && SUCCEEDED(proxy->CreateComputeShader(__rt_ground_copy.data(), __rt_ground_copy.size(), nullptr, state.copy.ReleaseAndGetAddressOf()));
    const auto compute = [proxy](std::span<const uint8_t> code, ComPtr<ID3D11ComputeShader>* shader) {
      return SUCCEEDED(proxy->CreateComputeShader(code.data(), code.size(), nullptr, shader->ReleaseAndGetAddressOf()));
    };
    const auto constants = [proxy](UINT size, ComPtr<ID3D11Buffer>* buffer) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = size, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      return SUCCEEDED(proxy->CreateBuffer(&desc, nullptr, buffer->ReleaseAndGetAddressOf()));
    };
    if (made && state.bend) {
      auto& bend = state.bend_objects;
      const D3D11_BUFFER_DESC arguments_desc = {
          .ByteWidth = 8 * 12,
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
          .MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
      };
      const D3D11_UNORDERED_ACCESS_VIEW_DESC arguments_view = {
          .Format = DXGI_FORMAT_R32_TYPELESS,
          .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
          .Buffer = {.FirstElement = 0, .NumElements = 8 * 3, .Flags = D3D11_BUFFER_UAV_FLAG_RAW},
      };
      const D3D11_BUFFER_DESC parameters_desc = {
          .ByteWidth = 9 * 16,
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
          .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
          .StructureByteStride = 16,
      };
      const D3D11_TEXTURE2D_DESC occlusion_desc = {
          .Width = bridge.width,
          .Height = bridge.height,
          .MipLevels = 1,
          .ArraySize = 1,
          .Format = DXGI_FORMAT_R8_UNORM,
          .SampleDesc = {.Count = 1},
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
      };
      // Off screen: the far depth (Bend reads past the edges on purpose).
      const D3D11_SAMPLER_DESC border_desc = {
          .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
          .AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
          .AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
          .AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
          .ComparisonFunc = D3D11_COMPARISON_NEVER,
          .BorderColor = {1.f, 1.f, 1.f, 1.f},
          .MaxLOD = D3D11_FLOAT32_MAX,
      };
      made = compute(__sss_setup, &bend.setup) && compute(__sss_bend, &bend.march) && compute(__ground_bend, &bend.combine)
             && constants(13 * 16, &bend.camera) && constants(4 * 16, &bend.lighting) && constants(sizeof(BendSettings), &bend.settings)
             && SUCCEEDED(proxy->CreateBuffer(&arguments_desc, nullptr, bend.arguments.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateUnorderedAccessView(bend.arguments.Get(), &arguments_view, bend.arguments_uav.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateBuffer(&parameters_desc, nullptr, bend.parameters.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateUnorderedAccessView(bend.parameters.Get(), nullptr, bend.parameters_uav.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateShaderResourceView(bend.parameters.Get(), nullptr, bend.parameters_srv.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateTexture2D(&occlusion_desc, nullptr, bend.occlusion.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateUnorderedAccessView(bend.occlusion.Get(), nullptr, bend.occlusion_uav.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateShaderResourceView(bend.occlusion.Get(), nullptr, bend.occlusion_srv.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateSamplerState(&border_desc, bend.border.ReleaseAndGetAddressOf()));
      for (UINT index = 0; made && index < bend.dispatches.size(); ++index) {
        const std::array<UINT, 4> value = {index, 0, 0, 0};
        const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
        const D3D11_SUBRESOURCE_DATA data = {.pSysMem = value.data()};
        made = SUCCEEDED(proxy->CreateBuffer(&desc, &data, bend.dispatches[index].ReleaseAndGetAddressOf()));
      }
      if (made) {
        const BendSettings settings = {
            .width = static_cast<float>(bridge.width),
            .height = static_cast<float>(bridge.height),
            .inverse_width = 1.f / static_cast<float>(bridge.width),
            .inverse_height = 1.f / static_cast<float>(bridge.height),
            .render_width = static_cast<float>(bridge.width),
            .render_height = static_cast<float>(bridge.height),
        };
        context->UpdateSubresource(bend.settings.Get(), 0, nullptr, &settings, 0, 0);
      }
    }
    if (made && (state.vbao || state.bounce)) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 32 * sizeof(float), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      made = SUCCEEDED(proxy->CreateBuffer(&desc, nullptr, state.vbao_settings.ReleaseAndGetAddressOf()))
             && (!state.vbao || SUCCEEDED(proxy->CreateComputeShader(__ground_vbao.data(), __ground_vbao.size(), nullptr, state.vbao_shader.ReleaseAndGetAddressOf())));
    }
    if (made && (state.bounce || state.reflections)) {
      made = SUCCEEDED(proxy->CreateShaderResourceView(bridge.color.proxy.Get(), nullptr, state.light_view.ReleaseAndGetAddressOf()));
    }
    if (made && state.bounce) {
      made = SUCCEEDED(proxy->CreateComputeShader(__ground_bounce.data(), __ground_bounce.size(), nullptr, state.bounce_shader.ReleaseAndGetAddressOf()))
             && bridge::MakeShared(proxy, host_ex.Get(), D3DFMT_A16B16G16R16F, &state.ground_light, true);
    }
    if (made && state.reflections) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 60 * sizeof(float), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      made = SUCCEEDED(proxy->CreateComputeShader(__water_reflections.data(), __water_reflections.size(), nullptr,
                                                  state.reflection_shader.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateBuffer(&desc, nullptr, state.reflection_settings.ReleaseAndGetAddressOf()))
             && bridge::MakeShared(proxy, host_ex.Get(), D3DFMT_A16B16G16R16F, &state.reflection, true);
    }
    if (made && state.guides) {
      made = bridge::MakeShared(proxy, host_ex.Get(), D3DFMT_A8R8G8B8, &state.guide_albedo)
             && bridge::MakeShared(proxy, host_ex.Get(), D3DFMT_A16B16G16R16F, &state.guide_normal)
             && bridge::MakeShared(proxy, host_ex.Get(), D3DFMT_A16B16G16R16F, &state.guide_water)
             && SUCCEEDED(proxy->CreateShaderResourceView(state.guide_albedo.proxy.Get(), nullptr, state.guide_views[0].ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateShaderResourceView(state.guide_normal.proxy.Get(), nullptr, state.guide_views[1].ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateShaderResourceView(state.guide_water.proxy.Get(), nullptr, state.guide_views[2].ReleaseAndGetAddressOf()))
             && compute(__rt_guides, &state.guides_shader) && constants(44 * sizeof(float), &state.guide_settings)
             && constants(12 * sizeof(float), &state.texel_settings);
    }
    if (made && state.traced_bounce != 0) {
      D3D11_TEXTURE2D_DESC desc = {
          .Width = (bridge.width + BOUNCE_SCALE - 1) / BOUNCE_SCALE,
          .Height = (bridge.height + BOUNCE_SCALE - 1) / BOUNCE_SCALE,
          .MipLevels = 1,
          .ArraySize = 1,
          .Format = DXGI_FORMAT_R11G11B10_FLOAT,
          .SampleDesc = {.Count = 1},
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
      };
      made = SUCCEEDED(proxy->CreateTexture2D(&desc, nullptr, state.prepared_light.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateUnorderedAccessView(state.prepared_light.Get(), nullptr, state.prepared_light_uav.ReleaseAndGetAddressOf()));
      desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      made = made && SUCCEEDED(proxy->CreateTexture2D(&desc, nullptr, state.traced_light.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateShaderResourceView(state.traced_light.Get(), nullptr, state.traced_light_view.ReleaseAndGetAddressOf()))
             && compute(__rt_bounce_light, &state.bounce_light_shader) && compute(__rt_texels, &state.texels_shader)
             && constants(24 * sizeof(float), &state.bounce_light_settings);
    }
    if (made && state.traced_reflections) {
      made = compute(__rt_reflection_texels, &state.reflection_texels_shader);
    }
    if (made && (state.ambient || state.vbao)) {
      made = bridge::MakeShared(proxy, host_ex.Get(), D3DFMT_R32F, &state.occlusion, true)
             && (!state.bounce || SUCCEEDED(proxy->CreateShaderResourceView(state.occlusion.work.Get(), nullptr, state.occlusion_view.ReleaseAndGetAddressOf())));
    }
    if (made && (state.objects || state.reflections || state.guides)) {
      D3D11_TEXTURE2D_DESC desc = {
          .Width = bridge.width,
          .Height = bridge.height,
          .MipLevels = 1,
          .ArraySize = 1,
          .Format = DXGI_FORMAT_R32_FLOAT,
          .SampleDesc = {.Count = 1},
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_SHADER_RESOURCE,
      };
      const D3D11_SHADER_RESOURCE_VIEW_DESC start_view = {.Format = DXGI_FORMAT_R32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                          .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
      const D3D11_UNORDERED_ACCESS_VIEW_DESC start_output = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
      const D3D11_BUFFER_DESC constants = {.ByteWidth = 24 * sizeof(float), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const bool kept = SUCCEEDED(proxy->CreateTexture2D(&desc, nullptr, state.kept_depth.ReleaseAndGetAddressOf()))
                        && SUCCEEDED(proxy->CreateShaderResourceView(state.kept_depth.Get(), nullptr, state.kept_depth_view.ReleaseAndGetAddressOf()));
      desc.Format = DXGI_FORMAT_R32_TYPELESS;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
      made = kept && SUCCEEDED(proxy->CreateTexture2D(&desc, nullptr, state.start_depth.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateShaderResourceView(state.start_depth.Get(), &start_view, state.start_depth_view.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateUnorderedAccessView(state.start_depth.Get(), &start_output, state.start_depth_uav.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateComputeShader(__object_depth.data(), __object_depth.size(), nullptr, state.reproject.ReleaseAndGetAddressOf()))
             && SUCCEEDED(proxy->CreateBuffer(&constants, nullptr, state.reprojection.ReleaseAndGetAddressOf()));
    }
    if (!made) {
      state.shadows.Reset();
      state.kept_depth.Reset();
      reshade::log::message(reshade::log::level::warning, "[R.U.S.E.] Ground shadows: their objects weren't made: off");
      return false;
    }
    state.failed = false;
  }
  ComPtr<IDirect3DTexture9> prepass_depth;
  ComPtr<IDirect3DSurface9> mask_surface;
  if (FAILED(reinterpret_cast<IDirect3DSurface9*>(state.prepass_depth)->GetContainer(IID_PPV_ARGS(prepass_depth.GetAddressOf())))
      || FAILED(reinterpret_cast<IDirect3DTexture9*>(state.ground_mask)->GetSurfaceLevel(0, mask_surface.GetAddressOf()))) {
    return false;
  }
  const std::array<IDirect3DSurface9*, 1> depth_target = {bridge.depth.host_surface.Get()};
  const std::array<IDirect3DBaseTexture9*, 1> depth_source = {prepass_depth.Get()};
  bridge::DrawQuad(host, depth_target, state.depth_copy.Get(), depth_source, {});
  bridge::WaitHost(host);

  // The camera: the camera pass's inverse view-projection (rows acting on column vectors) and its inverse.
  const float* inverse = bridge.camera.data() + 16;
  std::array<double, 16> view_projection = {};
  std::array<double, 3> eye = {};
  if (!bridge::CurrentCamera(&view_projection, &eye)) return false;
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
  // This frame's clip to last frame's (c0-c3 x c4-c7); an offset from this frame's eye to this frame's clip and to last
  // frame's (the view-projections with the eye's translation folded in): in doubles (world coordinates near 2e6).
  std::array<float, 16> to_previous = {};
  std::array<float, 16> to_clip = {};
  std::array<float, 16> to_previous_clip = {};
  for (size_t row = 0; row < 4; ++row) {
    for (size_t column = 0; column < 4; ++column) {
      double value = 0.0;
      for (size_t k = 0; k < 4; ++k) value += static_cast<double>(bridge.camera[(row * 4) + k]) * static_cast<double>(inverse[(k * 4) + column]);
      to_previous[(row * 4) + column] = static_cast<float>(value);
    }
    double now = view_projection[(row * 4) + 3];
    double previous = bridge.camera[(row * 4) + 3];
    for (size_t axis = 0; axis < 3; ++axis) {
      to_clip[(row * 4) + axis] = static_cast<float>(view_projection[(row * 4) + axis]);
      to_previous_clip[(row * 4) + axis] = bridge.camera[(row * 4) + axis];
      now += view_projection[(row * 4) + axis] * eye[axis];
      previous += static_cast<double>(bridge.camera[(row * 4) + axis]) * eye[axis];
    }
    to_clip[(row * 4) + 3] = static_cast<float>(now);
    to_previous_clip[(row * 4) + 3] = static_cast<float>(previous);
  }
  const auto [p00, p11, depth_scale, depth_offset] = bridge::ProjectionTerms(view_projection);
  const std::array<float, 4> size = {static_cast<float>(bridge.width), static_cast<float>(bridge.height), 1.f / static_cast<float>(bridge.width),
                                     1.f / static_cast<float>(bridge.height)};

  // O1 (and the water reflections: the water's surface; the guides): last frame's depth (clip -> clip by this frame's
  // view-projection x its inverse, in doubles) over the ground's. Moved units are a frame behind; where nothing lands,
  // the ground's depth.
  ID3D11ShaderResourceView* start_depth = state.depth_view.Get();
  if ((state.objects || state.reflections || state.guides) && state.kept_frame + 1 == bridge.frame) {
    std::array<float, 24> reprojection = {};
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        double value = 0.0;
        for (int k = 0; k < 4; ++k) value += view_projection[(row * 4) + k] * static_cast<double>(state.kept_inverse[(k * 4) + column]);
        reprojection[(row * 4) + column] = static_cast<float>(value);
      }
    }
    reprojection[16] = state.kept_jitter[0];
    reprojection[17] = state.kept_jitter[1];
    reprojection[18] = 2.f * bridge.jitter[0] / static_cast<float>(bridge.width);
    reprojection[19] = -2.f * bridge.jitter[1] / static_cast<float>(bridge.height);
    reprojection[20] = static_cast<float>(bridge.width);
    reprojection[21] = static_cast<float>(bridge.height);
    reprojection[22] = 1.f / static_cast<float>(bridge.width);
    reprojection[23] = 1.f / static_cast<float>(bridge.height);
    context->UpdateSubresource(state.reprojection.Get(), 0, nullptr, reprojection.data(), 0, 0);
    context->CopyResource(state.start_depth.Get(), bridge.depth.proxy.Get());
    ID3D11ShaderResourceView* kept = state.kept_depth_view.Get();
    ID3D11UnorderedAccessView* scattered = state.start_depth_uav.Get();
    ID3D11Buffer* constants = state.reprojection.Get();
    context->CSSetShader(state.reproject.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, &kept);
    context->CSSetUnorderedAccessViews(0, 1, &scattered, nullptr);
    context->CSSetConstantBuffers(0, 1, &constants);
    context->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
    ID3D11ShaderResourceView* no_view = nullptr;
    ID3D11UnorderedAccessView* no_output = nullptr;
    ID3D11Buffer* no_constants = nullptr;
    context->CSSetShaderResources(0, 1, &no_view);
    context->CSSetUnorderedAccessViews(0, 1, &no_output, nullptr);
    context->CSSetConstantBuffers(0, 1, &no_constants);
    start_depth = state.start_depth_view.Get();
  }
  // The guides: last frame's targets where this frame's start depth was then, into the helper's (the water's surface into
  // the start depth first: the rays start there).
  auto& helper = twattila::dlaa_bridge::bridge;
  if (state.guides && start_depth == state.start_depth_view.Get() && state.guides_closed && state.guides_frame == state.kept_frame
      && helper.normals.uav) {
    std::array<float, 44> settings = {};
    std::ranges::copy(to_previous, settings.begin());
    std::ranges::copy(to_clip, settings.begin() + 16);
    for (size_t axis = 0; axis < 3; ++axis) settings[32 + axis] = static_cast<float>(state.kept_eye[axis] - eye[axis]);
    std::ranges::copy(size, settings.begin() + 36);
    const std::array<float, 4> projection = {p00, p11, depth_scale, depth_offset};
    std::ranges::copy(projection, settings.begin() + 40);
    context->UpdateSubresource(state.guide_settings.Get(), 0, nullptr, settings.data(), 0, 0);
    const std::array<ID3D11ShaderResourceView*, 4> inputs = {state.guide_views[0].Get(), state.guide_views[1].Get(), state.guide_views[2].Get(),
                                                             state.kept_depth_view.Get()};
    const std::array<ID3D11UnorderedAccessView*, 4> outputs = {state.start_depth_uav.Get(), helper.diffuse_albedo.uav.Get(), helper.normals.uav.Get(),
                                                               helper.roughness.uav.Get()};
    constexpr std::array<ID3D11ShaderResourceView*, 4> NO_INPUTS = {};
    constexpr std::array<ID3D11UnorderedAccessView*, 4> NO_GUIDE_OUTPUTS = {};
    context->CSSetShader(state.guides_shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 4, inputs.data());
    context->CSSetUnorderedAccessViews(0, 4, outputs.data(), nullptr);
    context->CSSetConstantBuffers(0, 1, state.guide_settings.GetAddressOf());
    context->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
    context->CSSetShaderResources(0, 4, NO_INPUTS.data());
    context->CSSetUnorderedAccessViews(0, 4, NO_GUIDE_OUTPUTS.data(), nullptr);
    state.gathered_frame = bridge.frame;
  }

  // G1: the rays (and the ambient's); without them (Bend alone, or no trace this frame) the visibility starts at 1.
  bool traced = false;
  if (state.ground) {
    twattila::rt_scene::WriteSun(context.Get(), state.toward_sun);
    replay::CaptureScene(state.proxy_queue->get_immediate_command_list(), view_projection);
    traced = twattila::rt_scene::Trace(context.Get(), start_depth, camera);
    if (traced != state.tracing && state.changes_logged <= 8) {
      ++state.changes_logged;
      reshade::log::message(
          reshade::log::level::info,
          std::format("[R.U.S.E.] Ray tracing: {}{}",
                      (traced ? std::format("tracing (ground shadows{}{}{}{})", (state.objects ? ", object shadows" : ""),
                                            (state.ambient ? ", ambient occlusion" : ""),
                                            (state.traced_bounce == 1 ? ", Light Bounce" : (state.traced_bounce == 2 ? ", path-traced Light Bounce" : "")),
                                            (state.traced_reflections ? ", water reflections" : ""))
                              : std::string("not tracing (the helper's scene isn't ready): the game's shadows this frame")),
                      (state.changes_logged > 8 ? " (no more of these lines this start)" : ""))
              .c_str());
    }
    state.tracing = traced;
  }
  const auto [visibility, occlusion] = (traced ? twattila::rt_scene::Visibilities() : std::array<ID3D11ShaderResourceView*, 2>{});
  if (visibility == nullptr && !state.bend && !state.vbao && !state.bounce && !state.reflections) return false;
  constexpr std::array<ID3D11ShaderResourceView*, 3> NO_VIEWS = {};
  constexpr std::array<ID3D11UnorderedAccessView*, 2> NO_OUTPUTS = {};
  constexpr std::array<ID3D11Buffer*, 4> NO_CONSTANTS = {};
  // 8 x 8 groups over the bridge's size: `shader` from `source` into `destination` (the helper's R8 results into the
  // shared textures' work copies, R32F; Bend's occlusion into the visibility), copied into the shared ones after. The
  // output first and unbound after: D3D11 binds a view of a resource still bound as an output as null (a pass reading
  // the one before it read nothing: Bend's combine, GT-VBGI's occlusion).
  const auto run = [&](ID3D11ComputeShader* shader, ID3D11ShaderResourceView* source, ID3D11UnorderedAccessView* destination) {
    context->CSSetShader(shader, nullptr, 0);
    context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
    context->CSSetShaderResources(0, 1, &source);
    context->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
    context->CSSetUnorderedAccessViews(0, 1, NO_OUTPUTS.data(), nullptr);
  };
  if (visibility != nullptr) {
    run(state.copy.Get(), visibility, state.visibility.work_uav.Get());
    state.visibility_frame = bridge.frame;
  } else {
    constexpr std::array<FLOAT, 4> LIT = {1.f, 1.f, 1.f, 1.f};
    context->ClearUnorderedAccessViewFloat(state.visibility.work_uav.Get(), LIT.data());
  }
  // The bounce and the reflections read last frame's scene: the bridge's colour, written at its bloom.
  const bool light_kept = bridge.depth_frame + 1 == bridge.frame;
  const bool rays_occlusion = state.ambient && occlusion != nullptr;
  if (rays_occlusion) {
    run(state.copy.Get(), occlusion, state.occlusion.work_uav.Get());
    state.occlusion_frame = bridge.frame;
  }
  // The rays' Light Bounce: the light leaving what each texel shows (last frame's scene there) to the helper, its bounce
  // into the s14 texel (below, with the occlusion). Path tracing: the paths' sky is the game's ambient (last frame's,
  // linear) in the helper's ambient cube, and once the helper replaces the ambient with it (its frames of history) the
  // texel's x takes the game's away.
  const UINT bounce_width = (bridge.width + BOUNCE_SCALE - 1) / BOUNCE_SCALE;
  const UINT bounce_height = (bridge.height + BOUNCE_SCALE - 1) / BOUNCE_SCALE;
  bool bounced = false;
  if (traced && state.traced_bounce != 0 && light_kept) {
    if (state.traced_bounce == 2 && state.ambient_frame != UINT32_MAX && helper.rt_sun.texture) {
      std::array<float, 24> cube = {};
      for (size_t side = 0; side < 6; ++side) {
        for (size_t channel = 0; channel < 3; ++channel) cube[(side * 4) + channel] = std::pow(std::max(state.ambient_colour[channel], 0.f), 2.2f);
      }
      cube[3] = 1.f;  // texel 3's w: the cube holds the game's
      cube[7] = 1.f;  // texel 4's w: its scale
      const D3D11_BOX cube_texels = {.left = 3, .top = 0, .front = 0, .right = 9, .bottom = 1, .back = 1};
      context->UpdateSubresource(helper.rt_sun.texture.Get(), 0, &cube_texels, cube.data(), static_cast<UINT>(cube.size() * sizeof(float)), 0);
    }
    std::array<float, 24> light_settings = {};
    std::ranges::copy(to_previous, light_settings.begin());
    std::ranges::copy(size, light_settings.begin() + 16);
    const std::array<UINT, 4> bounce_texels = {bounce_width, bounce_height, BOUNCE_SCALE, 0u};
    std::memcpy(light_settings.data() + 20, bounce_texels.data(), sizeof(bounce_texels));
    context->UpdateSubresource(state.bounce_light_settings.Get(), 0, nullptr, light_settings.data(), 0, 0);
    const std::array<ID3D11ShaderResourceView*, 2> light_inputs = {start_depth, state.light_view.Get()};
    context->CSSetShader(state.bounce_light_shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, light_inputs.data());
    context->CSSetUnorderedAccessViews(0, 1, state.prepared_light_uav.GetAddressOf(), nullptr);
    context->CSSetConstantBuffers(0, 1, state.bounce_light_settings.GetAddressOf());
    context->Dispatch((bounce_width + 7) / 8, (bounce_height + 7) / 8, 1);
    context->CSSetShaderResources(0, 2, NO_VIEWS.data());
    context->CSSetUnorderedAccessViews(0, 1, NO_OUTPUTS.data(), nullptr);
    bounced = twattila::rt_scene::internal::TraceLightBounce(context.Get(), state.prepared_light.Get(), bounce_width, bounce_height, BOUNCE_SCALE,
                                                             state.traced_light.Get());
  }
  // The rays' reflections (after the bounce: their hits read its prepared light), the share that met a surface with them.
  bool reflected = false;
  if (traced && state.traced_reflections) {
    if (ID3D11ShaderResourceView* reflection = twattila::rt_scene::Reflections(context.Get())[0]; reflection != nullptr) {
      run(state.reflection_texels_shader.Get(), reflection, state.reflection.work_uav.Get());
      context->CSSetShaderResources(0, 1, NO_VIEWS.data());
      state.reflection_frame = bridge.frame;
      reflected = true;
    }
  }
  // Without the rays this frame, GT-VBGI and the screen-space march.
  const bool bouncing = state.bounce && light_kept && !bounced;
  // GT-VBAO where the rays' ambient occlusion didn't run this frame; with the bounce GT-VBGI's march instead, its light
  // and the occlusion (the rays', else its own with the GT-VBAO row) in one texel. Their noise still without the upscaler
  // (no frames to settle it). The bounce's light: this frame's clip to last frame's (c0-c3 x c4-c7, in doubles).
  if (bouncing || (state.vbao && !rays_occlusion)) {
    std::array<float, 32> settings = {
        p00, p11, depth_scale, depth_offset, size[0], size[1], size[2], size[3], VBAO_RADIUS, VBAO_THICKNESS, static_cast<float>(bridge.enabled ? (bridge.frame % 64u) : 0u), BOUNCE_STRENGTH,
    };
    std::ranges::copy(to_previous, settings.begin() + 12);
    settings[28] = (rays_occlusion ? 2.f : (state.vbao ? 1.f : 0.f));
    context->UpdateSubresource(state.vbao_settings.Get(), 0, nullptr, settings.data(), 0, 0);
    context->CSSetConstantBuffers(0, 1, state.vbao_settings.GetAddressOf());
    if (bouncing) {
      const std::array<ID3D11ShaderResourceView*, 2> inputs = {state.light_view.Get(), state.occlusion_view.Get()};
      context->CSSetShaderResources(1, 2, inputs.data());
      run(state.bounce_shader.Get(), start_depth, state.ground_light.work_uav.Get());
      state.bounce_frame = bridge.frame;
    } else {
      run(state.vbao_shader.Get(), start_depth, state.occlusion.work_uav.Get());
      state.occlusion_frame = bridge.frame;
    }
    context->CSSetShaderResources(0, 3, NO_VIEWS.data());
    context->CSSetConstantBuffers(0, 1, NO_CONSTANTS.data());
  }
  // The rays' bounce into the s14 texel with the occlusion: the rays', else GT-VBAO's just above (its own work copy).
  if (bounced) {
    const bool replaced = state.traced_bounce == 2 && twattila::rt_scene::internal::runtime.path_ambient_on;
    ID3D11ShaderResourceView* bounce_occlusion = occlusion;
    if (!rays_occlusion) {
      bounce_occlusion = ((state.occlusion_frame == bridge.frame) ? state.occlusion_view.Get() : nullptr);
    }
    std::array<float, 12> texel_settings = {BOUNCE_STRENGTH};
    const std::array<UINT, 8> counts = {bounce_width, bounce_height, BOUNCE_SCALE, bridge.width, (replaced ? 1u : 0u),
                                        (bounce_occlusion != nullptr ? 1u : 0u), bridge.height, 0u};
    std::memcpy(texel_settings.data() + 4, counts.data(), sizeof(counts));
    context->UpdateSubresource(state.texel_settings.Get(), 0, nullptr, texel_settings.data(), 0, 0);
    context->CSSetConstantBuffers(0, 1, state.texel_settings.GetAddressOf());
    const std::array<ID3D11ShaderResourceView*, 2> bounce_inputs = {helper.rt_positions.srv.Get(), bounce_occlusion};
    context->CSSetShaderResources(1, 2, bounce_inputs.data());
    run(state.texels_shader.Get(), state.traced_light_view.Get(), state.ground_light.work_uav.Get());
    context->CSSetShaderResources(0, 3, NO_VIEWS.data());
    context->CSSetConstantBuffers(0, 1, NO_CONSTANTS.data());
    state.bounce_frame = bridge.frame;
  }
  // Water reflections: the march's matrices act on offsets from this frame's eye (world coordinates near 2e6): this
  // frame's clip to an offset (rt's relative rows, the inverse's w row), an offset to this frame's clip and to last
  // frame's (the view-projections with the eye's translation folded in, in doubles).
  if (state.reflections && light_kept && !reflected) {
    std::array<float, 60> settings = {};
    std::copy_n(camera.relative_inverse.begin(), 12, settings.begin());
    std::copy_n(inverse + 12, 4, settings.begin() + 12);
    std::ranges::copy(to_clip, settings.begin() + 16);
    std::ranges::copy(to_previous_clip, settings.begin() + 32);
    const std::array<float, 12> tail = {
        size[0], size[1], size[2], size[3], p00, p11, depth_scale, depth_offset, REFLECTION_REACH, REFLECTION_THICKNESS, 0.f, 0.f,
    };
    std::ranges::copy(tail, settings.begin() + 48);
    context->UpdateSubresource(state.reflection_settings.Get(), 0, nullptr, settings.data(), 0, 0);
    context->CSSetConstantBuffers(0, 1, state.reflection_settings.GetAddressOf());
    context->CSSetShaderResources(1, 1, state.light_view.GetAddressOf());
    run(state.reflection_shader.Get(), start_depth, state.reflection.work_uav.Get());
    context->CSSetShaderResources(0, 2, NO_VIEWS.data());
    context->CSSetConstantBuffers(0, 1, NO_CONSTANTS.data());
    state.reflection_frame = bridge.frame;
  }
  // G2: Bend's dispatch list for this camera and sun (camera_VS_PS: the projection's depth at infinity in _m22 / _m23,
  // the view-projection, both read as column-major: our rows as they are), its march over the rays' start depth.
  if (state.bend) {
    auto& bend = state.bend_objects;
    // A perspective view-projection's depth row is a x its w row + b: a, the depth at infinity, from the w row's largest.
    size_t axis = 0;
    for (size_t column = 1; column < 3; ++column) {
      if (std::abs(view_projection[12 + column]) > std::abs(view_projection[12 + axis])) axis = column;
    }
    std::array<float, 13 * 4> camera_registers = {};
    camera_registers[(5 * 4) + 10] = static_cast<float>(view_projection[8 + axis] / view_projection[12 + axis]);
    camera_registers[(5 * 4) + 14] = 1.f;
    for (size_t index = 0; index < 16; ++index) camera_registers[(9 * 4) + index] = static_cast<float>(view_projection[index]);
    const std::array<float, 16> lighting = {-state.toward_sun[0], -state.toward_sun[1], -state.toward_sun[2], 0.f};
    context->UpdateSubresource(bend.camera.Get(), 0, nullptr, camera_registers.data(), 0, 0);
    context->UpdateSubresource(bend.lighting.Get(), 0, nullptr, lighting.data(), 0, 0);
    constexpr std::array<FLOAT, 4> NONE = {};
    context->ClearUnorderedAccessViewFloat(bend.occlusion_uav.Get(), NONE.data());
    const std::array<ID3D11UnorderedAccessView*, 2> list = {bend.arguments_uav.Get(), bend.parameters_uav.Get()};
    const std::array<ID3D11Buffer*, 3> list_constants = {bend.camera.Get(), bend.lighting.Get(), bend.settings.Get()};
    context->CSSetShader(bend.setup.Get(), nullptr, 0);
    context->CSSetUnorderedAccessViews(0, 2, list.data(), nullptr);
    context->CSSetConstantBuffers(0, 3, list_constants.data());
    context->Dispatch(1, 1, 1);
    context->CSSetUnorderedAccessViews(0, 2, NO_OUTPUTS.data(), nullptr);
    const std::array<ID3D11ShaderResourceView*, 2> march_inputs = {start_depth, bend.parameters_srv.Get()};
    ID3D11UnorderedAccessView* march_output = bend.occlusion_uav.Get();
    context->CSSetShader(bend.march.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, march_inputs.data());
    context->CSSetUnorderedAccessViews(0, 1, &march_output, nullptr);
    context->CSSetSamplers(0, 1, bend.border.GetAddressOf());
    for (UINT index = 0; index < bend.dispatches.size(); ++index) {
      context->CSSetConstantBuffers(3, 1, bend.dispatches[index].GetAddressOf());
      context->DispatchIndirect(bend.arguments.Get(), index * 12);
    }
    context->CSSetShaderResources(0, 2, NO_VIEWS.data());
    run(bend.combine.Get(), bend.occlusion_srv.Get(), state.visibility.work_uav.Get());
    ID3D11SamplerState* no_sampler = nullptr;
    context->CSSetSamplers(0, 1, &no_sampler);
    context->CSSetConstantBuffers(0, 4, NO_CONSTANTS.data());
  }
  context->CSSetShaderResources(0, 1, NO_VIEWS.data());
  context->CSSetUnorderedAccessViews(0, 1, NO_OUTPUTS.data(), nullptr);
  context->CSSetShader(nullptr, nullptr, 0);
  for (const auto& [shared, written] : {std::pair{&state.visibility, visibility != nullptr || state.bend},
                                        std::pair{&state.occlusion, state.occlusion_frame == bridge.frame},
                                        std::pair{&state.ground_light, state.bounce_frame == bridge.frame},
                                        std::pair{&state.reflection, state.reflection_frame == bridge.frame}}) {
    if (written) {
      context->CopyResource(shared->proxy.Get(), shared->work.Get());
    }
  }
  bridge::WaitProxy(proxy, context.Get());

  const std::array<IDirect3DSurface9*, 1> mask_target = {mask_surface.Get()};
  const std::array<IDirect3DBaseTexture9*, 1> shaded = {state.visibility.host.Get()};
  if (visibility != nullptr) {
    // The rays in place of the game's shadow maps: the pre-pass (0x8588D239) writes only their term into the mask's red.
    // A frame without a trace keeps the game's.
    bridge::DrawQuad(host, mask_target, state.shadows.Get(), shaded, {}, {{D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED}});
  } else if (state.bend) {
    // Bend with the game's shadow maps (the contact shadows they miss): the darker of the two.
    bridge::DrawQuad(host, mask_target, state.shadows.Get(), shaded, {},
                     {{D3DRS_ALPHABLENDENABLE, TRUE}, {D3DRS_SEPARATEALPHABLENDENABLE, FALSE}, {D3DRS_BLENDOP, D3DBLENDOP_MIN},
                      {D3DRS_SRCBLEND, D3DBLEND_ONE}, {D3DRS_DESTBLEND, D3DBLEND_ONE}, {D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED}});
  }
  return true;
}

// The patched shaders' textures, read by VPOS (point, clamped: one texel a pixel): s15 the sun visibility (O1's object
// shaders' shadow: the rays' where its alpha is 1, the R32F texture's), s14 the ambient
// occlusion (the terrain's; with the bounce x it, yzw the light), s13 the reflection (the water's); white or
// (1, 0, 0, 0) (the game's shading: alpha 0 keeps its shadow maps) unless `traced` this frame.
inline void BindTraced(IDirect3DDevice9* host, bool traced) {
  const auto bind = [host](DWORD stage, IDirect3DTexture9* texture) {
    host->SetTexture(stage, texture);
    host->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    host->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    host->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    host->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    host->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    host->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
  };
  if (state.objects) {
    bind(15, (traced && state.visibility_frame == bridge::state.frame) ? state.visibility.host.Get() : state.unlit.Get());
  }
  if (state.bounce) {
    bind(14, (traced && state.bounce_frame == bridge::state.frame) ? state.ground_light.host.Get() : state.unlit.Get());
  } else if (state.ambient || state.vbao) {
    bind(14, (traced && state.occlusion_frame == bridge::state.frame) ? state.occlusion.host.Get() : state.unshadowed.Get());
  }
  if (state.reflections) {
    bind(13, (traced && state.reflection_frame == bridge::state.frame) ? state.reflection.host.Get() : state.unlit.Get());
  }
}

// The guide targets bound beside the scene's (`bind`) or not, with the write masks a draw needs.
inline void BindGuides(IDirect3DDevice9* host, bool bind) {
  if (bind != state.guides_bound) {
    state.guides_bound = bind;
    host->SetRenderTarget(1, bind ? state.guide_albedo.host_surface.Get() : nullptr);
    host->SetRenderTarget(2, bind ? state.guide_normal.host_surface.Get() : nullptr);
    host->SetRenderTarget(3, bind ? state.guide_water.host_surface.Get() : nullptr);
  }
  if (!bind) {
    host->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
    host->SetRenderState(D3DRS_COLORWRITEENABLE2, 0xF);
    host->SetRenderState(D3DRS_COLORWRITEENABLE3, 0xF);
  }
}

// The bloom's first draw, before the bridge's passes: this frame's guides closed (their targets unbound).
inline void OnBloomStart(reshade::api::command_list* cmd_list) {
  if (state.guides_frame != bridge::state.frame || state.guides_closed) return;
  BindGuides(reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native()), false);
  state.guides_closed = true;
}

// At the bloom: the patched shaders' textures white again (the sprite atlas draws with the object shaders before the
// next main pass). O1: the bridge's prepare pass has just written this frame's main depth into its shared depth: kept
// for the next frame's trace, since the shadow-map, sprite and ground passes reuse the game's depth before then.
inline void OnBloom(reshade::api::command_list* cmd_list) {
  auto& bridge = bridge::state;
  auto* proxy_reshade = renodx::utils::device_proxy::proxy_device_reshade;
  BindTraced(reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native()), false);
  if (!(state.objects || state.reflections || state.guides) || state.kept_depth == nullptr || proxy_reshade == nullptr
      || bridge.depth_frame != bridge.frame) {
    return;
  }
  ComPtr<ID3D11DeviceContext> context;
  reinterpret_cast<ID3D11Device*>(proxy_reshade->get_native())->GetImmediateContext(context.GetAddressOf());
  context->CopyResource(state.kept_depth.Get(), bridge.depth.proxy.Get());
  std::memcpy(state.kept_inverse.data(), bridge.camera.data() + 16, sizeof(state.kept_inverse));
  state.kept_jitter = {2.f * bridge.jitter[0] / static_cast<float>(bridge.width), -2.f * bridge.jitter[1] / static_cast<float>(bridge.height)};
  std::array<double, 16> view_projection = {};
  if (bridge::CurrentCamera(&view_projection, &state.kept_eye)) {
    state.kept_frame = bridge.frame;
  }
}

// Every D3D9 draw while this frame's guides are open: their targets beside a frame-sized scene target (none beside any
// other: a draw's targets must have one size; none once closed, or a frame without a bloom left them open), written by the
// patched shaders alone (D3D9 has one blend state, but a write mask for each target): oC1 / oC2 by the terrain's and the
// objects' with the rays' bounce, oC2 / oC3 by the water's with the rays' reflections.
inline void OnGuideDraw(reshade::api::command_list* cmd_list) {
  const bool open = state.guides_frame == bridge::state.frame && !state.guides_closed;
  if (!open && !state.guides_bound) return;
  auto* host = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());
  const bool beside = open && bridge::state.render_target_is_frame_sized;
  BindGuides(host, beside);
  if (!beside) return;
  const uint32_t pixel_shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
  const auto listed = [pixel_shader](std::span<const PatchedShader> table) {
    const auto found = std::ranges::lower_bound(table, pixel_shader, {}, &PatchedShader::hash);
    return found != table.end() && found->hash == pixel_shader;
  };
  const bool lit = state.traced_bounce != 0 && (listed(GROUND_LIGHT_GUIDE_SHADERS) || (state.objects && listed(OBJECT_LIGHT_SHADERS)));
  const bool water = state.traced_reflections && listed(WATER_REFLECTION_GUIDE_SHADERS);
  host->SetRenderState(D3DRS_COLORWRITEENABLE1, lit ? 0xF : 0);
  host->SetRenderState(D3DRS_COLORWRITEENABLE2, (lit || water) ? 0xF : 0);
  host->SetRenderState(D3DRS_COLORWRITEENABLE3, water ? 0xF : 0);
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
  // The patched shaders' textures white from each frame's first scene draw until a trace.
  auto* host = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());
  if ((state.objects || state.ambient || state.vbao || state.bounce || state.reflections) && state.bound_frame != bridge.frame) {
    state.bound_frame = bridge.frame;
    const auto make_texel = [host](D3DCOLOR color, ComPtr<IDirect3DTexture9>* texture) {
      if (*texture != nullptr) return;
      ComPtr<IDirect3DSurface9> texel;
      if (FAILED(host->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, texture->GetAddressOf(), nullptr))
          || FAILED((*texture)->GetSurfaceLevel(0, texel.GetAddressOf())) || FAILED(host->ColorFill(texel.Get(), nullptr, color))) {
        texture->Reset();
      }
    };
    make_texel(D3DCOLOR_ARGB(255, 255, 255, 255), &state.unshadowed);
    make_texel(D3DCOLOR_ARGB(0, 255, 0, 0), &state.unlit);
    BindTraced(host, false);
  }
  // Path-Traced Ambient's sky: the game's ambient from the first terrain shader drawn (the next frame's trace takes it).
  if (state.traced_bounce == 2 && state.ambient_frame != bridge.frame) {
    const uint32_t pixel_shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
    const auto found = std::ranges::lower_bound(AMBIENT_COLOUR_SHADERS, pixel_shader, {}, &SunRegister::hash);
    if (found != AMBIENT_COLOUR_SHADERS.end() && found->hash == pixel_shader) {
      std::copy_n(bridge.pixel_registers.data() + (found->register_index * 4), 3, state.ambient_colour.begin());
      state.ambient_frame = bridge.frame;
    }
  }
  if (state.mask_frame != bridge.frame || state.traced_frame == bridge.frame || bridge.depth_stencil == state.prepass_depth) return;
  state.traced_frame = bridge.frame;
  if (state.guides_bound) {
    BindGuides(host, false);
  }
  if (ShadeGround(cmd_list)) {
    BindTraced(host, true);
  }
  // This frame's guides from here to the bloom (the gather above read last frame's and the proxy waited for it).
  if (state.guides && state.guide_water.host_surface != nullptr) {
    for (IDirect3DSurface9* guide : {state.guide_albedo.host_surface.Get(), state.guide_normal.host_surface.Get(), state.guide_water.host_surface.Get()}) {
      host->ColorFill(guide, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
    }
    state.guides_frame = bridge.frame;
    state.guides_closed = false;
  }
}

// The game's present: rt_core's new frame.
inline void OnPresent() {
  twattila::rt_scene::Update({
      .sun_shadows = state.ground,
      .ambient_occlusion = state.ambient,
      .ambient_range = AMBIENT_RANGE,
      .light_bounce = state.traced_bounce != 0,
      .light_bounce_rays = BOUNCE_RAYS,
      .light_bounce_range = BOUNCE_RANGE,
      .path_tracing = state.traced_bounce == 2,
      .path_ambient = state.traced_bounce == 2,
      .reflections = state.traced_reflections,
  });
}

// The game's device's objects (its shaders, the mask, the shared textures, the white texels) with its device and at its
// swap chain's end (a D3D9 Reset: the size may change): the next ShadeGround makes them again, its D3D11 ones with them
// at the bridge's size; last frame's depth is gone with them.
inline void ReleaseHost() {
  state.depth_copy.Reset();
  state.shadows.Reset();
  state.unshadowed.Reset();
  state.unlit.Reset();
  state.bound_frame = UINT32_MAX;
  state.kept_frame = UINT32_MAX;
  state.visibility = {};
  state.occlusion = {};
  state.ground_light = {};
  state.reflection = {};
  state.guide_albedo = {};
  state.guide_normal = {};
  state.guide_water = {};
  state.guides_closed = true;
  state.guides_bound = false;
  state.ground_mask = 0;
  state.mask_frame = UINT32_MAX;
  state.prepass_depth = 0;
  state.prepass_target = 0;
  state.failed = false;
}

// The proxy's device (rt_core's objects, ours on it) or the game's (its shaders, the mask) going; no other device (rt_core
// forgets its scene at any D3D11 device's end).
inline void OnDestroyDevice(reshade::api::device* device) {
  const bool proxy = renodx::utils::device_proxy::IsProxyDevice(device);
  if (!proxy && device != bridge::state.host_device) return;
  if (proxy) {
    twattila::rt_scene::OnDestroyDevice(device);
    state.visibility = {};
    state.depth_view.Reset();
    state.copy.Reset();
    state.shadows.Reset();
    state.proxy_queue = nullptr;
    state.kept_depth.Reset();
    state.kept_depth_view.Reset();
    state.start_depth.Reset();
    state.start_depth_view.Reset();
    state.start_depth_uav.Reset();
    state.reproject.Reset();
    state.reprojection.Reset();
    state.kept_frame = UINT32_MAX;
    state.occlusion = {};
    state.vbao_shader.Reset();
    state.vbao_settings.Reset();
    state.light_view.Reset();
    state.occlusion_view.Reset();
    state.bounce_shader.Reset();
    state.ground_light = {};
    state.reflection_shader.Reset();
    state.reflection_settings.Reset();
    state.reflection = {};
    state.bend_objects = {};
    state.guide_albedo = {};
    state.guide_normal = {};
    state.guide_water = {};
    state.guide_views = {};
    state.guides_shader.Reset();
    state.guide_settings.Reset();
    state.prepared_light.Reset();
    state.prepared_light_uav.Reset();
    state.traced_light.Reset();
    state.traced_light_view.Reset();
    state.bounce_light_shader.Reset();
    state.bounce_light_settings.Reset();
    state.texels_shader.Reset();
    state.reflection_texels_shader.Reset();
    state.texel_settings.Reset();
    state.failed = false;
  } else {
    ReleaseHost();
  }
}

}  // namespace ruse::ray_tracing
