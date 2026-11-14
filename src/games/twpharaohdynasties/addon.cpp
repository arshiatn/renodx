/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <cmath>
#include <string>
#include <utility>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/ao_pass.hpp"
#include "../twattila/contact_shadows_off.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/light_count.hpp"
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
#include "../twattila/sun_replace_maps.hpp"
#include "../twattila/test_hooks.hpp"
#include "../twattila/view_share.hpp"
#include "../twattila/water_effects.hpp"
#include "../twattila/water_reflections.hpp"
#include "./more_tests.h"
#include "../twrome2/engine_tweaks.hpp"
#include "./dead_units.hpp"
#include "./trace_reads.hpp"
#include "./shadow_map.hpp"
#include "../twwarhammer2/line_sweep_ao.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "../twtroy/rt_particle_lights_gs.h"
#include "./rt_outside_view_branch_gs.h"
#include "./rt_outside_view_card_gs.h"
#include "./rt_outside_view_leaf_gs.h"
#include "./rt_outside_view_unit_gs.h"
#include "./rt_outside_view_world_gs.h"
#include "./rt_particle_quads_gs.h"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;


bool IsExtendedMode() {
  return shader_injection.hdr == 1.f;
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f;
}

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsPsychoV31Mode() {
  return shader_injection.hdr == 3.f;
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 4.f;
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, the ACES fit).
constexpr float HUE_CLIP_DEFAULT = 80.f;

bool IsModdedAutoExposureEnabled() {
  return shader_injection.auto_exposure_highlight_protection == 1.f && IsHDRMode();
}

void ApplyResetPreset() {
  // Reset only controls that are currently active in this addon.
  // Peak / Paper White / UI brightness, Settings Mode, and output settings
  // are intentionally preserved.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 1.f},
      {"gamma_correction", 1.f},
      {"video_autohdr", 0.f},

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 1.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      // HDR Extended / user grading
      {"exposure_tpm", 1.f},
      {"highlights_tpm", 1.f},
      {"shadows_tpm", 1.f},
      {"contrast_tpm", 1.f},
      {"saturation_tpm", 1.f},
      {"highlight_expansion", 1.f},

      // Active extra controls
      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"dof", 1.f},
      {"distortion", 1.f},
      {"chromaticaberration", 1.f},
      {"sharpening", 1.f},
      {"some_indicators", 1.f},
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
      {"v31_highlight_hue_shift", 0.06f},
      {"v31_highlight_saturation_shift", 0.f},

      // Active experimental controls
      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 2.f},
      {"vfx_fire_brightness", 1.f},
      {"vfx_base_brightness", 1.f},
      {"sky_deband", 2.f},
      {"filmgrain", 0.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

// SDR: the game's own tonemapper and look, every other control at its default; video AutoHDR is already off in Reset.
void ApplySdrPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemapper", 0.f}});
}

void ApplyPsychoRecommendedNoVFXBoost() {
  // ATN 2026-11-12: every recommended preset in PsychoV35 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"chromaticaberration", 1.f},

      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 2.f},
      {"vfx_fire_brightness", 1.f},
      {"vfx_base_brightness", 1.f},
      {"sky_deband", 2.f},
      {"filmgrain", 0.f},
  });
}

void ApplyPsychoRecommendedVFXBoost2500() {
  // ATN 2026-11-12: every recommended preset in PsychoV35 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"chromaticaberration", 1.f},

      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 2.0f},
      {"vfx_fire_brightness", 6.f},
      {"vfx_base_brightness", 4.5f},
      {"sky_deband", 2.f},
      {"filmgrain", 0.f},
  });
}

void ApplyPsychoRecommendedVFXBoost1000() {
  // ATN 2026-11-12: every recommended preset in PsychoV35 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"chromaticaberration", 1.f},

      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 2.0f},
      {"vfx_fire_brightness", 4.f},
      {"vfx_base_brightness", 2.4f},
      {"sky_deband", 2.f},
      {"filmgrain", 0.f},
  });
}


// Frame copy for sharpening / CA ///////////////////////////////////////////////////////
// The game copies the frame into an 8-bit texture (dropped in HDR -> black scene).
// Instead: copy the bound RT into our own texture, bind it at t0 for that draw only.
namespace frame_copy {

struct Data {
  reshade::api::device* device = nullptr;
  reshade::api::resource texture = {0u};
  reshade::api::resource_view srv = {0u};
  reshade::api::resource_desc desc = {};
};

Data data;
std::mutex mutex;
thread_local ID3D11ShaderResourceView* game_srv = nullptr;
thread_local bool swapped = false;

// Caller holds the mutex.
void Destroy() {
  if (data.device != nullptr) {
    if (data.srv.handle != 0u) data.device->destroy_resource_view(data.srv);
    if (data.texture.handle != 0u) data.device->destroy_resource(data.texture);
  }
  data = {};
}

// Copies the frame into our texture ((re)created to match it). Returns its SRV.
reshade::api::resource_view CopyFrame(reshade::api::command_list* cmd_list, reshade::api::resource frame) {
  auto* device = cmd_list->get_device();
  const auto frame_desc = device->get_resource_desc(frame);
  if (frame_desc.type != reshade::api::resource_type::texture_2d || frame_desc.texture.samples != 1) return {0u};

  const std::scoped_lock lock(mutex);
  if (data.device != device
      || data.desc.texture.width != frame_desc.texture.width
      || data.desc.texture.height != frame_desc.texture.height
      || data.desc.texture.depth_or_layers != frame_desc.texture.depth_or_layers
      || data.desc.texture.levels != frame_desc.texture.levels
      || data.desc.texture.format != frame_desc.texture.format) {
    Destroy();
    data.device = device;
    data.desc = reshade::api::resource_desc(
        frame_desc.texture.width,
        frame_desc.texture.height,
        frame_desc.texture.depth_or_layers,
        frame_desc.texture.levels,
        frame_desc.texture.format,
        1,
        reshade::api::memory_heap::gpu_only,
        reshade::api::resource_usage::shader_resource | reshade::api::resource_usage::copy_dest);
    if (!device->create_resource(data.desc, nullptr, reshade::api::resource_usage::shader_resource, &data.texture)) {
      data.texture = {0u};
      return {0u};
    }
    const reshade::api::resource_view_desc view_desc(reshade::api::format_to_default_typed(frame_desc.texture.format));
    if (!device->create_resource_view(data.texture, reshade::api::resource_usage::shader_resource, view_desc, &data.srv)) {
      data.srv = {0u};
      return {0u};
    }
  }
  if (data.srv.handle == 0u) return {0u};

  cmd_list->copy_resource(frame, data.texture);
  return data.srv;
}

bool OnDraw(reshade::api::command_list* cmd_list) {
  swapped = false;
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());

  // The frame as bound right now (back buffer, or RenoDX's 16-bit clone of it).
  ID3D11RenderTargetView* rtv = nullptr;
  context->OMGetRenderTargets(1, &rtv, nullptr);
  if (rtv == nullptr) return true;
  ID3D11Resource* frame = nullptr;
  rtv->GetResource(&frame);
  rtv->Release();
  if (frame == nullptr) return true;

  const auto srv = CopyFrame(cmd_list, {reinterpret_cast<uintptr_t>(frame)});
  frame->Release();
  if (srv.handle == 0u) return true;

  // Swap t0 to our copy for this draw.
  context->PSGetShaderResources(0, 1, &game_srv);
  auto* copy_srv = reinterpret_cast<ID3D11ShaderResourceView*>(srv.handle);
  context->PSSetShaderResources(0, 1, &copy_srv);
  swapped = true;
  return true;
}

// Puts the game's t0 back.
void OnDrawn(reshade::api::command_list* cmd_list) {
  if (!swapped) return;
  swapped = false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  context->PSSetShaderResources(0, 1, &game_srv);
  if (game_srv != nullptr) {
    game_srv->Release();
    game_srv = nullptr;
  }
}

void OnDestroyDevice(reshade::api::device* device) {
  const std::scoped_lock lock(mutex);
  if (data.device == device) Destroy();
}

}  // namespace frame_copy

