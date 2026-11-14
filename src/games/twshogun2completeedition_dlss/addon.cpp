/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/icons.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/state.hpp"
#include "./dlss_jitter.hpp"
#include "./hdr_link.hpp"
#include "./frame_trace.hpp"
#include "./game_dlss.hpp"
#include "./grass_distance.hpp"
#include "./model_detail.hpp"
#include "./object_velocity.hpp"
#include "./particle_resolution.hpp"
#include "./render_scale.hpp"
#include "./shadow_resolution.hpp"
#include "./terrain_detail.hpp"
#include "../twattila/dlaa_bridge.h"
#include "../twattila/texture_lod_bias.hpp"
#include "./unit_detail.hpp"

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
// Frame generation (Experimental, the HDR addon's: twattila/frame_generation.hpp, the RenoDX helper presents; in place
// of Tales' in-process one, v55) and NVIDIA Reflex (twattila/reflex.hpp). ReShade.ini HelperFrameGeneration (off,
// 2x-6x, Dynamic), FrameGenerationMethod (Auto, DLSS, FSR, XeSS), Reflex (off, on, on + boost), FrameGenerationFrameLimit,
// FrameGenerationHudRecomposition, FrameGenerationPreset (the driver's, A-C), FrameGenerationDebugView. Overlay and
// present thread; the evaluations read the frame generation row.
std::atomic_int frame_generation_choice = 0;
int frame_method_choice = 0;
int reflex_choice = 0;
int frame_limit = 0;
int hud_recomposition = 1;
int frame_preset_choice = 0;
int frame_debug_view = 0;
int upscale_debug_view = 0;  // UpscalingDebugView: 1 the motion the helper's upscaler gets
constexpr std::array<uint32_t, 4> FRAME_BACKENDS = {twattila::dlaa_bridge::FG_BACKEND_AUTO, twattila::dlaa_bridge::FG_BACKEND_DLSS,
                                                    twattila::dlaa_bridge::FG_BACKEND_FSR, twattila::dlaa_bridge::FG_BACKEND_XESS};
// Data-run marks in ReShade.log (2026-11-14): each sun light draw's first run (the HDR addon's sunlight00-03: battle,
// campaign map), each new render size the game's DLSS is evaluated at.
struct SunShader {
  uint32_t hash;
  const char* name;
};
constexpr std::array<SunShader, 4> SUN_SHADERS = {{
    {.hash = 0xED1BED93, .name = "directionallight ps30_main_highdef (battle)"},
    {.hash = 0x5496DE73, .name = "directionallight ps30_main_lowdef (battle)"},
    {.hash = 0xC3E0E939, .name = "directionallight ps30_overlay_highdef (campaign map)"},
    {.hash = 0xD5FDDE8F, .name = "directionallight ps30_overlay_lowdef (campaign map)"},
}};
std::array<std::atomic_bool, SUN_SHADERS.size()> sun_seen = {};
constexpr auto OnSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& shader = command.template GetCallbackData<SunShader>();
  const auto index = static_cast<size_t>(&shader - SUN_SHADERS.data());
  if (sun_seen[index].load(std::memory_order_relaxed) || sun_seen[index].exchange(true)) return {};
  renodx::utils::log::i(std::format("[Shogun 2 DLSS] First run: sun light draw {:08X} {}", shader.hash, shader.name));
  return {};
};
std::array<uint32_t, 2> evaluated_size = {};  // render thread
// RENODX_TW_SET="<seconds>:<key>=<value>;..." (read at launch; seconds from the first DLSS evaluation) and a
// `renodx_set_now` file next to the exe ("<key>=<value>;...", at once): RenderScale, RenderScaleCustom, Preset,
// TextureLodBias for this start only (ReShade.ini is not written).
struct TestSet {
  double seconds = -1.;  // below 0: at once
  std::string key;
  int value = 0;
  bool done = false;
};
std::vector<TestSet> test_sets;  // AddonInit, then the present thread
std::atomic<int64_t> first_evaluation = 0;  // steady_clock ticks; 0: none yet

