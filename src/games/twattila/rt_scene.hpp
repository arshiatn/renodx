/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/state.hpp"
#include "./anti_aliasing.hpp"
#include "./rt_core.hpp"
#include "./ao_pass.hpp"
#include "./dlaa_bridge.hpp"
#include "./frame_trace.hpp"
#include "./feature_settings.hpp"
#include "./object_motion.hpp"
#include "./light_bounce.hpp"
#include "./ssr_hiz.hpp"

// Ray tracing in the Total War games (rt_core.hpp's core with the TW games' passes and settings): at the game's ambient
// occlusion pass (its depth and camera bound) the frame's trace; its ambient visibility then multiplies the pass's
// result (rt_ambient_apply, red only), and the sun's visibility goes to the patched sun shaders' occlusion (t49;
// screen_space_shadows.hpp asks: rt_occlusion, the darker with its own). The sun's direction: the frame's first sun
// light draw's lighting buffer (rt_sun), for the next frame's trace. Games without an ambient occlusion pass of the
// addon's (Warhammer: their own line sweeps) trace at the frame's first sun light draw instead (its G-buffer depth and
// camera), and its ambient visibility multiplies the AO the sun draws read (rt_ambient_combine, bound in its place). The
// Light Bounce's trace (light_bounce.hpp) goes through rays when on; the reflection passes read the reflections. The
// scene's captures: rt_capture_*.vs (tools\tw\attila_motion\make_wind_motion_vs.py: the game's code giving the world
// position), Warhammer III's the game's own shaders. Needs DLAA / DLSS (the bridge). D3D11.
namespace twattila::rt_scene {

using Microsoft::WRL::ComPtr;

// The settings as the addon's sliders hold them (in the anti-aliasing section: with DLAA / DLSS).
struct Sliders {
  float sun_shadows = 0.f;
  float sun_softness = 0.5f;       // degrees of the sun's disk (0: hard)
  float light_softness = 0.25f;    // the torches' and fires' radius (metres; 0: hard)
  float light_glow = 1.f;          // torch and fire light in fog and smoke (0: none)
  float output_rays = 0.f;         // Rays a Pixel: 0 one, 1 the output pixels a render pixel stands for, 2-4 that many
  float rays_shadows = 1.f;        // what it counts for
  float rays_ambient = 1.f;
  float rays_reflections = 1.f;
  float rays_paths = 1.f;
  float debug_log = 0.f;           // the statistics in the log every 10 s
  float sprite_shadows = 1.f;      // the flat far soldiers in the scene (Attila)
  float far_tree_shadows = 1.f;    // the flat far trees in the scene (Attila)
  float leaf_light = 0.f;
  float coloured_shadows = 1.f;    // Attila (its sun light shaders' colour)
  float grass_sway = 1.f;          // placed grass's swaying copies (Attila)
  float sway_range = 0.f;          // metres (0: everywhere)
  float sway_off_screen = 1.f;
  float fast_leaves = 0.f;
  float grass_shadow_range = 0.f;  // metres (0: everywhere)
  float path_ser = 0.f;
  float sharc = 0.f;
  float light_importance = 0.f;
  float ambient_occlusion = 0.f;   // off, on (Attila: in place of the game's and GT-VBAO)
  float ambient_range = 2.f;       // metres
  float ambient_rays = 2.f;
  float ambient_quarter = 0.f;     // 1: a quarter of the rays (faster)
  float fog_shadows = 0.f;  // the fog's (games whose fog InstallFogShadows hooks)
  float micromaps = 1.f;
  float gpu_micromaps = 1.f;
  float smoke_shadows = 0.f;  // games whose particles InstallSmokeShadows hooks
  float light_bounce = 0.f;  // the Light Bounce's tracer: 0 its own (screen space), 1 rays, 2 paths
  float light_bounce_rays = 4.f;
  float light_bounce_range = 10.f;
  float light_bounce_resolution = 1.f;  // 0 full, 1 half, 2 quarter
  float path_bounces = 2.f;
  float path_ambient = 0.f;  // Path-Traced Ambient (games InstallSunDraws gives the ambient cube's place)
  float reflections = 0.f;
  float specular_occlusion = 0.f;
  float water_reflections = 1.f;  // the water's from its own surface (games whose reflection pass OnReflectionDraw hooks)
  float reflection_samples = 1.f;
  float reflection_reuse = 0.f;  // 0 off, 1 from ReSTIR GI, 2 Own ReSTIR
  float debug_view = 0.f;  // games whose tone mapper hook calls OnTonemapDrawn
  float scene_test = 0.f;  // with the Debug View on
};

namespace internal {
// The sun draws' lighting buffer's ambient cube, its first float (InstallSunDraws; 0: none): the paths' sky.
static UINT ambient_cube = 0;
}  // namespace internal

// `light_bounce_strength`: the game's Light Bounce (its rays and paths replace the Light Bounce's trace; off, nothing to
// trace for them, so they keep ray tracing off).
inline Settings SettingsOf(const Sliders& sliders, const feature_settings::AaSliders& aa, float light_bounce_strength) {
  const bool dlaa = aa.method == 5.f;
  const bool bounce = light_bounce_strength > 0.f && dlaa;
  const bool path_ambient = sliders.path_ambient != 0.f && sliders.light_bounce == 2.f && bounce && internal::ambient_cube != 0;
  // Rays a Pixel: the picture is at the render size until DLSS, nothing traced can be kept finer than that: each render pixel
  // traces more rays instead, each from its own spot of it. Match Output: those of the output pixels it stands for (at most
  // 4: Performance), as many on screen as at DLAA (ATN: RR Performance's 1080p rays looked noisy at 4K).
  const float render_scale = feature_settings::RenderScale(aa);
  const auto density = static_cast<uint32_t>(
      (sliders.output_rays == 1.f) ? std::clamp(std::lround(1.f / (render_scale * render_scale)), 1l, 4l)
                                   : std::clamp(std::lround(sliders.output_rays), 1l, 4l));
  const auto rays_of = [density](float on) { return (on != 0.f) ? density : 1u; };
  return {
      .sun_shadows = sliders.sun_shadows != 0.f && dlaa,
      .ambient_occlusion = sliders.ambient_occlusion != 0.f && dlaa && !path_ambient,
      .ambient_range = sliders.ambient_range,
      .ambient_rays = static_cast<uint32_t>(sliders.ambient_rays) * rays_of(sliders.rays_ambient),
      // Effects at Output Resolution: every pixel each frame from one step of 2 down (as many rays on screen as at 100%;
      // with Rays a Pixel the rays make up for it).
      .ambient_quarter = sliders.ambient_quarter != 0.f && (rays_of(sliders.rays_ambient) > 1u || frame_history::EffectSteps() < 1.f),
      .sun_softness = sliders.sun_softness,
      .light_size = sliders.light_softness,
      .shadow_rays = rays_of(sliders.rays_shadows),
      .light_glow = sliders.light_glow,
      .light_bounce = sliders.light_bounce != 0.f && bounce,
      .light_bounce_rays = std::min(static_cast<uint32_t>(sliders.light_bounce_rays) * rays_of(sliders.rays_paths), 16u),
      .light_bounce_range = sliders.light_bounce_range,
      .light_bounce_resolution = sliders.light_bounce_resolution,
      .path_tracing = sliders.light_bounce == 2.f && bounce,
      .path_bounces = static_cast<uint32_t>(sliders.path_bounces),
      .path_ambient = path_ambient,
      .reflections = sliders.reflections != 0.f && dlaa,
      .specular_occlusion = sliders.specular_occlusion != 0.f && dlaa,
      .water_reflections = sliders.water_reflections != 0.f,
      .reflection_samples = static_cast<uint32_t>(sliders.reflection_samples) * rays_of(sliders.rays_reflections),
      .reflection_reuse = sliders.reflection_reuse == 1.f && sliders.light_bounce == 2.f,
      .reflection_own = sliders.reflection_reuse == 2.f,
      // Ray-traced sun shadows are every shadow: the point lights' too (games whose light draws InstallPointLights hooks),
      // the game's sun shadow maps not drawn (games that InstallShadowsOnly).
      .local_lights = sliders.sun_shadows != 0.f && dlaa,
      .leaf_light = sliders.leaf_light != 0.f,
      .coloured_shadows = sliders.coloured_shadows != 0.f && sliders.leaf_light != 0.f && internal::ambient_cube != 0,
      .grass_sway = sliders.grass_sway != 0.f,
      .sway_range = sliders.sway_range,
      .sway_off_screen = sliders.sway_off_screen != 0.f,
      .fast_leaves = sliders.fast_leaves != 0.f,
      .grass_shadow_range = sliders.grass_shadow_range,
      .path_ser = sliders.path_ser != 0.f,
      .sharc = sliders.sharc != 0.f,
      .light_importance = sliders.light_importance != 0.f,
      .fog_shadows = sliders.fog_shadows != 0.f && dlaa,
      .micromaps = sliders.micromaps != 0.f,
      .gpu_micromaps = sliders.gpu_micromaps != 0.f,
      .smoke_shadows = sliders.smoke_shadows != 0.f && sliders.sun_shadows != 0.f && dlaa,
      .shadows_only = sliders.sun_shadows != 0.f && dlaa,
      .debug_view = static_cast<uint32_t>(sliders.debug_view),
      .scene_test = ((sliders.debug_view != 0.f) ? static_cast<uint32_t>(sliders.scene_test) : 0u),
      .debug_log = sliders.debug_log != 0.f,
      .sprite_shadows = sliders.sprite_shadows != 0.f,
      .far_tree_shadows = sliders.far_tree_shadows != 0.f,
  };
}

// What the anti-aliasing's bridge makes for these settings: the ray tracing textures, the G-buffer guides (the paths read
// them) and the reflections' textures.
inline void Configure(const Settings& settings, anti_aliasing::Settings* aa) {
  aa->ray_tracing = settings.sun_shadows || settings.ambient_occlusion || settings.light_bounce || settings.reflections
                    || settings.specular_occlusion || settings.local_lights || settings.fog_shadows;
  aa->ray_traced_guides = settings.path_tracing || settings.reflections || settings.specular_occlusion;
  aa->ray_traced_reflections = settings.reflections || settings.specular_occlusion;
  // The Light Bounce through rays: at their own resolution, no Checkerboard.
  light_bounce::SetRayTraced(settings.light_bounce, settings.light_bounce_resolution);
}

// The TW games' view: DLAA's (anti_aliasing.hpp); a rigid draw DLAA's motion saw moving stays out of the scene.
inline const Host TW_HOST = {
    .camera = [](ID3D11Buffer* buffer, Camera* camera) {
      anti_aliasing::internal::Camera main;
      if (!anti_aliasing::MainCamera(buffer, &main)) return false;
      *camera = {.view_projection = main.view_projection, .inverse_view_projection = main.inverse_view_projection, .position = main.position};
      return true;
    },
    .render_size = anti_aliasing::RenderSize,
    .guides_written = anti_aliasing::GuidesWritten,
    .moving = [](ID3D11DeviceContext* native, const renodx::utils::command_action::DrawArguments& arguments) {
      return object_motion::MovingMesh(object_motion::MeshKey(native, arguments));
    },
    .moving_indexed = [](ID3D11DeviceContext* native, const renodx::utils::command_action::DrawIndexedArguments& arguments) {
      return object_motion::MovingMesh(object_motion::MeshKey(native, arguments));
    },
    .main_view = frame_history::MainViewport,
    .rewrite_guides = anti_aliasing::RewriteGuides,
};

namespace internal {
// A reflection pass OnReflectionDraw hooks ran (Attila's shaders: Ray-Traced Water Reflections shown).
static bool water_pass = false;
// The game has sprite soldiers in its scene shaders (Sprite Soldier Shadows shown).
static bool sprite_shadows_installed = false;
static bool far_tree_shadows_installed = false;
}  // namespace internal

// The game's reflection pass (environment_specular_light, Attila's shaders: Attila, Thrones), before its draws: the
// ray-traced reflections (the water's from its own surface: the depth-stencil the draw reads its stencil from at t8),
// and the screen-space march's pyramid and resolve (ssr_hiz.hpp) for what keeps the march (everything without them;
// with them water, with Ray-Traced Water Reflections off). True: draw.
inline bool OnReflectionDraw(reshade::api::command_list* cmd_list) {
  internal::water_pass = true;
  ComPtr<ID3D11ShaderResourceView> depth_stencil;
  if (auto* native = internal::NativeContext(cmd_list); native != nullptr) native->PSGetShaderResources(8, 1, depth_stencil.GetAddressOf());
  const bool reflected = BindReflections(cmd_list, false, true, depth_stencil.Get());
  bool water_traced = false;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    water_traced = internal::runtime.water_traced;
  }
  return ssr_hiz::OnReflectionDraw(cmd_list, ssr_hiz::internal::DEPTH_SLOT, !(reflected && water_traced));
}