#define FrameCopyShader(value)              \
  {                                         \
      value,                                \
      {                                     \
          .crc32 = value,                   \
          .code = __##value,                \
          .on_draw = &frame_copy::OnDraw,   \
          .on_drawn = &frame_copy::OnDrawn, \
      },                                    \
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

// The fog's in-scattering (battlefog inscattering_transmittance, shadows_hardware_gather_5_0; the only fog light shader in the
// data run's battle, siege and SSAO-off traces, a quarter of the render) marches the sun's shadow maps (the atlas t2, the kept
// baked map t0): Fog Shadows' (OnFogShadowsDraw). The other permutations (fetch4, gather 4.1, pcf, baked shadows only) were
// drawn in no trace: left as the game's. WARP on the game's bytes: tools\tw\warp_tests\pharaoh\fog_v54.
constexpr std::array<uint32_t, 1> FOG_SHADERS = {0x598C59F3};

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      FrameCopyShader(0x6F09FC39), //chromatic aberration (reads our 16-bit frame copy)
      FrameCopyShader(0x19045C91), //sharpening (reads our 16-bit frame copy)
      UpgradeRTVReplaceShader(0x976C1C74), //t00
      UpgradeRTVReplaceShader(0x769869A4), //t01
      UpgradeRTVReplaceShader(0xDC7D89CD), //t02
      UpgradeRTVReplaceShader(0x60683951), //t03
      UpgradeRTVReplaceShader(0x504E6093), //t04
      UpgradeRTVReplaceShader(0xBB00FA18), //t05-t08: depth of field on
      UpgradeRTVReplaceShader(0x436A0FBA),
      UpgradeRTVReplaceShader(0xDBB55D7B),
      UpgradeRTVReplaceShader(0xAD62FB1E),
      UpgradeRTVReplaceShader(0x7137A4E2), //photo01-04: photo mode
      UpgradeRTVReplaceShader(0x63E7B6D1),
      UpgradeRTVReplaceShader(0x9F49A50F),
      UpgradeRTVReplaceShader(0xBD868B0A),
      // UpgradeRTVReplaceShader(EMPTY),
      __ALL_CUSTOM_SHADERS,
  };
  // The fog's in-scattering stays the game's: our copy is bound only for Fog Shadows' draws (OnFogShadowsDraw).
  custom_shaders.erase(FOG_SHADERS[0]);
  // So does the water's reflection apply: ours only where the rays left from the water or below 100%
  // (twattila/water_reflections.hpp OnWaterApplyDraw).
  custom_shaders.erase(0xDE4F92CD);
  // Ambient Occlusion: Visibility Bitmask in place of the AO's horizontal blur: the addon's own shader
  // (ao_bitmask_blur, twwarhammer2/line_sweep_ao.hpp) with the blur's bindings; the blur's replacement stays the game's
  // code (Game's costs what it did). The collect always runs: where no blur runs (Warhammer II at some settings), the
  // game's AO stays, never an unwritten one.
  custom_shaders[0x6CF96BAF].on_draw = [](auto* cmd_list) {
    if (shader_injection.ssao_mode < 1.f) return true;
    return !tw::line_sweep_ao::DispatchInstead(cmd_list, __ao_bitmask_blur, &shader_injection, sizeof(shader_injection),
                                               false, __ao_upsample_blur, shader_injection.ssao_resolution != 0.f);
  };

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
        .tooltip = "SDR uses original SDR tonemapper.\nExtended unclips and extends the native ACES tonemapper.\n"
                   "HDR (PsychoV30) enables the HDR pipeline and uses PsychoV30 as tonemapper.\n"
                   "HDR (PsychoV31): PsychoV31 as tonemapper, more grading sliders.\n"
                   "HDR (PsychoV35): Musa's PsychoV35 as tonemapper, with Hue Clip Emulation (the Recommended presets).",
        .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV30)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
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

    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::TEXT,
    //     .label =
    //         "IMPORTANT: Disable Chromatic Aberration in the game's graphics settings.\n"
    //         "Both effects can make the game scene render black.",
    //     .section = "Important",
    // },
    
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Resets active image controls to their defaults.\n"
                   "Peak, Paper White, UI brightness, Settings Mode, and output settings are preserved.",
        .on_change = []() {
          ApplyResetPreset();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (no VFX boost)",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "HDR (PsychoV35) and Video AutoHDR 0.20, the rest at its default.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
          ApplyPsychoRecommendedNoVFXBoost();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (VFX boost for 1000nits)",
        .section = "Presets",
        .group = "preset-line-2",
        .tooltip = "As Recommended, with brighter fire and effects for ~1000 nit displays.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
          ApplyPsychoRecommendedVFXBoost1000();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (VFX boost for 2500nits)",
        .section = "Presets",
        .group = "preset-line-2",
        .tooltip = "As Recommended, with brighter fire and effects for ~2500 nit displays.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
          ApplyPsychoRecommendedVFXBoost2500();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "SDR (monitors without HDR)",
        .section = "Presets",
        .group = "preset-line-3",
        .tooltip = "For SDR monitors: Rendering Mode SDR (the game's own picture) and Output SDR (restart the game if it\n"
                   "was HDR), no video AutoHDR. The game's and the UI's white are your monitor's own white.",
        .tint = 0xFFC107,
        .on_change = []() {
          ApplySdrPreset();
          renodx::utils::settings::UpdateSettings({{"SwapChainEncoding", 2.f}});
        },
    },
    // Extra //////////////////////////////////////////////////////////////////////////////////////
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
        .default_value = 1.00f,
        .label = "Exposure",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "highlights",
        .binding = &shader_injection.highlights,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Highlights",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "shadows",
        .binding = &shader_injection.shadows,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Shadows",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "contrast",
        .binding = &shader_injection.contrast,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Contrast",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    // new renodx::utils::settings::Setting{
    //     .key = "flare",
    //     .binding = &shader_injection.flare,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.0f,
    //     .label = "Flare",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 2.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },
    new renodx::utils::settings::Setting{
        .key = "purity_scale",
        .binding = &shader_injection.saturation,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Saturation / Purity",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "cone_response_exponent",
        .binding = &shader_injection.cone_response_exponent,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Cone Response Exponent",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "hue_shift",
        .binding = &shader_injection.hue_shift,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Highlight Hue Shift",
        .section = "Psycho V30",
        .tooltip = "Hue shift of bright highlights, like SDR: fire and explosions turn orange/yellow instead of pink/red.\n0 = none, 1 = PsychoV30 default (like my old addons), 2 = full.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    // ONLY FOR DEBUG. ADD AGAIN IF MID GREY IS WRONG
    // new renodx::utils::settings::Setting{
    //     .key = "current_adaptive_state_bt709",
    //     .binding = &shader_injection.current_adaptive_state_bt709,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.18f,
    //     .label = "current_adaptive_state_bt709",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 1.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },

    // new renodx::utils::settings::Setting{
    //     .key = "current_background_state_bt709",
    //     .binding = &shader_injection.current_background_state_bt709,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.18f,
    //     .label = "current_background_state_bt709",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 1.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },


    //TPM:
    new renodx::utils::settings::Setting{
        .key = "highlight_expansion",
        .binding = &shader_injection.highlight_expansion,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Highlight Expansion",
        .section = "User Grading",
        .tooltip = "On: the highlights ACES squeezed into the top of its curve are\n"
           "stretched back out into HDR headroom - the bright end Troy\n"
           "compressed away comes back.\n\n"
           "Off: unclipped SDR. Only what the game actually clipped expands,\n"
           "which is very little.\n\n"
           "Below that point it is the SDR image either way.",
        .labels = {"Off (Unclipped SDR)", "On"},
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },

    new renodx::utils::settings::Setting{
        .key = "exposure_tpm",
        .binding = &shader_injection.exposure_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Exposure",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "highlights_tpm",
        .binding = &shader_injection.highlights_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Highlights",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "shadows_tpm",
        .binding = &shader_injection.shadows_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Shadows",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "contrast_tpm",
        .binding = &shader_injection.contrast_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Contrast",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "saturation_tpm",
        .binding = &shader_injection.saturation_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Saturation / Purity",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },


    ///EXTRA-----////

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
               "0 = off, 1 = SDR hues. Default 0.06.",
               1.f, 0.06f),
    v31_slider("v31_highlight_saturation_shift", &shader_injection.v31_highlight_saturation_shift,
               "Highlight Saturation Shift",
               "Gives bright colors the saturation of the SDR image (fire cores less pale and pink).\n"
               "0 = off (default), 1 = SDR's.",
               1.f, 0.f),

    new renodx::utils::settings::Setting{
        .key = "bloom",
        .binding = &shader_injection.bloom,
        .default_value = 1.0f,
        .label = "Bloom",
        .section = "Extra",
        .tooltip = "Bloom multiplier.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
    },
      new renodx::utils::settings::Setting{
        .key = "lensflare",
        .binding = &shader_injection.lensflare,
        .default_value = 1.00f,
        .label = "Lensflare",
        .section = "Extra",
        .tooltip = "Lensflare multiplier.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "godrays",
        .binding = &shader_injection.godrays,
        .default_value = 1.f,
        .label = "Godrays",
        .section = "Extra",
        .tooltip = "Multiplier on godrays.  Needs in-game Godrays setting on.",
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
        .tooltip = "Multiplier on distortion. Needs haze to be on.",
        // .tooltip = "Multiplier on distortion. YOU NEED TO CHOOSE PRESET ULTRA\n"
        //            "THEN YOUR OWN CUSTOME SETTINGS for the hidden distortion\n"
        //            "to get turned on. \n"
        //            "It was probably hidden because it is not getting used anymore!",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "chromaticaberration",
        .binding = &shader_injection.chromaticaberration,
        .default_value = 1.f,
        .label = "Chromatic aberration",
        .section = "Extra",
        .tooltip = "Multiplier on Chromatic aberration. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "sharpening",
        .binding = &shader_injection.sharpening,
        .default_value = 1.f,
        .label = "Sharpening",
        .section = "Extra",
        .tooltip = "RenoDX's own sharpening (the game has none). 0 = off, 1 = default.",
        .min = 0.f,
        .max = 4.f,
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
        .tooltip = "Brightness of selected highlight indicators.",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
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


    // EXTRA EXPERIMENTAL//////////
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
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "auto_exposure_highlight_headroom_stops",
        .binding = &shader_injection.auto_exposure_highlight_headroom_stops,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 2.0f,
        .label = "Modded Auto Exposure: Sensitivity",
        .section = "Extra (Experimental)",
        .tooltip = "Maximum metered highlight above the frame average, in stops. Lower values protect exposure more strongly. Higher values are closer to stock.",
        .min = 0.6f,
        .max = 4.0f,
        .format = "%.1f",
        .is_enabled = IsModdedAutoExposureEnabled,
    },
    new renodx::utils::settings::Setting{
        .key = "vfx_fire_brightness",
        .binding = &shader_injection.vfxfirebrightness,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Brightness boost: torches/burning houses/etc.",
        .section = "Extra (Experimental)",
        .tooltip = "torches/burning houses/etc. Recommmendation is max 10\n"
                   "Experimental! Find your sweets spot! ",
        .min = 1.0f,
        .max = 30.0f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "vfx_base_brightness",
        .binding = &shader_injection.vfxbasebrightness,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness boost: All of them!",
        .section = "Extra (Experimental)",
        .tooltip = "Brightness of every single VFX effect. Recommendation: 2.5 for 1000nits, 6 for 2500nits.\n"
                   "Probably not usable in desert storms. Experimental! Find your sweets spot! ",
        .min = 1.0f,
        .max = 20.0f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "sky_deband",
        .binding = &shader_injection.sky_deband,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 2.f,
        .label = "Dithering for sky's 7-bit texture",
        .section = "Extra (Experimental)",
        .tooltip = "Dithering to reduce sky's bandings.\n"
                   "Higher levels destroy more sky details while fixing banding.",
        .labels = {"Off", "Low", "Medium", "High"},
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "filmgrain",
        .binding = &shader_injection.filmgrain,
        .default_value = 0.f,
        .label = "Filmgrain (probably isn't used)",
        .section = "Extra (Experimental)",
        .tooltip = "Multiplier on filmgrain. YOU NEED TO CHOOSE PRESET HIGH/ULTRA\n"
                   "THEN YOUR OWN CUSTOME SETTINGS for the hidden filmgrain\n"
                   "to get turned on. \n"
                   "It was probably hidden because it is not getting used anymore!",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
    },
    // VFX //////////////////////////////////////////////////////////////////////////////////////
   
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
tw::engine_tweaks::Option dead_units;  // dead_units.hpp
tw::engine_tweaks::Option unit_animation;
tw::engine_tweaks::Option unit_variety;
tw::engine_tweaks::Option footstep_effects;
tw::engine_tweaks::Option model_detail;
tw::engine_tweaks::Option grass_distance;
tw::engine_tweaks::Option small_objects;
tw::engine_tweaks::Option terrain_detail;
tw::engine_tweaks::Option building_detail;
tw::engine_tweaks::Option campaign_detail;
tw::engine_tweaks::Option reflection_quality;
tw::engine_tweaks::Option contact_shadow_quality;
// Shadow Map Size (shadow_map.hpp): the highest Shadows settings' shadow map, an index into SHADOW_MAP_SIZES.
float shadow_map_size = 0.f;
constexpr std::array<uint32_t, 4> SHADOW_MAP_SIZES = {0, 8192, 12288, 16384};  // 0: the game's (6144)
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
// The shadow pass's casters in the rays' scene (SCENE_SHADERS' cascade_keep entries): Shadow Casters Out of View, and Hills Out of
// View for its terrain (Experimental, off: no game run has shown them yet).
float rays_units_out_of_view_row = 0.f;
float rays_hills_out_of_view_row = 0.f;
// Fire Shadows from Particle Lights (Experimental, off, not in the RT/PT preset): PARTICLE_LIGHTS' draws left alone while 0.
float particle_light_shadows_row = 0.f;
// Texture LOD Bias on All Textures (Experimental, off, as Warhammer III's): the game's wrapping trilinear samplers biased too
// (texture_lod_bias.hpp SetWrappingTrilinear). Which of Pharaoh's samplers filter without anisotropy is unread (their descs
// come at run time; Log Details' Texture LOD Bias line counts them).
float lod_bias_trilinear_row = 0.f;

// Smoke Shadows' particles (Experimental, off): vfx vs50_main_instancing 109B79CE draws each particle as an instance of a
// quad (6 indices), and of the pixel shaders it draws with only vfxextremeultra 3F1B86E1 (ps_main_high_quality) blends quads
// over the light buffer (ONE / INV_SRC_ALPHA); the others are distortion (2A315C27), meshes (ACE25F9D, CFB48C60) and the
// particles' lights (5AD46115). Every draw reads the same frame-wide sorted list; the vertex shader keeps only emitters whose
// m_draw_flags meet the draw's g_draw_flags (cb_vfx). rt_particle_quads makes a record of each instance's first triangle.
// Effects quality 0-2 draw vfxlowmedhigh00 E8524BB1 (ps_main, no shadow maps) in its place: the same input signature,
// EMITTER_CONSTANTS_PS (stride 80) and alpha (texture alpha at both cells, the sRGB decode with 0x3F72A76F, x COLOR.w,
// the alpha crush), its diffuse array t3 / s1 and constants t5 three view and two sampler slots below 3F1B86E1's t6 / s3 / t8
// (fxc -dumpbin of both; in no trace: ATN plays at 4). 3F1B86E1's MSAA twin ps_main_high_quality_ms 740B2536 stays out:
// no rays under the game's MSAA.
constexpr std::array<twattila::rt_scene::SmokeVertexShader, 1> SMOKE_VERTEX_SHADERS = {{{.hash = 0x109B79CE, .quad_shader = __rt_particle_quads}}};
constexpr std::array<twattila::rt_scene::SmokePixelShader, 2> SMOKE_PIXEL_SHADERS = {{
    {.hash = 0x3F1B86E1},
    {.hash = 0xE8524BB1, .view_shift = 3, .sampler_shift = 2},
}};
// The fires (burning buildings) in the rays, behind their own row: the particle lights' draw (ps_main_deferred_dynamic_light
// 5AD46115 with 109B79CE: 60 indices an instance, 13-85 instances in the phase 2 captures) holds its lights in the particle
// list, not in instances: streamed from the vertex shader's outputs (Troy's rt_particle_lights: 109B79CE's output signature
// is F5CE48CC's), read by shaders/particlelight00.
constexpr twattila::rt_scene::LightInstances PARTICLE_LIGHTS = {.stream_shader = __rt_particle_lights, .enabled = &particle_light_shadows_row};

// Ray Reconstruction's guides again after the water (water_troy 833E7963 writes the G-buffer after the sun draws, and the
// sky dome 9CF980D0 draws after it, once a frame in every data-run trace): from the first sun draw's channels.
constexpr auto OnSkyDomeGuides = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  twattila::anti_aliasing::RewriteSunGuides(native);
  return {};
};

