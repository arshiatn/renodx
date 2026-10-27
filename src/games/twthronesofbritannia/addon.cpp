/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <algorithm>
#include <cmath>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/graphics_hooks.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/ao_pass.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "../twattila/ssr_hiz.hpp"
#include "../twrome2/engine_tweaks.hpp"
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

// void ApplyHDRPreset(
//     float exposure,
//     float highlights,
//     float shadows,
//     float contrast,
//     float purity_scale,
//     float cone_response_exponent,
//     float bloom,
//     float godrays,
//     float vignette,
//     float distortion,
//     float filmgrain,
//     float some_indicators,
//     float auto_exposure_highlight_protection,
//     float auto_exposure_highlight_headroom_stops,
//     float vfx_fire_brightness,
//     float vfx_blood_splash_brightness,
//     float vfx_fog_brightness,
//     float vfx_snow,
//     float vfx_rain,
//     float hue_shift) {
//   renodx::utils::settings::UpdateSettings({
//       {"tonemapper", 1.f},
//       {"exposure", exposure},
//       {"highlights", highlights},
//       {"shadows", shadows},
//       {"contrast", contrast},
//       {"purity_scale", purity_scale},
//       {"cone_response_exponent", cone_response_exponent},
//       {"hue_shift", hue_shift},
//       {"bloom", bloom},
//       {"godrays", godrays},
//       {"vignette", vignette},
//       {"distortion", distortion},
//       {"filmgrain", filmgrain},
//       {"some_indicators", some_indicators},
//       {"auto_exposure_highlight_protection", auto_exposure_highlight_protection},
//       {"auto_exposure_highlight_headroom_stops", auto_exposure_highlight_headroom_stops},
//       {"vfx_fire_brightness", vfx_fire_brightness},
//       {"vfx_blood_splash_brightness", vfx_blood_splash_brightness},
//       {"vfx_fog_brightness", vfx_fog_brightness},
//       {"vfxsnow", vfx_snow},
//       {"vfxrain", vfx_rain},
//   });
// }

void ApplyResetPreset() {
  // Reset only the active HDR image controls.
  // Preserve display calibration and global settings.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 2.f},  // no Extended mode here: PsychoV31 (ATN)

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},

      // Extra
      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"filmgrain", 1.f},
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
      {"v31_highlight_hue_shift", 0.13f},
      {"v31_highlight_saturation_shift", 0.f},
      {"video_autohdr", 0.f},
  });
}

