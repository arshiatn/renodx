/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include "../../utils/state.hpp"
#include "./dlss_jitter.hpp"
#include "./frame_trace.hpp"
#include "./game_dlss.hpp"
#include "./grass_distance.hpp"
#include "./model_detail.hpp"
#include "./particle_resolution.hpp"
#include "./render_scale.hpp"
#include "./shadow_resolution.hpp"
#include "./terrain_detail.hpp"
#include "./texture_lod_bias.hpp"

// Total War: SHOGUN 2 Complete Edition: the game's DLSS (DLAA only) toward real upscaling. Step 1: hooks on the
// game's DLSS creation and evaluation (what it passes, in the overlay and ReShade.log) and a one-frame trace.
// v3: the camera jitter the game leaves off with DLSS (game_dlss.hpp), while its DLSS runs.
// v4: render scale (experimental): the 3D image below the display size, upscaled by the game's DLSS (render_scale.hpp).
// v7: DLSS told the motion vectors are at the render size (game_dlss.hpp FLAGS_IMMEDIATES), texture LOD bias
// (texture_lod_bias.hpp). v11: DLSS presets (game_dlss.hpp OnSetUI); graphics options here (as Ryse / Expeditions /
// Tales), the shader ones read by the HDR addon from ReShade.ini. v12: Simple / Advanced as rysesonofrome_dlss, grass
// distance (grass_distance.hpp), the LOD bias bound again when the main view starts. v13: grass distance up to 32x.
// v14: terrain detail (terrain_detail.hpp). v15: model detail distance (model_detail.hpp). v16: render sizes at the
// display's exact aspect (game_dlss.hpp RenderSize), DLSS input capture (frame_trace.hpp). v17: particle resolution
// (particle_resolution.hpp), SSR ray steps and screen-space shadow samples (HDR addon), particle mask test removed.
// v18: the particles' reduced resolution scaled with the render scale (render_scale.hpp); SSR to 16x, SSS to 960.
// v19: particle resolution's minimum as a share of the display (particle_resolution.hpp), replacing Game / Full.
// v20: that minimum follows the render scale by itself (the game's share of the display), its slider removed.
// v21: SSR ray steps to 64x (6400), shadow samples to 32x (3840). v22: graphics presets (Recommended (ATN's
// settings), Reset); SSR ray steps removed (ATN: no visible or performance change). v23: shadow stability (HDR addon),
// shorter tooltips. v24: the frame's DLSS jitter shared with the HDR addon (dlss_jitter.hpp: its heat haze), shadow
// softness (HDR addon). v25: shadow map resolution (shadow_resolution.hpp), shadow map stability (HDR addon).
// v26: shadow softness removed (ATN), Recommended with both shadow stabilities at Steadier. v27: particle resolution
// On / Off (Off: the game's own minimum), DLSS preset K by default (ATN: steadier at DLAA). v28: particle resolution
// Off by default and in Reset (ATN). v29: less CPU: the frame trace's events only while tracing, debug counts only
// while the overlay shows them, the texture LOD bias's sampler tracking only below 100%, no lock per buffer unmap.
// v30: SSR ray steps back (ATN: the sea's reflections in sea battles). v31: removed again (ATN: no change there either).

