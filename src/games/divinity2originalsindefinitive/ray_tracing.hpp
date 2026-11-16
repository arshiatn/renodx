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
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <unordered_set>

#include <embed/shaders.h>
#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/hash.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../talesofarise_dlss/ue_light_bounce.hpp"
#include "../twattila/rt_core.hpp"
#include "../twattila/rt_scene.hpp"
#include "./dlaa.hpp"

// Ray tracing (Graphics, Experimental, off; divinity2.tsv D2-11): Divinity 2's scene through the Total War helper
// (twattila/rt_core.hpp; renodx-tw-dlaa64.exe next to the addon, its own D3D12 device with DXR). R1 / R1b (level A, the
// city's and the inventory's frames): the G-buffer pass (GBuffer0-3 at rt0-rt3, the D24S8 scene depth) draws every opaque
// object of the main view with PerView's camera; LinearizeDepth (CB4F88B7) follows it; then the AO chain (PPSAO_Deinterleave
// 4F0C572C, PPSAO B4AF6326 x 4, PPSAO_Reinterleave 2CA00ABD, PPSAO_Blur 6FFB511E x 2) into an RGB10A2 texture whose red
// PPApplyAO (F3A764F3: the sky's ambient light x it) and ClusteredLightingWithSSAO (A6E6A660, its Base at t8: the
// specular occlusion and the direct light's share) read; the cascades' atlas, then one full-screen mask draw (16 PS: PCF,
// PCSS, VSM, unfiltered and debug x 2 or 4 cascades x 1024 or 2048, the sun's visibility in red) that only
// ClusteredLightingWithSSAO reads (t9, as `Shadow`). The scene: those G-buffer draws (the overlays laid on other surfaces
// left out: decals, the ground's surfaces, water) streamed out through the game's own vertex shaders every frame (its
// object matrix at VS b1 is one buffer written for each draw; characters aren't in the game's depth prepass, so not it). Right after LinearizeDepth the trace (the kept depth and camera, dlaa.hpp); the sun's direction from PerFrame
// (b13, column-major: LightPropertyMatrix row 0 = c0-c2's x, toward the sun) at the lighting draw, for the next trace.
// Ours replaces the game's (ATN 2026-11-16): under Ray-Traced Sun Shadows the mask draw writes the traced visibility in
// place of the game's filter; under Ray-Traced AO the AO chain isn't drawn and its two blurs write the traced visibility
// into their targets (the texture both readers take); Skip the Game's Shadow Maps leaves the cascades' draws out under the
// rays' sun. Global Illumination (the rays' One Bounce or the paths' ReSTIR GI, Expeditions' route): the scene right after
// ClusteredLightingWithSSAO prepared, traced, resolved over frames and added to it times each pixel's diffuse colour.
// Path-Traced Ambient (tpa_'s route for a host without rt_core's ambient cube): DeferredSky's distant diffuse cube into the
// paths' sky (rt_sky: the bridge's sun texels 3-8); once the paths have learned the game's light, the IBL diffuse passes'
// cubes black (the sky's and the light probes') and the AO chain open (1): the paths bring that light and its occlusion.
// The G-buffer guides (Ray Reconstruction, the paths, the reflections) and the reflections at ClusteredLightingWithSSAO's
// draw: the rays' reflections blended into the IBL specular it reads (the sky's and the probes' alike; the game's stays
// where the rays met nothing), the game's screen-space reflections left out under them.
// Off, or not traced this frame: the game's passes as they are. Its
// scene's shaders' code is kept from the game's start: Install only when a row is on at launch.
namespace divinity2::ray_tracing {

using Microsoft::WRL::ComPtr;

// The rows (rt_scene::AppendSettings', on the graphics page; restart after turning sun shadows or AO on).
static twattila::rt_scene::Sliders sliders;
// Skip the Game's Shadow Maps (Experimental, off): under Ray-Traced Shadows nothing is drawn into the cascades' atlas
// (R1's city: 962 of the frame's 3,526 draws); its only reader in the census by name (Shadow at t1) is the mask the rays
// replace.
static float skip_shadow_maps = 0.f;
// Ambient Occlusion's and Global Illumination's rows (the graphics page's ssao_* and light_bounce_* keys): GT-VBAO's and
// GT-VBGI's (screen_space.hpp); light_bounce_strength (0: off) also the rays' light bounce (One Bounce) or the paths'
// (ReSTIR GI), and with them Path-Traced Ambient's.
static talesofarise::ue_light_bounce::Rows lighting_rows;

namespace internal {

// The perspective camera's sun shadow masks (CascadedPCF / PCSS / VSM / Unfiltered / ShadowsDebug x 2 or 4 cascades x
// 1024 or 2048, EngineShaders.pak; the debug ones share bytes across sizes). The orthographic camera's have no rays (no
// jittered camera there).
constexpr std::array<uint32_t, 16> SUN_MASK_SHADERS = {0xD91F061A, 0xC1768EDE, 0x59902559, 0x767A46DF,  // PCF
                                                       0x772DD22F, 0x2087D506, 0x55D4F3B3, 0x34974AB4,  // PCSS (replaced)
                                                       0x9ACAC163, 0x5F71F674,                          // VSM
                                                       0x9BC27A7E, 0x16097D3D,                          // Unfiltered
                                                       0x7CE78DEE, 0xBB450A85};                         // ShadowsDebug
// The AO chain before its blurs, and its blur (two draws, the second's target the one read).
constexpr std::array<uint32_t, 3> AO_CHAIN_SHADERS = {0x4F0C572C, 0xB4AF6326, 0x2CA00ABD};
constexpr uint32_t AO_BLUR_SHADER = 0x6FFB511E;
// The G-buffer pixel shaders (sorted) of overlays laid on other surfaces: decals, the surfaces (blood, oil, Source:
// RS3_Surface_*), footprints and water (NAT_Water_*, RS3_FX_PBR_Overlay_Water) (level B: every live _DEF_ pixel shader all
// of whose materials are such, none shared with another material; R1's city: 284 of the G-buffer pass's 1,205 draws).
// The game's G-buffer pass blends every draw alike (one, 1 - source alpha), so only their shaders tell them apart.
constexpr std::array<uint32_t, 107> OVERLAY_PIXEL_SHADERS = {
    0x01739B4E, 0x02632166, 0x0352C122, 0x04780170, 0x056DEC23, 0x095FE280, 0x0B6A7C0F, 0x0CCB175D,
    0x0E0D74B8, 0x0FB6F0CF, 0x11058A22, 0x1470834E, 0x1609FD8F, 0x16C49B09, 0x1CFA7D1C, 0x20D41452,
    0x213F3E5B, 0x32C58199, 0x33C1A1E5, 0x36FDAEE5, 0x3714D165, 0x3BB0D670, 0x3C460081, 0x3E30D6F8,
    0x40C715BF, 0x42ED0B92, 0x43281AD4, 0x4476CDA6, 0x44F489AB, 0x46956303, 0x47A54D5A, 0x4B5A297A,
    0x4C329534, 0x4CA772E3, 0x4CECE260, 0x546E1AFA, 0x56EFC617, 0x57319E93, 0x57E603B3, 0x5B04FC17,
    0x5CAC1D96, 0x5CC71BF2, 0x5E2042B1, 0x60AB50A3, 0x61958F5D, 0x635DE5BD, 0x63B35198, 0x68BC6253,
    0x6B439B56, 0x6C3CF421, 0x6C4FD883, 0x6CE3C3C6, 0x6CF24781, 0x6DDCE4B0, 0x6E85C36C, 0x72852203,
    0x75A83B75, 0x77C8D237, 0x7DB62B89, 0x7E9CD94E, 0x7F835FC5, 0x809F6846, 0x837C2AD0, 0x84B9ABA7,
    0x863D170C, 0x893DF275, 0x8B356F3E, 0x8E9A953B, 0x8ECD01DA, 0x92CCA771, 0x935A2AD6, 0x98DBF5EB,
    0x99661C6F, 0x9C023E84, 0x9DE070C4, 0x9E002249, 0x9E65A438, 0x9FF771A3, 0xA1663AC9, 0xA321DE9B,
    0xA4A6DC22, 0xA8806DF1, 0xAE80079E, 0xB157633B, 0xB2EAEA30, 0xB4CAA33E, 0xB5B7750C, 0xB8C1F4B2,
    0xC07BBE7B, 0xC430AAA7, 0xD1495861, 0xD5E7617E, 0xD6F59CE0, 0xD75B6F92, 0xD854B7AD, 0xD9CF8D7B,
    0xDB7751E1, 0xDBAE1C57, 0xE486915F, 0xE50A8259, 0xEB074F94, 0xEB4FF5F6, 0xEE9C5480, 0xEEADB9A5,
    0xEFEAEAA8, 0xF0BD3D36, 0xFD61875E};
static_assert(std::ranges::is_sorted(OVERLAY_PIXEL_SHADERS));
// The passes that light with the game's IBL diffuse (RDEF: their IBL_Diffuse_RGB / _2_RGB cubes' slots): the sky
// (DeferredSky and its fades: the distant probe, Path-Traced Ambient's sky from its t1) and the local light probes. Under
// Path-Traced Ambient their diffuse cubes are black (the paths bring that light); their specular stays.
struct AmbientPass {
  uint32_t hash;
  std::array<UINT, 2> diffuse_slots;
  UINT diffuse_count;
  bool sky;
};
constexpr std::array<AmbientPass, 7> AMBIENT_PASSES = {{
    {.hash = 0x518005B9, .diffuse_slots = {1, 0}, .diffuse_count = 1, .sky = true},   // DeferredSky
    {.hash = 0x6E5BAE70, .diffuse_slots = {1, 0}, .diffuse_count = 1, .sky = true},   // DeferredSky_FadeBlack
    {.hash = 0xD11AF8DB, .diffuse_slots = {1, 4}, .diffuse_count = 2, .sky = true},   // DeferredSky_FadeCapture
    {.hash = 0x1EF3024A, .diffuse_slots = {1, 4}, .diffuse_count = 2, .sky = true},   // DeferredSky_FadeProbe
    {.hash = 0x7BFF3402, .diffuse_slots = {1, 0}, .diffuse_count = 1, .sky = false},  // DeferredLightProbe
    {.hash = 0xE65FA8E5, .diffuse_slots = {1, 0}, .diffuse_count = 1, .sky = false},  // DeferredLightProbe_FadeBlack
    {.hash = 0x7B6DC76E, .diffuse_slots = {1, 6}, .diffuse_count = 2, .sky = false},  // DeferredLightProbe_FadeCapture
}};
// The most frames the bounce's resolve averages (Expeditions' value).
constexpr float BOUNCE_FRAMES = 24.f;

struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> sun;
  ComPtr<ID3D11ComputeShader> sky;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> write;
  ComPtr<ID3D11PixelShader> bounce_prepare;
  ComPtr<ID3D11ComputeShader> bounce_resolve;
  ComPtr<ID3D11PixelShader> bounce_apply;
  ComPtr<ID3D11ComputeShader> guides;
  ComPtr<ID3D11PixelShader> reflect_apply;
  ComPtr<ID3D11PixelShader> copy;
  ComPtr<ID3D11RasterizerState> no_cull;
  ComPtr<ID3D11BlendState> add;
  ComPtr<ID3D11BlendState> reflect;  // one / inverse source alpha (RGB): rt_reflect_apply over the IBL specular
  ComPtr<ID3D11SamplerState> linear;
  ComPtr<ID3D11Buffer> bounce_constants;  // 96 bytes: the resolve's (the most), the prepare's and the apply's
  ComPtr<ID3D11Buffer> sky_settings;
  ComPtr<ID3D11Buffer> ibl_view;  // rt_ibl.hlsli's (IblView)
  ComPtr<ID3D11ShaderResourceView> black_cube;
  bool failed = false;
};

// The light bounce's textures (the render thread's: Clustered's draw).
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
  std::unordered_map<ID3D11Resource*, ComPtr<ID3D11ShaderResourceView>> scene_views;
  // An ambient pass's own diffuse cubes while black ones stand in (OnAmbientPass to OnAmbientPassDrawn).
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> kept_cubes;
  const AmbientPass* kept_pass = nullptr;
};

