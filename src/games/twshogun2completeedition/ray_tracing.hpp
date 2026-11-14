/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <span>
#include <type_traits>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../expeditionsrome_dlss/constant_mirror.hpp"
#include "../talesofarise_dlss/matrix.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/rt_core.hpp"
#include "../twpharaohdynasties/rt_outside_view_leaf_gs.h"
#include "../twpharaohdynasties/rt_outside_view_world_gs.h"
#include "../twwarhammer3/rt_outside_view_gs.h"
#include "./upscaling.hpp"

// Ray tracing (Experimental): Shogun 2's scene through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe
// next to the addon, its own D3D12 device with DXR). The scene: every G-buffer draw (rt0-2 RGBA16F, depth written,
// unblended; leaves alpha-tested against their diffuse alpha: rt_core's SceneCoverage), streamed out through the game's
// own vertex shaders (those with bone inputs every frame). The camera: camera_VS_PS (view_projection c10-c13 as
// uploaded, jittered as the scene was drawn; the position c0; the render size g_viewport_dimensions c38) from the
// game's uploads (expeditionsrome_dlss/constant_mirror.hpp). At the frame's first sun light draw (sunlight00-03, the
// addon's): rt_sun from its lighting_VS_PS (the sun from c0, its colour c1, the ambient cube c3-c8: the trace's sun, the
// paths' sun and sky), the G-buffer guides when the reflections or paths want them (rt_guides) and the trace (its
// readable depth); the traced sun and ambient visibility at t40 / t41 for those draws (shaders/rt_sun.hlsli: the darker
// of them and the game's shadow mask and AO). The traced reflections replace the screen-space march's hit in its resolve
// (ssrresolve00, twattila/rt_reflection.hlsli; the march and its blurs left out), whose lit scene is the helper's last
// frame's colour for the next frame (KeepColor). The local lights' shadows: the point lights the frame draws
// (pointlight00 / 01: point_light_PS b2) for the next frame's trace (rt_core picks the nearest and brightest), its lit
// bits and positions bound for the point light draws (t42 / b9, twattila/rt_local_lights.hlsli). The Light Bounce's
// trace goes through rays (path traced when on). Installed when an effect is on at launch.
// Not twattila/rt_scene.hpp's InstallSunDraws / InstallPointLights / SettingsOf: that file needs the TW games'
// anti-aliasing (its shaders); this one hosts rt_core alone (as Ryse and Expeditions: Rome).
namespace twshogun2::ray_tracing {

using Microsoft::WRL::ComPtr;

// The sun light draws (the addon's sunlight00-03): camera_VS_PS at b0, environment_PS at b1, the G-buffer (normal,
// colour, constants, depth) from gbuffer_slot, lighting_VS_PS (the sun from c0) at b2 or b3, the world's harmonics (and
// the previous ones after them) at t2 or t4 (rt_sun's sky). Also the Light Bounce's. The campaign map's: lighting at b3.
constexpr std::array<twattila::light_bounce::SunDraw, 4> SUN_DRAWS = {{
    {.shader_hash = 0xED1BED93, .standard = true, .gbuffer_slot = 7, .lighting_slot = 2, .sun_direction = 0, .sky_harmonics_slot = 2,
     .environment_slot = 1},
    {.shader_hash = 0x5496DE73, .standard = true, .gbuffer_slot = 7, .lighting_slot = 2, .sun_direction = 0, .sky_harmonics_slot = 2,
     .environment_slot = 1},
    {.shader_hash = 0xC3E0E939, .standard = true, .gbuffer_slot = 9, .lighting_slot = 3, .sun_direction = 0, .sky_harmonics_slot = 4,
     .environment_slot = 1},
    {.shader_hash = 0xD5FDDE8F, .standard = true, .gbuffer_slot = 9, .lighting_slot = 3, .sun_direction = 0, .sky_harmonics_slot = 4,
     .environment_slot = 1},
}};
constexpr UINT CAMPAIGN_LIGHTING_SLOT = 3;
// The scene's families by their G-buffer vertex shaders (shader_names.txt; the 2026-11-14 data run's battle drew 11
// cloth and 90 grass G-buffer draws): cloth (sails, banners) thin sheets (RT_THIN), rigidgrass / rigidgrasswet the
// grass (RT_GRASS, the grass's share of the alpha-tested corners). The sprite soldiers (imposter, 03FB2979: 68 draws)
// face the camera as drawn; with Sprite Soldier Shadows on they face the sun (rt_capture_imposter). The far trees
// (rigidtreebillboard, FAAE535B: 98; its pixel shader reads its mask by a UV it works out, so the scene left them out)
// only with Far Tree Shadows on, facing the sun (rt_capture_tree_billboard: its mask the diffuse map's alpha at t1, the
// UV at TEXCOORD1). rigidtreebillboard_3k (4B0E8CE6) drew in none of the traces.
constexpr uint32_t CLOTH_VERTEX_SHADER = 0xBDFF60D4;
constexpr uint32_t GRASS_VERTEX_SHADER = 0xB7A0270E;
constexpr uint32_t IMPOSTER_VERTEX_SHADER = 0x03FB2979;
constexpr uint32_t TREE_BILLBOARD_VERTEX_SHADER = 0xFAAE535B;
// Shadow Casters Out of View (Experimental): the shadow pass's rigid casters (rigidterrainblend vs30_shadow, AC25DAB8; 488 draws in
// the 2026-11-14 battle trace, all instanced: 38M, 53M and 326M indices x instances in cascades 1-3, far too many to stream out) as
// listed scene shaders besides SceneDraw's (twattila/rt_core.hpp, as Warhammer III's): placed by their instances' rows, those whose
// origin is in the main view left out (the main pass has them). Shogun 2 draws its shadow maps before its main view: the last
// frame's (Host::cascade_last_view). The same for variantrigid's (B0E006EE: 10 draws, 4-256 instances; their rows every frame, as
// moving: they sway in the wind like banners, what carries them isn't known). Cloth (8A00A1D4: 44 draws), sprite soldiers (E3DA20D4: 272 draws, cards turned to the sun;
// with Sprite Soldier Shadows on) and trees' leaves and branches (5851CA62, 6F20485E: 22 and 17 draws, 327K and 32K corners in
// the four cascades; one capture version, the leaves' own reading their fade) streamed out every frame through their capture versions (world positions, the mask's
// UV) and Pharaoh's outside-view geometry shader (SceneShader::cascade_world: the game's constants kept, the main view at its b0).
// The far tree cards' (30C453EE: 8 of the data run's 12 traces; turned to the shadow camera per corner, their atlas view picked from
// its way to the tree; with Far Tree Shadows on) the same. Soldiers and horses (variantweightedvtf, A22FA941: 164 draws, 1.18M
// corners in the four cascades of the battle trace; two bones each from the animation map the shadow pass binds at t0) through a
// capture version writing their world alone and Pharaoh's world-only geometry shader. The whole-dump caster census (2026-11-14)
// had these two families in view only, and rigidambientmapmesh_mtb's (7B067623: 4 of the naval traces; AC25DAB8 with a TEXCOORD1
// it never reads) placed as AC25DAB8's.
inline const std::array<twattila::rt_scene::SceneShader, 9> SCENE_SHADERS = {{
    {.hash = 0xAC25DAB8, .capture_shader = __rt_capture_cascade_rigid, .rows_shader = __rt_rows_cascade_rigid, .cascade_keep = __rt_outside_view},
    {.hash = 0x7B067623, .capture_shader = __rt_capture_cascade_rigid, .rows_shader = __rt_rows_cascade_rigid, .cascade_keep = __rt_outside_view},
    {.hash = 0xB0E006EE, .capture_shader = __rt_capture_cascade_variant, .rows_shader = __rt_rows_cascade_variant, .moving = true,
     .cascade_keep = __rt_outside_view},
    {.hash = 0x8A00A1D4, .capture_shader = __rt_capture_cascade_cloth, .dynamic = true, .foliage = true, .thin = true,
     .cascade_keep = __rt_outside_view_leaf, .cascade_world = true},
    {.hash = 0xE3DA20D4, .capture_shader = __rt_capture_cascade_imposter, .dynamic = true, .foliage = true, .sun_facing = true,
     .cascade_keep = __rt_outside_view_leaf, .cascade_world = true},
    {.hash = 0x5851CA62, .capture_shader = __rt_capture_cascade_tree_leaves, .dynamic = true, .foliage = true, .mask_slot = 1,
     .cascade_keep = __rt_outside_view_leaf, .cascade_world = true},
    {.hash = 0x6F20485E, .capture_shader = __rt_capture_cascade_tree, .dynamic = true, .cascade_keep = __rt_outside_view_leaf,
     .cascade_world = true},
    {.hash = 0x30C453EE, .capture_shader = __rt_capture_cascade_far_tree, .dynamic = true, .foliage = true, .sun_facing = true,
     .far_tree = true, .mask_slot = 1, .cascade_keep = __rt_outside_view_leaf, .cascade_world = true},
    {.hash = 0xA22FA941, .capture_shader = __rt_capture_cascade_unit, .dynamic = true, .cascade_keep = __rt_outside_view_world,
     .cascade_world = true},
}};
// The point light draws (pointlight00 / 01: point_light_PS at b2, its light's position c0, colour c1.xyz, reach c1.w).
constexpr std::array<uint32_t, 2> POINT_LIGHT_SHADERS = {0x5CBF4FB6, 0x57139F8D};
// The battle fog's light (fog01, battlefog ps40_main_inscattering: the sun's part through rt_fog.hlsli's RtFogSun).
constexpr uint32_t FOG_LIGHT_SHADER = 0x4355E897;
// The battles' rivers and lakes (waterplane ps30_battle_high / medium / ultra_water_quality): Ray-Traced Water Reflections.
constexpr uint32_t WATER_PLANE_SHADER = 0x9B9EC116;

namespace internal {

// camera_VS_PS through g_render_target_dimensions (c40): view_projection c10-c13, g_viewport_dimensions c38.
constexpr uint32_t CAMERA_ROWS = 41;
constexpr uint32_t CAMERA_VIEW_PROJECTION = 10;
constexpr uint32_t CAMERA_VIEWPORT = 38;
// The traced visibilities' slots (shaders/rt_sun.hlsli), Coloured Shadows' filter's (RtSunColour).
constexpr UINT VISIBILITY_SLOT = 40;
constexpr UINT TINT_SLOT = 50;
// rt_sun's b1 (rt_sun.cs_5_0.hlsl): lighting_VS_PS's first floats of the sun's direction (c0), the ambient cube (c3-c8:
// +x, -x, +y, -y, +z, -z, index 0 where the normal's axis is 0 or more; with the harmonics the paths' sky) and the sun
// colour (c1; the paths' sun light with Path-Traced AO). Its third: 0 (Shogun 2's rt_sun makes the same sky either way).
constexpr std::array<uint32_t, 8> SUN_OFFSETS = {0, 12, 0, 0, 4, 0, 0, 0};
// Softer Water Reflections' least roughness on the water's guide (rt_water_plane): Attila's WATER_SOFT_ROUGHNESS
// (twattila/rt_scene.hpp, which this host can't include), past the helper's mirrors (under 0.05: one ray, no history), so
// Samples, the history and its filter take the water.
constexpr float WATER_SOFT_ROUGHNESS = 0.06f;

// Ray-Traced Water Reflections' objects (WaterSurface): rt_water_plane, its constants, the water's depth (R32) and mask
// (R8) at the depth's size, the replayed water meshes' depth (D32) and its states.
struct Water {
  ComPtr<ID3D11ComputeShader> shader;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11Texture2D> meshes;
  ComPtr<ID3D11DepthStencilView> meshes_target;
  ComPtr<ID3D11ShaderResourceView> meshes_view;
  ComPtr<ID3D11DepthStencilState> nearest;
  ComPtr<ID3D11RasterizerState> both_sides;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11UnorderedAccessView> depth_uav;
  ComPtr<ID3D11ShaderResourceView> depth_view;
  ComPtr<ID3D11Texture2D> mask;
  ComPtr<ID3D11UnorderedAccessView> mask_uav;
  ComPtr<ID3D11ShaderResourceView> mask_view;
};

// A battle water plane's draw (waterplane, rivers and lakes) kept for the next frame's WaterSurface: its mesh and vertex
// shader, a copy of its g_world (waterplane_VS, b2), its draw's numbers (`indices` null: not indexed).
struct WaterDraw {
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11Buffer> vertices;
  UINT stride = 0;
  UINT offset = 0;
  ComPtr<ID3D11Buffer> indices;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  ComPtr<ID3D11VertexShader> shader;
  ComPtr<ID3D11Buffer> world;
  UINT count = 0;
  UINT instances = 0;
  UINT first = 0;
  INT base = 0;
  UINT first_instance = 0;
};
constexpr size_t MAX_WATER_DRAWS = 32;

struct State {
  std::mutex mutex;
  twattila::rt_scene::Settings settings;
  std::array<UINT, 2> render_size = {};
  std::array<UINT, 2> depth_size = {};  // the sun draws' depth's: the main view's (other views' G-buffers stay out)
  uint64_t frame = 0;
  uint64_t guides_frame = UINT64_MAX;  // the frame the guides were written in
  uint64_t sun_frame = UINT64_MAX;     // the frame rt_sun ran in
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> guides;
  ComPtr<ID3D11ComputeShader> sun;
  ComPtr<ID3D11Buffer> sun_offsets;
  bool failed = false;
  bool campaign = false;  // the last sun draw the campaign map's (no Path-Traced AO there: its overlay takes the ambient)
  // Path-Traced AO: the sun draws' lighting_VS_PS copied without the game's ambient (the cube and the harmonics' flag
  // zero: the shaders' own math gives none), once a frame for each buffer; the game's put back after the draw.
  ComPtr<ID3D11Buffer> no_ambient;
  ComPtr<ID3D11Buffer> ambient_lighting;
  UINT ambient_lighting_bytes = 0;
  uint64_t ambient_frame = UINT64_MAX;
  ID3D11Buffer* ambient_source = nullptr;  // compared only
  ComPtr<ID3D11Buffer> game_lighting;
  UINT game_lighting_slot = 0;
  // Ray-Traced Water Reflections: the sea's plane (its normal up) from its last draw, the frame of it, the frame the rays
  // left from it (its mask valid).
  std::array<float, 4> sea_plane = {};
  uint64_t sea_frame = UINT64_MAX;
  uint64_t water_frame = UINT64_MAX;
  Water water;
  // The battle water planes' draws of `water_draws_frame` (the first `water_draw_count`; their buffers kept for reuse).
  std::vector<WaterDraw> water_draws;
  size_t water_draw_count = 0;
  uint64_t water_draws_frame = UINT64_MAX;
};
static State state;

// The main view's camera last uploaded in `buffer` (camera_VS_PS); false: none yet.
inline bool Camera(ID3D11Buffer* buffer, twattila::rt_scene::Camera* camera) {
  expeditionsrome::dlss::constants::Watch(buffer, CAMERA_ROWS * 16);
  std::array<std::array<float, 4>, CAMERA_ROWS> rows = {};
  if (!expeditionsrome::dlss::constants::Read(buffer, 0, std::as_writable_bytes(std::span(rows)))) return false;
  std::array<float, 16> view_projection = {};
  for (size_t row = 0; row < 4; ++row) std::copy_n(rows[CAMERA_VIEW_PROJECTION + row].begin(), 4, view_projection.begin() + (row * 4));
  *camera = {
      .view_projection = view_projection,
      .inverse_view_projection = talesofarise::frame_generation::Inverse(view_projection),
      .position = {rows[0][0], rows[0][1], rows[0][2]},
  };
  const std::unique_lock lock(state.mutex);
  state.render_size = {static_cast<UINT>(rows[CAMERA_VIEWPORT][0]), static_cast<UINT>(rows[CAMERA_VIEWPORT][1])};
  return true;
}

// A G-buffer draw (rt0-2 RGBA16F and the depth, depth written, unblended) of the main view (its depth the sun draws'
// size): the bridge prepared at that size, the pixel shader's alpha test, its vertex shader skinned or not.
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  std::array<ID3D11RenderTargetView*, 3> targets = {};
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth_view.GetAddressOf());
  bool gbuffer = static_cast<bool>(depth_view);
  for (auto* target : targets) {
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    if (target != nullptr) target->GetDesc(&desc);
    gbuffer = gbuffer && target != nullptr && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
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
  if (!depth_desc.DepthEnable || depth_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ALL || blend_desc.RenderTarget[0].BlendEnable) {
    return false;
  }
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  D3D11_TEXTURE2D_DESC depth_texture_desc = {};
  if (FAILED(depth_resource.As(&depth_texture))) return false;
  depth_texture->GetDesc(&depth_texture_desc);
  bool sprite_shadows = false;
  {
    const std::unique_lock lock(state.mutex);
    if (state.depth_size != std::array<UINT, 2>{depth_texture_desc.Width, depth_texture_desc.Height}) return false;
    sprite_shadows = state.settings.sprite_shadows;
  }
  const bool far_tree = shader->hash == TREE_BILLBOARD_VERTEX_SHADER;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!upscaling::PrepareBridge(device.Get(), depth_texture_desc.Width, depth_texture_desc.Height)
      || !(far_tree || twattila::rt_scene::SceneCoverage(native, shader))) {
    return false;
  }
  shader->thin = shader->hash == CLOTH_VERTEX_SHADER;
  shader->grass = shader->hash == GRASS_VERTEX_SHADER;
  // Sun-facing cards (rt_core skips the far trees with Far Tree Shadows off): drawn from rebuilt buffers, captured every frame.
  if (far_tree) {
    shader->capture_shader = __rt_capture_tree_billboard;
    shader->far_tree = true;
    shader->foliage = true;
    shader->uv_semantic = "TEXCOORD";
    shader->uv_semantic_index = 1;
    shader->mask_slot = 1;
  } else if (shader->hash == IMPOSTER_VERTEX_SHADER && sprite_shadows) {
    shader->capture_shader = __rt_capture_imposter;
  }
  shader->sun_facing = !shader->capture_shader.empty();
  shader->dynamic = shader->sun_facing || twattila::rt_scene::SkinnedVertexShader(shader->hash);
  return true;
}