// Recommended (ATN: every recommended preset in PsychoV31): PsychoV31 at its defaults, Video AutoHDR 0.20.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 2.f},
      {"video_autohdr", 0.2f},
  });
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
      // UpgradeRTVReplaceShader(0xEE1DA45C),
      // UpgradeRTVReplaceShader(0x850AEE44),
      // UpgradeRTVReplaceShader(0x56DE55F3),
      // UpgradeRTVReplaceShader(0x9BA371A3),
      // UpgradeRTVReplaceShader(0x2C229891),
      // UpgradeRTVReplaceShader(0xCB9D771B),
      // UpgradeRTVReplaceShader(0x129A6955),
      // UpgradeRTVReplaceShader(0x47B6D580),
      // UpgradeRTVReplaceShader(0x200D77F2),
  UpgradeRTVReplaceShader(0x8BF6B657),    //t00 auto exposre + LUT + some more stuffs
  UpgradeRTVReplaceShader(0xD0FAF089),    //t01 AA off/MLAA + DOF off + Distoriton off + Filmgrain off
  UpgradeRTVReplaceShader(0x0D18E0E9),    //t02 AA off/MLAA + DOF off + Distoriton on + Filmgrain off
  UpgradeRTVReplaceShader(0x40462368),    //t03 AA off/MLAA + DOF off + Distoriton off + Filmgrain on
  UpgradeRTVReplaceShader(0x1A8B4861),    //t04 AA off/MLAA + DOF off + Distoriton on + Filmgrain on
  UpgradeRTVReplaceShader(0x0872321D),    //t05 AA off/MLAA + DOF on + Distoriton off + Filmgrain off
  UpgradeRTVReplaceShader(0x55D276C9),    //t06 AA off/MLAA + DOF on + Distoriton on + Filmgrain off
  UpgradeRTVReplaceShader(0x7E15EB30),    //t07 AA off/MLAA + DOF on + Distoriton off + Filmgrain on
  UpgradeRTVReplaceShader(0x572661FA),    //t08 AA off/MLAA + DOF on + Distoriton on + Filmgrain on
  UpgradeRTVReplaceShader(0x10D135FB),    //t09 AA 2x/4x/8x + DOF off + Distoriton off + Filmgrain off
  UpgradeRTVReplaceShader(0xD3F8514D),   //t10 AA 2x/4x/8x + DOF off + Distoriton on + Filmgrain off
  UpgradeRTVReplaceShader(0xBA669496),   //t11 AA 2x/4x/8x + DOF off + Distoriton off + Filmgrain on
  UpgradeRTVReplaceShader(0x9A80FD94),   //t12 AA 2x/4x/8x + DOF off + Distoriton on + Filmgrain on
  UpgradeRTVReplaceShader(0x01A26750),   //t13 AA 2x/4x/8x + DOF on + Distoriton off + Filmgrain off
  UpgradeRTVReplaceShader(0x76428D7D),  //t14 AA 2x/4x/8x + DOF on + Distoriton on + Filmgrain off
  UpgradeRTVReplaceShader(0x7B522144),   //t15 AA 2x/4x/8x + DOF on + Distoriton off + Filmgrain on
  UpgradeRTVReplaceShader(0xBF43914D),   //t16 AA 2x/4x/8x + DOF on + Distoriton on + Filmgrain on
  UpgradeRTVReplaceShader(0x95398068),   //t17 constant tone mapper with bloom
  UpgradeRTVReplaceShader(0x6E20B0EF),   //t18 constant tone mapper
      __ALL_CUSTOM_SHADERS,
  };
  // The reflections' pass (environment_specular_light, Attila's shaders): the ray-traced reflections, else the
  // hierarchical march's pyramid and resolve (rt_scene.hpp, ssr_hiz.hpp).
  for (const uint32_t hash : {0x66006BEAu, 0x47D75349u}) {
    custom_shaders[hash].on_draw = twattila::rt_scene::OnReflectionDraw;
    custom_shaders[hash].on_drawn = twattila::rt_scene::OnReflectionDrawn;
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
        .label = "Use these in-game settings: Gamma 100% and Brightness 36%.",
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
        .tooltip = "SDR: the original SDR tonemapper.\n"
                   "HDR (PsychoV30): the HDR pipeline with PsychoV30 (default).\n"
                   "HDR (PsychoV31): the HDR pipeline with PsychoV31, more grading sliders.",
        .labels = {"SDR", "HDR (PsychoV30)", "HDR (PsychoV31)"},
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "HDR (PsychoV31) and Video AutoHDR 0.20. Peak, Paper White, UI brightness, Settings Mode and output\n"
                   "encoding are kept.",
        .on_change = []() { ApplyRecommendedPreset(); },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Resets active HDR image controls to defaults. Peak, Paper White, UI brightness, Settings Mode, and output encoding are preserved.",
        .on_change = []() {
          ApplyResetPreset();
        },
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
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Reset",
    //     .section = "Presets",
    //     .group = "preset-line-1",
    //     .tooltip = "Resets all image tuning and VFX sliders to their default HDR values.",
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
    //           1.0f,  // vignette
    //           1.0f,  // distortion
    //           1.0f,  // filmgrain
    //           0.4f,  // some_indicators
    //           0.0f,  // auto_exposure_highlight_protection
    //           1.0f,  // auto_exposure_highlight_headroom_stops
    //           1.0f,  // vfx_fire_brightness
    //           1.0f,  // vfx_blood_splash_brightness
    //           1.0f,  // vfx_fog_brightness
    //           1.0f,  // vfxsnow
    //           1.0f,  // vfxrain
    //           1.0f   // hue_shift
    //       );
    //     },
    // },
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
               "0 = off, 1 = SDR hues. Default 0.13.",
               1.f, 0.13f),
    v31_slider("v31_highlight_saturation_shift", &shader_injection.v31_highlight_saturation_shift,
               "Highlight Saturation Shift",
               "Gives bright colors the saturation of the SDR image (fire cores less pale and pink).\n"
               "0 = off (default), 1 = SDR's.",
               1.f, 0.f),

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
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "lensflare",
        .binding = &shader_injection.lensflare,
        .default_value = 1.f,
        .label = "Lensflare",
        .section = "Extra",
        .tooltip = "Lensflare multiplier.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
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
    


    // // Auto Exposure //////////////////////////////////////////////////////////////////////////////////////
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
    // // VFX //////////////////////////////////////////////////////////////////////////////////////
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