struct State {
  std::shared_mutex mutex;
  Objects objects;  // on the game's device
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  Bounce bounce;
  // DeferredSky's LUT (its t3) as this frame's sky draw had it, for the guides (the render thread's).
  ComPtr<ID3D11ShaderResourceView> lut;
  uint64_t lut_frame = UINT64_MAX;
  // The IBL specular ClusteredLightingWithSSAO reads (its t7) and a target view of it; the frame the rays' reflections
  // went into it (the render thread's).
  ComPtr<ID3D11Resource> specular;
  ComPtr<ID3D11RenderTargetView> specular_target;
  uint64_t reflected_frame = UINT64_MAX;
};
static State state;
// The G-buffer guides written since the last present (rt_core reads it through HOST under its own lock: no state.mutex).
static std::atomic_bool guides_written = false;

// rt_ibl.hlsli's view: the View's rows, the jittered view-projection's inverse, the eye, the render size.
struct IblView {
  std::array<float, 12> view_rows;
  std::array<float, 16> inverse_view_projection;
  std::array<float, 4> eye;
  std::array<uint32_t, 4> size;
};
static bool installed = false;
// The game's overlay pixel shaders as made (ReShade's D3D11 pipeline handle is the native shader); its own lock:
// destroy_pipeline also fires inside D3D11 calls made under state.mutex.
static std::shared_mutex overlay_mutex;
static std::unordered_set<uint64_t> overlay_pixel_shaders;
// The cascades' atlas (the sun mask's t1, R1: R16 4096 x 4096), learned at the mask's draw and held so its address stays
// its own; the draws while it is the bound depth target are skipped (the render thread's).
static std::mutex atlas_mutex;
static ComPtr<ID3D11Resource> shadow_atlas;
static std::atomic<uintptr_t> shadow_atlas_address = 0;
static std::atomic_bool skipping_casters = false;