// The addon's objects on the context's device (made again when it changes); false: not available. Hold state.mutex.
inline bool PrepareObjects(ID3D11DeviceContext* native) {
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (state.device == device) return !state.failed;
  ComPtr<ID3D11Device1> device1;
  const auto feature_level = device->GetFeatureLevel();
  const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
  state.device = device;
  state.private_state.Reset();
  state.guides.Reset();
  state.sun.Reset();
  state.sun_offsets.Reset();
  state.no_ambient.Reset();
  state.ambient_lighting.Reset();
  state.ambient_lighting_bytes = 0;
  state.water = {};
  state.water_draws.clear();
  state.water_draw_count = 0;
  const D3D11_BUFFER_DESC offsets_desc = {.ByteWidth = sizeof(SUN_OFFSETS), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA offsets_data = {.pSysMem = SUN_OFFSETS.data()};
  state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                 || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                             nullptr, state.private_state.GetAddressOf()))
                 || FAILED(device->CreateComputeShader(__rt_guides.data(), __rt_guides.size(), nullptr, state.guides.GetAddressOf()))
                 || FAILED(device->CreateComputeShader(__rt_sun.data(), __rt_sun.size(), nullptr, state.sun.GetAddressOf()))
                 || FAILED(device->CreateBuffer(&offsets_desc, &offsets_data, state.sun_offsets.GetAddressOf()));
  return !state.failed;
}

