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
#include <unordered_set>

#include <embed/shaders.h>
#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../../utils/hash.hpp"
#include "../talesofarise_dlss/native_state.hpp"
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
// rays' sun. Off, or not traced this frame: the game's passes as they are. Its
// scene's shaders' code is kept from the game's start: Install only when a row is on at launch.
namespace divinity2::ray_tracing {

using Microsoft::WRL::ComPtr;

// The rows (rt_scene::AppendSettings', on the graphics page; restart after turning sun shadows or AO on).
static twattila::rt_scene::Sliders sliders;
// Skip the Game's Shadow Maps (Experimental, off): under Ray-Traced Shadows nothing is drawn into the cascades' atlas
// (R1's city: 962 of the frame's 3,526 draws); its only reader in the census by name (Shadow at t1) is the mask the rays
// replace.
static float skip_shadow_maps = 0.f;

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

struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> sun;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> write;
  ComPtr<ID3D11RasterizerState> no_cull;
  bool failed = false;
};

struct State {
  std::shared_mutex mutex;
  Objects objects;  // on the game's device
};
static State state;
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
    if (state.objects.device.Get() == native) state.objects = {};
  }
  const std::unique_lock lock(atlas_mutex);
  shadow_atlas_address.store(0, std::memory_order_relaxed);
  shadow_atlas.Reset();
}

inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
                      uint32_t, const reshade::api::rect*) {
  // The rows as Attila's (they need an upscaler), at 100%.
  const twattila::rt_scene::Settings settings = twattila::rt_scene::SettingsOf(sliders, dlaa::sliders, 0.f, 1.f);
  dlaa::rays = settings.sun_shadows || settings.ambient_occlusion;
  twattila::rt_scene::Update(settings);
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
  objects.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(0, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                               objects.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rt_frame_sun.data(), __rt_frame_sun.size(), nullptr, objects.sun.GetAddressOf()))
                   || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, objects.full_screen.GetAddressOf()))
                   || FAILED(device->CreatePixelShader(__rt_write.data(), __rt_write.size(), nullptr, objects.write.GetAddressOf()))
                   || FAILED(device->CreateRasterizerState(&raster_desc, objects.no_cull.GetAddressOf()));
  return !objects.failed;
}

inline ID3D11DeviceContext* ImmediateContext(reshade::api::command_list* cmd_list) {
  if (!dlaa::rays) return nullptr;
  return dlaa::internal::ImmediateContext(cmd_list);
}

// `visibility` (R8, 1 open) into `target` in place of what the game's draw would have written there (its red; no blend),
// in the addon's own context state (the game's put back after).
inline void DrawVisibility(ID3D11DeviceContext* native, ID3D11RenderTargetView* target, ID3D11ShaderResourceView* visibility) {
  ComPtr<ID3D11DeviceContext1> native1;
  if (FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  const D3D11_TEXTURE2D_DESC target_desc = dlaa::internal::TextureOf(target);
  const std::shared_lock lock(state.mutex);
  const auto& objects = state.objects;
  if (objects.failed || !objects.write) return;
  const talesofarise::dlss::ContextStateScope scope(native1.Get(), objects.private_state.Get());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(target_desc.Width), .Height = static_cast<float>(target_desc.Height), .MaxDepth = 1.f};
  native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  native->VSSetShader(objects.full_screen.Get(), nullptr, 0);
  native->PSSetShader(objects.write.Get(), nullptr, 0);
  native->PSSetShaderResources(0, 1, &visibility);
  native->RSSetState(objects.no_cull.Get());
  native->RSSetViewports(1, &viewport);
  native->OMSetRenderTargets(1, &target, nullptr);
  native->Draw(3, 0);
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

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .render_size = dlaa::SceneSize,
    .guides_written = [] { return false; },
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

// ClusteredLightingWithSSAO (before its draw: PerFrame at its b13): the sun's direction for the next trace.
inline void KeepSun(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr || !internal::MainViewInput(native)) return;
  ComPtr<ID3D11Buffer> frame_constants;
  native->PSGetConstantBuffers(13, 1, frame_constants.GetAddressOf());
  if (frame_constants) internal::WriteSun(native, frame_constants.Get());
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

// The AO chain's draws (before them): not drawn while the traced AO replaces them (true: skipped), the main view's only.
inline bool SkipAmbientChain(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  return native != nullptr && AmbientReplaced() && internal::MainViewInput(native);
}

// The AO chain's blur (before it): the traced visibility into its target in its place (true: drawn, the blur skipped).
inline bool ReplaceAmbientBlur(reshade::api::command_list* cmd_list) {
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr || !AmbientReplaced() || !internal::MainViewInput(native)) return false;
  ComPtr<ID3D11RenderTargetView> target;
  native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  ID3D11ShaderResourceView* visibility = twattila::rt_scene::Visibilities()[1];
  if (!target || visibility == nullptr) return false;
  internal::DrawVisibility(native, target.Get(), visibility);
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
