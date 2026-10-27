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
#include <limits>
#include <mutex>
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
#include "./dlaa_bridge.hpp"
#include "./dxbc.hpp"
#include "./frame_trace.hpp"

// Ray tracing through the DLAA helper, the part any D3D11 game can use (rt_scene.hpp adds the Total War games' passes and
// settings; Ryse, Expeditions and Shogun 2 use this alone): DXR runs in the 64-bit helper (dlaa_bridge.h v4-v8,
// dlaa_helper/rt_d3d12.hpp), which keeps the scene and traces it. The scene is the game's own geometry as its main view
// draws it: each draw of a scene shader not yet in it (and every dynamic draw each frame, up to MAX_DYNAMIC_VERTICES:
// the helper builds them into that frame's scene only) is drawn once more into a stream-output buffer, through its
// capture version (the game's code giving the world position: the TW games' rt_capture_*.vs) or the game's own shader
// (its SV_Position, made world positions with the frame's camera), keyed by its buffers, ranges, textures and constant
// buffers (but the camera's); the helper keeps a BLAS per key and builds the frame's TLAS from what it drew. The trace
// (Trace, at a pass with the main view's depth and camera bound): the captures into the shared vertex texture
// (rt_vertices), each pixel's world position (rt_positions, with the camera's position), the request; back come the
// sun's and the ambient visibility, and on request the Light Bounce's light (path traced with bridge v8), reflections and
// specular occlusion. The sun's direction: the game's (WriteSun, or rt_sun from a lighting buffer). The game's view
// (camera, render size, guides) comes from the addon's anti-aliasing through a Host. D3D11.
namespace twattila::rt_scene {

using Microsoft::WRL::ComPtr;

// A local light a light draw had: world position, reach (metres); its colour (rgb).
struct LocalLight {
  std::array<float, 4> light = {};
  std::array<float, 4> colour = {};
};

struct Settings {
  bool sun_shadows = false;
  bool ambient_occlusion = false;
  bool units = true;  // the units in the scene (captured every frame)
  bool grass = false;  // the grass in the scene (captured every frame, alpha-tested; with the units' budget)
  float ambient_range = 2.f;  // metres
  uint32_t ambient_rays = 2;
  bool ambient_quarter = false;  // a quarter of the rays (one pixel of each 2 x 2 traced a frame, in turn)
  float sun_softness = 0.5f;  // degrees of the sun's disk (0: hard shadows)
  // The Light Bounce's light found with rays (light_bounce.hpp's trace replaced; it needs the Light Bounce on).
  bool light_bounce = false;
  uint32_t light_bounce_rays = 4;
  float light_bounce_range = 10.f;  // metres
  // Path tracing (bridge v8, dlaa_helper/rt_path.hlsli): the ray-traced Light Bounce's rays become paths going on past
  // what the screen sees (`path_bounces` surfaces past the first), shaded from a world radiance cache and the game's
  // lighting learned from the screen; the reflections' paths too.
  bool path_tracing = false;
  uint32_t path_bounces = 2;
  // Ray-traced reflections and specular occlusion (bridge v8; the game's reflection pass reads them: Reflections()).
  bool reflections = false;
  bool specular_occlusion = false;
  uint32_t reflection_samples = 1;
  // The local lights' shadows (bridge v9): of the lights the game's light draws had (AddLocalLight) the last frame's
  // nearest and brightest go with the trace; their lit bits and positions for the light draws: BindLocalLights.
  bool local_lights = false;
  // Light through leaves (bridge v11): the sun's rays pass each leaf at a share (else leaves block it).
  bool leaf_light = false;
  // Fog shadows (bridge v11): the games' fog loses the sun's light where the camera's ray runs through shadow
  // (BindFogShadows for their fog pass).
  bool fog_shadows = false;
  // Ray-traced sun shadows only (Experimental): the game's sun shadow map gets no casters (InstallShadowsOnly).
  bool shadows_only = false;
};

// A game vertex shader whose main-view draws are in the scene, and its capture version; the terrain's draws are in it
// only in the frames that draw them, dynamic ones (units) are captured every frame, foliage (leaves: their capture
// version also gives the UV) is alpha-tested against its diffuse map's alpha (the pixel shader's t0; dlaa_bridge.h
// RT_*).
// Without a capture version (`capture_shader` empty) the game's own vertex shader is streamed out (its SV_Position, made
// world positions with the frame's camera before the trace; Warhammer III); its foliage also streams out the output
// holding the UV (`uv_semantic`, `uv_semantic_index`: its xy).
struct SceneShader {
  uint32_t hash = 0;
  std::span<const uint8_t> capture_shader;
  // Instanced draws (their rows in POSITION1-3): the game's record version, one point per instance with its rows at
  // TEXCOORD0-2 (object_motion's): the draw's first instance captured, each instance placed in the helper (RT_INSTANCED).
  std::span<const uint8_t> rows_shader;
  bool terrain = false;
  bool dynamic = false;
  bool foliage = false;
  bool grass = false;  // in the scene only with Settings.grass
  bool thin = false;   // cloth and sails (RT_THIN)
  const char* uv_semantic = nullptr;
  UINT uv_semantic_index = 0;
};

// The main view's camera as ray tracing needs it: its view-projection and inverse (rows acting on column vectors; with
// the jitter, as its depth was drawn) and its position.
struct Camera {
  std::array<float, 16> view_projection = {};
  std::array<float, 16> inverse_view_projection = {};
  std::array<float, 3> position = {};
};

// The game's main view as ray tracing needs it, from the addon's anti-aliasing (the TW games' anti_aliasing.hpp,
// Warhammer III's taa_dlss.hpp, the other games' DLSS addons): the camera when `buffer` holds the main view's (the scene
// shaders' at `camera_slot`), the render size, whether the G-buffer guides were written this frame, and optionally
// whether a static draw's mesh was seen moving lately (it stays out of the scene) and whether a draw is the main view's.
// Games whose scene shaders are too many to list (Ryse) give `scene_draw` instead: every draw is asked whether it is a
// scene draw (an opaque one into the G-buffer), which fills in its shader (its vertex shader's hash: the game's own,
// streamed out; whether skinned: `skinned_vertex_shader`), and `capture_camera` reads the draw's camera from the
// context (`camera` is then not needed).
struct Host {
  bool (*camera)(ID3D11Buffer* buffer, Camera* camera) = nullptr;
  std::array<UINT, 2> (*render_size)() = nullptr;
  bool (*guides_written)() = nullptr;
  UINT camera_slot = 0;
  bool (*capture_camera)(ID3D11DeviceContext* native, Camera* camera) = nullptr;
  bool (*scene_draw)(ID3D11DeviceContext* native, SceneShader* shader) = nullptr;
  // Whether a vertex shader's code has bone inputs (null: BLENDINDICES in its input signature).
  bool (*skinned_code)(std::span<const uint8_t> code) = nullptr;
  bool (*moving)(ID3D11DeviceContext* native, const renodx::utils::command_action::DrawArguments& arguments) = nullptr;
  bool (*moving_indexed)(ID3D11DeviceContext* native, const renodx::utils::command_action::DrawIndexedArguments& arguments) = nullptr;
  // Whether a draw is the main view's (null: every draw into its depth is).
  bool (*main_view)(ID3D11DeviceContext* native) = nullptr;
};

namespace internal {

constexpr UINT MAX_VERTICES = dlaa_bridge::RT_TRANSFER_WIDTH * dlaa_bridge::RT_TRANSFER_HEIGHT;
// The units' share of it each frame (about a million triangles).
constexpr UINT MAX_DYNAMIC_VERTICES = 3u << 20;
// As the helper's: meshes unseen this long are released there, and forgotten here.
constexpr uint64_t RELEASE_FRAMES = 3600;
// The local lights a frame keeps for the next trace to pick from.
constexpr size_t MAX_LOCAL_LIGHTS = 8192;

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  std::vector<ComPtr<ID3D11VertexShader>> capture_shaders;  // as the scene shaders
  std::vector<ComPtr<ID3D11VertexShader>> rows_shaders;     // as the scene shaders (instanced ones)
  std::vector<ComPtr<ID3D11GeometryShader>> rows_outputs;
  std::vector<ComPtr<ID3D11GeometryShader>> capture_outputs;
  std::unordered_map<uint32_t, ComPtr<ID3D11GeometryShader>> game_outputs;  // the game's own, by their hash
  std::unordered_map<uint32_t, ComPtr<ID3D11GeometryShader>> game_uv_outputs;  // with the UV (their foliage)
  ComPtr<ID3D11Buffer> captured;  // stream output: world positions, three per triangle
  ComPtr<ID3D11ShaderResourceView> captured_srv;
  ComPtr<ID3D11Buffer> captured_uv;  // the game's own shaders' foliage UVs, at their vertices' places
  ComPtr<ID3D11ShaderResourceView> captured_uv_srv;
  ComPtr<ID3D11ComputeShader> vertices_shader;
  ComPtr<ID3D11ComputeShader> skies_shader;
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
  // The traced local lights' positions (RT_MAX_LIGHTS, then their count: rt_local_lights.hlsli's b12).
  ComPtr<ID3D11Buffer> light_constants;
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
  // A static draw's short key (its shader, arguments, first vertex buffer and index buffer) and the full key it stood
  // for; ambiguous once it stood for two (instances told apart by their constants): Capture's fast path.
  struct ShortKey {
    uint64_t key = 0;
    bool ambiguous = false;
  };
  std::unordered_map<uint64_t, ShortKey> short_keys;
  // The local lights the light draws had this frame and last; those this frame's trace took (their positions also in
  // objects.light_constants, b12) and whether it did.
  std::vector<LocalLight> lights_drawn;
  std::vector<LocalLight> lights_last;
  std::vector<std::array<float, 4>> lights;
  std::vector<std::array<float, 4>> light_colours;
  bool lights_traced = false;
  uint64_t traced_frame = UINT64_MAX;     // the frame the visibilities are from
  uint64_t occlusion_frame = UINT64_MAX;  // the frame `occlusion` was made in
  uint64_t sun_frame = UINT64_MAX;        // the frame the sun was written in
  uint64_t combined_frame = UINT64_MAX;   // the frame `combined` was made in
  uint64_t ambient_frame = UINT64_MAX;    // the frame an ambient occlusion pass took the traced ambient visibility
  uint64_t reflected_frame = UINT64_MAX;  // the frame the reflections were asked for
  bool reflected = false;                 // and whether they were traced
  bool reflections_bound = false;         // the reflection pass's draw has them bound (no screen-space march)
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
static Host host;
// The code of the scene shaders without a capture version, as the game made them (every vertex shader with
// host.scene_draw; runtime.mutex), and those with bone inputs (BLENDINDICES: skinned, captured every frame).
static std::unordered_map<uint32_t, std::vector<uint8_t>> game_code;
static std::unordered_map<uint32_t, bool> skinned_code;
// A scene draw's pixel shader (host.scene_draw: SceneCoverage): solid (none, or its discards dither only), alpha-tested
// against its t0 at the input `uv_semantic` (xy: an opacity mask), or left out (t0 read otherwise: a tiled or computed
// mask); by the native shader (ReShade's pipeline handle; runtime.mutex).
enum class Coverage : uint8_t {
  SOLID,
  MASKED,
  LEFT_OUT,
};
struct PixelCoverage {
  Coverage coverage = Coverage::SOLID;
  std::string uv_semantic;
  UINT uv_index = 0;
};
static std::unordered_map<uint64_t, PixelCoverage> pixel_coverage;
static bool clip_captures = false;  // the scene shaders stream out their SV_Position
// Either effect on (else every callback returns at once).
static std::atomic_bool active = false;

inline bool Enabled() {
  return runtime.settings.sun_shadows || runtime.settings.ambient_occlusion || runtime.settings.light_bounce || runtime.settings.reflections
         || runtime.settings.specular_occlusion || runtime.settings.local_lights || runtime.settings.fog_shadows;
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
  const D3D11_BUFFER_DESC captured_uv_desc = {.ByteWidth = MAX_VERTICES * 8, .Usage = D3D11_USAGE_DEFAULT,
                                              .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE};
  D3D11_SHADER_RESOURCE_VIEW_DESC captured_uv_view = {.Format = DXGI_FORMAT_R32G32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  captured_uv_view.Buffer.NumElements = MAX_VERTICES;
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 96, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC sun_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC light_constants_desc = {.ByteWidth = (dlaa_bridge::RT_MAX_LIGHTS + 1) * 16, .Usage = D3D11_USAGE_DEFAULT,
                                                  .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
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
  // The instanced draws' rows: four texels a point (rows 0-2, the record).
  const std::array<D3D11_SO_DECLARATION_ENTRY, 4> rows_output = {{
      {.SemanticName = "TEXCOORD", .SemanticIndex = 0, .ComponentCount = 4},
      {.SemanticName = "TEXCOORD", .SemanticIndex = 1, .ComponentCount = 4},
      {.SemanticName = "TEXCOORD", .SemanticIndex = 2, .ComponentCount = 4},
      {.SemanticName = "TEXCOORD", .SemanticIndex = 3, .ComponentCount = 4},
  }};
  constexpr UINT ROWS_STRIDE = 64;
  // The game's own shaders get their stream output at their first capture (game_outputs).
  bool made = std::ranges::all_of(scene_shaders, [&](const SceneShader& shader) {
    auto& vertex_shader = objects.capture_shaders.emplace_back();
    auto& stream_output = objects.capture_outputs.emplace_back();
    auto& rows_shader = objects.rows_shaders.emplace_back();
    auto& rows_output_shader = objects.rows_outputs.emplace_back();
    return (shader.capture_shader.empty()
            || (SUCCEEDED(device->CreateVertexShader(shader.capture_shader.data(), shader.capture_shader.size(), nullptr,
                                                     vertex_shader.GetAddressOf()))
                && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(shader.capture_shader.data(), shader.capture_shader.size(), &output,
                                                                          1, &STRIDE, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                                          stream_output.GetAddressOf()))))
           && (shader.rows_shader.empty()
               || (SUCCEEDED(device->CreateVertexShader(shader.rows_shader.data(), shader.rows_shader.size(), nullptr, rows_shader.GetAddressOf()))
                   && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(shader.rows_shader.data(), shader.rows_shader.size(), rows_output.data(),
                                                                             static_cast<UINT>(rows_output.size()), &ROWS_STRIDE, 1,
                                                                             D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                                             rows_output_shader.GetAddressOf()))));
  });
  made = made && SUCCEEDED(device->CreateBuffer(&captured_desc, nullptr, objects.captured.GetAddressOf()))
         && SUCCEEDED(device->CreateShaderResourceView(objects.captured.Get(), &captured_view, objects.captured_srv.GetAddressOf()))
         && (!clip_captures
             || (SUCCEEDED(device->CreateBuffer(&captured_uv_desc, nullptr, objects.captured_uv.GetAddressOf()))
                 && SUCCEEDED(device->CreateShaderResourceView(objects.captured_uv.Get(), &captured_uv_view,
                                                               objects.captured_uv_srv.GetAddressOf()))))
         && compute(__rt_vertices, &objects.vertices_shader) && compute(__rt_skies, &objects.skies_shader)
         && compute(__rt_positions, &objects.positions_shader)
         && compute(__rt_sun, &objects.sun_shader) && compute(__rt_occlusion, &objects.occlusion_shader)
         && compute(__rt_atlas, &objects.atlas_shader) && compute(__rt_ambient_combine, &objects.combine_shader)
         && SUCCEEDED(device->CreateSamplerState(&wrap_desc, objects.linear_wrap.GetAddressOf()))
         && SUCCEEDED(device->CreateBuffer(&light_constants_desc, nullptr, objects.light_constants.GetAddressOf()))
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
void Capture(Context& command, const SceneShader& shader) {
  if (frame_trace::Tracing()) {
    frame_trace::Mark(std::format("ray tracing capture: vs {:08X} ps {:08X}", shader.hash,
                                  renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::pixel_shader,
                                                                             &command.bound_state_cache)));
  }
  auto* native = NativeContext(command.cmd_list);
  if (native == nullptr) return;
  const auto& arguments = command.arguments;
  constexpr bool INDEXED = std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DrawIndexedArguments>;
  // An instanced draw of a shader with a record version: its first instance's triangles, then its instances' rows.
  const bool instanced = !shader.rows_shader.empty() && !shader.dynamic && arguments.instance_count > 1;
  const UINT instances = instanced ? arguments.instance_count : 1u;
  UINT vertex_count = 0;
  if constexpr (INDEXED) {
    vertex_count = arguments.index_count * (instanced ? 1u : std::max(arguments.instance_count, 1u));
  } else {
    vertex_count = arguments.vertex_count * (instanced ? 1u : std::max(arguments.instance_count, 1u));
  }
  const UINT row_texels = instanced ? instances * 4u : 0u;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  native->IAGetPrimitiveTopology(&topology);
  if (topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST || vertex_count == 0 || vertex_count > MAX_VERTICES) return;
  // Rigid objects seen moving stay out (a mesh's shadow where it stood).
  if constexpr (INDEXED) {
    if (!shader.terrain && !shader.dynamic && host.moving_indexed != nullptr && host.moving_indexed(native, arguments)) return;
  } else {
    if (!shader.terrain && !shader.dynamic && host.moving != nullptr && host.moving(native, arguments)) return;
  }
  // The fast path (CPU): a static mesh drawn before, known by its short key while that stood for it alone, is only
  // marked drawn (no camera, depth or full identity check). Units and leaves take the full path.
  const bool short_path = !shader.dynamic && !shader.foliage;
  uint64_t short_key = 14695981039346656037ull;
  if (short_path) {
    ID3D11Buffer* first_vertices = nullptr;
    UINT first_stride = 0;
    UINT first_offset = 0;
    ID3D11Buffer* draw_indices = nullptr;
    DXGI_FORMAT draw_index_format = DXGI_FORMAT_UNKNOWN;
    UINT draw_index_offset = 0;
    native->IAGetVertexBuffers(0, 1, &first_vertices, &first_stride, &first_offset);
    native->IAGetIndexBuffer(&draw_indices, &draw_index_format, &draw_index_offset);
    const auto mix_short = [&short_key](uint64_t value) { short_key = (short_key ^ value) * 1099511628211ull; };
    mix_short(shader.hash);
    if constexpr (INDEXED) {
      for (const uint64_t value : {uint64_t{arguments.index_count}, uint64_t{arguments.instance_count}, uint64_t{arguments.first_index},
                                   uint64_t{static_cast<uint32_t>(arguments.vertex_offset)}, uint64_t{arguments.first_instance}}) {
        mix_short(value);
      }
    } else {
      for (const uint64_t value : {uint64_t{arguments.vertex_count}, uint64_t{arguments.instance_count}, uint64_t{arguments.first_vertex},
                                   uint64_t{arguments.first_instance}}) {
        mix_short(value);
      }
    }
    mix_short(reinterpret_cast<uintptr_t>(first_vertices));
    mix_short((uint64_t{first_stride} << 32) | first_offset);
    mix_short(reinterpret_cast<uintptr_t>(draw_indices));
    mix_short((uint64_t{static_cast<uint32_t>(draw_index_format)} << 32) | draw_index_offset);
    if (first_vertices != nullptr) first_vertices->Release();
    if (draw_indices != nullptr) draw_indices->Release();
    const std::unique_lock lock(runtime.mutex);
    if (const auto entry = runtime.short_keys.find(short_key); entry != runtime.short_keys.end() && !entry->second.ambiguous) {
      if (const auto known = runtime.known.find(entry->second.key); known != runtime.known.end()) {
        if (known->second != runtime.frame) {
          known->second = runtime.frame;
          if (runtime.drawn.size() < dlaa_bridge::RT_DRAWN) runtime.drawn.push_back(entry->second.key);
        }
        return;
      }
    }
  }
  // The main view (not the UI's small 3D views, whose models stand elsewhere in the world): its camera and its depth at
  // the display's size, single-sampled; the bridge with ray tracing.
  if (host.main_view != nullptr && !host.main_view(native)) return;
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(0, nullptr, depth_view.GetAddressOf());
  Camera camera;
  if (!depth_view) return;
  if (host.capture_camera != nullptr) {
    if (!host.capture_camera(native, &camera)) return;
  } else {
    ComPtr<ID3D11Buffer> camera_buffer;
    native->VSGetConstantBuffers(host.camera_slot, 1, camera_buffer.GetAddressOf());
    if (!camera_buffer || !host.camera(camera_buffer.Get(), &camera)) return;
  }
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  if (FAILED(depth_resource.As(&depth_texture))) return;
  depth_texture->GetDesc(&depth_desc);
  if (depth_desc.SampleDesc.Count != 1 || depth_desc.Width != dlaa_bridge::bridge.width || depth_desc.Height != dlaa_bridge::bridge.height) {
    return;
  }

  // The draw's identity: its buffers, ranges, textures and constant buffers (but the camera's at b0).
  std::array<ID3D11Buffer*, 4> constant_buffers = {};
  std::array<ID3D11ShaderResourceView*, 4> textures = {};
  std::array<ID3D11Buffer*, 4> vertex_buffers = {};
  std::array<UINT, 4> strides = {};
  std::array<UINT, 4> offsets = {};
  ID3D11Buffer* index_buffer = nullptr;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  // Their ranges too (D3D11.1: one buffer can hold many draws' constants).
  std::array<UINT, 4> first_constants = {};
  std::array<UINT, 4> constant_counts = {};
  if (ComPtr<ID3D11DeviceContext1> native1; SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
    native1->VSGetConstantBuffers1(1, static_cast<UINT>(constant_buffers.size()), constant_buffers.data(), first_constants.data(),
                                   constant_counts.data());
  } else {
    native->VSGetConstantBuffers(1, static_cast<UINT>(constant_buffers.size()), constant_buffers.data());
  }
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
  for (size_t slot = 0; slot < constant_buffers.size(); ++slot) {
    mix(reinterpret_cast<uintptr_t>(constant_buffers[slot]));
    mix(first_constants[slot]);
  }
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
  if (short_path) {
    const auto [entry, added] = runtime.short_keys.try_emplace(short_key, Runtime::ShortKey{.key = key});
    if (!added && entry->second.key != key) entry->second.ambiguous = true;
  }
  // Units: every frame, within their share.
  if (shader.grass ? !runtime.settings.grass : (shader.dynamic && !runtime.settings.units)) return;
  if (shader.dynamic && runtime.dynamic_vertices + vertex_count > MAX_DYNAMIC_VERTICES) return;
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
  if (runtime.captures.size() >= dlaa_bridge::RT_CAPTURES || runtime.captured_vertices + vertex_count + row_texels > MAX_VERTICES) return;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!PrepareDevice(device.Get())) return;
  auto& objects = runtime.objects;
  // Leaves wait for a slice of the atlas (none: not in the scene this frame).
  uint32_t slice = 0;
  if (shader.foliage && (!mask || !AtlasSlice(native, mask.Get(), &slice))) return;
  // The capture version (by the shader's place in the list), else the game's own: its stream output (SV_Position; its
  // foliage's UV into the second buffer) made once from its code (null: it could not be).
  const bool clip_space = shader.capture_shader.empty();
  const bool clip_uv = clip_space && shader.foliage && shader.uv_semantic != nullptr;
  ID3D11VertexShader* capture_vertex_shader = nullptr;
  ID3D11GeometryShader* stream_output = nullptr;
  if (clip_space) {
    const auto [made, first] = (clip_uv ? objects.game_uv_outputs : objects.game_outputs).try_emplace(shader.hash);
    if (first) {
      const auto code = game_code.find(shader.hash);
      const std::array<D3D11_SO_DECLARATION_ENTRY, 2> entries = {{
          {.SemanticName = "SV_POSITION", .SemanticIndex = 0, .ComponentCount = 4},
          {.SemanticName = shader.uv_semantic, .SemanticIndex = shader.uv_semantic_index, .ComponentCount = 2, .OutputSlot = 1},
      }};
      constexpr std::array<UINT, 2> STRIDES = {16, 8};
      const UINT outputs = clip_uv ? 2 : 1;
      if (code == game_code.end()
          || FAILED(device->CreateGeometryShaderWithStreamOutput(code->second.data(), code->second.size(), entries.data(), outputs,
                                                                  STRIDES.data(), outputs, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                                  made->second.GetAddressOf()))) {
        renodx::utils::log::w(std::format("[Total War] Ray tracing: no stream output for vertex shader {:08X}{}.", shader.hash,
                                          clip_uv ? " with its UV" : ""));
      }
    }
    if (!made->second) return;
    stream_output = made->second.Get();
  } else {
    const auto index = static_cast<size_t>(&shader - scene_shaders.data());
    if (index >= objects.capture_shaders.size()) return;
    capture_vertex_shader = objects.capture_shaders[index].Get();
    stream_output = objects.capture_outputs[index].Get();
  }

