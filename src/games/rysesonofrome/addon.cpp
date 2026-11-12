/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
// F9 logs one frame of the HUD's flat-shape draws to ryse_ui_draws.txt (how the execution skull was found).
// #define RYSE_UI_DRAW_LOG

#include <d3d11.h>
#include <d3d11_1.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/data.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "./contact_shadows.hpp"
#include "./light_bounce.hpp"
#include "./ray_tracing.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;
// Hotkeys (OnPresentHotkeys), not saved: the HUD shaders also draw the menus.
std::atomic_bool hud_hidden = false;
std::atomic_bool execution_highlights_hidden = false;
std::atomic_bool execution_skull_hidden = false;

bool IsAdvancedSettings() {
  return current_settings_mode >= 1.f;
}

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsExtendedMode() {
  return shader_injection.hdr == 1.f && IsAdvancedSettings();
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f && IsAdvancedSettings();
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 3.f && IsAdvancedSettings();
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, Hable on each channel).
constexpr float HUE_CLIP_DEFAULT = 80.f;

bool IsHdrOutput() {
  return shader_injection.swap_chain_encoding != 2.f;
}

// The fire settings (ATN): Option 1 (also the sliders' defaults, Reset and the Recommended presets) and Option 2, for
// PsychoV31 and for Extended (its own sliders; Dechroma and Highlight Saturation are V31's).
const std::vector<std::pair<std::string, float>> FIRE_OPTION_1 = {
    {"dechroma", 0.f},
    {"highlight_saturation", 1.f},
    {"highlight_hue_shift", 0.03f},
    {"highlight_saturation_shift", 0.1f},
    {"fire_hue", 0.f},
    {"fire_hue_focus", 0.5f},
};
const std::vector<std::pair<std::string, float>> FIRE_OPTION_2 = {
    {"dechroma", 0.f},
    {"highlight_saturation", 1.f},
    {"highlight_hue_shift", 0.11f},
    {"highlight_saturation_shift", 0.07f},
    {"fire_hue", -0.05f},
    {"fire_hue_focus", 0.75f},
};
const std::vector<std::pair<std::string, float>> EXT_FIRE_OPTION_1 = {
    {"ext_highlight_hue_shift", 0.03f},
    {"ext_highlight_saturation_shift", 0.1f},
    {"ext_fire_hue", 0.f},
    {"ext_fire_hue_focus", 0.5f},
};
const std::vector<std::pair<std::string, float>> EXT_FIRE_OPTION_2 = {
    {"ext_highlight_hue_shift", 0.11f},
    {"ext_highlight_saturation_shift", 0.07f},
    {"ext_fire_hue", -0.05f},
    {"ext_fire_hue_focus", 0.75f},
};
// Recommended, also under its deeper shadows twin (ATN 2026-11-12: PsychoV35, its sliders at their defaults).
const std::vector<std::pair<std::string, float>> LESS_POST_PROCESSING = {
    {"tonemapper", 3.f},
    {"film_grain", 0.f},
    {"motion_blur_camera", 0.f},
    {"motion_blur_object", 0.f},
    {"sharpening", 0.25f},
    {"depth_of_field", 2.f},
    {"video_highlights", 0.25f},
};

void ApplyResetPreset() {
  // Peak / Paper White / UI, Settings Mode and output are kept.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 1.f},
      {"ext_exposure", 1.f},
      {"ext_highlights", 1.f},
      {"ext_shadows", 1.f},
      {"ext_contrast", 1.f},
      {"ext_saturation", 1.f},
      {"ext_highlight_saturation", 1.f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"saturation", 1.f},
      {"highlight_contrast", 1.f},
      {"shadow_contrast", 1.f},
      {"flare", 0.f},
      {"gamut_compression", 1.f},
      {"gamut_compression_mode", 1.f},
      {"roll_off", 1.f},
      {"game_shadows", 0.f},
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"sun_shafts", 1.f},
      {"lens_flare", 1.f},
      {"chromatic_aberration_strength", 0.f},
      {"film_grain", 1.f},
      {"sharpening", 0.f},
      {"motion_blur_camera", 1.f},
      {"motion_blur_object", 1.f},
      {"depth_of_field", 1.f},
      {"video_highlights", 0.f},
      {"video_dithering", 1.f},
  });
  renodx::utils::settings::UpdateSettings(FIRE_OPTION_1);
  renodx::utils::settings::UpdateSettings(EXT_FIRE_OPTION_1);
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

// Ray tracing (ray_tracing.hpp): its sliders; installed when on at launch (its scene's shaders are kept from the start).
struct RtSliders {
  float sun_shadows = 0.f;
  float softness = 0.5f;  // degrees of the sun's disk
  float ambient_occlusion = 0.f;
  float ambient_range = 2.f;  // metres
  float ambient_quarter = 0.f;
  float reflections = 0.f;
  float reflection_samples = 1.f;
  float light_bounce = 0.f;
  float rays = 4.f;
  float range = 10.f;  // metres
  float path_tracing = 0.f;
  float bounces = 2.f;
} rt_sliders;
bool ray_tracing_installed = false;