// A depth target bound: the cascades' atlas under Skip the Game's Shadow Maps, with this frame's traced sun (the trace at
// LinearizeDepth comes before the cascades), skips the draws into it until the next bind.
inline void OnBindTargets(reshade::api::command_list*, uint32_t, const reshade::api::resource_view*, reshade::api::resource_view dsv) {
  bool skip = false;
  const uintptr_t atlas = shadow_atlas_address.load(std::memory_order_relaxed);
  if (skip_shadow_maps != 0.f && sliders.sun_shadows != 0.f && dsv.handle != 0 && atlas != 0) {
    ComPtr<ID3D11Resource> resource;
    reinterpret_cast<ID3D11View*>(dsv.handle)->GetResource(resource.GetAddressOf());
    skip = reinterpret_cast<uintptr_t>(resource.Get()) == atlas && twattila::rt_scene::Visibilities()[0] != nullptr;
  }
  skipping_casters.store(skip, std::memory_order_relaxed);
}

inline bool OnCasterDraw(reshade::api::command_list*, uint32_t, uint32_t, uint32_t, uint32_t) {
  return skipping_casters.load(std::memory_order_relaxed);
}

inline bool OnCasterDrawIndexed(reshade::api::command_list*, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  return skipping_casters.load(std::memory_order_relaxed);
}

inline bool OnCasterDrawIndirect(reshade::api::command_list*, reshade::api::indirect_command type, reshade::api::resource, uint64_t,
                                 uint32_t, uint32_t) {
  return (type == reshade::api::indirect_command::draw || type == reshade::api::indirect_command::draw_indexed)
         && skipping_casters.load(std::memory_order_relaxed);
}

inline void OnInitPipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                           const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline pipeline) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader || desc == nullptr || desc->code == nullptr) continue;
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(static_cast<const uint8_t*>(desc->code), desc->code_size);
    if (!std::ranges::binary_search(OVERLAY_PIXEL_SHADERS, hash)) continue;
    const std::unique_lock lock(overlay_mutex);
    overlay_pixel_shaders.insert(pipeline.handle);
  }
}

inline void OnDestroyPipeline(reshade::api::device*, reshade::api::pipeline pipeline) {
  const std::unique_lock lock(overlay_mutex);
  overlay_pixel_shaders.erase(pipeline.handle);
}

// A G-buffer draw of the main view: four targets (five for some characters) and the scene's depth at the display's
// size, not an overlay's (OVERLAY_PIXEL_SHADERS), its pixel shader's coverage (rt_core's SceneCoverage); captured every
// frame.
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  std::array<ID3D11RenderTargetView*, 4> targets = {};
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(4, targets.data(), depth_view.GetAddressOf());
  bool all_targets = true;
  for (ID3D11RenderTargetView* target : targets) {
    all_targets = all_targets && target != nullptr;
    if (target != nullptr) target->Release();
  }
  if (!all_targets || !depth_view) return false;
  const D3D11_TEXTURE2D_DESC depth_desc = dlaa::internal::TextureOf(depth_view.Get());
  const auto [width, height] = dlaa::SceneSize();
  if (width == 0 || depth_desc.Width != width || depth_desc.Height != height || depth_desc.SampleDesc.Count != 1) return false;
  {
    ComPtr<ID3D11PixelShader> pixel_shader;
    native->PSGetShader(pixel_shader.GetAddressOf(), nullptr, nullptr);
    const std::shared_lock lock(overlay_mutex);
    if (overlay_pixel_shaders.contains(reinterpret_cast<uint64_t>(pixel_shader.Get()))) return false;
  }
  if (!twattila::rt_scene::SceneCoverage(native, shader)) return false;
  shader->dynamic = true;
  // Each draw its own capture: one mesh drawn with other object matrices (VS b1, outside rt_core's key) is other triangles.
  shader->every_draw = true;
  return true;
}

// The game's `device` is going away: the objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  twattila::rt_scene::OnDestroyDevice(device);
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  {
    const std::unique_lock lock(state.mutex);
    if (state.objects.device.Get() == native) {
      state.objects = {};
      state.bounce = {};
      state.lut.Reset();
      state.specular.Reset();
      state.specular_target.Reset();
    }
  }
  const std::unique_lock lock(atlas_mutex);
  shadow_atlas_address.store(0, std::memory_order_relaxed);
  shadow_atlas.Reset();
}

inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
                      uint32_t, const reshade::api::rect*) {
  // The rows as Attila's (they need an upscaler), at 100%. Path-Traced Ambient for a host without rt_core's ambient cube
  // (tpa_'s route, Expeditions' 170aa2058): the host's own sky (rt_sky) and the rays' AO off under it (one AO method).
  twattila::rt_scene::Settings settings = twattila::rt_scene::SettingsOf(sliders, dlaa::sliders, lighting_rows.light_bounce_strength, 1.f);
  settings.path_ambient = sliders.path_ambient != 0.f && settings.path_tracing;
  settings.ambient_occlusion = settings.ambient_occlusion && !settings.path_ambient;
  dlaa::reflections = settings.reflections || settings.specular_occlusion;
  dlaa::rays = settings.sun_shadows || settings.ambient_occlusion || settings.light_bounce || dlaa::reflections;
  // The guides for Ray Reconstruction, the paths and the reflections (Attila's: normals and roughness from the G-buffer).
  dlaa::guides = twattila::feature_settings::AaSettings(dlaa::sliders).ray_reconstruction || settings.path_tracing || dlaa::reflections;
  twattila::rt_scene::Update(settings);
  guides_written.store(false, std::memory_order_relaxed);
  const std::unique_lock lock(state.mutex);
  state.settings = settings;
  ++state.frame;
}

// The paths bring the sky this frame: Path-Traced Ambient on, the last light bounce's paths had learned the game's
// light (rt_core's path_ambient_on) and this frame was traced (at LinearizeDepth, before the AO and the sky passes).
inline bool PathsBringSky() {
  {
    const std::shared_lock lock(state.mutex);
    if (!state.settings.path_ambient) return false;
  }
  {
    auto& runtime = twattila::rt_scene::internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    if (!runtime.path_ambient_on) return false;
  }
  return twattila::rt_scene::Visibilities()[0] != nullptr;
}

