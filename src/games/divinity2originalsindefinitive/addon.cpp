/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../talesofarise_dlss/frame_trace.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/draw_dispatch.hpp"
#include "../twattila/psycho35_settings.hpp"
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

bool IsPsychoV35() {
  return shader_injection.tonemap_type == 4.f;
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

bool ShowPsychoV35Settings() {
  return IsAdvancedSettings() && IsPsychoV35();
}

// Hue Clip Emulation's default: 80 as the other per-channel ACES games until test/tw_psychov35 part 6's hue report
// and ATN's eye give D2's own.
constexpr float HUE_CLIP_DEFAULT = 80.f;

// Every image control at its default and Rendering Mode Vanilla; Peak, Paper White, UI, Settings Mode and Output kept.
void ApplyResetPreset() {
  renodx::utils::settings::UpdateSettings({
      {"tonemap_type", 1.f},
      {"aces_exposure", 1.f},
      {"aces_contrast", 1.f},
      {"aces_highlights", 1.f},
      {"aces_shadows", 1.f},
      {"aces_saturation", 1.f},
      {"aces_highlight_saturation", 1.f},
      {"aces_tweaked_exposure", 1.f},
      {"aces_tweaked_contrast", 1.f},
      {"aces_tweaked_highlights", 1.f},
      {"aces_tweaked_shadows", 1.f},
      {"aces_tweaked_saturation", 1.f},
      {"aces_tweaked_highlight_saturation", 1.f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"saturation", 1.f},
      {"cone_response_exponent", 1.f},
      {"fire_color_strength", 0.45f},
      {"fire_color_hue", 0.65f},
      {"aces_tweaked_fire_color_strength", 0.25f},
      {"aces_tweaked_fire_color_hue", 0.60f},
      {"vanilla_fire_color_strength", 0.f},
      {"vanilla_fire_color_hue", 0.75f},
      {"bloom", 1.f},
      {"v35_mid_gray", 0.f},
      {"vanilla_per_channel_shoulder", 1.f},
      {"exact_lut_sampling", 0.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
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
        .labels = {"Off", "Vanilla", "Vanilla+ (Tweaked ACES)", "Vanilla+ (PsychoV30)", "Vanilla+ (PsychoV35)"},
    },

        // Presets //////////////////////////////////////////////////////////////////////////////////////
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended",
        .section = "Presets",
        .group = "preset-buttons-0",
        .tooltip = "Recommended look: Vanilla+ (PsychoV35), Bloom 0.44.\n"
                   "Peak, Paper White, UI, Settings Mode and Output are kept.",
        .on_change = []() {
          ApplyResetPreset();
          renodx::utils::settings::UpdateSettings({{"tonemap_type", 4.f}, {"bloom", 0.44f}});
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-buttons-0",
        .tooltip = "Every image setting at its default (Rendering Mode Vanilla).\n"
                   "Peak, Paper White, UI, Settings Mode and Output are kept.",
        .on_change = []() { ApplyResetPreset(); },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "SDR (monitors without HDR)",
        .section = "Presets",
        .group = "preset-buttons-0",
        .tooltip = "For SDR monitors: Rendering Mode Off (the game's own picture).\n"
                   "Turn HDR off in the game's graphics options too.",
        .tint = 0xFFC107,
        .on_change = []() {
          ApplyResetPreset();
          renodx::utils::settings::UpdateSettings({{"tonemap_type", 0.f}});
        },
    },
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
    new renodx::utils::settings::Setting{
        .key = "vanilla_per_channel_shoulder",
        .binding = &shader_injection.vanilla_per_channel_shoulder,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Per-Channel Highlights",
        .section = "Vanilla",
        .tooltip = "Peak below 1000 nits (at Paper White 203): each colour rolls off on its own, so fire cores turn\n"
                   "orange / white as in SDR. Off = all colours together (fire stays saturated).",
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
        .section = "Highlight Colours (For Tweaked ACES)",
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
    new renodx::utils::settings::Setting{
        .key = "exact_lut_sampling",
        .binding = &shader_injection.exact_lut_sampling,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = "Exact Colour LUT (Experimental)",
        .section = "Extra",
        .tooltip = "The game reads its colour LUT half a step off: mid gray about a fifth darker than the mode sets it.\n"
                   "On: read exactly (Vanilla and Vanilla+ modes; brighter mid tones). Off: the game's own reading.",
        .is_enabled = IsModded,
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

constexpr uint32_t LUT_BUILDER_HASH = 0x7016F292;  // t00, PPComputeLUT (HDR and SDR LUTs)
constexpr uint32_t SIGHT_HASH = 0xA3A4EC44;        // outlines00, EoCSight_HDR: binds PerView b12

// Each replaced shader's first use is logged once (the "applied" lines of a data run); a bit per custom shader.
std::atomic_uint64_t applied_shaders = 0;

// The traced frame (frame_trace: renodx_trace_now next to the exe, RENODX_TW_SET's "trace" or the Advanced button) also
// gets a constant buffer's registers, read through a staging copy (that frame only).
void AppendConstants(reshade::api::command_list* cmd_list, bool compute, UINT slot, UINT registers, const std::string& label) {
  auto* context = talesofarise::frame_trace::internal::NativeContext(cmd_list);
  if (context == nullptr) return;
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (compute) {
    context->CSGetConstantBuffers(slot, 1, &buffer);
  } else {
    context->PSGetConstantBuffers(slot, 1, &buffer);
  }
  if (buffer == nullptr) {
    talesofarise::frame_trace::internal::Append(label + ": not bound");
    return;
  }
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  desc.StructureByteStride = 0;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  context->GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
  if (FAILED(device->CreateBuffer(&desc, nullptr, &staging))) return;
  context->CopyResource(staging.Get(), buffer.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
  const auto* values = static_cast<const float*>(mapped.pData);
  std::string line = label + ":";
  for (UINT c = 0; c < std::min(registers, desc.ByteWidth / 16u); ++c) {
    line += std::format(" c{}=({:.9g} {:.9g} {:.9g} {:.9g})", c, values[c * 4], values[c * 4 + 1], values[c * 4 + 2], values[c * 4 + 3]);
  }
  context->Unmap(staging.Get(), 0);
  talesofarise::frame_trace::internal::Append(line);
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
               uint32_t, const reshade::api::rect*) {
  talesofarise::frame_trace::OnPresent();
}

}  // namespace

namespace divinity2::test_hooks {
// RENODX_TW_PROFILE has nothing to record here (no ray-tracing scene).
inline void StartProfile() {
  renodx::utils::log::w("[Divinity 2] Test: RENODX_TW_PROFILE: no profile in this add-on.");
}
}  // namespace divinity2::test_hooks

#define RENODX_TEST_HOOKS_PROFILE divinity2::test_hooks::StartProfile
#include "../twattila/test_hooks.hpp"

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
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX for Divinity: Original Sin 2 Definitive Edition by ATN";

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

        settings.insert(settings.end(), {
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::BUTTON,
                .label = "Trace One Frame",
                .section = "Debug",
                .tooltip = "Writes the next frame's draws, their formats and the colour LUT's settings to\n"
                           "tales_frame_trace.txt next to the game's exe.",
                .on_change = []() { talesofarise::frame_trace::Request(); },
                .is_visible = IsAdvancedSettings,
            },
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::TEXT,
                .label = "RenoDX, PsychoV30, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom "
                         "PsychoV30 and PsychoV35.",
                .section = "About",
            },
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::TEXT,
                .label = twattila::credits::ACES,
                .section = "About",
            },
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::TEXT,
                .label = "Build Date: " + build_date + " - " + build_time,
                .section = "About",
                .is_visible = IsAdvancedSettings,
            },
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::BUTTON,
                .label = "ATN's Ko-fi",
                .section = "About",
                .group = "about-buttons",
                .tint = 0xFF5A16,
                .on_change = []() { renodx::utils::platform::LaunchURL("https://ko-fi.com/atnthegreat"); },
            },
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::BUTTON,
                .label = "NexusMods",
                .section = "About",
                .group = "about-buttons",
                .tint = 0x2196F3,
                .on_change = []() {
                  renodx::utils::platform::LaunchURL("https://www.nexusmods.com/divinityoriginalsin2definitiveedition/mods/547");
                },
            },
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::BUTTON,
                .label = "GitHub (Downloads)",
                .section = "About",
                .group = "about-buttons",
                .tint = 0x6E7681,
                .on_change = []() { renodx::utils::platform::LaunchURL("https://github.com/arshiatn/renodx/releases"); },
            },
        });

        twattila::psycho35::InsertBefore(
            &settings, "Psycho V30", twattila::psycho35::Rows(&shader_injection, ShowPsychoV35Settings, HUE_CLIP_DEFAULT));
        twattila::psycho35::InsertBefore(
            &settings, "Psycho V30",
            {new renodx::utils::settings::Setting{
                .key = "v35_mid_gray",
                .binding = &shader_injection.v35_mid_gray,
                .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                .default_value = 0.f,
                .label = "Mid Gray",
                .section = "PsychoV35",
                .tooltip = "How bright mid gray is: as in the game's HDR (default) or as in its SDR picture (a little brighter).",
                .labels = {"Game's HDR", "Game's SDR"},
                .is_enabled = ShowPsychoV35Settings,
                .is_visible = ShowPsychoV35Settings,
            }});

        // Each replaced shader's first use goes to the log; on a traced frame the LUT builder's constants (both LUTs)
        // and the view's (at the sight pass) go to the trace.
        uint32_t shader_index = 0;
        for (auto& [hash, shader] : custom_shaders) {
          const uint64_t bit = uint64_t{1} << shader_index++;
          shader.on_draw = [hash, bit](reshade::api::command_list* cmd_list) {
            if ((applied_shaders.load(std::memory_order_relaxed) & bit) == 0 && (applied_shaders.fetch_or(bit) & bit) == 0) {
              renodx::utils::log::i(std::format("[Divinity 2] Replaced shader 0x{:08X} applied (Rendering Mode {:.0f}).", hash,
                                                shader_injection.tonemap_type));
            }
            if (!talesofarise::frame_trace::internal::state.tracing.load(std::memory_order_relaxed)) return true;
            switch (hash) {
              case LUT_BUILDER_HASH:
                AppendConstants(cmd_list, true, 0, 33,
                                "PPComputeLUT cb0 (c0 luminance apply / red / highlights fix, c1-20 grading, c21 ranges, c22-25 "
                                "white balance, c26-32 Params: TsParams, c31.zw / c32.x shaper, c32.y HDR gamma, c32.z output "
                                "device bits: 1.4e-45 = HDR)");
                break;
              case SIGHT_HASH:
                AppendConstants(cmd_list, false, 12, 15,
                                "PerView b12 at EoCSight_HDR (c0-3 View, c4-7 Projection, c8-11 ViewProjection, c12 ViewPos, "
                                "c13 ViewInfo: far, near, width, height, c14 ScaleAndBias)");
                break;
              default:
                break;
            }
            return true;
          };
        }

        reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);  // peak nits
        reshade::register_event<reshade::addon_event::present>(OnPresent);
        twattila::test_hooks::Install(&settings, true, "[Divinity 2]");

        initialized = true;
      }

      break;
    case DLL_PROCESS_DETACH:
      twattila::test_hooks::Uninstall();
      reshade::unregister_event<reshade::addon_event::present>(OnPresent);
      talesofarise::frame_trace::Stop();
      reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);  // peak nits
      reshade::unregister_addon(h_module);
      break;
  }

  renodx::utils::settings::Use(fdw_reason, &settings, nullptr);
  renodx::mods::swapchain::Use(fdw_reason, &shader_injection);
  // The fast draw dispatch with the resource upgrades' gate: attached after mods::shader (command_action), detached
  // before it.
  if (fdw_reason == DLL_PROCESS_DETACH) {
    twattila::draw_dispatch::Use(fdw_reason);
  }
  renodx::mods::shader::Use(fdw_reason, custom_shaders, &shader_injection);
  if (fdw_reason == DLL_PROCESS_ATTACH) {
    twattila::draw_dispatch::Use(fdw_reason);
  }

  return TRUE;
}