void BuildRuntimeData() {
  custom_shaders = {
      CustomShaderEntry(0xB9B672AA),  // tonemap00 (HDRFinalPass)
      CustomShaderEntry(0xF2D692D1),  // smaa00 (SMAA neighborhood blending)
      CustomShaderEntry(0x121C45E4),  // taa00
      CustomShaderEntry(0xD0B247FF),  // post00 (sharpening, grain)
      CustomShaderEntry(0xB022AFEA),  // post01 (sharpening, grain, lens flares)
      CustomShaderEntry(0xC3A469C1),  // post02 (grain)
      CustomShaderEntry(0xCC6DF78E),  // post03 (grain, lens flares)
      CustomShaderEntry(0xCD8548F6),  // upscale00 (native upscaling)
      CustomShaderEntry(0x799A5EB8),  // mb00 (motion blur velocity pack)
      CustomShaderEntry(0x910E0E0C),  // dof00 (depth of field composite)
      CustomShaderEntry(0x4DFAE399),  // shafts00 (sun shafts mask)
      CustomShaderEntry(0xF1FCED4E),  // shafts01 (sun shafts radial blur)
      CustomShaderEntry(0x37ABF6CF),  // video00
      CustomShaderEntry(0xC1F889A4),  // video01 (alpha)
      CustomShaderEntry(0xD1106E8B),  // ssdo00 (SSDO; Ambient Occlusion GT-VBAO)
      CustomShaderEntry(0xE9264ADE),  // sunshadow00 (sun shadow mask, near cascades; Sun Shadow Softness)
      CustomShaderEntry(0xB5DC6E36),  // sunshadow01
      CustomShaderEntry(0xF2B4EF30),  // sunshadow02 (middle cascade)
      CustomShaderEntry(0xF864A048),  // sunshadow03
      CustomShaderEntry(0xF81C7168),  // sunshadow04 (far cascade)
  };
  // Not replaced; their draws skipped while the hotkeys hide them. Scaleform's text (0x96675B77) and flat shapes
  // (0x1B59F534, in OnDrawUi / OnDrawIndexedUi: the execution skull is one of them): the HUD, also the menus. The
  // execution highlights on enemies: highlight_shaders.
  const auto keep_shader = [](reshade::api::command_list*) { return false; };
  const auto draw_hud = [](reshade::api::command_list*) { return !hud_hidden; };
  custom_shaders[0x96675B77] = {.crc32 = 0x96675B77, .on_replace = keep_shader, .on_draw = draw_hud};
  // The tiled lighting (kept): this frame's ray tracing, then the contact shadows (and the ray-traced sun's) into the
  // sun's shadow mask and the ray-traced ambient into its occlusion before it runs, the traced reflections bound for it.
  const auto contact_shadows = [](reshade::api::command_list* cmd_list) {
    ID3D11ShaderResourceView* sun_visibility = nullptr;
    ID3D11ShaderResourceView* ambient_visibility = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (ray_tracing_installed && cmd_list->get_device()->get_api() == reshade::api::device_api::d3d11) {
      context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&context);
      if (rysesonofrome::ray_tracing::Trace(context)) {
        const auto [sun, ambient] = twattila::rt_scene::Visibilities();
        if (rt_sliders.sun_shadows != 0.f) sun_visibility = sun;
        if (rt_sliders.ambient_occlusion != 0.f) ambient_visibility = ambient;
      }
    }
    rysesonofrome::contact_shadows::Apply(cmd_list,
                                          {
                                              .length = shader_injection.contact_shadow_length,
                                              .thickness = shader_injection.contact_shadow_thickness,
                                              .strength = shader_injection.contact_shadows,
                                              .frame = shader_injection.ssao_frame,
                                          },
                                          sun_visibility, ambient_visibility);
    if (context != nullptr) rysesonofrome::ray_tracing::BindReflections(context);
    return true;
  };
  // After it: the Light Bounce added to the scene it lit.
  const auto light_bounce = [](reshade::api::command_list* cmd_list) {
    const auto power_of_two = [](float exponent) { return static_cast<float>(1u << static_cast<uint32_t>(std::clamp(exponent, 0.f, 3.f))); };
    if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    if (ray_tracing_installed) rysesonofrome::ray_tracing::RestoreReflections(context);
    rysesonofrome::light_bounce::Apply(context,
                                       {
                                           .strength = shader_injection.light_bounce,
                                           .radius = shader_injection.light_bounce_radius,
                                           .thickness = shader_injection.ssao_thickness,
                                           .steps = 8.f * power_of_two(shader_injection.light_bounce_quality),
                                           .slices = power_of_two(shader_injection.light_bounce_directions),
                                           .scale = power_of_two(shader_injection.light_bounce_resolution),
                                           .history = 0.95f,
                                           .frame = shader_injection.ssao_frame,
                                           .bounces = shader_injection.light_bounce_multi,
                                       },
                                       shader_injection.light_bounce_denoise != 0.f);
  };
  custom_shaders[0x897232B5] = {.crc32 = 0x897232B5, .on_replace = keep_shader, .on_draw = contact_shadows, .on_drawn = light_bounce};
  // The screen-space reflections' trace: left out while the traced reflections take its place.
  const auto reflection_trace = [](reshade::api::command_list*) {
    return !ray_tracing_installed || rysesonofrome::ray_tracing::OnReflectionTrace();
  };
  custom_shaders[0xE0B7E115] = {.crc32 = 0xE0B7E115, .on_replace = keep_shader, .on_draw = reflection_trace};
  // The game's copy of its HDR scene (also other copies; ray_tracing.hpp picks the scene's): the helper's last frame's
  // colour.
  const auto keep_color = [](reshade::api::command_list* cmd_list) {
    if (!ray_tracing_installed || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    rysesonofrome::ray_tracing::KeepColor(context);
  };
  custom_shaders[0x3BA5B162] = {.crc32 = 0x3BA5B162, .on_replace = keep_shader, .on_drawn = keep_color};

  // 0-max slider (default 2), 1 = the game's.
  auto slider = [](const std::string& key, float* binding, const std::string& label, const std::string& section,
                   const std::string& tooltip, bool (*is_enabled)(), bool (*is_visible)(), float default_value = 1.f,
                   float max = 2.f) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .min = 0.f,
        .max = max,
        .format = "%.2f",
        .is_enabled = is_enabled,
        .is_visible = is_visible,
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
          .labels = {"Simple", "Advanced"},
          .is_global = true,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Hotkeys (reset at start): F6 / Caps Lock: HUD (menus too). F7 / Tab: execution highlights.\n"
                   "F5: execution skull. Controller: map them in Steam Input (e.g. back buttons).",
          .section = "General",
      },
      new renodx::utils::settings::Setting{
          .key = "peak_white_nits",
          .binding = &shader_injection.peak_white_nits,
          .default_value = 1000.f,
          .can_reset = false,
          .label = "Peak",
          .section = "General",
          .tooltip = "Maximum brightness output in nits.",
          .min = 400.f,
          .max = 10000.f,
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .key = "diffuse_white_nits",
          .binding = &shader_injection.diffuse_white_nits,
          .default_value = 203.f,
          .label = "Paper White",
          .section = "General",
          .tooltip = "Brightness of the scene/game and videos.",
          .min = 1.f,
          .max = 500.f,
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
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .key = "tonemapper",
          .binding = &shader_injection.hdr,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Rendering Mode",
          .section = "General",
          .tooltip = "SDR: the game's native SDR tonemapper.\n"
                     "HDR (Extended): the game's native Filmic (Uncharted 2) tonemapper, extended into HDR\n"
                     "(UC2 Extended). As close as possible to the original SDR look (default).\n"
                     "HDR (PsychoV31): replaces the Filmic tonemapper with PsychoV31, keeping the game's\n"
                     "grading (exposure, bloom, color balance, sun shafts, color charts).\n"
                     "HDR (PsychoV35): the same with Musa's PsychoV35 and Hue Clip Emulation (Recommended).",
          .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (All Post Processing On)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV35), with all vanilla post processing, \n"
                     "hidden chromatic aberration, sharpening at 0.5, depth of field at 2 and Video AutoHDR at 0.25.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings({
                {"tonemapper", 3.f},
                {"film_grain", 0.25f},
                {"motion_blur_camera", 0.20f},
                {"motion_blur_object", 0.50f},
                {"chromatic_aberration_strength", 0.35f},
                {"depth_of_field", 2.f},
                {"sharpening", 0.5f},
                // {"sun_shafts", 0.5f},
                {"video_highlights", 0.25f},
            });
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV35), no film grain, no motion blur,\n"
                     "no chromatic aberration. Sharpening at 0.25, depth of field at 2, Video AutoHDR at 0.25.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings(LESS_POST_PROCESSING);
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (with Deeper Shadows)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "As Recommended, with deeper shadows closer to the game's\n"
                     "(PsychoV35 Shadows 0.75; in PsychoV31 Shadows 0.75, Flare 0.25).",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings(LESS_POST_PROCESSING);
            renodx::utils::settings::UpdateSettings({{"shadows", 0.75f}, {"flare", 0.25f}, {"v35_shadows", 0.75f}});
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "Resets image controls to their defaults (HDR Extended).\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() { ApplyResetPreset(); },
      },
      // Extended ////////////////////////////////////////////////////////////////////////////////////
      slider("ext_exposure", &shader_injection.ext_exposure, "Exposure", "Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_highlights", &shader_injection.ext_highlights, "Highlights", "Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_shadows", &shader_injection.ext_shadows, "Shadows", "Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_contrast", &shader_injection.ext_contrast, "Contrast", "Extended",
             "1 = the game's own contrast.", IsExtendedMode, IsExtendedMode),
      slider("ext_saturation", &shader_injection.ext_saturation, "Saturation / Purity", "Extended",
             "1 = like SDR.", IsExtendedMode, IsExtendedMode),
      slider("ext_highlight_saturation", &shader_injection.ext_highlight_saturation, "Highlight Saturation",
             "Extended", "How much brighter colors desaturate. 0 = more, 1 = default, 2 = less.", IsExtendedMode,
             IsExtendedMode),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too yellow, green or pink? Pick one or change the settings below.",
          .section = "Extended",
          .tint = 0xFFD23C,
          .is_visible = IsExtendedMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Option 1 (Default)",
          .section = "Extended",
          .group = "extended-fire",
          .tooltip = "Highlight Hue Shift 0.03, Highlight Saturation Shift 0.10, Fire Hue Focus 0.50.",
          .on_change = []() { renodx::utils::settings::UpdateSettings(EXT_FIRE_OPTION_1); },
          .is_visible = IsExtendedMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Option 2",
          .section = "Extended",
          .group = "extended-fire",
          .tooltip = "Highlight Hue Shift 0.11, Highlight Saturation Shift 0.07, Fire Hue -0.05, Fire Hue Focus 0.75.",
          .on_change = []() { renodx::utils::settings::UpdateSettings(EXT_FIRE_OPTION_2); },
          .is_visible = IsExtendedMode,
      },
      slider("ext_highlight_hue_shift", &shader_injection.ext_highlight_hue_shift, "Highlight Hue Shift", "Extended",
             "Bright colors take the game's SDR hues: fire more orange, less yellow / green or pink.\n"
             "0 = off, 1 = SDR hues.",
             IsExtendedMode, IsExtendedMode, 0.03f, 1.f),
      slider("ext_highlight_saturation_shift", &shader_injection.ext_highlight_saturation_shift,
             "Highlight Saturation Shift", "Extended",
             "Gives bright colors the saturation of the game's SDR look.\n0 = off, 1 = SDR's.", IsExtendedMode,
             IsExtendedMode, 0.1f, 1.f),
      new renodx::utils::settings::Setting{
          .key = "ext_fire_hue",
          .binding = &shader_injection.ext_fire_hue,
          .default_value = 0.f,
          .label = "Fire Hue (OkLCh)",
          .section = "Extended",
          .tooltip = "Rotates the OkLCh hue of bright warm colors (fire): below 0 redder, above 0 more yellow.\n"
                     "0 = off.",
          .min = -1.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsExtendedMode,
          .is_visible = IsExtendedMode,
      },
      slider("ext_fire_hue_focus", &shader_injection.ext_fire_hue_focus, "Fire Hue Focus (OkLCh)", "Extended",
             "Pulls pink and yellow fire toward orange (OkLCh hue). 0 = off, 1 = all orange.", IsExtendedMode,
             IsExtendedMode, 0.5f, 1.f),

      // PsychoV31 ///////////////////////////////////////////////////////////////////////////////////
      slider("exposure", &shader_injection.exposure, "Exposure", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("highlights", &shader_injection.highlights, "Highlights", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("shadows", &shader_injection.shadows, "Shadows", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("contrast", &shader_injection.contrast, "Contrast", "PsychoV31", "1 = PsychoV31's own.", IsPsychoMode,
             IsPsychoMode),
      slider("highlight_contrast", &shader_injection.highlight_contrast, "Highlight Contrast", "PsychoV31",
             "Contrast above mid gray. 1 = PsychoV31's own.", IsPsychoMode, IsPsychoMode),
      slider("shadow_contrast", &shader_injection.shadow_contrast, "Shadow Contrast", "PsychoV31",
             "Contrast below mid gray. 1 = PsychoV31's own.", IsPsychoMode, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "flare",
          .binding = &shader_injection.flare,
          .default_value = 0.f,
          .label = "Flare",
          .section = "PsychoV31",
          .tooltip = "Deepens the darkest shadows. 0 = off.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .parse = [](float value) { return std::pow(value, 5.f); },
          .is_visible = IsPsychoMode,
      },
      slider("saturation", &shader_injection.saturation, "Saturation / Purity", "PsychoV31", "1 = PsychoV31's own.",
             IsPsychoMode, IsPsychoMode),
      slider("roll_off", &shader_injection.roll_off, "Highlight Roll-off", "PsychoV31",
             "How soon highlights start to compress toward Peak.\n0 = latest, 1 = default, 2 = soonest.",
             IsPsychoMode, IsPsychoMode),
      slider("gamut_compression", &shader_injection.gamut_compression, "Gamut Compression", "PsychoV31",
             "Pulls colors your display can't show back inside its color space.\n"
             "0 = off (the display clips them), 1 = full (default).",
             IsPsychoMode, IsPsychoMode, 1.f, 1.f),
      new renodx::utils::settings::Setting{
          .key = "gamut_compression_mode",
          .binding = &shader_injection.gamut_compression_mode,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Gamut Compression Mode",
          .section = "PsychoV31",
          .tooltip = "The color space colors are fitted into.\n"
                     "BT.2020: the whole HDR signal (default).\n"
                     "Display P3: what most HDR monitors and TVs show. BT.709: SDR colors.",
          .labels = {"BT.709", "BT.2020", "Display P3"},
          .is_enabled = []() { return IsPsychoMode() && IsHdrOutput(); },
          // PsychoV31's modes: 0 BT.709, 1 BT.2020, 3 Display P3.
          .parse = [](float value) { return (value == 2.f ? 3.f : value); },
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .key = "game_shadows",
          .binding = &shader_injection.game_shadows,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Game Shadows (experimental)",
          .section = "PsychoV31",
          .tooltip = "Off: PsychoV31's own shadows.\n"
                     "On: the game's darker shadows (Filmic toe) below mid gray, faded in over the stop\n"
                     "below it.",
          .labels = {"Off", "On"},
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink, too yellow or too pale? Pick one or change the settings below.",
          .section = "PsychoV31",
          .tint = 0xFFD23C,
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Option 1 (Default)",
          .section = "PsychoV31",
          .group = "psychov31-fire",
          .tooltip = "Highlight Hue Shift 0.03, Highlight Saturation Shift 0.10, Fire Hue Focus 0.50.",
          .on_change = []() { renodx::utils::settings::UpdateSettings(FIRE_OPTION_1); },
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Option 2",
          .section = "PsychoV31",
          .group = "psychov31-fire",
          .tooltip = "Highlight Hue Shift 0.11, Highlight Saturation Shift 0.07, Fire Hue -0.05, Fire Hue Focus 0.75.",
          .on_change = []() { renodx::utils::settings::UpdateSettings(FIRE_OPTION_2); },
          .is_visible = IsPsychoMode,
      },
      slider("dechroma", &shader_injection.dechroma, "Dechroma", "PsychoV31",
             "Very bright colors fade toward white, as if overexposed. 0 = off.", IsPsychoMode, IsPsychoMode, 0.f, 1.f),
      slider("highlight_saturation", &shader_injection.highlight_saturation, "Highlight Saturation", "PsychoV31",
             "Saturation of bright colors. 0 = less, 1 = PsychoV31's own (default), 2 = more.", IsPsychoMode,
             IsPsychoMode),
      slider("highlight_hue_shift", &shader_injection.highlight_hue_shift, "Highlight Hue Shift", "PsychoV31",
             "Bright colors take the game's SDR hues: fire more orange / yellow, less pink.\n"
             "0 = off, 1 = SDR hues.",
             IsPsychoMode, IsPsychoMode, 0.03f, 1.f),
      slider("highlight_saturation_shift", &shader_injection.highlight_saturation_shift, "Highlight Saturation Shift",
             "PsychoV31",
             "Gives bright colors the saturation of the game's SDR look (fire cores less pale and pink).\n"
             "0 = off, 1 = SDR's.",
             IsPsychoMode, IsPsychoMode, 0.1f, 1.f),
      new renodx::utils::settings::Setting{
          .key = "fire_hue",
          .binding = &shader_injection.fire_hue,
          .default_value = 0.f,
          .label = "Fire Hue (OkLCh)",
          .section = "PsychoV31",
          .tooltip = "Rotates the OkLCh hue of bright warm colors (fire): below 0 redder, above 0 more yellow.\n"
                     "0 = off.",
          .min = -1.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      slider("fire_hue_focus", &shader_injection.fire_hue_focus, "Fire Hue Focus (OkLCh)", "PsychoV31",
             "Pulls pink and yellow fire toward orange (OkLCh hue). 0 = off, 1 = all orange.", IsPsychoMode,
             IsPsychoMode, 0.5f, 1.f),

      // Extra (HDR only) /////////////////////////////////////////////////////////////////////////
      slider("bloom", &shader_injection.bloom, "Bloom", "Extra", "Bloom multiplier.", IsHDRMode, IsAdvancedSettings),
      slider("vignette", &shader_injection.vignette, "Vignette", "Extra", "Vignette multiplier.", IsHDRMode,
             IsAdvancedSettings),
      slider("sun_shafts", &shader_injection.sun_shafts, "Sun Shafts", "Extra", "Sun shaft (godray) multiplier.",
             IsHDRMode, IsAdvancedSettings),
      slider("lens_flare", &shader_injection.lens_flare, "Lens Flares", "Extra", "Lens flare multiplier.", IsHDRMode,
             IsAdvancedSettings),
      slider("chromatic_aberration_strength", &shader_injection.chromatic_aberration, "Chromatic Aberration", "Extra",
             "The game's own full-screen chromatic aberration (red and blue color fringes toward the\n"
             "screen edges). The game ships with it off (0).\n"
             "1 = twice the strength the game gives its lens flares' color shift.",
             IsHDRMode, IsAdvancedSettings, 0.f),
      slider("film_grain", &shader_injection.film_grain, "Film Grain", "Extra", "Film grain multiplier.", IsHDRMode,
             IsAdvancedSettings),
      slider("sharpening", &shader_injection.sharpening, "Sharpening", "Extra",
             "Multiplier on the game's sharpening (after TAA).\n0 = off (default), 1 = the game's own.", IsHDRMode,
             IsAdvancedSettings, 0.f),
      slider("motion_blur_camera", &shader_injection.motion_blur_camera, "Motion Blur (Camera)", "Extra",
             "Blur from camera movement. 0 = off.\nThe game's maximum blur length still applies.", IsHDRMode,
             IsAdvancedSettings),
      slider("motion_blur_object", &shader_injection.motion_blur_object, "Motion Blur (Objects)", "Extra",
             "Blur from objects' own movement (characters, weapons). 0 = off.\n"
             "The game's maximum blur length still applies.",
             IsHDRMode, IsAdvancedSettings),
      slider("depth_of_field", &shader_injection.depth_of_field, "Depth of Field", "Extra",
             "Depth of field blur multiplier. 0 = off.", IsHDRMode, IsAdvancedSettings),
      new renodx::utils::settings::Setting{
          .key = "video_highlights",
          .binding = &shader_injection.video_highlights,
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
          .is_visible = IsAdvancedSettings,
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
          .is_enabled = [] { return IsHDRMode() && IsHdrOutput() && shader_injection.video_highlights > 0.f; },
          .is_visible = IsAdvancedSettings,
      },
      // Ambient Occlusion (ssdo00): the game's SSDO or GT-VBAO in its place (ssdo_bitmask.hlsli).
      new renodx::utils::settings::Setting{
          .key = "ssao_mode",
          .binding = &shader_injection.ssao_mode,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ambient Occlusion",
          .section = "Ambient Occlusion",
          .tooltip = "GT-VBAO: contact shading that lets light pass behind thin things (railings, spears, grass)\n"
                     "instead of the game's dark halos. Experimental.",
          .labels = {"Game's (SSDO)", "GT-VBAO"},
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_radius",
          .binding = &shader_injection.ssao_radius,
          .default_value = 1.f,
          .label = "AO Radius",
          .section = "Ambient Occlusion",
          .tooltip = "How far around a point other surfaces shade it (metres).",
          .min = 0.25f,
          .max = 4.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.ssao_mode == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_strength",
          .binding = &shader_injection.ssao_strength,
          .default_value = 1.f,
          .label = "AO Strength",
          .section = "Ambient Occlusion",
          .tooltip = "How dark the shading gets.",
          .min = 0.f,
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = [] { return shader_injection.ssao_mode == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_thickness",
          .binding = &shader_injection.ssao_thickness,
          .default_value = 0.1f,
          .label = "AO Thickness",
          .section = "Ambient Occlusion",
          .tooltip = "How thick things are taken to be: thicker shades more behind them (metres).",
          .min = 0.01f,
          .max = 1.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.ssao_mode == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_quality",
          .binding = &shader_injection.ssao_quality,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "AO Quality",
          .section = "Ambient Occlusion",
          .tooltip = "Samples per side: more catch thin things. Heavier.",
          .labels = {"8", "16", "32", "64"},
          .is_enabled = [] { return shader_injection.ssao_mode == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_directions",
          .binding = &shader_injection.ssao_directions,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "AO Directions",
          .section = "Ambient Occlusion",
          .tooltip = "Directions per pixel: less noise. Heavier.",
          .labels = {"1", "2", "4", "8"},
          .is_enabled = [] { return shader_injection.ssao_mode == 1.f; },
      },
      // Contact Shadows (contact_shadows.hpp): short screen-space shadows toward the sun, into its shadow mask.
      new renodx::utils::settings::Setting{
          .key = "contact_shadows",
          .binding = &shader_injection.contact_shadows,
          .default_value = 0.f,
          .label = "Contact Shadows",
          .section = "Contact Shadows",
          .tooltip = "Small sharp sun shadows where the game's shadow map is too coarse (feet, folds of armour,\n"
                     "stones on the ground). 0 = off. Experimental.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
      },
      new renodx::utils::settings::Setting{
          .key = "contact_shadow_length",
          .binding = &shader_injection.contact_shadow_length,
          .default_value = 0.5f,
          .label = "Contact Shadow Length",
          .section = "Contact Shadows",
          .tooltip = "How far toward the sun each pixel looks for something in the way (metres).",
          .min = 0.1f,
          .max = 2.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.contact_shadows > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "contact_shadow_thickness",
          .binding = &shader_injection.contact_shadow_thickness,
          .default_value = 0.2f,
          .label = "Contact Shadow Thickness",
          .section = "Contact Shadows",
          .tooltip = "How thick things are taken to be: thicker casts more, thinner leaves gaps behind edges (metres).",
          .min = 0.05f,
          .max = 1.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.contact_shadows > 0.f; },
      },
      // Sun Shadow Softness (sunshadow00-04, sun_shadow_pcss.hlsli): contact-hardening sun shadows (PCSS).
      new renodx::utils::settings::Setting{
          .key = "sun_shadow_softness",
          .binding = &shader_injection.sun_shadow_softness,
          .default_value = 0.f,
          .label = "Sun Shadow Softness",
          .section = "Contact Shadows",
          .tooltip = "Sun shadows sharp where things touch and softer farther away (the sun's size in degrees;\n"
                     "the real sun 0.5). 0 = the game's. Experimental.",
          .min = 0.f,
          .max = 5.f,
          .format = "%.2f",
      },
      // Light Bounce (light_bounce.hpp): GT-VBGI after the tiled lighting, added to the scene it lit.
      new renodx::utils::settings::Setting{
          .key = "light_bounce",
          .binding = &shader_injection.light_bounce,
          .default_value = 0.f,
          .label = "Light Bounce",
          .section = "Light Bounce",
          .tooltip = "Light bouncing off what you see onto things around it (colour bleeding, brighter corners).\n"
                     "0 = off. Experimental.",
          .min = 0.f,
          .max = 2.f,
          .format = "%.2f",
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_radius",
          .binding = &shader_injection.light_bounce_radius,
          .default_value = 3.f,
          .label = "Light Bounce Radius",
          .section = "Light Bounce",
          .tooltip = "How far light travels to bounce (metres).",
          .min = 0.5f,
          .max = 10.f,
          .format = "%.1f m",
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_quality",
          .binding = &shader_injection.light_bounce_quality,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Light Bounce Quality",
          .section = "Light Bounce",
          .tooltip = "Samples per direction: finer detail. Heavier.",
          .labels = {"8", "16", "32"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_directions",
          .binding = &shader_injection.light_bounce_directions,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Light Bounce Directions",
          .section = "Light Bounce",
          .tooltip = "Directions per pixel: less noise. Heavier.",
          .labels = {"1", "2", "4", "8"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_resolution",
          .binding = &shader_injection.light_bounce_resolution,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Light Bounce Resolution",
          .section = "Light Bounce",
          .tooltip = "Traced at full, half or quarter resolution (lower is faster, softer).",
          .labels = {"Full", "Half", "Quarter"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_multi",
          .binding = &shader_injection.light_bounce_multi,
          .default_value = 0.f,
          .label = "Multi-Bounce",
          .section = "Light Bounce",
          .tooltip = "Bounced light bouncing again (brighter, softer interiors). 0 = one bounce.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_denoise",
          .binding = &shader_injection.light_bounce_denoise,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Light Bounce Denoiser",
          .section = "Light Bounce",
          .tooltip = "Smooths the bounce along surfaces (less noise, never across edges).",
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      // Ray tracing (ray_tracing.hpp): through renodx-tw-dlaa64.exe; on at launch only (restart).
      new renodx::utils::settings::Setting{
          .key = "RayTracedSunShadows",
          .binding = &rt_sliders.sun_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Sun Shadows (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Sun shadows from rays, with the game's: also where its shadow maps miss things.\n"
                     "Needs an RTX GPU and renodx-tw-dlaa64.exe next to the addon. Restart the game after turning on.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedShadowSoftness",
          .binding = &rt_sliders.softness,
          .default_value = 0.5f,
          .label = "Ray-Traced Shadow Softness",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "The sun's size in degrees: shadows softer away from what casts them. 0: sharp.",
          .min = 0.f,
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = [] { return rt_sliders.sun_shadows != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAmbientOcclusion",
          .binding = &rt_sliders.ambient_occlusion,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Ambient Occlusion (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Shade in corners and under things from rays, with the game's.\n"
                     "Restart the game after turning on.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAmbientRange",
          .binding = &rt_sliders.ambient_range,
          .default_value = 2.f,
          .label = "Ray-Traced AO Range",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "How far its rays look for what shades a point.",
          .min = 0.25f,
          .max = 5.f,
          .format = "%.2f m",
          .is_enabled = [] { return rt_sliders.ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAOQuarter",
          .binding = &rt_sliders.ambient_quarter,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced AO Rate",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Quarter: one pixel of each 2 x 2 traced a frame, in turn (DLSS smooths it). Faster.",
          .labels = {"Every pixel", "Quarter"},
          .is_enabled = [] { return rt_sliders.ambient_occlusion != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedReflections",
          .binding = &rt_sliders.reflections,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Reflections (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Reflections from rays in place of the screen-space ones: also what is off screen.\n"
                     "Restart the game after turning on.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedReflectionSamples",
          .binding = &rt_sliders.reflection_samples,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Ray-Traced Reflection Samples",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Rays per pixel on rough surfaces: less noise, heavier.",
          .min = 1.f,
          .max = 8.f,
          .format = "%d",
          .is_enabled = [] { return rt_sliders.reflections != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounce",
          .binding = &rt_sliders.light_bounce,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Light Bounce (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "The Light Bounce's light found with rays: no guessing behind what you see, light from farther.\n"
                     "Needs the Light Bounce on. Restart the game after turning on.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceRays",
          .binding = &rt_sliders.rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 4.f,
          .label = "Ray-Traced Light Bounce Rays",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Rays per texel: less noise, heavier.",
          .min = 1.f,
          .max = 16.f,
          .format = "%d",
          .is_enabled = [] { return rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceRange",
          .binding = &rt_sliders.range,
          .default_value = 10.f,
          .label = "Ray-Traced Light Bounce Range",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "How far its rays look for light.",
          .min = 1.f,
          .max = 50.f,
          .format = "%.0f m",
          .is_enabled = [] { return rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "PathTracing",
          .binding = &rt_sliders.path_tracing,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path Tracing (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "The Light Bounce's and the reflections' rays go on bouncing: lit by what the screen never saw.\n"
                     "Needs the Light Bounce or the reflections on. Heavy. Restart the game after turning on.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "PathTracingBounces",
          .binding = &rt_sliders.bounces,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 2.f,
          .label = "Path Tracing Bounces",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Bounces past the first. More: brighter shade, heavier.",
          .min = 1.f,
          .max = 4.f,
          .format = "%d",
          .is_enabled = [] { return rt_sliders.path_tracing != 0.f; },
      },
  };
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));
}

// The execution skull above enemies: the largest 0x1B59F534 draw, missing from frames without it (F9 logs, 4K).
// Scaleform triangulates it for its size on screen: 3744 indices far (scale 2.07e-6, alpha 0.5) to 4401-4539 near
// (scale 4.2e-6), about size^0.23. The other HUD shapes stay at or below 3219 at 4K. The cut between, 3500 at 2160
// lines, follows the same law with the back buffer height (lower resolutions: fewer triangles everywhere).
uint32_t execution_skull_min_indices = 3500;  // render thread

#ifdef RYSE_UI_DRAW_LOG
// F9 appends the next frame's 0x1B59F534 draws (indices, VS cCompositeMat, PS cBitmapColorTransform) to
// ryse_ui_draws.txt next to the exe (ReShade.log is replaced at every start). Render thread only.
std::FILE* ui_log = nullptr;
uint32_t logged_ui_draws = 0;
#endif

// The execution highlights on enemies (yellow / blue): every pixel shader with VisionMtlParams in its constant
// buffers (seen: body 0xD2063417, head 0xF495DD17, 0x6B40240A), found as the game creates them, so the material
// permutations not seen yet are covered too. Added on any thread, read on the render thread.
renodx::utils::data::ParallelFlatHashMap<uint32_t, bool, std::shared_mutex> highlight_shaders;

void OnInitPipelineHighlights(reshade::api::device*, reshade::api::pipeline_layout, uint32_t subobject_count,
                              const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline) {
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
    const auto* shader = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
    if (shader == nullptr || shader->code == nullptr) continue;
    if (std::string_view(static_cast<const char*>(shader->code), shader->code_size).find("VisionMtlParams") == std::string_view::npos) {
      continue;
    }
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(static_cast<const uint8_t*>(shader->code), shader->code_size);
    if (highlight_shaders.insert({hash, true}).second) {
      reshade::log::message(reshade::log::level::info, std::format("[Ryse UI] Execution highlight shader 0x{:08X}", hash).c_str());
    }
  }
}

bool IsHiddenHighlight(reshade::api::command_list* cmd_list) {
  return execution_highlights_hidden
         && highlight_shaders.contains(renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader));
}

// Scaleform's flat shapes (0x1B59F534) while the HUD is hidden (the skull is indexed: OnDrawIndexedUi), the execution
// highlights while they are.
bool OnDrawUi(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, uint32_t) {
  if (IsHiddenHighlight(cmd_list)) return true;
  return hud_hidden
         && renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader) == 0x1B59F534;
}

// The skull follows its own hotkey, every other flat shape the HUD's.
bool OnDrawIndexedUi(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count,
                     uint32_t first_index, int32_t vertex_offset, uint32_t) {
  if (IsHiddenHighlight(cmd_list)) return true;
  const bool skip = (index_count >= execution_skull_min_indices ? execution_skull_hidden.load() : hud_hidden.load());
#ifdef RYSE_UI_DRAW_LOG
  if (ui_log == nullptr && !skip) return false;
#else
  if (!skip) return false;
#endif
  if (renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader) != 0x1B59F534) {
    return false;
  }
#ifdef RYSE_UI_DRAW_LOG
  if (ui_log != nullptr) {
    // b0 of both stages read back (stalls, one frame): VS cCompositeMat (4x4), PS cBitmapColorTransform (multiply,
    // add) and cPremultiplyAlpha.
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    ID3D11DeviceContext1* context1 = nullptr;
    std::array<float, 16> vertex_constants = {};
    std::array<float, 12> pixel_constants = {};
    const auto read_constants = [&](bool vertex_stage, std::span<float> values) {
      ID3D11Buffer* buffer = nullptr;
      UINT first_constant = 0;
      UINT constant_count = 0;
      if (vertex_stage) {
        context1->VSGetConstantBuffers1(0, 1, &buffer, &first_constant, &constant_count);
      } else {
        context1->PSGetConstantBuffers1(0, 1, &buffer, &first_constant, &constant_count);
      }
      if (buffer == nullptr) return;
      D3D11_BUFFER_DESC desc = {};
      buffer->GetDesc(&desc);
      ID3D11Device* device = nullptr;
      buffer->GetDevice(&device);
      const D3D11_BUFFER_DESC staging_desc = {
          .ByteWidth = desc.ByteWidth,
          .Usage = D3D11_USAGE_STAGING,
          .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
      };
      ID3D11Buffer* staging = nullptr;
      if (SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, &staging))) {
        context->CopyResource(staging, buffer);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const size_t offset = size_t{first_constant} * 16;
        if (offset < desc.ByteWidth && SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
          std::memcpy(values.data(), static_cast<const std::byte*>(mapped.pData) + offset,
                      std::min(values.size_bytes(), desc.ByteWidth - offset));
          context->Unmap(staging, 0);
        }
        staging->Release();
      }
      device->Release();
      buffer->Release();
    };
    if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {
      read_constants(true, vertex_constants);
      read_constants(false, pixel_constants);
      context1->Release();
    }
    const auto& m = vertex_constants;
    const auto& p = pixel_constants;
    std::fprintf(ui_log,
                 "%u: indices %u, instances %u, first index %u, base vertex %d | mat %g %g %g %g / %g %g %g %g / "
                 "%g %g %g %g / %g %g %g %g | mul %g %g %g %g add %g %g %g %g pm %g\n",
                 logged_ui_draws++, index_count, instance_count, first_index, vertex_offset, m[0], m[1], m[2], m[3],
                 m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15], p[0], p[1], p[2], p[3],
                 p[4], p[5], p[6], p[7], p[8]);
  }
#endif
  return skip;
}

// Polled at Present, while the game window is in front.
std::array<bool, 256> keys_down = {};
bool ui_draw_events = false;  // OnDrawUi / OnDrawIndexedUi registered (Present thread)

// True at the Present where `key` went down.
bool KeyWentDown(int key) {
  const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
  const bool went_down = down && !keys_down[key];
  keys_down[key] = down;
  return went_down;
}

void OnPresentHotkeys(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // GT-VBAO's noise moves every frame (the game's SSDO blur and TAA / DLSS settle it).
  shader_injection.ssao_frame = static_cast<float>((static_cast<uint32_t>(shader_injection.ssao_frame) + 1u) % 4096u);
  if (ray_tracing_installed) {
    const bool bounce = shader_injection.light_bounce > 0.f && (rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f);
    const bool reflections = rt_sliders.reflections != 0.f;
    rysesonofrome::ray_tracing::Update({
        .sun_shadows = rt_sliders.sun_shadows != 0.f,
        .ambient_occlusion = rt_sliders.ambient_occlusion != 0.f,
        .ambient_range = rt_sliders.ambient_range,
        .ambient_quarter = rt_sliders.ambient_quarter != 0.f,
        .sun_softness = rt_sliders.softness,
        .light_bounce = bounce,
        .light_bounce_rays = static_cast<uint32_t>(rt_sliders.rays),
        .light_bounce_range = rt_sliders.range,
        .path_tracing = (bounce || reflections) && rt_sliders.path_tracing != 0.f,
        .path_bounces = static_cast<uint32_t>(rt_sliders.bounces),
        .reflections = reflections,
        .reflection_samples = static_cast<uint32_t>(rt_sliders.reflection_samples),
    });
  }
#ifdef RYSE_UI_DRAW_LOG
  if (ui_log != nullptr) {
    std::fputs("frame end\n", ui_log);
    std::fclose(ui_log);
    ui_log = nullptr;
  }
#endif
  const auto back_buffer = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
  execution_skull_min_indices =
      static_cast<uint32_t>(3500.f * std::pow(static_cast<float>(back_buffer.texture.height) / 2160.f, 0.23f));
  if (GetForegroundWindow() != static_cast<HWND>(swapchain->get_hwnd())) return;
#ifdef RYSE_UI_DRAW_LOG
  if (KeyWentDown(VK_F9)) {
    std::array<wchar_t, MAX_PATH> path = {};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), MAX_PATH);
    std::wstring file(path.data(), length);
    file = file.substr(0, file.find_last_of(L'\\') + 1) + L"ryse_ui_draws.txt";
    if (_wfopen_s(&ui_log, file.c_str(), L"a") == 0) {
      logged_ui_draws = 0;
      std::fprintf(ui_log, "frame start (F9, hud %s, highlights %s, skull %s)\n", (hud_hidden ? "hidden" : "shown"),
                   (execution_highlights_hidden ? "hidden" : "shown"), (execution_skull_hidden ? "hidden" : "shown"));
      reshade::log::message(reshade::log::level::info, "[Ryse UI] F9: logging one frame to ryse_ui_draws.txt");
    }
  }
#endif
  const bool hud_key = KeyWentDown(VK_F6);
  if (KeyWentDown(VK_CAPITAL) || hud_key) {
    hud_hidden = !hud_hidden;
  }
  const bool highlights_key = KeyWentDown(VK_F7);
  if (KeyWentDown(VK_TAB) || highlights_key) {
    execution_highlights_hidden = !execution_highlights_hidden;
  }
  // F5 only: the key above Tab is the game's command key, and F8 is Print on some keyboards.
  if (KeyWentDown(VK_F5)) {
    execution_skull_hidden = !execution_skull_hidden;
  }
  // The draw events only while something is hidden (else every draw of the game went through them).
  bool wanted = hud_hidden || execution_highlights_hidden || execution_skull_hidden;
#ifdef RYSE_UI_DRAW_LOG
  wanted = wanted || ui_log != nullptr;
#endif
  if (wanted != ui_draw_events) {
    ui_draw_events = wanted;
    if (wanted) {
      reshade::register_event<reshade::addon_event::draw>(OnDrawUi);
      reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexedUi);
    } else {
      reshade::unregister_event<reshade::addon_event::draw>(OnDrawUi);
      reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexedUi);
    }
  }
}

// RenoDX makes the swap chain flip model, where Present unbinds the back buffer from the pipeline
// (the game's blt model kept it bound). The menu binds its back buffer once and keeps drawing:
// after the first Present into nothing (black windowed; one frozen frame after an alt-tab, which
// rebinds). Gameplay rebinds every frame. So at the first draw after Present, if the game's last
// binding was the back buffer and nothing is bound, it is bound again. Back buffer binds, current or
// older (kept after a resize), go to RenoDX's FP16 clone of the current one, which the proxy presents.
namespace back_buffer_fix {
struct Data {
  reshade::api::device* device = nullptr;
  std::vector<uint64_t> old_buffers;  // earlier back buffers, the newest last
  reshade::api::resource current = {0u};
  reshade::api::resource target = {0u};  // RenoDX's clone of the current back buffer, else itself
  reshade::api::resource_view rtv = {0u};
  std::vector<uint64_t> logged;
  int lines = 0;
};

Data data;
std::mutex mutex;
std::atomic_bool check_after_present = false;
// Read without the mutex on every bind and draw (the mutex only for back buffer binds): the current and earlier back
// buffers (data.current, data.old_buffers), the immediate context and the game's last binding on it.
constexpr size_t MAX_BACK_BUFFERS = 16;
std::array<std::atomic<uint64_t>, MAX_BACK_BUFFERS> back_buffers = {};
std::atomic<uint64_t> immediate_context = 0u;
std::atomic_bool bound_back_buffer = false;

// Caller holds the mutex: back_buffers from data (the oldest earlier ones dropped beyond MAX_BACK_BUFFERS).
void PublishBackBuffers() {
  if (data.old_buffers.size() >= MAX_BACK_BUFFERS) {
    data.old_buffers.erase(data.old_buffers.begin(), data.old_buffers.end() - (MAX_BACK_BUFFERS - 1));
  }
  size_t index = 0;
  back_buffers[index++].store(data.current.handle, std::memory_order_relaxed);
  for (const uint64_t buffer : data.old_buffers) back_buffers[index++].store(buffer, std::memory_order_relaxed);
  while (index < MAX_BACK_BUFFERS) back_buffers[index++].store(0u, std::memory_order_relaxed);
}

// Caller holds the mutex.
void Log(const char* text) {
  if (data.lines++ < 32) reshade::log::message(reshade::log::level::info, text);
}

// Caller holds the mutex. Logs the first time per key.
void LogOnce(uint64_t key, const char* what, uint64_t from) {
  if (std::find(data.logged.begin(), data.logged.end(), key) != data.logged.end()) return;
  data.logged.push_back(key);
  char text[128];
  std::snprintf(text, sizeof(text), "[Ryse] %s 0x%llx, bound 0x%llx instead", what,
                static_cast<unsigned long long>(from), static_cast<unsigned long long>(data.target.handle));
  Log(text);
}

// Caller holds the mutex.
void DestroyView() {
  if (data.device != nullptr && data.rtv.handle != 0u) data.device->destroy_resource_view(data.rtv);
  data.rtv = {0u};
  data.target = {0u};
}

bool IsBackBuffer(uint64_t resource) {
  return resource != 0u && std::ranges::any_of(back_buffers, [&](const std::atomic<uint64_t>& buffer) {
           return buffer.load(std::memory_order_relaxed) == resource;
         });
}

// Binds our view of the clone (else the current back buffer) to slot 0. Caller holds the mutex.
bool BindTarget(reshade::api::device* device, ID3D11DeviceContext* context,
                ID3D11RenderTargetView* const* views, uint32_t count, ID3D11DepthStencilView* dsv) {
  if (data.device != device || data.current.handle == 0u) return false;
  if (data.rtv.handle == 0u) {
    data.target = data.current;
    renodx::utils::resource::GetResourceInfo(data.current, [&](const renodx::utils::resource::ResourceInfo& info) {
      if (info.clone_enabled && info.clone.handle != 0u) data.target = info.clone;
    });
    const reshade::api::resource_view_desc desc(
        reshade::api::format_to_default_typed(device->get_resource_desc(data.target).texture.format));
    if (!device->create_resource_view(data.target, reshade::api::resource_usage::render_target, desc, &data.rtv)) {
      data.rtv = {0u};
      Log("[Ryse] could not create a view on the back buffer");
      return false;
    }
  }
  ID3D11RenderTargetView* moved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  for (uint32_t i = 0; i < count; ++i) moved[i] = views[i];
  moved[0] = reinterpret_cast<ID3D11RenderTargetView*>(data.rtv.handle);
  context->OMSetRenderTargets(std::max(count, 1u), moved, dsv);
  return true;
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
  const std::scoped_lock lock(mutex);
  if (data.current.handle != 0u) data.old_buffers.push_back(data.current.handle);
  DestroyView();
  data.device = swapchain->get_device();
  data.current = swapchain->get_current_back_buffer();
  std::erase(data.old_buffers, data.current.handle);  // may reuse an old address
  PublishBackBuffers();
  ID3D11DeviceContext* immediate = nullptr;
  reinterpret_cast<ID3D11Device*>(data.device->get_native())->GetImmediateContext(&immediate);
  if (immediate != nullptr) {
    immediate_context.store(reinterpret_cast<uint64_t>(immediate), std::memory_order_relaxed);
    immediate->Release();
  }
  char text[96];
  std::snprintf(text, sizeof(text), "[Ryse] swap chain %s: back buffer 0x%llx", resize ? "resized" : "created",
                static_cast<unsigned long long>(data.current.handle));
  Log(text);
}

// Before a resize: our view must not hold the back buffer.
void OnDestroySwapchain(reshade::api::swapchain*, bool) {
  const std::scoped_lock lock(mutex);
  DestroyView();
}

// A new resource at an old back buffer's address is not that buffer.
void OnInitResource(
    reshade::api::device*, const reshade::api::resource_desc&, const reshade::api::subresource_data*,
    reshade::api::resource_usage, reshade::api::resource resource) {
  if (!IsBackBuffer(resource.handle)) return;
  const std::scoped_lock lock(mutex);
  std::erase(data.old_buffers, resource.handle);
  PublishBackBuffers();
}

void OnBindRenderTargets(
    reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view* rtvs,
    reshade::api::resource_view dsv) {
  auto* device = cmd_list->get_device();
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const uint64_t bound = (count != 0 && rtvs[0].handle != 0u) ? device->get_resource_from_view(rtvs[0]).handle : 0u;
  const bool back_buffer = IsBackBuffer(bound);
  if (cmd_list->get_native() == immediate_context.load(std::memory_order_relaxed)) {
    bound_back_buffer.store(back_buffer, std::memory_order_relaxed);
  }
  if (!back_buffer) return;

  const std::scoped_lock lock(mutex);

  ID3D11RenderTargetView* views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  const uint32_t view_count = std::min<uint32_t>(count, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
  for (uint32_t i = 0; i < view_count; ++i) views[i] = reinterpret_cast<ID3D11RenderTargetView*>(rtvs[i].handle);
  // RenoDX binds the clone itself when it can; then this is the same view.
  if (BindTarget(device, reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()), views, view_count,
                 reinterpret_cast<ID3D11DepthStencilView*>(dsv.handle))) {
    LogOnce(bound, bound == data.current.handle ? "game binds back buffer" : "game binds old back buffer", bound);
  }
}

void OnPresent(
    reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
    uint32_t, const reshade::api::rect*) {
  check_after_present = true;
}

// First draw on the immediate context after Present.
void CheckBound(reshade::api::command_list* cmd_list) {
  if (!check_after_present.load()) return;
  auto* device = cmd_list->get_device();
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  if (cmd_list->get_native() != immediate_context.load(std::memory_order_relaxed)) return;
  if (!check_after_present.exchange(false)) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());

  ID3D11RenderTargetView* views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* dsv = nullptr;
  context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, views, &dsv);
  uint64_t bound = 0u;
  if (views[0] != nullptr) {
    ID3D11Resource* resource = nullptr;
    views[0]->GetResource(&resource);
    bound = reinterpret_cast<uint64_t>(resource);
    if (resource != nullptr) resource->Release();
  }
  uint32_t count = 0;
  while (count < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && views[count] != nullptr) ++count;
  {
    const std::scoped_lock lock(mutex);
    if (bound == 0u && bound_back_buffer.load(std::memory_order_relaxed)) {
      // Unbound by Present: the game still draws to its back buffer.
      if (BindTarget(device, context, views, count, dsv)) LogOnce(1u, "back buffer unbound by Present, game draws to", data.current.handle);
    } else if (IsBackBuffer(bound)) {
      if (BindTarget(device, context, views, count, dsv)) LogOnce(bound, "back buffer still bound", bound);
    }
  }
  for (auto* view : views) {
    if (view != nullptr) view->Release();
  }
  if (dsv != nullptr) dsv->Release();
}

bool OnDraw(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, uint32_t) {
  CheckBound(cmd_list);
  return false;
}

bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  CheckBound(cmd_list);
  return false;
}

void OnDestroyDevice(reshade::api::device* device) {
  rysesonofrome::contact_shadows::Release();
  rysesonofrome::light_bounce::Release();
  if (ray_tracing_installed) {
    twattila::rt_scene::OnDestroyDevice(device);
    rysesonofrome::ray_tracing::Release();
  }
  const std::scoped_lock lock(mutex);
  if (data.device != device) return;
  DestroyView();
  data = {};
  PublishBackBuffers();
  immediate_context.store(0u, std::memory_order_relaxed);
  bound_back_buffer.store(false, std::memory_order_relaxed);
}

}  // namespace back_buffer_fix

bool configured = false;
bool attached = false;
bool addon_registered = false;

}  // namespace

// Lifecycle /////////////////////////////////////////////////////////////////////////////////////////

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  auto peak = renodx::utils::swapchain::GetPeakNits(swapchain);
  if (!peak.has_value()) {
    peak = 1000.f;
  }

  for (auto& setting : settings) {
    if (setting->binding != &shader_injection.peak_white_nits) continue;
    setting->default_value = peak.value();
    setting->max = std::max(peak.value(), setting->max);
    setting->can_reset = true;
    break;
  }
}

