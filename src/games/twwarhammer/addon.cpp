/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <cmath>
#include <format>
#include <shared_mutex>
#include <string>
#include <utility>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>
// Shadow Casters Out of View: Pharaoh's geometry shaders (Warhammer II's shadow vertex shaders', the same as these).
#include "../twpharaohdynasties/rt_outside_view_unit_gs.h"
#include "../twpharaohdynasties/rt_outside_view_world_gs.h"

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/ao_filter.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "../twattila/water_effects.hpp"
#include "../twattila/test_hooks.hpp"
#include "./more_tests.h"
#include "../twrome2/engine_tweaks.hpp"
#include "../twwarhammer2/line_sweep_ao.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "./shared.h"
#include "./trace_reads.hpp"

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

// Hue Clip Emulation's default: Attila's 40, ATN's choice (2026-11-14). WH1's own tone mappers measured closest to the
// game's SDR picture at 80-90 (test/tw_psychov35 part 5 curve 7).
constexpr float HUE_CLIP_DEFAULT = 40.f;

// bool IsModdedAutoExposureEnabled() {
//   return shader_injection.auto_exposure_highlight_protection == 1.f;
// }

void ApplyHDRPreset(
    float exposure,
    float highlights,
    float shadows,
    float contrast,
    float purity_scale,
    float cone_response_exponent,
    float bloom,
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
      {"tonemapper", 1.f},
      {"gamma_correction", 1.f},
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
      {"godrays", godrays},
      {"vignette", vignette},
      {"lensflare", 1.f},
      {"distortion", distortion},
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
}

void ApplyResetPreset() {
  ApplyHDRPreset(
      1.0f,  // exposure
      1.0f,  // highlights
      1.0f,  // shadows
      1.0f,  // contrast
      1.0f,  // purity_scale
      1.0f,  // cone_response_exponent
      1.0f,  // bloom
      1.0f,  // godrays
      1.0f,  // vignette
      1.0f,  // distortion
      1.0f,  // filmgrain
      0.4f,  // some_indicators
      0.0f,  // auto_exposure_highlight_protection
      1.0f,  // auto_exposure_highlight_headroom_stops
      1.0f,  // vfx_fire_brightness
      1.0f,  // vfx_blood_splash_brightness
      1.0f,  // vfx_fog_brightness
      1.0f,  // vfxsnow
      1.0f,  // vfxrain
      1.0f   // hue_shift
        );
  // No Extended mode here: Reset in PsychoV35 (ATN 2026-11-12).
  renodx::utils::settings::UpdateSettings({{"tonemapper", 3.f}, {"video_autohdr", 0.f}});
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

// Recommended (ATN 2026-11-12: every recommended preset in PsychoV35): Reset, PsychoV35, Video AutoHDR 0.20.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemapper", 3.f}, {"video_autohdr", 0.2f}});
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

// Our copy of one of the game's pixel shaders with the rays applied (shaders/*_0x<hash>), drawn in its place only where
// rt_scene's hook for that draw (registered before this one) bound the rays at `SLOT`; otherwise the game's own bytes run
// (Warhammer II's).
template <uint32_t HASH, UINT SLOT>
struct RaysShader {
  static constexpr uint32_t SHADER = HASH;
  static inline std::span<const uint8_t> code;
  static inline Microsoft::WRL::ComPtr<ID3D11PixelShader> ours;
  static inline Microsoft::WRL::ComPtr<ID3D11PixelShader> game_shader;

  // BuildRuntimeData: our bytes kept for the swap; the game's shader is left as it is.
  static void Take(renodx::mods::shader::CustomShaders* shaders) {
    code = (*shaders)[HASH].code;
    (*shaders)[HASH] = {.crc32 = HASH, .on_inject = [](auto*) { return false; }};
  }

  static constexpr auto OnDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    if (native == nullptr) return {};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rays;
    native->PSGetShaderResources(SLOT, 1, rays.GetAddressOf());
    if (!rays) return {};
    if (!ours) {
      Microsoft::WRL::ComPtr<ID3D11Device> device;
      native->GetDevice(device.GetAddressOf());
      if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, ours.GetAddressOf()))) return {};
      renodx::utils::log::i(std::format("[Total War] First run: {:08X} drawn with the rays (t{}).", HASH, SLOT));
    }
    native->PSGetShader(game_shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
    native->PSSetShader(ours.Get(), nullptr, 0);
    return {.finalize_callback = [](Context& drawn, const void*) {
      auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
      if (drawn_native != nullptr) drawn_native->PSSetShader(game_shader.Get(), nullptr, 0);
      game_shader.Reset();
    }};
  };

  static void OnDestroyDevice(reshade::api::device*) {
    ours.Reset();
    game_shader.Reset();
  }
};

// Torch and Fire Shadows (twattila/rt_scene.hpp InstallPointLights, rt_local_lights.hlsli): the point lights' shaders x each
// light's traced visibility (shaders/pointlight00, 01), the rays' mask at t42. ATN's night siege drew its point lights with
// these two (smooth and linear falloff, VS B5B5BFA4: Warhammer II's D2A0FF23, the same code and signatures).
using PointLightRays = RaysShader<0xD0C67938, 42>;
using LinearPointLightRays = RaysShader<0x5D1695BC, 42>;

// Rays Replace Shadow Maps (Experimental, off): the 18 sun shaders that read the sun's atlas (g_tShadowBuffer, t1 in each;
// tools/tw/wh1/wh1_atlas_reads.py) read it three ways: sample_l and their own `lt receiver, stored` (6), gather4_c through
// s1 (6, CF0C5118 among them), or the read itself as the lit share (6). Where the rays' shadow is bound at t49 a 1 x 1
// texture of 1 (the far plane, a cleared map's value) at t1 makes all three lit, so their patched shadows (each x the rays'
// visibility) give the rays' shadow alone; cloud shadows stay the game's, the maps are still drawn (their forward readers:
// water, forward units). The game's view is bound again after the draw (finalize: the Light Bounce draws the sun draw
// itself and bypasses).
constexpr std::array<uint32_t, 18> ATLAS_SUN_SHADERS = {0x230F2241, 0xC601C862, 0xCF0C5118, 0x4324BBE8, 0xA14D4C74, 0xB8F4B964,
                                                        0x41365626, 0x42114392, 0xA8D2616A, 0xD0F4F425, 0xD56D16EA, 0xD9DDB44A,
                                                        0x34FEF0FF, 0x7BA77953, 0x9AA0E558, 0xA1AF2AEC, 0xB1AFBCF1, 0xCAD9815A};
