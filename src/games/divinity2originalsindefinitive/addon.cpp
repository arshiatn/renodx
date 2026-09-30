/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/settings.hpp"
#include "./shared.h"

const std::string build_date = __DATE__;
const std::string build_time = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;

bool IsAdvancedSettings() {
  return current_settings_mode >= 1.f;
}

bool IsAcesFix() {
  return shader_injection.tonemap_type == 1.f;
}

bool IsAcesTweaked() {
  return shader_injection.tonemap_type == 2.f;
}

bool IsPsychoV30() {
  return shader_injection.tonemap_type == 3.f;
}

bool IsModded() {
  return shader_injection.tonemap_type != 0;
}

bool ShowACESSettings() {
  return IsAdvancedSettings() && IsAcesFix();
}

bool ShowACEStweakedSettings() {
  return IsAdvancedSettings() && IsAcesTweaked();
}

bool ShowPsychoSettings() {
  return IsAdvancedSettings() && IsPsychoV30();
}


bool ShowPsychoFireHue() {
  return ShowPsychoSettings() && shader_injection.fire_color_strength > 0.f;
}

bool ShowACESFireHue() {
  return ShowACEStweakedSettings() && shader_injection.aces_tweaked_fire_color_strength > 0.f;
}

bool ShowVanillaFireHue() {
  return ShowACESSettings() && shader_injection.vanilla_fire_color_strength > 0.f;
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
      },                                                                                              \
  }

renodx::mods::shader::CustomShaders custom_shaders = {
    __ALL_CUSTOM_SHADERS
};



