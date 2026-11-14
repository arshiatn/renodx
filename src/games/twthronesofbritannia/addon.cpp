/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <algorithm>
#include <atomic>
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
#include "../../utils/command_action.hpp"
#include "../../utils/log.hpp"
#include "../../utils/pipeline.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/graphics_hooks.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/ao_pass.hpp"
#include "../twattila/engine_code.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/grass_fade.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/particle_layer.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "../twattila/smoke_map.hpp"
#include "../twattila/ssr_hiz.hpp"
#include "../twattila/test_hooks.hpp"
#include "../twattila/water_effects.hpp"
#include "../twrome2/engine_tweaks.hpp"
#include "../twattila/psycho35_settings.hpp"
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

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; Attila's chain, part 4).
constexpr float HUE_CLIP_DEFAULT = 40.f;

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
      {"tonemapper", 3.f},  // no Extended mode here: PsychoV35 (ATN 2026-11-12)
      {"gamma_correction", 1.f},
      {"sky_smooth", 0.f},
      {"sky_smooth_strength", 1.f},
      {"fog_strength", 1.f},

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
      {"dof", 1.f},
      {"distortion", 1.f},
      {"filmgrain", 1.f},
      {"some_indicators", 1.f},
      {"vfx_base_brightness", 1.f},
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
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

// Recommended (ATN 2026-11-12: every recommended preset in PsychoV35): PsychoV35 at its defaults, Video AutoHDR 0.20.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 3.f},
      {"video_autohdr", 0.2f},
  });
}

// SDR monitors (as Attila's): Reset's controls in Rendering Mode SDR, no video AutoHDR (the button also sets Output SDR).
void ApplySdrPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 0.f},
      {"video_autohdr", 0.f},
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
            twattila::screen_space_shadows::OnTonemapDrawn(cmd_list);                                 \
            twattila::anti_aliasing::OnTonemapDrawn(cmd_list);                                        \
            twattila::rt_scene::OnTonemapDrawn(cmd_list); },                                          \
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
  // Lens flares 00-03 and the vignette (Attila's shaders, the same bytes), drawn on the output after hdr_to_screen and its
  // anti-aliasing (S1 trace_100a l.4059-4072): part of frame generation's HUD-less image, as Attila's.
  for (const uint32_t hash : {0x37FA1283u, 0x177AC333u, 0xF0153CB5u, 0x1125AA01u, 0x2C002375u}) {
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
    twattila::graphics_page::Note(twattila::graphics_page::FULL_DOWNLOAD_NOTE, "General", twattila::graphics_page::WARNING_TINT,
                                  [] { return !twattila::dlaa_bridge::HelperInstalled(); }),
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
        .tooltip = "Rendering Mode SDR: the game's picture at gamma 2.0, turned into gamma 2.2.\n"
                   "Off: the game's own picture, at the in-game Gamma.",
        .labels = {"Off (In-game Gamma value)", "On (Gamma 2.2)"},
        .is_visible = [] { return !IsHDRMode(); },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "HDR (PsychoV35) and Video AutoHDR 0.20. Peak, Paper White, UI brightness, Settings Mode and output\n"
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
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "SDR (monitors without HDR)",
        .section = "Presets",
        .group = "preset-line-2",
        .tooltip = "For SDR monitors: Rendering Mode SDR (the game's own picture) and Output SDR (restart the game if it was\n"
                   "HDR), no video AutoHDR. The game's and the UI's white are your monitor's own white.",
        .tint = 0xFFC107,
        .on_change = []() {
          ApplySdrPreset();
          renodx::utils::settings::UpdateSettings({{"SwapChainEncoding", 2.f}});
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
        .max = 1.f,
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
        .key = "fog_strength",
        .binding = &shader_injection.fog_strength,
        .default_value = 1.f,
        .label = "Fog Strength",
        .section = "Extra",
        .tooltip = "How thick the game's fog and haze are. 0 = none, 1 = the game's, 2 = double.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
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
    // Particle Resolution as Attila's (twattila/particle_layer.hpp; shaders/vfx00 and vfx01, Attila's: WARP
    // tools/tw/thrones/vfx_test).
    new renodx::utils::settings::Setting{
        .key = "VfxHalfResolution",
        .binding = &twattila::particle_layer::half_resolution_row,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Particle Resolution (Experimental)",
        .section = "Extra",
        .tooltip = "Smoke, fire and dust drawn at half or a quarter of the render's size.\n"
                   "Much faster in big fires and smoke, softer particles.",
        .labels = {"Full", "Half", "Quarter"},
    },
    new renodx::utils::settings::Setting{
        .key = "VfxFullResolutionEdges",
        .binding = &twattila::particle_layer::edges_row,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Full-Resolution Particle Edges",
        .section = "Extra",
        .tooltip = "Smoke, fire and dust stay sharp against the outlines of units, trees and walls:\n"
                   "drawn at full size there. Small cost.",
        .labels = {"Off", "On"},
        .is_enabled = [] { return twattila::particle_layer::half_resolution_row != 0.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "VfxParticleSmoothing",
        .binding = &twattila::particle_layer::smoothing_row,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = "Smooth Half-Size Particles",
        .section = "Extra",
        .tooltip = "Half-size smoke, fire and dust keep clean edges against units and flicker less.\n"
                   "Small cost.",
        .labels = {"Off", "On"},
        .is_enabled = [] { return twattila::particle_layer::half_resolution_row != 0.f; },
    },
    // Attila's Extra (Experimental) rows on Attila's shaders, the same bytes here (shaders/vfx00 / vfx01, markersbattle00-02).
    // Attila's snow and weather rows stay out: its shaders pick them by the particle array's slices 1 and 7, not read here.
    new renodx::utils::settings::Setting{
        .key = "vfx_base_brightness",
        .binding = &shader_injection.vfxbasebrightness,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness: Flame, Dust, Clouds & Smoke",
        .section = "Extra (Experimental)",
        .tooltip = "Brightness of smoke, clouds, dust, and flame. This does not boost the inner fire color.",
        .min = 1.0f,
        .max = 30.0f,
        .format = "%.1f",
    },
    new renodx::utils::settings::Setting{
        .key = "some_indicators",
        .binding = &shader_injection.someindicators,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Unit/Building Indicator Brightness",
        .section = "Extra (Experimental)",
        .tooltip = "Brightness of selected highlight indicators, particularly useful at night.",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
    },



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
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));
}


