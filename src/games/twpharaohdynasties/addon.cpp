/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <cmath>
#include <string>
#include <utility>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/ao_pass.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/rt_scene.hpp"
#include "../twattila/anti_aliasing.hpp"
#include "../twattila/ao_filter.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/feature_settings.hpp"
#include "../twattila/graphics_page.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/screen_space_shadows.hpp"
#include "../twattila/test_hooks.hpp"
#include "./more_tests.h"
#include "../twrome2/engine_tweaks.hpp"
#include "./dead_units.hpp"
#include "./shadow_map.hpp"
#include "../twwarhammer2/line_sweep_ao.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "./rt_outside_view_branch_gs.h"
#include "./rt_outside_view_card_gs.h"
#include "./rt_outside_view_leaf_gs.h"
#include "./rt_outside_view_unit_gs.h"
#include "./rt_outside_view_world_gs.h"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;


bool IsExtendedMode() {
  return shader_injection.hdr == 1.f;
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f;
}

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsPsychoV31Mode() {
  return shader_injection.hdr == 3.f;
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 4.f;
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, the ACES fit).
constexpr float HUE_CLIP_DEFAULT = 80.f;

bool IsModdedAutoExposureEnabled() {
  return shader_injection.auto_exposure_highlight_protection == 1.f && IsHDRMode();
}

void ApplyResetPreset() {
  // Reset only controls that are currently active in this addon.
  // Peak / Paper White / UI brightness, Settings Mode, and output settings
  // are intentionally preserved.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 1.f},
      {"gamma_correction", 1.f},
      {"video_autohdr", 0.f},

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 1.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      // HDR Extended / user grading
      {"exposure_tpm", 1.f},
      {"highlights_tpm", 1.f},
      {"shadows_tpm", 1.f},
      {"contrast_tpm", 1.f},
      {"saturation_tpm", 1.f},
      {"highlight_expansion", 1.f},

      // Active extra controls
      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"chromaticaberration", 1.f},
      {"sharpening", 1.f},
      {"some_indicators", 1.f},
      {"v31_exposure", 1.f},
      {"v31_highlights", 1.f},
      {"v31_shadows", 1.f},
      {"v31_contrast", 1.f},
      {"v31_highlight_contrast", 1.f},
      {"v31_shadow_contrast", 1.f},
      {"v31_flare", 0.f},
      {"v31_saturation", 1.f},
      {"v31_dechroma", 0.f},
      {"v31_highlight_saturation", 1.f},
      {"v31_roll_off", 1.f},
      {"v31_gamut_compression", 1.f},
      {"v31_gamut_compression_mode", 1.f},
      {"v31_highlight_hue_shift", 0.06f},
      {"v31_highlight_saturation_shift", 0.f},

      // Active experimental controls
      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 2.f},
      {"vfx_fire_brightness", 1.f},
      {"vfx_base_brightness", 1.f},
      {"sky_deband", 2.f},
      {"filmgrain", 0.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

void ApplyPsychoRecommendedNoVFXBoost() {
  // ATN 2026-11-12: every recommended preset in PsychoV35 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"chromaticaberration", 1.f},

      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 2.f},
      {"vfx_fire_brightness", 1.f},
      {"vfx_base_brightness", 1.f},
      {"sky_deband", 2.f},
      {"filmgrain", 0.f},
  });
}

void ApplyPsychoRecommendedVFXBoost2500() {
  // ATN 2026-11-12: every recommended preset in PsychoV35 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"chromaticaberration", 1.f},

      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 2.0f},
      {"vfx_fire_brightness", 6.f},
      {"vfx_base_brightness", 4.5f},
      {"sky_deband", 2.f},
      {"filmgrain", 0.f},
  });
}

void ApplyPsychoRecommendedVFXBoost1000() {
  // ATN 2026-11-12: every recommended preset in PsychoV35 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"distortion", 1.f},
      {"chromaticaberration", 1.f},

      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 2.0f},
      {"vfx_fire_brightness", 4.f},
      {"vfx_base_brightness", 2.4f},
      {"sky_deband", 2.f},
      {"filmgrain", 0.f},
  });
}


// Frame copy for sharpening / CA ///////////////////////////////////////////////////////
// The game copies the frame into an 8-bit texture (dropped in HDR -> black scene).
// Instead: copy the bound RT into our own texture, bind it at t0 for that draw only.
namespace frame_copy {

struct Data {
  reshade::api::device* device = nullptr;
  reshade::api::resource texture = {0u};
  reshade::api::resource_view srv = {0u};
  reshade::api::resource_desc desc = {};
};

Data data;
std::mutex mutex;
thread_local ID3D11ShaderResourceView* game_srv = nullptr;
thread_local bool swapped = false;

// Caller holds the mutex.
void Destroy() {
  if (data.device != nullptr) {
    if (data.srv.handle != 0u) data.device->destroy_resource_view(data.srv);
    if (data.texture.handle != 0u) data.device->destroy_resource(data.texture);
  }
  data = {};
}

// Copies the frame into our texture ((re)created to match it). Returns its SRV.
reshade::api::resource_view CopyFrame(reshade::api::command_list* cmd_list, reshade::api::resource frame) {
  auto* device = cmd_list->get_device();
  const auto frame_desc = device->get_resource_desc(frame);
  if (frame_desc.type != reshade::api::resource_type::texture_2d || frame_desc.texture.samples != 1) return {0u};

  const std::scoped_lock lock(mutex);
  if (data.device != device
      || data.desc.texture.width != frame_desc.texture.width
      || data.desc.texture.height != frame_desc.texture.height
      || data.desc.texture.depth_or_layers != frame_desc.texture.depth_or_layers
      || data.desc.texture.levels != frame_desc.texture.levels
      || data.desc.texture.format != frame_desc.texture.format) {
    Destroy();
    data.device = device;
    data.desc = reshade::api::resource_desc(
        frame_desc.texture.width,
        frame_desc.texture.height,
        frame_desc.texture.depth_or_layers,
        frame_desc.texture.levels,
        frame_desc.texture.format,
        1,
        reshade::api::memory_heap::gpu_only,
        reshade::api::resource_usage::shader_resource | reshade::api::resource_usage::copy_dest);
    if (!device->create_resource(data.desc, nullptr, reshade::api::resource_usage::shader_resource, &data.texture)) {
      data.texture = {0u};
      return {0u};
    }
    const reshade::api::resource_view_desc view_desc(reshade::api::format_to_default_typed(frame_desc.texture.format));
    if (!device->create_resource_view(data.texture, reshade::api::resource_usage::shader_resource, view_desc, &data.srv)) {
      data.srv = {0u};
      return {0u};
    }
  }
  if (data.srv.handle == 0u) return {0u};

  cmd_list->copy_resource(frame, data.texture);
  return data.srv;
}

bool OnDraw(reshade::api::command_list* cmd_list) {
  swapped = false;
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());

  // The frame as bound right now (back buffer, or RenoDX's 16-bit clone of it).
  ID3D11RenderTargetView* rtv = nullptr;
  context->OMGetRenderTargets(1, &rtv, nullptr);
  if (rtv == nullptr) return true;
  ID3D11Resource* frame = nullptr;
  rtv->GetResource(&frame);
  rtv->Release();
  if (frame == nullptr) return true;

  const auto srv = CopyFrame(cmd_list, {reinterpret_cast<uintptr_t>(frame)});
  frame->Release();
  if (srv.handle == 0u) return true;

  // Swap t0 to our copy for this draw.
  context->PSGetShaderResources(0, 1, &game_srv);
  auto* copy_srv = reinterpret_cast<ID3D11ShaderResourceView*>(srv.handle);
  context->PSSetShaderResources(0, 1, &copy_srv);
  swapped = true;
  return true;
}

// Puts the game's t0 back.
void OnDrawn(reshade::api::command_list* cmd_list) {
  if (!swapped) return;
  swapped = false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  context->PSSetShaderResources(0, 1, &game_srv);
  if (game_srv != nullptr) {
    game_srv->Release();
    game_srv = nullptr;
  }
}

void OnDestroyDevice(reshade::api::device* device) {
  const std::scoped_lock lock(mutex);
  if (data.device == device) Destroy();
}

}  // namespace frame_copy