// Settings //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
renodx::utils::settings::Settings settings = {
    new renodx::utils::settings::Setting{
        .key = "SettingsMode",
        .binding = &current_settings_mode,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .can_reset = false,
        .label = "Settings Mode",
        .labels = {"Simple", "Advanced"},
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
        .key = "peak_white_nits",
        .binding = &shader_injection.peak_white_nits,
        .default_value = 400.f,
        .can_reset = false,
        .label = "Peak",
        .section = "General",
        .min = 400.f,
        .max = 10000.f,
        .is_enabled = IsModded,
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
        .min = 1.f,
        .max = 500.f,
        .is_enabled = IsModded,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "graphics_white_nits",
        .binding = &shader_injection.graphics_white_nits,
        .default_value = 203.f,
        .label = "UI",
        .section = "General",
        .min = 1.f,
        .max = 500.f,
        .is_enabled = IsModded,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "tonemap_type",
        .binding = &shader_injection.tonemap_type,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Rendering Mode",
        .section = "General",
        .tooltip = "Off uses the game's HDR. Vanilla follows the native look with adjustable HDR brightness. Vanilla+ modes change the rendering.",
        .labels = {"Off", "Vanilla", "Vanilla+ (Tweaked ACES)","Vanilla+ (PsychoV30)"},
    },

        // Presets //////////////////////////////////////////////////////////////////////////////////////
    // Neutral reference keeps the user's existing personal presets available.
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (PsychoV30)",
        .section = "Presets",
        .group = "preset-buttons-1",
        .on_change = []() {
            renodx::utils::settings::UpdateSetting("tonemap_type", 3.f);
            renodx::utils::settings::UpdateSetting("exposure", 1.00f);
            renodx::utils::settings::UpdateSetting("highlights", 1.00f);
            renodx::utils::settings::UpdateSetting("shadows", 1.00f);
            renodx::utils::settings::UpdateSetting("contrast", 1.00f);
            renodx::utils::settings::UpdateSetting("saturation", 1.00f);
            renodx::utils::settings::UpdateSetting("cone_response_exponent", 1.00f);
            renodx::utils::settings::UpdateSetting("fire_color_strength", 0.45f);
            renodx::utils::settings::UpdateSetting("fire_color_hue", 0.65f);
            renodx::utils::settings::UpdateSetting("bloom", 0.44f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (Tweaked ACES)",
        .section = "Presets",
        .group = "preset-buttons-1",
        .on_change = []() {
            renodx::utils::settings::UpdateSetting("tonemap_type", 2.f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_exposure", 1.f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_contrast", 0.88f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_highlights", 1.f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_shadows", 1.05f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_saturation", 1.f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_highlight_saturation", 1.f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_fire_color_strength", 0.49f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_fire_color_hue", 0.39f);
            renodx::utils::settings::UpdateSetting("bloom", 0.44f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Closer to Native (PsychoV30)",
        .section = "Presets",
        .group = "preset-buttons-2",
        .on_change = []() {
            renodx::utils::settings::UpdateSetting("tonemap_type", 3.f);
            renodx::utils::settings::UpdateSetting("exposure", 1.00f);
            renodx::utils::settings::UpdateSetting("highlights", 1.00f);
            renodx::utils::settings::UpdateSetting("shadows", 0.95f);
            renodx::utils::settings::UpdateSetting("contrast", 1.06f);
            renodx::utils::settings::UpdateSetting("saturation", 1.00f);
            renodx::utils::settings::UpdateSetting("cone_response_exponent", 1.11f);
            renodx::utils::settings::UpdateSetting("fire_color_strength", 0.50f);
            renodx::utils::settings::UpdateSetting("fire_color_hue", 0.65f);
            renodx::utils::settings::UpdateSetting("bloom", 0.44f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Default everything",
        .section = "Presets",
        .group = "preset-buttons-3",
        .on_change = []() {
            // General
            // renodx::utils::settings::UpdateSetting("tonemap_type", 1.f);

            // Fixed ACES
            renodx::utils::settings::UpdateSetting("aces_exposure", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_contrast", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_highlights", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_shadows", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_saturation", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_highlight_saturation", 1.00f);

            // Tweaked ACES
            renodx::utils::settings::UpdateSetting("aces_tweaked_exposure", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_contrast", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_highlights", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_shadows", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_saturation", 1.00f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_highlight_saturation", 1.00f);

            // Psycho V30
            renodx::utils::settings::UpdateSetting("exposure", 1.00f);
            renodx::utils::settings::UpdateSetting("highlights", 1.00f);
            renodx::utils::settings::UpdateSetting("shadows", 1.00f);
            renodx::utils::settings::UpdateSetting("contrast", 1.00f);
            renodx::utils::settings::UpdateSetting("saturation", 1.00f);
            renodx::utils::settings::UpdateSetting("cone_response_exponent", 1.00f);

            // Reset all three independent warm-highlight corrections.
            renodx::utils::settings::UpdateSetting("fire_color_strength", 0.45f);
            renodx::utils::settings::UpdateSetting("fire_color_hue", 0.65f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_fire_color_strength", 0.25f);
            renodx::utils::settings::UpdateSetting("aces_tweaked_fire_color_hue", 0.60f);
            renodx::utils::settings::UpdateSetting("vanilla_fire_color_strength", 0.f);
            renodx::utils::settings::UpdateSetting("vanilla_fire_color_hue", 0.75f);

            // Extra
            renodx::utils::settings::UpdateSetting("bloom", 1.0f);
        },
    },
    
    // PsychoV30 //////////////////////////////////////////////////////////////////////////////////////
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
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
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
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
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
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
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
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "saturation",
        .binding = &shader_injection.saturation,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Saturation / Purity",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
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
        .is_enabled = IsPsychoV30,
        .is_visible = ShowPsychoSettings,
    },

    

    // FOR FIXED_ACES //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .key = "aces_exposure",
        .binding = &shader_injection.aces_exposure,
        .default_value = 1.0f,
        .label = "Exposure",
        .section = "Vanilla",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACESSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_highlights",
        .binding = &shader_injection.aces_highlights,
        .default_value = 1.0f,
        .label = "Highlights",
        .section = "Vanilla",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACESSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_shadows",
        .binding = &shader_injection.aces_shadows,
        .default_value = 1.0f,
        .label = "Shadows",
        .section = "Vanilla",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACESSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_contrast",
        .binding = &shader_injection.aces_contrast,
        .default_value = 1.0f,
        .label = "Contrast",
        .section = "Vanilla",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACESSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_saturation",
        .binding = &shader_injection.aces_saturation,
        .default_value = 1.0f,
        .label = "Saturation",
        .section = "Vanilla",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACESSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_highlight_saturation",
        .binding = &shader_injection.aces_highlight_saturation,
        .default_value = 1.0f,
        .label = "Highlight Saturation",
        .section = "Vanilla",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACESSettings,
    },


    // FOR TWEAKED ACES //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .key = "aces_tweaked_exposure",
        .binding = &shader_injection.aces_tweaked_exposure,
        .default_value = 1.0f,
        .label = "Exposure",
        .section = "Tweaked ACES",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACEStweakedSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_tweaked_highlights",
        .binding = &shader_injection.aces_tweaked_highlights,
        .default_value = 1.0f,
        .label = "Highlights",
        .section = "Tweaked ACES",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACEStweakedSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_tweaked_shadows",
        .binding = &shader_injection.aces_tweaked_shadows,
        .default_value = 1.0f,
        .label = "Shadows",
        .section = "Tweaked ACES",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACEStweakedSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_tweaked_contrast",
        .binding = &shader_injection.aces_tweaked_contrast,
        .default_value = 1.0f,
        .label = "Contrast",
        .section = "Tweaked ACES",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACEStweakedSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_tweaked_saturation",
        .binding = &shader_injection.aces_tweaked_saturation,
        .default_value = 1.0f,
        .label = "Saturation",
        .section = "Tweaked ACES",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACEStweakedSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_tweaked_highlight_saturation",
        .binding = &shader_injection.aces_tweaked_highlight_saturation,
        .default_value = 1.0f,
        .label = "Highlight Saturation",
        .section = "Tweaked ACES",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.2f",
        .is_visible = ShowACEStweakedSettings,
    },



    // Independent fire controls. Existing unprefixed keys belong to Psycho.
    new renodx::utils::settings::Setting{
        .key = "vanilla_fire_color_strength",
        .binding = &shader_injection.vanilla_fire_color_strength,
        .default_value = 0.f,
        .label = "Fire Colour Correction",
        .section = "Highlight Colours (For Native)",
        .tooltip = "Vanilla only. 0 = off, 1 = full correction. Changes bright reddish/orange colours while preserving luminance. Other bright red materials can also be affected.",
        .min = 0.00f,
        .max = 1.00f,
        .format = "%.2f",
        .is_visible = ShowACESSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "vanilla_fire_color_hue",
        .binding = &shader_injection.vanilla_fire_color_hue,
        .default_value = 0.75f,
        .label = "Fire Hue: Red to Yellow",
        .section = "Highlight Colours (For Native)",
        .tooltip = "Target hue: 0 = red, 1 = yellow. White highlights remain white; the selected peak limits the available colour change.",
        .min = 0.00f,
        .max = 1.00f,
        .format = "%.2f",
        .is_visible = ShowVanillaFireHue,
    },
    new renodx::utils::settings::Setting{
        .key = "fire_color_strength",
        .binding = &shader_injection.fire_color_strength,
        .default_value = 0.45f,
        .label = "Fire Colour Correction",
        .section = "Highlight Colours (For PsychoV30)",
        .tooltip = "PsychoV30 only. 0 = off, 1 = full correction. Changes bright reddish/pinkish colours while preserving luminance. Other bright red materials can also be affected.",
        .min = 0.00f,
        .max = 1.00f,
        .format = "%.2f",
        .is_visible = ShowPsychoSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "fire_color_hue",
        .binding = &shader_injection.fire_color_hue,
        .default_value = 0.65f,
        .label = "Fire Hue: Red to Yellow",
        .section = "Highlight Colours (For PsychoV30)",
        .tooltip = "Target hue: 0 = red, 1 = yellow. White highlights remain white; the selected peak limits the available colour change.",
        .min = 0.00f,
        .max = 1.00f,
        .format = "%.2f",
        .is_visible = ShowPsychoFireHue,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_tweaked_fire_color_strength",
        .binding = &shader_injection.aces_tweaked_fire_color_strength,
        .default_value = 0.25f,
        .label = "Fire Colour Correction",
        .section = "Highlight Colours (For Tweaked ACES)",
        .tooltip = "Tweaked ACES only. 0 = off, 1 = full correction. Changes bright reddish/pinkish colours while preserving luminance. Other bright red materials can also be affected.",
        .min = 0.00f,
        .max = 1.00f,
        .format = "%.2f",
        .is_visible = ShowACEStweakedSettings,
    },
    new renodx::utils::settings::Setting{
        .key = "aces_tweaked_fire_color_hue",
        .binding = &shader_injection.aces_tweaked_fire_color_hue,
        .default_value = 0.60f,
        .label = "Fire Hue: Red to Yellow",
        .section = "Highlight Colours (Tweaked ACES)",
        .tooltip = "Target hue: 0 = red, 1 = yellow. White highlights remain white; the selected peak limits the available colour change.",
        .min = 0.00f,
        .max = 1.00f,
        .format = "%.2f",
        .is_visible = ShowACESFireHue,
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
        .is_visible = IsAdvancedSettings,
    },
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
};


bool initialized = false;

}  // namespace

// DllMain //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
  auto peak = renodx::utils::swapchain::GetPeakNits(swapchain);
  auto white_level = 203.f;
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

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Divinity: Original Sin 2 Definitive Edition by ATN"; //TODO: change me!

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(h_module)) return FALSE;

      if (!initialized) {
        // Shaders cb
        renodx::mods::shader::expected_constant_buffer_space = 50;
        renodx::mods::shader::expected_constant_buffer_index = 10;

        // Swapchain cb
        renodx::mods::swapchain::expected_constant_buffer_index = 10;
        renodx::mods::swapchain::expected_constant_buffer_space = 50;
        
        // Swapchain upgrade settings
        renodx::mods::swapchain::use_resource_cloning = false;
        renodx::mods::swapchain::force_borderless = false;
        renodx::mods::swapchain::prevent_full_screen = false;
        
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

        // // Resource Upgrades
        // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
        //     renodx::mods::swapchain::resource_upgrade_infos.push_back({
        // #else
        //     renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
        // #endif
        //     .old_format = reshade::api::format::r10g10b10a2_unorm,
        //     .new_format = reshade::api::format::r16g16b16a16_float,
        //     .ignore_size = true,
        //     .use_resource_view_cloning = true, //TODO: needed?
        //     .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
        //     .usage_include = reshade::api::resource_usage::render_target,
        // });

        // Appended Settings (HDR10)
        {
            auto* setting = new renodx::utils::settings::Setting{
                .key = "SwapChainEncoding",
                .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                .default_value = 0.f,
                .label = "Output",
                .section = "Output (Restart Required)",
                // .tooltip = "HDR10: 10bit, enough quality (banding?).\nscRGB: 16bit, overkill quality.",
                // .labels = {"HDR10", "scRGB"},
                .tooltip = "HDR10: 10bit",
                .labels = {"HDR10"},
                .is_global = true,
            };
            renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
            auto v = setting->GetValue();
            shader_injection.swap_chain_encoding = v;
            renodx::mods::swapchain::SetUseHDR10(v == 0);
            settings.push_back(setting);
        }

        // Appended Settings (About)
        {
            auto* s1 = new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::TEXT,
                .label = "Build Date: " + build_date + " - " + build_time,
                .section = "About",
            };
            settings.push_back(s1);
        }

        reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);  // peak nits

        initialized = true;
      }

      break;
    case DLL_PROCESS_DETACH:
      reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);  // peak nits
      reshade::unregister_addon(h_module);
      break;
  }

  renodx::utils::settings::Use(fdw_reason, &settings, nullptr);
  renodx::mods::swapchain::Use(fdw_reason, &shader_injection);
  renodx::mods::shader::Use(fdw_reason, custom_shaders, &shader_injection);

  return TRUE;
}
