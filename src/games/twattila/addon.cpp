/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0
// Threaded Draw Recording, Parallel Instance Fill and Fill With Draw Recording (draw_recording.hpp, instance_fill.hpp) are
// built into this add-on; the other TW add-ons share those headers without them.
#define TWATTILA_DRAW_RECORDING 1

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
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
#include "../../utils/float16.hpp"
#include "../../utils/path.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/resource_upgrade.hpp"
#include "../../utils/settings.hpp"
#include "../talesofarise_dlss/pixel_trace.hpp"
#include "../twrome2/engine_tweaks.hpp"
#include "./graphics_hooks.hpp"
#include "./anti_aliasing.hpp"
#include "./ao_pass.hpp"
#include "./attila64_plugin_link.hpp"
#include "./buffer_pool.hpp"
#include "./cpu_sets.hpp"
#include "./device_loss.hpp"
#include "./draw_dispatch.hpp"
#include "./effect_update.hpp"
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
#include "./pass_timing.hpp"
#include "./rt_scene.hpp"
#include "./screen_space_shadows.hpp"
#include "./shadow_pass.hpp"
#include "./start_files.hpp"
// RENODX_TW_SET's key "memory": the constant buffer ring's own reservations beside the address space.
#define RENODX_TEST_HOOKS_MEMORY twattila::buffer_pool::Reserved
#include "./test_hooks.hpp"
#include "./water_effects.hpp"
#include "./worker_pool.hpp"
#include "./smoke_map.hpp"
#include "./ssr_hiz.hpp"
#include "./shared.h"
#include "./frame_gamma.hlsli"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

// A 64-bit build of this add-on only ever runs under the 64-bit port of Attila (the retail game is 32-bit): there the
// game's code is not empire.retail.dll, and every engine-code path stays off. Every port-only line sits inside
// `if constexpr (ATTILA64)`, so the 32-bit add-on compiles none of it.
constexpr bool ATTILA64 = sizeof(void*) == 8;

// The port: a tone mapper target without an upgrade (a renderer asking for another 8-bit format) keeps the HDR output
// 8-bit. A game target that is already RGBA16F (as on 32-bit) needs no clone. Said once; reset at detach.
std::atomic_bool tone_mapper_target_warned = false;

inline void WarnToneMapperTarget(reshade::api::device* device, reshade::api::resource_view rtv) {
  if (rtv.handle == 0u || tone_mapper_target_warned.load(std::memory_order_relaxed)) return;
  const reshade::api::resource resource = device->get_resource_from_view(rtv);
  if (resource.handle == 0u) return;
  bool upgraded = true;
  reshade::api::resource_desc desc = {};
  renodx::utils::resource::GetResourceInfo(resource, [&](const renodx::utils::resource::ResourceInfo& info) {
    upgraded = info.is_clone || info.clone_target != nullptr
               || info.desc.texture.format == reshade::api::format::r16g16b16a16_float;
    desc = info.desc;
  });
  if (upgraded || tone_mapper_target_warned.exchange(true)) return;
  renodx::utils::log::w("[Total War] 64-bit Attila: the tone mapper's target (format ", static_cast<uint32_t>(desc.texture.format), ", ",
                        desc.texture.width, "x", desc.texture.height, ") has no upgrade");
}

ShaderInjectData shader_injection;
// Settings Mode: 0 HDR & Effects, 1 Graphics 1 of 2 (lighting), 2 Graphics 2 of 2 (engine graphics), 3 CPU (the port
// only).
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

// Modded Auto Exposure: Sensitivity's default, also what the presets with the protection off write (Reset, Recommended,
// SDR): a player turning it on afterwards starts at the row's default.
constexpr float AUTO_EXPOSURE_HEADROOM_DEFAULT = 4.f;

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
  ApplyHDRPreset(0.f, 1.f, 1.00f, 1.f, 0.f, AUTO_EXPOSURE_HEADROOM_DEFAULT, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f, 0.15f);
}

// Recommended: no VFX boost (right for any peak); Output Auto takes it once when Windows HDR comes on after a first start
// without HDR.
void ApplyRecommendedPreset() {
  ApplyHDRPreset(3.f, 1.f, 1.00f, 0.35f, 0.f, AUTO_EXPOSURE_HEADROOM_DEFAULT, 1.f, 1.f, 1.f, 2.f, 0.2f);
}

// The tone mappers' scene view slot (t00 t1, t09 / t10 t0: anti_aliasing.hpp OnToneMapperDraw); hdr_to_screen has none.
constexpr UINT NO_SCENE_SLOT = UINT_MAX;