constexpr UINT ATLAS_SLOT = 1;
float rays_replace_maps_row = 0.f;
// Skip the Shadow Maps (Experimental, off; under Rays Replace Shadow Maps): rt_scene's Ray-Traced Shadows Only skips the
// atlas's casters. Its other readers in ATN's traces (tools/tw/wh1/atlas_readers_wh1.txt: battle and campaign water,
// forward-lit units and props, particles, all gather4_c through their comparison sampler) take the rays' shadow instead
// (screen_space_shadows::PatchForwardShadows with declare_position: 0BF2DB7F / BCB9B1D6 / 7865CDC9 leave SV_Position's xy
// undeclared; rt_scene::InstallShadowReaders).
float rays_skip_cascades_row = 0.f;
// Light Through All Banners (Experimental, off; Warhammer III's row): the units' banners are rigidtextureblend meshes
// (DBCC30FD, and E7F0D71A with 2 bones: ATN's battles draw 20 a frame), the same shaders as other unit parts, so only with
// this row: thin in the rays (DBCC30FD's capture) and in the screen-space shadows (InstallThin; their vertex shaders' outputs
// start with SV_Position, as sss_thin reads, test/tw_warhammer_shaders part 10).
float rays_banner_light_row = 0.f;
// Water Clarity and Caustics (Experimental; twattila/water_effects.hpp, as Rome II's): the battle water 44B4A2F0 (ATN's
// battles), g_sea_bed_light_scatter at its lighting_VS_PS's (b3) c10 the base of the depth's power (asm 673). The campaign's
// sea and lake read c10 otherwise (1 - c10.xyz x c10.w): left out. test/tw_warhammer_shaders part 12.
constexpr std::array<twattila::water_effects::WaterShader, 1> WATER_SHADERS = {{
    {.hash = 0x44B4A2F0, .lighting_slot = 3, .world_input = 3, .refraction_slot = 4, .normal_slot = 3},
}};
// Cut-outs on More Models (Experimental, off): the units' pixel shaders that clip at their base map's alpha 0.5 at TEXCOORD1
// (tools/tw/wh1/cut_out_census_wh1.txt, by their asm: fx__weighted's G-buffer 1ACA3E8C / F4333A3C, alpha overlay F0BB1D3C,
// forward 0BF2DB7F), cut out in the rays through the units' captures with that UV (rt_capture_unit*_cut_out, part 13).
float rays_unit_cut_outs_row = 0.f;
constexpr std::array<twattila::rt_scene::PixelMask, 4> CUT_OUT_MASKS = {{
    {.pixel_shader = 0x0BF2DB7F, .slot = 5, .uv_semantic_index = 1},
    {.pixel_shader = 0x1ACA3E8C, .slot = 0, .uv_semantic_index = 1},
    {.pixel_shader = 0xF0BB1D3C, .slot = 4, .uv_semantic_index = 1},
    {.pixel_shader = 0xF4333A3C, .slot = 0, .uv_semantic_index = 1},
}};
// Skip the Game's Reflection Passes (Experimental, off): under the rays' reflections the resolve's replacement (ssr00) reads
// neither the march's hits (t5) nor the scene's light mips (t6), and nothing else reads them or the hierarchical depth
// (tools/tw/wh1/reader_census_wh1.txt, ATN's 34 frames): those passes go with the march (test part 14). Decided at the depth's
// copy, the first of them, for the frame.
float rays_skip_reflection_passes_row = 0.f;
bool reflection_passes_skipped = false;
// Rays' AO Alone (Experimental, off; Warhammer III's way): the rays' AO in place of the game's SSAO, not on top of it. The SSAO's
// one reader is the sun draws' t_ssao red (the census; rt_ambient_combine multiplies the rays' in): while the rays feed it (this
// frame's trace or the last: these passes come before it), its four passes are left out and the collect's output, what the
// sun reads, cleared to open (test part 15).
float rays_ao_alone_row = 0.f;
bool SkipGameSsao() {
  const auto& runtime = twattila::rt_scene::internal::runtime;
  return rays_ao_alone_row != 0.f && runtime.settings.ambient_occlusion && twattila::dlaa_bridge::bridge.ray_tracing
         && (runtime.traced_frame == runtime.frame || runtime.traced_frame + 1 == runtime.frame);
}
constexpr std::array<twattila::screen_space_shadows::ThinShader, 2> THIN_SHADERS = {{
    {.hash = 0xDBCC30FD, .row = &rays_banner_light_row},
    {.hash = 0xE7F0D71A, .row = &rays_banner_light_row},
}};
constexpr std::array<uint32_t, 7> SHADOW_READER_SHADERS = {0x44B4A2F0, 0x0BF2DB7F, 0xBCB9B1D6, 0x7865CDC9, 0x122F218C, 0x1A3FE75F, 0x0FDF70BF};
struct AtlasLit {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // the stand-in's (compared only)
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> far_plane;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> game_view;  // bound again after the draw
  bool logged = false;
};
AtlasLit atlas_lit;
constexpr auto OnSunReplaceMaps = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (rays_replace_maps_row == 0.f || command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rays;
  native->PSGetShaderResources(twattila::screen_space_shadows::internal::MASK_SLOT, 1, rays.GetAddressOf());
  // With the rays' sun shadows on, t49 holds theirs alone (Bend's are off under them: OnPresent).
  if (!rays || !twattila::rt_scene::SunShadowsTraced()) return {};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = atlas_lit;
  const std::unique_lock lock(state.mutex);
  if (state.device != device.Get()) {
    state.far_plane.Reset();
    state.device = nullptr;
    const float far_plane = 1.f;
    const D3D11_TEXTURE2D_DESC desc = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = &far_plane, .SysMemPitch = 4};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, state.far_plane.GetAddressOf()))) {
      return {};
    }
    state.device = device.Get();
  }
  native->PSGetShaderResources(ATLAS_SLOT, 1, state.game_view.ReleaseAndGetAddressOf());
  native->PSSetShaderResources(ATLAS_SLOT, 1, state.far_plane.GetAddressOf());
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: Rays Replace Shadow Maps: the sun draws' shadow atlas read as lit (the rays shadow).");
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    auto& drawn_state = atlas_lit;
    const std::unique_lock drawn_lock(drawn_state.mutex);
    if (drawn_native != nullptr) drawn_native->PSSetShaderResources(ATLAS_SLOT, 1, drawn_state.game_view.GetAddressOf());
    drawn_state.game_view.Reset();
  }};
};

