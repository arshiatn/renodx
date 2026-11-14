/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0

#include <d3d11.h>
#include <array>
#include <atomic>
#include <mutex>
#include <cmath>
#include <string>
#include <utility>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/command_action.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/ao_pass.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/light_count.hpp"
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
#include "../twattila/smoke_map.hpp"
#include "../twattila/contact_shadows_off.hpp"
#include "../twattila/sun_replace_maps.hpp"
#include "../twattila/test_hooks.hpp"
#include "../twattila/water_effects.hpp"
#include "../twattila/water_reflections.hpp"
#include "./more_tests.h"
#include "./tweak_objects.hpp"
#include "../twpharaohdynasties/trace_reads.hpp"
// Pharaoh's outside-view geometry shaders (built bytes): Troy's shadow vertex shaders write the same outputs (SCENE_SHADERS).
#include "../twpharaohdynasties/rt_outside_view_branch_gs.h"
#include "../twpharaohdynasties/rt_outside_view_card_gs.h"
#include "../twpharaohdynasties/rt_outside_view_unit_gs.h"
#include "../twpharaohdynasties/rt_outside_view_world_gs.h"
#include "./rt_outside_view_leaf_gs.h"
#include "./rt_particle_lights_gs.h"
#include "./rt_particle_quads_gs.h"
#include "../twrome2/engine_tweaks.hpp"
#include "../twwarhammer2/line_sweep_ao.hpp"
#include "../twattila/psycho35_settings.hpp"
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

// bool IsModdedAutoExposureEnabled() {
//   return shader_injection.auto_exposure_highlight_protection == 1.f;
// }

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

      // PsychoV31
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
      {"v31_highlight_hue_shift", 0.1f},
      {"v31_highlight_saturation_shift", 0.f},

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
      {"dof", 1.f},
      {"chromaticaberration", 1.f},
      {"sharpening", 1.f},
      {"some_indicators", 1.0f},

      // Active experimental controls
      {"distortion", 1.f},
      {"filmgrain", 1.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

void ApplyPsychoRecommended() {
  // ATN 2026-11-12: every recommended preset in PsychoV35 (its sliders at their defaults), Video AutoHDR 0.20.
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 4.f},
      {"video_autohdr", 0.2f},

      // PsychoV30
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"purity_scale", 1.f},
      {"cone_response_exponent", 1.f},
      {"hue_shift", 2.f},
      // {"current_adaptive_state_bt709", 0.18f},
      // {"current_background_state_bt709", 0.18f},

      // HDR Extended / user grading
      // {"exposure_tpm", 1.f},
      // {"highlights_tpm", 1.f},
      // {"shadows_tpm", 1.f},
      // {"contrast_tpm", 1.f},
      // {"saturation_tpm", 1.f},
      // {"highlight_expansion", 1.f},

      // Active extra controls
      {"bloom", 1.f},
      {"lensflare", 1.f},
      {"godrays", 1.f},
      {"vignette", 1.f},
      {"dof", 1.f},
      {"chromaticaberration", 1.f},
      {"sharpening", 1.f},
      {"some_indicators", 1.0f},

      // Active experimental controls
      {"distortion", 1.f},
      {"filmgrain", 1.f},
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
            twattila::anti_aliasing::OnTonemapDrawn(cmd_list);                                        \
            twattila::rt_scene::OnTonemapDrawn(cmd_list); },                                          \
      },                                                                                              \
  }

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