// The objects on the game's `device` (made again when it changes); false: not available.
inline bool MakeObjects(ID3D11Device* device) {
  const std::unique_lock lock(state.mutex);
  auto& objects = state.objects;
  if (objects.device.Get() == device) return !objects.failed;
  objects = {.device = device};
  ComPtr<ID3D11Device1> device1;
  const D3D_FEATURE_LEVEL feature_level = device->GetFeatureLevel();
  const D3D11_RASTERIZER_DESC raster_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
  D3D11_BLEND_DESC add_desc = {};
  add_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ONE, .DestBlend = D3D11_BLEND_ONE, .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ZERO, .DestBlendAlpha = D3D11_BLEND_ONE, .BlendOpAlpha = D3D11_BLEND_OP_ADD, .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE};
  D3D11_BLEND_DESC reflect_desc = add_desc;
  reflect_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  const D3D11_SAMPLER_DESC linear_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  const D3D11_BUFFER_DESC bounce_desc = {.ByteWidth = 96, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC sky_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC view_desc = {.ByteWidth = sizeof(IblView), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  // A 1 x 1 black cube in place of the ambient passes' diffuse ones.
  const D3D11_TEXTURE2D_DESC cube_desc = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 6, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE, .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
  constexpr uint32_t BLACK = 0u;
  const std::array<D3D11_SUBRESOURCE_DATA, 6> cube_data = {{{&BLACK, 4}, {&BLACK, 4}, {&BLACK, 4}, {&BLACK, 4}, {&BLACK, 4}, {&BLACK, 4}}};
  ComPtr<ID3D11Texture2D> cube;
  objects.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(0, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                               objects.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rt_frame_sun.data(), __rt_frame_sun.size(), nullptr, objects.sun.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rt_sky.data(), __rt_sky.size(), nullptr, objects.sky.GetAddressOf()))
                   || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, objects.full_screen.GetAddressOf()))
                   || FAILED(device->CreatePixelShader(__rt_write.data(), __rt_write.size(), nullptr, objects.write.GetAddressOf()))
                   || FAILED(device->CreatePixelShader(__rt_bounce_prepare.data(), __rt_bounce_prepare.size(), nullptr, objects.bounce_prepare.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rt_bounce_resolve.data(), __rt_bounce_resolve.size(), nullptr, objects.bounce_resolve.GetAddressOf()))
                   || FAILED(device->CreatePixelShader(__rt_bounce_apply.data(), __rt_bounce_apply.size(), nullptr, objects.bounce_apply.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rt_guides.data(), __rt_guides.size(), nullptr, objects.guides.GetAddressOf()))
                   || FAILED(device->CreatePixelShader(__rt_reflect_apply.data(), __rt_reflect_apply.size(), nullptr, objects.reflect_apply.GetAddressOf()))
                   || FAILED(device->CreatePixelShader(__rt_copy.data(), __rt_copy.size(), nullptr, objects.copy.GetAddressOf()))
                   || FAILED(device->CreateRasterizerState(&raster_desc, objects.no_cull.GetAddressOf()))
                   || FAILED(device->CreateBlendState(&add_desc, objects.add.GetAddressOf()))
                   || FAILED(device->CreateBlendState(&reflect_desc, objects.reflect.GetAddressOf()))
                   || FAILED(device->CreateSamplerState(&linear_desc, objects.linear.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&bounce_desc, nullptr, objects.bounce_constants.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&sky_desc, nullptr, objects.sky_settings.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&view_desc, nullptr, objects.ibl_view.GetAddressOf()))
                   || FAILED(device->CreateTexture2D(&cube_desc, cube_data.data(), cube.GetAddressOf()))
                   || FAILED(device->CreateShaderResourceView(cube.Get(), nullptr, objects.black_cube.GetAddressOf()));
  return !objects.failed;
}

inline ID3D11DeviceContext* ImmediateContext(reshade::api::command_list* cmd_list) {
  if (!dlaa::rays) return nullptr;
  return dlaa::internal::ImmediateContext(cmd_list);
}

// A full-screen triangle of `pixel_shader` into `target` (`size`: the viewport's; `blend` null: none) with `views` at
// t0 on and `constants` at b0, in the addon's own context state (the game's put back after; state.mutex held shared).
inline void FullScreen(ID3D11DeviceContext* native, ID3D11RenderTargetView* target, std::array<UINT, 2> size, ID3D11PixelShader* pixel_shader,
                       std::span<ID3D11ShaderResourceView* const> views, ID3D11Buffer* constants = nullptr, ID3D11BlendState* blend = nullptr) {
  ComPtr<ID3D11DeviceContext1> native1;
  if (FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  const auto& objects = state.objects;
  const talesofarise::dlss::ContextStateScope scope(native1.Get(), objects.private_state.Get());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(size[0]), .Height = static_cast<float>(size[1]), .MaxDepth = 1.f};
  native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  native->VSSetShader(objects.full_screen.Get(), nullptr, 0);
  native->PSSetShader(pixel_shader, nullptr, 0);
  native->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  native->PSSetSamplers(0, 1, objects.linear.GetAddressOf());
  if (constants != nullptr) native->PSSetConstantBuffers(0, 1, &constants);
  native->RSSetState(objects.no_cull.Get());
  native->RSSetViewports(1, &viewport);
  native->OMSetBlendState(blend, nullptr, 0xFFFFFFFF);
  native->OMSetRenderTargets(1, &target, nullptr);
  native->Draw(3, 0);
}

// `visibility` (R8, 1 open) into `target` in place of what the game's draw would have written there (its red; no blend).
inline void DrawVisibility(ID3D11DeviceContext* native, ID3D11RenderTargetView* target, ID3D11ShaderResourceView* visibility) {
  const D3D11_TEXTURE2D_DESC target_desc = dlaa::internal::TextureOf(target);
  const std::shared_lock lock(state.mutex);
  if (state.objects.failed || !state.objects.write) return;
  FullScreen(native, target, {target_desc.Width, target_desc.Height}, state.objects.write.Get(), std::span(&visibility, 1));
}

