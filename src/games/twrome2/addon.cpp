/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <array>
#include <cmath>
#include <algorithm>
#include <shared_mutex>
#include <span>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/graphics_hooks.hpp"
#include "../twattila/ao_pass.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/engine_code.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "../twattila/ssr_hiz.hpp"
#include "../twattila/test_hooks.hpp"
#include "../twattila/water_effects.hpp"
#include "./more_tests.h"
#include "./engine_tweaks.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "./rt_particle_quads_085DDEDD_gs.h"
#include "./rt_particle_quads_18FE5E2B_gs.h"
#include "./rt_particle_quads_B5D63E8A_gs.h"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;


bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsPsychoV30Mode() {
  return shader_injection.hdr == 1.f;
}

bool IsPsychoV31Mode() {
  return shader_injection.hdr == 2.f;
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 3.f;
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, a luminance curve then the clip).
constexpr float HUE_CLIP_DEFAULT = 80.f;

bool IsModdedAutoExposureEnabled() {
  return shader_injection.auto_exposure_highlight_protection == 1.f;
}

// rendering_mode: PsychoV35 (3) in all of them (ATN 2026-11-12: Recommended in V35; Reset too, as there is no Extended
// mode), every mode's sliders at their defaults.
void ApplyHDRPreset(
    float rendering_mode,
    float video_autohdr,
    float exposure,
    float highlights,
    float shadows,
    float contrast,
    float purity_scale,
    float cone_response_exponent,
    float bloom,
    float lensflare,
    float godrays,
    float vignette,
    float distortion,
    float filmgrain,
    float some_indicators,
    float auto_exposure_highlight_protection,
    float auto_exposure_highlight_headroom_stops,
    float vfx_fire_brightness,
    float vfx_blood_splash_brightness,
    float vfx_fog_brightness,
    float vfx_snow,
    float vfx_rain,
    float hue_shift) {
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", rendering_mode},
      {"gamma_correction", 1.f},
      {"video_autohdr", video_autohdr},
      {"sky_deband", 0.f},
      {"sky_smooth", 0.f},
      {"sky_smooth_strength", 1.f},
      {"exposure", exposure},
      {"highlights", highlights},
      {"shadows", shadows},
      {"contrast", contrast},
      {"purity_scale", purity_scale},
      {"cone_response_exponent", cone_response_exponent},
      {"hue_shift", hue_shift},
      {"bloom", bloom},
      {"lensflare", lensflare},
      {"godrays", godrays},
      {"vignette", vignette},
      {"distortion", distortion},
      {"dof", 1.f},
      {"filmgrain", filmgrain},
      {"some_indicators", some_indicators},
      {"auto_exposure_highlight_protection", auto_exposure_highlight_protection},
      {"auto_exposure_highlight_headroom_stops", auto_exposure_highlight_headroom_stops},
      {"vfx_fire_brightness", vfx_fire_brightness},
      {"vfx_blood_splash_brightness", vfx_blood_splash_brightness},
      {"vfx_fog_brightness", vfx_fog_brightness},
      {"vfxsnow", vfx_snow},
      {"vfxrain", vfx_rain},
      {"v31_exposure", 1.f},
      {"v31_highlights", 1.f},
      {"v31_shadows", 1.f},
      {"v31_contrast", 1.f},
      {"v31_highlight_contrast", 1.f},
      {"v31_shadow_contrast", 1.f},
      {"v31_flare", 0.f},
      {"v31_saturation", 1.f},
      {"v31_dechroma", 0.f},
      {"v31_highlight_saturation", 1.f},
      {"v31_roll_off", 1.f},
      {"v31_gamut_compression", 1.f},
      {"v31_gamut_compression_mode", 1.f},
      {"v31_highlight_hue_shift", 0.05f},
      {"v31_highlight_saturation_shift", 0.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

#define UpgradeRTVReplaceShader(value)                                                                \
  {                                                                                                   \
      value,                                                                                          \
      {                                                                                               \
          .crc32 = value,                                                                             \
          .code = __##value,                                                                          \
          .on_draw = [](auto* cmd_list) {                                                             \
            auto rtvs = renodx::utils::swapchain::GetRenderTargets(cmd_list);                         \
            bool changed = false;                                                                     \
            for (auto rtv : rtvs) {                                                                   \
              changed = renodx::mods::swapchain::ActivateCloneHotSwap(cmd_list->get_device(), rtv);   \
            }                                                                                         \
            if (changed) {                                                                            \
              renodx::mods::swapchain::FlushDescriptors(cmd_list);                                    \
              renodx::mods::swapchain::RewriteRenderTargets(cmd_list, rtvs.size(), rtvs.data(), {0}); \
            }                                                                                         \
            return true; },                                                                           \
          .on_drawn = [](auto* cmd_list) {                                                            \
            twattila::screen_space_shadows::OnTonemapDrawn(cmd_list);                                 \
            twattila::anti_aliasing::OnTonemapDrawn(cmd_list);                                        \
            twattila::rt_scene::OnTonemapDrawn(cmd_list); },                                          \
      },                                                                                              \
  }

// The point lights without the game's cube shadow maps, the only ones drawn in ATN's runs (A1, A2, C1, N1, L1, L2, S1: 802 and
// 784 draws): pointlightsmoothfalloff ps30 slm_highdef and skinlm_highdef (point shadows hardware gather 5.0). The two with the
// maps (3B5C9DAF, D2F190A8) keep them. Ours (shaders/pointlight00-01: the game's code x the light's traced visibility) draw
// only where the rays are bound (OnLightShadowsDraw): 7CB7BB8A's copy compiles up to 0.12% apart from the game's bytes (WARP
// tools/tw/rome2/lights_r3), so without the rays the game's own.
constexpr std::array<uint32_t, 2> POINT_LIGHT_SHADERS = {0xEB8E5349, 0x7CB7BB8A};
constexpr std::array<std::span<const uint8_t>, 2> RAY_TRACED_LIGHT_SHADERS = {__0xEB8E5349, __0x7CB7BB8A};

// Smoke Shadows' particles (Experimental, off): each smoke effect has its own generated vertex and pixel shader (drawn only
// together) and draws each particle as an instance of a quad (6 indices). In ATN's runs (proof level A):
// impact_hit_generic_heavy 7DF7491E / 18FE5E2B (shaders/smokefromarrow00), fire_smoke_loop and campaign_fire_smoke_loop
// AF6C187A / 085DDEDD, destruction_spawnmesh_heavy B6F63295 / B5D63E8A (explosionsmoke00); one rt_particle_quads build a
// pair, with its pixel shader's alpha (WARP tools/tw/rome2/smoke_r3). The permutations of other shadow modes, 31C72F52 /
// 7115DE09 (18FE5E2B's) and 803E1461 / CC3CB1B7 (B5D63E8A's), in no trace: the same slots and alpha (B, and on WARP). Fire
// (explosion_heavy), the units' dust (terrain_impact, entity_terrain), trails and torches stay out.
constexpr std::array<twattila::rt_scene::SmokeVertexShader, 3> SMOKE_VERTEX_SHADERS = {{
    {.hash = 0x7DF7491E, .quad_shader = __rt_particle_quads_18FE5E2B},
    {.hash = 0xAF6C187A, .quad_shader = __rt_particle_quads_085DDEDD},
    {.hash = 0xB6F63295, .quad_shader = __rt_particle_quads_B5D63E8A},
}};
constexpr std::array<twattila::rt_scene::SmokePixelShader, 7> SMOKE_PIXEL_SHADERS = {
    {{.hash = 0x18FE5E2B}, {.hash = 0x31C72F52}, {.hash = 0x7115DE09}, {.hash = 0x085DDEDD}, {.hash = 0xB5D63E8A}, {.hash = 0x803E1461}, {.hash = 0xCC3CB1B7}}};

// Torch and Fire Shadows: in a light draw where rt_scene's light hook (InstallPointLights, registered before this) bound this
// frame's lit bits at t42, ours draws in the game's place and the game's is bound again after it (finalize).
struct LightShadows {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // ours' (compared only)
  std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>, 2> ours;  // RAY_TRACED_LIGHT_SHADERS'
  Microsoft::WRL::ComPtr<ID3D11PixelShader> game_shader;          // bound again after the draw
  bool logged = false;
};
LightShadows light_shadows;
constexpr auto OnLightShadowsDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  const auto light = std::ranges::find(POINT_LIGHT_SHADERS, command.matched_shader_hash);
  if (light == POINT_LIGHT_SHADERS.end()) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lit_bits;
  native->PSGetShaderResources(42, 1, lit_bits.GetAddressOf());
  if (!lit_bits) return {};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = light_shadows;
  const std::unique_lock lock(state.mutex);
  if (state.device != device.Get()) {
    state.device = nullptr;
    for (size_t index = 0; index < state.ours.size(); ++index) {
      state.ours[index].Reset();
      if (FAILED(device->CreatePixelShader(RAY_TRACED_LIGHT_SHADERS[index].data(), RAY_TRACED_LIGHT_SHADERS[index].size(), nullptr,
                                           state.ours[index].GetAddressOf()))) {
        return {};
      }
    }
    state.device = device.Get();
  }
  native->PSGetShader(state.game_shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
  native->PSSetShader(state.ours[light - POINT_LIGHT_SHADERS.begin()].Get(), nullptr, 0);
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: Torch and Fire Shadows: a light drawn with the rays.");
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    auto& drawn_state = light_shadows;
    const std::unique_lock drawn_lock(drawn_state.mutex);
    if (drawn_native != nullptr) {
      drawn_native->PSSetShader(drawn_state.game_shader.Get(), nullptr, 0);
    }
    drawn_state.game_shader.Reset();
  }};
};

// Only our shaders' device (nullptr: the addon's end, DetachAddon).
void OnDestroyLightShadowsDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = light_shadows;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  for (auto& shader : state.ours) shader.Reset();
  state.device = nullptr;
}

// Ray-Traced Water Reflections (Experimental: the Reflections method Ray-Traced, off by default). The water is drawn forward
// after its reflections 7C9C8ECF (helpers screen_space_reflections_ps, below the render size: the scene copy t0, the depth
// before the water t1, the water's height map t2 / s2, b0 its camera and heights) and has no G-buffer of its own. Before
// 7C9C8ECF, rt_water_surface puts the water where 7C9C8ECF finds it (each render pixel's view ray against the height map)
// into a copy of the depth and writes the water's guides there; rt_scene's Reflections trace from that copy (the positions
// again; rt_host's rewrite_guides has nothing left to do) and ours (shaders/waterreflection00) returns the rays' reflection
// where they are bound and left from the water (b10). This frame's water, as the game's own reflections take it.
struct WaterRays {
  std::mutex mutex;
  ID3D11Device* device = nullptr;  // the objects' (compared only)
  Microsoft::WRL::ComPtr<ID3D11ComputeShader> surface;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;  // R32, the game's depth's size
  Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> depth_uav;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth_view;
  bool logged = false;

  void Release() {
    surface.Reset();
    constants.Reset();
    depth_view.Reset();
    depth_uav.Reset();
    depth.Reset();
    device = nullptr;
  }
};
WaterRays water_rays;

