/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <type_traits>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./device_end.hpp"
#include "./dlaa_bridge.hpp"
#include "./frame_history.hpp"
#include "./light_bounce.h"

// Light Bounce (Experimental): screen-space indirect light with the game's deferred sun light draws (each lighting
// model draws its own pixels, by stencil), their camera and G-buffer, bound again at the addon's own slots (t42-t45,
// b11) for its shaders. After the frame's first standard draw (most of the screen lit by then), at full, half, quarter
// or eighth resolution with the addon's full-screen triangle: a copy of the light buffer (t40; with the game's MSAA its
// resolve), the prepare pass
// (light_bounce_prepare: light, position and normal per texel, t46-t47; with Multi-Bounce the last trace's output at
// t41 too) and its levels at 1/2 to 1/32 of that
// (light_bounce_levels: the trace's far samples read them, in the texture cache), then the trace (light_bounce_trace)
// into the raw target (light_bounce_trace, the history at t41 for Checkerboard), the resolve (light_bounce_resolve: this
// frame and the last frames' bounce held within the frame's neighbourhood) into one of two history textures, reading
// the other (t41) and the last frame's camera (b10, a copy made after each trace), and with the Denoiser
// (light_bounce_denoise: 16 taps along the surface) into the denoised target the apply then reads.
// After every sun draw: the apply (light_bounce_apply), that draw again (its vertices, depth / stencil: its pixels),
// added to the light buffer; draws before the trace take the last frame's. Everything it binds is put back after.
// With the game's MSAA (its *_ms sun light draws: a multisampled light buffer and G-buffer) the prepare and the apply are
// their *_ms builds (light_bounce_gbuffer.hlsli: the prepare reads sample 0, the apply each sample, or with the Fast
// MSAA setting once per pixel at sample 0: its *_ms_pixel build). D3D11, the main
// view (the display's size). Every Total War game with the Light Bounce, each with its own light_bounce_*.hlsl wrappers
// of these shaders (its G-buffer's decode: light_bounce.hlsli).
namespace twattila::light_bounce {

using Microsoft::WRL::ComPtr;

// SunDraw::ambient_final_sites' Warhammer II form (screen_space_shadows::PatchAmbient): one place, after the spherical
// harmonics' if (on lighting_VS_PS c11.y). _Z: Warhammer's, the if on c11.z.
constexpr uint32_t AMBIENT_AFTER_HARMONICS = UINT32_MAX;
constexpr uint32_t AMBIENT_AFTER_HARMONICS_Z = UINT32_MAX - 1;

// One of the game's sun light draws: its pixel shader, whether it is the standard lighting model's (the trace follows
// the frame's first), its G-buffer's first slot (normal, colour, constants, depth: 4), its camera's, its lighting_VS_PS'
// and sun_direction's first float there (the screen-space shadows'), its camera's layout when not Attila's (for the
// screen-space shadows and ao_filter.hpp; the Light Bounce's shaders declare each game's own), and its ambient
// occlusion texture's slot (ao_filter.hpp; none: UINT_MAX).
struct SunDraw {
  uint32_t shader_hash = 0u;
  bool standard = false;
  UINT gbuffer_slot = 0;
  UINT camera_slot = 0;
  UINT lighting_slot = 2;
  UINT sun_direction = 1;  // c0.y
  std::span<const frame_history::CameraRun> camera_layout = {};
  UINT ao_slot = UINT_MAX;
  // Warhammer III: its cb_ssao, whose two overscans say where the AO texture holds a pixel (UINT_MAX: where it is on screen).
  UINT ssao_constants_slot = UINT_MAX;
  // How many places its final ambient is made in (screen_space_shadows::PatchAmbient; 0: the first cube read, Attila's;
  // AMBIENT_AFTER_HARMONICS: Warhammer II's one place; AMBIENT_AFTER_HARMONICS_Z: Warhammer's).
  uint32_t ambient_final_sites = 0;
  // For its light_bounce.hlsli's BounceDiffuseAlbedo, where a game's sun shaders differ.
  float diffuse_rule = 0.f;
  // Warhammer III: its t_world_spherical_harmonics, for the sky rt_sun builds (UINT_MAX: rt_sun reads no texture).
  UINT sky_harmonics_slot = UINT_MAX;
  // Warhammer III: the slot of its environment_PS constants, its t_specular_array and its sampler (the same in its sun
  // shaders), for the sky rt_sun builds as the game's light probes see it (UINT_MAX: none).
  UINT environment_slot = UINT_MAX;
};

// The Light Bounce sliders, as the addon's settings hold them (strength 0: off).
struct Sliders {
  float strength = 0.f;
  float radius = 3.f;      // metres
  float thickness = 0.15f;  // metres, the ambient occlusion's
  float quality = 0.f;     // 8 x 2^this samples per side of a direction (0-2)
  float directions = 1.f;  // 2^this directions (0-4)
  float resolution = 1.f;  // the trace at 1 / 2^this of the resolution (0-3)
  float temporal = 1.f;    // 1: the last frames' bounce kept (with the noise moving every frame)
  float msaa_pixel = 0.f;  // 1: with the game's MSAA the apply once per pixel (else each sample)
  float multi_bounce = 0.f;  // 0-1: the share of the last frame's bounce that bounces again (0: one bounce)
  float checkerboard = 0.f;  // 1: with temporal, half the texels traced each frame
  float backface = 0.f;        // 1: light from a surface's back side rejected
  float auto_thickness = 0.f;  // 1: each occluder as thick as it is wide
  float denoise = 0.f;         // 1: the resolved bounce smoothed along surfaces before the apply
};

namespace internal {

// The addon's views at t40-t48.
constexpr UINT VIEWS_SLOT = 40;
constexpr size_t LIGHT_VIEW = 0;     // the light copy
constexpr size_t HISTORY_VIEW = 1;   // the trace's last output, then the apply's bounce
constexpr size_t GBUFFER_VIEW = 2;   // the sun draw's 4 G-buffer channels
constexpr size_t PREPARED_VIEW = 6;  // the prepared light and geometry
constexpr size_t FRAMES_VIEW = 8;    // the resolve's last frame counts
constexpr UINT VIEW_COUNT = 9;
// Its constant buffers at b10-b12: the last frame's camera, the sun draw's camera, the Light Bounce constants.
constexpr UINT CONSTANTS_SLOT = 10;
constexpr UINT CONSTANTS_COUNT = 3;
constexpr float HISTORY_SHARE = 0.95f;  // at most (about the last 20 frames; fewer where newly seen)
constexpr UINT PREPARED_LEVELS = 6;     // the prepared textures' own size and 1/2 to 1/32 of it

// A prepared texture: the prepare draws its first level, light_bounce_levels each next one from the one above; the trace
// reads every level.
struct PreparedTarget {
  ComPtr<ID3D11Texture2D> texture;
  std::array<ComPtr<ID3D11RenderTargetView>, PREPARED_LEVELS> level_rtvs;
  std::array<ComPtr<ID3D11ShaderResourceView>, PREPARED_LEVELS> level_srvs;
  ComPtr<ID3D11ShaderResourceView> srv;  // every level
};

// Made on the game's device, again when it changes.
struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> prepare;
  ComPtr<ID3D11PixelShader> prepare_msaa;
  ComPtr<ID3D11PixelShader> levels;
  ComPtr<ID3D11PixelShader> trace;
  ComPtr<ID3D11PixelShader> resolve;
  ComPtr<ID3D11PixelShader> denoise;
  ComPtr<ID3D11PixelShader> apply;
  ComPtr<ID3D11PixelShader> apply_msaa;
  ComPtr<ID3D11PixelShader> apply_msaa_pixel;
  ComPtr<ID3D11BlendState> replace_blend;
  ComPtr<ID3D11BlendState> add_blend;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ComPtr<ID3D11DepthStencilState> no_depth;
  ComPtr<ID3D11Buffer> constants;
  LightBounceConstants uploaded = {};
  ComPtr<ID3D11Buffer> previous_camera;
  // The light buffer's copy (its size); the prepared light and geometry and the two histories (1 / scale of it).
  D3D11_TEXTURE2D_DESC light_desc = {};
  DXGI_FORMAT light_view_format = DXGI_FORMAT_UNKNOWN;
  UINT bounce_scale = 0;
  ComPtr<ID3D11Texture2D> light_copy;
  ComPtr<ID3D11ShaderResourceView> light_copy_srv;
  PreparedTarget prepared_light;
  PreparedTarget prepared_geometry;
  UINT prepared_levels = 0;
  frame_history::Target traced;  // this frame's trace
  std::array<frame_history::Target, 2> history;
  std::array<frame_history::Target, 2> history_frames;  // each history's frame count per texel
  size_t history_index = 0;  // the one the last resolve wrote
  frame_history::Target denoised;
  uint64_t traced_frame = UINT64_MAX;
};

struct Runtime {
  std::shared_mutex mutex;
  // Set by the addon every present.
  LightBounceConstants settings = {};
  uint64_t frame = 0;
  UINT display_width = 0;
  UINT display_height = 0;
  bool apply_per_pixel = false;  // with the game's MSAA
  bool denoise = false;
  DeviceObjects objects;
};

// 2^`exponent` (0 to `most`).
inline float PowerOfTwo(float exponent, float most) {
  return static_cast<float>(1u << static_cast<uint32_t>(std::clamp(exponent, 0.f, most)));
}

static Runtime runtime;
// The trace through rays (rt_scene.hpp: the prepared light's first level, its texels' size and scale, the raw target;
// false: the screen-space trace); null: off.
using RayTracedTrace = bool (*)(ID3D11DeviceContext* context, ID3D11Texture2D* prepared_light, UINT width, UINT height, UINT scale,
                                ID3D11Texture2D* traced);
static std::atomic<RayTracedTrace> ray_traced_trace = nullptr;

// The shaders and states on `device`, then the textures for this light buffer and scale. Under the lock.
inline bool Prepare(ID3D11DeviceContext* context, ID3D11Device* device, const D3D11_TEXTURE2D_DESC& light_desc,
                    DXGI_FORMAT light_view_format, UINT scale) {
  auto& objects = runtime.objects;
  if (objects.device.Get() != device) {
    objects = {};
    objects.device = device;
    D3D11_BLEND_DESC replace = {};
    replace.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_BLEND_DESC add = {};
    add.RenderTarget[0] = {
        .BlendEnable = TRUE,
        .SrcBlend = D3D11_BLEND_ONE,
        .DestBlend = D3D11_BLEND_ONE,
        .BlendOp = D3D11_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D11_BLEND_ZERO,
        .DestBlendAlpha = D3D11_BLEND_ONE,
        .BlendOpAlpha = D3D11_BLEND_OP_ADD,
        .RenderTargetWriteMask = static_cast<UINT8>(D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN
                                                    | D3D11_COLOR_WRITE_ENABLE_BLUE),
    };
    const D3D11_RASTERIZER_DESC rasterizer = {
        .FillMode = D3D11_FILL_SOLID,
        .CullMode = D3D11_CULL_NONE,
        .DepthClipEnable = TRUE,
    };
    const D3D11_DEPTH_STENCIL_DESC no_depth = {};
    const D3D11_BUFFER_DESC constants = {
        .ByteWidth = sizeof(LightBounceConstants),
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    objects.failed =
        FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, objects.full_screen.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_prepare.data(), __light_bounce_prepare.size(), nullptr, objects.prepare.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_prepare_ms.data(), __light_bounce_prepare_ms.size(), nullptr, objects.prepare_msaa.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_levels.data(), __light_bounce_levels.size(), nullptr, objects.levels.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_trace.data(), __light_bounce_trace.size(), nullptr, objects.trace.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_resolve.data(), __light_bounce_resolve.size(), nullptr, objects.resolve.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_denoise.data(), __light_bounce_denoise.size(), nullptr, objects.denoise.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_apply.data(), __light_bounce_apply.size(), nullptr, objects.apply.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_apply_ms.data(), __light_bounce_apply_ms.size(), nullptr, objects.apply_msaa.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__light_bounce_apply_ms_pixel.data(), __light_bounce_apply_ms_pixel.size(), nullptr, objects.apply_msaa_pixel.GetAddressOf()))
        || FAILED(device->CreateBlendState(&replace, objects.replace_blend.GetAddressOf()))
        || FAILED(device->CreateBlendState(&add, objects.add_blend.GetAddressOf()))
        || FAILED(device->CreateRasterizerState(&rasterizer, objects.rasterizer.GetAddressOf()))
        || FAILED(device->CreateDepthStencilState(&no_depth, objects.no_depth.GetAddressOf()))
        || FAILED(device->CreateBuffer(&constants, nullptr, objects.constants.GetAddressOf()));
    if (objects.failed) renodx::utils::log::w("[Total War] Light Bounce: could not create its shaders: off.");
  }
  if (objects.failed) return false;
  if (objects.light_copy && objects.light_desc.Width == light_desc.Width && objects.light_desc.Height == light_desc.Height
      && objects.light_desc.Format == light_desc.Format
      && objects.light_desc.SampleDesc.Count == light_desc.SampleDesc.Count
      && objects.light_view_format == light_view_format && objects.bounce_scale == scale) {
    return true;
  }
  objects.light_copy.Reset();
  objects.light_copy_srv.Reset();
  objects.prepared_light = {};
  objects.prepared_geometry = {};
  objects.traced = {};
  objects.history = {};
  objects.history_frames = {};
  objects.denoised = {};
  objects.light_desc = light_desc;
  objects.light_view_format = light_view_format;
  objects.bounce_scale = scale;
  // Single-sampled (with the game's MSAA its resolve).
  D3D11_TEXTURE2D_DESC copy = light_desc;
  copy.SampleDesc = {.Count = 1};
  copy.Usage = D3D11_USAGE_DEFAULT;
  copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  copy.CPUAccessFlags = 0;
  copy.MiscFlags = 0;
  const D3D11_SHADER_RESOURCE_VIEW_DESC copy_view = {
      .Format = light_view_format,
      .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
      .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
  };
  const UINT width = (light_desc.Width + scale - 1) / scale;
  const UINT height = (light_desc.Height + scale - 1) / scale;
  objects.prepared_levels = std::min<UINT>(PREPARED_LEVELS, std::bit_width(std::max(width, height)));
  const auto make_prepared = [&](DXGI_FORMAT format, PreparedTarget* prepared) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = width,
        .Height = height,
        .MipLevels = objects.prepared_levels,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };
    if (FAILED(device->CreateTexture2D(&desc, nullptr, prepared->texture.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(prepared->texture.Get(), nullptr, prepared->srv.GetAddressOf()))) {
      return false;
    }
    for (UINT level = 0; level < objects.prepared_levels; ++level) {
      const D3D11_RENDER_TARGET_VIEW_DESC target_view = {
          .Format = format,
          .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
          .Texture2D = {.MipSlice = level},
      };
      const D3D11_SHADER_RESOURCE_VIEW_DESC level_view = {
          .Format = format,
          .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
          .Texture2D = {.MostDetailedMip = level, .MipLevels = 1},
      };
      if (FAILED(device->CreateRenderTargetView(prepared->texture.Get(), &target_view,
                                                prepared->level_rtvs[level].GetAddressOf()))
          || FAILED(device->CreateShaderResourceView(prepared->texture.Get(), &level_view,
                                                     prepared->level_srvs[level].GetAddressOf()))) {
        return false;
      }
    }
    return true;
  };
  if (FAILED(device->CreateTexture2D(&copy, nullptr, objects.light_copy.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(objects.light_copy.Get(), &copy_view, objects.light_copy_srv.GetAddressOf()))
      || !make_prepared(DXGI_FORMAT_R11G11B10_FLOAT, &objects.prepared_light)
      || !make_prepared(DXGI_FORMAT_R32G32_UINT, &objects.prepared_geometry)
      || !frame_history::MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.traced)
      || !frame_history::MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.history[0])
      || !frame_history::MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.history[1])
      || !frame_history::MakeTarget(device, width, height, DXGI_FORMAT_R16_FLOAT, &objects.history_frames[0])
      || !frame_history::MakeTarget(device, width, height, DXGI_FORMAT_R16_FLOAT, &objects.history_frames[1])
      || !frame_history::MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.denoised)) {
    renodx::utils::log::w("[Total War] Light Bounce: could not create its textures (", light_desc.Width, " x ",
                          light_desc.Height, ").");
    objects.light_copy.Reset();
    return false;
  }
  // No bounce and no history until the first trace (alpha 0: nothing there).
  constexpr float NOTHING[4] = {0.f, 0.f, 0.f, 0.f};
  for (const frame_history::Target* target : {&objects.history[0], &objects.history[1], &objects.history_frames[0],
                                               &objects.history_frames[1], &objects.denoised}) {
    context->ClearRenderTargetView(target->rtv.Get(), NOTHING);
  }
  objects.traced_frame = UINT64_MAX;
  return true;
}

}  // namespace internal