// Graphics (Experimental): Attila's (the same engine): the LOD pick hook (model detail), Attila's unit hook
// (twattila/graphics_hooks.hpp) and engine tweaks (filled in ConfigureAddon).
std::array<tw::engine_tweaks::Option, 4> unit_detail;  // foot soldiers, cavalry, elephants, artillery
float big_battle_fix = 0.f;
float shadow_map_size = 0.f;  // the game's sizes x 2^this (graphics_hooks.hpp shadow_map)
tw::engine_tweaks::Option dead_units;
tw::engine_tweaks::Option unit_animation;
tw::engine_tweaks::Option unit_variety;
tw::engine_tweaks::Option footstep_effects;
tw::engine_tweaks::Option object_detail;
tw::engine_tweaks::Option building_detail;
tw::engine_tweaks::Option campaign_detail;
// Every engine option above, then the tests' (Graphics 2 of 2: Attila's, twattila/engine_tests.hpp).
std::vector<tw::engine_tweaks::Option*> graphics_options;
std::vector<twattila::engine_tests::EngineTest> engine_tests;
std::atomic<bool> at_start_done = false;
std::atomic<bool> at_start_stop = false;
HMODULE addon_module_handle = nullptr;
twattila::feature_settings::SssSliders sss_sliders;
twattila::feature_settings::AaSliders aa_sliders;
// Ray tracing's (twattila/rt_scene.hpp).
twattila::rt_scene::Sliders rt_sliders;
float ssr_denoiser = 1.f;  // twattila/ssr_hiz.hpp: the resolve's history

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
bool graphics_found = false;
bool graphics_hooks_installed = false;

// Ambient Occlusion Visibility Bitmask (twattila/ao_pass.hpp): the addon's own shader in place of the game's AO
// pass (whose replacement is the game's code only).
constexpr std::array<twattila::ao_pass::AmbientOcclusionDraw, 1> AO_DRAWS = {{
    {.shader_hash = 0xBD389EF1, .bitmask = __ao_bitmask, .upsample = __ao_upsample, .resolve = __ao_resolve,
     .denoise = __ao_denoise},  // ssao00
}};

