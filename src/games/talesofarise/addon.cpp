/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <algorithm>
#include <atomic>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/settings.hpp"
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
      {"highlight_hue_shift", 0.24f},
      {"ext_curve", 0.f},
      {"sdr_curve", 0.f},
      {"ext_highlight_hue_shift", 0.17f},
      {"ext_filmic_highlight_hue_shift", 0.30f},
      // {"game_shadows", 0.f},
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"sharpening", 1.f},
      {"film_grain", 1.f},
      {"chromatic_aberration", 1.f},
      {"motion_blur", 1.f},
      {"sun_ray_smoothing", 1.f},
      {"video_highlights", 0.f},
      {"video_dithering", 1.f},
  });
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
      // Tonemapper permutations (features in each file; 10-19 add the animated noise overlay).
      UpgradeRTVReplaceShader(0xDCFDF14D),  // tonemap00
      UpgradeRTVReplaceShader(0x8D5C5C52),  // tonemap01
      UpgradeRTVReplaceShader(0xC89B89AF),  // tonemap02
      UpgradeRTVReplaceShader(0x7C1D6772),  // tonemap03
      UpgradeRTVReplaceShader(0x283D10A8),  // tonemap04
      UpgradeRTVReplaceShader(0x94099C09),  // tonemap05
      UpgradeRTVReplaceShader(0xADA0697A),  // tonemap06
      UpgradeRTVReplaceShader(0x222F0E81),  // tonemap07
      UpgradeRTVReplaceShader(0x5113B408),  // tonemap08
      UpgradeRTVReplaceShader(0x1CB60A72),  // tonemap09
      UpgradeRTVReplaceShader(0x82A4BA46),  // tonemap10
      UpgradeRTVReplaceShader(0xBC89DFD2),  // tonemap11
      UpgradeRTVReplaceShader(0xC0DF59F9),  // tonemap12
      UpgradeRTVReplaceShader(0x0DF83FCE),  // tonemap13
      UpgradeRTVReplaceShader(0x167CE09D),  // tonemap14
      UpgradeRTVReplaceShader(0x44994D1C),  // tonemap15
      UpgradeRTVReplaceShader(0x757DCFEF),  // tonemap16
      UpgradeRTVReplaceShader(0xD3DC1A67),  // tonemap17
      UpgradeRTVReplaceShader(0xD936FC2B),  // tonemap18
      UpgradeRTVReplaceShader(0x0867B746),  // tonemap19
      UpgradeRTVShader(0x8533AF0C),         // fxaa00 (engine; after the tonemapper in Expeditions: Rome)
      UpgradeRTVShader(0x75751BBD),         // copy01 (engine; viewport-clamped copy / upscale)
      UpgradeRTVShader(0xE6551133),         // copy00 (engine)
      UpgradeRTVShader(0xE462919A),         // smaa00 (SMAA neighborhood blending; after the tonemapper unverified)
      CustomShaderEntry(0x876F0F03),        // lut00 (CombineLUTs)
      CustomShaderEntry(0xFBB78F9F),        // lut01 (+ 1 SDR LUT)
      CustomShaderEntry(0x357C5DF0),        // lut02 (+ 2 SDR LUTs)
      CustomShaderEntry(0xD81AC5B1),        // lut03 (+ 3 SDR LUTs)
      CustomShaderEntry(0xCF1F1C65),        // lut04 (+ 4 SDR LUTs)
      // Opaque CriWare Sofdec videos (UMG materials). Alpha videos (UI effects) stay at UI brightness.
      CustomShaderEntry(0x295C50DE),        // video00
      CustomShaderEntry(0x3474E3BC),        // video01 (UV region)
      CustomShaderEntry(0x5E300F3E),        // video02 (UV region, faded look)
      CustomShaderEntry(0xCB3E2B7E),        // video03 (faded look)
      // UE 4.21 motion blur filters (pixel shader path; quality levels / permutations): Motion Blur slider.
      CustomShaderEntry(0xF2A6E883),        // lighting00 (deferred sun lighting: Smooth Volumetric Light)
      CustomShaderEntry(0x87FBD5AE),        // motionblur00
      CustomShaderEntry(0x0C04E57A),        // motionblur01
      CustomShaderEntry(0x14168103),        // motionblur02
      CustomShaderEntry(0x15EED702),        // motionblur03
      CustomShaderEntry(0x361FB737),        // motionblur04
      CustomShaderEntry(0x3A6B3A7B),        // motionblur05
      CustomShaderEntry(0x521A2C2A),        // motionblur06
      CustomShaderEntry(0x5622A37F),        // motionblur07
      CustomShaderEntry(0x579514E7),        // motionblur08
      CustomShaderEntry(0x5D770744),        // motionblur09
      CustomShaderEntry(0x68A6E6C5),        // motionblur10
      CustomShaderEntry(0x78A0D210),        // motionblur11
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
          .tooltip = "Brightness of the scene/game and videos.",
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
                     "HDR (UE Extended): UE's Filmic tonemapper (the game's film settings) extended into HDR with UE Filmic Extended\n"
                     "from RenoDX's UE Extended addon (by marat569), at that addon's defaults (default).\n"
                     "HDR (PsychoV31): replaces the game's tonemapper with PsychoV31, keeping the artist's\n"
                     "grading (white balance, color grading, fades). Recommended.",
          .labels = {"SDR", "HDR (UE Extended)", "HDR (PsychoV31)"},
      },
      new renodx::utils::settings::Setting{
          .key = "sdr_curve",
          .binding = &shader_injection.sdr_curve,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "SDR Tonemapper",
          .section = "General",
          .tooltip = "Game's (Legacy): this game's own SDR look, UE's legacy tonemapper (default).\n"
                     "UE Filmic: UE's Filmic tonemapper with its gamut expansion, as r.TonemapperFilm=1 in Engine.ini:\n"
                     "deeper shadows, more saturated colors. HDR has its own choice (UE Extended: Base Curve).",
          .labels = {"Game's (Legacy)", "UE Filmic"},
          .is_enabled = []() { return !IsHDRMode(); },
          .is_visible = []() { return !IsHDRMode(); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV31), Video AutoHDR 0.25, Motion Blur and Film Grain 0.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings(
                {{"tonemapper", 2.f}, {"video_highlights", 0.25f}, {"motion_blur", 0.f}, {"film_grain", 0.f}});
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Resets image controls to their defaults (HDR UE Extended).\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() { ApplyResetPreset(); },
      },
      // UE Extended ////////////////////////////////////////////////////////////////////////////////////
      new renodx::utils::settings::Setting{
          .key = "ext_curve",
          .binding = &shader_injection.ext_curve,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Base Curve",
          .section = "UE Extended",
          .tooltip = "Game's (Legacy): this game's own SDR curve (UE's legacy tonemapper), extended into HDR the\n"
                     "UE Extended way: identical up to its shoulder, then its midtone line continues instead of\n"
                     "flattening. Closest to SDR (default).\n"
                     "UE Filmic: UE's Filmic curve, which RenoDX's UE Extended addon (by marat569) always uses:\n"
                     "deeper shadows and different contrast than this game's SDR.",
          .labels = {"Game's (Legacy)", "UE Filmic"},
          .is_enabled = IsExtendedMode,
          .is_visible = IsExtendedMode,
      },
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
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink or too orange? Change Highlight Hue Shift below.",
          .section = "UE Extended",
          .is_visible = IsExtendedMode,
      },
      // One amount per base curve (ATN: the legacy curve 0.17, UE Filmic 0.30).
      new renodx::utils::settings::Setting{
          .key = "ext_highlight_hue_shift",
          .binding = &shader_injection.ext_highlight_hue_shift,
          .default_value = 0.17f,
          .label = "Highlight Hue Shift",
          .section = "UE Extended",
          .tooltip = "Hue of bright colors like the game's SDR: fire and explosions turn orange/yellow instead of\n"
                     "pink/red (brightness and saturation stay UE Extended's). For the game's (legacy) base curve.\n"
                     "0 = UE Extended's own hues, 1 = the game's SDR hues in highlights. Default 0.17.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsExtendedMode,
          .is_visible = []() { return IsExtendedMode() && shader_injection.ext_curve == 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ext_filmic_highlight_hue_shift",
          .binding = &shader_injection.ext_filmic_highlight_hue_shift,
          .default_value = 0.30f,
          .label = "Highlight Hue Shift",
          .section = "UE Extended",
          .tooltip = "Hue of bright colors like the game's SDR: fire and explosions turn orange/yellow instead of\n"
                     "pink/red (brightness and saturation stay UE Extended's). For the UE Filmic base curve.\n"
                     "0 = UE Extended's own hues, 1 = the game's SDR hues in highlights. Default 0.30.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsExtendedMode,
          .is_visible = []() { return IsExtendedMode() && shader_injection.ext_curve != 0.f; },
      },

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
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink or too orange? Change Highlight Hue Shift below.",
          .section = "PsychoV31",
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .key = "highlight_hue_shift",
          .binding = &shader_injection.highlight_hue_shift,
          .default_value = 0.24f,
          .label = "Highlight Hue Shift",
          .section = "PsychoV31",
          .tooltip = "Hue of bright colors like the game's SDR: fire and explosions turn orange/yellow instead of\n"
                     "pink/red (brightness and saturation stay PsychoV31's).\n"
                     "0 = PsychoV31's own hues, 1 = the game's SDR hues in highlights. Default 0.24.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      // Game Shadows (ATN, 2026-10-09, in Expeditions: Rome, same code: shadows look like SDR / Extended, colors
      // desaturated). Off (0) without it.
      // new renodx::utils::settings::Setting{
      //     .key = "game_shadows",
      //     .binding = &shader_injection.game_shadows,
      //     .value_type = renodx::utils::settings::SettingValueType::INTEGER,
      //     .default_value = 0.f,
      //     .label = "Game Shadows (experimental)",
      //     .section = "PsychoV31",
      //     .tooltip = "Off: PsychoV31's own shadows.\n"
      //                "On: the game's darker shadows (Filmic toe of ACES) below mid gray, faded in over the stop\n"
      //                "below it.",
      //     .labels = {"Off", "On"},
      //     .is_enabled = IsPsychoMode,
      //     .is_visible = IsPsychoMode,
      // },

      // Extra (HDR only) /////////////////////////////////////////////////////////////////////////
      slider("bloom", &shader_injection.bloom, "Bloom", "Extra", "Bloom multiplier (incl. lens dirt). Needs in-game Bloom on.",
             IsHDRMode, IsAdvancedSettings),
      slider("vignette", &shader_injection.vignette, "Vignette", "Extra", "Vignette multiplier.", IsHDRMode,
             IsAdvancedSettings),
      slider("sharpening", &shader_injection.sharpening, "Sharpening", "Extra",
             "Multiplier on the game's sharpening (only where the game uses it).", IsHDRMode, IsAdvancedSettings),
      slider("film_grain", &shader_injection.film_grain, "Film Grain", "Extra",
             "Multiplier on the game's film grain (cutscenes).", IsHDRMode, IsAdvancedSettings),
      slider("chromatic_aberration", &shader_injection.chromatic_aberration, "Chromatic Aberration", "Extra",
             "Multiplier on the game's chromatic aberration (cutscenes).", IsHDRMode, IsAdvancedSettings),
      slider("motion_blur", &shader_injection.motion_blur, "Motion Blur", "Extra",
             "Motion blur strength (blur length) multiplier, SDR and HDR. Needs in-game Motion Blur on; 0 turns it off.",
             [] { return true; }, IsAdvancedSettings),
      new renodx::utils::settings::Setting{
          // New key (v29): the v28 setting's "Along Sun Rays" was 2.
          .key = "sun_ray_smoothing",
          .binding = &shader_injection.volumetric_light_smoothing,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Smooth Volumetric Light",
          .section = "Extra",
          .tooltip = "The sunlight haze in the air (strongest around the sun) is computed in 8-pixel blocks: where\n"
                     "mountains or buildings shadow it, its edge looks like stairs.\n"
                     "Along Sun Rays: blends the blocks along the sun's rays, the direction the shafts run: smooth\n"
                     "edges that stay sharp (about 3-4 fps). SDR and HDR. Off: the game's.",
          .labels = {"Off", "Along Sun Rays"},
          .is_visible = IsAdvancedSettings,
      },
      new renodx::utils::settings::Setting{
          .key = "video_highlights",
          .binding = &shader_injection.video_highlights,
          .default_value = 0.f,
          .label = "Video AutoHDR",
          .section = "Extra",
          .tooltip = "Inverse tone mapping (ITM) for the game's videos (8-bit SDR):\n"
                     "makes their bright parts brighter.\n"
                     "0 = off (like SDR), 1 = strongest.\n"
                     "High values can show banding (see Video Dithering).",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsHDRMode,
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
          .is_enabled = [] { return IsHDRMode() && shader_injection.video_highlights > 0.f; },
          .is_visible = IsAdvancedSettings,
      },
  };
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
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Tales of Arise by ATN";

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

  // Scene buffers: Bandai keeps the scene (and its half-size copies) in R11G11B10 (r.SceneColorFormat 4 has no
  // effect): 6 / 6 / 5 mantissa bits, so smooth skies band (ATN: in SDR too). The pixel trace (DLAA addon) showed
  // the sky (Unreal's sky sphere, ps 0x11039AD5, a gradient from constants) written to it and then passed through
  // two more R11G11B10 passes before the tonemapper. Upgraded at creation, as RenoDX's UE Extended does.
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SceneBuffers16",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Scene buffers",
        .section = "Output (Restart Required)",
        .tooltip = "The game keeps its scene in 11/10-bit float buffers: smooth skies show banding.\n"
                   "16-bit (default): no banding, a little more video memory.",
        .labels = {"Game's (11/10-bit)", "16-bit"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    if (setting->GetValue() != 0.f) {
      renodx::mods::swapchain::resource_upgrade_infos.push_back({
          .old_format = reshade::api::format::r11g11b10_float,
          .new_format = reshade::api::format::r16g16b16a16_float,
          .use_resource_view_cloning = true,
          .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
          .usage_include = reshade::api::resource_usage::render_target | reshade::api::resource_usage::copy_dest,
      });
    }
    settings.push_back(setting);
  }

  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "HDR10: 10-bit.\nscRGB: 16-bit.",
        .labels = {"HDR10", "scRGB"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const auto value = setting->GetValue();
    shader_injection.swap_chain_encoding = value;
    renodx::mods::swapchain::SetUseHDR10(value == 0);
    settings.push_back(setting);
  }

  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
      .section = "About",
      .is_visible = IsAdvancedSettings,
  });

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

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