constexpr auto OnSunDraw = []<typename T, typename Context>(
                               Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
    // Other views' sun draws (the battle portrait's 3D) neither trace nor take the main view's bounce.
    if (!frame_history::MainViewport(native)) return {};
    auto& runtime = internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    if (!(runtime.settings.strength > 0.f)) return {};

    std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets;
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_target_pointers = {};
    ComPtr<ID3D11DepthStencilView> game_depth;
    native->OMGetRenderTargets(static_cast<UINT>(game_target_pointers.size()), game_target_pointers.data(),
                               game_depth.GetAddressOf());
    for (size_t index = 0; index < game_targets.size(); ++index) game_targets[index].Attach(game_target_pointers[index]);
    if (!game_targets[0]) return {};
    ComPtr<ID3D11Resource> light_resource;
    game_targets[0]->GetResource(light_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> light_texture;
    if (FAILED(light_resource.As(&light_texture))) return {};
    D3D11_TEXTURE2D_DESC light_desc = {};
    light_texture->GetDesc(&light_desc);
    if (light_desc.MipLevels != 1 || light_desc.ArraySize != 1
        || light_desc.Width != runtime.display_width || light_desc.Height != runtime.display_height) {
      return {};
    }
    D3D11_RENDER_TARGET_VIEW_DESC light_view = {};
    game_targets[0]->GetDesc(&light_view);
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    const auto scale = static_cast<UINT>(runtime.settings.scale);
    const bool msaa = (light_desc.SampleDesc.Count > 1);
    if (!internal::Prepare(native, device.Get(), light_desc, light_view.Format, scale)) return {};
    auto& objects = runtime.objects;
    const auto& sun_draw = command.template GetCallbackData<T>();
    runtime.settings.diffuse_rule = sun_draw.diffuse_rule;
    if (std::memcmp(&objects.uploaded, &runtime.settings, sizeof(LightBounceConstants)) != 0) {
      native->UpdateSubresource(objects.constants.Get(), 0, nullptr, &runtime.settings, 0, 0);
      objects.uploaded = runtime.settings;
    }

    // The game's draw first.
    frame_history::DrawAgain(native, command.arguments);

    ComPtr<ID3D11PixelShader> game_shader;
    native->PSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
    std::array<ID3D11ShaderResourceView*, internal::VIEW_COUNT> game_views = {};
    native->PSGetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, game_views.data());
    std::array<ID3D11Buffer*, internal::CONSTANTS_COUNT> game_constants = {};
    native->PSGetConstantBuffers(internal::CONSTANTS_SLOT, internal::CONSTANTS_COUNT, game_constants.data());
    ComPtr<ID3D11BlendState> game_blend;
    std::array<FLOAT, 4> game_blend_factor = {};
    UINT game_sample_mask = 0;
    native->OMGetBlendState(game_blend.GetAddressOf(), game_blend_factor.data(), &game_sample_mask);

    // This draw's G-buffer and camera at the addon's slots, with the last frame's camera and the constants.
    std::array<ID3D11ShaderResourceView*, internal::VIEW_COUNT> inputs = {};
    native->PSGetShaderResources(sun_draw.gbuffer_slot, 4, &inputs[internal::GBUFFER_VIEW]);
    std::array<ID3D11Buffer*, internal::CONSTANTS_COUNT> constants = {
        objects.previous_camera.Get(), nullptr, objects.constants.Get()};
    native->PSGetConstantBuffers(sun_draw.camera_slot, 1, &constants[1]);
    native->PSSetConstantBuffers(internal::CONSTANTS_SLOT, internal::CONSTANTS_COUNT, constants.data());
    // The prepare and the trace, once a frame after the first standard draw, with the addon's full-screen triangle over
    // the game's viewport / scale (no depth / stencil: every texel). The game's vertex stage, input, viewports,
    // rasterizer, depth / stencil state and targets are put back after.
    if (sun_draw.standard && objects.traced_frame != runtime.frame) {
      ComPtr<ID3D11VertexShader> game_vertex_shader;
      native->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
      ComPtr<ID3D11InputLayout> game_layout;
      native->IAGetInputLayout(game_layout.GetAddressOf());
      D3D11_PRIMITIVE_TOPOLOGY game_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      native->IAGetPrimitiveTopology(&game_topology);
      std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_viewports = {};
      UINT game_viewport_count = static_cast<UINT>(game_viewports.size());
      native->RSGetViewports(&game_viewport_count, game_viewports.data());
      ComPtr<ID3D11RasterizerState> game_rasterizer;
      native->RSGetState(game_rasterizer.GetAddressOf());
      ComPtr<ID3D11DepthStencilState> game_depth_state;
      UINT game_stencil_reference = 0;
      native->OMGetDepthStencilState(game_depth_state.GetAddressOf(), &game_stencil_reference);

      const D3D11_VIEWPORT& game_viewport = game_viewports[0];
      const D3D11_VIEWPORT trace_viewport = {
          .TopLeftX = std::floor(game_viewport.TopLeftX / scale),
          .TopLeftY = std::floor(game_viewport.TopLeftY / scale),
          .Width = std::ceil(game_viewport.Width / scale),
          .Height = std::ceil(game_viewport.Height / scale),
          .MaxDepth = 1.f,
      };
      if (msaa) {
        native->ResolveSubresource(objects.light_copy.Get(), 0, light_texture.Get(), 0, light_view.Format);
      } else {
        // Only the texels the prepare reads: `scale` a side for each pixel of the trace's viewport (all of them at 100%; below it
        // the view is the texture's top-left part, and the rest was three quarters of the copy at half the size).
        const D3D11_BOX read = {
            .left = static_cast<UINT>(trace_viewport.TopLeftX) * scale,
            .top = static_cast<UINT>(trace_viewport.TopLeftY) * scale,
            .front = 0,
            .right = std::min(objects.light_desc.Width, static_cast<UINT>(trace_viewport.TopLeftX + trace_viewport.Width) * scale),
            .bottom = std::min(objects.light_desc.Height, static_cast<UINT>(trace_viewport.TopLeftY + trace_viewport.Height) * scale),
            .back = 1,
        };
        native->CopySubresourceRegion(objects.light_copy.Get(), 0, read.left, read.top, 0, light_texture.Get(), 0, &read);
      }
      // The view's part of the prepared textures (all of them at 100%; the top-left with the DLSS render scale).
      D3D11_TEXTURE2D_DESC prepared_desc = {};
      objects.prepared_light.texture->GetDesc(&prepared_desc);
      runtime.settings.region_width = trace_viewport.Width;
      runtime.settings.region_height = trace_viewport.Height;
      runtime.settings.level0_width = static_cast<float>(prepared_desc.Width);
      if (std::memcmp(&objects.uploaded, &runtime.settings, sizeof(LightBounceConstants)) != 0) {
        native->UpdateSubresource(objects.constants.Get(), 0, nullptr, &runtime.settings, 0, 0);
        objects.uploaded = runtime.settings;
      }
      native->OMSetBlendState(objects.replace_blend.Get(), nullptr, 0xFFFFFFFF);
      native->OMSetDepthStencilState(objects.no_depth.Get(), 0);
      native->RSSetState(objects.rasterizer.Get());
      native->RSSetViewports(1, &trace_viewport);
      native->IASetInputLayout(nullptr);
      native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      native->VSSetShader(objects.full_screen.Get(), nullptr, 0);

      // The prepare: the light copy and the G-buffer (and the last trace's output) into the prepared light and geometry.
      inputs[internal::LIGHT_VIEW] = objects.light_copy_srv.Get();
      inputs[internal::HISTORY_VIEW] = objects.history[objects.history_index].srv.Get();
      native->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, inputs.data());
      ID3D11RenderTargetView* prepared[] = {objects.prepared_light.level_rtvs[0].Get(),
                                            objects.prepared_geometry.level_rtvs[0].Get()};
      native->OMSetRenderTargets(2, prepared, nullptr);
      const ComPtr<ID3D11PixelShader>& prepare = (msaa ? objects.prepare_msaa : objects.prepare);
      native->PSSetShader(prepare.Get(), nullptr, 0);
      native->Draw(3, 0);

      // The trace through rays when ray tracing has it (the prepared textures no longer targets; it reads their first level).
      native->OMSetRenderTargets(0, nullptr, nullptr);
      const internal::RayTracedTrace ray_traced = internal::ray_traced_trace.load(std::memory_order_relaxed);
      const bool rays = ray_traced != nullptr
                        && ray_traced(native, objects.prepared_light.texture.Get(), static_cast<UINT>(trace_viewport.Width),
                                      static_cast<UINT>(trace_viewport.Height), scale, objects.traced.texture.Get());
      // Else the prepared light and geometry's levels, each from the one above (that level only, at t46-t47).
      if (!rays) {
        native->PSSetShader(objects.levels.Get(), nullptr, 0);
        for (UINT level = 1; level < objects.prepared_levels; ++level) {
          ID3D11RenderTargetView* level_targets[] = {objects.prepared_light.level_rtvs[level].Get(),
                                                     objects.prepared_geometry.level_rtvs[level].Get()};
          native->OMSetRenderTargets(2, level_targets, nullptr);
          ID3D11ShaderResourceView* above[] = {objects.prepared_light.level_srvs[level - 1].Get(),
                                               objects.prepared_geometry.level_srvs[level - 1].Get()};
          native->PSSetShaderResources(internal::VIEWS_SLOT + internal::PREPARED_VIEW, 2, above);
          const D3D11_VIEWPORT level_viewport = {
              .Width = static_cast<FLOAT>(std::max(1u, prepared_desc.Width >> level)),
              .Height = static_cast<FLOAT>(std::max(1u, prepared_desc.Height >> level)),
              .MaxDepth = 1.f,
          };
          native->RSSetViewports(1, &level_viewport);
          native->Draw(3, 0);
        }
        native->RSSetViewports(1, &trace_viewport);
      }

      // The screen-space trace (the last history at t41 for Checkerboard), the resolve into the other history and its frame counts
      // (this frame's trace at t40, the last history at t41, its counts at t48), with the Denoiser its output smoothed
      // (at t40) into the denoised target. Each target set before its input, so no texture is bound as both.
      const auto pass = [&](const frame_history::Target& target, ID3D11PixelShader* shader,
                            const frame_history::Target* frames = nullptr) {
        ID3D11RenderTargetView* rtv[] = {target.rtv.Get(), (frames != nullptr) ? frames->rtv.Get() : nullptr};
        native->OMSetRenderTargets((frames != nullptr) ? 2 : 1, rtv, nullptr);
        native->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, inputs.data());
        native->PSSetShader(shader, nullptr, 0);
        native->Draw(3, 0);
      };
      const size_t previous_index = objects.history_index;
      objects.history_index ^= 1;
      inputs[internal::LIGHT_VIEW] = nullptr;
      inputs[internal::HISTORY_VIEW] = objects.history[previous_index].srv.Get();
      inputs[internal::PREPARED_VIEW] = objects.prepared_light.srv.Get();
      inputs[internal::PREPARED_VIEW + 1] = objects.prepared_geometry.srv.Get();
      if (!rays) pass(objects.traced, objects.trace.Get());
      // The resolve over frames also under Ray Reconstruction (ATN's debug33: the rays' trace applied as traced, RR alone,
      // was noisier); the denoiser not (Update).
      inputs[internal::LIGHT_VIEW] = objects.traced.srv.Get();
      inputs[internal::FRAMES_VIEW] = objects.history_frames[previous_index].srv.Get();
      pass(objects.history[objects.history_index], objects.resolve.Get(), &objects.history_frames[objects.history_index]);
      inputs[internal::FRAMES_VIEW] = nullptr;
      if (runtime.denoise) {
        inputs[internal::LIGHT_VIEW] = objects.history[objects.history_index].srv.Get();
        inputs[internal::HISTORY_VIEW] = nullptr;
        pass(objects.denoised, objects.denoise.Get());
      }

      // This frame's camera for the next trace.
      if (constants[1] != nullptr) {
        frame_history::KeepCamera(device.Get(), native, constants[1], std::addressof(objects.previous_camera));
      }

      native->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
      native->IASetInputLayout(game_layout.Get());
      native->IASetPrimitiveTopology(game_topology);
      native->RSSetViewports(game_viewport_count, game_viewports.data());
      native->RSSetState(game_rasterizer.Get());
      native->OMSetDepthStencilState(game_depth_state.Get(), game_stencil_reference);
      native->OMSetRenderTargets(static_cast<UINT>(game_target_pointers.size()), game_target_pointers.data(),
                                 game_depth.Get());
      objects.traced_frame = runtime.frame;
    }
    // The apply: the game's draw again (its pixels), added to the light buffer (the denoised bounce with the Denoiser).
    inputs[internal::LIGHT_VIEW] = nullptr;
    inputs[internal::HISTORY_VIEW] = runtime.denoise ? objects.denoised.srv.Get() : objects.history[objects.history_index].srv.Get();
    inputs[internal::PREPARED_VIEW] = nullptr;
    inputs[internal::PREPARED_VIEW + 1] = nullptr;
    native->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, inputs.data());
    native->OMSetBlendState(objects.add_blend.Get(), nullptr, 0xFFFFFFFF);
    const ComPtr<ID3D11PixelShader>& apply =
        (msaa ? (runtime.apply_per_pixel ? objects.apply_msaa_pixel : objects.apply_msaa) : objects.apply);
    native->PSSetShader(apply.Get(), nullptr, 0);
    frame_history::DrawAgain(native, command.arguments);

    native->PSSetShader(game_shader.Get(), nullptr, 0);
    native->PSSetShaderResources(internal::VIEWS_SLOT, internal::VIEW_COUNT, game_views.data());
    native->PSSetConstantBuffers(internal::CONSTANTS_SLOT, internal::CONSTANTS_COUNT, game_constants.data());
    native->OMSetBlendState(game_blend.Get(), game_blend_factor.data(), game_sample_mask);
    for (auto* view : game_views) {
      if (view != nullptr) view->Release();
    }
    for (size_t index = internal::GBUFFER_VIEW; index < internal::PREPARED_VIEW; ++index) {
      if (inputs[index] != nullptr) inputs[index]->Release();
    }
    for (auto* buffer : game_constants) {
      if (buffer != nullptr) buffer->Release();
    }
    if (constants[1] != nullptr) constants[1]->Release();
    return {.bypass = true};
  }
};

