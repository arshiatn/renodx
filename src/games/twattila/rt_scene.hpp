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
  float ambient_occlusion = 0.f;   // off, on, alone (the game's and GT-VBAO's not drawn; Attila)
  float ambient_range = 2.f;       // metres
  float ambient_rays = 2.f;
  float ambient_quarter = 0.f;     // 1: a quarter of the rays (faster)
  float units = 1.f;
  float grass = 0.f;
  float point_lights = 0.f;  // the point lights' shadows (games whose light draws InstallPointLights hooks)
  float light_bounce = 0.f;
  float light_bounce_rays = 4.f;
  float light_bounce_range = 10.f;
  float path_tracing = 0.f;
  float path_bounces = 2.f;
  float reflections = 0.f;
  float specular_occlusion = 0.f;
  float reflection_samples = 1.f;
};

inline Settings SettingsOf(const Sliders& sliders, const feature_settings::AaSliders& aa) {
  const bool dlaa = aa.method == 5.f;
  return {
      .sun_shadows = sliders.sun_shadows != 0.f && dlaa,
      .ambient_occlusion = sliders.ambient_occlusion != 0.f && dlaa,
      .units = sliders.units != 0.f,
      .grass = sliders.grass != 0.f,
      .ambient_range = sliders.ambient_range,
      .ambient_rays = static_cast<uint32_t>(sliders.ambient_rays),
      .ambient_quarter = sliders.ambient_quarter != 0.f,
      .sun_softness = sliders.sun_softness,
      .light_bounce = (sliders.light_bounce != 0.f || sliders.path_tracing != 0.f) && dlaa,
      .light_bounce_rays = static_cast<uint32_t>(sliders.light_bounce_rays),
      .light_bounce_range = sliders.light_bounce_range,
      .path_tracing = sliders.path_tracing != 0.f && dlaa,
      .path_bounces = static_cast<uint32_t>(sliders.path_bounces),
      .reflections = sliders.reflections != 0.f && dlaa,
      .specular_occlusion = sliders.specular_occlusion != 0.f && dlaa,
      .reflection_samples = static_cast<uint32_t>(sliders.reflection_samples),
      .local_lights = sliders.point_lights != 0.f && dlaa,
  };
}

// What the anti-aliasing's bridge makes for these settings: the ray tracing textures, the G-buffer guides (the paths read
// them) and the reflections' textures.
inline void Configure(const Settings& settings, anti_aliasing::Settings* aa) {
  aa->ray_tracing = settings.sun_shadows || settings.ambient_occlusion || settings.light_bounce || settings.reflections
                    || settings.specular_occlusion || settings.local_lights;
  aa->ray_traced_guides = settings.path_tracing || settings.reflections || settings.specular_occlusion;
  aa->ray_traced_reflections = settings.reflections || settings.specular_occlusion;
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
};

// The game's reflection pass (environment_specular_light, Attila's shaders: Attila, Thrones), before its draws: the
// ray-traced reflections; without them the screen-space march's pyramid and resolve (ssr_hiz.hpp) as before. True:
// draw.
inline bool OnReflectionDraw(reshade::api::command_list* cmd_list) {
  const bool reflected = BindReflections(cmd_list, false, true);
  {
    const std::unique_lock lock(internal::runtime.mutex);
    internal::runtime.reflections_bound = reflected;
  }
  return reflected || ssr_hiz::OnReflectionDraw(cmd_list);
}

