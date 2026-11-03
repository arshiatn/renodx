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
#include "./engine_tests.hpp"
#include "./feature_settings.hpp"
#include "./grass_fade.hpp"
#include "./graphics_page.hpp"
#include "./light_bounce.hpp"
#include "./lighting_settings.hpp"
#include "./rt_scene.hpp"
#include "./screen_space_shadows.hpp"
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

bool IsModdedAutoExposureEnabled() {
  return shader_injection.auto_exposure_highlight_protection == 1.f;
}

// PsychoV30 has no V25-style highlight-only saturation control. Recommended
// saturation values therefore target V30's adaptation-relative LMS purity.
// rendering_mode: PsychoV31 (2) in all of them, with V31's own sliders (ATN; Reset too: no Extended mode here).
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
  renodx::utils::settings::UpdateSettings({
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
  });
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
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "Use these in-game settings: Gamma 20 and Brightness 10.",
        .section = "General",
        .tint = 0xFFD23C,
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
                   "HDR (PsychoV31): the HDR pipeline with PsychoV31, more grading sliders.",
        .labels = {"SDR", "HDR (PsychoV30)", "HDR (PsychoV31)"},
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
          ApplyHDRPreset(2.f, 1.f, 1.00f, 1.f, 0.f, 2.f, 1.f, 1.f, 1.f, 1.f, 0.f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: no VFX boost",
        .group = "preset-line-1",
        .tooltip = "Resets all image tuning to HDR defaults while preserving Peak, Paper White, UI brightness, Settings Mode, and output encoding.",
        .on_change = []() {
          ApplyHDRPreset(2.f, 1.f, 1.00f, 0.35f, 0.f, 2.f, 1.f, 1.f, 1.f, 2.f, 0.2f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: All Conditions",
        .group = "preset-line-1",
        .tooltip = "Balanced recommendation intended to work across all weather and lighting conditions. Display calibration is preserved.",
        .on_change = []() {
          ApplyHDRPreset(2.f, 1.f, 1.00f, 0.1f, 1.f, 4.f, 2.3f, 1.0f, 1.5f, 2.f, 0.2f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: Day/Dry/Rain",
        .group = "preset-line-2",
        .tooltip = "Recommendation for daytime scenes and dry weather. Display calibration is preserved.",
        .on_change = []() {
          ApplyHDRPreset(2.f, 1.f, 1.00f, 0.1f, 1.f, 4.f, 4.4f, 2.f, 1.5f, 2.f, 0.2f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: Night/Snow/Fog",
        .group = "preset-line-2",
        .tooltip = "Recommendation for fog, snow, and nighttime scenes. Display calibration is preserved.",
        .on_change = []() {
          ApplyHDRPreset(2.f, 1.f, 1.00f, 0.1f, 1.f, 4.f, 1.8f, 0.7f, 1.5f, 2.f, 0.2f);
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
}

// Graphics (Experimental): the sliders, through the LOD pick hook (object detail), Attila's unit hook
// (graphics_hooks.hpp) and engine tweaks (filled in ConfigureAddon).
std::array<tw::engine_tweaks::Option, 4> unit_detail;  // foot soldiers, cavalry, elephants, artillery
float big_battle_fix = 0.f;
float ssr_denoiser = 1.f;  // ssr_hiz.hpp: the resolve's history
float shadow_map_size = 0.f;  // the game's sizes x 2^this (graphics_hooks.hpp shadow_map)
twattila::feature_settings::SssSliders sss_sliders;
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
constexpr std::array<twattila::rt_scene::SceneShader, 38> SCENE_SHADERS = {{
    {.hash = 0x51FCE4CC, .capture_shader = __rt_capture_terrain_sm3, .terrain = true},
    {.hash = 0x2FF76986, .capture_shader = __rt_capture_terrain_sm41, .terrain = true},
    {.hash = 0x6ECB6C09, .capture_shader = __rt_capture_terrain_old_sm41, .terrain = true},
    {.hash = 0x28399869, .capture_shader = __rt_capture_terrain_old_sm3, .terrain = true},
    {.hash = 0x6A0D0455, .capture_shader = __rt_capture_terrain_custom_material_sm3, .terrain = true},
    {.hash = 0xD8DA0B13, .capture_shader = __rt_capture_terrain_custom_material_sm41, .terrain = true},
    {.hash = 0x829CDC4F, .capture_shader = __rt_capture_terrain_custom_sm41, .terrain = true},
    {.hash = 0xF1C6020E, .capture_shader = __rt_capture_terrain_custom_sm3, .terrain = true},
    {.hash = 0x347F3DD9, .capture_shader = __rt_capture_rigid_sm3, .rows_shader = __motion_record_rigid_sm3, .cut_out_shader = __rt_capture_rigid_cut_out_sm3},
    {.hash = 0xD830AD9B, .capture_shader = __rt_capture_rigid_sm41, .rows_shader = __motion_record_rigid_sm41, .cut_out_shader = __rt_capture_rigid_cut_out_sm41},
    {.hash = 0xB06CB969, .capture_shader = __rt_capture_rigid_blend_sm3, .rows_shader = __motion_record_rigid_blend_sm3},
    {.hash = 0x10FE7EBC, .capture_shader = __rt_capture_rigid_blend_wind_sm3, .rows_shader = __motion_record_rigid_blend_wind_sm3},
    // Rocks and cliffs blended into the terrain (rigidterrainblend): its depth pass (its material pass has no camera).
    {.hash = 0xE03E161C, .capture_shader = __rt_capture_rigid_terrain_blend_sm3, .rows_shader = __motion_record_rigid_terrain_blend_sm3},
    {.hash = 0x77B993FD, .capture_shader = __rt_capture_branch_sm3, .rows_shader = __motion_record_tree},
    {.hash = 0xB8C3E83D, .capture_shader = __rt_capture_branch_sm41, .rows_shader = __motion_record_tree},
    {.hash = 0x6EEF4062, .capture_shader = __rt_capture_branch_sm5, .rows_shader = __motion_record_tree},
    {.hash = 0x46C616F0, .capture_shader = __rt_capture_leaf_sm3, .rows_shader = __motion_record_tree, .foliage = true},
    {.hash = 0x5B880546, .capture_shader = __rt_capture_leaf_sm41, .rows_shader = __motion_record_tree, .foliage = true},
    {.hash = 0xCABCB9D1, .capture_shader = __rt_capture_leaf_sm5, .rows_shader = __motion_record_tree, .foliage = true},
    {.hash = 0x5F9CD405, .capture_shader = __rt_capture_unit_sm3, .dynamic = true},
    {.hash = 0x0ECFE74C, .capture_shader = __rt_capture_unit_4_bones_sm3, .dynamic = true},
    {.hash = 0xDAC80ECC, .capture_shader = __rt_capture_unit_dismember_sm3, .dynamic = true},
    {.hash = 0xC2AA8FDA, .capture_shader = __rt_capture_unit_4_bones_dismember_sm3, .dynamic = true},
    {.hash = 0xA691CFF4, .capture_shader = __rt_capture_unit_sm41, .dynamic = true},
    {.hash = 0x372628AB, .capture_shader = __rt_capture_unit_4_bones_sm41, .dynamic = true},
    {.hash = 0xEA1AEB0E, .capture_shader = __rt_capture_unit_dismember_sm41, .dynamic = true},
    {.hash = 0x31B66A15, .capture_shader = __rt_capture_unit_4_bones_dismember_sm41, .dynamic = true},
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
    {.hash = 0x4D50E1C7, .capture_shader = __rt_capture_billboard, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true},
    // The campaign's plants and forests, and its trees: placed like the battle's, no wind.
    {.hash = 0x924D7813, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true},
    {.hash = 0x819A414C, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true},
    {.hash = 0x4657DA8A, .capture_shader = __rt_capture_campaign_tree, .rows_shader = __rt_rows_campaign_tree, .foliage = true, .still = true},
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

// Water Clarity (ATN: rivers more see-through): the water shaders (WATER_SHADERS) blend toward the deep water colour by
// g_sea_bed_light_scatter (lighting_VS_PS c10) to the power of the water they look through. At their draws a copy of those
// constants with it raised to 1 / clarity is bound (water_clarity.cs), the game's put back after the draw (later draws, and
// the game's updates of its own buffer, stay its). Up to 16: murky rivers (ATN's debug28: no change seen at 4) keep little
// light a metre, 4 times as far still dark. Logged once where it applies.
struct WaterShader {
  uint32_t hash = 0;
  UINT lighting_slot = 0;        // b: lighting_VS_PS
  uint32_t world_input = 0;      // v: the world position
  uint32_t refraction_slot = 0;  // t: the water refraction's output
  bool caustics = true;          // the campaign's world is a twentieth of the battles': a 4 m tile means nothing there
};
// The campaign's rivers (1B25BD46) are no water in the view: painted into a map overlay, nothing under them to see.
constexpr std::array<WaterShader, 15> WATER_SHADERS = {{
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
float water_clarity = 1.f;
// Water Caustics (ATN): the shaders' refracted bed colour x (1 + t51 at their world position / WATER_CAUSTICS_TILE), the
// tile water_caustics.cs makes at the first water draw of a frame; t51 unbound reads 0 (the game's water). Patched as
// they are created (PatchWaterCaustics).
constexpr UINT WATER_CAUSTICS_SLOT = 51;
constexpr float WATER_CAUSTICS_TILE = 4.f;  // metres
constexpr UINT WATER_CAUSTICS_SIZE = 256;
float water_caustics = 1.f;
using Microsoft::WRL::ComPtr;
struct CausticsObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> shader;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
  uint64_t made_tick = 0;  // its last time (one a frame: within 4 ms of it, the same frame's)
};
CausticsObjects caustics_objects;
// The patched water shaders by the game's hash (kept: the game may create them again).
std::unordered_map<uint32_t, std::vector<uint8_t>> water_patched;
std::mutex water_mutex;
struct WaterObjects {
  ComPtr<ID3D11Device> device;
  UINT bytes = 0;  // the game's lighting constants' (0: none made)
  ComPtr<ID3D11ComputeShader> shader;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11Buffer> copied;  // raw: the game's constants
  ComPtr<ID3D11ShaderResourceView> copied_srv;
  ComPtr<ID3D11Buffer> clarified;  // raw: with the scatter raised
  ComPtr<ID3D11UnorderedAccessView> clarified_uav;
  ComPtr<ID3D11Buffer> lighting;       // the constant buffer bound for the draw
  ComPtr<ID3D11Buffer> game_lighting;  // the game's, put back after it
  UINT lighting_slot = 0;              // the draw's
  ID3D11Buffer* source = nullptr;      // the game's buffer `lighting` was made from (compared only)
  uint64_t made_tick = 0;              // its last time (within 4 ms: the same frame's)
  bool logged = false;
};
WaterObjects water_objects;

// A water shader with the caustics (empty: not as expected): after the refraction's colour sample (a sample of 3+
// components into rD from its t: the last, the river's first takes w alone, the sea reads two) or the `if` picking between
// them, with rT one more temp and t51 declared:
//   mul rT.xy, vW.xzxx, l(1 / tile) / frc rT.xy, rT.xyxx / sample_l rT.x, rT.xyxx, t51, s(the normal map's), l(0) /
//   mad rD.xyz(w's left), rD, rT.xxxx, rD
std::vector<uint8_t> PatchWaterCaustics(std::span<const uint8_t> code, const WaterShader& water) {
  constexpr uint32_t OP_ENDIF = 21, OP_FRC = 26, OP_IF = 31, OP_MAD = 50, OP_MUL = 56, OP_SAMPLE = 69, OP_SAMPLE_L = 72, OP_DCL_RESOURCE = 88,
                     OP_DCL_TEMPS = 104;
  constexpr uint32_t EXTENDED = 1u << 31;
  twattila::dxbc::Program program = twattila::dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  size_t temps_at = 0;
  size_t refraction_at = 0;  // the refraction's colour sample, its operands' start
  size_t refraction_operands = 0;
  size_t normal_at = 0;  // the normal map's (t0) sample, its operands' start
  size_t normal_operands = 0;
  for (size_t at = 2; at < tokens.size();) {
    const uint32_t opcode = tokens[at] & 0x7FF;
    const size_t length = twattila::dxbc::Length(tokens, at);
    if (length == 0) return {};
    if (opcode == OP_DCL_TEMPS) temps_at = at;
    if (opcode == OP_SAMPLE || opcode == OP_SAMPLE_L) {
      size_t operands = at + 1;
      if ((tokens[at] & EXTENDED) != 0) {
        while ((tokens[operands] & EXTENDED) != 0) ++operands;
        ++operands;
      }
      // dest, address, resource, sampler: two tokens each (one immediate index).
      const bool plain = operands + 8 <= at + length && (tokens[operands + 4] & 0x001FF000) == 0x00107000
                         && (tokens[operands + 6] & 0x001FF000) == 0x00106000;
      if (plain && opcode == OP_SAMPLE && tokens[operands + 5] == 0 && normal_at == 0) {
        normal_at = at;
        normal_operands = operands;
      }
      if (plain && tokens[operands + 5] == water.refraction_slot && (tokens[operands] & 0x001FF00F) == 0x00100002
          && std::popcount((tokens[operands] >> 4) & 0xFu) >= 3) {
        refraction_at = at;
        refraction_operands = operands;
      }
    }
    at += length;
  }
  if (temps_at == 0 || refraction_at == 0 || normal_at == 0) return {};
  const uint32_t temp = tokens[temps_at + 1];
  const uint32_t bed = tokens[refraction_operands + 1];
  const uint32_t scale = std::bit_cast<uint32_t>(1.f / WATER_CAUSTICS_TILE);
  // A whole xyzw read: its w stays.
  const uint32_t destination = ((tokens[refraction_operands] & 0xF0) == 0xF0 ? (tokens[refraction_operands] & ~0x80u) : tokens[refraction_operands]);
  std::vector<uint32_t> added = {
      // mul rT.xy, vW.xzxx, l(scale, scale, 0, 0) / frc rT.xy, rT.xyxx (the tile repeats whatever the sampler's address mode)
      OP_MUL | (10u << 24), 0x00100032, temp, 0x00101086, water.world_input, 0x00004002, scale, scale, 0u, 0u,
      OP_FRC | (5u << 24), 0x00100032, temp, 0x00100046, temp,
      // sample_l(_indexable(texture2d)(float,float,float,float): the normal map's extended tokens, none below shader model 5)
      // rT.x, rT.xyxx, t51.xyzw, sN, l(0)
      OP_SAMPLE_L | (tokens[normal_at] & EXTENDED) | (static_cast<uint32_t>(10 + normal_operands - normal_at) << 24),
  };
  added.insert(added.end(), tokens.begin() + static_cast<ptrdiff_t>(normal_at + 1), tokens.begin() + static_cast<ptrdiff_t>(normal_operands));
  added.insert(added.end(), {
      0x00100012, temp, 0x00100046, temp, 0x00107E46, WATER_CAUSTICS_SLOT, tokens[normal_operands + 6], tokens[normal_operands + 7], 0x00004001, 0u,
      // mad rD, rD.xyzw, rT.xxxx, rD.xyzw
      OP_MAD | (9u << 24), destination, bed, 0x00100E46, bed, 0x00100006, temp, 0x00100E46, bed,
  });
  size_t added_at = refraction_at + twattila::dxbc::Length(tokens, refraction_at);
  if ((tokens[added_at] & 0x7FF) == OP_IF) {
    while ((tokens[added_at] & 0x7FF) != OP_ENDIF) added_at += twattila::dxbc::Length(tokens, added_at);
    ++added_at;
  }
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(added_at), added.begin(), added.end());
  // dcl_resource_texture2d (float,float,float,float) t51, before dcl_temps (the declarations come first).
  const std::array<uint32_t, 4> declaration = {OP_DCL_RESOURCE | (3u << 11) | (4u << 24), 0x00107000, WATER_CAUSTICS_SLOT, 0x5555};
  ++tokens[temps_at + 1];
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), declaration.begin(), declaration.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return twattila::dxbc::Write(code, program);
}

// The game creates a pipeline: the water shaders with the caustics (renodx keeps their original hash; one the render
// scale patched first by its hash).
bool OnCreateWater(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                   const reshade::api::pipeline_subobject* subobjects) {
  if (device->get_api() != reshade::api::device_api::d3d11) return false;
  bool modified = false;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
    auto* desc = static_cast<reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const std::span<const uint8_t> code(static_cast<const uint8_t*>(desc->code), desc->code_size);
    const auto scaled = twattila::view_share::OriginalHash(desc->code);
    const uint32_t hash = scaled.value_or(renodx::utils::hash::ComputeCRC32(code.data(), code.size()));
    const auto water = std::ranges::find(WATER_SHADERS, hash, &WaterShader::hash);
    if (water == WATER_SHADERS.end() || !water->caustics) continue;
    const std::unique_lock lock(water_mutex);
    auto [entry, added] = water_patched.try_emplace(hash);
    if (added) {
      entry->second = PatchWaterCaustics(code, *water);
      renodx::utils::log::i(std::format("[Total War] Water Caustics: water shader {:08X} {}", hash,
                                        entry->second.empty() ? "not as expected, left as it is." : "patched."));
    }
    if (entry->second.empty()) continue;
    desc->code = entry->second.data();
    desc->code_size = entry->second.size();
    twattila::view_share::Repatched(code.data(), desc->code, hash);
    modified = true;
  }
  return modified;
}

std::atomic_bool water_drawn = false;  // logged once: which water shader the game draws

constexpr auto OnWaterDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!water_drawn.exchange(true)) {
    renodx::utils::log::i(std::format("[Total War] Water: first draw of shader {:08X} (Clarity {}, Caustics {}).",
                                      command.matched_shader_hash, water_clarity, water_caustics));
  }
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  // Water Caustics: the tile made at the frame's first water draw, bound at t51 for the draw.
  auto& caustics = caustics_objects;
  if (water_caustics > 0.f && caustics.device != device) {
    caustics = {.device = device};
    const D3D11_TEXTURE2D_DESC tile_desc = {.Width = WATER_CAUSTICS_SIZE, .Height = WATER_CAUSTICS_SIZE, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R16_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                            .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    ComPtr<ID3D11Texture2D> tile;
    const bool made = SUCCEEDED(device->CreateComputeShader(__water_caustics.data(), __water_caustics.size(), nullptr, caustics.shader.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&constants_desc, nullptr, caustics.constants.GetAddressOf()))
                      && SUCCEEDED(device->CreateTexture2D(&tile_desc, nullptr, tile.GetAddressOf()))
                      && SUCCEEDED(device->CreateShaderResourceView(tile.Get(), nullptr, caustics.srv.GetAddressOf()))
                      && SUCCEEDED(device->CreateUnorderedAccessView(tile.Get(), nullptr, caustics.uav.GetAddressOf()));
    if (!made) caustics.srv.Reset();
  }
  const bool caustics_on = water_caustics > 0.f && caustics.srv;
  const uint64_t now = GetTickCount64();
  if (caustics_on) {
    if (now > caustics.made_tick + 4) {
      caustics.made_tick = now;
      const twattila::rt_scene::internal::ComputeState kept(native);
      const std::array<float, 4> constants = {static_cast<float>(now % 3600000) / 1000.f, water_caustics, 0.f, 0.f};
      native->UpdateSubresource(caustics.constants.Get(), 0, nullptr, constants.data(), 0, 0);
      native->CSSetShader(caustics.shader.Get(), nullptr, 0);
      native->CSSetUnorderedAccessViews(0, 1, caustics.uav.GetAddressOf(), nullptr);
      native->CSSetConstantBuffers(0, 1, caustics.constants.GetAddressOf());
      native->Dispatch(WATER_CAUSTICS_SIZE / 8, WATER_CAUSTICS_SIZE / 8, 1);
    }
    native->PSSetShaderResources(WATER_CAUSTICS_SLOT, 1, caustics.srv.GetAddressOf());
  }
  // Water Clarity.
  const UINT lighting_slot = std::ranges::find(WATER_SHADERS, command.matched_shader_hash, &WaterShader::hash)->lighting_slot;
  ComPtr<ID3D11Buffer> game_lighting;
  if (std::abs(water_clarity - 1.f) >= 0.01f) native->PSGetConstantBuffers(lighting_slot, 1, game_lighting.GetAddressOf());
  D3D11_BUFFER_DESC game_desc = {};
  if (game_lighting) game_lighting->GetDesc(&game_desc);
  auto& objects = water_objects;
  if (game_lighting && (objects.device != device || objects.bytes != game_desc.ByteWidth)) {
    objects = {.device = device};
    const D3D11_BUFFER_DESC raw_in = {.ByteWidth = game_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_BUFFER_DESC raw_out = {.ByteWidth = game_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                       .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_BUFFER_DESC lighting_desc = {.ByteWidth = game_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX};
    srv_desc.BufferEx = {.FirstElement = 0, .NumElements = game_desc.ByteWidth / 4, .Flags = D3D11_BUFFEREX_SRV_FLAG_RAW};
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
    uav_desc.Buffer = {.FirstElement = 0, .NumElements = game_desc.ByteWidth / 4, .Flags = D3D11_BUFFER_UAV_FLAG_RAW};
    const bool made = SUCCEEDED(device->CreateComputeShader(__water_clarity.data(), __water_clarity.size(), nullptr, objects.shader.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&constants_desc, nullptr, objects.constants.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&raw_in, nullptr, objects.copied.GetAddressOf()))
                      && SUCCEEDED(device->CreateShaderResourceView(objects.copied.Get(), &srv_desc, objects.copied_srv.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&raw_out, nullptr, objects.clarified.GetAddressOf()))
                      && SUCCEEDED(device->CreateUnorderedAccessView(objects.clarified.Get(), &uav_desc, objects.clarified_uav.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&lighting_desc, nullptr, objects.lighting.GetAddressOf()));
    if (made) objects.bytes = game_desc.ByteWidth;
  }
  if (!game_lighting || objects.bytes != game_desc.ByteWidth) {
    if (!caustics_on) return {};
    return {.finalize_callback = [](Context& post, const void*) {
      ID3D11ShaderResourceView* no_view = nullptr;
      reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native())->PSSetShaderResources(WATER_CAUSTICS_SLOT, 1, &no_view);
    }};
  }
  // Once a frame (its water draws share the game's lighting buffer; the river is dozens of draws).
  if (now > objects.made_tick + 4 || objects.source != game_lighting.Get()) {
    objects.made_tick = now;
    objects.source = game_lighting.Get();
    const twattila::rt_scene::internal::ComputeState kept(native);
    native->CopyResource(objects.copied.Get(), game_lighting.Get());
    const std::array<uint32_t, 4> constants = {objects.bytes / 4, std::bit_cast<uint32_t>(1.f / water_clarity), 0u, 0u};
    native->UpdateSubresource(objects.constants.Get(), 0, nullptr, constants.data(), 0, 0);
    native->CSSetShader(objects.shader.Get(), nullptr, 0);
    native->CSSetShaderResources(0, 1, objects.copied_srv.GetAddressOf());
    native->CSSetUnorderedAccessViews(0, 1, objects.clarified_uav.GetAddressOf(), nullptr);
    native->CSSetConstantBuffers(0, 1, objects.constants.GetAddressOf());
    native->Dispatch(((objects.bytes / 4) + 63) / 64, 1, 1);
    ID3D11UnorderedAccessView* no_uav = nullptr;
    native->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
    native->CopyResource(objects.lighting.Get(), objects.clarified.Get());
  }
  objects.game_lighting = std::move(game_lighting);
  objects.lighting_slot = lighting_slot;
  native->PSSetConstantBuffers(lighting_slot, 1, objects.lighting.GetAddressOf());
  if (!std::exchange(objects.logged, true)) {
    renodx::utils::log::i("[Total War] Water Clarity: applied at a water draw (", objects.bytes, " bytes of lighting constants).");
  }
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    post_native->PSSetConstantBuffers(water_objects.lighting_slot, 1, water_objects.game_lighting.GetAddressOf());
    water_objects.game_lighting.Reset();
    ID3D11ShaderResourceView* no_view = nullptr;
    post_native->PSSetShaderResources(WATER_CAUSTICS_SLOT, 1, &no_view);
  }};
};

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

// Tests without input, that many seconds after the addon loaded: RENODX_TW_SHOT=<seconds>[,<count>[,<gap>]] ReShade
// screenshots (its save path, "auto"; by default a second 5 s later), RENODX_TW_PROFILE=<seconds> Record Ray Tracing Profile.
void OnReshadePresentShot(reshade::api::effect_runtime* runtime) {
  const auto seconds = [](const char* name) {
    std::array<char, 32> value = {};
    return (GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size())) != 0) ? std::strtoull(value.data(), nullptr, 10) * 1000ull
                                                                                                : 0ull;
  };
  static const uint64_t shot_ms = seconds("RENODX_TW_SHOT");
  static const auto [shot_count, shot_gap_ms] = [] {
    std::array<char, 32> value = {};
    GetEnvironmentVariableA("RENODX_TW_SHOT", value.data(), static_cast<DWORD>(value.size()));
    const char* comma = std::strchr(value.data(), ',');
    const char* gap = (comma != nullptr) ? std::strchr(comma + 1, ',') : nullptr;
    return std::pair<int, uint64_t>{(comma != nullptr) ? std::atoi(comma + 1) : 2, (gap != nullptr) ? std::strtoull(gap + 1, nullptr, 10) * 1000ull : 5000ull};
  }();
  static const uint64_t profile_ms = seconds("RENODX_TW_PROFILE");
  // RENODX_TW_SET=<seconds>:<key>=<value>[;<seconds>:<key>=<value>...]: settings changed then (as their sliders would).
  static std::string changes = [] {
    std::array<char, 1024> value = {};
    return std::string(value.data(), GetEnvironmentVariableA("RENODX_TW_SET", value.data(), static_cast<DWORD>(value.size())));
  }();
  static const uint64_t loaded = GetTickCount64();
  static int taken = 0;
  static bool profiled = false;
  const uint64_t now = GetTickCount64();
  if (profile_ms != 0 && !profiled && now >= loaded + profile_ms) {
    twattila::rt_scene::StartProfile();
    profiled = true;
  }
  for (size_t colon = changes.find(':'), equals = changes.find('=');
       colon != std::string::npos && equals > colon && equals != std::string::npos
       && now >= loaded + (std::strtoull(changes.c_str(), nullptr, 10) * 1000ull);
       colon = changes.find(':'), equals = changes.find('=')) {
    const std::string key = changes.substr(colon + 1, equals - colon - 1);
    for (auto* setting : settings) {
      if (setting->key != key || setting->binding == nullptr) continue;
      *setting->binding = std::strtof(changes.c_str() + equals + 1, nullptr);
      renodx::utils::log::i(std::format("[Total War] Test: {} set to {}.", key, *setting->binding));
    }
    const size_t next = changes.find(';');
    changes.erase(0, (next == std::string::npos) ? next : next + 1);
  }
  if (shot_ms == 0 || taken >= shot_count || now < loaded + shot_ms + (taken * shot_gap_ms)) return;
  runtime->save_screenshot("auto");
  ++taken;
}

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  talesofarise::pixel_trace::OnPresent(queue);
  if (auto& campaign_frames = twattila::frame_history::campaign_frames; campaign_frames.load(std::memory_order_relaxed) != 0) {
    campaign_frames.fetch_sub(1, std::memory_order_relaxed);
  }
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  const twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
  // Ray-traced shadows replace the screen-space ones too.
  auto sss_settings = twattila::feature_settings::SssSettings(sss_sliders);
  sss_settings.enabled = sss_settings.enabled && !rt_settings.sun_shadows;
  twattila::screen_space_shadows::Update(swapchain, sss_settings);
  // Ray Reconstruction is the only denoiser: the reflections' denoiser (and its history) off under it.
  twattila::feature_settings::UpdateSsr(&shader_injection, twattila::dlaa_bridge::bridge.reconstructing ? 0.f : ssr_denoiser);
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
          .big_battle_fix = big_battle_fix != 0.f,
      },
      &graphics_hooks_installed, graphics_status);
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

// Output Auto: HDR10 with Windows HDR on for the game's display (or unknown), else SDR (frame generation takes no scRGB, and
// turned on in a game started in it did nothing, ATN 2026-11-01). Set at the first swap chain, before the swapchain mod's
// create_swapchain (registered after this).
bool auto_output_pending = false;
// No settings saved yet: a player's first start. On an SDR monitor it starts with the SDR preset (ATN 2026-11-01).
bool first_start = false;

bool OnCreateSwapchain(reshade::api::device_api, reshade::api::swapchain_desc&, void* hwnd) {
  if (!auto_output_pending) {
    // Output scRGB: HDR10 with frame generation.
    if (aa_sliders.method == 5.f && aa_sliders.frame_generation != 0.f && shader_injection.swap_chain_encoding == 1.f) SetOutputEncoding(0);
    return false;
  }
  auto_output_pending = false;
  const HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY);
  const auto path = renodx::utils::platform::GetPathInfo(monitor);
  const bool windows_hdr = !path.has_value() || renodx::utils::swapchain::GetHDREnabled(path.value());
  SetOutputEncoding(windows_hdr ? 0 : 2);
  if (first_start && !windows_hdr) ApplySdrPreset();
  return false;
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
                     "with fewer gaps. Heavier. Needs in-game Screen Space Reflections on.",
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
      // The pixel trace (talesofarise_dlss/pixel_trace.hpp), one frame on request; its marker only while it is open.
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
          .section = "Graphics (Experimental)",
          .on_draw = [] {
            if (!ImGui::TreeNode("Pixel trace")) return false;
            ImGui::SliderFloat2("Traced pixel (% of width, height)", traced_pixel.data(), 0.f, 100.f, "%.1f%%");
            // A red cross marks the pixel, the red line the traced line through it.
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            const ImVec2 cross = {screen.x * traced_pixel[0] / 100.f, screen.y * traced_pixel[1] / 100.f};
            const float half_line = screen.x * talesofarise::pixel_trace::LINE_WIDTH * 0.5f;
            ImDrawList* foreground = ImGui::GetForegroundDrawList(nullptr);
            foreground->AddLine({cross.x - half_line, cross.y}, {cross.x + half_line, cross.y}, IM_COL32(255, 0, 0, 255), 1.f);
            foreground->AddLine({cross.x, cross.y - 12.f}, {cross.x, cross.y + 12.f}, IM_COL32(255, 0, 0, 255), 2.f);
            if (ImGui::Button("Trace these pixels for one frame")) {
              talesofarise::pixel_trace::Request(traced_pixel[0] / 100.f, traced_pixel[1] / 100.f);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Saves every pass that changes the pixels under the red line during the next frame\n"
                                "to attila_pixel_trace.txt next to the game's exe (one short hitch). Put the cross\n"
                                "on a sea or river reflection.");
            }
            ImGui::TextWrapped("%s", talesofarise::pixel_trace::Status().c_str());
            ImGui::TreePop();
            return false;
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
  twattila::lighting_settings::Append(&settings, &shader_injection, [] { return rt_sliders.light_bounce != 0.f && aa_sliders.method == 5.f; });

  // Appended Settings (Screen-Space Shadows, Anti-Aliasing: twattila/feature_settings.hpp)
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, true, true, twattila::feature_settings::AA_SECTION,
                                     &shader_injection.light_bounce_strength);
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterClarity",
      .binding = &water_clarity,
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
      .binding = &water_caustics,
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
                   "HDR10: 10-bit. scRGB: 16-bit, frame generation uses HDR10 instead. SDR: displays without HDR (all fixes\n"
                   "and options stay).",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const int value = static_cast<int>(setting->GetValue());
    auto_output_pending = (value == 3);
    float saved_mode = 0.f;
    first_start = !reshade::get_config_value(nullptr, (renodx::utils::settings::global_name + "-preset1").c_str(), "tonemapper", saved_mode);
    SetOutputEncoding(auto_output_pending ? 0 : value);
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV30 and PsychoV31 by ShortFuse. This mod uses Musa's custom PsychoV31. Visibility "
                   "Bitmask ambient occlusion and Light Bounce: the method of Therrien, Levesque and Gilet (2023) on "
                   "GTAO (Jimenez et al. 2016), as GT-VBAO and GT-VBGI by Mirko Salm (2024); auto thickness after "
                   "Bjorn Ottosson.\n"
                   "Screen-space shadows: Bend Studio, Copyright 2023 Sony Interactive Entertainment, Apache License "
                   "2.0.\n"
                   "SMAA: Jimenez, Echevarria, Masia, Navarro and Gutierrez (2013), MIT License. CMAA2: Filip Strugar, "
                   "Copyright (c) 2018 Intel Corporation, Apache License 2.0. Reflections' ray march: AMD FidelityFX "
                   "SSSR, Copyright (c) 2021 Advanced Micro Devices, MIT License.",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "NVIDIA DLSS (Super Resolution, Ray Reconstruction and Frame Generation), NVIDIA Reflex, Streamline, "
                   "NVAPI, the Opacity Micro-Map SDK and SHaRC: NVIDIA Corporation, under NVIDIA's licenses (this software "
                   "contains source code provided by NVIDIA Corporation). NVIDIA, GeForce "
                   "RTX, DLSS and Reflex are trademarks and/or registered trademarks of NVIDIA Corporation. DirectX "
                   "Agility SDK: Microsoft. Ray tracing: ideas from Embark Studios' kajiya (MIT) and Bustanity's "
                   "Phantom FX (MIT). Every license is in the licenses folder.",
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
      {"General", "Psycho V30", "PsychoV31", "Extra", "Extra (Experimental)", "Output (Restart Required)", "About"},
      engine_sections);

  // Graphics 1 of 2's layout (graphics_page.hpp), then Settings Mode's pages.
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders, .sss = &sss_sliders, .rt = &rt_sliders, .ssao_mode = &shader_injection.ssao_mode,
                                             .light_bounce_strength = &shader_injection.light_bounce_strength,
                                             .ssr_hierarchical = &shader_injection.ssr_hierarchical});
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  if (GetEnvironmentVariableA("RENODX_TW_SHOT", nullptr, 0) != 0 || GetEnvironmentVariableA("RENODX_TW_PROFILE", nullptr, 0) != 0
      || GetEnvironmentVariableA("RENODX_TW_SET", nullptr, 0) != 0) {
    reshade::register_event<reshade::addon_event::reshade_present>(OnReshadePresentShot);
  }
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::grass_fade::OnDestroyDevice);

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
                                      sizeof(shader_injection));
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace.
  // Their lighting buffer's ambient cube at c2 (lighting_VS_PS): the paths' sky.
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8);
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::InstallFoliage(FOLIAGE_VERTEX_SHADERS, AFTER_FOLIAGE_VERTEX_SHADERS);
  renodx::utils::pipeline::RegisterOnCreateCallback(OnCreateWater);
  twattila::grass_fade::Install();
  // The AO pass before the ray tracing (its multiply after the pass's own output), both before the Light Bounce (it
  // draws the sun draws itself: no callback after it runs).
  twattila::ao_pass::Install(AO_DRAWS, &shader_injection, sizeof(shader_injection));
  twattila::rt_scene::Install(SCENE_SHADERS, AO_DRAWS);
  twattila::rt_scene::InstallCutOuts(0xF79BFB14);  // rigidcommon ps30_shadow_alpha_test
  for (const uint32_t shader_hash : CAMPAIGN_SHADERS) {
    renodx::utils::command_action::Register(
        OnCampaignDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  // The point lights' ray-traced shadows (their light shaders replaced: shaders/pointlight*).
  twattila::rt_scene::InstallPointLights(twattila::rt_scene::POINT_LIGHT_SHADERS);
  // The battle fog's ray-traced shadows (its shader replaced: shaders/battlefog00).
  twattila::rt_scene::InstallFogShadows(twattila::rt_scene::FOG_SHADERS);
  // Smoke's ray-traced shadows (the lit particles streamed out).
  twattila::rt_scene::InstallSmokeShadows(twattila::rt_scene::PARTICLE_VERTEX_SHADER, twattila::rt_scene::LIT_PARTICLE_PIXEL_SHADER);
  // Ray-Traced Shadows Only: the sun's shadow map's casters skipped (the map the sun draws read); the forward passes that
  // read it (near grass, particles) take the rays' shadows instead.
  twattila::rt_scene::InstallShadowsOnly();
  twattila::screen_space_shadows::PatchForwardShadows(twattila::rt_scene::SHADOW_READER_SHADERS);
  twattila::rt_scene::InstallShadowReaders(twattila::rt_scene::SHADOW_READER_SHADERS);
  for (const WaterShader& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
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
  reshade::unregister_event<reshade::addon_event::reshade_present>(OnReshadePresentShot);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::grass_fade::OnDestroyDevice);
  at_start_stop = true;
  while (!at_start_done.load()) {
    Sleep(1);
  }
  tw::engine_tweaks::Uninstall();
  twattila::graphics_hooks::Uninstall();
  twattila::graphics_hooks::shadow_map::Uninstall();
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  renodx::utils::pipeline::UnregisterOnCreateCallback(OnCreateWater);
  twattila::grass_fade::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::ao_pass::Uninstall();
  twattila::rt_scene::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
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
