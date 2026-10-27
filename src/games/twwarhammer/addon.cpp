/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <cmath>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/ao_filter.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "./more_tests.h"
#include "../twrome2/engine_tweaks.hpp"
#include "../twwarhammer2/line_sweep_ao.hpp"
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
  // No Extended mode here: Reset in PsychoV31 (ATN).
  renodx::utils::settings::UpdateSettings({{"tonemapper", 2.f}, {"video_autohdr", 0.f}});
}

// Recommended (ATN: every recommended preset in PsychoV31): Reset, PsychoV31, Video AutoHDR 0.20.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemapper", 2.f}, {"video_autohdr", 0.2f}});
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
            twattila::anti_aliasing::OnTonemapDrawn(cmd_list);                                        \
            twattila::screen_space_shadows::OnTonemapDrawn(cmd_list); },                              \
      },                                                                                              \
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
                   "HDR (PsychoV31): the HDR pipeline with PsychoV31, more grading sliders.",
        .labels = {"SDR", "HDR (PsychoV30)", "HDR (PsychoV31)"},
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
        .is_enabled = IsHDRMode,
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
        .is_enabled = IsHDRMode,
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

// Output: 0 HDR10, 1 scRGB, 2 SDR (10-bit, gamma 2.2).
void SetOutputEncoding(int encoding) {
  shader_injection.swap_chain_encoding = static_cast<float>(encoding);
  if (encoding == 2) {
    renodx::mods::swapchain::target_format = reshade::api::format::r10g10b10a2_unorm;
    renodx::mods::swapchain::target_color_space = reshade::api::color_space::srgb_nonlinear;
  } else {
    renodx::mods::swapchain::SetUseHDR10(encoding == 0);
  }
}

// Output Auto: scRGB with Windows HDR on for the game's display (or unknown), else SDR. Set at the first swap chain,
// before the swapchain mod's create_swapchain (registered after this).
bool auto_output_pending = false;

bool OnCreateSwapchain(reshade::api::device_api, reshade::api::swapchain_desc&, void* hwnd) {
  // Frame generation takes no scRGB: HDR10 with it.
  const bool frame_generation = aa_sliders.method == 5.f && aa_sliders.frame_generation != 0.f;
  if (!auto_output_pending) {
    if (frame_generation && shader_injection.swap_chain_encoding == 1.f) SetOutputEncoding(0);
    return false;
  }
  auto_output_pending = false;
  const HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY);
  const auto path = renodx::utils::platform::GetPathInfo(monitor);
  const bool windows_hdr = !path.has_value() || renodx::utils::swapchain::GetHDREnabled(path.value());
  SetOutputEncoding(windows_hdr ? (frame_generation ? 0 : 1) : 2);
  return false;
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
// The floating unit banners (rigidtextureblend, 2 bones) stay out: the game gives them no shadow.
constexpr std::array<twattila::rt_scene::SceneShader, 28> SCENE_SHADERS = {{
    {.hash = 0x10401C91, .capture_shader = __rt_capture_terrain_geom_new, .terrain = true},
    {.hash = 0x06AB0766, .capture_shader = __rt_capture_terrain_geom_new_sm41, .terrain = true},
    {.hash = 0x5696080B, .capture_shader = __rt_capture_terrain_custom, .terrain = true},
    {.hash = 0x2A7775B0, .capture_shader = __rt_capture_terrain_custom_material, .terrain = true},
    {.hash = 0x7858BE0F, .capture_shader = __rt_capture_terrain_custom_sm41, .terrain = true},
    {.hash = 0xBDCF1803, .capture_shader = __rt_capture_terrain_custom_material_sm41, .terrain = true},
    {.hash = 0x51EDFD6E, .capture_shader = __rt_capture_rigid_standard},
    {.hash = 0xF6DBA4F4, .capture_shader = __rt_capture_rigid_standard_sm41},
    {.hash = 0x875BB987, .capture_shader = __rt_capture_rigid_default},
    {.hash = 0xD1A96B76, .capture_shader = __rt_capture_rigid_vertexpush},
    {.hash = 0xDBCC30FD, .capture_shader = __rt_capture_rigid_blend},
    {.hash = 0xC1E79772, .capture_shader = __rt_capture_rigid_blend_wind},
    {.hash = 0x29F7B3D7, .capture_shader = __rt_capture_branch},
    {.hash = 0xD26C7B7A, .capture_shader = __rt_capture_branch_sm40},
    {.hash = 0x327CC0DB, .capture_shader = __rt_capture_branch_sm41},
    {.hash = 0xF336AA76, .capture_shader = __rt_capture_leaf, .foliage = true},
    {.hash = 0x308BB4D5, .capture_shader = __rt_capture_leaf_sm40, .foliage = true},
    {.hash = 0xEDAC68AD, .capture_shader = __rt_capture_leaf_sm41, .foliage = true},
    {.hash = 0x20A9E5FD, .capture_shader = __rt_capture_unit, .dynamic = true},
    {.hash = 0x3FC5424C, .capture_shader = __rt_capture_unit_4_bones, .dynamic = true},
    {.hash = 0x0EA8B7F1, .capture_shader = __rt_capture_unit_sm41, .dynamic = true},
    {.hash = 0x2F6FFDA0, .capture_shader = __rt_capture_unit_4_bones_sm41, .dynamic = true},
    {.hash = 0x3F139F3D, .capture_shader = __rt_capture_unit_4_bones_dismember, .dynamic = true},
    {.hash = 0x306EDC60, .capture_shader = __rt_capture_unit_4_bones_dismember_sm41, .dynamic = true},
    {.hash = 0xC1334DA8, .capture_shader = __rt_capture_unit_dismember, .dynamic = true},
    {.hash = 0xDA2B125C, .capture_shader = __rt_capture_unit_dismember_sm41, .dynamic = true},
    {.hash = 0x86460214, .capture_shader = __rt_capture_unit_emissive_wind, .dynamic = true},
    {.hash = 0x2335CB90, .capture_shader = __rt_capture_unit_4_bones_wind, .dynamic = true},
}};

