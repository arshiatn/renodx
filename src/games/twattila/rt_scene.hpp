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
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/state.hpp"
#include "./anti_aliasing.hpp"
#include "./ao_pass.hpp"
#include "./dlaa_bridge.hpp"
#include "./frame_trace.hpp"
#include "./feature_settings.hpp"
#include "./object_motion.hpp"
#include "./light_bounce.hpp"

// Ray-traced sun shadows and ambient occlusion (Experimental; Attila): DXR runs only in 64-bit processes, so the scene
// goes to the DLAA helper (dlaa_bridge.h v4, dlaa_helper/rt_d3d12.hpp) and comes back as two visibilities. The scene
// is the game's own geometry as its main view draws it: each terrain, rigid object (but those DLAA's motion saw
// moving), tree trunk and leaf draw not yet in it (and every unit draw each frame, up to MAX_DYNAMIC_VERTICES; the
// helper builds them into that frame's scene only) is drawn once more with its capture shader (rt_capture_*.vs, made by
// tools\tw\attila_motion\make_wind_motion_vs.py: the game's code giving the world position) into a stream-output
// buffer, keyed by its buffers, ranges, textures and constant buffers (but the camera's); the helper keeps a BLAS per key
// and builds the frame's TLAS from what it drew. At the game's ambient occlusion pass (its depth and camera bound): the
// captures into the shared vertex texture (rt_vertices), each pixel's world position (rt_positions, with the camera's
// position), the request; its ambient visibility then multiplies the pass's result (rt_ambient_apply, red only), and the
// sun's visibility goes to the patched sun shaders' occlusion (t49; screen_space_shadows.hpp asks: rt_occlusion, the
// darker with its own). The sun's direction: the frame's first sun light draw's lighting buffer (rt_sun), for the next
// frame's trace. Games without an ambient occlusion pass of the addon's (Warhammer: their own line sweeps) trace at the
// frame's first sun light draw instead (its G-buffer depth and camera), and its ambient visibility multiplies the AO the
// sun draws read (rt_ambient_combine, bound in its place). Needs DLAA / DLSS (the bridge) and the game's ambient
// occlusion on. D3D11.
namespace twattila::rt_scene {

using Microsoft::WRL::ComPtr;

struct Settings {
  bool sun_shadows = false;
  bool ambient_occlusion = false;
  bool units = true;  // the units in the scene (captured every frame)
  float ambient_range = 2.f;  // metres
  uint32_t ambient_rays = 2;
  float sun_softness = 0.5f;  // degrees of the sun's disk (0: hard shadows)
  // The Light Bounce's light found with rays (light_bounce.hpp's trace replaced; it needs the Light Bounce on).
  bool light_bounce = false;
  uint32_t light_bounce_rays = 4;
  float light_bounce_range = 10.f;  // metres
};

// A game vertex shader whose main-view draws are in the scene, and its capture version; the terrain's draws are in it
// only in the frames that draw them, dynamic ones (units) are captured every frame, foliage (leaves: their capture
// version also gives the UV) is alpha-tested against its diffuse map's alpha (the pixel shader's t0; dlaa_bridge.h
// RT_*).
struct SceneShader {
  uint32_t hash = 0;
  std::span<const uint8_t> capture_shader;
  bool terrain = false;
  bool dynamic = false;
  bool foliage = false;
};

// The settings as the addon's sliders hold them (in the anti-aliasing section: with DLAA / DLSS).
struct Sliders {
  float sun_shadows = 0.f;
  float sun_softness = 0.5f;       // degrees of the sun's disk (0: hard)
  float ambient_occlusion = 0.f;   // off, on, alone (the game's and GT-VBAO's not drawn; Attila)
  float ambient_range = 2.f;       // metres
  float ambient_rays = 2.f;
  float units = 1.f;
  float light_bounce = 0.f;
  float light_bounce_rays = 4.f;
  float light_bounce_range = 10.f;
};

inline Settings SettingsOf(const Sliders& sliders, const feature_settings::AaSliders& aa) {
  const bool dlaa = aa.method == 5.f;
  return {
      .sun_shadows = sliders.sun_shadows != 0.f && dlaa,
      .ambient_occlusion = sliders.ambient_occlusion != 0.f && dlaa,
      .units = sliders.units != 0.f,
      .ambient_range = sliders.ambient_range,
      .ambient_rays = static_cast<uint32_t>(sliders.ambient_rays),
      .sun_softness = sliders.sun_softness,
      .light_bounce = sliders.light_bounce != 0.f && dlaa,
      .light_bounce_rays = static_cast<uint32_t>(sliders.light_bounce_rays),
      .light_bounce_range = sliders.light_bounce_range,
  };
}

namespace internal {

constexpr UINT MAX_VERTICES = dlaa_bridge::RT_TRANSFER_WIDTH * dlaa_bridge::RT_TRANSFER_HEIGHT;
// The units' share of it each frame (about a million triangles).
constexpr UINT MAX_DYNAMIC_VERTICES = 3u << 20;
// As the helper's: meshes unseen this long are released there, and forgotten here.
constexpr uint64_t RELEASE_FRAMES = 3600;

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  std::vector<ComPtr<ID3D11VertexShader>> capture_shaders;  // as the scene shaders
  std::vector<ComPtr<ID3D11GeometryShader>> capture_outputs;
  ComPtr<ID3D11Buffer> captured;  // stream output: world positions, three per triangle
  ComPtr<ID3D11ShaderResourceView> captured_srv;
  ComPtr<ID3D11ComputeShader> vertices_shader;
  ComPtr<ID3D11ComputeShader> positions_shader;
  ComPtr<ID3D11ComputeShader> sun_shader;
  ComPtr<ID3D11ComputeShader> occlusion_shader;
  ComPtr<ID3D11ComputeShader> atlas_shader;
  ComPtr<ID3D11SamplerState> linear_wrap;
  ComPtr<ID3D11VertexShader> fullscreen_shader;
  ComPtr<ID3D11PixelShader> ambient_shader;
  ComPtr<ID3D11Buffer> constants;  // the compute's and the apply's, 96 bytes
  ComPtr<ID3D11Buffer> sun_constants;  // rt_sun's b1: where the draw's sun_direction starts
  ComPtr<ID3D11BlendState> multiply;
  ComPtr<ID3D11SamplerState> linear_clamp;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ComPtr<ID3D11DepthStencilState> no_depth;
  // The sun's occlusion for the patched sun shaders (the display's size).
  ComPtr<ID3D11Texture2D> occlusion;
  ComPtr<ID3D11ShaderResourceView> occlusion_srv;
  ComPtr<ID3D11UnorderedAccessView> occlusion_uav;
  // Tracing at the sun draws: their AO times the traced ambient visibility (the display's size, RGBA16F).
  ComPtr<ID3D11ComputeShader> combine_shader;
  ComPtr<ID3D11Texture2D> combined;
  ComPtr<ID3D11ShaderResourceView> combined_srv;
  ComPtr<ID3D11UnorderedAccessView> combined_uav;
};

struct Runtime {
  std::mutex mutex;
  Settings settings;
  uint64_t frame = 0;
  // This frame's new captures and the scene meshes it drew; the keys in the scene (the frame they were last drawn).
  std::vector<dlaa_bridge::RtCapture> captures;
  std::vector<uint32_t> slices;  // each capture's opacity mask's atlas slice (leaves)
  uint32_t captured_vertices = 0;
  uint32_t dynamic_vertices = 0;
  std::vector<uint64_t> drawn;
  std::unordered_map<uint64_t, uint64_t> known;
  uint64_t traced_frame = UINT64_MAX;     // the frame the visibilities are from
  uint64_t occlusion_frame = UINT64_MAX;  // the frame `occlusion` was made in
  uint64_t sun_frame = UINT64_MAX;        // the frame the sun was written in
  uint64_t combined_frame = UINT64_MAX;   // the frame `combined` was made in
  uint64_t ambient_frame = UINT64_MAX;    // the frame an ambient occlusion pass took the traced ambient visibility
  ID3D11ShaderResourceView* game_ao = nullptr;  // the AO it was made from (compared only)
  std::array<UINT, 2> traced_size = {};   // the render size traced
  std::array<float, 16> traced_view_projection = {};  // its camera's (the ray-traced Light Bounce's)
  bool logged_bounce = false;  // the ray-traced Light Bounce's last state, as logged
  // The atlas's textures (kept alive while they have a slice; for the bridge's atlas `atlas_generation`), its free
  // slices and the frame each was last used.
  std::unordered_map<ID3D11Resource*, std::pair<ComPtr<ID3D11Resource>, uint32_t>> atlas_slices;
  std::vector<uint32_t> free_slices;
  std::array<uint64_t, dlaa_bridge::RT_ATLAS_SLICES> slice_used = {};
  uint32_t atlas_generation = 0;
  DeviceObjects objects;
};

static Runtime runtime;
// The addon's (Install), kept while installed.
static std::span<const SceneShader> scene_shaders;
// Either effect on (else every callback returns at once).
static std::atomic_bool active = false;

inline bool Enabled() {
  return runtime.settings.sun_shadows || runtime.settings.ambient_occlusion || runtime.settings.light_bounce;
}

inline ID3D11DeviceContext* NativeContext(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  return (native != nullptr && native->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) ? native : nullptr;
}

// The device's shaders and buffers (once); false: not available.
inline bool PrepareDevice(ID3D11Device* device) {
  auto& objects = runtime.objects;
  if (objects.device.Get() == device) return static_cast<bool>(objects.constants);
  objects = {};
  objects.device = device;
  const D3D11_SO_DECLARATION_ENTRY output = {.SemanticName = "TEXCOORD", .SemanticIndex = 0, .ComponentCount = 4};
  constexpr UINT STRIDE = 16;
  const auto compute = [&](std::span<const uint8_t> code, ComPtr<ID3D11ComputeShader>* shader) {
    return SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
  };
  const D3D11_BUFFER_DESC captured_desc = {.ByteWidth = MAX_VERTICES * 16, .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE};
  D3D11_SHADER_RESOURCE_VIEW_DESC captured_view = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  captured_view.Buffer.NumElements = MAX_VERTICES;
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 96, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC sun_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  D3D11_BLEND_DESC multiply_desc = {};
  multiply_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ZERO, .DestBlend = D3D11_BLEND_SRC_COLOR,
                                   .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ZERO,
                                   .DestBlendAlpha = D3D11_BLEND_ONE, .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                                   .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED};
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxLOD = D3D11_FLOAT32_MAX};
  const D3D11_SAMPLER_DESC wrap_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                                        .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
                                        .MaxLOD = D3D11_FLOAT32_MAX};
  const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE};
  const D3D11_DEPTH_STENCIL_DESC depth_desc = {.DepthEnable = FALSE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
                                               .DepthFunc = D3D11_COMPARISON_ALWAYS};
  bool made = std::ranges::all_of(scene_shaders, [&](const SceneShader& shader) {
    return SUCCEEDED(device->CreateVertexShader(shader.capture_shader.data(), shader.capture_shader.size(), nullptr,
                                                objects.capture_shaders.emplace_back().GetAddressOf()))
           && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(shader.capture_shader.data(), shader.capture_shader.size(), &output,
                                                                     1, &STRIDE, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                                     objects.capture_outputs.emplace_back().GetAddressOf()));
  });
  made = made && SUCCEEDED(device->CreateBuffer(&captured_desc, nullptr, objects.captured.GetAddressOf()))
         && SUCCEEDED(device->CreateShaderResourceView(objects.captured.Get(), &captured_view, objects.captured_srv.GetAddressOf()))
         && compute(__rt_vertices, &objects.vertices_shader) && compute(__rt_positions, &objects.positions_shader)
         && compute(__rt_sun, &objects.sun_shader) && compute(__rt_occlusion, &objects.occlusion_shader)
         && compute(__rt_atlas, &objects.atlas_shader) && compute(__rt_ambient_combine, &objects.combine_shader)
         && SUCCEEDED(device->CreateSamplerState(&wrap_desc, objects.linear_wrap.GetAddressOf()))
         && SUCCEEDED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr,
                                                 objects.fullscreen_shader.GetAddressOf()))
         && SUCCEEDED(device->CreatePixelShader(__rt_ambient_apply.data(), __rt_ambient_apply.size(), nullptr,
                                                objects.ambient_shader.GetAddressOf()))
         && SUCCEEDED(device->CreateBlendState(&multiply_desc, objects.multiply.GetAddressOf()))
         && SUCCEEDED(device->CreateSamplerState(&sampler_desc, objects.linear_clamp.GetAddressOf()))
         && SUCCEEDED(device->CreateRasterizerState(&rasterizer_desc, objects.rasterizer.GetAddressOf()))
         && SUCCEEDED(device->CreateDepthStencilState(&depth_desc, objects.no_depth.GetAddressOf()))
         && SUCCEEDED(device->CreateBuffer(&constants_desc, nullptr, objects.constants.GetAddressOf()))
         && SUCCEEDED(device->CreateBuffer(&sun_desc, nullptr, objects.sun_constants.GetAddressOf()));
  if (!made) {
    renodx::utils::log::w("[Total War] Ray tracing: could not create its shaders or buffers.");
    objects = {};
    objects.device = device;
  }
  return made;
}

