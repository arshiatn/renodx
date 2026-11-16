/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/resource.hpp"
#include "../../utils/settings.hpp"
#include "../../utils/swapchain.hpp"
#include "../talesofarise_dlss/post_filters.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/draw_dispatch.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/frame_generation.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/reflex.hpp"
#include "../twattila/output_encoding.hpp"
#include "./dlss_graphics.hpp"
#include "./hdr_only.hpp"
#include "./test_hooks.hpp"
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

bool IsHdrOutput() {
  return shader_injection.swap_chain_encoding != 2.f;
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
      {"ext_highlight_saturation", 0.92f},
      {"ext_filmic_highlight_saturation", 1.05f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"saturation", 1.f},
      {"highlight_saturation", 1.f},
      {"highlight_contrast", 1.f},
      {"shadow_contrast", 1.f},
      {"flare", 0.f},
      {"dechroma", 0.f},
      {"roll_off", 1.f},
      {"gamut_compression", 1.f},
      {"gamut_compression_mode", 1.f},
      {"highlight_hue_shift", 0.57f},
      {"highlight_saturation_shift", 0.3f},
      {"ext_curve", 0.f},
      {"sdr_curve", 0.f},
      {"ext_highlight_hue_shift", 0.2f},
      {"ext_filmic_highlight_hue_shift", 0.34f},
      // {"game_shadows", 0.f},
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"sharpening", 1.f},
      {"film_grain", 1.f},
      {"chromatic_aberration", 1.f},
      {"motion_blur", 1.f},
      {"volumetric_light_smoothing", 2.f},
      {"sun_shadow_smoothing", 1.f},
      {"shadow_edge_color", 5.f},
      {"sun_haze", 1.f},
      {"sun_shadow_edge", 0.22f},
      {"painterly", 1.f},
      {"outlines", 1.f},
      {"video_highlights", 0.f},
      {"video_dithering", 1.f},
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
  renodx::utils::settings::UpdateSettings({{"tonemapper", 3.f}, {"video_highlights", 0.25f}, {"motion_blur", 0.f}, {"film_grain", 0.f}});
}

// Set once the tone mapper (or a pass after it) has drawn this frame; the next present starts a new frame.
std::atomic<bool> frame_tonemapped = false;