// Graphics (Experimental): Attila's (the same engine): the LOD pick hook (model detail), Attila's unit hook
// (twattila/graphics_hooks.hpp) and engine tweaks (filled in ConfigureAddon).
std::array<tw::engine_tweaks::Option, 4> unit_detail;  // foot soldiers, cavalry, elephants, artillery
float big_battle_fix = 0.f;
float small_soldiers_as_sprites = 0.f;
float sprite_size = 20.f;  // pixels on screen
float shadow_map_size = 0.f;  // the game's sizes x 2^this (graphics_hooks.hpp shadow_map)
tw::engine_tweaks::Option dead_units;
tw::engine_tweaks::Option unit_animation;  // shared pose
tw::engine_tweaks::Option facial_animation;
tw::engine_tweaks::Option hand_animation;
tw::engine_tweaks::Option unit_variety;
tw::engine_tweaks::Option footstep_effects;
tw::engine_tweaks::Option object_detail;
tw::engine_tweaks::Option building_detail;
tw::engine_tweaks::Option campaign_trees;
tw::engine_tweaks::Option campaign_plants;
tw::engine_tweaks::Option campaign_props;
tw::engine_tweaks::Option grass_distance;  // graphics_hooks' scrub hooks
tw::engine_tweaks::Option grass_density;
// Every engine option above, then the tests' (Graphics 2 of 2: Attila's, twattila/engine_tests.hpp).
std::vector<tw::engine_tweaks::Option*> graphics_options;
std::vector<twattila::engine_tests::EngineTest> engine_tests;
std::atomic<bool> at_start_done = false;
std::atomic<bool> at_start_stop = false;
HMODULE addon_module_handle = nullptr;
twattila::feature_settings::SssSliders sss_sliders;
twattila::smoke_map::Sliders smoke_map_sliders;
twattila::feature_settings::AaSliders aa_sliders;
// Ray tracing's (twattila/rt_scene.hpp).
twattila::rt_scene::Sliders rt_sliders;
float ssr_denoiser = 1.f;  // twattila/ssr_hiz.hpp: the resolve's history
float lod_bias_trilinear_row = 0.f;  // twattila/texture_lod_bias.hpp SetWrappingTrilinear (graphics_page's row)
// Faster Game CPU Code (twattila/engine_code.hpp, Game::THRONES): Attila's three routines on this game's build (the soldier
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
// Ray tracing's scene (twattila/rt_scene.hpp; tools\tw\rt_scene_map.py), as Attila's (tools/tw/thrones/th_caster_census.txt
// over the data run's three battle traces, th_caster_census_campaign.txt over ATN's campaign traces: every G-buffer and
// shadow-map caster held; the campaign's far terrain BE94F17D, drawn into depth only and never into the shadow map, and its
// sea bed left out): the main view's terrain (rigidterrain geom / custom
// geom, shader models 3 and 4.1, old and new; Thrones' own), rigid objects and their alpha-tested cut-outs, rocks blended into
// the terrain, tree branches and leaves, grass, sprite soldiers and the campaign's plants and trees (Attila's shaders, the
// same hashes; their rows shaders Attila's), the far trees' cards (Thrones' 79DFD18D: Attila's inputs and view choice) and
// the units (Thrones' own, one rounding apart from Attila's; their cut-outs as Attila's).
// Light Through All Banners (Experimental, off): the unit banners (rigidtextureblend and its wind version, Attila's bytes:
// the 3D UI's army_banner_flag models' material, in data.pack; no trace has drawn them: level B) as thin cloth. Attila's
// flags the CPU moves (rigidcommon 347F3DD9 / D830AD9B from a dynamic vertex buffer) are left out: here every 347F3DD9
// draw is dynamic and culls back faces, and AAE52888 (the twin of Attila's flag pixel shader) draws 561-820 times a battle
// frame, so the rule would make every alpha-tested rigid object thin (th_drawn_names.py over the data run's traces).
float rays_banner_light_row = 0.f;

