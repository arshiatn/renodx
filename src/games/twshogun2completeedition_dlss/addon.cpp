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
#include <shared_mutex>
#include <string>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/icons.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/state.hpp"
#include "./dlss_jitter.hpp"
#include "./frame_trace.hpp"
#include "./game_dlss.hpp"
#include "./grass_distance.hpp"
#include "./model_detail.hpp"
#include "./object_velocity.hpp"
#include "./particle_resolution.hpp"
#include "./render_scale.hpp"
#include "./shadow_resolution.hpp"
#include "./terrain_detail.hpp"
#include "../twattila/fg_camera.hpp"
#include "../twattila/texture_lod_bias.hpp"
#include "./unit_detail.hpp"
#include "../talesofarise_dlss/frame_generation.hpp"

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
// v50: motion vectors for ships, sails, ropes and water (object_velocity.hpp: Attila's object motion).
// v51: Sun shadow softness (the HDR addon's PCSS), the object motion's target half the size.

namespace {

using Microsoft::WRL::ComPtr;

bool registered = false;
bool attached = false;
bool advanced = false;  // overlay: Settings mode Advanced (ReShade.ini SettingsMode)
// DLSS Frame Generation (Experimental, Tales' frame_generation.hpp): set at launch (config FrameGeneration).
bool frame_generation = false;
// Frame generation's inputs from the game's DLSS evaluation, submitted at the next present (`frame`: presents so far),
// and the camera's motion between frames.
struct FrameGenerationFrame {
  std::shared_mutex mutex;
  talesofarise::frame_generation::Inputs inputs;
  uint64_t frame = UINT64_MAX;
  twattila::fg_camera::Tracker camera;
} frame_generation_frame;
std::atomic<uint64_t> presents = 0;
// The HDR addon's tonemappers (display size, after DLSS): frame generation's hudless colour after them.
constexpr std::array<uint32_t, 2> TONEMAPPER_SHADERS = {0xEF060C7Cu, 0xBEDBEFD8u};

// Frame generation's inputs from the game's DLSS evaluation and the main view's camera (camera_VS_PS).
void StoreFrameGeneration(const NVSDK_NGX_D3D11_DLSS_Eval_Params& eval) {
  std::array<float, 44 * 4> c;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11Texture2D> motion;
  if (!twshogun2::render_scale::MainCamera(&c) || eval.pInDepth == nullptr || eval.pInMotionVectors == nullptr
      || FAILED(eval.pInDepth->QueryInterface(IID_PPV_ARGS(depth.GetAddressOf())))
      || FAILED(eval.pInMotionVectors->QueryInterface(IID_PPV_ARGS(motion.GetAddressOf())))) {
    return;
  }
  const std::unique_lock lock(frame_generation_frame.mutex);
  frame_generation_frame.inputs = {
      .depth = depth,
      .motion = motion,
      .render_width = eval.InRenderSubrectDimensions.Width,
      .render_height = eval.InRenderSubrectDimensions.Height,
      .jitter = {eval.InJitterOffsetX, eval.InJitterOffsetY},
      .motion_scale = {eval.InMVScaleX, eval.InMVScaleY},
      .reset = eval.InReset != 0,
      .camera = frame_generation_frame.camera.Next(c.data()),
  };
  frame_generation_frame.frame = presents.load();
}

// After the tonemapper's draw (the display size): frame generation's hudless colour from its target.
constexpr auto OnTonemapper = []<typename Context>(Context&) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!talesofarise::frame_generation::Presenting()) return {};
  return {.post_callback = [](Context& command, const void*) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    if (context == nullptr) return;
    ComPtr<ID3D11RenderTargetView> target;
    context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
    talesofarise::frame_generation::DrawHudless(context, target.Get(), presents.load());
  }};
};
// ReShade.ini [Shogun 2 DLSS]: Jitter (on), JitterFlip (debug, 0).
std::atomic_bool jitter_enabled = true;
int jitter_flip = 0;           // overlay
uint64_t last_evaluations = 0;  // present thread
// Render scale (ReShade.ini [Shogun 2 DLSS] RenderScale: index into RENDER_SCALES, or CUSTOM_RENDER_SCALE for
// RenderScaleCustom in percent).
constexpr std::array<float, 6> RENDER_SCALES = {1.f, 0.77f, 2.f / 3.f, 0.58f, 0.5f, 1.f / 3.f};
constexpr int CUSTOM_RENDER_SCALE = static_cast<int>(RENDER_SCALES.size());
int render_scale_choice = 0;    // overlay
constexpr int DEFAULT_RENDER_SCALE_PERCENT = 50;
int render_scale_percent = DEFAULT_RENDER_SCALE_PERCENT;  // overlay, custom
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
// ReShade.ini ShadowMapResolution: the sun's shadow map at the highest Shadows setting (shadow_resolution.hpp; 0: the
// game's). 14336 made the game quit too (ATN).
constexpr std::array<const char*, 3> SHADOW_MAP_RESOLUTIONS = {"4096 (Game's)", "8192", "12288"};
constexpr std::array<uint32_t, 3> SHADOW_MAP_SIZES = {0, 8192, 12288};
int shadow_map_resolution = 0;  // overlay
// ReShade.ini ShadowMapStability (HDR addon): the cascades' receivers off their surface, 0 the game's, 1 / 2 further.
constexpr std::array<const char*, 3> SHADOW_MAP_STABILITIES = {"Game", "Steadier", "Steadiest"};
int shadow_map_stability = 0;  // overlay
// ReShade.ini SunShadowSoftness (HDR addon, pcss.hlsli): contact-hardening sun shadows, the sun's size in degrees; 0 the
// game's filter. In no preset (ATN removed the uniform shadow softness: this one changes the look too).
float sun_shadow_softness = 0.f;  // overlay
// Ambient Occlusion (GT-VBAO) and Global Illumination (GT-VBGI, the Light Bounce): the HDR addon's
// (twattila/lighting_settings.hpp's settings; it reads these from ReShade.ini). Defaults: ATN's recommended presets
// (Global Illumination off).
constexpr std::array<const char*, 2> AO_MODES = {"Game's", "GT-VBAO"};
constexpr std::array<const char*, 6> SAMPLES = {"Normal (8)", "High (16)", "Ultra (32)", "64", "128", "256"};
constexpr std::array<const char*, 4> AO_DIRECTIONS = {"1", "2", "4", "8"};
constexpr std::array<const char*, 2> AO_RESOLUTIONS = {"Full", "Half"};
constexpr std::array<const char*, 2> OFF_ON = {"Off", "On"};
struct AmbientOcclusion {
  int mode = 1;               // index into AO_MODES
  float strength = 1.f;       // an exponent
  float radius = 12.5f;       // metres
  float thickness = 0.1f;     // metres
  int quality = 2;            // index into SAMPLES
  int directions = 0;         // index into AO_DIRECTIONS
  int resolution = 0;         // index into AO_RESOLUTIONS
  int auto_thickness = 1;     // index into OFF_ON
};
constexpr AmbientOcclusion AO_DEFAULT = {};
AmbientOcclusion ao;  // overlay
constexpr std::array<const char*, 4> GI_RESOLUTIONS = {"Full", "Half", "Quarter", "Eighth"};
constexpr std::array<const char*, 5> GI_DIRECTIONS = {"1", "2", "4", "8", "16"};
struct GlobalIllumination {
  float strength = 0.f;      // 0: off
  float radius = 20.f;       // metres
  int resolution = 2;        // index into GI_RESOLUTIONS
  int quality = 3;           // index into SAMPLES
  int directions = 0;        // index into GI_DIRECTIONS
  int temporal = 1;          // Temporal Smoothing: index into OFF_ON
  int denoise = 1;           // index into OFF_ON
  float multi_bounce = 0.f;  // 0: one bounce
  int checkerboard = 1;      // index into OFF_ON
  int backface = 0;          // index into OFF_ON
  int auto_thickness = 1;    // index into OFF_ON
};
constexpr GlobalIllumination GI_DEFAULT = {};
GlobalIllumination gi;  // overlay
// ATN's presets (as the other Total War games'): ambient occlusion Faster / Heavier (Recommended) / Reset (the game's),
// Global Illumination Faster (Recommended) / Heavier (More accurate) / Reset (off).
constexpr AmbientOcclusion AO_FASTER = {.radius = 5.f, .quality = 1, .resolution = 1};
constexpr AmbientOcclusion AO_RESET = {.mode = 0};
constexpr GlobalIllumination GI_FASTER = {.strength = 1.f};
constexpr GlobalIllumination GI_HEAVIER = {.strength = 1.f, .resolution = 1, .quality = 2};
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
// ReShade.ini UnitDetail / UnitAnimation / UnitVariety: indices (unit_detail.hpp).
constexpr std::array<ScaleChoice, 7> UNIT_DETAILS = {{
    {.scale = 1.f, .label = "Game"},
    {.scale = 1.5f, .label = "1.5x"},
    {.scale = 2.f, .label = "2x"},
    {.scale = 3.f, .label = "3x"},
    {.scale = 4.f, .label = "4x"},
    {.scale = 6.f, .label = "6x"},
    {.scale = 8.f, .label = "8x"},
}};
constexpr std::array<ScaleChoice, 5> UNIT_ANIMATIONS = {{
    {.scale = 1.f, .label = "Game"},
    {.scale = 1.5f, .label = "1.5x"},
    {.scale = 2.f, .label = "2x"},
    {.scale = 3.f, .label = "3x"},
    {.scale = 4.f, .label = "4x"},
}};
int unit_detail = 0;     // overlay
int unit_animation = 0;  // overlay
int unit_variety = 0;    // overlay, UNIT_ANIMATIONS' choices
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
  int unit_detail = 0;
  int unit_animation = 0;
  int unit_variety = 0;
};
constexpr GraphicsPreset GRAPHICS_DEFAULT = {};
constexpr GraphicsPreset GRAPHICS_ATN = {
    .ssr_resolution = 1,
    .sss_samples = 2,
    .sss_stability = 1,
    .shadow_map_stability = 1,
    .terrain_detail = 1,
};