// The game's passes that read render-size textures at the display's size (texture slots), below 100%.
constexpr std::array<twattila::view_share::RegionRead, 1> RENDER_SCALE_READS = {{
    {.shader_hash = 0x404B4343, .slots = 0b11},  // shadow depth analysis
}};

constexpr std::array<twattila::light_bounce::SunDraw, 31> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0x230F2241, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},
    {.shader_hash = 0x27AF3D62, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},
    {.shader_hash = 0x2B3F8DC8, .standard = true, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 1},
    {.shader_hash = 0xC601C862, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},
    {.shader_hash = 0xCF0C5118, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},
    {.shader_hash = 0x9547FB5E, .standard = true, .gbuffer_slot = 3, .camera_slot = 1},
    {.shader_hash = 0x1CE77E85, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1},
    {.shader_hash = 0x4324BBE8, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},
    {.shader_hash = 0xA14D4C74, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},
    {.shader_hash = 0xA359A234, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},
    {.shader_hash = 0xB8F4B964, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x41365626, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},  // slm_edges
    {.shader_hash = 0x42114392, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},  // slm_interior
    {.shader_hash = 0x7BC58986, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},  // slm_edges
    {.shader_hash = 0x8A72EA4D, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},  // slm_interior
    {.shader_hash = 0xA8D2616A, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},  // slm_edges
    {.shader_hash = 0xC65915B6, .standard = true, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 1},  // slm_interior
    {.shader_hash = 0xD0F4F425, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},  // slm_interior
    {.shader_hash = 0xD56D16EA, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},  // slm_edges
    {.shader_hash = 0xD9DDB44A, .standard = true, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 5, .ao_slot = 2},  // slm_interior
    {.shader_hash = 0xDA9B8767, .standard = true, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 1},  // slm_edges
    {.shader_hash = 0x0C2B2229, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1},  // skinlm_interior
    {.shader_hash = 0x0EADBE7E, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},  // skinlm_interior
    {.shader_hash = 0x110884BF, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},  // skinlm_edges
    {.shader_hash = 0x34FEF0FF, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},  // skinlm_interior
    {.shader_hash = 0x7BA77953, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},  // skinlm_edges
    {.shader_hash = 0x8647AA2A, .gbuffer_slot = 4, .camera_slot = 2, .lighting_slot = 3, .ao_slot = 1},  // skinlm_edges
    {.shader_hash = 0x9AA0E558, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},  // skinlm_interior
    {.shader_hash = 0xA1AF2AEC, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},  // skinlm_edges
    {.shader_hash = 0xB1AFBCF1, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},  // skinlm_edges
    {.shader_hash = 0xCAD9815A, .gbuffer_slot = 5, .camera_slot = 2, .lighting_slot = 4, .ao_slot = 2},  // skinlm_interior
}};

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twattila::screen_space_shadows::Update(swapchain, twattila::feature_settings::SssSettings(sss_sliders));
  // GT-VBAO's Temporal Smoothing (its noise moving each frame) and Denoiser (twattila/ao_filter.hpp).
  const bool bitmask_ao = shader_injection.ssao_mode >= 1.f;
  static uint32_t ao_frame = 0;
  shader_injection.ssao_frame =
      (bitmask_ao && shader_injection.ssao_temporal != 0.f) ? static_cast<float>(++ao_frame % 64) : 0.f;
  twattila::ao_filter::Update(swapchain, {.temporal = bitmask_ao && shader_injection.ssao_temporal != 0.f,
                                          .edge_fix = shader_injection.ssao_edge_fix != 0.f,
                                          .denoise = bitmask_ao && shader_injection.ssao_denoise != 0.f});
  const twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders);
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.ray_tracing = rt_settings.sun_shadows || rt_settings.ambient_occlusion;
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: Warhammer by ATN";  // TODO: change me!