// After its draws: the views unbound, the screen-space march's resolve when it ran.
inline void OnReflectionDrawn(reshade::api::command_list* cmd_list) {
  ssr_hiz::OnReflectionDrawn(cmd_list);
  BindReflections(cmd_list, false, false);
}

// The game's ambient occlusion pass (pre callback): this frame's trace; after it (post), its result times the traced
// ambient visibility.
constexpr auto OnAmbientOcclusionDraw = []<typename T, typename Context>(
                                            Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (!internal::active.load(std::memory_order_relaxed)) return {};
    auto* native = internal::NativeContext(command.cmd_list);
    if (native == nullptr) return {};
    auto& runtime = internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    // The Debug View's Screen-Space AO: this pass's target (the game's or GT-VBAO's ambient occlusion), copied after it
    // and before the traced ambient visibility goes in (it showed both, ATN 2026-10-28).
    const bool keep_ambient = runtime.settings.debug_view >= 8;
    if (keep_ambient) {
      ComPtr<ID3D11RenderTargetView> target;
      native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
      ComPtr<ID3D11Resource> resource;
      if (target) target->GetResource(resource.GetAddressOf());
      auto& objects = runtime.objects;
      if (resource && resource.Get() != objects.debug_ambient_resource.Get()) {
        D3D11_RENDER_TARGET_VIEW_DESC target_desc = {};
        target->GetDesc(&target_desc);
        const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = target_desc.Format,
                                                           .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                           .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
        objects.debug_ambient.Reset();
        objects.debug_ambient_copy.Reset();
        objects.debug_ambient_resource = resource;
        ComPtr<ID3D11Device> device;
        native->GetDevice(device.GetAddressOf());
        ComPtr<ID3D11Texture2D> texture;
        D3D11_TEXTURE2D_DESC copy_desc = {};
        if (target_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D && SUCCEEDED(resource.As(&texture))) {
          texture->GetDesc(&copy_desc);
          copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
          copy_desc.MiscFlags = 0;
          if (copy_desc.SampleDesc.Count == 1 && SUCCEEDED(device->CreateTexture2D(&copy_desc, nullptr, objects.debug_ambient_copy.GetAddressOf()))) {
            device->CreateShaderResourceView(objects.debug_ambient_copy.Get(), &view_desc, objects.debug_ambient.GetAddressOf());
          }
        }
      }
    }
    ComPtr<ID3D11ShaderResourceView> depth;
    ComPtr<ID3D11Buffer> camera;
    native->PSGetShaderResources(ao_pass::internal::DEPTH_SLOT, 1, depth.GetAddressOf());
    native->PSGetConstantBuffers(ao_pass::internal::CAMERA_SLOT, 1, camera.GetAddressOf());
    internal::Trace(native, depth.Get(), camera.Get());
    if (!keep_ambient && (!runtime.settings.ambient_occlusion || runtime.traced_frame != runtime.frame)) return {};
    return {.post_callback = [](Context& post, const void*) {
      auto* post_native = internal::NativeContext(post.cmd_list);
      if (post_native == nullptr) return;
      auto& post_runtime = internal::runtime;
      const std::unique_lock post_lock(post_runtime.mutex);
      auto& objects = post_runtime.objects;
      if (post_runtime.settings.debug_view >= 8 && objects.debug_ambient_copy) {
        post_native->CopyResource(objects.debug_ambient_copy.Get(), objects.debug_ambient_resource.Get());
        post_runtime.debug_ambient_frame = post_runtime.frame;
      }
      if (!post_runtime.settings.ambient_occlusion || post_runtime.traced_frame != post_runtime.frame) return;
      post_runtime.ambient_frame = post_runtime.frame;
      std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
      UINT viewport_count = static_cast<UINT>(viewports.size());
      post_native->RSGetViewports(&viewport_count, viewports.data());
      if (viewport_count == 0 || viewports[0].Width <= 0.f || viewports[0].Height <= 0.f) return;
      // The game's bindings this changes, put back after.
      ComPtr<ID3D11VertexShader> game_vertex_shader;
      ComPtr<ID3D11PixelShader> game_pixel_shader;
      ComPtr<ID3D11BlendState> game_blend;
      std::array<float, 4> blend_factor = {};
      UINT sample_mask = 0;
      ComPtr<ID3D11RasterizerState> game_rasterizer;
      ComPtr<ID3D11DepthStencilState> game_depth;
      UINT stencil_reference = 0;
      ComPtr<ID3D11ShaderResourceView> game_view;
      ComPtr<ID3D11SamplerState> game_sampler;
      ComPtr<ID3D11Buffer> game_constants;
      ComPtr<ID3D11InputLayout> game_layout;
      D3D11_PRIMITIVE_TOPOLOGY game_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      post_native->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
      post_native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
      post_native->OMGetBlendState(game_blend.GetAddressOf(), blend_factor.data(), &sample_mask);
      post_native->RSGetState(game_rasterizer.GetAddressOf());
      post_native->OMGetDepthStencilState(game_depth.GetAddressOf(), &stencil_reference);
      post_native->PSGetShaderResources(0, 1, game_view.GetAddressOf());
      post_native->PSGetSamplers(0, 1, game_sampler.GetAddressOf());
      post_native->PSGetConstantBuffers(0, 1, game_constants.GetAddressOf());
      post_native->IAGetInputLayout(game_layout.GetAddressOf());
      post_native->IAGetPrimitiveTopology(&game_topology);
      const std::array<float, 4> constants = {
          static_cast<float>(post_runtime.traced_size[0]) / viewports[0].Width,
          static_cast<float>(post_runtime.traced_size[1]) / viewports[0].Height,
          1.f / static_cast<float>(dlaa_bridge::bridge.width),
          1.f / static_cast<float>(dlaa_bridge::bridge.height),
      };
      std::array<uint8_t, 96> padded = {};
      std::memcpy(padded.data(), constants.data(), sizeof(constants));
      post_native->UpdateSubresource(objects.constants.Get(), 0, nullptr, padded.data(), 0, 0);
      ID3D11ShaderResourceView* ambient = dlaa_bridge::bridge.rt_ambient.srv.Get();
      post_native->VSSetShader(objects.fullscreen_shader.Get(), nullptr, 0);
      post_native->PSSetShader(objects.ambient_shader.Get(), nullptr, 0);
      post_native->PSSetShaderResources(0, 1, &ambient);
      post_native->PSSetSamplers(0, 1, objects.linear_clamp.GetAddressOf());
      post_native->PSSetConstantBuffers(0, 1, objects.constants.GetAddressOf());
      post_native->IASetInputLayout(nullptr);
      post_native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      post_native->OMSetBlendState(objects.multiply.Get(), nullptr, 0xFFFFFFFF);
      post_native->RSSetState(objects.rasterizer.Get());
      post_native->OMSetDepthStencilState(objects.no_depth.Get(), 0);
      post_native->Draw(3, 0);
      post_native->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
      post_native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
      post_native->OMSetBlendState(game_blend.Get(), blend_factor.data(), sample_mask);
      post_native->RSSetState(game_rasterizer.Get());
      post_native->OMSetDepthStencilState(game_depth.Get(), stencil_reference);
      post_native->PSSetShaderResources(0, 1, game_view.GetAddressOf());
      post_native->PSSetSamplers(0, 1, game_sampler.GetAddressOf());
      post_native->PSSetConstantBuffers(0, 1, game_constants.GetAddressOf());
      post_native->IASetInputLayout(game_layout.Get());
      post_native->IASetPrimitiveTopology(game_topology);
    }};
  }
};