// Every present: the sliders and the main view's size (the display's; not looked up while it is off).
inline void Update(reshade::api::swapchain* swapchain, const Sliders& sliders) {
  reshade::api::resource_desc display = {};
  if (sliders.strength > 0.f) {
    display = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
  }
  const bool temporal = (sliders.temporal != 0.f);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.settings = {
      .strength = sliders.strength,
      .radius = sliders.radius,
      .thickness = sliders.thickness,
      .steps = 8.f * internal::PowerOfTwo(sliders.quality, 5.f),
      .slices = internal::PowerOfTwo(sliders.directions, 4.f),
      // Effects at Output Resolution: the render scale's steps of 2 off the divisor (the detail of 100%).
      .scale = internal::PowerOfTwo(sliders.resolution - frame_history::EffectSteps(), 3.f),
      .history = (temporal ? internal::HISTORY_SHARE : 0.f),
      .frame = (temporal ? static_cast<float>(internal::runtime.frame % 64) : 0.f),
      .bounces = std::clamp(sliders.multi_bounce, 0.f, 1.f),
      .checkerboard = ((temporal && sliders.checkerboard != 0.f) ? 1.f : 0.f),
      .backface = ((sliders.backface != 0.f) ? 1.f : 0.f),
      .auto_thickness = ((sliders.auto_thickness != 0.f) ? 1.f : 0.f),
  };
  internal::runtime.display_width = display.texture.width;
  internal::runtime.display_height = display.texture.height;
  internal::runtime.apply_per_pixel = (sliders.msaa_pixel != 0.f);
  // Ray Reconstruction or NRD denoises it (the bounce's resolve over frames stays: its texels are coarser).
  internal::runtime.denoise = (sliders.denoise != 0.f) && !dlaa_bridge::bridge.reconstructing && !dlaa_bridge::bridge.nrd_bounce;
  // Off: its memory back (Prepare makes it again).
  if (!(sliders.strength > 0.f) && internal::runtime.objects.device) {
    internal::runtime.objects = {};
  }
  ++internal::runtime.frame;
}

