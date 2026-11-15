/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"

// The Unreal add-ons' (Tales of Arise, Expeditions: Rome) shared rows: the red full-download notice while the full
// download's files are absent (the HDR-only snapshot upstream ships), GT-VBAO's rows and the About line's buttons.
namespace talesofarise::hdr_only {

constexpr uint32_t WARNING_TINT = 0xFF5050;

// Shown while `missing` says the full download's files are absent (read as the page draws).
inline renodx::utils::settings::Setting* FullDownloadNote(std::function<bool()> missing) {
  return new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "DLSS, Frame Generation, ray tracing and the extra graphics settings need the full download from GitHub: "
               "github.com/arshiatn/renodx/releases",
      .section = "General",
      .tint = WARNING_TINT,
      .is_visible = std::move(missing),
  };
}

// GT-VBAO in the game's ambient occlusion (ue_bitmask_ao.hlsli, bitmask_ao.hlsli): its rows in the Ambient Occlusion
// section (twattila/graphics_page.hpp takes them by key), bound where each add-on keeps them.
struct AmbientOcclusionBindings {
  float* mode;  // 0 the game's, 1 GT-VBAO
  float* radius;
  float* strength;
  float* thickness;
  float* quality;
  float* directions;
};

inline std::vector<renodx::utils::settings::Setting*> AmbientOcclusionRows(const AmbientOcclusionBindings& b) {
  using renodx::utils::settings::Setting;
  using renodx::utils::settings::SettingValueType;
  const auto gtvbao = [mode = b.mode] { return *mode != 0.f; };
  return {
      new Setting{
          .key = "ssao_mode",
          .binding = b.mode,
          .value_type = SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ambient Occlusion",
          .section = "Ambient Occlusion",
          .tooltip = "GT-VBAO: contact shading that lets light pass behind thin things (grass, poles,\n"
                     "characters) instead of dark halos. Needs in-game ambient occlusion on.",
          .labels = {"Game's", "GT-VBAO"},
      },
      new Setting{
          .key = "ssao_radius",
          .binding = b.radius,
          .default_value = 1.5f,
          .label = "Radius",
          .section = "Ambient Occlusion",
          .tooltip = "How far around a point other surfaces shade it (metres).",
          .min = 0.25f,
          .max = 5.f,
          .format = "%.2f m",
          .is_enabled = gtvbao,
      },
      new Setting{
          .key = "ssao_strength",
          .binding = b.strength,
          .default_value = 1.f,
          .label = "Strength",
          .section = "Ambient Occlusion",
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = gtvbao,
      },
      new Setting{
          .key = "ssao_thickness",
          .binding = b.thickness,
          .default_value = 0.1f,
          .label = "Thickness",
          .section = "Ambient Occlusion",
          .tooltip = "How thick things are taken to be: thicker shades more behind them.",
          .min = 0.01f,
          .max = 1.f,
          .format = "%.2f m",
          .is_enabled = gtvbao,
      },
      new Setting{
          .key = "ssao_quality",
          .binding = b.quality,
          .value_type = SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Quality",
          .section = "Ambient Occlusion",
          .tooltip = "Samples per side: more catch thin things. Heavier.",
          .labels = {"8", "16", "32", "64"},
          .is_enabled = gtvbao,
      },
      new Setting{
          .key = "ssao_directions",
          .binding = b.directions,
          .value_type = SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Directions",
          .section = "Ambient Occlusion",
          .tooltip = "Directions per pixel: less noise. Heavier.",
          .labels = {"1", "2", "4", "8"},
          .is_enabled = gtvbao,
      },
  };
}

// Ko-fi, the game's NexusMods page (none: no button) and GitHub (Downloads), on one line of the About section.
inline std::vector<renodx::utils::settings::Setting*> AboutButtons(const std::string& nexus_url) {
  std::vector<renodx::utils::settings::Setting*> buttons = {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "ATN's Ko-fi",
          .section = "About",
          .group = "line_1",
          .tint = 0xFF5A16,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://ko-fi.com/atnthegreat"); },
      },
  };
  if (!nexus_url.empty()) {
    buttons.push_back(new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "NexusMods (For RenoDX + DLSS)",
        .section = "About",
        .group = "line_1",
        .tint = 0xFF2196F3,
        .on_change = [nexus_url]() { renodx::utils::platform::LaunchURL(nexus_url.c_str()); },
    });
  }
  buttons.push_back(new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::BUTTON,
      .label = "GitHub (Downloads)",
      .section = "About",
      .group = "line_1",
      .tint = 0x6E7681,
      .on_change = []() { renodx::utils::platform::LaunchURL("https://github.com/arshiatn/renodx/releases"); },
  });
  return buttons;
}

}  // namespace talesofarise::hdr_only