namespace {

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
  // Graphics 2 of 2's tests: Attila's that this game has, then More Tests (more_tests.h).
  engine_tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), more_tests::ATTILA_TESTS);
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
  // Graphics presets (ATN): MAX (every graphics setting at its maximum) and Reset (all
  // graphics settings at their defaults; not the ambient occlusion or the Light Bounce).
  shadow_map.tweaks = {
      {.name = "shadowmap_extreme_quality", .flag = true},
  };
  const std::vector<std::pair<std::string, float>> graphics_defaults = {
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
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders);

  // Appended Settings (HDR10)
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR. Default. Frame generation uses HDR10 for scRGB.\n"
                   "HDR10: 10-bit. scRGB: 16-bit. SDR: displays without HDR (all fixes and options stay).",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const int value = static_cast<int>(setting->GetValue());
    auto_output_pending = (value == 3);
    SetOutputEncoding(auto_output_pending ? 1 : value);
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV30 and PsychoV31 by ShortFuse. This mod uses Musa's custom PsychoV31. Visibility "
                   "Bitmask ambient occlusion and Light Bounce: the method of Therrien, Levesque and Gilet (2023) on "
                   "GTAO (Jimenez et al. 2016), as GT-VBAO and GT-VBGI by Mirko Salm (2024). Screen-space shadows: "
                   "Bend Studio (Apache 2.0, Sony Interactive Entertainment). SMAA (MIT, Jimenez et al.), CMAA2 "
                   "(Apache 2.0, Intel).",
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
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections);

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();
  tw::engine_tweaks::watch::StartIfRequested(graphics_options, WATCHED_VALUES, 300);

  reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
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
  twattila::rt_scene::Install(SCENE_SHADERS, {});
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace.
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS);
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection));
  // Before the Light Bounce: it draws the sun draws itself (no callback after it runs).
  twattila::ao_filter::Install(LIGHT_BOUNCE_DRAWS);
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  at_start_stop = true;
  while (!at_start_done.load()) {
    Sleep(1);
  }
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::rt_scene::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
  twattila::ao_filter::Uninstall();
  tw::engine_tweaks::Uninstall();

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
