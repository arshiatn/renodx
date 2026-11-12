#pragma once

// Musa's PsychoV35 (psycho_test35.hlsli): its rows and its preset values, the same in every game that has it (ATN:
// PsychoV31's order, its own sliders apart from V31's, Hue Clip Emulation last).

#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "../../utils/settings.hpp"

namespace twattila::psycho35 {

// `injection`: the add-on's shader data (its v35_* fields and hue_clip); `shown`: Rendering Mode is PsychoV35;
// `hue_clip`: the game's Hue Clip Emulation default, 0 - 100 (test/tw_psychov35's hue report).
template <typename Injection>
std::vector<renodx::utils::settings::Setting*> Rows(Injection* injection, bool (*shown)(), float hue_clip) {
  using renodx::utils::settings::Setting;
  const auto slider = [shown](const std::string& key, float* binding, const std::string& label, const std::string& tooltip,
                              float max = 2.f, float default_value = 1.f) {
    return new Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = label,
        .section = "PsychoV35",
        .tooltip = tooltip,
        .max = max,
        .format = "%.2f",
        .is_enabled = shown,
        .is_visible = shown,
    };
  };
  return {
      slider("v35_exposure", &injection->v35_exposure, "Exposure", ""),
      slider("v35_highlights", &injection->v35_highlights, "Highlights", ""),
      slider("v35_shadows", &injection->v35_shadows, "Shadows", ""),
      slider("v35_contrast", &injection->v35_contrast, "Contrast", "1 = PsychoV35's own."),
      slider("v35_highlight_contrast", &injection->v35_highlight_contrast, "Highlight Contrast",
             "Contrast above mid gray. 1 = PsychoV35's own."),
      slider("v35_shadow_contrast", &injection->v35_shadow_contrast, "Shadow Contrast",
             "Contrast below mid gray. 1 = PsychoV35's own."),
      new Setting{
          .key = "v35_flare",
          .binding = &injection->v35_flare,
          .default_value = 0.f,
          .label = "Flare",
          .section = "PsychoV35",
          .tooltip = "Deepens the darkest shadows. 0 = off.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = shown,
          .parse = [](float value) { return 0.1f * std::pow(value, 10.f); },
          .is_visible = shown,
      },
      slider("v35_saturation", &injection->v35_saturation, "Saturation / Purity", "1 = PsychoV35's own."),
      slider("v35_dechroma", &injection->v35_dechroma, "Dechroma",
             "Very bright colors fade toward white, as if overexposed. 0 = off.", 1.f, 0.f),
      slider("v35_roll_off", &injection->v35_roll_off, "Highlight Roll-off",
             "How soon highlights start to compress toward Peak. 0 = latest, 1 = default, 2 = soonest."),
      slider("v35_gamut_compression", &injection->v35_gamut_compression, "Gamut Compression",
             "Pulls colors your display can't show back inside its color space.\n"
             "0 = off (the display clips them), 1 = full (default).",
             1.f, 1.f),
      new Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink, too orange or too pale? Change the settings below.",
          .section = "PsychoV35",
          .is_visible = shown,
      },
      slider("v35_highlight_saturation", &injection->v35_highlight_saturation, "Highlight Saturation",
             "Saturation of bright colors. 0 = less, 1 = PsychoV35's own, 2 = more."),
      new Setting{
          .key = "hue_clip",
          .binding = &injection->hue_clip,
          .default_value = hue_clip,
          .label = "Hue Clip Emulation",
          .section = "PsychoV35",
          .tooltip = std::format("Bright colors take the hue SDR clips them to: fire more orange / yellow, sky more cyan.\n"
                                 "0 = off. Default {:.0f}: the closest to the SDR picture.",
                                 hue_clip),
          .max = 100.f,
          .is_enabled = shown,
          .parse = [](float value) { return value * 0.01f; },
          .is_visible = shown,
      },
  };
}

// Before the first row of `section` (the add-on's PsychoV31 rows: V35's come first, as in Attila's page).
inline void InsertBefore(std::vector<renodx::utils::settings::Setting*>* settings, const std::string& section,
                         const std::vector<renodx::utils::settings::Setting*>& rows) {
  auto at = settings->begin();
  while (at != settings->end() && (*at)->section != section) ++at;
  settings->insert(at, rows.begin(), rows.end());
}

// Every row at its default (the presets).
inline std::vector<std::pair<std::string, float>> Defaults(float hue_clip) {
  return {
      {"v35_exposure", 1.f}, {"v35_highlights", 1.f}, {"v35_shadows", 1.f}, {"v35_contrast", 1.f},
      {"v35_highlight_contrast", 1.f}, {"v35_shadow_contrast", 1.f}, {"v35_flare", 0.f}, {"v35_saturation", 1.f},
      {"v35_dechroma", 0.f}, {"v35_roll_off", 1.f}, {"v35_gamut_compression", 1.f}, {"v35_highlight_saturation", 1.f},
      {"hue_clip", hue_clip},
  };
}

}  // namespace twattila::psycho35