constexpr std::array<twattila::rt_scene::SceneShader, 38> SCENE_SHADERS = {{
    {.hash = 0x289AC6F1, .capture_shader = __rt_capture_terrain_sm3, .terrain = true},
    {.hash = 0x958620C8, .capture_shader = __rt_capture_terrain_sm41, .terrain = true},
    {.hash = 0xBFD9331A, .capture_shader = __rt_capture_terrain_old_sm41, .terrain = true},
    {.hash = 0xC56F019B, .capture_shader = __rt_capture_terrain_old_sm3, .terrain = true},
    {.hash = 0x44EBD517, .capture_shader = __rt_capture_terrain_custom_material_sm3, .terrain = true},
    {.hash = 0xBC15D489, .capture_shader = __rt_capture_terrain_custom_material_sm41, .terrain = true},
    {.hash = 0x0BF40414, .capture_shader = __rt_capture_terrain_custom_sm41, .terrain = true},
    {.hash = 0x7E9E7121, .capture_shader = __rt_capture_terrain_custom_sm3, .terrain = true},
    {.hash = 0x347F3DD9, .capture_shader = __rt_capture_rigid_sm3, .rows_shader = __motion_record_rigid_sm3, .cut_out_shader = __rt_capture_rigid_cut_out_sm3},
    {.hash = 0xD830AD9B, .capture_shader = __rt_capture_rigid_sm41, .rows_shader = __motion_record_rigid_sm41, .cut_out_shader = __rt_capture_rigid_cut_out_sm41},
    {.hash = 0xB06CB969, .capture_shader = __rt_capture_rigid_blend_sm3, .rows_shader = __motion_record_rigid_blend_sm3, .thin_row = &rays_banner_light_row},
    {.hash = 0x10FE7EBC, .capture_shader = __rt_capture_rigid_blend_wind_sm3, .rows_shader = __motion_record_rigid_blend_wind_sm3, .thin_row = &rays_banner_light_row},
    // Rocks and cliffs blended into the terrain (rigidterrainblend): its depth pass (its material pass has no camera).
    {.hash = 0xE03E161C, .capture_shader = __rt_capture_rigid_terrain_blend_sm3, .rows_shader = __motion_record_rigid_terrain_blend_sm3},
    {.hash = 0x77B993FD, .capture_shader = __rt_capture_branch_sm3, .rows_shader = __motion_record_tree},
    {.hash = 0xB8C3E83D, .capture_shader = __rt_capture_branch_sm41, .rows_shader = __motion_record_tree},
    {.hash = 0x6EEF4062, .capture_shader = __rt_capture_branch_sm5, .rows_shader = __motion_record_tree},
    {.hash = 0x46C616F0, .capture_shader = __rt_capture_leaf_sm3, .rows_shader = __motion_record_tree, .foliage = true},
    {.hash = 0x5B880546, .capture_shader = __rt_capture_leaf_sm41, .rows_shader = __motion_record_tree, .foliage = true},
    {.hash = 0xCABCB9D1, .capture_shader = __rt_capture_leaf_sm5, .rows_shader = __motion_record_tree, .foliage = true},
    {.hash = 0xDC9FBA4A, .capture_shader = __rt_capture_unit_sm3, .cut_out_shader = __rt_capture_unit_cut_out_sm3, .dynamic = true},
    {.hash = 0x76682C60, .capture_shader = __rt_capture_unit_4_bones_sm3, .cut_out_shader = __rt_capture_unit_4_bones_cut_out_sm3, .dynamic = true},
    {.hash = 0x0D8C2A96, .capture_shader = __rt_capture_unit_dismember_sm3, .cut_out_shader = __rt_capture_unit_dismember_cut_out_sm3, .dynamic = true},
    {.hash = 0x61DED001, .capture_shader = __rt_capture_unit_4_bones_dismember_sm3, .cut_out_shader = __rt_capture_unit_4_bones_dismember_cut_out_sm3, .dynamic = true},
    {.hash = 0x25839CC5, .capture_shader = __rt_capture_unit_sm41, .cut_out_shader = __rt_capture_unit_cut_out_sm41, .dynamic = true},
    {.hash = 0xFF473BE8, .capture_shader = __rt_capture_unit_4_bones_sm41, .cut_out_shader = __rt_capture_unit_4_bones_cut_out_sm41, .dynamic = true},
    {.hash = 0xC9F9DBA5, .capture_shader = __rt_capture_unit_dismember_sm41, .cut_out_shader = __rt_capture_unit_dismember_cut_out_sm41, .dynamic = true},
    {.hash = 0x65C0AE28, .capture_shader = __rt_capture_unit_4_bones_dismember_sm41, .cut_out_shader = __rt_capture_unit_4_bones_dismember_cut_out_sm41, .dynamic = true},
    // Grass placed (its instance rows), swaying within its share, past it still (rt_rows_grass).
    {.hash = 0xF3741248, .capture_shader = __rt_capture_grass_sm5, .rows_shader = __rt_rows_grass, .foliage = true, .grass = true},
    {.hash = 0x01FA9CDE, .capture_shader = __rt_capture_grass_sm4, .rows_shader = __rt_rows_grass, .foliage = true, .grass = true},
    // Cloth (banners, tents) and sails: thin, captured every frame (the CPU moves them).
    {.hash = 0x772B26FF, .capture_shader = __rt_capture_cloth_sm3, .dynamic = true, .thin = true},
    {.hash = 0x902253FD, .capture_shader = __rt_capture_cloth_sm41, .dynamic = true, .thin = true},
    {.hash = 0xD11B5EC7, .capture_shader = __rt_capture_sail_sm3, .dynamic = true, .thin = true},
    {.hash = 0x202FA188, .capture_shader = __rt_capture_sail_sm41, .dynamic = true, .thin = true},
    // Sprite soldiers: cards facing the sun, cut out by their diffuse map's alpha.
    {.hash = 0x1B9E7692, .capture_shader = __rt_capture_imposter, .dynamic = true, .foliage = true, .sun_facing = true},
    // Far trees' cards: the same, each showing the tree from the sun (their alpha at t1, as Attila's).
    {.hash = 0x79DFD18D, .capture_shader = __rt_capture_billboard, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 1},
    // The campaign's plants and forests, and its trees: placed like the battle's, no wind (924D7813 instanced in ATN's
    // campaign traces, its alpha-tested G-buffer PS DB830D45: the foliage's texture alpha at the hit).
    {.hash = 0x924D7813, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true},
    {.hash = 0x819A414C, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true},
    {.hash = 0x4657DA8A, .capture_shader = __rt_capture_campaign_tree, .rows_shader = __rt_rows_campaign_tree, .foliage = true, .still = true},
}};

// The units' G-buffer pixel shaders that clip at t0's alpha (weighted ps30_gbuffer without noclip), sorted: Attila's 92
// (56 the same hash, 36 Thrones twins; each clip to its discard Attila's, tools/tw/thrones/th_cut_outs.py over the dump
// and blood.pack).
constexpr std::array<uint32_t, 92> UNIT_CUT_OUT_PIXEL_SHADERS = {
    0x04451F97, 0x078FE2F8, 0x092FAE7D, 0x0C6FF9DE, 0x0CA5AA79, 0x11CD57DD, 0x16468155, 0x165E8EE7, 0x1B064C40, 0x20C7AB1F,
    0x21A8210A, 0x239963D1, 0x24576145, 0x24C09B83, 0x28C3EAD4, 0x29482F73, 0x2B7D2720, 0x2B92395E, 0x2C3EA513, 0x2D4B1E78,
    0x2DBF5C78, 0x2EA00B20, 0x32463113, 0x3BCC202B, 0x3CE0CAD3, 0x3DE5867F, 0x3DF777E5, 0x3EA3AE02, 0x3EA5229E, 0x4918D429,
    0x4DB87C72, 0x58BCF4AE, 0x61CD6092, 0x680FB8BE, 0x6D764062, 0x6E058CEB, 0x6EC2D95F, 0x7AC17F0A, 0x7B01A295, 0x7B85D2C8,
    0x7C44CB62, 0x7D6AFE41, 0x82FF1DB5, 0x8524F338, 0x879A8AFD, 0x8A031046, 0x8C501D38, 0x8D8F67DE, 0x940D83F4, 0x950C02A3,
    0x95BD2B3D, 0x9C38C3D8, 0xA01EEF45, 0xA223822E, 0xA482BA91, 0xA4C87797, 0xAF38BF92, 0xB6E881F4, 0xB866973E, 0xBA2A6179,
    0xBB381C44, 0xBCC9321F, 0xBE51C839, 0xBE7CF084, 0xC1822803, 0xC3057069, 0xC3144AD4, 0xC3728871, 0xC5DF37DF, 0xC5E213AC,
    0xC64A7A31, 0xC8CB357B, 0xCA398999, 0xCACD9B8E, 0xCEDA829D, 0xD5668711, 0xD764AEC8, 0xDCC30AB7, 0xDD85EE06, 0xE39EBF7B,
    0xE5A5F801, 0xE7AE71FE, 0xE91382B4, 0xEBD63231, 0xEDCD6CC3, 0xEE62565B, 0xEF7C0A67, 0xF0CF5FF7, 0xF504924C, 0xF6A9B5E2,
    0xFBD501F7, 0xFF1C02CC,
};