// The sun's direction from PerFrame (`frame_constants`, the game's b13) into the bridge's sun texture (texel 0), for the
// next trace.
inline void WriteSun(ID3D11DeviceContext* native, ID3D11Buffer* frame_constants) {
  auto& bridge = twattila::dlaa_bridge::bridge;
  ComPtr<ID3D11DeviceContext1> native1;
  if (!bridge.rt_sun.uav || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  const std::shared_lock lock(state.mutex);
  const auto& objects = state.objects;
  if (objects.failed || !objects.sun) return;
  const talesofarise::dlss::ContextStateScope scope(native1.Get(), objects.private_state.Get());
  native->CSSetShader(objects.sun.Get(), nullptr, 0);
  native->CSSetConstantBuffers(0, 1, &frame_constants);
  native->CSSetUnorderedAccessViews(0, 1, bridge.rt_sun.uav.GetAddressOf(), nullptr);
  native->Dispatch(1, 1, 1);
}

// A draw's pixel shader input at t0 is the display's size (the main view's; the portrait's chain is another size).
inline bool MainViewInput(ID3D11DeviceContext* native) {
  ComPtr<ID3D11ShaderResourceView> input;
  native->PSGetShaderResources(0, 1, input.GetAddressOf());
  const D3D11_TEXTURE2D_DESC desc = dlaa::internal::TextureOf(input.Get());
  const auto [width, height] = dlaa::SceneSize();
  return width != 0 && desc.Width == width && desc.Height == height;
}

// A draw's first render target is the display's size (the main view's lighting: ClusteredLighting's t0 is the 1 x 1
// exposure).
inline ComPtr<ID3D11RenderTargetView> MainViewTarget(ID3D11DeviceContext* native) {
  ComPtr<ID3D11RenderTargetView> target;
  native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  const D3D11_TEXTURE2D_DESC desc = dlaa::internal::TextureOf(target.Get());
  const auto [width, height] = dlaa::SceneSize();
  return (width != 0 && desc.Width == width && desc.Height == height) ? target : nullptr;
}

// rt_ibl.hlsli's view of `camera` at the render `size` (the guides and the reflections).
inline IblView IblViewOf(const dlaa::internal::Camera& camera, std::array<UINT, 2> size) {
  const auto& view = camera.view;
  return {
      .view_rows = {view[0], view[1], view[2], 0.f, view[4], view[5], view[6], 0.f, view[8], view[9], view[10], 0.f},
      .inverse_view_projection = camera.inverse_view_projection,
      .eye = {camera.position[0], camera.position[1], camera.position[2], 0.f},
      .size = {size[0], size[1], 0u, 0u},
  };
}

// The rays' reflections replace the game's screen-space ones this frame: Ray-Traced Reflections on and this frame traced.
inline bool ReflectionsReplace() {
  {
    const std::shared_lock lock(state.mutex);
    if (!state.settings.reflections) return false;
  }
  return twattila::rt_scene::Visibilities()[0] != nullptr;
}

// A bounce texture of `format` at the bounce's size, read by views, written by a render target (`rtv`) or unordered
// access.
inline bool MakeBounceTexture(ID3D11Device* device, std::array<UINT, 2> size, DXGI_FORMAT format, twattila::dlaa_bridge::Texture* texture,
                              ComPtr<ID3D11RenderTargetView>* rtv = nullptr) {
  *texture = {};
  const D3D11_TEXTURE2D_DESC desc = {.Width = size[0], .Height = size[1], .MipLevels = 1, .ArraySize = 1, .Format = format, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE | (rtv != nullptr ? D3D11_BIND_RENDER_TARGET : D3D11_BIND_UNORDERED_ACCESS))};
  return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture->texture.GetAddressOf()))
         && SUCCEEDED(device->CreateShaderResourceView(texture->texture.Get(), nullptr, texture->srv.GetAddressOf()))
         && (rtv != nullptr ? SUCCEEDED(device->CreateRenderTargetView(texture->texture.Get(), nullptr, rtv->ReleaseAndGetAddressOf()))
                            : SUCCEEDED(device->CreateUnorderedAccessView(texture->texture.Get(), nullptr, texture->uav.GetAddressOf())));
}

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .render_size = dlaa::SceneSize,
    .guides_written = [] { return internal::guides_written.load(std::memory_order_relaxed); },
    // The captures' clip positions go back to the world with the trace's camera (every G-buffer draw has the main view's).
    .capture_camera = [](ID3D11DeviceContext*, twattila::rt_scene::Camera*) { return true; },
    .scene_draw = internal::SceneDraw,
};

// LinearizeDepth drawn (the G-buffer complete, the depth kept by its on_draw): this frame's trace (once a frame), its sun
// from PerFrame where b13 is bound here (else the lighting draw's of the last frame).
inline void Trace(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  dlaa::internal::Camera camera;
  const ComPtr<ID3D11ShaderResourceView> depth = dlaa::SceneDepth();
  if (!depth || !dlaa::MainCamera(&camera)) return;
  const auto [width, height] = dlaa::SceneSize();
  if (camera.size[0] != width || camera.size[1] != height) return;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!internal::MakeObjects(device.Get()) || !dlaa::PrepareBridge(device.Get(), width, height)) return;
  ComPtr<ID3D11Buffer> frame_constants;
  native->PSGetConstantBuffers(13, 1, frame_constants.GetAddressOf());
  if (frame_constants) internal::WriteSun(native, frame_constants.Get());
  twattila::rt_scene::Trace(native, depth.Get(),
                            {
                                .view_projection = camera.jittered_view_projection,
                                .inverse_view_projection = camera.inverse_view_projection,
                                .position = camera.position,
                            });
}

