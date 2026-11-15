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
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "../twattila/credits.hpp"
#include "../talesofarise/hdr_only.hpp"
#include "../talesofarise/test_hooks.hpp"
#include "../twattila/draw_dispatch.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/output_encoding.hpp"
#include "./dlss_graphics.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsExtendedMode() {
  return shader_injection.hdr == 1.f;
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f;
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 3.f;
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

// SDR: the game's own tonemapper and look; the game's and the UI's white are the monitor's own.
void ApplySdrPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemapper", 0.f}});
}

// The Recommended button; Output Auto takes it once when Windows HDR comes on after a first start without HDR.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemapper", 3.f}, {"volumetric_fog", 0.5f}, {"sharpening", 0.0f}});
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
                   const std::string& tooltip, bool (*is_enabled)(), bool (*is_visible)() = [] { return true; }) {
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
          .labels = {"HDR & Effects", "Graphics 1 of 2", "Graphics 2 of 2"},
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
          .on_change = []() { ApplyRecommendedPreset(); },
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
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "SDR (monitors without HDR)",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "For SDR monitors: Rendering Mode SDR (the game's own picture) and Output SDR (restart the game if it was\n"
                     "HDR). The game's and the UI's white are your monitor's own white.",
          .tint = 0xFFC107,
          .on_change = []() {
            ApplySdrPreset();
            renodx::utils::settings::UpdateSettings({{"SwapChainEncoding", 2.f}});
          },
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
             IsHDRMode),
      slider("vignette", &shader_injection.vignette, "Vignette", "Extra", "Vignette multiplier. Probably needs in-game Post Processing on.",
             IsHDRMode),
      slider("godrays", &shader_injection.godrays, "Godrays", "Extra", "Light shaft multiplier. Probably needs in-game Post Processing on.",
             IsHDRMode),
      slider("volumetric_fog", &shader_injection.volumetric_fog, "Volumetric Fog (experimental)", "Extra",
             "Volumetric fog multiplier, 0 = off. Fog on translucent objects (particles, glass) stays\n"
             "unchanged, and the fog's cost remains: Engine.ini r.VolumetricFog=0 is the cheaper off.\n"
             "Only shadows High/Ultra should have volumetric fog on.",
             IsHDRMode),
      slider("sharpening", &shader_injection.sharpening, "Sharpening", "Extra",
             "Multiplier on the game's sharpening (CAS pass and tonemapper). Default is 1.0. Recommendation: 0",
             IsHDRMode),

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
             "Multiplier on the game's film grain (where the game uses it).", IsHDRMode),
      slider("chromatic_aberration", &shader_injection.chromatic_aberration, "Chromatic Aberration",
             "Extra (Probably not used)", "Multiplier on the game's chromatic aberration (where the game uses it).",
             IsHDRMode),
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

  // The former DLSS add-on's [Rome AA] keys into the page's, once, before the settings load (settings::Use).
  expeditionsrome::dlss_graphics::Migrate();
  BuildRuntimeData();
  // Without the helper (the HDR-only snapshot), after Settings Mode.
  settings.insert(settings.begin() + 1, new renodx::utils::settings::Setting{
                                            .value_type = renodx::utils::settings::SettingValueType::TEXT,
                                            .label = twattila::credits::FULL_DOWNLOAD_NOTE,
                                            .section = "General",
                                            .tint = talesofarise::hdr_only::WARNING_TINT,
                                            .is_visible = [] { return !twattila::dlaa_bridge::HelperInstalled(); },
                                        });
  settings.insert(settings.begin() + 2, new renodx::utils::settings::Setting{
                                            .value_type = renodx::utils::settings::SettingValueType::TEXT,
                                            .label = "The game has no HDR option of its own: this add-on's Output makes it HDR.",
                                            .section = "General",
                                        });
  // Section titles: one colour, Graphics 2 of 2's pink, the page's its own; then the page (Graphics 1 and 2 of 2).
  const std::vector<std::string> engine_sections = expeditionsrome::dlss_graphics::EngineSections();
  twattila::feature_settings::SetTints({"General", "Presets", "UE Extended", "PsychoV35", "PsychoV31", "Extra", "Extra (Probably not used)",
                                        "Output (Restart Required)", "About"},
                                       engine_sections);
  expeditionsrome::dlss_graphics::AppendPage(&settings, &shader_injection.ssao_mode);

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
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "HDR10: 10-bit. scRGB: 16-bit. HDR frame generation always uses HDR10.\n"
                   "SDR: displays without HDR (all fixes and options stay). Auto (default): scRGB with Windows HDR on, else SDR.",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = static_cast<int>(setting->GetValue()),
        // The DLSS addon's frame generation takes no scRGB: HDR10 with it.
        .hdr_encoding = [](int output) {
          bool frame_generation = false;
          reshade::get_config_value(nullptr, "Rome AA", "FrameGeneration", frame_generation);
          if (frame_generation || output == twattila::output_encoding::HDR10) return twattila::output_encoding::HDR10;
          return twattila::output_encoding::SCRGB;
        },
        .first_start_without_hdr = &ApplySdrPreset,
        .recommended_preset = &ApplyRecommendedPreset,
        .log_name = "[Rome]",
    });
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
  });
  // No NexusMods page known for this game's mod yet.
  const auto buttons = talesofarise::hdr_only::AboutButtons("https://www.nexusmods.com/expeditionsrome/mods/2");
  settings.insert(settings.end(), buttons.begin(), buttons.end());

  // Settings Mode's pages: HDR & Effects, Graphics 1 of 2 (graphics_page.hpp and the upscaling tools), Graphics 2 of 2.
  std::vector<std::string> graphics_sections = twattila::graphics_page::Sections();
  graphics_sections.push_back(expeditionsrome::dlss_graphics::TOOLS_SECTION);
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, graphics_sections);

  configured = true;
}

// Output (twattila/output_encoding.hpp), decided for each new swap chain before the swapchain mod's create_swapchain
// (registered after this).
bool OnCreateSwapchain(reshade::api::device_api, reshade::api::swapchain_desc&, void* hwnd) {
  twattila::output_encoding::Decide(static_cast<HWND>(hwnd));
  return false;
}

// GT-VBAO's noise moves every frame (the game's TAA / DLSS settle it).
void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
               uint32_t, const reshade::api::rect*) {
  shader_injection.ssao_frame = std::fmod(shader_injection.ssao_frame + 1.f, 64.f);
}

bool AttachAddon(HMODULE addon_module) {
  if (attached) return true;

  ConfigureAddon();

  reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::present>(OnPresent);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  // Scripted runs (RENODX_TW_SET / SHOT / PROFILE, twattila/test_hooks.hpp): nothing without them.
  twattila::test_hooks::Install(&settings, true, "[Rome]");
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // The fast draw dispatch, with the gate of view cloning's command-wide callback (resource upgrades).
  twattila::draw_dispatch::Use(DLL_PROCESS_ATTACH);
  // DLSS, graphics and ray tracing (the former DLSS add-on): after the shader mod, so its callbacks run after this part's
  // on the same shaders, as when it loaded second.
  expeditionsrome::dlss_graphics::Install(addon_module);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  expeditionsrome::dlss_graphics::Uninstall();
  reshade::unregister_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);

  twattila::draw_dispatch::Use(DLL_PROCESS_DETACH);
  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);
  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);
  twattila::test_hooks::Uninstall();
  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);

  attached = false;
}

}  // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE) {
  if (!addon_registered) {
    if (!reshade::register_addon(addon_module)) return false;
    addon_registered = true;
  }

  return AttachAddon(addon_module);
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