// The forward passes that read the sun's shadow map (the data run's traces, by the map's handle): the battle water and
// rivers, the lit particles and their other path (Attila's, the same bytes; Thrones draws the game's own, patched), the
// ocean (Thrones' F7E011A4: gather4_c at t9, S2's trace) and the campaign's sea (DE739661 with 960AB011 in ATN's campaign
// traces, its low twin 08F47D8C by name).
constexpr std::array<uint32_t, 7> SHADOW_READER_SHADERS = {0xC9FBACCB, 0x2E9F5935, 0xACE33D5D, 0xF5379F78, 0xF7E011A4, 0xDE739661, 0x08F47D8C};

// Particle Resolution (twattila/particle_layer.hpp): the light buffer's readers after the particles, where their layer goes
// in: the luma downsample (781681E4, next after the particles in every trace of the data run and ATN's campaign), else the
// tone mappers (Attila's twins by name and this game's replaced hdr_to_screen ones).
constexpr std::array<uint32_t, 20> PARTICLE_LAYER_READERS = {
    0x781681E4, 0x8BF6B657, 0x95398068, 0x6E20B0EF, 0xD0FAF089, 0x0D18E0E9, 0x40462368, 0x1A8B4861, 0x0872321D, 0x55D276C9,
    0x7E15EB30, 0x572661FA, 0x10D135FB, 0xD3F8514D, 0xBA669496, 0x9A80FD94, 0x01A26750, 0x76428D7D, 0x7B522144, 0xBF43914D,
};

// What only the campaign map draws (ATN's campaign traces against the battle traces): its borders' VS (Attila's, in every
// campaign frame), the shroud plane's VS, its sea (waterplane ps30_campaign, the low twin by name).
constexpr std::array<uint32_t, 4> CAMPAIGN_SHADERS = {0x0CC96E53, 0x2860172D, 0xDE739661, 0x08F47D8C};
constexpr auto OnCampaignDraw = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  twattila::frame_history::campaign_frames.store(2, std::memory_order_relaxed);
  return {};
};

// Attila's water rows read again here (tools/tw/thrones/th_water_rows.py: the same bytes, else Thrones' twin by name, its
// bindings from the reflection; WARP tools/tw/thrones/water_test). The seas' and the campaign lake's twins don't read
// g_sea_bed_light_scatter: Water Clarity changes the rivers, the battle plane and the placed water meshes only.
constexpr std::array<twattila::water_effects::WaterShader, 15> WATER_SHADERS = {{
    {.hash = 0x2E9F5935, .lighting_slot = 2, .world_input = 1, .refraction_slot = 2},  // battle river
    {.hash = 0xC9FBACCB, .lighting_slot = 2, .world_input = 3, .refraction_slot = 2},  // battle plane, Water Quality Ultra
    {.hash = 0x2E69BF8E, .lighting_slot = 1, .world_input = 3, .refraction_slot = 2},  // placed water meshes; the plane below Ultra
    {.hash = 0xF7E011A4, .lighting_slot = 2, .world_input = 2, .refraction_slot = 6},  // battle sea, high / ultra
    {.hash = 0xA9724510, .lighting_slot = 1, .world_input = 2, .refraction_slot = 6},  // battle sea, medium
    {.hash = 0x736DB670, .lighting_slot = 1, .world_input = 2, .refraction_slot = 5},  // battle sea, low
    {.hash = 0xDE739661, .lighting_slot = 2, .world_input = 3, .refraction_slot = 6, .caustics = false},  // campaign sea
    {.hash = 0x08F47D8C, .lighting_slot = 1, .world_input = 3, .refraction_slot = 6, .caustics = false},  // campaign sea, low
    {.hash = 0x4229959B, .lighting_slot = 1, .world_input = 3, .refraction_slot = 6, .caustics = false},  // campaign lake
    // Without shadows.
    {.hash = 0x52228234, .lighting_slot = 1, .world_input = 1, .refraction_slot = 2},
    {.hash = 0x5CB9A865, .lighting_slot = 1, .world_input = 1, .refraction_slot = 2},
    {.hash = 0xBF93E312, .lighting_slot = 1, .world_input = 2, .refraction_slot = 6},
    {.hash = 0x761A07B1, .lighting_slot = 1, .world_input = 2, .refraction_slot = 5},
    {.hash = 0xF6DFFEBC, .lighting_slot = 1, .world_input = 3, .refraction_slot = 6, .caustics = false},
    {.hash = 0xECB1E7B5, .lighting_slot = 1, .world_input = 3, .refraction_slot = 6, .caustics = false},
}};

// The screen-space shadows' Light Through Foliage: grass and leaves, and the rigid objects drawn after them (Attila's
// shaders, the same bytes in this dump: depth copies around the foliage, as Attila's; its draw order not yet seen here).
// Grass Height Distance's colour fade (twattila/grass_fade.hpp): this game's twins by name of Attila's 13 grass pixel shaders
// (rigidgrass(wet) gbuffer no_clip / gbuffer / gbuffer_ms, forward_lit_oit and _aoit by shadow method), each with the one
// fade grass_fade patches (tools/tw/thrones/th_grass_patterns.py; WARP tools/tw/thrones/grass_test); its depth pre-pass
// 125A0DD5 Attila's. The grass vertex shaders are Attila's bytes: shaders/grass00-02 Attila's.
constexpr std::array<uint32_t, 13> GRASS_FADE_SHADERS = {
    0xA196A465, 0x23A8968D, 0xF348EED4,                          // gbuffer_no_clip, gbuffer, gbuffer_ms
    0x0C375991, 0x89A78CBE, 0x305C940C, 0x32E0182A, 0x50138B9D,  // forward_lit_oit (none, pcf, fetch4, software, gather)
    0x99808ED0, 0x153497D2, 0x348F6F8C, 0x10109427, 0xF411ABD8,  // forward_lit_aoit (none, software, gather, pcf, fetch4)
};