// Before 7C9C8ECF. True: the rays left from the water this frame (ours draws them: no march).
bool TraceWaterReflections(reshade::api::command_list* cmd_list) {
  auto& rays = twattila::rt_scene::internal::runtime;
  float water_roughness = 0.f;
  {
    const std::unique_lock lock(rays.mutex);
    if (!rays.settings.reflections || !rays.settings.water_reflections) return false;
    water_roughness = rays.settings.water_soft ? twattila::rt_scene::WATER_SOFT_ROUGHNESS : 0.f;
  }
  if (!twattila::rt_scene::internal::active.load(std::memory_order_relaxed) || !twattila::anti_aliasing::GuidesWritten()
      || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) {
    return false;
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
  const auto [width, height] = twattila::anti_aliasing::RenderSize();
  std::array<ID3D11ShaderResourceView*, 2> inputs = {};  // the depth before the water, the height map
  native->PSGetShaderResources(1, static_cast<UINT>(inputs.size()), inputs.data());
  Microsoft::WRL::ComPtr<ID3D11SamplerState> height_sampler;
  native->PSGetSamplers(2, 1, height_sampler.GetAddressOf());
  Microsoft::WRL::ComPtr<ID3D11Buffer> params;
  native->PSGetConstantBuffers(0, 1, params.GetAddressOf());
  D3D11_TEXTURE2D_DESC depth_desc = {};
  if (Microsoft::WRL::ComPtr<ID3D11Resource> resource; inputs[0] != nullptr) {
    inputs[0]->GetResource(resource.GetAddressOf());
    if (Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource.As(&texture))) texture->GetDesc(&depth_desc);
  }
  const auto release = [](auto* object) {
    if (object != nullptr) object->Release();
  };
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> surface_depth;
  auto& state = water_rays;
  if (inputs[1] != nullptr && height_sampler && params && depth_desc.SampleDesc.Count == 1 && width != 0 && height != 0
      && width <= depth_desc.Width && height <= depth_desc.Height) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    const std::unique_lock lock(state.mutex);
    D3D11_TEXTURE2D_DESC kept_desc = {};
    if (state.depth) state.depth->GetDesc(&kept_desc);
    if (state.device != device.Get() || kept_desc.Width != depth_desc.Width || kept_desc.Height != depth_desc.Height) {
      state.Release();
      const D3D11_TEXTURE2D_DESC desc = {.Width = depth_desc.Width, .Height = depth_desc.Height, .MipLevels = 1, .ArraySize = 1,
                                         .Format = DXGI_FORMAT_R32_TYPELESS, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE};
      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_R32_FLOAT, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D};
      view_desc.Texture2D.MipLevels = 1;
      const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      if (SUCCEEDED(device->CreateComputeShader(__rt_water_surface.data(), __rt_water_surface.size(), nullptr, state.surface.GetAddressOf()))
          && SUCCEEDED(device->CreateBuffer(&constants_desc, nullptr, state.constants.GetAddressOf()))
          && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, state.depth.GetAddressOf()))
          && SUCCEEDED(device->CreateUnorderedAccessView(state.depth.Get(), &uav_desc, state.depth_uav.GetAddressOf()))
          && SUCCEEDED(device->CreateShaderResourceView(state.depth.Get(), &view_desc, state.depth_view.GetAddressOf()))) {
        state.device = device.Get();
      }
    }
    if (state.device == device.Get()) {
      const std::array<uint32_t, 4> constants = {width, height, std::bit_cast<uint32_t>(water_roughness), 0u};
      native->UpdateSubresource(state.constants.Get(), 0, nullptr, constants.data(), 0, 0);
      // The compute bindings it changes put back (the game's next dispatch may count on them).
      Microsoft::WRL::ComPtr<ID3D11ComputeShader> game_shader;
      std::array<ID3D11ShaderResourceView*, 2> game_views = {};
      std::array<ID3D11UnorderedAccessView*, 5> game_uavs = {};
      std::array<ID3D11Buffer*, 2> game_constants = {};
      std::array<ID3D11SamplerState*, 1> game_sampler = {};
      native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
      native->CSGetShaderResources(0, static_cast<UINT>(game_views.size()), game_views.data());
      native->CSGetUnorderedAccessViews(0, static_cast<UINT>(game_uavs.size()), game_uavs.data());
      native->CSGetConstantBuffers(0, static_cast<UINT>(game_constants.size()), game_constants.data());
      native->CSGetSamplers(0, 1, game_sampler.data());
      const auto& bridge = twattila::dlaa_bridge::bridge;
      const std::array<ID3D11UnorderedAccessView*, 5> uavs = {state.depth_uav.Get(), bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(),
                                                              bridge.normals.uav.Get(), bridge.roughness.uav.Get()};
      const std::array<ID3D11Buffer*, 2> constant_buffers = {params.Get(), state.constants.Get()};
      constexpr std::array<UINT, 5> KEEP_COUNTS = {UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX};
      native->CSSetShader(state.surface.Get(), nullptr, 0);
      native->CSSetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.data());
      native->CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data(), KEEP_COUNTS.data());
      native->CSSetConstantBuffers(0, static_cast<UINT>(constant_buffers.size()), constant_buffers.data());
      native->CSSetSamplers(0, 1, height_sampler.GetAddressOf());
      native->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
      native->CSSetShader(game_shader.Get(), nullptr, 0);
      native->CSSetUnorderedAccessViews(0, static_cast<UINT>(game_uavs.size()), game_uavs.data(), KEEP_COUNTS.data());
      native->CSSetShaderResources(0, static_cast<UINT>(game_views.size()), game_views.data());
      native->CSSetConstantBuffers(0, static_cast<UINT>(game_constants.size()), game_constants.data());
      native->CSSetSamplers(0, 1, game_sampler.data());
      std::ranges::for_each(game_views, release);
      std::ranges::for_each(game_uavs, release);
      std::ranges::for_each(game_constants, release);
      std::ranges::for_each(game_sampler, release);
      surface_depth = state.depth_view;
    }
  }
  std::ranges::for_each(inputs, release);
  if (!surface_depth) return false;
  const bool reflected = twattila::rt_scene::BindReflections(cmd_list, false, true, surface_depth.Get());
  bool water_traced = false;
  {
    const std::unique_lock lock(rays.mutex);
    water_traced = rays.water_traced;
  }
  if (!(reflected && water_traced)) return false;
  const std::unique_lock lock(state.mutex);
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: Ray-Traced Water Reflections: the water's surface from its height map, 7C9C8ECF drawn with the rays.");
  }
  return true;
}

