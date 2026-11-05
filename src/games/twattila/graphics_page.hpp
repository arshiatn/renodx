/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../../utils/settings.hpp"
#include "./feature_settings.hpp"
#include "./lighting_settings.hpp"
#include "./rt_scene.hpp"

// Graphics 1 of 2 (ATN; Attila's, and with Bindings another game's): one section per job (anti-aliasing, shadows, ambient occlusion, Global Illumination,
// reflections, ray tracing), each with a method slider and only that method's settings, the less important ones under
// More Options. Every setting keeps its key (saved settings and presets stay as they are): this file only moves them,
// hides them and renames them. The method sliders hold nothing themselves: they read and write the keys that decide
// the method, so a loaded preset shows through at once.
namespace twattila::graphics_page {

using renodx::utils::settings::Setting;
using renodx::utils::settings::SettingValueType;
using Values = std::vector<std::pair<std::string, float>>;

inline const std::string AA_SECTION = "Anti-Aliasing and Upscaling";
inline const std::string AO_SECTION = "Ambient Occlusion";
inline const std::string SHADOWS_SECTION = "Shadows";
inline const std::string GI_SECTION = "Global Illumination";
inline const std::string REFLECTIONS_SECTION = "Reflections and Water";
inline const std::string RT_SECTION = "Ray Tracing Extra Options";
constexpr uint32_t TITLE_TINT = 0x7FBFFF;
constexpr uint32_t MORE_TINT = 0xFFB86B;
constexpr uint32_t WARNING_TINT = 0xFF5050;
constexpr const char* FULL_DOWNLOAD_NOTE = "DLSS, FSR, XeSS, Frame Generation and ray tracing need the full download from GitHub: github.com/arshiatn/renodx/releases";

// What the page says that is true of one game only.
struct Texts {
  // The anti-aliasing methods' names by value, all of them: a saved value is clamped to these at each start.
  std::array<const char*, 8> aa_labels;
  const char* aa_tooltip;
  const char* reset_tooltip;
  const char* no_rays_tooltip;
  const char* rays_tooltip;
  const char* shadow_tooltip;
  const char* ao_tooltip;
  const char* ao_paths_label = "Path-Traced (ReSTIR GI)";
  const char* specular_occlusion_label = "Ray-Traced Specular Occlusion";
  const char* water_caustics_label = "Water Caustics";
  const char* water_reflections_label = "Water Reflections";
  const char* water_reflections_tooltip = "";  // Empty: the row's own.
  const char* fog_shadows_label = "Fog Shadows";
  const char* fog_shadows_tooltip = "";  // Empty: the row's own.
  const char* light_glow_label = "";     // Empty: the row's own.
  const char* light_glow_tooltip = "";   // Empty: the row's own.
  const char* gi_tooltip_end = "";
  const char* coloured_shadows_tooltip = "";      // Empty: the row's own.
  const char* rays_a_pixel_shadows_tooltip = "";  // Empty: the row's own.
  const char* rays_a_pixel_paths_tooltip = "";    // Empty: the row's own.
  const char* object_motion_tooltip =
      "Soldiers, props and cloth tell the upscaler how they move: sharper while they move.\n"
      "Off: faster with many soldiers on screen (they can smear a little when moving).";
  const char* effects_at_output_tooltip =
      "While the upscaler renders below 100%, ambient occlusion, Light Bounce, reflections and shadow softness\n"
      "keep their 100% detail. Looks better, can cost some performance.";
  const char* dlss_preset_tooltip = "NVIDIA's models: K the first transformer, M the second, L the sharpest (default).";
  const char* reflections_tooltip =
      "Reflections on water, armour and metal. Need in-game Screen Space Reflections on.\n"
      "Game's SSR: the original. FidelityFX SSSR: AMD's hierarchical ray march, exact, no gaps, reaching\n"
      "across the screen, usually faster. DXR: from rays, also what is off screen or hidden. Needs an upscaler.";
  const char* reflections_middle_label = "FidelityFX SSSR";
  const char* reflections_rays_label = "Ray-Traced (DXR)";
};

inline constexpr Texts ATTILA_TEXTS = {
    .aa_labels = {"Off", "SMAA", "CMAA2", "SMAA T2x", "TAA", "DLSS", "FSR", "XeSS"},
    .aa_tooltip = "Smooths jagged edges. Turn the in-game anti-aliasing off.\n"
                  "SMAA: catches more edges. CMAA2: sharper, keeps more fine detail.\n"
                  "SMAA T2x: SMAA with two frames blended. TAA: blends frames, best on grass, softer.\n"
                  "DLSS: NVIDIA's (RTX), the best. FSR: AMD's, FSR 4 on Radeon RX 6000 and newer, else 3.1. XeSS: Intel's, any GPU.\n"
                  "Every ray-traced effect needs an upscaler. Needs renodx-tw-dlaa64.exe and its DLLs in the game folder.",
    .reset_tooltip = "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its SSAO, SSR and water.\n"
                     "The rest at its default.",
    .no_rays_tooltip = "Bend Screen-Space Shadows (Length 480), FidelityFX SSSR, GT-VBAO and GT-VBGI at their Recommended\n"
                       "settings. The rest of this page at its default.",
    .rays_tooltip = "Ray-traced shadows and reflections, path-traced ambient occlusion and Global Illumination at my\n"
                    "settings. The rest of this page at its default. Needs an upscaler (Anti-Aliasing above).\n"
                    "On NVIDIA also turns DLSS Ray Reconstruction on. Elsewhere Experimental, slower: its own denoisers.",
    .shadow_tooltip = "Game's Shadow Maps: the original. Bend Screen-Space Shadows: those plus fine contact shadows from\n"
                      "what is on screen (Bend Studio's; soldiers, weapons, grass; needs in-game shadows on).\n"
                      "DXR: DirectX Raytracing, every shadow from rays, the sun's and the torches' and fires', in place of\n"
                      "both. Needs an upscaler.",
    .ao_tooltip = "Soft shadows in corners and where objects meet the ground. Keep in-game SSAO on for all of these.\n"
                  "Game's SSAO: everything counts as solid, so soldiers and trees get dark halos.\n"
                  "GT-VBAO (Ground Truth AO with a visibility bitmask): objects have a thickness, so light gets behind\n"
                  "thin ones.\n"
                  "RTAO: from rays through the scene, in place of both. Needs an upscaler.\n"
                  "ReSTIR GI: Global Illumination's paths bring the sky's light in place of the game's ambient;\n"
                  "turns it on below. Needs an upscaler.",
};

inline constexpr std::array<int, 8> ALL_AA_METHODS = {0, 1, 2, 3, 4, 5, 6, 7};

// What the page reads. The defaults are Attila's: a game without something says so here.
struct Bindings {
  feature_settings::AaSliders* aa = nullptr;
  feature_settings::SssSliders* sss = nullptr;
  rt_scene::Sliders* rt = nullptr;
  float* ssao_mode = nullptr;
  float* light_bounce_strength = nullptr;
  float* ssr_hierarchical = nullptr;  // FidelityFX SSSR's; null: the game's march and the rays only
  // The smoke's shadows without rays (smoke_map.hpp); null: no such rows.
  float* smoke_strength = nullptr;
  float* smoke_softness = nullptr;
  // The game owns its scale (has_scale_rows false) and the addon can write it (`upscaling_mode_found`): Game's, DLAA, the upscalers'
  // modes, Custom; null: no such rows.
  float* upscaling_mode = nullptr;
  bool (*upscaling_mode_found)() = nullptr;
  float* below_100_detail = nullptr;  // Models as detailed as at 100% below it; null: no such row
  float* placed_objects = nullptr;    // Placed meshes in a clip-space game (rt_core.hpp Host::placed_row); null: no such row
  float* rays_replace_maps = nullptr;  // The sun's shadow from rays alone where they'd multiply its mask; null: no such row
  float* rays_ao_spot_filter = nullptr;  // RTAO's lone dark pixels raised to their neighbours; null: no such row
  float* rays_hills_out_of_view = nullptr;  // The terrain out of the view in the scene too; null: no such row
  float* water_motion = nullptr;      // The fill's camera motion at the water's own depth; null: no such row
  float* scale_fix_gi_shadow = nullptr;  // Warhammer III: its GI grid and shadow blur below 100%
  const Texts* texts = &ATTILA_TEXTS;
  std::span<const int> aa_methods = ALL_AA_METHODS;  // ascending
  // The status line under the anti-aliasing with any upscaler; null: the helper's, FSR's and XeSS's only.
  std::string (*upscaler_status)() = nullptr;
  // The upscaling mode, render scale, LOD bias, effects' resolution, object motion; false: the game owns its scale.
  bool has_scale_rows = true;
  bool has_object_motion = false;  // Without the scale rows: Object Motion Vectors still has an effect.
  bool has_foliage_light = true;  // Screen-space shadows know grass and leaves.
  bool has_path_ambient = true;   // The paths can take the sky's light from the game's ambient (rt_scene::InstallSunDraws).
  bool rays_preset_path_ambient = true;  // The RT/PT preset takes it; false: ray-traced AO, over the game's.
  bool rays_preset_caustics = true;      // The RT/PT preset turns Water Caustics on.
  bool rays_preset_water_reflections = true;  // The RT/PT preset turns Water Reflections on.
  // Point light shadows have their own row (RT/PT turns it on); Fog Shadows, light in the fog and RIS stay off in that preset.
  bool lights_own_row = false;
  bool has_reflections = true;
  bool rays_replace_ao = true;  // Ray-traced AO in place of the game's; false: it multiplies the game's.
};

enum AoMethod : int { AO_GAME, AO_GTVBAO, AO_RAYS, AO_PATHS };
enum ShadowMethod : int { SHADOW_GAME, SHADOW_SCREEN, SHADOW_RAYS };
enum GiMethod : int { GI_OFF, GI_GTVBGI, GI_RAYS, GI_PATHS };
enum ReflectionMethod : int { REFLECTION_GAME, REFLECTION_HIERARCHICAL, REFLECTION_RAYS };

inline int AoMethodOf(const Bindings& b) {
  if (b.has_path_ambient && b.rt->path_ambient != 0.f && b.rt->light_bounce == 2.f && *b.light_bounce_strength > 0.f) return AO_PATHS;
  if (b.rt->ambient_occlusion != 0.f) return AO_RAYS;
  return (*b.ssao_mode >= 1.f) ? AO_GTVBAO : AO_GAME;
}

inline int ShadowMethodOf(const Bindings& b) {
  if (b.rt->sun_shadows != 0.f) return SHADOW_RAYS;
  return (b.sss->enabled != 0.f) ? SHADOW_SCREEN : SHADOW_GAME;
}

inline int GiMethodOf(const Bindings& b) {
  if (*b.light_bounce_strength <= 0.f) return GI_OFF;
  return GI_GTVBGI + static_cast<int>(b.rt->light_bounce);
}

inline int ReflectionMethodOf(const Bindings& b) {
  if (b.rt->reflections != 0.f) return REFLECTION_RAYS;
  return (b.ssr_hierarchical != nullptr && *b.ssr_hierarchical != 0.f) ? REFLECTION_HIERARCHICAL : REFLECTION_GAME;
}

// The choices write what the method needs. Path-traced AO is Global Illumination's paths bringing the sky's light: it
// turns them on, and leaving them falls back to ray-traced AO (the game's ambient would come back with no AO on it).
inline void ChooseAo(const Bindings& b, int method) {
  Values values = {{"RayTracedPathAmbient", (method == AO_PATHS) ? 1.f : 0.f}};
  // Multiplying, ray-traced AO goes over the game's, not over GT-VBAO too.
  if (method == AO_GAME || method == AO_GTVBAO || (method >= AO_RAYS && !b.rays_replace_ao)) values.emplace_back("ssao_mode", (method == AO_GTVBAO) ? 1.f : 0.f);
  values.emplace_back("RayTracedAmbientOcclusion", (method == AO_RAYS) ? 1.f : 0.f);
  if (method == AO_PATHS) {
    values.emplace_back("RayTracedLightBounce", 2.f);
    values.emplace_back("light_bounce_strength", std::max(*b.light_bounce_strength, 1.f));
  }
  renodx::utils::settings::UpdateSettings(values);
}

inline void ChooseShadows(int method) {
  // Screen-space shadows stay set under ray-traced ones: they come back without DLSS.
  Values values = {{"RayTracedSunShadows", (method == SHADOW_RAYS) ? 1.f : 0.f}};
  if (method != SHADOW_RAYS) values.emplace_back("ScreenSpaceShadows", (method == SHADOW_SCREEN) ? 1.f : 0.f);
  renodx::utils::settings::UpdateSettings(values);
}

inline void ChooseGi(const Bindings& b, int method) {
  const bool leaves_paths = AoMethodOf(b) == AO_PATHS && method != GI_PATHS;
  Values values = {
      {"light_bounce_strength", (method == GI_OFF) ? 0.f : std::max(*b.light_bounce_strength, 1.f)},
      {"RayTracedLightBounce", (method >= GI_RAYS) ? static_cast<float>(method - GI_GTVBGI) : 0.f},
  };
  if (leaves_paths) {
    values.emplace_back("RayTracedPathAmbient", 0.f);
    values.emplace_back("RayTracedAmbientOcclusion", 1.f);
  }
  renodx::utils::settings::UpdateSettings(values);
}

inline void ChooseReflections(int method) {
  Values values = {{"RayTracedReflections", (method == REFLECTION_RAYS) ? 1.f : 0.f}};
  if (method != REFLECTION_RAYS) values.emplace_back("ssr_hierarchical", (method == REFLECTION_HIERARCHICAL) ? 1.f : 0.f);
  renodx::utils::settings::UpdateSettings(values);
}

// A section's method slider ("Method" on every section: its section in its ImGui id, or they are one slider to ImGui).
inline Setting* MethodSlider(const std::string& section, const std::string& tooltip, std::vector<std::string> labels, std::function<int()> current,
                             std::function<void(int)> choose) {
  const std::string label = "Method##" + section;
  return new Setting{
      .value_type = SettingValueType::CUSTOM,
      .label = label,
      .section = section,
      .tooltip = tooltip,
      .on_draw = [label, labels = std::move(labels), current = std::move(current), choose = std::move(choose)] {
        int value = current();
        const int last = static_cast<int>(labels.size()) - 1;
        const bool changed = ImGui::SliderInt(label.c_str(), &value, 0, last, labels[std::clamp(value, 0, last)].c_str(),
                                              ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
        if (changed) choose(value);
        return changed;
      },
  };
}

inline Setting* Note(const std::string& label, const std::string& section, uint32_t tint, std::function<bool()> shown) {
  return new Setting{.value_type = SettingValueType::TEXT, .label = label, .section = section, .tint = tint, .is_visible = std::move(shown)};
}

inline Setting* EmptyLine(const std::string& section) {
  return new Setting{.value_type = SettingValueType::TEXT, .label = "", .section = section};
}

// A section's More Options: closed at each start, its colour the same on every section. It reads back into `open`, which
// its rows' visibility reads as they are drawn after it.
inline Setting* MoreOptions(const std::string& section, bool* open, std::function<bool()> shown) {
  return new Setting{
      .value_type = SettingValueType::CUSTOM,
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

// A row of the page: the settings with this key, or this button group, or this label; `shown` when the method needs it.
struct Row {
  std::string match;
  std::string section;
  std::function<bool()> shown;
  std::string label = {};
  std::string tooltip = {};
  std::function<bool()> enabled = {};
  bool only_some_games = false;  // No such setting is no mistake.
};

// Moves settings out of `settings`, into the page's order. Returns the place they took.
class Page {
 public:
  explicit Page(std::vector<Setting*>* settings) : settings_(settings) {}

  void Add(Setting* setting) { page_.push_back(setting); }

  void Take(const Row& row) {
    bool found = false;
    for (auto entry = settings_->begin(); entry != settings_->end();) {
      Setting* setting = *entry;
      if (setting->key != row.match && setting->group != row.match && !(setting->key.empty() && setting->label == row.match)) {
        ++entry;
        continue;
      }
      found = true;
      place_ = std::min(place_, static_cast<size_t>(entry - settings_->begin()));
      entry = settings_->erase(entry);
      setting->section = row.section;
      if (!row.label.empty()) setting->label = row.label;
      if (!row.tooltip.empty()) setting->tooltip = row.tooltip;
      // This page's Anti-Aliasing calls it DLSS, with the other upscalers beside it.
      if (const size_t at = setting->tooltip.find("Needs DLAA"); at != std::string::npos) setting->tooltip.replace(at, 10, "Needs an upscaler");
      if (row.enabled) setting->is_enabled = row.enabled;
      setting->is_visible = [shown = row.shown, own = setting->is_visible] { return shown() && (own == nullptr || own()); };
      page_.push_back(setting);
    }
    if (!found && !row.only_some_games) renodx::utils::log::w("[Total War] Graphics page: no setting ", row.match.c_str());
  }

  // A setting the method sliders hold: it stays among the saved ones, not drawn.
  void Keep(const std::string& key) {
    kept_.insert(key);
    for (Setting* setting : *settings_) {
      if (setting->key == key) setting->is_visible = [] { return false; };
    }
  }

  // The page in at the place its first setting had; what stayed in `old_sections` follows under the last section (none is
  // lost if a game adds a setting), empty lines dropped.
  void Finish(const std::vector<std::string>& old_sections, const std::string& last_section) {
    std::vector<Setting*> left;
    std::erase_if(*settings_, [&](Setting* setting) {
      if (kept_.contains(setting->key) || std::ranges::find(old_sections, setting->section) == old_sections.end()) return false;
      if (setting->value_type != SettingValueType::TEXT || !setting->label.empty()) {
        setting->section = last_section;
        left.push_back(setting);
      }
      return true;
    });
    page_.insert(page_.end(), left.begin(), left.end());
    settings_->insert(settings_->begin() + static_cast<ptrdiff_t>(std::min(place_, settings_->size())), page_.begin(), page_.end());
  }

  // Every setting of the finished page outside `section`, the method sliders' too, at its default.
  [[nodiscard]] Values Defaults(const std::string& section) const {
    Values values;
    for (const Setting* setting : *settings_) {
      const bool on_page = std::ranges::find(page_, setting) != page_.end() && setting->section != section;
      if (!setting->key.empty() && (on_page || kept_.contains(setting->key))) values.emplace_back(setting->key, setting->default_value);
    }
    return values;
  }

 private:
  std::vector<Setting*>* settings_;
  std::vector<Setting*> page_;
  std::set<std::string> kept_;
  size_t place_ = SIZE_MAX;
};

// The page's sections, `Graphics 1 of 2`'s for AssignPages.
inline std::vector<std::string> Sections() {
  return {AA_SECTION, SHADOWS_SECTION, AO_SECTION, GI_SECTION, REFLECTIONS_SECTION, RT_SECTION};
}

inline void Apply(std::vector<Setting*>* settings, const Bindings& b) {
  static bool more_aa = false;
  static bool more_ao = false;
  static bool more_shadows = false;
  static bool more_gi = false;
  static bool more_rt = false;
  static Values page_defaults;

  const auto always = [] { return true; };
  const auto upscaling = [b] { return feature_settings::Upscaling(*b.aa); };
  const auto scale_rows = [b] { return b.has_scale_rows && feature_settings::Upscaling(*b.aa); };
  const auto below_100_rows = [b] { return (b.has_scale_rows || b.upscaling_mode != nullptr) && feature_settings::Upscaling(*b.aa); };
  const auto no_upscaler = [b] { return !feature_settings::Upscaling(*b.aa); };
  const auto dlss = [b] { return b.aa->method == 5.f; };
  const auto nvidia = [] { return dlaa_bridge::OnNvidia(); };
  const auto frame_generation = [b] { return feature_settings::Upscaling(*b.aa) && b.aa->frame_generation != 0.f; };
  const auto dlss_generation = [frame_generation] { return frame_generation() && dlaa_bridge::FrameGenerationBackend() == dlaa_bridge::FG_BACKEND_DLSS; };
  const auto composing_generation = [frame_generation] { return frame_generation() && dlaa_bridge::FrameGenerationBackend() != dlaa_bridge::FG_BACKEND_FSR; };
  const auto ao_is = [b](int method) { return [b, method] { return AoMethodOf(b) == method; }; };
  const auto shadows_are = [b](int method) { return [b, method] { return ShadowMethodOf(b) == method; }; };
  const auto gi_is = [b](int method) { return [b, method] { return GiMethodOf(b) == method; }; };
  const auto gi_traced = [b] { return GiMethodOf(b) >= GI_RAYS; };
  const auto reflections_are = [b](int method) { return [b, method] { return ReflectionMethodOf(b) == method; }; };
  const auto more = [](const bool* open, std::function<bool()> shown) {
    return [open, shown = std::move(shown)] { return *open && shown(); };
  };

  Page page(settings);
  for (const auto& key : {"ssao_mode", "RayTracedAmbientOcclusion", "RayTracedPathAmbient", "ScreenSpaceShadows", "RayTracedSunShadows",
                          "RayTracedLightBounce", "ssr_hierarchical", "RayTracedReflections"}) {
    page.Keep(key);
  }

  // Anti-aliasing, then the upscalers' parts in the order of the frame: upscaling, denoising, generated frames, latency.
  {
    const std::string& section = AA_SECTION;
    for (Setting* setting : *settings) {
      // Hidden (the row below draws it), but the saved value is clamped to these labels at each start.
      if (setting->key == "AntiAliasing") setting->labels = std::vector<std::string>(b.texts->aa_labels.begin(), b.texts->aa_labels.end());
      if (setting->key == "AntiAliasingDlssMode") setting->labels[0] = "Native (100%)";
      if (setting->key == "AntiAliasingDebugView" && !b.has_scale_rows) setting->labels = {"Off", "Motion Vectors"};
    }
    page.Add(Note(FULL_DOWNLOAD_NOTE, section, WARNING_TINT, [] { return !dlaa_bridge::HelperInstalled(); }));
    page.Take({.match = "AntiAliasing", .section = section, .shown = [] { return false; }});
    // The list without DLSS off NVIDIA's GPUs: the slider's places past it are one lower than the values.
    page.Add(new Setting{
        .value_type = SettingValueType::CUSTOM,
        .label = "Anti-Aliasing",
        .section = section,
        .tooltip = b.texts->aa_tooltip,
        .on_draw = [b] {
          std::vector<int> listed;
          std::ranges::copy_if(b.aa_methods, std::back_inserter(listed), [](int method) { return method != 5 || dlaa_bridge::OnNvidia(); });
          const int last = static_cast<int>(listed.size()) - 1;
          const int method = std::clamp(static_cast<int>(b.aa->method), 0, static_cast<int>(b.texts->aa_labels.size()) - 1);
          int place = std::min(static_cast<int>(std::ranges::lower_bound(listed, method) - listed.begin()), last);
          const bool changed = ImGui::SliderInt("Anti-Aliasing", &place, 0, last, b.texts->aa_labels[listed[place]], ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
          if (changed) renodx::utils::settings::UpdateSettings({{"AntiAliasing", static_cast<float>(listed[place])}});
          return changed;
        },
    });
    // What the helper says it really runs.
    page.Add(new Setting{.value_type = SettingValueType::CUSTOM,
                         .section = section,
                         .on_draw = [b] {
                           if (b.upscaler_status != nullptr) {
                             ImGui::TextWrapped("%s", b.upscaler_status().c_str());
                           } else {
                             ImGui::TextWrapped("%s", (dlaa_bridge::bridge.shared != nullptr ? dlaa_bridge::bridge.shared->status : "Upscaling off"));
                           }
                           return false;
                         },
                         .is_visible = [b] { return (b.upscaler_status != nullptr) ? feature_settings::Upscaling(*b.aa) : b.aa->method > 5.f; }});
    if (b.upscaling_mode != nullptr) {
      const auto mode_shown = [b, upscaling] { return upscaling() && b.upscaling_mode_found(); };
      page.Add(new Setting{
          .key = "UpscalingMode",
          .binding = b.upscaling_mode,
          .value_type = SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Upscaling Mode",
          .section = section,
          .tooltip = "The size the game draws at; the upscaler brings it up (faster). Works at once.\n"
                     "Game's: the in-game Resolution Scale. The others take over from that slider.",
          .labels = {"Game's", "DLAA (100%)", "Ultra Quality (77%)", "Quality (66.7%)", "Balanced (58%)", "Performance (50%)",
                     "Ultra Performance (33.3%)", "Custom"},
          .is_visible = mode_shown,
      });
      // Its key's value changes only when the slider is let go: every new size makes the upscaler start again.
      page.Take({.match = "AntiAliasingRenderScale", .section = section, .shown = [] { return false; }});
      page.Add(new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Custom Render Scale",
          .section = section,
          .tooltip = "The size the game is drawn at, applied when you let go.",
          .on_draw = [b] {
            static int percent = 100;
            static bool dragging = false;
            if (!dragging) percent = static_cast<int>(b.aa->render_scale);
            ImGui::SliderInt("Custom Render Scale", &percent, feature_settings::MIN_CUSTOM_SCALE, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
            dragging = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) renodx::utils::settings::UpdateSettings({{"AntiAliasingRenderScale", static_cast<float>(percent)}});
            return false;
          },
          .is_visible = [b, mode_shown] { return mode_shown() && *b.upscaling_mode == 7.f; },
      });
    }
    if (b.below_100_detail != nullptr) {
      page.Add(new Setting{
          .key = "AntiAliasingBelow100Detail",
          .binding = b.below_100_detail,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Full Detail Below 100%",
          .section = section,
          .tooltip = "Models and draw distances as at 100% in-game Resolution Scale, from the next load. Off: as the\n"
                     "smaller picture asks for, fewer triangles, faster.",
          .is_visible = upscaling,
      });
    }
    page.Take({.match = "AntiAliasingQuality", .section = section,
               .shown = [b] { return b.aa->method == 1.f || b.aa->method == 2.f || b.aa->method == 3.f; }});
    page.Take({.match = "AntiAliasingTaaStrength", .section = section, .shown = [b] { return b.aa->method == 4.f; }});
    page.Add(new Setting{
        .key = "AntiAliasingTaaMotion",
        .binding = &b.aa->taa_motion,
        .value_type = SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = "TAA Motion Vectors",
        .section = section,
        .tooltip = "Camera: only the camera's motion. What moves on its own stays softer.\n"
                   "Objects: soldiers, props, cloth and swaying plants too, as DLSS gets them.\n"
                   "Sharper while they move, heavier on the CPU with many soldiers.",
        .labels = {"Camera", "Objects"},
        .is_visible = [b] { return b.aa->method == 4.f; },
    });
    page.Take({.match = "AntiAliasingSharpening", .section = section, .shown = [b] { return b.aa->method == 3.f || b.aa->method == 4.f; },
               .label = "Sharpening"});
    page.Take({.match = "AntiAliasingDlssMode", .section = section, .shown = scale_rows, .label = "Upscaling Mode",
               .tooltip = "Native: the game at your resolution. The others draw it smaller and the upscaler brings it up (faster)."});
    if (b.upscaling_mode == nullptr) {
      page.Take({.match = "AntiAliasingRenderScale", .section = section, .shown = scale_rows,
                 .tooltip = "The size the game is drawn at (100%: Native). Below 33% the upscalers' own modes end."});
    }
    page.Take({.match = "AntiAliasingDlaaPreset", .section = section, .shown = dlss, .label = "DLSS Preset",
               .tooltip = b.texts->dlss_preset_tooltip});
    page.Take({.match = "AntiAliasingRayReconstruction", .section = section, .shown = dlss, .label = "DLSS Ray Reconstruction",
               .tooltip = "NVIDIA's denoiser for ray tracing in place of DLSS's usual model: much less noise in everything\n"
                          "ray-traced. Runs only while something is ray-traced. Needs nvngx_dlssd.dll next to nvngx_dlss.dll."});
    page.Take({.match = "FrameGeneration", .section = section, .shown = upscaling, .label = "Frame Generation (Experimental)",
               .tooltip = "Frames shown per rendered frame. Turn the game's VSync off. Needs a windowed or borderless game.\n"
                          "DLSS: 2x on RTX 40 or newer, 3x to 6x and Dynamic on RTX 50. FSR: 2x. XeSS: 2x, up to 4x on Intel Arc."});
    page.Add(new Setting{
        .key = "FrameGenerationMethod",
        .binding = &b.aa->frame_backend,
        .value_type = SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Frame Generation Method (Experimental)",
        .section = section,
        .tooltip = "Auto: DLSS on an RTX 40 or newer, XeSS on Intel, else FSR.\n"
                   "FSR and XeSS run on any GPU and with any upscaler, in the game's HDR10, scRGB or SDR.\n"
                   "DLSS needs NVIDIA. Needs their files next to renodx-tw-dlaa64.exe.",
        .labels = {"Auto", "DLSS", "FSR", "XeSS"},
        .is_visible = frame_generation,
    });
    // What the helper says it really does (refreshed every 30 presents): frames generated, or why none.
    page.Add(new Setting{.value_type = SettingValueType::CUSTOM,
                         .section = section,
                         .on_draw = [] {
                           ImGui::TextWrapped("%s", dlaa_bridge::FrameGenerationStatus().c_str());
                           return false;
                         },
                         .is_visible = frame_generation});
    page.Take({.match = "Reflex", .section = section, .shown = nvidia});
    page.Take({.match = "FrameGenerationFrameLimit", .section = section,
               .shown = [nvidia] { return nvidia() || dlaa_bridge::FrameGenerationBackend() == dlaa_bridge::FG_BACKEND_XESS; },
               .tooltip = "Caps the rendered frame rate with NVIDIA Reflex or Xe Low Latency (frame generation multiplies it). A\n"
                          "cap the game always reaches keeps the frame rate steady: less VRR flicker on OLED displays.\n"
                          "Needs Reflex or frame generation on. 0: off."});
    const auto cmaa = [b] { return b.aa->method == 2.f; };
    page.Add(MoreOptions(section, &more_aa, [b] { return feature_settings::Upscaling(*b.aa) || b.aa->method == 2.f; }));
    page.Take({.match = "AntiAliasingExtraSharpness", .section = section, .shown = more(&more_aa, cmaa), .label = "Extra Sharpness"});
    page.Take({.match = "AntiAliasingTextureLodBias", .section = section, .shown = more(&more_aa, below_100_rows),
               .tooltip = "How sharp textures are while the upscaler renders below 100%.\n"
                          "Matched: as sharp as at your display's resolution. Sharper / Sharpest: one / two steps more\n"
                          "(more detail, can shimmer)."});
    page.Take({.match = "AntiAliasingEffectsAtOutput", .section = section, .shown = more(&more_aa, below_100_rows), .label = "Effects at Output Resolution",
               .tooltip = b.texts->effects_at_output_tooltip});
    page.Take({.match = "AntiAliasingObjectMotion", .section = section,
               .shown = more(&more_aa, [b] { return (b.has_scale_rows || b.has_object_motion) && feature_settings::Upscaling(*b.aa); }),
               .tooltip = b.texts->object_motion_tooltip});
    if (b.water_motion != nullptr) {
      page.Add(new Setting{
          .key = "AntiAliasingWaterMotion",
          .binding = b.water_motion,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Water Motion (Experimental)",
          .section = section,
          .tooltip = "Water tells the upscaler how the camera moves it, at its own depth instead of the riverbed's.",
          .is_visible = more(&more_aa, upscaling),
      });
    }
    if (b.scale_fix_gi_shadow != nullptr) {
      page.Add(new Setting{
          .key = "ScaleFixGiShadowBlur",
          .binding = b.scale_fix_gi_shadow,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "GI and Shadow Blur at Scale (Experimental)",
          .section = section,
          .tooltip = "Below 100%, with Effects at Output on: the game's GI grid and shadow blur read the right depth.",
          .is_visible = more(&more_aa, upscaling),
      });
    }
    page.Take({.match = "FrameGenerationHudRecomposition", .section = section, .shown = more(&more_aa, composing_generation)});
    page.Take({.match = "FrameGenerationPreset", .section = section, .shown = more(&more_aa, dlss_generation)});
    page.Take({.match = "AntiAliasingDebugView", .section = section, .shown = more(&more_aa, upscaling), .label = "Upscaling Debug View",
               .tooltip = (b.has_scale_rows ? "Motion Vectors: the motion the upscaler gets (colour: direction, brighter: faster).\n"
                                              "Object Motion: the soldiers', props' and cloth's own (grey: still, yellow: drawn twice, red: hidden).\n"
                                              "No Camera Motion: the upscaler gets the objects' motion alone."
                                            : "Motion Vectors: the motion the upscaler gets (colour: direction, brighter: faster).")});
    page.Take({.match = "FrameGenerationDebugView", .section = section, .shown = more(&more_aa, dlss_generation)});
    page.Add(EmptyLine(section));
  }

  // Shadows.
  {
    const std::string& section = SHADOWS_SECTION;
    const auto screen = shadows_are(SHADOW_SCREEN);
    const auto rays = shadows_are(SHADOW_RAYS);
    // ATN's presets: every setting of the page but Anti-Aliasing and the upscalers', at its default but for theirs (Page::Defaults).
    const auto preset_button = [&section](const std::string& label, const std::string& tooltip, const Values& changes) {
      return new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = label,
          .section = section,
          .group = "page-presets",
          .tooltip = tooltip,
          .tint = TITLE_TINT,
          .on_change = [changes] {
            renodx::utils::settings::UpdateSettings(page_defaults);
            renodx::utils::settings::UpdateSettings(changes);
          },
      };
    };
    page.Add(preset_button("Reset", b.texts->reset_tooltip,
                           {{"ssao_mode", 0.f}, {"ssr_hierarchical", 0.f}, {"WaterClarity", 1.f}, {"WaterCaustics", 0.f}}));
    page.Add(preset_button("No RT/PT (Recommended)", b.texts->no_rays_tooltip,
                           {{"ScreenSpaceShadows", 1.f}, {"ScreenSpaceShadowsLength", 3.f}, {"light_bounce_strength", 1.f}}));
    Values rays_values = {{"RayTracedSunShadows", 1.f},
                            {"RayTracedShadowSoftness", 0.75f},
                            {"RayTracedLeafLight", 1.f},
                            {"RayTracedColouredShadows", 1.f},
                            {"RayTracedSmokeShadows", 1.f},
                            {"RayTracedSmokeQuality", 2.f},
                            {"RayTracedLightSoftness", 0.25f},
                            {"RayTracedLightImportance", 1.f},
                            {"RayTracedSpriteShadows", 0.f},
                            {"RayTracedFarTreeShadows", 0.f},
                            {"RayTracedLightGlow", 1.f},
                            {"RayTracedGrassSway", 1.f},
                            {"RayTracedSwayRange", 50.f},
                            {"RayTracedOffScreenSway", 1.f},
                            {"RayTracedGrassShadowRange", 0.f},
                            {"RayTracedPathAmbient", 1.f},
                            {"RayTracedPathSkyApart", 1.f},
                            {"RayTracedLightBounce", 2.f},
                            {"light_bounce_strength", 1.f},
                            {"RayTracedLightBounceRays", 1.f},
                            {"RayTracedLightBounceResolution", 0.f},
                            {"RayTracedLightBounceRange", 50.f},
                            {"PathTracingBounces", 1.f},
                            {"RayTracedReflections", 1.f},
                            {"RayTracedReflectionSamples", 1.f},
                            {"RayTracedReflectionReuse", 0.f},
                            {"RayTracedWaterReflections", 1.f},
                            {"RayTracedSpecularOcclusion", 1.f},
                            {"WaterClarity", 1.f},
                            {"WaterCaustics", 1.f},
                            {"RayTracingOutputResolution", 0.f},
                            {"RayTracedFogShadows", 1.f}};
    // No paths for the sky's light here: ray-traced AO, over the game's.
    if (!b.has_path_ambient || !b.rays_preset_path_ambient) rays_values.insert(rays_values.end(), {{"RayTracedPathAmbient", 0.f}, {"RayTracedAmbientOcclusion", 1.f}, {"ssao_mode", 0.f}});
    if (!b.rays_preset_caustics) rays_values.emplace_back("WaterCaustics", 0.f);
    if (!b.rays_preset_water_reflections) rays_values.emplace_back("RayTracedWaterReflections", 0.f);
    if (b.lights_own_row) rays_values.insert(rays_values.end(), {{"RayTracedPointLightShadows", 1.f}, {"RayTracedLightImportance", 0.f}, {"RayTracedFogShadows", 0.f}, {"RayTracedLightGlow", 0.f}});
    auto* rays_preset = preset_button("RT/PT", b.texts->rays_tooltip, rays_values);
    // Ray Reconstruction where it was off: the presets leave the rest of the upscaler alone, and without it the rays have
    // only the built-in denoisers (ATN 2026-11-01: grass shadows flickering when moving). 4: F, as ATN plays. It follows the
    // GPU, not the method: RT/PT pressed under TAA with DLSS chosen later keeps it; it does nothing off DLSS (AaSettings).
    rays_preset->on_change = [apply = rays_preset->on_change, b, nvidia] {
      apply();
      if (nvidia() && b.aa->ray_reconstruction == 0.f) renodx::utils::settings::UpdateSettings({{"AntiAliasingRayReconstruction", 4.f}});
    };
    page.Add(rays_preset);
    page.Add(MethodSlider(section, b.texts->shadow_tooltip,
                          {"Game's Shadow Maps", "Bend Screen-Space", "Ray-Traced (DXR)"}, [b] { return ShadowMethodOf(b); },
                          [](int method) { ChooseShadows(method); }));
    page.Add(Note("Ray-traced effects need an upscaler (Anti-Aliasing above).", section, WARNING_TINT,
                  [b] { return ShadowMethodOf(b) == SHADOW_RAYS && !feature_settings::Upscaling(*b.aa); }));
    // The game's shadow maps' size and the UI 4K mod's lock on it (Graphics 2 of 2's before v120), with the game's shadows only.
    const auto game = shadows_are(SHADOW_GAME);
    page.Take({.match = "GraphicsShadowMapSize", .section = section, .shown = game, .only_some_games = true});
    page.Take({.match = "UI 4K Mod Compatibility", .section = section, .shown = game, .only_some_games = true});
    page.Take({.match = "ScreenSpaceShadowsLength", .section = section, .shown = screen});
    page.Take({.match = "ScreenSpaceShadowsSoftness", .section = section, .shown = screen});
    page.Take({.match = "ScreenSpaceShadowsThickness", .section = section, .shown = screen});
    if (b.has_foliage_light) {
      page.Add(new Setting{
          .key = "ScreenSpaceShadowsFoliage",
          .binding = &b.sss->foliage_light,
          .default_value = 0.5f,
          .label = "Light Through Foliage",
          .section = section,
          .tooltip = "How much sun comes through the shadows grass and leaves cast: they are thin. Walls and soldiers\n"
                     "keep their full shadows, on foliage too. 0: as dark as any other shadow.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_visible = screen,
      });
    }
    if (b.smoke_strength != nullptr) {
      const auto no_rays = [b] { return ShadowMethodOf(b) != SHADOW_RAYS; };
      page.Add(new Setting{
          .key = "SmokeShadowMapStrength",
          .binding = b.smoke_strength,
          .default_value = 1.f,
          .label = "Smoke Shadows",
          .section = section,
          .tooltip = "Smoke and dust dim the sun on what lies below them. No rays, any GPU. Needs in-game shadows on.\n0: off.",
          .min = 0.f,
          .max = 2.f,
          .format = "%.2f",
          .is_visible = no_rays,
      });
      page.Add(new Setting{
          .key = "SmokeShadowMapSoftness",
          .binding = b.smoke_softness,
          .default_value = 2.f,
          .label = "Smoke Shadow Softness",
          .section = section,
          .tooltip = "How soft their shadows are, in metres.",
          .min = 0.f,
          .max = 8.f,
          .format = "%.1f m",
          .is_visible = [b, no_rays] { return no_rays() && *b.smoke_strength > 0.f; },
      });
    }
    page.Take({.match = "RayTracedShadowSoftness", .section = section, .shown = rays, .label = "Softness"});
    page.Take({.match = "RayTracedLeafLight", .section = section, .shown = rays});
    page.Take({.match = "RayTracedColouredShadows", .section = section, .shown = rays, .tooltip = b.texts->coloured_shadows_tooltip});
    page.Take({.match = "RayTracedSmokeShadows", .section = section, .shown = rays, .label = "Smoke Shadows"});
    page.Take({.match = "RayTracedSmokeQuality", .section = section, .shown = rays});
    page.Add(MoreOptions(section, &more_shadows, [b] { return ShadowMethodOf(b) != SHADOW_GAME; }));
    const auto more_screen = more(&more_shadows, screen);
    for (const auto* key : {"ScreenSpaceShadowsContrast", "ScreenSpaceShadowsHardSamples", "ScreenSpaceShadowsFadeSamples", "ScreenSpaceShadowsEdgeThreshold",
                            "ScreenSpaceShadowsIgnoreEdges", "ScreenSpaceShadowsPrecisionOffset", "ScreenSpaceShadowsAlongRay",
                            "ScreenSpaceShadowsMirrorEdges", "ScreenSpaceShadowsDebugView"}) {
      page.Take({.match = key, .section = section, .shown = more_screen});
    }
    const auto more_rays = more(&more_shadows, rays);
    const auto more_lights = [b, more_rays] { return more_rays() && b.rt->local_lights != 0.f; };
    page.Take({.match = "RayTracedPointLightShadows", .section = section, .shown = more_rays, .only_some_games = true});
    page.Take({.match = "RayTracedLightSoftness", .section = section, .shown = more_lights});
    page.Take({.match = "RayTracedLightImportance", .section = section, .shown = more_lights, .label = "Importance-Sampled Torch Shadows (RIS)"});
    page.Take({.match = "RayTracedSpriteShadows", .section = section, .shown = more_rays});
    page.Take({.match = "RayTracedFarTreeShadows", .section = section, .shown = more_rays});
    if (b.rays_replace_maps != nullptr) {
      page.Add(new Setting{
          .key = "RayTracedReplaceShadowMaps",
          .binding = b.rays_replace_maps,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Rays Replace Shadow Maps (Experimental)",
          .section = section,
          .tooltip = "The sun's shadows from rays alone, so Softness shows in full. Smoke, clouds and hills keep the game's.\n"
                     "A unit off screen, such as a flying dragon, loses its shadow.\n"
                     "Off: the rays' shadows on top of the game's shadow maps and contact shadows.",
          .is_visible = more_rays,
      });
    }
    if (b.rays_hills_out_of_view != nullptr) {
      page.Add(new Setting{
          .key = "RayTracedHillsOutOfView",
          .binding = b.rays_hills_out_of_view,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Hills Out of View (Experimental)",
          .section = section,
          .tooltip = "Hills outside the view give ray-traced AO, Light Bounce and reflections.",
          .is_visible = more_rays,
      });
    }
    page.Take({.match = "RayTracedLightGlow", .section = section, .shown = b.lights_own_row ? std::function<bool()>(more_lights) : std::function<bool()>(more_rays),
               .label = b.texts->light_glow_label, .tooltip = b.texts->light_glow_tooltip});
    page.Take({.match = "RayTracedGrassSway", .section = section, .shown = more_rays});
    page.Take({.match = "RayTracedSwayRange", .section = section, .shown = more_rays, .label = "Swaying Shadows Range"});
    page.Take({.match = "RayTracedOffScreenSway", .section = section, .shown = more_rays, .label = "Off-Screen Sway"});
    page.Take({.match = "RayTracedGrassShadowRange", .section = section, .shown = more_rays, .label = "Grass Shadow Range"});
    page.Add(EmptyLine(section));
  }

  // Ambient occlusion.
  {
    const std::string& section = AO_SECTION;
    const auto gtvbao = ao_is(AO_GTVBAO);
    const auto rays = ao_is(AO_RAYS);
    std::vector<std::string> labels = {"Game's SSAO", "GT-VBAO", "Ray-Traced (RTAO)"};
    if (b.has_path_ambient) labels.emplace_back(b.texts->ao_paths_label);
    page.Add(MethodSlider(section, b.texts->ao_tooltip, std::move(labels), [b] { return AoMethodOf(b); },
                          [b](int method) { ChooseAo(b, method); }));
    page.Add(Note("Ray-traced effects need an upscaler (Anti-Aliasing above).", section, WARNING_TINT,
                  [b] { return AoMethodOf(b) >= AO_RAYS && !feature_settings::Upscaling(*b.aa); }));
    page.Take({.match = "Darker shading behind trees and objects: raise Thickness. Lighter around spears: lower it.", .section = section, .shown = gtvbao});
    page.Take({.match = "ao-presets", .section = section, .shown = gtvbao});
    page.Take({.match = "ssao_strength", .section = section, .shown = gtvbao});
    page.Take({.match = "ssao_radius", .section = section, .shown = gtvbao});
    page.Take({.match = "ssao_thickness", .section = section, .shown = [b] { return AoMethodOf(b) == AO_GTVBAO || GiMethodOf(b) == GI_GTVBGI; },
               .tooltip = "How thick objects are taken to be (GT-VBAO and GT-VBGI).\n"
                          "Lower: light gets behind thin things; higher: darker behind them.",
               .enabled = always});
    page.Take({.match = "ssao_resolution", .section = section, .shown = gtvbao});
    page.Take({.match = "RayTracedAORange", .section = section, .shown = rays, .label = "Range"});
    page.Take({.match = "RayTracedAORays", .section = section, .shown = rays, .label = "Rays"});
    page.Take({.match = "RayTracedAOQuarter", .section = section, .shown = rays, .label = "Rate"});
    if (b.rays_ao_spot_filter != nullptr) {
      page.Add(new Setting{
          .key = "RayTracedAOSpotFilter",
          .binding = b.rays_ao_spot_filter,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Spot Filter (Experimental)",
          .section = section,
          .tooltip = "Lifts single black pixels of ray-traced AO to the shade around them. For black spots, more at low render scales.",
          .is_visible = rays,
      });
    }
    page.Take({.match = "RayTracedPathSkyApart", .section = section, .shown = ao_is(AO_PATHS), .label = "Own Sky Rays"});
    page.Add(Note("Follows Global Illumination below (its Path-Traced settings).", section, MORE_TINT, ao_is(AO_PATHS)));
    page.Add(MoreOptions(section, &more_ao, gtvbao));
    const auto more_gtvbao = more(&more_ao, gtvbao);
    for (const auto* key : {"ssao_quality", "ssao_directions", "ssao_auto_thickness", "ssao_temporal", "ssao_edge_fix", "ssao_denoise"}) {
      page.Take({.match = key, .section = section, .shown = more_gtvbao});
    }
    page.Add(EmptyLine(section));
  }

  // Global Illumination.
  {
    const std::string& section = GI_SECTION;
    const auto gtvbgi = gi_is(GI_GTVBGI);
    const auto on = [b] { return GiMethodOf(b) != GI_OFF; };
    page.Add(MethodSlider(section,
                          std::string("Sunlit ground, walls and soldiers bounce their light and colour into nearby shadows.\n"
                                      "GT-VBGI (visibility bitmask): from what is on screen. One Bounce: DXR rays through the scene,\n"
                                      "nothing guessed behind what the camera sees. ReSTIR GI: path tracing, several bounces, also from off\n"
                                      "screen, its samples reused over frames and between neighbours. Heavy. The last two need an upscaler.")
                              + b.texts->gi_tooltip_end,
                          {"Off", "GT-VBGI", "Ray-Traced (One Bounce)", "Path-Traced (ReSTIR GI)"}, [b] { return GiMethodOf(b); },
                          [b](int method) { ChooseGi(b, method); }));
    page.Add(Note("Ray-traced effects need an upscaler (Anti-Aliasing above).", section, WARNING_TINT,
                  [b] { return GiMethodOf(b) >= GI_RAYS && !feature_settings::Upscaling(*b.aa); }));
    page.Take({.match = "gi-presets", .section = section, .shown = gtvbgi});
    page.Take({.match = "light_bounce_strength", .section = section, .shown = on});
    page.Take({.match = "light_bounce_radius", .section = section, .shown = gtvbgi});
    page.Take({.match = "light_bounce_resolution", .section = section, .shown = gtvbgi});
    page.Take({.match = "RayTracedLightBounceRays", .section = section, .shown = gi_traced, .label = "Rays"});
    page.Take({.match = "RayTracedLightBounceResolution", .section = section, .shown = gi_traced, .label = "Resolution"});
    page.Take({.match = "RayTracedLightBounceRange", .section = section, .shown = gi_traced, .label = "Range"});
    page.Take({.match = "PathTracingBounces", .section = section, .shown = gi_is(GI_PATHS), .label = "Bounces"});
    page.Add(MoreOptions(section, &more_gi, on));
    const auto more_on = more(&more_gi, on);
    const auto more_search = more(&more_gi, gtvbgi);
    page.Take({.match = "light_bounce_quality", .section = section, .shown = more_search});
    page.Take({.match = "light_bounce_directions", .section = section, .shown = more_search});
    page.Take({.match = "light_bounce_temporal", .section = section, .shown = more_on});
    page.Take({.match = "light_bounce_denoise", .section = section, .shown = more_on});
    page.Take({.match = "light_bounce_multi_bounce", .section = section, .shown = more_on});
    page.Take({.match = "light_bounce_checkerboard", .section = section, .shown = more_search});
    page.Take({.match = "light_bounce_backface", .section = section, .shown = more_search});
    page.Take({.match = "light_bounce_auto_thickness", .section = section, .shown = more_search});
    page.Take({.match = "light_bounce_msaa_pixel", .section = section, .shown = more_on, .only_some_games = true});
    const auto more_paths = more(&more_gi, gi_is(GI_PATHS));
    page.Take({.match = "RayTracedPathSer", .section = section, .shown = more_paths, .label = "Shader Execution Reordering",
               .tooltip = (b.has_reflections ? "" : "Shader execution reordering for Path Tracing (DXR 1.2, RTX 40 / 50 series): may be faster.")});
    page.Take({.match = "RayTracedSharc", .section = section, .shown = more_paths, .label = "SHaRC Radiance Cache"});
    page.Add(EmptyLine(section));
  }

  // Reflections, and the water they fall on.
  if (b.has_reflections) {
    const std::string& section = REFLECTIONS_SECTION;
    const auto hierarchical = reflections_are(REFLECTION_HIERARCHICAL);
    const auto rays = reflections_are(REFLECTION_RAYS);
    if (b.ssr_hierarchical != nullptr) {
      page.Add(MethodSlider(section, b.texts->reflections_tooltip, {"Game's SSR", b.texts->reflections_middle_label, b.texts->reflections_rays_label}, [b] { return ReflectionMethodOf(b); },
                            [](int method) { ChooseReflections(method); }));
    } else {
      page.Add(MethodSlider(section, b.texts->reflections_tooltip, {"Game's SSR", "Ray-Traced (DXR, Experimental)"}, [b] { return (ReflectionMethodOf(b) == REFLECTION_RAYS) ? 1 : 0; },
                            [](int place) { ChooseReflections((place == 1) ? REFLECTION_RAYS : REFLECTION_GAME); }));
    }
    page.Add(Note("Ray-traced effects need an upscaler (Anti-Aliasing above).", section, WARNING_TINT,
                  [b] { return ReflectionMethodOf(b) == REFLECTION_RAYS && !feature_settings::Upscaling(*b.aa); }));
    page.Take({.match = "ssr_quality", .section = section, .shown = [b] { return ReflectionMethodOf(b) != REFLECTION_RAYS; }, .label = "Quality", .only_some_games = true});
    page.Take({.match = "ssr_rays", .section = section, .shown = hierarchical, .label = "Rays", .only_some_games = true});
    page.Take({.match = "SSRDenoiser", .section = section, .shown = hierarchical, .label = "Denoiser", .only_some_games = true});
    page.Take({.match = "RayTracedReflectionSamples", .section = section, .shown = rays, .label = "Samples"});
    page.Take({.match = "RayTracedReflectionReuse", .section = section, .shown = rays, .label = "Reuse"});
    page.Take({.match = "RayTracedWaterReflections", .section = section, .shown = rays, .label = b.texts->water_reflections_label, .tooltip = b.texts->water_reflections_tooltip});
    page.Take({.match = "RayTracedSpecularOcclusion", .section = section, .shown = always, .label = b.texts->specular_occlusion_label});
    page.Take({.match = "WaterClarity", .section = section, .shown = always, .only_some_games = true});
    page.Take({.match = "WaterCaustics", .section = section, .shown = always, .label = b.texts->water_caustics_label, .only_some_games = true});
    page.Add(EmptyLine(section));
  }

  // What every ray-traced effect shares.
  {
    const std::string& section = RT_SECTION;
    page.Take({.match = "RayTracingOutputResolution", .section = section, .shown = upscaling,
               .label = (b.has_scale_rows ? "" : "Rays a Pixel"),
               .tooltip = (b.has_scale_rows ? "" : "More rays for every pixel, each from its own spot of it: less noise, finer shadow edges.\nThe cost grows with the rays. "
                                                 "Match Output: as many rays on screen as at 100% in-game Resolution Scale (up to 8 a pixel, 1 at 100%).")});
    page.Add(new Setting{
        .key = "RayTracingDenoiser",
        .binding = &b.rt->denoiser,
        .value_type = SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Ray Tracing Denoiser (Experimental)",
        .section = section,
        .tooltip = (b.has_reflections ? "Cleans the ray-traced shadows, AO, Light Bounce and reflections when DLSS Ray Reconstruction is off.\n"
                                        "Built-in: the mod's own filters. NRD: NVIDIA's denoisers, steadier, uses more memory."
                                      : "Cleans the ray-traced shadows, AO and Light Bounce when DLSS Ray Reconstruction is off.\n"
                                        "Built-in: the mod's own filters. NRD: NVIDIA's denoisers, steadier, uses more memory."),
        .labels = {"Built-in", "NRD"},
        .is_visible = [upscaling, b] { return upscaling() && !(b.aa->method == 5.f && b.aa->ray_reconstruction != 0.f); },
    });
    page.Take({.match = "RayTracedFogShadows", .section = section, .shown = upscaling, .label = b.texts->fog_shadows_label, .tooltip = b.texts->fog_shadows_tooltip});
    page.Add(Note("Needs an upscaler (Anti-Aliasing above).", section, MORE_TINT, no_upscaler));
    page.Add(MoreOptions(section, &more_rt, upscaling));
    const auto more_rt_rows = more(&more_rt, upscaling);
    page.Take({.match = "RayTracingRaysShadows", .section = section, .shown = more_rt_rows, .tooltip = b.texts->rays_a_pixel_shadows_tooltip});
    page.Take({.match = "RayTracingRaysAmbient", .section = section, .shown = more_rt_rows});
    page.Take({.match = "RayTracingRaysReflections", .section = section, .shown = more(&more_rt, [b, upscaling] { return b.has_reflections && upscaling(); })});
    page.Take({.match = "RayTracingRaysPaths", .section = section, .shown = more_rt_rows, .tooltip = b.texts->rays_a_pixel_paths_tooltip});
    page.Take({.match = "RayTracedLeafMicromaps", .section = section, .shown = more_rt_rows, .label = "Opacity Micromaps (Leaves)"});
    page.Take({.match = "RayTracedGpuMicromaps", .section = section, .shown = more_rt_rows, .label = "Opacity Micromaps: GPU Baking"});
    page.Take({.match = "RayTracedFastLeafShadows", .section = section, .shown = more_rt_rows, .label = "Fast Leaf Shadows"});
    if (b.placed_objects != nullptr) {
      page.Add(new Setting{
          .key = "RayTracedPlacedObjects",
          .binding = b.placed_objects,
          .value_type = SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Placed Objects",
          .section = section,
          .tooltip = "Buildings, rocks, trees and weapons enter the ray tracing once: faster, and they cast from off screen.",
          .is_visible = more_rt_rows,
      });
    }
    page.Take({.match = "RayTracedDebugView", .section = section, .shown = more_rt_rows});
    page.Take({.match = "RayTracedSceneTest", .section = section, .shown = more_rt_rows});
    page.Take({.match = "Record Ray Tracing Profile (30 s)", .section = section, .shown = more_rt_rows});
    page.Take({.match = "RayTracedDebugLog", .section = section, .shown = more_rt_rows});
    page.Add(EmptyLine(section));
  }

  page.Finish({lighting_settings::AO_SECTION, lighting_settings::GI_SECTION, feature_settings::SSS_SECTION, feature_settings::AA_SECTION}, RT_SECTION);
  page_defaults = page.Defaults(AA_SECTION);
  // The presets leave Shadow Map Size alone (it needs a restart; Graphics 2 of 2's presets set it) and Log Details (not a look).
  std::erase_if(page_defaults, [](const auto& value) { return value.first == "GraphicsShadowMapSize" || value.first == "RayTracedDebugLog"; });
  for (const std::string& section : Sections()) renodx::utils::settings::section_tints.emplace_back(section, TITLE_TINT);
}

}  // namespace twattila::graphics_page