  // The same draw into the stream output only (not rasterized); the game's shaders and outputs put back.
  ComPtr<ID3D11VertexShader> game_vertex_shader;
  ComPtr<ID3D11GeometryShader> game_geometry_shader;
  ComPtr<ID3D11PixelShader> game_pixel_shader;
  ComPtr<ID3D11Buffer> game_output;
  native->VSGetShader(game_vertex_shader.GetAddressOf(), nullptr, nullptr);
  native->GSGetShader(game_geometry_shader.GetAddressOf(), nullptr, nullptr);
  native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
  native->SOGetTargets(1, game_output.GetAddressOf());
  const std::array<ID3D11Buffer*, 2> targets = {objects.captured.Get(), objects.captured_uv.Get()};
  const std::array<UINT, 2> stream_offsets = {runtime.captured_vertices * 16, runtime.captured_vertices * 8};
  if (!clip_space) native->VSSetShader(capture_vertex_shader, nullptr, 0);
  native->GSSetShader(stream_output, nullptr, 0);
  native->PSSetShader(nullptr, nullptr, 0);
  native->SOSetTargets(clip_uv ? 2 : 1, targets.data(), stream_offsets.data());
  if constexpr (INDEXED) {
    native->DrawIndexedInstanced(arguments.index_count, instanced ? 1u : std::max(arguments.instance_count, 1u), arguments.first_index,
                                 arguments.vertex_offset, arguments.first_instance);
  } else {
    native->DrawInstanced(arguments.vertex_count, instanced ? 1u : std::max(arguments.instance_count, 1u), arguments.first_vertex,
                          arguments.first_instance);
  }
  if (instanced) {
    // One point per instance (its first vertex) with its rows, after the triangles.
    const auto index = static_cast<size_t>(&shader - scene_shaders.data());
    ID3D11Buffer* rows_target = objects.captured.Get();
    const UINT rows_offset = (runtime.captured_vertices + vertex_count) * 16;
    native->VSSetShader(objects.rows_shaders[index].Get(), nullptr, 0);
    native->GSSetShader(objects.rows_outputs[index].Get(), nullptr, 0);
    native->SOSetTargets(1, &rows_target, &rows_offset);
    if constexpr (INDEXED) {
      native->DrawIndexedInstanced(1, instances, arguments.first_index, arguments.vertex_offset, arguments.first_instance);
    } else {
      native->DrawInstanced(1, instances, arguments.first_vertex, arguments.first_instance);
    }
  }
  ID3D11Buffer* output = game_output.Get();
  constexpr UINT APPEND = UINT_MAX;
  native->SOSetTargets(1, &output, &APPEND);
  native->VSSetShader(game_vertex_shader.Get(), nullptr, 0);
  native->GSSetShader(game_geometry_shader.Get(), nullptr, 0);
  native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
  runtime.captures.push_back({.key = key, .first_vertex = runtime.captured_vertices, .vertex_count = vertex_count,
                              .flags = (shader.terrain ? dlaa_bridge::RT_TERRAIN : 0u) | (shader.dynamic ? dlaa_bridge::RT_DYNAMIC : 0u)
                                       | (shader.foliage ? dlaa_bridge::RT_FOLIAGE : 0u) | (instanced ? dlaa_bridge::RT_INSTANCED : 0u)
                                       | (shader.thin ? dlaa_bridge::RT_THIN : 0u),
                              .instances = instanced ? instances : 0u});
  runtime.slices.push_back(slice);
  runtime.captured_vertices += vertex_count + row_texels;
  if (shader.dynamic) {
    runtime.dynamic_vertices += vertex_count;
  }
}

// A listed scene shader's draw (its SceneShader the callback's data).
template <typename Context>
void CaptureDraw(Context& command, const void*) {
  Capture(command, command.template GetCallbackData<SceneShader>());
}

// Any draw when the host tells scene draws apart (host.scene_draw).
template <typename Context>
void CaptureAnyDraw(Context& command, const void*) {
  auto* native = NativeContext(command.cmd_list);
  SceneShader shader = {.hash = renodx::utils::state::GetCurrentShaderHash(command.cmd_list, reshade::api::pipeline_stage::vertex_shader,
                                                                           &command.bound_state_cache)};
  if (native != nullptr && shader.hash != 0u && host.scene_draw(native, &shader)) Capture(command, shader);
}

// This frame's trace from the main view's `depth` and `camera_buffer` (the ambient occlusion pass's, or the first sun
// draw's): the captures, the positions, the request; false: none.
// `depth` null: the bridge's positions (and the camera in the sun texture's texel 1) were written by the addon's own pass
// (Ryse's, from its linear depth).
inline bool Trace(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth, const Camera& camera) {
  auto& objects = runtime.objects;
  if (!Enabled() || runtime.traced_frame == runtime.frame || !dlaa_bridge::bridge.ray_tracing || !objects.constants) return false;
  if (depth != nullptr) {
    D3D11_SHADER_RESOURCE_VIEW_DESC depth_desc = {};
    depth->GetDesc(&depth_desc);
    if (depth_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) return false;
  }
  const auto render_size = host.render_size();
  if (render_size[0] == 0 || render_size[0] > dlaa_bridge::bridge.width || render_size[1] > dlaa_bridge::bridge.height) return false;
  {
    const ComputeState kept(native);
    if (!runtime.captures.empty()) {
      // Clip-space captures become world positions with the camera they were drawn with (this frame's).
      struct VertexConstants {
        std::array<uint32_t, 4> counts;
        std::array<float, 16> inverse_view_projection;
      };
      const VertexConstants constants = {
          .counts = {runtime.captured_vertices, dlaa_bridge::RT_TRANSFER_WIDTH, clip_captures ? 1u : 0u, 0},
          .inverse_view_projection = camera.inverse_view_projection,
      };
      Dispatch(native, objects.vertices_shader.Get(), {objects.captured_srv.Get(), objects.captured_uv_srv.Get()},
               {dlaa_bridge::bridge.rt_vertices.uav.Get(), nullptr},
               &constants, sizeof(constants), (runtime.captured_vertices + 255) / 256, 1);
      // Skies (rt_skies): the new static captures that hold the camera and are larger than the view reaches (the farther
      // of its near and far planes' centres; unbounded with an infinite far plane) go empty.
      const auto& inverse = camera.inverse_view_projection;
      float reach = 0.f;
      for (const float depth : {0.f, 1.f}) {
        const float w = (inverse[14] * depth) + inverse[15];
        if (std::abs(w) < 1e-12f) {
          reach = std::numeric_limits<float>::max();
          break;
        }
        const float x = (((inverse[2] * depth) + inverse[3]) / w) - camera.position[0];
        const float y = (((inverse[6] * depth) + inverse[7]) / w) - camera.position[1];
        const float z = (((inverse[10] * depth) + inverse[11]) / w) - camera.position[2];
        reach = std::max(reach, std::sqrt((x * x) + (y * y) + (z * z)));
      }
      struct SkyConstants {
        std::array<uint32_t, 4> capture;
        std::array<float, 16> inverse_view_projection;
        std::array<float, 4> camera;
      };
      for (const auto& capture : runtime.captures) {
        if ((capture.flags & (dlaa_bridge::RT_TERRAIN | dlaa_bridge::RT_DYNAMIC | dlaa_bridge::RT_FOLIAGE | dlaa_bridge::RT_INSTANCED)) != 0) continue;
        const SkyConstants sky = {
            .capture = {capture.first_vertex, capture.vertex_count, clip_captures ? 1u : 0u, dlaa_bridge::RT_TRANSFER_WIDTH},
            .inverse_view_projection = camera.inverse_view_projection,
            .camera = {camera.position[0], camera.position[1], camera.position[2], reach},
        };
        Dispatch(native, objects.skies_shader.Get(), {objects.captured_srv.Get(), nullptr}, {dlaa_bridge::bridge.rt_vertices.uav.Get(), nullptr},
                 &sky, sizeof(sky), 1, 1);
      }
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
    if (depth != nullptr) {
      Dispatch(native, objects.positions_shader.Get(), {depth, nullptr},
               {dlaa_bridge::bridge.rt_positions.uav.Get(), dlaa_bridge::bridge.rt_sun.uav.Get()}, &constants, sizeof(constants),
               (render_size[0] + 7) / 8, (render_size[1] + 7) / 8);
    }
  }
  // The local lights: last frame's nearest and brightest (their reach over their distance, squared, by their colour's
  // brightest channel).
  runtime.lights.clear();
  runtime.light_colours.clear();
  if (runtime.settings.local_lights && !runtime.lights_last.empty()) {
    const auto score = [&](const LocalLight& light) {
      const float x = light.light[0] - camera.position[0];
      const float y = light.light[1] - camera.position[1];
      const float z = light.light[2] - camera.position[2];
      return std::max({light.colour[0], light.colour[1], light.colour[2]}) * light.light[3] * light.light[3]
             / std::max((x * x) + (y * y) + (z * z), 1.f);
    };
    auto& candidates = runtime.lights_last;
    const size_t count = std::min<size_t>(candidates.size(), dlaa_bridge::RT_MAX_LIGHTS);
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<ptrdiff_t>(count), candidates.end(),
                      [&](const LocalLight& a, const LocalLight& b) { return score(a) > score(b); });
    std::array<std::array<float, 4>, dlaa_bridge::RT_MAX_LIGHTS + 1> constants = {};
    for (size_t index = 0; index < count; ++index) {
      runtime.lights.push_back(candidates[index].light);
      runtime.light_colours.push_back(candidates[index].colour);
      constants[index] = candidates[index].light;
    }
    constants.back()[0] = std::bit_cast<float>(static_cast<uint32_t>(count));
    native->UpdateSubresource(objects.light_constants.Get(), 0, nullptr, constants.data(), 0, 0);
  }
  const bool lights = !runtime.lights.empty();
  const dlaa_bridge::TraceRequest request = {
      .flags = (runtime.settings.sun_shadows ? dlaa_bridge::RT_SUN_SHADOWS : 0u)
               | (runtime.settings.ambient_occlusion ? dlaa_bridge::RT_AMBIENT_OCCLUSION : 0u)
               | (runtime.settings.ambient_quarter ? dlaa_bridge::RT_AMBIENT_QUARTER : 0u)
               | (lights ? dlaa_bridge::RT_LOCAL_LIGHTS : 0u)
               | (runtime.settings.leaf_light ? dlaa_bridge::RT_LEAF_LIGHT : 0u)
               | (runtime.settings.fog_shadows ? dlaa_bridge::RT_FOG_SHADOWS : 0u),
      .ambient_range = runtime.settings.ambient_range,
      .ambient_rays = runtime.settings.ambient_rays,
      .sun_softness = runtime.settings.sun_softness,
      .frame = static_cast<uint32_t>(runtime.frame),
      .width = render_size[0],
      .height = render_size[1],
      .captures = runtime.captures,
      .slices = runtime.slices,
      .drawn = runtime.drawn,
      .lights = runtime.lights,
      .light_colours = runtime.light_colours,
      .view_projection = camera.view_projection,
  };
  const bool traced = dlaa_bridge::TraceScene(native, request);
  runtime.lights_traced = traced && lights;
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

// The same with the camera in `camera_buffer` (host.camera); false: not the main view's.
inline bool Trace(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth, ID3D11Buffer* camera_buffer) {
  Camera camera;
  return depth != nullptr && camera_buffer != nullptr && host.camera(camera_buffer, &camera) && Trace(native, depth, camera);
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
      .path_flags = runtime.settings.path_tracing ? (dlaa_bridge::RT_PATH_TRACING | dlaa_bridge::RT_PATH_REUSE) : 0u,
      .path_bounces = runtime.settings.path_bounces,
      .path_samples = runtime.settings.light_bounce_rays,
      .guides = host.guides_written(),
  };
  const bool bounced = dlaa_bridge::TraceBounce(native, request);
  if (bounced != runtime.logged_bounce) {
    runtime.logged_bounce = bounced;
    renodx::utils::log::i("[Total War] Ray-traced Light Bounce: ", (bounced ? (request.path_flags != 0 ? "path tracing" : "tracing")
                                                                            : "not traced (the screen-space one)"),
                          " (", width, " x ", height, ", ", request.rays, " rays).");
  }
  if (!bounced) return false;
  native->CopySubresourceRegion(traced, 0, 0, 0, 0, bridge.rt_bounce.texture.Get(), 0, &region);
  return true;
}

// A pixel shader's coverage: whether it discards, and where it samples t0.
inline PixelCoverage ClassifyCoverage(std::span<const uint8_t> code) {
  constexpr uint32_t OP_DISCARD = 13, OP_SAMPLE = 69, OP_SAMPLE_B = 74;
  constexpr uint32_t OPERAND_INPUT = 1, OPERAND_RESOURCE = 7;
  const dxbc::Program program = dxbc::Read(code);
  const auto& tokens = program.tokens;
  if (tokens.empty()) return {.coverage = Coverage::LEFT_OUT};
  // An operand's tokens at `at` (0: relative indexing, which these operands do not use, or malformed).
  const auto operand_length = [&](size_t at) -> size_t {
    if (at >= tokens.size()) return 0;
    const uint32_t token = tokens[at];
    size_t length = 1 + (token >> 31);
    if (((token >> 12) & 0xFF) == 4) return length + (((token & 3) == 2) ? 4 : 1);  // immediate32
    for (uint32_t dimension = 0; dimension < ((token >> 20) & 3); ++dimension) {
      if (((token >> (22 + (3 * dimension))) & 7) != 0) return 0;
      ++length;
    }
    return length;
  };
  bool discards = false;
  bool sampled_elsewhere = false;
  uint32_t uv_register = UINT32_MAX;
  for (size_t at = 2; at < tokens.size();) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {.coverage = Coverage::LEFT_OUT};
    discards = discards || opcode == OP_DISCARD;
    if (opcode >= OP_SAMPLE && opcode <= OP_SAMPLE_B) {
      // Past the extended opcode tokens: the destination, the address, the resource.
      size_t operand = at + 1;
      for (bool extended = (tokens[at] >> 31) != 0; extended && operand < at + length; ++operand) extended = (tokens[operand] >> 31) != 0;
      const size_t address = operand + operand_length(operand);
      const size_t resource = address + operand_length(address);
      if (address > operand && resource > address && resource + 1 < at + length && ((tokens[resource] >> 12) & 0xFF) == OPERAND_RESOURCE
          && tokens[resource + 1 + (tokens[resource] >> 31)] == 0) {
        const bool input = ((tokens[address] >> 12) & 0xFF) == OPERAND_INPUT;
        if (input && uv_register == UINT32_MAX) {
          uv_register = tokens[address + 1 + (tokens[address] >> 31)];
        } else if (!input) {
          sampled_elsewhere = true;
        }
      }
    }
    at += length;
  }
  if (!discards || (uv_register == UINT32_MAX && !sampled_elsewhere)) return {};
  dxbc::SignatureElement uv;
  if (sampled_elsewhere || uv_register == UINT32_MAX
      || !dxbc::FindSignatureElement(code, program, "ISGN", [&](const dxbc::SignatureElement&, uint32_t found) { return found == uv_register; },
                                     &uv)) {
    return {.coverage = Coverage::LEFT_OUT};
  }
  return {.coverage = Coverage::MASKED, .uv_semantic = std::string(uv.name), .uv_index = uv.index};
}

