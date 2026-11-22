/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "../../utils/settings.hpp"
#include "./credits.hpp"

// Settings Mode's pages, their section names and colours, and the page's small rows (notes, More Options): every Total
// War addon's. Apart from feature_settings.hpp, lighting_settings.hpp and graphics_page.hpp (which include them), so an
// addon without Attila's anti-aliasing or Light Bounce shaders (Shogun 2, Ryse) has them too.

// The ambient occlusion's and Global Illumination's sections (lighting_settings.hpp's rows).
namespace twattila::lighting_settings {

constexpr const char* AO_SECTION = "Ambient Occlusion (GT-VBAO, Experimental, needs in-game SSAO)";
constexpr const char* GI_SECTION = "Global Illumination (GT-VBGI, Experimental, no in-game SSAO needed)";
constexpr uint32_t AO_TINT = 0x7FBFFF;
constexpr uint32_t GI_TINT = 0xFFB86B;

}  // namespace twattila::lighting_settings

namespace twattila::feature_settings {

inline const std::string SSS_SECTION = "Screen-Space Shadows (Bend Studio, Experimental, needs in-game shadows)";
inline const std::string AA_SECTION = "Anti-Aliasing (Experimental, in-game AA off)";
inline const std::string GRAPHICS_SECTION = "Graphics (Experimental)";
constexpr uint32_t SSS_TINT = 0xC08CFF;
constexpr uint32_t AA_TINT = 0x7FFFD4;
constexpr uint32_t GRAPHICS_TINT = 0xFF7FAF;
constexpr uint32_t BASE_TINT = 0x7EE08A;

// The section colours (ATN): `base_sections` (tone mapping, grading, effects, output, About) one green, the AO, GI,
// SSS and AA their own, `engine_sections` (Graphics 2 of 2) pink.
inline void SetTints(std::initializer_list<std::string> base_sections, const std::vector<std::string>& engine_sections) {
  renodx::utils::settings::section_tints = {
      {lighting_settings::AO_SECTION, lighting_settings::AO_TINT},
      {lighting_settings::GI_SECTION, lighting_settings::GI_TINT},
      {SSS_SECTION, SSS_TINT},
      {AA_SECTION, AA_TINT},
  };
  for (const std::string& section : base_sections) renodx::utils::settings::section_tints.emplace_back(section, BASE_TINT);
  for (const std::string& section : engine_sections) {
    renodx::utils::settings::section_tints.emplace_back(section, GRAPHICS_TINT);
  }
}

// Settings Mode's pages (ATN): HDR & Effects (0): tone mapping, grading, effects, output; Graphics 1 of 2: the ambient
// occlusion, Global Illumination, screen-space shadows and anti-aliasing; Graphics 2 of 2 (`engine_sections`): the engine
// graphics and their tests. About and the mode rows (Settings Mode, Ryse's Simple / Advanced) on every page. `mode`: the
// Settings Mode slider's value. A setting without its own visibility shows on its page.
inline void AssignPages(std::vector<renodx::utils::settings::Setting*>* settings, const float* mode,
                        const std::vector<std::string>& engine_sections, const std::vector<std::string>& graphics_sections = {}) {
  for (auto* setting : *settings) {
    if (setting->key == "SettingsMode" || setting->key == "SettingsAdvanced" || setting->section == "About") continue;
    int page = 0;
    if (std::ranges::find(engine_sections, setting->section) != engine_sections.end()) {
      page = 2;
    } else if (setting->section == lighting_settings::AO_SECTION || setting->section == lighting_settings::GI_SECTION
               || setting->section == SSS_SECTION || setting->section == AA_SECTION
               || std::ranges::find(graphics_sections, setting->section) != graphics_sections.end()) {
      page = 1;
    }
    setting->is_visible = [page, mode, shown = setting->is_visible] {
      return (static_cast<int>(*mode) == page) && (shown == nullptr || shown());
    };
  }
}

}  // namespace twattila::feature_settings

namespace twattila::graphics_page {

inline const std::string AA_SECTION = "Anti-Aliasing and Upscaling";
inline const std::string AO_SECTION = "Ambient Occlusion";
inline const std::string SHADOWS_SECTION = "Shadows";
inline const std::string GI_SECTION = "Global Illumination";
inline const std::string REFLECTIONS_SECTION = "Reflections and Water";
inline const std::string RT_SECTION = "Ray Tracing Extra Options";
// Light Through Foliage's and the smoke's shadows without rays' defaults where a game run has shown them (Attila's).
constexpr float FOLIAGE_LIGHT_DEFAULT = 0.5f;
constexpr float SMOKE_MAP_DEFAULT = 1.f;
constexpr uint32_t TITLE_TINT = 0x7FBFFF;
constexpr uint32_t MORE_TINT = 0xFFB86B;
constexpr uint32_t WARNING_TINT = 0xFF5050;
constexpr uint32_t NOTICE_TINT = 0xFFE066;
using credits::FULL_DOWNLOAD_NOTE;

inline renodx::utils::settings::Setting* Note(const std::string& label, const std::string& section, uint32_t tint, std::function<bool()> shown) {
  return new renodx::utils::settings::Setting{.value_type = renodx::utils::settings::SettingValueType::TEXT,
                                              .label = label,
                                              .section = section,
                                              .tint = tint,
                                              .is_visible = std::move(shown)};
}

inline renodx::utils::settings::Setting* EmptyLine(const std::string& section) {
  return new renodx::utils::settings::Setting{.value_type = renodx::utils::settings::SettingValueType::TEXT, .label = "", .section = section};
}

// A section's More Options: closed at each start, its colour the same on every section. It reads back into `open`, which
// its rows' visibility reads as they are drawn after it.
inline renodx::utils::settings::Setting* MoreOptions(const std::string& section, bool* open, std::function<bool()> shown) {
  return new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
      .label = "More Options##" + section,
      .section = section,
      .on_draw = [open] {
        ImGui::PushStyleColor(ImGuiCol_Text, renodx::utils::settings::ImVec4FromHex(MORE_TINT));
        *open = ImGui::TreeNodeEx("More Options", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
        ImGui::PopStyleColor();
        return false;
      },
      .is_visible = std::move(shown),
  };
}

}  // namespace twattila::graphics_page