constexpr std::array<uint32_t, 5> FOLIAGE_VERTEX_SHADERS = {0x46C616F0, 0x5B880546, 0xCABCB9D1, 0xF3741248, 0x01FA9CDE};
constexpr std::array<uint32_t, 4> AFTER_FOLIAGE_VERTEX_SHADERS = {0x347F3DD9, 0xD830AD9B, 0xB06CB969, 0x10FE7EBC};

// The screen-space shadows' Light Through Banners: cloth and sails, the unit banners with Light Through All Banners (as the rays).
constexpr std::array<twattila::screen_space_shadows::ThinShader, 6> THIN_SHADERS = {{
    {.hash = 0x772B26FF},
    {.hash = 0x902253FD},
    {.hash = 0xD11B5EC7},
    {.hash = 0x202FA188},
    {.hash = 0xB06CB969, .row = &rays_banner_light_row},
    {.hash = 0x10FE7EBC, .row = &rays_banner_light_row},
}};

// The game's passes that read render-size textures at the display's size (texture slots), below 100%, and the
// half-size passes drawn whole (their readers: view_share::WHOLE_TEXTURES).
constexpr std::array<twattila::view_share::RegionRead, 6> RENDER_SCALE_READS = {{
    {.shader_hash = 0x781681E4, .slots = 0b1},  // luma / bloom downsample
    {.shader_hash = 0x16390F4F, .slots = 0b11},  // shadow depth analysis
    {.shader_hash = 0xB4026AF2, .slots = 0b11, .whole = true},  // AO blur_x
    {.shader_hash = 0x6203C824, .slots = 0b10, .whole = true},  // AO blur_y
    {.shader_hash = 0x820D451B, .slots = 0b11, .whole = true},  // water refraction (3 channels)
    {.shader_hash = 0x25944B2D, .slots = 0b1, .whole = true},   // water refraction (4 channels)
}};
// The game's vertex shaders the addon replaces with ones that read the render share below 100% (cb12), as Attila's.
constexpr std::array<uint32_t, 1> RENDER_SCALE_VERTEX_SHADERS = {
    0x7C04E880,  // lens flare: the sun's occlusion counted at the render size (lensflare04)
};
// The game's 3D sprites (sprite.hlsl vertex_3d*: the unit markers) drawn against the scene's depth after the tone mapper
// (S1 trace_50a l.3406: 2823783C on the 3840 x 2160 output, the depth 0x16009124 holding DLSS's render size): the depth
// stretched to the display's size for them below 100%, as Attila's (the same four hashes; the other three in the dump).
constexpr std::array<uint32_t, 4> FULL_DEPTH_SHADERS = {0x2823783C, 0xF1CA8DC6, 0x59A0821D, 0x23C7B5C0};

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