// A pipeline going away: its pixel shader's coverage with it (its handle may come back).
inline void OnDestroyPipeline(reshade::api::device*, reshade::api::pipeline pipeline) {
  const std::unique_lock lock(runtime.mutex);
  pixel_coverage.erase(pipeline.handle);
}

// A new pipeline: the code of a scene shader without a capture version kept (its stream output is made from it).
inline void OnInitPipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                           const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline pipeline) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const auto* code = static_cast<const uint8_t*>(desc->code);
    if (subobjects[index].type == reshade::api::pipeline_subobject_type::pixel_shader && host.scene_draw != nullptr) {
      PixelCoverage coverage = ClassifyCoverage({code, desc->code_size});
      const std::unique_lock lock(runtime.mutex);
      pixel_coverage[pipeline.handle] = std::move(coverage);
      continue;
    }
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::vertex_shader) continue;
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(code, desc->code_size);
    if (host.scene_draw == nullptr
        && std::ranges::none_of(scene_shaders, [hash](const SceneShader& shader) { return shader.hash == hash && shader.capture_shader.empty(); })) {
      continue;
    }
    // Bone inputs: the host's test, else the input signature's semantic names (plain text in the code).
    constexpr std::string_view BONES = "BLENDINDICES";
    const std::string_view bytes(reinterpret_cast<const char*>(code), desc->code_size);
    const bool skinned = (host.skinned_code != nullptr) ? host.skinned_code({code, desc->code_size}) : bytes.find(BONES) != std::string_view::npos;
    const std::unique_lock lock(runtime.mutex);
    game_code[hash].assign(code, code + desc->code_size);
    skinned_code[hash] = skinned;
  }
}

}  // namespace internal