#define UpgradeRTVReplaceShader(value, scene_slot)                                                    \
  {                                                                                                   \
      value,                                                                                          \
      {                                                                                               \
          .crc32 = value,                                                                             \
          .code = __##value,                                                                          \
          .on_draw = [](auto* cmd_list) {                                                             \
            auto rtvs = renodx::utils::swapchain::GetRenderTargets(cmd_list);                         \
            if constexpr (ATTILA64) {                                                                 \
              for (auto rtv : rtvs) {                                                                 \
                WarnToneMapperTarget(cmd_list->get_device(), rtv);                                    \
              }                                                                                       \
            }                                                                                         \
            bool changed = false;                                                                     \
            for (auto rtv : rtvs) {                                                                   \
              changed = renodx::mods::swapchain::ActivateCloneHotSwap(cmd_list->get_device(), rtv);   \
            }                                                                                         \
            if (changed) {                                                                            \
              renodx::mods::swapchain::FlushDescriptors(cmd_list);                                    \
              renodx::mods::swapchain::RewriteRenderTargets(cmd_list, rtvs.size(), rtvs.data(), {0}); \
            }                                                                                         \
            if constexpr (ATTILA64 && (scene_slot) != NO_SCENE_SLOT) {                                \
              twattila::anti_aliasing::OnToneMapperDraw(cmd_list, scene_slot);                        \
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
      UpgradeRTVReplaceShader(0x8BF6B657, 1),
      UpgradeRTVReplaceShader(0x2C8169CA, NO_SCENE_SLOT),
      UpgradeRTVReplaceShader(0xFEA9F9E2, NO_SCENE_SLOT),
      UpgradeRTVReplaceShader(0xBA446301, NO_SCENE_SLOT),
      UpgradeRTVReplaceShader(0xB7FBF3E2, NO_SCENE_SLOT),
      UpgradeRTVReplaceShader(0xA2D88FCD, NO_SCENE_SLOT),
      UpgradeRTVReplaceShader(0xCC79C76F, NO_SCENE_SLOT),
      UpgradeRTVReplaceShader(0x836B7901, NO_SCENE_SLOT),
      UpgradeRTVReplaceShader(0xF42FDDC6, NO_SCENE_SLOT),
      UpgradeRTVReplaceShader(0x95398068, 0),  // t09 constant tone mapper with bloom
      UpgradeRTVReplaceShader(0x6E20B0EF, 0),  // t10 constant tone mapper
      __ALL_CUSTOM_SHADERS,
  };
  // The point lights with the game's point light shadows (pointlight08-39): the game's bytes but at a traced draw.
  twattila::rt_scene::TakePointShadowLightCodes(&custom_shaders);
  // The reflections' draws (ssr00, ssr01; ssr02-05 their Shader Model 4 / 4.1 builds): the ray-traced reflections, else the
  // depth pyramid (rt_scene.hpp, ssr_hiz.hpp). The game's MSAA builds (ssr06, ssr07) have neither.
  for (const uint32_t hash : {0x66006BEAu, 0x47D75349u, 0x485F33D7u, 0x0AC579F6u, 0x8E734BAAu, 0xC8BD2B2Fu}) {
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
        .tooltip = "The game's gamma 2.0 picture as a gamma 2.2 display shows it (deeper shadows), in every Rendering Mode.\n"
                   "Off: gamma 2.0; Rendering Mode SDR keeps the in-game Gamma.",
        .labels = {"Off (In-game Gamma value)", "On (Gamma 2.2)"},
    },
        new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset to default",
        .group = "preset-line-1",
        .tooltip = "Resets all image tuning to HDR defaults while preserving Peak, Paper White, UI brightness, Settings Mode, and output encoding.",
        .on_change = []() {
          ApplyHDRPreset(3.f, 1.f, 1.00f, 1.f, 0.f, AUTO_EXPOSURE_HEADROOM_DEFAULT, 1.f, 1.f, 1.f, 1.f, 0.f);
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended: no VFX boost",
        .group = "preset-line-1",
        .tooltip = "Resets all image tuning to HDR defaults while preserving Peak, Paper White, UI brightness, Settings Mode, and output encoding.",
        .on_change = []() { ApplyRecommendedPreset(); },
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
        .default_value = AUTO_EXPOSURE_HEADROOM_DEFAULT,
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

// Graphics (Experimental): the sliders, through the LOD pick (object detail: engine_code's site), Attila's unit hook
// (graphics_hooks.hpp) and engine tweaks (filled in ConfigureAddon).
std::array<tw::engine_tweaks::Option, 4> unit_detail;  // foot soldiers, cavalry, elephants, artillery
float big_battle_fix = 0.f;
float small_soldiers_as_sprites = 0.f;
float sprite_size = 20.f;  // pixels on screen
float ssr_denoiser = 1.f;  // ssr_hiz.hpp: the resolve's history
float path_reflection_occlusion = 1.f;  // Path-Traced Reflection Occlusion (ssr00 / ssr01's reflection_occlusion)
float shadow_map_size = 0.f;  // the game's sizes x 2^this (graphics_hooks.hpp shadow_map)
float shadow_cascades_row = 0.f;  // Two Shadow Cascades Under Rays (graphics_hooks.hpp shadow_map)
float shadow_pass_row = 0.f;  // Skip the Game's Shadow Map Work (shadow_pass.hpp)
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

// The 64-bit port's state (ATTILA64), reset at detach: the loader's probe device and a plugin's test device each attach
// and detach the add-on before the game's device, and a kept image must not hand the next attach their values.
// The APIs whose devices the add-on left alone, one log line each (bit: the API's value >> 12).
std::atomic<uint32_t> stood_down_apis = 0;
// The game's image read at the first present (once).
std::atomic_bool identity_read = false;
// Engine Performance Fix's FrameBridge upscaling and its waving flags as this start has them (read with the image).
std::atomic_bool framebridge_on = false;
std::atomic_bool waving_flags_on = false;
// The loader's present picture (its resource handle) once its clone is on, and its no-upgrade warning (once).
std::atomic<uint64_t> present_picture = 0;
std::atomic_bool picture_warned = false;
// The loader's overlay converted: one line for each target (buffer 0, the picture).
std::atomic_bool buffer_overlay_logged = false;
std::atomic_bool picture_overlay_logged = false;
// Fewer Constant Uploads' on line (once).
std::atomic_bool fewer_constant_uploads_logged = false;
// The page dump's READY-forced variant (TWATTILA_PAGE_DUMP_PLUGIN_READY): every game-code row shown, as if the plugin
// offered each (its driver publishes a READY block, so the notes and RowsOffered read a real one).
#if defined(TWATTILA_PAGE_DUMP_PLUGIN_READY)
constexpr bool PLUGIN_READY_FORCED = true;
#else
constexpr bool PLUGIN_READY_FORCED = false;
#endif

// The rows the 64-bit engine plugin can bring back: its rows (attila64_plugin::Row). TWEAKS: no hook, the engine values
// alone (shown once its option's values are all found). NONE: hidden on the port.
enum class PluginRow : uint8_t {
  PASS_DRAW = twattila::attila64_plugin::ROW_PASS_DRAW,
  CPU_PLACEMENT = twattila::attila64_plugin::ROW_CPU_PLACEMENT,
  UNIT_LOD = twattila::attila64_plugin::ROW_UNIT_LOD,
  MODEL_DETAIL = twattila::attila64_plugin::ROW_MODEL_DETAIL,
  SHADOW_MAP = twattila::attila64_plugin::ROW_SHADOW_MAP,
  GRASS = twattila::attila64_plugin::ROW_GRASS,
  CAMPAIGN_TREES = twattila::attila64_plugin::ROW_CAMPAIGN_TREES,
  SPRITES = twattila::attila64_plugin::ROW_SPRITES,
  GAME_CODE = twattila::attila64_plugin::ROW_GAME_CODE,
  PARALLEL_EFFECTS = twattila::attila64_plugin::ROW_PARALLEL_EFFECTS,
  BATTLE_STEP = twattila::attila64_plugin::ROW_BATTLE_STEP,
  CONTACT_SPLIT = twattila::attila64_plugin::ROW_CONTACT_SPLIT,
  TWEAKS = 0xFE,
  NONE = 0xFF,
};

// A row whose reader is the 32-bit game's code (engine tweaks, graphics_hooks, engine_code, the worker pool's users,
// shadow_pass, the shadow map hooks, cpu_sets' thread moves, buffer_pool's ring): by key, or by group for a group of
// buttons. The keyless ones are taken by their pointers in ConfigureAddon.
struct GameCodeRow {
  const char* match = "";
  PluginRow plugin_row = PluginRow::NONE;
};

constexpr std::array<GameCodeRow, 42> GAME_CODE_ROWS = {{
    {.match = "GraphicsUnitDetailSmall", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsUnitDetailMedium", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsUnitDetailLarge", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsUnitDetailHuge", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsDeadUnits", .plugin_row = PluginRow::SPRITES},
    {.match = "GraphicsUnitAnimation", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsFacialAnimation", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsHandAnimation", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsUnitVariety", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsFootstepEffects", .plugin_row = PluginRow::UNIT_LOD},
    {.match = "GraphicsObjectDetail", .plugin_row = PluginRow::MODEL_DETAIL},
    {.match = "GraphicsBuildingDetail", .plugin_row = PluginRow::TWEAKS},
    {.match = "GraphicsCampaignDetail", .plugin_row = PluginRow::CAMPAIGN_TREES},
    {.match = "GraphicsCampaignPlants", .plugin_row = PluginRow::TWEAKS},
    {.match = "GraphicsCampaignProps", .plugin_row = PluginRow::TWEAKS},
    // The grass cell row (ROW_GRASS_CELLS) has no row here: it only cuts what the grass row's hooks reach.
    {.match = "GraphicsGrassDistance", .plugin_row = PluginRow::GRASS},
    {.match = "GraphicsGrassDensity", .plugin_row = PluginRow::GRASS},
    {.match = "GraphicsShadowMapSize", .plugin_row = PluginRow::SHADOW_MAP},
    // Hidden on the port until ray-traced shadows: the plugin only checks the cascade sites.
    {.match = "RayTracedShadowCascades"},
    {.match = "GraphicsBigBattleFix"},
    {.match = "GraphicsSmallSoldiersAsSprites", .plugin_row = PluginRow::SPRITES},
    {.match = "GraphicsSpriteSize", .plugin_row = PluginRow::SPRITES},
    {.match = "RayTracedSkipShadowPass", .plugin_row = PluginRow::PASS_DRAW},
    {.match = "FasterGameCpuCode", .plugin_row = PluginRow::GAME_CODE},
    {.match = "FasterRenderLists"},
    {.match = "FasterJobWakeUps"},
    {.match = "FasterEffectPrefetch"},
    // The port's Keep Render Core Free is the plugin's render core (the 32-bit one moves the game's threads itself).
    {.match = "KeepRenderCoreFree", .plugin_row = PluginRow::CPU_PLACEMENT},
    {.match = "FasterEmitterPrefetch"},
    {.match = "FasterParticlePrefetch"},
    {.match = "FasterSweepPrefetch"},
    {.match = "WorkerThreads"},
    {.match = "AnimationRowsWorkers"},
    {.match = "ParallelEffectUpdate"},
    {.match = "ParallelEffectSpawns"},
    {.match = "CachedParticleCount"},
    {.match = "DrawRecordingStage"},
    {.match = "DrawRecordingFamilies"},
    {.match = "ParallelInstanceFill"},
    {.match = "FillWithDrawRecording"},
    // Fewer Driver Waits: its ring needs the 32-bit game's single-threaded device; never comes back on the port.
    {.match = "FewerDriverWaits"},
    // The preset buttons, with the Unit Detail rows: on the port they write only the rows shown (ConfigureAddon's
    // port_row_of).
    {.match = "graphics-presets", .plugin_row = PluginRow::UNIT_LOD},
}};

// A game-code row on the port: shown once the 64-bit engine plugin is ready and offers its row (TWEAKS: no row of its
// own; its values decide).
inline bool GameCodeRowShown(PluginRow row) {
  static_assert(twattila::attila64_plugin::ROW_COUNT <= 32, "a row's bit in a 32-bit mask");
  if constexpr (PLUGIN_READY_FORCED) return true;
  if (row == PluginRow::NONE
      || twattila::attila64_plugin_link::Result() != twattila::attila64_plugin_link::Found::READY) {
    return false;
  }
  return row == PluginRow::TWEAKS
         || (twattila::attila64_plugin_link::RowsOffered() & (1u << static_cast<uint32_t>(row))) != 0;
}

// An engine value option on the port: every value of it found by the plugin's lookup (acquire: FindTweaks' values are
// final). A row bound to it shows, and a hook of the same row applies, only then.
inline bool OptionValuesFound(const tw::engine_tweaks::Option& option) {
  return twattila::attila64_plugin_link::TweaksFound()
         && std::ranges::all_of(option.tweaks,
                                [](const tw::engine_tweaks::Tweak& tweak) { return tweak.value != nullptr; });
}

// A game-code row on the port with the engine value option it is bound to (null: no values of its own): shown once the
// plugin offers its row and every value of the option was found.
struct PortRow {
  PluginRow row = PluginRow::NONE;
  const tw::engine_tweaks::Option* option = nullptr;

  bool Shown() const {
    return GameCodeRowShown(row) && (PLUGIN_READY_FORCED || option == nullptr || OptionValuesFound(*option));
  }
};

// The note rows in graphics_status's place on the port, at most one shown: one per final lookup result, READY's two
// split by the block. A v1 block (an older plugin) shows the update note; a v2 block shows the other one while the
// plugin could not turn on an engine option (attila64_plugin_link::OptionsRefused), whether a settings row reads it or
// not.
struct PluginNote {
  twattila::attila64_plugin_link::Found found = twattila::attila64_plugin_link::Found::PENDING;
  bool v1_block = false;  // READY's note for a v1 block
  const char* label = "";
};
constexpr std::array<PluginNote, 9> PLUGIN_NOTES = {{
    {.found = twattila::attila64_plugin_link::Found::NOT_INSTALLED,
     .label = "Engine options: the 64-bit engine plugin is not installed."},
    {.found = twattila::attila64_plugin_link::Found::PLUGINS_OFF,
     .label = "Engine options: plugins are switched off (ATTILA_NO_PLUGINS / ATTILA_PLAIN)."},
    {.found = twattila::attila64_plugin_link::Found::NOT_LOADED,
     .label = "Engine options: the 64-bit engine plugin is in plugins\\ but did not load (attila64_log.txt)."},
    {.found = twattila::attila64_plugin_link::Found::OFF,
     .label = "Engine options: the 64-bit engine plugin is turned off in ReShade.ini."},
    {.found = twattila::attila64_plugin_link::Found::WRONG_GAME,
     .label = "Engine options: the 64-bit engine plugin does not support this game version."},
    {.found = twattila::attila64_plugin_link::Found::WRONG_LOADER,
     .label = "Engine options: the 64-bit engine plugin does not support this loader version."},
    {.found = twattila::attila64_plugin_link::Found::OTHER_VERSION,
     .label = "Engine options: the 64-bit engine plugin does not match this add-on's version."},
    {.found = twattila::attila64_plugin_link::Found::READY, .v1_block = true,
     .label = "Engine options: update the 64-bit engine plugin for more options."},
    {.found = twattila::attila64_plugin_link::Found::READY,
     .label = "Engine options: the 64-bit engine plugin could not turn on every engine option."},
}};

// The core rows' status lines on the port, after the row's prefix ("Battle core: "), by the word's CoreState. Format
// arguments: {0} / {1} logical CPU A / B, {2} the Win32 error (FAILED only). State 0 (not built) hides the rows. The
// battle word never holds the render part's states (their texts close to the plugin's log). The render row passes its
// own error byte (RENDER_ERROR_LARGE: larger, its FAILED line then points at attila64_log.txt).
constexpr std::array<const char*, twattila::attila64_plugin::CORE_STATE_COUNT> CORE_STATE_TEXTS = {
    "",                                                                  // not built
    "on, waiting for a battle",                                          // CORE_WAITING
    "on: logical CPUs {0} + {1} kept free",                              // CORE_ACTIVE
    "not used: no free CPU core with two threads in the largest cache",  // CORE_NO_SMT
    "not used: too few CPU cores for the game",                          // CORE_FEW_CORES
    "not used: more than 64 CPU threads",                                // CORE_UNSUPPORTED
    "stopped: another program changed the game's CPU cores",             // CORE_YIELDED
    "stopped: Windows refused (error {2})",                              // CORE_FAILED
    "not used: no free core next to the battle core",                    // CORE_NO_CORE
    "not used: the render thread has its own CPU rule",                  // CORE_OWN_RULE
    "off in ReShade.ini (RenderCore=0)",                                 // CORE_OFF_INI
    "not used: the render thread was not found, or it ended",            // CORE_NO_THREAD
    "not used: the render thread was not busy enough to need it",        // CORE_NOT_RENDER
    "stopped with the battle core",                                      // CORE_WITH_BATTLE
};
static_assert(std::ranges::find(CORE_STATE_TEXTS, nullptr) == CORE_STATE_TEXTS.end(), "a text for every CoreState");

// An engine row's status line on the port, by its lane's EngineState (src: PK8 F1 item 7, one table indexed by
// EngineState). State 0 (not built) hides the row.
using EngineTexts = std::array<const char*, twattila::attila64_plugin::ENGINE_CODE_DIFFERS + 1>;
// Faster Battle Step's, after "Battle step: " (the texts PK8 11.1's).
constexpr EngineTexts BATTLE_STEP_TEXTS = {
    "",                                                            // not built
    "not used yet",                                                // ENGINE_NOT_USED_YET
    "active",                                                      // ENGINE_ACTIVE
    "partly (some battle data is new to it: normal speed there)",  // ENGINE_PARTLY
    "off",                                                         // ENGINE_OFF
    "off (ini)",                                                   // ENGINE_OFF_INI
    "off (game code differs)",                                     // ENGINE_CODE_DIFFERS
};
// Multithreaded Collisions', after "Collisions: " (src: PK8 Stage B 14). deviation: from 14's three reason texts
// ("partly (ships: normal speed)", "partly (too few CPU threads)", "partly (very large battle: normal speed)"),
// because lane B has one PARTLY value and no reason (3.7): every PARTLY reads as 14's Rev 2 / Rev 4 text for the log
// reasons, "partly (normal speed)", true for each reason (the original runs those ticks); the reason is in the
// loader's log.
constexpr EngineTexts CONTACT_SPLIT_TEXTS = {
    "",                         // not built
    "not used yet",             // ENGINE_NOT_USED_YET
    "active",                   // ENGINE_ACTIVE
    "partly (normal speed)",    // ENGINE_PARTLY
    "off",                      // ENGINE_OFF
    "off (ini)",                // ENGINE_OFF_INI
    "off (game code differs)",  // ENGINE_CODE_DIFFERS
};

// Ambient Occlusion Visibility Bitmask (twattila/ao_pass.hpp): the addon's own shader in place of the game's AO
// pass (whose replacement is the game's code only).
constexpr std::array<twattila::ao_pass::AmbientOcclusionDraw, 1> AO_DRAWS = {{
    {.shader_hash = 0xBD389EF1, .bitmask = __ao_bitmask, .upsample = __ao_upsample, .resolve = __ao_resolve,
     .denoise = __ao_denoise, .blurs = {0xB4026AF2, 0x6203C824}},  // ssao00, its blur_x and blur_y
}};

// The Dawnless Days (TDD 1.1.x: its shader pack tdd_pack1_main v1.1.0) rebuilds these game shaders. Ours apply to its
// builds as to the game's: the same logic, or logic our hooks don't copy (they patch, name or mark its draws; TDD's
// change stays in effect). tools/tw/round_notes/mod_shaders/tdd_shaders.md. Its old ocean vertex shader is no twin: its
// own MOTION_SHADERS row (0xF80C6EEE, the motion_*ocean_tdd shaders). A TDD release with other shader bytes needs this
// table, that row and the ocean's check again. Every module registers each twin row on its own.
struct ShaderTwin {
  uint32_t game = 0;
  uint32_t twin = 0;
};
constexpr std::array<ShaderTwin, 36> TDD_TWINS = {{
    // weightedcommon vs30_main, main_4_bones, main_dismember, main_4_bones_dismember (shader models 3, 4.1)
    {.game = 0x5F9CD405, .twin = 0xDC9FBA4A},
    {.game = 0x0ECFE74C, .twin = 0x76682C60},
    {.game = 0xDAC80ECC, .twin = 0x0D8C2A96},
    {.game = 0xC2AA8FDA, .twin = 0x61DED001},
    {.game = 0xA691CFF4, .twin = 0x25839CC5},
    {.game = 0x372628AB, .twin = 0xFF473BE8},
    {.game = 0xEA1AEB0E, .twin = 0xC9F9DBA5},
    {.game = 0x31B66A15, .twin = 0x65C0AE28},
    // rigidterrain geom new / old, custom geom (mat) (shader models 3, 4.1)
    {.game = 0x51FCE4CC, .twin = 0x289AC6F1},
    {.game = 0x2FF76986, .twin = 0x958620C8},
    {.game = 0x6ECB6C09, .twin = 0xBFD9331A},
    {.game = 0x28399869, .twin = 0xC56F019B},
    {.game = 0x6A0D0455, .twin = 0x44EBD517},
    {.game = 0xD8DA0B13, .twin = 0xBC15D489},
    {.game = 0x829CDC4F, .twin = 0x0BF40414},
    {.game = 0xF1C6020E, .twin = 0x7E9E7121},
    {.game = 0xC1B5C13E, .twin = 0x2860172D},  // shroud_plane vs30_shroud_main
    // rigidtreebillboard vs30_main (shader models 3, 4.1, 5)
    {.game = 0x4D50E1C7, .twin = 0x17ABBE84},
    {.game = 0x398BD3DF, .twin = 0x235B26C7},
    {.game = 0xB2585ABA, .twin = 0x5818842F},
    {.game = 0xED1DF335, .twin = 0xAAE52888},  // rigidstandard ps30_gbuffer_alpha_test (capture-point flag)
    {.game = 0xCE6F17A1, .twin = 0x0D90A81D},  // rigidstandard ps41_gbuffer_alpha_test_ms
    {.game = 0xB386A8BC, .twin = 0x16390F4F},  // shadow_map_depth_buffer_analysis ps30_downsize_store_data
    // rigidgrass(wet): gbuffer_no_clip, gbuffer, gbuffer_ms, forward_lit_oit, forward_lit_aoit (grass_fade.hpp's order)
    {.game = 0x7B0FF908, .twin = 0xA196A465},
    {.game = 0x39F985CD, .twin = 0x23A8968D},
    {.game = 0x57591D38, .twin = 0xF348EED4},
    {.game = 0x9FA74069, .twin = 0x0C375991},
    {.game = 0x0F7F49E2, .twin = 0x89A78CBE},
    {.game = 0x1BDE4E9D, .twin = 0x305C940C},
    {.game = 0x5ADA3882, .twin = 0x32E0182A},
    {.game = 0x88D39D8E, .twin = 0x50138B9D},
    {.game = 0x50F6A618, .twin = 0x99808ED0},
    {.game = 0x10FB512C, .twin = 0x153497D2},
    {.game = 0x88CBA801, .twin = 0x348F6F8C},
    {.game = 0x8CE99E1F, .twin = 0x10109427},
    {.game = 0xF2C2DE70, .twin = 0xF411ABD8},
}};

// No hash twice in `rows` (HASH: the rows' hash member, or the row itself).
template <auto HASH = std::identity{}, typename Row, size_t N>
consteval bool DistinctHashes(const std::array<Row, N>& rows) {
  std::array<uint32_t, N> hashes = {};
  std::ranges::transform(rows, hashes.begin(), HASH);
  std::ranges::sort(hashes);
  return std::ranges::adjacent_find(hashes) == hashes.end();
}

// `game_rows`, then each row whose hash (HASH as DistinctHashes') has a twin in TDD_TWINS again with the twin's hash.
// TWIN_COUNT: how many rows get one. A stale count, or a hash twice in the result (a twin among the game rows), fails
// the build.
template <size_t TWIN_COUNT, auto HASH = std::identity{}, typename Row, size_t N>
consteval std::array<Row, N + TWIN_COUNT> WithTwins(const std::array<Row, N>& game_rows) {
  std::array<Row, N + TWIN_COUNT> rows = {};
  std::ranges::copy(game_rows, rows.begin());
  size_t count = N;
  for (Row row : game_rows) {
    auto& hash = std::invoke(HASH, row);
    const auto twin = std::ranges::find(TDD_TWINS, hash, &ShaderTwin::game);
    if (twin == TDD_TWINS.end()) continue;
    if (count == rows.size()) throw "WithTwins: more rows with a twin than TWIN_COUNT";
    hash = twin->twin;
    rows[count++] = row;
  }
  if (count != rows.size()) throw "WithTwins: fewer rows with a twin than TWIN_COUNT";
  if (!DistinctHashes<HASH>(rows)) throw "WithTwins: a hash twice";
  return rows;
}

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl ps30 slm / slm_reflection (standard) and
// skinlm, every shadow method, with the camera at b0 and the G-buffer at t0-t3.
// The game's passes that read render-size textures at the display's size (texture slots), below 100%, and the
// half-size passes drawn whole (their readers: view_share::WHOLE_TEXTURES).
// The overlays' readers: the selection indicators (indicator.hlsl main_to_overlay) and the colour overlays are drawn by
// the main camera into display-sized targets, so below 100% into their render share; the ocean projects their uv in its
// vertex shader, the sun (slm) through directional_light_PS.g_overlay_view_projection and the campaign's sea through its
// vertex shader: no camera chain view_share finds. Their region reads take the share only while the overlay is
// display-sized or a halving (the campaign's map-sized overlay, the battles' 1 x 1 stand-in: as at 100%). Whole-dump census:
// tools/tw/round_notes/bugs_1117/bug4_census.py (DEBUG44 (f): ships' selection as stripes at DLSS Performance).
constexpr auto RENDER_SCALE_READS = WithTwins<1, &twattila::view_share::RegionRead::shader_hash>(
    std::array<twattila::view_share::RegionRead, 50>{{
    {.shader_hash = 0x781681E4, .slots = 0b1},   // luma / bloom downsample
    {.shader_hash = 0xB386A8BC, .slots = 0b11},  // shadow depth analysis
    {.shader_hash = 0xB4026AF2, .slots = 0b11, .whole = true},  // AO blur_x
    {.shader_hash = 0x6203C824, .slots = 0b10, .whole = true},  // AO blur_y
    {.shader_hash = 0x820D451B, .slots = 0b11, .whole = true},  // water refraction (3 channels)
    {.shader_hash = 0x25944B2D, .slots = 0b1, .whole = true},   // water refraction (4 channels)
    // ocean.hlsl: colour_overlay_texture and ui_colour_overlay_texture.
    {.shader_hash = 0x108F6C71, .slots = 0b11000000},   // hardware_gather_5_0 ocean ps30_main_medium
    {.shader_hash = 0x18FF7787, .slots = 0b11000000},   // hardware_gather_4_1 ocean ps30_main_high
    {.shader_hash = 0x1F407A91, .slots = 0b110000000},  // hardware_pcf ocean ps30_main_high
    {.shader_hash = 0x2BD79C89, .slots = 0b110000000},  // hardware_fetch4 ocean ps30_main_high
    {.shader_hash = 0x2BD86A89, .slots = 0b11000000},   // hardware_gather_5_0 ocean ps30_main_high
    {.shader_hash = 0x301DB1B5, .slots = 0b11000000},   // hardware_gather_5_0 ocean ps30_main_low
    {.shader_hash = 0x333ECE21, .slots = 0b11000000},   // hardware_gather_4_1 ocean ps30_main_low
    {.shader_hash = 0x33758F15, .slots = 0b11000000},   // hardware_fetch4 ocean ps30_main_low
    {.shader_hash = 0x6583F5D8, .slots = 0b11000000},   // hardware_fetch4 ocean ps30_main_medium
    {.shader_hash = 0xBEFDC3F9, .slots = 0b11000000},   // hardware_gather_4_1 ocean ps30_main_medium
    {.shader_hash = 0xE31E1F10, .slots = 0b110000000},  // software ocean ps30_main_high
    // directionallight.hlsl slm: s_overlay.
    {.shader_hash = 0x32539E8C, .slots = 0b100},       // hardware_gather_5_0 ps41_slm_highdef_ms
    {.shader_hash = 0x4816852C, .slots = 0b10000000},  // software ps30_slm_highdef
    {.shader_hash = 0x54C48CB4, .slots = 0b1000},      // software ps41_slm_highdef_ms
    {.shader_hash = 0x60D325D6, .slots = 0b100},       // hardware_gather_5_0 ps41_slm_lowdef_ms
    {.shader_hash = 0x66A4E910, .slots = 0b100},       // ps41_slm_highdef_ms
    {.shader_hash = 0x73310E1D, .slots = 0b10000000},  // hardware_fetch4 ps30_slm_lowdef
    {.shader_hash = 0x7B036C94, .slots = 0b1000},      // hardware_pcf ps41_slm_lowdef_ms
    {.shader_hash = 0x837C91EB, .slots = 0b1000000},   // ps30_slm_highdef
    {.shader_hash = 0x95A706E4, .slots = 0b10000000},  // hardware_fetch4 ps30_slm_highdef
    {.shader_hash = 0x9A79AB20, .slots = 0b10000000},  // hardware_pcf ps30_slm_lowdef
    {.shader_hash = 0xB01BD600, .slots = 0b1000000},   // hardware_gather_4_1 ps30_slm_lowdef
    {.shader_hash = 0xB3D8781B, .slots = 0b100},       // ps41_slm_lowdef_ms
    {.shader_hash = 0xB6DA03C5, .slots = 0b1000000},   // hardware_gather_5_0 ps30_slm_lowdef
    {.shader_hash = 0xC64DB947, .slots = 0b1000000},   // hardware_gather_5_0 ps30_slm_highdef
    {.shader_hash = 0xC7E46DDE, .slots = 0b1000},      // hardware_pcf ps41_slm_highdef_ms
    {.shader_hash = 0xCA3AAE00, .slots = 0b1000000},   // ps30_slm_lowdef
    {.shader_hash = 0xCE8427DD, .slots = 0b1000},      // software ps41_slm_lowdef_ms
    {.shader_hash = 0xCF85CE09, .slots = 0b1000},      // hardware_fetch4 ps41_slm_lowdef_ms
    {.shader_hash = 0xDFCD4A25, .slots = 0b1000},      // hardware_fetch4 ps41_slm_highdef_ms
    {.shader_hash = 0xE7BAB48E, .slots = 0b1000000},   // hardware_gather_4_1 ps30_slm_highdef
    {.shader_hash = 0xE86B51A4, .slots = 0b100},       // hardware_gather_4_1 ps41_slm_lowdef_ms
    {.shader_hash = 0xEBFFED86, .slots = 0b100},       // hardware_gather_4_1 ps41_slm_highdef_ms
    {.shader_hash = 0xFAD02810, .slots = 0b10000000},  // software ps30_slm_lowdef
    {.shader_hash = 0xFE8DFAA1, .slots = 0b10000000},  // hardware_pcf ps30_slm_highdef
    // waterplane.hlsl campaign seas: s_overlay.
    {.shader_hash = 0x05A111E1, .slots = 0b100000000},   // hardware_gather_5_0 ps30_campaign_high
    {.shader_hash = 0x370647E9, .slots = 0b100000000},   // hardware_gather_4_1 ps30_campaign_low
    {.shader_hash = 0x450F6992, .slots = 0b100000000},   // hardware_gather_5_0 ps30_campaign_low
    {.shader_hash = 0x82C6EE8B, .slots = 0b100000000},   // hardware_gather_4_1 ps30_campaign_high
    {.shader_hash = 0x875E02F2, .slots = 0b1000000000},  // software ps30_campaign_high
    {.shader_hash = 0xA140EFBA, .slots = 0b100000000},   // hardware_fetch4 ps30_campaign_low
    {.shader_hash = 0xA8168F0C, .slots = 0b1000000000},  // hardware_pcf ps30_campaign_high
    {.shader_hash = 0xE928D4B8, .slots = 0b100000000},   // ps30_campaign_high
    {.shader_hash = 0xF16834C6, .slots = 0b1000000000},  // hardware_fetch4 ps30_campaign_high
}});
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
constexpr auto MOTION_SHADERS = WithTwins<8, &twattila::anti_aliasing::MotionShader::hash>(
    std::array<twattila::anti_aliasing::MotionShader, 33>{{
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
    // The Dawnless Days' old ocean, not a twin (TDD_TWINS' note): the game's without the sink over land, its own motion
    // versions.
    {.hash = 0xF80C6EEE, .motion_shader = __motion_ocean_tdd, .draw_shader = __motion_draw_ocean_tdd},
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
}});

// Ray tracing's scene (twattila/rt_scene.hpp): the main view's terrain (rigidterrain geom / custom geom, shader models 3 and
// 4.1, old and new), rigid objects and tree branches, and their capture versions (world positions).
// Light Through All Banners (Experimental): the unit banners (rigidtextureblend and its wind version: the 3D UI's army_banner_flag
// models' material) and the flags the CPU moves (rigidcommon from a dynamic vertex buffer, drawn from both sides or by the
// capture-point flag's pixel shaders) as thin cloth.
float rays_banner_light_row = 0.f;
// Rarer Objects in the Rays (Experimental): the scene's families drawn in no trace (level B: SCENE_SHADERS' held_row).
float rays_untraced_casters_row = 0.f;
// The capture-point flag: rigidstandard's ps30_gbuffer_alpha_test with vs 347F3DD9 (a pixel trace and names.tsv: level A), its
// ps41_gbuffer_alpha_test_ms twin with vs D830AD9B under MSAA (the shader dump only: level B). The game culls its back faces.
constexpr auto FLAG_PIXEL_SHADERS = WithTwins<1>(std::array<uint32_t, 1>{0xED1DF335});
constexpr auto FLAG_PIXEL_SHADERS_MS = WithTwins<1>(std::array<uint32_t, 1>{0xCE6F17A1});
// Texture LOD Bias on All Textures (Experimental, graphics_page.hpp; Warhammer III's row): the game's wrapping trilinear
// samplers biased too (texture_lod_bias.hpp SetWrappingTrilinear).
float lod_bias_trilinear_row = 0.f;
// Faster Game CPU Code (graphics_page.hpp, twattila/engine_code.hpp): on by default, every routine proven to give the game's
// results (test/tw_engine_code). The 64-bit port: the engine plugin's game code row at the next start, through
// attila64_plugin_link::MirrorRestartValues.
float engine_code_row = 1.f;
// Denoise Before Tone Mapping (Experimental, 64-bit only; graphics_page.hpp's Ray Reconstruction rows): anti_aliasing.hpp
// Settings::rr_before_tone_map.
float rr_before_tone_map_row = 0.f;
// Faster Render Lists (on by default; graphics_page.hpp): engine_code.hpp's render list push site.
float render_lists_row = 1.f;
// Faster Job Wake-Ups (Experimental, off; graphics_page-style row below): engine_code.hpp's job system sites.
float job_wakeups_row = 0.f;
// Fewer Constant Uploads (Experimental; on on the 64-bit port, off on 32-bit): mods::shader::use_native_d3d11_injection
// (once used, the game's device keeps it: Off after a restart).
float fewer_constant_uploads_row = (ATTILA64 ? 1.f : 0.f);
// Fewer Driver Waits (Experimental, off): buffer_pool.hpp, the game's constant buffers through one ring (Off after a
// restart).
float fewer_driver_waits_row = 0.f;
// Faster Effect Prefetch (Experimental, off): engine_code.hpp's effect tree child sum site and particle update loop site.
float effect_prefetch_row = 0.f;
// Keep Render Core Free (32-bit: Experimental, off, cpu_sets.hpp; the port: the 64-bit engine plugin's render core, its
// core_wanted bit, attila64_plugin_link::Update).
constexpr float KEEP_RENDER_CORE_DEFAULT = ((ATTILA64 && twattila::attila64_plugin_link::RENDER_CORE_DEFAULT) ? 1.f : 0.f);
float keep_render_core_row = KEEP_RENDER_CORE_DEFAULT;
// The port's Keep Render Core Free shows only once its A/B measured it (true: a release step).
constexpr bool RENDER_CORE_ROW_MEASURED = false;
// Keep Battle Core Free (the port only, on): the 64-bit engine plugin's battle core, its core_wanted bit
// (attila64_plugin_link::Update).
constexpr float KEEP_BATTLE_CORE_DEFAULT = (twattila::attila64_plugin_link::BATTLE_CORE_DEFAULT ? 1.f : 0.f);
float keep_battle_core_row = KEEP_BATTLE_CORE_DEFAULT;
// Effects on All Cores (the port only, Experimental, Off until the soak: PK9 10): 0 Off, 1 Automatic, 2 On, the 64-bit
// engine plugin's two effect core_wanted bits (attila64_plugin_link::Update).
float effects_on_all_cores_row = 0.f;
// Faster Battle Step (the port only): the 64-bit engine plugin's battle step, its CORE_STEP core_wanted bit
// (attila64_plugin_link::Update). Its default is the plugin's (PK8 11.1: On once S-C passes and ATN says so, Q1).
constexpr float FASTER_BATTLE_STEP_DEFAULT = (twattila::attila64_plugin_link::BATTLE_STEP_DEFAULT ? 1.f : 0.f);
float faster_battle_step_row = FASTER_BATTLE_STEP_DEFAULT;
// src: PK8 F1 item 4 (:228-229): CORE_WANTED_DEFAULT holds CORE_STEP for the S-C build, kept at the release only if
// S-C passes and ATN says on (Q1). deviation: from PK8 11.1, which shows the row, because PK2P P1a's form hides it
// until then: the row and its status line show only once S-C measured it (true: a release step), and the add-on
// writes the plugin's default.
constexpr bool BATTLE_STEP_ROW_MEASURED = false;
// Multithreaded Collisions (the port only, Experimental, Off: PK8 Stage B 14): the 64-bit engine plugin's contact
// split, its CORE_SPLIT core_wanted bit (attila64_plugin_link::Update).
float multithreaded_collisions_row = 0.f;
// deviation: from PK8 Stage B 14, which gives no state before the batch, because PK2P P1a's form hides the row (as
// BATTLE_STEP_ROW_MEASURED does): the row and its status line show only once the batch measured it (true: a release
// step, ATN decides the default), and while hidden the add-on writes no CORE_SPLIT (rp1a_ S1).
constexpr bool CONTACT_SPLIT_ROW_MEASURED = false;
// Loader Frame Cap (the port only, off: ATN 11-26): the 64-bit loader's 240 fps cap at the next start, through the
// engine plugin (attila64_plugin_link::MirrorRestartValues).
float loader_frame_cap_row = 0.f;
// The restart notes' text: UI 4K Mod Compatibility's, and the port's audio and Loader Frame Cap notes.
constexpr const char* RESTART_NOW = "Restart the game now to apply this!";
// Parallel Effect Update (Experimental, off): effect_update.hpp, the game's effect update on the worker pool. Parallel Effect Spawns
// (off, needs the row above): its particle spawns on the pool too. Cached Particle Count (off): engine_code.hpp's visible particle sum
// once per update of a node instead of once per view.
float effect_update_row = 0.f;
float effect_spawns_row = 0.f;
float particle_count_row = 0.f;
// Worker Threads (Experimental): the threads, the render thread included, that worker_pool.hpp's users may use.
float worker_threads_row = 4.f;
// Animation Rows on Workers (Experimental, on): animation_rows.hpp, the game's animation texture fill on the worker pool. A
// bench's RENODX_TW_ANIMATION_CHECK, read with the env generation, makes On a Check (never a player's).
float animation_rows_row = 1.f;
bool animation_check_env = false;
// The env generation: the pool reads RENODX_TW_WORKER_LIMITS and RENODX_TW_POOL_SPIN_US again when it changes (test_hooks'
// "env." set keys bump it after they set a variable).
std::atomic<uint32_t> worker_env_generation = 0;
// A bench's RENODX_TW_WORKER_TRACE, read once at the start: engine_code puts the pending list sweep's trace site in only then, never for a player.
const bool worker_trace_env = (GetEnvironmentVariableA("RENODX_TW_WORKER_TRACE", nullptr, 0) != 0);
// Exit Job Drain (engine_code.hpp 21., a bug fix: no row): a bench's RENODX_TW_EXIT_DRAIN=0, read once at the start, takes it out (the end batch's
// "part Off" run); never a player's.
const bool exit_job_drain_on = [] {
  std::array<char, 2> value = {};
  return !(GetEnvironmentVariableA("RENODX_TW_EXIT_DRAIN", value.data(), static_cast<DWORD>(value.size())) == 1 && value[0] == '0');
}();
// The CPUs the game may use (cpu_sets.hpp), the worker pool's budget (worker_pool.hpp's BudgetWorkers; a bug fix: no row): a
// bench's RENODX_TW_GAME_CPUS=N, read once at the start, sets them (0: not known, no limit: knob studies above K8, the pool as
// before); never a player's. A value that does not parse is said at the attach and ignored.
const std::optional<uint32_t> game_cpus_env = []() -> std::optional<uint32_t> {
  std::array<char, 16> value = {};
  const DWORD length = GetEnvironmentVariableA("RENODX_TW_GAME_CPUS", value.data(), static_cast<DWORD>(value.size()));
  if (length == 0 || length >= value.size()) return std::nullopt;
  uint32_t cpus = 0;
  if (const auto [end, error] = std::from_chars(value.data(), value.data() + length, cpus); error != std::errc() || end != value.data() + length) {
    return std::nullopt;
  }
  return cpus;
}();
// A bench's RENODX_TW_COUNT_CHECK (set at all), read again when the env generation changes: Cached Particle Count also runs the game's
// sum on every call and compares it with its cache; never a player's.
bool particle_count_check_env = false;
uint32_t worker_env_read_generation = ~0u;
// Where the game's own threads start (the job workers in the engine's DLL); the rest is the driver's, audio's, etc.
constexpr std::array<const wchar_t*, 2> GAME_THREAD_MODULES = {nullptr, L"empire.retail.dll"};
// Faster Emitter Prefetch (Experimental, off): engine_code.hpp's emitter update loop site, a row of its own so that a bench
// can decide it apart from Faster Effect Prefetch.
float emitter_prefetch_row = 0.f;
// Faster Particle Prefetch (Experimental, off): engine_code.hpp's particle writer table entry site, a row of its own.
float particle_prefetch_row = 0.f;
// Faster Sweep Prefetch (Experimental, off): engine_code.hpp's pending list sweep site, a row of its own.
float sweep_prefetch_row = 0.f;
#if defined(TWATTILA_DRAW_RECORDING)
// Threaded Draw Recording (Experimental, Workers; draw_recording.hpp): the stage, the four loop groups (bit g of
// draw_recording::Settings::loops, in the order of draw_recording::Group; all on) and the B dispatchers' families. The groups and
// the families are hidden (ReShade.ini only), as are the two fill rows below (parked: slower in the end batch).
float draw_recording_stage_row = 6.f;
std::array<float, 4> draw_recording_group_rows = {1.f, 1.f, 1.f, 1.f};
float draw_recording_families_row = 255.f;
// Parallel Instance Fill (off, hidden): instance_fill.hpp, the ring buffer fills of the draw passes on the worker pool.
float instance_fill_row = 0.f;
// Fill With Draw Recording (off, hidden): the fill's job stays open to the draw dispatcher's cut (instance_fill.hpp's stubs,
// draw_recording.hpp's fold).
float fill_fold_row = 0.f;

// One of the four group rows: its key and label, the tooltip's loop.
renodx::utils::settings::Setting* DrawRecordingGroupRow(const char* key, const char* label, size_t group, const char* tooltip) {
  return new renodx::utils::settings::Setting{
      .key = key,
      .binding = &draw_recording_group_rows[group],
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 1.f,
      .label = label,
      .section = "Graphics (Experimental)",
      .tooltip = tooltip,
      .labels = {"Off", "On"},
      .is_visible = [] { return false; },
  };
}
#endif

constexpr auto SCENE_SHADERS = WithTwins<19, &twattila::rt_scene::SceneShader::hash>(
    std::array<twattila::rt_scene::SceneShader, 44>{{
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
    // Rigid objects with the game's Tessellation on (rigidcommon vs30_displacement, HS 0A3E0133, DS 1C0B3B3A; in no trace): D830AD9B's
    // placement and world position (the same instructions), the patches without the domain shader's displacement, which fades out
    // 5 to 20 m from the camera (test/tw_attila_builds).
    {.hash = 0x9AEE504E, .capture_shader = __rt_capture_rigid_sm41, .rows_shader = __motion_record_rigid_sm41, .patch_triangles = true, .held_row = &rays_untraced_casters_row},
    // Rocks and cliffs blended into the terrain (rigidterrainblend): its depth pass (its material pass has no camera).
    {.hash = 0xE03E161C, .capture_shader = __rt_capture_rigid_terrain_blend_sm3, .rows_shader = __motion_record_rigid_terrain_blend_sm3},
    {.hash = 0x77B993FD, .capture_shader = __rt_capture_branch_sm3, .rows_shader = __motion_record_tree},
    {.hash = 0xB8C3E83D, .capture_shader = __rt_capture_branch_sm41, .rows_shader = __motion_record_tree},
    {.hash = 0x6EEF4062, .capture_shader = __rt_capture_branch_sm5, .rows_shader = __motion_record_tree},
    {.hash = 0x46C616F0, .capture_shader = __rt_capture_leaf_sm3, .rows_shader = __motion_record_tree, .foliage = true},
    {.hash = 0x5B880546, .capture_shader = __rt_capture_leaf_sm41, .rows_shader = __motion_record_tree, .foliage = true},
    {.hash = 0xCABCB9D1, .capture_shader = __rt_capture_leaf_sm5, .rows_shader = __motion_record_tree, .foliage = true},
    // Batched Unit Capture (on by default): the units' compact draws (weightedcommon vs30_main: 2 bones a vertex; main_4_bones: 4;
    // each with or without dismemberment) streamed as their mesh and rows, their corners their capture version's own code in one
    // pass (test/tw_rays_captures B).
    {.hash = 0x5F9CD405, .capture_shader = __rt_capture_unit_sm3, .cut_out_shader = __rt_capture_unit_cut_out_sm3, .dynamic = true,
     .batch_mesh_shader = __rt_batch_unit_mesh, .batch_rows_shader = __rt_batch_unit_rows, .batch_compute = __rt_batch_unit_sm3},
    {.hash = 0x0ECFE74C, .capture_shader = __rt_capture_unit_4_bones_sm3, .cut_out_shader = __rt_capture_unit_4_bones_cut_out_sm3, .dynamic = true,
     .batch_mesh_shader = __rt_batch_unit_4_bones_mesh, .batch_mesh_texels = 3, .batch_rows_shader = __rt_batch_unit_4_bones_rows,
     .batch_compute = __rt_batch_unit_4_bones_sm3},
    {.hash = 0xDAC80ECC, .capture_shader = __rt_capture_unit_dismember_sm3, .cut_out_shader = __rt_capture_unit_dismember_cut_out_sm3, .dynamic = true,
     .batch_mesh_shader = __rt_batch_unit_mesh, .batch_rows_shader = __rt_batch_unit_rows, .batch_compute = __rt_batch_unit_dismember_sm3},
    {.hash = 0xC2AA8FDA, .capture_shader = __rt_capture_unit_4_bones_dismember_sm3, .cut_out_shader = __rt_capture_unit_4_bones_dismember_cut_out_sm3, .dynamic = true,
     .batch_mesh_shader = __rt_batch_unit_4_bones_mesh, .batch_mesh_texels = 3, .batch_rows_shader = __rt_batch_unit_4_bones_rows,
     .batch_compute = __rt_batch_unit_4_bones_dismember_sm3},
    {.hash = 0xA691CFF4, .capture_shader = __rt_capture_unit_sm41, .cut_out_shader = __rt_capture_unit_cut_out_sm41, .dynamic = true,
     .batch_mesh_shader = __rt_batch_unit_mesh, .batch_rows_shader = __rt_batch_unit_rows, .batch_compute = __rt_batch_unit_sm41},
    {.hash = 0x372628AB, .capture_shader = __rt_capture_unit_4_bones_sm41, .cut_out_shader = __rt_capture_unit_4_bones_cut_out_sm41, .dynamic = true,
     .batch_mesh_shader = __rt_batch_unit_4_bones_mesh, .batch_mesh_texels = 3, .batch_rows_shader = __rt_batch_unit_4_bones_rows,
     .batch_compute = __rt_batch_unit_4_bones_sm41},
    {.hash = 0xEA1AEB0E, .capture_shader = __rt_capture_unit_dismember_sm41, .cut_out_shader = __rt_capture_unit_dismember_cut_out_sm41, .dynamic = true,
     .batch_mesh_shader = __rt_batch_unit_mesh, .batch_rows_shader = __rt_batch_unit_rows, .batch_compute = __rt_batch_unit_dismember_sm41},
    {.hash = 0x31B66A15, .capture_shader = __rt_capture_unit_4_bones_dismember_sm41, .cut_out_shader = __rt_capture_unit_4_bones_dismember_cut_out_sm41, .dynamic = true,
     .batch_mesh_shader = __rt_batch_unit_4_bones_mesh, .batch_mesh_texels = 3, .batch_rows_shader = __rt_batch_unit_4_bones_rows,
     .batch_compute = __rt_batch_unit_4_bones_dismember_sm41},
    // Grass placed (its instance rows), swaying within its share, past it still (rt_rows_grass).
    {.hash = 0xF3741248, .capture_shader = __rt_capture_grass_sm5, .rows_shader = __rt_rows_grass, .foliage = true, .grass = true},
    {.hash = 0x01FA9CDE, .capture_shader = __rt_capture_grass_sm4, .rows_shader = __rt_rows_grass, .foliage = true, .grass = true},
    // Cloth (banners, tents) and sails: thin, captured every frame (the CPU moves them).
    {.hash = 0x772B26FF, .capture_shader = __rt_capture_cloth_sm3, .dynamic = true, .thin = true},
    {.hash = 0x902253FD, .capture_shader = __rt_capture_cloth_sm41, .dynamic = true, .thin = true},
    {.hash = 0xD11B5EC7, .capture_shader = __rt_capture_sail_sm3, .dynamic = true, .thin = true},
    {.hash = 0x202FA188, .capture_shader = __rt_capture_sail_sm41, .dynamic = true, .thin = true},
    // Ropes: ribbons the game turns to the camera, solid, captured every frame as drawn (Rome II's 4F90C2B5); the CPU's (8D0D26B9, in no
    // trace) and the placed ones (3926A823, turned in view space: back to the world through inv_view; the ships' rigging, a triangle
    // strip, drawn in sea battles: DEBUG44's sea1, so not held).
    {.hash = 0x8D0D26B9, .capture_shader = __rt_capture_rope_sm3, .dynamic = true, .held_row = &rays_untraced_casters_row},
    {.hash = 0x3926A823, .capture_shader = __rt_capture_rope_rigid_sm3, .dynamic = true},
    // Sprite soldiers: cards facing the sun, cut out by their diffuse map's alpha.
    // Each with its Shader Model 4.1 / 5 builds (E42AE09F; 398BD3DF, B2585ABA): the same instructions, only the version
    // token and dcl_globalFlags differ (tools/tw/attila_gaps/atg_cards_bytes.txt; no trace drew them: level B).
    {.hash = 0x1B9E7692, .capture_shader = __rt_capture_imposter, .dynamic = true, .foliage = true, .sun_facing = true},
    {.hash = 0xE42AE09F, .capture_shader = __rt_capture_imposter, .dynamic = true, .foliage = true, .sun_facing = true},
    // Far trees' billboards: the same, each showing the tree from the sun.
    {.hash = 0x4D50E1C7, .capture_shader = __rt_capture_billboard, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 1},
    {.hash = 0x398BD3DF, .capture_shader = __rt_capture_billboard, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 1},
    {.hash = 0xB2585ABA, .capture_shader = __rt_capture_billboard, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 1},
    // The campaign's plants and forests, and its trees: placed like the battle's, no wind.
    {.hash = 0x924D7813, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true},
    {.hash = 0x819A414C, .capture_shader = __rt_capture_campaign_vegetation, .rows_shader = __rt_rows_campaign_vegetation, .foliage = true, .still = true},
    {.hash = 0x4657DA8A, .capture_shader = __rt_capture_campaign_tree, .rows_shader = __rt_rows_campaign_tree, .foliage = true, .still = true},
}});

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
constexpr auto CAMPAIGN_SHADERS = WithTwins<1>(std::array<uint32_t, 4>{
    0x0CC96E53, 0xC1B5C13E, twattila::rt_scene::CAMPAIGN_SEA_SHADERS[0], twattila::rt_scene::CAMPAIGN_SEA_SHADERS[1]});
constexpr auto OnCampaignDraw = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  twattila::frame_history::campaign_frames.store(2, std::memory_order_relaxed);
  return {};
};

// Grass Height Distance's fade (grass_fade.hpp): Attila's grass pixel shaders and their TDD twins.
constexpr auto GRASS_FADE_SHADERS = WithTwins<13>(twattila::grass_fade::HASHES);

// TDD_TWINS: 36 distinct pairs, no twin one of their game hashes, every twin in a table (WithTwins checks each table
// for a hash twice).
static_assert(DistinctHashes<&ShaderTwin::game>(TDD_TWINS) && DistinctHashes<&ShaderTwin::twin>(TDD_TWINS));
static_assert(std::ranges::all_of(TDD_TWINS, [](const ShaderTwin& pair) {
  const auto listed = [&pair](const auto& table, auto hash) {
    return std::ranges::find(table, pair.twin, hash) != table.end();
  };
  return std::ranges::find(TDD_TWINS, pair.twin, &ShaderTwin::game) == TDD_TWINS.end()
         && (listed(SCENE_SHADERS, &twattila::rt_scene::SceneShader::hash)
             || listed(MOTION_SHADERS, &twattila::anti_aliasing::MotionShader::hash)
             || listed(RENDER_SCALE_READS, &twattila::view_share::RegionRead::shader_hash)
             || listed(FLAG_PIXEL_SHADERS, std::identity{}) || listed(FLAG_PIXEL_SHADERS_MS, std::identity{})
             || listed(CAMPAIGN_SHADERS, std::identity{}) || listed(GRASS_FADE_SHADERS, std::identity{}));
}));

// Grass Dump (graphics_hooks.hpp): the grass draws' instance rows in the two frames after the dumped scrub update.
constexpr std::array<uint32_t, 2> GRASS_VERTEX_SHADERS = {0xF3741248, 0x01FA9CDE};
constexpr auto OnGrassDumpDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!twattila::graphics_hooks::GrassDumpReadsRows()) return {};
  if constexpr (requires { command.arguments.index_count; }) {
    twattila::graphics_hooks::AddGrassDumpRows(reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native()),
                                               {.vertex_shader = command.matched_shader_hash,
                                                .indexed = true,
                                                .count = command.arguments.index_count,
                                                .instances = command.arguments.instance_count,
                                                .first = command.arguments.first_index,
                                                .base_vertex = command.arguments.vertex_offset,
                                                .first_instance = command.arguments.first_instance});
  } else if constexpr (requires { command.arguments.vertex_count; }) {
    twattila::graphics_hooks::AddGrassDumpRows(reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native()),
                                               {.vertex_shader = command.matched_shader_hash,
                                                .count = command.arguments.vertex_count,
                                                .instances = command.arguments.instance_count,
                                                .first = command.arguments.first_vertex,
                                                .first_instance = command.arguments.first_instance});
  }
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