namespace internal {

// Ray-Traced Shadows Only: the sun draws' shadow map (a square depth texture of theirs, 1024 and up), learned at each
// frame's first sun draw; while it is bound alone for drawing, the casters are skipped (the game's clear leaves it
// shadowing nothing) as long as the sun draws took the rays' shadows the frame before (a sun shader without the
// patch, screen_space_shadows.hpp, keeps the game's).
static bool shadows_only_installed = false;
// The Debug View: shown once the addon's tone mapper hook draws it (OnTonemapDrawn).
static std::atomic_bool debug_view_drawn = false;
static std::atomic<uint64_t> shadow_map = 0;
static std::atomic_bool skipping_casters = false;

}  // namespace internal

// A sun light draw (pre callback): the frame's first writes the sun's direction (from its lighting buffer, as the game's
// layout for the draw says) for the next trace. Without an ambient occlusion pass this frame (none in the game, or the
// game's SSAO off) the frame's first traces (its G-buffer depth and camera), and each then reads its AO times the
// traced ambient visibility.
constexpr auto OnSunDraw = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!internal::active.load(std::memory_order_relaxed)) return {};
  const auto& draw = command.template GetCallbackData<light_bounce::SunDraw>();
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !frame_history::MainViewport(native)) return {};
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (runtime.settings.shadows_only && runtime.sun_frame != runtime.frame) {
    std::array<ID3D11ShaderResourceView*, 16> views = {};
    native->PSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    uint64_t map = 0;
    for (auto* view : views) {
      if (view == nullptr) continue;
      ComPtr<ID3D11Resource> resource;
      view->GetResource(resource.GetAddressOf());
      view->Release();
      ComPtr<ID3D11Texture2D> texture;
      D3D11_TEXTURE2D_DESC desc = {};
      if (FAILED(resource.As(&texture))) continue;
      texture->GetDesc(&desc);
      if (desc.Width == desc.Height && desc.Width >= 1024
          && (desc.Format == DXGI_FORMAT_R32_TYPELESS || desc.Format == DXGI_FORMAT_R24G8_TYPELESS || desc.Format == DXGI_FORMAT_R16_TYPELESS)) {
        map = reinterpret_cast<uintptr_t>(resource.Get());
      }
    }
    internal::shadow_map.store(map, std::memory_order_relaxed);
  }
  if (runtime.ambient_frame != runtime.frame) {
    auto& objects = runtime.objects;
    if (runtime.traced_frame != runtime.frame) {
      ComPtr<ID3D11ShaderResourceView> depth;
      ComPtr<ID3D11Buffer> camera;
      native->PSGetShaderResources(draw.gbuffer_slot + 3, 1, depth.GetAddressOf());
      native->PSGetConstantBuffers(draw.camera_slot, 1, camera.GetAddressOf());
      internal::Trace(native, depth.Get(), camera.Get());
    }
    ComPtr<ID3D11ShaderResourceView> game_ao;
    if (runtime.settings.ambient_occlusion && runtime.traced_frame == runtime.frame && draw.ao_slot != UINT_MAX) {
      native->PSGetShaderResources(draw.ao_slot, 1, game_ao.GetAddressOf());
    }
    if (game_ao && runtime.combined_frame != runtime.frame) {
      D3D11_TEXTURE2D_DESC current = {};
      if (objects.combined) objects.combined->GetDesc(&current);
      if (current.Width != dlaa_bridge::bridge.width || current.Height != dlaa_bridge::bridge.height) {
        objects.combined.Reset();
        objects.combined_srv.Reset();
        objects.combined_uav.Reset();
        const D3D11_TEXTURE2D_DESC desc = {
            .Width = dlaa_bridge::bridge.width,
            .Height = dlaa_bridge::bridge.height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
            .SampleDesc = {.Count = 1},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
        };
        if (FAILED(objects.device->CreateTexture2D(&desc, nullptr, objects.combined.GetAddressOf()))
            || FAILED(objects.device->CreateShaderResourceView(objects.combined.Get(), nullptr, objects.combined_srv.GetAddressOf()))
            || FAILED(objects.device->CreateUnorderedAccessView(objects.combined.Get(), nullptr, objects.combined_uav.GetAddressOf()))) {
          objects.combined.Reset();
          objects.combined_srv.Reset();
          objects.combined_uav.Reset();
        }
      }
      if (objects.combined) {
        const internal::ComputeState kept(native);
        ComPtr<ID3D11SamplerState> game_sampler;
        native->CSGetSamplers(0, 1, game_sampler.GetAddressOf());
        native->CSSetSamplers(0, 1, objects.linear_clamp.GetAddressOf());
        const std::array<uint32_t, 4> size = {runtime.traced_size[0], runtime.traced_size[1], 0, 0};
        internal::Dispatch(native, objects.combine_shader.Get(), {game_ao.Get(), dlaa_bridge::bridge.rt_ambient.srv.Get()},
                           {objects.combined_uav.Get(), nullptr}, size.data(), sizeof(size), (dlaa_bridge::bridge.width + 7) / 8,
                           (dlaa_bridge::bridge.height + 7) / 8);
        native->CSSetSamplers(0, 1, game_sampler.GetAddressOf());
        runtime.combined_frame = runtime.frame;
        runtime.game_ao = game_ao.Get();
      }
    }
    if (game_ao && game_ao.Get() == runtime.game_ao && runtime.combined_frame == runtime.frame) {
      native->PSSetShaderResources(draw.ao_slot, 1, objects.combined_srv.GetAddressOf());
    }
  }
  if (!runtime.settings.sun_shadows || runtime.sun_frame == runtime.frame || !dlaa_bridge::bridge.ray_tracing || !runtime.objects.constants) {
    return {};
  }
  ComPtr<ID3D11Buffer> lighting;
  native->PSGetConstantBuffers(draw.lighting_slot, 1, lighting.GetAddressOf());
  if (!lighting) return {};
  const internal::ComputeState kept(native);
  ComPtr<ID3D11Buffer> game_second;
  native->CSGetConstantBuffers(1, 1, game_second.GetAddressOf());
  const std::array<uint32_t, 4> offset = {draw.sun_direction, internal::ambient_cube, 0, 0};
  native->UpdateSubresource(runtime.objects.sun_constants.Get(), 0, nullptr, offset.data(), 0, 0);
  native->CSSetShader(runtime.objects.sun_shader.Get(), nullptr, 0);
  ID3D11UnorderedAccessView* sun = dlaa_bridge::bridge.rt_sun.uav.Get();
  constexpr UINT KEEP_COUNT = UINT_MAX;
  native->CSSetUnorderedAccessViews(0, 1, &sun, &KEEP_COUNT);
  ID3D11Buffer* constants[] = {lighting.Get(), runtime.objects.sun_constants.Get()};
  native->CSSetConstantBuffers(0, 2, constants);
  native->Dispatch(1, 1, 1);
  native->CSSetConstantBuffers(1, 1, game_second.GetAddressOf());
  runtime.sun_frame = runtime.frame;
  return {};
};

