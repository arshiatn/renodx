/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
#define DEBUG_LEVEL_0
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
#include "../../utils/settings.hpp"
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
      {"ext_hue_correction", 1.f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"saturation", 1.f},
      {"highlight_saturation", 1.f},
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
}

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

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
  };
  // Not replaced; their draws skipped while the hotkeys hide them. Scaleform's text (0x96675B77) and flat shapes
  // (0x1B59F534, in OnDrawUi / OnDrawIndexedUi: the execution skull is one of them): the HUD, also the menus. The
  // execution highlights on enemies: highlight_shaders.
  const auto keep_shader = [](reshade::api::command_list*) { return false; };
  const auto draw_hud = [](reshade::api::command_list*) { return !hud_hidden; };
  custom_shaders[0x96675B77] = {.crc32 = 0x96675B77, .on_replace = keep_shader, .on_draw = draw_hud};

  // 0-2 slider, 1 = the game's.
  auto slider = [](const std::string& key, float* binding, const std::string& label, const std::string& section,
                   const std::string& tooltip, bool (*is_enabled)(), bool (*is_visible)(), float default_value = 1.f) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
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
                     "HDR (Extended): the game's native Filmic (Uncharted 2) tonemapper, extended into HDR\n"
                     "(UC2 Extended). As close as possible to the original SDR look (default).\n"
                     "HDR (PsychoV31): replaces the Filmic tonemapper with PsychoV31, keeping the game's\n"
                     "grading (exposure, bloom, color balance, sun shafts, color charts).",
          .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV31)"},
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (All Post Processing On)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV31), with all vanilla post processing, \n"
                     "hidden chromatic aberration, bloom at 0.5 and Video Highlights at 0.25 enabled.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings({
                {"tonemapper", 2.f},
                {"film_grain", 0.25f},
                {"motion_blur_camera", 0.20f},
                {"motion_blur_object", 0.50f},
                {"chromatic_aberration_strength", 0.35f},
                {"bloom", 0.5f},
                // {"sun_shafts", 0.5f},
                {"video_highlights", 0.25f},
            });
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (Less Post Processing)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV31), no film grain, no motion blur,\n"
                     "no chromatic aberration. Bloom at 0.5, video highlights at 0.25.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings({
                {"tonemapper", 2.f},
                {"film_grain", 0.f},
                {"motion_blur_camera", 0.f},
                {"motion_blur_object", 0.f},
                {"bloom", 0.5f},
                // {"sun_shafts", 0.5f},
                {"video_highlights", 0.25f},
            });
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
          .key = "ext_hue_correction",
          .binding = &shader_injection.ext_hue_correction,
          .default_value = 1.f,
          .label = "Hue Correction",
          .section = "Extended",
          .tooltip = "Keeps the scene's hue where the game's curve shifts it (mostly mid tones and dark colors).\n"
                     "1 = full (default, like Musa's Batman), 0 = the curve's hue. Above Paper White colors still\n"
                     "turn white like SDR (bright fire: yellow / white core).",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsExtendedMode,
          .is_visible = IsExtendedMode,
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
          .label = "Video Highlights",
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
                     "Only with Video Highlights above 0.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return IsHDRMode() && shader_injection.video_highlights > 0.f; },
          .is_visible = IsAdvancedSettings,
      },
  };
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

// True at the Present where `key` went down.
bool KeyWentDown(int key) {
  const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
  const bool went_down = down && !keys_down[key];
  keys_down[key] = down;
  return went_down;
}

void OnPresentHotkeys(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
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
  uint64_t immediate_context = 0u;
  std::vector<uint64_t> old_buffers;  // earlier back buffers
  reshade::api::resource current = {0u};
  reshade::api::resource target = {0u};  // RenoDX's clone of the current back buffer, else itself
  reshade::api::resource_view rtv = {0u};
  bool bound_back_buffer = false;  // the game's last binding on the immediate context
  std::vector<uint64_t> logged;
  int lines = 0;
};

Data data;
std::mutex mutex;
std::atomic_bool check_after_present = false;

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

// Caller holds the mutex.
bool IsBackBuffer(uint64_t resource) {
  return resource != 0u
         && (resource == data.current.handle
             || std::find(data.old_buffers.begin(), data.old_buffers.end(), resource) != data.old_buffers.end());
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
  ID3D11DeviceContext* immediate = nullptr;
  reinterpret_cast<ID3D11Device*>(data.device->get_native())->GetImmediateContext(&immediate);
  if (immediate != nullptr) {
    data.immediate_context = reinterpret_cast<uint64_t>(immediate);
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
  const std::scoped_lock lock(mutex);
  std::erase(data.old_buffers, resource.handle);
}

void OnBindRenderTargets(
    reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view* rtvs,
    reshade::api::resource_view dsv) {
  auto* device = cmd_list->get_device();
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const uint64_t bound = (count != 0 && rtvs[0].handle != 0u) ? device->get_resource_from_view(rtvs[0]).handle : 0u;

  const std::scoped_lock lock(mutex);
  const bool back_buffer = IsBackBuffer(bound);
  if (cmd_list->get_native() == data.immediate_context) data.bound_back_buffer = back_buffer;
  if (!back_buffer) return;

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
  {
    const std::scoped_lock lock(mutex);
    if (cmd_list->get_native() != data.immediate_context) return;
  }
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
    if (bound == 0u && data.bound_back_buffer) {
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
  const std::scoped_lock lock(mutex);
  if (data.device != device) return;
  DestroyView();
  data = {};
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
  reshade::register_event<reshade::addon_event::present>(OnPresentHotkeys);
  reshade::register_event<reshade::addon_event::init_pipeline>(OnInitPipelineHighlights);
  reshade::register_event<reshade::addon_event::draw>(OnDrawUi);
  reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexedUi);

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