// The port's other renderers (its OpenGL fallback, a D3D12 route) are left alone: this add-on's handlers are D3D11-only
// (OnPresent's casts would crash there). Logged once per API.
inline bool StandsDown(reshade::api::device* device) {
  const reshade::api::device_api api = device->get_api();
  if (api == reshade::api::device_api::d3d11) return false;
  const uint32_t bit = 1u << ((static_cast<uint32_t>(api) >> 12) & 31u);
  if ((stood_down_apis.fetch_or(bit) & bit) == 0) {
    const char* name = "unknown";
    switch (api) {
      case reshade::api::device_api::d3d9:   name = "d3d9"; break;
      case reshade::api::device_api::d3d10:  name = "d3d10"; break;
      case reshade::api::device_api::d3d12:  name = "d3d12"; break;
      case reshade::api::device_api::opengl: name = "opengl"; break;
      case reshade::api::device_api::vulkan: name = "vulkan"; break;
      default:                               break;
    }
    renodx::utils::log::i("[Total War] 64-bit Attila: a ", name, " device: the add-on leaves it alone");
  }
  return true;
}

// The port's loader draws the frame into its own RGBA8 picture and copies it into buffer 0: the picture plays the swap
// chain's part, so its clone turns on at its first copy (by resource: every view of it follows). The frame drawn before
// that copy went to the 8-bit picture: one black frame per picture.
inline bool OnCopyPresentPicture(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11 || source.handle == 0u || dest.handle == 0u) return false;
  bool to_swap_chain = false;
  bool dest_clone_on = false;
  reshade::api::resource_desc dest_desc = {};
  renodx::utils::resource::GetResourceInfo(dest, [&](const renodx::utils::resource::ResourceInfo& info) {
    to_swap_chain = info.is_swap_chain;
    dest_clone_on = info.clone_enabled;
    dest_desc = info.desc;
  });
  if (!to_swap_chain) return false;
  reshade::api::resource source_clone = {0u};
  reshade::api::resource_desc desc = {};
  reshade::api::resource_desc clone_desc = {};
  renodx::utils::resource::GetResourceInfo(source, [&](const renodx::utils::resource::ResourceInfo& info) {
    if (info.clone_enabled) source_clone = info.clone;
    desc = info.desc;
    clone_desc = info.clone_desc;
  });
  if (source_clone.handle == 0u) {
    renodx::utils::resource::ResourceUpgradeInfo* target = nullptr;
    source_clone = renodx::utils::resource::upgrade::GetResourceClone(
        source, {.require_enabled = false, .allow_create = true, .activate = true}, &target);
    renodx::utils::resource::GetResourceInfo(source, [&clone_desc](const renodx::utils::resource::ResourceInfo& info) {
      clone_desc = info.clone_desc;
    });
    if (target != nullptr) {
      if (present_picture.exchange(source.handle) != source.handle) {
        renodx::utils::log::i("[Total War] 64-bit Attila: the game's picture upgraded (", desc.texture.width, "x", desc.texture.height, ")");
      }
    } else if (!picture_warned.exchange(true)) {
      renodx::utils::log::w("[Total War] 64-bit Attila: the game's picture (format ", static_cast<uint32_t>(desc.texture.format), ", ",
                            desc.texture.width, "x", desc.texture.height, ") has no upgrade: the HDR output stays black");
    }
  }
  // utils copies next: HDR10 / SDR clone to clone; scRGB the picture's clone into the real RGBA16F buffer 0 (its clone,
  // the swap chain pass's copy, is off and left alone). Only when another add-on's utils copies (the first RenoDX add-on
  // loaded handles the events, for example a DevKit built before f3dc754dd) does the copy also happen here: that one may
  // copy into the pass's copy, which the pass then overwrites with the real buffer (black). Only into a buffer the copy is
  // valid for (RGBA16F, the clone's size); any other is left to utils.
  const bool same_layout = dest_desc.texture.format == reshade::api::format::r16g16b16a16_float
                           && dest_desc.texture.width == clone_desc.texture.width
                           && dest_desc.texture.height == clone_desc.texture.height;
  if (!dest_clone_on && source_clone.handle != 0u && same_layout
      && !renodx::utils::resource::upgrade::shared.IsEventHandler()) {
    cmd_list->copy_resource(source_clone, dest);
    return true;
  }
  return false;
}