void ApplyUnitDetail() {
  twshogun2::unit_detail::SetScales(UNIT_DETAILS[unit_detail].scale, UNIT_ANIMATIONS[unit_animation].scale,
                                    UNIT_ANIMATIONS[unit_variety].scale);
}

float SelectedRenderScale() {
  if (render_scale_choice == CUSTOM_RENDER_SCALE) return static_cast<float>(render_scale_percent) / 100.f;
  return RENDER_SCALES[render_scale_choice];
}

void OnInitDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  twshogun2::game_dlss::Install();
  twshogun2::grass_distance::Install();
  twshogun2::model_detail::Install();
  twshogun2::unit_detail::Install();
  twshogun2::terrain_detail::Install();
  twshogun2::particle_resolution::Install();
  twshogun2::shadow_resolution::Install();
  twshogun2::shadow_resolution::Set(SHADOW_MAP_SIZES[shadow_map_resolution]);
}

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twshogun2::frame_trace::OnPresent();
  twshogun2::terrain_detail::Apply(terrain_detail.load() == 1);
  if (talesofarise::frame_generation::Presenting()) {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(queue->get_immediate_command_list()->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    const uint64_t frame = presents.load();
    talesofarise::frame_generation::Inputs inputs;
    {
      const std::shared_lock lock(frame_generation_frame.mutex);
      if (frame_generation_frame.frame == frame) inputs = frame_generation_frame.inputs;
    }
    talesofarise::frame_generation::Submit(context, inputs, frame);
  }
  ++presents;
  // The game's DLSS ran since the last present: jitter for the next frames (off with any other anti-aliasing).
  const uint64_t evaluations = twshogun2::game_dlss::Evaluations();
  const bool dlss_running = (evaluations != last_evaluations);
  last_evaluations = evaluations;
  twshogun2::game_dlss::SetRunning(dlss_running);
  twshogun2::object_velocity::OnPresent(dlss_running);
  if (!dlss_running) {
    twshogun2::dlss_jitter::Store(0.f, 0.f);
  }
  twshogun2::game_dlss::SetJitter(jitter_enabled.load() && dlss_running);
  const float scale = twshogun2::game_dlss::UpdateFeature();
  const auto display = twshogun2::game_dlss::DisplaySize();
  const bool scaling = dlss_running && scale < 0.999f;
  const auto render = twattila::frame_history::AspectSize(display[0], display[1], scale);
  if (scaling && particle_resolution.load() == 1 && display[0] != 0 && render[0] != 0) {
    twshogun2::particle_resolution::Apply(static_cast<float>(render[0]) / static_cast<float>(display[0]));
  } else {
    twshogun2::particle_resolution::Apply(1.f);
  }
  twshogun2::render_scale::BeginFrame(scaling, display, render, talesofarise::frame_generation::Presenting() && dlss_running);
  // Until the DLSS evaluation (its callback clears it): log2(render / display width), minus 1 (sharper) or 2.
  float bias = 0.f;
  if (const int choice = texture_lod_bias_choice.load(); scaling && choice != 0 && display[0] != 0) {
    bias = std::log2(static_cast<float>(render[0]) / static_cast<float>(display[0])) - static_cast<float>(choice - 1);
  }
  twattila::texture_lod_bias::SetTracking(queue, bias != 0.f);
  twattila::texture_lod_bias::SetBias(bias);
}

