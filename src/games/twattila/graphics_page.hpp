/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../../utils/settings.hpp"
#include "./feature_settings.hpp"
#include "./lighting_settings.hpp"
#include "./rt_scene.hpp"
#include "./smoke_map.hpp"

// Attila's Graphics 1 of 2 (ATN): one section per job (anti-aliasing, shadows, ambient occlusion, Global Illumination,
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

// What the page reads.
struct Bindings {
  feature_settings::AaSliders* aa = nullptr;
  feature_settings::SssSliders* sss = nullptr;
  rt_scene::Sliders* rt = nullptr;
  float* ssao_mode = nullptr;
  float* light_bounce_strength = nullptr;
  float* ssr_hierarchical = nullptr;
  smoke_map::Sliders* smoke = nullptr;
};

enum AoMethod : int { AO_GAME, AO_GTVBAO, AO_RAYS, AO_PATHS };
enum ShadowMethod : int { SHADOW_GAME, SHADOW_SCREEN, SHADOW_RAYS };
enum GiMethod : int { GI_OFF, GI_GTVBGI, GI_RAYS, GI_PATHS };
enum ReflectionMethod : int { REFLECTION_GAME, REFLECTION_HIERARCHICAL, REFLECTION_RAYS };

inline int AoMethodOf(const Bindings& b) {
  if (b.rt->path_ambient != 0.f && b.rt->light_bounce == 2.f && *b.light_bounce_strength > 0.f) return AO_PATHS;
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
  return (*b.ssr_hierarchical != 0.f) ? REFLECTION_HIERARCHICAL : REFLECTION_GAME;
}