// The sun for this frame's trace (toward it) and the paths' sun and sky from a sun draw's lighting_VS_PS, once a frame
// (rt_sun), in the addon's own context state.
inline void WriteSun(ID3D11DeviceContext* native, const twattila::light_bounce::SunDraw& draw) {
  auto& bridge = twattila::dlaa_bridge::bridge;
  ComPtr<ID3D11Buffer> lighting;
  native->PSGetConstantBuffers(draw.lighting_slot, 1, lighting.GetAddressOf());
  std::array<ComPtr<ID3D11Buffer>, 2> camera_environment;
  native->PSGetConstantBuffers(draw.camera_slot, 1, camera_environment[0].GetAddressOf());
  native->PSGetConstantBuffers(draw.environment_slot, 1, camera_environment[1].GetAddressOf());
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> harmonics;
  native->PSGetShaderResources(draw.sky_harmonics_slot, 1, harmonics[0].GetAddressOf());
  native->PSGetShaderResources(draw.sky_harmonics_slot + 1, 1, harmonics[1].GetAddressOf());
  ComPtr<ID3D11DeviceContext1> native1;
  if (!bridge.rt_sun.uav || !lighting || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  {
    const std::unique_lock lock(state.mutex);
    if (state.sun_frame == state.frame || !PrepareObjects(native)) return;
    state.sun_frame = state.frame;
  }
  const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
  ID3D11Buffer* constants[] = {lighting.Get(), state.sun_offsets.Get(), camera_environment[0].Get(), camera_environment[1].Get()};
  ID3D11UnorderedAccessView* sun = bridge.rt_sun.uav.Get();
  native->CSSetShader(state.sun.Get(), nullptr, 0);
  native->CSSetConstantBuffers(0, 4, constants);
  ID3D11ShaderResourceView* harmonic_views[] = {harmonics[0].Get(), harmonics[1].Get()};
  native->CSSetShaderResources(0, 2, harmonic_views);
  native->CSSetUnorderedAccessViews(0, 1, &sun, nullptr);
  native->Dispatch(1, 1, 1);
}

// The G-buffer guides for the reflections and paths from a sun draw's G-buffer (its first three slots: normal, colour,
// constants), once a frame, in the addon's own context state.
inline void WriteGuides(ID3D11DeviceContext* native, UINT gbuffer_slot) {
  auto& bridge = twattila::dlaa_bridge::bridge;
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> gbuffer;
  for (UINT index = 0; index < gbuffer.size(); ++index) native->PSGetShaderResources(gbuffer_slot + index, 1, gbuffer[index].GetAddressOf());
  ComPtr<ID3D11DeviceContext1> native1;
  if (!bridge.guides || !gbuffer[0] || !gbuffer[1] || !gbuffer[2] || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
    return;
  }
  {
    const std::unique_lock lock(state.mutex);
    if (state.guides_frame == state.frame || !PrepareObjects(native)) return;
    state.guides_frame = state.frame;
  }
  const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
  ID3D11ShaderResourceView* views[] = {gbuffer[0].Get(), gbuffer[1].Get(), gbuffer[2].Get()};
  ID3D11UnorderedAccessView* written[] = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(), bridge.normals.uav.Get(),
                                          bridge.roughness.uav.Get()};
  native->CSSetShader(state.guides.Get(), nullptr, 0);
  native->CSSetShaderResources(0, 3, views);
  native->CSSetUnorderedAccessViews(0, 4, written, nullptr);
  native->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
}