// Rays Replace Shadow Maps (Experimental, off): the sun draws reading the per-frame atlas (g_tShadowBuffer_SM50 t3 through
// sShadowBuffer_SM50 s2, its only reads of s2; the other 13 sun shaders read the kept baked map alone or no map, and all
// of them sample another texture with s2) take every comparison there as lit (an ALWAYS sampler) where the rays' shadow is
// bound at t49, so their patched comparisons (screen_space_shadows.hpp Patch, each x the rays' visibility) give the rays'
// shadow alone. The kept baked map (t1 by s1: what the game baked for the map, from about 200 m) stays the game's, and so do
// the atlas's other readers (water, rivers, particles, the fog without Fog Shadows). The game's sampler is bound again after
// the draw (finalize: the Light Bounce draws the sun draw itself and bypasses). Skip the Shadow Maps (under it): rt_scene's
// Ray-Traced Shadows Only skips the atlas's draws (its learned map is Host::cascade_target's, so never the baked one). WARP on
// the game's bytes with the traces' constants: tools\tw\pharaoh\sun_test (P). The hook: twattila/sun_replace_maps.hpp.
constexpr std::array<uint32_t, 12> ATLAS_SUN_SHADERS = {0xF883F5B2, 0xFA856110, 0x1549C88A, 0xE238D4C7, 0x24FD1318, 0x6AFC2ACA,
                                                        0xC701CDD0, 0xE5A6A379, 0x3323BDCC, 0x47DDD6EE, 0x955658C3, 0xF4E19A48};
float rays_replace_maps_row = 0.f;
float rays_skip_cascades_row = 0.f;
// Fog Shadows (FOG_SHADERS): in a draw where rt_scene's fog hook (InstallFogShadows, registered before this) bound this frame's
// rays at t43, our copy (shaders/battlefog00: the march's gate false, the sun's light in the fog x the rays) draws in its place
// and the game's shader is bound again after it (finalize); otherwise the game's own bytes.
struct FogShadows {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // ours' (compared only)
  Microsoft::WRL::ComPtr<ID3D11PixelShader> ours;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> game_shader;  // bound again after the draw
  bool logged = false;
};
FogShadows fog_shadows;
constexpr auto OnFogShadowsDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rays;
  native->PSGetShaderResources(43, 1, rays.GetAddressOf());
  if (!rays) return {};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = fog_shadows;
  const std::unique_lock lock(state.mutex);
  if (state.device != device.Get()) {
    state.ours.Reset();
    state.device = nullptr;
    if (FAILED(device->CreatePixelShader(__0x598C59F3.data(), __0x598C59F3.size(), nullptr, state.ours.GetAddressOf()))) return {};
    state.device = device.Get();
  }
  native->PSGetShader(state.game_shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
  native->PSSetShader(state.ours.Get(), nullptr, 0);
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: Fog Shadows: the fog's in-scattering 598C59F3 drawn with the rays.");
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    auto& drawn_state = fog_shadows;
    const std::unique_lock drawn_lock(drawn_state.mutex);
    if (drawn_native != nullptr) {
      drawn_native->PSSetShader(drawn_state.game_shader.Get(), nullptr, 0);
    }
    drawn_state.game_shader.Reset();
  }};
};

// Only our shader's device (nullptr: the addon's end, DetachAddon).
void OnDestroyFogShadowsDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = fog_shadows;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.ours.Reset();
  state.device = nullptr;
}

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
bool graphics_found = false;
bool dead_units_installed = false;


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

// Ambient Occlusion Visibility Bitmask (twattila/ao_pass.hpp): the addon's own shader in place of the game's AO
// pass (whose replacement is the game's code only).
constexpr std::array<twattila::ao_pass::AmbientOcclusionDraw, 1> AO_DRAWS = {{
    {.shader_hash = 0xD42C3523, .bitmask = __ao_bitmask, .upsample = __ao_upsample},  // ssao01
}};

// DLAA / DLSS object motion (twattila/anti_aliasing.hpp, as Attila's): the game's shaders whose things move on
// their own and their motion versions (tools\tw\attila_motion\port_motion.py, GPU-checked by test_motion.py).
using twattila::anti_aliasing::MotionKind;
constexpr std::array<twattila::anti_aliasing::MotionShader, 26> MOTION_SHADERS = {{
    {.hash = 0x07936F02, .motion_shader = __motion_unit_weighted_0793, .kind = MotionKind::UNIT},
    {.hash = 0x15389925, .motion_shader = __motion_unit_weighted_standard_4_feat_1538, .kind = MotionKind::UNIT},
    {.hash = 0x2A6B49A6, .motion_shader = __motion_grass_vfx_phar_grass_flame_ti_2a6b},
    {.hash = 0x2C5A6427, .motion_shader = __motion_unit_weighted_standard_2_feat_2c5a, .kind = MotionKind::UNIT},
    // Cloth (phar_rigid_battle_cloth_wind) and flags (rigid_default_flag): their rows, time and wind last frame.
    {.hash = 0x2F2A0E7E, .motion_shader = __motion_rigid_cloth_wind_2f2a, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_cloth_wind_2f2a},
    {.hash = 0x3EF23291, .motion_shader = __motion_tree_troy_prefab_sky_cylinder_3ef2},
    {.hash = 0x475A8CDF, .motion_shader = __motion_unit_weighted_475a, .kind = MotionKind::UNIT},
    {.hash = 0x4A716936, .motion_shader = __motion_unit_weighted_4a71, .kind = MotionKind::UNIT},
    {.hash = 0x5CE17813, .motion_shader = __motion_unit_weighted_standard_2_feat_5ce1, .kind = MotionKind::UNIT},
    {.hash = 0x6061911B, .motion_shader = __motion_rigid_rigidtextureblend_6061, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblend_6061},
    {.hash = 0x621E86DD, .motion_shader = __motion_unit_weighted4_fur_alpha_621e, .kind = MotionKind::UNIT},
    {.hash = 0x6A599241, .motion_shader = __motion_rigid_rigidambientmapground_6a59, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidambientmapground_6a59},
    {.hash = 0x72BD9CFD, .motion_shader = __motion_unit_weighted_standard_4_feat_72bd, .kind = MotionKind::UNIT},
    // Banners (rigidtextureblend 2_bones): FF969484's skinning to the byte, only their UVs differ.
    {.hash = 0x76225B91, .motion_shader = __motion_unit_weighted_ff96, .kind = MotionKind::UNIT},
    {.hash = 0x88A29130, .motion_shader = __motion_sail_sailsymbol_88a2, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sailsymbol_88a2},
    {.hash = 0x8DE94E2F, .motion_shader = __motion_rigid_default_flag_8de9, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_default_flag_8de9},
    {.hash = 0x9BD142BD, .motion_shader = __motion_grass_vfx_phar_grass_flame_ti_9bd1},
    {.hash = 0xCB04CFA7, .motion_shader = __motion_rigid_rigidtextureblendwind_cb04, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblendwind_cb04},
    {.hash = 0xDB1EC9BA, .motion_shader = __motion_unit_weighted_standard_2_feat_db1e, .kind = MotionKind::UNIT},
    {.hash = 0xDD5D5694, .motion_shader = __motion_rope_rope_dd5d, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope_rope_dd5d},
    {.hash = 0xE7792B60, .motion_shader = __motion_tree_campaignvegetation_e779},
    {.hash = 0xED0BC391, .motion_shader = __motion_sail_sail_ed0b, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sail_ed0b},
    {.hash = 0xF704C8EC, .motion_shader = __motion_leaf_leaf_v3_f704},
    {.hash = 0xF81881EB, .motion_shader = __motion_unit_weightedcommon_f818, .kind = MotionKind::UNIT},
    {.hash = 0xF8FFE639, .motion_shader = __motion_unit_weighted_standard_2_feat_f8ff, .kind = MotionKind::UNIT},
    {.hash = 0xFF969484, .motion_shader = __motion_unit_weighted_ff96, .kind = MotionKind::UNIT},
}};