namespace {

bool registered = false;
bool attached = false;
bool advanced = false;  // overlay: Settings mode Advanced (ReShade.ini SettingsMode)
// ReShade.ini [Shogun 2 DLSS]: Jitter (on), JitterFlip (debug, 0).
std::atomic_bool jitter_enabled = true;
int jitter_flip = 0;           // overlay
uint64_t last_evaluations = 0;  // present thread
// Render scale (ReShade.ini [Shogun 2 DLSS] RenderScale: index into RENDER_SCALES, or CUSTOM_RENDER_SCALE for
// RenderScaleCustom in percent).
constexpr std::array<float, 6> RENDER_SCALES = {1.f, 0.77f, 2.f / 3.f, 0.58f, 0.5f, 1.f / 3.f};
constexpr int CUSTOM_RENDER_SCALE = static_cast<int>(RENDER_SCALES.size());
int render_scale_choice = 0;    // overlay
int render_scale_percent = 50;  // overlay, custom
// Texture LOD bias below 100% (ReShade.ini TextureLodBias): 0 off, 1 matched, 2 sharper (NVIDIA's), 3 sharpest.
std::atomic_int texture_lod_bias_choice = 2;
// DLSS preset for every mode (ReShade.ini Preset: the NGX value), as rysesonofrome_dlss.
struct PresetChoice {
  uint32_t value;
  const char* label;
};
constexpr std::array<PresetChoice, 4> PRESETS = {{
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_E, .label = "E (CNN, Fastest, Worst looking, not recommended)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_K, .label = "K (Transformer 1, default, steadiest at DLAA)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_M, .label = "M (Transformer 2)"},
    {.value = NVSDK_NGX_DLSS_Hint_Render_Preset_L, .label = "L (Transformer 2, Slower but better than M)"},
}};
uint32_t preset = NVSDK_NGX_DLSS_Hint_Render_Preset_K;  // overlay
// Graphics (ReShade.ini SSRResolution, read by the RenoDX HDR addon, whose shaders it changes): 0 the game's, 1 full.
constexpr std::array<const char*, 2> SSR_RESOLUTIONS = {"Game (half the render size)", "Full"};
int ssr_resolution = 1;  // overlay
// ReShade.ini ParticleResolution (particle_resolution.hpp): 0 the game's minimum (30% of the render size), 1 its share
// of the display (30% / render scale).
constexpr std::array<const char*, 2> PARTICLE_RESOLUTIONS = {"Off (the game's)", "On (follows the render scale)"};
std::atomic_int particle_resolution = 0;
// ReShade.ini SSSSamples (HDR addon: the screen-space shadows' samples, 120 x 2^index).
constexpr std::array<const char*, 6> SSS_SAMPLES = {"Game (120)", "2x (240)", "4x (480)", "8x (960)", "16x (1920)",
                                                    "32x (3840)"};
int sss_samples = 0;  // overlay
// ReShade.ini SSSStability (HDR addon): 0 the game's, 1 softer shadow edges, 2 and depth edges cast no shadow.
constexpr std::array<const char*, 3> SSS_STABILITIES = {"Game", "Steadier (softer)", "Steadiest (softer, thinner)"};
int sss_stability = 0;  // overlay
// ReShade.ini ShadowMapResolution: the sun's shadow map, 4096 x (index + 1) (shadow_resolution.hpp).
constexpr std::array<const char*, 4> SHADOW_MAP_RESOLUTIONS = {"Game (4096)", "2x (8192)", "3x (12288)", "4x (16384)"};
int shadow_map_resolution = 0;  // overlay
// ReShade.ini ShadowMapStability (HDR addon): the cascades' receivers off their surface, 0 the game's, 1 / 2 further.
constexpr std::array<const char*, 3> SHADOW_MAP_STABILITIES = {"Game", "Steadier", "Steadiest"};
int shadow_map_stability = 0;  // overlay
// Graphics distance sliders: the game's distance x scale.
struct ScaleChoice {
  float scale;
  const char* label;
};
// ReShade.ini GrassDistance: index.
constexpr std::array<ScaleChoice, 11> GRASS_DISTANCES = {{
    {.scale = 1.f, .label = "Game"},
    {.scale = 1.5f, .label = "1.5x"},
    {.scale = 2.f, .label = "2x"},
    {.scale = 3.f, .label = "3x"},
    {.scale = 4.f, .label = "4x"},
    {.scale = 6.f, .label = "6x"},
    {.scale = 8.f, .label = "8x"},
    {.scale = 12.f, .label = "12x"},
    {.scale = 16.f, .label = "16x"},
    {.scale = 24.f, .label = "24x"},
    {.scale = 32.f, .label = "32x"},
}};
int grass_distance = 0;  // overlay
// ReShade.ini ModelDetail: index.
constexpr std::array<ScaleChoice, 8> MODEL_DETAILS = {{
    {.scale = 1.f, .label = "Game"},
    {.scale = 1.5f, .label = "1.5x"},
    {.scale = 2.f, .label = "2x"},
    {.scale = 3.f, .label = "3x"},
    {.scale = 4.f, .label = "4x"},
    {.scale = 6.f, .label = "6x"},
    {.scale = 8.f, .label = "8x"},
    {.scale = 16.f, .label = "16x"},
}};
int model_detail = 0;  // overlay
// Graphics (ReShade.ini TerrainDetail): 0 the game's, 1 the finest level everywhere.
constexpr std::array<const char*, 2> TERRAIN_DETAILS = {"Game", "Finest everywhere"};
std::atomic_int terrain_detail = 0;
// Graphics presets (overlay buttons): indices into the options above.
struct GraphicsPreset {
  int ssr_resolution = 1;
  int particle_resolution = 0;
  int sss_samples = 0;
  int sss_stability = 0;
  int shadow_map_resolution = 0;
  int shadow_map_stability = 0;
  int grass_distance = 0;
  int model_detail = 0;
  int terrain_detail = 0;
};
constexpr GraphicsPreset GRAPHICS_DEFAULT = {};
constexpr GraphicsPreset GRAPHICS_ATN = {
    .ssr_resolution = 1,
    .sss_samples = 2,
    .sss_stability = 1,
    .shadow_map_stability = 1,
    .terrain_detail = 1,
};

