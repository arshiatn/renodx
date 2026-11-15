/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include "../../utils/settings.hpp"
#include "./engine_cvars.hpp"

// The Unreal add-ons' live graphics settings (Tales of Arise, Expeditions: Rome; engine_cvars.hpp writes them): each game's
// table of settings, each with its own choice saved by value in its own ReShade.ini section, and their rows on the add-on's
// page (Graphics 2 of 2). A preset only raises quality: higher-is-better settings take the larger of the game's value and the
// preset's, lower-is-better ones (LOD scales, thresholds, cell sizes) the smaller; KEEP: the game's value. What a game writes
// each frame and what its presets set stay in its add-on.
namespace talesofarise::graphics_table {

enum class Better : uint8_t { HIGHER,
                              LOWER,
                              EXACT };
constexpr float KEEP = std::numeric_limits<float>::quiet_NaN();

struct Setting {
  const char* group;
  const wchar_t* name;
  const char* label;
  Better better;
  // The game's presets above its own (High, Ultra, ...); then one extra choice offered only in the setting's own list.
  std::array<float, 5> values;
  const char* tooltip = nullptr;  // shown above the cvar name
  std::array<float, 3> more_extras = {KEEP, KEEP, KEEP};  // further choices offered only in the setting's own list
};

// A setting's state. `choice` 0: the game's value, else choices[choice - 1].
struct Entry {
  engine_cvars::Cvar cvar;
  std::string details;  // int / float and the game's value at the search, or why it is unusable
  std::string key;      // the cvar name
  std::vector<float> choices;
  int choice = 0;
  bool controlling = false;
  float game_value = 0.f;
  float written = 0.f;
};

// "game" for the game's own value (not a number: it reads back as unset).
inline void SaveChoice(const char* config_section, const Entry& entry) {
  const std::string value = (entry.choice == 0 ? std::string("game") : std::format("{:g}", entry.choices[entry.choice - 1]));
  reshade::set_config_value(nullptr, config_section, entry.key.c_str(), value.c_str());
}

// Each entry's key and choices (its distinct preset values and extras, with steps between them) and its saved choice: the
// saved value's nearest (steps move the indexes; "game" does not read as a number: the game's value).
inline void Prepare(const char* config_section, std::span<const Setting> table, std::span<Entry> entries) {
  for (size_t index = 0; index < table.size(); ++index) {
    const auto& setting = table[index];
    auto& entry = entries[index];
    entry.key = std::filesystem::path(setting.name).string();
    entry.choices.clear();
    for (const auto& values : {std::span<const float>(setting.values), std::span<const float>(setting.more_extras)}) {
      for (const float value : values) {
        if (!std::isnan(value) && std::ranges::find(entry.choices, value) == entry.choices.end()) entry.choices.push_back(value);
      }
    }
    std::ranges::sort(entry.choices);
    // The slider's steps between neighbouring choices: every whole number across small gaps (cascades 6 -> 10), the
    // halves across small gaps of halves (sun shadow distance 1.5 -> 3: 2, 2.5), the 1-2-5 numbers across gaps of more
    // than 2x (draw distance 3 -> 10: 5).
    std::vector<float> steps;
    for (size_t choice = 0; choice + 1 < entry.choices.size(); ++choice) {
      const float low = entry.choices[choice];
      const float high = entry.choices[choice + 1];
      if (low == std::round(low) && high == std::round(high) && high - low <= 4.f && low <= 10.f) {
        for (float value = low + 1.f; value < high; ++value) steps.push_back(value);
      } else if (low * 2.f == std::round(low * 2.f) && high * 2.f == std::round(high * 2.f) && high - low <= 2.f) {
        for (float value = low + .5f; value < high; value += .5f) steps.push_back(value);
      } else if (low > 0.f && high > low * 2.f) {
        for (float decade = std::pow(10.f, std::floor(std::log10(low))); decade < high; decade *= 10.f) {
          for (const float digit : {1.f, 2.f, 5.f}) {
            if (const float value = decade * digit; value > low * 1.001f && value < high * .999f) steps.push_back(value);
          }
        }
      }
    }
    entry.choices.insert(entry.choices.end(), steps.begin(), steps.end());
    std::ranges::sort(entry.choices);
    if (float saved = 0.f; !entry.choices.empty() && reshade::get_config_value(nullptr, config_section, entry.key.c_str(), saved)) {
      const auto nearest = std::ranges::min_element(entry.choices, {}, [saved](float choice) { return std::abs(choice - saved); });
      entry.choice = static_cast<int>(nearest - entry.choices.begin()) + 1;
    }
  }
}

// The setting's row on the add-on's page in `page_section`: a slider over the game's value and its choices, the cvar's
// name and value in its tooltip; greyed out where the game lacks it or `disabled` says so. It saves its choice itself.
inline renodx::utils::settings::Setting* Row(const Setting& setting, Entry* entry, const char* config_section, const std::string& page_section,
                                             std::function<bool()> disabled = {}) {
  const std::string id = std::format("{}##{}", setting.label, std::filesystem::path(setting.name).string());
  return new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
      .label = setting.label,
      .section = page_section,
      .tooltip = (setting.tooltip != nullptr ? setting.tooltip : ""),
      .on_draw = [&setting, entry, config_section, id, disabled = std::move(disabled)] {
        const bool usable = entry->cvar.values[0] != nullptr;
        const float game_value = (entry->controlling ? entry->game_value : (usable ? entry->cvar.Get() : 0.f));
        const std::string label = (entry->choice == 0 ? (usable ? std::format("Game's ({:g})", game_value) : std::string("Game's"))
                                                       : std::format("{:g}", entry->choices[entry->choice - 1]));
        ImGui::BeginDisabled(!usable || (disabled && disabled()));
        if (ImGui::SliderInt(id.c_str(), &entry->choice, 0, static_cast<int>(entry->choices.size()), label.c_str(),
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
          SaveChoice(config_section, *entry);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
          const std::string now = (usable ? (entry->controlling ? std::format(": now {:g}", entry->cvar.Get()) : std::string(": the game's"))
                                          : ": unavailable (" + entry->details + ")");
          ImGui::SetTooltip("%s%s%s%s", (setting.tooltip != nullptr ? setting.tooltip : ""), (setting.tooltip != nullptr ? "\n\n" : ""),
                            entry->key.c_str(), now.c_str());
        }
        return false;
      },
  };
}

}  // namespace talesofarise::graphics_table
