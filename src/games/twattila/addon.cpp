/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../talesofarise_dlss/pixel_trace.hpp"
#include "../twrome2/engine_tweaks.hpp"
#include "./graphics_hooks.hpp"
#include "./anti_aliasing.hpp"
#include "./ao_pass.hpp"
#include "./device_loss.hpp"
#include "./engine_code.hpp"
#include "./engine_tests.hpp"
#include "./feature_settings.hpp"
#include "./credits.hpp"
#include "./grass_fade.hpp"
#include "./graphics_page.hpp"
#include "./psycho35_settings.hpp"
#include "./light_bounce.hpp"
#include "./lighting_settings.hpp"
#include "./particle_layer.hpp"
#include "./rt_scene.hpp"
#include "./screen_space_shadows.hpp"
#include "./test_hooks.hpp"
#include "./water_effects.hpp"
#include "./smoke_map.hpp"
#include "./ssr_hiz.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
// Settings Mode: 0 HDR & Effects, 1 Graphics 1 of 2 (lighting), 2 Graphics 2 of 2 (engine graphics).
float current_settings_mode = 0.f;

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsHdrOutput() {
  return shader_injection.swap_chain_encoding != 2.f;
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

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to Attila's SDR picture).
constexpr float HUE_CLIP_DEFAULT = 40.f;

bool IsModdedAutoExposureEnabled() {
  return shader_injection.auto_exposure_highlight_protection == 1.f;
}

// PsychoV30 has no V25-style highlight-only saturation control. Recommended
// saturation values therefore target V30's adaptation-relative LMS purity.
// rendering_mode: PsychoV35 (3) in all the HDR ones (ATN 2026-11-12; Reset too: no Extended mode here), every mode's
// sliders at their defaults.
// ATN: Highlight Saturation 0.70 and Hue Shift 0.04 in the HDR ones (their defaults; the SDR preset keeps 1 and 0.15),
// video AutoHDR 0.20 in the Recommended ones, off in Reset; Sky Debanding off (too heavy).
void ApplyHDRPreset(
    float rendering_mode,
    float highlights,
    float purity_scale,
    float bloom,
    float auto_exposure_highlight_protection,
    float auto_exposure_highlight_headroom_stops,
    float vfx_base_brightness,
    float vfx_weather,
    float vfx_snow,
    float hue_shift,
    float video_autohdr,
    float highlight_saturation = 0.7f,
    float highlight_hue_shift = 0.04f) {
  std::vector<std::pair<std::string, float>> values = {
      {"tonemapper", rendering_mode},
      {"gamma_correction", 1.f},
      {"highlights", highlights},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", purity_scale},
      {"cone_response_exponent", 1.f},
      {"hue_shift", hue_shift},
      {"bloom", bloom},
      {"lensflare", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"dof", 1.f},
      {"auto_exposure_highlight_protection", auto_exposure_highlight_protection},
      {"auto_exposure_highlight_headroom_stops", auto_exposure_highlight_headroom_stops},
      {"vfx_base_brightness", vfx_base_brightness},
      {"vfxweather", vfx_weather},
      {"vfxsnow", vfx_snow},
      {"some_indicators", 1.0f},
      {"v31_exposure", 1.f},
      {"v31_highlights", 1.f},
      {"v31_shadows", 1.f},
      {"v31_contrast", 1.f},
      {"v31_highlight_contrast", 1.f},
      {"v31_shadow_contrast", 1.f},
      {"v31_flare", 0.f},
      {"v31_saturation", 1.f},
      {"v31_dechroma", 0.f},
      {"v31_highlight_saturation", highlight_saturation},
      {"v31_roll_off", 1.f},
      {"v31_gamut_compression", 1.f},
      {"v31_gamut_compression_mode", 1.f},
      {"v31_highlight_hue_shift", highlight_hue_shift},
      {"v31_highlight_saturation_shift", 0.f},
      {"video_autohdr", video_autohdr},
      {"sky_deband", 0.f},
      {"sky_smooth", 0.f},
      {"sky_smooth_strength", 1.f},
      {"fog_strength", 1.f},
  };
  const auto v35 = twattila::psycho35::Defaults(HUE_CLIP_DEFAULT);
  values.insert(values.end(), v35.begin(), v35.end());
  renodx::utils::settings::UpdateSettings(values);
}

// SDR: Attila's own tonemapper and look (ATN 2026-11-01: this ran the HDR pipeline, PsychoV31, darker than the game);
// the game's and the UI's white are the monitor's own (no Paper White / UI nits to set), so the HDR boosts off.
void ApplySdrPreset() {
  ApplyHDRPreset(0.f, 1.f, 1.00f, 1.f, 0.f, 2.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f, 0.15f);
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
      UpgradeRTVReplaceShader(0x8BF6B657),
      UpgradeRTVReplaceShader(0x2C8169CA),
      UpgradeRTVReplaceShader(0xFEA9F9E2),
      UpgradeRTVReplaceShader(0xBA446301),
      UpgradeRTVReplaceShader(0xB7FBF3E2),
      UpgradeRTVReplaceShader(0xA2D88FCD),
      UpgradeRTVReplaceShader(0xCC79C76F),
      UpgradeRTVReplaceShader(0x836B7901),
      UpgradeRTVReplaceShader(0xF42FDDC6),
      UpgradeRTVReplaceShader(0x95398068),  // t09 constant tone mapper with bloom
      UpgradeRTVReplaceShader(0x6E20B0EF),  // t10 constant tone mapper
      __ALL_CUSTOM_SHADERS,
  };
  // The reflections' draws (ssr00, ssr01): the ray-traced reflections, else the depth pyramid (rt_scene.hpp, ssr_hiz.hpp).
  for (const uint32_t hash : {0x66006BEAu, 0x47D75349u}) {
    custom_shaders[hash].on_draw = twattila::rt_scene::OnReflectionDraw;
    custom_shaders[hash].on_drawn = twattila::rt_scene::OnReflectionDrawn;
  }

  // Lens flares 00-03 and the vignette: part of frame generation's HUD-less image.
  for (const uint32_t hash : {0x37FA1283u, 0x177AC333u, 0xF0153CB5u, 0x1125AA01u, 0x2C002375u}) {
    custom_shaders[hash].on_drawn = twattila::anti_aliasing::OnOutputEffectDrawn;
  }

  // Settings ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // PsychoV31 sliders: 0 - max, 1 = its own (PsychoV35's: twattila/psycho35_settings.hpp).
  const auto v31_slider = [](const std::string& key, float* binding, const std::string& label, const std::string& tooltip,
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
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "Use these in-game settings: Gamma 20 and Brightness 10.",
        .section = "General",
        .tint = 0xFFD23C,
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
        .key = "peak_white_nits",
        .binding = &shader_injection.peak_white_nits,
        .default_value = 400.f,
        .can_reset = false,
        .label = "Peak",
        .section = "General",
        .tooltip = "Maximum brightness output in nits.",
        .min = 400.f,
        .max = 10000.f,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
        .is_visible = IsHdrOutput,
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
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
        .is_visible = IsHdrOutput,
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
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
        .is_visible = IsHdrOutput,
    },
    new renodx::utils::settings::Setting{
        .key = "tonemapper",
        .binding = &shader_injection.hdr,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Rendering Mode",
        .section = "General",
        .tooltip = "SDR: Attila's original SDR tonemapper.\n"
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
        .label = "Reset to default",
        .group = "preset-line-1",
        .tooltip = "Resets all image tuning to HDR defaults while preserving Peak, Paper White, UI brightness, Settings Mode, and output encoding.",
        .on_change = []() {
          ApplyHDRPreset(3.f, 1.f, 1.00f, 1.f, 0.f, 2.f, 1.f, 1.f, 1.f, 1.f, 0.f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: no VFX boost",
        .group = "preset-line-1",
        .tooltip = "Resets all image tuning to HDR defaults while preserving Peak, Paper White, UI brightness, Settings Mode, and output encoding.",
        .on_change = []() {
          ApplyHDRPreset(3.f, 1.f, 1.00f, 0.35f, 0.f, 2.f, 1.f, 1.f, 1.f, 2.f, 0.2f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: All Conditions",
        .group = "preset-line-1",
        .tooltip = "Balanced recommendation intended to work across all weather and lighting conditions. Display calibration is preserved.",
        .on_change = []() {
          ApplyHDRPreset(3.f, 1.f, 1.00f, 0.1f, 1.f, 4.f, 2.3f, 1.0f, 1.5f, 2.f, 0.2f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: Day/Dry/Rain",
        .group = "preset-line-2",
        .tooltip = "Recommendation for daytime scenes and dry weather. Display calibration is preserved.",
        .on_change = []() {
          ApplyHDRPreset(3.f, 1.f, 1.00f, 0.1f, 1.f, 4.f, 4.4f, 2.f, 1.5f, 2.f, 0.2f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: Night/Snow/Fog",
        .group = "preset-line-2",
        .tooltip = "Recommendation for fog, snow, and nighttime scenes. Display calibration is preserved.",
        .on_change = []() {
          ApplyHDRPreset(3.f, 1.f, 1.00f, 0.1f, 1.f, 4.f, 1.8f, 0.7f, 1.5f, 2.f, 0.2f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "SDR (monitors without HDR)",
        .group = "preset-line-3",
        .tooltip = "For SDR monitors: Rendering Mode SDR (Attila's own picture) and Output SDR (restart the game if it was\n"
                   "HDR), no VFX boost, no video AutoHDR. The game's and the UI's white are your monitor's own white.",
        .tint = 0xFFC107,
        .on_change = []() {
          ApplySdrPreset();
          renodx::utils::settings::UpdateSettings({{"SwapChainEncoding", 2.f}});
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
    //     .key = "vignette",
    //     .binding = &shader_injection.vignette,
    //     .default_value = 1.f,
    //     .label = "Vignette",
    //     .section = "Extra",
    //     .tooltip = "Multiplier on vignette mask.",
    //     .min = 0.f,
    //     .max = 1.f,
    //     .format = "%.2f",
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
        .key = "purity_scale",
        .binding = &shader_injection.purity_scale,
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
        .tooltip = "Hue shift of bright highlights, like SDR: fire and explosions turn orange/yellow instead of pink/red.\n0 = none, 1 = PsychoV30 default, 2 = full.",
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
               1.f, 1.f),
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
        .is_enabled = [] { return IsPsychoV31Mode() && IsHdrOutput(); },
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
               "Saturation of bright colors. 0 = less, 1 = PsychoV31's own, 2 = more. Default 0.70.", 2.f, 0.7f),
    v31_slider("v31_highlight_hue_shift", &shader_injection.v31_highlight_hue_shift, "Highlight Hue Shift",
               "Bright colors take the SDR image's hues: fire more orange / yellow, less pink.\n"
               "0 = off, 1 = SDR hues. Default 0.04.",
               1.f, 0.04f),
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
        .key = "vignette",
        .binding = &shader_injection.vignette,
        .default_value = 1.f,
        .label = "Vignette",
        .section = "Extra",
        .tooltip = "Darker screen edges around sun flares (the game's lens vignette). 0 = off.",
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
        .is_enabled = [] { return IsHDRMode() && IsHdrOutput(); },
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
        .is_enabled = [] { return IsHDRMode() && IsHdrOutput() && shader_injection.video_autohdr > 0.f; },
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
        .default_value = 4.0f,
        .label = "Modded Auto Exposure: Sensitivity",
        .section = "Extra (Experimental)",
        .tooltip = "Maximum metered highlight above the frame average, in stops. Lower values protect exposure more strongly. Higher values are closer to stock.",
        .min = 1.0f,
        .max = 8.0f,
        .format = "%.0f",
        .is_enabled = IsModdedAutoExposureEnabled,
    },
    // VFX //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fire_brightness",
    //     .binding = &shader_injection.vfxfirebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Fire Brightness",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Fire, embers, sparks, and other additive emission.",
    //     .min = 0.0f,
    //     .max = 10.0f,
    //     .format = "%.0f",
    // },
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
        new renodx::utils::settings::Setting{
        .key = "vfxweather",
        .binding = &shader_injection.vfxweather,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness: Blood Splash & desert storm & Fog",
        .section = "Extra (Experimental)",
        .tooltip = "Brightness of blood splashes and the camera-following fog used by snowstorms and sandstorms. Recommmendation is max 0.5 in storm/fog.",
        .min = 0.01f,
        .max = 30.0f,
        .format = "%.2f",
    },
        new renodx::utils::settings::Setting{
        .key = "vfxsnow",
        .binding = &shader_injection.vfxsnow,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness: Snowflakes, Rain & Fire Sparks",
        .section = "Extra (Experimental)",
        .tooltip = "Brightness of snow, rain, fire sparks and probably more. Recommmendation is max 5.",
        .min = 0.01f,
        .max = 30.0f,
        .format = "%.1f",
    },
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
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fog_brightness",
    //     .binding = &shader_injection.vfxfogbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Fog Brightness",
    //     .section = "VFX (Requires Shadow & Partial Max Quality)",
    //     .tooltip = "Brightness of the fog color applied to particles.",
    //     .min = 0.0f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    // },
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
    new renodx::utils::settings::Setting{
        .key = "VfxHalfResolution",
        .binding = &twattila::particle_layer::half_resolution_row,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Particle Resolution",
        .section = "Extra (Experimental)",
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
        .section = "Extra (Experimental)",
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
        .section = "Extra (Experimental)",
        .tooltip = "Half-size smoke, fire and dust keep clean edges against units and flicker less.\n"
                   "Small cost.",
        .labels = {"Off", "On"},
        .is_enabled = [] { return twattila::particle_layer::half_resolution_row != 0.f; },
    },
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
  };
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));
}

// Graphics (Experimental): the sliders, through the LOD pick hook (object detail), Attila's unit hook
// (graphics_hooks.hpp) and engine tweaks (filled in ConfigureAddon).
std::array<tw::engine_tweaks::Option, 4> unit_detail;  // foot soldiers, cavalry, elephants, artillery
float big_battle_fix = 0.f;
float small_soldiers_as_sprites = 0.f;
float sprite_size = 20.f;  // pixels on screen
float ssr_denoiser = 1.f;  // ssr_hiz.hpp: the resolve's history
float shadow_map_size = 0.f;  // the game's sizes x 2^this (graphics_hooks.hpp shadow_map)
twattila::feature_settings::SssSliders sss_sliders;
twattila::smoke_map::Sliders smoke_map_sliders;
twattila::feature_settings::AaSliders aa_sliders;
// Ray tracing's (twattila/rt_scene.hpp).
twattila::rt_scene::Sliders rt_sliders;
tw::engine_tweaks::Option dead_units;
tw::engine_tweaks::Option unit_animation;  // shared pose
tw::engine_tweaks::Option facial_animation;
tw::engine_tweaks::Option hand_animation;
tw::engine_tweaks::Option unit_variety;
tw::engine_tweaks::Option footstep_effects;
tw::engine_tweaks::Option object_detail;
tw::engine_tweaks::Option campaign_trees;
tw::engine_tweaks::Option grass_distance;  // graphics_hooks' scrub hooks
tw::engine_tweaks::Option grass_density;
tw::engine_tweaks::Option campaign_plants;
tw::engine_tweaks::Option campaign_props;
tw::engine_tweaks::Option building_detail;
std::vector<twattila::engine_tests::EngineTest> engine_tests;
// Every engine option above, then the tests' (their addresses fixed once engine_tests is filled).
std::vector<tw::engine_tweaks::Option*> graphics_options;
// The pixel trace's position, % of the screen's width and height.
std::array<float, 2> traced_pixel = {50.f, 50.f};
HMODULE addon_module_handle = nullptr;
// The at-start tests' thread (AttachAddon): done once they are written (or it gave up), stopped at the addon's detach.
std::atomic<bool> at_start_done = false;
std::atomic<bool> at_start_stop = false;

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
bool graphics_found = false;
bool graphics_hooks_installed = false;
// ReShade.ini [Attila] DisableGraphics=1: Shadow Map Size locked at the game's: the UI 4K mod changes the same engine code
// (ATN: nothing else needs locking). On without the key (ATN: the UI 4K mod is common).
bool shadow_size_locked = true;
int ui_mod_compatibility = 0;  // its slider: written to ReShade.ini, used from the next start


// Ambient Occlusion Visibility Bitmask (twattila/ao_pass.hpp): the addon's own shader in place of the game's AO
// pass (whose replacement is the game's code only).
constexpr std::array<twattila::ao_pass::AmbientOcclusionDraw, 1> AO_DRAWS = {{
    {.shader_hash = 0xBD389EF1, .bitmask = __ao_bitmask, .upsample = __ao_upsample, .resolve = __ao_resolve,
     .denoise = __ao_denoise},  // ssao00
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl ps30 slm / slm_reflection (standard) and
// skinlm, every shadow method, with the camera at b0 and the G-buffer at t0-t3.
// The game's passes that read render-size textures at the display's size (texture slots), below 100%, and the
// half-size passes drawn whole (their readers: view_share::WHOLE_TEXTURES).
constexpr std::array<twattila::view_share::RegionRead, 6> RENDER_SCALE_READS = {{
    {.shader_hash = 0x781681E4, .slots = 0b1},   // luma / bloom downsample
    {.shader_hash = 0xB386A8BC, .slots = 0b11},  // shadow depth analysis
    {.shader_hash = 0xB4026AF2, .slots = 0b11, .whole = true},  // AO blur_x
    {.shader_hash = 0x6203C824, .slots = 0b10, .whole = true},  // AO blur_y
    {.shader_hash = 0x820D451B, .slots = 0b11, .whole = true},  // water refraction (3 channels)
    {.shader_hash = 0x25944B2D, .slots = 0b1, .whole = true},   // water refraction (4 channels)
}};
// Particles at Half Resolution (twattila/particle_layer.hpp): the light buffer's readers after the particles, where their layer
// goes in: the luma / bloom downsample (the first in the traced frame), else the tone mappers (BuildRuntimeData's replaced ones).
constexpr std::array<uint32_t, 12> PARTICLE_LAYER_READERS = {0x781681E4, 0x8BF6B657, 0x2C8169CA, 0xFEA9F9E2, 0xBA446301, 0xB7FBF3E2,
                                                             0xA2D88FCD, 0xCC79C76F, 0x836B7901, 0xF42FDDC6, 0x95398068, 0x6E20B0EF};
// The game's vertex shaders the addon replaces with ones that read the render share below 100% (cb12).
constexpr std::array<uint32_t, 1> RENDER_SCALE_VERTEX_SHADERS = {
    0x7C04E880,  // lens flare: the sun's occlusion counted at the render size (lensflare04)
};
// The game's 3D sprites (sprite.hlsl vertex_3d*: the unit markers) drawn against the scene's depth after the tone
// mapper: the depth stretched to the display's size for them below 100%.
constexpr std::array<uint32_t, 4> FULL_DEPTH_SHADERS = {0x2823783C, 0xF1CA8DC6, 0x59A0821D, 0x23C7B5C0};

constexpr std::array<twattila::light_bounce::SunDraw, 50> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0x4816852C, .standard = true},
    {.shader_hash = 0x73310E1D, .standard = true},
    {.shader_hash = 0x837C91EB, .standard = true, .lighting_slot = 1},
    {.shader_hash = 0x95A706E4, .standard = true},
    {.shader_hash = 0x9A79AB20, .standard = true},
    {.shader_hash = 0x9ECED7F8, .standard = true, .lighting_slot = 1},
    {.shader_hash = 0xB01BD600, .standard = true},
    {.shader_hash = 0xB6DA03C5, .standard = true},
    {.shader_hash = 0xC64DB947, .standard = true},
    {.shader_hash = 0xCA3AAE00, .standard = true, .lighting_slot = 1},
    {.shader_hash = 0xDCD5E5BA, .standard = true, .lighting_slot = 1},
    {.shader_hash = 0xE7BAB48E, .standard = true},
    {.shader_hash = 0xFAD02810, .standard = true},
    {.shader_hash = 0xFE8DFAA1, .standard = true},
    {.shader_hash = 0x00D1F51E},
    {.shader_hash = 0x085807B8},
    {.shader_hash = 0x23E4B7D8},
    {.shader_hash = 0x39BCCAB9},
    {.shader_hash = 0x592DA747, .lighting_slot = 1},
    {.shader_hash = 0x6202730B},
    {.shader_hash = 0x94E77C68},
    {.shader_hash = 0xBA04D236},
    {.shader_hash = 0xCC911898},
    {.shader_hash = 0xD40229F1, .lighting_slot = 1},
    {.shader_hash = 0xD620D623},
    {.shader_hash = 0xDF104626},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x32539E8C, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0x54C48CB4, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0x60D325D6, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0x66A4E910, .standard = true, .gbuffer_slot = 4, .camera_slot = 0, .lighting_slot = 1},  // slm_highdef
    {.shader_hash = 0x7B036C94, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0xB3D8781B, .standard = true, .gbuffer_slot = 4, .camera_slot = 0, .lighting_slot = 1},  // slm_lowdef
    {.shader_hash = 0xC7E46DDE, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0xCE8427DD, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0xCF85CE09, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0xDFCD4A25, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0xE86B51A4, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_lowdef
    {.shader_hash = 0xEBFFED86, .standard = true, .gbuffer_slot = 5, .camera_slot = 0},  // slm_highdef
    {.shader_hash = 0x0969A752, .gbuffer_slot = 1, .camera_slot = 0, .lighting_slot = 1},  // skinlm_highdef
    {.shader_hash = 0x0C1E6AE3, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0x1B03A9FA, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0x2E104183, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
    {.shader_hash = 0x44519729, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0x52252829, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
    {.shader_hash = 0x8414658A, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
    {.shader_hash = 0x8D6F220D, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
    {.shader_hash = 0xB0B816EB, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0xB5D97BD8, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_lowdef
    {.shader_hash = 0xB8693711, .gbuffer_slot = 1, .camera_slot = 0, .lighting_slot = 1},  // skinlm_lowdef
    {.shader_hash = 0xCE41C3F9, .gbuffer_slot = 2, .camera_slot = 0},  // skinlm_highdef
}};

// DLAA's object motion (twattila/anti_aliasing.hpp): the swaying main-pass vertex shaders (grass, leaves, branches at
// shader models 3, 4.1 and 5; the grass's are the addon's own grass00 / grass01; the old ocean's waves), the units'
// (weightedcommon.hlsl vs30_main / main_4_bones, with or without dismemberment, shader models 3 and 4.1), the rigid
// objects' (rigidcommon at shader models 3 and 4.1, textureblend with and without wind), the deformed meshes' (cloth,
// sails, sail symbols, ropes) and the ocean's (ocean2: deep, shallow, coast; their displacement textures' slots), and
// their motion versions (rigid: and records; deformed: and captures).
using twattila::anti_aliasing::MotionKind;
constexpr std::array<twattila::anti_aliasing::MotionShader, 32> MOTION_SHADERS = {{
    {.hash = 0xF3741248, .motion_shader = __motion_grass00, .draw_shader = __motion_draw_grass00},
    {.hash = 0x01FA9CDE, .motion_shader = __motion_grass01, .draw_shader = __motion_draw_grass01},
    {.hash = 0x46C616F0, .motion_shader = __motion_leaf_sm3, .draw_shader = __motion_draw_leaf_sm3},
    {.hash = 0x5B880546, .motion_shader = __motion_leaf_sm41, .draw_shader = __motion_draw_leaf_sm41},
    {.hash = 0xCABCB9D1, .motion_shader = __motion_leaf_sm5, .draw_shader = __motion_draw_leaf_sm5},
    {.hash = 0x77B993FD, .motion_shader = __motion_branch_sm3, .draw_shader = __motion_draw_branch_sm3},
    {.hash = 0xB8C3E83D, .motion_shader = __motion_branch_sm41, .draw_shader = __motion_draw_branch_sm41},
    {.hash = 0x6EEF4062, .motion_shader = __motion_branch_sm5, .draw_shader = __motion_draw_branch_sm5},
    {.hash = 0x5F9CD405, .motion_shader = __motion_unit_sm3, .kind = MotionKind::UNIT, .record_shader = __motion_unit_point, .draw_shader = __motion_draw_unit_sm3},
    {.hash = 0x0ECFE74C, .motion_shader = __motion_unit_4_bones_sm3, .kind = MotionKind::UNIT, .record_shader = __motion_unit_point_4_bones, .draw_shader = __motion_draw_unit_4_bones_sm3},
    {.hash = 0xDAC80ECC, .motion_shader = __motion_unit_dismember_sm3, .kind = MotionKind::UNIT, .record_shader = __motion_unit_point, .draw_shader = __motion_draw_unit_dismember_sm3},
    {.hash = 0xC2AA8FDA, .motion_shader = __motion_unit_4_bones_dismember_sm3, .kind = MotionKind::UNIT, .record_shader = __motion_unit_point_4_bones, .draw_shader = __motion_draw_unit_4_bones_dismember_sm3},
    {.hash = 0xA691CFF4, .motion_shader = __motion_unit_sm41, .kind = MotionKind::UNIT, .record_shader = __motion_unit_point, .draw_shader = __motion_draw_unit_sm41},
    {.hash = 0x372628AB, .motion_shader = __motion_unit_4_bones_sm41, .kind = MotionKind::UNIT, .record_shader = __motion_unit_point_4_bones, .draw_shader = __motion_draw_unit_4_bones_sm41},
    {.hash = 0xEA1AEB0E, .motion_shader = __motion_unit_dismember_sm41, .kind = MotionKind::UNIT, .record_shader = __motion_unit_point, .draw_shader = __motion_draw_unit_dismember_sm41},
    {.hash = 0x31B66A15, .motion_shader = __motion_unit_4_bones_dismember_sm41, .kind = MotionKind::UNIT, .record_shader = __motion_unit_point_4_bones, .draw_shader = __motion_draw_unit_4_bones_dismember_sm41},
    {.hash = 0x347F3DD9,
     .motion_shader = __motion_rigid_sm3,
     .kind = MotionKind::RIGID,
     .record_shader = __motion_record_rigid_sm3,
     .draw_shader = __motion_draw_rigid_sm3},
    {.hash = 0xD830AD9B,
     .motion_shader = __motion_rigid_sm41,
     .kind = MotionKind::RIGID,
     .record_shader = __motion_record_rigid_sm41,
     .draw_shader = __motion_draw_rigid_sm41},
    {.hash = 0xB06CB969,
     .motion_shader = __motion_rigid_blend_sm3,
     .kind = MotionKind::RIGID,
     .record_shader = __motion_record_rigid_blend_sm3,
     .draw_shader = __motion_draw_rigid_blend_sm3},
    {.hash = 0x10FE7EBC,
     .motion_shader = __motion_rigid_blend_wind_sm3,
     .kind = MotionKind::RIGID,
     .record_shader = __motion_record_rigid_blend_wind_sm3,
     .draw_shader = __motion_draw_rigid_blend_wind_sm3},
    {.hash = 0xAB20418F, .motion_shader = __motion_ocean, .draw_shader = __motion_draw_ocean},
    {.hash = 0x772B26FF, .motion_shader = __motion_cloth_sm3, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_cloth_sm3, .draw_shader = __motion_draw_cloth_sm3},
    {.hash = 0x902253FD, .motion_shader = __motion_cloth_sm41, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_cloth_sm41, .draw_shader = __motion_draw_cloth_sm41},
    {.hash = 0xD11B5EC7, .motion_shader = __motion_sail_sm3, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sm3, .draw_shader = __motion_draw_sail_sm3},
    {.hash = 0x202FA188, .motion_shader = __motion_sail_sm41, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sm41, .draw_shader = __motion_draw_sail_sm41},
    {.hash = 0xB2CCA764,
     .motion_shader = __motion_sail_symbol_sm3,
     .kind = MotionKind::DEFORMED,
     .record_shader = __motion_capture_sail_symbol_sm3,
     .draw_shader = __motion_draw_sail_symbol_sm3},
    {.hash = 0xE9C34FAB,
     .motion_shader = __motion_sail_symbol_sm41,
     .kind = MotionKind::DEFORMED,
     .record_shader = __motion_capture_sail_symbol_sm41,
     .draw_shader = __motion_draw_sail_symbol_sm41},
    {.hash = 0x8D0D26B9, .motion_shader = __motion_rope_sm3, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope_sm3, .draw_shader = __motion_draw_rope_sm3},
    {.hash = 0x9EECBA5D, .motion_shader = __motion_ocean_deep, .kind = MotionKind::HISTORY, .history_textures = 0b10, .draw_shader = __motion_draw_ocean_deep},
    {.hash = 0x97101A59, .motion_shader = __motion_ocean_shallow_sm3, .kind = MotionKind::HISTORY, .history_textures = 0b1010, .draw_shader = __motion_draw_ocean_shallow_sm3},
    {.hash = 0xE0671FA4, .motion_shader = __motion_ocean_shallow_sm4, .kind = MotionKind::HISTORY, .history_textures = 0b1010, .draw_shader = __motion_draw_ocean_shallow_sm4},
    {.hash = 0xF64EE8CB, .motion_shader = __motion_ocean_coast, .kind = MotionKind::HISTORY, .history_textures = 0b101010, .draw_shader = __motion_draw_ocean_coast},
}};

// Ray tracing's scene (twattila/rt_scene.hpp): the main view's terrain (rigidterrain geom / custom geom, shader models 3 and
// 4.1, old and new), rigid objects and tree branches, and their capture versions (world positions).
// Light Through All Banners (Experimental): the unit banners (rigidtextureblend and its wind version: the 3D UI's army_banner_flag
// models' material) and the flags the CPU moves (rigidcommon from a dynamic vertex buffer, drawn from both sides or by the
// capture-point flag's pixel shaders) as thin cloth.
float rays_banner_light_row = 0.f;
// The capture-point flag: rigidstandard's ps30_gbuffer_alpha_test with vs 347F3DD9 (a pixel trace and names.tsv: level A), its
// ps41_gbuffer_alpha_test_ms twin with vs D830AD9B under MSAA (the shader dump only: level B). The game culls its back faces.
constexpr std::array<uint32_t, 1> FLAG_PIXEL_SHADERS = {0xED1DF335};
constexpr std::array<uint32_t, 1> FLAG_PIXEL_SHADERS_MS = {0xCE6F17A1};
// Texture LOD Bias on All Textures (Experimental, graphics_page.hpp; Warhammer III's row): the game's wrapping trilinear
// samplers biased too (texture_lod_bias.hpp SetWrappingTrilinear).
float lod_bias_trilinear_row = 0.f;
// Faster Game CPU Code (graphics_page.hpp, twattila/engine_code.hpp): on by default, every routine proven to give the game's
// results (test/tw_engine_code).
float engine_code_row = 1.f;
// Torch and Fire Light / Shadows from Path Tracing (Experimental, graphics_page.hpp; helper111): the path-traced Light
// Bounce's unified reservoir lights the torches (rt_core.hpp Settings::lights_in_bounce) or gives their shadows' ratio
// (light_ratio).
float lights_in_bounce_row = 0.f;
float light_ratio_row = 0.f;

constexpr std::array<twattila::rt_scene::SceneShader, 38> SCENE_SHADERS = {{
    {.hash = 0x51FCE4CC, .capture_shader = __rt_capture_terrain_sm3, .terrain = true},
    {.hash = 0x2FF76986, .capture_shader = __rt_capture_terrain_sm41, .terrain = true},
    {.hash = 0x6ECB6C09, .capture_shader = __rt_capture_terrain_old_sm41, .terrain = true},
    {.hash = 0x28399869, .capture_shader = __rt_capture_terrain_old_sm3, .terrain = true},
    {.hash = 0x6A0D0455, .capture_shader = __rt_capture_terrain_custom_material_sm3, .terrain = true},
    {.hash = 0xD8DA0B13, .capture_shader = __rt_capture_terrain_custom_material_sm41, .terrain = true},
    {.hash = 0x829CDC4F, .capture_shader = __rt_capture_terrain_custom_sm41, .terrain = true},
    {.hash = 0xF1C6020E, .capture_shader = __rt_capture_terrain_custom_sm3, .terrain = true},
    {.hash = 0x347F3DD9, .capture_shader = __rt_capture_rigid_sm3, .rows_shader = __motion_record_rigid_sm3, .cut_out_shader = __rt_capture_rigid_cut_out_sm3, .thin_sheets_row = &rays_banner_light_row,
     .thin_sheets_pixel_shaders = FLAG_PIXEL_SHADERS},
    {.hash = 0xD830AD9B, .capture_shader = __rt_capture_rigid_sm41, .rows_shader = __motion_record_rigid_sm41, .cut_out_shader = __rt_capture_rigid_cut_out_sm41, .thin_sheets_row = &rays_banner_light_row,
     .thin_sheets_pixel_shaders = FLAG_PIXEL_SHADERS_MS},
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
    {.hash = 0x5F9CD405, .capture_shader = __rt_capture_unit_sm3, .cut_out_shader = __rt_capture_unit_cut_out_sm3, .dynamic = true},
    {.hash = 0x0ECFE74C, .capture_shader = __rt_capture_unit_4_bones_sm3, .cut_out_shader = __rt_capture_unit_4_bones_cut_out_sm3, .dynamic = true},
    {.hash = 0xDAC80ECC, .capture_shader = __rt_capture_unit_dismember_sm3, .cut_out_shader = __rt_capture_unit_dismember_cut_out_sm3, .dynamic = true},
    {.hash = 0xC2AA8FDA, .capture_shader = __rt_capture_unit_4_bones_dismember_sm3, .cut_out_shader = __rt_capture_unit_4_bones_dismember_cut_out_sm3, .dynamic = true},
    {.hash = 0xA691CFF4, .capture_shader = __rt_capture_unit_sm41, .cut_out_shader = __rt_capture_unit_cut_out_sm41, .dynamic = true},
    {.hash = 0x372628AB, .capture_shader = __rt_capture_unit_4_bones_sm41, .cut_out_shader = __rt_capture_unit_4_bones_cut_out_sm41, .dynamic = true},
    {.hash = 0xEA1AEB0E, .capture_shader = __rt_capture_unit_dismember_sm41, .cut_out_shader = __rt_capture_unit_dismember_cut_out_sm41, .dynamic = true},
    {.hash = 0x31B66A15, .capture_shader = __rt_capture_unit_4_bones_dismember_sm41, .cut_out_shader = __rt_capture_unit_4_bones_dismember_cut_out_sm41, .dynamic = true},
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
    // Far trees' billboards: the same, each showing the tree from the sun.
    {.hash = 0x4D50E1C7, .capture_shader = __rt_capture_billboard, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 1},
    // The campaign's plants and forests, and its trees: placed like the battle's, no wind.
    {.hash = 0x924D7813, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true},
    {.hash = 0x819A414C, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true},
    {.hash = 0x4657DA8A, .capture_shader = __rt_capture_campaign_tree, .rows_shader = __rt_rows_campaign_tree, .foliage = true, .still = true},
}};

// The screen-space shadows' Light Through Banners: cloth and sails, the unit banners with Light Through All Banners (as the rays).
constexpr std::array<twattila::screen_space_shadows::ThinShader, 6> THIN_SHADERS = {{
    {.hash = 0x772B26FF},
    {.hash = 0x902253FD},
    {.hash = 0xD11B5EC7},
    {.hash = 0x202FA188},
    {.hash = 0xB06CB969, .row = &rays_banner_light_row},
    {.hash = 0x10FE7EBC, .row = &rays_banner_light_row},
}};

// What only the campaign map draws: its borders, the shroud's plane, its sea.
constexpr std::array<uint32_t, 4> CAMPAIGN_SHADERS = {0x0CC96E53, 0xC1B5C13E, twattila::rt_scene::CAMPAIGN_SEA_SHADERS[0], twattila::rt_scene::CAMPAIGN_SEA_SHADERS[1]};
constexpr auto OnCampaignDraw = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  twattila::frame_history::campaign_frames.store(2, std::memory_order_relaxed);
  return {};
};

// Light Through Foliage: grass and leaves (SCENE_SHADERS), and the rigid objects drawn after them.
constexpr std::array<uint32_t, 5> FOLIAGE_VERTEX_SHADERS = {0x46C616F0, 0x5B880546, 0xCABCB9D1, 0xF3741248, 0x01FA9CDE};
constexpr std::array<uint32_t, 4> AFTER_FOLIAGE_VERTEX_SHADERS = {0x347F3DD9, 0xD830AD9B, 0xB06CB969, 0x10FE7EBC};

bool configured = false;
bool attached = false;
bool addon_registered = false;

// The units' G-buffer pixel shaders that clip at t0's alpha (weighted ps30_gbuffer without noclip; names.tsv), sorted.
constexpr std::array<uint32_t, 92> UNIT_CUT_OUT_PIXEL_SHADERS = {
    0x015AD59B, 0x04451F97, 0x078FE2F8, 0x092FAE7D, 0x0C6FF9DE, 0x0CB448B3, 0x0E00C067, 0x1124A46A, 0x11CD57DD, 0x1259BC90,
    0x20C7AB1F, 0x220C5180, 0x239963D1, 0x24C09B83, 0x28C3EAD4, 0x29482F73, 0x2B7D2720, 0x2C3EA513, 0x2D4B1E78, 0x2F59B3DA,
    0x3782B858, 0x3BCC202B, 0x3BD3FC99, 0x3D7ED953, 0x3EA5229E, 0x40593CDB, 0x4918D429, 0x4E9EDE79, 0x58BCF4AE, 0x59F3A5EA,
    0x5B7A9F80, 0x5C18A800, 0x5FEC953E, 0x61CD6092, 0x6EC2D95F, 0x742B695C, 0x7AC17F0A, 0x7B01A295, 0x7D6AFE41, 0x82FF1DB5,
    0x8524F338, 0x879A8AFD, 0x8A031046, 0x8A1DBDD5, 0x8C501D38, 0x8D8F67DE, 0x8FCC1EE1, 0x920E9860, 0x93C95E3C, 0x940D83F4,
    0x95BD2B3D, 0x9A0630CE, 0x9C38C3D8, 0x9D2D0AFB, 0x9D5903B6, 0x9EF52CAC, 0xA01EEF45, 0xA482BA91, 0xA4C87797, 0xB866973E,
    0xBA2A6179, 0xBB381C44, 0xBE51C839, 0xC1822803, 0xC3057069, 0xC3144AD4, 0xC3728871, 0xC5E213AC, 0xC64A7A31, 0xC85DD331,
    0xCACD9B8E, 0xD2C68515, 0xD5668711, 0xD5DC3322, 0xDB17E286, 0xDCC30AB7, 0xDD85EE06, 0xDEBD1020, 0xE0D9788D, 0xE39EBF7B,
    0xE5A5F801, 0xE5A7FEE6, 0xE7AE71FE, 0xE91382B4, 0xEBD63231, 0xED40D015, 0xEDBF9EC6, 0xEDCD6CC3, 0xF73222D6, 0xFBD501F7,
    0xFF1C02CC, 0xFF9A9D05,
};

// The campaign's rivers (1B25BD46) are no water in the view: painted into a map overlay, nothing under them to see.
constexpr std::array<twattila::water_effects::WaterShader, 15> WATER_SHADERS = {{
    {.hash = 0x2E9F5935, .lighting_slot = 2, .world_input = 1, .refraction_slot = 2},  // battle river
    {.hash = 0xC9FBACCB, .lighting_slot = 2, .world_input = 3, .refraction_slot = 2},  // battle plane, Water Quality Ultra
    {.hash = 0x2E69BF8E, .lighting_slot = 1, .world_input = 3, .refraction_slot = 2},  // placed water meshes; the plane below Ultra
    {.hash = 0x2BD86A89, .lighting_slot = 2, .world_input = 2, .refraction_slot = 5},  // battle sea, high / ultra
    {.hash = 0x108F6C71, .lighting_slot = 1, .world_input = 2, .refraction_slot = 5},  // battle sea, medium
    {.hash = 0x301DB1B5, .lighting_slot = 1, .world_input = 2, .refraction_slot = 5},  // battle sea, low
    {.hash = 0x05A111E1, .lighting_slot = 2, .world_input = 3, .refraction_slot = 6, .caustics = false},  // campaign sea
    {.hash = 0x450F6992, .lighting_slot = 1, .world_input = 3, .refraction_slot = 6, .caustics = false},  // campaign sea, low
    {.hash = 0x197D701E, .lighting_slot = 1, .world_input = 3, .refraction_slot = 6, .caustics = false},  // campaign lake
    // Without shadows.
    {.hash = 0x52228234, .lighting_slot = 1, .world_input = 1, .refraction_slot = 2},
    {.hash = 0x5CB9A865, .lighting_slot = 1, .world_input = 1, .refraction_slot = 2},
    {.hash = 0x6583F5D8, .lighting_slot = 1, .world_input = 2, .refraction_slot = 5},
    {.hash = 0x33758F15, .lighting_slot = 1, .world_input = 2, .refraction_slot = 5},
    {.hash = 0xE928D4B8, .lighting_slot = 1, .world_input = 3, .refraction_slot = 6, .caustics = false},
    {.hash = 0xA140EFBA, .lighting_slot = 1, .world_input = 3, .refraction_slot = 6, .caustics = false},
}};

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
  if (shadow_size_locked) return;
  twattila::graphics_hooks::shadow_map::Install(std::exp2(shadow_map_size));
}

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  talesofarise::pixel_trace::OnPresent(queue);
  if (auto& campaign_frames = twattila::frame_history::campaign_frames; campaign_frames.load(std::memory_order_relaxed) != 0) {
    campaign_frames.fetch_sub(1, std::memory_order_relaxed);
  }
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twattila::particle_layer::Update(swapchain);
  // Before any setting is read this frame: DLSS never starts on another vendor's GPU, a saved DLSS or a preset's is XeSS
  // on Intel's, else FSR.
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
  rt_settings.lights_in_bounce = lights_in_bounce_row != 0.f;
  rt_settings.light_ratio = light_ratio_row != 0.f;
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
  twattila::grass_fade::Update(shader_injection.grass_height_distance);
  twattila::rt_scene::Update(rt_settings);
  // Ray-traced or path-traced ambient occlusion in place of the game's and GT-VBAO (theirs not drawn: their cost saved;
  // under Path-Traced Ambient they stacked on it, ATN's debug29).
  shader_injection.rt_ambient_alone = (rt_settings.ambient_occlusion || rt_settings.path_ambient) ? 1.f : 0.f;
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  shader_injection.ssao_scale = twattila::ao_pass::ResolutionScale(shader_injection.ssao_resolution);
  shader_injection.ssao_frame =
      twattila::ao_pass::Update(shader_injection.ssao_mode >= 1.f && shader_injection.rt_ambient_alone == 0.f,
                                shader_injection.ssao_temporal != 0.f,
                                shader_injection.ssao_scale, shader_injection.ssao_denoise != 0.f);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  twattila::engine_code::Update(engine_code_row != 0.f);
  tw::engine_tweaks::Update(L"empire.retail.dll", graphics_options, &object_detail, &graphics_found, graphics_status);
  if (!shadow_size_locked) twattila::graphics_hooks::shadow_map::Set(std::exp2(shadow_map_size));
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
          // Under the rays' shadows the sun's shadow map draws nothing: the game drops the 3D shadows its unit shadow
          // distance rule lets go, as long as its casters are skipped (the game's own back at once when they aren't).
          .unit_shadows = (twattila::rt_scene::CastersSkipped() ? 0.f : 1.f),
          .big_battle_fix = big_battle_fix != 0.f,
          .sprite_pixels = (small_soldiers_as_sprites != 0.f ? sprite_size : 0.f),
          .focal_pixels = twattila::anti_aliasing::MainFocalPixels(),
      },
      &graphics_hooks_installed, graphics_status);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: Attila by ATN";  // TODO: change me!

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
      // Battle buildings' LOD distances (100, 200, 300, 600 m; x their size / 20 with
      // battle_building_enable_lod_size_scale), copied into a building model's LODs when it loads (+0x17fc1c1).
      {.name = "battle_building_lod0_distance"},
      {.name = "battle_building_lod1_distance"},
      {.name = "battle_building_lod2_distance"},
      {.name = "battle_building_lod3_distance"},
  };
  // Campaign trees: the tree LOD hook (graphics_hooks.hpp, live) and the rigid tree LOD distance (100 m, read as 1 /
  // it, live). Plants (model type 0x6f) take these LOD distances when their models load (the model setup +0x17fbd00):
  // 60 m. Props: 60 m x 0.25-1 by quality, live.
  campaign_trees.tweaks = {{.name = "rigid_campaign_tree_lod"}};
  campaign_plants.tweaks = {
      {.name = "campaign_grass_lod0_distance"},
      {.name = "campaign_grass_lod1_distance"},
      {.name = "campaign_grass_lod2_distance"},
      {.name = "campaign_grass_lod3_distance"},
      {.name = "campaign_grass_lod4_distance"},
  };
  campaign_props.tweaks = {{.name = "campaign_props_max_distance"}};

  // Graphics 2 of 2's tests (twattila/engine_tests.hpp).
  engine_tests = twattila::engine_tests::Attila();
  graphics_options = {&object_detail, &building_detail, &campaign_trees, &campaign_plants, &campaign_props};
  for (auto& test : engine_tests) {
    graphics_options.push_back(&test.option);
  }
  talesofarise::pixel_trace::file_prefix = "attila";
  graphics_status = tw::engine_tweaks::StatusLine();
  reshade::get_config_value(nullptr, "Attila", "DisableGraphics", shadow_size_locked);
  ui_mod_compatibility = shadow_size_locked ? 1 : 0;
  // Every Graphics (Experimental) setting at its default: the presets change some, the rest goes back (none left over).
  // Not the ambient occlusion or the Light Bounce (their own sections); the tests too.
  std::vector<std::pair<std::string, float>> graphics_defaults = {
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
      {"GraphicsBigBattleFix", 0.f},
      {"GraphicsSmallSoldiersAsSprites", 0.f},
      {"GraphicsSpriteSize", 20.f},
      {"GraphicsShadowMapSize", 0.f},
      {"GraphicsObjectDetail", 1.f},
      {"GraphicsBuildingDetail", 1.f},
      {"GraphicsCampaignDetail", 1.f},
      {"GraphicsGrassDistance", 1.f},
      {"GraphicsGrassDensity", 1.f},
      {"GrassHeightDistance", 1.f},
      {"GraphicsCampaignPlants", 1.f},
      {"GraphicsCampaignProps", 1.f},
  };
  for (const auto& test : engine_tests) {
    // Its option's start: 1x, Game's (0), the game's value or the second choice (1).
    graphics_defaults.emplace_back(test.key, test.option.scale);
  }
  // A preset button: `defaults` with `changes` (in order) written to the settings.
  const auto preset = [](const std::vector<std::pair<std::string, float>>& defaults,
                         const std::vector<std::pair<std::string, float>>& changes) {
    return [values = defaults, changes]() mutable {
      for (const auto& [key, value] : changes) {
        std::ranges::find(values, key, &std::pair<std::string, float>::first)->second = value;
      }
      renodx::utils::settings::UpdateSettings(values);
    };
  };
  // ATN's own settings: the CPU-heavy unit distances up, sprites nearer with 4x / 8x unit sizes (the Unit Sizes mod).
  const auto unit_detail_all = [](float scale) {
    return std::vector<std::pair<std::string, float>>{
        {"GraphicsUnitDetailSmall", scale},
        {"GraphicsUnitDetailMedium", scale},
        {"GraphicsUnitDetailLarge", scale},
        {"GraphicsUnitDetailHuge", scale},
    };
  };
  // Both of them.
  const std::vector<std::pair<std::string, float>> atn_settings = {
      {"GraphicsUnitVariety", 3.f},
      {"GraphicsFootstepEffects", 3.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsCampaignPlants", 4.f},
      {"GraphicsCampaignProps", 4.f},
      {"GraphicsShadowMapSize", 2.f},
  };
  auto large_units = unit_detail_all(0.69f);
  large_units.insert(large_units.end(),
                     {{"GraphicsDeadUnits", 0.5f},
                      {"GraphicsUnitAnimation", 1.5f},
                      {"GraphicsFacialAnimation", 1.5f},
                      {"GraphicsHandAnimation", 1.5f},
                      {"GraphicsBuildingDetail", 3.f}});
  large_units.insert(large_units.end(), atn_settings.begin(), atn_settings.end());
  auto vanilla_units = unit_detail_all(1.2f);
  vanilla_units.insert(vanilla_units.end(),
                       {{"GraphicsDeadUnits", 0.85f},
                        {"GraphicsUnitAnimation", 3.f},
                        {"GraphicsFacialAnimation", 3.f},
                        {"GraphicsHandAnimation", 3.f},
                        {"GraphicsBuildingDetail", 4.f}});
  vanilla_units.insert(vanilla_units.end(), atn_settings.begin(), atn_settings.end());
  auto* note = tw::engine_tweaks::Note();
  note->label += " Some apply only after loading a battle or a save.\n"
                 "The two presets below are the settings I would use myself.";
  const auto unit_distance = [](size_t index, const std::string& key, const std::string& label,
                                const std::string& units) {
    return tw::engine_tweaks::Slider(&unit_detail[index], key, label,
                                     "How far " + units + " stay 3D before turning into flat sprites.\n"
                                     "Higher: heavier for the CPU and GPU.",
                                     8.f, 0.1f);
  };
  // Two empty lines after this section (ATN).
  const auto empty_line = [] {
    return new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "",
        .section = "Graphics (Experimental)",
    };
  };
  settings.insert(settings.end(), {
      note,
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
          .label = "UI 4K Mod Compatibility",  // the Graphics page takes it by this (graphics_page.hpp)
          .section = "Graphics (Experimental)",
          .on_draw = [] {
            const char* state = (ui_mod_compatibility != 0) ? "On" : "Off";
            if (ImGui::SliderInt("UI 4K Mod Compatibility", &ui_mod_compatibility, 0, 1, state,
                                 ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
              reshade::set_config_value(nullptr, "Attila", "DisableGraphics", ui_mod_compatibility);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("On (default): Shadow Map Size stays at the game's, for the UI 4K mod (it changes the\n"
                                "same game code). Everything else on this page works either way.\n"
                                "Takes effect after restarting the game. By hand: ReShade.ini, [Attila] DisableGraphics.");
            }
            ImGui::SameLine();
            const bool restart_pending = (ui_mod_compatibility != 0) != shadow_size_locked;
            ImGui::TextColored(ImVec4(1.f, 0.31f, 0.31f, 1.f), "%s",
                               (restart_pending ? "Restart the game now to apply this!" : "Changing this needs a game restart."));
            return false;
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "With my 4x / 8x unit sizes",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 0.69x, Dead Unit Detail 0.50x, Unit / Facial / Hand Animation 1.50x, Unit Variety and\n"
                     "Footstep Effects 3x, Building Detail 3x, Campaign Trees / Plants / Props 4x, Shadow Map Size 8192.\n"
                     "The rest at its default.",
          .on_change = preset(graphics_defaults, large_units),
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "With vanilla unit sizes",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 1.20x, Dead Unit Detail 0.85x, Unit / Facial / Hand Animation, Unit Variety and\n"
                     "Footstep Effects 3x, Building Detail 4x, Campaign Trees / Plants / Props 4x, Shadow Map Size 8192.\n"
                     "The rest at its default.",
          .on_change = preset(graphics_defaults, vanilla_units),
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting back to its default.",
          .on_change = preset(graphics_defaults, {}),
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
          .tooltip = "Screen-space reflections (water, armour, metal): more steps, so reflections reach farther\n"
                     "with fewer gaps. Heavier. Below 1x: faster, shorter. Needs in-game Screen Space Reflections on.",
          .min = 0.25f,
          .max = 16.f,
          .format = "%.2fx",
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
          .key = "GraphicsShadowMapSize",
          .binding = &shadow_map_size,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Shadow Map Size",
          .section = "Graphics (Experimental)",
          .tooltip = "Sharper sun shadows that flicker less, at Shadows Max (lower Shadows settings scale\n"
                     "with it). More video memory. Applies after a restart. Locked by UI 4K Mod Compatibility.",
          .labels = {"2048 (Game's)", "4096", "8192"},
          .is_enabled = [] { return !shadow_size_locked; },
      },
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
      // The pixel trace (talesofarise_dlss/pixel_trace.hpp), one frame on request; its marker only while it is open.
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
          .section = "Graphics (Experimental)",
          .on_draw = [] {
            return talesofarise::pixel_trace::DrawRow(&traced_pixel, "Put the cross\non a sea or river reflection.");
          },
      },
      graphics_status,
      empty_line(),
      empty_line(),
  });

  // Appended Settings (Graphics 2 of 2's tests): one section per kind, an empty line after each.
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
      .label = "Grass Height Distance",
      .section = twattila::engine_tests::GRASS,
      .tooltip = "Grass blades shrink and fade into the ground with distance, gone at 100 m (the game's).\n"
                 "Higher: full grass farther away.",
      .min = 0.25f,
      .max = 10.f,
      .format = "%.2fx",
  });
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
      .on_change = [] {
        twattila::graphics_hooks::TraceGrass();
        renodx::utils::log::i("[Total War] Grass trace asked: Grass Height Distance ", shader_injection.grass_height_distance);
      },
  });
  twattila::engine_tests::Append(&settings, &engine_tests, SyncEarlyLoad);

  // Appended Settings (Ambient Occlusion, Global Illumination)
  twattila::lighting_settings::Append(&settings, &shader_injection, [] { return rt_sliders.light_bounce != 0.f && twattila::feature_settings::Upscaling(aa_sliders); });

  // Appended Settings (Screen-Space Shadows, Anti-Aliasing: twattila/feature_settings.hpp)
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, true, true, twattila::feature_settings::AA_SECTION,
                                     &shader_injection.light_bounce_strength);
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterClarity",
      .binding = &twattila::water_effects::water_clarity,
      .default_value = 1.f,
      .label = "Water Clarity",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "How far you see into rivers, lakes and the sea. 1: the game's; higher: clearer water.",
      .min = 0.25f,
      .max = 16.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterCaustics",
      .binding = &twattila::water_effects::water_caustics,
      .default_value = 1.f,
      .label = "Water Caustics (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Rippling light on river, lake and shallow sea beds in battles. 0: off.",
      .max = 2.f,
      .format = "%.2f",
  });

  // Appended Settings (Output)
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: HDR10 with Windows HDR on, else SDR. Default.\n"
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
        .hdr_encoding = [](int output) { return (output == twattila::output_encoding::SCRGB) ? twattila::output_encoding::SCRGB : twattila::output_encoding::HDR10; },
        .first_start_without_hdr = (first_start ? &ApplySdrPreset : nullptr),
    });
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.insert(settings.end(), {
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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarattila/mods/158"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "GitHub (Downloads)",
          .section = "About",
          .group = "line_1",
          .tint = 0x6E7681,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://github.com/arshiatn/renodx/releases"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV30, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's "
                   "custom PsychoV31 and PsychoV35. Visibility Bitmask ambient occlusion and Light Bounce: the method of Therrien, Levesque "
                   "and Gilet (2023) on GTAO (Jimenez et al. 2016), as GT-VBAO and GT-VBGI by Mirko Salm (2024); auto "
                   "thickness after Bjorn Ottosson.\n"
                   "Screen-space shadows: Bend Studio, Copyright 2023 Sony Interactive Entertainment, Apache License "
                   "2.0.\n"
                   "SMAA: Jimenez, Echevarria, Masia, Navarro and Gutierrez (2013), MIT License. CMAA2: Filip Strugar, "
                   "Copyright (c) 2018 Intel Corporation, Apache License 2.0. Reflections' ray march: AMD FidelityFX "
                   "SSSR, Copyright (c) 2021 Advanced Micro Devices, MIT License.",
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
  });

  // Section titles: one colour, the engine graphics pink, the ambient occlusion, Global Illumination and screen-space
  // shadows their own (ATN).
  std::vector<std::string> engine_sections = {"Graphics (Experimental)"};
  for (const auto& test : engine_tests) {
    if (std::ranges::find(engine_sections, test.section) == engine_sections.end()) {
      engine_sections.push_back(test.section);
    }
  }
  twattila::feature_settings::SetTints(
      {"General", "Psycho V30", "PsychoV31", "PsychoV35", "Extra", "Extra (Experimental)", "Output (Restart Required)", "About"},
      engine_sections);

  // Graphics 1 of 2's layout (graphics_page.hpp), then Settings Mode's pages.
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders, .sss = &sss_sliders, .rt = &rt_sliders, .ssao_mode = &shader_injection.ssao_mode,
                                             .light_bounce_strength = &shader_injection.light_bounce_strength,
                                             .ssr_hierarchical = &shader_injection.ssr_hierarchical,
                                             .smoke_strength = &smoke_map_sliders.strength, .smoke_softness = &smoke_map_sliders.softness,
                                             .lod_bias_trilinear = &lod_bias_trilinear_row, .rays_banner_light = &rays_banner_light_row, .rays_sheets_unselfed = &rt_sliders.sheets_unselfed,
                                             .engine_code = &engine_code_row, .lights_in_bounce = &lights_in_bounce_row,
                                             .light_ratio = &light_ratio_row});
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
  twattila::test_hooks::Install(&settings, true);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::smoke_map::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::grass_fade::OnDestroyDevice);

  // Rows saved at an old default, cleared once. Before v149: NRD when Ray Reconstruction is off, the banners' light and colour on.
  constexpr int SETTINGS_REVISION = 1;
  if (int revision = 0; !reshade::get_config_value(nullptr, "renodx", "SettingsRevision", revision) || revision < SETTINGS_REVISION) {
    for (const char* preset : {"renodx-preset1", "renodx-preset2", "renodx-preset3"}) {
      for (const char* key : {"RayTracingDenoiser", "ScreenSpaceShadowsBannerLight", "ScreenSpaceShadowsColoured"}) {
        reshade::set_config_value(nullptr, preset, key, static_cast<const char*>(nullptr));
      }
    }
    reshade::set_config_value(nullptr, "renodx", "SettingsRevision", SETTINGS_REVISION);
  }

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests (audio memory, read once as the game's audio starts): written as soon as the game has set its own
  // values, on a thread of their own (the addon may load before the game's code runs: SyncEarlyLoad). Up to a minute.
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(L"empire.retail.dll", &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // Before the Light Bounce's (its callback draws the sun draw again).
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit_recorded,
                                                        .unit_records_compute_shader = __motion_unit_records,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, RENDER_SCALE_VERTEX_SHADERS, FULL_DEPTH_SHADERS,
                                   twattila::view_share::WHOLE_TEXTURES);
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace.
  // Their lighting buffer's ambient cube at c2 (lighting_VS_PS): the paths' sky; its sun_colour at c1 (B6DA03C5, C64DB947,
  // 9ECED7F8: N.L x the G-buffer's albedo^2 x (1 - its reflectivity) x c1 x 0.004 with c11.x, the shadow): their sun.
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4);
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::InstallFoliage(FOLIAGE_VERTEX_SHADERS, AFTER_FOLIAGE_VERTEX_SHADERS);
  // The G-buffer's colour squared (gamma 2).
  twattila::screen_space_shadows::InstallThin(THIN_SHADERS, 2.f);
  twattila::water_effects::water_shaders = WATER_SHADERS;
  renodx::utils::pipeline::RegisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  twattila::grass_fade::Install();
  // The AO pass before the ray tracing (its multiply after the pass's own output), both before the Light Bounce (it
  // draws the sun draws itself: no callback after it runs).
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
  // Smoke's ray-traced shadows (the lit particles streamed out).
  twattila::rt_scene::InstallSmokeShadows(twattila::rt_scene::PARTICLE_VERTEX_SHADERS, twattila::rt_scene::LIT_PARTICLE_PIXEL_SHADERS);
  // Ray-Traced Shadows Only: the sun's shadow map's casters skipped (the map the sun draws read); the forward passes that
  // read it (near grass, particles) take the rays' shadows instead.
  twattila::rt_scene::InstallShadowsOnly();
  twattila::screen_space_shadows::PatchForwardShadows(twattila::rt_scene::SHADOW_READER_SHADERS);
  twattila::rt_scene::InstallShadowReaders(twattila::rt_scene::SHADOW_READER_SHADERS);
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);
  twattila::screen_space_shadows::SetSmokeOcclusion(&twattila::smoke_map::Occlusion);
  // After the smoke's capture on the same draws (it sees the game's targets and camera).
  twattila::particle_layer::Install(twattila::rt_scene::PARTICLE_VERTEX_SHADER, PARTICLE_LAYER_READERS);

  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::particle_layer::OnDestroyDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::particle_layer::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  twattila::particle_layer::Uninstall();
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  twattila::test_hooks::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::smoke_map::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::grass_fade::OnDestroyDevice);
  twattila::engine_tests::StopAtStart(&at_start_stop);
  tw::engine_tweaks::Uninstall(graphics_options);
  twattila::engine_code::Uninstall();
  twattila::graphics_hooks::Uninstall();
  twattila::graphics_hooks::shadow_map::Uninstall();
  renodx::utils::command_action::Unregister(OnCampaignDraw);
  renodx::utils::command_action::Unregister(twattila::water_effects::OnWaterDraw);
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  renodx::utils::pipeline::UnregisterOnCreateCallback(twattila::water_effects::OnCreateWater);
  twattila::grass_fade::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::ao_pass::Uninstall();
  twattila::rt_scene::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
  twattila::screen_space_shadows::SetSmokeOcclusion(nullptr);
  twattila::smoke_map::Uninstall();
  talesofarise::pixel_trace::Stop();

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