float SelectedRenderScale() {
  if (render_scale_choice == CUSTOM_RENDER_SCALE) return static_cast<float>(render_scale_percent) / 100.f;
  return RENDER_SCALES[render_scale_choice];
}

void OnInitDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  twshogun2::game_dlss::Install();
  twshogun2::grass_distance::Install();
  twshogun2::model_detail::Install();
  twshogun2::terrain_detail::Install();
  twshogun2::particle_resolution::Install();
  twshogun2::shadow_resolution::Install();
  twshogun2::shadow_resolution::Set(shadow_map_resolution + 1);
}

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twshogun2::frame_trace::OnPresent();
  twshogun2::terrain_detail::Apply(terrain_detail.load() == 1);
  // The game's DLSS ran since the last present: jitter for the next frames (off with any other anti-aliasing).
  const uint64_t evaluations = twshogun2::game_dlss::Evaluations();
  const bool dlss_running = (evaluations != last_evaluations);
  last_evaluations = evaluations;
  twshogun2::game_dlss::SetRunning(dlss_running);
  if (!dlss_running) {
    twshogun2::dlss_jitter::Store(0.f, 0.f);
  }
  twshogun2::game_dlss::SetJitter(jitter_enabled.load() && dlss_running);
  const float scale = twshogun2::game_dlss::UpdateFeature();
  const auto display = twshogun2::game_dlss::DisplaySize();
  const bool scaling = dlss_running && scale < 0.999f;
  const auto render = twshogun2::game_dlss::RenderSize(display[0], display[1], scale);
  if (scaling && particle_resolution.load() == 1 && display[0] != 0 && render[0] != 0) {
    twshogun2::particle_resolution::Apply(static_cast<float>(render[0]) / static_cast<float>(display[0]));
  } else {
    twshogun2::particle_resolution::Apply(1.f);
  }
  twshogun2::render_scale::BeginFrame(scaling, display, render);
  // Until the DLSS evaluation (its callback clears it): log2(render / display width), minus 1 (sharper) or 2.
  float bias = 0.f;
  if (const int choice = texture_lod_bias_choice.load(); scaling && choice != 0 && display[0] != 0) {
    bias = std::log2(static_cast<float>(render[0]) / static_cast<float>(display[0])) - static_cast<float>(choice - 1);
  }
  twshogun2::texture_lod_bias::SetTracking(queue, bias != 0.f);
  twshogun2::texture_lod_bias::SetBias(bias);
}