// The compute stage's bindings this module uses (t0-t1, u0-u1, b0), kept and put back.
struct ComputeState {
  ComPtr<ID3D11ComputeShader> shader;
  std::array<ID3D11ShaderResourceView*, 2> views = {};
  std::array<ID3D11UnorderedAccessView*, 2> uavs = {};
  ComPtr<ID3D11Buffer> constants;

  explicit ComputeState(ID3D11DeviceContext* native) : native_(native) {
    native->CSGetShader(shader.GetAddressOf(), nullptr, nullptr);
    native->CSGetShaderResources(0, 2, views.data());
    native->CSGetUnorderedAccessViews(0, 2, uavs.data());
    native->CSGetConstantBuffers(0, 1, constants.GetAddressOf());
  }
  ~ComputeState() {
    constexpr std::array<UINT, 2> KEEP_COUNTS = {UINT_MAX, UINT_MAX};
    native_->CSSetShader(shader.Get(), nullptr, 0);
    native_->CSSetUnorderedAccessViews(0, 2, uavs.data(), KEEP_COUNTS.data());
    native_->CSSetShaderResources(0, 2, views.data());
    native_->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
    for (auto* view : views) {
      if (view != nullptr) view->Release();
    }
    for (auto* uav : uavs) {
      if (uav != nullptr) uav->Release();
    }
  }
  ComputeState(const ComputeState&) = delete;
  ComputeState& operator=(const ComputeState&) = delete;