#define FrameCopyShader(value)              \
  {                                         \
      value,                                \
      {                                     \
          .crc32 = value,                   \
          .code = __##value,                \
          .on_draw = &frame_copy::OnDraw,   \
          .on_drawn = &frame_copy::OnDrawn, \
      },                                    \
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
            twattila::anti_aliasing::OnTonemapDrawn(cmd_list); },                                     \
      },                                                                                              \
  }

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      FrameCopyShader(0x6F09FC39), //chromatic aberration (reads our 16-bit frame copy)
      FrameCopyShader(0x19045C91), //sharpening (reads our 16-bit frame copy)
      UpgradeRTVReplaceShader(0x976C1C74), //t00
      UpgradeRTVReplaceShader(0x769869A4), //t01
      UpgradeRTVReplaceShader(0xDC7D89CD), //t02
      UpgradeRTVReplaceShader(0x60683951), //t03
      UpgradeRTVReplaceShader(0x504E6093), //t04
      UpgradeRTVReplaceShader(0xBB00FA18), //t05-t08: depth of field on
      UpgradeRTVReplaceShader(0x436A0FBA),
      UpgradeRTVReplaceShader(0xDBB55D7B),
      UpgradeRTVReplaceShader(0xAD62FB1E),
      UpgradeRTVReplaceShader(0x7137A4E2), //photo01-04: photo mode
      UpgradeRTVReplaceShader(0x63E7B6D1),
      UpgradeRTVReplaceShader(0x9F49A50F),
      UpgradeRTVReplaceShader(0xBD868B0A),
      // UpgradeRTVReplaceShader(EMPTY),
      __ALL_CUSTOM_SHADERS,
  };
  // Ambient Occlusion: Visibility Bitmask in place of the AO's horizontal blur: the addon's own shader
  // (ao_bitmask_blur, twwarhammer2/line_sweep_ao.hpp) with the blur's bindings; the blur's replacement stays the game's
  // code (Game's costs what it did). The collect always runs: where no blur runs (Warhammer II at some settings), the
  // game's AO stays, never an unwritten one.
  custom_shaders[0x6CF96BAF].on_draw = [](auto* cmd_list) {
    if (shader_injection.ssao_mode < 1.f) return true;
    return !tw::line_sweep_ao::DispatchInstead(cmd_list, __ao_bitmask_blur, &shader_injection, sizeof(shader_injection),
                                               false, __ao_upsample_blur, shader_injection.ssao_resolution != 0.f);
  };

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
        .value_type = renodx::utils::settings::SettingValueType::TEXT,
        .label = "Use these in-game settings: Gamma 22 and Brightness 10.",
        .section = "General",
        .tint = 0xFFD23C,
    },
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
        // .is_enabled = IsHDRMode,
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
        .tooltip = "Brightness of the scene/game.",
        .min = 1.f,
        .max = 500.f,
        // .is_enabled = IsHDRMode,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
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
        // .is_enabled = IsHDRMode,
        // .format = "%.2f",
        // .parse = [](float value) { return value / 80.f; },
    },
    new renodx::utils::settings::Setting{
        .key = "tonemapper",
        .binding = &shader_injection.hdr,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Rendering Mode",
        .section = "General",
        .tooltip = "SDR uses original SDR tonemapper.\nExtended unclips and extends the native ACES tonemapper.\n"
                   "HDR (PsychoV30) enables the HDR pipeline and uses PsychoV30 as tonemapper.\n"
                   "HDR (PsychoV31): PsychoV31 as tonemapper, more grading sliders.\n"
                   "HDR (PsychoV35): Musa's PsychoV35 as tonemapper, with Hue Clip Emulation (the Recommended presets).",
        .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV30)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
    },
    new renodx::utils::settings::Setting{
        .key = "gamma_correction",
        .binding = &shader_injection.gamma_correction,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Gamma Correction",
        .section = "General",
        .tooltip = "Rendering Mode SDR: any in-game Gamma shown as gamma 2.2.\n"
                   "Off: the game's own picture, at the in-game Gamma.",
        .labels = {"Off (In-game Gamma value)", "On (Gamma 2.2)"},
        .is_visible = [] { return !IsHDRMode(); },
    },

    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::TEXT,
    //     .label =
    //         "IMPORTANT: Disable Chromatic Aberration in the game's graphics settings.\n"
    //         "Both effects can make the game scene render black.",
    //     .section = "Important",
    // },
    
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "Resets active image controls to their defaults.\n"
                   "Peak, Paper White, UI brightness, Settings Mode, and output settings are preserved.",
        .on_change = []() {
          ApplyResetPreset();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (no VFX boost)",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "HDR (PsychoV35) and Video AutoHDR 0.20, the rest at its default.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
          ApplyPsychoRecommendedNoVFXBoost();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (VFX boost for 1000nits)",
        .section = "Presets",
        .group = "preset-line-2",
        .tooltip = "As Recommended, with brighter fire and effects for ~1000 nit displays.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
          ApplyPsychoRecommendedVFXBoost1000();
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Recommended (VFX boost for 2500nits)",
        .section = "Presets",
        .group = "preset-line-2",
        .tooltip = "As Recommended, with brighter fire and effects for ~2500 nit displays.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
          ApplyPsychoRecommendedVFXBoost2500();
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
        .key = "exposure",
        .binding = &shader_injection.exposure,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Exposure",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "highlights",
        .binding = &shader_injection.highlights,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Highlights",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "shadows",
        .binding = &shader_injection.shadows,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Shadows",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "contrast",
        .binding = &shader_injection.contrast,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Contrast",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    // new renodx::utils::settings::Setting{
    //     .key = "flare",
    //     .binding = &shader_injection.flare,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.0f,
    //     .label = "Flare",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 2.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },
    new renodx::utils::settings::Setting{
        .key = "purity_scale",
        .binding = &shader_injection.saturation,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Saturation / Purity",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "cone_response_exponent",
        .binding = &shader_injection.cone_response_exponent,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Cone Response Exponent",
        .section = "Psycho V30",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    new renodx::utils::settings::Setting{
        .key = "hue_shift",
        .binding = &shader_injection.hue_shift,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Highlight Hue Shift",
        .section = "Psycho V30",
        .tooltip = "Hue shift of bright highlights, like SDR: fire and explosions turn orange/yellow instead of pink/red.\n0 = none, 1 = PsychoV30 default (like my old addons), 2 = full.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsPsychoMode,
        .is_visible = IsPsychoMode,
    },
    // ONLY FOR DEBUG. ADD AGAIN IF MID GREY IS WRONG
    // new renodx::utils::settings::Setting{
    //     .key = "current_adaptive_state_bt709",
    //     .binding = &shader_injection.current_adaptive_state_bt709,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.18f,
    //     .label = "current_adaptive_state_bt709",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 1.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },

    // new renodx::utils::settings::Setting{
    //     .key = "current_background_state_bt709",
    //     .binding = &shader_injection.current_background_state_bt709,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 0.18f,
    //     .label = "current_background_state_bt709",
    //     .section = "Psycho V30",
    //     .min = 0.00f,
    //     .max = 1.00f,
    //     .format = "%.2f",
    //     .is_enabled = IsPsychoMode,
    //     .is_visible = IsPsychoMode,
    // },


    //TPM:
    new renodx::utils::settings::Setting{
        .key = "highlight_expansion",
        .binding = &shader_injection.highlight_expansion,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Highlight Expansion",
        .section = "User Grading",
        .tooltip = "On: the highlights ACES squeezed into the top of its curve are\n"
           "stretched back out into HDR headroom - the bright end Troy\n"
           "compressed away comes back.\n\n"
           "Off: unclipped SDR. Only what the game actually clipped expands,\n"
           "which is very little.\n\n"
           "Below that point it is the SDR image either way.",
        .labels = {"Off (Unclipped SDR)", "On"},
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },

    new renodx::utils::settings::Setting{
        .key = "exposure_tpm",
        .binding = &shader_injection.exposure_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Exposure",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "highlights_tpm",
        .binding = &shader_injection.highlights_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.00f,
        .label = "Highlights",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "shadows_tpm",
        .binding = &shader_injection.shadows_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Shadows",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "contrast_tpm",
        .binding = &shader_injection.contrast_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Contrast",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },
    new renodx::utils::settings::Setting{
        .key = "saturation_tpm",
        .binding = &shader_injection.saturation_tpm,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Saturation / Purity",
        .section = "User Grading",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
        .is_enabled = IsExtendedMode,
        .is_visible = IsExtendedMode,
    },


    ///EXTRA-----////

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
        .is_enabled = IsPsychoV31Mode,
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
               "Saturation of bright colors. 0 = less, 1 = PsychoV31's own, 2 = more."),
    v31_slider("v31_highlight_hue_shift", &shader_injection.v31_highlight_hue_shift, "Highlight Hue Shift",
               "Bright colors take the SDR image's hues: fire more orange / yellow, less pink.\n"
               "0 = off, 1 = SDR hues. Default 0.06.",
               1.f, 0.06f),
    v31_slider("v31_highlight_saturation_shift", &shader_injection.v31_highlight_saturation_shift,
               "Highlight Saturation Shift",
               "Gives bright colors the saturation of the SDR image (fire cores less pale and pink).\n"
               "0 = off (default), 1 = SDR's.",
               1.f, 0.f),

    new renodx::utils::settings::Setting{
        .key = "bloom",
        .binding = &shader_injection.bloom,
        .default_value = 1.0f,
        .label = "Bloom",
        .section = "Extra",
        .tooltip = "Bloom multiplier.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
    },
      new renodx::utils::settings::Setting{
        .key = "lensflare",
        .binding = &shader_injection.lensflare,
        .default_value = 1.00f,
        .label = "Lensflare",
        .section = "Extra",
        .tooltip = "Lensflare multiplier.",
        .min = 0.00f,
        .max = 2.00f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "godrays",
        .binding = &shader_injection.godrays,
        .default_value = 1.f,
        .label = "Godrays",
        .section = "Extra",
        .tooltip = "Multiplier on godrays.  Needs in-game Godrays setting on.",
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
        .tooltip = "Multiplier on vignette mask. Needs in-game setting on.",
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
        .tooltip = "Multiplier on distortion. Needs haze to be on.",
        // .tooltip = "Multiplier on distortion. YOU NEED TO CHOOSE PRESET ULTRA\n"
        //            "THEN YOUR OWN CUSTOME SETTINGS for the hidden distortion\n"
        //            "to get turned on. \n"
        //            "It was probably hidden because it is not getting used anymore!",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "chromaticaberration",
        .binding = &shader_injection.chromaticaberration,
        .default_value = 1.f,
        .label = "Chromatic aberration",
        .section = "Extra",
        .tooltip = "Multiplier on Chromatic aberration. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
    },
    new renodx::utils::settings::Setting{
        .key = "sharpening",
        .binding = &shader_injection.sharpening,
        .default_value = 1.f,
        .label = "Sharpening",
        .section = "Extra",
        .tooltip = "RenoDX's own sharpening (the game has none). 0 = off, 1 = default.",
        .min = 0.f,
        .max = 4.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "some_indicators",
        .binding = &shader_injection.someindicators,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Brightness of some Unit/Building Indicator ",
        .section = "Extra",
        .tooltip = "Brightness of selected highlight indicators.",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
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
        .is_enabled = IsHDRMode,
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
        .is_enabled = [] { return IsHDRMode() && shader_injection.video_autohdr > 0.f; },
    },


    // EXTRA EXPERIMENTAL//////////
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
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "auto_exposure_highlight_headroom_stops",
        .binding = &shader_injection.auto_exposure_highlight_headroom_stops,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 2.0f,
        .label = "Modded Auto Exposure: Sensitivity",
        .section = "Extra (Experimental)",
        .tooltip = "Maximum metered highlight above the frame average, in stops. Lower values protect exposure more strongly. Higher values are closer to stock.",
        .min = 0.6f,
        .max = 4.0f,
        .format = "%.1f",
        .is_enabled = IsModdedAutoExposureEnabled,
    },
    new renodx::utils::settings::Setting{
        .key = "vfx_fire_brightness",
        .binding = &shader_injection.vfxfirebrightness,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "Brightness boost: torches/burning houses/etc.",
        .section = "Extra (Experimental)",
        .tooltip = "torches/burning houses/etc. Recommmendation is max 10\n"
                   "Experimental! Find your sweets spot! ",
        .min = 1.0f,
        .max = 30.0f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "vfx_base_brightness",
        .binding = &shader_injection.vfxbasebrightness,
        .value_type = renodx::utils::settings::SettingValueType::FLOAT,
        .default_value = 1.0f,
        .label = "VFX Brightness boost: All of them!",
        .section = "Extra (Experimental)",
        .tooltip = "Brightness of every single VFX effect. Recommendation: 2.5 for 1000nits, 6 for 2500nits.\n"
                   "Probably not usable in desert storms. Experimental! Find your sweets spot! ",
        .min = 1.0f,
        .max = 20.0f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "sky_deband",
        .binding = &shader_injection.sky_deband,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 2.f,
        .label = "Dithering for sky's 7-bit texture",
        .section = "Extra (Experimental)",
        .tooltip = "Dithering to reduce sky's bandings.\n"
                   "Higher levels destroy more sky details while fixing banding.",
        .labels = {"Off", "Low", "Medium", "High"},
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "filmgrain",
        .binding = &shader_injection.filmgrain,
        .default_value = 0.f,
        .label = "Filmgrain (probably isn't used)",
        .section = "Extra (Experimental)",
        .tooltip = "Multiplier on filmgrain. YOU NEED TO CHOOSE PRESET HIGH/ULTRA\n"
                   "THEN YOUR OWN CUSTOME SETTINGS for the hidden filmgrain\n"
                   "to get turned on. \n"
                   "It was probably hidden because it is not getting used anymore!",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
    },
    // VFX //////////////////////////////////////////////////////////////////////////////////////
   
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_blood_splash_brightness",
    //     .binding = &shader_injection.vfxbloodsplash,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Blood splash",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Bloodsplash brightness. Recommmendation is max 10.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.0f",
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fog_brightness",
    //     .binding = &shader_injection.vfxfogbrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Storms (Sand/Fog)",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of the fog color applied to particles. Recommmendation is max 1.",
    //     .min = 0.1f,
    //     .max = 4.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    //   new renodx::utils::settings::Setting{
    //     .key = "vfxsnow",
    //     .binding = &shader_injection.vfxsnow,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Snowflakes",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of snow. Recommmendation is max 5.",
    //     .min = 0.1f,
    //     .max = 20.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxrain",
    //     .binding = &shader_injection.vfxrain,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: rain",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Brightness of rain. Recommmendation is max 5.",
    //     .min = 0.1f,
    //     .max = 20.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
    /////////////////////////////////////////////////////////////////////////////////////////////
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
    //     new renodx::utils::settings::Setting{
    //     .key = "vfxweather",
    //     .binding = &shader_injection.vfxweather,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Blood Splash & desert storm & Fog",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Brightness of blood splashes and the camera-following fog used by snowstorms and sandstorms. Recommmendation is max 0.5 in storm/fog.",
    //     .min = 0.01f,
    //     .max = 20.0f,
    //     .format = "%.2f",
    //     .is_visible = IsAdvancedSettings,
    // },

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
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "HDR Defaults",
    //     .group = "preset-line-1",
    //     .tooltip = "Resets all image tuning to HDR defaults while preserving Peak, Paper White, UI brightness, Settings Mode, and output encoding.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.00f, 1.f, 0.f, 2.f, 1.f, 1.f, 1.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: All Conditions",
    //     .group = "preset-line-1",
    //     .tooltip = "Balanced recommendation intended to work across all weather and lighting conditions. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 4.f, 2.5f, 0.5f, 3.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: Day/Dry/Rain",
    //     .group = "preset-line-2",
    //     .tooltip = "Recommendation for daytime scenes and dry weather. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 4.f, 5.f, 2.f, 5.f);
    //     },
    // },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: Night/Snow/Fog",
    //     .group = "preset-line-2",
    //     .tooltip = "Recommendation for fog, snow, and nighttime scenes. Display calibration is preserved.",
    //     .on_change = []() {
    //       ApplyHDRPreset(1.f, 1.03f, 0.f, 1.f, 3.f, 2.f, 0.5f, 2.f);
    //     },
    // },
  };
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));
}