// The reflections and specular occlusion (the reflection texture: reflected light, the share of samples that met a
// surface; the occlusion: 1 open), traced once a frame at the game's reflection pass (after this frame's trace: its
// G-buffer guides and positions); both null when off or not traced this frame (the pass keeps the game's then).
inline std::array<ID3D11ShaderResourceView*, 2> Reflections(ID3D11DeviceContext* native) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& bridge = dlaa_bridge::bridge;
  if (!(runtime.settings.reflections || runtime.settings.specular_occlusion) || runtime.traced_frame != runtime.frame
      || !bridge.rt_reflection.texture) {
    return {};
  }
  if (runtime.reflected_frame != runtime.frame) {
    runtime.reflected_frame = runtime.frame;
    const dlaa_bridge::ReflectRequest request = {
        .frame = static_cast<uint32_t>(runtime.frame),
        .width = runtime.traced_size[0],
        .height = runtime.traced_size[1],
        .flags = (runtime.settings.reflections ? dlaa_bridge::RT_REFLECTIONS : 0u)
                 | (runtime.settings.specular_occlusion ? dlaa_bridge::RT_SPECULAR_OCCLUSION : 0u)
                 | (dlaa_bridge::bridge.reconstructing ? dlaa_bridge::RT_REFLECTIONS_RAW : 0u),
        .samples = runtime.settings.reflection_samples,
        .bounces = runtime.settings.path_tracing ? runtime.settings.path_bounces : 0u,
        .range = runtime.settings.light_bounce_range,
        .reflection_range = 10.f,
        .view_projection = runtime.traced_view_projection,
        .guides = internal::host.guides_written(),
    };
    const bool reflected = dlaa_bridge::TraceReflections(native, request);
    if (reflected != runtime.reflected) {
      renodx::utils::log::i("[Total War] Ray-traced reflections: ", (reflected ? "tracing" : "not traced (the game's)"), ".");
    }
    runtime.reflected = reflected;
  }
  if (!runtime.reflected) return {};
  return {bridge.rt_reflection.srv.Get(), bridge.rt_specular.srv.Get()};
}