// The game's sun light draws (`draws`: the addon's, kept for as long as it is installed; with MSAA, their *_ms ones too).
// Not the unlit model's (no normals). After renodx::mods::shader::Use: its own callback on replaced sun shaders (their injection) first.
inline void Install(std::span<const SunDraw> draws) {
  for (const SunDraw& draw : draws) {
    renodx::utils::command_action::Register(
        OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &draw);
  }
  // Here because every Total War addon installs this module (and registers its OnDestroyDevice).
  reshade::register_event<reshade::addon_event::destroy_swapchain>(device_end::OnDestroySwapchain);
}

// Ray tracing's trace in place of the screen-space one (rt_scene.hpp; null: none).
inline void SetRayTracedTrace(internal::RayTracedTrace trace) {
  internal::ray_traced_trace.store(trace);
}

// Every present after Update, while ray tracing traces the Light Bounce (`on`): the rays at their own resolution (1 /
// 2^`resolution`, not the screen-space search's) and every texel each frame (no Checkerboard; ATN 2026-10-30: the
// screen-space search's resolution and Checkerboard made the rays' light coarse and noisy).
inline void SetRayTraced(bool on, float resolution) {
  if (!on) return;
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.settings.scale = internal::PowerOfTwo(resolution - frame_history::EffectSteps(), 3.f);
  internal::runtime.settings.checkerboard = 0.f;
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  device_end::Log(device, "device");
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.objects.device.Get() != native) return;
  internal::runtime.objects = {};
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::destroy_swapchain>(device_end::OnDestroySwapchain);
  renodx::utils::command_action::Unregister(OnSunDraw);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.objects = {};
}

}  // namespace twattila::light_bounce