// After its draws: the views unbound, the screen-space march's resolve when it ran.
inline void OnReflectionDrawn(reshade::api::command_list* cmd_list) {
  bool reflected = false;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    reflected = std::exchange(internal::runtime.reflections_bound, false);
  }
  if (!reflected) ssr_hiz::OnReflectionDrawn(cmd_list);
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
    ComPtr<ID3D11ShaderResourceView> depth;
    ComPtr<ID3D11Buffer> camera;
    native->PSGetShaderResources(ao_pass::internal::DEPTH_SLOT, 1, depth.GetAddressOf());
    native->PSGetConstantBuffers(ao_pass::internal::CAMERA_SLOT, 1, camera.GetAddressOf());
    internal::Trace(native, depth.Get(), camera.Get());
    if (!runtime.settings.ambient_occlusion || runtime.traced_frame != runtime.frame) return {};
    return {.post_callback = [](Context& post, const void*) {
      auto* post_native = internal::NativeContext(post.cmd_list);
      if (post_native == nullptr) return;
      auto& post_runtime = internal::runtime;
      const std::unique_lock post_lock(post_runtime.mutex);
      auto& objects = post_runtime.objects;
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
  const std::array<uint32_t, 4> offset = {draw.sun_direction, 0, 0, 0};
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
constexpr auto OnPointLightDraw = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
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
          std::array<float, 4> colour = {};
          LocalLight light = {};
          std::memcpy(colour.data(), instance + copy.layout.colour, sizeof(colour));
          std::memcpy(light.light.data(), instance + copy.layout.position, sizeof(light.light));
          light.brightness = std::max({colour[0], colour[1], colour[2]});
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
  const auto& arguments = command.arguments;
  const UINT count = stride == 0 ? 0 : std::min(arguments.instance_count, (internal::POINT_LIGHT_BYTES - used) / stride);
  if (instances && buffer && count != 0 && std::max(layout.colour, layout.position) + 16 <= stride) {
    const UINT first = offset + (arguments.first_instance * stride);
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

// The scene's shaders (`shaders`) and the game's ambient occlusion passes (none: the trace at the sun draws), the
// addon's, kept while installed (after the ambient occlusion pass's own hooks: its result is multiplied after); the
// Light Bounce's trace through rays.
inline void Install(std::span<const SceneShader> shaders, std::span<const ao_pass::AmbientOcclusionDraw> ambient_draws,
                    const Host& host = TW_HOST) {
  InstallScene(shaders, host);
  light_bounce::SetRayTracedTrace(&internal::TraceLightBounce);
  for (const auto& draw : ambient_draws) {
    renodx::utils::command_action::Register(
        OnAmbientOcclusionDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &draw);
  }
}

// The sun light draws (the addon's, kept while installed): before the screen-space shadows (their occlusion reads this
// frame's trace), the AO filter and the Light Bounce.
inline void InstallSunDraws(std::span<const light_bounce::SunDraw> sun_draws) {
  for (const auto& draw : sun_draws) {
    renodx::utils::command_action::Register(
        OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
}

inline void Uninstall() {
  light_bounce::SetRayTracedTrace(nullptr);
  renodx::utils::command_action::Unregister(OnAmbientOcclusionDraw);
  renodx::utils::command_action::Unregister(OnSunDraw);
  if (std::exchange(internal::point_lights_installed, false)) {
    renodx::utils::command_action::Unregister(OnPointLightDraw);
    reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitInputLayout);
    reshade::unregister_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyInputLayout);
  }
  UninstallScene();
}

// The settings after the anti-aliasing's (Attila's ambient occlusion also has Alone: `ambient_alone`).
// `reflection_pass`: the game's reflection pass reads the ray-traced reflections (Attila, Thrones, Warhammer,
// Warhammer II); the others get no reflection settings.
inline void AppendSettings(std::vector<renodx::utils::settings::Setting*>* settings, Sliders* sliders,
                           const feature_settings::AaSliders* aa,
                           bool ambient_alone = false, bool reflection_pass = false,
                           const std::string& section = feature_settings::AA_SECTION) {
  const auto dlaa_on = [aa] { return aa->method == 5.f; };
  const std::vector<std::string> ambient_labels = ambient_alone ? std::vector<std::string>{"Off", "On", "Alone"}
                                                                : std::vector<std::string>{"Off", "On"};
  // Quality presets: the rays and passes of the effects that are on (which effects are on stays as it is).
  using Values = std::vector<std::pair<std::string, float>>;
  const auto preset = [](Values values) {
    return [values = std::move(values)]() { renodx::utils::settings::UpdateSettings(values); };
  };
  const auto quality_button = [&](const char* label, const char* tooltip, Values values) {
    return new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = label,
        .section = section,
        .group = "rt-quality",
        .tooltip = tooltip,
        .is_enabled = dlaa_on,
        .on_change = preset(std::move(values)),
    };
  };
  settings->insert(settings->end(), {
      quality_button("Ray Tracing: Low", "Hard shadows, AO 1 ray at a quarter rate, bounce 2 rays, 1 path bounce, 1 reflection sample.",
                     {{"RayTracedShadowSoftness", 0.f}, {"RayTracedAORays", 1.f}, {"RayTracedAOQuarter", 1.f},
                      {"RayTracedLightBounceRays", 2.f}, {"PathTracingBounces", 1.f}, {"RayTracedReflectionSamples", 1.f}}),
      quality_button("Medium", "AO 2 rays at a quarter rate, bounce 4 rays, 1 path bounce, 1 reflection sample.",
                     {{"RayTracedShadowSoftness", 0.5f}, {"RayTracedAORays", 2.f}, {"RayTracedAOQuarter", 1.f},
                      {"RayTracedLightBounceRays", 4.f}, {"PathTracingBounces", 1.f}, {"RayTracedReflectionSamples", 1.f}}),
      quality_button("High", "AO 2 rays every pixel, bounce 4 rays, 2 path bounces, 1 reflection sample (the defaults).",
                     {{"RayTracedShadowSoftness", 0.5f}, {"RayTracedAORays", 2.f}, {"RayTracedAOQuarter", 0.f},
                      {"RayTracedLightBounceRays", 4.f}, {"PathTracingBounces", 2.f}, {"RayTracedReflectionSamples", 1.f}}),
      quality_button("Ultra", "AO 4 rays every pixel, bounce 8 rays, 3 path bounces, 4 reflection samples.",
                     {{"RayTracedShadowSoftness", 0.5f}, {"RayTracedAORays", 4.f}, {"RayTracedAOQuarter", 0.f},
                      {"RayTracedLightBounceRays", 8.f}, {"PathTracingBounces", 3.f}, {"RayTracedReflectionSamples", 4.f}}),
  });
  settings->insert(settings->end(), {
      new renodx::utils::settings::Setting{
          .key = "RayTracedSunShadows",
          .binding = &sliders->sun_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Sun Shadows (Experimental)",
          .section = section,
          .tooltip = "Sun shadows from rays: terrain, buildings, trees and leaves cast them far beyond the shadow map's reach.\n"
                     "Faster: Screen-Space Shadows off, the rays then alone (grass casts no contact shadows).\n"
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
          .key = "RayTracedAmbientOcclusion",
          .binding = &sliders->ambient_occlusion,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray-Traced Ambient Occlusion (Experimental)",
          .section = section,
          .tooltip = ambient_alone ? "Darker crevices and contact shade from rays. Keep the game's ambient occlusion on.\n"
                                     "On: on top of the game's or GT-VBAO. Alone: replaces them (faster; units, leaves\n"
                                     "and grass then get only what the scene casts on them).\n"
                                     "Needs DLAA, an RTX GPU and renodx-tw-dlaa64.exe."
                                   : "Darker crevices and contact shade from rays, on top of the game's ambient occlusion\n"
                                     "(keep it on). Needs DLAA, an RTX GPU and renodx-tw-dlaa64.exe.",
          .labels = ambient_labels,
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
          .key = "RayTracedUnits",
          .binding = &sliders->units,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Ray-Traced Units",
          .section = section,
          .tooltip = "Soldiers and horses cast ray-traced shadows and shade (about a million triangles a frame). Heavier.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] {
            return aa->method == 5.f && (sliders->sun_shadows != 0.f || sliders->ambient_occlusion != 0.f || sliders->light_bounce != 0.f);
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedGrass",
          .binding = &sliders->grass,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Grass",
          .section = section,
          .tooltip = "Grass casts ray-traced shadows and shade too (with the units' share of triangles). Heavy with dense grass.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] {
            return aa->method == 5.f && (sliders->sun_shadows != 0.f || sliders->ambient_occlusion != 0.f || sliders->light_bounce != 0.f);
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedPointLights",
          .binding = &sliders->point_lights,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Point Light Shadows",
          .section = section,
          .tooltip = "Torches and fires cast ray-traced shadows (the 32 nearest and brightest).",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
          .is_visible = [] { return internal::point_lights_installed; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounce",
          .binding = &sliders->light_bounce,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Light Bounce (Experimental)",
          .section = section,
          .tooltip = "The Light Bounce finds its light with rays through the scene: farther, and nothing guessed behind what\n"
                     "the camera sees. Needs the Light Bounce on, DLAA, an RTX GPU and renodx-tw-dlaa64.exe.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
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
          .is_enabled = [sliders, aa] { return aa->method == 5.f && (sliders->light_bounce != 0.f || sliders->path_tracing != 0.f); },
      },
      new renodx::utils::settings::Setting{
          .key = "PathTracing",
          .binding = &sliders->path_tracing,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path Tracing (Experimental)",
          .section = section,
          .tooltip = "The Light Bounce's light path traced: light bouncing several times, also from what is off screen.\n"
                     "Needs the Light Bounce on, DLAA, an RTX GPU and renodx-tw-dlaa64.exe. Heavy.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
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
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->path_tracing != 0.f; },
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
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->reflections != 0.f; },
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
  });
}


}  // namespace twattila::rt_scene