// Path-Traced AO this frame (rt_core: on once the paths ran long enough, and they ran last frame).
inline bool PathAmbientNow() {
  auto& runtime = twattila::rt_scene::internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  return runtime.path_ambient_on && runtime.bounced_frame + 1 >= runtime.frame;
}

// The draw's lighting_VS_PS replaced by its copy without the game's ambient (the cube c3-c8 and the harmonics' flag c10.w
// zero, as rt_scene.hpp's OnPathAmbientDraw does with Attila's cube); the game's kept to put back. False: the game's stays.
inline bool LightingWithoutAmbient(ID3D11DeviceContext* native, UINT slot) {
  ComPtr<ID3D11Buffer> game_lighting;
  native->PSGetConstantBuffers(slot, 1, game_lighting.GetAddressOf());
  if (!game_lighting) return false;
  D3D11_BUFFER_DESC desc = {};
  game_lighting->GetDesc(&desc);
  constexpr UINT CUBE_OFFSET = 3 * 16;
  constexpr UINT CUBE_BYTES = 6 * 16;
  constexpr UINT HARMONICS_FLAG_OFFSET = (10 * 16) + 12;
  if (desc.ByteWidth < HARMONICS_FLAG_OFFSET + 4) return false;
  const std::unique_lock lock(state.mutex);
  if (!state.device) return false;
  if (state.ambient_lighting_bytes != desc.ByteWidth) {
    state.ambient_lighting.Reset();
    state.ambient_lighting_bytes = 0;
    const D3D11_BUFFER_DESC copy = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(state.device->CreateBuffer(&copy, nullptr, state.ambient_lighting.GetAddressOf()))) return false;
    state.ambient_lighting_bytes = desc.ByteWidth;
  }
  if (!state.no_ambient) {
    constexpr std::array<float, CUBE_BYTES / 4> ZEROS = {};
    const D3D11_BUFFER_DESC zeros = {.ByteWidth = CUBE_BYTES, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = ZEROS.data()};
    if (FAILED(state.device->CreateBuffer(&zeros, &data, state.no_ambient.GetAddressOf()))) return false;
  }
  if (state.ambient_frame != state.frame || state.ambient_source != game_lighting.Get()) {
    native->CopyResource(state.ambient_lighting.Get(), game_lighting.Get());
    const D3D11_BOX cube = {.left = 0, .top = 0, .front = 0, .right = CUBE_BYTES, .bottom = 1, .back = 1};
    native->CopySubresourceRegion(state.ambient_lighting.Get(), 0, CUBE_OFFSET, 0, 0, state.no_ambient.Get(), 0, &cube);
    const D3D11_BOX flag = {.left = 0, .top = 0, .front = 0, .right = 4, .bottom = 1, .back = 1};
    native->CopySubresourceRegion(state.ambient_lighting.Get(), 0, HARMONICS_FLAG_OFFSET, 0, 0, state.no_ambient.Get(), 0, &flag);
    state.ambient_frame = state.frame;
    state.ambient_source = game_lighting.Get();
  }
  native->PSSetConstantBuffers(slot, 1, state.ambient_lighting.GetAddressOf());
  state.game_lighting = std::move(game_lighting);
  state.game_lighting_slot = slot;
  return true;
}