// The loader frees its picture right before it makes the next (mode change, new window), which can get the same handle:
// forgotten here, the next picture is logged and its overlay found.
inline void OnDestroyPresentPicture(reshade::api::device* /*device*/, reshade::api::resource resource) {
  uint64_t picture = resource.handle;
  present_picture.compare_exchange_strong(picture, 0u);
}

// The loader's 8-bit overlay (the frame-rate number, the Discord button) comes as 4-byte rows: into buffer 0 after the
// copy (1.3.20) or into the picture before it (the fixed loader). Under the add-on both are RGBA16F there (their clones, or
// scRGB's real buffer), 8 bytes a pixel: the rows are converted, else scRGB reads past them and HDR10 / SDR lose them. No
// gamma change: the swap chain pass decodes the frame's encoding and applies the UI brightness.
inline bool OnUpdateOverlay(reshade::api::device* device, const reshade::api::subresource_data& data, reshade::api::resource dest,
                            uint32_t dest_subresource, const reshade::api::subresource_box* dest_box) {
  if (dest_box == nullptr || dest_subresource != 0 || data.data == nullptr) return false;
  // An empty or inverted box (D3D11: nothing written).
  if (dest_box->right <= dest_box->left || dest_box->bottom <= dest_box->top) return false;
  const uint32_t width = dest_box->right - dest_box->left;
  const uint32_t height = dest_box->bottom - dest_box->top;
  if (data.row_pitch != 4 * width || device->get_api() != reshade::api::device_api::d3d11) return false;
  // The fixed loader's target is the picture (before the copy), 1.3.20's buffer 0 (after it).
  const bool to_picture = dest.handle == present_picture.load(std::memory_order_relaxed);
  bool to_swap_chain = false;
  reshade::api::resource target = dest;
  reshade::api::format format = reshade::api::format::unknown;
  renodx::utils::resource::GetResourceInfo(dest, [&](const renodx::utils::resource::ResourceInfo& info) {
    to_swap_chain = info.is_swap_chain;
    if (info.clone_enabled && info.clone.handle != 0u) {
      target = info.clone;
      format = info.clone_desc.texture.format;
    } else {
      format = info.desc.texture.format;
    }
  });
  if ((!to_picture && !to_swap_chain) || format != reshade::api::format::r16g16b16a16_float) return false;
  static const std::array<uint16_t, 256> HALVES = [] {
    std::array<uint16_t, 256> halves = {};
    for (size_t value = 0; value < halves.size(); ++value) {
      halves[value] = renodx::utils::float16::Encode(static_cast<float>(value) / 255.f);
    }
    return halves;
  }();
  // Tight rows (row pitch = 4 * width): one byte a channel, in order.
  const std::span bytes(static_cast<const uint8_t*>(data.data), size_t{width} * height * 4);
  std::vector<uint16_t> rows(bytes.size());
  std::ranges::transform(bytes, rows.begin(), [](uint8_t value) { return HALVES[value]; });
  device->update_texture_region({.data = rows.data(), .row_pitch = 8 * width, .slice_pitch = 8 * width * height}, target, 0, dest_box);
  if (to_picture) {
    if (!picture_overlay_logged.exchange(true)) {
      renodx::utils::log::i("[Total War] 64-bit Attila: the loader's 8-bit overlay converted (picture)");
    }
  } else if (!buffer_overlay_logged.exchange(true)) {
    renodx::utils::log::i("[Total War] 64-bit Attila: the loader's 8-bit overlay converted (buffer)");
  }
  return true;
}

}  // namespace

// Lifecycle /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  if constexpr (ATTILA64) {
    if (StandsDown(swapchain->get_device())) return;
  }
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