// Graphics (Experimental): engine values x the sliders (their tweaks are filled in ConfigureAddon).
tw::engine_tweaks::Option unit_detail[5];  // very small, small, medium, large, very large units
tw::engine_tweaks::Option dead_units;  // dead_units.hpp
tw::engine_tweaks::Option unit_animation;
tw::engine_tweaks::Option unit_variety;
tw::engine_tweaks::Option footstep_effects;
tw::engine_tweaks::Option model_detail;
tw::engine_tweaks::Option grass_distance;
tw::engine_tweaks::Option small_objects;
tw::engine_tweaks::Option terrain_detail;
tw::engine_tweaks::Option building_detail;
tw::engine_tweaks::Option campaign_detail;
tw::engine_tweaks::Option reflection_quality;
tw::engine_tweaks::Option contact_shadow_quality;
// Shadow Map Size (shadow_map.hpp): the highest Shadows settings' shadow map, an index into SHADOW_MAP_SIZES.
float shadow_map_size = 0.f;
constexpr std::array<uint32_t, 4> SHADOW_MAP_SIZES = {0, 8192, 12288, 16384};  // 0: the game's (6144)
// Every engine option above, then the tests' (Graphics 2 of 2: more_tests.h, twattila/engine_tests.hpp).
std::vector<tw::engine_tweaks::Option*> graphics_options;
std::vector<twattila::engine_tests::EngineTest> engine_tests;
std::atomic<bool> at_start_done = false;
std::atomic<bool> at_start_stop = false;
HMODULE addon_module_handle = nullptr;
twattila::feature_settings::SssSliders sss_sliders;
twattila::feature_settings::AaSliders aa_sliders;
// Ray tracing's (twattila/rt_scene.hpp).
twattila::rt_scene::Sliders rt_sliders;
// The shadow pass's casters in the rays' scene (SCENE_SHADERS' cascade_keep entries): Shadow Casters Out of View, and Hills Out of
// View for its terrain (Experimental, off: no game run has shown them yet).
float rays_units_out_of_view_row = 0.f;
float rays_hills_out_of_view_row = 0.f;

// Log Details' light count, once (a frame trace gives a light draw's indices times its instances, not its lights): one whole
// frame's draws, instances and indices (or vertices) of the point lights (E318CE77: a light an instance, POSITION1-4) and of
// the particle lights (5AD46115: their layout unknown). Logged the first frame either draws, again the first frame point
// lights draw; with Log Details off a draw reads one flag.
struct LightCount {
  std::shared_mutex mutex;
  std::array<uint32_t, 2> draws = {};  // point lights, particle lights
  std::array<uint64_t, 2> instances = {};
  std::array<uint64_t, 2> indices = {};
  bool logged = false;
  bool points_logged = false;
};
LightCount light_count;
std::atomic<bool> light_counting = false;  // from a present on: the whole frame to the next
constexpr auto OnLightCountDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!light_counting.load(std::memory_order_relaxed)) return {};
  if constexpr (requires { command.arguments.instance_count; }) {
    const size_t family = (command.matched_shader_hash == 0xE318CE77) ? 0 : 1;
    const std::unique_lock lock(light_count.mutex);
    ++light_count.draws[family];
    light_count.instances[family] += command.arguments.instance_count;
    if constexpr (requires { command.arguments.index_count; }) {
      light_count.indices[family] += command.arguments.index_count;
    } else {
      light_count.indices[family] += command.arguments.vertex_count;
    }
  }
  return {};
};

// One method for contact shadows: the sun PS's own march toward the sun (lighting_VS_PS's g_contact_shadows, an int at byte 4
// in all 25 sun draws; 16 steps at ATN's settings, its result min'ed into the shadow) goes in a sun draw that ours shadow:
// Bend or the rays, whose mask screen_space_shadows' OnSunDraw (registered before this) bound at t49 for that draw. The draw
// reads a copy of its lighting constants with the flag 0, the game's bound again after it (finalize: the Light Bounce draws
// the sun draw itself and bypasses). Without our mask the game's buffer, flag and bytes stay. WARP on the game's bytes with
// the traces' constants: tools\tw\pharaoh\sun_test.
constexpr UINT CONTACT_SHADOWS_OFFSET = 4;
struct ContactShadowsOff {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // the copy's (compared only)
  Microsoft::WRL::ComPtr<ID3D11Buffer> lighting;
  UINT lighting_bytes = 0;
  Microsoft::WRL::ComPtr<ID3D11Buffer> zeros;
  Microsoft::WRL::ComPtr<ID3D11Buffer> game_lighting;  // bound again after the draw
  UINT game_slot = 0;
  bool logged = false;
};
ContactShadowsOff contact_shadows_off;
constexpr auto OnSunContactShadows = []<typename T, typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> our_shadow;
  native->PSGetShaderResources(twattila::screen_space_shadows::internal::MASK_SLOT, 1, our_shadow.GetAddressOf());
  if (!our_shadow) return {};
  const auto& sun_draw = command.template GetCallbackData<T>();
  Microsoft::WRL::ComPtr<ID3D11Buffer> game_lighting;
  native->PSGetConstantBuffers(sun_draw.lighting_slot, 1, game_lighting.GetAddressOf());
  if (!game_lighting) return {};
  D3D11_BUFFER_DESC desc = {};
  game_lighting->GetDesc(&desc);
  if (desc.ByteWidth < CONTACT_SHADOWS_OFFSET + sizeof(uint32_t)) return {};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = contact_shadows_off;
  const std::unique_lock lock(state.mutex);
  if (state.device != device.Get() || state.lighting_bytes != desc.ByteWidth) {
    state.lighting.Reset();
    state.lighting_bytes = 0;
    state.device = nullptr;
    constexpr std::array<uint32_t, 4> ZEROS = {};
    const D3D11_BUFFER_DESC copy = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC zeros = {.ByteWidth = sizeof(ZEROS), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA zeros_data = {.pSysMem = ZEROS.data()};
    if (FAILED(device->CreateBuffer(&copy, nullptr, state.lighting.GetAddressOf()))
        || FAILED(device->CreateBuffer(&zeros, &zeros_data, state.zeros.ReleaseAndGetAddressOf()))) {
      state.lighting.Reset();
      return {};
    }
    state.lighting_bytes = desc.ByteWidth;
    state.device = device.Get();
  }
  // A constant buffer takes no update of a part: the game's whole, then the flag from the buffer of zeros.
  native->CopyResource(state.lighting.Get(), game_lighting.Get());
  const D3D11_BOX flag = {.left = 0, .top = 0, .front = 0, .right = sizeof(uint32_t), .bottom = 1, .back = 1};
  native->CopySubresourceRegion(state.lighting.Get(), 0, CONTACT_SHADOWS_OFFSET, 0, 0, state.zeros.Get(), 0, &flag);
  native->PSSetConstantBuffers(sun_draw.lighting_slot, 1, state.lighting.GetAddressOf());
  state.game_lighting = std::move(game_lighting);
  state.game_slot = sun_draw.lighting_slot;
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i(std::format("[Total War] First run: the game's contact shadows off in sun draw {:08X} (ours shadow it).", sun_draw.shader_hash));
  }
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* drawn_native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&drawn_native);
    auto& drawn_state = contact_shadows_off;
    const std::unique_lock drawn_lock(drawn_state.mutex);
    if (drawn_native != nullptr) {
      drawn_native->PSSetConstantBuffers(drawn_state.game_slot, 1, drawn_state.game_lighting.GetAddressOf());
    }
    drawn_state.game_lighting.Reset();
  }};
};