// The frame's first sun light draw: the sun, the guides and the trace; every one: the traced visibilities bound and, with
// Path-Traced AO, the lighting without the game's ambient (the paths bring the sky's light instead; put back after).
constexpr auto OnSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& draw = command.template GetCallbackData<twattila::light_bounce::SunDraw>();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  twattila::rt_scene::Settings settings;
  {
    const std::unique_lock lock(state.mutex);
    settings = state.settings;
  }
  ComPtr<ID3D11ShaderResourceView> depth;
  ComPtr<ID3D11Buffer> camera_buffer;
  native->PSGetShaderResources(draw.gbuffer_slot + 3, 1, depth.GetAddressOf());
  native->PSGetConstantBuffers(draw.camera_slot, 1, camera_buffer.GetAddressOf());
  if (!depth || !camera_buffer) return {};
  {
    ComPtr<ID3D11Resource> depth_resource;
    depth->GetResource(depth_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> depth_texture;
    D3D11_TEXTURE2D_DESC depth_desc = {};
    if (SUCCEEDED(depth_resource.As(&depth_texture))) depth_texture->GetDesc(&depth_desc);
    const std::unique_lock lock(state.mutex);
    state.depth_size = {depth_desc.Width, depth_desc.Height};
    state.campaign = draw.lighting_slot == CAMPAIGN_LIGHTING_SLOT;
  }
  WriteSun(native, draw);
  // Coloured Shadows: the helper colours the sun's filter by the albedo guide where the screen shows the cloth.
  if (settings.reflections || settings.path_tracing || settings.coloured_shadows || upscaling::Reconstructing()) WriteGuides(native, draw.gbuffer_slot);
  if (twattila::rt_scene::Camera camera; Camera(camera_buffer.Get(), &camera)) twattila::rt_scene::Trace(native, depth.Get(), camera);
  const auto [sun, ambient] = twattila::rt_scene::Visibilities();
  const std::array<ID3D11ShaderResourceView*, 2> visibilities = {settings.sun_shadows ? sun : nullptr,
                                                                 settings.ambient_occlusion ? ambient : nullptr};
  const bool no_ambient = draw.lighting_slot != CAMPAIGN_LIGHTING_SLOT && PathAmbientNow() && LightingWithoutAmbient(native, draw.lighting_slot);
  if (visibilities[0] == nullptr && visibilities[1] == nullptr && !no_ambient) return {};
  native->PSSetShaderResources(VISIBILITY_SLOT, static_cast<UINT>(visibilities.size()), visibilities.data());
  ID3D11ShaderResourceView* tint = (settings.coloured_shadows && visibilities[0] != nullptr) ? twattila::dlaa_bridge::bridge.rt_tint.srv.Get() : nullptr;
  native->PSSetShaderResources(TINT_SLOT, 1, &tint);
  // Put back in finalize callbacks: the Light Bounce draws the sun draw itself (bypass), which skips post callbacks.
  return {.finalize_callback = [](Context& command, const void*) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native == nullptr) return;
    constexpr std::array<ID3D11ShaderResourceView*, 2> NONE = {};
    native->PSSetShaderResources(VISIBILITY_SLOT, static_cast<UINT>(NONE.size()), NONE.data());
    native->PSSetShaderResources(TINT_SLOT, 1, NONE.data());
    const std::unique_lock lock(state.mutex);
    if (state.game_lighting) native->PSSetConstantBuffers(state.game_lighting_slot, 1, state.game_lighting.GetAddressOf());
    state.game_lighting.Reset();
  }};
};

// A point light draw: its light (point_light_PS) kept for the next frame's trace; this frame's traced lights' bits and
// positions bound (put back after).
constexpr auto OnPointLightDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  ComPtr<ID3D11Buffer> point_light;
  native->PSGetConstantBuffers(2, 1, point_light.GetAddressOf());
  if (point_light) expeditionsrome::dlss::constants::Watch(point_light.Get(), 32);
  if (std::array<std::array<float, 4>, 2> rows = {};
      point_light && expeditionsrome::dlss::constants::Read(point_light.Get(), 0, std::as_writable_bytes(std::span(rows)))) {
    twattila::rt_scene::AddLocalLight({.light = {rows[0][0], rows[0][1], rows[0][2], rows[1][3]}, .colour = {rows[1][0], rows[1][1], rows[1][2], 0.f}});
  }
  if (!twattila::rt_scene::BindLocalLights(native)) return {};
  return {.post_callback = [](Context& command, const void*) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native != nullptr) twattila::rt_scene::UnbindLocalLights(native);
  }};
};

// A fog light draw: this frame's fog shadows bound (put back after), as rt_scene.hpp's OnFogDraw.
constexpr auto OnFogDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || !twattila::rt_scene::BindFogShadows(native)) return {};
  return {.post_callback = [](Context& command, const void*) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native != nullptr) twattila::rt_scene::UnbindFogShadows(native);
  }};
};

}  // namespace internal

// Whether this frame's G-buffer guides were written (the reflections, paths and Ray Reconstruction read them).
inline bool GuidesWritten() {
  const std::unique_lock lock(internal::state.mutex);
  return internal::state.guides_frame == internal::state.frame;
}

inline const twattila::rt_scene::Host HOST = {
    .camera = internal::Camera,
    .render_size = [] {
      const std::unique_lock lock(internal::state.mutex);
      return internal::state.render_size;
    },
    .guides_written = GuidesWritten,
    // The main view's camera for the cascades' main-view test (its draws' b0; none read: the cascades keep every instance).
    .capture_camera =
        [](ID3D11DeviceContext* native, twattila::rt_scene::Camera* camera) {
          ComPtr<ID3D11Buffer> buffer;
          native->VSGetConstantBuffers(0, 1, buffer.GetAddressOf());
          if (buffer) internal::Camera(buffer.Get(), camera);
          return true;
        },
    .scene_draw = internal::SceneDraw,
    // The water's guides are rt_water_plane's (WaterSurface), written with its depth.
    .rewrite_guides = [](ID3D11DeviceContext*) {},
    .cascade_last_view = true,
};

// Every present: the settings and a new frame.
inline void Update(twattila::rt_scene::Settings settings) {
  {
    const std::unique_lock lock(internal::state.mutex);
    // The campaign map keeps the game's ambient (its overlay is lit by it in the sun shader): ray-traced AO there.
    if (internal::state.campaign && settings.path_ambient) {
      settings.path_ambient = false;
      settings.ambient_occlusion = true;
    }
    internal::state.settings = settings;
    ++internal::state.frame;
  }
  twattila::rt_scene::Update(settings);
}