void BuildRuntimeData() {
  custom_shaders = {
      FrameCopyShader(0x506EFD2A), //sharpening (reads our 16-bit frame copy)
      FrameCopyShader(0x280C1743), //chromatic aberration (reads our 16-bit frame copy)
      UpgradeRTVReplaceShader(0x0C12E608), //t00
      UpgradeRTVReplaceShader(0x48DBD700), //t01
      UpgradeRTVReplaceShader(0x3CEBDECA), //t02
      UpgradeRTVReplaceShader(0x92CF3357), //t03
      UpgradeRTVReplaceShader(0x93463F8F), //t04
      UpgradeRTVReplaceShader(0xFE084600), //t05
      UpgradeRTVReplaceShader(0x52D64412), //t06
      UpgradeRTVReplaceShader(0x9DF1DC36), //t07
      UpgradeRTVReplaceShader(0xF0E7480B), //t08
      UpgradeRTVReplaceShader(0x35EB068F), //FXAA
      UpgradeRTVReplaceShader(0x624D3FBE), //photo01-04: photo mode
      UpgradeRTVReplaceShader(0xA59AAB78),
      UpgradeRTVReplaceShader(0xF3905B4B),
      UpgradeRTVReplaceShader(0x0C8CDDF6),
      // UpgradeRTVReplaceShader(0x0F325087), //cutscene
      // UpgradeRTVReplaceShader(EMPTY),
      __ALL_CUSTOM_SHADERS,
  };
  // The water's reflection apply (shaders/ssrapply00): ours only where the rays left from the water or below 100%
  // (twattila/water_reflections.hpp OnWaterApplyDraw), as Pharaoh's.
  custom_shaders.erase(0x00645DA1);
  // Ambient Occlusion: Visibility Bitmask in place of the AO's horizontal blur: the addon's own shader
  // (ao_bitmask_blur, twwarhammer2/line_sweep_ao.hpp) with the blur's bindings; the blur's replacement stays the game's
  // code (Game's costs what it did). The collect always runs: where no blur runs (Warhammer II at some settings), the
  // game's AO stays, never an unwritten one.
  custom_shaders[0x16A457D0].on_draw = [](auto* cmd_list) {
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
        .tooltip = "SDR: the game's SDR tone mapper.\n"
                   "HDR (Extended): the game's curve extended into HDR.\n"
                   "HDR (PsychoV30): PsychoV30 as tone mapper.\n"
                   "HDR (PsychoV31): PsychoV31 as tone mapper, more grading sliders.\n"
                   "HDR (PsychoV35): Musa's PsychoV35 as tone mapper, with Hue Clip Emulation (the Recommended presets).",
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
        .label = "Recommended",
        .section = "Presets",
        .group = "preset-line-1",
        .tooltip = "HDR (PsychoV35) and Video AutoHDR 0.20, the rest at its default.\nPeak, Paper White, UI brightness, Settings Mode and output settings are kept.",
        .on_change = []() {
           ApplyPsychoRecommended();
        },
    },
    // new renodx::utils::settings::Setting{
    //     .value_type = renodx::utils::settings::SettingValueType::BUTTON,
    //     .label = "Recommended: PsychoV30 (1000 nits)",
    //     .section = "Presets",
    //     .group = "preset-line-1",
    //     .tooltip = "Recommended PsychoV30 tuning for a ~1000-nit display.\n"
    //                "Uses PsychoV30 defaults except Highlights = 0.82.\n"
    //                "Peak, Paper White, UI brightness, and unrelated controls are preserved.",
    //     .on_change = []() {
    //       ApplyPsychoRecommended1000Preset();
    //     },
    // },
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
               "0 = off, 1 = SDR hues. Default 0.10.",
               1.f, 0.1f),
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
        .tooltip = "Multiplier on Sharpening. Needs in-game setting on.",
        .min = 0.f,
        .max = 2.f,
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
        .tooltip = "Brightness of selected highlight indicators, particularly useful at night.",
        .min = 0.0f,
        .max = 2.0f,
        .format = "%.1f",
        .is_enabled = IsHDRMode,
    },
    new renodx::utils::settings::Setting{
        .key = "distortion",
        .binding = &shader_injection.uvdistort,
        .default_value = 1.f,
        .label = "Distortion",
        .section = "Extra (Experimental)",
        .tooltip = "Multiplier on distortion. YOU NEED TO CHOOSE PRESET ULTRA\n"
                   "THEN YOUR OWN CUSTOME SETTINGS for the hidden distortion\n"
                   "to get turned on. \n"
                   "It was probably hidden because it is not getting used anymore!",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
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
    new renodx::utils::settings::Setting{
        .key = "sky_deband",
        .binding = &shader_injection.sky_deband,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Sky Debanding",
        .section = "Extra",
        .tooltip = "Smooths the sky's banding and adds fine dither.\n"
                   "Higher: removes wider bands, softens sky details more.",
        .labels = {"Off", "Low", "Medium", "High"},
    },
    new renodx::utils::settings::Setting{
        .key = "filmgrain",
        .binding = &shader_injection.filmgrain,
        .default_value = 1.f,
        .label = "Filmgrain",
        .section = "Extra (Experimental)",
        .tooltip = "Multiplier on filmgrain. YOU NEED TO CHOOSE PRESET ULTRA\n"
                   "THEN YOUR OWN CUSTOME SETTINGS for the hidden filmgrain\n"
                   "to get turned on. \n"
                   "It was probably hidden because it is not getting used anymore!",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = IsHDRMode,
    },
    


    // Auto Exposure //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "auto_exposure_highlight_protection",
    //     .binding = &shader_injection.auto_exposure_highlight_protection,
    //     .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
    //     .default_value = 0.0f,
    //     .label = "Modded Auto Exposure",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Reduces the exposure influence of isolated highlights such as the sun, fire, and sparks without reducing their rendered brightness. Off is stock.",
    //     .labels = {"Off (Stock)", "On (Protected)"},
    //     .is_visible = IsAdvancedSettings,
    // },
    // new renodx::utils::settings::Setting{
    //     .key = "auto_exposure_highlight_headroom_stops",
    //     .binding = &shader_injection.auto_exposure_highlight_headroom_stops,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "Modded Auto Exposure: Sensitivity",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Maximum metered highlight above the frame average, in stops. Lower values protect exposure more strongly. Higher values are closer to stock.",
    //     .min = 0.0f,
    //     .max = 2.0f,
    //     .format = "%.1f",
    //     .is_enabled = IsModdedAutoExposureEnabled,
    //     .is_visible = IsAdvancedSettings,
    // },
    // VFX //////////////////////////////////////////////////////////////////////////////////////
    // new renodx::utils::settings::Setting{
    //     .key = "vfx_fire_brightness",
    //     .binding = &shader_injection.vfxfirebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: fire/smoke/unit's dust",
    //     .section = "VFX (Requires Shadow at least on medium)",
    //     .tooltip = "Fire, embers, sparks, smoke, and .... Recommmendation is max 10 with modded auto exposure.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.0f",
    //     .is_visible = IsAdvancedSettings,
    // },
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
    //     .key = "vfx_base_brightness",
    //     .binding = &shader_injection.vfxbasebrightness,
    //     .value_type = renodx::utils::settings::SettingValueType::FLOAT,
    //     .default_value = 1.0f,
    //     .label = "VFX Brightness: Flame, Dust, Clouds & Smoke",
    //     .section = "Extra (Experimental)",
    //     .tooltip = "Brightness of smoke, clouds, dust, and flame. This does not boost the inner fire color.",
    //     .min = 1.0f,
    //     .max = 30.0f,
    //     .format = "%.1f",
    //     .is_visible = IsAdvancedSettings,
    // },
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


// Ambient Occlusion Visibility Bitmask (twattila/ao_pass.hpp): the addon's own shader in place of the game's AO
// pass (whose replacement is the game's code only).
constexpr std::array<twattila::ao_pass::AmbientOcclusionDraw, 1> AO_DRAWS = {{
    {.shader_hash = 0xD42C3523, .bitmask = __ao_bitmask, .upsample = __ao_upsample},  // ssao01
}};

// Light Bounce (twattila/light_bounce.hpp): directionallight.hlsl's sun light draws, their camera at b1,
// the G-buffer's channels 1-4 from their first slot.
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
// Smoke Shadows without rays (twattila/smoke_map.hpp, as Attila's and Thrones').
twattila::smoke_map::Sliders smoke_map_sliders;
// Texture LOD Bias on All Textures (Graphics 1 of 2, Experimental): the trilinear samplers too
// (texture_lod_bias.hpp SetWrappingTrilinear).
float lod_bias_trilinear_row = 0.f;
// Shadow Casters Out of View and the hills' (Graphics 1 of 2, Experimental): SCENE_SHADERS' cascade_keep casters.
float rays_units_out_of_view_row = 0.f;
float rays_hills_out_of_view_row = 0.f;
// Rays Replace Shadow Maps and Skip the Shadow Maps under it (Graphics 1 of 2, Experimental): twattila/sun_replace_maps.hpp on the
// sun draws reading the per-frame atlas through s2: the twins of Pharaoh's 12 (same names; tools\tw\troy\troy_atlas_twins.py).
constexpr std::array<uint32_t, 12> ATLAS_SUN_SHADERS = {0x049C3CFB, 0x2599D799, 0xD38FA7E4, 0x66D6229E, 0x5B17424A, 0xEB98DED2,
                                                        0x707B069C, 0x6D3D2B69, 0xE1707311, 0x089D1B5B, 0x5835F96C, 0xE885C018};
float rays_replace_maps_row = 0.f;
float rays_skip_cascades_row = 0.f;

void SyncEarlyLoad() { twattila::engine_tests::SyncEarlyLoad(addon_module_handle, engine_tests); }
renodx::utils::settings::Setting* graphics_status = nullptr;
bool graphics_found = false;

// DLAA / DLSS object motion (twattila/anti_aliasing.hpp, as Attila's): the game's shaders whose things move on
// their own and their motion versions (tools\tw\attila_motion\port_motion.py, GPU-checked by test_motion.py).
using twattila::anti_aliasing::MotionKind;
constexpr std::array<twattila::anti_aliasing::MotionShader, 20> MOTION_SHADERS = {{
    {.hash = 0x04C7C807, .motion_shader = __motion_tree_campaignvegetation_04c7},
    {.hash = 0x0AF83680, .motion_shader = __motion_grass_grass_0af8},
    {.hash = 0x18CE6F5A, .motion_shader = __motion_sail_sail_18ce, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sail_18ce},
    {.hash = 0x26A41B7D, .motion_shader = __motion_unit_weighted_26a4, .kind = MotionKind::UNIT},
    {.hash = 0x283E18D7, .motion_shader = __motion_rigid_rigidtextureblend_283e, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblend_283e},
    {.hash = 0x3CEE34E8, .motion_shader = __motion_unit_weighted4_default_alpha_3cee, .kind = MotionKind::UNIT},
    {.hash = 0x459A414F, .motion_shader = __motion_ocean_coast_ocean2_459a, .kind = MotionKind::HISTORY, .history_textures = 0b10101},
    {.hash = 0x4EA73F65, .motion_shader = __motion_leaf_leaf_v3_4ea7},
    {.hash = 0x592E666F, .motion_shader = __motion_sail_sailsymbol_592e, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_sailsymbol_592e},
    {.hash = 0x5D27A793, .motion_shader = __motion_unit_weighted_5d27, .kind = MotionKind::UNIT},
    {.hash = 0x5E788ED3, .motion_shader = __motion_branch_branch_v3_5e78},
    {.hash = 0x5F79A748, .motion_shader = __motion_rope_rope_5f79, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope_rope_5f79},
    {.hash = 0x88CACF98, .motion_shader = __motion_ocean_shallow_ocean2_88ca, .kind = MotionKind::HISTORY, .history_textures = 0b101},
    {.hash = 0x92CC9B10, .motion_shader = __motion_oldocean_ocean_92cc},
    {.hash = 0xB057851F, .motion_shader = __motion_rigid_rigidambientmapground_b057, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidambientmapground_b057},
    {.hash = 0xB9B62F83, .motion_shader = __motion_unit_weighted2_default_alpha_b9b6, .kind = MotionKind::UNIT},
    {.hash = 0xD1CF69DC, .motion_shader = __motion_ocean_deep_ocean2_d1cf, .kind = MotionKind::HISTORY, .history_textures = 0b1},
    {.hash = 0xDD82D47A, .motion_shader = __motion_rigid_rigidtextureblendwind_dd82, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_rigidtextureblendwind_dd82},
    {.hash = 0xEE95696C, .motion_shader = __motion_unit_weighted4_fur_alpha_ee95, .kind = MotionKind::UNIT},
    {.hash = 0xEF212374, .motion_shader = __motion_tree_troy_prefab_sky_cylinder_ef21},
}};

// Ray tracing's scene (twattila/rt_scene.hpp; tools\tw\rt_port.py): the main view's terrain, rigid objects, tree
// branches and leaves, units, grass and far tree cards, and their capture versions (world positions; leaves, grass and
// cards also UVs). The floating unit banners (rigidtextureblend, 2 bones) stay out: the game gives them no shadow. So do
// the far forest's blocks (rigidtreebillboard vs40_main_block 1FD86210): cards facing the camera with no shadow-pass draw,
// as Pharaoh's FE92BA2D.
// A96ACBCB (rigid_default.xml and its building materials) also draws the sea and lakes (water_troy), the roads' spline
// overlays, vfx borders and random-colour meshes, and the emissive buildings' second pass (deferred_alpha AC1DC303): only
// its G-buffer pixel shaders are in the scene, the three ATN's traces drew (sessions 2 / 3, 2026-10-27; tools\tw\troy\runs),
// as Pharaoh's BUILDING_MASKS. None alpha-tests (a dither, t2 / t0, or nothing).
constexpr std::array<twattila::rt_scene::PixelMask, 3> BUILDING_MASKS = {{
    {.pixel_shader = 0x42AE1C31},  // rigid_default
    {.pixel_shader = 0xB6F037CC},  // rigid_building_default
    {.pixel_shader = 0xD64BE263},  // rigid_building_emissive (all discarded where g_dynamic_light_scale <= 0.2)
}};

// B057851F's alpha-tested materials (sorted): each discards where t0.w x the fade (v6.w) is below 0.5 at TEXCOORD1.xy (the
// vertex's TEXCOORD0.xy, the VS's o2), the helper's cut-out (LEAF_ALPHA_CUTOFF 0.5); captured with that UV
// (rt_capture_rigid_standard_cut_out). Pharaoh's CUT_OUT_MASKS' twins; 8A83B40C 472 of session 3's captures.
constexpr std::array<twattila::rt_scene::PixelMask, 4> CUT_OUT_MASKS = {{
    {.pixel_shader = 0x18A8AF9B},  // rigidstandard gbuffer_alpha_test_shroud_fizzle_oblique_tris
    {.pixel_shader = 0x1C0705F6},  // rigidambientmapground gbuffer_shroud, rigidambientmapmesh gbuffer_alpha_test
    {.pixel_shader = 0x36A9A33D},  // rigidwithdirtmesh gbuffer_alpha_test
    {.pixel_shader = 0x8A83B40C},  // rigidstandard gbuffer_alpha_test
}};

constexpr std::array<twattila::rt_scene::SceneShader, 30> SCENE_SHADERS = {{
    {.hash = 0x981F929B, .capture_shader = __rt_capture_terrain_geom_new, .terrain = true},
    {.hash = 0x8BE644E7, .capture_shader = __rt_capture_terrain_custom_material, .terrain = true},
    {.hash = 0x8974E616, .capture_shader = __rt_capture_terrain_custom, .terrain = true},
    {.hash = 0xA96ACBCB, .capture_shader = __rt_capture_rigid_default, .pixel_masks = BUILDING_MASKS},
    {.hash = 0xB057851F, .capture_shader = __rt_capture_rigid_standard, .rows_shader = __motion_record_rigid_rigidambientmapground_b057,
     .cut_out_shader = __rt_capture_rigid_standard_cut_out, .clip_cut_outs = true},
    // The campaign's trees (rigidstandard vs40_main_campaign_trees, Pharaoh's 1C4B8681): B057851F's vertex shader and instance
    // rows, then an x / z sway of at most 0.12 m x g_wind_vegetation_multiplier (left out: still); C7FDC209 cuts out t0.w x the
    // fade below 0.01 (the helper's cut-out 0.5).
    {.hash = 0x54D2CFBD, .capture_shader = __rt_capture_rigid_standard_cut_out, .rows_shader = __motion_record_rigid_rigidambientmapground_b057,
     .foliage = true, .still = true},
    {.hash = 0x283E18D7, .capture_shader = __rt_capture_rigid_blend, .rows_shader = __motion_record_rigid_rigidtextureblend_283e},
    {.hash = 0xDD82D47A, .capture_shader = __rt_capture_rigid_blend_wind, .rows_shader = __motion_record_rigid_rigidtextureblendwind_dd82},
    {.hash = 0x86534458, .capture_shader = __rt_capture_rigid_vertexpush},
    {.hash = 0x4D3FD8F9, .capture_shader = __rt_capture_rigid_terrain_blend},
    {.hash = 0x5E788ED3, .capture_shader = __rt_capture_branch},
    {.hash = 0x4EA73F65, .capture_shader = __rt_capture_leaf, .foliage = true},
    {.hash = 0x5D27A793, .capture_shader = __rt_capture_unit, .dynamic = true},
    {.hash = 0x26A41B7D, .capture_shader = __rt_capture_unit_4_bones, .dynamic = true},
    {.hash = 0xB9B62F83, .capture_shader = __rt_capture_unit_dismember, .dynamic = true},
    {.hash = 0x3CEE34E8, .capture_shader = __rt_capture_unit_4_bones_dismember, .dynamic = true},
    {.hash = 0x0AF83680, .capture_shader = __rt_capture_grass_0af8, .dynamic = true, .foliage = true, .grass = true},
    // The drawn grass (grass.hlsl vs40_main: 2026-10-27's battle, 7 draws, 9.7 M indices; ATN's 2026-11-14 traces), its UV
    // at o2.xy (TEXCOORD0; Pharaoh's twin E710D4E7 has it at o3).
    {.hash = 0x2C7FBC7D, .capture_shader = __rt_capture_grass_2c7f, .dynamic = true, .foliage = true, .grass = true},
    // Far trees' cards (rigidtreebillboard vs40_main): turned to the sun, their alpha at t3 (FCE1EAEC's t_diffuse_map_tree).
    {.hash = 0xE25688ED, .capture_shader = __rt_capture_far_tree, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 3},
    // The campaign's far trees (campaigntreebillboards 0xDF8EAF69: instanced cards turned to the camera, none closer than the
    // game's start distance or under the shroud; no shadow-pass draw, so only with Far Tree Shadows): turned to the sun, their
    // alpha DAEF2EEF's t0 (below 0.5 discarded, as the helper's cut-out). Its vertex shader reads the shroud at t0: rt_sun at t1.
    {.hash = 0xDF8EAF69, .capture_shader = __rt_capture_campaign_billboard, .dynamic = true, .foliage = true, .sun_facing = true, .sun_slot = 1, .far_tree = true},
    // The shadow pass's casters (the 6144 x 6144 R32 atlas, 2 x 2 cascades; its vertex shaders' view-projection at cb0[9-12]):
    // what is out of the main view still casts there (Pharaoh's rt_outside_view geometry shaders: each variant's input is a
    // prefix of these vertex shaders' outputs, fxc /dumpbin, and their world position is that output: tools\tw\troy\
    // troy_signatures.txt, troy_output_writes_casters.txt). Every frame as drawn; far cards cut out by 9004F948's t2, leaves by
    // 3372DE0B's t0 (t_diffuse_map). Troy's leaves write no TEXCOORD4: their variant without it (RT_NO_FADE, our own header).
    {.hash = 0xA295269F, .dynamic = true, .foliage = true, .cascade_keep = __rt_outside_view_leaf},
    {.hash = 0xAF7C4636, .terrain = true, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0xC8DA195D, .terrain = true, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0x83A7A039, .dynamic = true, .cascade_keep = __rt_outside_view_world},
    {.hash = 0xCB8BA7B6, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0xAEE2A80A, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x8624BF7C, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0xB6BC2496, .dynamic = true, .cascade_keep = __rt_outside_view_unit},
    {.hash = 0x4215C70C, .dynamic = true, .cascade_keep = __rt_outside_view_branch},
    {.hash = 0xB320F789, .dynamic = true, .foliage = true, .sun_facing = true, .far_tree = true, .mask_slot = 2, .cascade_keep = __rt_outside_view_card},
}};

// Smoke Shadows' particles (Experimental, off; Pharaoh's): particle_vfx_main vs50_main_instancing F5CE48CC (Pharaoh's 109B79CE:
// the same signatures, 14 of 594 instructions apart) draws each particle as an instance of a quad, and ps_main_high_quality
// 563BA3E8 blends the lit ones (its alpha Pharaoh's 3F1B86E1's: the diffuse array's alpha at both cells, sRGB-decoded with
// 0.947867334, x COLOR.w, the alpha crush; EMITTER_CONSTANTS_PS the same to byte 72). Its diffuse array, emitters and sampler
// at t7 / t9 / s5: Pharaoh's rt_particle_quads with those slots (rt_particle_quads_gs.h). Effects quality 0-2's ps_main
// 9C8987DD binds them at t4 / t6 / s3: three views and two samplers up.
constexpr std::array<twattila::rt_scene::SmokeVertexShader, 1> SMOKE_VERTEX_SHADERS = {{{.hash = 0xF5CE48CC, .quad_shader = __rt_particle_quads}}};
constexpr std::array<twattila::rt_scene::SmokePixelShader, 2> SMOKE_PIXEL_SHADERS = {{
    {.hash = 0x563BA3E8},
    {.hash = 0x9C8987DD, .view_shift = 3, .sampler_shift = 2},
}};
// The night siege's fires in Torch and Fire Shadows: the particle lights' draw (ps_main_deferred_dynamic_light 643FC156, one
// light volume an instance of F5CE48CC's mesh branch, every draw over the frame's whole sorted list: ATN's session 2, 60
// indices x 642-1335 instances) holds its lights in the particle list, not in instances: streamed from the vertex shader's
// outputs (rt_particle_lights_gs.h: the position, reach and colour shaders/particlelight00 draws with).
constexpr twattila::rt_scene::LightInstances PARTICLE_LIGHTS = {.stream_shader = __rt_particle_lights};

// Water Clarity and Water Caustics (twattila/water_effects.hpp): every water pixel shader of the dump (water_troy's battle and
// campaign sea and lake, battle_river_troy; ps_5_0 and ps_4_1; Pharaoh's twins by name, no water props or big river here),
// each a material water (g_sea_bed_light_scatter unread). Their absorption is exp2(-depth x opacity), no log2(e): Clarity
// scales the mul that reads the opacity, sp_water_opacity (shader_params + 76: cb2[4].w, cb3[4].w or cb4[4].w) or the
// river's COLOR2.w (v10.w). The caustics after the refraction's colour sample, off in the campaign (its world a twentieth of
// a battle's). WARP: tools\tw\troy\water (Pharaoh's water_v58 on Troy's bytes; neutral: the game's bits).
constexpr std::array<twattila::water_effects::WaterShader, 68> WATER_SHADERS = {{
    {.hash = 0x50583F38, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_high (traced), shadows
    {.hash = 0x50724F56, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_ultra (traced), shadows
    {.hash = 0x8B98D7DE, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_high (traced), shadows
    {.hash = 0xD71305F1, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_ultra (traced), shadows
    {.hash = 0xF016D3AB, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_high, baked shadows
    {.hash = 0x50E4A201, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_high, shadows
    {.hash = 0xB520BED0, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_high, ps_4_1
    {.hash = 0xB503EC5F, .world_input = 1, .refraction_slot = 1, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // battle_lake_high, no shadows
    {.hash = 0x45DFBB3B, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_low, baked shadows
    {.hash = 0x6EFFCB80, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_low, shadows
    {.hash = 0x869B898A, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_low, ps_4_1
    {.hash = 0x02BE0F85, .world_input = 1, .refraction_slot = 1, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // battle_lake_low, no shadows
    {.hash = 0x15EB14F9, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_medium, baked shadows
    {.hash = 0x943723E6, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_medium, shadows
    {.hash = 0x56256223, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_lake_medium, ps_4_1
    {.hash = 0x4E6F49B5, .world_input = 1, .refraction_slot = 1, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // battle_lake_medium, no shadows
    {.hash = 0x6DF2A99D, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_high, baked shadows
    {.hash = 0x54109424, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_high, ps_4_1
    {.hash = 0xA384045C, .world_input = 1, .refraction_slot = 2, .normal_slot = 3, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_high, no shadows
    {.hash = 0xFB2749CE, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_low, baked shadows
    {.hash = 0x8B40E4B0, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_low, shadows
    {.hash = 0x426038E1, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_low, ps_4_1
    {.hash = 0xA0CD3B27, .world_input = 1, .refraction_slot = 2, .normal_slot = 3, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_low, no shadows
    {.hash = 0x6F0C2423, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_medium, baked shadows
    {.hash = 0xEBC7559B, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_medium, shadows
    {.hash = 0xF8842109, .world_input = 1, .refraction_slot = 5, .normal_slot = 6, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_medium, ps_4_1
    {.hash = 0xDAE336C0, .world_input = 1, .refraction_slot = 2, .normal_slot = 3, .material_water = true, .opacity = {.type = 1, .index = {10, 0}, .component = 3}},  // battle_river_medium, no shadows
    {.hash = 0x4D51A9D7, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_high, baked shadows
    {.hash = 0xF9031B39, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_high, shadows
    {.hash = 0xB457E719, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_high, ps_4_1
    {.hash = 0xA80386D9, .world_input = 1, .refraction_slot = 1, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // battle_sea_high, no shadows
    {.hash = 0xBDCCF45B, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_low, baked shadows
    {.hash = 0x467EBEF2, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_low, shadows
    {.hash = 0x1499C98E, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_low, ps_4_1
    {.hash = 0x5FF73DC6, .world_input = 1, .refraction_slot = 1, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // battle_sea_low, no shadows
    {.hash = 0xEEE9CAC5, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_medium, baked shadows
    {.hash = 0x0DF1C0DF, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_medium, shadows
    {.hash = 0x0ACDB2A0, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_medium, ps_4_1
    {.hash = 0x37375FE5, .world_input = 1, .refraction_slot = 1, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // battle_sea_medium, no shadows
    {.hash = 0x980F5FB1, .world_input = 1, .refraction_slot = 3, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_ultra, baked shadows
    {.hash = 0xEC4A5FD9, .world_input = 1, .refraction_slot = 4, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // battle_sea_ultra, ps_4_1
    {.hash = 0x8A932872, .world_input = 1, .refraction_slot = 1, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // battle_sea_ultra, no shadows
    {.hash = 0x93D324C1, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_high, baked shadows
    {.hash = 0xDAF9DD3B, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_high, ps_4_1
    {.hash = 0xFBB014C5, .world_input = 1, .refraction_slot = 1, .caustics = false, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // campaign_lake_high, no shadows
    {.hash = 0x7151CEEC, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_low, baked shadows
    {.hash = 0xD8A2D8AF, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_low, shadows
    {.hash = 0x3A16F373, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_low, ps_4_1
    {.hash = 0xE2FA720F, .world_input = 1, .refraction_slot = 1, .caustics = false, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // campaign_lake_low, no shadows
    {.hash = 0x31C5503D, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 4, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_medium, baked shadows
    {.hash = 0x96AF6BBC, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_medium, shadows
    {.hash = 0x58465DAC, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_lake_medium, ps_4_1
    {.hash = 0x7E0D088A, .world_input = 1, .refraction_slot = 1, .caustics = false, .normal_slot = 2, .material_water = true, .opacity = {.type = 8, .index = {2, 4}, .component = 3}},  // campaign_lake_medium, no shadows
    {.hash = 0xCF12BB0F, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_high, baked shadows
    {.hash = 0x55CABB08, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_high, shadows
    {.hash = 0x659D3BC7, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_high, ps_4_1
    {.hash = 0x2E12E239, .world_input = 1, .refraction_slot = 1, .caustics = false, .normal_slot = 3, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_sea_high, no shadows
    {.hash = 0x2AD79ECA, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_low, baked shadows
    {.hash = 0x5D77B117, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_low, shadows
    {.hash = 0x2E660037, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_low, ps_4_1
    {.hash = 0x9DB8B110, .world_input = 1, .refraction_slot = 1, .caustics = false, .normal_slot = 3, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_sea_low, no shadows
    {.hash = 0x3F289065, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_medium, baked shadows
    {.hash = 0xDD32A3BC, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_medium, shadows
    {.hash = 0xB17A0632, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_medium, ps_4_1
    {.hash = 0xCA6428C5, .world_input = 1, .refraction_slot = 1, .caustics = false, .normal_slot = 3, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_sea_medium, no shadows
    {.hash = 0xDBCA18D4, .world_input = 1, .refraction_slot = 3, .caustics = false, .normal_slot = 5, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_ultra, baked shadows
    {.hash = 0x5F53DEE8, .world_input = 1, .refraction_slot = 4, .caustics = false, .normal_slot = 6, .material_water = true, .opacity = {.type = 8, .index = {4, 4}, .component = 3}},  // campaign_sea_ultra, ps_4_1
    {.hash = 0x2C1F9FA0, .world_input = 1, .refraction_slot = 1, .caustics = false, .normal_slot = 3, .material_water = true, .opacity = {.type = 8, .index = {3, 4}, .component = 3}},  // campaign_sea_ultra, no shadows
}};

// The game's passes that read render-size textures at the display's size (texture slots), below 100%.
constexpr std::array<twattila::view_share::RegionRead, 1> RENDER_SCALE_READS = {{
    {.shader_hash = 0x075C096D, .slots = 0b11},  // glow bright pass (after DLSS)
}};

constexpr std::array<twattila::light_bounce::SunDraw, 25> LIGHT_BOUNCE_DRAWS = {{
    {.shader_hash = 0x049C3CFB, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},
    {.shader_hash = 0x0989C3CE, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .sun_direction = 4, .ao_slot = 1},
    {.shader_hash = 0x2599D799, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},
    {.shader_hash = 0x76E178F0, .standard = true, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 3},
    {.shader_hash = 0xA137BA4D, .standard = true, .gbuffer_slot = 3, .camera_slot = 1, .sun_direction = 4},
    {.shader_hash = 0x3120FEA6, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 3},
    {.shader_hash = 0x43A9A4D2, .gbuffer_slot = 3, .camera_slot = 1, .sun_direction = 4, .ao_slot = 1},
    {.shader_hash = 0x66D6229E, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},
    {.shader_hash = 0xD38FA7E4, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},
    // The game's MSAA (*_ms; its G-buffer multisampled, the camera as the others').
    {.shader_hash = 0x0EFE48EB, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .sun_direction = 4, .ao_slot = 1},  // slm_edges
    {.shader_hash = 0x3E215AAC, .standard = true, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 3},  // slm_edges
    {.shader_hash = 0x464036D6, .standard = true, .gbuffer_slot = 4, .camera_slot = 1, .sun_direction = 4, .ao_slot = 1},  // slm_interior
    {.shader_hash = 0x5B17424A, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},  // slm_interior
    {.shader_hash = 0x6D3D2B69, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},  // slm_edges
    {.shader_hash = 0x707B069C, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},  // slm_interior
    {.shader_hash = 0xE616A792, .standard = true, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 3},  // slm_interior
    {.shader_hash = 0xEB98DED2, .standard = true, .gbuffer_slot = 7, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},  // slm_edges
    {.shader_hash = 0x089D1B5B, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},  // skinlm_edges
    {.shader_hash = 0x32FC67C6, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 3},  // skinlm_edges
    {.shader_hash = 0x5835F96C, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},  // skinlm_interior
    {.shader_hash = 0xA8D1CDD2, .gbuffer_slot = 5, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 3},  // skinlm_interior
    {.shader_hash = 0xB9910A51, .gbuffer_slot = 3, .camera_slot = 1, .sun_direction = 4, .ao_slot = 1},  // skinlm_edges
    {.shader_hash = 0xD07380B1, .gbuffer_slot = 3, .camera_slot = 1, .sun_direction = 4, .ao_slot = 1},  // skinlm_interior
    {.shader_hash = 0xE1707311, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},  // skinlm_edges
    {.shader_hash = 0xE885C018, .gbuffer_slot = 6, .camera_slot = 1, .lighting_slot = 3, .sun_direction = 4, .ao_slot = 4},  // skinlm_interior
}};

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
  // GT-VBAO's Temporal Smoothing (its noise moving each frame) and Denoiser (twattila/ao_filter.hpp).
  const bool bitmask_ao = shader_injection.ssao_mode >= 1.f;
  static uint32_t ao_frame = 0;
  shader_injection.ssao_frame =
      (bitmask_ao && shader_injection.ssao_temporal != 0.f) ? static_cast<float>(++ao_frame % 64) : 0.f;
  twattila::ao_filter::Update(swapchain, {.temporal = bitmask_ao && shader_injection.ssao_temporal != 0.f,
                                          .edge_fix = shader_injection.ssao_edge_fix != 0.f,
                                          .denoise = bitmask_ao && shader_injection.ssao_denoise != 0.f});
  twattila::rt_scene::Settings rt_settings = twattila::rt_scene::SettingsOf(rt_sliders, aa_sliders, shader_injection.light_bounce_strength);
  rt_settings.casters_out_of_view = rt_settings.sun_shadows && rays_units_out_of_view_row != 0.f;
  rt_settings.terrain_outside_view = rays_hills_out_of_view_row != 0.f;
  // Skip the Shadow Maps: Ray-Traced Shadows Only's skip of the atlas's casters (with the rays' sun shadows alone).
  rt_settings.shadows_only = rt_settings.sun_shadows && rays_replace_maps_row != 0.f && rays_skip_cascades_row != 0.f;
  // The game's reflections fall on the water alone (its apply 00645DA1, as Pharaoh's): twattila/water_reflections.hpp's gate.
  twattila::water_reflections::GateSettings(&rt_settings);
  twattila::texture_lod_bias::SetWrappingTrilinear(lod_bias_trilinear_row != 0.f);
  // Ray-traced shadows replace the screen-space ones too (Graphics 1 of 2's DXR: in place of Bend's).
  auto sss_settings = twattila::feature_settings::SssSettings(sss_sliders);
  sss_settings.enabled = sss_settings.enabled && !rt_settings.sun_shadows;
  twattila::screen_space_shadows::Update(swapchain, sss_settings);
  // Smoke Shadows without rays: off under the rays' sun shadows (their own Smoke Shadows); their particles streamed without the rays.
  auto smoke_map_settings = smoke_map_sliders;
  if (rt_settings.sun_shadows) {
    smoke_map_settings.strength = 0.f;
  }
  twattila::smoke_map::Update(smoke_map_settings);
  rt_settings.stream_particles = smoke_map_settings.strength > 0.f;
  twattila::rt_scene::Update(rt_settings);
  auto aa_settings = twattila::feature_settings::AaSettings(aa_sliders);
  aa_settings.sdr_output = shader_injection.swap_chain_encoding == 2.f;
  twattila::rt_scene::Configure(rt_settings, &aa_settings);
  twattila::anti_aliasing::Update(swapchain, aa_settings);
  twattila::light_count::OnPresent(rt_sliders.debug_log != 0.f);
  // After the at-start tests' thread (one writer at a time).
  if (!at_start_done.load()) return;
  // Troy's tweaks by its own initializers' code before Update's Find, whose `lea rdx, [name]` finds none here (tweak_objects.hpp).
  if (!graphics_found) {
    twtroy::tweak_objects::Find(tw::engine_tweaks::ModuleSections(GetModuleHandleW(nullptr)), graphics_options);
  }
  tw::engine_tweaks::Update(nullptr, graphics_options, nullptr, &graphics_found, graphics_status);
}

// The data run's clock (test_hooks: RENODX_TW_SET's seconds) from the first sun light draw: a battle's first 3D frame,
// whatever its load or menus took. Its line is what the run script waits for.
std::atomic_bool sun_drawn = false;
constexpr auto OnSunDrawClock = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  if (sun_drawn.load(std::memory_order_relaxed) || sun_drawn.exchange(true, std::memory_order_relaxed)) return {};
  renodx::utils::log::i("[Troy] First run: sun light draw (the tests' clock starts).");
  twattila::test_hooks::StartClock();
  return {};
};

// The frame trace's draw reads (FrameTraceReads=1): Pharaoh's layouts (twpharaohdynasties/trace_reads.hpp) at Troy's twins of
// its shaders, the same RDEF strides and slots (particle_vfx_main F5CE48CC: SORTDATA 8, PARTICLE_INSTANCE_UNPACKED 64,
// EMITTER_INSTANCE_UNPACKED 128, EMITTER_CONSTANTS_VS 112 at t2 / t6 / t1 / t4; the lit PS 563BA3E8 and 9C8987DD; the point
// light PS 143A02ED; the SSR apply 00645DA1 with the tone mapper's quad VS A0EA5C9E, the same bytes; the terrain's 981F929B and
// 347B13ED, its vb0 x (1, 1, 1.154760) as Pharaoh's asm).
constexpr twpharaohdynasties::trace_reads::Shaders TRACE_READS = {
    .particle_vertex = 0xF5CE48CC,
    .particle_pixel = {0x563BA3E8, 0x9C8987DD},
    .light_pixel = 0x143A02ED,
    .quad_vertex = 0xA0EA5C9E,
    .quad_pixel = 0x00645DA1,
    .terrain_rows = 0x981F929B,
    .terrain_scaled = 0x347B13ED,
};

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

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War Saga: Troy by ATN";  // TODO: change me!

namespace {

// Graphics 1 of 2's (twattila/graphics_page.hpp): what is true here. The rays' sun shadows multiply the game's shadow maps
// (the sun shaders' patched comparisons), ray-traced AO multiplies the game's SSAO, the paths take the sky from the sun
// shaders' ambient cube; point lights (143A02ED), smoke (F5CE48CC), the water's Clarity and Caustics and its ray-traced
// reflections (the game's reflections fall on the water alone, Pharaoh's texts) hooked, no fog.
constexpr twattila::graphics_page::Texts PAGE_TEXTS = {
    .aa_labels = twattila::graphics_page::ATTILA_TEXTS.aa_labels,
    .aa_tooltip = twattila::graphics_page::ATTILA_TEXTS.aa_tooltip,
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its ambient occlusion\n"
                     "and shadows. The rest at its default.",
    .no_rays_tooltip = "Bend Screen-Space Shadows (Length 480), GT-VBAO and GT-VBGI at their Recommended settings.\n"
                       "The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows and water reflections, path-traced ambient occlusion and Global Illumination at my\n"
                    "settings. The rest of this page at its default. Needs an upscaler (Anti-Aliasing above).\n"
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
    .specular_occlusion_label = "Ray-Traced Specular Occlusion (Experimental)",
    .water_caustics_label = "Water Caustics (Experimental)",
    .water_reflections_label = "Water Reflections (Experimental)",
    .water_reflections_tooltip = "Rivers and the sea reflect what the rays find, also off screen. Needs in-game Screen Space\n"
                                 "Reflections on. Off: the game's reflections.",
    .replace_maps_tooltip = "The sun's shadows from rays alone, so Softness shows in full. Water, effects and the shadows the game\n"
                            "baked for the map (past about 200 m) keep the game's. Use with Shadow Casters Out of View.\n"
                            "Off: the rays' shadows on top of the game's shadow maps.",
    .reflections_tooltip = "Reflections on water. Need in-game Screen Space Reflections on.\n"
                           "Game's SSR: the original. DXR: from rays, also what is off screen or hidden (turn Water Reflections\n"
                           "on below). Needs an upscaler.",
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
  // Graphics 2 of 2's tests: Attila's that this game has, then More Tests (more_tests.h).
  engine_tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), more_tests::ATTILA_TESTS);
  {
    auto more = more_tests::MoreTests();
    engine_tests.insert(engine_tests.end(), more.begin(), more.end());
  }
  graphics_options = {};
  for (auto& test : engine_tests) graphics_options.push_back(&test.option);
  graphics_status = tw::engine_tweaks::StatusLine();
  settings.push_back(graphics_status);

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
  // The water's reflection apply reads the ray-traced reflections (twattila/water_reflections.hpp): their rows, the water's too.
  twattila::rt_scene::AppendSettings(&settings, &rt_sliders, &aa_sliders, false, true);
  twattila::rt_scene::internal::water_pass = true;

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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarsagatroy/mods/131"); },
      },
  });

  // Section titles and Settings Mode's pages as Attila's (twattila/feature_settings.hpp).
  renodx::utils::settings::default_section_tint = twattila::feature_settings::BASE_TINT;
  std::vector<std::string> engine_sections = {twattila::feature_settings::GRAPHICS_SECTION};
  for (const auto& test : engine_tests) {
    if (std::ranges::find(engine_sections, test.section) == engine_sections.end()) engine_sections.push_back(test.section);
  }
  twattila::feature_settings::SetTints({}, engine_sections);

  // Graphics 1 of 2's layout (as Pharaoh's), ATN's RT/PT preset as the defaults of what the methods leave, then Settings
  // Mode's pages. Only what this game hooks: the paths' sky from the sun shaders' ambient cube (InstallSunDraws' 24),
  // foliage in the screen-space shadows (bit 128) and casters out of view (the shadow pass's; both Experimental, off: no
  // game run has shown them here), Rays Replace Shadow Maps, point lights, smoke, the water's Clarity and Caustics and its
  // ray-traced reflections (Experimental, off); no fog (its rows stay hidden).
  twattila::graphics_page::SetRaysPresetDefaults(&settings);
  // Fog Shadows, the torches' light in it (hidden: not hooked here) and Smoke Shadows (no game run has shown the particles'
  // records in this game) start off, Far Tree Shadows too (Experimental: the cards' capture is new here); RT/PT turns them on,
  // as Attila's (rays_preset_as_attila).
  for (auto* setting : settings) {
    if (setting->key == "RayTracedFogShadows" || setting->key == "RayTracedLightGlow" || setting->key == "RayTracedSmokeShadows"
        || setting->key == "RayTracedFarTreeShadows") {
      setting->default_value = 0.f;
    }
  }
  // Water Reflections (Experimental, off: no game run has shown the apply with the rays here; RT/PT turns it on, as ATN's list
  // does), Specular Occlusion and the reflections' Samples and Reuse where the water alone takes them, as Pharaoh's.
  twattila::water_reflections::SetRows(&settings, &rt_sliders);
  // Water Clarity at the game's 1 and Water Caustics off (Experimental: no game run has shown them here yet; RT/PT turns the
  // caustics on, as Attila's), Pharaoh's rows, under Reflections and Water.
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterClarity",
      .binding = &twattila::water_effects::water_clarity,
      .default_value = 1.f,
      .label = "Water Clarity (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "How far you see into water. 1: the game's; higher: clearer water.",
      .min = 0.25f,
      .max = 16.f,
      .format = "%.2f",
  });
  settings.push_back(new renodx::utils::settings::Setting{
      .key = "WaterCaustics",
      .binding = &twattila::water_effects::water_caustics,
      .default_value = 0.f,
      .label = "Water Caustics (Experimental)",
      .section = twattila::feature_settings::AA_SECTION,
      .tooltip = "Rippling light on the bed under battle water and rivers. 0: off.",
      .max = 2.f,
      .format = "%.2f",
  });
  // Torch and Fire Shadows from rays (Pharaoh's row): after the preset's defaults, so it starts off (Experimental: no game run
  // has shown them here yet); RT/PT turns it on, as ATN's list does.
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
                                             .smoke_strength = &smoke_map_sliders.strength,
                                             .smoke_softness = &smoke_map_sliders.softness,
                                             .lod_bias_trilinear = &lod_bias_trilinear_row,
                                             .rays_replace_maps = &rays_replace_maps_row,
                                             .rays_replace_maps_tested = false,
                                             .rays_units_out_of_view = &rays_units_out_of_view_row,
                                             .units_out_of_view_tested = false,
                                             .rays_skip_cascades = &rays_skip_cascades_row,
                                             .rays_hills_out_of_view = &rays_hills_out_of_view_row,
                                             .texts = &PAGE_TEXTS,
                                             .foliage_light_tested = false,
                                             .smoke_map_tested = false,
                                             .smoke_shadows_tested = false,
                                             .far_cards_tested = false,
                                             .water_caustics_tested = false,
                                             .rays_preset_as_attila = true,
                                             .lights_own_row = true,
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
  reshade::register_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  // The data run's switches (RENODX_TW_SET / SHOT / PROFILE; nothing hooked without them), timed from the first sun draw.
  // Epic starts the game through its launchers, so a run script's environment may never reach it: then they come from
  // ReShade.ini [renodx] TestSet / TestShot / TestProfile (read here, at start; the environment first).
  for (const auto& [key, variable] : {std::pair{"TestSet", "RENODX_TW_SET"}, std::pair{"TestShot", "RENODX_TW_SHOT"},
                                      std::pair{"TestProfile", "RENODX_TW_PROFILE"}}) {
    std::array<char, 8192> value = {};
    size_t size = value.size();
    if (GetEnvironmentVariableA(variable, nullptr, 0) == 0 && reshade::get_config_value(nullptr, "renodx", key, value.data(), &size)) {
      SetEnvironmentVariableA(variable, value.data());
      renodx::utils::log::i(std::string("[Troy] Test: ") + variable + " from ReShade.ini's " + key + ".");
    }
  }
  twattila::test_hooks::Install(&settings, false);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::smoke_map::OnDestroyDevice);
  // Water Clarity and Caustics: every row a material water (the patched copy swapped in at the draw), as Pharaoh's.
  twattila::water_effects::water_shaders = WATER_SHADERS;
  renodx::utils::pipeline::RegisterOnInitCallback(twattila::water_effects::OnInitMaterialWater);
  for (const auto& water : WATER_SHADERS) {
    renodx::utils::command_action::Register(
        twattila::water_effects::OnWaterDraw, {.shader_hash = water.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);

  // The at-start tests: written as the game sets its own values (twattila/engine_tests.hpp).
  SyncEarlyLoad();
  twattila::engine_tests::StartAtStart(nullptr, &engine_tests, &at_start_done, &at_start_stop);

  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);

  // Ray-Traced Water Reflections (twattila/water_reflections.hpp, Pharaoh's): the twins of Pharaoh's ssr_new chain, the same
  // order in ATN's traces (the water's G-buffer after the sun draw, extract_mask 1CE9217F, then these). Traced at backward_ssr
  // C1DC66B4 (its depth from t0: Troy's binds no mask before it; the light, R16G16B16A16F, at t2); where they left from the
  // water, downsample C2017771 and its 10 downsample_mip 9877558C and 10 lute_mip F49D4738 left out and our apply 00645DA1
  // drawn (shaders/ssrapply00; WARP tools\tw\troy\water\ssr_rays).
  twattila::water_reflections::Install(&custom_shaders,
                                       {.backward_ssr = 0xC1DC66B4, .inputs_slot = 0, .chain = {0xC2017771, 0x9877558C, 0xF49D4738}, .apply = 0x00645DA1,
                                        .apply_code = __0x00645DA1},
                                       &rt_sliders);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // The tests' clock first: the Light Bounce's callback draws the sun draws itself (none after it runs).
  for (const auto& draw : LIGHT_BOUNCE_DRAWS) {
    renodx::utils::command_action::Register(
        OnSunDrawClock, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  // The Debug View's Screen-Space AO: before every other sun draw hook (they replace the AO the draw reads). No AO pass hook
  // keeps it here (ssao01 binds no camera, below).
  twattila::rt_scene::InstallSunDebugAmbient(LIGHT_BOUNCE_DRAWS, &rt_sliders);
  // Before the Light Bounce's (its callback draws the sun draw again).
  // Ray tracing's sun draw hooks first: the screen-space shadows read its trace. The ambient cube starts at
  // lighting_VS_PS float 24 (c6-c11: +x, -x, +y, -y, +z, -z), the sun colour at float 12 (c3; x 0x389FB405 in rt_sun:
  // twtroy/rt_sun.cs_5_0.hlsl), the paths' sky and sun light: all 25 sun shaders' RDEF, WARP tools\tw\troy\sun_test.
  twattila::rt_scene::InstallSunDraws(LIGHT_BOUNCE_DRAWS, 24, 12);
  // Coloured Shadows (Bend's and the rays' filter at t50) on the sun's colour: lighting_VS_PS c3 (sun_colour; c1 is
  // sun_direction), in all 25 sun shaders' RDEF (b3, b2 without the shadow maps; tools\tw\troy\troy_sun_rdef.py).
  twattila::screen_space_shadows::sun_colour_register = 3;
  twattila::screen_space_shadows::Install(LIGHT_BOUNCE_DRAWS);
  // Light Through Foliage: the G-buffer's channel 3 alpha byte, bit 128, which the sun shaders' own code tests (049C3CFB:
  // and with 128). Of the 2026-10-27 battle's 21 G-buffer shaders, leaves (A6D82BAF), far tree cards (FCE1EAEC) and blocks
  // (6DA297BD) write 128, grass (EBC9553B) 160, the others 0-64 (units 1 / 33, the lake 64; terrain material 3C1C6172 its
  // vertex value; tools\tw\troy\troy_material_writes_1027.txt).
  twattila::screen_space_shadows::foliage_material_bit = 128;
  // The game's contact shadows off where ours shadow (Pharaoh's): g_contact_shadows at byte 4 of every sun draw's lighting_VS_PS
  // (troy_sun_rdef.py), its 8-step march under one if_nz cb[0].y (049C3CFB). After the screen-space shadows' (t49), before the
  // Light Bounce's. WARP: tools\tw\troy\sun_test v51.
  twattila::contact_shadows_off::Install(LIGHT_BOUNCE_DRAWS);
  // Rays Replace Shadow Maps: after the screen-space shadows' (t49) and before the Light Bounce's. Skip the Shadow Maps through
  // Ray-Traced Shadows Only (rt_settings.shadows_only, OnPresent).
  twattila::sun_replace_maps::Install(ATLAS_SUN_SHADERS, &rays_replace_maps_row);
  twattila::rt_scene::InstallShadowsOnly();
  // The frame trace (the data run): renodx_trace_now next to the exe traces the next frame, and FrameTrace's frame also at
  // 100% (twattila/frame_trace.hpp). Constants once each, b0-b5 up to 1 KB (the sun PS's environment b0, camera b1 with
  // g_screen_size c27 / g_viewport_dimensions c29 as the Options' defaults, shadowmap b2's splits, lighting_VS_PS b3's sun
  // colour c3 and ambient cube at float 24, directional_light b4; 049C3CFB's RDEF): the sun draws (slm 049C3CFB, skinlm
  // 66D6229E, slm without dynamic shadows 76E178F0, slm interior under the game's MSAA 5B17424A), the tone mapper 0C12E608 and
  // hdr_to_screen 9DF1DC36 (both drawn in the 2026-10-27 trace), the SSAO 2 generate D42C3523, the point light 143A02ED, the
  // lit particles 563BA3E8, the battle fog 018FED0C, the SSR apply 00645DA1, the sky dome 934FE258, the campaign's shroud
  // decals 516488AB and terrain A34C0DDA (tools/tw/troy/plan_1114.md; names.tsv of the dump). The SSR apply's and the lit
  // particles' blend; the VS inputs of the particles F5CE48CC, the point lights 6CFF8F83, the grass 2C7FBC7D and the far
  // trees' blocks 1FD86210 and cards E25688ED (the 10-27 trace's casters the rays lack); instanced counts apart.
  // The game maps many uvs to its viewport itself (uv x (c29.zw x c31.xy): the forward-lit shaders, the sun shaders' SSEO;
  // the scale census's 807 unclear, Pharaoh's class): view_share leaves those chains.
  twattila::view_share::SetViewportChains(true);
  // The campaign's shroud decals (campaignshrouddecals 516488AB) build their NDC from SV_Position x inv_rt_size (their
  // buffer's c13, as Pharaoh's): patched as the camera's target size below 100%.
  twattila::view_share::SetTargetSizes(std::array<const char*, 1>{"inv_rt_size"});
  twattila::anti_aliasing::Install(LIGHT_BOUNCE_DRAWS, {.shaders = MOTION_SHADERS,
                                                        .wind_pixel_shader = __motion_wind,
                                                        .unit_pixel_shader = __motion_unit,
                                                        .capture_pixel_shader = __motion_capture,
                                                        .rigid_compute_shader = __motion_rigid},
                                   RENDER_SCALE_READS, {}, {}, {},
                                   {
                                       .value_hashes = {0x049C3CFB, 0x66D6229E, 0x76E178F0, 0x5B17424A, 0x0C12E608, 0x9DF1DC36, 0xD42C3523,
                                                        0x143A02ED, 0x563BA3E8, 0x018FED0C, 0x00645DA1, 0x934FE258, 0x516488AB, 0xA34C0DDA},
                                       .value_slots = 6,
                                       .value_floats = 256,
                                       .on_demand = true,
                                       .at_100 = true,
                                       .blend_hashes = {0x00645DA1, 0x563BA3E8, 0x9C8987DD},
                                       .vertex_input_hashes = {0xF5CE48CC, 0x6CFF8F83, 0x2C7FBC7D, 0x1FD86210, 0xE25688ED},
                                       .instances_apart = true,
                                       .draw_reads = [](const twattila::frame_trace::Draw& draw) {
                                         return twpharaohdynasties::trace_reads::ReadOf(draw, TRACE_READS);
                                       },
                                   });
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  // Before the Light Bounce: it draws the sun draws itself (no callback after it runs).
  twattila::ao_filter::Install(LIGHT_BOUNCE_DRAWS);
  // The ambient occlusion pass: the Visibility Bitmask in its place. No ambient occlusion pass for ray tracing: ssao01
  // D42C3523 binds only its own constants (ssao2_PS at b0, the linear depth at t0), no camera, so the trace and the rays'
  // AO stay at the sun draws (their t_ssao at ao_slot), as Pharaoh's.
  twattila::ao_pass::Install(AO_DRAWS, &shader_injection, sizeof(shader_injection));
  // The TW games' view; the shadow pass's casters through world positions (its vertex shaders' view-projection at cb0[9-12]),
  // from the sun's per-frame atlas only (R32_TYPELESS 6144 x 6144, 2 x 2 cascades of 3072: ATN's 2026-11-14 traces; not the
  // kept baked map, R16 4096 x 4096), as Pharaoh's.
  twattila::rt_scene::Host rt_host = twattila::rt_scene::TW_HOST;
  rt_host.cascade_world = true;
  rt_host.cascade_target = [](const D3D11_TEXTURE2D_DESC& desc) {
    return desc.ArraySize == 1 && desc.Width == desc.Height && desc.Format == DXGI_FORMAT_R32_TYPELESS;
  };
  // The point light PS's diffuse scale (143A02ED's literal 0x389FB405, Pharaoh's).
  rt_host.local_light_scale = 7.615242e-05f;
  // The water's reflections (water_reflections' OnBackwardSsr): its guides again from the sun draw's G-buffer channels, the
  // water drawn into them since, and its R16G16B16A16F light copied, as Pharaoh's.
  rt_host.rewrite_guides = twattila::anti_aliasing::RewriteSunGuides;
  rt_host.light_copy = __rt_light_copy;
  twattila::rt_scene::Install(SCENE_SHADERS, {}, rt_host);
  twattila::rt_scene::InstallCutOutMasks(CUT_OUT_MASKS);
  // The point lights (ATN's session 3: two instanced draws a frame, VS 6CFF8F83 with PS 143A02ED after the sun draw; 64-byte
  // instances, the light's position at POSITION2.xyz): shaders/pointlight00 reads the rays' bits (Pharaoh's fire02 form).
  twattila::rt_scene::InstallPointLights(std::array<uint32_t, 1>{0x143A02ED});
  // The fires: their particle lights' draws, the lights streamed (PARTICLE_LIGHTS); shaders/particlelight00 reads the bits.
  twattila::rt_scene::InstallPointLights(std::array<uint32_t, 1>{0x643FC156}, twattila::rt_scene::internal::NO_LIGHT_CONSTANTS, &PARTICLE_LIGHTS);
  // Log Details' light count: the point lights and the night siege's fires (particle lights 643FC156, VS F5CE48CC: one draw,
  // 192k indices in ATN's session 2; Pharaoh's 5AD46115).
  twattila::light_count::Install(0x143A02ED, 0x643FC156);
  twattila::rt_scene::InstallSmokeShadows(SMOKE_VERTEX_SHADERS, SMOKE_PIXEL_SHADERS);
  twattila::light_bounce::Install(LIGHT_BOUNCE_DRAWS);
  twattila::screen_space_shadows::SetExtraOcclusion(&twattila::rt_scene::Occlusion);
  // Smoke Shadows without rays: the smoke's dimming into the sun draws' t49 (their x(1 - t49) patch), after the rays' occlusion.
  twattila::screen_space_shadows::SetSmokeOcclusion(&twattila::smoke_map::Occlusion);

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
  reshade::unregister_event<reshade::addon_event::destroy_device>(frame_copy::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  twattila::test_hooks::Uninstall();
  renodx::utils::command_action::Unregister(OnSunDrawClock);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_pass::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::screen_space_shadows::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::anti_aliasing::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::rt_scene::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::water_effects::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::smoke_map::OnDestroyDevice);
  twattila::water_reflections::Uninstall();
  renodx::utils::command_action::Unregister(twattila::water_effects::OnWaterDraw);
  renodx::utils::pipeline::UnregisterOnInitCallback(twattila::water_effects::OnInitMaterialWater);
  twattila::engine_tests::StopAtStart(&at_start_stop);
  tw::engine_tweaks::Uninstall(graphics_options);
  twattila::anti_aliasing::Uninstall();
  twattila::frame_generation::Uninstall();
  twattila::screen_space_shadows::Uninstall();
  twattila::contact_shadows_off::Uninstall();
  twattila::sun_replace_maps::Uninstall();
  twattila::light_bounce::Uninstall();
  twattila::light_count::Uninstall();
  twattila::rt_scene::Uninstall();
  twattila::screen_space_shadows::SetExtraOcclusion(nullptr);
  twattila::screen_space_shadows::SetSmokeOcclusion(nullptr);
  twattila::smoke_map::Uninstall();
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