// The shadow map size and cascades, before the game builds its shadows.
void OnInitDevice(reshade::api::device* device) {
  if constexpr (ATTILA64) {
    if (StandsDown(device)) return;
    // The plugin publishes its block before the game's device: this is the normal lookup (and shadow_pass's install),
    // the first present the fallback.
    if (twattila::attila64_plugin_link::Result() == twattila::attila64_plugin_link::Found::PENDING) {
      twattila::attila64_plugin_link::Lookup(false);
    }
    // The shadow map size and cascades patch the 32-bit game's code: never on the port.
    return;
  }
  if (shadow_size_locked) return;
  twattila::graphics_hooks::shadow_map::Install(
      std::exp2(shadow_map_size),
      shadow_cascades_row != 0.f && twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength).shadows_only);
}

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  if constexpr (ATTILA64) {
    if (StandsDown(queue->get_device())) return;
  }
  // The bench's RENODX_TW_GAME_CPUS, else the count cpu_sets reads. value_or always evaluates its argument, so cpu_sets::Update
  // (Keep Render Core Free and the count's reads) runs every present either way: never move it into a branch that may skip it.
  // The port: cpu_sets' Keep Render Core Free off (it moves the 32-bit game's threads; the port's row is the engine
  // plugin's render core); the count's reads stay.
  const uint32_t game_cpus = game_cpus_env.value_or(twattila::cpu_sets::Update(!ATTILA64 && keep_render_core_row != 0.f, GAME_THREAD_MODULES));
  const bool fewer_constant_uploads = fewer_constant_uploads_row != 0.f;
  renodx::mods::shader::use_native_d3d11_injection.store(fewer_constant_uploads, std::memory_order_relaxed);
  if constexpr (ATTILA64) {
    if (fewer_constant_uploads && !fewer_constant_uploads_logged.exchange(true)) {
      renodx::utils::log::i("[Total War] Fewer Constant Uploads: on.");
    }
  }
  // A traced frame's full-ring line waits (its own work fills the ring).
  if (queue->get_device()->get_api() == reshade::api::device_api::d3d11) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_immediate_command_list()->get_native());
    const bool traced = twattila::frame_trace::Tracing()
                        || twattila::frame_trace::internal::map_trace.tracing.load(std::memory_order_relaxed);
    twattila::buffer_pool::Update(context, !ATTILA64 && fewer_driver_waits_row != 0.f, traced);
  }
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
  // Path-Traced Reflection Occlusion: the specular occlusion traced for the reflections under Path-Traced Ambient (its own
  // row keeps it whole).
  const bool reflections_occluded = rt_settings.path_ambient && !rt_settings.specular_occlusion && path_reflection_occlusion > 0.f;
  rt_settings.specular_occlusion = rt_settings.specular_occlusion || reflections_occluded;
  shader_injection.reflection_occlusion = reflections_occluded ? path_reflection_occlusion : 1.f;
  twattila::rt_scene::Update(rt_settings);
  // Ray-traced or path-traced ambient occlusion in place of the game's and GT-VBAO (theirs not drawn: their cost saved;
  // under Path-Traced Ambient they stacked on it, ATN's debug29).
  shader_injection.rt_ambient_alone = twattila::rt_scene::AmbientReplaced() ? 1.f : 0.f;
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  aa_settings.frame_gamma = TW_FRAME_GAMMA;
  aa_settings.linear_frame = IsHDRMode();  // t00 writes linear in the HDR modes (rrbi_ G0); SDR as before (aaf_ 11-26)
  aa_settings.rr_before_tone_map = ATTILA64 && rr_before_tone_map_row != 0.f;
  aa_settings.stretched_debug_view = sss_settings.enabled && sss_settings.debug_view != 0;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  shader_injection.ssao_scale = twattila::ao_pass::ResolutionScale(shader_injection.ssao_resolution);
  shader_injection.ssao_frame =
      twattila::ao_pass::Update(shader_injection.ssao_mode >= 1.f && shader_injection.rt_ambient_alone == 0.f,
                                shader_injection.ssao_temporal != 0.f,
                                shader_injection.ssao_scale, shader_injection.ssao_denoise != 0.f);
  if constexpr (ATTILA64) {
    // The game's ELF image, mapped by the loader's game thread before the first frame (the probe and test devices never
    // present): its build id and code ranges for the map trace's callers, read inside its first page only.
    if (!identity_read.exchange(true)) {
      constexpr uintptr_t GAME_BASE = 0x400000;
      constexpr size_t PAGE = 4096;
      constexpr DWORD READABLE = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE;
      MEMORY_BASIC_INFORMATION region = {};
      const bool readable = VirtualQuery(reinterpret_cast<const void*>(GAME_BASE), &region, sizeof(region)) == sizeof(region)
                            && region.State == MEM_COMMIT && (region.Protect & READABLE) != 0 && (region.Protect & PAGE_GUARD) == 0
                            && region.RegionSize >= PAGE;
      const auto* page = reinterpret_cast<const uint8_t*>(GAME_BASE);
      // Elf64_Phdr.
      struct ProgramHeader {
        uint32_t type;
        uint32_t flags;
        uint64_t offset;
        uint64_t address;
        uint64_t physical_address;
        uint64_t file_size;
        uint64_t memory_size;
        uint64_t align;
      };
      std::string build_id = "none";
      std::vector<std::pair<uintptr_t, uintptr_t>> ranges;
      // ELF magic, 64-bit, little endian; the program header table (e_phoff, e_phentsize, e_phnum) inside the page.
      bool elf = readable && std::memcmp(page, "\x7F" "ELF\x02\x01", 6) == 0;
      uint64_t table = 0;
      uint16_t entry_size = 0;
      uint16_t entries = 0;
      if (elf) {
        std::memcpy(&table, page + 32, sizeof(table));
        std::memcpy(&entry_size, page + 54, sizeof(entry_size));
        std::memcpy(&entries, page + 56, sizeof(entries));
        elf = entry_size == sizeof(ProgramHeader) && table <= PAGE && entries <= (PAGE - table) / sizeof(ProgramHeader);
      }
      for (uint16_t index = 0; elf && index < entries; ++index) {
        ProgramHeader header;
        std::memcpy(&header, page + table + (index * sizeof(header)), sizeof(header));
        // PT_LOAD with PF_X: game code.
        if (header.type == 1 && (header.flags & 1u) != 0 && header.memory_size <= UINTPTR_MAX - header.address) {
          ranges.emplace_back(header.address, header.address + header.memory_size);
        }
        // PT_NOTE inside the page: its notes (name size, description size, type, then the 4-aligned name and description).
        if (header.type != 4 || header.offset > PAGE || header.file_size > PAGE - header.offset) continue;
        const uint64_t notes_end = header.offset + header.file_size;
        for (uint64_t at = header.offset; at + 12 <= notes_end;) {
          std::array<uint32_t, 3> note;
          std::memcpy(note.data(), page + at, sizeof(note));
          const uint64_t name_at = at + 12;
          const uint64_t description_at = name_at + ((note[0] + 3ull) & ~3ull);
          const uint64_t next = description_at + ((note[1] + 3ull) & ~3ull);
          if (next > notes_end) break;
          // NT_GNU_BUILD_ID, owner "GNU".
          if (note[2] == 3 && note[0] == 4 && std::memcmp(page + name_at, "GNU", 4) == 0) {
            build_id.clear();
            for (const uint8_t byte : std::span(page + description_at, note[1])) {
              build_id += std::format("{:02x}", byte);
            }
          }
          at = next;
        }
      }
      // Each code range cut at its first page that is not committed and readable: the map trace's caller scan reads the
      // code bytes before a return address.
      for (auto& [begin, end] : ranges) {
        uintptr_t at = begin;
        MEMORY_BASIC_INFORMATION code = {};
        while (at < end && VirtualQuery(reinterpret_cast<const void*>(at), &code, sizeof(code)) == sizeof(code)
               && code.State == MEM_COMMIT && (code.Protect & READABLE) != 0 && (code.Protect & PAGE_GUARD) == 0) {
          at = reinterpret_cast<uintptr_t>(code.BaseAddress) + code.RegionSize;
        }
        end = std::min(end, at);
      }
      std::erase_if(ranges, [](const std::pair<uintptr_t, uintptr_t>& range) { return range.second <= range.first; });
      if (elf && !ranges.empty()) {
        renodx::utils::log::i("[Total War] 64-bit Attila: game code mapped, build id ", build_id, ", ", ranges.size(), " code ranges");
        // Its offsets are the game's addresses (base 0).
        twattila::frame_trace::SetGameCode("game_code_64bit", 0, std::move(ranges));
      } else {
        renodx::utils::log::w("[Total War] 64-bit Attila: game code not readable at the first present");
      }
      // Engine Performance Fix (perffix64.dll, started by the loader before the game's code): its FrameBridge upscaling
      // and its waving flags as their files are at this start (the loader writes them from the start window's ticks
      // before the add-on loads; both are read only at the start). No exe path: neither is read.
      if (const std::filesystem::path exe_dir = renodx::utils::path::GetExecutableBasePath();
          GetModuleHandleW(L"perffix64.dll") != nullptr && !exe_dir.empty()) {
        // <game>\FrameBridge\helper64.exe and its settings.txt not saying off (S28 picker.c:236-263).
        const std::filesystem::path framebridge = exe_dir.parent_path() / L"FrameBridge";
        std::error_code error;
        framebridge_on = std::filesystem::exists(framebridge / L"helper64.exe", error)
                         && !twattila::start_files::FrameBridgeOff(framebridge / L"settings.txt");
        // PerfFix's own line for them on (its other flag wave lines say why not), and no flagwave_off.txt from the
        // start window's tick (S28 plugins.inc:200-211).
        bool flags_installed = false;
        twattila::start_files::ForEachLine(exe_dir / L"perffix64.log", [&flags_installed](std::string_view line) {
          flags_installed = flags_installed || line.find(" flag wave: on (") != std::string_view::npos;
        });
        waving_flags_on = flags_installed && !std::filesystem::exists(exe_dir / L"flagwave_off.txt", error);
        renodx::utils::log::i(std::format("[Total War] 64-bit Attila: Engine Performance Fix loaded: FrameBridge upscaling {}, waving flags {}",
                                          (framebridge_on ? "on" : "off"), (waving_flags_on ? "on" : "off")));
      }
    }
    // The plugin's lookup, final here when init_device did not find it READY. Present runs on the loader's drawing
    // thread between frames (never Install from a bind or draw callback).
    if (twattila::attila64_plugin_link::Result() == twattila::attila64_plugin_link::Found::PENDING) {
      twattila::attila64_plugin_link::Lookup(true);
    }
    if (twattila::attila64_plugin_link::Result() == twattila::attila64_plugin_link::Found::READY) {
      namespace plugin = twattila::attila64_plugin;
      // The engine values the plugin checked: found once, then the sliders applied as on 32-bit (found: no module
      // lookup, no status line), then the restart-only ones and Loader Frame Cap mirrored for the next start.
      if (!graphics_found) {
        graphics_found = twattila::attila64_plugin_link::FindTweaks(graphics_options);
        // Campaign Tree Detail is one row of a value and a hook: without its hook (refused, switched off, or changed at
        // the game's start) its value stays the game's too. The rows offered are final once the values are (the
        // plugin's rows_changed is stored before its values' release).
        if (graphics_found && !GameCodeRowShown(PluginRow::CAMPAIGN_TREES)) {
          for (tw::engine_tweaks::Tweak& tweak : campaign_trees.tweaks) {
            tweak.value = nullptr;
          }
        }
      }
      if (graphics_found) {
        tw::engine_tweaks::Update(L"empire.retail.dll", graphics_options, nullptr, &graphics_found, graphics_status);
      }
      twattila::attila64_plugin_link::MirrorRestartValues(loader_frame_cap_row != 0.f, static_cast<int>(shadow_map_size),
                                                          engine_code_row != 0.f);
      // The scale rows' hooks, folded as the 32-bit hooks fold them (graphics_hooks.hpp OnUnitDistances, its battle
      // budget 1 until Big Battle Fix; engine_code.hpp's 1 / Model Detail squared); the other scales neutral.
      std::array<float, plugin::SCALE_COUNT> scales = plugin::NEUTRAL_SCALES;
      for (size_t size = 0; size < unit_detail.size(); ++size) {
        scales[plugin::SCALE_UNIT_LOD + size] = unit_detail[size].scale;
      }
      std::fill_n(&scales[8], 4, unit_animation.scale);
      scales[12] = unit_variety.scale;
      scales[13] = facial_animation.scale;
      scales[14] = hand_animation.scale;
      scales[15] = footstep_effects.scale;
      scales[16] = (twattila::rt_scene::CastersSkipped() ? 0.f : 1.f);
      scales[plugin::SCALE_MODEL_DETAIL] = 1.f / (object_detail.scale * object_detail.scale);
      // Campaign Tree Detail's hook (graphics_hooks.hpp Update: the distances squared) only while its engine value is
      // found and kept too (above): its row shows with both, so nothing applies while the player cannot see it.
      if (OptionValuesFound(campaign_trees)) {
        scales[plugin::SCALE_CAMPAIGN_TREES] = campaign_trees.scale * campaign_trees.scale;
      }
      // The grass rows as graphics_hooks.hpp's scrub hooks take them: the models' LODs x Grass Detail squared, the
      // cells' radius and bias x it, the bias / Grass Density (at least its floor).
      scales[plugin::SCALE_GRASS_LOD] = grass_distance.scale * grass_distance.scale;
      scales[plugin::SCALE_GRASS_RADIUS] = grass_distance.scale;
      scales[plugin::SCALE_GRASS_DENSITY] =
          std::max(grass_density.scale, twattila::graphics_hooks::GRASS_DENSITY_FLOOR);
      // The sprite rows as graphics_hooks.hpp's Update takes them: the main view's focal length / Sprite Size while Small
      // Soldiers as Sprites is on and Sprite Size above 0 (0: off, also with no main camera yet), the dead soldiers'
      // distance x Dead Unit Detail.
      scales[plugin::SCALE_SPRITE_DISTANCE] =
          ((small_soldiers_as_sprites != 0.f && sprite_size > 0.f)
               ? twattila::anti_aliasing::MainFocalPixels() / sprite_size
               : 0.f);
      scales[plugin::SCALE_DEAD_UNITS] = dead_units.scale;
      twattila::attila64_plugin_link::PublishScales(scales);
      // shadow_pass's Update runs every present (it ends the frame's counts), here its second site: the 32-bit one
      // below waits for the at-start tests; the port has none.
      const bool pass_draw =
          twattila::shadow_pass::Update(shadow_pass_row != 0.f, twattila::rt_scene::CastersSkipped(), true);
      // Bone flush: before each bone pack, limb motion vectors' unit records (object_motion's own switch:
      // FlushUnitRecords has work only while it is on). Batched Unit Capture batches only where the host flushes it
      // (Host::flushes_unit_captures, PacksHooked: never on the port yet); C3 adds its term when that flag is wired.
      twattila::attila64_plugin_link::Update(
          (twattila::object_motion::internal::active.load(std::memory_order_relaxed)
               ? plugin::RowBit(plugin::ROW_BONE_FLUSH)
               : 0u)
          | (pass_draw ? plugin::RowBit(plugin::ROW_PASS_DRAW) : 0u),
          keep_battle_core_row != 0.f,
          // Hidden until measured: the plugin's own bit stays (a saved On, from a copied 32-bit ini say, waits too).
          (RENDER_CORE_ROW_MEASURED ? keep_render_core_row != 0.f : twattila::attila64_plugin_link::RENDER_CORE_DEFAULT),
          static_cast<uint32_t>(effects_on_all_cores_row),
          // Hidden until measured: the plugin's default bit is written (a hand-edited Off waits too).
          (BATTLE_STEP_ROW_MEASURED ? faster_battle_step_row != 0.f
                                    : twattila::attila64_plugin_link::BATTLE_STEP_DEFAULT),
          // Hidden until measured: no CORE_SPLIT write, so the plugin's ContactSplit key holds (a saved On waits too).
          (CONTACT_SPLIT_ROW_MEASURED ? std::optional<bool>(multithreaded_collisions_row != 0.f) : std::nullopt));
    }
    // Everything below writes the 32-bit game's code or values.
    return;
  }
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  // Too few CPUs for the game leave no room for workers: every pool user's stage that wakes workers is off (the game's own
  // code runs, as with the rows off), and the pool stops.
  const bool pool_room = (twattila::worker_pool::BudgetWorkers(game_cpus) != 0);
  const bool effect_update_on = (pool_room && effect_update_row != 0.f);
  uint32_t recording_groups = 0;
  // Any user of the worker pool is in a stage that wakes workers.
  bool pool_in_use = effect_update_on;
  bool instance_fill_on = false;
  bool fill_fold_on = false;
#if defined(TWATTILA_DRAW_RECORDING)
  twattila::draw_recording::Settings recording = {
      .stage = static_cast<twattila::draw_recording::Stage>(draw_recording_stage_row),
      .loops = 0,
      // The thread context_hooks installed on (0: not installed), for the trace's note.
      .context_hooks_thread = (twattila::context_hooks::internal::state.context.load() != nullptr
                                   ? twattila::context_hooks::internal::state.thread
                                   : 0),
      .b_families = static_cast<uint32_t>(draw_recording_families_row)};
  for (size_t group = 0; group < draw_recording_group_rows.size(); ++group) {
    recording.loops |= (draw_recording_group_rows[group] != 0.f ? 1u << group : 0u);
  }
  const bool recording_wakes_workers = (recording.stage == twattila::draw_recording::Stage::TOUCH_AHEAD
                                        || recording.stage == twattila::draw_recording::Stage::ONE_WORKER
                                        || recording.stage == twattila::draw_recording::Stage::WORKERS);
  // Before SiteGroups: no room takes the sites out (the stages that need no worker stay as set).
  if (recording_wakes_workers && !pool_room) {
    recording.stage = twattila::draw_recording::Stage::OFF;
  }
  recording_groups = twattila::draw_recording::SiteGroups(recording.stage, recording.loops);
  pool_in_use |= (recording_wakes_workers && pool_room);
  instance_fill_on = (pool_room && instance_fill_row != 0.f);
  fill_fold_on = (pool_room && fill_fold_row != 0.f);
  pool_in_use |= instance_fill_on;
  // A fold still open here (a path that did not close it) would make the pool's Update wait for workers parked on its publish.
  twattila::draw_recording::CloseFoldAtPresent();
#endif
  const uint32_t env_generation = worker_env_generation.load(std::memory_order_relaxed);
  if (env_generation != worker_env_read_generation) {
    worker_env_read_generation = env_generation;
    particle_count_check_env = (GetEnvironmentVariableA("RENODX_TW_COUNT_CHECK", nullptr, 0) != 0);
    animation_check_env = (GetEnvironmentVariableA("RENODX_TW_ANIMATION_CHECK", nullptr, 0) != 0);
  }
  const twattila::animation_rows::Stage animation_stage =
      ((animation_rows_row == 0.f || !pool_room)
           ? twattila::animation_rows::Stage::OFF
           : (animation_check_env ? twattila::animation_rows::Stage::CHECK : twattila::animation_rows::Stage::ON));
  pool_in_use |= (animation_stage != twattila::animation_rows::Stage::OFF);
  // Before the users' own Updates.
  twattila::worker_pool::Update({.threads = static_cast<uint32_t>(worker_threads_row),
                                 .in_use = pool_in_use,
                                 .env_generation = env_generation,
                                 .game_cpus = game_cpus});
  // Before the engine code's: its site goes out after the stage is off, and goes in after the stage is on.
  twattila::animation_rows::Update(animation_stage, env_generation);
  twattila::engine_code::Update(engine_code_row != 0.f, twattila::engine_code::Game::ATTILA, render_lists_row != 0.f,
                                job_wakeups_row != 0.f, effect_prefetch_row != 0.f, emitter_prefetch_row != 0.f,
                                particle_prefetch_row != 0.f, sweep_prefetch_row != 0.f, object_detail.scale, recording_groups,
                                {.animation = animation_stage,
                                 .pending_sweep_trace = worker_trace_env,
                                 .effect_update = effect_update_on,
                                 .particle_count = (particle_count_row != 0.f),
                                 .particle_count_check = particle_count_check_env,
                                 .instance_fill = instance_fill_on,
                                 .fill_fold = fill_fold_on,
                                 .exit_job_drain = exit_job_drain_on});
  // After the engine code's Update (the sites are in or out for this present) and the pool's.
  twattila::effect_update::Update(effect_update_on, effect_spawns_row != 0.f, env_generation);
#if defined(TWATTILA_DRAW_RECORDING)
  // After the engine code's Update: the sites are in or out for this present, and only the groups whose sites all are
  // in run.
  recording.groups_in = twattila::engine_code::DrawRecordingGroupsIn();
  twattila::draw_recording::Update(recording);