 private:
  ID3D11DeviceContext* native_;
};

// One dispatch with its views and constants (`constants`: up to 96 bytes).
inline void Dispatch(ID3D11DeviceContext* native, ID3D11ComputeShader* shader, std::array<ID3D11ShaderResourceView*, 2> views,
                     std::array<ID3D11UnorderedAccessView*, 2> uavs, const void* constants, UINT constant_bytes, UINT groups_x,
                     UINT groups_y) {
  constexpr std::array<ID3D11ShaderResourceView*, 2> NO_VIEWS = {};
  constexpr std::array<UINT, 2> KEEP_COUNTS = {UINT_MAX, UINT_MAX};
  std::array<uint8_t, 96> padded = {};
  std::memcpy(padded.data(), constants, std::min<UINT>(constant_bytes, 96));
  native->UpdateSubresource(runtime.objects.constants.Get(), 0, nullptr, padded.data(), 0, 0);
  native->CSSetShader(shader, nullptr, 0);
  native->CSSetShaderResources(0, 2, NO_VIEWS.data());
  native->CSSetUnorderedAccessViews(0, 2, uavs.data(), KEEP_COUNTS.data());
  native->CSSetShaderResources(0, 2, views.data());
  native->CSSetConstantBuffers(0, 1, runtime.objects.constants.GetAddressOf());
  native->Dispatch(groups_x, groups_y, 1);
}