// The data run's clock (test_hooks: RENODX_TW_SET's seconds) from the first sun light draw: a command-line battle's first
// 3D frame, whatever its load took (the battle benchmark's camera runs 90 s). Its line is what the run script waits for.
std::atomic_bool sun_drawn = false;
constexpr auto OnSunDrawClock = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  if (sun_drawn.load(std::memory_order_relaxed) || sun_drawn.exchange(true, std::memory_order_relaxed)) return {};
  renodx::utils::log::i("[Thrones] First run: sun light draw (the tests' clock starts).");
  twattila::test_hooks::StartClock();
  return {};
};

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
  twattila::graphics_hooks::shadow_map::Install(std::exp2(shadow_map_size));
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  if (auto& campaign_frames = twattila::frame_history::campaign_frames; campaign_frames.load(std::memory_order_relaxed) != 0) {
    campaign_frames.fetch_sub(1, std::memory_order_relaxed);
  }
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twattila::particle_layer::Update(swapchain);
  twattila::grass_fade::Update(shader_injection.grass_height_distance);
  // Before any setting is read this frame (as Attila's): DLSS never starts on another vendor's GPU, a saved DLSS or a
  // preset's is XeSS on Intel's, else FSR.
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
  twattila::texture_lod_bias::SetWrappingTrilinear(lod_bias_trilinear_row != 0.f);
  // Ray-traced shadows replace the screen-space ones too.
  auto sss_settings = twattila::feature_settings::SssSettings(sss_sliders);
  sss_settings.enabled = sss_settings.enabled && !rt_settings.sun_shadows;
  twattila::screen_space_shadows::Update(swapchain, sss_settings);
  // The rays bring their own smoke.
  auto smoke_map_settings = smoke_map_sliders;
  if (rt_settings.sun_shadows) smoke_map_settings.strength = 0.f;
  twattila::smoke_map::Update(smoke_map_settings);
  rt_settings.stream_particles = smoke_map_settings.strength > 0.f;
  // Ray Reconstruction is the only denoiser: the reflections' denoiser (and its history) off under it.
  twattila::feature_settings::UpdateSsr(&shader_injection, (twattila::dlaa_bridge::bridge.reconstructing || twattila::dlaa_bridge::bridge.nrd_reflections) ? 0.f : ssr_denoiser);
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  shader_injection.ssao_scale = twattila::ao_pass::ResolutionScale(shader_injection.ssao_resolution);
  // The rays' AO or the paths' in place of the game's (ssao00) and GT-VBAO's (not drawn), as Attila's.
  shader_injection.rt_ambient_alone = (rt_settings.ambient_occlusion || rt_settings.path_ambient) ? 1.f : 0.f;
  shader_injection.ssao_frame =
      twattila::ao_pass::Update(shader_injection.ssao_mode >= 1.f && shader_injection.rt_ambient_alone == 0.f,
                                shader_injection.ssao_temporal != 0.f, shader_injection.ssao_scale, shader_injection.ssao_denoise != 0.f);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  twattila::engine_code::Update(engine_code_row != 0.f, twattila::engine_code::Game::THRONES);
  tw::engine_tweaks::Update(L"empire.retail.dll", graphics_options, &object_detail, &graphics_found, graphics_status);
  twattila::graphics_hooks::shadow_map::Set(std::exp2(shadow_map_size));
  twattila::graphics_hooks::Update(
      {
          .unit_detail = {unit_detail[0].scale, unit_detail[1].scale, unit_detail[2].scale, unit_detail[3].scale},
          .dead_units = dead_units.scale,
          .unit_animation = unit_animation.scale,
          .facial_animation = facial_animation.scale,
          .hand_animation = hand_animation.scale,
          .unit_variety = unit_variety.scale,
          .footstep_effects = footstep_effects.scale,
          .campaign_trees = campaign_trees.scale,
          .grass_distance = grass_distance.scale,
          .grass_density = grass_density.scale,
          // As Attila's (the shadow record's code the same here): the 3D shadows its rule lets go dropped while the rays'
          // shadows skip the sun's shadow map casters.
          .unit_shadows = (twattila::rt_scene::CastersSkipped() ? 0.f : 1.f),
          .big_battle_fix = big_battle_fix != 0.f,
          .sprite_pixels = (small_soldiers_as_sprites != 0.f ? sprite_size : 0.f),
          .focal_pixels = twattila::anti_aliasing::MainFocalPixels(),
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
  // As Attila: the rigid tree LOD live (the trees: the tree LOD hook), plants when their models load, props live.
  campaign_trees.tweaks = {{.name = "rigid_campaign_tree_lod"}};
  campaign_plants.tweaks = {
      {.name = "campaign_grass_lod0_distance"},
      {.name = "campaign_grass_lod1_distance"},
      {.name = "campaign_grass_lod2_distance"},
      {.name = "campaign_grass_lod3_distance"},
      {.name = "campaign_grass_lod4_distance"},
  };
  campaign_props.tweaks = {{.name = "campaign_props_max_distance"}};
  // Graphics 2 of 2's tests: Attila's (the same engine values).
  engine_tests = twattila::engine_tests::Attila();
  graphics_options = {&object_detail, &building_detail, &campaign_trees, &campaign_plants, &campaign_props};
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
      {"GraphicsFacialAnimation", 1.f},
      {"GraphicsHandAnimation", 1.f},
      {"GraphicsUnitVariety", 1.f},
      {"GraphicsFootstepEffects", 1.f},
      {"GraphicsObjectDetail", 1.f},
      {"GraphicsBuildingDetail", 1.f},
      {"GraphicsCampaignDetail", 1.f},
      {"GraphicsCampaignPlants", 1.f},
      {"GraphicsCampaignProps", 1.f},
      {"GraphicsGrassDistance", 1.f},
      {"GraphicsGrassDensity", 1.f},
      {"GrassHeightDistance", 1.f},
      {"GraphicsBigBattleFix", 0.f},
      {"GraphicsSmallSoldiersAsSprites", 0.f},
      {"GraphicsSpriteSize", 20.f},
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
      {"GraphicsFacialAnimation", 8.f},
      {"GraphicsHandAnimation", 8.f},
      {"GraphicsUnitVariety", 4.f},
      {"GraphicsFootstepEffects", 4.f},
      {"GraphicsObjectDetail", 8.f},
      {"GraphicsBuildingDetail", 8.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsCampaignPlants", 4.f},
      {"GraphicsCampaignProps", 4.f},
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
      {"GraphicsFacialAnimation", 2.5f},
      {"GraphicsHandAnimation", 2.5f},
      {"GraphicsUnitVariety", 2.5f},
      {"GraphicsFootstepEffects", 2.5f},
      {"GraphicsObjectDetail", 1.5f},
      {"GraphicsBuildingDetail", 3.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsCampaignPlants", 4.f},
      {"GraphicsCampaignProps", 4.f},
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
          .tooltip = "Unit Detail 1.75x, Dead Unit Detail 0.60x, Unit, Facial and Hand Animation, Unit Variety and\n"
                     "Footstep Effects 2.50x, Model Detail 1.50x, Building Detail 3x, Campaign Tree, Plant and Prop\n"
                     "4x, Shadow Map Size 8192. The rest at its default.",
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
                                "How far soldiers move on their own (beyond it they copy their neighbours' moves).\n"
                                "Higher: heavier for the CPU.",
                                4.f, 0.1f),
      tw::engine_tweaks::Slider(&facial_animation, "GraphicsFacialAnimation", "Facial Animation Distance",
                                "How far soldiers' faces move. Higher: heavier for the CPU. Game: 10 m.", 8.f, 0.1f),
      tw::engine_tweaks::Slider(&hand_animation, "GraphicsHandAnimation", "Hand Animation Distance",
                                "How far soldiers' hands and fingers move. Higher: heavier for the CPU. Game: 10 m.", 8.f,
                                0.1f),
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
      tw::engine_tweaks::Slider(&campaign_trees, "GraphicsCampaignDetail", "Campaign Tree Detail Distance",
                                "Campaign map: trees keep their detail farther away.", 4.f),
      tw::engine_tweaks::Slider(&campaign_plants, "GraphicsCampaignPlants", "Campaign Plant Detail Distance",
                                "Campaign map: plants keep their detail farther away. From the next campaign load.", 4.f),
      tw::engine_tweaks::Slider(&campaign_props, "GraphicsCampaignProps", "Campaign Prop Distance",
                                "Campaign map: props are drawn farther away.", 4.f),
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
          .key = "GraphicsSmallSoldiersAsSprites",
          .binding = &small_soldiers_as_sprites,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Small Soldiers as Sprites (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "Soldiers turn into flat sprites once they are smaller on screen than Sprite Size, even within\n"
                     "Unit Detail's distance. Lighter for the CPU in big battles. Needs TAA, SMAA T2x, DLSS, FSR or XeSS.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "GraphicsSpriteSize",
          .binding = &sprite_size,
          .default_value = 20.f,
          .label = "Sprite Size",
          .section = "Graphics (Experimental)",
          .tooltip = "How tall soldiers are on screen when they turn into sprites. Higher: sprites nearer, lighter.",
          .min = 4.f,
          .max = 80.f,
          .format = "%.0f px",
          .is_visible = [] { return small_soldiers_as_sprites != 0.f; },
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
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "GrassHeightDistance",
      .binding = &shader_injection.grass_height_distance,
      .default_value = 1.f,
      .label = "Grass Height Distance (Experimental)",
      .section = twattila::engine_tests::GRASS,
      .tooltip = "Grass blades shrink and fade into the ground with distance, gone at 100 m (the game's).\n"
                 "Higher: full grass farther away.",
      .min = 0.25f,
      .max = 10.f,
      .format = "%.2fx",
  });
  // Attila's grass hooks (ScrubUpdate, its cell loop and quadtree query, DrawScrub to its no-LOD return: the same code here).
  for (auto* setting : {tw::engine_tweaks::Slider(&grass_distance, "GraphicsGrassDistance", "Grass Detail Distance",
                                                  "Battle: grass and plants keep their fuller versions farther away, and their cells reach\n"
                                                  "farther (the game: 200 m). Heavier for the CPU. How far grass shows: Grass Height Distance.",
                                                  4.f),
                        tw::engine_tweaks::Slider(&grass_density, "GraphicsGrassDensity", "Grass Density",
                                                  "Battle: more grass and plants (the game leaves some out, more of them farther away;\n"
                                                  "2x: all of them near the camera).",
                                                  8.f)}) {
    setting->section = twattila::engine_tests::GRASS;
    settings.push_back(setting);
  }
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::BUTTON,
      .label = "Grass Trace",
      .section = twattila::engine_tests::GRASS,
      .tooltip = "One frame of the grass into ReShade.log (in a battle, looking at where the grass ends).",
      .on_change = &twattila::graphics_hooks::TraceGrass,
  });
  twattila::engine_tests::Append(&settings, &engine_tests, SyncEarlyLoad);

  // Appended Settings (Ambient Occlusion, Global Illumination, Screen-Space Shadows, Anti-Aliasing)
  twattila::lighting_settings::Append(&settings, &shader_injection, [] { return rt_sliders.light_bounce != 0.f && twattila::feature_settings::Upscaling(aa_sliders); });
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);
  // The rays' AO in place of the game's and GT-VBAO (ssao00's rt_ambient_alone, as Attila's), the reflections' pass hooked.
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, true, true, twattila::feature_settings::AA_SECTION,
                                     &shader_injection.light_bounce_strength);
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
      .tooltip = "Rippling light on river, lake and shallow sea beds in battles. 0: off.",
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
    // No settings saved yet: a player's first start. On an SDR monitor it starts with the SDR preset (as Attila's).
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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/atotalwarsagathronesofbritannia/mods/1"); },
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

  // Graphics 1 of 2's layout (twattila/graphics_page.hpp, as Attila's and Rome II's), ATN's RT/PT preset as the defaults of
  // what the methods leave, then Settings Mode's pages. Light Through Foliage, both Smoke Shadows, Water Caustics and the
  // sprite soldiers' and far trees' ray-traced shadows Experimental (no run has shown them here); Light Through All Banners
  // off (no trace has drawn the unit banners). RT/PT sets them all as Attila's does (ATN 2026-11-14).
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  for (auto* setting : settings) {
    if (setting->key == "RayTracedSmokeShadows" || setting->key == "RayTracedSpriteShadows" || setting->key == "RayTracedFarTreeShadows"
        || setting->key == "WaterCaustics" || setting->key == "RayTracedBannerLight") {
      setting->default_value = 0.f;
    }
  }
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders, .sss = &sss_sliders, .rt = &rt_sliders, .ssao_mode = &shader_injection.ssao_mode,
                                             .light_bounce_strength = &shader_injection.light_bounce_strength,
                                             .ssr_hierarchical = &shader_injection.ssr_hierarchical,
                                             .smoke_strength = &smoke_map_sliders.strength, .smoke_softness = &smoke_map_sliders.softness,
                                             .lod_bias_trilinear = &lod_bias_trilinear_row, .rays_banner_light = &rays_banner_light_row,
                                             .rays_sheets_unselfed = &rt_sliders.sheets_unselfed,
                                             .engine_code = &engine_code_row,
                                             .foliage_light_tested = false,
                                             .smoke_map_tested = false,
                                             .smoke_shadows_tested = false,
                                             .far_cards_tested = false,
                                             .water_caustics_tested = false,
                                             .banner_light_tested = false,
                                             .rays_preset_as_attila = true});
  // Faster Game CPU Code (engine_code_row) and Water Caustics Experimental, off.
  for (auto* setting : settings) {
    if (setting->key != "FasterGameCpuCode" && setting->key != "WaterCaustics") continue;
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
  // The data run's switches (RENODX_TW_SET / SHOT / PROFILE; nothing hooked without them), timed from the first sun draw.
  twattila::test_hooks::Install(&settings, false);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::smoke_map::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::grass_fade::OnDestroyDevice);

  // Rows saved at an old default, cleared once (as Rome II's): Graphics 1 of 2 (SetRaysPresetDefaults) raised these, hidden
  // until what they act on is hooked: they show then at ATN's values. 2: the sliders split from Unit Animation and Campaign
  // Detail start at the saved value of the one they come from.
  constexpr int SETTINGS_REVISION = 2;
  if (int revision = 0; !reshade::get_config_value(nullptr, "renodx", "SettingsRevision", revision) || revision < SETTINGS_REVISION) {
    for (const char* preset : {"renodx-preset1", "renodx-preset2", "renodx-preset3"}) {
      if (revision < 1) {
        for (const char* key : {"RayTracedFarTreeShadows", "RayTracedLightImportance", "RayTracedPathKeptCube", "RayTracedSmokeQuality",
                                "RayTracedSpriteShadows", "RayTracedSwayRange"}) {
          reshade::set_config_value(nullptr, preset, key, static_cast<const char*>(nullptr));
        }
      }
      for (const auto& [from, to] : std::initializer_list<std::pair<const char*, const char*>>{
               {"GraphicsUnitAnimation", "GraphicsFacialAnimation"}, {"GraphicsUnitAnimation", "GraphicsHandAnimation"},
               {"GraphicsCampaignDetail", "GraphicsCampaignPlants"}, {"GraphicsCampaignDetail", "GraphicsCampaignProps"}}) {
        if (float value = 0.f, kept = 0.f; reshade::get_config_value(nullptr, preset, from, value)
                                           && !reshade::get_config_value(nullptr, preset, to, kept)) {
          reshade::set_config_value(nullptr, preset, to, value);
        }
      }
    }
    reshade::set_config_value(nullptr, "renodx", "SettingsRevision", SETTINGS_REVISION);
  }

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests (audio memory): written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(L"empire.retail.dll", &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // The tests' clock first: the Light Bounce's callback draws the sun draws itself (none after it runs).
  for (const auto& draw : LIGHT_BOUNCE_DRAWS) {
    renodx::utils::command_action::Register(
        OnSunDrawClock, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  // Before the Light Bounce's (its callback draws the sun draw again).
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace. lighting_VS_PS as Attila's (1B10DDD8's
  // RDEF; WARP sun test tools/tw/thrones/sun_test): the ambient cube at float 8 (c2-c7, the paths' sky), the sun colour at
  // float 4 (c1, x 0.004 with g_hdr_on c11.x: the paths' sun light).
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4);
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::InstallFoliage(FOLIAGE_VERTEX_SHADERS, AFTER_FOLIAGE_VERTEX_SHADERS);
  // Light Through Banners: cloth and sails, the unit banners with Light Through All Banners (as the rays), the G-buffer's
  // colour squared (gamma 2).
  twattila::screen_space_shadows::InstallThin(THIN_SHADERS, 2.f);
  twattila::water_effects::water_shaders = WATER_SHADERS;
  renodx::utils::pipeline::RegisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  twattila::grass_fade::Install(GRASS_FADE_SHADERS);
  // The frame trace (the data run): renodx_trace_now next to the exe traces the next frame, and FrameTrace's frame also at
  // 100% (twattila/frame_trace.hpp). Constants once each, b0-b5 up to 1 KB (the sun PS's camera b0, shadowmap b1,
  // lighting_VS_PS b2, shroud b3, directional_light b4): the SM5 sun draws (slm 1B10DDD8 / 21B916AF, skinlm 77586F30 /
  // 5E7DFFF2), the tone mapper 8BF6B657 and hdr_to_screen 55D276C9 (depth of field and distortion on), the SSAO resolve
  // BD389EF1, battlefog A6A0F887, the point light 6426B042, the water (battle ultra C9FBACCB, ocean F7E011A4, campaign
  // DE739661), the lit particles ACE33D5D, the units' VS (sm_4_1 25839CC5, sm_3_0 DC9FBA4A) and the campaign's shroud
  // plane 2860172D (tools/tw/thrones/plan_1114.md; names.tsv of the dump, none seen drawn yet). The units' and particles'
  // VS inputs at each draw, the particles' blend, instanced draws' counts apart.
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, RENDER_SCALE_VERTEX_SHADERS, FULL_DEPTH_SHADERS, twattila::view_share::WHOLE_TEXTURES,
                                   {
                                       .value_hashes = {0x1B10DDD8, 0x21B916AF, 0x77586F30, 0x5E7DFFF2, 0x8BF6B657, 0x55D276C9, 0xBD389EF1,
                                                        0xA6A0F887, 0x6426B042, 0xC9FBACCB, 0xF7E011A4, 0xDE739661, 0xACE33D5D, 0x25839CC5,
                                                        0xDC9FBA4A, 0x2860172D},
                                       .value_slots = 6,
                                       .value_floats = 256,
                                       .on_demand = true,
                                       .at_100 = true,
                                       .blend_hashes = {0xACE33D5D, 0xF5379F78},
                                       .vertex_input_hashes = {0x25839CC5, 0xFF473BE8, 0xDC9FBA4A, 0x76682C60, 0xA7F257B5},
                                       .instances_apart = true,
                                   });
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  // The ambient occlusion pass before ray tracing's (its result multiplied after), as Attila.
  twattila::ao_pass::Install(AO_DRAWS, &shader_injection, sizeof(shader_injection));
  twattila::rt_scene::Install(SCENE_SHADERS, AO_DRAWS);
  twattila::rt_scene::InstallCutOuts(0xF79BFB14, UNIT_CUT_OUT_PIXEL_SHADERS);  // rigidcommon ps30_shadow_alpha_test
  for (const uint32_t shader_hash : CAMPAIGN_SHADERS) {
    renodx::utils::command_action::Register(
        OnCampaignDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  // The point lights' ray-traced shadows (their light shaders replaced: shaders/pointlight*).
  twattila::rt_scene::InstallPointLights(twattila::rt_scene::POINT_LIGHT_SHADERS);
  // The battle fog's ray-traced shadows (its shader replaced: shaders/battlefog00).
  twattila::rt_scene::InstallFogShadows(twattila::rt_scene::FOG_SHADERS);
  // Smoke's ray-traced shadows and the smoke map (Attila's particle vertex shader and lit pixel shaders, the same bytes here).
  twattila::rt_scene::InstallSmokeShadows(twattila::rt_scene::PARTICLE_VERTEX_SHADERS, twattila::rt_scene::LIT_PARTICLE_PIXEL_SHADERS);
  // Ray-Traced Shadows Only: the sun's shadow map's casters skipped (the map the sun draws read); the forward passes that
  // read it take the rays' shadows instead.
  twattila::rt_scene::InstallShadowsOnly();
  twattila::screen_space_shadows::PatchForwardShadows(SHADOW_READER_SHADERS);
  twattila::rt_scene::InstallShadowReaders(SHADOW_READER_SHADERS);
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  // Last of the sun draws' hooks: it draws the sun draws itself (no callback after it runs).
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);
  twattila::screen_space_shadows::SetSmokeOcclusion(&twattila::smoke_map::Occlusion);
  // After the smoke's capture on the same draws (it sees the game's targets and camera).
  twattila::particle_layer::Install(twattila::rt_scene::PARTICLE_VERTEX_SHADER, PARTICLE_LAYER_READERS);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::particle_layer::OnDestroyDevice);

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
  renodx::utils::command_action::Unregister(OnCampaignDraw);
  renodx::utils::command_action::Unregister(OnSunDrawClock);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::smoke_map::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::grass_fade::OnDestroyDevice);
  renodx::utils::command_action::Unregister(twattila::water_effects::OnWaterDraw);
  renodx::utils::pipeline::UnregisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  twattila::grass_fade::Uninstall();
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
  twattila::screen_space_shadows::SetSmokeOcclusion(nullptr);
  twattila::smoke_map::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::particle_layer::OnDestroyDevice);
  twattila::particle_layer::Uninstall();
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