// Path-Traced Ambient, at each sun light draw after OnSunDraw (its rt_sun read the game's ambient cube first: the
// paths' sky; the paths bring the occluded sky's light instead). A sun shader whose diffuse ambient screen_space_shadows
// patched: t51 a texel of 1 (its diffuse ambient off, the foliage's sky sheen from the cube kept; ATN's debug32: zeroing
// the cube took that sheen too, the far ground came out darker and warmer), its foliage ones also t52 the sun texture
// (their share of the bounce: light_bounce_apply); both unbound after it. Else the draw's lighting constants without the
// game's ambient cube (the copy made once a frame), the game's put back after it. Only while the paths run (a frame
// without them keeps the game's ambient). Put back in finalize callbacks: the Light Bounce draws the sun draw itself
// (bypass), which skips post callbacks (ATN's debug33: t51 left bound took the ambient away under Rays and Off).
constexpr auto OnPathAmbientDraw = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!internal::active.load(std::memory_order_relaxed)) return {};
  const auto& draw = command.template GetCallbackData<light_bounce::SunDraw>();
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !frame_history::MainViewport(native)) return {};
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.path_ambient_on || runtime.bounced_frame + 1 < runtime.frame) return {};
  auto& objects = runtime.objects;
  if (bool foliage = false; screen_space_shadows::AmbientPatched(draw.shader_hash, &foliage)) {
    if (!objects.ambient_off) {
      constexpr float ONE = 1.f;
      const D3D11_TEXTURE2D_DESC one = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                        .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = &ONE, .SysMemPitch = 4};
      ComPtr<ID3D11Texture2D> texture;
      if (FAILED(objects.device->CreateTexture2D(&one, &data, texture.GetAddressOf()))
          || FAILED(objects.device->CreateShaderResourceView(texture.Get(), nullptr, objects.ambient_off.GetAddressOf()))) {
        return {};
      }
    }
    std::array<ID3D11ShaderResourceView*, 2> views = {objects.ambient_off.Get(), foliage ? dlaa_bridge::bridge.rt_sun.srv.Get() : nullptr};
    native->PSSetShaderResources(screen_space_shadows::internal::AMBIENT_SLOT, static_cast<UINT>(views.size()), views.data());
    return {.finalize_callback = [](Context& post, const void*) {
      auto* post_native = internal::NativeContext(post.cmd_list);
      if (post_native == nullptr) return;
      const std::array<ID3D11ShaderResourceView*, 2> none = {};
      post_native->PSSetShaderResources(screen_space_shadows::internal::AMBIENT_SLOT, static_cast<UINT>(none.size()), none.data());
    }};
  }
  ComPtr<ID3D11Buffer> game_lighting;
  native->PSGetConstantBuffers(draw.lighting_slot, 1, game_lighting.GetAddressOf());
  if (!game_lighting) return {};
  D3D11_BUFFER_DESC desc = {};
  game_lighting->GetDesc(&desc);
  constexpr UINT CUBE_BYTES = 96;  // six float3 registers
  const UINT cube_offset = internal::ambient_cube * 4;
  if (desc.ByteWidth < cube_offset + CUBE_BYTES) return {};
  if (objects.ambient_lighting_bytes != desc.ByteWidth) {
    objects.ambient_lighting.Reset();
    objects.ambient_lighting_bytes = 0;
    const D3D11_BUFFER_DESC copy = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(objects.device->CreateBuffer(&copy, nullptr, objects.ambient_lighting.GetAddressOf()))) return {};
    objects.ambient_lighting_bytes = desc.ByteWidth;
  }
  if (!objects.no_ambient) {
    constexpr std::array<float, CUBE_BYTES / 4> ZEROS = {};
    const D3D11_BUFFER_DESC zeros = {.ByteWidth = CUBE_BYTES, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = ZEROS.data()};
    if (FAILED(objects.device->CreateBuffer(&zeros, &data, objects.no_ambient.GetAddressOf()))) return {};
  }
  if (runtime.ambient_off_frame != runtime.frame || runtime.ambient_off_source != game_lighting.Get()) {
    native->CopyResource(objects.ambient_lighting.Get(), game_lighting.Get());
    const D3D11_BOX cube = {.left = 0, .top = 0, .front = 0, .right = CUBE_BYTES, .bottom = 1, .back = 1};
    native->CopySubresourceRegion(objects.ambient_lighting.Get(), 0, cube_offset, 0, 0, objects.no_ambient.Get(), 0, &cube);
    runtime.ambient_off_frame = runtime.frame;
    runtime.ambient_off_source = game_lighting.Get();
  }
  native->PSSetConstantBuffers(draw.lighting_slot, 1, objects.ambient_lighting.GetAddressOf());
  runtime.ambient_game_lighting = std::move(game_lighting);
  runtime.ambient_slot = draw.lighting_slot;
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = internal::NativeContext(post.cmd_list);
    auto& post_runtime = internal::runtime;
    const std::unique_lock post_lock(post_runtime.mutex);
    if (post_native != nullptr) {
      post_native->PSSetConstantBuffers(post_runtime.ambient_slot, 1, post_runtime.ambient_game_lighting.GetAddressOf());
    }
    post_runtime.ambient_game_lighting.Reset();
  }};
};

namespace internal {

// The point lights' instance data as the game's input layouts give it (POSITION1: the light's colour, POSITION2: its
// world position and reach; the light shaders' inputs), by input layout.
struct PointLightLayout {
  uint32_t slot = 0;
  uint32_t colour = 0;
  uint32_t position = 0;
};
static std::mutex point_light_mutex;
static std::unordered_map<uint64_t, PointLightLayout> point_light_layouts;
static bool point_lights_installed = false;
static bool shadow_readers_installed = false;

// The light draws' instances copied for the CPU into a ring of staging buffers, read when the ring comes round (three
// frames on: the GPU done with them) into the lights the next trace picks from.
constexpr UINT POINT_LIGHT_BYTES = 512 * 1024;
struct PointLightCopy {
  UINT offset = 0;  // bytes into the staging buffer
  UINT count = 0;
  UINT stride = 0;
  PointLightLayout layout;
};
struct PointLightReadback {
  ComPtr<ID3D11Device> device;
  std::array<ComPtr<ID3D11Buffer>, 3> staging;
  std::array<std::vector<PointLightCopy>, 3> copies;
  std::array<UINT, 3> used = {};
  uint64_t frame = UINT64_MAX;  // the frame the current buffer fills in
  size_t current = 0;
};
static PointLightReadback point_light_readback;
static bool fog_shadows_installed = false;
static bool smoke_shadows_installed = false;

inline void OnInitInputLayout(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                              const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline pipeline) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  for (const auto& subobject : std::span(subobjects, subobject_count)) {
    if (subobject.type != reshade::api::pipeline_subobject_type::input_layout) continue;
    const std::span elements(static_cast<const reshade::api::input_element*>(subobject.data), subobject.count);
    const auto find = [&](uint32_t index) {
      return std::ranges::find_if(elements, [&](const reshade::api::input_element& element) {
        return element.semantic != nullptr && std::string_view(element.semantic) == "POSITION" && element.semantic_index == index;
      });
    };
    const auto colour = find(1);
    const auto position = find(2);
    if (colour == elements.end() || position == elements.end() || colour->buffer_binding != position->buffer_binding) continue;
    const std::unique_lock lock(point_light_mutex);
    point_light_layouts[pipeline.handle] = {.slot = position->buffer_binding, .colour = colour->offset, .position = position->offset};
  }
}

inline void OnDestroyInputLayout(reshade::api::device*, reshade::api::pipeline pipeline) {
  const std::unique_lock lock(point_light_mutex);
  point_light_layouts.erase(pipeline.handle);
}

}  // namespace internal