// Binds the ray-traced reflections (t47, with the reflections on) and the specular occlusion (t46) for the game's
// reflection pass's draw or dispatch (`compute`), or unbinds them (`bind` false). True: the reflections are bound (the
// pass leaves out its screen-space march; rt_reflection.hlsli).
inline bool BindReflections(reshade::api::command_list* cmd_list, bool compute, bool bind) {
  ID3D11DeviceContext* native = internal::NativeContext(cmd_list);
  if (native == nullptr) return false;
  const auto views = (bind && internal::active.load(std::memory_order_relaxed)) ? Reflections(native)
                                                                                 : std::array<ID3D11ShaderResourceView*, 2>{};
  bool reflected = false;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    reflected = views[0] != nullptr && internal::runtime.settings.reflections;
  }
  const std::array<ID3D11ShaderResourceView*, 2> bound = {views[1], reflected ? views[0] : nullptr};
  if (compute) {
    native->CSSetShaderResources(46, static_cast<UINT>(bound.size()), bound.data());
  } else {
    native->PSSetShaderResources(46, static_cast<UINT>(bound.size()), bound.data());
  }
  return reflected;
}

// The game's screen-space reflection march and its blurs (Warhammer, Warhammer II: environment_specular_light raycast,
// filter_blur): left out while the ray-traced reflections replace it in the resolve. True: dispatch.
inline bool OnReflectionMarch(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::NativeContext(cmd_list);
  if (native == nullptr || !internal::active.load(std::memory_order_relaxed)) return true;
  const auto views = Reflections(native);
  const std::unique_lock lock(internal::runtime.mutex);
  return views[0] == nullptr || !internal::runtime.settings.reflections;
}

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
  std::swap(runtime.lights_last, runtime.lights_drawn);
  runtime.lights_drawn.clear();
  if (std::erase_if(runtime.known, [&](const auto& entry) { return entry.second + internal::RELEASE_FRAMES < runtime.frame; }) != 0) {
    std::erase_if(runtime.short_keys, [&](const auto& entry) { return !runtime.known.contains(entry.second.key); });
  }
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