void OnOverlay(reshade::api::effect_runtime*) {
  if (int settings_mode = (advanced ? 1 : 0);
      ImGui::Combo("Settings mode", &settings_mode, "Simple\0Advanced (For debugging and extra information)\0")) {
    advanced = (settings_mode == 1);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SettingsMode", settings_mode);
  }
  if (ImGui::Combo("Render scale (experimental)", &render_scale_choice,
                   "100% (DLAA, the game's)\0" "77% (Ultra Quality)\0" "66.7% (Quality)\0" "58% (Balanced)\0"
                   "50% (Performance)\0" "33.3% (Ultra Performance)\0" "Custom\0")) {
    twshogun2::game_dlss::RequestScale(SelectedRenderScale());
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "RenderScale", render_scale_choice);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Renders the 3D image smaller and lets DLSS upscale it; the UI stays sharp.\n"
                      "Needs the game's Anti-aliasing on DLSS and the RenoDX HDR addon.");
  }
  if (render_scale_choice == CUSTOM_RENDER_SCALE) {
    ImGui::SliderInt("Custom render scale", &render_scale_percent, 1, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    // Applied on release: every new scale recreates the game's DLSS.
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      twshogun2::game_dlss::RequestScale(SelectedRenderScale());
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "RenderScaleCustom", render_scale_percent);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Applied when you let go. DLSS may refuse scales below 33%%.");
    }
  }
  const auto selected_preset = std::ranges::find(PRESETS, preset, &PresetChoice::value);
  if (ImGui::BeginCombo("DLSS preset", (selected_preset != PRESETS.end() ? selected_preset->label : PRESETS[1].label))) {
    for (const auto& option : PRESETS) {
      if (ImGui::Selectable(option.label, option.value == preset)) {
        preset = option.value;
        twshogun2::game_dlss::RequestPreset(preset);
        reshade::set_config_value(nullptr, "Shogun 2 DLSS", "Preset", preset);
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("For every render scale. A DLSS preset override in the NVIDIA app wins over this.");
  }
  if (int choice = texture_lod_bias_choice.load();
      ImGui::Combo("Texture LOD bias", &choice,
                   "Off (the game's own)\0" "Matched (as sharp as at your resolution)\0"
                   "Sharper (one step sharper; NVIDIA's recommendation)\0" "Sharpest (two steps sharper; can shimmer)\0")) {
    texture_lod_bias_choice.store(choice);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "TextureLodBias", choice);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Texture sharpness below 100%%. Matched: as at your resolution; Sharper / Sharpest: more.");
  }
  if (const auto display = twshogun2::game_dlss::DisplaySize(); render_scale_choice != 0 && display[0] != 0) {
    const auto render = twshogun2::game_dlss::RenderSize(display[0], display[1], SelectedRenderScale());
    ImGui::Text("Render %ux%u -> %ux%u", render[0], render[1], display[0], display[1]);
  }

  ImGui::SeparatorText("Graphics");
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.2f, 1.f));
  ImGui::TextWrapped("Above the game's highest settings: heavier. SSR and shadows need the RenoDX HDR addon.");
  ImGui::PopStyleColor();
  const auto apply_graphics = [](const GraphicsPreset& graphics) {
    ssr_resolution = graphics.ssr_resolution;
    particle_resolution.store(graphics.particle_resolution);
    sss_samples = graphics.sss_samples;
    sss_stability = graphics.sss_stability;
    shadow_map_resolution = graphics.shadow_map_resolution;
    shadow_map_stability = graphics.shadow_map_stability;
    grass_distance = graphics.grass_distance;
    model_detail = graphics.model_detail;
    terrain_detail.store(graphics.terrain_detail);
    twshogun2::grass_distance::SetScale(GRASS_DISTANCES[grass_distance].scale);
    twshogun2::model_detail::SetScale(MODEL_DETAILS[model_detail].scale);
    twshogun2::shadow_resolution::Set(shadow_map_resolution + 1);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSRResolution", ssr_resolution);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ParticleResolution", graphics.particle_resolution);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSSamples", sss_samples);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSStability", sss_stability);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapResolution", shadow_map_resolution);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapStability", shadow_map_stability);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "GrassDistance", grass_distance);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ModelDetail", model_detail);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "TerrainDetail", graphics.terrain_detail);
  };
  if (ImGui::Button("Recommended (ATN's settings)")) {
    apply_graphics(GRAPHICS_ATN);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("SSR resolution Full, Particle resolution Off, Shadow samples 4x,\n"
                      "both shadow stabilities Steadier, Terrain detail Finest everywhere; the rest default.");
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    apply_graphics(GRAPHICS_DEFAULT);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Graphics options back to their defaults.");
  }
  // Sliders over named choices (as rysesonofrome_dlss's game settings).
  if (ImGui::SliderInt("SSR resolution", &ssr_resolution, 0, static_cast<int>(SSR_RESOLUTIONS.size()) - 1,
                       SSR_RESOLUTIONS[ssr_resolution], ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSRResolution", ssr_resolution);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reflection resolution below 100%%. Full: sharper, steadier reflections. At 100%% no change.");
  }
  if (int choice = particle_resolution.load();
      ImGui::SliderInt("Particle resolution", &choice, 0, static_cast<int>(PARTICLE_RESOLUTIONS.size()) - 1,
                       PARTICLE_RESOLUTIONS[choice], ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    particle_resolution.store(choice);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ParticleResolution", choice);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Smoke and other particles below 100%%. On: as sharp as at 100%% (heavier).\n"
                      "Off: the game's own scaling (30-100%% of the render size). At 100%% no change.");
  }
  if (ImGui::SliderInt("Shadow samples (SSS)", &sss_samples, 0, static_cast<int>(SSS_SAMPLES.size()) - 1,
                       SSS_SAMPLES[sss_samples], ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSSamples", sss_samples);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reach of the screen-space shadows (game option SSS): longer contact shadows. Heavier.");
  }
  if (ImGui::SliderInt("Shadow stability (SSS)", &sss_stability, 0, static_cast<int>(SSS_STABILITIES.size()) - 1,
                       SSS_STABILITIES[sss_stability], ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSStability", sss_stability);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Less flickering screen-space shadows. Steadier: softer shadow edges.\n"
                      "Steadiest: also no shadows from thin edges (slightly thinner shadows).");
  }
  if (ImGui::SliderInt("Shadow map resolution", &shadow_map_resolution, 0,
                       static_cast<int>(SHADOW_MAP_RESOLUTIONS.size()) - 1, SHADOW_MAP_RESOLUTIONS[shadow_map_resolution],
                       ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    twshogun2::shadow_resolution::Set(shadow_map_resolution + 1);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapResolution", shadow_map_resolution);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Sharper, steadier sun shadows, with SSS on or off. Heavier; 4x uses 4 GB of video memory.\n"
                      "Applies after a restart or after changing the game's Shadows option.");
  }
  if (ImGui::SliderInt("Shadow map stability", &shadow_map_stability, 0,
                       static_cast<int>(SHADOW_MAP_STABILITIES.size()) - 1, SHADOW_MAP_STABILITIES[shadow_map_stability],
                       ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapStability", shadow_map_stability);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Less flickering self-shadowing on slopes and walls the sun grazes, with SSS on or off.\n"
                      "Steadiest: shadows start slightly further from where objects touch.");
  }
  if (ImGui::SliderInt("Grass distance", &grass_distance, 0, static_cast<int>(GRASS_DISTANCES.size()) - 1,
                       GRASS_DISTANCES[grass_distance].label, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    twshogun2::grass_distance::SetScale(GRASS_DISTANCES[grass_distance].scale);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "GrassDistance", grass_distance);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("How far grass is drawn (the game's: 450 m) x this; near grass also gets fuller.\n"
                      "Heavy on CPU and GPU above 4x.");
  }
  if (ImGui::SliderInt("Model detail distance", &model_detail, 0, static_cast<int>(MODEL_DETAILS.size()) - 1,
                       MODEL_DETAILS[model_detail].label, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    twshogun2::model_detail::SetScale(MODEL_DETAILS[model_detail].scale);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ModelDetail", model_detail);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("How far models keep their detail x this (plants, rocks, props, buildings). Less popping.");
  }
  if (int choice = terrain_detail.load();
      ImGui::SliderInt("Terrain detail (experimental)", &choice, 0, static_cast<int>(TERRAIN_DETAILS.size()) - 1,
                       TERRAIN_DETAILS[choice], ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
    terrain_detail.store(choice);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "TerrainDetail", choice);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Battle terrain detail. Finest everywhere: full detail at any distance; heavier.");
  }
  if (!advanced) return;

  ImGui::SeparatorText("Debug");
  if (bool value = jitter_enabled.load(); ImGui::Checkbox("Camera jitter (fixes the game's DLSS)", &value)) {
    jitter_enabled.store(value);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "Jitter", value);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Sub-pixel camera movement DLSS needs for fine detail (the game leaves it off). Keep on.");
  }
  if (ImGui::Combo("Jitter sign", &jitter_flip, "Game's\0X flipped\0Y flipped\0Both flipped\0")) {
    twshogun2::game_dlss::SetJitterFlip(jitter_flip);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "JitterFlip", jitter_flip);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Only if the image wobbles or looks soft with jitter on.");
  }
  ImGui::TextWrapped("The game's DLSS: %s", twshogun2::game_dlss::Status().c_str());
  if (render_scale_choice != 0) {
    twshogun2::render_scale::RequestCounts();
    const auto counts = twshogun2::render_scale::Counts();
    ImGui::TextWrapped("Last frame: %llu viewports scaled, %llu camera constants, particles at %.0f%% of the render size",
                       static_cast<unsigned long long>(counts.viewports), static_cast<unsigned long long>(counts.cameras),
                       counts.particle_scale * 100.f);
  }
  ImGui::Text("Texture LOD bias now: %.2f (game samplers seen: %zu)", twshogun2::texture_lod_bias::Bias(),
              twshogun2::texture_lod_bias::SamplerCount());
  ImGui::TextWrapped("Grass distance: %s", twshogun2::grass_distance::Status().c_str());
  ImGui::TextWrapped("Model detail distance: %s", twshogun2::model_detail::Status().c_str());
  ImGui::TextWrapped("Terrain detail: %s", twshogun2::terrain_detail::Status().c_str());
  ImGui::TextWrapped("Shadow map resolution: %s", twshogun2::shadow_resolution::Status().c_str());
  ImGui::TextWrapped("Particle resolution: %s, minimum now %.2f of the render size",
                     twshogun2::particle_resolution::Status().c_str(), twshogun2::particle_resolution::Minimum());
  if (ImGui::Button("Trace one frame (shogun2_frame_trace.txt next to the exe)")) {
    twshogun2::frame_trace::Request();
  }
  if (ImGui::Button("Capture DLSS inputs (4 frames, shogun2_dlss_capture_* next to the exe)")) {
    twshogun2::frame_trace::RequestCapture();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Saves DLSS's color, motion and depth inputs of 4 frames (about 60 MB at 33%%).");
  }
  if (const std::string status = twshogun2::frame_trace::Status(); !status.empty()) {
    ImGui::TextWrapped("%s", status.c_str());
  }
}

}  // namespace

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX Total War: SHOGUN 2 DLSS";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: SHOGUN 2 DLSS by ATN";