// A point light draw (the addon's replaced light shaders with rt_local_lights.hlsli): its instances copied for the next
// traces; this frame's traced lights' bits and positions bound (put back after).
constexpr auto OnPointLightDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!internal::active.load(std::memory_order_relaxed)) return {};
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr) return {};
  uint64_t frame = 0;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    if (!internal::runtime.settings.local_lights) return {};
    frame = internal::runtime.frame;
  }
  auto& readback = internal::point_light_readback;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (readback.device != device) {
    readback = {.device = device};
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = internal::POINT_LIGHT_BYTES, .Usage = D3D11_USAGE_STAGING,
                                            .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    for (auto& buffer : readback.staging) device->CreateBuffer(&staging_desc, nullptr, buffer.GetAddressOf());
  }
  // A new frame: the buffer filled three frames ago read (each light once) into this frame's lights, then filled again.
  if (readback.frame != frame) {
    readback.frame = frame;
    readback.current = (readback.current + 1) % readback.staging.size();
    auto& buffer = readback.staging[readback.current];
    auto& copies = readback.copies[readback.current];
    if (D3D11_MAPPED_SUBRESOURCE mapped = {}; !copies.empty() && SUCCEEDED(native->Map(buffer.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      std::vector<LocalLight> lights;
      for (const auto& copy : copies) {
        for (UINT index = 0; index < copy.count; ++index) {
          const auto* instance = static_cast<const std::byte*>(mapped.pData) + copy.offset + (index * copy.stride);
          LocalLight light = {};
          std::memcpy(light.light.data(), instance + copy.layout.position, sizeof(light.light));
          std::memcpy(light.colour.data(), instance + copy.layout.colour, sizeof(light.colour));
          lights.push_back(light);
        }
      }
      native->Unmap(buffer.Get(), 0);
      std::ranges::sort(lights, {}, &LocalLight::light);
      const auto repeated = std::ranges::unique(lights, {}, &LocalLight::light);
      lights.erase(repeated.begin(), repeated.end());
      AddLocalLights(lights);
    }
    copies.clear();
    readback.used[readback.current] = 0;
  }
  ComPtr<ID3D11InputLayout> input_layout;
  native->IAGetInputLayout(input_layout.GetAddressOf());
  internal::PointLightLayout layout;
  {
    const std::unique_lock lock(internal::point_light_mutex);
    const auto found = internal::point_light_layouts.find(reinterpret_cast<uintptr_t>(input_layout.Get()));
    if (found != internal::point_light_layouts.end()) layout = found->second;
  }
  ComPtr<ID3D11Buffer> instances;
  UINT stride = 0;
  UINT offset = 0;
  if (input_layout) native->IAGetVertexBuffers(layout.slot, 1, instances.GetAddressOf(), &stride, &offset);
  auto& buffer = readback.staging[readback.current];
  UINT& used = readback.used[readback.current];
  UINT instance_count = 0;
  UINT first_instance = 0;
  if constexpr (requires { command.arguments.instance_count; }) {
    instance_count = command.arguments.instance_count;
    first_instance = command.arguments.first_instance;
  }
  const UINT count = (stride == 0) ? 0 : std::min(instance_count, (internal::POINT_LIGHT_BYTES - used) / stride);
  if (instances && buffer && count != 0 && std::max(layout.colour, layout.position) + 16 <= stride) {
    const UINT first = offset + (first_instance * stride);
    const D3D11_BOX box = {.left = first, .top = 0, .front = 0, .right = first + (count * stride), .bottom = 1, .back = 1};
    native->CopySubresourceRegion(buffer.Get(), 0, used, 0, 0, instances.Get(), 0, &box);
    readback.copies[readback.current].push_back({.offset = used, .count = count, .stride = stride, .layout = layout});
    used += count * stride;
  }
  if (!BindLocalLights(native)) return {};
  return {.post_callback = [](Context& post, const void*) {
    auto* post_native = internal::NativeContext(post.cmd_list);
    if (post_native != nullptr) UnbindLocalLights(post_native);
  }};
};

// Attila's and Thrones' point light shaders without the game's point shadow maps (shaders/pointlight00-07: smooth and
// linear falloff, standard and skin lighting, high and low detail).
constexpr std::array<uint32_t, 8> POINT_LIGHT_SHADERS = {0x6426B042, 0x90981DA1, 0x89FF812E, 0xFD3BF1A8,
                                                         0x593989DE, 0x0D7A1213, 0xB8F368CF, 0xC9E4E52D};

// The point light draws (the addon's replaced pixel shaders reading rt_local_lights.hlsli), kept while installed: the
// game's input layouts tracked from now (before the device: install at load), Ray-Traced Point Light Shadows shown.
inline void InstallPointLights(std::span<const uint32_t> shader_hashes) {
  internal::point_lights_installed = true;
  reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitInputLayout);
  reshade::register_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyInputLayout);
  for (const uint32_t shader_hash : shader_hashes) {
    renodx::utils::command_action::Register(
        OnPointLightDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

// Attila's and Thrones' battle fog (shaders/battlefog00, shader model 3; the MSAA one left out: DLAA has none).
constexpr std::array<uint32_t, 1> FOG_SHADERS = {0xA6A0F887};

// A fog draw (the addon's replaced fog shaders with rt_fog.hlsli): this frame's fog shadows bound (put back after).
constexpr auto OnFogDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!internal::active.load(std::memory_order_relaxed)) return {};
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !BindFogShadows(native)) return {};
  return {.post_callback = [](Context& post, const void*) {
    auto* post_native = internal::NativeContext(post.cmd_list);
    if (post_native != nullptr) UnbindFogShadows(post_native);
  }};
};

// Attila's and Thrones' particles (particle_vfx_main: one vertex a particle, a geometry shader makes its quad) and the
// main pass's lit pixel shader (ps_main_high_quality: smoke and dust; fire's glow is added, the overlay pass's).
constexpr uint32_t PARTICLE_VERTEX_SHADER = 0xA7F257B5;
constexpr uint32_t LIT_PARTICLE_PIXEL_SHADER = 0xACE33D5D;

// A particle draw (Smoke Shadows): its lit particles streamed out for the next trace.
constexpr auto OnParticleDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (internal::active.load(std::memory_order_relaxed)) internal::CaptureParticles(command);
  return {};
};