// The choices write what the method needs. Path-traced AO is Global Illumination's paths bringing the sky's light: it
// turns them on, and leaving them falls back to ray-traced AO (the game's ambient would come back with no AO on it).
inline void ChooseAo(const Bindings& b, int method) {
  Values values = {{"RayTracedPathAmbient", (method == AO_PATHS) ? 1.f : 0.f}};
  if (method == AO_GAME || method == AO_GTVBAO) values.emplace_back("ssao_mode", (method == AO_GTVBAO) ? 1.f : 0.f);
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
    if (!found) renodx::utils::log::w("[Total War] Graphics page: no setting ", row.match.c_str());
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
  const auto no_upscaler = [b] { return !feature_settings::Upscaling(*b.aa); };
  const auto dlss = [b] { return b.aa->method == 5.f; };
  const auto nvidia = [] { return dlaa_bridge::OnNvidia(); };
  const auto frame_generation = [b] { return feature_settings::Upscaling(*b.aa) && dlaa_bridge::OnNvidia() && b.aa->frame_generation != 0.f; };
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
      if (setting->key == "AntiAliasing") setting->labels = {"Off", "SMAA", "CMAA2", "SMAA T2x", "TAA", "DLSS", "FSR"};
    }
    page.Add(Note("Upscaling, Frame Generation and ray tracing need the full download, not only this addon: github.com/arshiatn/renodx/releases or Nexus.",
                  section, WARNING_TINT, [] { return !dlaa_bridge::HelperInstalled(); }));
    page.Take({.match = "AntiAliasing", .section = section, .shown = [] { return false; }});
    // The list without DLSS off NVIDIA's GPUs: the slider's places past it are one lower than the values.
    page.Add(new Setting{
        .value_type = SettingValueType::CUSTOM,
        .label = "Anti-Aliasing",
        .section = section,
        .tooltip = "Smooths jagged edges. Turn the in-game anti-aliasing off.\n"
                   "SMAA: catches more edges. CMAA2: sharper, keeps more fine detail.\n"
                   "SMAA T2x: SMAA with two frames blended. TAA: blends frames, best on grass, softer.\n"
                   "DLSS: NVIDIA's (RTX), the best. FSR: AMD's, FSR 4 on Radeon RX 6000 and newer, else 3.1.\n"
                   "Every ray-traced effect needs an upscaler. Needs renodx-tw-dlaa64.exe and its DLLs in the game folder.",
        .on_draw = [b] {
          static constexpr std::array<const char*, 7> LABELS = {"Off", "SMAA", "CMAA2", "SMAA T2x", "TAA", "DLSS", "FSR"};
          const int skipped = (dlaa_bridge::OnNvidia() ? 0 : 1);
          const int last = static_cast<int>(LABELS.size()) - 1 - skipped;
          const int method = std::clamp(static_cast<int>(b.aa->method), 0, static_cast<int>(LABELS.size()) - 1);
          int place = (method > 5 ? method - skipped : method);
          const int shown = (place >= 5 ? place + skipped : place);
          const bool changed = ImGui::SliderInt("Anti-Aliasing", &place, 0, last, LABELS[shown], ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
          if (changed) renodx::utils::settings::UpdateSettings({{"AntiAliasing", static_cast<float>(place >= 5 ? place + skipped : place)}});
          return changed;
        },
    });
    // What the helper says it really runs.
    page.Add(new Setting{.value_type = SettingValueType::CUSTOM,
                         .section = section,
                         .on_draw = [] {
                           ImGui::TextWrapped("%s", (dlaa_bridge::bridge.shared != nullptr ? dlaa_bridge::bridge.shared->status : "Upscaling off"));
                           return false;
                         },
                         .is_visible = [b] { return b.aa->method > 5.f; }});
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
    page.Take({.match = "AntiAliasingDlssMode", .section = section, .shown = upscaling, .label = "Upscaling Mode",
               .tooltip = "Native: the game at your resolution. The others draw it smaller and the upscaler brings it up (faster)."});
    page.Take({.match = "AntiAliasingRenderScale", .section = section, .shown = upscaling,
               .tooltip = "The size the game is drawn at (100%: Native). Below 33% the upscalers' own modes end."});
    page.Take({.match = "AntiAliasingDlaaPreset", .section = section, .shown = dlss, .label = "DLSS Preset",
               .tooltip = "NVIDIA's models: K the first transformer, M the second, L the sharpest (default)."});
    page.Take({.match = "AntiAliasingRayReconstruction", .section = section, .shown = dlss, .label = "DLSS Ray Reconstruction",
               .tooltip = "NVIDIA's denoiser for ray tracing in place of DLSS's usual model: much less noise in everything\n"
                          "ray-traced. Runs only while something is ray-traced. Needs nvngx_dlssd.dll next to nvngx_dlss.dll."});
    page.Take({.match = "FrameGeneration", .section = section, .shown = [b] { return feature_settings::Upscaling(*b.aa) && dlaa_bridge::OnNvidia(); }, .label = "DLSS Frame Generation",
               .tooltip = "NVIDIA DLSS Frame Generation: frames shown per rendered frame. Turn the game's VSync off.\n"
                          "2x: RTX 40 or newer. 3x to 6x and Dynamic (as many as the display's refresh rate takes): RTX 50.\n"
                          "Needs a windowed or borderless game and Streamline's files next to renodx-tw-dlaa64.exe."});
    // What the helper says it really does (refreshed every 30 presents): frames generated, or why none.
    page.Add(new Setting{.value_type = SettingValueType::CUSTOM,
                         .section = section,
                         .on_draw = [] {
                           ImGui::TextWrapped("%s", dlaa_bridge::FrameGenerationStatus().c_str());
                           return false;
                         },
                         .is_visible = frame_generation});
    page.Take({.match = "Reflex", .section = section, .shown = nvidia});
    page.Take({.match = "FrameGenerationFrameLimit", .section = section, .shown = nvidia});
    const auto cmaa = [b] { return b.aa->method == 2.f; };
    page.Add(MoreOptions(section, &more_aa, [b] { return feature_settings::Upscaling(*b.aa) || b.aa->method == 2.f; }));
    page.Take({.match = "AntiAliasingExtraSharpness", .section = section, .shown = more(&more_aa, cmaa), .label = "Extra Sharpness"});
    page.Take({.match = "AntiAliasingTextureLodBias", .section = section, .shown = more(&more_aa, upscaling),
               .tooltip = "How sharp textures are while the upscaler renders below 100%.\n"
                          "Matched: as sharp as at your display's resolution. Sharper / Sharpest: one / two steps more\n"
                          "(more detail, can shimmer)."});
    page.Take({.match = "AntiAliasingEffectsAtOutput", .section = section, .shown = more(&more_aa, upscaling), .label = "Effects at Output Resolution",
               .tooltip = "While the upscaler renders below 100%, ambient occlusion, Light Bounce, reflections and shadow softness\n"
                          "keep their 100% detail. Looks better, can cost some performance."});
    page.Take({.match = "AntiAliasingObjectMotion", .section = section, .shown = more(&more_aa, upscaling),
               .tooltip = "Soldiers, props and cloth tell the upscaler how they move: sharper while they move.\n"
                          "Off: faster with many soldiers on screen (they can smear a little when moving)."});
    page.Take({.match = "FrameGenerationHudRecomposition", .section = section, .shown = more(&more_aa, frame_generation)});
    page.Take({.match = "FrameGenerationPreset", .section = section, .shown = more(&more_aa, frame_generation)});
    page.Take({.match = "AntiAliasingDebugView", .section = section, .shown = more(&more_aa, upscaling), .label = "Upscaling Debug View",
               .tooltip = "Motion Vectors: the motion the upscaler gets (colour: direction, brighter: faster).\n"
                          "Object Motion: the soldiers', props' and cloth's own (grey: still, yellow: drawn twice, red: hidden).\n"
                          "No Camera Motion: the upscaler gets the objects' motion alone."});
    page.Take({.match = "FrameGenerationDebugView", .section = section, .shown = more(&more_aa, frame_generation)});
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
    page.Add(preset_button("Reset",
                           "Everything on this page but Anti-Aliasing and Upscaling back to the game's: its SSAO, SSR and water.\n"
                           "The rest at its default.",
                           {{"ssao_mode", 0.f}, {"ssr_hierarchical", 0.f}, {"WaterClarity", 1.f}, {"WaterCaustics", 0.f}}));
    page.Add(preset_button("No RT/PT (Recommended)",
                           "Bend Screen-Space Shadows (Length 480), FidelityFX SSSR, GT-VBAO and GT-VBGI at their Recommended\n"
                           "settings. The rest of this page at its default.",
                           {{"ScreenSpaceShadows", 1.f}, {"ScreenSpaceShadowsLength", 3.f}, {"light_bounce_strength", 1.f}}));
    auto* rays_preset = preset_button("RT/PT",
                           "Ray-traced shadows and reflections, path-traced ambient occlusion and Global Illumination at my\n"
                           "settings. The rest of this page at its default. Needs an upscaler (Anti-Aliasing above).\n"
                           "On NVIDIA also turns DLSS Ray Reconstruction on. Elsewhere Experimental, slower: its own denoisers.",
                           {{"RayTracedSunShadows", 1.f},
                            {"RayTracedShadowSoftness", 0.75f},
                            {"RayTracedLeafLight", 1.f},
                            {"RayTracedColouredShadows", 1.f},
                            {"RayTracedSmokeShadows", 1.f},
                            {"RayTracedSmokeQuality", 1.f},
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
                            {"RayTracedReflectionReuse", 2.f},
                            {"RayTracedWaterReflections", 1.f},
                            {"RayTracedSpecularOcclusion", 1.f},
                            {"WaterClarity", 1.f},
                            {"WaterCaustics", 1.f},
                            {"RayTracingOutputResolution", 0.f},
                            {"RayTracedFogShadows", 1.f}});
    // Ray Reconstruction where it was off: the presets leave the rest of the upscaler alone, and without it the rays have
    // only the built-in denoisers (ATN 2026-11-01: grass shadows flickering when moving). 4: F, as ATN plays. It follows the
    // GPU, not the method: RT/PT pressed under TAA with DLSS chosen later keeps it; it does nothing off DLSS (AaSettings).
    rays_preset->on_change = [apply = rays_preset->on_change, b, nvidia] {
      apply();
      if (nvidia() && b.aa->ray_reconstruction == 0.f) renodx::utils::settings::UpdateSettings({{"AntiAliasingRayReconstruction", 4.f}});
    };
    page.Add(rays_preset);
    page.Add(MethodSlider(section,
                          "Game's Shadow Maps: the original. Bend Screen-Space Shadows: those plus fine contact shadows from\n"
                          "what is on screen (Bend Studio's; soldiers, weapons, grass; needs in-game shadows on).\n"
                          "DXR: DirectX Raytracing, every shadow from rays, the sun's and the torches' and fires', in place of\n"
                          "both. Needs an upscaler.",
                          {"Game's Shadow Maps", "Bend Screen-Space", "Ray-Traced (DXR)"}, [b] { return ShadowMethodOf(b); },
                          [](int method) { ChooseShadows(method); }));
    page.Add(Note("Ray-traced effects need an upscaler (Anti-Aliasing above).", section, WARNING_TINT,
                  [b] { return ShadowMethodOf(b) == SHADOW_RAYS && !feature_settings::Upscaling(*b.aa); }));
    // The game's shadow maps' size and the UI 4K mod's lock on it (Graphics 2 of 2's before v120), with the game's shadows only.
    const auto game = shadows_are(SHADOW_GAME);
    page.Take({.match = "GraphicsShadowMapSize", .section = section, .shown = game});
    page.Take({.match = "UI 4K Mod Compatibility", .section = section, .shown = game});
    page.Take({.match = "ScreenSpaceShadowsLength", .section = section, .shown = screen});
    page.Take({.match = "ScreenSpaceShadowsSoftness", .section = section, .shown = screen});
    page.Take({.match = "ScreenSpaceShadowsThickness", .section = section, .shown = screen});
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
    const auto no_rays = [b] { return ShadowMethodOf(b) != SHADOW_RAYS; };
    page.Add(new Setting{
        .key = "SmokeShadowMapStrength",
        .binding = &b.smoke->strength,
        .default_value = 0.f,
        .label = "Smoke Shadows (Experimental)",
        .section = section,
        .tooltip = "Smoke and dust dim the sun on what lies below them. No rays, any GPU. Needs in-game shadows on.\n0: off.",
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_visible = no_rays,
    });
    page.Add(new Setting{
        .key = "SmokeShadowMapSoftness",
        .binding = &b.smoke->softness,
        .default_value = 2.f,
        .label = "Smoke Shadow Softness",
        .section = section,
        .tooltip = "How soft their shadows are, in metres.",
        .min = 0.f,
        .max = 8.f,
        .format = "%.1f m",
        .is_visible = [b, no_rays] { return no_rays() && b.smoke->strength > 0.f; },
    });
    page.Take({.match = "RayTracedShadowSoftness", .section = section, .shown = rays, .label = "Softness"});
    page.Take({.match = "RayTracedLeafLight", .section = section, .shown = rays});
    page.Take({.match = "RayTracedColouredShadows", .section = section, .shown = rays});
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
    page.Take({.match = "RayTracedLightSoftness", .section = section, .shown = more_rays});
    page.Take({.match = "RayTracedLightImportance", .section = section, .shown = more_rays, .label = "Importance-Sampled Torch Shadows (RIS)"});
    page.Take({.match = "RayTracedSpriteShadows", .section = section, .shown = more_rays});
    page.Take({.match = "RayTracedFarTreeShadows", .section = section, .shown = more_rays});
    page.Take({.match = "RayTracedLightGlow", .section = section, .shown = more_rays});
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
    page.Add(MethodSlider(section,
                          "Soft shadows in corners and where objects meet the ground. Keep in-game SSAO on for all of these.\n"
                          "Game's SSAO: everything counts as solid, so soldiers and trees get dark halos.\n"
                          "GT-VBAO (Ground Truth AO with a visibility bitmask): objects have a thickness, so light gets behind\n"
                          "thin ones.\n"
                          "RTAO: from rays through the scene, in place of both. Needs an upscaler.\n"
                          "ReSTIR GI: Global Illumination's paths bring the sky's light in place of the game's ambient;\n"
                          "turns it on below. Needs an upscaler.",
                          {"Game's SSAO", "GT-VBAO", "Ray-Traced (RTAO)", "Path-Traced (ReSTIR GI)"}, [b] { return AoMethodOf(b); },
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
                          "Sunlit ground, walls and soldiers bounce their light and colour into nearby shadows.\n"
                          "GT-VBGI (visibility bitmask): from what is on screen. One Bounce: DXR rays through the scene,\n"
                          "nothing guessed behind what the camera sees. ReSTIR GI: path tracing, several bounces, also from off\n"
                          "screen, its samples reused over frames and between neighbours. Heavy. The last two need an upscaler.",
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
    page.Take({.match = "light_bounce_msaa_pixel", .section = section, .shown = more_on});
    const auto more_paths = more(&more_gi, gi_is(GI_PATHS));
    page.Take({.match = "RayTracedPathSer", .section = section, .shown = more_paths, .label = "Shader Execution Reordering"});
    page.Take({.match = "RayTracedSharc", .section = section, .shown = more_paths, .label = "SHaRC Radiance Cache"});
    page.Add(EmptyLine(section));
  }

  // Reflections, and the water they fall on.
  {
    const std::string& section = REFLECTIONS_SECTION;
    const auto hierarchical = reflections_are(REFLECTION_HIERARCHICAL);
    const auto rays = reflections_are(REFLECTION_RAYS);
    page.Add(MethodSlider(section,
                          "Reflections on water, armour and metal. Need in-game Screen Space Reflections on.\n"
                          "Game's SSR: the original. FidelityFX SSSR: AMD's hierarchical ray march, exact, no gaps, reaching\n"
                          "across the screen, usually faster. DXR: from rays, also what is off screen or hidden. Needs an upscaler.",
                          {"Game's SSR", "FidelityFX SSSR", "Ray-Traced (DXR)"}, [b] { return ReflectionMethodOf(b); },
                          [](int method) { ChooseReflections(method); }));
    page.Add(Note("Ray-traced effects need an upscaler (Anti-Aliasing above).", section, WARNING_TINT,
                  [b] { return ReflectionMethodOf(b) == REFLECTION_RAYS && !feature_settings::Upscaling(*b.aa); }));
    page.Take({.match = "ssr_quality", .section = section, .shown = [b] { return ReflectionMethodOf(b) != REFLECTION_RAYS; }, .label = "Quality"});
    page.Take({.match = "ssr_rays", .section = section, .shown = hierarchical, .label = "Rays"});
    page.Take({.match = "SSRDenoiser", .section = section, .shown = hierarchical, .label = "Denoiser"});
    page.Take({.match = "RayTracedReflectionSamples", .section = section, .shown = rays, .label = "Samples"});
    page.Take({.match = "RayTracedReflectionReuse", .section = section, .shown = rays, .label = "Reuse"});
    page.Take({.match = "RayTracedWaterReflections", .section = section, .shown = rays, .label = "Water Reflections"});
    page.Take({.match = "RayTracedSpecularOcclusion", .section = section, .shown = always, .label = "Ray-Traced Specular Occlusion"});
    page.Take({.match = "WaterClarity", .section = section, .shown = always});
    page.Take({.match = "WaterCaustics", .section = section, .shown = always, .label = "Water Caustics"});
    page.Add(EmptyLine(section));
  }

  // What every ray-traced effect shares.
  {
    const std::string& section = RT_SECTION;
    page.Take({.match = "RayTracingOutputResolution", .section = section, .shown = upscaling});
    page.Take({.match = "RayTracedFogShadows", .section = section, .shown = upscaling, .label = "Fog Shadows"});
    page.Add(Note("Needs an upscaler (Anti-Aliasing above).", section, MORE_TINT, no_upscaler));
    page.Add(MoreOptions(section, &more_rt, upscaling));
    const auto more_rt_rows = more(&more_rt, upscaling);
    for (const auto* key : {"RayTracingRaysShadows", "RayTracingRaysAmbient", "RayTracingRaysReflections", "RayTracingRaysPaths"}) {
      page.Take({.match = key, .section = section, .shown = more_rt_rows});
    }
    page.Take({.match = "RayTracedLeafMicromaps", .section = section, .shown = more_rt_rows, .label = "Opacity Micromaps (Leaves)"});
    page.Take({.match = "RayTracedGpuMicromaps", .section = section, .shown = more_rt_rows, .label = "Opacity Micromaps: GPU Baking"});
    page.Take({.match = "RayTracedFastLeafShadows", .section = section, .shown = more_rt_rows, .label = "Fast Leaf Shadows"});
    page.Take({.match = "RayTracedDebugView", .section = section, .shown = more_rt_rows});
    page.Take({.match = "RayTracedSceneTest", .section = section, .shown = more_rt_rows});
    page.Take({.match = "Record Ray Tracing Profile (30 s)", .section = section, .shown = more_rt_rows});
    page.Take({.match = "RayTracedDebugLog", .section = section, .shown = more_rt_rows});
    page.Add(EmptyLine(section));
  }

  page.Finish({lighting_settings::AO_SECTION, lighting_settings::GI_SECTION, feature_settings::SSS_SECTION, feature_settings::AA_SECTION}, RT_SECTION);
  page_defaults = page.Defaults(AA_SECTION);
  // The presets leave Shadow Map Size alone (it needs a restart; Graphics 2 of 2's presets set it).
  std::erase_if(page_defaults, [](const auto& value) { return value.first == "GraphicsShadowMapSize"; });
  for (const std::string& section : Sections()) renodx::utils::settings::section_tints.emplace_back(section, TITLE_TINT);
}

}  // namespace twattila::graphics_page