// Only our objects' device (nullptr: the addon's end, DetachAddon).
void OnDestroyWaterRaysDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = water_rays;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.Release();
}

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      UpgradeRTVReplaceShader(0xEE1DA45C),
      UpgradeRTVReplaceShader(0xA674585B),  // dof00 (DOF High): 16-bit target
      UpgradeRTVReplaceShader(0x850AEE44),
      UpgradeRTVReplaceShader(0x56DE55F3),
      UpgradeRTVReplaceShader(0x9BA371A3),
      UpgradeRTVReplaceShader(0x2C229891),
      UpgradeRTVReplaceShader(0xCB9D771B),
      UpgradeRTVReplaceShader(0x129A6955),
      UpgradeRTVReplaceShader(0x47B6D580),
      UpgradeRTVReplaceShader(0x200D77F2),
      UpgradeRTVReplaceShader(0x32F67EF7),  // t09 constant tone mapper with bloom
      UpgradeRTVReplaceShader(0x682B15ED),  // t10 constant tone mapper
      UpgradeRTVReplaceShader(0x70E194B5),  // t11-t14 hdr_to_screen, low post-processing quality
      UpgradeRTVReplaceShader(0xB39E2228),
      UpgradeRTVReplaceShader(0x3281E5FF),
      UpgradeRTVReplaceShader(0x9A5D3C0B),
      UpgradeRTVReplaceShader(0x25BD6591),  // dof03 (dof00 without distortion): 16-bit target
      UpgradeRTVReplaceShader(0xE0885F7F),  // dof04 / dof05 pixel shader DOF blurs
      UpgradeRTVReplaceShader(0x30F36F29),
      __ALL_CUSTOM_SHADERS,
  };
  // Torch and Fire Shadows' light shaders stay the game's: ours only where the rays are bound (OnLightShadowsDraw).
  for (const uint32_t hash : POINT_LIGHT_SHADERS) {
    custom_shaders.erase(hash);
  }
  // The depth of field passes (16-bit targets, before the tone mapper) are not the tone mapper: anti-aliasing / DLSS
  // and the screen-space shadows' frame end come after the tone mapper's draw only.
  for (const uint32_t depth_of_field : {0xA674585Bu, 0x25BD6591u, 0xE0885F7Fu, 0x30F36F29u}) {
    custom_shaders[depth_of_field].on_drawn = nullptr;
  }
  // The water's reflections (waterreflection00): the rays' where they left from the water (TraceWaterReflections), else the
  // hierarchical march's pyramid from its depth (t1).
  custom_shaders[0x7C9C8ECF].on_draw = [](auto* cmd_list) { return twattila::ssr_hiz::OnReflectionDraw(cmd_list, 1, !TraceWaterReflections(cmd_list)); };
  custom_shaders[0x7C9C8ECF].on_drawn = [](auto* cmd_list) {
    twattila::ssr_hiz::OnReflectionDrawn(cmd_list);
    twattila::rt_scene::BindReflections(cmd_list, false, false);
  };
  // Lens flares 00-03 and the vignette, drawn on the output after hdr_to_screen and its anti-aliasing (A2 trace_06
  // l.3315-3319): part of frame generation's HUD-less image, as Attila's.
  for (const uint32_t hash : {0xAFC2E867u, 0x68E9A1CEu, 0x69F4AEFCu, 0xB7758434u, 0x3968DBAEu}) {
    custom_shaders[hash].on_drawn = twattila::anti_aliasing::OnOutputEffectDrawn;
  }

  // Settings ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // PsychoV31 sliders: 0 - max, 1 = its own.
  auto v31_slider = [](const std::string& key, float* binding, const std::string& label, const std::string& tooltip,
                       float max = 2.f, float default_value = 1.f) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = label,
        .section = "PsychoV31",
        .tooltip = tooltip,
        .max = max,
        .format = "%.2f",
        .is_enabled = IsPsychoV31Mode,
        .is_visible = IsPsychoV31Mode,
    };
  };

  settings = {
    new renodx::utils::settings::Setting{
        .key = "SettingsMode",
        .binding = &current_settings_mode,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .can_reset = false,
        .label = "Settings Mode",
        .labels = {"HDR & Effects", "Graphics 1 of 2", "Graphics 2 of 2"},
        .is_global = true,
    },
    twattila::graphics_page::Note(twattila::graphics_page::FULL_DOWNLOAD_NOTE, "General", twattila::graphics_page::WARNING_TINT,
                                  [] { return !twattila::dlaa_bridge::HelperInstalled(); }),
    // Gamma Correction //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BULLET,
    //     .label = "Correct Windows' washed out gamma decode.",
    //     .section = "Gamma Correction",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "gamma_correction",
    //     .binding = &shader_injection.gamma_correction,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "Enable Correction",
    //     .section = "Gamma Correction",
    //     .tooltip = "Emulate Gamma 2.2 EOTF.\n(Attached to Scene Paper White.)",
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "(Google Slides) Further Explanation & Test",
    //     .section = "Gamma Correction",
    //     .group = "button-line-1",
    //     .on_change = []() {
    //       renodx::utils::platform::LaunchURL("https://docs.google.com/presentation/d/e/2PACX-1vSXeLHlbm6repcS7fels1-SXYGRmzziRrnuJ8nDO8J5rsWV3dT1-nVyCKp0Tj_stwx-9qlCI-N6rYIT/pub?start=false&loop=false&slide=id.g3e007eafba8_0_0");
    //     },
    // },
    // Peak //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "peak_test",
    //     .binding = &shader_injection.peak_test,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 0.f,
    //     .label = "Calibration",
    //     .section = "Peak",
    //     .tooltip = "3 rectangles within a large one.\n\n- Left: Not Visible.\n- Middle: Barely Visible.\n- Right: Easily Visible.\n\n(Ofc, you can always set lower to personal pref.)",
    // },
    
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "Use these in-game settings: Gamma 22 and Brightness 10.",
        .section = "General",
        .tint = 0xFFD23C,
    },
    new renodx::utils::settings::Setting{
        .key = "peak_white_nits",
        .binding = &shader_injection.peak_white_nits,
        .default_value = 400.f,
        .can_reset = false,
        .label = "Peak",
        .section = "General",
        .tooltip = "Maximum brightness output in nits.",
        .min = 400.f,
        .max = 10000.f,
        // .is_enabled = IsHDRMode,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    // new renodx::utils::settings::Setting{
    //     .key = "whiteclip",
    //     .binding = &shader_injection.whiteclip,
    //     .default_value = 1.f,
    //     .label = "Clip",
    //     .section = "Peak",
    //     .tooltip = "Raise to increase clipping in highlights.",
    //     .min = 0.1f,
    //     .max = 2.f,
    //     .format = "%.2f",
    // },
    // Paper White //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .key = "diffuse_white_nits",
        .binding = &shader_injection.diffuse_white_nits,
        .default_value = 203.f,
        .label = "Paper White",
        .section = "General",
        .tooltip = "Brightness of the scene/game.",
        .min = 1.f,
        .max = 500.f,
        // .is_enabled = IsHDRMode,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "graphics_white_nits",
        .binding = &shader_injection.graphics_white_nits,
        .default_value = 203.f,
        .label = "UI",
        .section = "General",
        .tooltip = "Brightness of UI and HUD in nits.",
        .min = 1.f,
        .max = 500.f,
        // .is_enabled = IsHDRMode,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "tonemapper",
        .binding = &shader_injection.hdr,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Rendering Mode",
        .section = "General",
        .tooltip = "SDR: the game's SDR.\n"
                   "HDR (PsychoV30): the HDR pipeline with PsychoV30 (default).\n"
                   "HDR (PsychoV31): the HDR pipeline with PsychoV31, more grading sliders.\n"
                   "HDR (PsychoV35): the HDR pipeline with Musa's PsychoV35 and Hue Clip Emulation (the Recommended presets).",
        .labels = {"SDR", "HDR (PsychoV30)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
    },
    new renodx::utils::settings::Setting{
        .key = "gamma_correction",
        .binding = &shader_injection.gamma_correction,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Gamma Correction",
        .section = "General",
        .tooltip = "Rendering Mode SDR: any in-game Gamma shown as gamma 2.2.\n"
                   "Off: the game's own picture, at the in-game Gamma.",
        .labels = {"Off (In-game Gamma value)", "On (Gamma 2.2)"},
        .is_visible = [] { return !IsHDRMode(); },
    },
      new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "My settings for 2500nits peak",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Settings, I would use with a 2500nits screen.",
        .on_change = []() {
          ApplyHDRPreset(
              3.f, 0.2f,  // rendering mode, video AutoHDR
              1.0f,  // exposure
              1.0f,  // highlights
              1.0f,  // shadows
              1.0f,  // contrast
              1.0f,  // purity_scale
              1.0f,  // cone_response_exponent
              1.0f,  // bloom
              1.0f,  // lenslare
              1.0f,  // godrays
              1.0f,  // vignette
              1.0f,  // distortion
              0.0f,  // filmgrain
              1.0f,  // some_indicators
              1.0f,  // auto_exposure_highlight_protection
              0.4f,  // auto_exposure_highlight_headroom_stops
              6.0f,  // vfx_fire_brightness
              6.0f,  // vfx_blood_splash_brightness
              0.7f,  // vfx_fog_brightness
              1.5f,  // vfxsnow
              1.5f,  // vfxrain
              2.0f   // hue_shift
          );
        },
    },
        new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "My settings for 1000nits peak",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Settings, I would use with a 1000nits screen.",
        .on_change = []() {
          ApplyHDRPreset(
              3.f, 0.2f,  // rendering mode, video AutoHDR
              1.0f,  // exposure
              1.0f,  // highlights
              1.0f,  // shadows
              1.0f,  // contrast
              1.0f,  // purity_scale
              1.0f,  // cone_response_exponent
              1.0f,  // bloom
              1.0f, //lensflare
              1.0f,  // godrays
              1.0f,  // vignette
              1.0f,  // distortion
              0.0f,  // filmgrain
              1.0f,  // some_indicators
              1.0f,  // auto_exposure_highlight_protection
              0.4f,  // auto_exposure_highlight_headroom_stops
              2.5f,  // vfx_fire_brightness
              3.0f,  // vfx_blood_splash_brightness
              0.7f,  // vfx_fog_brightness
              1.3f,  // vfxsnow
              1.3f,  // vfxrain
              2.0f   // hue_shift
          );
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Resets all image tuning and VFX sliders to their default HDR values.",
        .on_change = []() {
          ApplyHDRPreset(
              3.f, 0.f,  // rendering mode (no Extended mode here: PsychoV35, ATN), video AutoHDR
              1.0f,  // exposure
              1.0f,  // highlights
              1.0f,  // shadows
              1.0f,  // contrast
              1.0f,  // purity_scale
              1.0f,  // cone_response_exponent
              1.0f,  // bloom
              1.0f, //lensflare
              1.0f,  // godrays
              1.0f,  // vignette
              1.0f,  // distortion
              1.0f,  // filmgrain
              1.0f,  // some_indicators
              0.0f,  // auto_exposure_highlight_protection
              1.0f,  // auto_exposure_highlight_headroom_stops
              1.0f,  // vfx_fire_brightness
              1.0f,  // vfx_blood_splash_brightness
              1.0f,  // vfx_fog_brightness
              1.0f,  // vfxsnow
              1.0f,  // vfxrain
              1.0f   // hue_shift
          );
        },
    },
    // Extra //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "dof",
    //     .binding = &shader_injection.dof,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "Depth of Field",
    //     .section = "Extra",
    //     .tooltip = "Toggle depth of field effect.",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "uvdistort",
    //     .binding = &shader_injection.uvdistort,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "Lens Distortion",
    //     .section = "Extra",
    //     .tooltip = "Toggle lens distortion on color.",
    // },

    // new renodx::utils::settings::Setting{
    //     .key = "filmgrain",
    //     .binding = &shader_injection.filmgrain,
    //     .default_value = 1.f,
    //     .label = "Film Grain",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on film grain effect.",
    //     .min = 0.f,
    //     .max = 1.f,
    //     .format = "%.2f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "lut",
    //     .binding = &shader_injection.lut,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "LUT (Debug)",
    //     .section = "Extra",
    //     .tooltip = "Toggle color grading via LUT.",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "ui",
    //     .binding = &shader_injection.ui,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 1.f,
    //     .label = "UI (Debug)",
    //     .section = "Extra",
    //     .tooltip = "Toggle UI elements, enough for screenshot in pause menu.",
    // },

    // FPS Limit //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "fps_limit",
    //     .binding = &renodx::utils::swapchain::fps_limit,
    //     .default_value = 0.f,
    //     .can_reset = true,
    //     .label = "Value",
    //     .section = "FPS Limit",
    //     .min = 0.f,
    //     .max = 240.f,
    // },
      new renodx::utils::settings::Setting{
        .key = "exposure",
        .binding = &shader_injection.exposure,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Exposure",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsPsychoV30Mode,
    },
    new renodx::utils::settings::Setting{
        .key = "highlights",
        .binding = &shader_injection.highlights,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Highlights",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsPsychoV30Mode,
    },
    new renodx::utils::settings::Setting{
        .key = "shadows",
        .binding = &shader_injection.shadows,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Shadows",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsPsychoV30Mode,
    },
    new renodx::utils::settings::Setting{
        .key = "contrast",
        .binding = &shader_injection.contrast,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Contrast",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsPsychoV30Mode,
    },
    new renodx::utils::settings::Setting{
        .key = "purity_scale",
        .binding = &shader_injection.saturation,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Saturation / Purity",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsPsychoV30Mode,
    },
    new renodx::utils::settings::Setting{
        .key = "cone_response_exponent",
        .binding = &shader_injection.cone_response_exponent,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Cone Response Exponent",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsPsychoV30Mode,
    },
    new renodx::utils::settings::Setting{
        .key = "hue_shift",
        .binding = &shader_injection.hue_shift,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Highlight Hue Shift",
        .section = "Psycho V30",
        .tooltip = "Hue shift of bright highlights, like SDR: fire and explosions turn orange/yellow instead of pink/red.\n0 = none, 1 = PsychoV30 default (like my old addons), 2 = full.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsPsychoV30Mode,
    },

    // PsychoV31 ///////////////////////////////////////////////////////////////////////////////////
    v31_slider("v31_exposure", &shader_injection.v31_exposure, "Exposure", ""),
    v31_slider("v31_highlights", &shader_injection.v31_highlights, "Highlights", ""),
    v31_slider("v31_shadows", &shader_injection.v31_shadows, "Shadows", ""),
    v31_slider("v31_contrast", &shader_injection.v31_contrast, "Contrast", "1 = PsychoV31's own."),
    v31_slider("v31_highlight_contrast", &shader_injection.v31_highlight_contrast, "Highlight Contrast",
               "Contrast above mid gray. 1 = PsychoV31's own."),
    v31_slider("v31_shadow_contrast", &shader_injection.v31_shadow_contrast, "Shadow Contrast",
               "Contrast below mid gray. 1 = PsychoV31's own."),
    new renodx::utils::settings::Setting{
        .key = "v31_flare",
        .binding = &shader_injection.v31_flare,
        .default_value = 0.f,
        .label = "Flare",
        .section = "PsychoV31",
        .tooltip = "Deepens the darkest shadows. 0 = off.",
        .max = 1.f,
        .format = "%.2f",
        .is_enabled = IsPsychoV31Mode,
        .parse = [](float value) { return std::pow(value, 5.f); },
        .is_visible = IsPsychoV31Mode,
    },
    v31_slider("v31_saturation", &shader_injection.v31_saturation, "Saturation / Purity", "1 = PsychoV31's own."),
    v31_slider("v31_dechroma", &shader_injection.v31_dechroma, "Dechroma",
               "Very bright colors fade toward white, as if overexposed. 0 = off.", 1.f, 0.f),
    v31_slider("v31_roll_off", &shader_injection.v31_roll_off, "Highlight Roll-off",
               "How soon highlights start to compress toward Peak. 0 = latest, 1 = default, 2 = soonest."),
    v31_slider("v31_gamut_compression", &shader_injection.v31_gamut_compression, "Gamut Compression",
               "Pulls colors your display can't show back inside its color space.\n"
               "0 = off (the display clips them), 1 = full (default).",
               1.f),
    new renodx::utils::settings::Setting{
        .key = "v31_gamut_compression_mode",
        .binding = &shader_injection.v31_gamut_compression_mode,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Gamut Compression Mode",
        .section = "PsychoV31",
        .tooltip = "The color space colors are fitted into.\n"
                   "BT.2020: the whole HDR signal (default).\n"
                   "Display P3: what most HDR monitors and TVs show. BT.709: SDR colors.",
        .labels = {"BT.709", "BT.2020", "Display P3"},
        .is_enabled = IsPsychoV31Mode,
        // PsychoV31's modes: 0 BT.709, 1 BT.2020, 3 Display P3.
        .parse = [](float value) { return (value == 2.f ? 3.f : value); },
        .is_visible = IsPsychoV31Mode,
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "Fire too pink, too orange or too pale? Change the settings below.",
        .section = "PsychoV31",
        .is_visible = IsPsychoV31Mode,
    },
    v31_slider("v31_highlight_saturation", &shader_injection.v31_highlight_saturation, "Highlight Saturation",
               "Saturation of bright colors. 0 = less, 1 = PsychoV31's own, 2 = more."),
    v31_slider("v31_highlight_hue_shift", &shader_injection.v31_highlight_hue_shift, "Highlight Hue Shift",
               "Bright colors take the SDR image's hues: fire more orange / yellow, less pink.\n"
               "0 = off, 1 = SDR hues. Default 0.05.",
               1.f, 0.05f),
    v31_slider("v31_highlight_saturation_shift", &shader_injection.v31_highlight_saturation_shift,
               "Highlight Saturation Shift",
               "Gives bright colors the saturation of the SDR image (fire cores less pale and pink).\n"
               "0 = off (default), 1 = SDR's.",
               1.f, 0.f),

    ///EXTRA-----////

    new renodx::utils::settings::Setting{
        .key = "bloom",
        .binding = &shader_injection.bloom,
        .default_value = 1.f,
        .label = "Bloom",
        .section = "Extra",
        .tooltip = "Bloom multiplier.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "lensflare",
        .binding = &shader_injection.lensflare,
        .default_value = 1.f,
        .label = "Lensflare",
        .section = "Extra",
        .tooltip = "Multiplier on Lenslfare. ",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "godrays",
        .binding = &shader_injection.godrays,
        .default_value = 1.f,
        .label = "Godrays",
        .section = "Extra",
        .tooltip = "Multiplier on godrays.  Needs in-game Sun Rays setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "vignette",
        .binding = &shader_injection.vignette,
        .default_value = 1.f,
        .label = "Vignette",
        .section = "Extra",
        .tooltip = "Multiplier on vignette mask. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "dof",
        .binding = &shader_injection.dof,
        .default_value = 1.f,
        .label = "Depth of Field",
        .section = "Extra",
        .tooltip = "Multiplier on depth of field blur. Needs in-game Depth of Field on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "distortion",
        .binding = &shader_injection.uvdistort,
        .default_value = 1.f,
        .label = "Distortion",
        .section = "Extra",
        .tooltip = "Multiplier on Distortion. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "filmgrain",
        .binding = &shader_injection.filmgrain,
        .default_value = 1.f,
        .label = "Filmgrain",
        .section = "Extra",
        .tooltip = "Multiplier on filmgrain. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "some_indicators",
        .binding = &shader_injection.someindicators,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Brightness of some Unit/Building Indicator ",
        .section = "Extra",
        .tooltip = "Brightness of selected highlight indicators, particularly useful at night.",
        .min = 0.1f,
        .max = 2.0f,
        .format = "%.1f",
    },
    new renodx::utils::settings::Setting{
        .key = "video_autohdr",
        .binding = &shader_injection.video_autohdr,
        .default_value = 0.f,
        .label = "Video AutoHDR",
        .section = "Extra",
        .tooltip = "Makes the bright parts of the game's (SDR) videos brighter: only what is above mid gray,\n"
                   "the brightest the most. 0 = off, 1 = video white reaches Peak.\n"
                   "High values can show banding (see Video Dithering).",
        .min = 0.f,
        .max = 1.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "video_dithering",
        .binding = &shader_injection.video_dithering,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Video Dithering",
        .section = "Extra",
        .tooltip = "Fine noise that hides banding in the brightened videos.\n"
                   "Only with Video AutoHDR above 0.",
        .labels = {"Off", "On"},
        .is_enabled = [] { return IsHDRMode() && shader_injection.video_autohdr > 0.f; },
    },
    


    // Auto Exposure //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .key = "auto_exposure_highlight_protection",
        .binding = &shader_injection.auto_exposure_highlight_protection,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.0f,
        .label = "Modded Auto Exposure",
        .section = "Extra (Experimental)",
        .tooltip = "Reduces the exposure influence of isolated highlights such as the sun, fire, and sparks without reducing their rendered brightness. Off is stock.",
        .labels = {"Off (Stock)", "On (Protected)"},
    },
    new renodx::utils::settings::Setting{
        .key = "auto_exposure_highlight_headroom_stops",
        .binding = &shader_injection.auto_exposure_highlight_headroom_stops,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Modded Auto Exposure: Sensitivity",
        .section = "Extra (Experimental)",
        .tooltip = "Maximum metered highlight above the frame average, in stops. Lower values protect exposure more strongly. Higher values are closer to stock.",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.1f",
        .is_enabled = IsModdedAutoExposureEnabled,
    },
    new renodx::utils::settings::Setting{
        .key = "sky_smooth_strength",
        .binding = &shader_injection.sky_smooth_strength,
        .default_value = 1.f,
        .label = "Fog & Sky Smoothing Strength",
        .section = "Extra (Experimental)",
        .tooltip = "How wide Fog & Sky Smoothing below reaches. 1 = default.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = [] { return shader_injection.sky_smooth != 0.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "sky_smooth",
        .binding = &shader_injection.sky_smooth,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Fog & Sky Smoothing",
        .section = "Extra (Experimental)",
        .tooltip = "Removes the vertical lines and blocks the game's low-resolution sky leaves in thick fog.\n"
                   "Higher: smoother, a little heavier.",
        .labels = {"Off", "Low", "Medium", "High"},
    },
    new renodx::utils::settings::Setting{
        .key = "sky_deband",
        .binding = &shader_injection.sky_deband,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Sky Debanding",
        .section = "Extra (Experimental)",
        .tooltip = "Smooths the sky's banding and adds fine dither.\n"
                   "Higher: removes wider bands, softens sky details more.",
        .labels = {"Off", "Low", "Medium", "High"},
    },
    // VFX //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .key = "vfx_fire_brightness",
        .binding = &shader_injection.vfxfirebrightness,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness: fire/smoke/unit's dust",
        .section = "VFX",
        .tooltip = "Fire, embers, sparks, smoke, and .... Recommmendation is max 10 with modded auto exposure.",
        .min = 1.0f,
        .max = 30.0f,
        .format = "%.0f",
    },
    new renodx::utils::settings::Setting{
        .key = "vfx_blood_splash_brightness",
        .binding = &shader_injection.vfxbloodsplash,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness: Blood splash",
        .section = "VFX",
        .tooltip = "Bloodsplash brightness. Recommmendation is max 10.",
        .min = 1.0f,
        .max = 30.0f,
        .format = "%.0f",
    },
    new renodx::utils::settings::Setting{
        .key = "vfx_fog_brightness",
        .binding = &shader_injection.vfxfogbrightness,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness: Storms (Sand/Fog)",
        .section = "VFX",
        .tooltip = "Brightness of the fog color applied to particles. Recommmendation is max 1.",
        .min = 0.1f,
        .max = 4.0f,
        .format = "%.1f",
    },
      new renodx::utils::settings::Setting{
        .key = "vfxsnow",
        .binding = &shader_injection.vfxsnow,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness: Snowflakes",
        .section = "VFX",
        .tooltip = "Brightness of snow. Recommmendation is max 5.",
        .min = 0.1f,
        .max = 20.0f,
        .format = "%.1f",
    },
        new renodx::utils::settings::Setting{
        .key = "vfxrain",
        .binding = &shader_injection.vfxrain,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness: rain",
        .section = "VFX",
        .tooltip = "Brightness of rain. Recommmendation is max 5.",
        .min = 0.1f,
        .max = 20.0f,
        .format = "%.1f",
    },
    /////////////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_base_brightness",
    //     .binding = &shader_injection.vfxbasebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Flame, Dust, Clouds & Smoke",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Brightness of smoke, clouds, dust, and flame. This does not boost the inner fire color.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_normal_strength",
    //     .binding = &shader_injection.vfxnormalstrength,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Normal Strength",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Particle normal-map strength; 0 is flat.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_shadow_strength",
    //     .binding = &shader_injection.vfxshadowstrength,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Shadow Strength",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Received particle shadows; 0 disables them.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_lighting_strength",
    //     .binding = &shader_injection.vfxlightingstrength,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Lighting Strength",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Blends between unlit and stock-lit particles.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_specular_brightness",
    //     .binding = &shader_injection.vfxspecularbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Specular Brightness",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Direct sun specular highlight.",
    //     .min = 0.0f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_reflection_brightness",
    //     .binding = &shader_injection.vfxreflectionbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Reflection Brightness",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Environment-cubemap reflection.",
    //     .min = 0.0f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_soft_particle_strength",
    //     .binding = &shader_injection.vfxsoftparticlestrength,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Soft Particle Strength",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Depth-intersection fading; 0 disables it.",
    //     .min = 0.0f,
    //     .max = 1.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_opacity",
    //     .binding = &shader_injection.vfxopacity,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Opacity",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Opacity.",
    //     .min = 0.0f,
    //     .max = 1.0f,
    //     .format = "%.1f",
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_overall_brightness",
    //     .binding = &shader_injection.vfxoverallbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Overall VFX Brightness",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Overall VFX Brightness.",
    //     .min = 0.0f,
    //     .max = 10.0f,
    //     .format = "%.1f",
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxweather",
    //     .binding = &shader_injection.vfxweather,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Blood Splash & desert storm & Fog",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Brightness of blood splashes and the camera-following fog used by snowstorms and sandstorms. Recommmendation is max 0.5 in storm/fog.",
    //     .min = 0.01f,
    //     .max = 20.0f,
    //     .format = "%.2f",
    //     .is_visible = IsAdvancedSettings,
    // },

    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fog_amount",
    //     .binding = &shader_injection.vfxfogamount,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Fog Amount",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Amount of atmospheric fog applied to particles.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxdebugmode",
    //     .binding = &shader_injection.vfxdebugmode,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.f,
    //     .label = "Some highlight indicators Brightness",
    //     .section = "Game specific settings",
    //     .tooltip = "Some highligh indicators. For nights so you don't blind yourself",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.0f",
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxdebugslice",
    //     .binding = &shader_injection.vfxdebugslice,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.0f,
    //     .label = "Some highlight indicators Brightness",
    //     .section = "Game specific settings",
    //     .tooltip = "Some highligh indicators. For nights so you don't blind yourself",
    //     .min = 0.0f,
    //     .max = 511.0f,
    //     .format = "%.0f",
    // }
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "HDR Defaults",
    //     .group = "preset-line-1",
    //     .tooltip = "Resets all image tuning to HDR defaults while preserving Peak, Paper White, UI brightness, Settings Mode, and output encoding.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.00f, 1.f, 0.f, 2.f, 1.f, 1.f, 1.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: All Conditions",
    //     .group = "preset-line-1",
    //     .tooltip = "Balanced recommendation intended to work across all weather and lighting conditions. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 4.f, 2.5f, 0.5f, 3.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: Day/Dry/Rain",
    //     .group = "preset-line-2",
    //     .tooltip = "Recommendation for daytime scenes and dry weather. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 4.f, 5.f, 2.f, 5.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: Night/Snow/Fog",
    //     .group = "preset-line-2",
    //     .tooltip = "Recommendation for fog, snow, and nighttime scenes. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 3.f, 2.f, 0.5f, 2.f);
    //     },
    // },
  };
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));
}