// Ray Reconstruction's guides again after the water (water_troy 833E7963 writes the G-buffer after the sun draws, and the
// sky dome 9CF980D0 draws after it, once a frame in every data-run trace): from the first sun draw's channels.
constexpr auto OnSkyDomeGuides = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  twattila::anti_aliasing::RewriteSunGuides(native);
  return {};
};

// Only the copy's device (nullptr: the addon's end, DetachAddon): another device's end between a sun draw and its finalize
// must leave the game's buffer to bind again.
void OnDestroyContactShadowsDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = contact_shadows_off;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.lighting.Reset();
  state.lighting_bytes = 0;
  state.zeros.Reset();
  state.device = nullptr;
}

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
bool graphics_found = false;
bool dead_units_installed = false;


bool configured = false;
bool attached = false;
bool addon_registered = false;

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

// Ambient Occlusion Visibility Bitmask (twattila/ao_pass.hpp): the addon's own shader in place of the game's AO
// pass (whose replacement is the game's code only).
constexpr std::array<twattila::ao_pass::AmbientOcclusionDraw, 1> AO_DRAWS = {{
    {.shader_hash = 0xD42C3523, .bitmask = __ao_bitmask, .upsample = __ao_upsample},  // ssao01
}};

// DLAA / DLSS object motion (twattila/anti_aliasing.hpp, as Attila's): the game's shaders whose things move on
// their own and their motion versions (tools\tw\attila_motion\port_motion.py, GPU-checked by test_motion.py).
using twattila::anti_aliasing::MotionKind;
constexpr std::array<twattila::anti_aliasing::MotionShader, 26> MOTION_SHADERS = {{
    {.hash = 0x07936F02, .motion_shader = __motion_unit_weighted_0793, .kind = MotionKind::UNIT},
    {.hash = 0x15389925, .motion_shader = __motion_unit_weighted_standard_4_feat_1538, .kind = MotionKind::UNIT},
    {.hash = 0x2A6B49A6, .motion_shader = __motion_grass_vfx_phar_grass_flame_ti_2a6b},
    {.hash = 0x2C5A6427, .motion_shader = __motion_unit_weighted_standard_2_feat_2c5a, .kind = MotionKind::UNIT},
    // Cloth (phar_rigid_battle_cloth_wind) and flags (rigid_default_flag): their rows, time and wind last frame.
    {.hash = 0x2F2A0E7E, .motion_shader = __motion_rigid_cloth_wind_2f2a, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_cloth_wind_2f2a},
    {.hash = 0x3EF23291, .motion_shader = __motion_tree_troy_prefab_sky_cylinder_3ef2},
    {.hash = 0x475A8CDF, .motion_shader = __motion_unit_weighted_475a, .kind = MotionKind::UNIT},
    {.hash = 0x4A716936, .motion_shader = __motion_unit_weighted_4a71, .kind = MotionKind::UNIT},
    {.hash = 0x5CE17813, .motion_shader = __motion_unit_weighted_standard_2_feat_5ce1, .kind = MotionKind::UNIT},
    {.hash = 0x6061911B, .motion_shader = __motion_rigid_rigidtextureblend_6061, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblend_6061},
    {.hash = 0x621E86DD, .motion_shader = __motion_unit_weighted4_fur_alpha_621e, .kind = MotionKind::UNIT},
    {.hash = 0x6A599241, .motion_shader = __motion_rigid_rigidambientmapground_6a59, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidambientmapground_6a59},
    {.hash = 0x72BD9CFD, .motion_shader = __motion_unit_weighted_standard_4_feat_72bd, .kind = MotionKind::UNIT},
    // Banners (rigidtextureblend 2_bones): FF969484's skinning to the byte, only their UVs differ.
    {.hash = 0x76225B91, .motion_shader = __motion_unit_weighted_ff96, .kind = MotionKind::UNIT},
    {.hash = 0x88A29130, .motion_shader = __motion_sail_sailsymbol_88a2, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sailsymbol_88a2},
    {.hash = 0x8DE94E2F, .motion_shader = __motion_rigid_default_flag_8de9, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_default_flag_8de9},
    {.hash = 0x9BD142BD, .motion_shader = __motion_grass_vfx_phar_grass_flame_ti_9bd1},
    {.hash = 0xCB04CFA7, .motion_shader = __motion_rigid_rigidtextureblendwind_cb04, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblendwind_cb04},
    {.hash = 0xDB1EC9BA, .motion_shader = __motion_unit_weighted_standard_2_feat_db1e, .kind = MotionKind::UNIT},
    {.hash = 0xDD5D5694, .motion_shader = __motion_rope_rope_dd5d, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope_rope_dd5d},
    {.hash = 0xE7792B60, .motion_shader = __motion_tree_campaignvegetation_e779},
    {.hash = 0xED0BC391, .motion_shader = __motion_sail_sail_ed0b, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sail_ed0b},
    {.hash = 0xF704C8EC, .motion_shader = __motion_leaf_leaf_v3_f704},
    {.hash = 0xF81881EB, .motion_shader = __motion_unit_weightedcommon_f818, .kind = MotionKind::UNIT},
    {.hash = 0xF8FFE639, .motion_shader = __motion_unit_weighted_standard_2_feat_f8ff, .kind = MotionKind::UNIT},
    {.hash = 0xFF969484, .motion_shader = __motion_unit_weighted_ff96, .kind = MotionKind::UNIT},
}};

// Ray tracing's scene: 1ACBD907 (shaders__alphablend.xml and 87 other materials) draws the buildings, walls, clutter and the
// props blended into the terrain, and also the depth prefill again (6FDC1F3D, which binds a colour target: the 5BB18374 meshes, 498
// draws and 15.9 M indices in the six traces as 5BB18374's), the sea (water_troy 833E7963) and effects (vfx_spline_border 0051B762,
// ash 1B1F50CA). Only its G-buffer pixel shaders below are in the scene: the 21 the battle and siege traces drew. Cut out:
// CUT_OUT_MASKS; the others solid (no discard; a dither, t_dither.x; or a blend mask's test that can't fire: the three walls and
// terrain blends discard where 1 - mask.w x (1 - fade) or fade x mask.y is below 0, never for a texture's 0-1 values).
constexpr std::array<twattila::rt_scene::PixelMask, 21> BUILDING_MASKS = {{
    {.pixel_shader = 0x0842DFD4},  // phar_rigid_battle_blendmask_3tex_walls_sett (t_mask_texture.w, can't fire)
    {.pixel_shader = 0x1ABDA979},  // phar_rigid_battle_blendmask_4tex_walls_uv2
    {.pixel_shader = 0x23D56657},  // clutter_default
    {.pixel_shader = 0x246D4945},  // rigid_default_alpha_colorise (t_diffuse.w)
    {.pixel_shader = 0x2B2C6C82},  // rigid_default_colorize (t0 dither)
    {.pixel_shader = 0x3907635C},  // phar_rigid_battle_blendmask_3tex_statue
    {.pixel_shader = 0x538067F6},  // phar_rigid_battle_blendmask_3tex_walls (t_mask_texture.w, can't fire)
    {.pixel_shader = 0x5BB18374},  // phar_rigid_battle_blendmask_2tex_terrain (t_mask_texture.y, can't fire)
    {.pixel_shader = 0x5F579E7F},  // rigid_default (t0 dither)
    {.pixel_shader = 0x7758E0D6},  // rigid_building_default
    {.pixel_shader = 0x81DC17E9},  // phar_rigid_battle_blendmask_3tex_floor_sett
    {.pixel_shader = 0x8EFA45A0},  // phar_rigid_battle_blendmask_3tex_uv2_noise_sand
    {.pixel_shader = 0x97572547},  // phar_rigid_battle_blendmask_3tex
    {.pixel_shader = 0xA5A850D3},  // phar_rigid_battle_blendmask_3tex_uv2
    {.pixel_shader = 0xAC279A1B},  // phar_rigid_default_alpha (t_diffuse.w)
    {.pixel_shader = 0xB50D4F41},  // phar_rigid_default_alpha_colorize (t_diffuse.w)
    {.pixel_shader = 0xB87E0393},  // rigid_default_alpha (t_diffuse.w)
    {.pixel_shader = 0xC4349867},  // phar_rigid_battle_blendmask_2tex_walls
    {.pixel_shader = 0xC835A077},  // phar_rigid_building_default
    {.pixel_shader = 0xE25FD05C},  // phar_rigid_battle_blendmask_3tex_sand
    {.pixel_shader = 0xEA4AF236},  // phar_rigid_battle_blendmask_4tex_walls
}};

