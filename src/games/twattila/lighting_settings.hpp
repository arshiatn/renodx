/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../../utils/settings.hpp"
#include "./light_bounce.hpp"

// The ambient occlusion (GT-VBAO) and Global Illumination (GT-VBGI, the Light Bounce) settings of every Total War
// addon: the same keys, presets and tooltips (each game's shader_injection names the bindings the same; its section
// tints and settings mode use the section names here). ATN's presets: ambient occlusion Faster / Recommended / Heavier,
// Global Illumination Faster (Recommended) / Heavier (More accurate); the defaults are the recommended ones (Global
// Illumination off).
namespace twattila::lighting_settings {

constexpr const char* AO_SECTION = "Ambient Occlusion (GT-VBAO, Experimental, needs in-game SSAO)";
constexpr const char* GI_SECTION = "Global Illumination (GT-VBGI, Experimental, no in-game SSAO needed)";
constexpr uint32_t AO_TINT = 0x7FBFFF;
constexpr uint32_t GI_TINT = 0xFFB86B;

// Global Illumination's Fast MSAA: the games with MSAA have its setting.
template <typename ShaderInjection>
constexpr bool HAS_MSAA = requires(const ShaderInjection& data) { data.light_bounce_msaa_pixel; };

// The Light Bounce's sliders from the settings, every present.
template <typename ShaderInjection>
light_bounce::Sliders LightBounceSliders(const ShaderInjection& data) {
  light_bounce::Sliders sliders = {
      .strength = data.light_bounce_strength,
      .radius = data.light_bounce_radius,
      .thickness = data.ssao_thickness,
      .quality = data.light_bounce_quality,
      .directions = data.light_bounce_directions,
      .resolution = data.light_bounce_resolution,
      .temporal = data.light_bounce_temporal,
      .multi_bounce = data.light_bounce_multi_bounce,
      .checkerboard = data.light_bounce_checkerboard,
      .backface = data.light_bounce_backface,
      .auto_thickness = data.light_bounce_auto_thickness,
      .denoise = data.light_bounce_denoise,
  };
  if constexpr (HAS_MSAA<ShaderInjection>) {
    sliders.msaa_pixel = data.light_bounce_msaa_pixel;
  }
  return sliders;
}

// Both sections onto `settings`, bound to `data` (the addon's shader_injection). The ambient occlusion's 3/4 and 1/4
// resolution: the games drawn by ao_pass.hpp's own targets (their shader_injection has ssao_scale); its Temporal
// Smoothing, Edge Fix and Denoiser: those and the games with ao_filter.hpp (their shader_injection has ssao_temporal).
// `traced`: whether rays or paths find the Light Bounce's light (rt_scene): its screen-space search's settings off then.
template <typename ShaderInjection>
void Append(std::vector<renodx::utils::settings::Setting*>* settings, ShaderInjection* data,
            const std::function<bool()>& traced = {}) {
  constexpr bool AO_SCALES = requires(const ShaderInjection& injection) { injection.ssao_scale; };
  constexpr bool AO_TEMPORAL = requires(const ShaderInjection& injection) { injection.ssao_temporal; };
  using renodx::utils::settings::Setting;
  using renodx::utils::settings::SettingValueType;
  using Values = std::vector<std::pair<std::string, float>>;
  // A preset button: its section's `defaults` with `changes` written to the settings.
  const auto preset = [](const Values& defaults, const Values& changes) {
    return [values = defaults, changes]() mutable {
      for (const auto& [key, value] : changes) {
        std::ranges::find(values, key, &std::pair<std::string, float>::first)->second = value;
      }
      renodx::utils::settings::UpdateSettings(values);
    };
  };
  const auto ao_enabled = [data] { return data->ssao_mode >= 1.f; };
  const auto gi_enabled = [data] { return data->light_bounce_strength > 0.f; };
  const auto search_enabled = [data, traced] { return data->light_bounce_strength > 0.f && !(traced && traced()); };
  // Recommended: 3/4 resolution where the pass has it (with Temporal Smoothing and the Denoiser), else full. Temporal
  // Edge Fix off (ATN: ghosting).
  Values ao_defaults = {
      {"ssao_mode", 1.f},
      {"ssao_strength", 1.f},
      {"ssao_radius", 12.5f},
      {"ssao_thickness", 0.1f},
      {"ssao_quality", 2.f},
      {"ssao_directions", 0.f},
      {"ssao_resolution", (AO_SCALES ? 1.f : 0.f)},
      {"ssao_auto_thickness", 1.f},
  };
  if constexpr (AO_TEMPORAL) {
    ao_defaults.insert(ao_defaults.end(), {{"ssao_temporal", 1.f}, {"ssao_edge_fix", 0.f}, {"ssao_denoise", 1.f}});
  }
  // Faster (Recommended) with Global Illumination off.
  Values gi_defaults = {
      {"light_bounce_strength", 0.f},
      {"light_bounce_radius", 20.f},
      {"light_bounce_resolution", 2.f},
      {"light_bounce_quality", 2.f},
      {"light_bounce_directions", 0.f},
      {"light_bounce_temporal", 1.f},
      {"light_bounce_denoise", 1.f},
      {"light_bounce_multi_bounce", 0.f},
      {"light_bounce_checkerboard", 1.f},
      {"light_bounce_backface", 0.f},
      {"light_bounce_auto_thickness", 1.f},
  };
  if constexpr (HAS_MSAA<ShaderInjection>) {
    gi_defaults.emplace_back("light_bounce_msaa_pixel", 1.f);
  }
  // Each setting's default from its section's list.
  const auto default_of = [&](const std::string& key) {
    for (const Values* values : {&ao_defaults, &gi_defaults}) {
      for (const auto& [listed, value] : *values) {
        if (listed == key) return value;
      }
    }
    return 0.f;
  };
  const auto toggle = [&](const std::string& key, float* binding, const std::string& label, const char* section,
                          const std::string& tooltip, const std::function<bool()>& is_enabled) {
    return new Setting{
        .key = key,
        .binding = binding,
        .value_type = SettingValueType::BOOLEAN,
        .default_value = default_of(key),
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .labels = {"Off", "On"},
        .is_enabled = is_enabled,
    };
  };
  const std::vector<std::string> sample_labels = {"Normal (8)", "High (16)", "Ultra (32)", "64", "128", "256"};

  settings->insert(settings->end(), {
      new Setting{
          .value_type = SettingValueType::TEXT,
          .label = "Darker shading behind trees and objects: raise Thickness. Lighter around spears: lower it.",
          .section = AO_SECTION,
          .tint = 0xFFD23C,
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Faster",
          .section = AO_SECTION,
          .group = "ao-presets",
          .tooltip = "Radius 12.5 m, Quality Ultra (32), half resolution.",
          .tint = AO_TINT,
          .on_change = preset(ao_defaults, {{"ssao_resolution", (AO_SCALES ? 2.f : 1.f)}}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Recommended",
          .section = AO_SECTION,
          .group = "ao-presets",
          .tooltip = (AO_SCALES ? "Radius 12.5 m, Quality Ultra (32), 3/4 resolution." : "Radius 12.5 m, Quality Ultra (32)."),
          .tint = AO_TINT,
          .on_change = preset(ao_defaults, {}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Heavier",
          .section = AO_SECTION,
          .group = "ao-presets",
          .tooltip = (AO_SCALES ? "Radius 25 m, Quality 64, 3/4 resolution." : "Radius 25 m, Quality 64."),
          .tint = AO_TINT,
          .on_change = preset(ao_defaults, {{"ssao_radius", 25.f}, {"ssao_quality", 3.f}}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Reset",
          .section = AO_SECTION,
          .group = "ao-presets",
          .tooltip = "The game's ambient occlusion, this section's sliders at their defaults.",
          .tint = AO_TINT,
          .on_change = preset(ao_defaults, {{"ssao_mode", 0.f}}),
      },
      new Setting{
          .key = "ssao_mode",
          .binding = &data->ssao_mode,
          .value_type = SettingValueType::INTEGER,
          .default_value = default_of("ssao_mode"),
          .label = "Ambient Occlusion",
          .section = AO_SECTION,
          .tooltip = "Soft shadows in corners and where objects meet the ground.\n"
                     "Game's: everything counts as solid, so soldiers and trees get dark halos.\n"
                     "GT-VBAO: objects have a thickness, so light gets behind thin ones.",
          .labels = {"Game's", "GT-VBAO"},
      },
      new Setting{
          .key = "ssao_strength",
          .binding = &data->ssao_strength,
          .default_value = default_of("ssao_strength"),
          .label = "Strength",
          .section = AO_SECTION,
          .tooltip = "How dark it gets. 1: physically correct; higher: darker.",
          .min = 0.f,
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = ao_enabled,
      },
      new Setting{
          .key = "ssao_radius",
          .binding = &data->ssao_radius,
          .default_value = default_of("ssao_radius"),
          .label = "Radius",
          .section = AO_SECTION,
          .tooltip = "How far it reaches. Larger: wider shading, heavier (raise Quality with it).",
          .min = 0.25f,
          .max = 30.f,
          .format = "%.2f m",
          .is_enabled = ao_enabled,
      },
      new Setting{
          .key = "ssao_thickness",
          .binding = &data->ssao_thickness,
          .default_value = default_of("ssao_thickness"),
          .label = "Thickness",
          .section = AO_SECTION,
          .tooltip = "How thick objects are taken to be (Global Illumination too).\n"
                     "Lower: light gets behind thin things; higher: darker behind them.",
          .min = 0.01f,
          .max = 3.f,
          .format = "%.2f m",
          .is_enabled = ao_enabled,
      },
      new Setting{
          .key = "ssao_quality",
          .binding = &data->ssao_quality,
          .value_type = SettingValueType::INTEGER,
          .default_value = default_of("ssao_quality"),
          .label = "Quality",
          .section = AO_SECTION,
          .tooltip = "Samples per direction. More: thin things still count at a large Radius. Heavier.",
          .labels = sample_labels,
          .is_enabled = ao_enabled,
      },
      new Setting{
          .key = "ssao_directions",
          .binding = &data->ssao_directions,
          .value_type = SettingValueType::INTEGER,
          .default_value = default_of("ssao_directions"),
          .label = "Directions",
          .section = AO_SECTION,
          .tooltip = "Directions searched around each pixel. More: less noise, heavier.",
          .labels = {"1", "2", "4", "8"},
          .is_enabled = ao_enabled,
      },
      new Setting{
          .key = "ssao_resolution",
          .binding = &data->ssao_resolution,
          .value_type = SettingValueType::INTEGER,
          .default_value = default_of("ssao_resolution"),
          .label = "Resolution",
          .section = AO_SECTION,
          .tooltip = "Lower: faster, softer; thin things (grass, spears) get less shading.",
          .labels = (AO_SCALES ? std::vector<std::string>{"Full", "3/4", "Half", "1/4"}
                                      : std::vector<std::string>{"Full", "Half"}),
          .is_enabled = ao_enabled,
      },
      toggle("ssao_auto_thickness", &data->ssao_auto_thickness, "Auto Thickness (Experimental)", AO_SECTION,
             "Guesses each object's thickness from its width, Thickness as the least:\n"
             "spears stay thin, walls and hills block fully.",
             ao_enabled),
  });
  if constexpr (AO_TEMPORAL) {
    settings->insert(settings->end(), {
        toggle("ssao_temporal", &data->ssao_temporal, "Temporal Smoothing", AO_SECTION,
               "Averages the last frames: much less noise. Off: the noise stands still.", ao_enabled),
        toggle("ssao_edge_fix", &data->ssao_edge_fix, "Temporal Edge Fix", AO_SECTION,
               "Keeps Temporal Smoothing's old shading off outlines (no thin halos).\n"
               "Outlines can shimmer a little more.",
               [data] { return data->ssao_mode >= 1.f && data->ssao_temporal != 0.f; }),
        toggle("ssao_denoise", &data->ssao_denoise, "Denoiser", AO_SECTION,
               "Smooths the noise along surfaces, never across edges. Cleaner, a bit softer.", ao_enabled),
    });
  }
  settings->insert(settings->end(), {
      new Setting{
          .value_type = SettingValueType::TEXT,
          .label = "",
          .section = AO_SECTION,
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Faster (Recommended)",
          .section = GI_SECTION,
          .group = "gi-presets",
          .tooltip = "Strength 1, Radius 20 m, quarter resolution, Quality Ultra (32).",
          .tint = GI_TINT,
          .on_change = preset(gi_defaults, {{"light_bounce_strength", 1.f}}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Heavier (More accurate)",
          .section = GI_SECTION,
          .group = "gi-presets",
          .tooltip = "Strength 1, Radius 20 m, half resolution, Quality Ultra (32).",
          .tint = GI_TINT,
          .on_change = preset(gi_defaults, {{"light_bounce_strength", 1.f}, {"light_bounce_resolution", 1.f}}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Reset",
          .section = GI_SECTION,
          .group = "gi-presets",
          .tooltip = "Off, this section's sliders at their defaults.",
          .tint = GI_TINT,
          .on_change = preset(gi_defaults, {}),
      },
      new Setting{
          .key = "light_bounce_strength",
          .binding = &data->light_bounce_strength,
          .default_value = default_of("light_bounce_strength"),
          .label = "Strength",
          .section = GI_SECTION,
          .tooltip = "Sunlit ground, walls and soldiers bounce their light and colour into nearby shadows.\n"
                     "1: physically correct; 0: off.",
          .min = 0.f,
          .max = 4.f,
          .format = "%.2f",
      },
      new Setting{
          .key = "light_bounce_radius",
          .binding = &data->light_bounce_radius,
          .default_value = default_of("light_bounce_radius"),
          .label = "Radius",
          .section = GI_SECTION,
          .tooltip = "How far bounced light reaches. Larger: heavier (raise Quality with it).",
          .min = 0.5f,
          .max = 20.f,
          .format = "%.2f m",
          .is_enabled = search_enabled,
      },
      new Setting{
          .key = "light_bounce_resolution",
          .binding = &data->light_bounce_resolution,
          .value_type = SettingValueType::INTEGER,
          .default_value = default_of("light_bounce_resolution"),
          .label = "Resolution",
          .section = GI_SECTION,
          .tooltip = "Lower: much faster, softer.",
          .labels = {"Full", "Half", "Quarter", "Eighth"},
          .is_enabled = gi_enabled,
      },
      new Setting{
          .key = "light_bounce_quality",
          .binding = &data->light_bounce_quality,
          .value_type = SettingValueType::INTEGER,
          .default_value = default_of("light_bounce_quality"),
          .label = "Quality",
          .section = GI_SECTION,
          .tooltip = "Samples per direction. More: small lit things still count at a large Radius. Heavier.",
          .labels = sample_labels,
          .is_enabled = search_enabled,
      },
      new Setting{
          .key = "light_bounce_directions",
          .binding = &data->light_bounce_directions,
          .value_type = SettingValueType::INTEGER,
          .default_value = default_of("light_bounce_directions"),
          .label = "Directions",
          .section = GI_SECTION,
          .tooltip = "Directions searched around each pixel. More: less noise, heavier.",
          .labels = {"1", "2", "4", "8", "16"},
          .is_enabled = search_enabled,
      },
      toggle("light_bounce_temporal", &data->light_bounce_temporal, "Temporal Smoothing", GI_SECTION,
             "Averages the last frames: much less noise. Off: the noise stands still.", gi_enabled),
      toggle("light_bounce_denoise", &data->light_bounce_denoise, "Denoiser", GI_SECTION,
             "Smooths the noise along surfaces, never across edges. Cleaner, a bit softer.", gi_enabled),
      new Setting{
          .key = "light_bounce_multi_bounce",
          .binding = &data->light_bounce_multi_bounce,
          .default_value = default_of("light_bounce_multi_bounce"),
          .label = "Multi-Bounce",
          .section = GI_SECTION,
          .tooltip = "Bounced light bounces again: fuller light in deep shadows and narrow streets. Almost free.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = gi_enabled,
      },
      toggle("light_bounce_checkerboard", &data->light_bounce_checkerboard, "Checkerboard", GI_SECTION,
             "Half the screen traced each frame, the rest kept from the last frames.\n"
             "About 2x faster; can lag a little behind fast motion.",
             [data, traced] {
               return data->light_bounce_strength > 0.f && data->light_bounce_temporal != 0.f && !(traced && traced());
             }),
      toggle("light_bounce_backface", &data->light_bounce_backface, "Back-Face Rejection (Experimental)", GI_SECTION,
             "Ignores light from surfaces seen from behind. Heavier; not always better.", search_enabled),
      toggle("light_bounce_auto_thickness", &data->light_bounce_auto_thickness, "Auto Thickness (Experimental)",
             GI_SECTION,
             "Guesses each object's thickness from its width, Thickness as the least:\n"
             "spears stay thin, walls and hills block fully.",
             search_enabled),
  });
  if constexpr (HAS_MSAA<ShaderInjection>) {
    settings->push_back(toggle("light_bounce_msaa_pixel", &data->light_bounce_msaa_pixel, "Fast MSAA (Once per Pixel)",
                               GI_SECTION,
                               "With in-game MSAA: added once per pixel, not per sample. Faster at 4x / 8x;\n"
                               "edges between two surfaces may get the other one's colour.",
                               gi_enabled));
  }
}

}  // namespace twattila::lighting_settings