// Graphics (Experimental): the sliders, through the LOD pick hook (model detail), Attila's unit hook
// (twattila/graphics_hooks.hpp) and engine tweaks (filled in ConfigureAddon).
std::array<tw::engine_tweaks::Option, 4> unit_detail;  // foot soldiers, cavalry, elephants, artillery
tw::engine_tweaks::Option unit_animation;
tw::engine_tweaks::Option unit_variety;
tw::engine_tweaks::Option footstep_effects;
tw::engine_tweaks::Option dead_units;
tw::engine_tweaks::Option model_detail;
tw::engine_tweaks::Option terrain_detail;
tw::engine_tweaks::Option building_detail;
// Campaign Tree Detail (Attila's): the engine's rigid_campaign_tree_lod alone (Rome II's tree hook isn't in graphics_hooks).
tw::engine_tweaks::Option campaign_trees;
// Shadow Map Size: the game's shadowmap_extreme_quality (8192 at the highest Shadows setting).
tw::engine_tweaks::Option shadow_map = {.scale = 0.f};
// Every engine option above, then the tests' (Graphics 2 of 2: more_tests.h, twattila/engine_tests.hpp).
std::vector<tw::engine_tweaks::Option*> graphics_options;
std::vector<twattila::engine_tests::EngineTest> engine_tests;
std::atomic<bool> at_start_done = false;
std::atomic<bool> at_start_stop = false;
HMODULE addon_module_handle = nullptr;
twattila::feature_settings::SssSliders sss_sliders;
twattila::feature_settings::AaSliders aa_sliders;
// Ray tracing's (twattila/rt_scene.hpp).
twattila::rt_scene::Sliders rt_sliders;
// Skip the Shadow Maps (Experimental, off): Ray-Traced Shadows Only with the rays' sun shadows (rt_scene InstallShadowsOnly).
float rays_skip_cascades_row = 0.f;
// Faster Game CPU Code (twattila/engine_code.hpp, Game::ROME2): Attila's three routines on this game's build (the soldier
// draw's rounding at its own stack slots, float to half and fmod the same bytes; test/tw_engine_code with this game's DLL:
// every register and the x87 state the game's). Experimental, off until a game run.
float engine_code_row = 0.f;

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
bool graphics_found = false;
bool graphics_hooks_installed = false;