// Only our stand-in's device (nullptr: the addon's end, DetachAddon).
void OnDestroyAtlasLitDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = atlas_lit;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.far_plane.Reset();
  state.device = nullptr;
}

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      UpgradeRTVReplaceShader(0xD275C403),
      UpgradeRTVReplaceShader(0x10759AE3),
      UpgradeRTVReplaceShader(0xCF1B16D3),
      UpgradeRTVReplaceShader(0xF383AEEE),
      UpgradeRTVReplaceShader(0x3BD5AF1A),
      UpgradeRTVReplaceShader(0x2FF3C8C6),
      UpgradeRTVReplaceShader(0x7947F107),
      UpgradeRTVReplaceShader(0x985D7B00),
      UpgradeRTVReplaceShader(0xDDC1F125),
      UpgradeRTVReplaceShader(0x7759B956),  // t09 constant tone mapper with bloom
      UpgradeRTVReplaceShader(0x9C8E40E6),  // t10 constant tone mapper
      __ALL_CUSTOM_SHADERS,
  };
  // Ambient Occlusion: Visibility Bitmask in place of the AO's horizontal blur: the addon's own shader
  // (ao_bitmask_blur, twwarhammer2/line_sweep_ao.hpp) with the blur's bindings; the blur's replacement stays the game's
  // code (Game's costs what it did). The collect always runs: where no blur runs (Warhammer II at some settings), the
  // game's AO stays, never an unwritten one.
  custom_shaders[0xE3DA9A8D].on_draw = [](auto* cmd_list) {
    if (SkipGameSsao()) return false;
    if (shader_injection.ssao_mode < 1.f) return true;
    return !tw::line_sweep_ao::DispatchInstead(cmd_list, __ao_bitmask_blur, &shader_injection, sizeof(shader_injection),
                                               false, __ao_upsample_blur, shader_injection.ssao_resolution != 0.f);
  };
  // Rays' AO Alone: the sweep 0C83DB37 and blur 4C1918DA left out, the collect 38F6D9B6's output cleared to open in its place.
  custom_shaders[0x0C83DB37].on_draw = [](auto*) { return !SkipGameSsao(); };
  custom_shaders[0x4C1918DA].on_draw = [](auto*) { return !SkipGameSsao(); };
  custom_shaders[0x38F6D9B6].on_draw = [](auto* cmd_list) {
    if (!SkipGameSsao()) return true;
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
    native->CSGetUnorderedAccessViews(0, 1, output.GetAddressOf());
    const std::array<float, 4> open = {1.f, 1.f, 1.f, 1.f};
    if (output) {
      native->ClearUnorderedAccessViewFloat(output.Get(), open.data());
    }
    return false;
  };

  // Reflections: the ray-traced ones in the resolve (ssr00, twattila/rt_scene.hpp, rt_reflection.hlsli); while they
  // replace the screen-space march, the march (raycast) and its blurs are left out.
  custom_shaders[0x6B80E35B].on_draw = [](auto* cmd_list) {
    twattila::rt_scene::BindReflections(cmd_list, true, true);
    return true;
  };
  custom_shaders[0x6B80E35B].on_drawn = [](auto* cmd_list) { twattila::rt_scene::BindReflections(cmd_list, true, false); };
  for (const uint32_t hash : {0x021D7515u, 0x4EBDA7FAu, 0x537CAD4Bu}) {
    custom_shaders[hash].on_draw = twattila::rt_scene::OnReflectionMarch;
  }
  // Skip the Game's Reflection Passes: the hierarchical depth (copy F2CCEF86, gathers 7AA39EE9) and both mip chains
  // (preconvolve copy 4D96DAB5, downscale F9870DE2, blurs E7A82FC2 / 53E74A97); the frame's rays asked for at the copy.
  custom_shaders[0xF2CCEF86].on_draw = [](auto* cmd_list) {
    reflection_passes_skipped = rays_skip_reflection_passes_row != 0.f && !twattila::rt_scene::OnReflectionMarch(cmd_list);
    return !reflection_passes_skipped;
  };
  for (const uint32_t hash : {0x7AA39EE9u, 0x4D96DAB5u, 0xF9870DE2u, 0xE7A82FC2u, 0x53E74A97u}) {
    custom_shaders[hash].on_draw = [](auto*) { return !reflection_passes_skipped; };
  }
  // The point lights' shaders: ours only while the rays are bound (RaysShader above).
  PointLightRays::Take(&custom_shaders);
  LinearPointLightRays::Take(&custom_shaders);

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
        .tooltip = "SDR: the game's SDR tone mapper.\n"
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
    //   new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "My settings for 2500nits peak",
    //     .section = "Presets",
    //     .group = "preset-line-1",
    //     .tooltip = "Settings, I would use with a 2500nits screen.",
    //     .on_change = []() {
    //       ApplyHDRPreset(
    //           1.0f,  // exposure
    //           1.0f,  // highlights
    //           1.0f,  // shadows
    //           1.0f,  // contrast
    //           1.0f,  // purity_scale
    //           1.0f,  // cone_response_exponent
    //           1.0f,  // bloom
    //           1.0f,  // godrays
    //           0.0f,  // vignette
    //           0.5f,  // distortion
    //           0.0f,  // filmgrain
    //           0.4f,  // some_indicators
    //           1.0f,  // auto_exposure_highlight_protection
    //           0.4f,  // auto_exposure_highlight_headroom_stops
    //           6.0f,  // vfx_fire_brightness
    //           6.0f,  // vfx_blood_splash_brightness
    //           0.7f,  // vfx_fog_brightness
    //           1.5f,  // vfxsnow
    //           1.5f,  // vfxrain
    //           2.0f   // hue_shift
    //       );
    //     },
    // },
    //     new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "My settings for 1000nits peak",
    //     .section = "Presets",
    //     .group = "preset-line-1",
    //     .tooltip = "Settings, I would use with a 2500nits screen.",
    //     .on_change = []() {
    //       ApplyHDRPreset(
    //           1.0f,  // exposure
    //           1.0f,  // highlights
    //           1.0f,  // shadows
    //           1.0f,  // contrast
    //           1.0f,  // purity_scale
    //           1.0f,  // cone_response_exponent
    //           1.0f,  // bloom
    //           1.0f,  // godrays
    //           0.0f,  // vignette
    //           0.5f,  // distortion
    //           0.0f,  // filmgrain
    //           0.4f,  // some_indicators
    //           1.0f,  // auto_exposure_highlight_protection
    //           0.4f,  // auto_exposure_highlight_headroom_stops
    //           2.5f,  // vfx_fire_brightness
    //           3.0f,  // vfx_blood_splash_brightness
    //           0.7f,  // vfx_fog_brightness
    //           1.3f,  // vfxsnow
    //           1.3f,  // vfxrain
    //           2.0f   // hue_shift
    //       );
    //     },
    // },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "HDR (PsychoV31) and Video AutoHDR 0.20, the rest at its default.",
        .on_change = []() { ApplyRecommendedPreset(); },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Resets all image tuning and VFX sliders to their default HDR values.",
        .on_change = []() {
          ApplyResetPreset();
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
        .default_value = 2.0f,
        .label = "Highlight Hue Shift",
        .section = "Psycho V30",
        .tooltip = "Hue shift of bright highlights, like SDR: fire and explosions turn orange/yellow instead of pink/red.\n0 = none, 1 = PsychoV30 default (like my old addons), 2 = full.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
        .is_visible = IsPsychoV30Mode,
    },

    ///EXTRA-----////

    // new renodx::utils::settings::Setting{
    //     .key = "bloom",
    //     .binding = &shader_injection.bloom,
    //     .default_value = 1.f,
    //     .label = "Bloom",
    //     .section = "Extra",
    //     .tooltip = "Bloom multiplier.",
    //     .min = 0.f,
    //     .max = 1000.f,
    //     .format = "%.2f",
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "godrays",
    //     .binding = &shader_injection.godrays,
    //     .default_value = 1.f,
    //     .label = "Godrays",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on godrays.  Needs in-game Sun Rays setting on.",
    //     .min = 0.f,
    //     .max = 2.f,
    //     .format = "%.2f",
    //     .is_enabled = IsHDRMode,  
    //     .is_visible = IsAdvancedSettings,
    // },
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

    new renodx::utils::settings::Setting{
        .key = "lensflare",
        .binding = &shader_injection.lensflare,
        .default_value = 1.f,
        .label = "Lens Flare",
        .section = "Extra",
        .tooltip = "Sun lens flares. 0 = off.",
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
        .max = 1.f,
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
    new renodx::utils::settings::Setting{
        .key = "sky_smooth_strength",
        .binding = &shader_injection.sky_smooth_strength,
        .default_value = 1.f,
        .label = "Fog & Sky Smoothing Strength",
        .section = "Extra",
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
        .section = "Extra",
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
        .section = "Extra",
        .tooltip = "Smooths the sky's banding and adds fine dither.\n"
                   "Higher: removes wider bands, softens sky details more.",
        .labels = {"Off", "Low", "Medium", "High"},
    },
    // new renodx::utils::settings::Setting{
    //     .key = "filmgrain",
    //     .binding = &shader_injection.filmgrain,
    //     .default_value = 1.f,
    //     .label = "Filmgrain",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on filmgrain. Needs in-game setting on.",
    //     .min = 0.f,
    //     .max = 2.f,
    //     .format = "%.2f",
    //     .is_enabled = IsHDRMode,
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "some_indicators",
    //     .binding = &shader_injection.someindicators,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.4f,
    //     .label = "Brightness of some Unit/Building Indicator ",
    //     .section = "Extra",
    //     .tooltip = "Brightness of selected highlight indicators, particularly useful at night.",
    //     .min = 0.0f,
    //     .max = 1.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    


    // Auto Exposure //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "auto_exposure_highlight_protection",
    //     .binding = &shader_injection.auto_exposure_highlight_protection,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 0.0f,
    //     .label = "Modded Auto Exposure",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Reduces the exposure influence of isolated highlights such as the sun, fire, and sparks without reducing their rendered brightness. Off is stock.",
    //     .labels = {"Off (Stock)", "On (Protected)"},
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "auto_exposure_highlight_headroom_stops",
    //     .binding = &shader_injection.auto_exposure_highlight_headroom_stops,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Modded Auto Exposure: Sensitivity",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Maximum metered highlight above the frame average, in stops. Lower values protect exposure more strongly. Higher values are closer to stock.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    //     .is_enabled = IsModdedAutoExposureEnabled,
    //     .is_visible = IsAdvancedSettings,
    // },
    // VFX //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fire_brightness",
    //     .binding = &shader_injection.vfxfirebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: fire/smoke/unit's dust",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Fire, embers, sparks, smoke, and .... Recommmendation is max 10 with modded auto exposure.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.0f",
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_blood_splash_brightness",
    //     .binding = &shader_injection.vfxbloodsplash,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Blood splash",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Bloodsplash brightness. Recommmendation is max 10.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.0f",
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fog_brightness",
    //     .binding = &shader_injection.vfxfogbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Storms (Sand/Fog)",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of the fog color applied to particles. Recommmendation is max 1.",
    //     .min = 0.1f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    //   new renodx::utils::settings::Setting{
    //     .key = "vfxsnow",
    //     .binding = &shader_injection.vfxsnow,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Snowflakes",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of snow. Recommmendation is max 5.",
    //     .min = 0.1f,
    //     .max = 20.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxrain",
    //     .binding = &shader_injection.vfxrain,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: rain",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of rain. Recommmendation is max 5.",
    //     .min = 0.1f,
    //     .max = 20.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
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


// Graphics (Experimental): engine values x the sliders (their tweaks are filled in ConfigureAddon).
tw::engine_tweaks::Option unit_detail[5];  // very small, small, medium, large, very large units
tw::engine_tweaks::Option unit_animation;
tw::engine_tweaks::Option unit_variety;
tw::engine_tweaks::Option footstep_effects;
tw::engine_tweaks::Option tree_detail;
tw::engine_tweaks::Option bush_detail;
tw::engine_tweaks::Option terrain_detail;
tw::engine_tweaks::Option terrain_draw;
tw::engine_tweaks::Option campaign_detail;
tw::engine_tweaks::Option campaign_draw;
tw::engine_tweaks::Option shadow_distance;
tw::engine_tweaks::Option small_shadows;
tw::engine_tweaks::Option unit_shadows;
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
// Texture LOD Bias on All Textures (Graphics 1 of 2, Experimental): the trilinear samplers too
// (texture_lod_bias.hpp SetWrappingTrilinear).
float lod_bias_trilinear_row = 0.f;
// The shadow pass's casters in the rays' scene (SCENE_SHADERS' cascade_keep entries): Shadow Casters Out of View
// (Experimental, off: no game run has shown them yet).
float rays_units_out_of_view_row = 0.f;

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
bool graphics_found = false;
// The graphics watch's values (4 hardware breakpoints).
constexpr std::array<std::string_view, 4> WATCHED_VALUES = {"tree_lod_scaler", "scrub_lod_distance_default", "campaign_culling_ratio", "terrain_visible_tiles"};

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
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
}