extern "C" __declspec(dllexport) bool AddonInit(HMODULE module, HMODULE) {
  if (!registered) {
    if (!reshade::register_addon(module)) return false;
    registered = true;
  }
  if (attached) return true;
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SettingsMode", value)) advanced = (value == 1);
  if (bool value = true; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "Jitter", value)) jitter_enabled.store(value);
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "JitterFlip", value) && value >= 0 && value <= 3) {
    jitter_flip = value;
    twshogun2::game_dlss::SetJitterFlip(value);
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "TextureLodBias", value) && value >= 0
                      && value <= 3) {
    texture_lod_bias_choice.store(value);
  }
  if (uint32_t value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "Preset", value)
                          && std::ranges::find(PRESETS, value, &PresetChoice::value) != PRESETS.end()) {
    preset = value;
  }
  twshogun2::game_dlss::RequestPreset(preset);
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "ParticleResolution", value) && value >= 0
                      && value < static_cast<int>(PARTICLE_RESOLUTIONS.size())) {
    particle_resolution.store(value);
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "TerrainDetail", value) && value >= 0
                      && value < static_cast<int>(TERRAIN_DETAILS.size())) {
    terrain_detail.store(value);
  }
  if (int value = 1; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSRResolution", value) && value >= 0
                      && value < static_cast<int>(SSR_RESOLUTIONS.size())) {
    ssr_resolution = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapResolution", value) && value >= 0
                      && value < static_cast<int>(SHADOW_MAP_RESOLUTIONS.size())) {
    shadow_map_resolution = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapStability", value) && value >= 0
                      && value < static_cast<int>(SHADOW_MAP_STABILITIES.size())) {
    shadow_map_stability = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSStability", value) && value >= 0
                      && value < static_cast<int>(SSS_STABILITIES.size())) {
    sss_stability = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSSamples", value) && value >= 0
                      && value < static_cast<int>(SSS_SAMPLES.size())) {
    sss_samples = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "GrassDistance", value) && value >= 0
                      && value < static_cast<int>(GRASS_DISTANCES.size())) {
    grass_distance = value;
  }
  twshogun2::grass_distance::SetScale(GRASS_DISTANCES[grass_distance].scale);
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "ModelDetail", value) && value >= 0
                      && value < static_cast<int>(MODEL_DETAILS.size())) {
    model_detail = value;
  }
  twshogun2::model_detail::SetScale(MODEL_DETAILS[model_detail].scale);
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "RenderScaleCustom", value) && value >= 1
                      && value <= 100) {
    render_scale_percent = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "RenderScale", value) && value >= 0
                      && value <= CUSTOM_RENDER_SCALE) {
    render_scale_choice = value;
    twshogun2::game_dlss::RequestScale(SelectedRenderScale());
  }
  twshogun2::game_dlss::SetEvaluateCallback([](ID3D11DeviceContext* context, const NVSDK_NGX_D3D11_DLSS_Eval_Params& eval) {
    twshogun2::render_scale::EndScaling();
    twshogun2::texture_lod_bias::SetBias(0.f);
    twshogun2::frame_trace::OnEvaluate(context, eval);
    twshogun2::dlss_jitter::Store(eval.InJitterOffsetX, eval.InJitterOffsetY);
  });
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::bind_viewports>(twshogun2::render_scale::OnBindViewports);
  // After render_scale's (it decides the main view).
  reshade::register_event<reshade::addon_event::bind_viewports>(twshogun2::texture_lod_bias::OnBindViewports);
  reshade::register_event<reshade::addon_event::bind_scissor_rects>(twshogun2::render_scale::OnBindScissorRects);
  reshade::register_event<reshade::addon_event::map_buffer_region>(twshogun2::render_scale::OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(twshogun2::render_scale::OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::init_sampler>(twshogun2::texture_lod_bias::OnInitSampler);
  reshade::register_event<reshade::addon_event::destroy_sampler>(twshogun2::texture_lod_bias::OnDestroySampler);
  reshade::register_event<reshade::addon_event::push_descriptors>(twshogun2::texture_lod_bias::OnPushDescriptors);
  reshade::register_overlay("Shogun 2 DLSS", OnOverlay);
  renodx::utils::state::Use(DLL_PROCESS_ATTACH);
  twshogun2::dlss_jitter::Use(DLL_PROCESS_ATTACH);
  attached = true;
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE) {
  if (attached) {
    twshogun2::frame_trace::Stop();
    renodx::utils::state::Use(DLL_PROCESS_DETACH);
    twshogun2::dlss_jitter::Use(DLL_PROCESS_DETACH);
    reshade::unregister_overlay("Shogun 2 DLSS", OnOverlay);
    reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
    reshade::unregister_event<reshade::addon_event::present>(OnPresent);
    reshade::unregister_event<reshade::addon_event::bind_viewports>(twshogun2::render_scale::OnBindViewports);
    reshade::unregister_event<reshade::addon_event::bind_viewports>(twshogun2::texture_lod_bias::OnBindViewports);
    reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(twshogun2::render_scale::OnBindScissorRects);
    reshade::unregister_event<reshade::addon_event::map_buffer_region>(twshogun2::render_scale::OnMapBufferRegion);
    reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(twshogun2::render_scale::OnUnmapBufferRegion);
    reshade::unregister_event<reshade::addon_event::init_sampler>(twshogun2::texture_lod_bias::OnInitSampler);
    reshade::unregister_event<reshade::addon_event::destroy_sampler>(twshogun2::texture_lod_bias::OnDestroySampler);
    reshade::unregister_event<reshade::addon_event::push_descriptors>(twshogun2::texture_lod_bias::OnPushDescriptors);
    twshogun2::game_dlss::SetEvaluateCallback(nullptr);
    twshogun2::game_dlss::Uninstall();
    twshogun2::grass_distance::Uninstall();
    twshogun2::model_detail::Uninstall();
    twshogun2::particle_resolution::Apply(1.f);
    attached = false;
  }
  if (registered) {
    reshade::unregister_addon(module);
    registered = false;
  }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    registered = reshade::register_addon(module);
    return registered ? TRUE : FALSE;
  }
  if (reason == DLL_PROCESS_DETACH && reserved == nullptr && registered) {
    reshade::unregister_addon(module);
    registered = false;
  }
  return TRUE;
}