// After the screen-space reflections' resolve (its t9: this frame's lit scene, with mips): the render pixels of its first
// level are the helper's colour, last frame's light for the next frame's reflections and paths. Not while the helper
// upscales: its input colour is that light.
inline void KeepColor(reshade::api::command_list* cmd_list) {
  std::array<UINT, 2> render_size;
  {
    const std::unique_lock lock(internal::state.mutex);
    if (!(internal::state.settings.reflections || internal::state.settings.path_tracing)) return;
    render_size = internal::state.render_size;
  }
  if (upscaling::Upscaled()) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (native == nullptr || !bridge.color.texture || render_size[0] == 0 || render_size[0] > bridge.width || render_size[1] > bridge.height) {
    return;
  }
  ComPtr<ID3D11ShaderResourceView> scene;
  native->CSGetShaderResources(9, 1, scene.GetAddressOf());
  if (!scene) return;
  ComPtr<ID3D11Resource> resource;
  scene->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC desc = {};
  if (FAILED(resource.As(&texture))) return;
  texture->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Width < render_size[0] || desc.Height < render_size[1]) return;
  const D3D11_BOX region = {.left = 0, .top = 0, .front = 0, .right = render_size[0], .bottom = render_size[1], .back = 1};
  native->CopySubresourceRegion(bridge.color.texture.Get(), 0, 0, 0, 0, resource.Get(), 0, &region);
  bridge.color_square_root = false;
}

// Ray-Traced Water Reflections (Experimental): the sea (ocean high 16ADFD60, ultra 6B8954A6) and the battles' rivers and
// lakes (waterplane, 9B9EC116) are drawn forward after the reflections with no G-buffer of their own. Before the reflections' first pass
// (ssrhiz00: its t0 the depth, b0 the camera), with water drawn in the last frame, the water planes' meshes are drawn
// again depth-only with this frame's camera (OnWaterDraw's records) and rt_water_plane puts the sea's plane and them into a
// copy of the depth with the water's guides and a mask; the reflections are traced from it (rt_core Reflections: the
// positions again). The resolve leaves the mask's pixels to the environment (shaders/ssrresolve00), the water takes the
// rays' reflection there (shaders/ocean00, waterplane00).
inline void WaterSurface(reshade::api::command_list* cmd_list) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  ComPtr<ID3D11DeviceContext1> native1;
  if (native == nullptr || !GuidesWritten() || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  ComPtr<ID3D11ShaderResourceView> depth;
  ComPtr<ID3D11Buffer> camera;
  native->CSGetShaderResources(0, 1, depth.GetAddressOf());
  native->CSGetConstantBuffers(0, 1, camera.GetAddressOf());
  ComPtr<ID3D11Resource> resource;
  if (depth) depth->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  if (!camera || !resource || FAILED(resource.As(&texture))) return;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = internal::state;
  internal::Water water;
  std::vector<internal::WaterDraw> draws;
  std::array<uint32_t, 12> constants = {};
  {
    const std::unique_lock lock(state.mutex);
    const auto& size = state.render_size;
    const bool sea = state.sea_frame + 1 >= state.frame && state.sea_frame != UINT64_MAX;
    const size_t draw_count = (state.water_draws_frame + 1 == state.frame) ? state.water_draw_count : 0u;
    if (!(state.settings.reflections && state.settings.water_reflections) || !(sea || draw_count != 0) || size[0] == 0
        || desc.SampleDesc.Count != 1 || desc.Width < size[0] || desc.Height < size[1] || !internal::PrepareObjects(native)) {
      return;
    }
    draws.assign(state.water_draws.begin(), state.water_draws.begin() + static_cast<std::ptrdiff_t>(draw_count));
    auto& kept = state.water;
    D3D11_TEXTURE2D_DESC kept_desc = {};
    if (kept.depth) kept.depth->GetDesc(&kept_desc);
    if (kept_desc.Width != desc.Width || kept_desc.Height != desc.Height) {
      const D3D11_TEXTURE2D_DESC depth_desc = {.Width = desc.Width, .Height = desc.Height, .MipLevels = 1, .ArraySize = 1,
                                               .Format = DXGI_FORMAT_R32_TYPELESS, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                               .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE};
      D3D11_TEXTURE2D_DESC mask_desc = depth_desc;
      mask_desc.Format = DXGI_FORMAT_R8_UNORM;
      const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_R32_FLOAT, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D};
      view_desc.Texture2D.MipLevels = 1;
      const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = sizeof(constants), .Usage = D3D11_USAGE_DEFAULT,
                                                .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      D3D11_TEXTURE2D_DESC meshes_desc = depth_desc;
      meshes_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
      const D3D11_DEPTH_STENCIL_VIEW_DESC target_desc = {.Format = DXGI_FORMAT_D32_FLOAT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
      const D3D11_DEPTH_STENCIL_DESC nearest_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
                                                     .DepthFunc = D3D11_COMPARISON_LESS};
      const D3D11_RASTERIZER_DESC both_sides_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
      kept = {};
      if (FAILED(device->CreateTexture2D(&meshes_desc, nullptr, kept.meshes.GetAddressOf()))
          || FAILED(device->CreateDepthStencilView(kept.meshes.Get(), &target_desc, kept.meshes_target.GetAddressOf()))
          || FAILED(device->CreateShaderResourceView(kept.meshes.Get(), &view_desc, kept.meshes_view.GetAddressOf()))
          || FAILED(device->CreateDepthStencilState(&nearest_desc, kept.nearest.GetAddressOf()))
          || FAILED(device->CreateRasterizerState(&both_sides_desc, kept.both_sides.GetAddressOf()))
          || FAILED(device->CreateComputeShader(__rt_water_plane.data(), __rt_water_plane.size(), nullptr, kept.shader.GetAddressOf()))
          || FAILED(device->CreateBuffer(&constants_desc, nullptr, kept.constants.GetAddressOf()))
          || FAILED(device->CreateTexture2D(&depth_desc, nullptr, kept.depth.GetAddressOf()))
          || FAILED(device->CreateUnorderedAccessView(kept.depth.Get(), &uav_desc, kept.depth_uav.GetAddressOf()))
          || FAILED(device->CreateShaderResourceView(kept.depth.Get(), &view_desc, kept.depth_view.GetAddressOf()))
          || FAILED(device->CreateTexture2D(&mask_desc, nullptr, kept.mask.GetAddressOf()))
          || FAILED(device->CreateUnorderedAccessView(kept.mask.Get(), nullptr, kept.mask_uav.GetAddressOf()))
          || FAILED(device->CreateShaderResourceView(kept.mask.Get(), nullptr, kept.mask_view.GetAddressOf()))) {
        kept = {};
        return;
      }
    }
    water = kept;
    const auto& plane = state.sea_plane;
    constants = {std::bit_cast<uint32_t>(plane[0]), std::bit_cast<uint32_t>(plane[1]), std::bit_cast<uint32_t>(plane[2]),
                 std::bit_cast<uint32_t>(plane[3]), size[0], size[1], sea ? 1u : 0u, draws.empty() ? 0u : 1u,
                 std::bit_cast<uint32_t>(state.settings.water_soft ? internal::WATER_SOFT_ROUGHNESS : 0.f)};
  }
  {
    const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
    const auto& bridge = twattila::dlaa_bridge::bridge;
    // The water planes' meshes, nearest first, with this frame's camera (b0) and their own g_world (b2).
    if (!draws.empty()) {
      const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(constants[4]), .Height = static_cast<float>(constants[5]), .MaxDepth = 1.f};
      native->ClearDepthStencilView(water.meshes_target.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
      native->OMSetRenderTargets(0, nullptr, water.meshes_target.Get());
      native->OMSetDepthStencilState(water.nearest.Get(), 0);
      native->RSSetState(water.both_sides.Get());
      native->RSSetViewports(1, &viewport);
      native->PSSetShader(nullptr, nullptr, 0);
      for (const auto& draw : draws) {
        const std::array<ID3D11Buffer*, 3> buffers = {camera.Get(), nullptr, draw.world.Get()};
        native->IASetInputLayout(draw.layout.Get());
        native->IASetVertexBuffers(0, 1, draw.vertices.GetAddressOf(), &draw.stride, &draw.offset);
        native->IASetIndexBuffer(draw.indices.Get(), draw.index_format, draw.index_offset);
        native->IASetPrimitiveTopology(draw.topology);
        native->VSSetShader(draw.shader.Get(), nullptr, 0);
        native->VSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
        if (draw.indices) {
          native->DrawIndexedInstanced(draw.count, draw.instances, draw.first, draw.base, draw.first_instance);
        } else {
          native->DrawInstanced(draw.count, draw.instances, draw.first, draw.first_instance);
        }
      }
      // Unbound: the CS reads it, the game's buffers are not held by the private state.
      const std::array<ID3D11Buffer*, 3> no_buffers = {};
      ID3D11Buffer* no_vertices = nullptr;
      const UINT zero = 0;
      native->OMSetRenderTargets(0, nullptr, nullptr);
      native->IASetVertexBuffers(0, 1, &no_vertices, &zero, &zero);
      native->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
      native->IASetInputLayout(nullptr);
      native->VSSetShader(nullptr, nullptr, 0);
      native->VSSetConstantBuffers(0, static_cast<UINT>(no_buffers.size()), no_buffers.data());
    }
    ID3D11Buffer* buffers[] = {camera.Get(), water.constants.Get()};
    std::array<ID3D11UnorderedAccessView*, 6> targets = {water.depth_uav.Get(),    bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(),
                                                         bridge.normals.uav.Get(), bridge.roughness.uav.Get(),      water.mask_uav.Get()};
    native->UpdateSubresource(water.constants.Get(), 0, nullptr, constants.data(), 0, 0);
    native->CSSetShader(water.shader.Get(), nullptr, 0);
    native->CSSetConstantBuffers(0, 2, buffers);
    const std::array<ID3D11ShaderResourceView*, 2> inputs = {depth.Get(), water.meshes_view.Get()};
    native->CSSetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.data());
    native->CSSetUnorderedAccessViews(0, static_cast<UINT>(targets.size()), targets.data(), nullptr);
    native->Dispatch((constants[4] + 7) / 8, (constants[5] + 7) / 8, 1);
    // Unbound here: the private state keeps its bindings, the depth and guides are read elsewhere.
    targets = {};
    const std::array<ID3D11ShaderResourceView*, 2> no_inputs = {};
    native->CSSetUnorderedAccessViews(0, static_cast<UINT>(targets.size()), targets.data(), nullptr);
    native->CSSetShaderResources(0, static_cast<UINT>(no_inputs.size()), no_inputs.data());
  }
  twattila::rt_scene::Reflections(native, water.depth_view.Get());
  bool traced = false;
  {
    auto& rays = twattila::rt_scene::internal::runtime;
    const std::unique_lock lock(rays.mutex);
    traced = rays.water_traced;
  }
  const std::unique_lock lock(state.mutex);
  state.water_frame = traced ? state.frame : UINT64_MAX;
}