// This frame's trace from the main view's `depth` (null: the addon wrote the bridge's positions) and `camera` at a pass
// of the game's (the addons' own hooks); false: not traced (off, already traced this frame).
inline bool Trace(ID3D11DeviceContext* native, ID3D11ShaderResourceView* depth, const Camera& camera) {
  const std::unique_lock lock(internal::runtime.mutex);
  if (!internal::active.load(std::memory_order_relaxed)) return false;
  internal::Trace(native, depth, camera);
  return internal::runtime.traced_frame == internal::runtime.frame;
}

// The sun's direction for the next trace (toward the sun, world space, unit length), where the game's lighting buffers
// do not hold it in the TW games' layout (rt_sun).
inline void WriteSun(ID3D11DeviceContext* native, const std::array<float, 3>& toward_sun) {
  auto& bridge = dlaa_bridge::bridge;
  if (!bridge.rt_sun.texture) return;
  const std::array<float, 4> sun = {toward_sun[0], toward_sun[1], toward_sun[2], 1.f};
  const D3D11_BOX texel = {.left = 0, .top = 0, .front = 0, .right = 1, .bottom = 1, .back = 1};
  native->UpdateSubresource(bridge.rt_sun.texture.Get(), 0, &texel, sun.data(), 16, 0);
}