// The atlas slice holding the opacity mask `texture` (copied into a free slice when new; the least recently used one
// unused for RELEASE_FRAMES reused); false: none free, or no atlas.
inline bool AtlasSlice(ID3D11DeviceContext* native, ID3D11ShaderResourceView* texture, uint32_t* slice) {
  auto& objects = runtime.objects;
  auto& atlas = dlaa_bridge::bridge.rt_atlas;
  if (!atlas.uav) return false;
  // A new atlas (another device): its slices are empty.
  if (runtime.atlas_generation != dlaa_bridge::bridge.rt_atlas_generation) {
    runtime.atlas_slices.clear();
    runtime.free_slices.clear();
    for (uint32_t free = dlaa_bridge::RT_ATLAS_SLICES; free > 0; --free) runtime.free_slices.push_back(free - 1);
    runtime.atlas_generation = dlaa_bridge::bridge.rt_atlas_generation;
  }
  ComPtr<ID3D11Resource> resource;
  texture->GetResource(resource.GetAddressOf());
  if (const auto found = runtime.atlas_slices.find(resource.Get()); found != runtime.atlas_slices.end()) {
    *slice = found->second.second;
    runtime.slice_used[*slice] = runtime.frame;
    return true;
  }
  if (runtime.free_slices.empty()) {
    const auto oldest = std::ranges::min_element(runtime.atlas_slices, {}, [](const auto& entry) { return runtime.slice_used[entry.second.second]; });
    if (oldest == runtime.atlas_slices.end() || runtime.slice_used[oldest->second.second] + RELEASE_FRAMES >= runtime.frame) return false;
    runtime.free_slices.push_back(oldest->second.second);
    runtime.atlas_slices.erase(oldest);
  }
  *slice = runtime.free_slices.back();
  runtime.free_slices.pop_back();
  runtime.atlas_slices.emplace(resource.Get(), std::pair{resource, *slice});
  runtime.slice_used[*slice] = runtime.frame;
  float mip = 0.f;
  if (ComPtr<ID3D11Texture2D> texture2d; SUCCEEDED(resource.As(&texture2d))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture2d->GetDesc(&desc);
    mip = std::max(std::log2(static_cast<float>(std::max(desc.Width, desc.Height)) / dlaa_bridge::RT_ATLAS_SIZE), 0.f);
  }
  const ComputeState kept(native);
  ComPtr<ID3D11SamplerState> game_sampler;
  native->CSGetSamplers(0, 1, game_sampler.GetAddressOf());
  native->CSSetSamplers(0, 1, objects.linear_wrap.GetAddressOf());
  const std::array<uint32_t, 4> copy = {*slice, std::bit_cast<uint32_t>(mip), dlaa_bridge::RT_ATLAS_SIZE, 0};
  Dispatch(native, objects.atlas_shader.Get(), {texture, nullptr}, {atlas.uav.Get(), nullptr}, copy.data(), sizeof(copy),
           dlaa_bridge::RT_ATLAS_SIZE / 8, dlaa_bridge::RT_ATLAS_SIZE / 8);
  native->CSSetSamplers(0, 1, game_sampler.GetAddressOf());
  return true;
}