// DLAA / DLSS object motion (twattila/anti_aliasing.hpp, as Attila's): the game's shaders whose things move on
// their own and their motion versions (tools\tw\attila_motion\port_motion.py, GPU-checked by test_motion.py).
using twattila::anti_aliasing::MotionKind;
constexpr std::array<twattila::anti_aliasing::MotionShader, 33> MOTION_SHADERS = {{
    {.hash = 0x0EA8B7F1, .motion_shader = __motion_unit_weighted_0ea8, .kind = MotionKind::UNIT},
    {.hash = 0x1857BB54, .motion_shader = __motion_ocean_shallow_ocean2_1857, .kind = MotionKind::HISTORY, .history_textures = 0b101},
    {.hash = 0x1A7891F6, .motion_shader = __motion_tree_campaignvegetation_1a78},
    {.hash = 0x1CF62070, .motion_shader = __motion_grass_grass_1cf6},
    {.hash = 0x20A9E5FD, .motion_shader = __motion_unit_weighted_20a9, .kind = MotionKind::UNIT},
    {.hash = 0x2335CB90, .motion_shader = __motion_unit_weighted4_wind_alpha_xml_2335, .kind = MotionKind::UNIT},
    {.hash = 0x29F7B3D7, .motion_shader = __motion_branch_branch_v3_29f7},
    {.hash = 0x2F6FFDA0, .motion_shader = __motion_unit_weighted_2f6f, .kind = MotionKind::UNIT},
    {.hash = 0x306EDC60, .motion_shader = __motion_unit_weighted_306e, .kind = MotionKind::UNIT},
    {.hash = 0x308BB4D5, .motion_shader = __motion_leaf_leaf_v3_308b},
    {.hash = 0x327CC0DB, .motion_shader = __motion_branch_branch_v3_327c},
    {.hash = 0x3F139F3D, .motion_shader = __motion_unit_weighted_3f13, .kind = MotionKind::UNIT},
    {.hash = 0x3FC5424C, .motion_shader = __motion_unit_weighted_3fc5, .kind = MotionKind::UNIT},
    {.hash = 0x4CB13100, .motion_shader = __motion_sail_sailsymbol_4cb1, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sailsymbol_4cb1},
    {.hash = 0x51EDFD6E, .motion_shader = __motion_rigid_rigidstandard_51ed, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidstandard_51ed},
    {.hash = 0x52C2D565, .motion_shader = __motion_sail_sail_52c2, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sail_52c2},
    {.hash = 0x77A5A3E5, .motion_shader = __motion_ocean_deep_ocean2_77a5, .kind = MotionKind::HISTORY, .history_textures = 0b1},
    {.hash = 0x7BD6E8CB, .motion_shader = __motion_rope_rope_7bd6, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope_rope_7bd6},
    {.hash = 0x84797031, .motion_shader = __motion_tree_campaignvegetation_8479},
    {.hash = 0x851C6D2A, .motion_shader = __motion_grass_grass_851c},
    {.hash = 0x86460214, .motion_shader = __motion_unit_weighted2_emissive_wind_8646, .kind = MotionKind::UNIT},
    {.hash = 0x8CC6A6C1, .motion_shader = __motion_ocean_coast_ocean2_8cc6, .kind = MotionKind::HISTORY, .history_textures = 0b10101},
    {.hash = 0xA8612F21, .motion_shader = __motion_sail_sailsymbol_a861, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sailsymbol_a861},
    {.hash = 0xC1334DA8, .motion_shader = __motion_unit_weightedwithburn_c133, .kind = MotionKind::UNIT},
    {.hash = 0xC1E79772, .motion_shader = __motion_rigid_rigidtextureblendwind_c1e7, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblendwind_c1e7},
    {.hash = 0xD26C7B7A, .motion_shader = __motion_branch_branch_v3_d26c},
    {.hash = 0xD2CBAF72, .motion_shader = __motion_oldocean_ocean_d2cb},
    {.hash = 0xDA2B125C, .motion_shader = __motion_unit_weightedwithburn_da2b, .kind = MotionKind::UNIT},
    {.hash = 0xDBCC30FD, .motion_shader = __motion_rigid_rigidtextureblend_dbcc, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblend_dbcc},
    {.hash = 0xE4B46D0E, .motion_shader = __motion_sail_sail_e4b4, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sail_e4b4},
    {.hash = 0xEDAC68AD, .motion_shader = __motion_leaf_leaf_v3_edac},
    {.hash = 0xF336AA76, .motion_shader = __motion_leaf_leaf_v3_f336},
    {.hash = 0xF6DBA4F4, .motion_shader = __motion_rigid_rigidambientmapfiremesh_f6db, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidambientmapfiremesh_f6db},
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl's sun light draws, their camera at b2 (the reflection's at b1),
// the G-buffer's channels 1-4 from their first slot.
// Ray tracing's scene (twattila/rt_scene.hpp; tools\tw\rt_port.py): the main view's terrain, rigid objects, tree
// branches and leaves and units, and their capture versions (world positions; leaves also UVs).
// The floating unit banners (rigidtextureblend, 2 bones, E7F0D71A) stay out: the game gives them no shadow (no atlas draw in
// any of ATN's 2026-11-14 traces). So does the campaign's far terrain (rigidterrain geometry_41_global, CDCF1ADD): no atlas
// draw either, and no trace shows where it lies against the near terrain.
constexpr std::array<twattila::rt_scene::SceneShader, 35> SCENE_SHADERS = {{
    {.hash = 0x10401C91, .capture_shader = __rt_capture_terrain_geom_new, .terrain = true},
    {.hash = 0x06AB0766, .capture_shader = __rt_capture_terrain_geom_new_sm41, .terrain = true},
    {.hash = 0x5696080B, .capture_shader = __rt_capture_terrain_custom, .terrain = true},
    {.hash = 0x2A7775B0, .capture_shader = __rt_capture_terrain_custom_material, .terrain = true},
    {.hash = 0x7858BE0F, .capture_shader = __rt_capture_terrain_custom_sm41, .terrain = true},
    {.hash = 0xBDCF1803, .capture_shader = __rt_capture_terrain_custom_material_sm41, .terrain = true},
    {.hash = 0x51EDFD6E, .capture_shader = __rt_capture_rigid_standard, .rows_shader = __motion_record_rigid_rigidstandard_51ed},
    {.hash = 0xF6DBA4F4, .capture_shader = __rt_capture_rigid_standard_sm41, .rows_shader = __motion_record_rigid_rigidambientmapfiremesh_f6db},
    {.hash = 0x875BB987, .capture_shader = __rt_capture_rigid_default},
    {.hash = 0xD1A96B76, .capture_shader = __rt_capture_rigid_vertexpush},
    {.hash = 0xDBCC30FD, .capture_shader = __rt_capture_rigid_blend, .rows_shader = __motion_record_rigid_rigidtextureblend_dbcc, .thin_row = &rays_banner_light_row},
    {.hash = 0xC1E79772, .capture_shader = __rt_capture_rigid_blend_wind, .rows_shader = __motion_record_rigid_rigidtextureblendwind_c1e7},
    {.hash = 0x29F7B3D7, .capture_shader = __rt_capture_branch},
    {.hash = 0xD26C7B7A, .capture_shader = __rt_capture_branch_sm40},
    {.hash = 0x327CC0DB, .capture_shader = __rt_capture_branch_sm41},
    {.hash = 0xF336AA76, .capture_shader = __rt_capture_leaf, .foliage = true},
    {.hash = 0x308BB4D5, .capture_shader = __rt_capture_leaf_sm40, .foliage = true},
    {.hash = 0xEDAC68AD, .capture_shader = __rt_capture_leaf_sm41, .foliage = true},
    {.hash = 0x20A9E5FD, .capture_shader = __rt_capture_unit, .cut_out_shader = __rt_capture_unit_cut_out, .clip_cut_outs = true, .cut_out_row = &rays_unit_cut_outs_row, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},
    {.hash = 0x3FC5424C, .capture_shader = __rt_capture_unit_4_bones, .cut_out_shader = __rt_capture_unit_4_bones_cut_out, .clip_cut_outs = true, .cut_out_row = &rays_unit_cut_outs_row, .dynamic = true, .uv_semantic = "TEXCOORD", .uv_semantic_index = 1},
    {.hash = 0x0EA8B7F1, .capture_shader = __rt_capture_unit_sm41, .dynamic = true},
    {.hash = 0x2F6FFDA0, .capture_shader = __rt_capture_unit_4_bones_sm41, .dynamic = true},
    {.hash = 0x3F139F3D, .capture_shader = __rt_capture_unit_4_bones_dismember, .dynamic = true},
    {.hash = 0x306EDC60, .capture_shader = __rt_capture_unit_4_bones_dismember_sm41, .dynamic = true},
    {.hash = 0xC1334DA8, .capture_shader = __rt_capture_unit_dismember, .dynamic = true},
    {.hash = 0xDA2B125C, .capture_shader = __rt_capture_unit_dismember_sm41, .dynamic = true},
    {.hash = 0x86460214, .capture_shader = __rt_capture_unit_emissive_wind, .dynamic = true},
    {.hash = 0x2335CB90, .capture_shader = __rt_capture_unit_4_bones_wind, .dynamic = true},
    {.hash = 0x1CF62070, .capture_shader = __rt_capture_grass_1cf6, .dynamic = true, .foliage = true, .grass = true},
    {.hash = 0x851C6D2A, .capture_shader = __rt_capture_grass_851c, .dynamic = true, .foliage = true, .grass = true},
    // The campaign's plants and forests (rigidcampaignvegetation vs40_main; ATN's trace 13: 116 draws), placed, standing and
    // still as Attila's, cut out by their diffuse map's alpha (73C9A8D6 t1).
    {.hash = 0x84797031, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true, .mask_slot = 1},
    // Far trees' cards (rigidtreebillboard vs40_main; ATN's battles: 18-20 draws a frame), turned to the sun (Far Tree Shadows),
    // cut out by their tree map's alpha (C5875F67 t3).
    {.hash = 0xCA6BCF19, .capture_shader = __rt_capture_far_tree, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 3},
    // The shadow pass's casters into the atlas, what is out of the main view (Shadow Casters Out of View): fx__rigidcommon
    // vs40_shadow, fx__weighted vs40_shadow and its 4 bones, Warhammer II's E2C36310, 2EE37868, 2A54DBEE (the same code and
    // signatures), so Pharaoh's rt_outside_view as there. Every frame as drawn.
    {.hash = 0x0E0E36F6, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0x2E5BBBA1, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x63BA47F1, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
}};

// The game's passes that read render-size textures at the display's size (texture slots), below 100%.
constexpr std::array<twattila::view_share::RegionRead, 2> RENDER_SCALE_READS = {{
    {.shader_hash = 0x404B4343, .slots = 0b11},  // shadow depth analysis
    // environment_specular_light ps40_copy: the SSR resolve's output (display-size, the view in its share) added to the scene
    // at the render viewport (ATN's DLSS Performance / Ultra Performance traces, 2026-11-14 03:30).
    {.shader_hash = 0x69A57B14, .slots = 0b1},
}};

// The 31 sun light shaders. Their diffuse ambient is the spherical harmonics (t_world_spherical_harmonics, if on
// lighting_VS_PS c11.z: on in every battle and campaign frame of ATN's 2026-11-14 traces) or the ambient cube; the
// ambient patch sits after both (AMBIENT_AFTER_HARMONICS_Z) and rt_sun reads the harmonics (sky_harmonics_slot, the RDEFs).
using twattila::light_bounce::AMBIENT_AFTER_HARMONICS_Z;
constexpr std::array<twattila::light_bounce::SunDraw, 31> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0x230F2241, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},
    {.shader_hash = 0x27AF3D62, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},
    {.shader_hash = 0x2B3F8DC8, .standard = true, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 2},
    {.shader_hash = 0xC601C862, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},
    {.shader_hash = 0xCF0C5118, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},
    {.shader_hash = 0x9547FB5E, .standard = true, .gbuffer_slot = 3, .camera_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 1},
    {.shader_hash = 0x1CE77E85, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 2},
    {.shader_hash = 0x4324BBE8, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},
    {.shader_hash = 0xA14D4C74, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},
    {.shader_hash = 0xA359A234, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},
    {.shader_hash = 0xB8F4B964, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x41365626, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // slm_edges
    {.shader_hash = 0x42114392, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // slm_interior
    {.shader_hash = 0x7BC58986, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // slm_edges
    {.shader_hash = 0x8A72EA4D, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // slm_interior
    {.shader_hash = 0xA8D2616A, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // slm_edges
    {.shader_hash = 0xC65915B6, .standard = true, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 2},  // slm_interior
    {.shader_hash = 0xD0F4F425, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // slm_interior
    {.shader_hash = 0xD56D16EA, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // slm_edges
    {.shader_hash = 0xD9DDB44A, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // slm_interior
    {.shader_hash = 0xDA9B8767, .standard = true, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 2},  // slm_edges
    {.shader_hash = 0x0C2B2229, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 2},  // skinlm_interior
    {.shader_hash = 0x0EADBE7E, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // skinlm_interior
    {.shader_hash = 0x110884BF, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // skinlm_edges
    {.shader_hash = 0x34FEF0FF, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // skinlm_interior
    {.shader_hash = 0x7BA77953, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // skinlm_edges
    {.shader_hash = 0x8647AA2A, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 2},  // skinlm_edges
    {.shader_hash = 0x9AA0E558, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // skinlm_interior
    {.shader_hash = 0xA1AF2AEC, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // skinlm_edges
    {.shader_hash = 0xB1AFBCF1, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // skinlm_edges
    {.shader_hash = 0xCAD9815A, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2, .ambient_final_sites = AMBIENT_AFTER_HARMONICS_Z, .sky_harmonics_slot = 3},  // skinlm_interior
}};

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // Before any setting is read this frame (as Attila's): DLSS never starts on another vendor's GPU, a saved DLSS or a
  // preset's is XeSS on Intel's, else FSR.
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  reflection_passes_skipped = false;
  // GT-VBAO's Temporal Smoothing (its noise moving each frame) and Denoiser (twattila/ao_filter.hpp).
  const bool bitmask_ao = shader_injection.ssao_mode >= 1.f;
  static uint32_t ao_frame = 0;
  shader_injection.ssao_frame =
      (bitmask_ao && shader_injection.ssao_temporal != 0.f) ? static_cast<float>(++ao_frame % 64) : 0.f;
  twattila::ao_filter::Update(swapchain, {.temporal = bitmask_ao && shader_injection.ssao_temporal != 0.f,
                                          .edge_fix = shader_injection.ssao_edge_fix != 0.f,
                                          .denoise = bitmask_ao && shader_injection.ssao_denoise != 0.f});
  twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
  rt_settings.casters_out_of_view = rt_settings.sun_shadows && rays_units_out_of_view_row != 0.f;
  // Skip the Shadow Maps: Ray-Traced Shadows Only's skip of the atlas's casters (with the rays' sun shadows alone).
  rt_settings.shadows_only = rt_settings.sun_shadows && rays_replace_maps_row != 0.f && rays_skip_cascades_row != 0.f;
  twattila::texture_lod_bias::SetWrappingTrilinear(lod_bias_trilinear_row != 0.f);
  // Ray-traced shadows replace the screen-space ones too (Graphics 1 of 2's DXR: in place of Bend's).
  auto sss_settings = twattila::feature_settings::SssSettings(sss_sliders);
  sss_settings.enabled = sss_settings.enabled && !rt_settings.sun_shadows;
  twattila::screen_space_shadows::Update(swapchain, sss_settings);
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: Warhammer by ATN";  // TODO: change me!

namespace {

// Graphics 1 of 2's (twattila/graphics_page.hpp): what is true here (as Warhammer II's). The rays' sun shadows multiply the
// game's shadow maps (the sun shaders' patched comparisons), ray-traced AO multiplies the game's SSAO (its line sweeps);
// reflections on the game's SSR resolve; no point lights, fog, water or smoke hooked.
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = twattila::graphics_page::ATTILA_TEXTS.aa_labels,
    .aa_tooltip = twattila::graphics_page::ATTILA_TEXTS.aa_tooltip,
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its ambient occlusion,\n"
                     "shadows and reflections. The rest at its default.",
    .no_rays_tooltip = "Bend Screen-Space Shadows (Length 480), GT-VBAO and GT-VBGI at their Recommended settings.\n"
                       "The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows, reflections and ambient occlusion, path-traced Global Illumination at my settings.\n"
                    "The rest of this page at its default. Needs an upscaler (Anti-Aliasing above).\n"
                    "On NVIDIA also turns DLSS Ray Reconstruction on. Elsewhere Experimental, slower: its own denoisers.",
    .shadow_tooltip = "Game's Shadow Maps: the original. Bend Screen-Space Shadows: those plus fine contact shadows from\n"
                      "what is on screen (Bend Studio's; soldiers, weapons, grass; needs in-game shadows on).\n"
                      "DXR: DirectX Raytracing, the sun's shadow from rays too, on top of the game's, in place of Bend's.\n"
                      "Needs an upscaler.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep in-game SSAO on for all of these.\n"
                  "Game's SSAO: everything counts as solid, so soldiers and trees get dark halos.\n"
                  "GT-VBAO (Ground Truth AO with a visibility bitmask): objects have a thickness, so light gets behind\n"
                  "thin ones.\n"
                  "RTAO: from rays through the scene, on top of the game's SSAO. Needs an upscaler.",
    .ao_paths_label = "Path-Traced (ReSTIR GI, Experimental)",
    .specular_occlusion_label = "Ray-Traced Specular Occlusion (Experimental)",
    .water_caustics_label = "Water Caustics (Experimental)",
    .replace_maps_tooltip = "The sun's shadows from rays alone, so Softness shows in full. Cloud shadows stay the game's;\n"
                            "what is outside the view casts none. Off: the rays' shadows on top of the game's shadow maps.",
    .reflections_tooltip = "Reflections on water, armour and metal. Need in-game Screen Space Reflections on.\n"
                           "Game's SSR: the original. DXR: from rays, also what is off screen or hidden. Needs an upscaler.",
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
          .old_format = reshade::api::format::r11g11b10_float,
          .new_format = reshade::api::format::r16g16b16a16_float,
          .ignore_size = false,
          .use_resource_view_cloning = true,  // TODO: needed?
          .use_resource_view_hot_swap = true,
          .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
          .usage_include = reshade::api::resource_usage::render_target,
      });

  // Appended Settings (Graphics (Experimental))
  unit_detail[0].tweaks = {{.name = "VERY_SMALL_IMPOSTER_DISTANCE"}};
  unit_detail[1].tweaks = {{.name = "SMALL_IMPOSTER_DISTANCE"}};
  unit_detail[2].tweaks = {{.name = "MEDIUM_IMPOSTER_DISTANCE"}};
  unit_detail[3].tweaks = {{.name = "LARGE_IMPOSTER_DISTANCE"}};
  unit_detail[4].tweaks = {{.name = "VERY_LARGE_IMPOSTER_DISTANCE"}};
  unit_animation.tweaks = {
      {.name = "VERY_SMALL_SHARED_POSE_DISTANCE"},
      {.name = "SMALL_SHARED_POSE_DISTANCE"},
      {.name = "MEDIUM_SHARED_POSE_DISTANCE"},
      {.name = "LARGE_SHARED_POSE_DISTANCE"},
      {.name = "VERY_LARGE_SHARED_POSE_DISTANCE"},
      {.name = "VERY_SMALL_FACIAL_ANIMATION_DISTANCE"},
      {.name = "SMALL_FACIAL_ANIMATION_DISTANCE"},
      {.name = "MEDIUM_FACIAL_ANIMATION_DISTANCE"},
      {.name = "LARGE_FACIAL_ANIMATION_DISTANCE"},
      {.name = "VERY_LARGE_FACIAL_ANIMATION_DISTANCE"},
      {.name = "VERY_SMALL_HANDS_ANIMATION_DISTANCE"},
      {.name = "SMALL_HANDS_ANIMATION_DISTANCE"},
      {.name = "MEDIUM_HANDS_ANIMATION_DISTANCE"},
      {.name = "LARGE_HANDS_ANIMATION_DISTANCE"},
      {.name = "VERY_LARGE_HANDS_ANIMATION_DISTANCE"},
  };
  unit_variety.tweaks = {
      {.name = "VERY_SMALL_NO_VARIANT_DISTANCE"},
      {.name = "SMALL_NO_VARIANT_DISTANCE"},
      {.name = "MEDIUM_NO_VARIANT_DISTANCE"},
      {.name = "LARGE_NO_VARIANT_DISTANCE"},
      {.name = "VERY_LARGE_NO_VARIANT_DISTANCE"},
  };
  footstep_effects.tweaks = {
      {.name = "VERY_SMALL_FOOTSTEP_VFX_DISTANCE"},
      {.name = "SMALL_FOOTSTEP_VFX_DISTANCE"},
      {.name = "MEDIUM_FOOTSTEP_VFX_DISTANCE"},
      {.name = "LARGE_FOOTSTEP_VFX_DISTANCE"},
      {.name = "VERY_LARGE_FOOTSTEP_VFX_DISTANCE"},
  };
  // Only tweaks the game's code reads (tools\tw\tw_tweak_survey.py): battle_entity_lod*, battle_building_lod*,
  // campaign_tree_lod* and campaign_grass_lod* are never read (Model / Building Detail removed).
  tree_detail.tweaks = {
      // The tree display's LOD distance factor (1.35), on the camera distance: lower keeps detail farther.
      {.name = "tree_lod_scaler", .inverse = true},
  };
  bush_detail.tweaks = {
      // Scrub LOD distance (175 m), copied into each scrub when it loads.
      {.name = "scrub_lod_distance_default"},
  };
  terrain_detail.tweaks = {
      {.name = "terrain_far_shader_distance"},
  };
  terrain_draw.tweaks = {
      {.name = "terrain_visible_tiles"},
  };
  campaign_detail.tweaks = {
      {.name = "rigid_campaign_tree_lod"},
  };
  campaign_draw.tweaks = {
      // The campaign map's culling: objects smaller than this share of the view (0.003) are not drawn.
      {.name = "campaign_culling_ratio", .inverse = true},
  };
  shadow_distance.tweaks = {
      {.name = "shadowmap_split_scale"},
  };
  small_shadows.tweaks = {
      // Objects smaller than this share of their distance (0.02) cast no shadow.
      {.name = "cull_small_shadow_size", .inverse = true},
  };
  unit_shadows.tweaks = {
      {.name = "VERY_SMALL_IMPOSTER_SHADOW_DISTANCE"},
      {.name = "SMALL_IMPOSTER_SHADOW_DISTANCE"},
      {.name = "MEDIUM_IMPOSTER_SHADOW_DISTANCE"},
      {.name = "LARGE_IMPOSTER_SHADOW_DISTANCE"},
      {.name = "VERY_LARGE_IMPOSTER_SHADOW_DISTANCE"},
      {.name = "entity_display_shadow_max_shadow_distance"},
  };
  // Graphics 2 of 2's tests: Attila's that this game has, as it has them (without the values it lacks, its own game values
  // in the tooltips), then More Tests (more_tests.h).
  engine_tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), more_tests::ATTILA_TESTS);
  for (auto& test : engine_tests) {
    std::erase_if(test.option.tweaks, [](const tw::engine_tweaks::Tweak& tweak) {
      return std::ranges::find(more_tests::MISSING_VALUES, tweak.name) != more_tests::MISSING_VALUES.end();
    });
    if (const auto tooltip = std::ranges::find(more_tests::TOOLTIPS, test.key, &std::pair<std::string, std::string>::first); tooltip != more_tests::TOOLTIPS.end()) {
      test.tooltip = tooltip->second;
    }
  }
  {
    auto more = more_tests::MoreTests();
    engine_tests.insert(engine_tests.end(), more.begin(), more.end());
  }
  graphics_options = {&unit_detail[0], &unit_detail[1], &unit_detail[2], &unit_detail[3], &unit_detail[4], &unit_animation, &unit_variety, &footstep_effects, &tree_detail, &bush_detail, &terrain_detail, &terrain_draw, &campaign_detail, &campaign_draw, &shadow_distance, &small_shadows, &unit_shadows, &shadow_map};
  for (auto& test : engine_tests) graphics_options.push_back(&test.option);
  graphics_status = new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Engine values: not searched yet",
      .section = "Graphics (Experimental)",
  };
  // Graphics presets (ATN): MAX (every graphics setting at its maximum) and Reset (all graphics settings and tests at their
  // defaults, as Attila's; not the ambient occlusion or the Light Bounce).
  shadow_map.tweaks = {
      {.name = "shadowmap_extreme_quality", .flag = true},
  };
  std::vector<std::pair<std::string, float>> graphics_defaults = {
      {"GraphicsShadowMapSize", 0.f},
      {"GraphicsUnitDetailVerySmall", 1.f},
      {"GraphicsUnitDetailSmall", 1.f},
      {"GraphicsUnitDetailMedium", 1.f},
      {"GraphicsUnitDetailLarge", 1.f},
      {"GraphicsUnitDetailVeryLarge", 1.f},
      {"GraphicsUnitAnimation", 1.f},
      {"GraphicsUnitVariety", 1.f},
      {"GraphicsFootstepEffects", 1.f},
      {"GraphicsTreeDetail", 1.f},
      {"GraphicsBushDetail", 1.f},
      {"GraphicsTerrainDetail", 1.f},
      {"GraphicsTerrainDraw", 1.f},
      {"GraphicsCampaignDetail", 1.f},
      {"GraphicsCampaignDraw", 1.f},
      {"GraphicsShadowDistance", 1.f},
      {"GraphicsSmallShadows", 1.f},
      {"GraphicsUnitShadows", 1.f},
  };
  for (const auto& test : engine_tests) {
    // Its option's start: the game's (1x, Game's or -1).
    graphics_defaults.emplace_back(test.key, test.option.scale);
  }
  const std::vector<std::pair<std::string, float>> graphics_max = {
      {"GraphicsShadowMapSize", 1.f},
      {"GraphicsUnitDetailVerySmall", 8.f},
      {"GraphicsUnitDetailSmall", 8.f},
      {"GraphicsUnitDetailMedium", 8.f},
      {"GraphicsUnitDetailLarge", 8.f},
      {"GraphicsUnitDetailVeryLarge", 8.f},
      {"GraphicsUnitAnimation", 4.f},
      {"GraphicsUnitVariety", 4.f},
      {"GraphicsFootstepEffects", 4.f},
      {"GraphicsTreeDetail", 4.f},
      {"GraphicsBushDetail", 4.f},
      {"GraphicsTerrainDetail", 4.f},
      {"GraphicsTerrainDraw", 4.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsCampaignDraw", 8.f},
      {"GraphicsShadowDistance", 4.f},
      {"GraphicsSmallShadows", 8.f},
      {"GraphicsUnitShadows", 4.f},
  };
  settings.insert(settings.end(), {
      tw::engine_tweaks::Note(),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting and test back to its default.",
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
      tw::engine_tweaks::Slider(&unit_detail[0], "GraphicsUnitDetailVerySmall", "Unit Detail (Very Small Units)",
                                "How far very small units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[1], "GraphicsUnitDetailSmall", "Unit Detail (Small Units)",
                                "How far small units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[2], "GraphicsUnitDetailMedium", "Unit Detail (Medium Units)",
                                "How far medium units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[3], "GraphicsUnitDetailLarge", "Unit Detail (Large Units)",
                                "How far large units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[4], "GraphicsUnitDetailVeryLarge", "Unit Detail (Very Large Units)",
                                "How far very large units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
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
      tw::engine_tweaks::Slider(&tree_detail, "GraphicsTreeDetail", "Tree Detail Distance",
                                "Trees keep their detailed versions farther away. Heavier.", 4.f),
      tw::engine_tweaks::Slider(&bush_detail, "GraphicsBushDetail", "Bush Detail Distance",
                                "Bushes and scrub keep their detail farther away. From the next load.", 4.f),
      tw::engine_tweaks::Slider(&terrain_detail, "GraphicsTerrainDetail", "Terrain Detail Distance",
                                "Detailed terrain reaches farther.", 4.f),
      tw::engine_tweaks::Slider(&terrain_draw, "GraphicsTerrainDraw", "Terrain Draw Distance",
                                "More terrain is drawn in the distance. Heavier.", 4.f),
      tw::engine_tweaks::Slider(&campaign_detail, "GraphicsCampaignDetail", "Campaign Tree Detail Distance",
                                "Campaign map: trees keep their detail farther away.", 4.f),
      tw::engine_tweaks::Slider(&campaign_draw, "GraphicsCampaignDraw", "Campaign Draw Distance",
                                "Campaign map: small objects stay visible farther away. Heavier.", 8.f),
      tw::engine_tweaks::Slider(&shadow_distance, "GraphicsShadowDistance", "Shadow Distance",
                                "Sun shadows reach farther, but are less sharp up close.", 4.f, 0.5f),
      tw::engine_tweaks::Slider(&small_shadows, "GraphicsSmallShadows", "Small Object Shadows",
                                "Small objects keep their shadows farther away. Heavier.", 8.f),
      tw::engine_tweaks::Slider(&unit_shadows, "GraphicsUnitShadows", "Unit Shadow Distance",
                                "Soldiers keep their shadows farther away. Heavier.", 4.f),
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
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, false, true);
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterClarity",
      .binding = &twattila::water_effects::water_clarity,
      .default_value = 1.f,
      .label = "Water Clarity (Experimental)",
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
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedAOAlone",
      .binding = &rays_ao_alone_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Rays' AO Alone (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "The rays' ambient occlusion in place of the game's SSAO, not on top of it: lighter shade in corners.\n"
                 "Leaves out the game's SSAO (a little faster).",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedSkipGameReflections",
      .binding = &rays_skip_reflection_passes_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Skip the Game's Reflection Passes (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Leaves out the game's screen-space reflection passes the rays' reflections replace: the same picture,\n"
                 "a little faster.",
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
                   "(Apache 2.0, Intel).",
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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarwarhammer/mods/1"); },
      },
  });

  // Section titles and Settings Mode's pages as Attila's (twattila/feature_settings.hpp).
  renodx::utils::settings::default_section_tint = twattila::feature_settings::BASE_TINT;
  std::vector<std::string> engine_sections = {twattila::feature_settings::GRAPHICS_SECTION};
  for (const auto& test : engine_tests) {
    if (std::ranges::find(engine_sections, test.section) == engine_sections.end()) engine_sections.push_back(test.section);
  }
  twattila::feature_settings::SetTints({}, engine_sections);

  // Graphics 1 of 2's layout (as Warhammer II's and Pharaoh's), ATN's RT/PT preset as the defaults of what the methods leave,
  // then Settings Mode's pages. Only what this game hooks: no fog march, water or smoke (their rows stay hidden). Rays
  // Replace and Skip the Shadow Maps, Far Tree Shadows, Shadow Casters Out of View and Light Through Foliage Experimental,
  // off (the *_tested flags).
  // The paths' sky from the game's ambient (the spherical harmonics or the cube, rt_sun) and Path-Traced AO in the AO methods
  // (Experimental), as Warhammer II's: ATN's 2026-11-14 traces show g_hdr_on and g_use_spherical_harmonics on.
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  // Fog Shadows, the torches' light in it and Smoke Shadows start off (hidden: not hooked here yet), Specular Occlusion, Far
  // Tree Shadows and Water Caustics too (Experimental); RT/PT turns them on, as Attila's (rays_preset_as_attila). Before Apply:
  // No RT/PT and Reset read these defaults as the page is built.
  for (auto* setting : settings) {
    if (setting->key == "RayTracedFogShadows" || setting->key == "RayTracedLightGlow" || setting->key == "RayTracedSmokeShadows"
        || setting->key == "RayTracedSpecularOcclusion" || setting->key == "RayTracedFarTreeShadows" || setting->key == "WaterCaustics") {
      setting->default_value = 0.f;
    }
  }
  // Torch and Fire Shadows from rays (Warhammer III's row): after the preset's defaults, so it starts off (Experimental: no game
  // run has shown them here yet); RT/PT turns it on, as ATN's list does.
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
                                             .lod_bias_trilinear = &lod_bias_trilinear_row,
                                             .rays_replace_maps = &rays_replace_maps_row,
                                             .rays_replace_maps_tested = false,
                                             .rays_units_out_of_view = &rays_units_out_of_view_row,
                                             .units_out_of_view_tested = false,
                                             .rays_skip_cascades = &rays_skip_cascades_row,
                                             .rays_banner_light = &rays_banner_light_row,
                                             .rays_unseen_cut_outs = &rays_unit_cut_outs_row,
                                             .texts = &PAGE_TEXTS,
                                             .foliage_light_tested = false,
                                             .smoke_shadows_tested = false,
                                             .far_cards_tested = false,
                                             .water_caustics_tested = false,
                                             .rays_preset_as_attila = true,
                                             .lights_own_row = true,
                                             .rays_replace_ao = false});
  // Skip the Shadow Maps here: the water and the forward-lit units take the rays' shadow (SHADOW_READER_SHADERS). Cut-outs on
  // More Models: the units' alone here (CUT_OUT_MASKS).
  for (auto* setting : settings) {
    if (setting->key == "RayTracedSkipCascades") {
      setting->tooltip = "With Rays Replace Shadow Maps: nothing is drawn into the game's shadow maps. Faster with many units and\n"
                         "buildings. Water and the other forward-lit surfaces take the rays' shadow too.";
    }
    if (setting->key == "RayTracedUnseenCutOuts") {
      setting->tooltip = "Rays see the soldiers' fur, feathers and frayed cloth with the holes the game cuts in them.";
    }
  }
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();
  tw::engine_tweaks::watch::StartIfRequested(graphics_options, WATCHED_VALUES, 300);

  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  // The data run's RENODX_TW_SET / SHOT / PROFILE (nothing without them), timed from the first present.
  twattila::test_hooks::Install(&settings, true);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests: written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(nullptr, &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // Before the Light Bounce's (its callback draws the sun draw again).
  // Ray tracing's at the sun draws first: its trace for the screen-space shadows and the AO filter after it.
  // The shadow pass's casters (cascade_keep) as Warhammer II's: their constants kept, world positions; the atlas the sun's
  // one 8192 R32 map (the sun draws' t1 in ATN's traces).
  twattila::rt_scene::Host rt_host = twattila::rt_scene::TW_HOST;
  rt_host.cascade_world = true;
  rt_host.cascade_target = [](const D3D11_TEXTURE2D_DESC& desc) {
    return desc.ArraySize == 1 && desc.Width == desc.Height && desc.Format == DXGI_FORMAT_R32_TYPELESS;
  };
  // The point lights' diffuse scale: their shaders' 0.318310 x 0.01 (g_hdr_on 1; the sun's too).
  rt_host.local_light_scale = 0.0031831f;
  twattila::rt_scene::Install(SCENE_SHADERS, {}, rt_host);
  // Torch and Fire Shadows: the point lights (one instanced draw each, VS B5B5BFA4 with PS D0C67938 or 5D1695BC; its
  // instances POSITION1 the colour, POSITION2 the place and reach, as Warhammer II's), then ours over the game's shader
  // while their rays are bound (PointLightRays).
  twattila::rt_scene::InstallPointLights(std::array<uint32_t, 2>{PointLightRays::SHADER, LinearPointLightRays::SHADER});
  renodx::utils::command_action::Register(
      PointLightRays::OnDraw, {.shader_hash = PointLightRays::SHADER, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  renodx::utils::command_action::Register(
      LinearPointLightRays::OnDraw, {.shader_hash = LinearPointLightRays::SHADER, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  reshade::register_event<reshade::addon_event::destroy_device>(PointLightRays::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(LinearPointLightRays::OnDestroyDevice);
  // The Debug View's Screen-Space AO: before every other sun draw hook (they replace the AO the draw reads). No AO pass hook
  // keeps it here (the game's AO is compute line sweeps).
  twattila::rt_scene::InstallSunDebugAmbient(LIGHT_BOUNCE_DRAWS, &rt_sliders);
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace. The ambient cube at c2 and the sun colour at c1
  // as Attila's (the sun PS's lighting_VS_PS RDEF); the paths' sky and sun in rt_sun.cs_5_0.hlsl. Not hooked: the unlit (ulm) and
  // default reflection sun PS (ED3BDD4C drawn in the 10-27 battle trace): they write the G-buffer's colour or black, no light.
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4);
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  // Skip the Shadow Maps: the atlas's casters skipped (Ray-Traced Shadows Only); its forward readers patched as they are
  // created to take the rays' occlusion at t49, bound at their draws only then (test/tw_warhammer_shaders part 8).
  twattila::screen_space_shadows::PatchForwardShadows(SHADOW_READER_SHADERS, true);
  twattila::rt_scene::InstallShadowReaders(SHADOW_READER_SHADERS);
  twattila::rt_scene::InstallShadowsOnly();
  twattila::rt_scene::InstallCutOutMasks(CUT_OUT_MASKS);
  // Light Through Foliage: the G-buffer's channel 3 alpha byte, bit 128 (Warhammer III's: the leaves' 744FDFF0 writes 128,
  // the sun shaders let the sun through from behind only there; test/tw_warhammer_shaders part 11).
  twattila::screen_space_shadows::foliage_material_bit = 128;
  // Light Through Banners' Coloured Shadows: the G-buffer's colour is linear (the sun shaders' diffuse, part 10).
  twattila::screen_space_shadows::InstallThin(THIN_SHADERS, 1.f);
  // Rays Replace Shadow Maps: after the screen-space shadows' and before the Light Bounce's.
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyAtlasLitDevice);
  for (const uint32_t shader_hash : ATLAS_SUN_SHADERS) {
    renodx::utils::command_action::Register(
        OnSunReplaceMaps, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  // The data run's frame traces (tools\tw\wh1\plan_1114.md; twattila/frame_trace.hpp): on demand (renodx_trace_now next to the
  // exe) and at 100% too. The camera's g_screen_size / g_viewport_dimensions at c27 / c29 as Attila's (the sun PS's camera RDEF,
  // 230F2241: offsets 432 / 464). Constants once each (the sun PS bind cb0-cb6): the 11 single-sampled sun PS (directionallight
  // slm / skinlm), the tone mappers (basic D275C403, constant 9C8E40E6, with bloom 7759B956, with bloom and gamma 50DEBCD2), the
  // point and spot lights' VS (B5B5BFA4 / 7053708A, F0C3E3A1 / 90EA5679), the battle fog (E684D54E, its VS E768F804), the battle
  // water (waterplane gather 4.1 / 5.0: 5C9DF968, 44B4A2F0) and river (B5C25930), the lit particles (63D80213, 122F218C; their
  // blend and the dynamic-light one's, C4D57D81) and their VS's buffers (3883A15B, 9FD3EC63); the lights' instances with
  // FrameTraceReads (trace_reads.hpp).
  // The game maps some uvs to its viewport itself (uv x (c29.zw x c31.xy): the weather effects 3619B413, 4BEEAAE2, 5B006315,
  // 9A369793; tools\tw\wh1\scale_census_wh1_base.txt): view_share leaves them.
  twattila::view_share::SetViewportChains(true);
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, {}, {}, {},
                                   {
                                       .value_hashes = {0x230F2241, 0x27AF3D62, 0x2B3F8DC8, 0xC601C862, 0xCF0C5118, 0x9547FB5E, 0x1CE77E85,
                                                        0x4324BBE8, 0xA14D4C74, 0xA359A234, 0xB8F4B964, 0xD275C403, 0x9C8E40E6, 0x7759B956,
                                                        0x50DEBCD2, 0xB5B5BFA4, 0x7053708A, 0xF0C3E3A1, 0x90EA5679, 0xE684D54E, 0xE768F804,
                                                        0x5C9DF968, 0x44B4A2F0, 0xB5C25930, 0x63D80213, 0x122F218C},
                                       .value_slots = 7,
                                       .value_floats = 256,
                                       .on_demand = true,
                                       .at_100 = true,
                                       .blend_hashes = {0x63D80213, 0x122F218C, 0xC4D57D81},
                                       .vertex_input_hashes = {0x3883A15B, 0x9FD3EC63},
                                       .instances_apart = true,
                                       .draw_reads = twwarhammer::trace_reads::Read,
                                   });
  // Water Clarity and Caustics at the battle water's draws; Caustics' patch on top of Skip the Shadow Maps' forward patch
  // (registered above), as part 12 chains them.
  twattila::water_effects::water_shaders = WATER_SHADERS;
  renodx::utils::pipeline::RegisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  // Before the Light Bounce: it draws the sun draws itself (no callback after it runs).
  twattila::ao_filter::Install(LIGHT_BOUNCE_DRAWS);
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);

  reshade::register_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  twattila::test_hooks::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  renodx::utils::command_action::Unregister(twattila::water_effects::OnWaterDraw);
  renodx::utils::pipeline::UnregisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  renodx::utils::command_action::Unregister(PointLightRays::OnDraw);
  renodx::utils::command_action::Unregister(LinearPointLightRays::OnDraw);
  renodx::utils::command_action::Unregister(OnSunReplaceMaps);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyAtlasLitDevice);
  OnDestroyAtlasLitDevice(nullptr);
  reshade::unregister_event<reshade::addon_event::destroy_device>(PointLightRays::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(LinearPointLightRays::OnDestroyDevice);
  twattila::engine_tests::StopAtStart(&at_start_stop);
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::rt_scene::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
  twattila::ao_filter::Uninstall();
  tw::engine_tweaks::Uninstall(graphics_options);

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
      // Not FALSE when it fails: ReShade next to renodx-tw-dlaa64.exe loads this addon there too, and after a FALSE the CRT's
      // thread_local cleanup faults in the loader (ntdll c0000005, a Windows crash report each start). AddonInit fails instead.
      addon_registered = reshade::register_addon(h_module);
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