// This frame's traced sun and ambient visibility (R8 at the render size, the bridge's textures' top-left; 1 open); null
// when not traced this frame.
inline std::array<ID3D11ShaderResourceView*, 2> Visibilities() {
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.traced_frame != internal::runtime.frame) return {};
  return {dlaa_bridge::bridge.rt_visibility.srv.Get(), dlaa_bridge::bridge.rt_ambient.srv.Get()};
}

// A local light a light draw had (one drawn twice counts once): the next frame's trace picks the nearest and brightest of
// this frame's.
inline void AddLocalLight(const LocalLight& light) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.settings.local_lights || !(light.light[3] > 0.f) || runtime.lights_drawn.size() >= internal::MAX_LOCAL_LIGHTS) return;
  if (std::ranges::any_of(runtime.lights_drawn, [&](const LocalLight& drawn) { return drawn.light == light.light; })) return;
  runtime.lights_drawn.push_back(light);
}

// Many at once (an instanced light draw's: each once).
inline void AddLocalLights(std::span<const LocalLight> lights) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (!runtime.settings.local_lights) return;
  for (const LocalLight& light : lights) {
    if (runtime.lights_drawn.size() >= internal::MAX_LOCAL_LIGHTS) break;
    if (light.light[3] > 0.f) runtime.lights_drawn.push_back(light);
  }
}