// After a scene draw of the main view: a draw not yet in the scene is drawn once more into the stream-output buffer.
template <typename Context>
void CaptureDraw(Context& command, const void*) {
  const auto& shader = command.template GetCallbackData<SceneShader>();
  if (frame_trace::Tracing()) {
    frame_trace::Mark(std::format("ray tracing capture: vs {:08X} ps {:08X}", shader.hash,
                                  renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader,
                                                                             &command.bound_state_cache)));
  }
  auto* native = NativeContext(command.cmd_list);
  if (native == nullptr) return;
  const auto& arguments = command.arguments;
  constexpr bool INDEXED = std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DrawIndexedArguments>;
  UINT vertex_count = 0;
  if constexpr (INDEXED) {
    vertex_count = arguments.index_count * std::max(arguments.instance_count, 1u);
  } else {
    vertex_count = arguments.vertex_count * std::max(arguments.instance_count, 1u);
  }
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  native->IAGetPrimitiveTopology(&topology);
  if (topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST || vertex_count == 0 || vertex_count > MAX_VERTICES) return;
  // The main view: its camera and its depth at the display's size, single-sampled; the bridge with ray tracing.
  ComPtr<ID3D11Buffer> camera_buffer;
  native->VSGetConstantBuffers(0, 1, camera_buffer.GetAddressOf());
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(0, nullptr, depth_view.GetAddressOf());
  anti_aliasing::internal::Camera camera;
  if (!camera_buffer || !depth_view || !anti_aliasing::MainCamera(camera_buffer.Get(), &camera)) return;
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  if (FAILED(depth_resource.As(&depth_texture))) return;
  depth_texture->GetDesc(&depth_desc);
  if (depth_desc.SampleDesc.Count != 1 || depth_desc.Width != dlaa_bridge::bridge.width || depth_desc.Height != dlaa_bridge::bridge.height) {
    return;
  }
  // Rigid objects seen moving stay out (a mesh's shadow where it stood).
  if (!shader.terrain && !shader.dynamic && object_motion::MovingMesh(object_motion::MeshKey(native, arguments))) return;

  // The draw's identity: its buffers, ranges, textures and constant buffers (but the camera's at b0).
  std::array<ID3D11Buffer*, 4> constant_buffers = {};
  std::array<ID3D11ShaderResourceView*, 4> textures = {};
  std::array<ID3D11Buffer*, 4> vertex_buffers = {};
  std::array<UINT, 4> strides = {};
  std::array<UINT, 4> offsets = {};
  ID3D11Buffer* index_buffer = nullptr;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  native->VSGetConstantBuffers(1, static_cast<UINT>(constant_buffers.size()), constant_buffers.data());
  native->VSGetShaderResources(0, static_cast<UINT>(textures.size()), textures.data());
  native->IAGetVertexBuffers(0, static_cast<UINT>(vertex_buffers.size()), vertex_buffers.data(), strides.data(), offsets.data());
  native->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
  uint64_t key = 14695981039346656037ull;
  const auto mix = [&key](uint64_t value) { key = (key ^ value) * 1099511628211ull; };
  mix(shader.hash);
  if constexpr (INDEXED) {
    for (const uint64_t value : {uint64_t{arguments.index_count}, uint64_t{arguments.instance_count}, uint64_t{arguments.first_index},
                                 uint64_t{static_cast<uint32_t>(arguments.vertex_offset)}, uint64_t{arguments.first_instance}}) {
      mix(value);
    }
  } else {
    for (const uint64_t value : {uint64_t{arguments.vertex_count}, uint64_t{arguments.instance_count}, uint64_t{arguments.first_vertex},
                                 uint64_t{arguments.first_instance}}) {
      mix(value);
    }
  }
  for (auto* buffer : constant_buffers) mix(reinterpret_cast<uintptr_t>(buffer));
  for (auto* texture : textures) mix(reinterpret_cast<uintptr_t>(texture));
  for (size_t slot = 0; slot < vertex_buffers.size(); ++slot) {
    mix(reinterpret_cast<uintptr_t>(vertex_buffers[slot]));
    mix((uint64_t{strides[slot]} << 32) | offsets[slot]);
  }
  mix(reinterpret_cast<uintptr_t>(index_buffer));
  mix((uint64_t{static_cast<uint32_t>(index_format)} << 32) | index_offset);
  // The Get calls added references; only compared.
  const auto release = [](auto* object) {
    if (object != nullptr) object->Release();
  };
  std::ranges::for_each(constant_buffers, release);
  std::ranges::for_each(textures, release);
  std::ranges::for_each(vertex_buffers, release);
  release(index_buffer);

  // A leaf's opacity mask: its pixel shader's diffuse map.
  ComPtr<ID3D11ShaderResourceView> mask;
  if (shader.foliage) native->PSGetShaderResources(0, 1, mask.GetAddressOf());

  const std::unique_lock lock(runtime.mutex);
  if (!Enabled() || !dlaa_bridge::bridge.ray_tracing) return;
  // Units: every frame, within their share.
  if (shader.dynamic && (!runtime.settings.units || runtime.dynamic_vertices + vertex_count > MAX_DYNAMIC_VERTICES)) return;
  if (const auto known = runtime.known.find(key); !shader.dynamic && known != runtime.known.end()) {
    if (known->second != runtime.frame) {
      known->second = runtime.frame;
      if (runtime.drawn.size() < dlaa_bridge::RT_DRAWN) runtime.drawn.push_back(key);
    }
    // A leaf's slice stays in use while it is drawn.
    if (mask) {
      ComPtr<ID3D11Resource> mask_resource;
      mask->GetResource(mask_resource.GetAddressOf());
      if (const auto found = runtime.atlas_slices.find(mask_resource.Get()); found != runtime.atlas_slices.end()) {
        runtime.slice_used[found->second.second] = runtime.frame;
      }
    }
    return;
  }
  if (std::ranges::find(runtime.captures, key, &dlaa_bridge::RtCapture::key) != runtime.captures.end()) return;
  if (runtime.captures.size() >= dlaa_bridge::RT_CAPTURES || runtime.captured_vertices + vertex_count > MAX_VERTICES) return;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!PrepareDevice(device.Get())) return;
  auto& objects = runtime.objects;
  const auto index = static_cast<size_t>(&shader - scene_shaders.data());
  if (index >= objects.capture_shaders.size()) return;
  // Leaves wait for a slice of the atlas (none: not in the scene this frame).
  uint32_t slice = 0;
  if (shader.foliage && (!mask || !AtlasSlice(native, mask.Get(), &slice))) return;

  // The same draw into the stream output only (not rasterized); the game's shaders and outputs put back.
  ComPtr<ID3D11VertexShader> game_vertex_shader;
  ComPtr<ID3D11GeometryShader> game_geometry_shader;
  ComPtr<ID3D11PixelShader> game_pixel_shader;
  ComPtr<ID3D11Buffer> game_output;
  native->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
  native->GSGetShader(game_geometry_shader.GetAddressOf(), nullptr, nullptr);
  native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
  native->SOGetTargets(1, game_output.GetAddressOf());
  ID3D11Buffer* target = objects.captured.Get();
  const UINT offset = runtime.captured_vertices * 16;
  native->VSSetShader(objects.capture_shaders[index].Get(), nullptr, 0);
  native->GSSetShader(objects.capture_outputs[index].Get(), nullptr, 0);
  native->PSSetShader(nullptr, nullptr, 0);
  native->SOSetTargets(1, &target, &offset);
  if constexpr (INDEXED) {
    native->DrawIndexedInstanced(arguments.index_count, std::max(arguments.instance_count, 1u), arguments.first_index,
                                 arguments.vertex_offset, arguments.first_instance);
  } else {
    native->DrawInstanced(arguments.vertex_count, std::max(arguments.instance_count, 1u), arguments.first_vertex, arguments.first_instance);
  }
  ID3D11Buffer* output = game_output.Get();
  constexpr UINT APPEND = UINT_MAX;
  native->SOSetTargets(1, &output, &APPEND);
  native->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
  native->GSSetShader(game_geometry_shader.Get(), nullptr, 0);
  native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
  runtime.captures.push_back({.key = key, .first_vertex = runtime.captured_vertices, .vertex_count = vertex_count,
                              .flags = (shader.terrain ? dlaa_bridge::RT_TERRAIN : 0u) | (shader.dynamic ? dlaa_bridge::RT_DYNAMIC : 0u)
                                       | (shader.foliage ? dlaa_bridge::RT_FOLIAGE : 0u)});
  runtime.slices.push_back(slice);
  runtime.captured_vertices += vertex_count;
  if (shader.dynamic) {
    runtime.dynamic_vertices += vertex_count;
  }
}