// DLAA / DLSS object motion (twattila/anti_aliasing.hpp, as Attila's): the game's shaders whose things move on
// their own and their motion versions (tools\tw\attila_motion\port_motion.py, GPU-checked by test_motion.py).
using twattila::anti_aliasing::MotionKind;
constexpr std::array<twattila::anti_aliasing::MotionShader, 32> MOTION_SHADERS = {{
    {.hash = 0xF3741248, .motion_shader = __motion_grass00},
    {.hash = 0x01FA9CDE, .motion_shader = __motion_grass01},
    {.hash = 0x46C616F0, .motion_shader = __motion_leaf_sm3},
    {.hash = 0x5B880546, .motion_shader = __motion_leaf_sm41},
    {.hash = 0xCABCB9D1, .motion_shader = __motion_leaf_sm5},
    {.hash = 0x77B993FD, .motion_shader = __motion_branch_sm3},
    {.hash = 0xB8C3E83D, .motion_shader = __motion_branch_sm41},
    {.hash = 0x6EEF4062, .motion_shader = __motion_branch_sm5},
    {.hash = 0xDC9FBA4A, .motion_shader = __motion_unit_sm3, .kind = MotionKind::UNIT},
    {.hash = 0x76682C60, .motion_shader = __motion_unit_4_bones_sm3, .kind = MotionKind::UNIT},
    {.hash = 0x0D8C2A96, .motion_shader = __motion_unit_dismember_sm3, .kind = MotionKind::UNIT},
    {.hash = 0x61DED001, .motion_shader = __motion_unit_4_bones_dismember_sm3, .kind = MotionKind::UNIT},
    {.hash = 0x25839CC5, .motion_shader = __motion_unit_sm41, .kind = MotionKind::UNIT},
    {.hash = 0xFF473BE8, .motion_shader = __motion_unit_4_bones_sm41, .kind = MotionKind::UNIT},
    {.hash = 0xC9F9DBA5, .motion_shader = __motion_unit_dismember_sm41, .kind = MotionKind::UNIT},
    {.hash = 0x65C0AE28, .motion_shader = __motion_unit_4_bones_dismember_sm41, .kind = MotionKind::UNIT},
    {.hash = 0x347F3DD9, .motion_shader = __motion_rigid_sm3, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_sm3},
    {.hash = 0xD830AD9B, .motion_shader = __motion_rigid_sm41, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_sm41},
    {.hash = 0xB06CB969, .motion_shader = __motion_rigid_blend_sm3, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_blend_sm3},
    {.hash = 0x10FE7EBC, .motion_shader = __motion_rigid_blend_wind_sm3, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_blend_wind_sm3},
    {.hash = 0xF80C6EEE, .motion_shader = __motion_oldocean},
    {.hash = 0x772B26FF, .motion_shader = __motion_cloth_sm3, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_cloth_sm3},
    {.hash = 0x902253FD, .motion_shader = __motion_cloth_sm41, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_cloth_sm41},
    {.hash = 0xD11B5EC7, .motion_shader = __motion_sail_sm3, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sm3},
    {.hash = 0x202FA188, .motion_shader = __motion_sail_sm41, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sm41},
    {.hash = 0xB2CCA764, .motion_shader = __motion_sail_symbol_sm3, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_symbol_sm3},
    {.hash = 0xE9C34FAB, .motion_shader = __motion_sail_symbol_sm41, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_symbol_sm41},
    {.hash = 0x8D0D26B9, .motion_shader = __motion_rope_sm3, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope_sm3},
    {.hash = 0x9EECBA5D, .motion_shader = __motion_ocean_deep, .kind = MotionKind::HISTORY, .history_textures = 0b10},
    {.hash = 0x97101A59, .motion_shader = __motion_ocean_shallow_sm3, .kind = MotionKind::HISTORY, .history_textures = 0b1010},
    {.hash = 0xE0671FA4, .motion_shader = __motion_ocean_shallow_sm4, .kind = MotionKind::HISTORY, .history_textures = 0b1010},
    {.hash = 0xF64EE8CB, .motion_shader = __motion_ocean_coast, .kind = MotionKind::HISTORY, .history_textures = 0b101010},
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl ps30 slm / slm_reflection (standard) and
// skinlm, every shadow method, with the camera at b0 and the G-buffer at t0-t3.
// Ray tracing's scene (twattila/rt_scene.hpp; tools\tw\rt_scene_map.py): the main view's terrain (rigidterrain geom /
// custom geom, shader models 3 and 4.1, old and new; Thrones' own), rigid objects, tree branches and leaves (Attila's
// shaders, the same hashes) and units (Thrones' own), and their capture versions (world positions; leaves also UVs).
constexpr std::array<twattila::rt_scene::SceneShader, 28> SCENE_SHADERS = {{
    {.hash = 0x289AC6F1, .capture_shader = __rt_capture_terrain_sm3, .terrain = true},
    {.hash = 0x958620C8, .capture_shader = __rt_capture_terrain_sm41, .terrain = true},
    {.hash = 0xBFD9331A, .capture_shader = __rt_capture_terrain_old_sm41, .terrain = true},
    {.hash = 0xC56F019B, .capture_shader = __rt_capture_terrain_old_sm3, .terrain = true},
    {.hash = 0x44EBD517, .capture_shader = __rt_capture_terrain_custom_material_sm3, .terrain = true},
    {.hash = 0xBC15D489, .capture_shader = __rt_capture_terrain_custom_material_sm41, .terrain = true},
    {.hash = 0x0BF40414, .capture_shader = __rt_capture_terrain_custom_sm41, .terrain = true},
    {.hash = 0x7E9E7121, .capture_shader = __rt_capture_terrain_custom_sm3, .terrain = true},
    {.hash = 0x347F3DD9, .capture_shader = __rt_capture_rigid_sm3},
    {.hash = 0xD830AD9B, .capture_shader = __rt_capture_rigid_sm41},
    {.hash = 0xB06CB969, .capture_shader = __rt_capture_rigid_blend_sm3},
    {.hash = 0x10FE7EBC, .capture_shader = __rt_capture_rigid_blend_wind_sm3},
    {.hash = 0x77B993FD, .capture_shader = __rt_capture_branch_sm3},
    {.hash = 0xB8C3E83D, .capture_shader = __rt_capture_branch_sm41},
    {.hash = 0x6EEF4062, .capture_shader = __rt_capture_branch_sm5},
    {.hash = 0x46C616F0, .capture_shader = __rt_capture_leaf_sm3, .foliage = true},
    {.hash = 0x5B880546, .capture_shader = __rt_capture_leaf_sm41, .foliage = true},
    {.hash = 0xCABCB9D1, .capture_shader = __rt_capture_leaf_sm5, .foliage = true},
    {.hash = 0xDC9FBA4A, .capture_shader = __rt_capture_unit_sm3, .dynamic = true},
    {.hash = 0x76682C60, .capture_shader = __rt_capture_unit_4_bones_sm3, .dynamic = true},
    {.hash = 0x0D8C2A96, .capture_shader = __rt_capture_unit_dismember_sm3, .dynamic = true},
    {.hash = 0x61DED001, .capture_shader = __rt_capture_unit_4_bones_dismember_sm3, .dynamic = true},
    {.hash = 0x25839CC5, .capture_shader = __rt_capture_unit_sm41, .dynamic = true},
    {.hash = 0xFF473BE8, .capture_shader = __rt_capture_unit_4_bones_sm41, .dynamic = true},
    {.hash = 0xC9F9DBA5, .capture_shader = __rt_capture_unit_dismember_sm41, .dynamic = true},
    {.hash = 0x65C0AE28, .capture_shader = __rt_capture_unit_4_bones_dismember_sm41, .dynamic = true},
    {.hash = 0xF3741248, .capture_shader = __rt_capture_grass_sm5, .dynamic = true, .foliage = true, .grass = true},
    {.hash = 0x01FA9CDE, .capture_shader = __rt_capture_grass_sm4, .dynamic = true, .foliage = true, .grass = true},
}};

// The game's passes that read render-size textures at the display's size (texture slots), below 100%.
constexpr std::array<twattila::view_share::RegionRead, 2> RENDER_SCALE_READS = {{
    {.shader_hash = 0x781681E4, .slots = 0b1},  // luma / bloom downsample
    {.shader_hash = 0x16390F4F, .slots = 0b11},  // shadow depth analysis
}};

constexpr std::array<twattila::light_bounce::SunDraw, 50> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0x1B10DDD8, .standard = true},
    {.shader_hash = 0x21B916AF, .standard = true},
    {.shader_hash = 0x2A84FFA0, .standard = true},
    {.shader_hash = 0x2AFF2B56, .standard = true},
    {.shader_hash = 0x3922F24C, .standard = true, .lighting_slot = 1},
    {.shader_hash = 0x4CFB45F9, .standard = true},
    {.shader_hash = 0x6F789EEE, .standard = true, .lighting_slot = 1},
    {.shader_hash = 0x70F6E2EA, .standard = true},
    {.shader_hash = 0x9ECED7F8, .standard = true, .lighting_slot = 1},
    {.shader_hash = 0xA1F4A2BA, .standard = true},
    {.shader_hash = 0xA7D2D500, .standard = true},
    {.shader_hash = 0xBCE62242, .standard = true},
    {.shader_hash = 0xDB252846, .standard = true},
    {.shader_hash = 0xDCD5E5BA, .standard = true, .lighting_slot = 1},
    {.shader_hash = 0x071E388A},
    {.shader_hash = 0x2742D8F3},
    {.shader_hash = 0x531B293D},
    {.shader_hash = 0x5E7DFFF2},
    {.shader_hash = 0x6D9D01EB},
    {.shader_hash = 0x77586F30},
    {.shader_hash = 0x7BC3D213, .lighting_slot = 1},
    {.shader_hash = 0x8107EC68},
    {.shader_hash = 0x8C667CF0},
    {.shader_hash = 0x93BB7878},
    {.shader_hash = 0xC8144F27, .lighting_slot = 1},
    {.shader_hash = 0xDB5CAA8F},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x13781D8E, .standard = true, .gbuffer_slot = 5, .camera_slot = 0, .lighting_slot = 1},  // slm_highdef
    {.shader_hash = 0x3F4EA48C, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0x70C16A46, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0x7F21230B, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0x89F671DD, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0x8F453935, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0x94D77BBB, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0xB6ADBBE1, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0xB7241C26, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0xC0B29F10, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0xDBF9617F, .standard = true, .gbuffer_slot = 5, .camera_slot = 0, .lighting_slot = 1},  // slm_lowdef
    {.shader_hash = 0xF145D6A2, .standard = true, .gbuffer_slot = 6, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0x220892C3, .gbuffer_slot = 1, .camera_slot = 0, .lighting_slot = 1},  // skinlm_highdef
    {.shader_hash = 0x332BD033, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0x46D58C5F, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
    {.shader_hash = 0x4D340C5E, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
    {.shader_hash = 0x576A0891, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0x5E1D5D83, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
    {.shader_hash = 0x6770157B, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0xADFD3FFB, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0xBCFFE786, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0xC4D3B93D, .gbuffer_slot = 1, .camera_slot = 0, .lighting_slot = 1},  // skinlm_lowdef
    {.shader_hash = 0xD89C82C0, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
    {.shader_hash = 0xF1009ADE, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
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
  twattila::graphics_hooks::shadow_map::Install(std::exp2(shadow_map_size));
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twattila::screen_space_shadows::Update(swapchain, twattila::feature_settings::SssSettings(sss_sliders));
  twattila::feature_settings::UpdateSsr(&shader_injection, ssr_denoiser);
  const twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders);
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  shader_injection.ssao_scale =
      twattila::ao_pass::RESOLUTION_SCALES[std::clamp(static_cast<int>(shader_injection.ssao_resolution), 0, 3)];
  shader_injection.ssao_frame =
      twattila::ao_pass::Update(shader_injection.ssao_mode >= 1.f, shader_injection.ssao_temporal != 0.f,
                                shader_injection.ssao_scale, shader_injection.ssao_denoise != 0.f);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  tw::engine_tweaks::Update(L"empire.retail.dll", graphics_options, &object_detail, &graphics_found, graphics_status);
  twattila::graphics_hooks::shadow_map::Set(std::exp2(shadow_map_size));
  twattila::graphics_hooks::Update(
      {
          .unit_detail = {unit_detail[0].scale, unit_detail[1].scale, unit_detail[2].scale, unit_detail[3].scale},
          .dead_units = dead_units.scale,
          .unit_animation = unit_animation.scale,
          .facial_animation = unit_animation.scale,
          .hand_animation = unit_animation.scale,
          .unit_variety = unit_variety.scale,
          .footstep_effects = footstep_effects.scale,
          .campaign_trees = campaign_detail.scale,
          .big_battle_fix = big_battle_fix != 0.f,
      },
      &graphics_hooks_installed, graphics_status, twattila::graphics_hooks::Game::THRONES);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - A Total War Saga: THRONES OF BRITANNIA by ATN";  // TODO: change me!

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
      .old_format = reshade::api::format::r8g8b8a8_unorm,
      .new_format = reshade::api::format::r16g16b16a16_float,
      .ignore_size = false,
      .use_resource_view_cloning = true,  // TODO: needed?
      .use_resource_view_hot_swap = true,
      .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
      .usage_include = reshade::api::resource_usage::render_target,
  });

  // Appended Settings (Graphics (Experimental))
  building_detail.tweaks = {
      // Battle buildings' LOD distances, copied into a building model's LODs when it loads (as Attila).
      {.name = "battle_building_lod0_distance"},
      {.name = "battle_building_lod1_distance"},
      {.name = "battle_building_lod2_distance"},
      {.name = "battle_building_lod3_distance"},
  };
  campaign_detail.tweaks = {
      // As Attila: plants when their models load, the rigid tree LOD and props live (the trees: the tree LOD hook).
      {.name = "campaign_grass_lod0_distance"},
      {.name = "campaign_grass_lod1_distance"},
      {.name = "campaign_grass_lod2_distance"},
      {.name = "campaign_grass_lod3_distance"},
      {.name = "campaign_grass_lod4_distance"},
      {.name = "rigid_campaign_tree_lod"},
      {.name = "campaign_props_max_distance"},
  };
  // Graphics 2 of 2's tests: Attila's (the same engine values).
  engine_tests = twattila::engine_tests::Attila();
  graphics_options = {&object_detail, &building_detail, &campaign_detail};
  for (auto& test : engine_tests) graphics_options.push_back(&test.option);
  graphics_status = tw::engine_tweaks::StatusLine();
  // Graphics presets (ATN): Reset (all graphics settings at their defaults), MAX (every
  // graphics setting at its maximum) and ATN's own settings.
  const std::vector<std::pair<std::string, float>> graphics_defaults = {
      {"GraphicsShadowMapSize", 0.f},
      {"ssr_quality", 1.f},
      {"ssr_hierarchical", 1.f},
      {"ssr_rays", 0.f},
      {"SSRDenoiser", 1.f},
      {"GraphicsUnitDetailSmall", 1.f},
      {"GraphicsUnitDetailMedium", 1.f},
      {"GraphicsUnitDetailLarge", 1.f},
      {"GraphicsUnitDetailHuge", 1.f},
      {"GraphicsDeadUnits", 1.f},
      {"GraphicsUnitAnimation", 1.f},
      {"GraphicsUnitVariety", 1.f},
      {"GraphicsFootstepEffects", 1.f},
      {"GraphicsObjectDetail", 1.f},
      {"GraphicsBuildingDetail", 1.f},
      {"GraphicsCampaignDetail", 1.f},
      {"GraphicsBigBattleFix", 0.f},
  };
  const std::vector<std::pair<std::string, float>> graphics_max = {
      {"GraphicsShadowMapSize", 2.f},
      {"ssr_quality", 8.f},
      {"GraphicsUnitDetailSmall", 8.f},
      {"GraphicsUnitDetailMedium", 8.f},
      {"GraphicsUnitDetailLarge", 8.f},
      {"GraphicsUnitDetailHuge", 8.f},
      {"GraphicsDeadUnits", 1.f},
      {"GraphicsUnitAnimation", 4.f},
      {"GraphicsUnitVariety", 4.f},
      {"GraphicsFootstepEffects", 4.f},
      {"GraphicsObjectDetail", 8.f},
      {"GraphicsBuildingDetail", 8.f},
      {"GraphicsCampaignDetail", 4.f},
  };
  // ATN's own settings: every graphics setting at its default but these.
  auto graphics_atn = graphics_defaults;
  for (const auto& [key, value] : std::vector<std::pair<std::string, float>>{
      {"GraphicsUnitDetailSmall", 1.75f},
      {"GraphicsUnitDetailMedium", 1.75f},
      {"GraphicsUnitDetailLarge", 1.75f},
      {"GraphicsUnitDetailHuge", 1.75f},
      {"GraphicsDeadUnits", 0.6f},
      {"GraphicsUnitAnimation", 2.5f},
      {"GraphicsUnitVariety", 2.5f},
      {"GraphicsFootstepEffects", 2.5f},
      {"GraphicsObjectDetail", 1.5f},
      {"GraphicsBuildingDetail", 3.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsShadowMapSize", 2.f},
       }) {
    std::ranges::find(graphics_atn, key, &std::pair<std::string, float>::first)->second = value;
  }
  const auto unit_distance = [](size_t index, const std::string& key, const std::string& label,
                                const std::string& units) {
    return tw::engine_tweaks::Slider(&unit_detail[index], key, label,
                                     "How far " + units + " stay 3D before turning into flat sprites.\n"
                                     "Higher: heavier for the CPU and GPU.",
                                     8.f, 0.1f);
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
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "What I would use:",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 1.75x, Dead Unit Detail 0.60x, Unit Animation, Unit Variety and Footstep Effects\n"
                     "2.50x, Model Detail 1.50x, Building Detail 3x, Campaign Detail 4x, Shadow Map Size 8192. The rest\n"
                     "at its default.",
          .on_change = [graphics_atn]() { renodx::utils::settings::UpdateSettings(graphics_atn); },
      },
  });
  {
    const auto reflections = twattila::feature_settings::SsrSettings(&shader_injection, &ssr_denoiser);
    settings.insert(settings.end(), reflections.begin(), reflections.end());
  }
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .key = "ssr_quality",
          .binding = &shader_injection.ssr_quality,
          .default_value = 1.f,
          .label = "Reflection Quality",
          .section = "Graphics (Experimental)",
          .tooltip = "Screen-space reflections on shiny surfaces (armour, metal): sharper, fewer gaps.\n"
                     "Heavier. 1 = the game's. Needs in-game Screen Space Reflections on.",
          .min = 1.f,
          .max = 8.f,
          .format = "%.0fx",
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
      tw::engine_tweaks::Slider(&object_detail, "GraphicsObjectDetail", "Model Detail Distance",
                                "Battle: trees and other objects keep their detailed versions farther away.", 8.f),
      tw::engine_tweaks::Slider(&building_detail, "GraphicsBuildingDetail", "Building Detail Distance",
                                "Battle: buildings keep their detailed versions farther away. From the next battle.",
                                8.f),
      tw::engine_tweaks::Slider(&campaign_detail, "GraphicsCampaignDetail", "Campaign Detail Distance",
                                "Campaign map: trees, plants and props keep their detail and are drawn farther away.\n"
                                "Plants: from the next campaign load.",
                                4.f),
      new renodx::utils::settings::Setting{
          .key = "GraphicsBigBattleFix",
          .binding = &big_battle_fix,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Big Battle Shadow Fix",
          .section = "Graphics (Experimental)",
          .tooltip = "In huge fights the game runs out of room to animate every soldier: some shadows stretch and\n"
                     "jump around. On: while that happens, the farthest soldiers turn into sprites a bit sooner.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "GraphicsShadowMapSize",
          .binding = &shadow_map_size,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Shadow Map Size",
          .section = "Graphics (Experimental)",
          .tooltip = "Sharper sun shadows that flicker less, at Shadows Max (lower Shadows settings scale\n"
                     "with it). More video memory. Applies after a restart.",
          .labels = {"2048 (Game's)", "4096", "8192"},
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
               "battle or after a restart; some may do nothing or crash.",
      .section = twattila::engine_tests::GRASS,
      .tint = 0xFFD23C,
  });
  twattila::engine_tests::Append(&settings, &engine_tests, SyncEarlyLoad);

  // Appended Settings (Ambient Occlusion, Global Illumination, Screen-Space Shadows, Anti-Aliasing)
  twattila::lighting_settings::Append(&settings, &shader_injection);
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, false, true);

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
                   "(Apache 2.0, Intel), reflections: AMD FidelityFX SSSR (MIT).",
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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/atotalwarsagathronesofbritannia/mods/1"); },
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

  reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests (audio memory): written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(L"empire.retail.dll", &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // Before the Light Bounce's (its callback draws the sun draw again).
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
  // The ambient occlusion pass before ray tracing's (its result multiplied after), as Attila.
  twattila::ao_pass::Install(AO_DRAWS, &shader_injection, sizeof(shader_injection));
  twattila::rt_scene::Install(SCENE_SHADERS, AO_DRAWS);
  // Last of the sun draws' hooks: it draws the sun draws itself (no callback after it runs).
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
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  at_start_stop = true;
  while (!at_start_done.load()) {
    Sleep(1);
  }
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  tw::engine_tweaks::Uninstall();
  twattila::graphics_hooks::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::ao_pass::Uninstall();
  twattila::rt_scene::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
  twattila::graphics_hooks::shadow_map::Uninstall();

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