// Output: 0 HDR10, 1 scRGB, 2 SDR (10-bit, gamma 2.2).
void SetOutputEncoding(int encoding) {
  // The DLSS addon's frame generation takes no scRGB: HDR10 with it.
  if (bool frame_generation = false; encoding == 1 && reshade::get_config_value(nullptr, "Ryse AA", "FrameGeneration", frame_generation)
                                     && frame_generation) {
    encoding = 0;
  }
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
  if (!auto_output_pending) return false;
  auto_output_pending = false;
  const HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY);
  const auto path = renodx::utils::platform::GetPathInfo(monitor);
  const bool windows_hdr = !path.has_value() || renodx::utils::swapchain::GetHDREnabled(path.value());
  SetOutputEncoding(windows_hdr ? 1 : 2);
  return false;
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Ryse: Son of Rome by ATN";

namespace {

void ConfigureAddon() {
  if (configured) return;

  BuildRuntimeData();

  // Shaders cb
  renodx::mods::shader::expected_constant_buffer_space = 50;
  renodx::mods::shader::expected_constant_buffer_index = 13;

  // Swapchain cb
  renodx::mods::swapchain::expected_constant_buffer_index = 13;
  renodx::mods::swapchain::expected_constant_buffer_space = 50;

  renodx::mods::swapchain::use_resource_cloning = true;
  // Game renders into a persistent FP16 clone of the back buffer. scRGB's default converts the
  // back buffer in place at Present: menus redraw over the last frame, so it was converted again
  // every frame (black/posterized menu, overlay trails).
  renodx::mods::swapchain::swapchain_proxy_compatibility_mode = false;
  // CryEngine caches D3D state too: restore it after the proxy draw.
  renodx::mods::swapchain::swapchain_proxy_revert_state = true;
  renodx::mods::swapchain::force_borderless = false;
  renodx::mods::swapchain::prevent_full_screen = true;

  renodx::mods::swapchain::swap_chain_proxy_shaders = {
      {
          reshade::api::device_api::d3d11,
          {
              .vertex_shader = __swap_chain_proxy_vertex_shader_dx11,
              .pixel_shader = __swap_chain_proxy_pixel_shader_dx11,
          },
      },
  };

  // Post-tonemap frame and everything else RGBA8 at back-buffer aspect, upgraded at creation.
  // No hot swap: CryEngine copies between these and the back buffer (CopyResource), which
  // needs the same format on both sides. sRGB views become FP16 (no decode): the TAA and the
  // upscaler decode by hand. The aspect tolerance takes the game's rounding of its reduced
  // targets (half, quarter, ...: 2960x1665 -> 740x416, off by 1.78 / height): an exact match
  // left the sun shafts' quarter target 8-bit (clipped at white) at DLSS sizes like 77%.
  // Edit. added view cloning to avoid some textures/stuff getting destroyed.
  for (auto format : {
           reshade::api::format::r8g8b8a8_typeless,
           reshade::api::format::r8g8b8a8_unorm,
           reshade::api::format::r8g8b8a8_unorm_srgb,
           reshade::api::format::b8g8r8a8_typeless,
           reshade::api::format::b8g8r8a8_unorm,
           reshade::api::format::b8g8r8a8_unorm_srgb,
           reshade::api::format::r10g10b10a2_typeless,
           reshade::api::format::r10g10b10a2_unorm,
       }) {
    renodx::mods::swapchain::resource_upgrade_infos.push_back({
        .old_format = format,
        .new_format = reshade::api::format::r16g16b16a16_float,
        .use_resource_view_cloning = true,
        .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
        .aspect_ratio_tolerance = 0.03f,
        .usage_include = reshade::api::resource_usage::render_target,
    });
  }

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
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const int value = static_cast<int>(setting->GetValue());
    auto_output_pending = (value == 3);
    SetOutputEncoding(auto_output_pending ? 1 : value);
    settings.push_back(setting);
  }

  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom PsychoV31 "
                   "and PsychoV35. UC2 Extended by Musa and Jon.",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
          .section = "About",
          .is_visible = IsAdvancedSettings,
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
          .label = "NexusMods (For RenoDX + DLSS)",
          .section = "About",
          .group = "line_1",
          .tint = 0x2196F3,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/rysesonofrome/mods/59"); },
      },
  });

  // Section titles: one colour (as Attila's), Output and About their own.
  renodx::utils::settings::section_tints = {
      {"General", 0x7EE08A},
      {"Presets", 0x7EE08A},
      {"Extended", 0x7EE08A},
      {"PsychoV31", 0x7EE08A},
      {"Extra", 0x7EE08A},
      {"Ambient Occlusion", 0x7EE08A},
      {"Contact Shadows", 0x7EE08A},
      {"Output (Restart Required)", 0xFF7FAF},
      {"About", 0xE6C27A},
  };

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  // Ray tracing on at launch: its scene's shaders kept from the start, the Light Bounce's trace through rays.
  ray_tracing_installed = rt_sliders.sun_shadows != 0.f || rt_sliders.ambient_occlusion != 0.f || rt_sliders.reflections != 0.f
                          || rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f;
  if (ray_tracing_installed) {
    rysesonofrome::ray_tracing::Install();
    rysesonofrome::light_bounce::ray_traced_trace = twattila::rt_scene::internal::TraceLightBounce;
  }
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  reshade::register_event<reshade::addon_event::present>(OnPresentHotkeys);
  reshade::register_event<reshade::addon_event::init_pipeline>(OnInitPipelineHighlights);

  // After RenoDX (its back buffer clone exists, its target rewrites ran).
  reshade::register_event<reshade::addon_event::init_swapchain>(back_buffer_fix::OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(back_buffer_fix::OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::init_resource>(back_buffer_fix::OnInitResource);
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(back_buffer_fix::OnBindRenderTargets);
  reshade::register_event<reshade::addon_event::present>(back_buffer_fix::OnPresent);
  reshade::register_event<reshade::addon_event::draw>(back_buffer_fix::OnDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(back_buffer_fix::OnDrawIndexed);
  reshade::register_event<reshade::addon_event::destroy_device>(back_buffer_fix::OnDestroyDevice);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::present>(OnPresentHotkeys);
  reshade::unregister_event<reshade::addon_event::init_pipeline>(OnInitPipelineHighlights);
  reshade::unregister_event<reshade::addon_event::draw>(OnDrawUi);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexedUi);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(back_buffer_fix::OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_swapchain>(back_buffer_fix::OnDestroySwapchain);
  reshade::unregister_event<reshade::addon_event::init_resource>(back_buffer_fix::OnInitResource);
  reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(back_buffer_fix::OnBindRenderTargets);
  reshade::unregister_event<reshade::addon_event::present>(back_buffer_fix::OnPresent);
  reshade::unregister_event<reshade::addon_event::draw>(back_buffer_fix::OnDraw);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(back_buffer_fix::OnDrawIndexed);
  reshade::unregister_event<reshade::addon_event::destroy_device>(back_buffer_fix::OnDestroyDevice);

  if (ray_tracing_installed) {
    rysesonofrome::light_bounce::ray_traced_trace = nullptr;
    rysesonofrome::ray_tracing::Uninstall();
  }
  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);
  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);
  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);

  attached = false;
}

}  // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE) {
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