void AddTestSets(std::string_view text) {
  for (const auto part : std::views::split(text, ';')) {
    const std::string_view entry(part.begin(), part.end());
    const size_t equals = entry.find('=');
    if (equals == std::string_view::npos) continue;
    TestSet set;
    size_t key_start = 0;
    if (const size_t colon = entry.substr(0, equals).find(':'); colon != std::string_view::npos) {
      std::from_chars(entry.data(), entry.data() + colon, set.seconds);
      key_start = colon + 1;
    }
    set.key = std::string(entry.substr(key_start, equals - key_start));
    const auto value = entry.substr(equals + 1);
    std::from_chars(value.data(), value.data() + value.size(), set.value);
    test_sets.push_back(set);
  }
}
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
// The helper's upscaler (Experimental, the HDR addon's: hdr_link.hpp; ReShade.ini HelperUpscaler): 0 the game's DLSS,
// 1 DLSS, 2 FSR, 3 XeSS; with DLSS its Ray Reconstruction (AntiAliasingRayReconstruction, Attila's: off, NVIDIA's
// preset, D, E, F).
std::atomic_int upscaler_choice = 0;
std::atomic_int ray_reconstruction_choice = 0;
constexpr std::array<uint32_t, 5> RAY_RECONSTRUCTION_PRESETS = {0, 0, 4, 5, 6};  // NVSDK_NGX_RayReconstruction_Hint_Render_Preset
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
// ReShade.ini SSSThickness, SSSContrast, SSSEdgeThreshold, SSSPrecisionOffset (HDR addon, sss.hlsli: Bend's parameters;
// 0 the game's). Advanced settings only, in no preset.
float sss_thickness = 0.f;       // overlay
float sss_contrast = 0.f;        // overlay
float sss_edge_threshold = 0.f;  // overlay
int sss_precision_offset = 0;    // overlay
float sss_foliage_light = 0.f;   // overlay: ReShade.ini SSSFoliageLight (HDR addon foliage_light.hpp), Light Through Foliage
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
  const int ray_reconstruction = ray_reconstruction_choice.load();
  const int frame_generation = frame_generation_choice.load();
  twshogun2::hdr_link::SetChoices({
      .upscaler = static_cast<uint32_t>(upscaler_choice.load()),
      .ray_reconstruction = (ray_reconstruction != 0) ? RAY_RECONSTRUCTION_PRESETS[ray_reconstruction] + 1 : 0u,
      // 2x-6x, Dynamic (1).
      .frame_generation = (frame_generation >= 6) ? 1u : ((frame_generation >= 1) ? static_cast<uint32_t>(frame_generation) + 1u : 0u),
      .frame_backend = FRAME_BACKENDS[frame_method_choice],
      .reflex = static_cast<uint32_t>(reflex_choice),
      .frame_limit = static_cast<uint32_t>(frame_limit),
      .frame_preset = static_cast<uint32_t>(frame_preset_choice),
      .hud_recomposition = hud_recomposition != 0,
      .frame_debug_view = static_cast<uint32_t>(frame_debug_view),
      .upscale_debug_view = static_cast<uint32_t>(upscale_debug_view),
  });
  const float scale = twshogun2::game_dlss::UpdateFeature();
  const auto display = twshogun2::game_dlss::DisplaySize();
  const bool scaling = dlss_running && scale < 0.999f;
  const auto render = twattila::frame_history::AspectSize(display[0], display[1], scale);
  if (scaling && particle_resolution.load() == 1 && display[0] != 0 && render[0] != 0) {
    twshogun2::particle_resolution::Apply(static_cast<float>(render[0]) / static_cast<float>(display[0]));
  } else {
    twshogun2::particle_resolution::Apply(1.f);
  }
  twshogun2::render_scale::BeginFrame(scaling, display, render,
                                      (upscaler_choice.load() != 0 || frame_generation_choice.load() != 0) && dlss_running);
  // Until the DLSS evaluation (its callback clears it): log2(render / display width), minus 1 (sharper) or 2.
  float bias = 0.f;
  if (const int choice = texture_lod_bias_choice.load(); scaling && choice != 0 && display[0] != 0) {
    bias = std::log2(static_cast<float>(render[0]) / static_cast<float>(display[0])) - static_cast<float>(choice - 1);
  }
  twattila::texture_lod_bias::SetTracking(queue, bias != 0.f);
  twattila::texture_lod_bias::SetBias(bias);
  if (const std::string marker = twshogun2::frame_trace::TakeSettingsMarker(); !marker.empty()) AddTestSets(marker);
  if (!test_sets.empty()) {
    // Seconds since the first DLSS evaluation; -0.5 before it (only the sets for "at once" then).
    double seconds = -0.5;
    if (const int64_t start = first_evaluation.load(std::memory_order_relaxed); start != 0) {
      const auto elapsed = std::chrono::steady_clock::duration(std::chrono::steady_clock::now().time_since_epoch().count() - start);
      seconds = std::chrono::duration<double>(elapsed).count();
    }
    for (auto& set : test_sets) {
      if (set.done || seconds < set.seconds) continue;
      set.done = true;
      if (set.key == "RenderScale" && set.value >= 0 && set.value <= CUSTOM_RENDER_SCALE) {
        render_scale_choice = set.value;
      } else if (set.key == "RenderScaleCustom" && set.value >= 1 && set.value <= 100) {
        render_scale_percent = set.value;
      } else if (set.key == "Preset" && std::ranges::find(PRESETS, static_cast<uint32_t>(set.value), &PresetChoice::value) != PRESETS.end()) {
        preset = static_cast<uint32_t>(set.value);
        twshogun2::game_dlss::RequestPreset(preset);
      } else if (set.key == "TextureLodBias" && set.value >= 0 && set.value <= 3) {
        texture_lod_bias_choice.store(set.value);
      } else {
        renodx::utils::log::i(std::format("[Shogun 2 DLSS] Test: no setting {}={}, nothing changed", set.key, set.value));
        continue;
      }
      if (set.key.starts_with("RenderScale")) twshogun2::game_dlss::RequestScale(SelectedRenderScale());
      renodx::utils::log::i(std::format("[Shogun 2 DLSS] Test: {} set to {} at {:.1f} s", set.key, set.value, seconds));
    }
  }
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
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("For every render scale. A DLSS preset override in the NVIDIA app wins over this.");
  }
  if (int choice = upscaler_choice.load();
      ImGui::Combo("Upscaler (Experimental)", &choice, "Game's DLSS\0DLSS (RenoDX helper)\0FSR (RenoDX helper)\0XeSS (RenoDX helper)\0")) {
    upscaler_choice.store(choice);
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "HelperUpscaler", choice);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("The game's DLSS, or the RenoDX helper's (renodx-tw-dlaa64.exe next to the addons, with the RenoDX HDR addon):\n"
                      "newer DLSS with Ray Reconstruction for the ray tracing, AMD FSR or Intel XeSS. The game's Anti-aliasing on DLSS.");
  }
  if (upscaler_choice.load() != 0 && !twshogun2::hdr_link::UpscalerAvailable()) {
    ImGui::TextWrapped("The RenoDX HDR addon isn't loaded: the game's DLSS runs.");
  }
  if (upscaler_choice.load() == 1) {
    if (int choice = ray_reconstruction_choice.load();
        ImGui::Combo("Ray Reconstruction (Experimental)", &choice, "Off\0NVIDIA's preset\0D\0E\0F\0")) {
      ray_reconstruction_choice.store(choice);
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AntiAliasingRayReconstruction", choice);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("DLSS Ray Reconstruction cleans the ray-traced effects (Ray Tracing below): sharper, steadier.\n"
                        "NVIDIA only; with a ray-traced effect on.");
    }
  }
  // A combo over a ReShade.ini key; true when changed.
  const auto helper_combo = [](const char* label, const char* key, int* value, const char* items, const char* tooltip) {
    const bool changed = ImGui::Combo(label, value, items);
    if (changed) reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, *value);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return changed;
  };
  if (upscaler_choice.load() != 0) {
    helper_combo("Upscaling debug view", "UpscalingDebugView", &upscale_debug_view, "Off\0Motion Vectors\0",
                 "Motion Vectors: the motion the upscaler gets over the picture (colour: direction, brighter: faster).");
  }
  if (int choice = frame_generation_choice.load();
      helper_combo("Frame generation (Experimental)", "HelperFrameGeneration", &choice, "Off\0" "2x\0" "3x\0" "4x\0" "5x\0" "6x\0" "Dynamic\0",
                   "Frames shown per rendered frame (the RenoDX helper presents, with the RenoDX HDR addon). Turn the game's VSync off;\n"
                   "windowed or borderless. DLSS: 2x on RTX 40 or newer, 3x to 6x and Dynamic on RTX 50. FSR: 2x. XeSS: 2x, up to 4x on Intel Arc.")) {
    frame_generation_choice.store(choice);
  }
  if (frame_generation_choice.load() != 0) {
    ImGui::TextWrapped("Frame generation pauses while this menu is open.");
    helper_combo("Frame generation method", "FrameGenerationMethod", &frame_method_choice, "Auto\0DLSS\0FSR\0XeSS\0",
                 "Auto: DLSS on an RTX 40 or newer, XeSS on Intel, else FSR. Their files next to renodx-tw-dlaa64.exe.");
    helper_combo("HUD recomposition", "FrameGenerationHudRecomposition", &hud_recomposition, "Off\0On\0",
                 "Lays the HUD over the generated frames instead of warping it with the scene (no HUD smears).");
    helper_combo("Frame generation preset", "FrameGenerationPreset", &frame_preset_choice, "Driver's\0A\0B\0C\0",
                 "NVIDIA's presets. A: HUD recomposition off. B: on. C (Lite): lighter, lower quality.\n"
                 "Saved in the NVIDIA driver profile of renodx-tw-dlaa64.exe; applies after restarting the game.");
    helper_combo("Frame generation debug view", "FrameGenerationDebugView", &frame_debug_view, "Off\0HUD-less image\0UI layer\0",
                 "What frame generation gets, shown while this menu is closed: the picture without the HUD, or the HUD in magenta.");
  }
  helper_combo("NVIDIA Reflex", "Reflex", &reflex_choice, "Off\0On\0On + Boost\0",
               "Less input lag on NVIDIA GPUs: each frame starts only as the GPU needs it. Always on with frame generation.");
  if (ImGui::SliderInt("Frame rate limit", &frame_limit, 0, 240, "%d fps", ImGuiSliderFlags_AlwaysClamp)) {
    reshade::set_config_value(nullptr, "Shogun 2 DLSS", "FrameGenerationFrameLimit", frame_limit);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Caps the rendered frame rate with Reflex (frame generation multiplies it). 0: off.");
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
  // Attila's key (twattila/texture_lod_bias.hpp SetWrappingTrilinear: the samplers filtered without anisotropy too).
  if (render_scale_choice != 0 && texture_lod_bias_choice.load() != 0) {
    bool all = false;
    reshade::get_config_value(nullptr, "Shogun 2 DLSS", "AntiAliasingTextureLodBiasTrilinear", all);
    if (ImGui::Checkbox("Texture LOD bias on all textures (Experimental)", &all)) {
      twattila::texture_lod_bias::SetWrappingTrilinear(all);
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AntiAliasingTextureLodBiasTrilinear", all);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Texture LOD bias also on the textures the game filters without anisotropy. Can shimmer.");
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
  if (advanced) {
    if (float_slider("SSS thickness (Experimental)", &sss_thickness, 0.f, 0.05f, (sss_thickness == 0.f ? "Game's" : "%.4f"), 0.f,
                     "How thick each pixel counts as for casting screen-space shadows. Thicker: fuller shadows, more halos.")) {
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSThickness", sss_thickness);
    }
    if (float_slider("SSS contrast (Experimental)", &sss_contrast, 0.f, 8.f, (sss_contrast == 0.f ? "Game's" : "%.2f"), 0.f,
                     "How fast screen-space shadows go from lit to shadowed (the game's: 4; halved by Shadow stability).")) {
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSContrast", sss_contrast);
    }
    if (float_slider("SSS edge threshold (Experimental)", &sss_edge_threshold, 0.f, 0.1f,
                     (sss_edge_threshold == 0.f ? "Game's" : "%.4f"), 0.f,
                     "Depth jumps above this count as edges, not blended between. Lower: crisper, more aliasing.")) {
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSEdgeThreshold", sss_edge_threshold);
    }
    if (choice_slider("SSS precision offset (Experimental)", &sss_precision_offset, OFF_ON.size(), OFF_ON[sss_precision_offset], 0,
                      "A small offset for depth imprecision: less shadow acne far away, contact shadows start a bit later.")) {
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSPrecisionOffset", sss_precision_offset);
    }
    if (float_slider("Light through foliage (Experimental)", &sss_foliage_light, 0.f, 1.f, "%.2f", 0.f,
                     "How much sun comes through the screen-space shadows grass and leaves cast: they are thin. Walls and\n"
                     "soldiers keep their full shadows, on foliage too. 0: as dark as any other shadow.")) {
      reshade::set_config_value(nullptr, "Shogun 2 DLSS", "SSSFoliageLight", sss_foliage_light);
    }
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
    // ReShade.ini [Shogun 2 DLSS] RayTraced* / RayTracing* / PathTracing*: the HDR addon's ray tracing (its
    // ray_tracing.hpp, twattila/rt_scene.hpp's rows; Attila's keys).
    const auto tooltip_of = [](const char* tooltip) {
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    };
    const auto checkbox = [&](const char* label, const char* key, const char* tooltip) {
      bool value = false;
      reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
      if (ImGui::Checkbox(label, &value)) reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, value);
      tooltip_of(tooltip);
      return value;
    };
    const auto slider = [&](const char* label, const char* key, float default_value, float min, float max, const char* format,
                            const char* tooltip) {
      float value = default_value;
      reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
      if (ImGui::SliderFloat(label, &value, min, max, format, ImGuiSliderFlags_AlwaysClamp)) {
        reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, value);
      }
      tooltip_of(tooltip);
    };
    const auto count = [&](const char* label, const char* key, int default_value, int max, const char* tooltip) {
      int value = default_value;
      reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
      if (ImGui::SliderInt(label, &value, 1, max, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, value);
      }
      tooltip_of(tooltip);
    };
    // A slider over named choices (0 the first); its value.
    const auto choice = [&](const char* label, const char* key, int default_value, std::span<const char* const> names,
                            const char* tooltip) {
      int value = default_value;
      reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, value);
      value = std::clamp(value, 0, static_cast<int>(names.size()) - 1);
      if (ImGui::SliderInt(label, &value, 0, static_cast<int>(names.size()) - 1, names[value],
                           ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
        reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, value);
      }
      tooltip_of(tooltip);
      return value;
    };
    constexpr std::array<const char*, 2> OFF_ON_NAMES = {"Off", "On"};
    ImGui::TextWrapped("Needs an RTX GPU and renodx-tw-dlaa64.exe next to the addons.");
    // The presets (twattila/graphics_page.hpp's): RT/PT is ATN's RAYS_PRESET in this section's keys (no Path-Traced AO
    // here: ray-traced AO in its place), No RT/PT turns every ray-traced effect off and keeps their rows, Reset puts
    // every row at its default. Whole values go in as integers (the checkboxes and choices read ints).
    constexpr std::pair<const char*, float> RAYS_OFF[] = {
        {"RayTracedSunShadows", 0.f},
        {"RayTracedAmbientOcclusion", 0.f},
        {"RayTracedLocalLights", 0.f},
        {"RayTracedReflections", 0.f},
        {"RayTracedLightBounce", 0.f},
        {"PathTracing", 0.f},
        {"RayTracedFogShadows", 0.f},
        {"RayTracedPathAmbient", 0.f},
        {"RayTracedSpecularOcclusion", 0.f},
    };
    constexpr std::pair<const char*, float> RAYS_PRESET[] = {
        {"RayTracedSunShadows", 1.f},
        {"RayTracedShadowSoftness", 0.8f},
        {"RayTracedLeafLight", 1.f},
        {"RayTracedAmbientOcclusion", 1.f},
        {"RayTracedLocalLights", 1.f},
        {"RayTracedLightSoftness", 0.25f},
        {"RayTracedLightImportance", 1.f},
        {"RayTracingDenoiser", 1.f},
        {"RayTracedLightBounce", 1.f},
        {"PathTracing", 1.f},
        {"RayTracedLightBounceRays", 1.f},
        {"RayTracedLightBounceResolution", 0.f},
        {"RayTracedLightBounceRange", 50.f},
        {"PathTracingBounces", 1.f},
        {"RayTracedReflections", 1.f},
        {"RayTracedReflectionSamples", 1.f},
        {"RayTracedFogShadows", 1.f},
    };
    constexpr std::pair<const char*, float> RAYS_RESET[] = {
        {"RayTracedShadowSoftness", 0.5f},
        {"RayTracedLeafLight", 0.f},
        {"RayTracedSpriteShadows", 0.f},
        {"RayTracedFarTreeShadows", 0.f},
        {"RayTracedUnitsOutOfView", 0.f},
        {"RayTracedAmbientRange", 2.f},
        {"RayTracedAORays", 2.f},
        {"RayTracedAOQuarter", 0.f},
        {"RayTracedLightSoftness", 0.25f},
        {"RayTracedLightCount", 0.f},
        {"RayTracedLightImportance", 0.f},
        {"RayTracedLightSteady", 0.f},
        {"RayTracedLightRange", 0.f},
        {"RayTracedReflectionSamples", 1.f},
        {"RayTracedReflectionReuse", 0.f},
        {"RayTracedWaterReflections", 0.f},
        {"RayTracedWaterSteady", 0.f},
        {"RayTracedLightBounceRays", 4.f},
        {"RayTracedLightBounceRange", 10.f},
        {"RayTracedLightBounceResolution", 1.f},
        {"PathTracingBounces", 2.f},
        {"RayTracedSharc", 0.f},
        {"RayTracedPathSkyApart", 0.f},
        {"RayTracedPathKeptCube", 0.f},
        {"RayTracedPathSer", 0.f},
        {"RayTracingDenoiser", 0.f},
        {"RayTracedLeafMicromaps", 0.f},
        {"RayTracedGpuMicromaps", 1.f},
        {"RayTracedFastLeafShadows", 0.f},
        {"RayTracingOutputResolution", 0.f},
        {"RayTracingRaysShadows", 1.f},
        {"RayTracingRaysAmbient", 1.f},
        {"RayTracingRaysReflections", 1.f},
        {"RayTracingRaysPaths", 1.f},
        {"RayTracedDebugLog", 0.f},
        {"RayTracedDebugView", 0.f},
        {"RayTracedSceneTest", 0.f},
    };
    const auto write_rays = [](std::span<const std::pair<const char*, float>> values) {
      for (const auto& [key, value] : values) {
        if (value == std::floor(value)) {
          reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, static_cast<int>(value));
        } else {
          reshade::set_config_value(nullptr, "Shogun 2 DLSS", key, value);
        }
      }
    };
    if (preset_button("RT/PT##rays", RT_COLOR,
                      "Every ray-traced effect and path tracing on, as ATN plays (restart the game).\n"
                      "Turns the Global Illumination on, and Ray Reconstruction where DLSS runs without it.")) {
      write_rays(RAYS_PRESET);
      gi.strength = std::max(gi.strength, 1.f);
      save_gi();
      // Without Ray Reconstruction the rays have only the built-in denoisers (Attila's preset: F).
      if (upscaler_choice.load() == 1 && ray_reconstruction_choice.load() == 0) {
        ray_reconstruction_choice.store(4);
        reshade::set_config_value(nullptr, "Shogun 2 DLSS", "AntiAliasingRayReconstruction", 4);
      }
    }
    ImGui::SameLine();
    if (preset_button("No RT/PT (Recommended)##rays", RT_COLOR, "Every ray-traced effect off, their sliders kept (restart the game).")) {
      write_rays(RAYS_OFF);
    }
    ImGui::SameLine();
    if (preset_button("Reset##rays", RT_COLOR, "Every ray-traced effect off, this section's sliders at their defaults (restart the game).")) {
      write_rays(RAYS_OFF);
      write_rays(RAYS_RESET);
    }
    const bool sun_shadows = checkbox("Ray-traced sun shadows", "RayTracedSunShadows",
                                      "Sun shadows from rays, with the game's: also where its shadow maps miss things or end.\n"
                                      "Faster: the game's Screen Space Shadows off, the rays then alone (grass casts no contact shadows).");
    ImGui::BeginDisabled(!sun_shadows);
    slider("Ray-traced shadow softness", "RayTracedShadowSoftness", 0.5f, 0.f, 3.f, "%.2f",
           "The sun's size in degrees: shadows softer away from what casts them. 0: sharp.");
    checkbox("Light through leaves", "RayTracedLeafLight", "Leaves let some sunlight through ray-traced shadows (where they reach past the game's).");
    choice("Sprite soldier shadows (Experimental)", "RayTracedSpriteShadows", 0, OFF_ON_NAMES,
           "The flat far soldiers' ray-traced shadows keep their shape as the camera turns.");
    choice("Far tree shadows (Experimental)", "RayTracedFarTreeShadows", 0, OFF_ON_NAMES, "The flat far trees cast ray-traced shadows too.");
    choice("Shadow casters out of view (Experimental)", "RayTracedUnitsOutOfView", 0, OFF_ON_NAMES,
           "Buildings, rocks, trees, sails and banners behind the camera cast ray-traced shadows into view too, far soldiers with Sprite "
           "soldier shadows (restart the game).");
    ImGui::EndDisabled();
    const bool ambient = checkbox("Ray-traced ambient occlusion", "RayTracedAmbientOcclusion",
                                  "Shade in corners and under things from rays, with the game's (the darker).\n"
                                  "Faster: the game's SSAO off, the rays then alone.");
    ImGui::BeginDisabled(!ambient);
    slider("Ray-traced AO range", "RayTracedAmbientRange", 2.f, 0.25f, 5.f, "%.2f m", "How far its rays look for what shades a point.");
    count("Ray-traced AO rays", "RayTracedAORays", 2, 8, "More rays: less noise. Heavier.");
    checkbox("Ray-traced AO at a quarter rate", "RayTracedAOQuarter",
             "One pixel of each 2 x 2 traced a frame, in turn: a quarter of the rays (DLSS smooths it). Faster.");
    ImGui::EndDisabled();
    const bool local_lights = checkbox("Ray-traced point light shadows", "RayTracedLocalLights",
                                       "Shadows from rays for the nearest, brightest point lights (fires, torches): what stands between them and the\n"
                                       "ground or walls shades them.");
    ImGui::BeginDisabled(!local_lights);
    if (upscaler_choice.load() == 1 && ray_reconstruction_choice.load() != 0) {
      slider("Torch and fire shadow softness", "RayTracedLightSoftness", 0.25f, 0.f, 1.f, "%.2f m",
             "How big torches and fires are for their soft shadows. 0: sharp. With Ray Reconstruction.");
    }
    constexpr std::array<const char*, 3> LIGHT_COUNTS = {"32", "64", "128"};
    choice("Torch and fire shadow lights (Experimental)", "RayTracedLightCount", 0, LIGHT_COUNTS,
           "How many torches and fires cast ray-traced shadows, the nearest and brightest first.\n"
           "Each spot of the screen traces the 32 that light it most. More: heavier in big fires.");
    choice("Importance-sampled torch shadows (Experimental)", "RayTracedLightImportance", 0, OFF_ON_NAMES,
           "Two torch / fire shadow rays a pixel, picked by how much light they give:\n"
           "faster with many fires; dim fires' shadows follow moving units slower.");
    choice("Steady torch shadows (Experimental)", "RayTracedLightSteady", 0, OFF_ON_NAMES,
           "Torch and fire shadows that don't flicker: every fire traced every frame, with sharp edges.\n"
           "Heavier with many fires. Overrides Importance-sampled torch shadows.");
    slider("Torch and fire shadow range (Experimental)", "RayTracedLightRange", 0.f, 0.f, 500.f, "%.0f m",
           "Only torches and fires this close to the camera cast ray-traced shadows. 0 m: any distance.");
    ImGui::EndDisabled();
    const bool fog_shadows = checkbox("Ray-traced fog shadows (Experimental)", "RayTracedFogShadows",
                                      "The sun's light in the battle fog shaded where hills, trees and buildings stand in its way.");
    const bool reflections = checkbox("Ray-traced reflections", "RayTracedReflections",
                                      "Reflections from rays in place of the screen-space ones: also what is off screen.\n"
                                      "Needs the game's screen-space reflections on.");
    ImGui::BeginDisabled(!reflections);
    count("Ray-traced reflection samples", "RayTracedReflectionSamples", 1, 8, "Rays per pixel on rough surfaces: less noise, heavier.");
    constexpr std::array<const char*, 3> REUSE_NAMES = {"Off", "From ReSTIR GI", "Own ReSTIR"};
    choice("Reflection reuse (Experimental)", "RayTracedReflectionReuse", 0, REUSE_NAMES,
           "Less noise on rough reflections.\n"
           "From ReSTIR GI: also the ray-traced Global Illumination's samples (needs it on).\n"
           "Own ReSTIR: their own samples, reused over frames and between neighbours.");
    const bool water_reflections = choice("Ray-traced water reflections (Experimental)", "RayTracedWaterReflections", 0, OFF_ON_NAMES,
                                          "The sea, rivers and lakes reflect ships, shores and units with rays (water quality High or Ultra).")
                                   != 0;
    ImGui::BeginDisabled(!water_reflections);
    choice("Steadier water reflections (Experimental)", "RayTracedWaterSteady", 0, OFF_ON_NAMES,
           "Fewer flickering dots in calm water's reflections: each pixel stays within what the water around it shows.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    const bool specular_occlusion = checkbox("Ray-traced specular occlusion (Experimental)", "RayTracedSpecularOcclusion",
                                             "The sky's reflection taken off shiny surfaces where something stands over them (inside\n"
                                             "doorways, under hulls and roofs). Needs the game's screen-space reflections on.");
    const bool bounce = checkbox("Ray-traced Global Illumination", "RayTracedLightBounce",
                                 "The Global Illumination's light found with rays: no guessing behind what you see, light from farther.\n"
                                 "Needs the Global Illumination on.");
    ImGui::BeginDisabled(!bounce);
    count("Ray-traced GI rays", "RayTracedLightBounceRays", 4, 16, "Rays per texel: less noise, heavier.");
    slider("Ray-traced GI range", "RayTracedLightBounceRange", 10.f, 1.f, 50.f, "%.0f m", "How far its rays look for light.");
    constexpr std::array<const char*, 3> RESOLUTIONS = {"Full", "Half", "Quarter"};
    choice("Ray-traced GI resolution", "RayTracedLightBounceResolution", 1, RESOLUTIONS,
           "Its own resolution (the Global Illumination's Resolution and Checkerboard no longer apply to it).\n"
           "Full: sharpest, about 4x Half's cost.");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!bounce && !reflections);
    const bool path_tracing = checkbox("Path tracing", "PathTracing",
                                       "The Global Illumination's and the reflections' rays go on bouncing: lit by what the screen never saw. Heavy.");
    ImGui::BeginDisabled(!path_tracing);
    count("Path tracing bounces", "PathTracingBounces", 2, 2, "Bounces past the first where the camera sees nothing. 1: faster, a little darker.");
    choice("SHaRC radiance cache (Experimental)", "RayTracedSharc", 0, OFF_ON_NAMES,
           "NVIDIA's SHaRC for the light of what the screen does not show (more bounces off-screen, paths end sooner).");
    const bool path_ambient = choice("Path-traced AO (Experimental)", "RayTracedPathAmbient", 0, OFF_ON_NAMES,
                                     "The paths bring the sky's light in place of the game's ambient: shade wherever the sky can't reach.\n"
                                     "In battles, in place of ray-traced AO (the campaign map keeps it). Needs Ray-traced Global Illumination.")
                              != 0;
    ImGui::BeginDisabled(!path_ambient);
    choice("Path-traced AO: own sky rays (Experimental)", "RayTracedPathSkyApart", 0, OFF_ON_NAMES,
           "The sky's light averaged from each pixel's own rays: less noise, and more rays help. Off: through ReSTIR GI.");
    choice("Path-traced AO: steady bounce light (Experimental)", "RayTracedPathKeptCube", 0, OFF_ON_NAMES,
           "Light bouncing off walls and ground follows the game's sky, not the view Path-traced AO started in.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    choice("Path tracing SER (Experimental)", "RayTracedPathSer", 0, OFF_ON_NAMES,
           "Shader execution reordering for path tracing and ray-traced reflections (DXR 1.2, RTX 40 / 50 series): may be faster.");
    ImGui::EndDisabled();
    if (sun_shadows || ambient || local_lights || fog_shadows || reflections || specular_occlusion || bounce) {
      if (upscaler_choice.load() != 0 && !(upscaler_choice.load() == 1 && ray_reconstruction_choice.load() != 0)) {
        constexpr std::array<const char*, 2> DENOISERS = {"Built-in", "NRD"};
        choice("Ray tracing denoiser (Experimental)", "RayTracingDenoiser", 0, DENOISERS,
               "Cleans the ray-traced shadows, AO, Global Illumination and reflections (with the helper's upscaler).\n"
               "Built-in: the mod's own filters. NRD: NVIDIA's denoisers, steadier, uses more memory.");
      }
      const bool micromaps = choice("Leaf micromaps (Experimental)", "RayTracedLeafMicromaps", 0, OFF_ON_NAMES,
                                    "Faster ray-traced shadows through leaves: their cut-outs are baked once (NVIDIA Opacity Micro-Map SDK).") != 0;
      ImGui::BeginDisabled(!micromaps);
      choice("GPU micromap baking", "RayTracedGpuMicromaps", 1, OFF_ON_NAMES,
             "Leaf micromaps baked on the GPU instead of CPU threads:\n"
             "frees the CPU, and new grass's and leaves' shadows settle sooner.");
      choice("Fast leaf shadows (Experimental)", "RayTracedFastLeafShadows", 0, OFF_ON_NAMES,
             "Leaves and grass with Leaf micromaps skip their alpha tests:\n"
             "faster shadows under trees and in fields, but coarser and darker.");
      ImGui::EndDisabled();
      // Attila's Rays a Pixel (twattila/rt_scene.hpp): the HDR addon counts them.
      constexpr std::array<const char*, 9> RAYS_A_PIXEL = {"1", "Match Output", "2", "3", "4", "5", "6", "7", "8"};
      if (choice("Rays a pixel (Experimental)", "RayTracingOutputResolution", 0, RAYS_A_PIXEL,
                 "More rays for every rendered pixel, each from its own spot of it: less noise, finer shadow edges.\n"
                 "The cost grows with the rays. Match Output: as many rays on screen as at 100% (up to 8 a pixel).") != 0) {
        choice("Rays a pixel: shadows", "RayTracingRaysShadows", 1, OFF_ON_NAMES, "The sun's and the torches' and fires' shadows take the extra rays.");
        choice("Rays a pixel: ray-traced AO", "RayTracingRaysAmbient", 1, OFF_ON_NAMES, "Ray-traced AO takes the extra rays.");
        choice("Rays a pixel: reflections", "RayTracingRaysReflections", 1, OFF_ON_NAMES, "Ray-traced reflections take the extra rays.");
        choice("Rays a pixel: Global Illumination", "RayTracingRaysPaths", 1, OFF_ON_NAMES,
               "Ray-traced and path-traced Global Illumination take the extra rays (up to 16 a texel). The heaviest.");
      }
      choice("Log details", "RayTracedDebugLog", 0, OFF_ON_NAMES,
             "Ray tracing's numbers in the logs every 10 s, for bug reports. Off: nothing of this runs.");
      // rt_scene's Debug View values (Off, its effects, All; the others: nothing here makes them).
      struct DebugView {
        int value;
        const char* label;
      };
      constexpr std::array<DebugView, 7> DEBUG_VIEWS = {{
          {.value = 0, .label = "Off"},
          {.value = 1, .label = "Sun Shadows"},
          {.value = 2, .label = "Ambient Occlusion"},
          {.value = 3, .label = "Global Illumination"},
          {.value = 4, .label = "Reflections"},
          {.value = 6, .label = "Point Light Shadows"},
          {.value = 9, .label = "All"},
      }};
      int debug_view = 0;
      reshade::get_config_value(nullptr, "Shogun 2 DLSS", "RayTracedDebugView", debug_view);
      const auto shown = std::ranges::find(DEBUG_VIEWS, debug_view, &DebugView::value);
      if (ImGui::BeginCombo("Ray tracing debug view", (shown != DEBUG_VIEWS.end() ? shown->label : DEBUG_VIEWS[0].label))) {
        for (const auto& view : DEBUG_VIEWS) {
          if (ImGui::Selectable(view.label, view.value == debug_view)) {
            reshade::set_config_value(nullptr, "Shogun 2 DLSS", "RayTracedDebugView", view.value);
          }
        }
        ImGui::EndCombo();
      }
      tooltip_of("A ray-traced effect's raw result as the picture (All: every one tiled), to check they work.\n"
                 "Magenta: it made nothing (off or not traced).");
      if (debug_view != 0) {
        constexpr std::array<const char*, 5> SCENE_TESTS = {"Everything", "Terrain Only", "Objects and Trees Only", "No Leaves",
                                                            "Nothing Kept Off-Screen"};
        choice("Ray tracing scene (test)", "RayTracedSceneTest", 0, SCENE_TESTS,
               "While the Debug View is on: one part of the scene alone, to find what breaks an effect.");
      }
      if (ImGui::Button("Record ray tracing profile (30 s)")) twshogun2::hdr_link::RequestProfile();
      tooltip_of("30 s of every frame's ray tracing costs into tw_rt_profile.csv and tw_rt_profile_helper.csv\n"
                 "(next to the game's exe). Turn the effects on, press, then move the camera over the map.");
    }
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
  if (bool all = false; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "AntiAliasingTextureLodBiasTrilinear", all)) {
    twattila::texture_lod_bias::SetWrappingTrilinear(all);
  }
  if (uint32_t value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "Preset", value)
                          && std::ranges::find(PRESETS, value, &PresetChoice::value) != PRESETS.end()) {
    preset = value;
  }
  twshogun2::game_dlss::RequestPreset(preset);
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "HelperUpscaler", value) && value >= 0 && value <= 3) {
    upscaler_choice.store(value);
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "AntiAliasingRayReconstruction", value) && value >= 0
                      && value < static_cast<int>(RAY_RECONSTRUCTION_PRESETS.size())) {
    ray_reconstruction_choice.store(value);
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "HelperFrameGeneration", value) && value >= 0 && value <= 6) {
    frame_generation_choice.store(value);
  }
  // A ReShade.ini choice from 0 to `last`.
  const auto read_choice = [](const char* key, int last, int* value) {
    if (int read = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", key, read) && read >= 0 && read <= last) *value = read;
  };
  read_choice("FrameGenerationMethod", static_cast<int>(FRAME_BACKENDS.size()) - 1, &frame_method_choice);
  read_choice("Reflex", 2, &reflex_choice);
  read_choice("UpscalingDebugView", 1, &upscale_debug_view);
  read_choice("FrameGenerationFrameLimit", 240, &frame_limit);
  read_choice("FrameGenerationHudRecomposition", 1, &hud_recomposition);
  read_choice("FrameGenerationPreset", 3, &frame_preset_choice);
  read_choice("FrameGenerationDebugView", 2, &frame_debug_view);
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
  if (float value = 0.f; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSThickness", value)) sss_thickness = std::clamp(value, 0.f, 0.05f);
  if (float value = 0.f; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSContrast", value)) sss_contrast = std::clamp(value, 0.f, 8.f);
  if (float value = 0.f; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSEdgeThreshold", value)) {
    sss_edge_threshold = std::clamp(value, 0.f, 0.1f);
  }
  if (int value = 0; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSPrecisionOffset", value)) sss_precision_offset = std::clamp(value, 0, 1);
  if (float value = 0.f; reshade::get_config_value(nullptr, "Shogun 2 DLSS", "SSSFoliageLight", value)) sss_foliage_light = std::clamp(value, 0.f, 1.f);
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
  test_sets.clear();
  if (const char* sets = std::getenv("RENODX_TW_SET"); sets != nullptr) {
    AddTestSets(sets);
    renodx::utils::log::i(std::format("[Shogun 2 DLSS] Test: {} timed settings from RENODX_TW_SET", test_sets.size()));
  }
  twshogun2::game_dlss::SetEvaluateCallback([](ID3D11DeviceContext* context, NVSDK_NGX_D3D11_DLSS_Eval_Params* eval) {
    if (int64_t expected = 0; first_evaluation.load(std::memory_order_relaxed) == 0) {
      first_evaluation.compare_exchange_strong(expected, std::chrono::steady_clock::now().time_since_epoch().count());
    }
    if (const std::array<uint32_t, 2> size = {eval->InRenderSubrectDimensions.Width, eval->InRenderSubrectDimensions.Height};
        size != evaluated_size) {
      evaluated_size = size;
      const auto display = twshogun2::game_dlss::DisplaySize();
      renodx::utils::log::i(std::format("[Shogun 2 DLSS] Evaluating {}x{} -> {}x{}, jitter ({:.4f}, {:.4f})", size[0],
                                        size[1], display[0], display[1], eval->InJitterOffsetX, eval->InJitterOffsetY));
    }
    twshogun2::render_scale::EndScaling();
    twattila::texture_lod_bias::SetBias(0.f);
    twshogun2::object_velocity::OnEvaluate(context, eval);
    twshogun2::frame_trace::OnEvaluate(context, *eval);
    twshogun2::dlss_jitter::Store(eval->InJitterOffsetX, eval->InJitterOffsetY);
    // The helper's upscaler in place of the game's DLSS, frame generation's inputs (the HDR addon's; false: the game's).
    if (upscaler_choice.load(std::memory_order_relaxed) == 0 && frame_generation_choice.load(std::memory_order_relaxed) == 0) return false;
    twshogun2::hdr_link::UpscaleRequest request = {
        .context = context,
        .color = eval->Feature.pInColor,
        .depth = eval->pInDepth,
        .motion = eval->pInMotionVectors,
        .output = eval->Feature.pInOutput,
        .render_width = eval->InRenderSubrectDimensions.Width,
        .render_height = eval->InRenderSubrectDimensions.Height,
        .jitter_x = eval->InJitterOffsetX,
        .jitter_y = eval->InJitterOffsetY,
        .motion_scale_x = eval->InMVScaleX,
        .motion_scale_y = eval->InMVScaleY,
        .reset = eval->InReset != 0,
        .preset = twshogun2::game_dlss::Preset(),
    };
    request.camera = twshogun2::render_scale::MainCamera(&request.constants);
    return twshogun2::hdr_link::Upscale(request);
  });
  renodx::utils::log::i(std::format(
      "[Shogun 2 DLSS] Options: render scale {} ({}% custom), preset {}, texture LOD bias {}, jitter {}, object motion {}, "
      "upscaler {} RR {}, frame generation {} method {} Reflex {} limit {}, particle resolution {}, SSR resolution {}, "
      "SSS samples {} stability {}, shadow map {} stability {}, sun shadow softness {}, grass {} model {} unit {} / {} / {} "
      "terrain {}",
      render_scale_choice, render_scale_percent, preset, texture_lod_bias_choice.load(), jitter_enabled.load(),
      twshogun2::object_velocity::Enabled(), upscaler_choice.load(), ray_reconstruction_choice.load(), frame_generation_choice.load(),
      frame_method_choice, reflex_choice, frame_limit, particle_resolution.load(), ssr_resolution, sss_samples,
      sss_stability, shadow_map_resolution, shadow_map_stability, sun_shadow_softness, grass_distance, model_detail,
      unit_detail, unit_animation, unit_variety, terrain_detail.load()));
  for (const auto& shader : SUN_SHADERS) {
    renodx::utils::command_action::Register(
        OnSunDraw, {.shader_hash = shader.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &shader);
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
  twshogun2::hdr_link::Use(DLL_PROCESS_ATTACH);
  attached = true;
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE) {
  if (attached) {
    twshogun2::frame_trace::Stop();
    renodx::utils::command_action::Unregister(OnSunDraw);
    renodx::utils::command_action::Use(DLL_PROCESS_DETACH);
    renodx::utils::state::Use(DLL_PROCESS_DETACH);
    twshogun2::dlss_jitter::Use(DLL_PROCESS_DETACH);
    twshogun2::hdr_link::Use(DLL_PROCESS_DETACH);
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