// ClusteredLightingWithSSAO (before its draw: PerFrame at its b13, the G-buffer at t2-t4 and t6, the IBL specular at t7,
// its target the scene): the sun's direction for the next trace; with them wanted this frame's G-buffer guides (rt_guides,
// once a frame: Ray Reconstruction, the paths, the reflections); then the rays' reflections and specular occlusion into
// the IBL specular before it reads it (rt_reflect_apply, one / inverse source alpha: the game's IBL specular stays where
// the rays met nothing). The light probes and the sky have all drawn into it by then: one method whatever their order.
inline void OnLighting(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr || !internal::MainViewTarget(native)) return;
  ComPtr<ID3D11Buffer> frame_constants;
  native->PSGetConstantBuffers(13, 1, frame_constants.GetAddressOf());
  if (frame_constants) internal::WriteSun(native, frame_constants.Get());
  auto& bridge = twattila::dlaa_bridge::bridge;
  auto& state = internal::state;
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  ComPtr<ID3D11ShaderResourceView> lut;
  {
    const std::shared_lock lock(state.mutex);
    settings = state.settings;
    frame = state.frame;
    if (state.lut_frame == frame) lut = state.lut;
  }
  const bool write_guides = dlaa::guides && bridge.guides && !internal::guides_written.load(std::memory_order_relaxed);
  const bool reflect = (settings.reflections || settings.specular_occlusion) && state.reflected_frame != frame;
  dlaa::internal::Camera camera;
  if ((!write_guides && !reflect) || !lut || !dlaa::MainCamera(&camera)) return;
  // rt_ibl.hlsli's inputs: LinearDepth (t6), GBuffer1-3 (t2-t4), this frame's DeferredSky LUT.
  std::array<ComPtr<ID3D11ShaderResourceView>, 5> ibl;
  native->PSGetShaderResources(6, 1, ibl[0].GetAddressOf());
  for (UINT index = 1; index < 4; ++index) native->PSGetShaderResources(index + 1, 1, ibl[index].GetAddressOf());
  ibl[4] = lut;
  ComPtr<ID3D11DeviceContext1> native1;
  if (std::ranges::any_of(ibl, [](const auto& view) { return !view; }) || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
    return;
  }
  const auto size = dlaa::SceneSize();
  const internal::IblView view = internal::IblViewOf(camera, size);
  if (write_guides) {
    {
      const std::shared_lock lock(state.mutex);
      const auto& objects = state.objects;
      if (objects.failed || !objects.guides) return;
      twattila::dlaa_bridge::BeforeSharedWrites(native);
      const talesofarise::dlss::ContextStateScope scope(native1.Get(), objects.private_state.Get());
      native->UpdateSubresource(objects.ibl_view.Get(), 0, nullptr, &view, 0, 0);
      const std::array<ID3D11ShaderResourceView*, 5> views = {ibl[0].Get(), ibl[1].Get(), ibl[2].Get(), ibl[3].Get(), ibl[4].Get()};
      const std::array<ID3D11UnorderedAccessView*, 4> outputs = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(),
                                                                 bridge.normals.uav.Get(), bridge.roughness.uav.Get()};
      native->CSSetShader(objects.guides.Get(), nullptr, 0);
      native->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
      native->CSSetUnorderedAccessViews(0, static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
      native->CSSetConstantBuffers(0, 1, objects.ibl_view.GetAddressOf());
      native->CSSetSamplers(0, 1, objects.linear.GetAddressOf());
      native->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
    }
    internal::guides_written.store(true, std::memory_order_relaxed);
    dlaa::NoteGuides();
  }
  if (!reflect) return;
  state.reflected_frame = frame;
  // The rays' reflections and specular occlusion (rt_core: traced here once a frame, after the guides); each only with
  // its row on (rt_core's BindReflections' rule).
  const auto traced = twattila::rt_scene::Reflections(native);
  ID3D11ShaderResourceView* reflection = settings.reflections ? traced[0] : nullptr;
  ID3D11ShaderResourceView* open = settings.specular_occlusion ? traced[1] : nullptr;
  ComPtr<ID3D11ShaderResourceView> specular_view;
  native->PSGetShaderResources(7, 1, specular_view.GetAddressOf());
  ComPtr<ID3D11Resource> specular;
  if (specular_view) specular_view->GetResource(specular.GetAddressOf());
  if ((reflection == nullptr && open == nullptr) || !specular) return;
  const std::shared_lock lock(state.mutex);
  // One target view kept: the IBL specular is the same texture every frame (a new one after a resize lets the old go).
  if (state.specular != specular) {
    state.specular = specular;
    state.specular_target.Reset();
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    if (FAILED(device->CreateRenderTargetView(specular.Get(), nullptr, state.specular_target.GetAddressOf()))) {
      state.specular.Reset();
      return;
    }
  }
  const auto& objects = state.objects;
  if (objects.failed || !objects.reflect_apply) return;
  native->UpdateSubresource(objects.ibl_view.Get(), 0, nullptr, &view, 0, 0);
  const std::array<ID3D11ShaderResourceView*, 7> views = {ibl[0].Get(), ibl[1].Get(), ibl[2].Get(), ibl[3].Get(), ibl[4].Get(), reflection, open};
  internal::FullScreen(native, state.specular_target.Get(), size, objects.reflect_apply.Get(), views, objects.ibl_view.Get(), objects.reflect.Get());
}

// A sun shadow mask's draw (before it): under Ray-Traced Sun Shadows this frame's traced visibility into its target in
// place of the game's filter (true: drawn, the game's draw skipped).
inline bool ReplaceSunMask(reshade::api::command_list* cmd_list) {
  if (sliders.sun_shadows == 0.f) return false;
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return false;
  // The cascades' atlas it reads (t1), for Skip the Game's Shadow Maps from the next frame on.
  {
    ComPtr<ID3D11ShaderResourceView> atlas_view;
    native->PSGetShaderResources(1, 1, atlas_view.GetAddressOf());
    ComPtr<ID3D11Resource> atlas;
    if (atlas_view) atlas_view->GetResource(atlas.GetAddressOf());
    if (atlas && reinterpret_cast<uintptr_t>(atlas.Get()) != internal::shadow_atlas_address.load(std::memory_order_relaxed)) {
      const std::unique_lock lock(internal::atlas_mutex);
      internal::shadow_atlas = atlas;
      internal::shadow_atlas_address.store(reinterpret_cast<uintptr_t>(atlas.Get()), std::memory_order_relaxed);
    }
  }
  ID3D11ShaderResourceView* visibility = twattila::rt_scene::Visibilities()[0];
  ComPtr<ID3D11RenderTargetView> target;
  native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (visibility == nullptr || !target) return false;
  const D3D11_TEXTURE2D_DESC desc = dlaa::internal::TextureOf(target.Get());
  const auto [width, height] = dlaa::SceneSize();
  if (desc.Width != width || desc.Height != height) return false;
  internal::DrawVisibility(native, target.Get(), visibility);
  return true;
}

// This frame's traced AO in place of the game's: true while Ray-Traced AO traced this frame.
inline bool AmbientReplaced() {
  return sliders.ambient_occlusion != 0.f && dlaa::rays && twattila::rt_scene::Visibilities()[1] != nullptr;
}

// The AO chain's draws (before them): not drawn while the traced AO or the paths replace them (true: skipped), the main
// view's only.
inline bool SkipAmbientChain(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  return native != nullptr && internal::MainViewInput(native) && (AmbientReplaced() || internal::PathsBringSky());
}

// The AO chain's blur (before it): the traced visibility into its target in its place, open (1) while the paths bring the
// sky (their blocked rays are the occlusion: the game's AO off; true: drawn, the blur skipped).
inline bool ReplaceAmbientBlur(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr || !internal::MainViewInput(native)) return false;
  ComPtr<ID3D11RenderTargetView> target;
  native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (!target) return false;
  if (internal::PathsBringSky()) {
    constexpr std::array<float, 4> OPEN = {1.f, 1.f, 1.f, 1.f};
    native->ClearRenderTargetView(target.Get(), OPEN.data());
    return true;
  }
  ID3D11ShaderResourceView* visibility = twattila::rt_scene::Visibilities()[1];
  if (!AmbientReplaced() || visibility == nullptr) return false;
  internal::DrawVisibility(native, target.Get(), visibility);
  return true;
}