// Ray tracing's scene: 1ACBD907 (shaders__alphablend.xml and 87 other materials) draws the buildings, walls, clutter and the
// props blended into the terrain, and also the depth prefill again (6FDC1F3D, which binds a colour target: the 5BB18374 meshes, 498
// draws and 15.9 M indices in the six traces as 5BB18374's), the sea (water_troy 833E7963) and effects (vfx_spline_border 0051B762,
// ash 1B1F50CA). Only its G-buffer pixel shaders below are in the scene: the 26 the battle, siege and campaign traces drew
// (v58: the campaign's settlements and cliffs, D24616D0, 0.33 M triangles a frame, and 4 from the 2026-10-27 trace; tools\tw\pharaoh\v58\
// coverage.tsv). Cut out: CUT_OUT_MASKS; the others solid (no discard; a dither, t_dither.x; or a blend mask's test that can't
// fire: the three walls and terrain blends discard where 1 - mask.w x (1 - fade) or fade x mask.y is below 0, never for a
// texture's 0-1 values).
constexpr std::array<twattila::rt_scene::PixelMask, 26> BUILDING_MASKS = {{
    {.pixel_shader = 0x0842DFD4},  // phar_rigid_battle_blendmask_3tex_walls_sett (t_mask_texture.w, can't fire)
    {.pixel_shader = 0x1ABDA979},  // phar_rigid_battle_blendmask_4tex_walls_uv2
    {.pixel_shader = 0x23D56657},  // clutter_default
    {.pixel_shader = 0x246D4945},  // rigid_default_alpha_colorise (t_diffuse.w)
    {.pixel_shader = 0x2B2C6C82},  // rigid_default_colorize (t0 dither)
    {.pixel_shader = 0x3907635C},  // phar_rigid_battle_blendmask_3tex_statue
    {.pixel_shader = 0x509F97FB},  // phar_rigid_battle_blendmask_3tex_terrain
    {.pixel_shader = 0x538067F6},  // phar_rigid_battle_blendmask_3tex_walls (t_mask_texture.w, can't fire)
    {.pixel_shader = 0x5BB18374},  // phar_rigid_battle_blendmask_2tex_terrain (t_mask_texture.y, can't fire)
    {.pixel_shader = 0x5F579E7F},  // rigid_default (t0 dither)
    {.pixel_shader = 0x730C5322},  // phar_rigid_battle_1tex (terrain blend)
    {.pixel_shader = 0x7758E0D6},  // rigid_building_default
    {.pixel_shader = 0x81DC17E9},  // phar_rigid_battle_blendmask_3tex_floor_sett
    {.pixel_shader = 0x85E52BED},  // phar_rigid_battle_blendmask_2tex
    {.pixel_shader = 0x8EFA45A0},  // phar_rigid_battle_blendmask_3tex_uv2_noise_sand
    {.pixel_shader = 0x97572547},  // phar_rigid_battle_blendmask_3tex
    {.pixel_shader = 0xA5A850D3},  // phar_rigid_battle_blendmask_3tex_uv2
    {.pixel_shader = 0xA6C653E1},  // phar_rigid_building_default_colorise
    {.pixel_shader = 0xAC279A1B},  // phar_rigid_default_alpha (t_diffuse.w)
    {.pixel_shader = 0xB50D4F41},  // phar_rigid_default_alpha_colorize (t_diffuse.w)
    {.pixel_shader = 0xB87E0393},  // rigid_default_alpha (t_diffuse.w)
    {.pixel_shader = 0xC4349867},  // phar_rigid_battle_blendmask_2tex_walls
    {.pixel_shader = 0xC835A077},  // phar_rigid_building_default
    {.pixel_shader = 0xD24616D0},  // phar_rigid_campaign_blendmask_2tex_terrain (the campaign's settlements, cliffs)
    {.pixel_shader = 0xE25FD05C},  // phar_rigid_battle_blendmask_3tex_sand
    {.pixel_shader = 0xEA4AF236},  // phar_rigid_battle_blendmask_4tex_walls
}};