void OnOverlay(reshade::api::effect_runtime*) {
  // Section headings (ATN): DLSS lavender, Graphics pink, ambient occlusion blue, Global Illumination orange, the rest
  // green (the HDR addon's titles).
  const auto heading = [](const char* label, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
  };
  constexpr ImU32 DLSS_COLOR = IM_COL32(0xB3, 0x9D, 0xDB, 0xFF);
  constexpr ImU32 GRAPHICS_COLOR = IM_COL32(0xFF, 0x7F, 0xAF, 0xFF);
  constexpr ImU32 AO_COLOR = IM_COL32(0x7F, 0xBF, 0xFF, 0xFF);  // ambient occlusion (ATN)
  constexpr ImU32 GI_COLOR = IM_COL32(0xFF, 0xB8, 0x6B, 0xFF);  // Global Illumination
  constexpr ImU32 RT_COLOR = IM_COL32(0x6B, 0xE0, 0xD8, 0xFF);  // ray tracing
  constexpr ImU32 OTHER_COLOR = IM_COL32(0x7E, 0xE0, 0x8A, 0xFF);
  // The HDR addons' reset button after a slider (gray while at the default); true when pressed.
  const auto reset_button = [](const char* id, bool at_default) {
    ImGui::SameLine();
    ImGui::BeginDisabled(at_default);
    if (at_default) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImColor::HSV(0, 0, 0.6f)));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImColor::HSV(0, 0, 0.7f)));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ImColor::HSV(0, 0, 0.8f)));
    }
    auto* font = ImGui::GetFont();
    const float old_scale = font->Scale;
    const float previous_font_size = ImGui::GetFontSize();
    font->Scale *= 0.75f;
    ImGui::PushFont(font);
    const float font_size = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, font_size * 2);
    ImVec2 cursor = ImGui::GetCursorPos();
    cursor.y += (previous_font_size - font_size) / 2.f;
    ImGui::SetCursorPos(cursor);
    ImGui::PushID(id);
    const bool pressed = ImGui::Button(renodx::utils::icons::View(renodx::utils::icons::UNDO));
    ImGui::PopID();
    ImGui::PopStyleVar();
    font->Scale = old_scale;
    ImGui::PopFont();
    if (at_default) ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    return pressed;
  };
  // A slider over named choices with its tooltip and reset button; true when changed.
  const auto choice_slider = [&](const char* label, int* value, size_t count, const char* name, int default_value,
                                 const char* tooltip) {
    bool changed = ImGui::SliderInt(label, value, 0, static_cast<int>(count) - 1, name,
                                    ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    if (reset_button(label, *value == default_value)) {
      *value = default_value;
      changed = true;
    }
    return changed;
  };
  const auto float_slider = [&](const char* label, float* value, float min, float max, const char* format,
                                float default_value, const char* tooltip) {
    bool changed = ImGui::SliderFloat(label, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    if (reset_button(label, *value == default_value)) {
      *value = default_value;
      changed = true;
    }
    return changed;
  };
  if (int settings_mode = (advanced ? 1 : 0);
      ImGui::Combo("Settings mode", &settings_mode, "Simple\0Advanced (For debugging and extra information)\0")) {
    advanced = (settings_mode == 1);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SettingsMode", settings_mode);
  }
  heading("DLSS", DLSS_COLOR);
  ImGui::PushStyleColor(ImGuiCol_Text, DLSS_COLOR);
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
    bool apply = ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Applied when you let go. DLSS may refuse scales below 33%%.");
    }
    if (reset_button("Custom render scale", render_scale_percent == DEFAULT_RENDER_SCALE_PERCENT)) {
      render_scale_percent = DEFAULT_RENDER_SCALE_PERCENT;
      apply = true;
    }
    if (apply) {
      twshogun2::game_dlss::RequestScale(SelectedRenderScale());
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "RenderScaleCustom", render_scale_percent);
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
  talesofarise::frame_generation::DrawSettings("Shogun 2 DLSS", &frame_generation);
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
    const auto render = twattila::frame_history::AspectSize(display[0], display[1], SelectedRenderScale());
    ImGui::Text("Render %ux%u -> %ux%u", render[0], render[1], display[0], display[1]);
  }
  ImGui::PopStyleColor();

  heading("Graphics", GRAPHICS_COLOR);
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
    unit_detail = graphics.unit_detail;
    unit_animation = graphics.unit_animation;
    unit_variety = graphics.unit_variety;
    ApplyUnitDetail();
    twshogun2::grass_distance::SetScale(GRASS_DISTANCES[grass_distance].scale);
    twshogun2::model_detail::SetScale(MODEL_DETAILS[model_detail].scale);
    twshogun2::shadow_resolution::Set(SHADOW_MAP_SIZES[shadow_map_resolution]);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSRResolution", ssr_resolution);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ParticleResolution", graphics.particle_resolution);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSSamples", sss_samples);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSStability", sss_stability);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapResolution", shadow_map_resolution);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapStability", shadow_map_stability);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "GrassDistance", grass_distance);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ModelDetail", model_detail);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "TerrainDetail", graphics.terrain_detail);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "UnitDetail", unit_detail);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "UnitAnimation", unit_animation);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "UnitVariety", unit_variety);
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
  if (choice_slider("SSR resolution", &ssr_resolution, SSR_RESOLUTIONS.size(), SSR_RESOLUTIONS[ssr_resolution],
                    GRAPHICS_DEFAULT.ssr_resolution,
                    "Reflection resolution below 100%. Full: sharper, steadier reflections. At 100% no change.")) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSRResolution", ssr_resolution);
  }
  if (int choice = particle_resolution.load();
      choice_slider("Particle resolution", &choice, PARTICLE_RESOLUTIONS.size(), PARTICLE_RESOLUTIONS[choice],
                    GRAPHICS_DEFAULT.particle_resolution,
                    "Smoke and other particles below 100%. On: as sharp as at 100% (heavier).\n"
                    "Off: the game's own scaling (30-100% of the render size). At 100% no change.")) {
    particle_resolution.store(choice);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ParticleResolution", choice);
  }
  if (choice_slider("Shadow samples (SSS)", &sss_samples, SSS_SAMPLES.size(), SSS_SAMPLES[sss_samples],
                    GRAPHICS_DEFAULT.sss_samples,
                    "Reach of the screen-space shadows (game option SSS): longer contact shadows. Heavier.")) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSSamples", sss_samples);
  }
  if (choice_slider("Shadow stability (SSS)", &sss_stability, SSS_STABILITIES.size(), SSS_STABILITIES[sss_stability],
                    GRAPHICS_DEFAULT.sss_stability,
                    "Less flickering screen-space shadows. Steadier: softer shadow edges.\n"
                    "Steadiest: also no shadows from thin edges (slightly thinner shadows).")) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSStability", sss_stability);
  }
  if (choice_slider("Shadow map resolution", &shadow_map_resolution, SHADOW_MAP_RESOLUTIONS.size(),
                    SHADOW_MAP_RESOLUTIONS[shadow_map_resolution], GRAPHICS_DEFAULT.shadow_map_resolution,
                    "Sharper, steadier sun shadows at the highest Shadows setting, with SSS on or off. Heavier;\n"
                    "12288 uses about 2.3 GB of video memory. "
                    "Applies after a restart or after changing the game's Shadows option.")) {
    twshogun2::shadow_resolution::Set(SHADOW_MAP_SIZES[shadow_map_resolution]);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapResolution", shadow_map_resolution);
  }
  if (choice_slider("Shadow map stability", &shadow_map_stability, SHADOW_MAP_STABILITIES.size(),
                    SHADOW_MAP_STABILITIES[shadow_map_stability], GRAPHICS_DEFAULT.shadow_map_stability,
                    "Less flickering self-shadowing on slopes and walls the sun grazes, with SSS on or off.\n"
                    "Steadiest: shadows start slightly further from where objects touch.")) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ShadowMapStability", shadow_map_stability);
  }
  if (float_slider("Sun shadow softness (PCSS, experimental)", &sun_shadow_softness, 0.f, 5.f,
                   (sun_shadow_softness == 0.f ? "Game's" : "%.2f deg"), 0.f,
                   "Contact-hardening sun shadows: sharp where objects touch the ground, softer the further they are.\n"
                   "The sun's size in degrees (the real sun: 0.5). Heavier than the game's filter.")) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SunShadowSoftness", sun_shadow_softness);
  }
  if (choice_slider("Grass distance", &grass_distance, GRASS_DISTANCES.size(), GRASS_DISTANCES[grass_distance].label,
                    GRAPHICS_DEFAULT.grass_distance,
                    "How far grass is drawn (the game's: 450 m) x this; near grass also gets fuller.\n"
                    "Heavy on CPU and GPU above 4x.")) {
    twshogun2::grass_distance::SetScale(GRASS_DISTANCES[grass_distance].scale);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "GrassDistance", grass_distance);
  }
  if (choice_slider("Model detail distance", &model_detail, MODEL_DETAILS.size(), MODEL_DETAILS[model_detail].label,
                    GRAPHICS_DEFAULT.model_detail,
                    "How far models keep their detail x this (plants, rocks, props, buildings). Less popping.")) {
    twshogun2::model_detail::SetScale(MODEL_DETAILS[model_detail].scale);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ModelDetail", model_detail);
  }
  if (choice_slider("Unit detail distance", &unit_detail, UNIT_DETAILS.size(), UNIT_DETAILS[unit_detail].label,
                    GRAPHICS_DEFAULT.unit_detail,
                    "How far units stay 3D before turning into flat sprites. Heavier for the CPU and GPU.")) {
    ApplyUnitDetail();
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "UnitDetail", unit_detail);
  }
  if (choice_slider("Unit animation distance", &unit_animation, UNIT_ANIMATIONS.size(),
                    UNIT_ANIMATIONS[unit_animation].label, GRAPHICS_DEFAULT.unit_animation,
                    "How far soldiers move on their own, with faces (beyond it they copy their neighbours'\n"
                    "moves). Heavier for the CPU.")) {
    ApplyUnitDetail();
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "UnitAnimation", unit_animation);
  }
  if (choice_slider("Unit variety distance", &unit_variety, UNIT_ANIMATIONS.size(), UNIT_ANIMATIONS[unit_variety].label,
                    GRAPHICS_DEFAULT.unit_variety,
                    "How far soldiers keep their own heads and gear (beyond it they look alike). Heavier.")) {
    ApplyUnitDetail();
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "UnitVariety", unit_variety);
  }
  if (int choice = terrain_detail.load();
      choice_slider("Terrain detail (experimental)", &choice, TERRAIN_DETAILS.size(), TERRAIN_DETAILS[choice],
                    GRAPHICS_DEFAULT.terrain_detail,
                    "Battle terrain detail. Finest everywhere: full detail at any distance; heavier.")) {
    terrain_detail.store(choice);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "TerrainDetail", choice);
  }
  // Two empty lines between the graphics and the ambient occlusion (ATN).
  ImGui::NewLine();
  ImGui::NewLine();
  heading("Ambient Occlusion (GT-VBAO, Experimental, needs in-game SSAO)", AO_COLOR);
  // Every setting of a section into ReShade.ini (after a slider or a preset).
  const auto save_ao = [] {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusion", ao.mode);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionStrength", ao.strength);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionRadius", ao.radius);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionThickness", ao.thickness);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionQuality", ao.quality);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionDirections", ao.directions);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionResolution", ao.resolution);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionAutoThickness", ao.auto_thickness);
  };
  const auto save_gi = [] {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceStrength", gi.strength);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceRadius", gi.radius);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceResolution", gi.resolution);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceQuality", gi.quality);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceDirections", gi.directions);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceTemporal", gi.temporal);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceDenoise", gi.denoise);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceMultiBounce", gi.multi_bounce);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceCheckerboard", gi.checkerboard);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceBackFace", gi.backface);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "LightBounceAutoThickness", gi.auto_thickness);
  };
  // A preset button in its section's colour; true when pressed.
  const auto preset_button = [](const char* label, ImU32 color, const char* tooltip) {
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(color);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(base.x * 0.45f, base.y * 0.45f, base.z * 0.45f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(base.x * 0.6f, base.y * 0.6f, base.z * 0.6f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(base.x * 0.75f, base.y * 0.75f, base.z * 0.75f, 1.f));
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
  };
  ImGui::TextColored(ImVec4(1.f, 0.82f, 0.24f, 1.f),
                     "Darker shading behind trees and objects: raise Thickness. Lighter around spears: lower it.");
  if (preset_button("Faster##ao", AO_COLOR, "Radius 5 m, Quality High, half resolution.")) {
    ao = AO_FASTER;
    save_ao();
  }
  ImGui::SameLine();
  if (preset_button("Heavier (Recommended)##ao", AO_COLOR, "Radius 12.5 m, Quality Ultra.")) {
    ao = AO_DEFAULT;
    save_ao();
  }
  ImGui::SameLine();
  if (preset_button("Reset##ao", AO_COLOR, "The game's ambient occlusion, this section's sliders at their defaults.")) {
    ao = AO_RESET;
    save_ao();
  }
  bool ao_changed = choice_slider("Ambient Occlusion", &ao.mode, AO_MODES.size(), AO_MODES[ao.mode], AO_DEFAULT.mode,
                                  "Soft shadows in corners and where objects meet the ground. Needs the RenoDX HDR addon.\n"
                                  "Game's: everything counts as solid, so soldiers and trees get dark halos.\n"
                                  "GT-VBAO: objects have a thickness, so light gets behind thin ones.");
  ImGui::BeginDisabled(ao.mode == 0);
  ao_changed |= float_slider("Strength", &ao.strength, 0.f, 3.f, "%.2f", AO_DEFAULT.strength,
                             "How dark it gets. 1: physically correct; higher: darker.");
  ao_changed |= float_slider("Radius", &ao.radius, 0.25f, 20.f, "%.2f m", AO_DEFAULT.radius,
                             "How far it reaches. Larger: wider shading, heavier (raise Quality with it).");
  ao_changed |= float_slider("Thickness", &ao.thickness, 0.01f, 3.f, "%.2f m", AO_DEFAULT.thickness,
                             "How thick objects are taken to be (Global Illumination too).\n"
                             "Lower: light gets behind thin things; higher: darker behind them.");
  ao_changed |= choice_slider("Quality", &ao.quality, SAMPLES.size(), SAMPLES[ao.quality], AO_DEFAULT.quality,
                              "Samples per direction. More: thin things still count at a large Radius. Heavier.");
  ao_changed |= choice_slider("Directions", &ao.directions, AO_DIRECTIONS.size(), AO_DIRECTIONS[ao.directions],
                              AO_DEFAULT.directions, "Directions searched around each pixel. More: less noise, heavier.");
  ao_changed |= choice_slider("Resolution", &ao.resolution, AO_RESOLUTIONS.size(), AO_RESOLUTIONS[ao.resolution],
                              AO_DEFAULT.resolution,
                              "Lower: faster, softer; thin things (grass, spears) get less shading.");
  ao_changed |= choice_slider("Auto Thickness (Experimental)", &ao.auto_thickness, OFF_ON.size(),
                              OFF_ON[ao.auto_thickness], AO_DEFAULT.auto_thickness,
                              "Guesses each object's thickness from its width, Thickness as the least:\n"
                              "spears stay thin, walls and hills block fully.");
  ImGui::EndDisabled();
  if (ao_changed) save_ao();
  ImGui::NewLine();
  heading("Global Illumination (GT-VBGI, Experimental, no in-game SSAO needed)", GI_COLOR);
  if (preset_button("Faster (Recommended)##gi", GI_COLOR, "Strength 1, Radius 20 m, quarter resolution, 64 samples.")) {
    gi = GI_FASTER;
    save_gi();
  }
  ImGui::SameLine();
  if (preset_button("Heavier (More accurate)##gi", GI_COLOR, "Strength 1, Radius 20 m, half resolution, Quality Ultra.")) {
    gi = GI_HEAVIER;
    save_gi();
  }
  ImGui::SameLine();
  if (preset_button("Reset##gi", GI_COLOR, "Off, this section's sliders at their defaults.")) {
    gi = GI_DEFAULT;
    save_gi();
  }
  bool gi_changed = float_slider("Strength##gi", &gi.strength, 0.f, 4.f, "%.2f", GI_DEFAULT.strength,
                                 "Sunlit ground, walls and soldiers bounce their light and colour into nearby shadows.\n"
                                 "1: physically correct; 0: off. Needs the RenoDX HDR addon.");
  ImGui::BeginDisabled(!(gi.strength > 0.f));
  gi_changed |= float_slider("Radius##gi", &gi.radius, 0.5f, 20.f, "%.2f m", GI_DEFAULT.radius,
                             "How far bounced light reaches. Larger: heavier (raise Quality with it).");
  gi_changed |= choice_slider("Resolution##gi", &gi.resolution, GI_RESOLUTIONS.size(), GI_RESOLUTIONS[gi.resolution],
                              GI_DEFAULT.resolution, "Lower: much faster, softer.");
  gi_changed |= choice_slider("Quality##gi", &gi.quality, SAMPLES.size(), SAMPLES[gi.quality], GI_DEFAULT.quality,
                              "Samples per direction. More: small lit things still count at a large Radius. Heavier.");
  gi_changed |= choice_slider("Directions##gi", &gi.directions, GI_DIRECTIONS.size(), GI_DIRECTIONS[gi.directions],
                              GI_DEFAULT.directions, "Directions searched around each pixel. More: less noise, heavier.");
  gi_changed |= choice_slider("Temporal Smoothing##gi", &gi.temporal, OFF_ON.size(), OFF_ON[gi.temporal],
                              GI_DEFAULT.temporal,
                              "Averages the last frames: much less noise. Off: the noise stands still.");
  gi_changed |= choice_slider("Denoiser##gi", &gi.denoise, OFF_ON.size(), OFF_ON[gi.denoise], GI_DEFAULT.denoise,
                              "Smooths the noise along surfaces, never across edges. Cleaner, a bit softer.");
  gi_changed |= float_slider("Multi-Bounce##gi", &gi.multi_bounce, 0.f, 1.f, "%.2f", GI_DEFAULT.multi_bounce,
                             "Bounced light bounces again: fuller light in deep shadows and narrow streets. Almost free.");
  ImGui::BeginDisabled(gi.temporal == 0);
  gi_changed |= choice_slider("Checkerboard##gi", &gi.checkerboard, OFF_ON.size(), OFF_ON[gi.checkerboard],
                              GI_DEFAULT.checkerboard,
                              "Half the screen traced each frame, the rest kept from the last frames.\n"
                              "About 2x faster; can lag a little behind fast motion.");
  ImGui::EndDisabled();
  gi_changed |= choice_slider("Back-Face Rejection (Experimental)##gi", &gi.backface, OFF_ON.size(), OFF_ON[gi.backface],
                              GI_DEFAULT.backface, "Ignores light from surfaces seen from behind. Heavier; not always better.");
  gi_changed |= choice_slider("Auto Thickness (Experimental)##gi", &gi.auto_thickness, OFF_ON.size(),
                              OFF_ON[gi.auto_thickness], GI_DEFAULT.auto_thickness,
                              "Guesses each object's thickness from its width, Thickness as the least:\n"
                              "spears stay thin, walls and hills block fully.");
  ImGui::EndDisabled();
  if (gi_changed) save_gi();
  heading("Ray Tracing (Experimental, restart after turning on)", RT_COLOR);
  {
    // ReShade.ini [Shogun 2 DLSS] RayTraced* / PathTracing*: the HDR addon's ray tracing (its ray_tracing.hpp).
    const auto checkbox = [](const char* label, const char* key, const char* tooltip) {
      bool value = false;
      reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
      if (ImGui::Checkbox(label, &value)) reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, value);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
      return value;
    };
    const auto slider = [](const char* label, const char* key, float default_value, float min, float max, const char* format,
                           const char* tooltip) {
      float value = default_value;
      reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
      if (ImGui::SliderFloat(label, &value, min, max, format, ImGuiSliderFlags_AlwaysClamp)) {
        reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, value);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    };
    const auto count = [](const char* label, const char* key, int default_value, int max, const char* tooltip) {
      int value = default_value;
      reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
      if (ImGui::SliderInt(label, &value, 1, max, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, value);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    };
    ImGui::TextWrapped("Needs an RTX GPU and renodx-tw-dlaa64.exe next to the addons.");
    const bool sun_shadows = checkbox("Ray-traced sun shadows", "RayTracedSunShadows",
                                      "Sun shadows from rays, with the game's: also where its shadow maps miss things or end.\n"
                                      "Faster: the game's Screen Space Shadows off, the rays then alone (grass casts no contact shadows).");
    ImGui::BeginDisabled(!sun_shadows);
    slider("Ray-traced shadow softness", "RayTracedShadowSoftness", 0.5f, 0.f, 3.f, "%.2f",
           "The sun's size in degrees: shadows softer away from what casts them. 0: sharp.");
    checkbox("Light through leaves", "RayTracedLeafLight", "Leaves let some sunlight through ray-traced shadows (where they reach past the game's).");
    ImGui::EndDisabled();
    const bool ambient = checkbox("Ray-traced ambient occlusion", "RayTracedAmbientOcclusion",
                                  "Shade in corners and under things from rays, with the game's (the darker).\n"
                                  "Faster: the game's SSAO off, the rays then alone.");
    ImGui::BeginDisabled(!ambient);
    slider("Ray-traced AO range", "RayTracedAmbientRange", 2.f, 0.25f, 5.f, "%.2f m", "How far its rays look for what shades a point.");
    checkbox("Ray-traced AO at a quarter rate", "RayTracedAOQuarter",
             "One pixel of each 2 x 2 traced a frame, in turn: a quarter of the rays (DLSS smooths it). Faster.");
    ImGui::EndDisabled();
    checkbox("Ray-traced point light shadows", "RayTracedLocalLights",
             "Shadows from rays for the nearest, brightest point lights (fires, torches): what stands between them and the\n"
             "ground or walls shades them.");
    const bool reflections = checkbox("Ray-traced reflections", "RayTracedReflections",
                                      "Reflections from rays in place of the screen-space ones: also what is off screen.\n"
                                      "Needs the game's screen-space reflections on.");
    ImGui::BeginDisabled(!reflections);
    count("Ray-traced reflection samples", "RayTracedReflectionSamples", 1, 8, "Rays per pixel on rough surfaces: less noise, heavier.");
    ImGui::EndDisabled();
    const bool bounce = checkbox("Ray-traced Global Illumination", "RayTracedLightBounce",
                                 "The Global Illumination's light found with rays: no guessing behind what you see, light from farther.\n"
                                 "Needs the Global Illumination on.");
    ImGui::BeginDisabled(!bounce);
    count("Ray-traced GI rays", "RayTracedLightBounceRays", 4, 16, "Rays per texel: less noise, heavier.");
    slider("Ray-traced GI range", "RayTracedLightBounceRange", 10.f, 1.f, 50.f, "%.0f m", "How far its rays look for light.");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!bounce && !reflections);
    const bool path_tracing = checkbox("Path tracing", "PathTracing",
                                       "The Global Illumination's and the reflections' rays go on bouncing: lit by what the screen never saw. Heavy.");
    ImGui::BeginDisabled(!path_tracing);
    count("Path tracing bounces", "PathTracingBounces", 2, 4, "Bounces past the first. More: brighter shade, heavier.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
  }
  heading("About", OTHER_COLOR);
  auto link_button = [](const char* label, ImU32 color, ImU32 hovered, ImU32 active, const char* url) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    if (ImGui::Button(label)) renodx::utils::platform::LaunchURL(url);
    ImGui::PopStyleColor(3);
  };
  link_button("ATN's Ko-fi", IM_COL32(255, 90, 22, 170), IM_COL32(255, 90, 22, 255), IM_COL32(220, 70, 10, 255),
              "https://ko-fi.com/atnthegreat");
  ImGui::SameLine();
  link_button("NexusMods (For RenoDX + DLSS)", IM_COL32(33, 150, 243, 170), IM_COL32(33, 150, 243, 255), IM_COL32(20, 120, 210, 255),
              "https://www.nexusmods.com/totalwarshogun2completeedition/mods/3");
  if (!advanced) return;

  ImGui::SeparatorText("Debug");
  if (bool value = jitter_enabled.load(); ImGui::Checkbox("Camera jitter (fixes the game's DLSS)", &value)) {
    jitter_enabled.store(value);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "Jitter", value);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Sub-pixel camera movement DLSS needs for fine detail (the game leaves it off). Keep on.");
  }
  if (bool value = twshogun2::object_velocity::Enabled(); ImGui::Checkbox("Ship and water motion vectors", &value)) {
    twshogun2::object_velocity::SetEnabled(value);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "ObjectMotion", value);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Motion vectors for ships, sails, ropes and water. Keep on.");
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
  ImGui::Text("Texture LOD bias now: %.2f (game samplers seen: %zu)", twattila::texture_lod_bias::Bias(),
              twattila::texture_lod_bias::SamplerCount());
  ImGui::TextWrapped("Grass distance: %s", twshogun2::grass_distance::Status().c_str());
  ImGui::TextWrapped("Model detail distance: %s", twshogun2::model_detail::Status().c_str());
  ImGui::TextWrapped("Unit detail: %s", twshogun2::unit_detail::Status().c_str());
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

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX Total War: SHOGUN 2 Complete Edition DLSS";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: SHOGUN 2 Complete Edition DLSS by ATN";