// rt_water_plane's mask at t45 (this frame's, where the rays left from the sea) for the resolve (`compute`) or the sea's
// draw; `bind` false: unbound.
inline void BindWaterMask(reshade::api::command_list* cmd_list, bool compute, bool bind) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  ComPtr<ID3D11ShaderResourceView> mask;
  if (bind) {
    const std::unique_lock lock(internal::state.mutex);
    if (internal::state.water_frame == internal::state.frame) mask = internal::state.water.mask_view;
  }
  if (compute) {
    native->CSSetShaderResources(45, 1, mask.GetAddressOf());
  } else {
    native->PSSetShaderResources(45, 1, mask.GetAddressOf());
  }
}

// The sea's draw (ocean high 16ADFD60, ultra 6B8954A6): its plane for the next frame's WaterSurface (ocean_VS at VS b1:
// c1, g_decal_ocean_projection_plane, taken with its normal up), the rays' reflections (b10, t46 / t47) and the water mask
// for its pixel shader (shaders/ocean00, 01); `bind` false after it: unbound.
inline void OnSeaDraw(reshade::api::command_list* cmd_list, bool bind) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  ComPtr<ID3D11Buffer> ocean;
  if (bind) native->VSGetConstantBuffers(1, 1, ocean.GetAddressOf());
  std::array<float, 4> plane = {};
  if (ocean && expeditionsrome::dlss::constants::Read(ocean.Get(), 16, std::as_writable_bytes(std::span(plane)))) {
    const float length = std::sqrt((plane[0] * plane[0]) + (plane[1] * plane[1]) + (plane[2] * plane[2]));
    const float up = (plane[1] < 0.f) ? -length : length;
    if (length > 0.f && std::abs(plane[1]) > 0.9f * length) {
      const std::unique_lock lock(internal::state.mutex);
      internal::state.sea_plane = {plane[0] / up, plane[1] / up, plane[2] / up, plane[3] / up};
      internal::state.sea_frame = internal::state.frame;
    }
  }
  twattila::rt_scene::BindReflections(cmd_list, false, bind);
  BindWaterMask(cmd_list, false, bind);
}

