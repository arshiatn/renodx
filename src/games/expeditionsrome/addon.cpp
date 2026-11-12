/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2

#include <algorithm>
#include <cmath>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "../twattila/credits.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;

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

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, UE's film curve).
constexpr float HUE_CLIP_DEFAULT = 70.f;

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
      {"highlight_saturation", 1.f},
      {"roll_off", 1.f},
      // {"game_shadows", 0.f},
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"sharpening", 0.f},
      {"film_grain", 1.f},
      {"chromatic_aberration", 1.f},
      {"godrays", 1.f},
      {"volumetric_fog", 1.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

// Moves the bound render targets to their 16-bit clones (hot swap), so the HDR frame
// written here (and read by the next passes) is not clipped to 8/10-bit UNORM.
bool ActivateRenderTargetClones(reshade::api::command_list* cmd_list) {
  auto rtvs = renodx::utils::swapchain::GetRenderTargets(cmd_list);
  bool changed = false;
  for (auto rtv : rtvs) {
    changed |= renodx::mods::swapchain::ActivateCloneHotSwap(cmd_list->get_device(), rtv);
  }
  if (changed) {
    renodx::mods::swapchain::FlushDescriptors(cmd_list);
    renodx::mods::swapchain::RewriteRenderTargets(
        cmd_list, rtvs.size(), rtvs.data(), renodx::utils::swapchain::GetDepthStencil(cmd_list));
  }
  return true;
}

// Replaced shader that writes the post-tonemap frame.
#define UpgradeRTVReplaceShader(value)            \
  {                                               \
      value,                                      \
      {                                           \
          .crc32 = value,                         \
          .code = __##value,                      \
          .on_draw = &ActivateRenderTargetClones, \
      },                                          \
  }

// Stock shader (not replaced) that writes the post-tonemap frame.
#define UpgradeRTVShader(value)                   \
  {                                               \
      value,                                      \
      {                                           \
          .crc32 = value,                         \
          .on_draw = &ActivateRenderTargetClones, \
      },                                          \
  }

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      UpgradeRTVShader(0x8533AF0C),         // fxaa00
      UpgradeRTVShader(0x75751BBD),         // copy01 (viewport-clamped copy / upscale)
      UpgradeRTVShader(0xE6551133),         // copy00
      UpgradeRTVReplaceShader(0x4DF6E0CF),  // outline00 (post process material)
      UpgradeRTVReplaceShader(0x84B3F0E5),  // outline01 (outline00 with view-rect UV clamps)
      // Tonemapper permutations (features in each file).
      UpgradeRTVReplaceShader(0x316D5B96),  // t00 (vignette)
      UpgradeRTVReplaceShader(0x70CBF75E),  // t01
      UpgradeRTVReplaceShader(0x3BAE1754),  // t02
      UpgradeRTVReplaceShader(0xC4ABB3CC),  // t03
      UpgradeRTVReplaceShader(0x3D18E77B),  // t04
      UpgradeRTVReplaceShader(0xA77C9003),  // t05
      UpgradeRTVReplaceShader(0xB176941C),  // t06
      UpgradeRTVReplaceShader(0xAAAEB4F0),  // t07
      UpgradeRTVReplaceShader(0x2501C25F),  // t08
      UpgradeRTVReplaceShader(0x37FDFDF2),  // t09
      UpgradeRTVReplaceShader(0x57737062),  // sharpen00 (CAS)
      UpgradeRTVReplaceShader(0xEDD3DA15),  // cutscene00 (Slate cutscene images)
      CustomShaderEntry(0x61F067B4),         // lut00 (CombineLUTs)
      CustomShaderEntry(0x3938E429),         // lut01 (+ 1 SDR LUT)
      CustomShaderEntry(0xDF7A41F2),         // lut02 (+ 2 SDR LUTs)
      CustomShaderEntry(0xB50332F4),         // lut03 (+ 3 SDR LUTs)
      CustomShaderEntry(0x50964B52),         // lut04 (+ 4 SDR LUTs)
      CustomShaderEntry(0x1E796BD3),         // godrays01
      CustomShaderEntry(0xCDCA640F),         // fog07 (height + volumetric fog)
      CustomShaderEntry(0xBCD37CAF),         // fog08 (height + volumetric fog, directional)
      CustomShaderEntry(0x31A1C68C),         // ao00 (ambient occlusion, last level: GT-VBAO)
      CustomShaderEntry(0xBD381E21),         // ao01
  };

  // 0-2 slider, 1 = default.
  auto slider = [](const std::string& key, float* binding, const std::string& label, const std::string& section,
                   const std::string& tooltip, bool (*is_enabled)(), bool (*is_visible)()) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = 1.f,
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .min = 0.f,
        .max = 2.f,
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
          .key = "peak_white_nits",
          .binding = &shader_injection.peak_white_nits,
          .default_value = 1000.f,
          .can_reset = false,
          .label = "Peak",
          .section = "General",
          .tooltip = "Maximum brightness output in nits.",
          .min = 400.f,
          .max = 10000.f,
      },
      new renodx::utils::settings::Setting{
          .key = "diffuse_white_nits",
          .binding = &shader_injection.diffuse_white_nits,
          .default_value = 203.f,
          .label = "Paper White",
          .section = "General",
          .tooltip = "Brightness of the scene/game and cutscenes.",
          .min = 1.f,
          .max = 500.f,
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
      },
      new renodx::utils::settings::Setting{
          .key = "tonemapper",
          .binding = &shader_injection.hdr,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Rendering Mode",
          .section = "General",
          .tooltip = "SDR: the game's native SDR tonemapper.\n"
                     "HDR (UE Extended): the game's Filmic tonemapper extended into HDR with UE Filmic Extended\n"
                     "from RenoDX's UE Extended addon (by marat569), at that addon's defaults (default).\n"
                     "HDR (PsychoV31): replaces ACES tonemapper with PsychoV31, keeping the artist's\n"
                     "grading (white balance, color grading, fades).\n"
                     "HDR (PsychoV35): the same with Musa's PsychoV35 and Hue Clip Emulation. Recommended.",
          .labels = {"SDR", "HDR (UE Extended)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (Deeper shadows)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV35). Deeper shadows.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings(
                {{"tonemapper", 3.f}, {"volumetric_fog", 0.5f}, {"sharpening", 0.0f}, {"shadows", 0.0f}, {"v35_shadows", 0.0f}});
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV35)\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings({{"tonemapper", 3.f}, {"volumetric_fog", 0.5f}, {"sharpening", 0.0f}});
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "Resets image controls to their defaults (HDR UE Extended) and sharpening to 0.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() { ApplyResetPreset(); },
      },
      // UE Extended ////////////////////////////////////////////////////////////////////////////////////
      slider("ext_exposure", &shader_injection.ext_exposure, "Exposure", "UE Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_highlights", &shader_injection.ext_highlights, "Highlights", "UE Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_shadows", &shader_injection.ext_shadows, "Shadows", "UE Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_contrast", &shader_injection.ext_contrast, "Contrast", "UE Extended",
             "1 = the game's own contrast.", IsExtendedMode, IsExtendedMode),
      slider("ext_saturation", &shader_injection.ext_saturation, "Saturation / Purity", "UE Extended",
             "1 = the game's own saturation.", IsExtendedMode, IsExtendedMode),
      slider("ext_highlight_saturation", &shader_injection.ext_highlight_saturation, "Highlight Saturation",
             "UE Extended", "How much brighter colors desaturate. 0 = more, 1 = default, 2 = less.", IsExtendedMode,
             IsExtendedMode),

      // PsychoV31 ///////////////////////////////////////////////////////////////////////////////////
      slider("exposure", &shader_injection.exposure, "Exposure", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("highlights", &shader_injection.highlights, "Highlights", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("shadows", &shader_injection.shadows, "Shadows", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("contrast", &shader_injection.contrast, "Contrast", "PsychoV31", "1 = PsychoV31's own.", IsPsychoMode,
             IsPsychoMode),
      slider("saturation", &shader_injection.saturation, "Saturation / Purity", "PsychoV31", "1 = PsychoV31's own.",
             IsPsychoMode, IsPsychoMode),
      slider("highlight_saturation", &shader_injection.highlight_saturation, "Highlight Saturation", "PsychoV31",
             "0 = less, 1 = PsychoV31's own, 2 = more.", IsPsychoMode, IsPsychoMode),
      slider("roll_off", &shader_injection.roll_off, "Highlight Roll-off", "PsychoV31",
             "How soon highlights start to compress toward Peak.\n0 = latest, 1 = default, 2 = soonest.",
             IsPsychoMode, IsPsychoMode),
      // Game Shadows (ATN, 2026-10-09: shadows look like SDR / Extended, colors desaturated). Off (0) without it.
      // new renodx::utils::settings::Setting{
      //     .key = "game_shadows",
      //     .binding = &shader_injection.game_shadows,
      //     .value_type = renodx::utils::settings::SettingValueType::INTEGER,
      //     .default_value = 0.f,
      //     .label = "Game Shadows (experimental)",
      //     .section = "PsychoV31",
      //     .tooltip = "Off: PsychoV31's own shadows.\n"
      //                "On: the game's darker shadows (Filmic toe of ACES) below mid gray, faded in over the stop\n"
      //                "below it. Not recommended",
      //     .labels = {"Off", "On"},
      //     .is_enabled = IsPsychoMode,
      //     .is_visible = IsPsychoMode,
      // },

      // Extra (HDR only) /////////////////////////////////////////////////////////////////////////
      slider("bloom", &shader_injection.bloom, "Bloom", "Extra", "Bloom multiplier (incl. lens dirt). Probably needs in-game Post Processing on.",
             IsHDRMode, IsAdvancedSettings),
      slider("vignette", &shader_injection.vignette, "Vignette", "Extra", "Vignette multiplier. Probably needs in-game Post Processing on.",
             IsHDRMode, IsAdvancedSettings),
      slider("godrays", &shader_injection.godrays, "Godrays", "Extra", "Light shaft multiplier. Probably needs in-game Post Processing on.",
             IsHDRMode, IsAdvancedSettings),
      slider("volumetric_fog", &shader_injection.volumetric_fog, "Volumetric Fog (experimental)", "Extra",
             "Volumetric fog multiplier, 0 = off. Fog on translucent objects (particles, glass) stays\n"
             "unchanged, and the fog's cost remains: Engine.ini r.VolumetricFog=0 is the cheaper off.\n"
             "Only shadows High/Ultra should have volumetric fog on.",
             IsHDRMode, IsAdvancedSettings),
      slider("sharpening", &shader_injection.sharpening, "Sharpening", "Extra",
             "Multiplier on the game's sharpening (CAS pass and tonemapper). Default is 1.0. Recommendation: 0",
             IsHDRMode, IsAdvancedSettings),

      // Extra (Probably not used): engine features in the shader dump that the game doesn't seem to use (ATN). ///
      // Ambient Occlusion: GT-VBAO in the game's last AO level (talesofarise/ue_bitmask_ao.hlsli). ////////////////
      new renodx::utils::settings::Setting{
          .key = "ssao_mode",
          .binding = &shader_injection.ssao_mode,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ambient Occlusion",
          .section = "Ambient Occlusion",
          .tooltip = "GT-VBAO: contact shading that lets light pass behind thin things (grass, poles,\n"
                     "characters) instead of dark halos. Needs in-game ambient occlusion on.",
          .labels = {"Game's", "GT-VBAO"},
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_radius",
          .binding = &shader_injection.ssao_radius,
          .default_value = 1.5f,
          .label = "Radius",
          .section = "Ambient Occlusion",
          .tooltip = "How far around a point other surfaces shade it (metres).",
          .min = 0.25f,
          .max = 5.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.ssao_mode != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_strength",
          .binding = &shader_injection.ssao_strength,
          .default_value = 1.f,
          .label = "Strength",
          .section = "Ambient Occlusion",
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = [] { return shader_injection.ssao_mode != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_thickness",
          .binding = &shader_injection.ssao_thickness,
          .default_value = 0.1f,
          .label = "Thickness",
          .section = "Ambient Occlusion",
          .tooltip = "How thick things are taken to be: thicker shades more behind them.",
          .min = 0.01f,
          .max = 1.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.ssao_mode != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_quality",
          .binding = &shader_injection.ssao_quality,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Quality",
          .section = "Ambient Occlusion",
          .tooltip = "Samples per side: more catch thin things. Heavier.",
          .labels = {"8", "16", "32", "64"},
          .is_enabled = [] { return shader_injection.ssao_mode != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_directions",
          .binding = &shader_injection.ssao_directions,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Directions",
          .section = "Ambient Occlusion",
          .tooltip = "Directions per pixel: less noise. Heavier.",
          .labels = {"1", "2", "4", "8"},
          .is_enabled = [] { return shader_injection.ssao_mode != 0.f; },
      },

      slider("film_grain", &shader_injection.film_grain, "Film Grain", "Extra (Probably not used)",
             "Multiplier on the game's film grain (where the game uses it).", IsHDRMode, IsAdvancedSettings),
      slider("chromatic_aberration", &shader_injection.chromatic_aberration, "Chromatic Aberration",
             "Extra (Probably not used)", "Multiplier on the game's chromatic aberration (where the game uses it).",
             IsHDRMode, IsAdvancedSettings),
  };
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));
}

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

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Expeditions: Rome by ATN";

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
  // UE4 caches D3D state: restore it after the proxy draw (else UI-only frames lose blending).
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

  // Color grading LUT: 32^3 R10G10B10A2 -> 16-bit float (holds HDR).
  for (auto format : {reshade::api::format::r10g10b10a2_unorm, reshade::api::format::r10g10b10a2_typeless}) {
    renodx::mods::swapchain::resource_upgrade_infos.push_back({
        .old_format = format,
        .new_format = reshade::api::format::r16g16b16a16_float,
        .dimensions = {.width = 32, .height = 32, .depth = 32},
    });
  }

  // Post-tonemap frame (UE4: B8G8R8A8 typeless, maybe R10G10B10A2 / R8G8B8A8). Clones are only
  // used once a shader above writes to them (hot swap), so the G-buffer and others stay stock.
  for (auto format : {
           reshade::api::format::b8g8r8a8_typeless,
           reshade::api::format::b8g8r8a8_unorm,
           reshade::api::format::r8g8b8a8_typeless,
           reshade::api::format::r8g8b8a8_unorm,
           reshade::api::format::r10g10b10a2_typeless,
           reshade::api::format::r10g10b10a2_unorm,
       }) {
    renodx::mods::swapchain::resource_upgrade_infos.push_back({
        .old_format = format,
        .new_format = reshade::api::format::r16g16b16a16_float,
        .use_resource_view_cloning = true,
        .use_resource_view_hot_swap = true,
        .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
        .usage_include = reshade::api::resource_usage::render_target,
    });
  }

  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "HDR10: 10-bit.\nscRGB: 16-bit.\nFrame generation always uses HDR10.",
        .labels = {"HDR10", "scRGB"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    // The DLSS addon's frame generation takes no scRGB: HDR10 with it.
    bool frame_generation = false;
    reshade::get_config_value(nullptr, "Rome AA", "FrameGeneration", frame_generation);
    const auto value = frame_generation ? 0.f : setting->GetValue();
    shader_injection.swap_chain_encoding = value;
    renodx::mods::swapchain::SetUseHDR10(value == 0);
    settings.push_back(setting);
  }

  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "RenoDX, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom PsychoV31 "
               "and PsychoV35. UE Extended by Marat.",
      .section = "About",
  });
  for (const char* line : {twattila::credits::ACES, twattila::credits::HELPER, twattila::credits::DLSS_ADDON}) {
    settings.push_back(new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = line,
        .section = "About",
    });
  }
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
      .section = "About",
      .is_visible = IsAdvancedSettings,
  });

  configured = true;
}

// GT-VBAO's noise moves every frame (the game's TAA / DLSS settle it).
void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
               uint32_t, const reshade::api::rect*) {
  shader_injection.ssao_frame = std::fmod(shader_injection.ssao_frame + 1.f, 64.f);
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::present>(OnPresent);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);

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