// Moves the bound render targets to their 16-bit clones (hot swap), so the HDR frame
// written here (and read by the next passes) is not clipped to 8/10-bit UNORM.
bool ActivateRenderTargetClones(reshade::api::command_list* cmd_list) {
  frame_tonemapped.store(true, std::memory_order_relaxed);
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

// Bandai's passes before the TAA: the DLSS addon's render scale, game sharpening, Ambient occlusion choice and sun
// shadow distance (1 / 1 / reduced / 1 without it).
bool LoadPostFilters(reshade::api::command_list*) {
  const auto filters = talesofarise::post_filters::Load();
  shader_injection.filter_scale = filters.scale;
  shader_injection.game_sharpening = filters.sharpening;
  shader_injection.ao_halo_fix = filters.ambient_occlusion;
  shader_injection.sun_shadow_distance = filters.sun_shadow_distance;
  shader_injection.ssao_mode = filters.ao_method;
  shader_injection.ssao_radius = filters.ao_radius;
  shader_injection.ssao_strength = filters.ao_strength;
  shader_injection.ssao_thickness = filters.ao_thickness;
  shader_injection.ssao_directions = filters.ao_directions;
  shader_injection.ssao_quality = filters.ao_quality;
  shader_injection.ssao_frame = filters.ao_frame;
  shader_injection.ssao_auto_thickness = filters.ao_auto_thickness;
  shader_injection.screen_space_shadows = filters.screen_space_shadows;
  shader_injection.gtvbgi = filters.gtvbgi;
  return true;
}

#define PostFilterShader(value)          \
  {                                      \
      value,                             \
      {                                  \
          .crc32 = value,                \
          .code = __##value,             \
          .on_draw = &LoadPostFilters,   \
      },                                 \
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
      // Bandai's screen filters before the TAA, sized in render pixels: in output pixels while DLSS upscales.
      CustomShaderEntry(0x038D003A),        // locallight00 (deferred local lights: the helper's torch shadows)
      PostFilterShader(0xF2A6E883),         // lighting00 (deferred sun lighting: Smooth Volumetric Light, shadow edge tint)
      PostFilterShader(0x6513DC8E),         // lighting01 (lighting00, Bandai's t14.y test folded)
      PostFilterShader(0xC28D2F1E),         // lighting02
      PostFilterShader(0xB2726B44),         // lighting03 (no volumetric lighting)
      PostFilterShader(0x81E3CED2),         // lighting04
      PostFilterShader(0xF95FABAA),         // lighting05
      // Bandai's toon lighting (characters, foliage): our AO and GI in place of Bandai's (toon00-09).
      PostFilterShader(0x0A9D482A),         // toon00
      PostFilterShader(0xDF1B494D),         // toon01
      PostFilterShader(0xF6334BA2),         // toon02
      PostFilterShader(0x1C8D11B6),         // toon03
      PostFilterShader(0x36E08F9A),         // toon04
      PostFilterShader(0x1FE8B2B6),         // toon05
      PostFilterShader(0x4E695056),         // toon06
      PostFilterShader(0x4E8218FE),         // toon07
      PostFilterShader(0x8CCA5153),         // toon08
      PostFilterShader(0x90EF2450),         // toon09
      // Bandai's ambient occlusion (6 compute, 6 pixel shader permutations) + blurs: the DLSS addon's Ambient occlusion;
      // below 70% DLSS ao00 / 01 / 06 / 07 read the full-resolution depth.
      PostFilterShader(0xE243580F),         // ao00
      PostFilterShader(0x312321CE),         // ao01
      PostFilterShader(0x14A01750),         // ao02
      PostFilterShader(0x2F09AD40),         // ao03
      PostFilterShader(0x8AA7DE1A),         // ao04
      PostFilterShader(0x923152AB),         // ao05
      PostFilterShader(0x506D32BE),         // ao06
      PostFilterShader(0xB2792BB9),         // ao07
      PostFilterShader(0x3A849493),         // ao08
      PostFilterShader(0xA081DC06),         // ao09
      PostFilterShader(0xE01AB3CF),         // ao10
      PostFilterShader(0xF3B29224),         // ao11
      PostFilterShader(0x8D9AC9A4),         // aoblur00 (horizontal)
      PostFilterShader(0x8C5387E3),         // aoblur01 (vertical + temporal)
      PostFilterShader(0x808E30C0),         // aoblur02 (vertical + temporal)
      PostFilterShader(0xE3BCE975),         // aoblur03 (vertical)
      PostFilterShader(0x2156645D),         // aoblur04
      PostFilterShader(0x347FDE2A),         // aoblur05
      PostFilterShader(0xB36F4199),         // aoblur06
      PostFilterShader(0xFC1A93FE),         // aoblur07
      CustomShaderEntry(0xD09F3430),        // volumetric00 (sun visibility per cell: Smooth Volumetric Light weights its shadow lookups)
      PostFilterShader(0x2084C889),         // sunshadow00 (sun shadow mask: Smooth Sun Shadows above the game's sun shadow distance)
      PostFilterShader(0x95802ADA),         // contactshadow00 (Bandai's character contact shadows: Bend's in place of its march toward the sun)
      PostFilterShader(0x66DC643D),         // painterly00 (painterly filter + its sharpening)
      PostFilterShader(0x2483CE26),         // painterly01 (painterly blur, horizontal)
      PostFilterShader(0x773EABCB),         // painterly02 (painterly blur, vertical)
      PostFilterShader(0x77D287B7),         // outline00 (character outlines)
      PostFilterShader(0x1A51F09D),         // outline01
      PostFilterShader(0x381F5A30),         // grading00 (stage grading + sharpening)
      PostFilterShader(0x4105D9D8),         // grading01
      PostFilterShader(0x376E3F34),         // grading02
      PostFilterShader(0xA1B7FCDA),         // grading03
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

  // 0-2 slider (Painterly Filter 0-1), 1 = default.
  auto slider = [](const std::string& key, float* binding, const std::string& label, const std::string& section,
                   const std::string& tooltip, bool (*is_enabled)(), bool (*is_visible)(), float max = 2.f,
                   float default_value = 1.f) {
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
          .labels = {"HDR & Effects", "Graphics 1 of 2", "Graphics 2 of 2"},
          .is_global = true,
      },
      // The game's Brightness (0-100, its default 50) sets r.ScreenGammaScaleTO14, the tonemapper's gamma (cb0[50].w).
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Use this in-game setting: Brightness 50.",
          .section = "General",
          .tint = 0xFFD23C,
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
          .tooltip = "SDR: the game's own look.\n"
                     "HDR (UE Extended): the game's look extended into HDR (default).\n"
                     "HDR (PsychoV31): PsychoV31 with the game's color grading.\n"
                     "HDR (PsychoV35): Musa's PsychoV35 and Hue Clip Emulation with it (recommended).\n"
                     "SDR output: the HDR modes are fitted into SDR.",
          .labels = {"SDR", "HDR (UE Extended)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
      },
      new renodx::utils::settings::Setting{
          .key = "sdr_curve",
          .binding = &shader_injection.sdr_curve,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "SDR Tonemapper",
          .section = "General",
          .tooltip = "Game's (Legacy): the game's own SDR look (default).\n"
                     "UE Filmic: UE's Filmic tonemapper: deeper shadows, more saturated colors.",
          .labels = {"Game's (Legacy)", "UE Filmic"},
          .is_enabled = []() { return !IsHDRMode(); },
          .is_visible = []() { return !IsHDRMode(); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (Vanilla shadows)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look with similar vanilla shadows: HDR (PsychoV35), Video AutoHDR 0.25, Motion Blur and Film Grain 0.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings(
                {{"tonemapper", 3.f}, {"video_highlights", 0.25f}, {"motion_blur", 0.f}, {"film_grain", 0.f}, {"shadows", 0.5f},
                 {"flare", 0.39f}, {"v35_shadows", 0.5f}});
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV35), Video AutoHDR 0.25, Motion Blur and Film Grain 0.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() { ApplyRecommendedPreset(); },
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
      // UE Extended ////////////////////////////////////////////////////////////////////////////////////
      new renodx::utils::settings::Setting{
          .key = "ext_curve",
          .binding = &shader_injection.ext_curve,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Base Curve",
          .section = "UE Extended",
          .tooltip = "Game's (Legacy): the game's own SDR curve extended into HDR, closest to SDR (default).\n"
                     "UE Filmic: UE's Filmic curve (as RenoDX's UE Extended addon): deeper shadows, other contrast.",
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
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink, too orange or too pale? Change the two settings below.",
          .section = "UE Extended",
          .is_visible = IsExtendedMode,
      },
      // One Highlight Saturation and Hue Shift per base curve (ATN: the legacy curve 0.92 / 0.20, UE Filmic 1.05 / 0.34).
      new renodx::utils::settings::Setting{
          .key = "ext_highlight_saturation",
          .binding = &shader_injection.ext_highlight_saturation,
          .default_value = 0.92f,
          .label = "Highlight Saturation",
          .section = "UE Extended",
          .tooltip = "How much brighter colors desaturate. 0 = more, 1 = UE Extended's own, 2 = less. Default 0.92.",
          .max = 2.f,
          .format = "%.2f",
          .is_enabled = IsExtendedMode,
          .is_visible = []() { return IsExtendedMode() && shader_injection.ext_curve == 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ext_filmic_highlight_saturation",
          .binding = &shader_injection.ext_filmic_highlight_saturation,
          .default_value = 1.05f,
          .label = "Highlight Saturation",
          .section = "UE Extended",
          .tooltip = "How much brighter colors desaturate. 0 = more, 1 = UE Extended's own, 2 = less. Default 1.05.",
          .max = 2.f,
          .format = "%.2f",
          .is_enabled = IsExtendedMode,
          .is_visible = []() { return IsExtendedMode() && shader_injection.ext_curve != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ext_highlight_hue_shift",
          .binding = &shader_injection.ext_highlight_hue_shift,
          .default_value = 0.2f,
          .label = "Highlight Hue Shift",
          .section = "UE Extended",
          .tooltip = "Keeps fire and explosions orange/yellow like SDR instead of pink/red.\n"
                     "0 = UE Extended's own hues, 1 = the game's SDR hues. Default 0.20.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsExtendedMode,
          .is_visible = []() { return IsExtendedMode() && shader_injection.ext_curve == 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ext_filmic_highlight_hue_shift",
          .binding = &shader_injection.ext_filmic_highlight_hue_shift,
          .default_value = 0.34f,
          .label = "Highlight Hue Shift",
          .section = "UE Extended",
          .tooltip = "Keeps fire and explosions orange/yellow like SDR instead of pink/red.\n"
                     "0 = UE Extended's own hues, 1 = the game's SDR hues. Default 0.34.",
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
      slider("dechroma", &shader_injection.dechroma, "Dechroma", "PsychoV31",
             "Very bright colors fade toward white, as if overexposed. 0 = off.", IsPsychoMode, IsPsychoMode, 1.f, 0.f),
      slider("roll_off", &shader_injection.roll_off, "Highlight Roll-off", "PsychoV31",
             "How soon highlights start to compress toward Peak.\n0 = latest, 1 = default, 2 = soonest.",
             IsPsychoMode, IsPsychoMode),
      slider("gamut_compression", &shader_injection.gamut_compression, "Gamut Compression", "PsychoV31",
             "Pulls colors your display can't show back inside its color space.\n"
             "0 = off (the display clips them), 1 = full (default).",
             IsPsychoMode, IsPsychoMode, 1.f),
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
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink, too orange or too pale? Change the settings below.",
          .section = "PsychoV31",
          .is_visible = IsPsychoMode,
      },
      // ATN's defaults (Reset and Recommended too): Highlight Saturation 1.00, Highlight Hue Shift 0.57,
      // Highlight Saturation Shift 0.30.
      new renodx::utils::settings::Setting{
          .key = "highlight_saturation",
          .binding = &shader_injection.highlight_saturation,
          .default_value = 1.f,
          .label = "Highlight Saturation",
          .section = "PsychoV31",
          .tooltip = "0 = less, 1 = PsychoV31's own (default), 2 = more.",
          .max = 2.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .key = "highlight_hue_shift",
          .binding = &shader_injection.highlight_hue_shift,
          .default_value = 0.57f,
          .label = "Highlight Hue Shift",
          .section = "PsychoV31",
          .tooltip = "Keeps fire and explosions orange/yellow like SDR instead of pink/red.\n"
                     "0 = PsychoV31's own hues, 1 = the game's SDR hues. Default 0.57.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      slider("highlight_saturation_shift", &shader_injection.highlight_saturation_shift, "Highlight Saturation Shift",
             "PsychoV31",
             "Gives bright colors the saturation of the game's SDR look (fire cores less pale and pink).\n"
             "0 = off, 1 = SDR's. Default 0.30.",
             IsPsychoMode, IsPsychoMode, 1.f, 0.3f),
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

      // Post Processing ////////////////////////////////////////////////////////////////////////////
      slider("bloom", &shader_injection.bloom, "Bloom", "Post Processing", "Bloom multiplier (incl. lens dirt). Needs in-game Bloom on.",
             IsHDRMode, [] { return true; }),
      slider("motion_blur", &shader_injection.motion_blur, "Motion Blur", "Post Processing",
             "Motion blur strength (blur length) multiplier, SDR and HDR. Needs in-game Motion Blur on; 0 turns it off.",
             [] { return true; }, [] { return true; }),
      slider("chromatic_aberration", &shader_injection.chromatic_aberration, "Chromatic Aberration", "Post Processing",
             "Multiplier on the game's chromatic aberration (cutscenes).", IsHDRMode, [] { return true; }),
      slider("vignette", &shader_injection.vignette, "Vignette", "Post Processing", "Vignette multiplier.", IsHDRMode,
             [] { return true; }),
      slider("film_grain", &shader_injection.film_grain, "Film Grain", "Post Processing",
             "Multiplier on the game's film grain (cutscenes).", IsHDRMode, [] { return true; }),
      slider("sharpening", &shader_injection.sharpening, "Sharpening", "Post Processing",
             "Multiplier on the tonemapper's sharpening, which the game only turns on in some scenes (normal\n"
             "gameplay has none, with or without DLSS). The sharpening before DLAA / DLSS is Graphics 1 of 2's Game's Sharpening.",
             IsHDRMode, [] { return true; }),
      // Bandai's passes (ATN), SDR and HDR.
      slider("painterly", &shader_injection.painterly, "Painterly Filter", "Post Processing",
             "Strength of the game's painterly (brush-like) filter. 0 = off.", [] { return true; }, [] { return true; }, 1.f),
      slider("outlines", &shader_injection.outlines, "Outlines", "Post Processing",
             "Strength of the characters' outlines. 0 = off.", [] { return true; }, [] { return true; }),

      // Extra /////////////////////////////////////////////////////////////////////////////////////
      new renodx::utils::settings::Setting{
          // New key (v47: Soft before Along Sun Rays; v29-v46 "sun_ray_smoothing", v28 another).
          .key = "volumetric_light_smoothing",
          .binding = &shader_injection.volumetric_light_smoothing,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 2.f,
          .label = "Smooth Volumetric Light",
          .section = "Extra",
          .tooltip = "Smooths the blocky edges of the sun's haze.\n"
                     "Soft: mild, in every direction. Along Sun Rays: along the rays, sharper edges.",
          .labels = {"Off", "Soft", "Along Sun Rays"},
      },
      slider("sun_haze", &shader_injection.sun_haze, "Sun Haze", "Extra",
             "Strength of the sunlit haze and light shafts in the air.", [] { return true; }, [] { return true; }),
      new renodx::utils::settings::Setting{
          .key = "sun_shadow_smoothing",
          .binding = &shader_injection.sun_shadow_smoothing,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Smooth Sun Shadows",
          .section = "Extra",
          .tooltip = "Smooths blocky sun shadow edges when Graphics 2 of 2's Sun shadow distance is above the game's.\n"
                     "Sharp to Sharpest: harder edges. Off: the game's.",
          .labels = {"Off", "Smooth", "Sharp", "Sharper", "Sharpest"},
      },
      slider("sun_shadow_edge", &shader_injection.sun_shadow_edge, "Thin Sun Shadow Edges", "Extra",
             "When Graphics 2 of 2's Sun shadow distance is above the game's, keeps the soft edge of sun shadows\n"
             "thin (less halo) and still smooth. 0 = off, 1 = as thin as at the game's distance, 2 = thinner.\n"
             "Default 0.22.",
             [] { return true; }, [] { return true; }, 2.f, 0.22f),
      slider("shadow_edge_color", &shader_injection.shadow_edge_color, "Shadow Edge Color", "Extra",
             "How much of the game's edge color fills the soft edges of sun shadows.\n"
             "0 = none: the shadow tint shows there as a colored fringe. Higher: cleaner edges. Default 5.",
             [] { return true; }, [] { return true; }, 10.f, 5.f),
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
      },
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
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Tales of Arise by ATN";

namespace {

void ConfigureAddon() {
  if (configured) return;

  // The former DLSS add-on's [Tales AA] keys into the page's, once, before the settings load (settings::Use).
  talesofarise::dlss_graphics::Migrate();
  // GT-VBGI joins Global Illumination: presets that had it off keep it off (SettingsRevision 2).
  talesofarise::ue_light_bounce::KeepSavedGiOff(2);
  // Bend and GT-VBGI on by default (ATN 2026-11-16): their rows saved at the old default (off) cleared once (SettingsRevision 3).
  twattila::graphics_page::ClearSavedRows(3, {"ScreenSpaceShadows", "ScreenSpaceShadowsLength", "light_bounce_strength"});
  BuildRuntimeData();
  // Without the helper (the HDR-only snapshot), after Settings Mode.
  settings.insert(settings.begin() + 1, talesofarise::hdr_only::FullDownloadNote());
  settings.insert(settings.begin() + 2, new renodx::utils::settings::Setting{
                                            .value_type = renodx::utils::settings::SettingValueType::TEXT,
                                            .label = "The game has no HDR option of its own: this add-on's Output makes it HDR.",
                                            .section = "General",
                                        });
  // Section titles: one colour, Graphics 2 of 2's pink, the page's its own; then the page (Graphics 1 and 2 of 2).
  const std::vector<std::string> engine_sections = talesofarise::dlss_graphics::EngineSections();
  twattila::feature_settings::SetTints({"General", "Presets", "UE Extended", "PsychoV35", "PsychoV31", "Post Processing", "Extra",
                                        "Output (Restart Required)", "About"},
                                       engine_sections);
  talesofarise::dlss_graphics::AppendPage(&settings);
  shader_injection.filter_scale = 1.f;
  shader_injection.game_sharpening = 1.f;

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
  // two more R11G11B10 passes before the tonemapper. Upgraded at creation, as RenoDX's UE Extended does; always (ATN:
  // no reason to keep the 11/10-bit buffers, HDR10 or scRGB output alike).
  renodx::mods::swapchain::resource_upgrade_infos.push_back({
      .old_format = reshade::api::format::r11g11b10_float,
      .new_format = reshade::api::format::r16g16b16a16_float,
      .use_resource_view_cloning = true,
      .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
      .usage_include = reshade::api::resource_usage::render_target | reshade::api::resource_usage::copy_dest,
  });

  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR. Default.\n"
                   "HDR10: 10-bit. scRGB: 16-bit. SDR: displays without HDR (all fixes and options stay).",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = static_cast<int>(setting->GetValue()),
        .first_start_without_hdr = &ApplySdrPreset,
        .recommended_preset = &ApplyRecommendedPreset,
        .log_name = "[Tales]",
    });
    settings.push_back(setting);
  }

  // The ACES license asks binaries to carry its copyright notice (UE Extended uses RenoDX's ACES RRT); the full license
  // and disclaimer ship as ACES_LICENSE.txt (docs/licenses).
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::Methods(talesofarise::hdr_only::SHIPPED_METHODS) + "\nBlue correction by Pumbo.",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::ACES,
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
  const auto buttons = talesofarise::hdr_only::AboutButtons("https://nexusmods.com/talesofarise/mods/444");
  settings.insert(settings.end(), buttons.begin(), buttons.end());

  // Settings Mode's pages: HDR & Effects, Graphics 1 of 2 (graphics_page.hpp and the upscaling tools), Graphics 2 of 2.
  std::vector<std::string> graphics_sections = twattila::graphics_page::Sections();
  graphics_sections.push_back(talesofarise::dlss_graphics::TOOLS_SECTION);
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, graphics_sections);

  configured = true;
}

// Pictures of the finished frame (ATN: loading and battle ends showed it clamped): once the tone mapper has drawn this
// frame, a render target of the frame's kind bound after it (a capture or fade) switches to its 16-bit clone as well,
// and so does the destination of a copy from an active clone (before, the swap chain module skipped that copy as
// mismatched). Registered before the swap chain module's own handlers, which then bind / copy the clones.
void OnBindRenderTargetsAndDepthStencil(reshade::api::command_list* cmd_list, uint32_t count,
                                        const reshade::api::resource_view* rtvs, reshade::api::resource_view) {
  if (!frame_tonemapped.load(std::memory_order_relaxed)) return;
  for (uint32_t index = 0; index < count; ++index) {
    renodx::mods::swapchain::ActivateCloneHotSwap(cmd_list->get_device(), rtvs[index]);
  }
}

bool OnCopyResource(reshade::api::command_list*, reshade::api::resource source, reshade::api::resource dest) {
  if (!frame_tonemapped.load(std::memory_order_relaxed)) return false;
  bool source_cloned = false;
  renodx::utils::resource::GetResourceInfo(source, [&source_cloned](const renodx::utils::resource::ResourceInfo& info) {
    source_cloned = info.clone_enabled;
  });
  if (!source_cloned) return false;
  renodx::utils::resource::UpdateResourceInfo(dest, [](renodx::utils::resource::ResourceInfo* info) {
    if (info->clone_target != nullptr) {
      info->clone_enabled = true;
    }
  });
  return false;
}

// The helper's frame generation and Reflex (the page's rows), its frames SDR with the SDR output.
void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  frame_tonemapped.store(false, std::memory_order_relaxed);
  const auto& sliders = talesofarise::dlss_graphics::aa_sliders;
  const auto aa = twattila::feature_settings::AaSettings(sliders);
  twattila::frame_generation::Update({
      .multiplier = (twattila::feature_settings::Upscaling(sliders) && twattila::dlaa_bridge::HelperInstalled()) ? aa.frame_generation : 0u,
      .reflex_boost = aa.reflex == 2,
      .frame_limit = aa.frame_limit,
      .preset = aa.frame_preset,
      .hud_recomposition = aa.hud_recomposition,
      .debug_view = aa.frame_debug_view,
      .sdr_output = shader_injection.swap_chain_encoding == static_cast<float>(twattila::output_encoding::SDR),
  });
  twattila::reflex::Update(swapchain, aa.reflex, aa.frame_limit);
}

// The game's device is going away: frame generation's and Reflex's objects on it with it.
void OnDestroyHelperDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  twattila::frame_generation::OnDestroyDevice(native);
  twattila::reflex::OnDestroyDevice(native);
}