// A battle water plane's draw (waterplane high / medium / ultra, 9B9EC116; the main view's, with the reflections and the
// water's on): kept for the next frame's WaterSurface (internal::WaterDraw), the rays' reflections and the water mask bound
// for its pixel shader (shaders/waterplane00), unbound after.
constexpr auto OnWaterDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  constexpr bool INDEXED = std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>;
  if constexpr (!INDEXED && !std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>) {
    return {};
  } else {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native == nullptr) return {};
    ComPtr<ID3D11DepthStencilView> depth_view;
    native->OMGetRenderTargets(0, nullptr, depth_view.GetAddressOf());
    ComPtr<ID3D11Resource> depth_resource;
    if (depth_view) depth_view->GetResource(depth_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> depth_texture;
    D3D11_TEXTURE2D_DESC depth_desc = {};
    if (depth_resource && SUCCEEDED(depth_resource.As(&depth_texture))) depth_texture->GetDesc(&depth_desc);
    ComPtr<ID3D11Buffer> world;
    native->VSGetConstantBuffers(2, 1, world.GetAddressOf());
    D3D11_BUFFER_DESC world_desc = {};
    if (world) world->GetDesc(&world_desc);
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    {
      auto& state = internal::state;
      const std::unique_lock lock(state.mutex);
      if (!(state.settings.reflections && state.settings.water_reflections) || !world
          || state.depth_size != std::array<UINT, 2>{depth_desc.Width, depth_desc.Height}) {
        return {};
      }
      if (state.water_draws_frame != state.frame) {
        state.water_draws_frame = state.frame;
        state.water_draw_count = 0;
      }
      if (state.water_draw_count == internal::MAX_WATER_DRAWS) return {};
      if (state.water_draws.size() == state.water_draw_count) state.water_draws.emplace_back();
      auto& draw = state.water_draws[state.water_draw_count];
      D3D11_BUFFER_DESC kept_desc = {};
      if (draw.world) draw.world->GetDesc(&kept_desc);
      const D3D11_BUFFER_DESC copy_desc = {.ByteWidth = world_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      if (kept_desc.ByteWidth != world_desc.ByteWidth && FAILED(device->CreateBuffer(&copy_desc, nullptr, draw.world.ReleaseAndGetAddressOf()))) {
        draw.world.Reset();
        return {};
      }
      native->CopyResource(draw.world.Get(), world.Get());
      native->IAGetInputLayout(draw.layout.ReleaseAndGetAddressOf());
      native->IAGetVertexBuffers(0, 1, draw.vertices.ReleaseAndGetAddressOf(), &draw.stride, &draw.offset);
      native->IAGetIndexBuffer(draw.indices.ReleaseAndGetAddressOf(), &draw.index_format, &draw.index_offset);
      native->IAGetPrimitiveTopology(&draw.topology);
      native->VSGetShader(draw.shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
      const auto& arguments = command.arguments;
      draw.instances = std::max(arguments.instance_count, 1u);
      draw.first_instance = arguments.first_instance;
      if constexpr (INDEXED) {
        draw.count = arguments.index_count;
        draw.first = arguments.first_index;
        draw.base = arguments.vertex_offset;
      } else {
        draw.indices.Reset();
        draw.count = arguments.vertex_count;
        draw.first = arguments.first_vertex;
        draw.base = 0;
      }
      ++state.water_draw_count;
    }
    twattila::rt_scene::BindReflections(command.cmd_list, false, true);
    BindWaterMask(command.cmd_list, false, true);
    return {.post_callback = [](Context& command, const void*) {
      twattila::rt_scene::BindReflections(command.cmd_list, false, false);
      BindWaterMask(command.cmd_list, false, false);
    }};
  }
};

// Before the Light Bounce's sun draw hooks (registration order: this frame's trace first). `casters_out_of_view`: SCENE_SHADERS
// listed (off: none, the scene as before: no listed draws, the captured buffer's clip-space room whole).
inline void Install(bool casters_out_of_view) {
  expeditionsrome::dlss::constants::Use(DLL_PROCESS_ATTACH);
  twattila::rt_scene::InstallScene(casters_out_of_view ? std::span<const twattila::rt_scene::SceneShader>(SCENE_SHADERS)
                                                       : std::span<const twattila::rt_scene::SceneShader>(),
                                   HOST);
  // The paths' sun light with Path-Traced AO (rt_core's RT_PATH_SUN_LIGHT, as rt_scene.hpp's InstallSunDraws sets it).
  twattila::rt_scene::internal::sun_colour = internal::SUN_OFFSETS[4];
  twattila::light_bounce::SetRayTracedTrace(&twattila::rt_scene::internal::TraceLightBounce);
  for (const auto& draw : SUN_DRAWS) {
    renodx::utils::command_action::Register(
        internal::OnSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
  for (const uint32_t shader_hash : POINT_LIGHT_SHADERS) {
    renodx::utils::command_action::Register(
        internal::OnPointLightDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  renodx::utils::command_action::Register(
      internal::OnFogDraw, {.shader_hash = FOG_LIGHT_SHADER, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  renodx::utils::command_action::Register(
      OnWaterDraw, {.shader_hash = WATER_PLANE_SHADER, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnSunDraw);
  renodx::utils::command_action::Unregister(internal::OnPointLightDraw);
  renodx::utils::command_action::Unregister(internal::OnFogDraw);
  renodx::utils::command_action::Unregister(OnWaterDraw);
  twattila::light_bounce::SetRayTracedTrace(nullptr);
  twattila::rt_scene::internal::sun_colour = 0;
  twattila::rt_scene::UninstallScene();
  expeditionsrome::dlss::constants::Use(DLL_PROCESS_DETACH);
}

// After the tone mapper (tonemap00 / 01): ray tracing's Debug View over its output (bridge-size RGBA16F only).
inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  if (ComPtr<ID3D11Texture2D> target; SUCCEEDED(resource.As(&target))) twattila::rt_scene::DrawDebugView(native, target.Get());
}

// The game's device is going away: the addon's objects on it with it (the bridge after it: upscaling.hpp).
inline void OnDestroyDevice(reshade::api::device* device) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.device.Reset();
    internal::state.private_state.Reset();
    internal::state.guides.Reset();
    internal::state.sun.Reset();
    internal::state.sun_offsets.Reset();
    internal::state.no_ambient.Reset();
    internal::state.ambient_lighting.Reset();
    internal::state.ambient_lighting_bytes = 0;
    internal::state.game_lighting.Reset();
    internal::state.water = {};
    internal::state.water_draws.clear();
    internal::state.water_draw_count = 0;
    internal::state.failed = false;
  }
  twattila::rt_scene::OnDestroyDevice(device);
}

}  // namespace twshogun2::ray_tracing