// For a light draw (its pixel shader with rt_local_lights.hlsli): this frame's lit bits at t42 and the traced lights'
// positions at b12; false: none traced this frame (nothing bound).
inline bool BindLocalLights(ID3D11DeviceContext* native) {
  ID3D11ShaderResourceView* mask = nullptr;
  ID3D11Buffer* constants = nullptr;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    if (internal::runtime.traced_frame != internal::runtime.frame || !internal::runtime.lights_traced) return false;
    mask = dlaa_bridge::bridge.rt_light_mask.srv.Get();
    constants = internal::runtime.objects.light_constants.Get();
  }
  native->PSSetShaderResources(42, 1, &mask);
  native->PSSetConstantBuffers(12, 1, &constants);
  return true;
}

// For a fog draw (its pixel shader with rt_fog.hlsli): this frame's fog shadows at t43; false: not traced this frame
// (nothing bound).
inline bool BindFogShadows(ID3D11DeviceContext* native) {
  ID3D11ShaderResourceView* fog = nullptr;
  {
    const std::unique_lock lock(internal::runtime.mutex);
    if (internal::runtime.traced_frame != internal::runtime.frame || !internal::runtime.settings.fog_shadows) return false;
    fog = dlaa_bridge::bridge.rt_fog.srv.Get();
  }
  native->PSSetShaderResources(43, 1, &fog);
  return fog != nullptr;
}

inline void UnbindFogShadows(ID3D11DeviceContext* native) {
  ID3D11ShaderResourceView* no_fog = nullptr;
  native->PSSetShaderResources(43, 1, &no_fog);
}

inline void UnbindLocalLights(ID3D11DeviceContext* native) {
  ID3D11ShaderResourceView* no_mask = nullptr;
  ID3D11Buffer* no_constants = nullptr;
  native->PSSetShaderResources(42, 1, &no_mask);
  native->PSSetConstantBuffers(12, 1, &no_constants);
}

// A scene draw's pixel shader (the one bound; host.scene_draw's): its alpha test into `shader` (foliage: the UV's
// input); false: left out of the scene (a mask read some other way).
inline bool SceneCoverage(ID3D11DeviceContext* native, SceneShader* shader) {
  ComPtr<ID3D11PixelShader> pixel_shader;
  native->PSGetShader(pixel_shader.GetAddressOf(), nullptr, nullptr);
  if (!pixel_shader) return true;
  const std::unique_lock lock(internal::runtime.mutex);
  const auto found = internal::pixel_coverage.find(reinterpret_cast<uint64_t>(pixel_shader.Get()));
  if (found == internal::pixel_coverage.end() || found->second.coverage == internal::Coverage::LEFT_OUT) return false;
  if (found->second.coverage == internal::Coverage::MASKED) {
    shader->foliage = true;
    shader->uv_semantic = found->second.uv_semantic.c_str();
    shader->uv_semantic_index = found->second.uv_index;
  }
  return true;
}

// Whether a game vertex shader (its hash) has bone inputs (host.scene_draw's skinned draws); false: unknown.
inline bool SkinnedVertexShader(uint32_t hash) {
  const std::unique_lock lock(internal::runtime.mutex);
  const auto found = internal::skinned_code.find(hash);
  return found != internal::skinned_code.end() && found->second;
}

// Any draw (host.scene_draw tells the scene's apart).
constexpr auto OnAnyDraw = []<typename T, typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (!internal::active.load(std::memory_order_relaxed)) return {};
    return {.post_callback = internal::CaptureAnyDraw<Context>};
  }
};

// The scene's shaders (`shaders`, the addon's, kept while installed; or every draw host.scene_draw accepts) and the
// game's view (`host`).
inline void InstallScene(std::span<const SceneShader> shaders, const Host& host) {
  internal::scene_shaders = shaders;
  internal::host = host;
  internal::clip_captures = host.scene_draw != nullptr
                            || std::ranges::any_of(shaders, [](const SceneShader& shader) { return shader.capture_shader.empty(); });
  if (internal::clip_captures) reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  if (host.scene_draw != nullptr) reshade::register_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  for (const SceneShader& shader : shaders) {
    renodx::utils::command_action::Register(
        OnSceneDraw, {.shader_hash = shader.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &shader);
  }
  if (host.scene_draw != nullptr) {
    renodx::utils::command_action::Register(OnAnyDraw, {.shader_hash = 0u, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void UninstallScene() {
  if (internal::clip_captures) reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  if (internal::host.scene_draw != nullptr) reshade::unregister_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  renodx::utils::command_action::Unregister(OnSceneDraw);
  renodx::utils::command_action::Unregister(OnAnyDraw);
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
  internal::runtime.short_keys.clear();
}

}  // namespace twattila::rt_scene