bool AttachAddon(HMODULE addon_module) {
  if (attached) return true;
  // Time Each Pass: before every other draw hook (a skipped draw still gets its stamp).
  twattila::pass_timing::Install("talesofarise");

  ConfigureAddon();

  // Before the swap chain mod: a removed device's chain let go, each new chain's output decided (Auto follows Windows
  // HDR for the game's display; a ResizeBuffers keeps the output decided at the chain's creation).
  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindRenderTargetsAndDepthStencil);
  reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
  reshade::register_event<reshade::addon_event::present>(OnPresent);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  // Scripted runs (RENODX_TW_SET / SHOT / PROFILE, twattila/test_hooks.hpp): nothing without them.
  twattila::test_hooks::Install(&settings, true, "[Tales]");
  talesofarise::post_filters::Use(DLL_PROCESS_ATTACH);
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // The fast draw dispatch, with the gate of view cloning's command-wide callback (resource upgrades).
  twattila::draw_dispatch::Use(DLL_PROCESS_ATTACH);
  // DLSS, ray tracing and the graphics settings (the former DLSS add-on): after the shader mod, so its callbacks run after
  // this part's on the same shaders, as when it loaded second.
  talesofarise::dlss_graphics::Install(addon_module);
  // Frame generation (the helper presents; HUD-less images through the swap chain shader, its constants at b13) and
  // Reflex for the game's own presents.
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  twattila::reflex::Install();
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyHelperDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyHelperDevice);
  twattila::reflex::Uninstall();
  twattila::frame_generation::Uninstall();
  // Last of the helper's users: the bridge ends with it.
  talesofarise::dlss_graphics::Uninstall();
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindRenderTargetsAndDepthStencil);
  reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);

  twattila::draw_dispatch::Use(DLL_PROCESS_DETACH);
  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);
  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);
  twattila::test_hooks::Uninstall();
  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);
  talesofarise::post_filters::Use(DLL_PROCESS_DETACH);
  twattila::pass_timing::Uninstall();

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