#endif
  tw::engine_tweaks::Update(L"empire.retail.dll", graphics_options, twattila::engine_code::ModelDetailStatus(object_detail.scale), &graphics_found,
                            graphics_status);
  if (!shadow_size_locked) twattila::graphics_hooks::shadow_map::Set(std::exp2(shadow_map_size), shadow_cascades_row != 0.f && rt_settings.shadows_only);
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
  twattila::shadow_pass::Update(shadow_pass_row != 0.f, twattila::rt_scene::CastersSkipped());
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
  // GetResourceViewClone answers the views never cloned without the view map's lock (resource.hpp ViewFilterData).
  renodx::utils::resource::use_view_filter = true;
  // A bind reuses its pipeline lookup's outcome until a pipeline write (pipeline.hpp BindCacheData): no map lock a bind.
  renodx::utils::pipeline::use_bind_cache = true;
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

  // Graphics 2 of 2's tests (twattila/engine_tests.hpp). The port: their values through the 64-bit engine plugin
  // (attila64_plugin_link::FindTweaks), the audio memory ones written at the next start (its restart mirror).
  engine_tests = twattila::engine_tests::Attila();
  if constexpr (ATTILA64) {
    // The port's tooltip where the 32-bit one does not hold (the 64-bit game has the room).
    std::ranges::find(engine_tests, "AudioSoundFileMemory", &twattila::engine_tests::EngineTest::key)->tooltip =
        "Memory for the loaded sound files. Restart the game. Game: 86 MB.";
  }
  // Model Detail has no value on the port (its scale goes to the plugin's hook): it only adds the TweaksFound gate to its
  // row.
  graphics_options = {&object_detail, &building_detail, &campaign_trees, &campaign_plants, &campaign_props};
  for (auto& test : engine_tests) {
    graphics_options.push_back(&test.option);
  }
  talesofarise::pixel_trace::file_prefix = "attila";
  graphics_status = tw::engine_tweaks::StatusLine();
  reshade::get_config_value(nullptr, "Attila", "DisableGraphics", shadow_size_locked);
#if defined(TWATTILA_ENGINE_PROBE)
  // The probe build (engine_probe.hpp): the histograms go to the log this many presents after the first site was written (ReShade.ini [Total War] EngineProbePresents; none: never).
  if (int presents = 0; reshade::get_config_value(nullptr, "Total War", "EngineProbePresents", presents) && presents > 0) {
    twattila::engine_code::ProbeArm(static_cast<uint32_t>(presents));
  }