// An ambient pass (before its draw; AMBIENT_PASSES): the sky's the paths' sky from its distant diffuse cube (zeros without
// Path-Traced Ambient), and its diffuse cubes black while the paths bring the sky (OnAmbientPassDrawn binds its own back).
inline void OnAmbientPass(reshade::api::command_list* cmd_list, const internal::AmbientPass& pass) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr || !internal::MainViewTarget(native)) return;
  auto& bridge = twattila::dlaa_bridge::bridge;
  bool path_ambient = false;
  {
    const std::shared_lock lock(internal::state.mutex);
    path_ambient = internal::state.settings.path_ambient;
  }
  // The sky's LUT for this frame's guides (OnLighting).
  if (pass.sky) {
    ComPtr<ID3D11ShaderResourceView> lut;
    native->PSGetShaderResources(3, 1, lut.GetAddressOf());
    const std::unique_lock lock(internal::state.mutex);
    internal::state.lut = lut;
    internal::state.lut_frame = internal::state.frame;
  }
  if (pass.sky && bridge.rt_sun.uav) {
    std::array<ID3D11ShaderResourceView*, 2> views = {};
    native->PSGetShaderResources(1, 1, &views[0]);
    native->PSGetShaderResources(0, 1, &views[1]);
    ComPtr<ID3D11Buffer> globals;
    native->PSGetConstantBuffers(0, 1, globals.GetAddressOf());
    ComPtr<ID3D11DeviceContext1> native1;
    const std::shared_lock lock(internal::state.mutex);
    const auto& objects = internal::state.objects;
    if (views[0] != nullptr && views[1] != nullptr && globals && !objects.failed && objects.sky
        && SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
      const talesofarise::dlss::ContextStateScope scope(native1.Get(), objects.private_state.Get());
      const std::array<float, 4> sky_settings = {path_ambient ? 1.f : 0.f, 0.f, 0.f, 0.f};
      native->UpdateSubresource(objects.sky_settings.Get(), 0, nullptr, sky_settings.data(), 0, 0);
      const std::array<ID3D11Buffer*, 2> constants = {globals.Get(), objects.sky_settings.Get()};
      native->CSSetShader(objects.sky.Get(), nullptr, 0);
      native->CSSetShaderResources(0, 2, views.data());
      native->CSSetConstantBuffers(0, 2, constants.data());
      native->CSSetSamplers(0, 1, objects.linear.GetAddressOf());
      native->CSSetUnorderedAccessViews(0, 1, bridge.rt_sun.uav.GetAddressOf(), nullptr);
      native->Dispatch(1, 1, 1);
    }
    for (auto* view : views) {
      if (view != nullptr) view->Release();
    }
  }
  if (!path_ambient || !internal::PathsBringSky()) return;
  auto& bounce = internal::state.bounce;
  ComPtr<ID3D11ShaderResourceView> black;
  {
    const std::shared_lock lock(internal::state.mutex);
    black = internal::state.objects.black_cube;
  }
  if (!black) return;
  for (UINT index = 0; index < pass.diffuse_count; ++index) {
    native->PSGetShaderResources(pass.diffuse_slots[index], 1, bounce.kept_cubes[index].ReleaseAndGetAddressOf());
    ID3D11ShaderResourceView* view = black.Get();
    native->PSSetShaderResources(pass.diffuse_slots[index], 1, &view);
  }
  bounce.kept_pass = &pass;
}

// An ambient pass drawn: its own diffuse cubes bound again.
inline void OnAmbientPassDrawn(reshade::api::command_list* cmd_list) {
  auto& bounce = internal::state.bounce;
  if (bounce.kept_pass == nullptr) return;
  ID3D11DeviceContext* native = dlaa::internal::ImmediateContext(cmd_list);
  if (native != nullptr) {
    for (UINT index = 0; index < bounce.kept_pass->diffuse_count; ++index) {
      ID3D11ShaderResourceView* view = bounce.kept_cubes[index].Get();
      native->PSSetShaderResources(bounce.kept_pass->diffuse_slots[index], 1, &view);
    }
  }
  bounce.kept_cubes = {};
  bounce.kept_pass = nullptr;
}