// 1ACBD907's alpha-tested materials (sorted): each discards where t_diffuse (t0).w is below 0.5 at TEXCOORD1.xy (the vertex's
// TEXCOORD0.xy, the VS's o2) without the game's MSAA, the helper's cut-out (LEAF_ALPHA_CUTOFF 0.5); captured with that UV
// (rt_capture_rigid_default_cut_out). WARP on the game's bytes: tools\tw\warp_tests\pharaoh\cut_out_v52.
constexpr std::array<twattila::rt_scene::PixelMask, 4> CUT_OUT_MASKS = {{
    {.pixel_shader = 0x246D4945},  // rigid_default_alpha_colorise
    {.pixel_shader = 0xAC279A1B},  // phar_rigid_default_alpha
    {.pixel_shader = 0xB50D4F41},  // phar_rigid_default_alpha_colorize
    {.pixel_shader = 0xB87E0393},  // rigid_default_alpha
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl's sun light draws (standard and skin lighting
// models), with the camera at b1 and the G-buffer's channels 1-4 from their first slot.
// Ray tracing's scene (twattila/rt_scene.hpp; tools\tw\rt_port.py): the main view's terrain, rigid objects, tree
// branches and leaves, units, grass, far tree cards, cloth and flags, and their capture versions (world positions; leaves,
// grass and cards also UVs).
// The floating unit banners (rigidtextureblend, 2 bones) stay out: the game gives them no shadow. So do the far forest's
// blocks (rigidtreebillboard vs40_main_block FE92BA2D): cards facing the camera with no shadow-pass draw, the forest's
// shadow there the game's kept baked map.
constexpr std::array<twattila::rt_scene::SceneShader, 26> SCENE_SHADERS = {{
    {.hash = 0x377DE0D5, .capture_shader = __rt_capture_terrain_geom_new, .terrain = true},
    {.hash = 0xDCC840AF, .capture_shader = __rt_capture_terrain_custom_material, .terrain = true},
    {.hash = 0xFAB7BA95, .capture_shader = __rt_capture_terrain_custom, .terrain = true},
    {.hash = 0x1ACBD907, .capture_shader = __rt_capture_rigid_default, .cut_out_shader = __rt_capture_rigid_default_cut_out, .clip_cut_outs = true, .pixel_masks = BUILDING_MASKS},
    {.hash = 0x6A599241, .capture_shader = __rt_capture_rigid_standard, .rows_shader = __motion_record_rigid_rigidambientmapground_6a59},
    {.hash = 0x6061911B, .capture_shader = __rt_capture_rigid_blend, .rows_shader = __motion_record_rigid_rigidtextureblend_6061},
    {.hash = 0xCB04CFA7, .capture_shader = __rt_capture_rigid_blend_wind, .rows_shader = __motion_record_rigid_rigidtextureblendwind_cb04},
    {.hash = 0x513D52B1, .capture_shader = __rt_capture_rigid_vertexpush},
    // Branches blend two sets of instance rows (v8-v10, v11-v13 by v6.xy): every frame as drawn, never placed.
    {.hash = 0x4A82B8C2, .capture_shader = __rt_capture_branch, .dynamic = true},
    {.hash = 0xF704C8EC, .capture_shader = __rt_capture_leaf, .foliage = true},
    // Far trees' cards (rigidtreebillboard vs40_main): turned to the sun, their alpha at t3 (t0 dissolve, t1 dither).
    {.hash = 0x97C53219, .capture_shader = __rt_capture_far_tree, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 3},
    // Cloth (phar_rigid_battle_cloth_wind) and flags (rigid_default_flag): sheets moved by the time and the wind, every frame.
    {.hash = 0x2F2A0E7E, .capture_shader = __rt_capture_cloth_wind, .dynamic = true, .thin = true},
    {.hash = 0x8DE94E2F, .capture_shader = __rt_capture_flag, .dynamic = true, .thin = true},
    {.hash = 0xFF969484, .capture_shader = __rt_capture_unit, .dynamic = true},
    {.hash = 0x4A716936, .capture_shader = __rt_capture_unit_4_bones, .dynamic = true},
    {.hash = 0x475A8CDF, .capture_shader = __rt_capture_unit_dismember, .dynamic = true},
    {.hash = 0x07936F02, .capture_shader = __rt_capture_unit_4_bones_dismember, .dynamic = true},
    {.hash = 0xE710D4E7, .capture_shader = __rt_capture_grass_e710, .dynamic = true, .foliage = true, .grass = true},
    // The shadow pass's casters (pass 6 of tools\tw\pharaoh\phase2.md, into the 2 x 2 atlas): what is out of the main view still casts
    // there (rt_outside_view.gs_5_0.hlsl: the game's own draws, world positions from their vertex shaders' outputs). Every frame as drawn;
    // solid (buildings too: DC5FE525's alpha-tested shadow draws, PS 985F8494, aren't cut out), leaves and far cards cut out by their
    // shadow pixel shaders' masks (6CA31B12 t0, 31718466 t2). No sprite soldiers: none drawn in any trace.
    {.hash = 0x67E28AF9, .terrain = true, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0xDC5FE525, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0xBC8DA38B, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x731EF7DD, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x039A9620, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x5AFB107C, .dynamic = true, .cascade_keep = __rt_outside_view_branch},
    {.hash = 0xA709F459, .dynamic = true, .foliage = true, .cascade_keep = __rt_outside_view_leaf},
    {.hash = 0xEB2A56A1, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 2, .cascade_keep = __rt_outside_view_card},
}};

// The game's passes that read render-size textures at the display's size (texture slots), below 100%; and the AO blurs,
// which read their input at their uv (TEXCOORD0 of the full-screen vertex shader 31C49ED2: 0-1 over the viewport, the
// render scale's share of their half-size target).
constexpr std::array<twattila::view_share::RegionRead, 3> RENDER_SCALE_READS = {{
    {.shader_hash = 0x57E5140E, .slots = 0b110},  // glow bright pass (after DLSS)
    {.shader_hash = 0x7F637259, .slots = 0b1},    // ssao2 blur_x
    {.shader_hash = 0xBFD285BD, .slots = 0b1},    // ssao2 blur_y
}};
// The game's 3D sprites (sprite.hlsl vertex_3d_gamma: the unit markers) drawn at the display's size after the tone mapper
// against the scene's depth (battle f07e0, siege 3aa60) and, in the battle, its copy (ec5e0): both stretched below 100%.
constexpr std::array<uint32_t, 1> FULL_DEPTH_SHADERS = {0x45A3B2DF};

constexpr std::array<twattila::light_bounce::SunDraw, 25> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0xF883F5B2, .standard = true, .gbuffer_slot = 8, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},
    {.shader_hash = 0xFA856110, .standard = true, .gbuffer_slot = 8, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},
    {.shader_hash = 0x40BE26E9, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 9},
    {.shader_hash = 0x5FF8081D, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 11},
    {.shader_hash = 0xFFD99CCD, .standard = true, .gbuffer_slot = 5, .camera_slot = 1, .sun_direction = 4},
    {.shader_hash = 0x1549C88A, .gbuffer_slot = 8, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},
    {.shader_hash = 0xE238D4C7, .gbuffer_slot = 8, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},
    {.shader_hash = 0x46B411BB, .gbuffer_slot = 5, .camera_slot = 1, .sun_direction = 4, .ao_slot = 9},
    {.shader_hash = 0x6C317272, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 11},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x24FD1318, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},  // slm_interior
    {.shader_hash = 0x4CA2F7BA, .standard = true, .gbuffer_slot = 1, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 9},  // slm_interior
    {.shader_hash = 0x6AFC2ACA, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},  // slm_edges
    {.shader_hash = 0x8213B16B, .standard = true, .gbuffer_slot = 3, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 11},  // slm_interior
    {.shader_hash = 0xA3EA9117, .standard = true, .gbuffer_slot = 1, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 9},  // slm_edges
    {.shader_hash = 0xC69A7EEB, .standard = true, .gbuffer_slot = 3, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 11},  // slm_edges
    {.shader_hash = 0xC701CDD0, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},  // slm_interior
    {.shader_hash = 0xE5A6A379, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 4, .sun_direction = 4, .ao_slot = 12},  // slm_edges
    {.shader_hash = 0x0F955270, .gbuffer_slot = 3, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 11},  // skinlm_edges
    {.shader_hash = 0x3323BDCC, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},  // skinlm_edges
    {.shader_hash = 0x47DDD6EE, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},  // skinlm_edges
    {.shader_hash = 0x4F5B587C, .gbuffer_slot = 1, .camera_slot = 1, .sun_direction = 4, .ao_slot = 9},  // skinlm_edges
    {.shader_hash = 0x64FD79C8, .gbuffer_slot = 3, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 11},  // skinlm_interior
    {.shader_hash = 0x7B122E81, .gbuffer_slot = 1, .camera_slot = 1, .sun_direction = 4, .ao_slot = 9},  // skinlm_interior
    {.shader_hash = 0x955658C3, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},  // skinlm_interior
    {.shader_hash = 0xF4E19A48, .gbuffer_slot = 4, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 12},  // skinlm_interior
}};