// This frame's trace from the main view's `depth` and `camera_buffer` (the ambient occlusion pass's, or the first sun
// draw's): the captures, the positions, the request; false: none.
inline bool Trace(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth, ID3D11Buffer* camera_buffer) {
  auto& objects = runtime.objects;
  if (!Enabled() || runtime.traced_frame == runtime.frame || !dlaa_bridge::bridge.ray_tracing || !objects.constants) return false;
  anti_aliasing::internal::Camera camera;
  if (depth == nullptr || camera_buffer == nullptr || !anti_aliasing::MainCamera(camera_buffer, &camera)) return false;
  D3D11_SHADER_RESOURCE_VIEW_DESC depth_desc = {};
  depth->GetDesc(&depth_desc);
  if (depth_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) return false;
  const auto render_size = anti_aliasing::RenderSize();
  if (render_size[0] == 0 || render_size[0] > dlaa_bridge::bridge.width || render_size[1] > dlaa_bridge::bridge.height) return false;
  {
    const ComputeState kept(native);
    if (!runtime.captures.empty()) {
      const std::array<uint32_t, 4> counts = {runtime.captured_vertices, dlaa_bridge::RT_TRANSFER_WIDTH, 0, 0};
      Dispatch(native, objects.vertices_shader.Get(), {objects.captured_srv.Get(), nullptr}, {dlaa_bridge::bridge.rt_vertices.uav.Get(), nullptr},
               counts.data(), sizeof(counts), (runtime.captured_vertices + 255) / 256, 1);
    }
    struct PositionConstants {
      std::array<float, 16> inverse_view_projection;
      std::array<float, 4> camera;
      std::array<uint32_t, 4> size;
    };
    const PositionConstants constants = {
        .inverse_view_projection = camera.inverse_view_projection,
        .camera = {camera.position[0], camera.position[1], camera.position[2], 1.f},
        .size = {render_size[0], render_size[1], 0, 0},
    };
    Dispatch(native, objects.positions_shader.Get(), {depth, nullptr},
             {dlaa_bridge::bridge.rt_positions.uav.Get(), dlaa_bridge::bridge.rt_sun.uav.Get()}, &constants, sizeof(constants),
             (render_size[0] + 7) / 8, (render_size[1] + 7) / 8);
  }
  const dlaa_bridge::TraceRequest request = {
      .flags = (runtime.settings.sun_shadows ? dlaa_bridge::RT_SUN_SHADOWS : 0u)
               | (runtime.settings.ambient_occlusion ? dlaa_bridge::RT_AMBIENT_OCCLUSION : 0u),
      .ambient_range = runtime.settings.ambient_range,
      .ambient_rays = runtime.settings.ambient_rays,
      .sun_softness = runtime.settings.sun_softness,
      .frame = static_cast<uint32_t>(runtime.frame),
      .width = render_size[0],
      .height = render_size[1],
      .captures = runtime.captures,
      .slices = runtime.slices,
      .drawn = runtime.drawn,
  };
  const bool traced = dlaa_bridge::TraceScene(native, request);
  if (traced) {
    for (const auto& capture : runtime.captures) {
      if ((capture.flags & dlaa_bridge::RT_DYNAMIC) == 0) {
        runtime.known[capture.key] = runtime.frame;
      }
    }
    runtime.traced_frame = runtime.frame;
    runtime.traced_size = render_size;
    runtime.traced_view_projection = camera.view_projection;
  }
  runtime.captures.clear();
  runtime.slices.clear();
  runtime.captured_vertices = 0;
  runtime.drawn.clear();
  return traced;
}