// 1ACBD907's and 6A599241's alpha-tested materials (sorted): each discards where t_diffuse (t0).w is below 0.5 at TEXCOORD1.xy
// (the vertex's TEXCOORD0.xy, the VS's o2) without the game's MSAA, the helper's cut-out (LEAF_ALPHA_CUTOFF 0.5); captured with
// that UV (rt_capture_rigid_default_cut_out, rt_capture_rigid_standard_cut_out). WARP on the game's bytes:
// tools\tw\warp_tests\pharaoh\cut_out_v52, trees_v58.
constexpr std::array<twattila::rt_scene::PixelMask, 8> CUT_OUT_MASKS = {{
    {.pixel_shader = 0x246D4945},  // rigid_default_alpha_colorise
    {.pixel_shader = 0x4B9BDD76},  // fx__rigidstandard gbuffer_alpha_test (6A599241)
    {.pixel_shader = 0x7550C192},  // rigidambientmapmesh gbuffer_alpha_test (6A599241)
    {.pixel_shader = 0x81931C18},  // rigidstandard gbuffer_alpha_test_shroud_fizzle_oblique_tris (6A599241)
    {.pixel_shader = 0xAC279A1B},  // phar_rigid_default_alpha
    {.pixel_shader = 0xB50D4F41},  // phar_rigid_default_alpha_colorize
    {.pixel_shader = 0xB87E0393},  // rigid_default_alpha
    {.pixel_shader = 0xBD6C63DE},  // rigidstandard gbuffer_shroud (6A599241)
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl's sun light draws (standard and skin lighting
// models), with the camera at b1 and the G-buffer's channels 1-4 from their first slot.
// Ray tracing's scene (twattila/rt_scene.hpp; tools\tw\rt_port.py): the main view's terrain, rigid objects, tree
// branches and leaves, units, grass, far tree cards, cloth and flags, and their capture versions (world positions; leaves,
// grass and cards also UVs).
// The floating unit banners (rigidtextureblend, 2 bones) stay out: the game gives them no shadow. So do the far forest's
// blocks (rigidtreebillboard vs40_main_block FE92BA2D): cards facing the camera with no shadow-pass draw, the forest's
// shadow there the game's kept baked map.
constexpr std::array<twattila::rt_scene::SceneShader, 27> SCENE_SHADERS = {{
    {.hash = 0x377DE0D5, .capture_shader = __rt_capture_terrain_geom_new, .terrain = true},
    {.hash = 0xDCC840AF, .capture_shader = __rt_capture_terrain_custom_material, .terrain = true},
    {.hash = 0xFAB7BA95, .capture_shader = __rt_capture_terrain_custom, .terrain = true},
    {.hash = 0x1ACBD907, .capture_shader = __rt_capture_rigid_default, .cut_out_shader = __rt_capture_rigid_default_cut_out, .clip_cut_outs = true, .pixel_masks = BUILDING_MASKS},
    {.hash = 0x6A599241, .capture_shader = __rt_capture_rigid_standard, .rows_shader = __motion_record_rigid_rigidambientmapground_6a59,
     .cut_out_shader = __rt_capture_rigid_standard_cut_out, .clip_cut_outs = true},
    // The campaign's palms and trees (rigidstandard vs40_main_campaign_trees): 6A599241's vertex shader and instance rows, then
    // an x / z sway of at most 0.12 m x g_wind_vegetation_multiplier (left out: still); B97AD7BC cuts out t0.w x the fade below 0.505.
    {.hash = 0x1C4B8681, .capture_shader = __rt_capture_rigid_standard_cut_out, .rows_shader = __motion_record_rigid_rigidambientmapground_6a59,
     .foliage = true, .still = true},
    {.hash = 0x6061911B, .capture_shader = __rt_capture_rigid_blend, .rows_shader = __motion_record_rigid_rigidtextureblend_6061},
    {.hash = 0xCB04CFA7, .capture_shader = __rt_capture_rigid_blend_wind, .rows_shader = __motion_record_rigid_rigidtextureblendwind_cb04},
    {.hash = 0x513D52B1, .capture_shader = __rt_capture_rigid_vertexpush},
    // Branches blend two sets of instance rows (v8-v10, v11-v13 by v6.xy): every frame as drawn, never placed.
    {.hash = 0x4A82B8C2, .capture_shader = __rt_capture_branch, .dynamic = true},
    {.hash = 0xF704C8EC, .capture_shader = __rt_capture_leaf, .foliage = true},
    // Far trees' cards (rigidtreebillboard vs40_main): turned to the sun, their alpha at t3 (t0 dissolve, t1 dither).
    {.hash = 0x97C53219, .capture_shader = __rt_capture_far_tree, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 3},
    // Cloth (phar_rigid_battle_cloth_wind) and flags (rigid_default_flag): sheets moved by the time and the wind, every frame.
    {.hash = 0x2F2A0E7E, .capture_shader = __rt_capture_cloth_wind, .dynamic = true, .thin = true},
    {.hash = 0x8DE94E2F, .capture_shader = __rt_capture_flag, .dynamic = true, .thin = true},
    {.hash = 0xFF969484, .capture_shader = __rt_capture_unit, .dynamic = true},
    {.hash = 0x4A716936, .capture_shader = __rt_capture_unit_4_bones, .dynamic = true},
    {.hash = 0x475A8CDF, .capture_shader = __rt_capture_unit_dismember, .dynamic = true},
    {.hash = 0x07936F02, .capture_shader = __rt_capture_unit_4_bones_dismember, .dynamic = true},
    {.hash = 0xE710D4E7, .capture_shader = __rt_capture_grass_e710, .dynamic = true, .foliage = true, .grass = true},
    // The shadow pass's casters (pass 6 of tools\tw\pharaoh\phase2.md, into the 2 x 2 atlas): what is out of the main view still casts
    // there (rt_outside_view.gs_5_0.hlsl: the game's own draws, world positions from their vertex shaders' outputs). Every frame as drawn;
    // solid (buildings too: DC5FE525's alpha-tested shadow draws, PS 985F8494, aren't cut out), leaves and far cards cut out by their
    // shadow pixel shaders' masks (6CA31B12 t0, 31718466 t2). No sprite soldiers: none drawn in any trace.
    {.hash = 0x67E28AF9, .terrain = true, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0xDC5FE525, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0xBC8DA38B, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x731EF7DD, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x039A9620, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x5AFB107C, .dynamic = true, .cascade_keep = __rt_outside_view_branch},
    {.hash = 0xA709F459, .dynamic = true, .foliage = true, .cascade_keep = __rt_outside_view_leaf},
    {.hash = 0xEB2A56A1, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 2, .cascade_keep = __rt_outside_view_card},
}};

// The screen-space shadows' Light Through Banners: the siege's cloth (phar_rigid_battle_cloth_wind, PS 5277CF9F and 97850C83)
// and flags (rigid_default_flag, PS 369C9E2E), as in the rays; both VS write SV_Position at o0. The banners (76225B91) stay
// out as there: the game draws them unlit (material 10) and gives them no shadow.
constexpr std::array<twattila::screen_space_shadows::ThinShader, 2> THIN_SHADERS = {{{.hash = 0x2F2A0E7E}, {.hash = 0x8DE94E2F}}};

// Water Clarity and Water Caustics (twattila/water_effects.hpp): every water pixel shader of the dump (battle and campaign sea,
// lake and river, the water props; ps_5_0 and ps_4_1), each a material water (its colour from its own constants, no
// g_sea_bed_light_scatter). Their absorption is exp2(-depth x opacity), no log2(e): Clarity scales the mul that reads the
// opacity, cb4[3].w (sea, lake, props), cb5[3].w (the campaign's sea), cb3[45].w / cb2[45].w (the campaign's big river) or the
// river's COLOR2.w (v11.w). The caustics after the refraction's colour sample, off in the campaign (its world a twentieth of a
// battle's). WARP: tools\tw\warp_tests\pharaoh\water_v58 (neutral: the game's bits).
constexpr std::array<twattila::water_effects::WaterShader, 84> WATER_SHADERS = {{
    {.hash = 0x4CA0D9CC, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_high (traced), shadows
    {.hash = 0x833E7963, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_ultra (traced), shadows
    {.hash = 0x3CD44028, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {3, 45}, .component = 3}},  // campaign_big_river (traced), shadows
    {.hash = 0x9CDEAD46, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_ultra (traced), shadows
    {.hash = 0xEA3E9B00, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_high (traced), shadows
    {.hash = 0xC0BB9ECD, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_high, baked shadows
    {.hash = 0x90FCD47D, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_high, shadows
    {.hash = 0xDFCFDDDA, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_high, ps_4_1
    {.hash = 0xC55527DC, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // battle_lake_high, no shadows
    {.hash = 0x234461B2, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_low, baked shadows
    {.hash = 0xD1702CFD, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_low, shadows
    {.hash = 0x978EAE9D, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_low, ps_4_1
    {.hash = 0x87C837D8, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // battle_lake_low, no shadows
    {.hash = 0xFEB833DF, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_medium, baked shadows
    {.hash = 0xECF83F6B, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_medium, shadows
    {.hash = 0xE9FFBC55, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_lake_medium, ps_4_1
    {.hash = 0xEBBDB926, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // battle_lake_medium, no shadows
    {.hash = 0xD08BF83F, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_high, baked shadows
    {.hash = 0x751065E4, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_high, ps_4_1
    {.hash = 0x06AA4824, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_high, no shadows
    {.hash = 0x28D418E2, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_low, baked shadows
    {.hash = 0x2AA4BAC8, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_low, shadows
    {.hash = 0x820579C1, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_low, ps_4_1
    {.hash = 0x112CDE4F, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_low, no shadows
    {.hash = 0x59EA33F4, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_medium, baked shadows
    {.hash = 0xB8D6EB67, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_medium, shadows
    {.hash = 0x27491210, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_medium, ps_4_1
    {.hash = 0x8E65BEFE, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 1, .index = {11, 0}, .component = 3}},  // battle_river_medium, no shadows
    {.hash = 0xA6DFC83D, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_high, baked shadows
    {.hash = 0xCF54FA00, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_high, shadows
    {.hash = 0xD17905E5, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_high, ps_4_1
    {.hash = 0xD21493DF, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // battle_sea_high, no shadows
    {.hash = 0xD1A66D4F, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_low, baked shadows
    {.hash = 0x0542C2C1, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_low, shadows
    {.hash = 0x90DBDB4A, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_low, ps_4_1
    {.hash = 0x7EE4F042, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // battle_sea_low, no shadows
    {.hash = 0x18ED5955, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_medium, baked shadows
    {.hash = 0x4ADA697D, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_medium, shadows
    {.hash = 0x73F12E9C, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_medium, ps_4_1
    {.hash = 0x0E377DF0, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // battle_sea_medium, no shadows
    {.hash = 0x591DC261, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_ultra, baked shadows
    {.hash = 0x0F82AFC8, .world_input = 1, .refraction_slot = 7, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // battle_sea_ultra, ps_4_1
    {.hash = 0xB9977DF2, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // battle_sea_ultra, no shadows
    {.hash = 0x1FA8BC8E, .world_input = 1, .refraction_slot = 5, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {3, 45}, .component = 3}},  // campaign_big_river, baked shadows
    {.hash = 0xD3909EC5, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {3, 45}, .component = 3}},  // campaign_big_river, ps_4_1
    {.hash = 0xD5521C8E, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {2, 45}, .component = 3}},  // campaign_big_river, no shadows
    {.hash = 0x34E357C3, .world_input = 1, .refraction_slot = 5, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_high, baked shadows
    {.hash = 0xD865AA3E, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_high, shadows
    {.hash = 0x192B07D0, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_high, ps_4_1
    {.hash = 0x305EC4CB, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // campaign_lake_high, no shadows
    {.hash = 0x0E930C0C, .world_input = 1, .refraction_slot = 5, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_low, baked shadows
    {.hash = 0xC51A1203, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_low, shadows
    {.hash = 0xB4823E2F, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_low, ps_4_1
    {.hash = 0xE8D2D529, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // campaign_lake_low, no shadows
    {.hash = 0x4DCD1A00, .world_input = 1, .refraction_slot = 5, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_medium, baked shadows
    {.hash = 0x52B3C97F, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_medium, shadows
    {.hash = 0x07FB971D, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_lake_medium, ps_4_1
    {.hash = 0x07DBF004, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // campaign_lake_medium, no shadows
    {.hash = 0xCF051012, .world_input = 1, .refraction_slot = 5, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_high, baked shadows
    {.hash = 0x6DB6D751, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_high, shadows
    {.hash = 0x87BEF758, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_high, ps_4_1
    {.hash = 0xBF868CB7, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_sea_high, no shadows
    {.hash = 0x68C9F797, .world_input = 1, .refraction_slot = 5, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_low, baked shadows
    {.hash = 0xD3E14854, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_low, shadows
    {.hash = 0x762EFE50, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_low, ps_4_1
    {.hash = 0xF64DBB81, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_sea_low, no shadows
    {.hash = 0xC0804990, .world_input = 1, .refraction_slot = 5, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_medium, baked shadows
    {.hash = 0x3CD00F55, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_medium, shadows
    {.hash = 0x66E7C0C3, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_medium, ps_4_1
    {.hash = 0x18794243, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_sea_medium, no shadows
    {.hash = 0x88927D58, .world_input = 1, .refraction_slot = 5, .caustics = false, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_ultra, baked shadows
    {.hash = 0x9DC3974A, .world_input = 1, .refraction_slot = 6, .caustics = false, .normal_slot = 8, .material_water = true, .opacity = {.type = 8, .index = {5, 3}, .component = 3}},  // campaign_sea_ultra, ps_4_1
    {.hash = 0x61417411, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // campaign_sea_ultra, no shadows
    {.hash = 0x24927A16, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_high, baked shadows
    {.hash = 0x1090D22F, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_high, ps_4_1
    {.hash = 0xC93FE38F, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // water_prop_high, no shadows
    {.hash = 0xE6F30447, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_low, baked shadows
    {.hash = 0x1B7C80C7, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_low, shadows
    {.hash = 0x33A766A2, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_low, ps_4_1
    {.hash = 0xC0195CC3, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // water_prop_low, no shadows
    {.hash = 0x05189050, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_med, baked shadows
    {.hash = 0x1F097D7C, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_med, shadows
    {.hash = 0x9476688A, .world_input = 1, .refraction_slot = 6, .normal_slot = 7, .material_water = true, .opacity = {.type = 8, .index = {4, 3}, .component = 3}},  // water_prop_med, ps_4_1
    {.hash = 0xF45F002E, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 3}, .component = 3}},  // water_prop_med, no shadows
}};

// The game's passes that read render-size textures at the display's size (texture slots), below 100%. The glow bright
// pass (after DLSS, VS 31C49ED2's uv 0-1 over the display) also reads the campaign's clouds at t0 (t_shroud_clouds, the
// shroud's half-size buffer drawn by 5EB2FDE8; a 1024 x 1024 stand-in in battles, its read gated off there), so its t1
// says whether the share applies. The AO blurs (7F637259 / BFD285BD) are replaced (shaders/ssao_blur.hlsli): a region
// read scaled their taps with their uv. The building pools (phar_building_water_01 ps_main_deferred_opaque_gbuffer, in no trace)
// read t_ssao at the view's uv (SV_Position x c29.zw): WARP tools\tw\warp_tests\pharaoh\scale_v61.
constexpr std::array<twattila::view_share::RegionRead, 5> RENDER_SCALE_READS = {{
    {.shader_hash = 0x57E5140E, .slots = 0b111, .size_slot = 1},  // glow bright pass
    {.shader_hash = 0x03B9CEFE, .slots = uint64_t{1} << 12},     // building water, shadows (ps_4_1)
    {.shader_hash = 0x9D28319F, .slots = uint64_t{1} << 12},     // building water, shadows
    {.shader_hash = 0x40154AD7, .slots = uint64_t{1} << 9},      // building water
    {.shader_hash = 0xAC04E623, .slots = uint64_t{1} << 11},     // building water, baked shadows
}};
// The game's 3D sprites (sprite.hlsl vertex_3d_gamma: the unit markers) drawn at the display's size after the tone mapper
// against the scene's depth (battle f07e0, siege 3aa60) and, in the battle, its copy (ec5e0): both stretched below 100%.
constexpr std::array<uint32_t, 1> FULL_DEPTH_SHADERS = {0x45A3B2DF};

constexpr std::array<twattila::light_bounce::SunDraw, 25> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0xF883F5B2, .standard = true, .gbuffer_slot = 8, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},
    {.shader_hash = 0xFA856110, .standard = true, .gbuffer_slot = 8, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},
    {.shader_hash = 0x40BE26E9, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 9},
    {.shader_hash = 0x5FF8081D, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 11},
    {.shader_hash = 0xFFD99CCD, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .sun_direction = 4},
    {.shader_hash = 0x1549C88A, .gbuffer_slot = 8, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},
    {.shader_hash = 0xE238D4C7, .gbuffer_slot = 8, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},
    {.shader_hash = 0x46B411BB, .gbuffer_slot = 5, .camera_slot = 1, .sun_direction = 4, .ao_slot = 9},
    {.shader_hash = 0x6C317272, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 11},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x24FD1318, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},  // slm_interior
    {.shader_hash = 0x4CA2F7BA, .standard = true, .gbuffer_slot = 1, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 9},  // slm_interior
    {.shader_hash = 0x6AFC2ACA, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},  // slm_edges
    {.shader_hash = 0x8213B16B, .standard = true, .gbuffer_slot = 3, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 11},  // slm_interior
    {.shader_hash = 0xA3EA9117, .standard = true, .gbuffer_slot = 1, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 9},  // slm_edges
    {.shader_hash = 0xC69A7EEB, .standard = true, .gbuffer_slot = 3, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 11},  // slm_edges
    {.shader_hash = 0xC701CDD0, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},  // slm_interior
    {.shader_hash = 0xE5A6A379, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},  // slm_edges
    {.shader_hash = 0x0F955270, .gbuffer_slot = 3, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 11},  // skinlm_edges
    {.shader_hash = 0x3323BDCC, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},  // skinlm_edges
    {.shader_hash = 0x47DDD6EE, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},  // skinlm_edges
    {.shader_hash = 0x4F5B587C, .gbuffer_slot = 1, .camera_slot = 1, .sun_direction = 4, .ao_slot = 9},  // skinlm_edges
    {.shader_hash = 0x64FD79C8, .gbuffer_slot = 3, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 11},  // skinlm_interior
    {.shader_hash = 0x7B122E81, .gbuffer_slot = 1, .camera_slot = 1, .sun_direction = 4, .ao_slot = 9},  // skinlm_interior
    {.shader_hash = 0x955658C3, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},  // skinlm_interior
    {.shader_hash = 0xF4E19A48, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},  // skinlm_interior
}};

// The shadow map size, before the game builds its shadows.
void OnInitDevice(reshade::api::device*) {
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
  twpharaohdynasties::shadow_map::Install(SHADOW_MAP_SIZES[static_cast<size_t>(shadow_map_size)]);
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // Before any setting is read this frame (as Attila's): DLSS never starts on another vendor's GPU, a saved DLSS or a
  // preset's is XeSS on Intel's, else FSR.
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  // The HBAO setting's Visibility Bitmask (ssao01): its Depth Levels (twattila/ao_pass.hpp; no Temporal Smoothing).
  twattila::ao_pass::Update(shader_injection.ssao_mode >= 1.f, false, (shader_injection.ssao_resolution != 0.f) ? 2.f : 1.f,
                            false);
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
  // As Warhammer III's rows (the game's shadow maps under the rays shadow from behind the camera already: without Rays Replace
  // Shadow Maps the casters are for the other rays).
  rt_settings.casters_out_of_view = rt_settings.sun_shadows && rays_units_out_of_view_row != 0.f;
  rt_settings.terrain_outside_view = rays_hills_out_of_view_row != 0.f;
  // Skip the Shadow Maps: Ray-Traced Shadows Only's skip of the atlas's casters (SettingsOf: with the rays' sun shadows alone).
  rt_settings.shadows_only = rt_settings.sun_shadows && rays_replace_maps_row != 0.f && rays_skip_cascades_row != 0.f;
  // Fog Shadows alone light no surface: no Ray Reconstruction for them (as Warhammer III's).
  rt_settings.cheap_fog_alone = true;
  twattila::texture_lod_bias::SetWrappingTrilinear(lod_bias_trilinear_row != 0.f);
  // The game's reflections fall on the water alone (its apply DE4F92CD): twattila/water_reflections.hpp's gate.
  twattila::water_reflections::GateSettings(&rt_settings);
  // Ray-traced shadows replace the screen-space ones (the Shadows method, as Attila's).
  auto sss_settings = twattila::feature_settings::SssSettings(sss_sliders);
  sss_settings.enabled = sss_settings.enabled && !rt_settings.sun_shadows;
  twattila::screen_space_shadows::Update(swapchain, sss_settings);
  // GT-VBAO's Temporal Smoothing (its noise moving each frame) and Denoiser (twattila/ao_filter.hpp).
  const bool bitmask_ao = shader_injection.ssao_mode >= 1.f;
  static uint32_t ao_frame = 0;
  shader_injection.ssao_frame =
      (bitmask_ao && shader_injection.ssao_temporal != 0.f) ? static_cast<float>(++ao_frame % 64) : 0.f;
  twattila::ao_filter::Update(swapchain, {.temporal = bitmask_ao && shader_injection.ssao_temporal != 0.f,
                                          .edge_fix = shader_injection.ssao_edge_fix != 0.f,
                                          .denoise = bitmask_ao && shader_injection.ssao_denoise != 0.f});
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  twpharaohdynasties::shadow_map::Set(SHADOW_MAP_SIZES[static_cast<size_t>(shadow_map_size)]);
  twattila::light_count::OnPresent(rt_sliders.debug_log != 0.f);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
  twpharaohdynasties::dead_units::Update(dead_units.scale, &dead_units_installed, graphics_status);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: PHARAOH DYNASTIES by ATN";  // TODO: change me!

namespace {

// Graphics 1 of 2's (twattila/graphics_page.hpp): what is true here. The rays' sun shadows multiply the game's shadow maps
// (the sun shaders' patched comparisons) or replace the per-frame ones (Rays Replace Shadow Maps), ray-traced AO multiplies
// the game's SSAO; point lights, the fog and the water's reflections hooked, no smoke.
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = twattila::graphics_page::ATTILA_TEXTS.aa_labels,
    .aa_tooltip = twattila::graphics_page::ATTILA_TEXTS.aa_tooltip,
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its ambient occlusion\n"
                     "and shadows. The rest at its default.",
    .no_rays_tooltip = "Bend Screen-Space Shadows (Length 480), GT-VBAO and GT-VBGI at their Recommended settings.\n"
                       "The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows and water reflections, path-traced ambient occlusion and Global Illumination at my\n"
                    "settings. The rest of this page at its default. Needs an upscaler (Anti-Aliasing above).\n"
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
    .specular_occlusion_label = "Ray-Traced Specular Occlusion (Experimental)",
    .water_caustics_label = "Water Caustics (Experimental)",
    .water_reflections_label = "Water Reflections (Experimental)",
    .water_reflections_tooltip = "Rivers and the sea reflect what the rays find, also off screen. Needs in-game Screen Space\n"
                                 "Reflections on. Off: the game's reflections.",
    .fog_shadows_label = "Fog Shadows (Experimental)",
    .fog_shadows_tooltip = "Light shafts from rays in place of the game's own in the fog: it loses the sun's glow in the shadow of\n"
                           "buildings, trees and hills, measured over the first 400 m. Some fog stays as the game draws it.",
    .light_glow_label = "Torch and Fire Light in Fog (Experimental)",
    .light_glow_tooltip = "Torches and fires light the fog around them, with their shafts and shadows in it. Needs Fog Shadows. 0: off.",
    .replace_maps_tooltip = "The sun's shadows from rays alone, so Softness shows in full. Water, effects and the shadows the game\n"
                            "baked for the map (past about 200 m) keep the game's. Use with Shadow Casters Out of View.\n"
                            "Off: the rays' shadows on top of the game's shadow maps.",
    .effects_at_output_tooltip = "While the upscaler renders below 100%, ambient occlusion, Light Bounce and shadow softness keep\n"
                                 "their 100% detail. Looks better, can cost some performance.",
    .reflections_tooltip = "Reflections on water. Need in-game Screen Space Reflections on.\n"
                           "Game's SSR: the original. DXR: from rays, also what is off screen or hidden (turn Water Reflections\n"
                           "on below). Needs an upscaler.",
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

          // Resource Upgrades
    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .shader_hash = 0x6F09FC39,
    //       // .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::copy_source
    //   });


      

    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::copy_dest,
    //   });

    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::shader_resource,
    //   });
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
  model_detail.tweaks = {
      {.name = "distance_lod_scaler", .inverse = true},
  };
  grass_distance.tweaks = {
      {.name = "grass_visible_range"},
      {.name = "grass_fade_start"},
  };
  small_objects.tweaks = {
      {.name = "cull_small_screenspace_objects_default_factor"},
      {.name = "cull_small_screenspace_vegetation_factor"},
      {.name = "terrain_blended_objects_cull_distance_nomask"},
      {.name = "terrain_blended_objects_cull_distance_mask_low"},
  };
  building_detail.tweaks = {
      // Battle buildings' LOD distances (100, 200, 300, 600 m), copied into a building model's LODs when it loads.
      {.name = "battle_building_lod0_distance"},
      {.name = "battle_building_lod1_distance"},
      {.name = "battle_building_lod2_distance"},
      {.name = "battle_building_lod3_distance"},
  };
  campaign_detail.tweaks = {
      // The campaign map's trees (30-120 m), plants (60 m, when their models load) and rigid trees (100 m).
      {.name = "campaign_tree_lod0_distance"},
      {.name = "campaign_tree_lod1_distance"},
      {.name = "campaign_tree_lod2_distance"},
      {.name = "campaign_tree_lod3_distance"},
      {.name = "campaign_grass_lod0_distance"},
      {.name = "campaign_grass_lod1_distance"},
      {.name = "campaign_grass_lod2_distance"},
      {.name = "campaign_grass_lod3_distance"},
      {.name = "campaign_grass_lod4_distance"},
      {.name = "rigid_campaign_tree_lod"},
  };
  terrain_detail.tweaks = {
      {.name = "terrain_distance_scale_low"},
      {.name = "terrain_distance_scale_medium"},
      {.name = "terrain_distance_scale_high"},
      {.name = "terrain_distance_scale_ultra"},
      {.name = "terrain_far_shader_distance"},
  };
  reflection_quality.tweaks = {
      {.name = "screen_space_environment_occlusion_step_count", .integer = true},
  };
  contact_shadow_quality.tweaks = {
      {.name = "screen_space_shadows_step_count", .integer = true},
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
  graphics_options = {&unit_detail[0], &unit_detail[1], &unit_detail[2], &unit_detail[3], &unit_detail[4], &unit_animation, &unit_variety, &footstep_effects, &model_detail, &grass_distance, &small_objects, &terrain_detail, &building_detail, &campaign_detail, &reflection_quality, &contact_shadow_quality};
  for (auto& test : engine_tests) graphics_options.push_back(&test.option);
  graphics_status = new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Engine values: not searched yet",
      .section = "Graphics (Experimental)",
  };
  // Graphics presets (ATN): MAX (every graphics setting at its maximum) and Reset (all graphics settings and tests at their
  // defaults, as Attila's; not the ambient occlusion or the Light Bounce).
  std::vector<std::pair<std::string, float>> graphics_defaults = {
      {"GraphicsShadowMapSize", 0.f},
      {"GraphicsUnitDetailVerySmall", 1.f},
      {"GraphicsUnitDetailSmall", 1.f},
      {"GraphicsUnitDetailMedium", 1.f},
      {"GraphicsUnitDetailLarge", 1.f},
      {"GraphicsUnitDetailVeryLarge", 1.f},
      {"GraphicsDeadUnits", 1.f},
      {"GraphicsUnitAnimation", 1.f},
      {"GraphicsUnitVariety", 1.f},
      {"GraphicsFootstepEffects", 1.f},
      {"GraphicsModelDetail", 1.f},
      {"GraphicsGrassDistance", 1.f},
      {"GraphicsSmallObjects", 1.f},
      {"GraphicsTerrainDetail", 1.f},
      {"GraphicsBuildingDetail", 1.f},
      {"GraphicsCampaignDetail", 1.f},
      {"GraphicsReflectionQuality", 1.f},
      {"GraphicsContactShadowQuality", 1.f},
  };
  for (const auto& test : engine_tests) {
    // Its option's start: the game's (1x, Game's or -1).
    graphics_defaults.emplace_back(test.key, test.option.scale);
  }
  const std::vector<std::pair<std::string, float>> graphics_max = {
      {"GraphicsShadowMapSize", 3.f},
      {"GraphicsUnitDetailVerySmall", 8.f},
      {"GraphicsUnitDetailSmall", 8.f},
      {"GraphicsUnitDetailMedium", 8.f},
      {"GraphicsUnitDetailLarge", 8.f},
      {"GraphicsUnitDetailVeryLarge", 8.f},
      {"GraphicsDeadUnits", 1.f},
      {"GraphicsUnitAnimation", 4.f},
      {"GraphicsUnitVariety", 4.f},
      {"GraphicsFootstepEffects", 4.f},
      {"GraphicsModelDetail", 8.f},
      {"GraphicsGrassDistance", 8.f},
      {"GraphicsSmallObjects", 8.f},
      {"GraphicsTerrainDetail", 4.f},
      {"GraphicsBuildingDetail", 8.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsReflectionQuality", 4.f},
      {"GraphicsContactShadowQuality", 4.f},
  };
  // A preset: every graphics setting and test at its default but `changes`.
  const auto preset = [&graphics_defaults](const std::vector<std::pair<std::string, float>>& changes) {
    auto values = graphics_defaults;
    for (const auto& [key, value] : changes) {
      std::ranges::find(values, key, &std::pair<std::string, float>::first)->second = value;
    }
    return values;
  };
  // Attila's two (ATN's settings with the Unit Sizes mod's 4x / 8x units, and with the game's), as this game has the rows:
  // Unit Animation also holds the faces and hands, Campaign Detail the plants; no campaign props here.
  const auto with_units = [&preset](float unit_detail, float dead_units, float animation, float buildings) {
    return preset({
        {"GraphicsUnitDetailVerySmall", unit_detail},
        {"GraphicsUnitDetailSmall", unit_detail},
        {"GraphicsUnitDetailMedium", unit_detail},
        {"GraphicsUnitDetailLarge", unit_detail},
        {"GraphicsUnitDetailVeryLarge", unit_detail},
        {"GraphicsDeadUnits", dead_units},
        {"GraphicsUnitAnimation", animation},
        {"GraphicsBuildingDetail", buildings},
        {"GraphicsUnitVariety", 3.f},
        {"GraphicsFootstepEffects", 3.f},
        {"GraphicsCampaignDetail", 4.f},
        {"GraphicsShadowMapSize", 1.f},
    });
  };
  // ATN's own settings: every graphics setting at its default but these.
  const auto graphics_atn = preset({
      {"GraphicsUnitDetailVerySmall", 1.2f},
      {"GraphicsUnitDetailSmall", 1.2f},
      {"GraphicsUnitDetailMedium", 1.2f},
      {"GraphicsUnitDetailLarge", 1.2f},
      {"GraphicsUnitDetailVeryLarge", 1.2f},
      {"GraphicsDeadUnits", 0.15f},
      {"GraphicsUnitAnimation", 2.f},
      {"GraphicsUnitVariety", 2.f},
      {"GraphicsFootstepEffects", 2.f},
      {"GraphicsModelDetail", 3.f},
      {"GraphicsGrassDistance", 1.25f},
      {"GraphicsSmallObjects", 4.f},
      {"GraphicsTerrainDetail", 4.f},
      {"GraphicsBuildingDetail", 6.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsContactShadowQuality", 4.f},
      {"GraphicsShadowMapSize", 1.f},
  });
  settings.insert(settings.end(), {
      tw::engine_tweaks::Note(),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "With my 4x / 8x unit sizes",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 0.69x, Dead Unit Detail 0.50x, Unit Animation 1.50x, Unit Variety and Footstep Effects\n"
                     "3x, Building Detail 3x, Campaign Detail 4x, Shadow Map Size 8192. The rest at its default.",
          .on_change = [values = with_units(0.69f, 0.5f, 1.5f, 3.f)]() { renodx::utils::settings::UpdateSettings(values); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "With vanilla unit sizes",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 1.20x, Dead Unit Detail 0.85x, Unit Animation, Unit Variety and Footstep Effects 3x,\n"
                     "Building Detail 4x, Campaign Detail 4x, Shadow Map Size 8192. The rest at its default.",
          .on_change = [values = with_units(1.2f, 0.85f, 3.f, 4.f)]() { renodx::utils::settings::UpdateSettings(values); },
      },
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
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "What I would use:",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 1.20x, Dead Unit Detail 0.15x, Unit Animation, Unit Variety and Footstep Effects 2x,\n"
                     "Model Detail 3x, Grass 1.25x, Small Objects and Terrain Detail 4x, Building Detail 6x, Campaign\n"
                     "Detail 4x, Contact Shadow Quality 4x, Shadow Map Size 8192. The rest at its default.",
          .on_change = [graphics_atn]() { renodx::utils::settings::UpdateSettings(graphics_atn); },
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
      tw::engine_tweaks::Slider(&dead_units, "GraphicsDeadUnits", "Dead Unit Detail Distance",
                                "Where dead soldiers turn into flat sprites, x the living ones' distance.\n"
                                "Lower: cheaper battles with many dead.",
                                1.f, 0.01f),
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
                                "Units, buildings and other models keep their detailed versions farther away.", 8.f),
      tw::engine_tweaks::Slider(&grass_distance, "GraphicsGrassDistance", "Grass Distance",
                                "Grass reaches farther.", 8.f),
      tw::engine_tweaks::Slider(&small_objects, "GraphicsSmallObjects", "Small Object Distance",
                                "Small objects, plants and rocks stay visible farther away.", 8.f),
      tw::engine_tweaks::Slider(&terrain_detail, "GraphicsTerrainDetail", "Terrain Detail Distance",
                                "Detailed terrain reaches farther.", 4.f),
      tw::engine_tweaks::Slider(&building_detail, "GraphicsBuildingDetail", "Building Detail Distance",
                                "Battle: buildings keep their detailed versions farther away. From the next battle.",
                                8.f),
      tw::engine_tweaks::Slider(&campaign_detail, "GraphicsCampaignDetail", "Campaign Detail Distance",
                                "Campaign map: trees and plants keep their detail farther away.\n"
                                "Plants: from the next campaign load.",
                                4.f),
      tw::engine_tweaks::Slider(&reflection_quality, "GraphicsReflectionQuality", "Reflection Quality",
                                "More steps for screen-space reflections: fewer gaps. Heavier.", 4.f),
      tw::engine_tweaks::Slider(&contact_shadow_quality, "GraphicsContactShadowQuality", "Contact Shadow Quality",
                                "More steps for the game's small screen-space shadows. Heavier.\n"
                                "Only with Game's Shadow Maps: Bend's and ray-traced shadows replace them.", 4.f),
      new renodx::utils::settings::Setting{
          .key = "GraphicsShadowMapSize",
          .binding = &shadow_map_size,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Shadow Map Size",
          .section = "Graphics (Experimental)",
          .tooltip = "Sharper sun shadows that flicker less, at the highest Shadows settings. More video memory\n"
                     "(16384: 1 GB or more). Applies after a restart or after changing the game's Shadows option.",
          .labels = {"6144 (Game's)", "8192", "12288", "16384"},
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
  // The water's reflection apply reads the ray-traced reflections (twattila/water_reflections.hpp): their rows, the water's too.
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, false, true);
  twattila::rt_scene::internal::water_pass = true;

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
    // No settings saved yet: a player's first start. On an SDR monitor it starts with the SDR preset.
    float saved_mode = 0.f;
    const bool first_start = !reshade::get_config_value(nullptr, (renodx::utils::settings::global_name + "-preset1").c_str(), "tonemapper", saved_mode);
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = value,
        .hdr_encoding = [](int output) {
          if (output != twattila::output_encoding::AUTO) return output;
          // Frame generation on from the start: HDR10, the frames need no pass of their own (frame_generation.hpp).
          const bool frame_generation = aa_sliders.method == 5.f && aa_sliders.frame_generation != 0.f;
          return frame_generation ? twattila::output_encoding::HDR10 : twattila::output_encoding::SCRGB;
        },
        .first_start_without_hdr = (first_start ? &ApplySdrPreset : nullptr),
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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarpharaoh/mods/19"); },
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

  // Graphics 1 of 2's layout (as Warhammer III's), ATN's RT/PT preset as the defaults of what the methods leave, then
  // Settings Mode's pages. Only what this game hooks: no smoke map.
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  // Fog Shadows and the torches' light in it start off too (Experimental: no game run has shown them here yet); RT/PT turns them
  // on, as ATN's list does. Bend's Light Through Banners and Coloured Shadows (v55: the cloth and flags, InstallThin) also start
  // off, Experimental, before the page reads its defaults (the presets write them).
  for (auto* setting : settings) {
    if (setting->key == "RayTracedFogShadows" || setting->key == "RayTracedLightGlow") {
      setting->default_value = 0.f;
    } else if (setting->key == "ScreenSpaceShadowsBannerLight") {
      setting->default_value = 0.f;
      setting->label = "Light Through Banners (Experimental)";
      setting->tooltip = "How much sun comes through the shadows cloth and flags cast: they are thin. 0: off.";
    } else if (setting->key == "ScreenSpaceShadowsColoured") {
      setting->default_value = 0.f;
      setting->label = "Coloured Shadows (Experimental)";
      setting->tooltip = "The sun through cloth and flags takes their colour.";
    } else if (setting->key == "WaterCaustics") {
      // Experimental, off (v58: no game run has shown the caustics yet); RT/PT turns them on, as ATN's list does.
      setting->default_value = 0.f;
    } else if (setting->key == "RayTracedSmokeShadows") {
      // Experimental, off (v57: no game run has shown the particles' records yet); RT/PT leaves it off (smoke_shadows_tested).
      setting->default_value = 0.f;
    }
  }
  // Water Reflections (Experimental, off, v56) and the reflection rows the water alone takes: twattila/water_reflections.hpp.
  twattila::water_reflections::SetRows(&settings, &rt_sliders);
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterClarity",
      .binding = &twattila::water_effects::water_clarity,
      .default_value = 1.f,
      .label = "Water Clarity (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "How far you see into water. 1: the game's; higher: clearer water.",
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
      .tooltip = "Rippling light on the bed under battle water and rivers. 0: off.",
      .max = 2.f,
      .format = "%.2f",
  });
  // Torch and Fire Shadows from rays (Warhammer III's row): after the preset's defaults, so it starts off (Experimental: no game
  // run has shown the light draws read yet).
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
  // The fires' particle lights in those rays (PARTICLE_LIGHTS): a row of their own, off and not in the RT/PT preset, so Torch
  // and Fire Shadows stays as it was while it is off.
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedParticleLightShadows",
      .binding = &particle_light_shadows_row,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Fire Shadows from Particle Lights (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Burning buildings and other fires cast their shadows from rays too.",
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
                                             .rays_hills_out_of_view = &rays_hills_out_of_view_row,
                                             .texts = &PAGE_TEXTS,
                                             .has_foliage_light = true,
                                             .foliage_light_tested = false,
                                             .smoke_shadows_tested = false,
                                             .lights_own_row = true,
                                             .rays_replace_ao = false});
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  twattila::test_hooks::Install(&settings, true);
  reshade::register_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  // Water Clarity and Caustics: every row a material water (the patched copy swapped in at the draw).
  twattila::water_effects::water_shaders = WATER_SHADERS;
  renodx::utils::pipeline::RegisterOnInitCallback(twattila::water_effects::OnInitMaterialWater);
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }

  // Rows saved at an old default, cleared once: Smoke Shadows was saved hidden at RT/PT's 1 (SetRaysPresetDefaults) until it
  // showed, Experimental and off.
  constexpr int SETTINGS_REVISION = 1;
  if (int revision = 0; !reshade::get_config_value(nullptr, "renodx", "SettingsRevision", revision) || revision < SETTINGS_REVISION) {
    for (const char* preset : {"renodx-preset1", "renodx-preset2", "renodx-preset3"}) {
      reshade::set_config_value(nullptr, preset, "RayTracedSmokeShadows", static_cast<const char*>(nullptr));
    }
    reshade::set_config_value(nullptr, "renodx", "SettingsRevision", SETTINGS_REVISION);
  }

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests: written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(nullptr, &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  // Ray-Traced Water Reflections (twattila/water_reflections.hpp): traced at backward_ssr 9A55B7F2 (its inputs from t1); where
  // they left from the water, the chain after it left out too (0B78827B, 55298A11, F49D4738) and our apply DE4F92CD drawn.
  twattila::water_reflections::Install(&custom_shaders,
                                       {.backward_ssr = 0x9A55B7F2, .inputs_slot = 1, .chain = {0x0B78827B, 0x55298A11, 0xF49D4738}, .apply = 0xDE4F92CD,
                                        .apply_code = __0xDE4F92CD},
                                       &rt_sliders);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // The Debug View's Screen-Space AO: before every other sun draw hook (they replace the AO the draw reads). No AO pass hook
  // keeps it here (ssao01 binds no camera).
  twattila::rt_scene::InstallSunDebugAmbient(LIGHT_BOUNCE_DRAWS, &rt_sliders);
  // Before the Light Bounce's (its callback draws the sun draw again).
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace. The ambient cube starts at
  // lighting_VS_PS float 24 (c6-c11: +x, -x, +y, -y, +z, -z; WARP on all 25 sun shaders, tools\tw\pharaoh\sun_test), the
  // sun colour at float 12 (c3; x 0x389FB405 in rt_sun: twpharaohdynasties/rt_sun.cs_5_0.hlsl), the paths' sun light.
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 24, 12);
  // Coloured Shadows (Bend's and the rays' filter at t50) on the sun's colour: lighting_VS_PS c3 (sun_colour; c1 is
  // sun_direction), in all 25 sun shaders' RDEF.
  twattila::screen_space_shadows::sun_colour_register = 3;
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  // Light Through Foliage: the G-buffer's channel 3 alpha byte, bit 128: leaves (5FC6C4AF), far tree cards (3DE66666) and
  // the far forest's blocks (6622ADC5) write 128, grass (12A15935) 160, no other drawn G-buffer shader sets it; the bit the
  // sun shader's own contact march lets half the sun through.
  twattila::screen_space_shadows::foliage_material_bit = 128;
  // Light Through Banners: the cloth and flags, the G-buffer's colour linear (an sRGB texture).
  twattila::screen_space_shadows::InstallThin(THIN_SHADERS, 1.f);
  // The game's contact shadows off where ours shadow: after the screen-space shadows' (they bind ours at t49), before the Light
  // Bounce's (it draws the sun draw itself with the constants bound then).
  twattila::contact_shadows_off::Install(LIGHT_BOUNCE_DRAWS);
  // Rays Replace Shadow Maps: also after the screen-space shadows' and before the Light Bounce's. Skip the Shadow Maps through
  // Ray-Traced Shadows Only (rt_settings.shadows_only, OnPresent).
  twattila::sun_replace_maps::Install(ATLAS_SUN_SHADERS, &rays_replace_maps_row);
  twattila::rt_scene::InstallShadowsOnly();
  // The frame trace (the data run): renodx_trace_now next to the exe traces the next frame, and FrameTrace's frame also
  // at 100% (twattila/frame_trace.hpp). The sun light draws' (standard and skin) and the tone mapper's constants once
  // each: b0-b5 up to 1 KB (camera b1 with camera_near_far at c25, shadowmap_PS b2's splits and baked map,
  // lighting_VS_PS b4's sun and ambient cube, directional_light_PS b5; the tone mapper's camera b0 and tone_mapping b1).
  // The 3D sprites' depth stretched below 100%, the scene's depth's copy too (FULL_DEPTH_SHADERS). The game maps many uvs to
  // its viewport itself (uv x (c29.zw x c31.xy): the fog's shadow ranges 99164788 and sun scan DB53BDDC, the shroud, the sun
  // shaders' SSEO, the SSR ray cast; tools\tw\pharaoh\v58\census_pharaoh.txt): view_share leaves them.
  twattila::view_share::SetViewportChains(true);
  // The campaign's shroud decals (campaignshrouddecals 61C823A0, 8852FBD3, AABB5EBA, 5078A6E0) build their NDC from SV_Position x
  // inv_rt_size (their buffer's c13): ATN_UP_181940 at 33% 1 / 1920, 1 / 1080 (their half-size target) at a 640 x 360 viewport,
  // so the NDC spanned a third of the view: patched as the camera's target size (WARP tools\tw\warp_tests\pharaoh\decal_v64).
  twattila::view_share::SetTargetSizes(std::array<const char*, 1>{"inv_rt_size"});
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, {}, FULL_DEPTH_SHADERS, {},
                                   {
                                       // Also the point light's and the AO's (g_viewport_offset_scale, g_Resolution: their
                                       // 100% values below 100%, assumed until a trace lists them), the particles' PS
                                       // vfxextremeultra 3F1B86E1, the shroud decals' 61C823A0 (inv_rt_size c13: render or
                                       // display size) and the campaign terrain's 1E864815.
                                       .value_hashes = {0xF883F5B2, 0xE238D4C7, 0x976C1C74, 0xE318CE77, 0xD42C3523, 0x3F1B86E1, 0x61C823A0,
                                                        0x1E864815},
                                       .value_slots = 6,
                                       .value_floats = 256,
                                       .on_demand = true,
                                       .at_100 = true,
                                       // The open facts (v56): the water reflection apply's blend (DE4F92CD) and the particles'
                                       // (3F1B86E1, effects quality 0-2's E8524BB1); the particle VS 109B79CE's buffers (t1,
                                       // t2, t4, t6) and its draws' indices and instances apart (smoke's layout).
                                       .blend_hashes = {0xDE4F92CD, 0x3F1B86E1, 0xE8524BB1},
                                       .vertex_input_hashes = {0x109B79CE},
                                       .instances_apart = true,
                                       .draw_reads = twpharaohdynasties::trace_reads::Read,
                                   },
                                   true);
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  // Before the Light Bounce: it draws the sun draws itself (no callback after it runs).
  twattila::ao_filter::Install(LIGHT_BOUNCE_DRAWS);
  // The ambient occlusion pass: the Visibility Bitmask in its place (the rays' AO goes in at the sun draws).
  twattila::ao_pass::Install(AO_DRAWS, &shader_injection, sizeof(shader_injection));
  // The TW games' view; the shadow pass's casters through world positions (its vertex shaders' view-projection at cb0[9-12], not
  // Warhammer III's cb0[10-13]), from the sun's per-frame atlas only (R32, 2 x 2 cascades; not the kept baked map, R16 4096 x 4096).
  twattila::rt_scene::Host rt_host = twattila::rt_scene::TW_HOST;
  rt_host.cascade_world = true;
  rt_host.cascade_target = [](const D3D11_TEXTURE2D_DESC& desc) {
    return desc.ArraySize == 1 && desc.Width == desc.Height && desc.Format == DXGI_FORMAT_R32_TYPELESS;
  };
  // The point light PS's diffuse scale (E318CE77's literal 0x389FB405, Warhammer III's).
  rt_host.local_light_scale = 7.615242e-05f;
  // The water's reflections (OnBackwardSsr): its guides again from the sun draw's G-buffer channels, the water drawn into them
  // since (TW_HOST's RewriteGuides reads PS slots 0-2, stale at backward_ssr's dispatch), and its R16G16B16A16F light copied.
  rt_host.rewrite_guides = twattila::anti_aliasing::RewriteSunGuides;
  rt_host.light_copy = __rt_light_copy;
  // No ambient occlusion pass: ssao01 D42C3523 binds only its own constants (ssao2_PS at b0, the linear depth at t0), no
  // camera, so the trace and the rays' AO stay at the sun draws (their t_ssao at ao_slot).
  twattila::rt_scene::Install(SCENE_SHADERS, {}, rt_host);
  twattila::rt_scene::InstallCutOutMasks(CUT_OUT_MASKS);
  // The point lights (siege trace: one instanced draw, VS E19549A1 with PS E318CE77, after the sun draw F883F5B2; fire02 reads
  // the rays' bits). Log Details' light count also counts the particle lights (5AD46115).
  twattila::rt_scene::InstallPointLights(std::array<uint32_t, 1>{0xE318CE77});
  // The fires: their particle lights' draws, the lights streamed (PARTICLE_LIGHTS, behind its own row); particlelight00 reads
  // the bits.
  twattila::rt_scene::InstallPointLights(std::array<uint32_t, 1>{0x5AD46115}, twattila::rt_scene::internal::NO_LIGHT_CONSTANTS, &PARTICLE_LIGHTS);
  twattila::rt_scene::InstallSmokeShadows(SMOKE_VERTEX_SHADERS, SMOKE_PIXEL_SHADERS);
  twattila::light_count::Install(0xE318CE77, 0x5AD46115);
  // Fog Shadows: rt_scene's fog hook binds the rays, then ours swaps the fog's shader where they are bound (registration order).
  twattila::rt_scene::InstallFogShadows(FOG_SHADERS);
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyFogShadowsDevice);
  renodx::utils::command_action::Register(
      OnFogShadowsDraw, {.shader_hash = FOG_SHADERS[0], .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);
  renodx::utils::command_action::Register(
      OnSkyDomeGuides, {.shader_hash = 0x9CF980D0, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});

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
  tw::engine_tweaks::Uninstall(graphics_options);
  twpharaohdynasties::dead_units::Uninstall();
  twpharaohdynasties::shadow_map::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  renodx::utils::command_action::Unregister(twattila::water_effects::OnWaterDraw);
  renodx::utils::pipeline::UnregisterOnInitCallback(twattila::water_effects::OnInitMaterialWater);
  twattila::engine_tests::StopAtStart(&at_start_stop);
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  twattila::contact_shadows_off::Uninstall();
  twattila::sun_replace_maps::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::rt_scene::Uninstall();
  renodx::utils::command_action::Unregister(OnFogShadowsDraw);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyFogShadowsDevice);
  OnDestroyFogShadowsDevice(nullptr);
  twattila::light_count::Uninstall();
  renodx::utils::command_action::Unregister(OnSkyDomeGuides);
  twattila::water_reflections::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
  twattila::ao_filter::Uninstall();
  twattila::ao_pass::Uninstall();

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