// The shadow map size, before the game builds its shadows.
void OnInitDevice(reshade::api::device*) {
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
  twpharaohdynasties::shadow_map::Install(SHADOW_MAP_SIZES[static_cast<size_t>(shadow_map_size)]);
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // Before any setting is read this frame (as Attila's): DLSS never starts on another vendor's GPU, a saved DLSS or a
  // preset's is XeSS on Intel's, else FSR.
  twattila::dlaa_bridge::ReadGpuVendor(reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native()));
  if (aa_sliders.method == 5.f && !twattila::dlaa_bridge::OnNvidia()) {
    renodx::utils::settings::UpdateSettings({{"AntiAliasing", (twattila::dlaa_bridge::gpu_vendor == 0x8086 ? 7.f : 6.f)}});
  }
  // The HBAO setting's Visibility Bitmask (ssao01): its Depth Levels (twattila/ao_pass.hpp; no Temporal Smoothing).
  twattila::ao_pass::Update(shader_injection.ssao_mode >= 1.f, false, (shader_injection.ssao_resolution != 0.f) ? 2.f : 1.f,
                            false);
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
  // As Warhammer III's rows (here the game's shadow maps under the rays shadow from behind the camera already: the casters are for
  // the other rays).
  rt_settings.casters_out_of_view = rt_settings.sun_shadows && rays_units_out_of_view_row != 0.f;
  rt_settings.terrain_outside_view = rays_hills_out_of_view_row != 0.f;
  // Ray-traced shadows replace the screen-space ones (the Shadows method, as Attila's).
  auto sss_settings = twattila::feature_settings::SssSettings(sss_sliders);
  sss_settings.enabled = sss_settings.enabled && !rt_settings.sun_shadows;
  twattila::screen_space_shadows::Update(swapchain, sss_settings);
  // GT-VBAO's Temporal Smoothing (its noise moving each frame) and Denoiser (twattila/ao_filter.hpp).
  const bool bitmask_ao = shader_injection.ssao_mode >= 1.f;
  static uint32_t ao_frame = 0;
  shader_injection.ssao_frame =
      (bitmask_ao && shader_injection.ssao_temporal != 0.f) ? static_cast<float>(++ao_frame % 64) : 0.f;
  twattila::ao_filter::Update(swapchain, {.temporal = bitmask_ao && shader_injection.ssao_temporal != 0.f,
                                          .edge_fix = shader_injection.ssao_edge_fix != 0.f,
                                          .denoise = bitmask_ao && shader_injection.ssao_denoise != 0.f});
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  twpharaohdynasties::shadow_map::Set(SHADOW_MAP_SIZES[static_cast<size_t>(shadow_map_size)]);
  {
    const std::unique_lock lock(light_count.mutex);
    const bool points = light_count.draws[0] != 0 && !light_count.points_logged;
    if (light_counting.load() && (points || (light_count.draws[1] != 0 && !light_count.logged))) {
      renodx::utils::log::i(std::format("[Total War] Light draws in a frame: point lights (E318CE77) {} draws, {} instances, {} indices; "
                                        "particle lights (5AD46115) {} draws, {} instances, {} indices.",
                                        light_count.draws[0], light_count.instances[0], light_count.indices[0], light_count.draws[1],
                                        light_count.instances[1], light_count.indices[1]));
      light_count.logged = true;
      light_count.points_logged = light_count.points_logged || points;
    }
    light_count.draws = {};
    light_count.instances = {};
    light_count.indices = {};
    light_counting.store(rt_sliders.debug_log != 0.f && !light_count.points_logged);
  }
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
  twpharaohdynasties::dead_units::Update(dead_units.scale, &dead_units_installed, graphics_status);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: PHARAOH DYNASTIES by ATN";  // TODO: change me!

