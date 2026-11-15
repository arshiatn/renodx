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

// The Unreal HDR add-ons' (Tales of Arise, Expeditions: Rome) rows for the HDR-only snapshot that upstream ships alone:
// the red full-download notice while the full download's files are absent, and the About line's download buttons.
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