// The particle draws (`vertex_shader`'s with `pixel_shader`), kept while installed (after Install, before the device:
// the vertex shader's code is kept as it is made, by the scene's code hook, made here when the scene has none):
// Ray-Traced Smoke Shadows shown.
inline void InstallSmokeShadows(uint32_t vertex_shader, uint32_t pixel_shader) {
  internal::smoke_shadows_installed = true;
  internal::smoke_vertex_shader = vertex_shader;
  internal::smoke_pixel_shader = pixel_shader;
  if (!internal::clip_captures) reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  renodx::utils::command_action::Register(
      OnParticleDraw, {.shader_hash = vertex_shader, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

// The fog draws (the addon's replaced pixel shaders reading rt_fog.hlsli), kept while installed: Ray-Traced Fog Shadows
// shown.
inline void InstallFogShadows(std::span<const uint32_t> shader_hashes) {
  internal::fog_shadows_installed = true;
  for (const uint32_t shader_hash : shader_hashes) {
    renodx::utils::command_action::Register(
        OnFogDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

// Attila's forward passes reading the sun's shadow map (ReShade.log 2026-10-28: lit everywhere under Ray-Traced Shadows
// Only): two of the game's (patched: screen_space_shadows::PatchForwardShadows) and the particles' (shaders/vfx00, which
// reads t49 itself); with them the particles' other path (shaders/vfx01: no shadows, the traced lights at b9 alone).
// The campaign's sea too (waterplane ps30_campaign: high to ultra, low; ATN's debug37: lit in the rays' shadows).
constexpr std::array<uint32_t, 2> CAMPAIGN_SEA_SHADERS = {0x05A111E1, 0x450F6992};
constexpr std::array<uint32_t, 6> SHADOW_READER_SHADERS = {0xC9FBACCB, 0x2E9F5935, 0xACE33D5D, 0xF5379F78, CAMPAIGN_SEA_SHADERS[0], CAMPAIGN_SEA_SHADERS[1]};

// A forward pass's draw reading the sun's shadow map: under Ray-Traced Shadows Only (its map empty) the rays' occlusion
// at t49 (the patched sun draws', this frame's), else none (t49 reads 0: the game's shadow).
constexpr auto OnShadowReaderDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr) return {};
  ID3D11ShaderResourceView* occlusion = nullptr;
  ID3D11ShaderResourceView* tint = nullptr;  // Coloured Shadows' filter (t50)
  ID3D11Buffer* lights = nullptr;  // the particles' (vfx00): torches and fires light the smoke and dust near them
  if (internal::active.load(std::memory_order_relaxed)) {
    const std::unique_lock lock(internal::runtime.mutex);
    if (internal::runtime.settings.shadows_only && internal::runtime.occlusion_frame == internal::runtime.frame) {
      occlusion = internal::runtime.objects.occlusion_srv.Get();
      if (internal::runtime.settings.coloured_shadows) tint = dlaa_bridge::bridge.rt_tint.srv.Get();
    }
    if (internal::runtime.lights_traced && internal::runtime.traced_frame == internal::runtime.frame) {
      lights = internal::runtime.objects.light_constants.Get();
    }
  }
  native->PSSetShaderResources(49, 1, &occlusion);
  native->PSSetShaderResources(50, 1, &tint);
  native->PSSetConstantBuffers(9, 1, &lights);
  return {};
};

// The forward passes reading the sun's shadow map (`shader_hashes`), kept while installed: the rays' shadows under
// Ray-Traced Shadows Only.
inline void InstallShadowReaders(std::span<const uint32_t> shader_hashes) {
  internal::shadow_readers_installed = true;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    internal::runtime.reader_shaders.insert(shader_hashes.begin(), shader_hashes.end());
  }
  for (const uint32_t shader_hash : shader_hashes) {
    renodx::utils::command_action::Register(
        OnShadowReaderDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

// A shadow pass draw that alpha-tests its mesh: its diffuse map (t0) noted, the scene's meshes with it cut out as well (ATN's
// debug37: campaign palms and pines were solid cards, blocks of black shadow). Runs before OnCasterDraw: skipped casters too.
constexpr auto OnCutOutShadowDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = internal::NativeContext(command.cmd_list);
  if (native == nullptr || !internal::active.load(std::memory_order_relaxed)) return {};
  ComPtr<ID3D11ShaderResourceView> view;
  native->PSGetShaderResources(0, 1, view.GetAddressOf());
  if (!view) return {};
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.cut_out_textures.insert(resource.Get());
  return {};
};

// The game's shadow pass pixel shader that alpha-tests its meshes.
inline void InstallCutOuts(uint32_t shadow_shader_hash) {
  renodx::utils::command_action::Register(
      OnCutOutShadowDraw, {.shader_hash = shadow_shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

// Ray-Traced Shadows Only: draws are skipped while the sun's shadow map is bound alone (depth only) for drawing.
inline void OnBindTargets(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view*,
                          reshade::api::resource_view dsv) {
  bool skip = false;
  const uint64_t map = internal::shadow_map.load(std::memory_order_relaxed);
  if (count == 0 && dsv.handle != 0 && map != 0 && cmd_list->get_device()->get_resource_from_view(dsv).handle == map) {
    const std::unique_lock lock(internal::runtime.mutex);
    skip = internal::runtime.settings.shadows_only && internal::runtime.occlusion_frame + 1 >= internal::runtime.frame;
  }
  internal::skipping_casters.store(skip, std::memory_order_relaxed);
}

// Under it, in the frame Update picks every 10 s: a draw whose pixel shader reads the sun's shadow map and is no patched
// sun draw (forward-lit: lit everywhere, the map has no casters) has its pixel shader noted for the report.
inline void NoteShadowReader(reshade::api::command_list* cmd_list) {
  auto& runtime = internal::runtime;
  if (runtime.readers_frame != runtime.frame) return;
  const uint64_t map = internal::shadow_map.load(std::memory_order_relaxed);
  auto* native = internal::NativeContext(cmd_list);
  if (map == 0 || native == nullptr) return;
  std::array<ID3D11ShaderResourceView*, 16> views = {};
  native->PSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  bool reads = false;
  for (auto* view : views) {
    if (view == nullptr) continue;
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    view->Release();
    reads = reads || reinterpret_cast<uintptr_t>(resource.Get()) == map;
  }
  if (!reads) return;
  const uint32_t shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
  const uint32_t vertex_shader = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.sun_draw_shaders.contains(shader) && !runtime.reader_shaders.contains(shader)) {
    runtime.shadow_readers.insert((uint64_t{shader} << 32) | vertex_shader);
  }
}

inline bool OnCasterDraw(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, uint32_t) {
  NoteShadowReader(cmd_list);
  return internal::skipping_casters.load(std::memory_order_relaxed);
}

inline bool OnCasterDrawIndexed(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  NoteShadowReader(cmd_list);
  return internal::skipping_casters.load(std::memory_order_relaxed);
}

inline bool OnCasterDrawIndirect(reshade::api::command_list*, reshade::api::indirect_command, reshade::api::resource, uint64_t, uint32_t,
                                 uint32_t) {
  return internal::skipping_casters.load(std::memory_order_relaxed);
}

// The sun's shadow map's casters skipped under Ray-Traced Shadows Only (shown once installed; needs InstallSunDraws).
inline void InstallShadowsOnly() {
  internal::shadows_only_installed = true;
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindTargets);
  reshade::register_event<reshade::addon_event::draw>(OnCasterDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(OnCasterDrawIndexed);
  reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnCasterDrawIndirect);
}

// The scene's shaders (`shaders`) and the game's ambient occlusion passes (none: the trace at the sun draws), the
// addon's, kept while installed (after the ambient occlusion pass's own hooks: its result is multiplied after); the
// Light Bounce's trace through rays.
inline void Install(std::span<const SceneShader> shaders, std::span<const ao_pass::AmbientOcclusionDraw> ambient_draws,
                    const Host& host = TW_HOST) {
  InstallScene(shaders, host);
  internal::sprite_shadows_installed = std::ranges::any_of(shaders, &SceneShader::sun_facing);
  internal::far_tree_shadows_installed = std::ranges::any_of(shaders, &SceneShader::far_tree);
  light_bounce::SetRayTracedTrace(&internal::TraceLightBounce);
  for (const auto& draw : ambient_draws) {
    renodx::utils::command_action::Register(
        OnAmbientOcclusionDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &draw);
  }
}

// The sun light draws (the addon's, kept while installed): before the screen-space shadows (their occlusion reads this
// frame's trace), the AO filter and the Light Bounce.
// `ambient_cube`: the first float of the lighting buffer's ambient cube (six colours: +x, -x, +y, -y, +z, -z; the paths'
// sky), 0 none.
inline void InstallSunDraws(std::span<const light_bounce::SunDraw> sun_draws, UINT ambient_cube = 0) {
  internal::ambient_cube = ambient_cube;
  for (const auto& draw : sun_draws) {
    internal::runtime.sun_draw_shaders.insert(draw.shader_hash);
    renodx::utils::command_action::Register(
        OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
    if (ambient_cube != 0) {
      renodx::utils::command_action::Register(
          OnPathAmbientDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
          &draw);
    }
  }
}

inline void Uninstall() {
  light_bounce::SetRayTracedTrace(nullptr);
  renodx::utils::command_action::Unregister(OnAmbientOcclusionDraw);
  renodx::utils::command_action::Unregister(OnSunDraw);
  if (internal::ambient_cube != 0) renodx::utils::command_action::Unregister(OnPathAmbientDraw);
  if (std::exchange(internal::fog_shadows_installed, false)) renodx::utils::command_action::Unregister(OnFogDraw);
  if (std::exchange(internal::smoke_shadows_installed, false)) {
    renodx::utils::command_action::Unregister(OnParticleDraw);
    if (!internal::clip_captures) reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  }
  if (std::exchange(internal::shadow_readers_installed, false)) renodx::utils::command_action::Unregister(OnShadowReaderDraw);
  if (std::exchange(internal::shadows_only_installed, false)) {
    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindTargets);
    reshade::unregister_event<reshade::addon_event::draw>(OnCasterDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnCasterDrawIndexed);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnCasterDrawIndirect);
  }
  if (std::exchange(internal::point_lights_installed, false)) {
    renodx::utils::command_action::Unregister(OnPointLightDraw);
    reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitInputLayout);
    reshade::unregister_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyInputLayout);
  }
  renodx::utils::command_action::Unregister(OnCutOutShadowDraw);
  UninstallScene();
}

// The settings after the anti-aliasing's (`ambient_alone`: the ray-traced ambient occlusion in place of the game's and
// GT-VBAO, Attila's).
// `reflection_pass`: the game's reflection pass reads the ray-traced reflections (Attila, Thrones, Warhammer,
// Warhammer II); the others get no reflection settings.
// `light_bounce_strength`: the Light Bounce's (Lighting), for a note when the effects that replace its trace are on
// without it (null: none).
inline void AppendSettings(std::vector<renodx::utils::settings::Setting*>* settings, Sliders* sliders,
                           const feature_settings::AaSliders* aa,
                           bool ambient_alone = false, bool reflection_pass = false,
                           const std::string& section = feature_settings::AA_SECTION,
                           const float* light_bounce_strength = nullptr) {
  const auto dlaa_on = [aa] { return aa->method == 5.f; };
  // Turned on: the Light Bounce (Lighting) on with them when it was off (they trace its light: without it they did
  // nothing, ATN 2026-10-28).
  const auto light_bounce_on = [light_bounce_strength] {
    if (light_bounce_strength == nullptr || *light_bounce_strength > 0.f) return;
    if (const auto* setting = renodx::utils::settings::FindSetting("RayTracedLightBounce"); setting != nullptr && setting->GetValue() != 0.f) {
      renodx::utils::settings::UpdateSettings({{"light_bounce_strength", 1.f}});
    }
  };
  settings->insert(settings->end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Ray tracing and path tracing do not work correctly yet (fixed in the next update).",
          .section = section,
          .tint = 0xFF5050,
      },
  });
  settings->insert(settings->end(), {
      new renodx::utils::settings::Setting{
          .key = "RayTracedSunShadows",
          .binding = &sliders->sun_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Shadows (Experimental)",
          .section = section,
          .tooltip = "Every shadow from rays: the sun's (terrain, buildings, trees, units, grass, banners) and the torches' and\n"
                     "fires'. Replaces the game's shadow maps and Screen-Space Shadows.\n"
                     "Needs DLAA, an RTX GPU and renodx-tw-dlaa64.exe.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedShadowSoftness",
          .binding = &sliders->sun_softness,
          .default_value = 0.5f,
          .label = "Ray-Traced Shadow Softness",
          .section = section,
          .tooltip = "How soft ray-traced shadows get away from what casts them. 0: sharp.",
          .min = 0.f,
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightSoftness",
          .binding = &sliders->light_softness,
          .default_value = 0.25f,
          .label = "Torch and Fire Shadow Softness",
          .section = section,
          .tooltip = "How big torches and fires are for their soft shadows. 0: sharp.\n"
                     "Needs Ray Reconstruction (DLSS's Ray Reconstruction setting).",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f m",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightImportance",
          .binding = &sliders->light_importance,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Importance-Sampled Torch Shadows (Experimental)",
          .section = section,
          .tooltip = "Two torch / fire shadow rays a pixel, picked by how much light they give (as ReSTIR DI picks lights):\n"
                     "faster with many fires; dim fires' shadows follow moving units slower.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightGlow",
          .binding = &sliders->light_glow,
          .default_value = 1.f,
          .label = "Torch and Fire Light in Fog and Smoke",
          .section = section,
          .tooltip = "Torches and fires light the fog around them (with Ray-Traced Fog Shadows: their shafts and\n"
                     "shadows in it) and the smoke and dust near them. 0: off.",
          .min = 0.f,
          .max = 2.f,
          .format = "%.2f",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingOutputResolution",
          .binding = &sliders->output_rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Rays a Pixel",
          .section = section,
          .tooltip = "More rays for every rendered pixel, each from its own spot of it: less noise, finer shadow edges.\n"
                     "The cost grows with the rays. Match Output: as many rays on screen as at DLAA (up to 4 a pixel,\n"
                     "1 at DLAA). 2 to 4 rays also work at DLAA. The rendered image stays at DLSS's size.",
          .labels = {"1", "Match Output", "2", "3", "4"},
          .is_enabled = [aa] { return aa->method == 5.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingRaysShadows",
          .binding = &sliders->rays_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Rays a Pixel: Shadows",
          .section = section,
          .tooltip = "The sun's and the torches' and fires' shadows take the extra rays.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->output_rays != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingRaysAmbient",
          .binding = &sliders->rays_ambient,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Rays a Pixel: Ray-Traced AO",
          .section = section,
          .tooltip = "Ray-Traced AO takes the extra rays.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->output_rays != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingRaysReflections",
          .binding = &sliders->rays_reflections,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Rays a Pixel: Reflections",
          .section = section,
          .tooltip = "Ray-traced reflections take the extra rays.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->output_rays != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracingRaysPaths",
          .binding = &sliders->rays_paths,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Rays a Pixel: Global Illumination",
          .section = section,
          .tooltip = "Ray-traced and path-traced Global Illumination and Path-Traced AO take the extra rays\n"
                     "(up to 16 a pixel). The heaviest of the four.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->output_rays != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSpriteShadows",
          .binding = &sliders->sprite_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Sprite Soldier Shadows",
          .section = section,
          .tooltip = "The flat far soldiers cast ray-traced shadows too.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::sprite_shadows_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedFarTreeShadows",
          .binding = &sliders->far_tree_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Far Tree Shadows",
          .section = section,
          .tooltip = "The flat far trees cast ray-traced shadows too.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::far_tree_shadows_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedDebugLog",
          .binding = &sliders->debug_log,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Log Details",
          .section = section,
          .tooltip = "Ray tracing's numbers in the logs every 10 s, for bug reports. With it on when a battle starts,\n"
                     "also where a GPU reset stopped. Off: nothing of this runs.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLeafLight",
          .binding = &sliders->leaf_light,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Light Through Leaves",
          .section = section,
          .tooltip = "Leaves, cloth and sails let some sunlight through ray-traced shadows (where they reach past the game's).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f; },
          // Shown where the scene has alpha-tested leaves or cloth (Warhammer III's has neither).
          .is_visible = [] {
            return std::ranges::any_of(internal::scene_shaders,
                                       [](const SceneShader& shader) { return (shader.foliage && !shader.dynamic) || shader.thin; });
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedColouredShadows",
          .binding = &sliders->coloured_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Coloured Shadows",
          .section = section,
          .tooltip = "Sunlight through cloth, sails and banners takes their colour.\nNeeds Light Through Leaves and Ray Reconstruction.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f && sliders->leaf_light != 0.f; },
          .is_visible = [] { return internal::ambient_cube != 0; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedGrassSway",
          .binding = &sliders->grass_sway,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Swaying Grass Shadows",
          .section = section,
          .tooltip = "Ray-traced grass sways with the grass (as much as fits a frame).\n"
                     "Off: still grass in the ray tracing, faster and less VRAM.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
          // Shown where grass is placed (Attila).
          .is_visible = [] {
            return std::ranges::any_of(internal::scene_shaders, [](const SceneShader& shader) { return shader.grass && !shader.rows_shader.empty(); });
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSwayRange",
          .binding = &sliders->sway_range,
          .default_value = 0.f,
          .label = "Swaying Shadows Range (Experimental)",
          .section = section,
          .tooltip = "Trees and grass sway in the ray tracing only this close to the camera;\n"
                     "farther ones stay still: faster. 0 m: everywhere.",
          .min = 0.f,
          .max = 300.f,
          .format = "%.0f m",
          .is_enabled = dlaa_on,
          .is_visible = [] {
            return std::ranges::any_of(internal::scene_shaders, [](const SceneShader& shader) { return shader.foliage && !shader.rows_shader.empty(); });
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedOffScreenSway",
          .binding = &sliders->sway_off_screen,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Off-Screen Sway (Experimental)",
          .section = section,
          .tooltip = "Off: trees and grass away from the view stay still in the ray tracing: faster.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
          .is_visible = [] {
            return std::ranges::any_of(internal::scene_shaders, [](const SceneShader& shader) { return shader.foliage && !shader.rows_shader.empty(); });
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedGrassShadowRange",
          .binding = &sliders->grass_shadow_range,
          .default_value = 0.f,
          .label = "Grass Shadow Range (Experimental)",
          .section = section,
          .tooltip = "Past this distance grass casts no ray-traced shadows (on itself or the ground):\n"
                     "faster over fields. 0 m: everywhere.",
          .min = 0.f,
          .max = 200.f,
          .format = "%.0f m",
          .is_enabled = dlaa_on,
          .is_visible = [] {
            return std::ranges::any_of(internal::scene_shaders, [](const SceneShader& shader) { return shader.grass; });
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAmbientOcclusion",
          .binding = &sliders->ambient_occlusion,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray-Traced Ambient Occlusion (Experimental)",
          .section = section,
          .tooltip = ambient_alone ? "Darker crevices and contact shade from rays, in place of the game's ambient occlusion and\n"
                                     "GT-VBAO. Needs DLAA, an RTX GPU and renodx-tw-dlaa64.exe."
                                   : "Darker crevices and contact shade from rays, on top of the game's ambient occlusion\n"
                                     "(keep it on). Needs DLAA, an RTX GPU and renodx-tw-dlaa64.exe.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAORange",
          .binding = &sliders->ambient_range,
          .default_value = 2.f,
          .label = "Ray-Traced AO Range",
          .section = section,
          .tooltip = "How far its rays look for something in the way.",
          .min = 0.5f,
          .max = 10.f,
          .format = "%.1f m",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAORays",
          .binding = &sliders->ambient_rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 2.f,
          .label = "Ray-Traced AO Rays",
          .section = section,
          .tooltip = "More rays: less noise. Heavier.",
          .min = 1.f,
          .max = 8.f,
          .format = "%d",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAOQuarter",
          .binding = &sliders->ambient_quarter,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced AO Rate",
          .section = section,
          .tooltip = "Quarter: one pixel of each 2 x 2 traced a frame, in turn (a quarter of the rays; DLAA / DLSS\n"
                     "smooth it over frames). Faster, a little softer at edges.",
          .labels = {"Every pixel", "Quarter"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLeafMicromaps",
          .binding = &sliders->micromaps,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Leaf Micromaps (Experimental)",
          .section = section,
          .tooltip = "Faster ray-traced shadows through leaves: their cut-outs are baked once (NVIDIA Opacity Micro-Map SDK).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] {
            return aa->method == 5.f && (sliders->sun_shadows != 0.f || sliders->ambient_occlusion != 0.f || sliders->light_bounce != 0.f);
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedGpuMicromaps",
          .binding = &sliders->gpu_micromaps,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "GPU Micromap Baking",
          .section = section,
          .tooltip = "Leaf Micromaps baked on the GPU (NVIDIA's way in game) instead of CPU threads:\n"
                     "frees the CPU, and new grass's and leaves' shadows settle sooner.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->micromaps != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedFastLeafShadows",
          .binding = &sliders->fast_leaves,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Fast Leaf Shadows (Experimental)",
          .section = section,
          .tooltip = "Leaves and grass with Leaf Micromaps skip their alpha tests:\n"
                     "faster shadows under trees and in fields, but coarser and darker.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->micromaps != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedFogShadows",
          .binding = &sliders->fog_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Fog Shadows (Experimental)",
          .section = section,
          .tooltip = "Light shafts: the fog loses the sun's glow where it lies in the shadow of buildings, trees and hills.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
          .is_visible = [] { return internal::fog_shadows_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSmokeShadows",
          .binding = &sliders->smoke_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Smoke Shadows (Experimental)",
          .section = section,
          .tooltip = "Smoke and dust dim the ray-traced sunlight they drift through (with Ray-Traced Shadows).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->sun_shadows != 0.f; },
          .is_visible = [] { return internal::smoke_shadows_installed; },
      },
      // One tracer for the Light Bounce (ATN: Path Tracing and the rays could both be on, only one ran).
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounce",
          .binding = &sliders->light_bounce,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray-Traced Light Bounce (Experimental)",
          .section = section,
          .tooltip = "What finds the Light Bounce's light in place of its screen-space search:\n"
                     "Rays: one bounce through the scene, nothing guessed behind what the camera sees.\n"
                     "Path Tracing: light bouncing several times, also from what is off screen. Heavy.\n"
                     "Turns the Light Bounce on. Needs DLAA, an RTX GPU and renodx-tw-dlaa64.exe.",
          .labels = {"Off", "Rays", "Path Tracing"},
          .is_enabled = dlaa_on,
          .on_change = light_bounce_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedPathAmbient",
          .binding = &sliders->path_ambient,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path-Traced Ambient (Experimental)",
          .section = section,
          .tooltip = "With Path Tracing: the paths bring the sky's light too, in place of the game's ambient light and the\n"
                     "ray-traced AO (less noise, faster). Small contact shadows follow the Light Bounce's resolution.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->light_bounce == 2.f; },
          .is_visible = [] { return internal::ambient_cube != 0; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedPathSer",
          .binding = &sliders->path_ser,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path Tracing SER (Experimental)",
          .section = section,
          .tooltip = "Shader execution reordering for Path Tracing and Ray-Traced Reflections\n"
                     "(DXR 1.2, RTX 40 / 50 series): may be faster.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && (sliders->light_bounce == 2.f || sliders->reflections != 0.f); },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSharc",
          .binding = &sliders->sharc,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "SHaRC Radiance Cache (Experimental)",
          .section = section,
          .tooltip = "With Path Tracing: NVIDIA's SHaRC for the light of what the screen does not show\n"
                     "(more bounces off-screen, paths end sooner).",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->light_bounce == 2.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceRays",
          .binding = &sliders->light_bounce_rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 4.f,
          .label = "Ray-Traced Light Bounce Rays",
          .section = section,
          .tooltip = "More rays: less noise. Heavier.",
          .min = 1.f,
          .max = 16.f,
          .format = "%d",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->light_bounce != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceResolution",
          .binding = &sliders->light_bounce_resolution,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Ray-Traced Light Bounce Resolution",
          .section = section,
          .tooltip = "Its own resolution (the screen-space Light Bounce's settings no longer apply to it).\n"
                     "Full: sharpest, about 4x Half's cost.",
          .labels = {"Full", "Half", "Quarter"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->light_bounce != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceRange",
          .binding = &sliders->light_bounce_range,
          .default_value = 10.f,
          .label = "Ray-Traced Light Bounce Range",
          .section = section,
          .tooltip = "How far its rays look for light.",
          .min = 1.f,
          .max = 50.f,
          .format = "%.0f m",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->light_bounce != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Light Bounce (Lighting) is off: the Ray-Traced Light Bounce does nothing without it.",
          .section = section,
          .tint = 0xFF5050,
          .is_visible = [sliders, light_bounce_strength] {
            return light_bounce_strength != nullptr && *light_bounce_strength <= 0.f && sliders->light_bounce != 0.f;
          },
      },
      new renodx::utils::settings::Setting{
          .key = "PathTracingBounces",
          .binding = &sliders->path_bounces,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 2.f,
          .label = "Path Tracing Bounces",
          .section = section,
          .tooltip = "Bounces past the first. More: brighter shade, heavier.",
          .min = 1.f,
          .max = 4.f,
          .format = "%d",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->light_bounce == 2.f; },
      },
  });
  if (!reflection_pass) return;
  settings->insert(settings->end(), {
      new renodx::utils::settings::Setting{
          .key = "RayTracedReflections",
          .binding = &sliders->reflections,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Reflections (Experimental)",
          .section = section,
          .tooltip = "Reflections on armour, metal and water from rays: what is off screen or hidden shows too.\n"
                     "Needs DLAA, an RTX GPU, renodx-tw-dlaa64.exe and in-game Screen Space Reflections on.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedReflectionSamples",
          .binding = &sliders->reflection_samples,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Ray-Traced Reflection Samples",
          .section = section,
          .tooltip = "Rays per pixel on rough surfaces: less noise. Heavier.",
          .min = 1.f,
          .max = 8.f,
          .format = "%d",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->reflections != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedReflectionReuse",
          .binding = &sliders->reflection_reuse,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Reflection Reuse (Experimental)",
          .section = section,
          .tooltip = "Less noise on rough reflections.\n"
                     "From ReSTIR GI: also the path-traced Light Bounce's samples (needs Path Tracing).\n"
                     "Own ReSTIR: their own samples, reused over frames and between neighbours.",
          .labels = {"Off", "From ReSTIR GI", "Own ReSTIR"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->reflections != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedWaterReflections",
          .binding = &sliders->water_reflections,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Ray-Traced Water Reflections (Experimental)",
          .section = section,
          .tooltip = "Rivers and lakes reflect what the rays find, also off screen. Off: screen-space reflections there.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->reflections != 0.f; },
          .is_visible = [] { return internal::water_pass; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSpecularOcclusion",
          .binding = &sliders->specular_occlusion,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Specular Occlusion (Experimental)",
          .section = section,
          .tooltip = "The sky's reflection dimmed where something blocks it.\n"
                     "Needs DLAA, an RTX GPU, renodx-tw-dlaa64.exe and in-game Screen Space Reflections on.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedDebugView",
          .binding = &sliders->debug_view,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray Tracing Debug View",
          .section = section,
          .tooltip = "A ray-traced effect's raw result as the picture (All: every one tiled), to check they work.\n"
                     "Screen-Space AO: the game's or GT-VBAO's. Magenta: it made nothing (off or not traced).",
          .labels = {"Off", "Sun Shadows", "Ambient Occlusion", "Light Bounce", "Reflections", "Specular Occlusion",
                     "Point Light Shadows", "Fog Shadows", "Screen-Space AO", "All"},
          .is_enabled = dlaa_on,
          .is_visible = [] { return internal::debug_view_drawn.load(); },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSceneTest",
          .binding = &sliders->scene_test,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray Tracing Scene (Test)",
          .section = section,
          .tooltip = "While the Debug View is on: one part of the scene alone, to find what breaks an effect.",
          .labels = {"Everything", "Terrain Only", "Objects and Trees Only", "No Leaves", "Nothing Kept Off-Screen"},
          .is_enabled = dlaa_on,
          .is_visible = [sliders] { return internal::debug_view_drawn.load() && sliders->debug_view != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Record Ray Tracing Profile (30 s)",
          .section = section,
          .tooltip = "30 s of every frame's ray tracing costs into tw_rt_profile.csv and tw_rt_profile_helper.csv\n"
                     "(next to the game's exe). Turn the effects on, press, then move the camera over the map.",
          .is_enabled = dlaa_on,
          .on_change = [] { StartProfile(); },
      },
  });
}

// After the tone mapper (the addon's tone mapper hook, after the anti-aliasing's): the Debug View over its output.
inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  internal::debug_view_drawn = true;
  auto* native = internal::NativeContext(cmd_list);
  if (native == nullptr) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  if (ComPtr<ID3D11Texture2D> target; SUCCEEDED(resource.As(&target))) DrawDebugView(native, target.Get());
}

}  // namespace twattila::rt_scene