// The Light Bounce's trace through rays (light_bounce.hpp SetRayTracedTrace): its prepared light (its first level's
// top-left `width` x `height`, a texel each `scale` render pixels) to the helper, the bounce into `traced`; false: not
// this frame (the screen-space trace then).
inline bool TraceLightBounce(ID3D11DeviceContext* native, ID3D11Texture2D* prepared_light, UINT width, UINT height, UINT scale,
                             ID3D11Texture2D* traced) {
  const std::unique_lock lock(runtime.mutex);
  auto& bridge = dlaa_bridge::bridge;
  if (!runtime.settings.light_bounce || runtime.traced_frame != runtime.frame || !bridge.ray_tracing || !bridge.rt_light.texture
      || width > bridge.width || height > bridge.height) {
    return false;
  }
  const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = width, .bottom = height, .back = 1};
  native->CopySubresourceRegion(bridge.rt_light.texture.Get(), 0, 0, 0, 0, prepared_light, 0, &region);
  const dlaa_bridge::BounceRequest request = {
      .frame = static_cast<uint32_t>(runtime.frame),
      .width = runtime.traced_size[0],
      .height = runtime.traced_size[1],
      .bounce_width = width,
      .bounce_height = height,
      .scale = scale,
      .rays = runtime.settings.light_bounce_rays,
      .range = runtime.settings.light_bounce_range,
      .view_projection = runtime.traced_view_projection,
  };
  const bool bounced = dlaa_bridge::TraceBounce(native, request);
  if (bounced != runtime.logged_bounce) {
    runtime.logged_bounce = bounced;
    renodx::utils::log::i("[Total War] Ray-traced Light Bounce: ", (bounced ? "tracing" : "not traced (the screen-space one)"), " (",
                          width, " x ", height, ", ", request.rays, " rays).");
  }
  if (!bounced) return false;
  native->CopySubresourceRegion(traced, 0, 0, 0, 0, bridge.rt_bounce.texture.Get(), 0, &region);
  return true;
}

}  // namespace internal

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

constexpr auto OnSceneDraw = []<typename T, typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (!internal::active.load(std::memory_order_relaxed)) return {};
    return {.post_callback = internal::CaptureDraw<Context>};
  }
};

// The sun light's occlusion for the patched sun shaders this frame (the screen-space shadows' `screen_space` mask, or
// null without it, and the traced sun's, the darker); null: no traced sun this frame (the screen-space mask alone).
inline ID3D11ShaderResourceView* Occlusion(ID3D11DeviceContext* native, ID3D11ShaderResourceView* screen_space) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& objects = runtime.objects;
  if (!runtime.settings.sun_shadows || runtime.traced_frame != runtime.frame) return nullptr;
  if (runtime.occlusion_frame == runtime.frame) return objects.occlusion_srv.Get();
  D3D11_TEXTURE2D_DESC current = {};
  if (objects.occlusion) objects.occlusion->GetDesc(&current);
  if (current.Width != dlaa_bridge::bridge.width || current.Height != dlaa_bridge::bridge.height) {
    objects.occlusion.Reset();
    objects.occlusion_srv.Reset();
    objects.occlusion_uav.Reset();
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = dlaa_bridge::bridge.width,
        .Height = dlaa_bridge::bridge.height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R8_UNORM,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
    };
    if (FAILED(objects.device->CreateTexture2D(&desc, nullptr, objects.occlusion.GetAddressOf()))
        || FAILED(objects.device->CreateShaderResourceView(objects.occlusion.Get(), nullptr, objects.occlusion_srv.GetAddressOf()))
        || FAILED(objects.device->CreateUnorderedAccessView(objects.occlusion.Get(), nullptr, objects.occlusion_uav.GetAddressOf()))) {
      objects.occlusion.Reset();
      objects.occlusion_srv.Reset();
      objects.occlusion_uav.Reset();
      return nullptr;
    }
  }
  // Not bound for the pixel shaders while written.
  ID3D11ShaderResourceView* none = nullptr;
  native->PSSetShaderResources(49, 1, &none);
  {
    const internal::ComputeState kept(native);
    const std::array<uint32_t, 4> size = {runtime.traced_size[0], runtime.traced_size[1], 0, 0};
    internal::Dispatch(native, objects.occlusion_shader.Get(), {dlaa_bridge::bridge.rt_visibility.srv.Get(), screen_space},
                       {objects.occlusion_uav.Get(), nullptr}, size.data(), sizeof(size), (dlaa_bridge::bridge.width + 7) / 8,
                       (dlaa_bridge::bridge.height + 7) / 8);
  }
  runtime.occlusion_frame = runtime.frame;
  return objects.occlusion_srv.Get();
}