#endif
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
  // A setting's row on the port: by GAME_CODE_ROWS' key or group, else TWEAKS by the value option it is bound to (the
  // tests); none for a setting the game's code does not read (shown as on 32-bit).
  const auto port_row_of = [](const renodx::utils::settings::Setting& setting) -> std::optional<PortRow> {
    const auto entry = std::ranges::find_if(GAME_CODE_ROWS, [&setting](const GameCodeRow& row) {
      return (!setting.key.empty() && setting.key == row.match) || (!setting.group.empty() && setting.group == row.match);
    });
    const auto bound = std::ranges::find_if(graphics_options, [&setting](const tw::engine_tweaks::Option* option) {
      return setting.binding == &option->scale;
    });
    const tw::engine_tweaks::Option* option = (bound != graphics_options.end() ? *bound : nullptr);
    if (entry != GAME_CODE_ROWS.end()) return PortRow{.row = entry->plugin_row, .option = option};
    if (option != nullptr) return PortRow{.row = PluginRow::TWEAKS, .option = option};
    return std::nullopt;
  };
  // A preset button: `defaults` with `changes` (in order) written to the settings. The port: only the rows shown (a
  // hidden row keeps its saved value).
  const auto preset = [port_row_of](const std::vector<std::pair<std::string, float>>& defaults,
                                    const std::vector<std::pair<std::string, float>>& changes) {
    return [defaults, changes, port_row_of] {
      auto values = defaults;
      for (const auto& [key, value] : changes) {
        std::ranges::find(values, key, &std::pair<std::string, float>::first)->second = value;
      }
      if constexpr (ATTILA64) {
        std::erase_if(values, [&port_row_of](const std::pair<std::string, float>& entry) {
          const auto* setting = renodx::utils::settings::FindSetting(entry.first);
          const std::optional<PortRow> port_row = (setting == nullptr ? std::nullopt : port_row_of(*setting));
          return port_row.has_value() && !port_row->Shown();
        });
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
  note->label += " Some apply only after loading a battle or a save.";
  constexpr const char* PRESETS_TEXT = "The two presets below are the settings I would use myself.";
  if constexpr (!ATTILA64) {
    note->label = note->label + "\n" + PRESETS_TEXT;
  }
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
  auto* ui_mod_row = new renodx::utils::settings::Setting{
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
                           (restart_pending ? RESTART_NOW : "Changing this needs a game restart."));
        return false;
      },
  };
  settings.insert(settings.end(), {
      note,
      ui_mod_row,
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
  // The port: the presets' sentence as its own row right after the note, shown with the buttons (they come with the
  // Unit Detail rows, which another row of the section can show without).
  if constexpr (ATTILA64) {
    settings.insert(std::next(std::ranges::find(settings, note)),
                    new renodx::utils::settings::Setting{
                        .value_type = renodx::utils::settings::SettingValueType::TEXT,
                        .label = PRESETS_TEXT,
                        .section = note->section,
                        .tint = note->tint,
                        .is_visible = [] { return GameCodeRowShown(PluginRow::UNIT_LOD); },
                    });
  }
  {
    const auto reflections = twattila::feature_settings::SsrSettings(&shader_injection, &ssr_denoiser);
    settings.insert(settings.end(), reflections.begin(), reflections.end());
  }
  // Sharp Ray-Traced Reflections (Experimental, off: the game's look, also in Reset and every preset; ATN 2026-11-16):
  // ssr00 / ssr01 leave the reflections blur's roughness step at 0 where the rays reflect (test/tw_thrones_ssr part H).
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedSharpReflections",
      .binding = &shader_injection.rays_sharp_reflections,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Sharp Ray-Traced Reflections (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Ray-traced reflections without the game's blur on top of them: sharper.\n"
                 "Off: the game's look.",
      .is_visible = [] { return rt_sliders.reflections != 0.f && twattila::feature_settings::Upscaling(aa_sliders); },
  });
  // Path-Traced Reflection Occlusion (on, ATN 2026-11-16 "if it is more correct, add it as on default but an extra slider"):
  // Path-Traced Ambient's AO is white, the game's reflections had no occlusion; ssr00 / ssr01 take the specular occlusion
  // at this strength (0: today's, nothing traced for it; test/tw_thrones_ssr part I).
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "PathTracedReflectionOcclusion",
      .binding = &path_reflection_occlusion,
      .default_value = 1.f,
      .label = "Path-Traced Reflection Occlusion",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Reflections darken where something blocks the sky: no bright sky in dark corners.\n"
                 "0: off.",
      .max = 1.f,
      .format = "%.2f",
      .is_visible = [] {
        return rt_sliders.light_bounce == 2.f && rt_sliders.path_ambient != 0.f && rt_sliders.specular_occlusion == 0.f
               && twattila::feature_settings::Upscaling(aa_sliders);
      },
  });
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
          // The port: no lock (the plugin checks the game's bytes before it writes, so another mod's change refuses
          // the row).
          .tooltip = std::string("Sharper sun shadows that flicker less, at Shadows Max (lower Shadows settings scale\n"
                                 "with it). More video memory. Applies after a restart.")
                     + (ATTILA64 ? "" : " Locked by UI 4K Mod Compatibility."),
          .labels = {"2048 (Game's)", "4096", "8192"},
          .is_enabled = [] { return ATTILA64 || !shadow_size_locked; },
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedShadowCascades",
          .binding = &shadow_cascades_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Two Shadow Cascades Under Rays (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "With ray-traced shadows: the game prepares 2 shadow cascades, as at Shadows Medium, instead of 4.\n"
                     "The rays shadow instead; faster in forests. On, off and the rays' shadows off apply after a restart\n"
                     "or a change of the game's Shadows option. Locked by UI 4K Mod Compatibility.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return !shadow_size_locked; },
      },
      new renodx::utils::settings::Setting{
          .key = "FasterRenderLists",
          .binding = &render_lists_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Faster Render Lists",
          .section = "Graphics (Experimental)",
          .tooltip = "The game's draw lists stop rewriting a flag every CPU core reads for each item they take:\n"
                     "less waiting between the cores in big battles. Same results.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "FasterJobWakeUps",
          .binding = &job_wakeups_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Faster Job Wake-Ups (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "The game's main thread wakes its helper threads one by one; now one helper wakes the others.\n"
                     "Less CPU time on the main thread in big battles. Same results.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "FewerConstantUploads",
          .binding = &fewer_constant_uploads_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = (ATTILA64 ? 1.f : 0.f),
          .label = (ATTILA64 ? "Fewer Constant Uploads (Experimental, restart)" : "Fewer Constant Uploads (Experimental)"),
          .section = "Graphics (Experimental)",
          .tooltip = "The mod's shader settings go to the GPU once a frame instead of before every draw:\n"
                     "less waiting on the graphics driver on the render thread. Same results.\n"
                     "Turning it off takes effect after restarting the game.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "FewerDriverWaits",
          .binding = &fewer_driver_waits_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Fewer Driver Waits (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "The game's shader constants go to the GPU through one shared ring instead of a new buffer each time:\n"
                     "the render thread waits less for the graphics driver. Same results.\n"
                     "Turning it off takes effect after restarting the game.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "FasterEffectPrefetch",
          .binding = &effect_prefetch_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Faster Effect Prefetch (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "The game's effect update asks the memory for the sub-effects and particles coming up next while it works on the current one.\n"
                     "May lower the CPU waiting in big battles. Same results.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "KeepRenderCoreFree",
          .binding = &keep_render_core_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = KEEP_RENDER_CORE_DEFAULT,
          .label = "Keep Render Core Free (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "Keeps the second half of the render thread's CPU core free for it.\n"
                     "Only the game's own threads move, inside the CPU cores Windows or your own rules give the game. Same results.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "FasterEmitterPrefetch",
          .binding = &emitter_prefetch_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Faster Emitter Prefetch (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "The game's effect update asks the memory for the emitters coming up next, and for what they read first, while it\n"
                     "works on the current one. May lower the CPU waiting in big battles. Same results.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "FasterParticlePrefetch",
          .binding = &particle_prefetch_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Faster Particle Prefetch (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "The game's particle drawing asks the memory for the place it will write each particle's vertices to, as soon\n"
                     "as it knows which one. May lower the CPU waiting in big battles. Same results.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "FasterSweepPrefetch",
          .binding = &sweep_prefetch_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Faster Sweep Prefetch (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "The game's check of its pending lists asks the memory for the next lists, and what the check reads first in\n"
                     "them, while it works on the current one. May lower the CPU waiting in big battles. Same results.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "WorkerThreads",
          .binding = &worker_threads_row,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Worker Threads (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "How many threads the multi-threaded engine options may use, the render thread included.\n"
                     "With fewer than 8 CPU threads for the game they stay off, and they use at most half of them.",
          .labels = {"2", "4", "6", "8", "12", "16"},
          .parse = [](float value) { return std::array<float, 6>{2.f, 4.f, 6.f, 8.f, 12.f, 16.f}[static_cast<size_t>(value)]; },
      },
      new renodx::utils::settings::Setting{
          .key = "AnimationRowsWorkers",
          .binding = &animation_rows_row,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Animation Rows on Workers (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "Animation setup on several threads. Same picture.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "ParallelEffectUpdate",
          .binding = &effect_update_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Parallel Effect Update (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "Updates the game's effects on several threads while a battle has many of them (arrow volleys). Same picture.",
          .labels = {"Off", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "ParallelEffectSpawns",
          .binding = &effect_spawns_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Parallel Effect Spawns (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "Lets effect particles start on worker threads too. Same picture unless the game's particle limit is reached.\n"
                     "Needs Parallel Effect Update.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return effect_update_row != 0.f; },
          .is_visible = [] { return false; },
      },
      new renodx::utils::settings::Setting{
          .key = "CachedParticleCount",
          .binding = &particle_count_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Cached Particle Count (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "Counts visible particles once a frame instead of once per view. Same picture.",
          .labels = {"Off", "On"},
          .is_visible = [] { return false; },
      },
#if defined(TWATTILA_DRAW_RECORDING)
      new renodx::utils::settings::Setting{
          .key = "DrawRecordingStage",
          .binding = &draw_recording_stage_row,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 6.f,
          .label = "Threaded Draw Recording (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "The game's draw calls are prepared on worker threads: less CPU time on the render thread in big battles.\n"
                     "Same picture. One Worker and Test 1-4 are slower steps, only for tracking down a problem.",
          .labels = {"Off", "Test 1", "Test 2", "Test 3", "Test 4", "One Worker", "On"},
      },
      DrawRecordingGroupRow("DrawRecordingDepthA", "Draw Recording Depth A (Private)", 0, "The depth passes' item loop."),
      DrawRecordingGroupRow("DrawRecordingColourA", "Draw Recording Colour A (Private)", 1, "The colour passes' item loop."),
      DrawRecordingGroupRow("DrawRecordingDepthB", "Draw Recording Depth B (Private)", 2, "The depth passes' per-mesh loops."),
      DrawRecordingGroupRow("DrawRecordingColourB", "Draw Recording Colour B (Private)", 3, "The colour passes' per-mesh loops."),
      new renodx::utils::settings::Setting{
          .key = "DrawRecordingFamilies",
          .binding = &draw_recording_families_row,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 255.f,
          .label = "Draw Recording B Families (Private)",
          .section = "Graphics (Experimental)",
          .tooltip = "The per-mesh dispatchers that may record (bit f: dispatcher f). A clear bit runs its meshes directly.",
          .min = 0.f,
          .max = 255.f,
          .is_visible = [] { return false; },
      },
      new renodx::utils::settings::Setting{
          .key = "ParallelInstanceFill",
          .binding = &instance_fill_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Parallel Instance Fill (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "Copies the instance data of each draw pass on the add-on's worker threads. Same picture.",
          .labels = {"Off", "On"},
          .is_visible = [] { return false; },
      },
      new renodx::utils::settings::Setting{
          .key = "FillWithDrawRecording",
          .binding = &fill_fold_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Fill With Draw Recording (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "Copies a pass's instance data on the worker threads while the draws are recorded. Needs Parallel Instance Fill"
                     " and a Threaded Draw Recording stage that uses workers. Same picture.",
          .labels = {"Off", "On"},
          .is_visible = [] { return false; },
      },
#endif
      new renodx::utils::settings::Setting{
          .key = "RayTracedSkipShadowPass",
          .binding = &shadow_pass_row,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Skip the Game's Shadow Map Work (Experimental)",
          .section = "Graphics (Experimental)",
          .tooltip = "With ray-traced shadows: the game stops preparing most of the shadow map draws the rays replace.\n"
                     "Less CPU time in forests and big battles.",
          .labels = {"Off", "On"},
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
  auto* grass_tests_note = settings.back();
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
  auto* grass_trace_row = settings.back();
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::BUTTON,
      .label = "Grass Dump",
      .section = twattila::engine_tests::GRASS,
      .tooltip = "One frame of the grass's placement into tw_grass_dump_1.bin (2, 3, ...) next to the game's exe\n"
                 "(in a battle; a short pause while it writes).",
      .on_change = [] { twattila::graphics_hooks::DumpGrass(); },
  });
  auto* grass_dump_row = settings.back();
  // The port has no at-start writes (the plugin applies the audio values at the next start): its change callback does
  // nothing (Append's rows call it).
  if constexpr (ATTILA64) {
    twattila::engine_tests::Append(&settings, &engine_tests, [] {});
  } else {
    twattila::engine_tests::Append(&settings, &engine_tests, &SyncEarlyLoad);
  }

  // Appended Settings (Ambient Occlusion, Global Illumination)
  twattila::lighting_settings::Append(&settings, &shader_injection, [] { return rt_sliders.light_bounce != 0.f && twattila::feature_settings::Upscaling(aa_sliders); });

  // Appended Settings (Screen-Space Shadows, Anti-Aliasing: twattila/feature_settings.hpp)
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, true, true, twattila::feature_settings::AA_SECTION,
                                     &shader_injection.light_bounce_strength);
  settings.push_back(twattila::pass_timing::Button(twattila::feature_settings::AA_SECTION));
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
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = value,
        .hdr_encoding = [](int output) { return (output == twattila::output_encoding::SCRGB) ? twattila::output_encoding::SCRGB : twattila::output_encoding::HDR10; },
        .first_start_without_hdr = &ApplySdrPreset,
        .recommended_preset = &ApplyRecommendedPreset,
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
          .label = twattila::credits::Methods({.gtvbao = true, .gtvbgi = true, .auto_thickness = true, .bend = true, .smaa = true,
                                               .cmaa2 = true, .sssr = true, .detours = true}),
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

  // Ray Reconstruction's rows (shown while it is on, in the anti-aliasing's More Options).
  std::vector<renodx::utils::settings::Setting*> rr_inputs;
  if constexpr (ATTILA64) {
    rr_inputs.push_back(new renodx::utils::settings::Setting{
        .key = "RayReconstructionBeforeToneMap",
        .binding = &rr_before_tone_map_row,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = "Denoise Before Tone Mapping (Experimental)",
        .tooltip = "Ray Reconstruction cleans the scene's light before exposure, bloom and tone mapping (NVIDIA's order).\n"
                   "Can change dark areas.",
        .labels = {"Off", "On"},
    });
  }
  // Graphics 1 of 2's layout (graphics_page.hpp), then Settings Mode's pages.
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders, .sss = &sss_sliders, .rt = &rt_sliders, .ssao_mode = &shader_injection.ssao_mode,
                                             .light_bounce_strength = &shader_injection.light_bounce_strength,
                                             .ssr_hierarchical = &shader_injection.ssr_hierarchical,
                                             .smoke_strength = &smoke_map_sliders.strength, .smoke_softness = &smoke_map_sliders.softness,
                                             .lod_bias_trilinear = &lod_bias_trilinear_row, .rays_banner_light = &rays_banner_light_row,
                                             .rays_untraced_casters = &rays_untraced_casters_row, .rays_sheets_unselfed = &rt_sliders.sheets_unselfed,
                                             .engine_code = &engine_code_row, .rr_inputs = rr_inputs,
                                             .lights_in_bounce = &rt_sliders.lights_in_bounce,
                                             .light_ratio = &rt_sliders.light_ratio});
  if constexpr (ATTILA64) {
    namespace plugin = twattila::attila64_plugin;
    // The game-code rows wait for the 64-bit engine plugin: wrapped (never replaced) before AssignPages wraps them in
    // turn, so a row the plugin brings back lands on its own page. Their keys stay: saved values come back with the rows.
    // A row bound to an engine value option (the tests: TWEAKS) shows only once every value of it was found.
    const std::array<const renodx::utils::settings::Setting*, 4> keyless_rows = {ui_mod_row, graphics_status, grass_trace_row,
                                                                                 grass_dump_row};
    for (auto* setting : settings) {
      std::optional<PortRow> port_row = port_row_of(*setting);
      if (!port_row.has_value()) {
        if (std::ranges::find(keyless_rows, setting) == keyless_rows.end()) continue;
        port_row = PortRow{};
      }
      setting->is_visible = [port_row = *port_row, own = setting->is_visible] {
        return port_row.Shown() && (own == nullptr || own());
      };
    }
    // The tests' empty lines and their note, and the Experimental note: only while a value or plugin row of their section
    // shows (an all-hidden section draws no header; the grass section's Grass Height Distance shows without the plugin,
    // its note only with a grass row the plugin offers).
    for (auto* setting : settings) {
      if (setting != note
          && (!setting->key.empty() || setting->value_type != renodx::utils::settings::SettingValueType::TEXT
              || std::ranges::none_of(engine_tests, [setting](const twattila::engine_tests::EngineTest& test) {
                   return test.section == setting->section;
                 }))) {
        continue;
      }
      std::vector<const renodx::utils::settings::Setting*> section_rows;
      std::ranges::copy_if(settings, std::back_inserter(section_rows), [setting](const renodx::utils::settings::Setting* other) {
        return other->section == setting->section
               && (std::ranges::any_of(graphics_options,
                                       [other](const tw::engine_tweaks::Option* option) {
                                         return other->binding == &option->scale;
                                       })
                   || std::ranges::any_of(GAME_CODE_ROWS, [other](const GameCodeRow& row) {
                        return row.plugin_row != PluginRow::NONE && !other->key.empty() && other->key == row.match;
                      }));
      });
      setting->is_visible = [section_rows = std::move(section_rows)] {
        return std::ranges::any_of(section_rows, [](const renodx::utils::settings::Setting* row) {
          return row->is_visible == nullptr || row->is_visible();
        });
      };
    }
    constexpr uint32_t RESTART_TINT = 0xFF4F4F;  // the UI 4K Mod Compatibility row's restart text
    // The audio rows' restart note, after the last of them (before the section's empty line).
    settings.insert(std::ranges::find_if(settings,
                                         [](const renodx::utils::settings::Setting* setting) {
                                           return setting->section == twattila::engine_tests::AUDIO && setting->key.empty();
                                         }),
                    new renodx::utils::settings::Setting{
                        .value_type = renodx::utils::settings::SettingValueType::TEXT,
                        .label = RESTART_NOW,
                        .section = twattila::engine_tests::AUDIO,
                        .tint = RESTART_TINT,
                        .is_visible = [] { return twattila::attila64_plugin_link::ValuesPending(); },
                    });
    // Shadow Map Size's restart note, right after its row and in its section (the plugin patches the sizes at the next
    // start): while the row shows.
    const auto shadow_map_row = std::ranges::find(settings, "GraphicsShadowMapSize", &renodx::utils::settings::Setting::key);
    settings.insert(std::next(shadow_map_row),
                    new renodx::utils::settings::Setting{
                        .value_type = renodx::utils::settings::SettingValueType::TEXT,
                        .label = RESTART_NOW,
                        .section = (*shadow_map_row)->section,
                        .tint = RESTART_TINT,
                        .is_visible = [row = *shadow_map_row] {
                          return (row->is_visible == nullptr || row->is_visible())
                                 && twattila::attila64_plugin_link::ShadowMapSizePending();
                        },
                    });
    // Loader Frame Cap beside Frame Rate Limit, with its restart note (a v2 plugin reads it at the next start).
    const auto loader_frame_cap_shown = [] {
      return PLUGIN_READY_FORCED
             || (twattila::attila64_plugin_link::Result() == twattila::attila64_plugin_link::Found::READY
                 && twattila::attila64_plugin_link::BlockV2());
    };
    const auto frame_limit = std::ranges::find(settings, "FrameGenerationFrameLimit", &renodx::utils::settings::Setting::key);
    const std::string frame_limit_section = (*frame_limit)->section;
    settings.insert(std::next(frame_limit),
                    {
                        new renodx::utils::settings::Setting{
                            .key = "LoaderFrameCap",
                            .binding = &loader_frame_cap_row,
                            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                            .default_value = 0.f,
                            .label = "Loader Frame Cap (restart)",
                            .section = frame_limit_section,
                            .tooltip = "The 64-bit loader's own 240 fps cap. Off: no loader cap; use V-Sync or a\n"
                                       "frame limit instead. Applies after a restart.",
                            .labels = {"Off", "On"},
                            .is_visible = loader_frame_cap_shown,
                        },
                        new renodx::utils::settings::Setting{
                            .value_type = renodx::utils::settings::SettingValueType::TEXT,
                            .label = RESTART_NOW,
                            .section = frame_limit_section,
                            .tint = RESTART_TINT,
                            .is_visible = [loader_frame_cap_shown] {
                              return loader_frame_cap_shown() && twattila::attila64_plugin_link::SolFrameCapPending();
                            },
                        },
                    });
    // The plugin's note in graphics_status's place (the engine values it reports are the 32-bit game's): one row per
    // lookup result, at most one shown.
    auto note_at = std::ranges::find(settings, graphics_status);
    for (const PluginNote& note : PLUGIN_NOTES) {
      note_at = std::next(settings.insert(
          note_at, new renodx::utils::settings::Setting{
                       .value_type = renodx::utils::settings::SettingValueType::TEXT,
                       .label = note.label,
                       .section = graphics_status->section,
                       .tint = twattila::graphics_page::WARNING_TINT,
                       .is_visible = [note] {
                         const twattila::attila64_plugin_link::Found result = twattila::attila64_plugin_link::Result();
                         if (result != note.found) return false;
                         if (result != twattila::attila64_plugin_link::Found::READY) return true;
                         if (twattila::attila64_plugin_link::BlockV2()) {
                           return !note.v1_block && twattila::attila64_plugin_link::OptionsRefused();
                         }
                         return note.v1_block;
                       },
                   }));
    }
    // Engine Performance Fix's FrameBridge upscaling (beside this mod's helper) and its waving flags, as this start has
    // them: warnings at the top of General.
    settings.insert(
        std::ranges::find(settings, "General", &renodx::utils::settings::Setting::section),
        {
            twattila::graphics_page::Note(
                "FrameBridge upscaling is on: untick Upscaling in the start window. It clashes with this mod's DLSS and "
                "frame generation.",
                "General", twattila::graphics_page::WARNING_TINT,
                [] { return framebridge_on.load(std::memory_order_relaxed) && twattila::dlaa_bridge::HelperInstalled(); }),
            twattila::graphics_page::Note(
                "Waving flags (Engine Performance Fix) is on: flags and props smear with DLSS, FSR, XeSS and TAA. Untick "
                "Waving flags in the start window.",
                "General", twattila::graphics_page::WARNING_TINT,
                [] { return waving_flags_on.load(std::memory_order_relaxed); }),
        });
    // The CPU page (Settings Mode 3), its section "CPU": every CPU row of this mod on the port, new CPU rows too (live
    // rows plain, a restart-only one with "(restart)" in its label and its restart note after it, a row live one way and
    // restart-only the other with "(restart)" in its label and its tooltip saying which: Fewer Constant Uploads). The
    // engine performance fix's and the start window's sections wait for ATN's answer on where their keys come from.
    constexpr const char* CPU_SECTION = "CPU";
    std::vector<renodx::utils::settings::Setting*> cpu_rows;
    // Keep Battle Core Free and its status line, while the plugin offers the CPU row and built its battle part (state 0:
    // not built). The graphics preset buttons leave the row alone (not a look setting); it is saved per preset slot like
    // every row.
    const auto battle_core_shown = [] {
      return GameCodeRowShown(PluginRow::CPU_PLACEMENT)
             && ((twattila::attila64_plugin_link::BattleCoreStatus() >> plugin::CORE_STATE_SHIFT)
                 & plugin::CORE_FIELD_MASK)
                    != 0;
    };
    cpu_rows.insert(
        cpu_rows.end(),
        {
            new renodx::utils::settings::Setting{
                .key = "KeepBattleCoreFree",
                .binding = &keep_battle_core_row,
                .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                .default_value = KEEP_BATTLE_CORE_DEFAULT,
                .label = "Keep Battle Core Free",
                .section = CPU_SECTION,
                .tooltip = "Big battles can run faster: the battle simulation gets a CPU core to itself. Same picture.\n"
                           "Changes take effect at once. If it says stopped, restart the game to try again.",
                .labels = {"Off", "On"},
                .is_visible = battle_core_shown,
            },
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
                .section = CPU_SECTION,
                .on_draw = [] {
                  const uint64_t status = twattila::attila64_plugin_link::BattleCoreStatus();
                  const auto field = [status](uint32_t shift) {
                    return static_cast<uint32_t>(status >> shift) & plugin::CORE_FIELD_MASK;
                  };
                  const uint32_t state = field(plugin::CORE_STATE_SHIFT);
                  // A state appended by a newer plugin: no line.
                  if (state >= plugin::CORE_STATE_COUNT) return false;
                  const uint32_t logical_a = field(plugin::CORE_LOGICAL_A_SHIFT);
                  const uint32_t logical_b = field(plugin::CORE_LOGICAL_B_SHIFT);
                  const auto error =
                      static_cast<uint32_t>((status >> plugin::CORE_ERROR_SHIFT) & plugin::CORE_ERROR_MASK);
                  // WAITING also holds while the row is off: the plugin waits for its bit.
                  const bool off = (state == plugin::CORE_WAITING && keep_battle_core_row == 0.f);
                  const char* format = (off ? "off" : CORE_STATE_TEXTS[state]);
                  ImGui::TextWrapped("Battle core: %s",
                                     std::vformat(format, std::make_format_args(logical_a, logical_b, error)).c_str());
                  return false;
                },
                .is_visible = battle_core_shown,
            },
        });
    // Keep Render Core Free (its 32-bit place: Graphics (Experimental)) and its status line the same way, for the render
    // part (render_core_status), once its A/B measured it. The toggle hides with RenderCore=0 (the plugin's own off for
    // the process: the row cannot turn it on); its line says so.
    const auto render_core_state = [] {
      return (twattila::attila64_plugin_link::RenderCoreStatus() >> plugin::CORE_STATE_SHIFT) & plugin::CORE_FIELD_MASK;
    };
    const auto render_core_shown = [render_core_state] {
      return RENDER_CORE_ROW_MEASURED && GameCodeRowShown(PluginRow::CPU_PLACEMENT) && render_core_state() != 0;
    };
    const auto render_core = std::ranges::find(settings, "KeepRenderCoreFree", &renodx::utils::settings::Setting::key);
    renodx::utils::settings::Setting* render_core_row = *render_core;
    settings.erase(render_core);
    render_core_row->label = "Keep Render Core Free";
    render_core_row->section = CPU_SECTION;
    render_core_row->tooltip = "Can raise the frame rate in big battles: the render thread gets a CPU core to itself. Same picture.\n"
                               "Changes take effect at once. If it says stopped, restart the game to try again.";
    render_core_row->is_visible = [render_core_shown, render_core_state] {
      return render_core_shown() && render_core_state() != plugin::CORE_OFF_INI;
    };
    cpu_rows.insert(
        cpu_rows.end(),
        {
            render_core_row,
            new renodx::utils::settings::Setting{
                .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
                .section = CPU_SECTION,
                .on_draw = [] {
                  const uint32_t status = twattila::attila64_plugin_link::RenderCoreStatus();
                  const auto field = [status](uint32_t shift) { return (status >> shift) & plugin::CORE_FIELD_MASK; };
                  const uint32_t state = field(plugin::CORE_STATE_SHIFT);
                  // A state appended by a newer plugin: no line.
                  if (state >= plugin::CORE_STATE_COUNT) return false;
                  const uint32_t logical_a = field(plugin::CORE_LOGICAL_A_SHIFT);
                  const uint32_t logical_b = field(plugin::CORE_LOGICAL_B_SHIFT);
                  const uint32_t error = field(plugin::RENDER_ERROR_SHIFT);
                  const char* format = CORE_STATE_TEXTS[state];
                  switch (state) {
                    // WAITING, and WITH_BATTLE (a battle core gate or end), also hold while the bit is clear (the row
                    // Off, or the plugin's default kept): off then.
                    case plugin::CORE_WAITING:
                    case plugin::CORE_WITH_BATTLE:
                      if (!twattila::attila64_plugin_link::RenderCoreWanted()) {
                        format = "off";
                      }
                      break;
                    // The shared text says the core is kept free; this one is the render thread's.
                    case plugin::CORE_ACTIVE:
                      format = "on: logical CPUs {0} + {1} kept for the render thread";
                      break;
                    // One byte: RENDER_ERROR_LARGE stands for a larger error, which the plugin's log has.
                    case plugin::CORE_FAILED:
                      if (error == plugin::RENDER_ERROR_LARGE) {
                        format = "stopped: Windows refused (see attila64_log.txt)";
                      }
                      break;
                    default:
                      break;
                  }
                  ImGui::TextWrapped("Render core: %s",
                                     std::vformat(format, std::make_format_args(logical_a, logical_b, error)).c_str());
                  return false;
                },
                .is_visible = render_core_shown,
            },
        });
    // Effects on All Cores (PK9 7.3 / 10; the 32-bit Parallel Effect Update / Spawns stay hidden here: GAME_CODE_ROWS),
    // live: shown while the plugin offers its walk hook and its loader slot's verdict is OK.
    cpu_rows.push_back(new renodx::utils::settings::Setting{
        .key = "EffectsOnAllCores",
        .binding = &effects_on_all_cores_row,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Effects on All Cores (Experimental)",
        .section = CPU_SECTION,
        .tooltip = "Runs battle effects on all CPU cores. Faster in big battles; small effect details can change.\n"
                   "Automatic: only in big battles.",
        .labels = {"Off", "Automatic", "On"},
        .is_visible = [] {
          return GameCodeRowShown(PluginRow::PARALLEL_EFFECTS)
                 && (PLUGIN_READY_FORCED || twattila::attila64_plugin_link::EffectsSlotOk());
        },
    });
    // The engine rows (PK8 11.1 Faster Battle Step, PK8 Stage B 14 Multithreaded Collisions) and their status lines,
    // live, while the plugin built the row's lane (0: not built), once their batch measured them. The toggle hides
    // while the row cannot run (its key 0 in the plugin's ini, or the game's code differs); its line says why. The
    // graphics preset buttons leave the rows alone (not look settings).
    struct EngineRow {
      const char* key = nullptr;
      float* binding = nullptr;
      float default_value = 0.f;
      const char* label = nullptr;
      const char* tooltip = nullptr;
      PluginRow plugin_row = PluginRow::NONE;
      uint32_t lane_shift = 0;
      const EngineTexts* texts = nullptr;
      const char* status_prefix = nullptr;
      bool measured = false;
    };
    const auto add_engine_row = [&cpu_rows](const EngineRow& row) {
      const auto status_shown = [row] {
        return row.measured
               && twattila::attila64_plugin_link::Result() == twattila::attila64_plugin_link::Found::READY
               && twattila::attila64_plugin_link::EngineLaneState(row.lane_shift) != 0;
      };
      cpu_rows.insert(
          cpu_rows.end(),
          {
              new renodx::utils::settings::Setting{
                  .key = row.key,
                  .binding = row.binding,
                  .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                  .default_value = row.default_value,
                  .label = row.label,
                  .section = CPU_SECTION,
                  .tooltip = row.tooltip,
                  .labels = {"Off", "On"},
                  .is_visible = [row, status_shown] {
                    const uint32_t state = twattila::attila64_plugin_link::EngineLaneState(row.lane_shift);
                    return status_shown() && GameCodeRowShown(row.plugin_row)
                           && state != plugin::ENGINE_OFF_INI && state != plugin::ENGINE_CODE_DIFFERS;
                  },
              },
              new renodx::utils::settings::Setting{
                  .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
                  .section = CPU_SECTION,
                  .on_draw = [row] {
                    const uint32_t state = twattila::attila64_plugin_link::EngineLaneState(row.lane_shift);
                    // 0 (not built) or a state appended by a newer plugin: no line.
                    if (state == 0 || state >= row.texts->size()) return false;
                    const char* text = (*row.texts)[state];
                    // The plugin changes its lane at the hook's next call: the row's Off reads off at once, its On
                    // as the plugin's next state (OFF -> NOT_USED_YET).
                    if (*row.binding == 0.f && state < plugin::ENGINE_OFF) {
                      text = (*row.texts)[plugin::ENGINE_OFF];
                    } else if (*row.binding != 0.f && state == plugin::ENGINE_OFF) {
                      text = (*row.texts)[plugin::ENGINE_NOT_USED_YET];
                    }
                    ImGui::TextWrapped("%s: %s", row.status_prefix, text);
                    return false;
                  },
                  .is_visible = status_shown,
              },
          });
    };
    add_engine_row({
        .key = "FasterBattleStep",
        .binding = &faster_battle_step_row,
        .default_value = FASTER_BATTLE_STEP_DEFAULT,
        .label = "Faster Battle Step",
        .tooltip = "Speeds up how units steer around each other in big battles. Same battle results.",
        .plugin_row = PluginRow::BATTLE_STEP,
        .lane_shift = plugin::ENGINE_STEP_SHIFT,
        .texts = &BATTLE_STEP_TEXTS,
        .status_prefix = "Battle step",
        .measured = BATTLE_STEP_ROW_MEASURED,
    });
    // src: PK8 Stage B 14 (name, tooltip, default Off; the texts in CONTACT_SPLIT_TEXTS).
    add_engine_row({
        .key = "MultithreadedCollisions",
        .binding = &multithreaded_collisions_row,
        .default_value = 0.f,
        .label = "Multithreaded Collisions (Experimental)",
        .tooltip = "Checks soldier collisions on more CPU threads. Same battle results.",
        .plugin_row = PluginRow::CONTACT_SPLIT,
        .lane_shift = plugin::ENGINE_SPLIT_SHIFT,
        .texts = &CONTACT_SPLIT_TEXTS,
        .status_prefix = "Collisions",
        .measured = CONTACT_SPLIT_ROW_MEASURED,
    });
    // The renderer's CPU row (its 32-bit place: Graphics 2 of 2). Fewer Driver Waits stays there, hidden (GAME_CODE_ROWS).
    const auto moved = std::ranges::find(settings, "FewerConstantUploads", &renodx::utils::settings::Setting::key);
    (*moved)->section = CPU_SECTION;
    cpu_rows.push_back(*moved);
    settings.erase(moved);
    // The game code row (its 32-bit place: Ray Tracing's More Options), restart-only: the plugin writes its code at its
    // init. Shown while the plugin offers the row (a player's Off keeps it offered), its restart note after it.
    const auto game_code = std::ranges::find(settings, "FasterGameCpuCode", &renodx::utils::settings::Setting::key);
    renodx::utils::settings::Setting* game_code_row = *game_code;
    settings.erase(game_code);
    game_code_row->label = "Faster Game CPU Code (restart)";
    game_code_row->section = CPU_SECTION;
    game_code_row->tooltip = "Faster versions of some of the game's own CPU code (sprite soldiers), same results.";
    game_code_row->is_visible = [] { return GameCodeRowShown(PluginRow::GAME_CODE); };
    cpu_rows.insert(cpu_rows.end(),
                    {
                        game_code_row,
                        new renodx::utils::settings::Setting{
                            .value_type = renodx::utils::settings::SettingValueType::TEXT,
                            .label = RESTART_NOW,
                            .section = CPU_SECTION,
                            .tint = RESTART_TINT,
                            .is_visible = [row = game_code_row] {
                              return row->is_visible() && twattila::attila64_plugin_link::GameCodePending();
                            },
                        },
                    });
    settings.insert(std::ranges::find(settings, "About", &renodx::utils::settings::Setting::section), cpu_rows.begin(),
                    cpu_rows.end());
    renodx::utils::settings::section_tints.emplace_back(CPU_SECTION, 0xF0E68Cu);
    (*std::ranges::find(settings, "SettingsMode", &renodx::utils::settings::Setting::key))->labels.push_back("CPU");
    // The other pages as on 32-bit, then the CPU rows on page 3 (AssignPages' wrap for a page of its own).
    std::vector<renodx::utils::settings::Setting*> paged_rows;
    std::ranges::copy_if(settings, std::back_inserter(paged_rows), [](const renodx::utils::settings::Setting* setting) {
      return setting->section != CPU_SECTION;
    });
    twattila::feature_settings::AssignPages(&paged_rows, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());
    for (auto* setting : cpu_rows) {
      setting->is_visible = [mode = &current_settings_mode, shown = setting->is_visible] {
        return static_cast<int>(*mode) == 3 && (shown == nullptr || shown());
      };
    }
  } else {
    twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());
  }

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;
  twattila::pass_timing::Install("attila");

  // Failed native-hook removal can retain this DLL across host unload/reload.
  graphics_found = false;
  graphics_hooks_installed = false;

  ConfigureAddon();

  if constexpr (ATTILA64) {
    // Who loaded the add-on: the exe's name and its version resource (kernel32 only). Nothing is read at the game's
    // range here: the loader reserves it no-access until its game thread maps the game (the first present reads it).
    std::array<wchar_t, MAX_PATH> exe_path = {};
    GetModuleFileNameW(nullptr, exe_path.data(), static_cast<DWORD>(exe_path.size()));
    std::wstring_view exe_file(exe_path.data());
    exe_file.remove_prefix(std::min(exe_file.size(), exe_file.find_last_of(L"\\/") + 1));
    std::string exe_name(WideCharToMultiByte(CP_UTF8, 0, exe_file.data(), static_cast<int>(exe_file.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, exe_file.data(), static_cast<int>(exe_file.size()), exe_name.data(), static_cast<int>(exe_name.size()),
                        nullptr, nullptr);
    const bool port_exe = CompareStringOrdinal(exe_file.data(), static_cast<int>(exe_file.size()), L"attila64.exe", -1, TRUE) == CSTR_EQUAL;
    std::string version = "unknown";
    bool port_product = false;
    // RT_VERSION is 16 (its macro is the ANSI form in this build).
    if (HRSRC info = FindResourceW(nullptr, MAKEINTRESOURCEW(VS_VERSION_INFO), MAKEINTRESOURCEW(16)); info != nullptr) {
      const auto* bytes = static_cast<const std::byte*>(LockResource(LoadResource(nullptr, info)));
      const size_t size = SizeofResource(nullptr, info);
      // VS_FIXEDFILEINFO is DWORD-aligned and starts with its signature; ProductName's value is UTF-16 text.
      for (size_t at = 0; bytes != nullptr && at + sizeof(VS_FIXEDFILEINFO) <= size; at += 4) {
        VS_FIXEDFILEINFO fixed;
        std::memcpy(&fixed, bytes + at, sizeof(fixed));
        if (fixed.dwSignature != VS_FFI_SIGNATURE) continue;
        version = std::format("{}.{}.{}.{}", HIWORD(fixed.dwFileVersionMS), LOWORD(fixed.dwFileVersionMS), HIWORD(fixed.dwFileVersionLS),
                              LOWORD(fixed.dwFileVersionLS));
        break;
      }
      port_product = (bytes != nullptr && std::wstring_view(reinterpret_cast<const wchar_t*>(bytes), size / sizeof(wchar_t)).find(L"64-bit Attila") != std::wstring_view::npos);
    }
    renodx::utils::log::i("[Total War] 64-bit Attila: ", exe_name, ", loader ", version);
    if (!port_exe || !port_product) {
      renodx::utils::log::w("[Total War] 64-bit Attila: not the port's exe or product name (", exe_name,
                            "): the add-on runs as on the port");
    }
    // The port's other renderers: swapchain_v2 leaves their swap chains alone. Before swapchain::Use reads it.
    renodx::mods::swapchain::ignored_device_apis = {reshade::api::device_api::d3d9, reshade::api::device_api::d3d10,
                                                    reshade::api::device_api::d3d12, reshade::api::device_api::opengl,
                                                    reshade::api::device_api::vulkan};
  }

  twattila::device_loss::Install();
  // The game's constant buffers are known from their creation on (the row may come on later).
  twattila::buffer_pool::Use();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  if constexpr (ATTILA64) {
    // Before swapchain::Use's resource upgrade registers its copy_resource: the clone is on when it copies.
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyPresentPicture);
    reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyPresentPicture);
    reshade::register_event<reshade::addon_event::update_texture_region>(OnUpdateOverlay);
  }
  // A bench's live env keys bump the pool's env generation; its mt_trace key opens one pool trace window.
  twattila::test_hooks::Install(&settings, true, "[Total War]",
                                {.env_changed = [] { return worker_env_generation.fetch_add(1, std::memory_order_relaxed) + 1; },
                                 .arm_trace = twattila::worker_pool::ArmTrace});
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::smoke_map::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ssr_hiz::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::grass_fade::OnDestroyDevice);

  // Rows saved at an old default (a preset saves every row, hidden ones too), each cleared once. 1 (before v149): NRD when
  // Ray Reconstruction is off, the banners' light and colour on. 2 (the render_mt defaults): the rows whose default changed
  // and the rows hidden now (a value saved earlier would stay on unseen). The 64-bit port has its own key,
  // Attila64SettingsRevision (32-bit never reads it): 1, the 64-bit port's first revision, Fewer Constant Uploads, on there
  // by default. A later port default change takes the next number.
  twattila::graphics_page::ClearSavedRows(1, {"RayTracingDenoiser", "ScreenSpaceShadowsBannerLight", "ScreenSpaceShadowsColoured"});
  twattila::graphics_page::ClearSavedRows(2, {"DrawRecordingStage", "DrawRecordingDepthA", "DrawRecordingColourA", "DrawRecordingDepthB",
                                              "DrawRecordingColourB", "DrawRecordingFamilies", "AnimationRowsWorkers", "ParallelInstanceFill",
                                              "FillWithDrawRecording", "ParallelEffectSpawns", "CachedParticleCount"});
  if constexpr (ATTILA64) {
    twattila::graphics_page::ClearSavedRows(1, {"FewerConstantUploads"}, "Attila64SettingsRevision");
  }

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  if (!exit_job_drain_on) {
    renodx::utils::log::i("[Total War] Exit Job Drain: off (RENODX_TW_EXIT_DRAIN=0)");
  }
  if (game_cpus_env) {
    renodx::utils::log::i(std::format("[Total War] CPUs for the game: {} from RENODX_TW_GAME_CPUS (bench only).", *game_cpus_env));
  } else if (GetEnvironmentVariableA("RENODX_TW_GAME_CPUS", nullptr, 0) != 0) {
    renodx::utils::log::w("[Total War] RENODX_TW_GAME_CPUS does not parse; ignored.");
  }

  if constexpr (ATTILA64) {
    // The port writes no at-start test (the plugin reads the audio values at its init). A ReShade.ini carried over from
    // the 32-bit game may still list the add-on in [ADDON] LoadFromDllMain: said at the attach, left as it is.
    std::array<char, 4096> listed = {};
    size_t listed_size = listed.size();
    if (reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", listed.data(), &listed_size)) {
      std::array<wchar_t, MAX_PATH> addon_path = {};
      GetModuleFileNameW(addon_module_handle, addon_path.data(), static_cast<DWORD>(addon_path.size()));
      const std::string addon_file = std::filesystem::path(addon_path.data()).filename().string();
      for (size_t offset = 0; offset < listed_size && listed[offset] != '\0'; offset += std::strlen(listed.data() + offset) + 1) {
        if (addon_file != listed.data() + offset) continue;
        renodx::utils::log::i("[Total War] 64-bit Attila: ReShade.ini [ADDON] LoadFromDllMain lists this add-on (from the "
                              "32-bit game): left as it is; not used on 64-bit");
        break;
      }
    }
  } else {
    // The at-start tests (audio memory, read once as the game's audio starts): written as soon as the game has set its
    // own values, on a thread of their own (the addon may load before the game's code runs: SyncEarlyLoad). Up to a
    // minute.
    SyncEarlyLoad();
    twattila::engine_tests::StartAtStart(L"empire.retail.dll", &engine_tests, &at_start_done, &at_start_stop);
  }

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
#if !defined(_WIN64)
  // After swapchain::Use's present callback: the address space's chain after its fake fullscreen's apply.
  reshade::register_event<reshade::addon_event::present>(twattila::rt_scene::OnPresentAddressSpace);
#endif

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  twattila::draw_dispatch::Use(DLL_PROCESS_ATTACH);
  // Before the Light Bounce's (its callback draws the sun draw again).
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit_recorded,
                                                        .unit_records_compute_shader = __motion_unit_records,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, RENDER_SCALE_VERTEX_SHADERS, FULL_DEPTH_SHADERS,
                                   twattila::view_share::WHOLE_TEXTURES, {.on_demand = true, .at_100 = true});
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
  twattila::grass_fade::Install(GRASS_FADE_SHADERS);
  // The AO pass before the ray tracing (its multiply after the pass's own output), both before the Light Bounce (it
  // draws the sun draws itself: no callback after it runs).
  twattila::ao_pass::Install(AO_DRAWS, &shader_injection, sizeof(shader_injection));
  // Batched Unit Capture's corners are made before each bone pack (graphics_hooks.hpp OnPackAnimations), where it is hooked.
  twattila::rt_scene::Host host = twattila::rt_scene::TW_HOST;
  host.flushes_unit_captures = twattila::graphics_hooks::PacksHooked;
  host.scene_release = true;
  twattila::rt_scene::Install(SCENE_SHADERS, AO_DRAWS, host);
  twattila::rt_scene::InstallCutOuts(0xF79BFB14, UNIT_CUT_OUT_PIXEL_SHADERS);  // rigidcommon ps30_shadow_alpha_test
  for (const uint32_t shader_hash : CAMPAIGN_SHADERS) {
    renodx::utils::command_action::Register(
        OnCampaignDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  for (const uint32_t shader_hash : GRASS_VERTEX_SHADERS) {
    renodx::utils::command_action::Register(
        OnGrassDumpDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  // The point lights' ray-traced shadows (their light shaders replaced: shaders/pointlight00-07), and with the game's point
  // light shadows (pointlight08-39, after them: their hook reads what InstallPointLights' bound).
  twattila::rt_scene::InstallPointLights(twattila::rt_scene::POINT_LIGHT_SHADERS);
  twattila::rt_scene::InstallPointShadowLights(&rt_sliders.game_point_shadows);
  twattila::rt_scene::InstallLinearFalloff(twattila::rt_scene::LINEAR_FALLOFF_LIGHT_SHADERS);
  // The battle fog's ray-traced shadows (its shader replaced: shaders/battlefog00).
  twattila::rt_scene::InstallFogShadows(twattila::rt_scene::FOG_SHADERS);
  // Smoke's ray-traced shadows (the lit particles streamed out).
  twattila::rt_scene::InstallSmokeShadows(twattila::rt_scene::PARTICLE_VERTEX_SHADERS, twattila::rt_scene::LIT_PARTICLE_PIXEL_SHADERS);
  // Ray-Traced Shadows Only: the sun's shadow map's casters skipped (the map the sun draws read); the forward passes that
  // read it (near grass, particles) take the rays' shadows instead.
  twattila::rt_scene::InstallShadowsOnly();
  if constexpr (ATTILA64) {
    // The port: shadow_pass at the lookup that finds the 64-bit engine plugin ready (attila64_plugin_link::Lookup).
    twattila::attila64_plugin_link::Install();
  } else {
    twattila::shadow_pass::Install();
  }
  twattila::screen_space_shadows::PatchForwardShadows(twattila::rt_scene::SHADOW_READER_SHADERS);
  twattila::rt_scene::InstallShadowReaders(twattila::rt_scene::SHADOW_READER_SHADERS);
  twattila::rt_scene::InstallShadowReaders(twattila::rt_scene::PARTICLE_BUILD_READERS);
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);
  twattila::screen_space_shadows::SetSmokeOcclusion(&twattila::smoke_map::Occlusion);
  // After the smoke's capture on the same draws (it sees the game's targets and camera).
  twattila::particle_layer::Install(twattila::rt_scene::PARTICLE_VERTEX_SHADER_HASHES, PARTICLE_LAYER_READERS);

  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::particle_layer::OnDestroyDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  twattila::buffer_pool::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::particle_layer::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  twattila::particle_layer::Uninstall();
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  if constexpr (ATTILA64) {
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyPresentPicture);
    reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyPresentPicture);
    reshade::unregister_event<reshade::addon_event::update_texture_region>(OnUpdateOverlay);
    // Before the modules its callbacks call are uninstalled (a present still in flight looks up and registers nothing
    // after it).
    twattila::attila64_plugin_link::Uninstall();
  }
  twattila::cpu_sets::Stop();
  twattila::test_hooks::Uninstall();
  twattila::pass_timing::Uninstall();
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
  twattila::effect_update::Detach();
  twattila::engine_code::Uninstall();
#if defined(TWATTILA_DRAW_RECORDING)
  // A fold still open on this thread is closed before the pool stops (its workers wait for the publish).
  twattila::draw_recording::CloseFoldAtPresent();
#endif
  // Before the users' Uninstall frees their memory and the module reference: a job open on another thread keeps the pool (and
  // the users keep their memory then).
  twattila::worker_pool::Stop();
#if defined(TWATTILA_DRAW_RECORDING)
  // After the engine code's Uninstall: the memory the sites use goes only when none is left in.
  twattila::draw_recording::Uninstall(twattila::engine_code::DrawRecordingSitesIn());
#endif
  twattila::graphics_hooks::Uninstall();
  twattila::shadow_pass::Uninstall();
  twattila::graphics_hooks::shadow_map::Uninstall();
  renodx::utils::command_action::Unregister(OnCampaignDraw);
  renodx::utils::command_action::Unregister(OnGrassDumpDraw);
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

  twattila::draw_dispatch::Use(DLL_PROCESS_DETACH);
  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);

#if !defined(_WIN64)
  reshade::unregister_event<reshade::addon_event::present>(twattila::rt_scene::OnPresentAddressSpace);
#endif
  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);

  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);

  if constexpr (ATTILA64) {
    stood_down_apis = 0;
    identity_read = false;
    framebridge_on = false;
    waving_flags_on = false;
    twattila::frame_trace::SetGameCode("", 0, {});
    present_picture = 0;
    picture_warned = false;
    buffer_overlay_logged = false;
    picture_overlay_logged = false;
    fewer_constant_uploads_logged = false;
    tone_mapper_target_warned = false;
  }

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