namespace {

// Graphics 1 of 2's (twattila/graphics_page.hpp): what is true here. The rays' sun shadows multiply the game's shadow maps
// (the sun shaders' patched comparisons), ray-traced AO the game's SSAO; point lights hooked, no smoke, fog or reflections.
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = twattila::graphics_page::ATTILA_TEXTS.aa_labels,
    .aa_tooltip = twattila::graphics_page::ATTILA_TEXTS.aa_tooltip,
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its ambient occlusion\n"
                     "and shadows. The rest at its default.",
    .no_rays_tooltip = "Bend Screen-Space Shadows (Length 480), GT-VBAO and GT-VBGI at their Recommended settings.\n"
                       "The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows, path-traced ambient occlusion and Global Illumination at my settings. The rest of\n"
                    "this page at its default. Needs an upscaler (Anti-Aliasing above).\n"
                    "On NVIDIA also turns DLSS Ray Reconstruction on. Elsewhere Experimental, slower: its own denoisers.",
    .shadow_tooltip = "Game's Shadow Maps: the original. Bend Screen-Space Shadows: those plus fine contact shadows from\n"
                      "what is on screen (Bend Studio's; soldiers, weapons, grass; needs in-game shadows on).\n"
                      "DXR: DirectX Raytracing, the sun's shadow from rays too, on top of the game's, in place of Bend's.\n"
                      "Needs an upscaler.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep in-game SSAO on for all of these.\n"
                  "Game's SSAO: everything counts as solid, so soldiers and trees get dark halos.\n"
                  "GT-VBAO (Ground Truth AO with a visibility bitmask): objects have a thickness, so light gets behind\n"
                  "thin ones.\n"
                  "RTAO: from rays through the scene, on top of the game's SSAO. Needs an upscaler.\n"
                  "ReSTIR GI: Global Illumination's paths bring the sky's light in place of the game's ambient;\n"
                  "turns it on below. Needs an upscaler.",
    .ao_paths_label = "Path-Traced (ReSTIR GI, Experimental)",
    .effects_at_output_tooltip = "While the upscaler renders below 100%, ambient occlusion, Light Bounce and shadow softness keep\n"
                                 "their 100% detail. Looks better, can cost some performance.",
};

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

          // Resource Upgrades
    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .shader_hash = 0x6F09FC39,
    //       // .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::copy_source
    //   });


      

    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::copy_dest,
    //   });

    // #if RENODX_MODS_SWAPCHAIN_VERSION == 2
    //   renodx::mods::swapchain::resource_upgrade_infos.push_back({
    // #else
    //   renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
    // #endif
    //       .old_format = reshade::api::format::r8g8b8a8_unorm,
    //       .new_format = reshade::api::format::r16g16b16a16_float,
    //       .ignore_size = false,
    //       .use_resource_view_cloning = true,  // TODO: needed?
    //       .use_resource_view_hot_swap = true,
    //       .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
    //       .usage_include = reshade::api::resource_usage::shader_resource,
    //   });
  // Appended Settings (Graphics (Experimental))
  unit_detail[0].tweaks = {{.name = "VERY_SMALL_IMPOSTER_DISTANCE"}};
  unit_detail[1].tweaks = {{.name = "SMALL_IMPOSTER_DISTANCE"}};
  unit_detail[2].tweaks = {{.name = "MEDIUM_IMPOSTER_DISTANCE"}};
  unit_detail[3].tweaks = {{.name = "LARGE_IMPOSTER_DISTANCE"}};
  unit_detail[4].tweaks = {{.name = "VERY_LARGE_IMPOSTER_DISTANCE"}};
  unit_animation.tweaks = {
      {.name = "VERY_SMALL_SHARED_POSE_DISTANCE"},
      {.name = "SMALL_SHARED_POSE_DISTANCE"},
      {.name = "MEDIUM_SHARED_POSE_DISTANCE"},
      {.name = "LARGE_SHARED_POSE_DISTANCE"},
      {.name = "VERY_LARGE_SHARED_POSE_DISTANCE"},
      {.name = "VERY_SMALL_FACIAL_ANIMATION_DISTANCE"},
      {.name = "SMALL_FACIAL_ANIMATION_DISTANCE"},
      {.name = "MEDIUM_FACIAL_ANIMATION_DISTANCE"},
      {.name = "LARGE_FACIAL_ANIMATION_DISTANCE"},
      {.name = "VERY_LARGE_FACIAL_ANIMATION_DISTANCE"},
      {.name = "VERY_SMALL_HANDS_ANIMATION_DISTANCE"},
      {.name = "SMALL_HANDS_ANIMATION_DISTANCE"},
      {.name = "MEDIUM_HANDS_ANIMATION_DISTANCE"},
      {.name = "LARGE_HANDS_ANIMATION_DISTANCE"},
      {.name = "VERY_LARGE_HANDS_ANIMATION_DISTANCE"},
  };
  unit_variety.tweaks = {
      {.name = "VERY_SMALL_NO_VARIANT_DISTANCE"},
      {.name = "SMALL_NO_VARIANT_DISTANCE"},
      {.name = "MEDIUM_NO_VARIANT_DISTANCE"},
      {.name = "LARGE_NO_VARIANT_DISTANCE"},
      {.name = "VERY_LARGE_NO_VARIANT_DISTANCE"},
  };
  footstep_effects.tweaks = {
      {.name = "VERY_SMALL_FOOTSTEP_VFX_DISTANCE"},
      {.name = "SMALL_FOOTSTEP_VFX_DISTANCE"},
      {.name = "MEDIUM_FOOTSTEP_VFX_DISTANCE"},
      {.name = "LARGE_FOOTSTEP_VFX_DISTANCE"},
      {.name = "VERY_LARGE_FOOTSTEP_VFX_DISTANCE"},
  };
  model_detail.tweaks = {
      {.name = "distance_lod_scaler", .inverse = true},
  };
  grass_distance.tweaks = {
      {.name = "grass_visible_range"},
      {.name = "grass_fade_start"},
  };
  small_objects.tweaks = {
      {.name = "cull_small_screenspace_objects_default_factor"},
      {.name = "cull_small_screenspace_vegetation_factor"},
      {.name = "terrain_blended_objects_cull_distance_nomask"},
      {.name = "terrain_blended_objects_cull_distance_mask_low"},
  };
  building_detail.tweaks = {
      // Battle buildings' LOD distances (100, 200, 300, 600 m), copied into a building model's LODs when it loads.
      {.name = "battle_building_lod0_distance"},
      {.name = "battle_building_lod1_distance"},
      {.name = "battle_building_lod2_distance"},
      {.name = "battle_building_lod3_distance"},
  };
  campaign_detail.tweaks = {
      // The campaign map's trees (30-120 m), plants (60 m, when their models load) and rigid trees (100 m).
      {.name = "campaign_tree_lod0_distance"},
      {.name = "campaign_tree_lod1_distance"},
      {.name = "campaign_tree_lod2_distance"},
      {.name = "campaign_tree_lod3_distance"},
      {.name = "campaign_grass_lod0_distance"},
      {.name = "campaign_grass_lod1_distance"},
      {.name = "campaign_grass_lod2_distance"},
      {.name = "campaign_grass_lod3_distance"},
      {.name = "campaign_grass_lod4_distance"},
      {.name = "rigid_campaign_tree_lod"},
  };
  terrain_detail.tweaks = {
      {.name = "terrain_distance_scale_low"},
      {.name = "terrain_distance_scale_medium"},
      {.name = "terrain_distance_scale_high"},
      {.name = "terrain_distance_scale_ultra"},
      {.name = "terrain_far_shader_distance"},
  };
  reflection_quality.tweaks = {
      {.name = "screen_space_environment_occlusion_step_count", .integer = true},
  };
  contact_shadow_quality.tweaks = {
      {.name = "screen_space_shadows_step_count", .integer = true},
  };
  // Graphics 2 of 2's tests: Attila's that this game has, as it has them (without the values it lacks, its own game values
  // in the tooltips), then More Tests (more_tests.h).
  engine_tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), more_tests::ATTILA_TESTS);
  for (auto& test : engine_tests) {
    std::erase_if(test.option.tweaks, [](const tw::engine_tweaks::Tweak& tweak) {
      return std::ranges::find(more_tests::MISSING_VALUES, tweak.name) != more_tests::MISSING_VALUES.end();
    });
    if (const auto tooltip = std::ranges::find(more_tests::TOOLTIPS, test.key, &std::pair<std::string, std::string>::first); tooltip != more_tests::TOOLTIPS.end()) {
      test.tooltip = tooltip->second;
    }
  }
  {
    auto more = more_tests::MoreTests();
    engine_tests.insert(engine_tests.end(), more.begin(), more.end());
  }
  graphics_options = {&unit_detail[0], &unit_detail[1], &unit_detail[2], &unit_detail[3], &unit_detail[4], &unit_animation, &unit_variety, &footstep_effects, &model_detail, &grass_distance, &small_objects, &terrain_detail, &building_detail, &campaign_detail, &reflection_quality, &contact_shadow_quality};
  for (auto& test : engine_tests) graphics_options.push_back(&test.option);
  graphics_status = new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Engine values: not searched yet",
      .section = "Graphics (Experimental)",
  };
  // Graphics presets (ATN): MAX (every graphics setting at its maximum) and Reset (all graphics settings and tests at their
  // defaults, as Attila's; not the ambient occlusion or the Light Bounce).
  std::vector<std::pair<std::string, float>> graphics_defaults = {
      {"GraphicsShadowMapSize", 0.f},
      {"GraphicsUnitDetailVerySmall", 1.f},
      {"GraphicsUnitDetailSmall", 1.f},
      {"GraphicsUnitDetailMedium", 1.f},
      {"GraphicsUnitDetailLarge", 1.f},
      {"GraphicsUnitDetailVeryLarge", 1.f},
      {"GraphicsDeadUnits", 1.f},
      {"GraphicsUnitAnimation", 1.f},
      {"GraphicsUnitVariety", 1.f},
      {"GraphicsFootstepEffects", 1.f},
      {"GraphicsModelDetail", 1.f},
      {"GraphicsGrassDistance", 1.f},
      {"GraphicsSmallObjects", 1.f},
      {"GraphicsTerrainDetail", 1.f},
      {"GraphicsBuildingDetail", 1.f},
      {"GraphicsCampaignDetail", 1.f},
      {"GraphicsReflectionQuality", 1.f},
      {"GraphicsContactShadowQuality", 1.f},
  };
  for (const auto& test : engine_tests) {
    // Its option's start: the game's (1x, Game's or -1).
    graphics_defaults.emplace_back(test.key, test.option.scale);
  }
  const std::vector<std::pair<std::string, float>> graphics_max = {
      {"GraphicsShadowMapSize", 3.f},
      {"GraphicsUnitDetailVerySmall", 8.f},
      {"GraphicsUnitDetailSmall", 8.f},
      {"GraphicsUnitDetailMedium", 8.f},
      {"GraphicsUnitDetailLarge", 8.f},
      {"GraphicsUnitDetailVeryLarge", 8.f},
      {"GraphicsDeadUnits", 1.f},
      {"GraphicsUnitAnimation", 4.f},
      {"GraphicsUnitVariety", 4.f},
      {"GraphicsFootstepEffects", 4.f},
      {"GraphicsModelDetail", 8.f},
      {"GraphicsGrassDistance", 8.f},
      {"GraphicsSmallObjects", 8.f},
      {"GraphicsTerrainDetail", 4.f},
      {"GraphicsBuildingDetail", 8.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsReflectionQuality", 4.f},
      {"GraphicsContactShadowQuality", 4.f},
  };
  // A preset: every graphics setting and test at its default but `changes`.
  const auto preset = [&graphics_defaults](const std::vector<std::pair<std::string, float>>& changes) {
    auto values = graphics_defaults;
    for (const auto& [key, value] : changes) {
      std::ranges::find(values, key, &std::pair<std::string, float>::first)->second = value;
    }
    return values;
  };
  // Attila's two (ATN's settings with the Unit Sizes mod's 4x / 8x units, and with the game's), as this game has the rows:
  // Unit Animation also holds the faces and hands, Campaign Detail the plants; no campaign props here.
  const auto with_units = [&preset](float unit_detail, float dead_units, float animation, float buildings) {
    return preset({
        {"GraphicsUnitDetailVerySmall", unit_detail},
        {"GraphicsUnitDetailSmall", unit_detail},
        {"GraphicsUnitDetailMedium", unit_detail},
        {"GraphicsUnitDetailLarge", unit_detail},
        {"GraphicsUnitDetailVeryLarge", unit_detail},
        {"GraphicsDeadUnits", dead_units},
        {"GraphicsUnitAnimation", animation},
        {"GraphicsBuildingDetail", buildings},
        {"GraphicsUnitVariety", 3.f},
        {"GraphicsFootstepEffects", 3.f},
        {"GraphicsCampaignDetail", 4.f},
        {"GraphicsShadowMapSize", 1.f},
    });
  };
  // ATN's own settings: every graphics setting at its default but these.
  const auto graphics_atn = preset({
      {"GraphicsUnitDetailVerySmall", 1.2f},
      {"GraphicsUnitDetailSmall", 1.2f},
      {"GraphicsUnitDetailMedium", 1.2f},
      {"GraphicsUnitDetailLarge", 1.2f},
      {"GraphicsUnitDetailVeryLarge", 1.2f},
      {"GraphicsDeadUnits", 0.15f},
      {"GraphicsUnitAnimation", 2.f},
      {"GraphicsUnitVariety", 2.f},
      {"GraphicsFootstepEffects", 2.f},
      {"GraphicsModelDetail", 3.f},
      {"GraphicsGrassDistance", 1.25f},
      {"GraphicsSmallObjects", 4.f},
      {"GraphicsTerrainDetail", 4.f},
      {"GraphicsBuildingDetail", 6.f},
      {"GraphicsCampaignDetail", 4.f},
      {"GraphicsContactShadowQuality", 4.f},
      {"GraphicsShadowMapSize", 1.f},
  });
  settings.insert(settings.end(), {
      tw::engine_tweaks::Note(),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "With my 4x / 8x unit sizes",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 0.69x, Dead Unit Detail 0.50x, Unit Animation 1.50x, Unit Variety and Footstep Effects\n"
                     "3x, Building Detail 3x, Campaign Detail 4x, Shadow Map Size 8192. The rest at its default.",
          .on_change = [values = with_units(0.69f, 0.5f, 1.5f, 3.f)]() { renodx::utils::settings::UpdateSettings(values); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "With vanilla unit sizes",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 1.20x, Dead Unit Detail 0.85x, Unit Animation, Unit Variety and Footstep Effects 3x,\n"
                     "Building Detail 4x, Campaign Detail 4x, Shadow Map Size 8192. The rest at its default.",
          .on_change = [values = with_units(1.2f, 0.85f, 3.f, 4.f)]() { renodx::utils::settings::UpdateSettings(values); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting and test back to its default.",
          .on_change = [graphics_defaults]() { renodx::utils::settings::UpdateSettings(graphics_defaults); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "MAX",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Every graphics setting at its maximum. Heavy.",
          .on_change = [graphics_max]() { renodx::utils::settings::UpdateSettings(graphics_max); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "What I would use:",
          .section = "Graphics (Experimental)",
          .group = "graphics-presets",
          .tooltip = "Unit Detail 1.20x, Dead Unit Detail 0.15x, Unit Animation, Unit Variety and Footstep Effects 2x,\n"
                     "Model Detail 3x, Grass 1.25x, Small Objects and Terrain Detail 4x, Building Detail 6x, Campaign\n"
                     "Detail 4x, Contact Shadow Quality 4x, Shadow Map Size 8192. The rest at its default.",
          .on_change = [graphics_atn]() { renodx::utils::settings::UpdateSettings(graphics_atn); },
      },
      tw::engine_tweaks::Slider(&unit_detail[0], "GraphicsUnitDetailVerySmall", "Unit Detail (Very Small Units)",
                                "How far very small units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[1], "GraphicsUnitDetailSmall", "Unit Detail (Small Units)",
                                "How far small units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[2], "GraphicsUnitDetailMedium", "Unit Detail (Medium Units)",
                                "How far medium units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[3], "GraphicsUnitDetailLarge", "Unit Detail (Large Units)",
                                "How far large units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_detail[4], "GraphicsUnitDetailVeryLarge", "Unit Detail (Very Large Units)",
                                "How far very large units stay 3D before turning into flat sprites.\n"
                                "Higher: heavier for the CPU and GPU.",
                                8.f, 0.1f),
      tw::engine_tweaks::Slider(&dead_units, "GraphicsDeadUnits", "Dead Unit Detail Distance",
                                "Where dead soldiers turn into flat sprites, x the living ones' distance.\n"
                                "Lower: cheaper battles with many dead.",
                                1.f, 0.01f),
      tw::engine_tweaks::Slider(&unit_animation, "GraphicsUnitAnimation", "Unit Animation Distance",
                                "How far soldiers move on their own, with faces and hands (beyond it they copy their\n"
                                "neighbours' moves). Higher: heavier for the CPU.",
                                4.f, 0.1f),
      tw::engine_tweaks::Slider(&unit_variety, "GraphicsUnitVariety", "Unit Variety Distance",
                                "How far soldiers keep their own heads and gear (beyond it they look alike).\n"
                                "Higher: heavier.",
                                4.f, 0.1f),
      tw::engine_tweaks::Slider(&footstep_effects, "GraphicsFootstepEffects", "Footstep Effects Distance",
                                "How far soldiers' footsteps kick up dust. Higher: heavier.", 4.f, 0.1f),
      tw::engine_tweaks::Slider(&model_detail, "GraphicsModelDetail", "Model Detail Distance",
                                "Units, buildings and other models keep their detailed versions farther away.", 8.f),
      tw::engine_tweaks::Slider(&grass_distance, "GraphicsGrassDistance", "Grass Distance",
                                "Grass reaches farther.", 8.f),
      tw::engine_tweaks::Slider(&small_objects, "GraphicsSmallObjects", "Small Object Distance",
                                "Small objects, plants and rocks stay visible farther away.", 8.f),
      tw::engine_tweaks::Slider(&terrain_detail, "GraphicsTerrainDetail", "Terrain Detail Distance",
                                "Detailed terrain reaches farther.", 4.f),
      tw::engine_tweaks::Slider(&building_detail, "GraphicsBuildingDetail", "Building Detail Distance",
                                "Battle: buildings keep their detailed versions farther away. From the next battle.",
                                8.f),
      tw::engine_tweaks::Slider(&campaign_detail, "GraphicsCampaignDetail", "Campaign Detail Distance",
                                "Campaign map: trees and plants keep their detail farther away.\n"
                                "Plants: from the next campaign load.",
                                4.f),
      tw::engine_tweaks::Slider(&reflection_quality, "GraphicsReflectionQuality", "Reflection Quality",
                                "More steps for screen-space reflections: fewer gaps. Heavier.", 4.f),
      tw::engine_tweaks::Slider(&contact_shadow_quality, "GraphicsContactShadowQuality", "Contact Shadow Quality",
                                "More steps for the game's small screen-space shadows. Heavier.\n"
                                "Only with Game's Shadow Maps: Bend's and ray-traced shadows replace them.", 4.f),
      new renodx::utils::settings::Setting{
          .key = "GraphicsShadowMapSize",
          .binding = &shadow_map_size,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Shadow Map Size",
          .section = "Graphics (Experimental)",
          .tooltip = "Sharper sun shadows that flicker less, at the highest Shadows settings. More video memory\n"
                     "(16384: 1 GB or more). Applies after a restart or after changing the game's Shadows option.",
          .labels = {"6144 (Game's)", "8192", "12288", "16384"},
      },
      graphics_status,
      // Two empty lines between this section and the ambient occlusion (ATN).
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "",
          .section = "Graphics (Experimental)",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "",
          .section = "Graphics (Experimental)",
      },
  });

  // Appended Settings (Graphics 2 of 2's tests)
  settings.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Tests: each changes the game's own values (1x / Game's: unchanged). Many apply from the next\n"
               "battle or after a restart; some may do nothing or crash. More Tests: every other graphics\n"
               "value the game reads (unsure ones).",
      .section = engine_tests.empty() ? std::string("More Tests") : engine_tests.front().section,
      .tint = 0xFFD23C,
  });
  twattila::engine_tests::Append(&settings, &engine_tests, SyncEarlyLoad);

  // Appended Settings (Ambient Occlusion, Global Illumination, Screen-Space Shadows, Anti-Aliasing)
  twattila::lighting_settings::Append(&settings, &shader_injection);
  twattila::feature_settings::AppendSss(&settings, &sss_sliders);
  twattila::feature_settings::AppendAa(&settings, &aa_sliders, true, true);
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders);

  // Appended Settings (HDR10)
  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR. Default.\n"
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
        .hdr_encoding = [](int output) {
          if (output != twattila::output_encoding::AUTO) return output;
          // Frame generation on from the start: HDR10, the frames need no pass of their own (frame_generation.hpp).
          const bool frame_generation = aa_sliders.method == 5.f && aa_sliders.frame_generation != 0.f;
          return frame_generation ? twattila::output_encoding::HDR10 : twattila::output_encoding::SCRGB;
        },
    });
    settings.push_back(setting);
  }

  // Appended Settings (About)
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV30, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom "
                   "PsychoV31 and PsychoV35. Visibility "
                   "Bitmask ambient occlusion and Light Bounce: the method of Therrien, Levesque and Gilet (2023) on "
                   "GTAO (Jimenez et al. 2016), as GT-VBAO and GT-VBGI by Mirko Salm (2024). Screen-space shadows: "
                   "Bend Studio (Apache 2.0, Sony Interactive Entertainment). SMAA (MIT, Jimenez et al.), CMAA2 "
                   "(Apache 2.0, Intel).",
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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarpharaoh/mods/19"); },
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

  // Section titles and Settings Mode's pages as Attila's (twattila/feature_settings.hpp).
  renodx::utils::settings::default_section_tint = twattila::feature_settings::BASE_TINT;
  std::vector<std::string> engine_sections = {twattila::feature_settings::GRAPHICS_SECTION};
  for (const auto& test : engine_tests) {
    if (std::ranges::find(engine_sections, test.section) == engine_sections.end()) engine_sections.push_back(test.section);
  }
  twattila::feature_settings::SetTints({}, engine_sections);

  // Graphics 1 of 2's layout (as Warhammer III's), ATN's RT/PT preset as the defaults of what the methods leave, then
  // Settings Mode's pages. Only what this game hooks: no smoke map, foliage light or reflections.
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  // Torch and Fire Shadows from rays (Warhammer III's row): after the preset's defaults, so it starts off (Experimental: no game
  // run has shown the light draws read yet).
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "RayTracedPointLightShadows",
      .binding = &rt_sliders.local_lights,
      .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
      .default_value = 0.f,
      .label = "Torch and Fire Shadows (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Torches and fires cast their shadows from rays too.\n"
                 "With Path-Traced Global Illumination they also light what the screen never saw.",
      .labels = {"Off", "On"},
  });
  twattila::graphics_page::Apply(&settings, {.aa = &aa_sliders, .sss = &sss_sliders, .rt = &rt_sliders, .ssao_mode = &shader_injection.ssao_mode,
                                             .light_bounce_strength = &shader_injection.light_bounce_strength,
                                             .rays_units_out_of_view = &rays_units_out_of_view_row,
                                             .units_out_of_view_tested = false,
                                             .rays_hills_out_of_view = &rays_hills_out_of_view_row,
                                             .texts = &PAGE_TEXTS,
                                             .has_foliage_light = false,
                                             .lights_own_row = true,
                                             .has_reflections = false,
                                             .rays_replace_ao = false,
                                             .rays_multiply_maps = true});
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, twattila::graphics_page::Sections());

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  twattila::test_hooks::Install(&settings, true);
  reshade::register_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests: written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(nullptr, &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // Before the Light Bounce's (its callback draws the sun draw again).
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace. The ambient cube starts at
  // lighting_VS_PS float 24 (c6-c11: +x, -x, +y, -y, +z, -z; WARP on all 25 sun shaders, tools\tw\pharaoh\sun_test).
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 24);
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  // The game's contact shadows off where ours shadow: after the screen-space shadows' (they bind ours at t49), before the Light
  // Bounce's (it draws the sun draw itself with the constants bound then).
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyContactShadowsDevice);
  for (const auto& sun_draw : LIGHT_BOUNCE_DRAWS) {
    renodx::utils::command_action::Register(
        OnSunContactShadows, {.shader_hash = sun_draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &sun_draw);
  }
  // The frame trace (the data run): renodx_trace_now next to the exe traces the next frame, and FrameTrace's frame also
  // at 100% (twattila/frame_trace.hpp). The sun light draws' (standard and skin) and the tone mapper's constants once
  // each: b0-b5 up to 1 KB (camera b1 with camera_near_far at c25, shadowmap_PS b2's splits and baked map,
  // lighting_VS_PS b4's sun and ambient cube, directional_light_PS b5; the tone mapper's camera b0 and tone_mapping b1).
  // The 3D sprites' depth stretched below 100%, the scene's depth's copy too (FULL_DEPTH_SHADERS).
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, {}, FULL_DEPTH_SHADERS, {},
                                   {
                                       // Also the point light's and the AO's (g_viewport_offset_scale, g_Resolution: their
                                       // 100% values below 100%, assumed until a trace lists them).
                                       .value_hashes = {0xF883F5B2, 0xE238D4C7, 0x976C1C74, 0xE318CE77, 0xD42C3523},
                                       .value_slots = 6,
                                       .value_floats = 256,
                                       .on_demand = true,
                                       .at_100 = true,
                                   },
                                   true);
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  // Before the Light Bounce: it draws the sun draws itself (no callback after it runs).
  twattila::ao_filter::Install(LIGHT_BOUNCE_DRAWS);
  // The ambient occlusion pass before ray tracing's (its result multiplied after), as Attila.
  twattila::ao_pass::Install(AO_DRAWS, &shader_injection, sizeof(shader_injection));
  // The TW games' view; the shadow pass's casters through world positions (its vertex shaders' view-projection at cb0[9-12], not
  // Warhammer III's cb0[10-13]), from the sun's per-frame atlas only (R32, 2 x 2 cascades; not the kept baked map, R16 4096 x 4096).
  twattila::rt_scene::Host rt_host = twattila::rt_scene::TW_HOST;
  rt_host.cascade_world = true;
  rt_host.cascade_target = [](const D3D11_TEXTURE2D_DESC& desc) {
    return desc.ArraySize == 1 && desc.Width == desc.Height && desc.Format == DXGI_FORMAT_R32_TYPELESS;
  };
  // The point light PS's diffuse scale (E318CE77's literal 0x389FB405, Warhammer III's).
  rt_host.local_light_scale = 7.615242e-05f;
  twattila::rt_scene::Install(SCENE_SHADERS, AO_DRAWS, rt_host);
  twattila::rt_scene::InstallCutOutMasks(CUT_OUT_MASKS);
  // The point lights (siege trace: one instanced draw, VS E19549A1 with PS E318CE77, after the sun draw F883F5B2; fire02 reads
  // the rays' bits). Log Details' light count also counts the particle lights (5AD46115).
  twattila::rt_scene::InstallPointLights(std::array<uint32_t, 1>{0xE318CE77});
  for (const uint32_t shader_hash : {0xE318CE77u, 0x5AD46115u}) {
    renodx::utils::command_action::Register(
        OnLightCountDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);
  renodx::utils::command_action::Register(
      OnSkyDomeGuides, {.shader_hash = 0x9CF980D0, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});

  reshade::register_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  twattila::test_hooks::Uninstall();
  tw::engine_tweaks::Uninstall(graphics_options);
  twpharaohdynasties::dead_units::Uninstall();
  twpharaohdynasties::shadow_map::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  twattila::engine_tests::StopAtStart(&at_start_stop);
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  renodx::utils::command_action::Unregister(OnSunContactShadows);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyContactShadowsDevice);
  OnDestroyContactShadowsDevice(nullptr);
  twattila::light_bounce::Uninstall();
  twattila::rt_scene::Uninstall();
  renodx::utils::command_action::Unregister(OnLightCountDraw);
  renodx::utils::command_action::Unregister(OnSkyDomeGuides);
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
  twattila::ao_filter::Uninstall();
  twattila::ao_pass::Uninstall();

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
      // Not FALSE when it fails: ReShade next to renodx-tw-dlaa64.exe loads this addon there too, and after a FALSE the CRT's
      // thread_local cleanup faults in the loader (ntdll c0000005, a Windows crash report each start). AddonInit fails instead.
      addon_registered = reshade::register_addon(h_module);
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