// ClusteredLightingWithSSAO drawn (the scene lit): the light bounce from it (Expeditions' route: prepared, traced by the
// rays or the paths, resolved over frames) added to it, times each pixel's diffuse colour and the strength.
inline void LightBounce(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  const ComPtr<ID3D11RenderTargetView> target = internal::MainViewTarget(native);
  dlaa::internal::Camera camera;
  if (!target || !dlaa::MainCamera(&camera)) return;
  twattila::rt_scene::Settings settings;
  uint64_t frame = 0;
  {
    const std::shared_lock lock(internal::state.mutex);
    settings = internal::state.settings;
    frame = internal::state.frame;
  }
  if (!settings.light_bounce || lighting_rows.light_bounce_strength <= 0.f) return;
  // The G-buffer the lighting read (t2 GBuffer1, t3 GBuffer2, t4 GBuffer3, t6 LinearDepth).
  std::array<ComPtr<ID3D11ShaderResourceView>, 4> gbuffer;
  native->PSGetShaderResources(2, 1, gbuffer[0].GetAddressOf());
  native->PSGetShaderResources(3, 1, gbuffer[1].GetAddressOf());
  native->PSGetShaderResources(4, 1, gbuffer[2].GetAddressOf());
  native->PSGetShaderResources(6, 1, gbuffer[3].GetAddressOf());
  if (!gbuffer[0] || !gbuffer[1] || !gbuffer[2] || !gbuffer[3]) return;
  ComPtr<ID3D11Resource> scene;
  target->GetResource(scene.GetAddressOf());
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  const auto render_size = dlaa::SceneSize();
  const std::shared_lock lock(internal::state.mutex);
  const auto& objects = internal::state.objects;
  auto& bounce = internal::state.bounce;
  if (objects.failed || !objects.bounce_apply) return;
  const UINT scale = 1u << std::min(static_cast<UINT>(settings.light_bounce_resolution), 2u);
  const std::array<UINT, 2> size = {(render_size[0] + scale - 1) / scale, (render_size[1] + scale - 1) / scale};
  // The bounce's textures at the render size's and the resolution's (made again when either changes; the scene's views
  // let go with the game's old targets).
  if (bounce.size != size) {
    bounce.size = size;
    bounce.resolved_frame = UINT64_MAX;
    bounce.scene_views.clear();
    if (!internal::MakeBounceTexture(device.Get(), size, DXGI_FORMAT_R11G11B10_FLOAT, &bounce.prepared, &bounce.prepared_rtv)
        || !internal::MakeBounceTexture(device.Get(), size, DXGI_FORMAT_R16G16B16A16_FLOAT, &bounce.traced)
        || !std::ranges::all_of(bounce.resolved, [&](auto& texture) {
             return internal::MakeBounceTexture(device.Get(), size, DXGI_FORMAT_R16G16B16A16_FLOAT, &texture);
           })
        || !std::ranges::all_of(bounce.resolved_positions, [&](auto& texture) {
             return internal::MakeBounceTexture(device.Get(), size, DXGI_FORMAT_R32G32B32A32_FLOAT, &texture);
           })) {
      bounce.size = {};
      return;
    }
  }
  auto& scene_view = bounce.scene_views[scene.Get()];
  if (!scene_view && FAILED(device->CreateShaderResourceView(scene.Get(), nullptr, scene_view.GetAddressOf()))) {
    bounce.scene_views.erase(scene.Get());
    return;
  }
  // The prepare's, the resolve's and the apply's constants (the buffer's whole 96 bytes: a constant buffer is written whole).
  struct Constants {
    std::array<uint32_t, 4> sizes;
    std::array<float, 4> settings;
    std::array<float, 16> rows;
  };
  const std::array<uint32_t, 4> sizes = {size[0], size[1], render_size[0], render_size[1]};
  const Constants prepare_constants = {.sizes = sizes, .settings = {0.f, static_cast<float>(scale)}};
  native->UpdateSubresource(objects.bounce_constants.Get(), 0, nullptr, &prepare_constants, 0, 0);
  ID3D11ShaderResourceView* scene_srv = scene_view.Get();
  internal::FullScreen(native, bounce.prepared_rtv.Get(), size, objects.bounce_prepare.Get(), std::span(&scene_srv, 1), objects.bounce_constants.Get());
  if (!twattila::rt_scene::internal::TraceLightBounce(native, bounce.prepared.texture.Get(), size[0], size[1], scale, bounce.traced.texture.Get())) {
    return;
  }
  auto& bridge = twattila::dlaa_bridge::bridge;
  const uint32_t previous = bounce.resolved_index;
  const uint32_t current = previous ^ 1u;
  {
    // The resolve's: last frame's camera (rows acting on column vectors, unjittered), the sizes, its settings.
    struct ResolveConstants {
      std::array<float, 16> previous_view_projection;
      std::array<uint32_t, 4> sizes;
      std::array<float, 4> settings;
    };
    const ResolveConstants constants = {
        .previous_view_projection = bounce.previous_view_projection,
        .sizes = sizes,
        .settings = {internal::BOUNCE_FRAMES, (bounce.resolved_frame + 1 == frame) ? 1.f : 0.f, static_cast<float>(scale)},
    };
    ComPtr<ID3D11DeviceContext1> native1;
    if (FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
    const talesofarise::dlss::ContextStateScope scope(native1.Get(), objects.private_state.Get());
    native->UpdateSubresource(objects.bounce_constants.Get(), 0, nullptr, &constants, 0, 0);
    const std::array<ID3D11ShaderResourceView*, 4> views = {bounce.traced.srv.Get(), bridge.rt_positions.srv.Get(), bounce.resolved[previous].srv.Get(),
                                                            bounce.resolved_positions[previous].srv.Get()};
    const std::array<ID3D11UnorderedAccessView*, 2> outputs = {bounce.resolved[current].uav.Get(), bounce.resolved_positions[current].uav.Get()};
    native->CSSetShader(objects.bounce_resolve.Get(), nullptr, 0);
    native->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    native->CSSetUnorderedAccessViews(0, static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
    native->CSSetConstantBuffers(0, 1, objects.bounce_constants.GetAddressOf());
    native->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
  }
  bounce.resolved_index = current;
  bounce.resolved_frame = frame;
  bounce.previous_view_projection = camera.view_projection;
  const Constants apply_constants = {
      .sizes = sizes,
      .settings = {lighting_rows.light_bounce_strength, static_cast<float>(scale)},
      .rows = {camera.view[0], camera.view[1], camera.view[2], 0.f, camera.view[4], camera.view[5], camera.view[6], 0.f, camera.view[8], camera.view[9],
               camera.view[10], 0.f},
  };
  native->UpdateSubresource(objects.bounce_constants.Get(), 0, nullptr, &apply_constants, 0, 0);
  const std::array<ID3D11ShaderResourceView*, 7> views = {bounce.resolved[current].srv.Get(), bounce.resolved_positions[current].srv.Get(),
                                                          bridge.rt_positions.srv.Get(), gbuffer[0].Get(),
                                                          gbuffer[1].Get(), gbuffer[2].Get(),
                                                          gbuffer[3].Get()};
  internal::FullScreen(native, target.Get(), render_size, objects.bounce_apply.Get(), views, objects.bounce_constants.Get(), objects.add.Get());
}

// DeferredSSR's march (before it; its t0 GBuffer1): not drawn while the rays' reflections replace it this frame (Ray-Traced
// Reflections on and traced; true: skipped).
inline bool SkipReflectionMarch(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  return native != nullptr && internal::MainViewInput(native) && internal::ReflectionsReplace();
}

// DeferredSSRCombine (before it): its scene (t0) into its target as it is in its place while the rays' reflections
// replace the march (rt_copy; true: drawn, the game's skipped). One method: no screen-space reflection over the rays'.
inline bool ReplaceReflectionCombine(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr || !internal::MainViewInput(native) || !internal::ReflectionsReplace()) return false;
  const ComPtr<ID3D11RenderTargetView> target = internal::MainViewTarget(native);
  ComPtr<ID3D11ShaderResourceView> scene;
  native->PSGetShaderResources(0, 1, scene.GetAddressOf());
  if (!target || !scene) return false;
  const std::shared_lock lock(internal::state.mutex);
  const auto& objects = internal::state.objects;
  if (objects.failed || !objects.copy) return false;
  ID3D11ShaderResourceView* input = scene.Get();
  internal::FullScreen(native, target.Get(), dlaa::SceneSize(), objects.copy.Get(), std::span(&input, 1));
  return true;
}

// At the addon's start, with a row on (the scene's shaders kept from the game's start).
inline void Install() {
  internal::installed = true;
  twattila::rt_scene::InstallScene({}, HOST);
  reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  reshade::register_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  reshade::register_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(internal::OnBindTargets);
  reshade::register_event<reshade::addon_event::draw>(internal::OnCasterDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(internal::OnCasterDrawIndexed);
  reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(internal::OnCasterDrawIndirect);
}

inline void Uninstall() {
  if (!internal::installed) return;
  internal::installed = false;
  dlaa::rays = false;
  twattila::rt_scene::UninstallScene();
  reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
  reshade::unregister_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  reshade::unregister_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(internal::OnBindTargets);
  reshade::unregister_event<reshade::addon_event::draw>(internal::OnCasterDraw);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(internal::OnCasterDrawIndexed);
  reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(internal::OnCasterDrawIndirect);
}

}  // namespace divinity2::ray_tracing