extern "C" __declspec(dllexport) bool AddonInit(HMODULE module, HMODULE) {
  if (!registered) {
    if (!reshade::register_addon(module)) return false;
    registered = true;
  }
  if (attached) return true;
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SettingsMode", value)) advanced = (value == 1);
  if (bool value = true; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "Jitter", value)) jitter_enabled.store(value);
  if (bool value = true; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "ObjectMotion", value)) {
    twshogun2::object_velocity::SetEnabled(value);
  }
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
  if (float value = 0.f; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SunShadowSoftness", value)) {
    sun_shadow_softness = std::clamp(value, 0.f, 5.f);
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSStability", value) && value >= 0
                      && value < static_cast<int>(SSS_STABILITIES.size())) {
    sss_stability = value;
  }
  // A choice from ReShade.ini, kept within its count.
  const auto load_choice = [](const char* key, size_t count, int* value) {
    if (int loaded = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, loaded)) {
      *value = std::clamp(loaded, 0, static_cast<int>(count) - 1);
    }
  };
  load_choice("AmbientOcclusion", AO_MODES.size(), &ao.mode);
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionStrength", ao.strength);
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionRadius", ao.radius);
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "AmbientOcclusionThickness", ao.thickness);
  load_choice("AmbientOcclusionQuality", SAMPLES.size(), &ao.quality);
  load_choice("AmbientOcclusionDirections", AO_DIRECTIONS.size(), &ao.directions);
  load_choice("AmbientOcclusionResolution", AO_RESOLUTIONS.size(), &ao.resolution);
  load_choice("AmbientOcclusionAutoThickness", OFF_ON.size(), &ao.auto_thickness);
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "LightBounceStrength", gi.strength);
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "LightBounceRadius", gi.radius);
  load_choice("LightBounceResolution", GI_RESOLUTIONS.size(), &gi.resolution);
  load_choice("LightBounceQuality", SAMPLES.size(), &gi.quality);
  load_choice("LightBounceDirections", GI_DIRECTIONS.size(), &gi.directions);
  load_choice("LightBounceTemporal", OFF_ON.size(), &gi.temporal);
  load_choice("LightBounceDenoise", OFF_ON.size(), &gi.denoise);
  reshade::get_config_value(nullptr, "Shogun 2 DLSS", "LightBounceMultiBounce", gi.multi_bounce);
  load_choice("LightBounceCheckerboard", OFF_ON.size(), &gi.checkerboard);
  load_choice("LightBounceBackFace", OFF_ON.size(), &gi.backface);
  load_choice("LightBounceAutoThickness", OFF_ON.size(), &gi.auto_thickness);
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
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "UnitDetail", value) && value >= 0
                      && value < static_cast<int>(UNIT_DETAILS.size())) {
    unit_detail = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "UnitAnimation", value) && value >= 0
                      && value < static_cast<int>(UNIT_ANIMATIONS.size())) {
    unit_animation = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "UnitVariety", value) && value >= 0
                      && value < static_cast<int>(UNIT_ANIMATIONS.size())) {
    unit_variety = value;
  }
  ApplyUnitDetail();
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "RenderScaleCustom", value) && value >= 1
                      && value <= 100) {
    render_scale_percent = value;
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "RenderScale", value) && value >= 0
                      && value <= CUSTOM_RENDER_SCALE) {
    render_scale_choice = value;
    twshogun2::game_dlss::RequestScale(SelectedRenderScale());
  }
  twshogun2::game_dlss::SetEvaluateCallback([](ID3D11DeviceContext* context, NVSDK_NGX_D3D11_DLSS_Eval_Params* eval) {
    twshogun2::render_scale::EndScaling();
    twattila::texture_lod_bias::SetBias(0.f);
    twshogun2::object_velocity::OnEvaluate(context, eval);
    twshogun2::frame_trace::OnEvaluate(context, *eval);
    twshogun2::dlss_jitter::Store(eval->InJitterOffsetX, eval->InJitterOffsetY);
    if (talesofarise::frame_generation::Presenting()) StoreFrameGeneration(*eval);
  });
  // Frame generation takes over the game's swap chain, which it makes after its device.
  talesofarise::frame_generation::LoadSettings("Shogun 2 DLSS", &frame_generation);
  if (frame_generation) {
    std::wstring module_path(32768, L'\0');
    module_path.resize(GetModuleFileNameW(module, module_path.data(), static_cast<DWORD>(module_path.size())));
    talesofarise::frame_generation::Install(std::filesystem::path(module_path).parent_path(), "Shogun 2");
    for (const uint32_t shader_hash : TONEMAPPER_SHADERS) {
      renodx::utils::command_action::Register(OnTonemapper, {
                                                                .shader_hash = shader_hash,
                                                                .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW,
                                                            });
    }
  }
  twshogun2::object_velocity::Install();
  reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::bind_viewports>(twshogun2::render_scale::OnBindViewports);
  // After render_scale's (it decides the main view).
  twattila::texture_lod_bias::SetMainView(&twshogun2::render_scale::MainView);
  reshade::register_event<reshade::addon_event::bind_viewports>(twattila::texture_lod_bias::OnBindViewports);
  reshade::register_event<reshade::addon_event::bind_scissor_rects>(twshogun2::render_scale::OnBindScissorRects);
  reshade::register_event<reshade::addon_event::map_buffer_region>(twshogun2::render_scale::OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(twshogun2::render_scale::OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::init_sampler>(twattila::texture_lod_bias::OnInitSampler);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::texture_lod_bias::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_sampler>(twattila::texture_lod_bias::OnDestroySampler);
  reshade::register_event<reshade::addon_event::push_descriptors>(twattila::texture_lod_bias::OnPushDescriptors);
  reshade::register_event<reshade::addon_event::destroy_device>(twshogun2::object_velocity::OnDestroyDevice);
  reshade::register_overlay("Shogun 2 DLSS", OnOverlay);
  renodx::utils::state::Use(DLL_PROCESS_ATTACH);
  renodx::utils::command_action::Use(DLL_PROCESS_ATTACH);
  twshogun2::dlss_jitter::Use(DLL_PROCESS_ATTACH);
  attached = true;
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE) {
  if (attached) {
    twshogun2::frame_trace::Stop();
    renodx::utils::command_action::Unregister(OnTonemapper);
    renodx::utils::command_action::Use(DLL_PROCESS_DETACH);
    renodx::utils::state::Use(DLL_PROCESS_DETACH);
    twshogun2::dlss_jitter::Use(DLL_PROCESS_DETACH);
    reshade::unregister_overlay("Shogun 2 DLSS", OnOverlay);
    reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
    reshade::unregister_event<reshade::addon_event::present>(OnPresent);
    reshade::unregister_event<reshade::addon_event::bind_viewports>(twshogun2::render_scale::OnBindViewports);
    reshade::unregister_event<reshade::addon_event::bind_viewports>(twattila::texture_lod_bias::OnBindViewports);
    reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(twshogun2::render_scale::OnBindScissorRects);
    reshade::unregister_event<reshade::addon_event::map_buffer_region>(twshogun2::render_scale::OnMapBufferRegion);
    reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(twshogun2::render_scale::OnUnmapBufferRegion);
    reshade::unregister_event<reshade::addon_event::init_sampler>(twattila::texture_lod_bias::OnInitSampler);
    reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::texture_lod_bias::OnDestroyDevice);
    reshade::unregister_event<reshade::addon_event::destroy_sampler>(twattila::texture_lod_bias::OnDestroySampler);
    reshade::unregister_event<reshade::addon_event::push_descriptors>(twattila::texture_lod_bias::OnPushDescriptors);
    twattila::texture_lod_bias::Shutdown();
    twshogun2::game_dlss::SetEvaluateCallback(nullptr);
    twshogun2::object_velocity::Uninstall();
    reshade::unregister_event<reshade::addon_event::destroy_device>(twshogun2::object_velocity::OnDestroyDevice);
    twshogun2::game_dlss::Uninstall();
    twshogun2::grass_distance::Uninstall();
    twshogun2::model_detail::Uninstall();
    twshogun2::unit_detail::Uninstall();
    twshogun2::shadow_resolution::Uninstall();
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