// Every present: the settings and a new frame; scene keys unseen for RELEASE_FRAMES forgotten (the helper released them).
inline void Update(const Settings& settings) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  runtime.settings = settings;
  internal::active.store(internal::Enabled(), std::memory_order_relaxed);
  ++runtime.frame;
  runtime.captures.clear();
  runtime.slices.clear();
  runtime.captured_vertices = 0;
  runtime.dynamic_vertices = 0;
  runtime.drawn.clear();
  std::erase_if(runtime.known, [&](const auto& entry) { return entry.second + internal::RELEASE_FRAMES < runtime.frame; });
}

// Whether ray tracing wants the bridge's textures (the anti-aliasing's DLAA makes them).
inline bool Wanted() {
  const std::unique_lock lock(internal::runtime.mutex);
  return internal::Enabled();
}

// The helper's ray tracing status (one line).
inline std::string Status() {
  return dlaa_bridge::TraceStatus();
}

// The scene's shaders (`shaders`) and the game's ambient occlusion passes (none: the trace at the sun draws), the
// addon's, kept while installed (after the ambient occlusion pass's own hooks: its result is multiplied after).
inline void Install(std::span<const SceneShader> shaders, std::span<const ao_pass::AmbientOcclusionDraw> ambient_draws) {
  internal::scene_shaders = shaders;
  light_bounce::SetRayTracedTrace(&internal::TraceLightBounce);
  for (const SceneShader& shader : shaders) {
    renodx::utils::command_action::Register(
        OnSceneDraw, {.shader_hash = shader.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &shader);
  }
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
  renodx::utils::command_action::Unregister(OnSceneDraw);
  renodx::utils::command_action::Unregister(OnAmbientOcclusionDraw);
  renodx::utils::command_action::Unregister(OnSunDraw);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::scene_shaders = {};
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.objects.device.Get() == native) internal::runtime.objects = {};
  internal::runtime.known.clear();
}

// The settings after the anti-aliasing's (Attila's ambient occlusion also has Alone: `ambient_alone`).
inline void AppendSettings(std::vector<renodx::utils::settings::Setting*>* settings, Sliders* sliders,
                           const feature_settings::AaSliders* aa,
                           bool ambient_alone = false) {
  const auto dlaa_on = [aa] { return aa->method == 5.f; };
  const std::vector<std::string> ambient_labels = ambient_alone ? std::vector<std::string>{"Off", "On", "Alone"}
                                                                : std::vector<std::string>{"Off", "On"};
  settings->insert(settings->end(), {
      new renodx::utils::settings::Setting{
          .key = "RayTracedSunShadows",
          .binding = &sliders->sun_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Sun Shadows (Experimental)",
          .section = feature_settings::AA_SECTION,
          .tooltip = "Sun shadows from rays: terrain, buildings, trees and leaves cast them far beyond the shadow map's reach.\n"
                     "Needs DLAA, an RTX GPU and renodx-tw-dlaa64.exe.",
          .labels = {"Off", "On"},
          .is_enabled = dlaa_on,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedShadowSoftness",
          .binding = &sliders->sun_softness,
          .default_value = 0.5f,
          .label = "Ray-Traced Shadow Softness",
          .section = feature_settings::AA_SECTION,
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
          .section = feature_settings::AA_SECTION,
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
          .section = feature_settings::AA_SECTION,
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
          .section = feature_settings::AA_SECTION,
          .tooltip = "More rays: less noise. Heavier.",
          .min = 1.f,
          .max = 8.f,
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedUnits",
          .binding = &sliders->units,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Ray-Traced Units",
          .section = feature_settings::AA_SECTION,
          .tooltip = "Soldiers and horses cast ray-traced shadows and shade (about a million triangles a frame). Heavier.",
          .labels = {"Off", "On"},
          .is_enabled = [sliders, aa] {
            return aa->method == 5.f && (sliders->sun_shadows != 0.f || sliders->ambient_occlusion != 0.f || sliders->light_bounce != 0.f);
          },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounce",
          .binding = &sliders->light_bounce,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Light Bounce (Experimental)",
          .section = feature_settings::AA_SECTION,
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
          .section = feature_settings::AA_SECTION,
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
          .section = feature_settings::AA_SECTION,
          .tooltip = "How far its rays look for light.",
          .min = 1.f,
          .max = 50.f,
          .format = "%.0f m",
          .is_enabled = [sliders, aa] { return aa->method == 5.f && sliders->light_bounce != 0.f; },
      },
  });
}


}  // namespace twattila::rt_scene