// Ambient Occlusion Visibility Bitmask (twattila/ao_pass.hpp): the addon's own shader in place of the game's AO
// pass (whose replacement is the game's code only).
constexpr std::array<twattila::ao_pass::AmbientOcclusionDraw, 1> AO_DRAWS = {{
    {.shader_hash = 0x1ED22F9C, .bitmask = __ao_bitmask01, .upsample = __ao_upsample01, .resolve = __ao_resolve01,
     .denoise = __ao_denoise01},  // ssao01
}};

// Its camera constants at their places in Attila's camera_VS_PS (frame_history::AttilaCamera) for the
// screen-space shadows and the AO filter (destination, source, bytes).
constexpr std::array<twattila::frame_history::CameraRun, 6> CAMERA_LAYOUT = {{
    {.destination = 0, .source = 0, .bytes = 12},
    {.destination = 16, .source = 16, .bytes = 192},
    {.destination = 272, .source = 272, .bytes = 64},
    {.destination = 336, .source = 208, .bytes = 64},
    {.destination = 400, .source = 336, .bytes = 52},
    {.destination = 464, .source = 400, .bytes = 16},
}};

// DLAA / DLSS object motion (twattila/anti_aliasing.hpp, as Attila's): the game's shaders whose things move on
// their own and their motion versions (tools\tw\attila_motion\port_motion.py, GPU-checked by test_motion.py).
using twattila::anti_aliasing::MotionKind;
constexpr std::array<twattila::anti_aliasing::MotionShader, 39> MOTION_SHADERS = {{
    {.hash = 0xD4D9A7CE, .motion_shader = __motion_grass00},
    {.hash = 0x239E12B0, .motion_shader = __motion_grass01},
    {.hash = 0x69FCD0B3, .motion_shader = __motion_grass_sm41},
    {.hash = 0xD0587AC7, .motion_shader = __motion_grass_plant_sm3},
    {.hash = 0x2D15A993, .motion_shader = __motion_grass_plant_sm5},
    {.hash = 0x5AB72F3F, .motion_shader = __motion_grass_lq},
    {.hash = 0x41FE62A8, .motion_shader = __motion_leaf_sm3_a},
    {.hash = 0x0DEC7CAB, .motion_shader = __motion_leaf_sm41_a},
    {.hash = 0xC36E2C1C, .motion_shader = __motion_leaf_sm5_a},
    {.hash = 0x929D693E, .motion_shader = __motion_leaf_lq},
    {.hash = 0x3333EFDC, .motion_shader = __motion_branch_sm3_a},
    {.hash = 0xEFCA94BC, .motion_shader = __motion_branch_sm41_b},
    {.hash = 0x7932D95F, .motion_shader = __motion_branch_lq},
    {.hash = 0xA30C5E0B, .motion_shader = __motion_unit_sm3_b, .kind = MotionKind::UNIT},
    {.hash = 0x0F1FBD46, .motion_shader = __motion_unit_4_bones_sm3_a, .kind = MotionKind::UNIT},
    {.hash = 0x1675B565, .motion_shader = __motion_unit_dismember_sm3, .kind = MotionKind::UNIT},
    {.hash = 0x9E564D81, .motion_shader = __motion_unit_4_bones_dismember_sm3, .kind = MotionKind::UNIT},
    {.hash = 0xA3550981, .motion_shader = __motion_unit_sm41_b, .kind = MotionKind::UNIT},
    {.hash = 0x1CD702E3, .motion_shader = __motion_unit_4_bones_sm41_a, .kind = MotionKind::UNIT},
    {.hash = 0x63DB20CA, .motion_shader = __motion_unit_dismember_sm41, .kind = MotionKind::UNIT},
    {.hash = 0xF2990459, .motion_shader = __motion_unit_4_bones_dismember_sm41, .kind = MotionKind::UNIT},
    {.hash = 0x59431BB2, .motion_shader = __motion_rigid_sm3_a, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_sm3_a},
    {.hash = 0x658FEA51, .motion_shader = __motion_rigid_sm3_b, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_sm3_b},
    {.hash = 0xF343950E, .motion_shader = __motion_rigid_sm41_b, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_sm41_b},
    {.hash = 0x3E086AFB, .motion_shader = __motion_rigid_lq, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_lq},
    {.hash = 0x54FB9355, .motion_shader = __motion_rigid_blend_sm3_a, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_blend_sm3_a},
    {.hash = 0xEBB7BFED, .motion_shader = __motion_rigid_blend_sm3_b, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_blend_sm3_b},
    {.hash = 0x95A90798, .motion_shader = __motion_rigid_blend_wind_sm3_a, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_blend_wind_sm3_a},
    {.hash = 0xB08CD963, .motion_shader = __motion_rigid_blend_wind_sm3_b, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_blend_wind_sm3_b},
    {.hash = 0x5E27F261, .motion_shader = __motion_oldocean_a},
    {.hash = 0xF5D2B072, .motion_shader = __motion_oldocean_b},
    {.hash = 0x997A7119, .motion_shader = __motion_cloth_sm3_b, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_cloth_sm3_b},
    {.hash = 0x438E2D53, .motion_shader = __motion_sail_sm3_a, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sm3_a},
    {.hash = 0x2C98F649, .motion_shader = __motion_sail_symbol_sm3_a, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_symbol_sm3_a},
    {.hash = 0x4F90C2B5, .motion_shader = __motion_rope_sm3_a, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope_sm3_a},
    {.hash = 0x305E3F04, .motion_shader = __motion_ocean_deep_a, .kind = MotionKind::HISTORY, .history_textures = 0b10},
    {.hash = 0xC9C8E179, .motion_shader = __motion_ocean_shallow_sm3_b, .kind = MotionKind::HISTORY, .history_textures = 0b1010},
    {.hash = 0x39FAADBB, .motion_shader = __motion_ocean_shallow_sm4_a, .kind = MotionKind::HISTORY, .history_textures = 0b1010},
    {.hash = 0x00C16C03, .motion_shader = __motion_ocean_coast_a, .kind = MotionKind::HISTORY, .history_textures = 0b101010},
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl ps30 slm / slm_reflection (standard) and
// skinlm, every shadow method, with the camera at b0 and the G-buffer at t0-t3.
// Ray tracing's scene (twattila/rt_scene.hpp; tools\tw\rt_scene_map.py): the main view's terrain (old and custom
// geom, shader models 3 and 4.1), rigid objects, tree branches and leaves and units (Rome II's own:
// data_rome2.pack's builds), and their capture versions (world positions; leaves also UVs).
constexpr std::array<twattila::rt_scene::SceneShader, 35> SCENE_SHADERS = {{
    {.hash = 0x83AB3AFC, .capture_shader = __rt_capture_terrain_old_sm41_a, .terrain = true},
    {.hash = 0x2898B8F6, .capture_shader = __rt_capture_terrain_old_sm3_a, .terrain = true},
    {.hash = 0xD8475AB1, .capture_shader = __rt_capture_terrain_custom_material_sm3_b, .terrain = true},
    {.hash = 0x5B7D7DF3, .capture_shader = __rt_capture_terrain_custom_material_sm41_b, .terrain = true},
    {.hash = 0x50FD2645, .capture_shader = __rt_capture_terrain_custom_sm41_b, .terrain = true},
    {.hash = 0x1E4E269B, .capture_shader = __rt_capture_terrain_custom_sm3_a, .terrain = true},
    {.hash = 0x41FE62A8, .capture_shader = __rt_capture_leaf_sm3_a, .foliage = true},
    {.hash = 0x0DEC7CAB, .capture_shader = __rt_capture_leaf_sm41_a, .foliage = true},
    {.hash = 0xC36E2C1C, .capture_shader = __rt_capture_leaf_sm5_a, .foliage = true},
    {.hash = 0x929D693E, .capture_shader = __rt_capture_leaf_lq, .foliage = true},
    {.hash = 0x3333EFDC, .capture_shader = __rt_capture_branch_sm3_a},
    {.hash = 0xEFCA94BC, .capture_shader = __rt_capture_branch_sm41_b},
    {.hash = 0x7932D95F, .capture_shader = __rt_capture_branch_lq},
    {.hash = 0xA30C5E0B, .capture_shader = __rt_capture_unit_sm3_b, .dynamic = true},
    {.hash = 0x0F1FBD46, .capture_shader = __rt_capture_unit_4_bones_sm3_a, .dynamic = true},
    {.hash = 0x1675B565, .capture_shader = __rt_capture_unit_dismember_sm3, .dynamic = true},
    {.hash = 0x9E564D81, .capture_shader = __rt_capture_unit_4_bones_dismember_sm3, .dynamic = true},
    {.hash = 0xA3550981, .capture_shader = __rt_capture_unit_sm41_b, .dynamic = true},
    {.hash = 0x1CD702E3, .capture_shader = __rt_capture_unit_4_bones_sm41_a, .dynamic = true},
    {.hash = 0x63DB20CA, .capture_shader = __rt_capture_unit_dismember_sm41, .dynamic = true},
    {.hash = 0xF2990459, .capture_shader = __rt_capture_unit_4_bones_dismember_sm41, .dynamic = true},
    {.hash = 0x59431BB2, .capture_shader = __rt_capture_rigid_sm3_a, .rows_shader = __motion_record_rigid_sm3_a},
    {.hash = 0x658FEA51, .capture_shader = __rt_capture_rigid_sm3_b, .rows_shader = __motion_record_rigid_sm3_b},
    {.hash = 0xF343950E, .capture_shader = __rt_capture_rigid_sm41_b, .rows_shader = __motion_record_rigid_sm41_b},
    {.hash = 0x3E086AFB, .capture_shader = __rt_capture_rigid_lq, .rows_shader = __motion_record_rigid_lq},
    {.hash = 0x54FB9355, .capture_shader = __rt_capture_rigid_blend_sm3_a, .rows_shader = __motion_record_rigid_blend_sm3_a},
    {.hash = 0xEBB7BFED, .capture_shader = __rt_capture_rigid_blend_sm3_b, .rows_shader = __motion_record_rigid_blend_sm3_b},
    {.hash = 0x95A90798, .capture_shader = __rt_capture_rigid_blend_wind_sm3_a, .rows_shader = __motion_record_rigid_blend_wind_sm3_a},
    {.hash = 0xB08CD963, .capture_shader = __rt_capture_rigid_blend_wind_sm3_b, .rows_shader = __motion_record_rigid_blend_wind_sm3_b},
    {.hash = 0xD4D9A7CE, .capture_shader = __rt_capture_grass_d4d9, .dynamic = true, .foliage = true, .grass = true},
    {.hash = 0x239E12B0, .capture_shader = __rt_capture_grass_239e, .dynamic = true, .foliage = true, .grass = true},
    {.hash = 0x69FCD0B3, .capture_shader = __rt_capture_grass_69fc, .dynamic = true, .foliage = true, .grass = true},
    {.hash = 0xD0587AC7, .capture_shader = __rt_capture_grass_d058, .dynamic = true, .foliage = true, .grass = true},
    {.hash = 0x2D15A993, .capture_shader = __rt_capture_grass_2d15, .dynamic = true, .foliage = true, .grass = true},
    {.hash = 0x5AB72F3F, .capture_shader = __rt_capture_grass_5ab7, .dynamic = true, .foliage = true, .grass = true},
}};

// The game's passes that read render-size textures at the display's size (texture slots), below 100%, and the
// half-size passes drawn whole (their readers: view_share::WHOLE_TEXTURES).
constexpr std::array<twattila::view_share::RegionRead, 5> RENDER_SCALE_READS = {{
    {.shader_hash = 0xAD818F92, .slots = 0b1},                  // luma / bloom downsample
    {.shader_hash = 0x41894C1F, .slots = 0b11, .whole = true},  // AO blur_x
    {.shader_hash = 0x7DDD84A3, .slots = 0b10, .whole = true},  // AO blur_y
    {.shader_hash = 0x2A3297C6, .slots = 0b11, .whole = true},  // water refraction (3 channels)
    {.shader_hash = 0x024D3FCB, .slots = 0b1, .whole = true},   // water refraction (4 channels)
}};

// Water Clarity and Caustics (twattila/water_effects.hpp): the battle water drawn in ATN's runs (A1, C1, A2), lighting_VS_PS's
// g_sea_bed_light_scatter at c10 as Attila's (its RDEF). The campaign's sea 3DCC4AED, lake F5A45F91 and river ED0C5F47 never
// read it. Caustics' patch: WARP tools/tw/rome2/water_r2 (the game's bytes and the render scale's patch first: t51 unbound
// bit-identical, t51 = k the refraction x (1 + k)).
constexpr std::array<twattila::water_effects::WaterShader, 3> WATER_SHADERS = {{
    {.hash = 0x58DC6E12, .lighting_slot = 2, .world_input = 3, .refraction_slot = 1},  // battle plane, Water Quality Ultra
    {.hash = 0xB0A1E151, .lighting_slot = 1, .world_input = 3, .refraction_slot = 1},  // battle plane, High
    {.hash = 0x3ECFEAF5, .lighting_slot = 2, .world_input = 1, .refraction_slot = 1},  // battle river
}};
// The sun's per-frame atlas's readers besides the sun draws (A1, C1, A2): the battle water Ultra and river, the campaign's sea.
constexpr std::array<uint32_t, 3> SHADOW_READER_SHADERS = {0x58DC6E12, 0x3ECFEAF5, 0x3DCC4AED};

constexpr std::array<twattila::light_bounce::SunDraw, 50> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0x0AF04E16, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x2D77EEF9, .standard = true, .lighting_slot = 1, .sun_direction = 0},
    {.shader_hash = 0x340CBE9D, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x4C4B1D0F, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x4CA464D1, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x57E876C4, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x6B22774C, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x6FC5ECEA, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x7A216579, .standard = true, .lighting_slot = 1, .sun_direction = 0},
    {.shader_hash = 0x802A199A, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x81058A1E, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x820C7A2D, .standard = true, .lighting_slot = 1, .sun_direction = 0},
    {.shader_hash = 0x9DECD766, .standard = true, .lighting_slot = 1, .sun_direction = 0},
    {.shader_hash = 0xBA46D003, .standard = true, .sun_direction = 0},
    {.shader_hash = 0x091A8FCC, .sun_direction = 0},
    {.shader_hash = 0x1A0532C0, .sun_direction = 0},
    {.shader_hash = 0x1B6DC4CE, .sun_direction = 0},
    {.shader_hash = 0x498A0B2F, .sun_direction = 0},
    {.shader_hash = 0x55568999, .sun_direction = 0},
    {.shader_hash = 0x56DCF011, .sun_direction = 0},
    {.shader_hash = 0x62AC2F63, .sun_direction = 0},
    {.shader_hash = 0x6DD888D9, .lighting_slot = 1, .sun_direction = 0},
    {.shader_hash = 0x86DAA88A, .sun_direction = 0},
    {.shader_hash = 0xB2616A57, .lighting_slot = 1, .sun_direction = 0},
    {.shader_hash = 0xFB3FC293, .sun_direction = 0},
    {.shader_hash = 0xFC564308, .sun_direction = 0},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x006F587A, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_highdef
    {.shader_hash = 0x362108F9, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_highdef
    {.shader_hash = 0x5167CB92, .standard = true, .gbuffer_slot = 5, .camera_slot = 0, .lighting_slot = 1, .sun_direction = 0},  // slm_highdef
    {.shader_hash = 0x6B9FD3CD, .standard = true, .gbuffer_slot = 5, .camera_slot = 0, .lighting_slot = 1, .sun_direction = 0},  // slm_lowdef
    {.shader_hash = 0x7358C058, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_lowdef
    {.shader_hash = 0x75650CEB, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_lowdef
    {.shader_hash = 0x7B50CFB1, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_lowdef
    {.shader_hash = 0x84F99EA7, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_highdef
    {.shader_hash = 0x958EF5CE, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_highdef
    {.shader_hash = 0x99DEEE40, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_lowdef
    {.shader_hash = 0xA250EBA2, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_lowdef
    {.shader_hash = 0xFA91A41F, .standard = true, .gbuffer_slot = 7, .camera_slot = 0, .sun_direction = 0},  // slm_highdef
    {.shader_hash = 0x172ACDE3, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_lowdef
    {.shader_hash = 0x25DD62CD, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_highdef
    {.shader_hash = 0x6260ABCD, .gbuffer_slot = 1, .camera_slot = 0, .lighting_slot = 1, .sun_direction = 0},  // skinlm_highdef
    {.shader_hash = 0x6CCDF7BA, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_lowdef
    {.shader_hash = 0x89F739AF, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_lowdef
    {.shader_hash = 0x8B0E5CAA, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_highdef
    {.shader_hash = 0x95C02182, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_highdef
    {.shader_hash = 0xAFDB7895, .gbuffer_slot = 1, .camera_slot = 0, .lighting_slot = 1, .sun_direction = 0},  // skinlm_lowdef
    {.shader_hash = 0xE3F77F6A, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_lowdef
    {.shader_hash = 0xF6383D19, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_highdef
    {.shader_hash = 0xFADD954D, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_lowdef
    {.shader_hash = 0xFCD6C263, .gbuffer_slot = 3, .camera_slot = 0, .sun_direction = 0},  // skinlm_highdef
}};

bool configured = false;
bool attached = false;
bool addon_registered = false;

}  // namespace

// Lifecycle /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  auto peak = renodx::utils::swapchain::GetPeakNits(swapchain);
  if (!peak.has_value()) {
    peak = 1000.f;
  }

  // find and set
  for (auto& setting : settings) {
    if (setting->binding != &shader_injection.peak_white_nits) continue;
    setting->default_value = peak.value();
    setting->max = std::max(peak.value(), setting->max);
    setting->can_reset = true;
    break;
  }

  // settings[3]->default_value = renodx::utils::swapchain::ComputeReferenceWhite(peak.value());
}

// The shadow map size, before the game builds its shadows.
void OnInitDevice(reshade::api::device*) {
  tw::engine_tweaks::Update(L"empire.retail.dll", graphics_options, &model_detail, &graphics_found, graphics_status);
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // Before any setting is read this frame (as Attila's): DLSS never starts on another vendor's GPU, a saved DLSS or a
  // preset's is XeSS on Intel's, else FSR.
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twattila::screen_space_shadows::Update(swapchain, twattila::feature_settings::SssSettings(sss_sliders));
  // The game's MSAA keeps DLAA / DLSS from running (A2_185833: its 2-sample sun draws 006F587A / FCD6C263 keep no camera):
  // the rays and frame generation, which need them, off with it (the rays' scene captures nothing multisampled).
  const bool game_msaa = twattila::anti_aliasing::GameMsaa();
  auto rays_aa = aa_sliders;
  if (game_msaa) {
    rays_aa.method = 0.f;
  }
  auto rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, rays_aa, shader_injection.light_bounce_strength);
  rt_settings.shadows_only = rt_settings.shadows_only && rays_skip_cascades_row != 0.f;
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  if (game_msaa) {
    aa_settings.frame_generation = 0;
  }
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  twattila::feature_settings::UpdateSsr(&shader_injection);
  shader_injection.ssao_scale = twattila::ao_pass::ResolutionScale(shader_injection.ssao_resolution);
  shader_injection.ssao_frame =
      twattila::ao_pass::Update(shader_injection.ssao_mode >= 1.f, shader_injection.ssao_temporal != 0.f,
                                shader_injection.ssao_scale, shader_injection.ssao_denoise != 0.f);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  twattila::engine_code::Update(engine_code_row != 0.f, twattila::engine_code::Game::ROME2);
  tw::engine_tweaks::Update(L"empire.retail.dll", graphics_options, &model_detail, &graphics_found, graphics_status);
  twattila::graphics_hooks::Update(
      {
          .unit_detail = {unit_detail[0].scale, unit_detail[1].scale, unit_detail[2].scale, unit_detail[3].scale},
          .dead_units = dead_units.scale,
          .unit_animation = unit_animation.scale,
          .facial_animation = unit_animation.scale,
          .hand_animation = unit_animation.scale,
          .unit_variety = unit_variety.scale,
          .footstep_effects = footstep_effects.scale,
      },
      &graphics_hooks_installed, graphics_status, twattila::graphics_hooks::Game::ROME2);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: Rome 2 by ATN";  // TODO: change me!

namespace {

// Graphics 1 of 2's (twattila/graphics_page.hpp): what is true here. The rays' sun shadows and AO multiply the game's
// shadow maps and SSAO; no ray-traced reflections (FidelityFX SSSR on the water), no fog hooked.
// The game's MSAA keeps the anti-aliasing's DLAA / DLSS and with them every ray-traced effect off (anti_aliasing.hpp).
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = twattila::graphics_page::ATTILA_TEXTS.aa_labels,
    .aa_tooltip = twattila::graphics_page::ATTILA_TEXTS.aa_tooltip,
    .in_game_aa_note = "In-game Anti-Aliasing must be Off for DLSS, FSR, XeSS, Frame Generation and ray tracing (MSAA keeps them off).",
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its ambient occlusion,\n"
                     "shadows and reflections. The rest at its default.",
    .no_rays_tooltip = twattila::graphics_page::ATTILA_TEXTS.no_rays_tooltip,
    .rays_tooltip = "Ray-traced shadows, path-traced ambient occlusion and Global Illumination at my settings. The rest of\n"
                    "this page at its default. Needs an upscaler (Anti-Aliasing above).\n"
                    "On NVIDIA also turns DLSS Ray Reconstruction on. Elsewhere Experimental, slower: its own denoisers.",
    .shadow_tooltip = "Game's Shadow Maps: the original. Bend Screen-Space Shadows: those plus fine contact shadows from\n"
                      "what is on screen (Bend Studio's; soldiers, weapons, grass; needs in-game shadows on).\n"
                      "DXR: DirectX Raytracing, the sun's shadow from rays too, on top of the game's, in place of Bend's.\n"
                      "Needs an upscaler.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep in-game SSAO on for all of these.\n"
                  "Game's SSAO: everything counts as solid, so soldiers and trees get dark halos.\n"
                  "GT-VBAO (Ground Truth AO with a visibility bitmask): objects have a thickness, so light gets behind\n"
                  "thin ones.\n"
                  "RTAO: from rays through the scene, on top of the game's SSAO. Needs an upscaler.\n"
                  "ReSTIR GI: Global Illumination's paths bring the sky's light in place of the game's ambient;\n"
                  "turns it on below. Needs an upscaler.",
    .ao_paths_label = "Path-Traced (ReSTIR GI, Experimental)",
    .effects_at_output_tooltip = "While the upscaler renders below 100%, ambient occlusion, Light Bounce and shadow softness keep\n"
                                 "their 100% detail. Looks better, can cost some performance.",
    .reflections_tooltip = "Reflections on water. Need in-game Screen Space Reflections on.\n"
                           "Game's SSR: the original. FidelityFX SSSR: AMD's hierarchical ray march, exact, no gaps, reaching\n"
                           "across the screen, usually faster. DXR: from rays, also what is off screen or hidden. Needs an upscaler.",
    .reflections_rays_label = "Ray-Traced (DXR, Experimental)",
    .water_caustics_label = "Water Caustics (Experimental)",
};

void ConfigureAddon() {
  if (configured) return;

  // Construct the non-trivial containers here rather than during DLL loading.
  BuildRuntimeData();

  // Shaders cb
  renodx::mods::shader::expected_constant_buffer_space = 50;
  renodx::mods::shader::expected_constant_buffer_index = 13;

  // Swapchain cb
  renodx::mods::swapchain::expected_constant_buffer_index = 13;
  renodx::mods::swapchain::expected_constant_buffer_space = 50;

  // Swapchain upgrade settings
  renodx::mods::swapchain::use_resource_cloning = true;
  renodx::mods::swapchain::force_borderless = false;
  renodx::mods::swapchain::prevent_full_screen = true;

  // Proxy Shaders
  renodx::mods::swapchain::swap_chain_proxy_shaders = {
      {
          reshade::api::device_api::d3d11,
          {
              .vertex_shader = __swap_chain_proxy_vertex_shader_dx11,
              .pixel_shader = __swap_chain_proxy_pixel_shader_dx11,
          },
      },
  };

  // Resource Upgrades
#if RENODX_MODS_SWAPCHAIN_VERSION == 2
  renodx::mods::swapchain::resource_upgrade_infos.push_back({
#else
  renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
#endif
      .old_format = reshade::api::format::r8g8b8a8_unorm,
      .new_format = reshade::api::format::r16g16b16a16_float,
      .ignore_size = false,
      .use_resource_view_cloning = true,  // TODO: needed?
      .use_resource_view_hot_swap = true,
      .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
      .usage_include = reshade::api::resource_usage::render_target,
  });

  // Appended Settings (Graphics (Experimental))
  terrain_detail.tweaks = {
      {.name = "terrain_far_shader_distance"},
  };
  building_detail.tweaks = {
      // Battle buildings' LOD distances, copied into a building model's LODs when it loads (as Attila).
      {.name = "battle_building_lod0_distance"},
      {.name = "battle_building_lod1_distance"},
      {.name = "battle_building_lod2_distance"},
      {.name = "battle_building_lod3_distance"},
  };
  const auto unit_distance = [](size_t index, const std::string& key, const std::string& label,
                                const std::string& units) {
    return tw::engine_tweaks::Slider(&unit_detail[index], key, label,
                                     "How far " + units + " stay 3D before turning into flat sprites.\n"
                                     "Higher: heavier for the CPU and GPU.",
                                     8.f, 0.1f);
  };
  // Graphics 2 of 2's tests: Attila's that this game has, then More Tests (more_tests.h).
  engine_tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), more_tests::ATTILA_TESTS);
  {
    auto more = more_tests::MoreTests();
    engine_tests.insert(engine_tests.end(), more.begin(), more.end());
  }
  campaign_trees.tweaks = {{.name = "rigid_campaign_tree_lod"}};
  graphics_options = {&model_detail, &terrain_detail, &building_detail, &campaign_trees, &shadow_map};
  for (auto& test : engine_tests) graphics_options.push_back(&test.option);
  graphics_status = tw::engine_tweaks::StatusLine();
  // Graphics presets (ATN): MAX (every graphics setting at its maximum) and Reset (all
  // graphics settings at their defaults; not the ambient occlusion or the Light Bounce).
  shadow_map.tweaks = {
      {.name = "shadowmap_extreme_quality", .flag = true},
  };
  const std::vector<std::pair<std::string, float>> graphics_defaults = {
      {"GraphicsShadowMapSize", 0.f},
      {"GraphicsUnitDetailSmall", 1.f},
      {"GraphicsUnitDetailMedium", 1.f},
      {"GraphicsUnitDetailLarge", 1.f},
      {"GraphicsUnitDetailHuge", 1.f},
      {"GraphicsDeadUnits", 1.f},
      {"GraphicsUnitAnimation", 1.f},
      {"GraphicsUnitVariety", 1.f},
      {"GraphicsFootstepEffects", 1.f},
      {"GraphicsModelDetail", 1.f},
      {"GraphicsTerrainDetail", 1.f},
      {"GraphicsBuildingDetail", 1.f},
      {"GraphicsCampaignDetail", 1.f},
  };
  const std::vector<std::pair<std::string, float>> graphics_max = {
      {"GraphicsShadowMapSize", 1.f},
      {"GraphicsUnitDetailSmall", 8.f},
      {"GraphicsUnitDetailMedium", 8.f},
      {"GraphicsUnitDetailLarge", 8.f},
      {"GraphicsUnitDetailHuge", 8.f},
      {"GraphicsDeadUnits", 1.f},
      {"GraphicsUnitAnimation", 4.f},
      {"GraphicsUnitVariety", 4.f},
      {"GraphicsFootstepEffects", 4.f},
      {"GraphicsModelDetail", 8.f},
      {"GraphicsTerrainDetail", 4.f},
      {"GraphicsBuildingDetail", 8.f},
      {"GraphicsCampaignDetail", 4.f},
  };
  // ATN's own settings: every graphics setting at its default but `changes`.
  const auto atn_preset = [&graphics_defaults](const std::vector<std::pair<std::string, float>>& changes) {
    auto values = graphics_defaults;
    for (const auto& [key, value] : changes) {
      std::ranges::find(values, key, &std::pair<std::string, float>::first)->second = value;
    }
    return values;
  };
  const auto graphics_atn = atn_preset({
      {"GraphicsUnitDetailSmall", 2.f},
      {"GraphicsUnitDetailMedium", 2.f},
      {"GraphicsUnitDetailLarge", 2.f},
      {"GraphicsUnitDetailHuge", 2.f},
      {"GraphicsDeadUnits", 0.5f},
      {"GraphicsUnitVariety", 3.f},
      {"GraphicsFootstepEffects", 3.f},
      {"GraphicsModelDetail", 1.5f},
      {"GraphicsTerrainDetail", 4.f},
      {"GraphicsBuildingDetail", 8.f},
      {"GraphicsShadowMapSize", 1.f},
  });
  // Attila's "With my 4x / 8x unit sizes" (the Unit Sizes mod): the CPU-heavy unit distances down, sprites nearer.
  const auto graphics_atn_large_units = atn_preset({
      {"GraphicsUnitDetailSmall", 0.69f},
      {"GraphicsUnitDetailMedium", 0.69f},
      {"GraphicsUnitDetailLarge", 0.69f},
      {"GraphicsUnitDetailHuge", 0.69f},
      {"GraphicsDeadUnits", 0.5f},
      {"GraphicsUnitAnimation", 1.5f},
      {"GraphicsUnitVariety", 3.f},
      {"GraphicsFootstepEffects", 3.f},
      {"GraphicsBuildingDetail", 3.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsShadowMapSize", 1.f},
  });
  settings.insert(settings.end(), {
      tw::engine_tweaks::Note(),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting back to its default.",
          .on_change = [graphics_defaults]() { renodx::utils::settings::UpdateSettings(graphics_defaults); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "MAX",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting at its maximum. Heavy.",
          .on_change = [graphics_max]() { renodx::utils::settings::UpdateSettings(graphics_max); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "What I would use:",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 2x, Dead Unit Detail 0.50x, Unit Variety and Footstep Effects 3x, Model Detail 1.50x,\n"
                     "Terrain Detail 4x, Building Detail 8x, Shadow Map Size 8192. The rest at its default.",
          .on_change = [graphics_atn]() { renodx::utils::settings::UpdateSettings(graphics_atn); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "With my 4x / 8x unit sizes",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 0.69x, Dead Unit Detail 0.50x, Unit Animation 1.50x, Unit Variety and Footstep Effects 3x,\n"
                     "Building Detail 3x, Campaign Tree Detail 4x, Shadow Map Size 8192. The rest at its default.",
          .on_change = [graphics_atn_large_units]() { renodx::utils::settings::UpdateSettings(graphics_atn_large_units); },
      },
      unit_distance(0, "GraphicsUnitDetailSmall", "Unit Detail: Foot Soldiers", "foot soldiers"),
      unit_distance(1, "GraphicsUnitDetailMedium", "Unit Detail: Cavalry", "cavalry"),
      unit_distance(2, "GraphicsUnitDetailLarge", "Unit Detail: Elephants", "elephants and other large units"),
      unit_distance(3, "GraphicsUnitDetailHuge", "Unit Detail: Artillery", "artillery and other huge units"),
      tw::engine_tweaks::Slider(&dead_units, "GraphicsDeadUnits", "Dead Unit Detail Distance",
                                "Where dead soldiers turn into flat sprites, x the living ones' distance.\n"
                                "Lower: cheaper battles with many dead.",
                                1.f, 0.05f),
      tw::engine_tweaks::Slider(&unit_animation, "GraphicsUnitAnimation", "Unit Animation Distance",
                                "How far soldiers move on their own, with faces and hands (beyond it they copy their\n"
                                "neighbours' moves). Higher: heavier for the CPU.",
                                4.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_variety, "GraphicsUnitVariety", "Unit Variety Distance",
                                "How far soldiers keep their own heads and gear (beyond it they look alike).\n"
                                "Higher: heavier.",
                                4.f, 0.1f),
      tw::engine_tweaks::Slider(&footstep_effects, "GraphicsFootstepEffects", "Footstep Effects Distance",
                                "How far soldiers' footsteps kick up dust. Higher: heavier.", 4.f, 0.1f),
      tw::engine_tweaks::Slider(&model_detail, "GraphicsModelDetail", "Model Detail Distance",
                                "Battle: trees and other objects keep their detailed versions farther away.", 8.f),
      tw::engine_tweaks::Slider(&terrain_detail, "GraphicsTerrainDetail", "Terrain Detail Distance",
                                "Detailed terrain shading reaches farther.", 4.f),
      tw::engine_tweaks::Slider(&building_detail, "GraphicsBuildingDetail", "Building Detail Distance",
                                "Battle: buildings keep their detailed versions farther away. From the next battle.",
                                8.f),
      tw::engine_tweaks::Slider(&campaign_trees, "GraphicsCampaignDetail", "Campaign Tree Detail Distance",
                                "Campaign map: trees keep their detail farther away.", 4.f),
      new renodx::utils::settings::Setting{
          .key = "GraphicsShadowMapSize",
          .binding = &shadow_map.scale,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Shadow Map Size",
          .section = "Graphics (Experimental)",
          .tooltip = "Sharper sun shadows that flicker less, at the highest Shadows setting. More video memory.\n"
                     "Applies after a restart.",
          .labels = {"4096 (Game's)", "8192"},
      },
  });
  {
    const auto reflections = twattila::feature_settings::SsrSettings(&shader_injection);
    settings.insert(settings.end(), reflections.begin(), reflections.end());
  }
  settings.insert(settings.end(), {
      graphics_status,
      // Two empty lines between this section and the ambient occlusion (ATN).
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "",
          .section = "Graphics (Experimental)",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "",
          .section = "Graphics (Experimental)",
      },
  });

  // Appended Settings (Graphics 2 of 2's tests)
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Tests: each changes the game's own values (1x / Game's: unchanged). Many apply from the next\n"
               "battle or after a restart; some may do nothing or crash. More Tests: every other graphics\n"
               "value the game reads (unsure ones).",
      .section = engine_tests.empty() ? std::string("More Tests") : engine_tests.front().section,
      .tint = 0xFFD23C,
  });
  twattila::engine_tests::Append(&settings, &engine_tests, SyncEarlyLoad);

  // Appended Settings (Ambient Occlusion, Global Illumination, Screen-Space Shadows, Anti-Aliasing)
  twattila::lighting_settings::Append(&settings, &shader_injection);
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);
  // The water's reflections read the ray-traced reflections (TraceWaterReflections): their rows, the water's too.
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, false, true);
  twattila::rt_scene::internal::water_pass = true;
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterClarity",
      .binding = &twattila::water_effects::water_clarity,
      .default_value = 1.f,
      .label = "Water Clarity",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "How far you see into rivers and lakes in battles. 1: the game's; higher: clearer water.",
      .min = 0.25f,
      .max = 16.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterCaustics",
      .binding = &twattila::water_effects::water_caustics,
      .default_value = 0.f,
      .label = "Water Caustics (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Rippling light on river and lake beds in battles. 0: off.",
      .max = 2.f,
      .format = "%.2f",
  });

  // Appended Settings (HDR10)
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR. Default.\n"
                   "HDR10: 10-bit. scRGB: 16-bit, 10-bit while Frame Generation is on. SDR: displays without HDR (all fixes\n"
                   "and options stay).",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const int value = static_cast<int>(setting->GetValue());
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = value,
        .hdr_encoding = [](int output) {
          if (output != twattila::output_encoding::AUTO) return output;
          // Frame generation on from the start: HDR10, the frames need no pass of their own (frame_generation.hpp).
          const bool frame_generation = aa_sliders.method == 5.f && aa_sliders.frame_generation != 0.f;
          return frame_generation ? twattila::output_encoding::HDR10 : twattila::output_encoding::SCRGB;
        },
    });
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV30, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom "
                   "PsychoV31 and PsychoV35. Visibility "
                   "Bitmask ambient occlusion and Light Bounce: the method of Therrien, Levesque and Gilet (2023) on "
                   "GTAO (Jimenez et al. 2016), as GT-VBAO and GT-VBGI by Mirko Salm (2024). Screen-space shadows: "
                   "Bend Studio (Apache 2.0, Sony Interactive Entertainment). SMAA (MIT, Jimenez et al.), CMAA2 "
                   "(Apache 2.0, Intel), reflections: AMD FidelityFX SSSR (MIT).",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::HELPER,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "ATN's Ko-fi",
          .section = "About",
          .group = "line_1",
          .tint = 0xFF5A16,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://ko-fi.com/atnthegreat"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "NexusMods",
          .section = "About",
          .group = "line_1",
          .tint = 0x2196F3,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarrome2/mods/38"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "GitHub (Downloads)",
          .section = "About",
          .group = "line_1",
          .tint = 0x6E7681,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://github.com/arshiatn/renodx/releases"); },
      },
  });

  // Section titles and Settings Mode's pages as Attila's (twattila/feature_settings.hpp).
  renodx::utils::settings::default_section_tint = twattila::feature_settings::BASE_TINT;
  std::vector<std::string> engine_sections = {twattila::feature_settings::GRAPHICS_SECTION};
  for (const auto& test : engine_tests) {
    if (std::ranges::find(engine_sections, test.section) == engine_sections.end()) engine_sections.push_back(test.section);
  }
  twattila::feature_settings::SetTints({}, engine_sections);

  // Graphics 1 of 2's layout (as Pharaoh's), ATN's RT/PT preset as the defaults of what the methods leave, then Settings
  // Mode's pages. Fog Shadows keep their 0 (nothing here hooks the fog yet), Smoke Shadows (Experimental) theirs (no game run
  // has shown them yet), Water Caustics its 0 (untested here).
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  for (auto* setting : settings) {
    if (setting->key == "RayTracedFogShadows" || setting->key == "RayTracedSmokeShadows" || setting->key == "WaterCaustics") {
      setting->default_value = 0.f;
    }
    // Torch and fire light in fog and smoke: no fog hooked here and no particle shader reads it.
    if (setting->key == "RayTracedLightGlow") {
      setting->is_visible = [] { return false; };
    }
    // Every reflection here is the water's: the method alone (Water Reflections kept on, unseen). Nothing reads the specular
    // occlusion: its row binds nothing.
    if (setting->key == "RayTracedWaterReflections") {
      setting->default_value = 1.f;
      setting->is_visible = [] { return false; };
    }
    if (setting->key == "RayTracedSpecularOcclusion") {
      setting->binding = nullptr;
      setting->is_visible = [] { return false; };
    }
  }
  // Torch and Fire Shadows from rays (Pharaoh's row), after the preset's defaults: off (no game run has shown the lights read).
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedPointLightShadows",
      .binding = &rt_sliders.local_lights,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Torch and Fire Shadows (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Torches and fires cast their shadows from rays too.\n"
                 "With Path-Traced Global Illumination they also light what the screen never saw.",
      .labels = {"Off", "On"},
  });
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders, .sss = &sss_sliders, .rt = &rt_sliders, .ssao_mode = &shader_injection.ssao_mode,
                                             .light_bounce_strength = &shader_injection.light_bounce_strength,
                                             .ssr_hierarchical = &shader_injection.ssr_hierarchical,
                                             .rays_skip_cascades = &rays_skip_cascades_row,
                                             .engine_code = &engine_code_row,
                                             .texts = &PAGE_TEXTS,
                                             .has_foliage_light = false,
                                             .smoke_shadows_tested = false,
                                             .water_caustics_tested = false,
                                             .lights_own_row = true,
                                             .rays_replace_ao = false,
                                             .rays_multiply_maps = true});
  // Faster Game CPU Code (engine_code_row) Experimental, off.
  for (auto* setting : settings) {
    if (setting->key != "FasterGameCpuCode") continue;
    setting->default_value = 0.f;
    setting->label += " (Experimental)";
  }
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  // Failed native-hook removal can retain this DLL across host unload/reload.
  graphics_found = false;
  graphics_hooks_installed = false;

  ConfigureAddon();

  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  // Scripted runs (RENODX_TW_SET / SHOT / PROFILE), their seconds counted from the first present as Attila's and Pharaoh's.
  twattila::test_hooks::Install(&settings, true);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);

  // Rows saved at an old default, cleared once. Graphics 1 of 2 (SetRaysPresetDefaults) raised these, hidden here until
  // what they act on is hooked: they show then at ATN's values.
  constexpr int SETTINGS_REVISION = 1;
  if (int revision = 0; !reshade::get_config_value(nullptr, "renodx", "SettingsRevision", revision) || revision < SETTINGS_REVISION) {
    for (const char* preset : {"renodx-preset1", "renodx-preset2", "renodx-preset3"}) {
      for (const char* key : {"RayTracedFarTreeShadows", "RayTracedLightImportance", "RayTracedPathKeptCube", "RayTracedSmokeQuality",
                              "RayTracedSpriteShadows", "RayTracedSwayRange"}) {
        reshade::set_config_value(nullptr, preset, key, static_cast<const char*>(nullptr));
      }
    }
    reshade::set_config_value(nullptr, "renodx", "SettingsRevision", SETTINGS_REVISION);
  }

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests: written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(L"empire.retail.dll", &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // Before the Light Bounce's (its callback draws the sun draw again).
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace. lighting_VS_PS as Attila's (WARP sun test,
  // tools/tw/rome2/sun_test): the ambient cube at float 8 (c2-c7, the paths' sky), the sun colour at float 4 (c1, x 0.004
  // with g_hdr_on c11.x: the paths' sun light).
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4);
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  // The frame trace (the data run): renodx_trace_now next to the exe traces the next frame, and FrameTrace's frame also at
  // 100% (twattila/frame_trace.hpp). Constants once each, b0-b5 up to 1 KB (the sun PS's camera b0, shadowmap b1, lighting
  // b2, shroud b3, directional_light b4): the sun draws (standard 340CBE9D, skin 1A0532C0), the tone mapper EE1DA45C, the
  // ultra water 58DC6E12, the SSAO resolve 1ED22F9C, battlefog 259AEC56 and the units' VS A30C5E0B (its
  // variant_animation_size b1); all drawn in the 10-27 trace. VT page_touch 28C847C0 (g_page_touch_constants b2: the
  // render-scale fix's reading) and the torches' VFX 97D98192 (cb_vfx b3's g_viewport_offset_scale: the soft particles'
  // depth read below 100%), drawn in every run of ATN's. The units' VS (A30C5E0B, 4 bones 0F1FBD46: their bones in
  // s_animation_map at t0) with their VS inputs at each draw, and instanced draws' counts apart: no unit was captured on
  // 10-27.
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, {}, {}, twattila::view_share::WHOLE_TEXTURES,
                                   {
                                       .value_hashes = {0x340CBE9D, 0x1A0532C0, 0xEE1DA45C, 0x58DC6E12, 0x1ED22F9C, 0x259AEC56, 0xA30C5E0B,
                                                        0x28C847C0, 0x97D98192},
                                       .value_slots = 6,
                                       .value_floats = 256,
                                       .on_demand = true,
                                       .at_100 = true,
                                       .vertex_input_hashes = {0xA30C5E0B, 0x0F1FBD46},
                                       .instances_apart = true,
                                   });
  // Water Clarity and Caustics at the battle water's draws; Caustics' patch after the render scale's (registered above).
  twattila::water_effects::water_shaders = WATER_SHADERS;
  renodx::utils::pipeline::RegisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  // The ambient occlusion pass before ray tracing's (its result multiplied after), as Attila.
  twattila::ao_pass::Install(AO_DRAWS, &shader_injection, sizeof(shader_injection));
  // The sun's per-frame atlas (8192 square R32) is the map Ray-Traced Shadows Only skips the casters of, never the kept baked
  // map (4096 square R16, also bound at the sun draws: its downsample draws only now and then, A2 trace_09).
  twattila::rt_scene::Host rt_host = twattila::rt_scene::TW_HOST;
  rt_host.cascade_target = [](const D3D11_TEXTURE2D_DESC& desc) {
    return desc.ArraySize == 1 && desc.Width == desc.Height && desc.Format == DXGI_FORMAT_R32_TYPELESS;
  };
  // Ray-Traced Water Reflections: rt_water_surface wrote the water's guides with its depth (TraceWaterReflections); 7C9C8ECF's
  // views are no G-buffer to write them from.
  rt_host.rewrite_guides = [](ID3D11DeviceContext*) {};
  twattila::rt_scene::Install(SCENE_SHADERS, AO_DRAWS, rt_host);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyWaterRaysDevice);
  // Torch and Fire Shadows: one draw a light (VS E98DDC06, POSITION0 only: a 792-index sphere), the light in point_light_PS b2
  // (c0 position, c1 colour and reach); ours drawn in place of the light shaders without the game's cube shadows where the
  // rays are bound (after rt_scene's hook: registration order).
  twattila::rt_scene::InstallPointLights(POINT_LIGHT_SHADERS, 2);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyLightShadowsDevice);
  for (const uint32_t hash : POINT_LIGHT_SHADERS) {
    renodx::utils::command_action::Register(
        OnLightShadowsDraw, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  twattila::rt_scene::InstallSmokeShadows(SMOKE_VERTEX_SHADERS, SMOKE_PIXEL_SHADERS);
  // Ray-Traced Shadows Only (Skip the Shadow Maps): the atlas's casters skipped; its other readers in ATN's runs (the battle
  // water and river, the campaign's sea) take the rays' shadow (WARP tools/tw/rome2/water_r2: t49 unbound bit-identical,
  // t49 = 1 every comparison shadowed).
  twattila::rt_scene::InstallShadowsOnly();
  twattila::screen_space_shadows::PatchForwardShadows(SHADOW_READER_SHADERS);
  twattila::rt_scene::InstallShadowReaders(SHADOW_READER_SHADERS);
  // Last of the sun draws' hooks: it draws the sun draws itself (no callback after it runs).
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);

  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  twattila::test_hooks::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  renodx::utils::command_action::Unregister(twattila::water_effects::OnWaterDraw);
  renodx::utils::pipeline::UnregisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  renodx::utils::command_action::Unregister(OnLightShadowsDraw);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyLightShadowsDevice);
  OnDestroyLightShadowsDevice(nullptr);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyWaterRaysDevice);
  OnDestroyWaterRaysDevice(nullptr);
  twattila::engine_tests::StopAtStart(&at_start_stop);
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  tw::engine_tweaks::Uninstall(graphics_options);
  twattila::engine_code::Uninstall();
  twattila::graphics_hooks::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::ao_pass::Uninstall();
  twattila::rt_scene::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);

  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);

  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);

  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);

  attached = false;
}

}  // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE) {
  addon_module_handle = addon_module;
  if (!addon_registered) {
    if (!reshade::register_addon(addon_module)) return false;
    addon_registered = true;
  }

  return AttachAddon();
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE) {
  DetachAddon();
  twattila::dlaa_bridge::Shutdown();
  if (addon_registered) {
    reshade::unregister_addon(addon_module);
    addon_registered = false;
  }
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      addon_module_handle = h_module;
      if (!reshade::register_addon(h_module)) return FALSE;
      addon_registered = true;
      break;

    case DLL_PROCESS_DETACH:
      // ReShade calls AddonUninit before an explicit unload. During process
      // termination, avoid calling into ReShade or RenoDX under the loader lock.
      if (lpv_reserved == nullptr && addon_registered) {
        reshade::unregister_addon(h_module);
        addon_registered = false;
      }
      break;
  }

  return TRUE;
}
