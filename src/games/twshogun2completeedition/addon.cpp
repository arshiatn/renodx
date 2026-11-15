/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"
#include "../../utils/swapchain.hpp"
#include "../twattila/ao_filter.hpp"
#include "../twattila/draw_dispatch.hpp"
#include "../twattila/engine_tests.hpp"
#include "../twattila/light_bounce.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/lighting_settings.hpp"
#include "../twattila/settings_pages.hpp"
#include "../twattila/texture_lod_bias.hpp"
#include "../twwarhammer2/line_sweep_ao.hpp"
#include "./foliage_light.hpp"
#include "./frame_trace.hpp"
#include "./game_dlss.hpp"
#include "./grass_distance.hpp"
#include "./model_detail.hpp"
#include "./object_velocity.hpp"
#include "./particle_resolution.hpp"
#include "./render_scale.hpp"
#include "./screen_space_shadows.hpp"
#include "./ray_tracing.hpp"
// RENODX_TW_PROFILE's action: rt_core's Record Ray Tracing Profile (this add-on can't include twattila/rt_scene.hpp).
#define RENODX_TEST_HOOKS_PROFILE twattila::rt_scene::StartProfile
#include "../twattila/test_hooks.hpp"
#include "./shadow_resolution.hpp"
#include "./terrain_detail.hpp"
#include "./tweak_objects.hpp"
#include "./unit_detail.hpp"
#include "./upscaling.hpp"
#include "../twattila/reflex.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "./shared.h"
#include "./vfx_effects.hpp"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
// Settings Mode's page (twattila/settings_pages.hpp AssignPages): HDR & Effects, Graphics 1 of 2, Graphics 2 of 2.
float current_settings_mode = 0.f;
// VFX effect test: 0 off, 1 hide the effect, 2 only the effect; the effect's number (vfx_effects.hpp).
float vfx_test = 0.f;
float vfx_test_effect = 1.f;

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsExtendedMode() {
  return shader_injection.hdr == 1.f;
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f;
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 3.f;
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, UE's film curve).
constexpr float HUE_CLIP_DEFAULT = 70.f;

bool IsHdrOutput() {
  return shader_injection.swap_chain_encoding != 2.f;
}

void ApplyResetPreset() {
  // Peak / Paper White / UI, Settings Mode and output are kept.
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 1.f},
      {"ext_exposure", 1.f},
      {"ext_highlights", 1.f},
      {"ext_shadows", 1.f},
      {"ext_contrast", 1.f},
      {"ext_saturation", 1.f},
      {"ext_highlight_saturation", 1.f},
      {"ext_highlight_hue_shift", 0.33f},
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"cone_response_exponent", 1.f},
      {"saturation", 1.f},
      {"highlight_saturation", 1.f},
      {"roll_off", 1.f},
      {"highlight_hue_shift", 0.46f},
      {"highlight_contrast", 1.f},
      {"shadow_contrast", 1.f},
      {"flare", 0.f},
      {"dechroma", 0.f},
      {"gamut_compression", 1.f},
      {"gamut_compression_mode", 1.f},
      {"highlight_saturation_shift", 0.f},
      {"bloom", 1.f},
      {"lens_flare", 1.f},
      {"vignette", 1.f},
      {"heat_haze", 1.f},
      {"depth_of_field", 1.f},
      {"battle_fog", 1.f},
      {"sun", 1.f},
      {"foliage_glints", 1.f},
      {"vfx_glow", 1.f},
      {"vfx_big_fire", 1.f},
      {"vfx_campfire", 1.f},
      {"vfx_trails", 1.f},
      {"vfx_firelight", 1.f},
      {"vfx_smoke", 1.f},
      {"vfx_smoke_opacity", 1.f},
      {"video_autohdr", 0.f},
      {"video_dithering", 1.f},
      {"auto_exposure_highlight_protection", 0.f},
      {"auto_exposure_highlight_headroom_stops", 4.f},
      {"some_indicators", 1.f},
  });
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
}

// The recommended presets (ATN's settings; 2026-11-12: Musa's PsychoV35 at its defaults): HDR (PsychoV35), no lens
// flares, faint foliage glints, faint haze, brighter VFX and videos. PsychoV31's sliders as before, for that mode.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 3.f},
      {"highlight_saturation", 0.75f},
      {"lens_flare", 0.f},
      {"foliage_glints", 0.15f},
      {"video_autohdr", 0.25f},
      {"heat_haze", 0.1f},
      {"vfx_glow", 5.f},
      {"vfx_big_fire", 1.f},
      {"vfx_campfire", 15.f},
      {"vfx_trails", 2.f},
      {"vfx_firelight", 1.8f},
      {"vfx_smoke", 2.f},
      {"vfx_smoke_opacity", 1.f},
      {"sun", 2.f},
  });
}

// SDR (monitors without HDR), as Attila's: Reset, Rendering Mode SDR (Reset has no video AutoHDR). Its button also sets
// Output SDR; a first start on a display without Windows HDR takes it by itself (output_encoding's first_start_without_hdr).
void ApplySdrPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemapper", 0.f}});
}

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

// The DLSS rows (Graphics 1 of 2's Anti-Aliasing and Upscaling; ReShade.ini keys as the DLSS add-on saved them until it
// became part of this one): the game's DLSS (game_dlss.hpp: its render scale, preset and jitter, render_scale.hpp) and the
// helper's upscaler and frame generation (upscaling.hpp, twattila/frame_generation.hpp), NVIDIA Reflex (twattila/reflex.hpp).
struct AaSliders {
  float method = 0.f;  // HelperUpscaler: 0 the game's DLSS, 5 + twattila::dlaa_bridge::UPSCALER_* (Attila's anti-aliasing values)
  float dlss_mode = 0.f;  // RenderScale: RENDER_SCALES, then Custom
  float render_scale = 50.f;  // RenderScaleCustom: Custom, in percent
  float preset = NVSDK_NGX_DLSS_Hint_Render_Preset_K;  // Preset: the NGX value, PRESETS'
  float ray_reconstruction = 0.f;  // AntiAliasingRayReconstruction: off, NVIDIA's preset, D, E, F
  float frame_generation = 0.f;    // HelperFrameGeneration: off, 2x-6x, Dynamic
  float frame_backend = 0.f;       // FrameGenerationMethod: Auto, DLSS, FSR, XeSS
  float reflex = 0.f;              // off, on, on + boost (frame generation: always on)
  float frame_limit = 0.f;         // rendered frames per second (0: none)
  float hud_recomposition = 1.f;
  float frame_preset = 0.f;      // the driver's, A, B, C
  float frame_debug_view = 0.f;  // off, the HUD-less image, the UI layer
  float texture_lod_bias = 2.f;  // below 100%: off, matched, sharper (NVIDIA's), sharpest
  float lod_bias_trilinear = 0.f;  // twattila/texture_lod_bias.hpp SetWrappingTrilinear
  float debug_view = 0.f;        // UpscalingDebugView: 1 the motion the helper's upscaler gets
  float jitter = 1.f;            // the camera jitter the game leaves off with its DLSS
  float object_motion = 1.f;     // ships', sails', ropes' and water's motion vectors (object_velocity.hpp)
  float jitter_flip = 0.f;       // the game's, X, Y or both flipped
} aa_sliders;
constexpr std::array<float, 6> RENDER_SCALES = {1.f, 0.77f, 2.f / 3.f, 0.58f, 0.5f, 1.f / 3.f};
constexpr std::array<uint32_t, 5> RAY_RECONSTRUCTION_PRESETS = {0, 0, 4, 5, 6};  // NVSDK_NGX_RayReconstruction_Hint_Render_Preset
constexpr std::array<uint32_t, 4> FRAME_BACKENDS = {twattila::dlaa_bridge::FG_BACKEND_AUTO, twattila::dlaa_bridge::FG_BACKEND_DLSS,
                                                    twattila::dlaa_bridge::FG_BACKEND_FSR, twattila::dlaa_bridge::FG_BACKEND_XESS};
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

bool HelperUpscaling() {
  return aa_sliders.method >= 5.f && twattila::dlaa_bridge::HelperInstalled();
}

bool DlssRayReconstruction() {
  return aa_sliders.method == 5.f && aa_sliders.ray_reconstruction != 0.f;
}

float SelectedRenderScale() {
  const auto mode = static_cast<size_t>(std::clamp(aa_sliders.dlss_mode, 0.f, static_cast<float>(RENDER_SCALES.size())));
  return (mode < RENDER_SCALES.size()) ? RENDER_SCALES[mode] : std::clamp(aa_sliders.render_scale, 1.f, 100.f) / 100.f;
}

// The helper's upscaler or frame generation takes the game's DLSS evaluations (present thread; the evaluations read it).
std::atomic_bool helper_evaluates = false;
// The jitter of the frame the game's DLSS evaluated last (its InJitterOffset, render pixels), for the passes after it
// that read textures drawn with it (the heat haze's distortion: tonemap01). 0 while the game's DLSS does not run.
std::atomic<float> dlss_jitter_x = 0.f;
std::atomic<float> dlss_jitter_y = 0.f;
uint64_t last_evaluations = 0;              // present thread
std::array<uint32_t, 2> evaluated_size = {};  // render thread

// Graphics 2 of 2: the game's distances and its shadow map, as the game's x these (their choices' scales).
constexpr std::array<float, 11> GRASS_DISTANCES = {1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f, 24.f, 32.f};
constexpr std::array<float, 8> MODEL_DETAILS = {1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f, 16.f};
constexpr std::array<float, 7> UNIT_DETAILS = {1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f};
constexpr std::array<float, 5> UNIT_ANIMATIONS = {1.f, 1.5f, 2.f, 3.f, 4.f};  // UnitAnimation's and UnitVariety's
// The highest Shadows setting's shadow map (shadow_resolution.hpp; 0: the game's 4096). 14336 made the game quit (ATN).
constexpr std::array<uint32_t, 3> SHADOW_MAP_SIZES = {0, 8192, 12288};
struct GraphicsRows {
  float particle_resolution = 0.f;  // particle_resolution.hpp: 0 the game's minimum, 1 its share of the display
  float shadow_map_resolution = 0.f;
  float grass_distance = 0.f;
  float model_detail = 0.f;
  float unit_detail = 0.f;
  float unit_animation = 0.f;
  float unit_variety = 0.f;
  float terrain_detail = 0.f;  // terrain_detail.hpp: 1 the finest level everywhere
} graphics_rows;
// Engine values (Experimental): Attila's Graphics 2 of 2 rows whose values Shogun 2's engine has (tweak_objects.hpp), each
// x the game's, in Graphics (Experimental); the campaign rows' tooltips with Shogun's own values. Built at the first
// use: Attila's rows and the key list are inline globals, whose initializers clang-cl's MSVC ABI may run after this file's
// (a global built from them came out empty in the page dump).
std::vector<twattila::engine_tests::EngineTest>& EngineTests() {
  static std::vector<twattila::engine_tests::EngineTest> tests = [] {
    auto rows = twattila::engine_tests::Only(twattila::engine_tests::Attila(), twshogun2::tweak_objects::ATTILA_TESTS);
    for (auto& row : rows) {
      row.section = twattila::feature_settings::GRAPHICS_SECTION;
      row.label += " (Experimental)";
      if (row.key == "GraphicsCampaignSunCameraDepth") row.tooltip = "How far towards the sun campaign shadows take in objects. Game: 20.";
      if (row.key == "GraphicsCampaignClouds") row.tooltip = "The campaign clouds' cull factor. Game: 1.";
    }
    return rows;
  }();
  return tests;
}
std::vector<tw::engine_tweaks::Option*> engine_options;
bool engine_values_found = false;
renodx::utils::settings::Setting engine_values_status;

// Ray tracing (Experimental, ray_tracing.hpp; Graphics 1 of 2's Ray Tracing, ReShade.ini RayTraced* / RayTracing* /
// PathTracing*, Attila's keys where it has the row): installed when an effect is on at launch, with the helper.
struct RaySliders {
  float sun_shadows = 0.f;
  float sun_softness = 0.5f;
  float leaf_light = 0.f;
  float coloured_shadows = 0.f;
  float sprite_shadows = 0.f;
  float far_tree_shadows = 0.f;
  float units_out_of_view = 0.f;
  float untraced_casters = 0.f;
  float sheets_unselfed = 0.f;
  float smoke_shadows = 0.f;
  float smoke_quality = 1.f;  // Low to Maximum
  float smoke_fast_build = 0.f;
  float grass_shadow_range = 0.f;  // metres (0: everywhere)
  float ambient_occlusion = 0.f;
  float ambient_range = 2.f;
  float ambient_rays = 2.f;
  float ambient_quarter = 0.f;
  float ao_reblur = 0.f;  // AO under Ray Reconstruction: Light History, REBLUR
  float local_lights = 0.f;
  float light_softness = 0.25f;
  float light_count = 0.f;  // Torch and Fire Shadow Lights: 32, 64, 128
  float light_importance = 0.f;
  float light_steady = 0.f;
  float light_range = 0.f;  // metres (0: any distance)
  float fog_shadows = 0.f;
  float light_glow = 0.f;
  float reflections = 0.f;
  float reflection_samples = 1.f;
  float reflection_reuse = 0.f;  // 0 off, 1 from ReSTIR GI, 2 Own ReSTIR
  float water_reflections = 0.f;
  float water_steady = 0.f;
  float water_soft = 0.f;
  float specular_occlusion = 0.f;
  float light_bounce = 0.f;  // the Global Illumination's light from rays (Shogun's: off or on; PathTracing: paths)
  float light_bounce_rays = 4.f;
  float light_bounce_range = 10.f;
  float light_bounce_resolution = 1.f;  // full, half, quarter
  float path_tracing = 0.f;
  float path_bounces = 2.f;
  float sharc = 0.f;
  float reuse_lean = 0.f;
  float lights_in_bounce = 0.f;
  float light_ratio = 0.f;
  float path_ambient = 0.f;
  float path_sky_apart = 0.f;
  float path_kept_cube = 0.f;
  float path_ser = 0.f;
  float denoiser = 0.f;  // Built-in, NRD
  float micromaps = 0.f;
  float gpu_micromaps = 1.f;
  float fast_leaves = 0.f;
  float output_rays = 0.f;  // Rays a Pixel: 1, Match Output, 2-8
  float rays_shadows = 1.f;  // what takes them
  float rays_ambient = 1.f;
  float rays_reflections = 1.f;
  float rays_paths = 1.f;
  float debug_log = 0.f;
  float debug_view = 0.f;  // rt_scene's Debug View values (Off, its effects, All)
  float scene_test = 0.f;
} rt_sliders;
bool ray_tracing_installed = false;

// The ray tracing's settings from its rows (at attach and each present), Rays a Pixel not counted yet (OnPresent: Match
// Output follows the render scale).
twattila::rt_scene::Settings RaySettings() {
  const auto& rows = rt_sliders;
  const auto on = [](float value) { return value != 0.f; };
  const auto count = [](float value, int min, int max) { return static_cast<uint32_t>(std::clamp(static_cast<int>(value), min, max)); };
  twattila::rt_scene::Settings rays;
  rays.sun_shadows = on(rows.sun_shadows);
  rays.sun_softness = std::clamp(rows.sun_softness, 0.f, 3.f);
  rays.leaf_light = on(rows.leaf_light);
  rays.coloured_shadows = rays.leaf_light && on(rows.coloured_shadows);
  rays.sprite_shadows = on(rows.sprite_shadows);
  rays.far_tree_shadows = on(rows.far_tree_shadows);
  rays.casters_out_of_view = rays.sun_shadows && on(rows.units_out_of_view);
  // Attila's Smooth Banner Shadows (RT_SHEETS_UNSELFED: the cloth the pixel shows doesn't shade itself) and Grass Shadow Range
  // (rays from pixels farther than it pass the grass), both the helper's.
  rays.sheets_unselfed = on(rows.sheets_unselfed);
  twshogun2::ray_tracing::untraced_casters.store(on(rows.untraced_casters), std::memory_order_relaxed);
  rays.grass_shadow_range = std::clamp(rows.grass_shadow_range, 0.f, 200.f);
  rays.ambient_occlusion = on(rows.ambient_occlusion);
  rays.ambient_range = std::clamp(rows.ambient_range, 0.25f, 5.f);
  rays.ambient_rays = count(rows.ambient_rays, 1, 8);
  rays.ambient_quarter = on(rows.ambient_quarter);
  // Attila's AO under Ray Reconstruction: 1 REBLUR (RT_AO_REBLUR, only while Ray Reconstruction runs), 0 the light history.
  rays.ao_reblur = on(rows.ao_reblur);
  rays.reflections = on(rows.reflections);
  rays.specular_occlusion = on(rows.specular_occlusion);
  rays.water_reflections = on(rows.water_reflections);
  rays.reflection_samples = count(rows.reflection_samples, 1, 8);
  rays.water_steady = on(rows.water_steady);
  // Softer Water Reflections (Attila's): the water's guide rougher (ray_tracing.hpp WaterSurface), its reflections' history
  // from the water's own positions (rt_core's RT_REFLECTIONS_WATER_POSITIONS).
  rays.water_soft = rays.water_reflections && on(rows.water_soft);
  rays.local_lights = on(rows.local_lights);
  rays.fog_shadows = on(rows.fog_shadows);
  // Smoke Shadows (Attila's keys; ray_tracing.hpp SMOKE_VERTEX_SHADERS): Low to Maximum, 1 Medium.
  rays.smoke_shadows = rays.sun_shadows && on(rows.smoke_shadows);
  rays.smoke_quality = count(rows.smoke_quality, 0, 5);
  // Faster Smoke Shadow Build (Attila's key; helper113's RT_SCENE_SMOKE_FAST_BUILD).
  rays.smoke_fast_build = rays.smoke_shadows && on(rows.smoke_fast_build);
  // Torch and Fire Light in Fog (Attila's key and range; fog01): 0 the helper traces none, as before.
  rays.light_glow = std::clamp(rows.light_glow, 0.f, 2.f);
  // Torch and Fire Shadow Lights: 32, 64, 128.
  rays.light_count = twattila::dlaa_bridge::RT_MAX_LIGHTS << count(rows.light_count, 0, 2);
  rays.light_importance = on(rows.light_importance);
  rays.light_steady = on(rows.light_steady);
  rays.light_range = std::clamp(rows.light_range, 0.f, 500.f);
  rays.light_bounce = on(rows.light_bounce);
  // Reflection Reuse (Attila's): 1 from ReSTIR GI (the ray-traced Global Illumination's), 2 Own ReSTIR.
  const uint32_t reflection_reuse = count(rows.reflection_reuse, 0, 2);
  rays.reflection_reuse = reflection_reuse == 1 && rays.light_bounce;
  rays.reflection_own = reflection_reuse == 2;
  rays.light_bounce_range = std::clamp(rows.light_bounce_range, 1.f, 50.f);
  rays.light_bounce_rays = count(rows.light_bounce_rays, 1, 16);
  rays.light_bounce_resolution = static_cast<float>(count(rows.light_bounce_resolution, 0, 2));
  rays.path_tracing = on(rows.path_tracing);
  // Path-Traced AO (twattila/rt_scene.hpp SettingsOf's): the paths bring the sky's light, the ray-traced AO off. Needs
  // the ray-traced Global Illumination's paths and the Global Illumination (its apply adds them).
  rays.path_ambient = rays.path_tracing && rays.light_bounce && shader_injection.light_bounce_strength > 0.f && on(rows.path_ambient);
  if (rays.path_ambient) rays.ambient_occlusion = false;
  // Own Sky Rays and Steady Bounce Light (rt_sun's texel 3 w 1 and 4 w 1: its cube is the sky, in the game's units).
  rays.path_sky_apart = on(rows.path_sky_apart);
  rays.path_kept_cube = on(rows.path_kept_cube);
  // 3 and 4 measured as 2 in Attila (twattila/rt_scene.hpp SettingsOf): the key's older values taken as 2.
  rays.path_bounces = count(rows.path_bounces, 1, 2);
  rays.sharc = rays.path_tracing && on(rows.sharc);
  rays.reuse_lean = rays.path_tracing && on(rows.reuse_lean);
  // Torch and Fire Light / Shadows from Path Tracing (Attila's keys; rt_core.hpp Settings::lights_in_bounce, light_ratio).
  rays.lights_in_bounce = on(rows.lights_in_bounce);
  rays.light_ratio = on(rows.light_ratio);
  rays.path_ser = on(rows.path_ser);
  rays.micromaps = on(rows.micromaps);
  rays.gpu_micromaps = on(rows.gpu_micromaps);
  rays.fast_leaves = on(rows.fast_leaves);
  rays.debug_view = count(rows.debug_view, 0, 9);
  rays.scene_test = (rays.debug_view != 0) ? count(rows.scene_test, 0, 4) : 0u;
  rays.debug_log = on(rows.debug_log);
  // With the helper's upscaler (OnPresent): NRD, the torches' size under Ray Reconstruction.
  rays.nrd = on(rows.denoiser);
  rays.light_size = std::clamp(rows.light_softness, 0.f, 1.f);
  return rays;
}

// Coloured Shadows (ScreenSpaceShadowsColoured, Experimental; 1 on): screen_space_shadows.hpp's banner filter.
float sss_coloured = 0.f;
// Between the reflections' HiZ and their resolve (the generic mip passes there are theirs; render thread).
bool reflection_passes = false;
// GT-VBAO's Temporal Smoothing, Temporal Edge Fix and Denoiser (Experimental; Attila's keys ssao_temporal / ssao_edge_fix
// / ssao_denoise): twattila/ao_filter.hpp on the AO the sun light draws read, as Warhammer II's.
struct AoFilterRows {
  float temporal = 0.f;
  float edge_fix = 0.f;
  float denoise = 0.f;
} ao_filter_rows;
std::atomic_bool ao_filter_on = false;  // a row on with GT-VBAO (present thread; the sun draws read it)
// renodx-twshogun2completeedition_dlss.addon64 (the DLSS add-on, part of this one now) still next to this one: it would
// hook the game's DLSS a second time.
bool old_dlss_addon_found = false;
bool game_hooks_attached = false;  // the game's DLSS, render scale and engine values (AttachAddon)

// The AO filter at the sun light draws: their AO at gbuffer_slot - 2 (t_ssao: t5, the campaign map's t7), the G-buffer depth at
// gbuffer_slot + 3 (t10, t12) and camera_VS_PS at b0, read from the game's bytes (camera_position at 0, view_projection at 160,
// inv_view_projection at 480; shaders/sss.hlsli's layout), copied into Attila's layout for the filter's shaders.
constexpr std::array<twattila::frame_history::CameraRun, 8> CAMERA_LAYOUT = {{
    {.destination = 0, .source = 0, .bytes = 12},       // camera_position
    {.destination = 16, .source = 32, .bytes = 192},    // view, projection, view_projection (c2-c13)
    {.destination = 208, .source = 288, .bytes = 64},   // inv_view (c18)
    {.destination = 272, .source = 416, .bytes = 148},  // inv_projection, inv_view_projection, camera_near_far, time (c26-c35.x)
    {.destination = 420, .source = 576, .bytes = 12},   // g_inverse_focal_length, g_vertical_fov (c36)
    {.destination = 432, .source = 592, .bytes = 16},   // screen_size (c37)
    {.destination = 448, .source = 568, .bytes = 4},    // g_texel_offset (c35.z)
    {.destination = 464, .source = 608, .bytes = 16},   // g_viewport_dimensions (c38)
}};
constexpr auto AO_FILTER_DRAWS = [] {
  auto draws = twshogun2::ray_tracing::SUN_DRAWS;
  for (auto& draw : draws) {
    draw.ao_slot = draw.gbuffer_slot - 2;
    draw.camera_layout = CAMERA_LAYOUT;
  }
  return draws;
}();

// Render scale (render_scale.hpp draws the 3D passes into the top-left part of the full-size targets) ////////////////
// The rendered fraction for the depth of field, the auto exposure and the tonemap's distortion lookup: a 3D pass's
// viewport over its back-buffer-size target (others: shadows, half size, the UI are skipped). Passes before the capture
// use the camera's g_viewport_dimensions instead (patched to the render size by render_scale.hpp).
namespace render_scale {

std::atomic<uint32_t> back_buffer_width = 0;
std::atomic<uint32_t> back_buffer_height = 0;

void Set(float x, float y) {
  shader_injection.render_scale_x = x;
  shader_injection.render_scale_y = y;
}

bool OnDrawCapture(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  if (viewport_count == 0 || viewport.Width <= 0.f || viewport.Height <= 0.f || viewport.TopLeftX != 0.f
      || viewport.TopLeftY != 0.f) {
    return true;
  }
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (target == nullptr) return true;
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;
  target->GetResource(resource.GetAddressOf());
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (FAILED(resource.As(&texture))) return true;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (desc.Width == 0 || desc.Width != back_buffer_width.load() || desc.Height != back_buffer_height.load()) return true;
  const float x = viewport.Width / static_cast<float>(desc.Width);
  const float y = viewport.Height / static_cast<float>(desc.Height);
  if (x < 0.05f || y < 0.05f) return true;
  Set(std::min(x, 1.f), std::min(y, 1.f));
  return true;
}

}  // namespace render_scale

// The AO filter's 3D size (twattila::frame_history::RenderSize; below 100% render_scale.hpp draws the 3D image into the top-left
// of the display-size targets): the sun light draw's viewport, 0 at the display's size. Before the filter's callback.
constexpr auto OnAoFilterSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!ao_filter_on.load(std::memory_order_relaxed)) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  native->RSGetViewports(&viewport_count, &viewport);
  const bool scaled = viewport_count != 0 && viewport.Width < static_cast<float>(render_scale::back_buffer_width.load());
  twattila::frame_history::render_width.store(scaled ? static_cast<uint32_t>(viewport.Width) : 0u, std::memory_order_relaxed);
  twattila::frame_history::render_height.store(scaled ? static_cast<uint32_t>(viewport.Height) : 0u, std::memory_order_relaxed);
  static std::atomic_bool applied_logged = false;
  if (!applied_logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i(std::format("[Shogun 2] AO filter: applied at a sun light draw ({} x {})", viewport.Width, viewport.Height));
  }
  return {};
};

// The sea's colour overlay (selection rings, firing arcs, wakes, bow waves: indicator ps30_main_to_overlay, wake,
// seainteraction; drawn only there): the ocean reads it at the full-screen position (its decal projection), but with the
// render scale its draws went into the top-left part (ATN debug19, 33%: rings and wakes at a third of their
// size in the top-left). Its draws use the whole back-buffer-size target, the game's viewport restored after each.
namespace sea_overlay {

D3D11_VIEWPORT game_viewport = {};
bool restore = false;

bool OnDraw(reshade::api::command_list* cmd_list) {
  restore = false;
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &game_viewport);
  const uint32_t width = render_scale::back_buffer_width.load();
  const uint32_t height = render_scale::back_buffer_height.load();
  if (viewport_count == 0
      || (game_viewport.Width >= static_cast<float>(width) && game_viewport.Height >= static_cast<float>(height))) {
    return true;
  }
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
  context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  if (target == nullptr) return true;
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;
  target->GetResource(resource.GetAddressOf());
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (FAILED(resource.As(&texture))) return true;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (desc.Width != width || desc.Height != height) return true;
  const D3D11_VIEWPORT full = {
      .TopLeftX = 0.f,
      .TopLeftY = 0.f,
      .Width = static_cast<float>(desc.Width),
      .Height = static_cast<float>(desc.Height),
      .MinDepth = game_viewport.MinDepth,
      .MaxDepth = game_viewport.MaxDepth,
  };
  context->RSSetViewports(1, &full);
  restore = true;
  return true;
}

void OnDrawn(reshade::api::command_list* cmd_list) {
  if (!restore) return;
  restore = false;
  reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->RSSetViewports(1, &game_viewport);
}

}  // namespace sea_overlay

using Values = std::vector<std::pair<std::string, float>>;

// Graphics 2 of 2's Reset (Recommended: it, then ATN's settings).
const Values GRAPHICS_RESET = {
    {"SSRResolution", 1.f}, {"ParticleResolution", 0.f}, {"SSSSamples", 0.f}, {"SSSStability", 0.f},
    {"ShadowMapResolution", 0.f}, {"ShadowMapStability", 0.f}, {"GrassDistance", 0.f}, {"ModelDetail", 0.f},
    {"TerrainDetail", 0.f}, {"UnitDetail", 0.f}, {"UnitAnimation", 0.f}, {"UnitVariety", 0.f},
};
// ATN's presets (as the other Total War games'): ambient occlusion Faster / Heavier (Recommended, the defaults) / Reset
// (the game's), Global Illumination Faster (Recommended) / Heavier (More accurate) / Reset (off, the defaults).
const Values AO_DEFAULTS = {
    {"AmbientOcclusion", 1.f}, {"AmbientOcclusionStrength", 1.f}, {"AmbientOcclusionRadius", 12.5f}, {"AmbientOcclusionThickness", 0.1f},
    {"AmbientOcclusionQuality", 2.f}, {"AmbientOcclusionDirections", 0.f}, {"AmbientOcclusionResolution", 0.f},
    {"AmbientOcclusionAutoThickness", 1.f}, {"ssao_temporal", 0.f}, {"ssao_edge_fix", 0.f}, {"ssao_denoise", 0.f},
};
const Values GI_DEFAULTS = {
    {"LightBounceStrength", 0.f}, {"LightBounceRadius", 20.f}, {"LightBounceResolution", 2.f}, {"LightBounceQuality", 3.f},
    {"LightBounceDirections", 0.f}, {"LightBounceTemporal", 1.f}, {"LightBounceDenoise", 1.f}, {"LightBounceMultiBounce", 0.f},
    {"LightBounceCheckerboard", 1.f}, {"LightBounceBackFace", 0.f}, {"LightBounceAutoThickness", 1.f},
};
const std::string RAYS_SECTION = "Ray Tracing (Experimental, restart after turning on)";
constexpr uint32_t RAYS_TINT = 0x6BE0D8;
// The Ray Tracing presets (twattila/graphics_page.hpp's, in this section's keys): RT/PT writes RAYS_RESET, then
// RAYS_PRESET; No RT/PT RAYS_OFF; Reset both.
const Values RAYS_OFF = {
    {"RayTracedSunShadows", 0.f}, {"RayTracedAmbientOcclusion", 0.f}, {"RayTracedLocalLights", 0.f},
    {"RayTracedReflections", 0.f}, {"RayTracedLightBounce", 0.f}, {"PathTracing", 0.f},
    {"RayTracedFogShadows", 0.f}, {"RayTracedPathAmbient", 0.f}, {"RayTracedSpecularOcclusion", 0.f},
};
const Values RAYS_PRESET = {
    {"RayTracedSunShadows", 1.f},
    {"RayTracedShadowSoftness", 0.8f},
    {"RayTracedLeafLight", 1.f},
    {"RayTracedColouredShadows", 1.f},
    {"RayTracedSpriteShadows", 1.f},
    {"RayTracedFarTreeShadows", 1.f},
    {"RayTracedUnitsOutOfView", 1.f},
    {"RayTracedSheetsUnselfed", 1.f},
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
    {"RayTracedPathAmbient", 1.f},
    {"RayTracedPathSkyApart", 1.f},
    {"RayTracedPathKeptCube", 1.f},
    {"RayTracedReflections", 1.f},
    {"RayTracedReflectionSamples", 1.f},
    {"RayTracedReflectionReuse", 2.f},
    {"RayTracedWaterReflections", 1.f},
    {"RayTracedWaterSteady", 1.f},
    {"RayTracedWaterSoft", 1.f},
    {"RayTracedSpecularOcclusion", 1.f},
    {"RayTracedFogShadows", 1.f},
    {"RayTracedLeafMicromaps", 1.f},
    // Attila's RT/PT values for this game's own rows (ATN: RT/PT as Attila's; still Experimental, off by default).
    {"RayTracedLightGlow", 1.f},
    {"RayTracedSmokeShadows", 1.f},
    {"RayTracedSmokeQuality", 3.f},
    {"WaterClarity", 1.f},
    {"WaterCaustics", 1.f},
};
const Values RAYS_RESET = {
    {"RayTracedShadowSoftness", 0.5f},
    {"RayTracedLeafLight", 0.f},
    {"RayTracedColouredShadows", 0.f},
    {"RayTracedSpriteShadows", 0.f},
    {"RayTracedFarTreeShadows", 0.f},
    {"RayTracedUnitsOutOfView", 0.f},
    {"RayTracedUntracedCasters", 0.f},
    {"RayTracedSheetsUnselfed", 0.f},
    {"RayTracedGrassShadowRange", 0.f},
    {"RayTracedAoReblur", 0.f},
    {"RayTracedLightGlow", 0.f},
    {"RayTracedSmokeShadows", 0.f},
    {"RayTracedSmokeQuality", 1.f},
    {"RayTracedSmokeFastBuild", 0.f},
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
    {"RayTracedWaterSoft", 0.f},
    {"RayTracedLightBounceRays", 4.f},
    {"RayTracedLightBounceRange", 10.f},
    {"RayTracedLightBounceResolution", 1.f},
    {"PathTracingBounces", 2.f},
    {"RayTracedSharc", 0.f},
    {"RayTracedReuseLean", 0.f},
    {"RayTracedLightsInBounce", 0.f},
    {"RayTracedLightShadowRatio", 0.f},
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
// rt_scene's Debug View values (Off, its effects, All; the others: nothing here makes them).
struct RaysDebugView {
  int value;
  const char* label;
};
constexpr std::array<RaysDebugView, 7> RAYS_DEBUG_VIEWS = {{
    {.value = 0, .label = "Off"},
    {.value = 1, .label = "Sun Shadows"},
    {.value = 2, .label = "Ambient Occlusion"},
    {.value = 3, .label = "Global Illumination"},
    {.value = 4, .label = "Reflections"},
    {.value = 6, .label = "Point Light Shadows"},
    {.value = 9, .label = "All"},
}};

void BuildRuntimeData() {
  custom_shaders = {
      CustomShaderEntry(0xEF060C7C),  // tonemap00 (hdr_to_screen)
      CustomShaderEntry(0xBEDBEFD8),  // tonemap01 (hdr_to_screen, distortion)
      CustomShaderEntry(0x3D56DB58),  // uiresolve00 (ui_resolve)
      CustomShaderEntry(0x3208750E),  // hdrflare00 (hdr_lenseflare: bloom + flare ghosts)
      CustomShaderEntry(0xD6708726),  // flare00 (lenseflare flare_0)
      CustomShaderEntry(0x5C6F73D3),  // flare01
      CustomShaderEntry(0x7645BCB0),  // flare02
      CustomShaderEntry(0xE23F06FF),  // flare03
      CustomShaderEntry(0x658E9237),  // vignette00 (lenseflare vignette)
      CustomShaderEntry(0x77F0657A),  // doffocus00 (depth of field auto focus)
      CustomShaderEntry(0x6449D186),  // dofblur00 (depth of field blur)
      CustomShaderEntry(0x32876DB2),  // dofblur01 (depth of field masked blur)
      CustomShaderEntry(0x962CEB71),  // ssrcopy00 (screen-space reflections onto the scene)
      CustomShaderEntry(0x5EAD0FF6),  // highlight00 (gbuffer_highlight)
      CustomShaderEntry(0x6531CCC3),  // dofblur02 (depth of field blur, 8-bit)
      CustomShaderEntry(0xB25F26C7),  // dofcoc00 (depth of field circle of confusion + bokeh, through the heat haze)
      CustomShaderEntry(0xB333A1B3),  // dofcoc01 (without bokeh)
      CustomShaderEntry(0x34ECD06E),  // sss00 (sun shadow mask: screen-space shadows + cascades)
      CustomShaderEntry(0x1B529672),  // sss01 (screen-space shadows without cascades)
      CustomShaderEntry(0x78E88E1D),  // sss02 (cascades only, screen-space shadows off)
      // Remastered CE's builds of sss00 / sss01 (its edge rejection and banner exclusion): ours.
      {0x9147EB4E, renodx::mods::shader::CreateCustomShader(0x9147EB4E, __0x34ECD06E)},
      {0x7FA1D600, renodx::mods::shader::CreateCustomShader(0x7FA1D600, __0x1B529672)},
      CustomShaderEntry(0x97624E56),  // ssrhiz00 (reflections' HiZ base level)
      CustomShaderEntry(0x5C0AF498),  // ssrray00 (reflection rays)
      CustomShaderEntry(0x13C66077),  // ssrfilter00 (hit buffer blur x)
      CustomShaderEntry(0x073A6CF3),  // ssrfilter01 (hit buffer blur y)
      CustomShaderEntry(0xF87ECE85),  // ssrresolve00 (reflection colour)
      CustomShaderEntry(0x16ADFD60),  // ocean00 (ocean high: Ray-Traced Water Reflections)
      CustomShaderEntry(0x6B8954A6),  // ocean01 (ocean ultra: Ray-Traced Water Reflections)
      CustomShaderEntry(0x5487502A),  // ocean02 (ocean low: Ray-Traced Water Reflections)
      CustomShaderEntry(0xE73D4A5E),  // ocean03 (ocean medium: Ray-Traced Water Reflections)
      CustomShaderEntry(0x9B9EC116),  // waterplane00 (battle waterplane high / medium / ultra: Ray-Traced Water Reflections)
      CustomShaderEntry(0x892D56F9),  // waterplane01 (battle waterplane low: Ray-Traced Water Reflections)
      CustomShaderEntry(0xA00FE3A3),  // ssao00 (ambient occlusion line sweeps)
      CustomShaderEntry(0x98BB1159),  // ssao01 (ambient occlusion collect: the Visibility Bitmask AO)
      CustomShaderEntry(0x5CBF4FB6),  // pointlight00
      CustomShaderEntry(0x57139F8D),  // pointlight01 (8-bit light accumulation)
      CustomShaderEntry(0x70CAA144),  // particlelight00 (light from particles)
      CustomShaderEntry(0xA583B6F4),  // ribbon00 (ribbon particles)
      CustomShaderEntry(0x8233E11F),  // autoexposure00
      CustomShaderEntry(0xA61D2853),  // fog00 (battle fog extinction)
      CustomShaderEntry(0x4355E897),  // fog01 (battle fog haze)
      CustomShaderEntry(0x3E33E81A),  // sun00 (sun disc)
      CustomShaderEntry(0x8BCB52BB),  // video00 (sprite pixel_yuv: the videos)
      CustomShaderEntry(0xAECABA3F),  // dissolve00 (rigidstandard alpha test G-buffer: fade pattern on the pixels)
      CustomShaderEntry(0x557A56D5),  // dissolve01 (rigidvertexwind G-buffer)
      CustomShaderEntry(0xED1BED93),  // sunlight00 (directional light: the sun)
      CustomShaderEntry(0x5496DE73),  // sunlight01 (8-bit)
      CustomShaderEntry(0xC3E0E939),  // sunlight02 (with the map overlay)
      CustomShaderEntry(0xD5FDDE8F),  // sunlight03 (with the map overlay, 8-bit)
      CustomShaderEntry(0x09117812),  // treevelocity00 (rigidleaf_v3 G-buffer: motion vectors)
      CustomShaderEntry(0xB670BCA1),  // treevelocity01 (rigidleafwet_v3)
      CustomShaderEntry(0x29EB778A),  // treevelocity02 (rigidbranch_v3)
      CustomShaderEntry(0x3A9E203D),  // treevelocity03 (rigidbranchwet_v3)
      CustomShaderEntry(0xD7E27B35),  // campaignoverlay00 (mtbcampaignterrainoverlay: map positions below 100%)
      CustomShaderEntry(0xD12FA660),  // indicator00 (indicator ps30_main: Indicator Brightness)
      CustomShaderEntry(0xC454C1EA),  // indicator01 (indicator ps30_main_deferred)
      CustomShaderEntry(0x3C9BC2F9),  // vfxoverlay00 (effects_overlay: the particles onto the scene)
      CustomShaderEntry(0x40744308),  // vfxglow00 (volumetric particles, additive)
      CustomShaderEntry(0xF70E8B73),  // vfxglow01 (volumetric particle splats, additive)
      CustomShaderEntry(0x03F58817),  // projectiletrail00 (arrow, bullet and artillery trails)
      CustomShaderEntry(0xBA0315BE),  // glowmesh00 (rigidglowcolour: burning buildings' fire panels)
      CustomShaderEntry(0x546A3049),  // glowmesh01 (rigidglowtexture)
      CustomShaderEntry(0xBDFF60D4),  // clothvelocity00 (cloth vertex shader: banners' motion vectors while paused)
      CustomShaderEntry(0x40E181C2),  // flagvelocity00 (rigidtextureblendwind: wind-blown flags, the same while paused)
      CustomShaderEntry(0xF45347E8),  // uifigure00 (variantweightedvtf ps30_main_ui: menu figures, clamped)
      CustomShaderEntry(0xA406D4BE),  // uifigure01 (rigidstandard ps30_main_ui)
      CustomShaderEntry(0xF7103E54),  // campaignwater00 (waterplane campaign high / medium / ultra: the map overlay)
      CustomShaderEntry(0x630BA7A4),  // campaignwater01 (waterplane campaign low)
  };

  // Ambient Occlusion: Visibility Bitmask: the line sweep skipped (its camera kept), the addon's own shader in place of
  // the collect (ao_bitmask_collect, with the collect's depth and the sweep's camera at b0); the collect's replacement
  // stays the game's code (Game's costs what it did). The first frame (no camera kept yet): nothing.
  custom_shaders[0xA00FE3A3].on_draw = [](auto* cmd_list) {
    tw::line_sweep_ao::OnBlur(cmd_list);
    return shader_injection.ssao_mode < 1.f;
  };
  custom_shaders[0x98BB1159].on_draw = [](auto* cmd_list) {
    if (shader_injection.ssao_mode < 1.f) return true;
    if (!tw::line_sweep_ao::OnCollect(cmd_list, true, false, 0, 0)) return false;
    tw::line_sweep_ao::DispatchInstead(cmd_list, __ao_bitmask_collect, &shader_injection, sizeof(shader_injection), false,
                                       __ao_upsample_collect, shader_injection.ssao_resolution != 0.f);
    tw::line_sweep_ao::OnCollectDone(cmd_list);
    return false;
  };

  // Reflections: the ray-traced ones in the resolve (ssrresolve00, twattila/rt_reflection.hlsli), its lit scene kept for
  // the next frame's; while they replace the screen-space march, what only the march reads is left out: its HiZ
  // (ssrhiz00 and its levels, cs 0xE2CE69ED), the scene's and the hit buffer's mip chains (cs 0x13A7F7D0, 0x4B546569,
  // 0x80196ECA: generic, left out only between the HiZ and the resolve), the march (ssrray00) and its blurs.
  custom_shaders[0x97624E56].on_draw = [](auto* cmd_list) {
    reflection_passes = ray_tracing_installed;
    if (ray_tracing_installed) twshogun2::ray_tracing::WaterSurface(cmd_list);
    return !ray_tracing_installed || twattila::rt_scene::OnReflectionMarch(cmd_list);
  };
  custom_shaders[0xF87ECE85].on_draw = [](auto* cmd_list) {
    reflection_passes = false;
    if (ray_tracing_installed) {
      twattila::rt_scene::BindReflections(cmd_list, true, true);
      twshogun2::ray_tracing::BindWaterMask(cmd_list, true, true);
    }
    return true;
  };
  custom_shaders[0xF87ECE85].on_drawn = [](auto* cmd_list) {
    if (!ray_tracing_installed) return;
    twattila::rt_scene::BindReflections(cmd_list, true, false);
    twshogun2::ray_tracing::BindWaterMask(cmd_list, true, false);
    twshogun2::ray_tracing::KeepColor(cmd_list);
  };
  // Ray-Traced Water Reflections (ray_tracing.hpp OnSeaDraw): the sea's plane, the rays' reflections for shaders/ocean00-03.
  for (const uint32_t hash : {0x16ADFD60u, 0x6B8954A6u, 0x5487502Au, 0xE73D4A5Eu}) {
    custom_shaders[hash].on_draw = [](auto* cmd_list) {
      if (ray_tracing_installed) twshogun2::ray_tracing::OnSeaDraw(cmd_list, true);
      return true;
    };
    custom_shaders[hash].on_drawn = [](auto* cmd_list) {
      if (ray_tracing_installed) twshogun2::ray_tracing::OnSeaDraw(cmd_list, false);
    };
  }
  for (const uint32_t hash : {0x5C0AF498u, 0x13C66077u, 0x073A6CF3u}) {
    custom_shaders[hash].on_draw = [](auto* cmd_list) { return !ray_tracing_installed || twattila::rt_scene::OnReflectionMarch(cmd_list); };
  }
  for (const uint32_t hash : {0xE2CE69EDu, 0x13A7F7D0u, 0x4B546569u, 0x80196ECAu}) {
    custom_shaders[hash] = {
        .crc32 = hash,
        .on_replace = [](reshade::api::command_list*) { return false; },
        .on_draw = [](auto* cmd_list) { return !reflection_passes || twattila::rt_scene::OnReflectionMarch(cmd_list); },
    };
  }

  // Light Through Foliage (foliage_light.hpp): the screen-space shadow passes' depth (sss00 and Remastered CE's at t3,
  // sss01 and its at t0) with the foliage negative while the row is on, the game's put back after. Then SSS Softness and
  // Debug View's texture at u1 (screen_space_shadows.hpp).
  for (const auto [hash, depth_slot] : {std::pair{0x34ECD06Eu, 3u}, std::pair{0x9147EB4Eu, 3u}, std::pair{0x1B529672u, 0u},
                                        std::pair{0x7FA1D600u, 0u}}) {
    custom_shaders[hash].on_draw = [depth_slot](reshade::api::command_list* cmd_list) {
      twshogun2::foliage_light::OnShadowPass(cmd_list, depth_slot, shader_injection.sss_foliage_light);
      twshogun2::screen_space_shadows::OnShadowPass(cmd_list, depth_slot, shader_injection.sss_softness, shader_injection.sss_debug_view,
                                                    shader_injection.sss_distance_softness, shader_injection.sss_banner_light,
                                                    sss_coloured != 0.f);
      return true;
    };
    custom_shaders[hash].on_drawn = [](reshade::api::command_list* cmd_list) {
      twshogun2::screen_space_shadows::OnShadowPassDone(cmd_list);
      twshogun2::foliage_light::OnShadowPassDone(cmd_list);
    };
  }
  // Render scale: captured on the game's own reflections copy and battle fog (drawn every frame before the depth of
  // field and the auto exposure), reset after the tonemaps.
  for (const uint32_t hash : {0x962CEB71u, 0xA61D2853u, 0x4355E897u}) {
    custom_shaders[hash].on_draw = render_scale::OnDrawCapture;
  }
  // Ray tracing's Debug View over the tone mapper's output; SSS Debug View's textures (screen_space_shadows.hpp) during it.
  for (const uint32_t hash : {0xEF060C7Cu, 0xBEDBEFD8u}) {
    custom_shaders[hash].on_drawn = [](reshade::api::command_list* cmd_list) {
      twshogun2::screen_space_shadows::OnTonemapDrawn(cmd_list);
      render_scale::Set(1.f, 1.f);
      if (ray_tracing_installed) twshogun2::ray_tracing::OnTonemapDrawn(cmd_list);
      twshogun2::upscaling::KeepFrame(cmd_list);
    };
  }
  // The heat haze's distortion texture was drawn with DLSS's jitter (tonemap01 reads it there).
  custom_shaders[0xEF060C7C].on_draw = [](reshade::api::command_list* cmd_list) {
    twshogun2::screen_space_shadows::OnTonemap(cmd_list, shader_injection.sss_debug_view);
    return true;
  };
  custom_shaders[0xBEDBEFD8].on_draw = [](reshade::api::command_list* cmd_list) {
    twshogun2::screen_space_shadows::OnTonemap(cmd_list, shader_injection.sss_debug_view);
    shader_injection.render_jitter_x = dlss_jitter_x.load(std::memory_order_relaxed);
    shader_injection.render_jitter_y = dlss_jitter_y.load(std::memory_order_relaxed);
    return true;
  };

  // VFX: the effects overlay draws the additive particles' buffer (blend dest ONE), then the alpha-blended one (smoke,
  // dust: dest source alpha, its transmittance).
  custom_shaders[0x3C9BC2F9].on_draw = [](reshade::api::command_list* cmd_list) {
    shader_injection.vfx_smoke_draw = 0.f;
    if (shader_injection.hdr < 1.f || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    context->OMGetBlendState(blend.GetAddressOf(), nullptr, nullptr);
    D3D11_BLEND_DESC desc = {};
    if (blend != nullptr) {
      blend->GetDesc(&desc);
    }
    const bool smoke = (desc.RenderTarget[0].BlendEnable != FALSE && desc.RenderTarget[0].DestBlend == D3D11_BLEND_SRC_ALPHA);
    shader_injection.vfx_smoke_draw = (smoke ? 1.f : 0.f);
    return true;
  };

  // The sea's colour overlay at full size (callbacks; the game's shaders but indicator02's, Indicator Brightness).
  for (const uint32_t hash : {0xC57C222Du, 0xC8375A74u, 0x06701006u}) {
    custom_shaders[hash] = {
        .crc32 = hash,
        .on_draw = sea_overlay::OnDraw,
        .on_drawn = sea_overlay::OnDrawn,
    };
  }
  custom_shaders[0xC57C222D].code = __0xC57C222D;

  // VFX effect test: one effect's fire & glow draws skipped, or all the others.
  auto shown_in_test = [](uint32_t id) {
    const bool selected = (id != 0 && id == twshogun2::vfx_effects::IdOf(static_cast<int>(vfx_test_effect)));
    return (vfx_test == 1.f) ? !selected : selected;
  };
  // Fire & glow draws: the slider their effect follows (vfx_effects.hpp; set in the test).
  auto brightness_of = [](uint32_t id) {
    switch (twshogun2::vfx_effects::KindOf(id)) {
      case twshogun2::vfx_effects::Kind::BIG_FIRE:
        return shader_injection.vfx_big_fire;
      case twshogun2::vfx_effects::Kind::CAMPFIRE:
        return shader_injection.vfx_campfire;
      default:
        return shader_injection.vfx_glow;
    }
  };
  // vfxglow00 / 01: the effect constants at their vertex shaders' b3 / b2, only looked up when the sliders differ or
  // in the test.
  for (const auto& [hash, slot] : {std::pair{0x40744308u, 3u}, std::pair{0xF70E8B73u, 2u}}) {
    custom_shaders[hash].on_draw = [slot, shown_in_test, brightness_of](reshade::api::command_list* cmd_list) {
      shader_injection.vfx_glow_draw = shader_injection.vfx_glow;
      const bool split = (shader_injection.hdr >= 1.f
                          && (shader_injection.vfx_big_fire != shader_injection.vfx_glow
                              || shader_injection.vfx_campfire != shader_injection.vfx_glow));
      if (!split && vfx_test == 0.f) return true;
      const auto effect = twshogun2::vfx_effects::DrawnParticles(cmd_list, slot);
      shader_injection.vfx_glow_draw = brightness_of(effect.id);
      if (vfx_test == 0.f) return true;
      if (effect.id != 0) {
        twshogun2::vfx_effects::Drawn(effect);
      }
      return shown_in_test(effect.id);
    };
  }
  for (const auto& [hash, id] : {std::pair{0xBA0315BEu, twshogun2::vfx_effects::GLOW_MESH_COLOR},
                                 std::pair{0x546A3049u, twshogun2::vfx_effects::GLOW_MESH_TEXTURE}}) {
    custom_shaders[hash].on_draw = [id, shown_in_test, brightness_of](reshade::api::command_list*) {
      shader_injection.vfx_glow_draw = brightness_of(id);
      if (vfx_test == 0.f) return true;
      twshogun2::vfx_effects::Drawn({.id = id});
      return shown_in_test(id);
    };
  }

  // 0-2 slider, 1 = the game's.
  auto slider = [](const std::string& key, float* binding, const std::string& label, const std::string& section,
                   const std::string& tooltip, bool (*is_enabled)(), bool (*is_visible)(), float default_value = 1.f) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .min = 0.f,
        .max = 2.f,
        .format = "%.2f",
        .is_enabled = is_enabled,
        .is_visible = is_visible,
    };
  };

  // VFX (Extra (Experimental), HDR): 1 = the game's.
  auto vfx = [](const std::string& key, float* binding, const std::string& label, const std::string& tooltip,
                float max, const char* format = "%.1f") {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = 1.f,
        .label = label,
        .section = "Extra (Experimental)",
        .tooltip = tooltip + " 1 = the game's.",
        .min = 0.f,
        .max = max,
        .format = format,
        .is_enabled = IsHDRMode,
    };
  };

  // VFX effect test (hidden; uncomment here and its settings below to use it again):
  // // VFX effect test: the previous / next effect drawn in the last frame.
  // auto test_step = [](const std::string& label, int step) {
  //   return new renodx::utils::settings::Setting{
  //       .value_type = renodx::utils::settings::SettingValueType::BUTTON,
  //       .label = label,
  //       .section = "VFX Effect Test",
  //       .group = "vfx-test-line",
  //       .is_enabled = [] { return vfx_test != 0.f; },
  //       .on_change = [step]() {
  //         const auto effects = twshogun2::vfx_effects::Effects();
  //         const int count = static_cast<int>(effects.size());
  //         for (int offset = 1; offset <= count; ++offset) {
  //           const int index = ((static_cast<int>(vfx_test_effect) - 1 + (step * offset)) % count + count) % count;
  //           if (!effects[index].on_screen) continue;
  //           renodx::utils::settings::UpdateSettings({{"vfx_test_effect", static_cast<float>(index + 1)}});
  //           return;
  //         }
  //       },
  //   };
  // };
  //
  // // VFX effect test: the slider the selected effect follows.
  // auto test_assign = [](const std::string& label, twshogun2::vfx_effects::Kind kind) {
  //   return new renodx::utils::settings::Setting{
  //       .value_type = renodx::utils::settings::SettingValueType::BUTTON,
  //       .label = label,
  //       .section = "VFX Effect Test",
  //       .group = "vfx-assign-line",
  //       .tooltip = "The selected effect follows this slider (saved).",
  //       .is_enabled = [] { return vfx_test != 0.f; },
  //       .on_change = [kind]() {
  //         if (const uint32_t id = twshogun2::vfx_effects::IdOf(static_cast<int>(vfx_test_effect)); id != 0) {
  //           twshogun2::vfx_effects::SetKind(id, kind);
  //         }
  //       },
  //   };
  // };

  // Highlight Hue Shift (as talesofarise): one amount per HDR mode, 0-1.
  auto hue_shift = [](const std::string& key, float* binding, const std::string& section, float default_value,
                      bool (*is_shown)()) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = "Highlight Hue Shift",
        .section = section,
        .tooltip = std::format("Bright colors take the game's SDR hues: fire more orange / yellow, less pink.\n"
                               "0 = off, 1 = SDR hues. Default {:.2f}.",
                               default_value),
        .min = 0.f,
        .max = 1.f,
        .format = "%.2f",
        .is_enabled = is_shown,
        .is_visible = is_shown,
    };
  };

  settings = {
      new renodx::utils::settings::Setting{
          .key = "SettingsMode",
          .binding = &current_settings_mode,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .can_reset = false,
          .label = "Settings Mode",
          .labels = {"HDR & Effects", "Graphics 1 of 2", "Graphics 2 of 2"},
          .is_global = true,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "The game's HDR sliders do nothing with this mod: use Peak, Paper White and UI below.",
          .section = "General",
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Use these in-game settings: Gamma 22 and Brightness 10.",
          .section = "General",
          .tint = 0xFFD23C,
      },
      twattila::graphics_page::Note(twattila::graphics_page::FULL_DOWNLOAD_NOTE, "General", twattila::graphics_page::WARNING_TINT,
                                    [] { return !twattila::dlaa_bridge::HelperInstalled(); }),
      twattila::graphics_page::Note("Delete renodx-twshogun2completeedition_dlss.addon64 from the game folder: its DLSS options are part of this add-on now.\n"
                                    "Until then this add-on's DLSS, render scale and game options stay off (restart the game after deleting it).",
                                    "General", twattila::graphics_page::WARNING_TINT, [] { return old_dlss_addon_found; }),
      new renodx::utils::settings::Setting{
          .key = "peak_white_nits",
          .binding = &shader_injection.peak_white_nits,
          .default_value = 1000.f,
          .can_reset = false,
          .label = "Peak",
          .section = "General",
          .tooltip = "Brightest output in nits.",
          .min = 400.f,
          .max = 10000.f,
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .key = "diffuse_white_nits",
          .binding = &shader_injection.diffuse_white_nits,
          .default_value = 203.f,
          .label = "Paper White",
          .section = "General",
          .tooltip = "Brightness of the game and videos in nits.",
          .min = 1.f,
          .max = 500.f,
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .key = "graphics_white_nits",
          .binding = &shader_injection.graphics_white_nits,
          .default_value = 203.f,
          .label = "UI",
          .section = "General",
          .tooltip = "Brightness of the UI and HUD in nits.",
          .min = 1.f,
          .max = 500.f,
          .is_visible = IsHdrOutput,
      },
      new renodx::utils::settings::Setting{
          .key = "tonemapper",
          .binding = &shader_injection.hdr,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Rendering Mode",
          .section = "General",
          .tooltip = "SDR: the game's SDR.\n"
                     "HDR (Extended): the game's own tone curve, extended into HDR. Default.\n"
                     "HDR (PsychoV31): PsychoV31 instead of the game's curve.\n"
                     "HDR (PsychoV35): Musa's PsychoV35 instead of it, with Hue Clip Emulation (Recommended).\n"
                     "SDR output: HDR (Extended) keeps the game's curve, PsychoV31 and PsychoV35 are mapped to SDR.",
          .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "HDR (PsychoV35), no lens flares, faint foliage glints, brighter sun, VFX and videos.\n"
                     "Keeps Peak, Paper White, UI and output.",
          .on_change = []() { ApplyRecommendedPreset(); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (SDR-like Contrast/Saturation)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended, with contrast and saturation closer to the game's SDR.\n"
                     "Keeps Peak, Paper White, UI and output.",
          .on_change = []() {
            ApplyRecommendedPreset();
            renodx::utils::settings::UpdateSettings({
                {"highlights", 1.00f},
                {"cone_response_exponent", 1.3f},
                {"highlight_saturation", 1.00f},
                {"roll_off", 2.00f},
                {"v35_contrast", 1.3f},
                {"v35_roll_off", 2.00f},
            });
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "All image settings back to their defaults (HDR Extended).\n"
                     "Keeps Peak, Paper White, UI and output.",
          .on_change = []() { ApplyResetPreset(); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "SDR (monitors without HDR)",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "For SDR monitors: Rendering Mode SDR (the game's own picture) and Output SDR (restart the game if it was\n"
                     "HDR), no video AutoHDR. The game's and the UI's white are your monitor's own white.",
          .tint = 0xFFC107,
          .on_change = []() {
            ApplySdrPreset();
            renodx::utils::settings::UpdateSettings({{"SwapChainEncoding", 2.f}});
          },
      },

      // Extended ////////////////////////////////////////////////////////////////////////////////////
      slider("ext_exposure", &shader_injection.ext_exposure, "Exposure", "Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_highlights", &shader_injection.ext_highlights, "Highlights", "Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_shadows", &shader_injection.ext_shadows, "Shadows", "Extended", "", IsExtendedMode, IsExtendedMode),
      slider("ext_contrast", &shader_injection.ext_contrast, "Contrast", "Extended", "1 = the game's.",
             IsExtendedMode, IsExtendedMode),
      slider("ext_saturation", &shader_injection.ext_saturation, "Saturation / Purity", "Extended", "1 = like SDR.",
             IsExtendedMode, IsExtendedMode),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink, too orange or too pale? Change the settings below.",
          .section = "Extended",
          .is_visible = IsExtendedMode,
      },
      slider("ext_highlight_saturation", &shader_injection.ext_highlight_saturation, "Highlight Saturation",
             "Extended", "How much bright colors lose saturation. 0 = more, 2 = less.", IsExtendedMode,
             IsExtendedMode),
      hue_shift("ext_highlight_hue_shift", &shader_injection.ext_highlight_hue_shift, "Extended", 0.33f,
                IsExtendedMode),

      // PsychoV31 ///////////////////////////////////////////////////////////////////////////////////
      slider("exposure", &shader_injection.exposure, "Exposure", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("highlights", &shader_injection.highlights, "Highlights", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("shadows", &shader_injection.shadows, "Shadows", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("contrast", &shader_injection.contrast, "Contrast", "PsychoV31", "1 = PsychoV31's own. Keeps saturation.",
             IsPsychoMode, IsPsychoMode),
      slider("cone_response_exponent", &shader_injection.cone_response_exponent, "Cone Response Exponent", "PsychoV31",
             "Like Contrast, but also changes saturation. 1 = PsychoV31's own.",
             IsPsychoMode, IsPsychoMode),
      slider("highlight_contrast", &shader_injection.highlight_contrast, "Highlight Contrast", "PsychoV31",
             "Contrast above mid gray. 1 = PsychoV31's own.", IsPsychoMode, IsPsychoMode),
      slider("shadow_contrast", &shader_injection.shadow_contrast, "Shadow Contrast", "PsychoV31",
             "Contrast below mid gray. 1 = PsychoV31's own.", IsPsychoMode, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "flare",
          .binding = &shader_injection.flare,
          .default_value = 0.f,
          .label = "Flare",
          .section = "PsychoV31",
          .tooltip = "Deepens the darkest shadows. 0 = off.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .parse = [](float value) { return std::pow(value, 5.f); },
          .is_visible = IsPsychoMode,
      },
      slider("saturation", &shader_injection.saturation, "Saturation / Purity", "PsychoV31", "1 = PsychoV31's own.",
             IsPsychoMode, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "dechroma",
          .binding = &shader_injection.dechroma,
          .default_value = 0.f,
          .label = "Dechroma",
          .section = "PsychoV31",
          .tooltip = "Very bright colors fade toward white, as if overexposed. 0 = off.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      slider("roll_off", &shader_injection.roll_off, "Highlight Roll-off", "PsychoV31",
             "How early highlights compress toward Peak. 0 = latest, 2 = soonest.",
             IsPsychoMode, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "gamut_compression",
          .binding = &shader_injection.gamut_compression,
          .default_value = 1.f,
          .label = "Gamut Compression",
          .section = "PsychoV31",
          .tooltip = "Pulls colors your display can't show back inside its color space.\n"
                     "0 = off (the display clips them), 1 = full (default).",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .key = "gamut_compression_mode",
          .binding = &shader_injection.gamut_compression_mode,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Gamut Compression Mode",
          .section = "PsychoV31",
          .tooltip = "The color space colors are fitted into.\n"
                     "BT.2020: the whole HDR signal (default).\n"
                     "Display P3: what most HDR monitors and TVs show. BT.709: SDR colors.",
          .labels = {"BT.709", "BT.2020", "Display P3"},
          .is_enabled = []() { return IsPsychoMode() && IsHdrOutput(); },
          // PsychoV31's modes: 0 BT.709, 1 BT.2020, 3 Display P3.
          .parse = [](float value) { return (value == 2.f ? 3.f : value); },
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Fire too pink, too orange or too pale? Change the settings below.",
          .section = "PsychoV31",
          .is_visible = IsPsychoMode,
      },
      slider("highlight_saturation", &shader_injection.highlight_saturation, "Highlight Saturation", "PsychoV31",
             "Saturation of bright colors. 0 = less, 2 = more.", IsPsychoMode, IsPsychoMode),
      hue_shift("highlight_hue_shift", &shader_injection.highlight_hue_shift, "PsychoV31", 0.46f, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .key = "highlight_saturation_shift",
          .binding = &shader_injection.highlight_saturation_shift,
          .default_value = 0.f,
          .label = "Highlight Saturation Shift",
          .section = "PsychoV31",
          .tooltip = "Gives bright colors the saturation of the game's SDR look (fire cores less pale and pink).\n"
                     "0 = off (default), 1 = SDR's.",
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },

      // Extra (HDR only) /////////////////////////////////////////////////////////////////////////
      slider("bloom", &shader_injection.bloom, "Bloom", "Extra", "Glow around bright lights.", IsHDRMode, nullptr),
      slider("lens_flare", &shader_injection.lens_flare, "Lens Flares", "Extra",
             "Sun flares and ghosts around bright lights.", IsHDRMode, nullptr),
      slider("vignette", &shader_injection.vignette, "Vignette", "Extra", "Darker screen edges.",
             IsHDRMode, nullptr),
      slider("heat_haze", &shader_injection.heat_haze, "Distortion and Haze", "Extra",
             "Heat haze over fires and other distortion.", IsHDRMode, nullptr),
      slider("depth_of_field", &shader_injection.depth_of_field, "Depth of Field", "Extra", "Depth of field blur strength.",
             IsHDRMode, nullptr),
      slider("battle_fog", &shader_injection.battle_fog, "Battle Fog", "Extra",
             "Battlefield fog, haze and distance blur.", IsHDRMode, nullptr),
      new renodx::utils::settings::Setting{
          .key = "sun",
          .binding = &shader_injection.sun,
          .default_value = 1.f,
          .label = "Sun",
          .section = "Extra",
          .tooltip = "Brightness of the sun disc (evenly bright, as in the game). 1 = the game's.",
          .min = 0.f,
          .max = 20.f,
          .format = "%.1f",
          .is_enabled = IsHDRMode,
      },
      slider("foliage_glints", &shader_injection.foliage_glints, "Foliage Sun Glints", "Extra",
             "Sun shine on grass, flowers and leaves (flickering white specks). 0 = off.",
             nullptr, nullptr),
      vfx("vfx_glow", &shader_injection.vfx_glow, "VFX Brightness: Fire & Glow",
          "Fire, sparks, explosions and muzzle flashes; not burning houses or campfires.", 10.f),
      vfx("vfx_big_fire", &shader_injection.vfx_big_fire, "VFX Brightness: Big Fires",
          "Burning buildings, castles and walls.", 10.f),
      vfx("vfx_campfire", &shader_injection.vfx_campfire, "VFX Brightness: Campfires", "Campfire flames.", 50.f, "%.0f"),
      vfx("vfx_trails", &shader_injection.vfx_trails, "VFX Brightness: Trails",
          "Arrow, bullet and artillery trails.", 10.f),
      vfx("vfx_firelight", &shader_injection.vfx_firelight, "VFX Brightness: Firelight",
          "Light from fires, explosions, torches and lanterns.", 10.f),
      vfx("vfx_smoke", &shader_injection.vfx_smoke, "VFX Brightness: Smoke & Dust",
          "Lit smoke and dust. Black smoke stays black (see VFX Opacity).", 10.f),
      vfx("vfx_smoke_opacity", &shader_injection.vfx_smoke_opacity, "VFX Opacity: Smoke & Dust",
          "Thickness of smoke and dust, black smoke too.", 3.f),
      // Attila's two rows and keys (autoexposure00).
      new renodx::utils::settings::Setting{
          .key = "auto_exposure_highlight_protection",
          .binding = &shader_injection.auto_exposure_highlight_protection,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Modded Auto Exposure",
          .section = "Extra (Experimental)",
          .tooltip = "The sun, fires and sparks darken the whole picture less when the camera looks at them. Their own "
                     "brightness stays. Off is the game's.",
          .labels = {"Off (Game's)", "On"},
      },
      new renodx::utils::settings::Setting{
          .key = "auto_exposure_highlight_headroom_stops",
          .binding = &shader_injection.auto_exposure_highlight_headroom_stops,
          .default_value = 4.f,
          .label = "Modded Auto Exposure: Sensitivity",
          .section = "Extra (Experimental)",
          .tooltip = "How much brighter than the picture a highlight may count, in stops. Lower: steadier exposure; higher: "
                     "closer to the game's.",
          .min = 1.f,
          .max = 8.f,
          .format = "%.0f",
          .is_enabled = [] { return shader_injection.auto_exposure_highlight_protection != 0.f; },
      },
      // Attila's row and key (indicator00-02).
      new renodx::utils::settings::Setting{
          .key = "some_indicators",
          .binding = &shader_injection.indicator_brightness,
          .default_value = 1.f,
          .label = "Unit/Building Indicator Brightness",
          .section = "Extra (Experimental)",
          .tooltip = "Brightness of the rings under units and buildings and of the firing arcs. Lower helps at night. "
                     "1 = the game's.",
          .min = 0.f,
          .max = 2.f,
          .format = "%.2f",
          .is_enabled = IsHDRMode,
      },
      new renodx::utils::settings::Setting{
          .key = "video_autohdr",
          .binding = &shader_injection.video_autohdr,
          .default_value = 0.f,
          .label = "Video AutoHDR",
          .section = "Extra",
          .tooltip = "Brighter highlights in the game's videos. 0 = off. High values can show banding.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = [] { return IsHDRMode() && IsHdrOutput(); },
      },
      new renodx::utils::settings::Setting{
          .key = "video_dithering",
          .binding = &shader_injection.video_dithering,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Video Dithering",
          .section = "Extra",
          .tooltip = "Hides banding in brightened videos.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return IsHDRMode() && IsHdrOutput() && shader_injection.video_autohdr > 0.f; },
      },
      // VFX effect test (hidden):
      // new renodx::utils::settings::Setting{
      //     .key = "vfx_test",
      //     .binding = &vfx_test,
      //     .value_type = renodx::utils::settings::SettingValueType::INTEGER,
      //     .default_value = 0.f,
      //     .label = "VFX Effect Test",
      //     .section = "VFX Effect Test",
      //     .tooltip = "Find a fire or glow effect (hide it, or show only it), then set the slider it follows.",
      //     .labels = {"Off", "Hide Effect", "Only Effect"},
      // },
      // new renodx::utils::settings::Setting{
      //     .key = "vfx_test_effect",
      //     .binding = &vfx_test_effect,
      //     .value_type = renodx::utils::settings::SettingValueType::INTEGER,
      //     .default_value = 1.f,
      //     .label = "Effect",
      //     .section = "VFX Effect Test",
      //     .tooltip = "Effects are numbered in the order they first appear.",
      //     .min = 1.f,
      //     .max = 200.f,
      //     .is_enabled = [] { return vfx_test != 0.f; },
      // },
      // test_step("Previous on Screen", -1),
      // test_step("Next on Screen", 1),
      // new renodx::utils::settings::Setting{
      //     .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
      //     .section = "VFX Effect Test",
      //     .on_draw = [] {
      //       const auto effects = twshogun2::vfx_effects::Effects();
      //       const auto on_screen = std::ranges::count_if(effects, [](const auto& effect) { return effect.on_screen; });
      //       std::string text = std::format("{} effects seen, {} on screen ({} read back).", effects.size(), on_screen,
      //                                      twshogun2::vfx_effects::ReadBacks());
      //       const int number = static_cast<int>(vfx_test_effect);
      //       if (number >= 1 && number <= static_cast<int>(effects.size())) {
      //         const auto& effect = effects[number - 1];
      //         const bool glow_mesh = (effect.id == twshogun2::vfx_effects::GLOW_MESH_COLOR
      //                                 || effect.id == twshogun2::vfx_effects::GLOW_MESH_TEXTURE);
      //         text += (glow_mesh ? std::format("\nEffect {}: glow mesh, ID {}", number, effect.id)
      //                            : std::format("\nEffect {}: ID {:08X}, emissive {:.2f} to {:.2f}", number, effect.id,
      //                                          effect.emissive_min, effect.emissive_max));
      //         text += (effect.on_screen ? "" : " (not on screen)");
      //         switch (twshogun2::vfx_effects::KindOf(effect.id)) {
      //           case twshogun2::vfx_effects::Kind::BIG_FIRE:
      //             text += "\nFollows: Big Fires";
      //             break;
      //           case twshogun2::vfx_effects::Kind::CAMPFIRE:
      //             text += "\nFollows: Campfires";
      //             break;
      //           default:
      //             text += "\nFollows: Fire & Glow";
      //             break;
      //         }
      //       }
      //       ImGui::TextUnformatted(text.c_str());
      //       return false;
      //     },
      //     .is_visible = [] { return vfx_test != 0.f; },
      // },
      // test_assign("Set as Fire & Glow", twshogun2::vfx_effects::Kind::FIRE_AND_GLOW),
      // test_assign("Set as Big Fires", twshogun2::vfx_effects::Kind::BIG_FIRE),
      // test_assign("Set as Campfires", twshogun2::vfx_effects::Kind::CAMPFIRE),
  };
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT));

  // Graphics 1 of 2 and 2 of 2: the DLSS add-on's overlay until it became part of this add-on (its rows, keys, defaults
  // and presets as they were; Settings Mode's pages: ConfigureAddon).
  using renodx::utils::settings::Setting;
  using renodx::utils::settings::SettingValueType;
  const std::string& aa_section = twattila::graphics_page::AA_SECTION;
  const std::string& graphics_section = twattila::feature_settings::GRAPHICS_SECTION;
  const std::vector<std::string> off_on = {"Off", "On"};
  const auto helper = [] { return twattila::dlaa_bridge::HelperInstalled(); };
  const auto hidden = [] { return false; };
  const auto generation_rows = [] { return twattila::dlaa_bridge::HelperInstalled() && aa_sliders.frame_generation != 0.f; };
  // A preset button: `defaults` then `changes`.
  const auto preset = [](const Values& defaults, const Values& changes) {
    return [defaults, changes] {
      renodx::utils::settings::UpdateSettings(defaults);
      renodx::utils::settings::UpdateSettings(changes);
    };
  };
  // A choice (its labels) of `section`.
  const auto choice = [](const std::string& key, float* binding, float default_value, const std::string& label, const std::string& section,
                         const std::string& tooltip, std::vector<std::string> labels, std::function<bool()> enabled = {},
                         std::function<bool()> shown = {}) {
    return new Setting{
        .key = key,
        .binding = binding,
        .value_type = SettingValueType::INTEGER,
        .default_value = default_value,
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .labels = std::move(labels),
        .is_enabled = std::move(enabled),
        .is_visible = std::move(shown),
    };
  };
  // An amount of `section`.
  const auto amount = [](const std::string& key, float* binding, float default_value, const std::string& label, const std::string& section,
                         const std::string& tooltip, float min, float max, const std::string& format, std::function<bool()> enabled = {},
                         std::function<bool()> shown = {}) {
    return new Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .min = min,
        .max = max,
        .format = format,
        .is_enabled = std::move(enabled),
        .is_visible = std::move(shown),
    };
  };
  // An on / off box of `section`.
  const auto toggle = [](const std::string& key, float* binding, float default_value, const std::string& label, const std::string& section,
                         const std::string& tooltip, std::function<bool()> enabled = {}, std::function<bool()> shown = {}) {
    return new Setting{
        .key = key,
        .binding = binding,
        .value_type = SettingValueType::BOOLEAN,
        .default_value = default_value,
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .is_enabled = std::move(enabled),
        .is_visible = std::move(shown),
    };
  };
  static bool more_aa = false;
  static bool more_graphics = false;

  // Anti-Aliasing and Upscaling: the game's DLSS (its render scale and preset), the helper's upscalers and frame
  // generation, Reflex.
  settings.insert(settings.end(), {
      twattila::graphics_page::Note(twattila::graphics_page::FULL_DOWNLOAD_NOTE, aa_section, twattila::graphics_page::WARNING_TINT,
                                    [] { return !twattila::dlaa_bridge::HelperInstalled(); }),
      choice("RenderScale", &aa_sliders.dlss_mode, 0.f, "Render scale (experimental)", aa_section,
             "Renders the 3D image smaller and lets DLSS upscale it; the UI stays sharp.\n"
             "Needs the game's Anti-aliasing on DLSS.",
             {"100% (DLAA, the game's)", "77% (Ultra Quality)", "66.7% (Quality)", "58% (Balanced)", "50% (Performance)",
              "33.3% (Ultra Performance)", "Custom"}),
      // Its value changes only when the slider is let go: every new scale creates the game's DLSS again.
      new Setting{
          .key = "RenderScaleCustom",
          .binding = &aa_sliders.render_scale,
          .value_type = SettingValueType::INTEGER,
          .default_value = 50.f,
          .label = "Custom render scale",
          .section = aa_section,
          .min = 1.f,
          .max = 100.f,
          .format = "%d%%",
          .is_visible = hidden,
      },
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Custom render scale",
          .section = aa_section,
          .tooltip = "Applied when you let go. DLSS may refuse scales below 33%.",
          .on_draw = [] {
            static int percent = 50;
            static bool dragging = false;
            if (!dragging) percent = static_cast<int>(aa_sliders.render_scale);
            ImGui::SliderInt("Custom render scale", &percent, 1, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
            dragging = ImGui::IsItemActive();
            if (!ImGui::IsItemDeactivatedAfterEdit()) return false;
            renodx::utils::settings::UpdateSettings({{"RenderScaleCustom", static_cast<float>(percent)}});
            return true;
          },
          .is_visible = [] { return aa_sliders.dlss_mode == 6.f; },
      },
      // The NGX value: PRESETS' (any other: K).
      new Setting{
          .key = "Preset",
          .binding = &aa_sliders.preset,
          .value_type = SettingValueType::INTEGER,
          .default_value = static_cast<float>(NVSDK_NGX_DLSS_Hint_Render_Preset_K),
          .label = "DLSS preset",
          .section = aa_section,
          .min = 0.f,
          .max = 255.f,
          .parse = [](float value) {
            const bool listed = std::ranges::find(PRESETS, static_cast<uint32_t>(value), &PresetChoice::value) != PRESETS.end();
            return listed ? value : static_cast<float>(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
          },
          .is_visible = hidden,
      },
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "DLSS preset",
          .section = aa_section,
          .tooltip = "For every render scale. A DLSS preset override in the NVIDIA app wins over this.",
          .on_draw = [] {
            const auto selected = std::ranges::find(PRESETS, static_cast<uint32_t>(aa_sliders.preset), &PresetChoice::value);
            bool changed = false;
            if (ImGui::BeginCombo("DLSS preset", (selected != PRESETS.end() ? selected->label : PRESETS[1].label))) {
              for (const auto& option : PRESETS) {
                if (!ImGui::Selectable(option.label, static_cast<float>(option.value) == aa_sliders.preset)) continue;
                renodx::utils::settings::UpdateSettings({{"Preset", static_cast<float>(option.value)}});
                changed = true;
              }
              ImGui::EndCombo();
            }
            return changed;
          },
      },
      new Setting{
          .key = "HelperUpscaler",
          .binding = &aa_sliders.method,
          .value_type = SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Upscaler (Experimental)",
          .section = aa_section,
          .tooltip = "The game's DLSS, or the RenoDX helper's (renodx-tw-dlaa64.exe next to the add-on): newer DLSS with\n"
                     "Ray Reconstruction for the ray tracing, AMD FSR or Intel XeSS. The game's Anti-aliasing on DLSS.",
          .labels = {"Game's DLSS", "DLSS (RenoDX helper)", "FSR (RenoDX helper)", "XeSS (RenoDX helper)"},
          // Attila's anti-aliasing values: 5 + twattila::dlaa_bridge::UPSCALER_*.
          .parse = [](float value) { return (value >= 1.f) ? value + 4.f : 0.f; },
          .is_visible = helper,
      },
      choice("AntiAliasingRayReconstruction", &aa_sliders.ray_reconstruction, 0.f, "Ray Reconstruction (Experimental)", aa_section,
             "DLSS Ray Reconstruction cleans the ray-traced effects (Ray Tracing below): sharper, steadier.\n"
             "NVIDIA only; with a ray-traced effect on.",
             {"Off", "NVIDIA's preset", "D", "E", "F"}, {}, [] { return HelperUpscaling() && aa_sliders.method == 5.f; }),
      choice("UpscalingDebugView", &aa_sliders.debug_view, 0.f, "Upscaling debug view", aa_section,
             "Motion Vectors: the motion the upscaler gets over the picture (colour: direction, brighter: faster).", {"Off", "Motion Vectors"}, {},
             HelperUpscaling),
      choice("HelperFrameGeneration", &aa_sliders.frame_generation, 0.f, "Frame generation (Experimental)", aa_section,
             "Frames shown per rendered frame (the RenoDX helper presents). Turn the game's VSync off; windowed or borderless.\n"
             "DLSS: 2x on RTX 40 or newer, 3x to 6x and Dynamic on RTX 50. FSR: 2x. XeSS: 2x, up to 4x on Intel Arc.",
             {"Off", "2x", "3x", "4x", "5x", "6x", "Dynamic"}, {}, helper),
      twattila::graphics_page::Note("Frame generation pauses while this menu is open.", aa_section, twattila::graphics_page::NOTICE_TINT,
                                    generation_rows),
      choice("FrameGenerationMethod", &aa_sliders.frame_backend, 0.f, "Frame generation method", aa_section,
             "Auto: DLSS on an RTX 40 or newer, XeSS on Intel, else FSR. Their files next to renodx-tw-dlaa64.exe.",
             {"Auto", "DLSS", "FSR", "XeSS"}, {}, generation_rows),
      choice("FrameGenerationHudRecomposition", &aa_sliders.hud_recomposition, 1.f, "HUD recomposition", aa_section,
             "Lays the HUD over the generated frames instead of warping it with the scene (no HUD smears).", off_on, {}, generation_rows),
      choice("FrameGenerationPreset", &aa_sliders.frame_preset, 0.f, "Frame generation preset", aa_section,
             "NVIDIA's presets. A: HUD recomposition off. B: on. C (Lite): lighter, lower quality.\n"
             "Saved in the NVIDIA driver profile of renodx-tw-dlaa64.exe; applies after restarting the game.",
             {"Driver's", "A", "B", "C"}, {}, generation_rows),
      choice("FrameGenerationDebugView", &aa_sliders.frame_debug_view, 0.f, "Frame generation debug view", aa_section,
             "What frame generation gets, shown while this menu is closed: the picture without the HUD, or the HUD in magenta.",
             {"Off", "HUD-less image", "UI layer"}, {}, generation_rows),
      choice("Reflex", &aa_sliders.reflex, 0.f, "NVIDIA Reflex", aa_section,
             "Less input lag on NVIDIA GPUs: each frame starts only as the GPU needs it. Always on with frame generation.",
             {"Off", "On", "On + Boost"}),
      new Setting{
          .key = "FrameGenerationFrameLimit",
          .binding = &aa_sliders.frame_limit,
          .value_type = SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Frame rate limit",
          .section = aa_section,
          .tooltip = "Caps the rendered frame rate with Reflex (frame generation multiplies it). 0: off.",
          .min = 0.f,
          .max = 240.f,
          .format = "%d fps",
      },
      choice("TextureLodBias", &aa_sliders.texture_lod_bias, 2.f, "Texture LOD bias", aa_section,
             "Texture sharpness below 100%. Matched: as at your resolution; Sharper / Sharpest: more.",
             {"Off (the game's own)", "Matched (as sharp as at your resolution)", "Sharper (one step sharper; NVIDIA's recommendation)",
              "Sharpest (two steps sharper; can shimmer)"}),
      // Attila's key (twattila/texture_lod_bias.hpp SetWrappingTrilinear: the samplers filtered without anisotropy too).
      toggle("AntiAliasingTextureLodBiasTrilinear", &aa_sliders.lod_bias_trilinear, 0.f, "Texture LOD bias on all textures (Experimental)",
             aa_section, "Texture LOD bias also on the textures the game filters without anisotropy. Can shimmer.", {},
             [] { return aa_sliders.dlss_mode != 0.f && aa_sliders.texture_lod_bias != 0.f; }),
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .section = aa_section,
          .on_draw = [] {
            const auto display = twshogun2::game_dlss::DisplaySize();
            const auto render = twattila::frame_history::AspectSize(display[0], display[1], SelectedRenderScale());
            ImGui::Text("Render %ux%u -> %ux%u", render[0], render[1], display[0], display[1]);
            return false;
          },
          .is_visible = [] { return aa_sliders.dlss_mode != 0.f && twshogun2::game_dlss::DisplaySize()[0] != 0; },
      },
      // The old add-on's Advanced settings (debugging and extra information).
      twattila::graphics_page::MoreOptions(aa_section, &more_aa, [] { return true; }),
      toggle("Jitter", &aa_sliders.jitter, 1.f, "Camera jitter (fixes the game's DLSS)", aa_section,
             "Sub-pixel camera movement DLSS needs for fine detail (the game leaves it off). Keep on.", {}, [] { return more_aa; }),
      toggle("ObjectMotion", &aa_sliders.object_motion, 1.f, "Ship and water motion vectors", aa_section,
             "Motion vectors for ships, sails, ropes and water. Keep on.", {}, [] { return more_aa; }),
      choice("JitterFlip", &aa_sliders.jitter_flip, 0.f, "Jitter sign", aa_section, "Only if the image wobbles or looks soft with jitter on.",
             {"Game's", "X flipped", "Y flipped", "Both flipped"}, {}, [] { return more_aa; }),
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .section = aa_section,
          .on_draw = [] {
            ImGui::TextWrapped("The game's DLSS: %s", twshogun2::game_dlss::Status().c_str());
            if (aa_sliders.dlss_mode != 0.f) {
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
            ImGui::TextWrapped("%s", engine_values_status.label.c_str());
            ImGui::TextWrapped("Particle resolution: %s, minimum now %.2f of the render size", twshogun2::particle_resolution::Status().c_str(),
                               twshogun2::particle_resolution::Minimum());
            return false;
          },
          .is_visible = [] { return more_aa; },
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Trace one frame (shogun2_frame_trace.txt next to the exe)",
          .section = aa_section,
          .on_change = [] { twshogun2::frame_trace::Request(); },
          .is_visible = [] { return more_aa; },
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Capture DLSS inputs (4 frames, shogun2_dlss_capture_* next to the exe)",
          .section = aa_section,
          .tooltip = "Saves DLSS's color, motion and depth inputs of 4 frames (about 60 MB at 33%).",
          .on_change = [] { twshogun2::frame_trace::RequestCapture(); },
          .is_visible = [] { return more_aa; },
      },
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .section = aa_section,
          .on_draw = [] {
            ImGui::TextWrapped("%s", twshogun2::frame_trace::Status().c_str());
            return false;
          },
          .is_visible = [] { return more_aa && !twshogun2::frame_trace::Status().empty(); },
      },

      // Graphics 2 of 2: above the game's highest settings.
      twattila::graphics_page::Note("Above the game's highest settings: heavier.", graphics_section, 0xFFBF33, [] { return true; }),
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Recommended (ATN's settings)",
          .section = graphics_section,
          .group = "graphics-presets",
          .tooltip = "SSR resolution Full, Particle resolution Off, Shadow samples 4x,\n"
                     "both shadow stabilities Steadier, Terrain detail Finest everywhere; the rest default.",
          .on_change = preset(GRAPHICS_RESET, {{"SSSSamples", 2.f}, {"SSSStability", 1.f}, {"ShadowMapStability", 1.f}, {"TerrainDetail", 1.f}}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Reset",
          .section = graphics_section,
          .group = "graphics-presets",
          .tooltip = "Graphics options back to their defaults.",
          .on_change = [] {
            renodx::utils::settings::UpdateSettings(GRAPHICS_RESET);
            renodx::utils::settings::UpdateSettings({{"WaterClarity", 1.f}, {"WaterCaustics", 0.f}});
            for (const auto& test : EngineTests()) renodx::utils::settings::UpdateSetting(test.key, 1.f);
          },
      },
      choice("SSRResolution", &shader_injection.ssr_resolution, 1.f, "SSR resolution", graphics_section,
             "Reflection resolution below 100%. Full: sharper, steadier reflections. At 100% no change.", {"Game (half the render size)", "Full"}),
      amount("WaterClarity", &shader_injection.water_clarity, 1.f, "Water clarity (Experimental)", graphics_section,
             "How far you see into rivers and lakes in battles. 1: the game's; higher: clearer water.", 0.25f, 16.f, "%.2f"),
      amount("WaterCaustics", &shader_injection.water_caustics, 0.f, "Water caustics (Experimental)", graphics_section,
             "Rippling light on river and lake beds in battles. 0: off.", 0.f, 2.f, "%.2f"),
      choice("ParticleResolution", &graphics_rows.particle_resolution, 0.f, "Particle resolution", graphics_section,
             "Smoke and other particles below 100%. On: as sharp as at 100% (heavier).\n"
             "Off: the game's own scaling (30-100% of the render size). At 100% no change.",
             {"Off (the game's)", "On (follows the render scale)"}),
      choice("SSSSamples", &shader_injection.sss_samples, 0.f, "Shadow samples (SSS)", graphics_section,
             "Reach of the screen-space shadows (game option SSS): longer contact shadows. Heavier.",
             {"Game (120)", "2x (240)", "4x (480)", "8x (960)", "16x (1920)", "32x (3840)"}),
      choice("SSSStability", &shader_injection.sss_stability, 0.f, "Shadow stability (SSS)", graphics_section,
             "Less flickering screen-space shadows. Steadier: softer shadow edges.\n"
             "Steadiest: also no shadows from thin edges (slightly thinner shadows).",
             {"Game", "Steadier (softer)", "Steadiest (softer, thinner)"}),
      choice("ShadowMapResolution", &graphics_rows.shadow_map_resolution, 0.f, "Shadow map resolution", graphics_section,
             "Sharper, steadier sun shadows at the highest Shadows setting, with SSS on or off. Heavier;\n"
             "12288 uses about 2.3 GB of video memory. Applies after a restart or after changing the game's Shadows option.",
             {"4096 (Game's)", "8192", "12288"}),
      choice("ShadowMapStability", &shader_injection.shadow_map_stability, 0.f, "Shadow map stability", graphics_section,
             "Less flickering self-shadowing on slopes and walls the sun grazes, with SSS on or off.\n"
             "Steadiest: shadows start slightly further from where objects touch.",
             {"Game", "Steadier", "Steadiest"}),
      amount("SunShadowSoftness", &shader_injection.sun_shadow_softness, 0.f, "Sun shadow softness (PCSS, experimental)", graphics_section,
             "Contact-hardening sun shadows: sharp where objects touch the ground, softer the further they are.\n"
             "The sun's size in degrees (the real sun: 0.5). Heavier than the game's filter. 0: the game's.",
             0.f, 5.f, "%.2f deg"),
      choice("GrassDistance", &graphics_rows.grass_distance, 0.f, "Grass distance", graphics_section,
             "How far grass is drawn (the game's: 450 m) x this; near grass also gets fuller.\n"
             "Heavy on CPU and GPU above 4x.",
             {"Game", "1.5x", "2x", "3x", "4x", "6x", "8x", "12x", "16x", "24x", "32x"}),
      choice("ModelDetail", &graphics_rows.model_detail, 0.f, "Model detail distance", graphics_section,
             "How far models keep their detail x this (plants, rocks, props, buildings). Less popping.",
             {"Game", "1.5x", "2x", "3x", "4x", "6x", "8x", "16x"}),
      choice("UnitDetail", &graphics_rows.unit_detail, 0.f, "Unit detail distance", graphics_section,
             "How far units stay 3D before turning into flat sprites. Heavier for the CPU and GPU.",
             {"Game", "1.5x", "2x", "3x", "4x", "6x", "8x"}),
      choice("UnitAnimation", &graphics_rows.unit_animation, 0.f, "Unit animation distance", graphics_section,
             "How far soldiers move on their own, with faces (beyond it they copy their neighbours'\n"
             "moves). Heavier for the CPU.",
             {"Game", "1.5x", "2x", "3x", "4x"}),
      choice("UnitVariety", &graphics_rows.unit_variety, 0.f, "Unit variety distance", graphics_section,
             "How far soldiers keep their own heads and gear (beyond it they look alike). Heavier.", {"Game", "1.5x", "2x", "3x", "4x"}),
      choice("TerrainDetail", &graphics_rows.terrain_detail, 0.f, "Terrain detail (experimental)", graphics_section,
             "Battle terrain detail. Finest everywhere: full detail at any distance; heavier.", {"Game", "Finest everywhere"}),
  });
  twattila::engine_tests::Append(&settings, &EngineTests(), nullptr);
  // The screen-space shadows' Bend parameters (sss.hlsli; 0 the game's) and Attila's SSS rows: the old add-on's Advanced
  // settings, in no preset.
  const auto more_graphics_open = [] { return more_graphics; };
  settings.insert(settings.end(), {
      twattila::graphics_page::MoreOptions(graphics_section, &more_graphics, [] { return true; }),
      amount("SSSThickness", &shader_injection.sss_thickness, 0.f, "SSS thickness (Experimental)", graphics_section,
             "How thick each pixel counts as for casting screen-space shadows. Thicker: fuller shadows, more halos. 0: the game's.", 0.f,
             0.05f, "%.4f", {}, more_graphics_open),
      amount("SSSContrast", &shader_injection.sss_contrast, 0.f, "SSS contrast (Experimental)", graphics_section,
             "How fast screen-space shadows go from lit to shadowed (the game's: 4; halved by Shadow stability). 0: the game's.", 0.f, 8.f,
             "%.2f", {}, more_graphics_open),
      amount("SSSEdgeThreshold", &shader_injection.sss_edge_threshold, 0.f, "SSS edge threshold (Experimental)", graphics_section,
             "Depth jumps above this count as edges, not blended between. Lower: crisper, more aliasing. 0: the game's.", 0.f, 0.1f, "%.4f",
             {}, more_graphics_open),
      choice("SSSPrecisionOffset", &shader_injection.sss_precision_offset, 0.f, "SSS precision offset (Experimental)", graphics_section,
             "A small offset for depth imprecision: less shadow acne far away, contact shadows start a bit later.", off_on, {},
             more_graphics_open),
      amount("SSSFoliageLight", &shader_injection.sss_foliage_light, 0.f, "Light through foliage (Experimental)", graphics_section,
             "How much sun comes through the screen-space shadows grass and leaves cast: they are thin. Walls and\n"
             "soldiers keep their full shadows, on foliage too. 0: as dark as any other shadow.",
             0.f, 1.f, "%.2f", {}, more_graphics_open),
      // The game's: 8 and 16 (its sss.hlsli's HARD_SHADOW_SAMPLES / FADE_OUT_SAMPLES), 0 for the shader.
      new Setting{
          .key = "SSSHardSamples",
          .binding = &shader_injection.sss_hard_samples,
          .value_type = SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "SSS hard shadow samples (Experimental)",
          .section = graphics_section,
          .tooltip = "The samples next to each pixel that shadow it fully on their own: crisp contact shadows.\n"
                     "Higher: darker, harder near contacts.",
          .labels = {"Game's (8)", "4", "16", "32"},
          .parse = [](float value) { return std::array<float, 4>{0.f, 4.f, 16.f, 32.f}[std::clamp(static_cast<int>(value), 0, 3)]; },
          .is_visible = more_graphics_open,
      },
      new Setting{
          .key = "SSSFadeSamples",
          .binding = &shader_injection.sss_fade_samples,
          .value_type = SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "SSS fade-out samples (Experimental)",
          .section = graphics_section,
          .tooltip = "The last samples, which fade the shadow out. Higher: softer shadow ends.",
          .labels = {"Game's (16)", "4", "8", "32", "64"},
          .parse = [](float value) { return std::array<float, 5>{0.f, 4.f, 8.f, 32.f, 64.f}[std::clamp(static_cast<int>(value), 0, 4)]; },
          .is_visible = more_graphics_open,
      },
      choice("SSSIgnoreEdges", &shader_injection.sss_ignore_edges, 0.f, "SSS ignore edge pixels (Experimental)", graphics_section,
             "Edge pixels cast no shadow: fewer stripes on flat ground under a low sun; thinner shadows from leaves.\n"
             "Shadow stability Steadiest does this too.",
             off_on, {}, more_graphics_open),
      choice("SSSAlongRay", &shader_injection.sss_along_ray, 0.f, "SSS sample along the ray (Experimental)", graphics_section,
             "Reads the depth along the shadow's ray instead of at pixel centres: better on grazing surfaces,\n"
             "can be more jagged elsewhere.",
             off_on, {}, more_graphics_open),
      choice("SSSMirrorEdges", &shader_injection.sss_mirror_edges, 0.f, "SSS mirror screen edges (Experimental)", graphics_section,
             "Near the screen's edges, shadows from what lies past them are guessed from what is just inside.\n"
             "Off: no shadows there when they would come from off screen.",
             off_on, {}, more_graphics_open),
      // Attila's SSS Softness (Off, 1-3 px), Softer Away from the Caster (the sun's degrees), Light Through Banners (the sun's
      // share, 0 off), Coloured Shadows and Debug View (Off, Shadows, Shadows + Screen Edge), its keys.
      choice("ScreenSpaceShadowsSoftness", &shader_injection.sss_softness, 0.f, "SSS softness (Experimental)", graphics_section,
             "Smooths the screen-space shadows' edges along surfaces, never across objects.", {"Off", "1 px", "2 px", "3 px"}, {},
             more_graphics_open),
      amount("ScreenSpaceShadowsDistanceSoftness", &shader_injection.sss_distance_softness, 0.f, "SSS softer away from the caster (Experimental)",
             graphics_section,
             "Shadows sharp where things touch the ground, softer farther from them, as under the sun.\n"
             "The sun's size in degrees. 0: off.",
             0.f, 2.f, "%.2f", {}, more_graphics_open),
      amount("ScreenSpaceShadowsBannerLight", &shader_injection.sss_banner_light, 0.f, "Light through banners (Experimental)", graphics_section,
             "How much sun comes through the screen-space shadows banners cast: they are thin cloth. 0: off.", 0.f, 1.f, "%.2f", {},
             more_graphics_open),
      choice("ScreenSpaceShadowsColoured", &sss_coloured, 0.f, "Coloured shadows (Experimental)", graphics_section,
             "With Light through banners: the sun through them takes their colour.", off_on, {}, more_graphics_open),
      choice("ScreenSpaceShadowsDebugView", &shader_injection.sss_debug_view, 0.f, "SSS debug view (Experimental)", graphics_section,
             "Shows only the screen-space shadows (black: shadowed).\n"
             "Screen Edge: red where a shadow would come from past the screen's edge (it cannot).",
             {"Off", "Shadows", "Shadows + Screen Edge"}, {}, more_graphics_open),
  });

  // Ambient Occlusion (GT-VBAO) and Global Illumination (GT-VBGI, the Light Bounce: twattila/lighting_settings.hpp's
  // sections, Shogun's keys and defaults). ATN's presets: the section's every row.
  const char* ao_section = twattila::lighting_settings::AO_SECTION;
  const char* gi_section = twattila::lighting_settings::GI_SECTION;
  const auto ao_on = [] { return shader_injection.ssao_mode != 0.f; };
  const auto gi_on = [] { return shader_injection.light_bounce_strength > 0.f; };
  const std::vector<std::string> samples = {"Normal (8)", "High (16)", "Ultra (32)", "64", "128", "256"};
  settings.insert(settings.end(), {
      twattila::graphics_page::Note("Darker shading behind trees and objects: raise Thickness. Lighter around spears: lower it.", ao_section,
                                    0xFFD23C, [] { return true; }),
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Faster",
          .section = ao_section,
          .group = "ao-presets",
          .tooltip = "Radius 5 m, Quality High, half resolution.",
          .tint = twattila::lighting_settings::AO_TINT,
          .on_change = preset(AO_DEFAULTS, {{"AmbientOcclusionRadius", 5.f}, {"AmbientOcclusionQuality", 1.f}, {"AmbientOcclusionResolution", 1.f}}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Heavier (Recommended)",
          .section = ao_section,
          .group = "ao-presets",
          .tooltip = "Radius 12.5 m, Quality Ultra.",
          .tint = twattila::lighting_settings::AO_TINT,
          .on_change = preset(AO_DEFAULTS, {}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Reset",
          .section = ao_section,
          .group = "ao-presets",
          .tooltip = "The game's ambient occlusion, this section's sliders at their defaults.",
          .tint = twattila::lighting_settings::AO_TINT,
          .on_change = preset(AO_DEFAULTS, {{"AmbientOcclusion", 0.f}}),
      },
      choice("AmbientOcclusion", &shader_injection.ssao_mode, 1.f, "Ambient Occlusion", ao_section,
             "Soft shadows in corners and where objects meet the ground.\n"
             "Game's: everything counts as solid, so soldiers and trees get dark halos.\n"
             "GT-VBAO: objects have a thickness, so light gets behind thin ones.",
             {"Game's", "GT-VBAO"}),
      amount("AmbientOcclusionStrength", &shader_injection.ssao_strength, 1.f, "Strength", ao_section,
             "How dark it gets. 1: physically correct; higher: darker.", 0.f, 3.f, "%.2f", ao_on),
      amount("AmbientOcclusionRadius", &shader_injection.ssao_radius, 12.5f, "Radius", ao_section,
             "How far it reaches. Larger: wider shading, heavier (raise Quality with it).", 0.25f, 20.f, "%.2f m", ao_on),
      amount("AmbientOcclusionThickness", &shader_injection.ssao_thickness, 0.1f, "Thickness", ao_section,
             "How thick objects are taken to be (Global Illumination too).\n"
             "Lower: light gets behind thin things; higher: darker behind them.",
             0.01f, 3.f, "%.2f m", ao_on),
      choice("AmbientOcclusionQuality", &shader_injection.ssao_quality, 2.f, "Quality", ao_section,
             "Samples per direction. More: thin things still count at a large Radius. Heavier.", samples, ao_on),
      choice("AmbientOcclusionDirections", &shader_injection.ssao_directions, 0.f, "Directions", ao_section,
             "Directions searched around each pixel. More: less noise, heavier.", {"1", "2", "4", "8"}, ao_on),
      choice("AmbientOcclusionResolution", &shader_injection.ssao_resolution, 0.f, "Resolution", ao_section,
             "Lower: faster, softer; thin things (grass, spears) get less shading.", {"Full", "Half"}, ao_on),
      choice("AmbientOcclusionAutoThickness", &shader_injection.ssao_auto_thickness, 1.f, "Auto Thickness (Experimental)", ao_section,
             "Guesses each object's thickness from its width, Thickness as the least:\n"
             "spears stay thin, walls and hills block fully.",
             off_on, ao_on),
      choice("ssao_temporal", &ao_filter_rows.temporal, 0.f, "Temporal Smoothing (Experimental)", ao_section,
             "Averages the last frames: much less noise. Off: the noise stands still.", off_on, ao_on),
      choice("ssao_edge_fix", &ao_filter_rows.edge_fix, 0.f, "Temporal Edge Fix (Experimental)", ao_section,
             "Keeps Temporal Smoothing's old shading off outlines (no thin halos).\n"
             "Outlines can shimmer a little more.",
             off_on, [] { return shader_injection.ssao_mode != 0.f && ao_filter_rows.temporal != 0.f; }),
      choice("ssao_denoise", &ao_filter_rows.denoise, 0.f, "Denoiser (Experimental)", ao_section,
             "Smooths the noise along surfaces, never across edges. Cleaner, a bit softer.", off_on, ao_on),
      twattila::graphics_page::EmptyLine(ao_section),
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Faster (Recommended)",
          .section = gi_section,
          .group = "gi-presets",
          .tooltip = "Strength 1, Radius 20 m, quarter resolution, 64 samples.",
          .tint = twattila::lighting_settings::GI_TINT,
          .on_change = preset(GI_DEFAULTS, {{"LightBounceStrength", 1.f}}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Heavier (More accurate)",
          .section = gi_section,
          .group = "gi-presets",
          .tooltip = "Strength 1, Radius 20 m, half resolution, Quality Ultra.",
          .tint = twattila::lighting_settings::GI_TINT,
          .on_change = preset(GI_DEFAULTS, {{"LightBounceStrength", 1.f}, {"LightBounceResolution", 1.f}, {"LightBounceQuality", 2.f}}),
      },
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Reset",
          .section = gi_section,
          .group = "gi-presets",
          .tooltip = "Off, this section's sliders at their defaults.",
          .tint = twattila::lighting_settings::GI_TINT,
          .on_change = preset(GI_DEFAULTS, {}),
      },
      amount("LightBounceStrength", &shader_injection.light_bounce_strength, 0.f, "Strength", gi_section,
             "Sunlit ground, walls and soldiers bounce their light and colour into nearby shadows.\n"
             "1: physically correct; 0: off.",
             0.f, 4.f, "%.2f"),
      amount("LightBounceRadius", &shader_injection.light_bounce_radius, 20.f, "Radius", gi_section,
             "How far bounced light reaches. Larger: heavier (raise Quality with it).", 0.5f, 20.f, "%.2f m", gi_on),
      choice("LightBounceResolution", &shader_injection.light_bounce_resolution, 2.f, "Resolution", gi_section, "Lower: much faster, softer.",
             {"Full", "Half", "Quarter", "Eighth"}, gi_on),
      choice("LightBounceQuality", &shader_injection.light_bounce_quality, 3.f, "Quality", gi_section,
             "Samples per direction. More: small lit things still count at a large Radius. Heavier.", samples, gi_on),
      choice("LightBounceDirections", &shader_injection.light_bounce_directions, 0.f, "Directions", gi_section,
             "Directions searched around each pixel. More: less noise, heavier.", {"1", "2", "4", "8", "16"}, gi_on),
      choice("LightBounceTemporal", &shader_injection.light_bounce_temporal, 1.f, "Temporal Smoothing", gi_section,
             "Averages the last frames: much less noise. Off: the noise stands still.", off_on, gi_on),
      choice("LightBounceDenoise", &shader_injection.light_bounce_denoise, 1.f, "Denoiser", gi_section,
             "Smooths the noise along surfaces, never across edges. Cleaner, a bit softer.", off_on, gi_on),
      amount("LightBounceMultiBounce", &shader_injection.light_bounce_multi_bounce, 0.f, "Multi-Bounce", gi_section,
             "Bounced light bounces again: fuller light in deep shadows and narrow streets. Almost free.", 0.f, 1.f, "%.2f", gi_on),
      choice("LightBounceCheckerboard", &shader_injection.light_bounce_checkerboard, 1.f, "Checkerboard", gi_section,
             "Half the screen traced each frame, the rest kept from the last frames.\n"
             "About 2x faster; can lag a little behind fast motion.",
             off_on, [] { return shader_injection.light_bounce_strength > 0.f && shader_injection.light_bounce_temporal != 0.f; }),
      choice("LightBounceBackFace", &shader_injection.light_bounce_backface, 0.f, "Back-Face Rejection (Experimental)", gi_section,
             "Ignores light from surfaces seen from behind. Heavier; not always better.", off_on, gi_on),
      choice("LightBounceAutoThickness", &shader_injection.light_bounce_auto_thickness, 1.f, "Auto Thickness (Experimental)", gi_section,
             "Guesses each object's thickness from its width, Thickness as the least:\n"
             "spears stay thin, walls and hills block fully.",
             off_on, gi_on),
      twattila::graphics_page::EmptyLine(gi_section),
  });

  // Ray Tracing (ray_tracing.hpp through the helper; restart after turning an effect on). RT/PT is Attila's (ATN
  // 2026-11-14): every row at its default, then its RT/PT values in this section's keys; Shadow casters out of view and
  // point light shadows on (as Warhammer III's), ray-traced AO kept on (OnPresent turns it off while Path-traced AO
  // runs: the campaign keeps it). No RT/PT turns every ray-traced effect off and keeps their rows, Reset puts every row
  // at its default.
  const auto rays_shown = [](std::function<bool()> shown) -> std::function<bool()> {
    return [shown = std::move(shown)] { return twattila::dlaa_bridge::HelperInstalled() && (!shown || shown()); };
  };
  const auto sun = [] { return rt_sliders.sun_shadows != 0.f; };
  const auto ambient = [] { return rt_sliders.ambient_occlusion != 0.f; };
  const auto local_lights = [] { return rt_sliders.local_lights != 0.f; };
  const auto reflections = [] { return rt_sliders.reflections != 0.f; };
  const auto water = [] { return rt_sliders.reflections != 0.f && rt_sliders.water_reflections != 0.f; };
  const auto bounce = [] { return rt_sliders.light_bounce != 0.f; };
  const auto paths_enabled = [] { return rt_sliders.light_bounce != 0.f || rt_sliders.reflections != 0.f; };
  const auto paths = [] { return (rt_sliders.light_bounce != 0.f || rt_sliders.reflections != 0.f) && rt_sliders.path_tracing != 0.f; };
  const auto reconstruction = [] { return DlssRayReconstruction(); };
  const auto micromaps = [] { return rt_sliders.micromaps != 0.f; };
  // With a ray-traced effect on: the rows for all of them.
  const auto any_rays = [] {
    return rt_sliders.sun_shadows != 0.f || rt_sliders.ambient_occlusion != 0.f || rt_sliders.local_lights != 0.f || rt_sliders.fog_shadows != 0.f
           || rt_sliders.reflections != 0.f || rt_sliders.specular_occlusion != 0.f || rt_sliders.light_bounce != 0.f;
  };
  const auto extra_rays = [any_rays] { return any_rays() && rt_sliders.output_rays != 0.f; };
  const auto ray_toggle = [&](const std::string& key, float* binding, const std::string& label, const std::string& tooltip,
                              std::function<bool()> enabled = {}) {
    return toggle(key, binding, 0.f, label, RAYS_SECTION, tooltip, std::move(enabled), rays_shown({}));
  };
  const auto ray_choice = [&](const std::string& key, float* binding, float default_value, const std::string& label, std::vector<std::string> labels,
                              const std::string& tooltip, std::function<bool()> enabled = {}, std::function<bool()> shown = {}) {
    return choice(key, binding, default_value, label, RAYS_SECTION, tooltip, std::move(labels), std::move(enabled), rays_shown(std::move(shown)));
  };
  const auto ray_amount = [&](const std::string& key, float* binding, float default_value, const std::string& label, float min, float max,
                              const std::string& format, const std::string& tooltip, std::function<bool()> enabled = {},
                              std::function<bool()> shown = {}) {
    return amount(key, binding, default_value, label, RAYS_SECTION, tooltip, min, max, format, std::move(enabled), rays_shown(std::move(shown)));
  };
  // A count from 1.
  const auto ray_count = [&](const std::string& key, float* binding, float default_value, float max, const std::string& label,
                             const std::string& tooltip, std::function<bool()> enabled) {
    return new Setting{
        .key = key,
        .binding = binding,
        .value_type = SettingValueType::INTEGER,
        .default_value = default_value,
        .label = label,
        .section = RAYS_SECTION,
        .tooltip = tooltip,
        .min = 1.f,
        .max = max,
        .format = "%d",
        .is_enabled = std::move(enabled),
        .is_visible = rays_shown({}),
    };
  };
  const auto ray_button = [](const std::string& label, const std::string& tooltip, std::function<void()> on_change) {
    return new Setting{
        .value_type = SettingValueType::BUTTON,
        .label = label,
        .section = RAYS_SECTION,
        .group = "rays-presets",
        .tooltip = tooltip,
        .tint = RAYS_TINT,
        .on_change = std::move(on_change),
        .is_visible = [] { return twattila::dlaa_bridge::HelperInstalled(); },
    };
  };
  settings.insert(settings.end(), {
      twattila::graphics_page::Note(twattila::graphics_page::FULL_DOWNLOAD_NOTE, RAYS_SECTION, twattila::graphics_page::WARNING_TINT,
                                    [] { return !twattila::dlaa_bridge::HelperInstalled(); }),
      new Setting{
          .value_type = SettingValueType::TEXT,
          .label = "Needs an RTX GPU and renodx-tw-dlaa64.exe next to the add-on.",
          .section = RAYS_SECTION,
          .is_visible = helper,
      },
      ray_button("RT/PT",
                 "Every ray-traced effect and path tracing on, as ATN plays; the rest of this section at its default\n"
                 "(restart the game). Turns the Global Illumination on, and Ray Reconstruction where DLSS runs without it.",
                 [] {
                   renodx::utils::settings::UpdateSettings(RAYS_RESET);
                   renodx::utils::settings::UpdateSettings(RAYS_PRESET);
                   renodx::utils::settings::UpdateSettings({{"LightBounceStrength", 1.f}});
                   // Without Ray Reconstruction the rays have only the built-in denoisers (Attila's preset: F).
                   if (aa_sliders.method == 5.f && aa_sliders.ray_reconstruction == 0.f) {
                     renodx::utils::settings::UpdateSettings({{"AntiAliasingRayReconstruction", 4.f}});
                   }
                 }),
      ray_button("No RT/PT (Recommended)", "Every ray-traced effect off, their sliders kept (restart the game).",
                 [] { renodx::utils::settings::UpdateSettings(RAYS_OFF); }),
      ray_button("Reset", "Every ray-traced effect off, this section's sliders at their defaults (restart the game).",
                 preset(RAYS_OFF, RAYS_RESET)),
      ray_toggle("RayTracedSunShadows", &rt_sliders.sun_shadows, "Ray-traced sun shadows",
                 "Sun shadows from rays, with the game's: also where its shadow maps miss things or end.\n"
                 "Faster: the game's Screen Space Shadows off, the rays then alone (grass casts no contact shadows)."),
      ray_amount("RayTracedShadowSoftness", &rt_sliders.sun_softness, 0.5f, "Ray-traced shadow softness", 0.f, 3.f, "%.2f",
                 "The sun's size in degrees: shadows softer away from what casts them. 0: sharp.", sun),
      ray_toggle("RayTracedLeafLight", &rt_sliders.leaf_light, "Light through leaves",
                 "Leaves let some sunlight through ray-traced shadows (where they reach past the game's).", sun),
      ray_choice("RayTracedColouredShadows", &rt_sliders.coloured_shadows, 0.f, "Coloured shadows (Experimental)", off_on,
                 "Sunlight through sails, banners and cloth takes their colour where you can see them.",
                 [] { return rt_sliders.sun_shadows != 0.f && rt_sliders.leaf_light != 0.f; }),
      ray_choice("RayTracedSpriteShadows", &rt_sliders.sprite_shadows, 0.f, "Sprite soldier shadows (Experimental)", off_on,
                 "The flat far soldiers' ray-traced shadows keep their shape as the camera turns.", sun),
      ray_choice("RayTracedFarTreeShadows", &rt_sliders.far_tree_shadows, 0.f, "Far tree shadows (Experimental)", off_on,
                 "The flat far trees cast ray-traced shadows too.", sun),
      ray_choice("RayTracedUnitsOutOfView", &rt_sliders.units_out_of_view, 0.f, "Shadow casters out of view (Experimental)", off_on,
                 "Buildings, rocks, trees, soldiers, horses, sails and banners behind the camera cast ray-traced shadows into view too; "
                 "far soldiers and far trees with their shadow rows on (restart the game).",
                 sun),
      ray_choice("RayTracedUntracedCasters", &rt_sliders.untraced_casters, 0.f, "Rarer objects in the rays (Experimental)", off_on,
                 "Objects and near ground the game tessellates (its Tessellation on) also cast ray-traced shadows.", sun),
      ray_choice("RayTracedSheetsUnselfed", &rt_sliders.sheets_unselfed, 0.f, "Smooth banner shadows (Experimental)", off_on,
                 "Banners, sails and other cloth don't shade themselves: no blocky triangles on them. Shadows of other things stay.", sun),
      ray_choice("RayTracedSmokeShadows", &rt_sliders.smoke_shadows, 0.f, "Ray-traced smoke shadows (Experimental)", off_on,
                 "Smoke and dust dim the ray-traced sunlight they drift through (restart the game).", sun),
      ray_choice("RayTracedSmokeQuality", &rt_sliders.smoke_quality, 1.f, "Smoke shadow quality", {"Low", "Medium", "High", "Ultra", "Extreme", "Maximum"},
                 "Higher: fewer holes and less flicker in thick smoke, slower there.", sun),
      ray_choice("RayTracedSmokeFastBuild", &rt_sliders.smoke_fast_build, 0.f, "Faster smoke shadow build (Experimental)", off_on,
                 "Smoke shadows' particles are made ready for the rays faster each frame (about 0.6 ms in thick smoke). In thick smoke a "
                 "sun ray may count other particles: slightly different smoke shadows.",
                 sun),
      ray_amount("RayTracedGrassShadowRange", &rt_sliders.grass_shadow_range, 0.f, "Grass shadow range (Experimental)", 0.f, 200.f, "%.0f m",
                 "Past this distance grass casts no ray-traced shadows (on itself or the ground): faster over fields. 0 m: everywhere.", sun),
      ray_toggle("RayTracedAmbientOcclusion", &rt_sliders.ambient_occlusion, "Ray-traced ambient occlusion",
                 "Shade in corners and under things from rays, with the game's (the darker).\n"
                 "Faster: the game's SSAO off, the rays then alone."),
      ray_amount("RayTracedAmbientRange", &rt_sliders.ambient_range, 2.f, "Ray-traced AO range", 0.25f, 5.f, "%.2f m",
                 "How far its rays look for what shades a point.", ambient),
      ray_count("RayTracedAORays", &rt_sliders.ambient_rays, 2.f, 8.f, "Ray-traced AO rays", "More rays: less noise. Heavier.", ambient),
      ray_toggle("RayTracedAOQuarter", &rt_sliders.ambient_quarter, "Ray-traced AO at a quarter rate",
                 "One pixel of each 2 x 2 traced a frame, in turn: a quarter of the rays (DLSS smooths it). Faster.", ambient),
      ray_choice("RayTracedAoReblur", &rt_sliders.ao_reblur, 0.f, "AO under Ray Reconstruction (Experimental)", {"Light History", "REBLUR"},
                 "Light History: the AO keeps a little of its last frames, Ray Reconstruction cleans the rest.\n"
                 "REBLUR: NVIDIA's denoiser cleans it first: steadier when moving, uses more memory, can darken corners.",
                 ambient, reconstruction),
      ray_toggle("RayTracedLocalLights", &rt_sliders.local_lights, "Ray-traced point light shadows",
                 "Shadows from rays for the nearest, brightest point lights (fires, torches): what stands between them and the\n"
                 "ground or walls shades them."),
      ray_amount("RayTracedLightSoftness", &rt_sliders.light_softness, 0.25f, "Torch and fire shadow softness", 0.f, 1.f, "%.2f m",
                 "How big torches and fires are for their soft shadows. 0: sharp. With Ray Reconstruction.", local_lights, reconstruction),
      ray_choice("RayTracedLightCount", &rt_sliders.light_count, 0.f, "Torch and fire shadow lights (Experimental)", {"32", "64", "128"},
                 "How many torches and fires cast ray-traced shadows, the nearest and brightest first.\n"
                 "Each spot of the screen traces the 32 that light it most. More: heavier in big fires.",
                 local_lights),
      ray_choice("RayTracedLightImportance", &rt_sliders.light_importance, 0.f, "Importance-sampled torch shadows (Experimental)", off_on,
                 "Two torch / fire shadow rays a pixel, picked by how much light they give:\n"
                 "faster with many fires; dim fires' shadows follow moving units slower.",
                 local_lights),
      ray_choice("RayTracedLightSteady", &rt_sliders.light_steady, 0.f, "Steady torch shadows (Experimental)", off_on,
                 "Torch and fire shadows that don't flicker: every fire traced every frame, with sharp edges.\n"
                 "Heavier with many fires. Overrides Importance-sampled torch shadows.",
                 local_lights),
      ray_amount("RayTracedLightRange", &rt_sliders.light_range, 0.f, "Torch and fire shadow range (Experimental)", 0.f, 500.f, "%.0f m",
                 "Only torches and fires this close to the camera cast ray-traced shadows. 0 m: any distance.", local_lights),
      ray_toggle("RayTracedFogShadows", &rt_sliders.fog_shadows, "Ray-traced fog shadows (Experimental)",
                 "The sun's light in the battle fog shaded where hills, trees and buildings stand in its way."),
      ray_amount("RayTracedLightGlow", &rt_sliders.light_glow, 0.f, "Torch and fire light in fog (Experimental)", 0.f, 2.f, "%.2f",
                 "Torches and fires light the battle fog around them, their shafts and shadows in it. 0: off.",
                 [] { return rt_sliders.local_lights != 0.f && rt_sliders.fog_shadows != 0.f; }),
      ray_toggle("RayTracedReflections", &rt_sliders.reflections, "Ray-traced reflections",
                 "Reflections from rays in place of the screen-space ones: also what is off screen.\n"
                 "Needs the game's screen-space reflections on."),
      ray_count("RayTracedReflectionSamples", &rt_sliders.reflection_samples, 1.f, 8.f, "Ray-traced reflection samples",
                "Rays per pixel on rough surfaces: less noise, heavier.", reflections),
      ray_choice("RayTracedReflectionReuse", &rt_sliders.reflection_reuse, 0.f, "Reflection reuse (Experimental)", {"Off", "From ReSTIR GI", "Own ReSTIR"},
                 "Less noise on rough reflections.\n"
                 "From ReSTIR GI: also the ray-traced Global Illumination's samples (needs it on).\n"
                 "Own ReSTIR: their own samples, reused over frames and between neighbours.",
                 reflections),
      ray_choice("RayTracedWaterReflections", &rt_sliders.water_reflections, 0.f, "Ray-traced water reflections (Experimental)", off_on,
                 "The sea, rivers and lakes reflect ships, shores and units with rays (water quality High or Ultra).", reflections),
      ray_choice("RayTracedWaterSteady", &rt_sliders.water_steady, 0.f, "Steadier water reflections (Experimental)", off_on,
                 "Fewer flickering dots in calm water's reflections: each pixel stays within what the water around it shows.", water),
      ray_choice("RayTracedWaterSoft", &rt_sliders.water_soft, 0.f, "Softer water reflections (Experimental)", off_on,
                 "Calmer water reflections, a little softer. Samples and reuse then work on water too. Heavier.", water),
      ray_toggle("RayTracedSpecularOcclusion", &rt_sliders.specular_occlusion, "Ray-traced specular occlusion (Experimental)",
                 "The sky's reflection taken off shiny surfaces where something stands over them (inside\n"
                 "doorways, under hulls and roofs). Needs the game's screen-space reflections on."),
      ray_toggle("RayTracedLightBounce", &rt_sliders.light_bounce, "Ray-traced Global Illumination",
                 "The Global Illumination's light found with rays: no guessing behind what you see, light from farther.\n"
                 "Needs the Global Illumination on."),
      ray_count("RayTracedLightBounceRays", &rt_sliders.light_bounce_rays, 4.f, 16.f, "Ray-traced GI rays", "Rays per texel: less noise, heavier.",
                bounce),
      ray_amount("RayTracedLightBounceRange", &rt_sliders.light_bounce_range, 10.f, "Ray-traced GI range", 1.f, 50.f, "%.0f m",
                 "How far its rays look for light.", bounce),
      ray_choice("RayTracedLightBounceResolution", &rt_sliders.light_bounce_resolution, 1.f, "Ray-traced GI resolution", {"Full", "Half", "Quarter"},
                 "Its own resolution (the Global Illumination's Resolution and Checkerboard no longer apply to it).\n"
                 "Full: sharpest, about 4x Half's cost.",
                 bounce),
      ray_toggle("PathTracing", &rt_sliders.path_tracing, "Path tracing",
                 "The Global Illumination's and the reflections' rays go on bouncing: lit by what the screen never saw. Heavy.", paths_enabled),
      ray_count("PathTracingBounces", &rt_sliders.path_bounces, 2.f, 2.f, "Path tracing bounces",
                "Bounces past the first where the camera sees nothing. 1: faster, a little darker.", paths),
      ray_choice("RayTracedSharc", &rt_sliders.sharc, 0.f, "SHaRC radiance cache (Experimental)", off_on,
                 "NVIDIA's SHaRC for the light of what the screen does not show (more bounces off-screen, paths end sooner).", paths),
      ray_choice("RayTracedReuseLean", &rt_sliders.reuse_lean, 0.f, "Lean ReSTIR reuse (Experimental)", off_on,
                 "A faster ReSTIR GI for path tracing: the same look in tests.", paths),
      // Attila's torch rows from Path Tracing (graphics_page.hpp's): with Ray-traced point light shadows on.
      ray_choice("RayTracedLightsInBounce", &rt_sliders.lights_in_bounce, 0.f, "Torch and fire light from path tracing (Experimental)", off_on,
                 "Torches and fires lit by the path-traced Global Illumination instead of their own shadow rays: faster with many fires.\n"
                 "Noisier (most at Half resolution), without the torches' highlights; follows the Global Illumination's strength.",
                 [paths] { return paths() && rt_sliders.local_lights != 0.f; }),
      ray_choice("RayTracedLightShadowRatio", &rt_sliders.light_ratio, 0.f, "Torch and fire shadows from path tracing (Experimental)", off_on,
                 "The torches' and fires' own light, their shadows from the path-traced Global Illumination instead of their own\n"
                 "rays: faster with many fires. Softer shadows that follow moving units a little later; 32 fires at most.",
                 [paths] { return paths() && rt_sliders.local_lights != 0.f; }),
      ray_choice("RayTracedPathAmbient", &rt_sliders.path_ambient, 0.f, "Path-traced AO (Experimental)", off_on,
                 "The paths bring the sky's light in place of the game's ambient: shade wherever the sky can't reach.\n"
                 "In battles, in place of ray-traced AO (the campaign map keeps it). Needs Ray-traced Global Illumination.",
                 paths),
      ray_choice("RayTracedPathSkyApart", &rt_sliders.path_sky_apart, 0.f, "Path-traced AO: own sky rays (Experimental)", off_on,
                 "The sky's light averaged from each pixel's own rays: less noise, and more rays help. Off: through ReSTIR GI.",
                 [paths] { return paths() && rt_sliders.path_ambient != 0.f; }),
      ray_choice("RayTracedPathKeptCube", &rt_sliders.path_kept_cube, 0.f, "Path-traced AO: steady bounce light (Experimental)", off_on,
                 "Light bouncing off walls and ground follows the game's sky, not the view Path-traced AO started in.",
                 [paths] { return paths() && rt_sliders.path_ambient != 0.f; }),
      ray_choice("RayTracedPathSer", &rt_sliders.path_ser, 0.f, "Path tracing SER (Experimental)", off_on,
                 "Shader execution reordering for path tracing and ray-traced reflections (DXR 1.2, RTX 40 / 50 series): may be faster.",
                 paths_enabled),
      // With a ray-traced effect on.
      ray_choice("RayTracingDenoiser", &rt_sliders.denoiser, 0.f, "Ray tracing denoiser (Experimental)", {"Built-in", "NRD"},
                 "Cleans the ray-traced shadows, AO, Global Illumination and reflections (with the helper's upscaler).\n"
                 "Built-in: the mod's own filters. NRD: NVIDIA's denoisers, steadier, uses more memory.",
                 {}, [any_rays] { return any_rays() && HelperUpscaling() && !DlssRayReconstruction(); }),
      ray_choice("RayTracedLeafMicromaps", &rt_sliders.micromaps, 0.f, "Leaf micromaps (Experimental)", off_on,
                 "Faster ray-traced shadows through leaves: their cut-outs are baked once (NVIDIA Opacity Micro-Map SDK).", {}, any_rays),
      ray_choice("RayTracedGpuMicromaps", &rt_sliders.gpu_micromaps, 1.f, "GPU micromap baking", off_on,
                 "Leaf micromaps baked on the GPU instead of CPU threads:\n"
                 "frees the CPU, and new grass's and leaves' shadows settle sooner.",
                 micromaps, any_rays),
      ray_choice("RayTracedFastLeafShadows", &rt_sliders.fast_leaves, 0.f, "Fast leaf shadows (Experimental)", off_on,
                 "Leaves and grass with Leaf micromaps skip their alpha tests:\n"
                 "faster shadows under trees and in fields, but coarser and darker.",
                 micromaps, any_rays),
      // Attila's Rays a Pixel (twattila/rt_scene.hpp): OnPresent counts them.
      ray_choice("RayTracingOutputResolution", &rt_sliders.output_rays, 0.f, "Rays a pixel (Experimental)",
                 {"1", "Match Output", "2", "3", "4", "5", "6", "7", "8"},
                 "More rays for every rendered pixel, each from its own spot of it: less noise, finer shadow edges.\n"
                 "The cost grows with the rays. Match Output: as many rays on screen as at 100% (up to 8 a pixel).",
                 {}, any_rays),
      ray_choice("RayTracingRaysShadows", &rt_sliders.rays_shadows, 1.f, "Rays a pixel: shadows", off_on,
                 "The sun's and the torches' and fires' shadows take the extra rays.", {}, extra_rays),
      ray_choice("RayTracingRaysAmbient", &rt_sliders.rays_ambient, 1.f, "Rays a pixel: ray-traced AO", off_on, "Ray-traced AO takes the extra rays.",
                 {}, extra_rays),
      ray_choice("RayTracingRaysReflections", &rt_sliders.rays_reflections, 1.f, "Rays a pixel: reflections", off_on,
                 "Ray-traced reflections take the extra rays.", {}, extra_rays),
      ray_choice("RayTracingRaysPaths", &rt_sliders.rays_paths, 1.f, "Rays a pixel: Global Illumination", off_on,
                 "Ray-traced and path-traced Global Illumination take the extra rays (up to 16 a texel). The heaviest.", {},
                 extra_rays),
      ray_choice("RayTracedDebugLog", &rt_sliders.debug_log, 0.f, "Log details", off_on,
                 "Ray tracing's numbers in the logs every 10 s, for bug reports. Off: nothing of this runs.", {}, any_rays),
      // rt_scene's Debug View values (Off, its effects, All; the others: nothing here makes them).
      new Setting{
          .key = "RayTracedDebugView",
          .binding = &rt_sliders.debug_view,
          .value_type = SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray tracing debug view",
          .section = RAYS_SECTION,
          .min = 0.f,
          .max = 9.f,
          .is_visible = hidden,
      },
      new Setting{
          .value_type = SettingValueType::CUSTOM,
          .label = "Ray tracing debug view",
          .section = RAYS_SECTION,
          .tooltip = "A ray-traced effect's raw result as the picture (All: every one tiled), to check they work.\n"
                     "Magenta: it made nothing (off or not traced).",
          .on_draw = [] {
            const auto shown = std::ranges::find(RAYS_DEBUG_VIEWS, static_cast<int>(rt_sliders.debug_view), &RaysDebugView::value);
            bool changed = false;
            if (ImGui::BeginCombo("Ray tracing debug view", (shown != RAYS_DEBUG_VIEWS.end() ? shown->label : RAYS_DEBUG_VIEWS[0].label))) {
              for (const auto& view : RAYS_DEBUG_VIEWS) {
                if (!ImGui::Selectable(view.label, static_cast<float>(view.value) == rt_sliders.debug_view)) continue;
                renodx::utils::settings::UpdateSettings({{"RayTracedDebugView", static_cast<float>(view.value)}});
                changed = true;
              }
              ImGui::EndCombo();
            }
            return changed;
          },
          .is_visible = rays_shown(any_rays),
      },
      ray_choice("RayTracedSceneTest", &rt_sliders.scene_test, 0.f, "Ray tracing scene (test)",
                 {"Everything", "Terrain Only", "Objects and Trees Only", "No Leaves", "Nothing Kept Off-Screen"},
                 "While the Debug View is on: one part of the scene alone, to find what breaks an effect.", {},
                 [any_rays] { return any_rays() && rt_sliders.debug_view != 0.f; }),
      new Setting{
          .value_type = SettingValueType::BUTTON,
          .label = "Record ray tracing profile (30 s)",
          .section = RAYS_SECTION,
          .tooltip = "30 s of every frame's ray tracing costs into tw_rt_profile.csv and tw_rt_profile_helper.csv\n"
                     "(next to the game's exe). Turn the effects on, press, then move the camera over the map.",
          .on_change = [] {
            if (ray_tracing_installed) twattila::rt_scene::StartProfile();
          },
          .is_visible = rays_shown(any_rays),
      },
  });
}


bool configured = false;
bool attached = false;
bool addon_registered = false;

// The DLSS rows and the game's options: into the game's DLSS and engine hooks (cheap stores; at attach and each present,
// so a preset, a loaded preset or a test set applies at once).
void ApplyGameRows() {
  twshogun2::game_dlss::RequestScale(SelectedRenderScale());
  twshogun2::game_dlss::RequestPreset(static_cast<uint32_t>(aa_sliders.preset));
  twshogun2::game_dlss::SetJitterFlip(std::clamp(static_cast<int>(aa_sliders.jitter_flip), 0, 3));
  twshogun2::object_velocity::SetEnabled(aa_sliders.object_motion != 0.f);
  twattila::texture_lod_bias::SetWrappingTrilinear(aa_sliders.lod_bias_trilinear != 0.f);
  const auto scale_of = [](const auto& scales, float row) { return scales[std::clamp(static_cast<int>(row), 0, static_cast<int>(scales.size()) - 1)]; };
  twshogun2::grass_distance::SetScale(scale_of(GRASS_DISTANCES, graphics_rows.grass_distance));
  twshogun2::model_detail::SetScale(scale_of(MODEL_DETAILS, graphics_rows.model_detail));
  twshogun2::unit_detail::SetScales(scale_of(UNIT_DETAILS, graphics_rows.unit_detail), scale_of(UNIT_ANIMATIONS, graphics_rows.unit_animation),
                                    scale_of(UNIT_ANIMATIONS, graphics_rows.unit_variety));
  twshogun2::shadow_resolution::Set(scale_of(SHADOW_MAP_SIZES, graphics_rows.shadow_map_resolution));
}

void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twattila::light_bounce::Update(swapchain, twattila::lighting_settings::LightBounceSliders(shader_injection));
  // GT-VBAO's Temporal Smoothing (its noise moving each frame) and Denoiser (twattila/ao_filter.hpp), as Warhammer II's.
  const bool bitmask_ao = shader_injection.ssao_mode >= 1.f;
  const bool ao_temporal = bitmask_ao && ao_filter_rows.temporal != 0.f;
  const bool ao_denoise = bitmask_ao && ao_filter_rows.denoise != 0.f;
  static uint32_t ao_frame = 0;
  shader_injection.ssao_frame = ao_temporal ? static_cast<float>(++ao_frame % 64) : 0.f;
  ao_filter_on.store(ao_temporal || ao_denoise, std::memory_order_relaxed);
  if (!ao_temporal && !ao_denoise) {
    twattila::frame_history::render_width.store(0u, std::memory_order_relaxed);
    twattila::frame_history::render_height.store(0u, std::memory_order_relaxed);
  }
  twattila::ao_filter::Update(swapchain, {.temporal = ao_temporal, .edge_fix = ao_filter_rows.edge_fix != 0.f, .denoise = ao_denoise});
  // The helper's frame generation and upscaler (the bridge: what it serves this frame, upscaling.hpp), Reflex; Ray
  // Reconstruction with a ray-traced effect only. Without the helper (the HDR-only download): the game's DLSS alone.
  const bool helper = twattila::dlaa_bridge::HelperInstalled();
  const int generation_row = static_cast<int>(aa_sliders.frame_generation);
  // 2x-6x, Dynamic (1).
  uint32_t frame_generation = 0;
  if (helper && generation_row >= 6) {
    frame_generation = 1;
  } else if (helper && generation_row >= 1) {
    frame_generation = static_cast<uint32_t>(generation_row) + 1u;
  }
  const auto reflex = static_cast<uint32_t>(std::clamp(aa_sliders.reflex, 0.f, 2.f));
  const auto frame_limit = static_cast<uint32_t>(std::clamp(aa_sliders.frame_limit, 0.f, 240.f));
  twattila::frame_generation::Update({
      .multiplier = frame_generation,
      .reflex_boost = reflex == 2,
      .frame_limit = frame_limit,
      .preset = static_cast<uint32_t>(std::clamp(aa_sliders.frame_preset, 0.f, 3.f)),
      .hud_recomposition = aa_sliders.hud_recomposition != 0.f,
      .debug_view = static_cast<uint32_t>(std::clamp(aa_sliders.frame_debug_view, 0.f, 2.f)),
      .sdr_output = !IsHdrOutput(),
  });
  twattila::reflex::Update(swapchain, reflex, frame_limit);
  const uint32_t upscaler = HelperUpscaling() ? std::min(static_cast<uint32_t>(aa_sliders.method), 7u) - 4u : 0u;
  helper_evaluates.store(upscaler != 0 || frame_generation != 0, std::memory_order_relaxed);
  auto rays = RaySettings();
  const bool any_rays = ray_tracing_installed
                        && (rays.sun_shadows || rays.ambient_occlusion || rays.light_bounce || rays.reflections || rays.local_lights
                            || rays.fog_shadows || rays.specular_occlusion);
  twshogun2::upscaling::SetUse({
      .ray_tracing = ray_tracing_installed,
      .guides = ray_tracing_installed && (rays.reflections || rays.path_tracing),
      .reflections = ray_tracing_installed && rays.reflections,
      .upscaler = upscaler,
      .ray_reconstruction = (any_rays && DlssRayReconstruction())
                                ? RAY_RECONSTRUCTION_PRESETS[std::clamp(static_cast<int>(aa_sliders.ray_reconstruction), 0, 4)] + 1
                                : 0u,
      .frame_format = twattila::frame_generation::Format(),
      .frame_backend = FRAME_BACKENDS[std::clamp(static_cast<int>(aa_sliders.frame_backend), 0, 3)],
      .debug_view = static_cast<uint32_t>(std::clamp(aa_sliders.debug_view, 0.f, 1.f)),
  });
  if (ray_tracing_installed) {
    // NRD reads the upscaler's motion and depth: only with the helper's.
    rays.nrd = rays.nrd && upscaler != 0;
    // Rays a Pixel: Match Output, those of the output pixels a render pixel stands for (at most 8).
    const auto render = twshogun2::ray_tracing::HOST.render_size();
    const uint32_t display = render_scale::back_buffer_width.load();
    const float scale = (render[0] != 0 && display != 0) ? std::min(static_cast<float>(render[0]) / static_cast<float>(display), 1.f) : 1.f;
    const auto choice = static_cast<uint32_t>(std::clamp(rt_sliders.output_rays, 0.f, 8.f));
    const auto density = static_cast<uint32_t>((choice == 1) ? std::clamp(std::lround(1.f / (scale * scale)), 1l, 8l) : std::clamp(choice, 1u, 8u));
    const auto rays_of = [density](float on) { return (on != 0.f) ? density : 1u; };
    rays.shadow_rays = rays_of(rt_sliders.rays_shadows);
    rays.ambient_rays *= rays_of(rt_sliders.rays_ambient);
    rays.reflection_samples *= rays_of(rt_sliders.rays_reflections);
    rays.light_bounce_rays = std::min(rays.light_bounce_rays * rays_of(rt_sliders.rays_paths), 16u);
    twattila::light_bounce::SetRayTraced(rays.light_bounce, rays.light_bounce_resolution);
    twshogun2::ray_tracing::Update(rays);
  }
  reflection_passes = false;
  twshogun2::vfx_effects::OnPresent();
  static uint32_t shadow_frame = 0;
  shader_injection.shadow_noise_frame = static_cast<float>(++shadow_frame % 64);
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
  ApplyGameRows();
}

// Data-run marks in ReShade.log (2026-11-14): each sun light draw's first run (sunlight00-03: battle, campaign map).
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

// The game's DLSS, render scale and engine values (after every other present handler, as when they were the DLSS
// add-on's).
void OnDlssPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*,
                   const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  twshogun2::frame_trace::OnPresent();
  twshogun2::terrain_detail::Apply(graphics_rows.terrain_detail == 1.f);
  // Engine values: Shogun's initializer form first (the shared Find reads none of it), once the engine module is there.
  if (HMODULE module = twshogun2::engine::Module(); !engine_values_found && module != nullptr) {
    twshogun2::tweak_objects::Find(tw::engine_tweaks::ModuleSections(module), engine_options);
  }
  tw::engine_tweaks::Update(twshogun2::engine::ModuleName(), engine_options, nullptr, &engine_values_found, &engine_values_status);
  // The game's DLSS ran since the last present: jitter for the next frames (off with any other anti-aliasing).
  const uint64_t evaluations = twshogun2::game_dlss::Evaluations();
  const bool dlss_running = (evaluations != last_evaluations);
  last_evaluations = evaluations;
  twshogun2::game_dlss::SetRunning(dlss_running);
  twshogun2::object_velocity::OnPresent(dlss_running);
  if (!dlss_running) {
    dlss_jitter_x.store(0.f, std::memory_order_relaxed);
    dlss_jitter_y.store(0.f, std::memory_order_relaxed);
  }
  twshogun2::game_dlss::SetJitter(aa_sliders.jitter != 0.f && dlss_running);
  // A `renodx_set_now` file next to the exe ("<key>=<value>;...", twshogun2::frame_trace): those settings at once, for
  // this start only (ReShade.ini is not written). RENODX_TW_SET's timed ones: twattila/test_hooks.hpp.
  if (const std::string marker = twshogun2::frame_trace::TakeSettingsMarker(); !marker.empty()) {
    for (const auto part : std::views::split(marker, ';')) {
      const std::string entry(part.begin(), part.end());
      const size_t equals = entry.find('=');
      if (equals == std::string::npos) continue;
      const std::string key = entry.substr(0, equals);
      const float value = std::strtof(entry.c_str() + equals + 1, nullptr);
      if (renodx::utils::settings::UpdateSetting(key, value)) {
        renodx::utils::log::i(std::format("[Shogun 2] Test: {} set to {}", key, value));
      } else {
        renodx::utils::log::i(std::format("[Shogun 2] Test: no setting {}={}, nothing changed", key, value));
      }
    }
  }
  ApplyGameRows();
  const float scale = twshogun2::game_dlss::UpdateFeature();
  const auto display = twshogun2::game_dlss::DisplaySize();
  const bool scaling = dlss_running && scale < 0.999f;
  const auto render = twattila::frame_history::AspectSize(display[0], display[1], scale);
  if (scaling && graphics_rows.particle_resolution == 1.f && display[0] != 0 && render[0] != 0) {
    twshogun2::particle_resolution::Apply(static_cast<float>(render[0]) / static_cast<float>(display[0]));
  } else {
    twshogun2::particle_resolution::Apply(1.f);
  }
  twshogun2::render_scale::BeginFrame(scaling, display, render, helper_evaluates.load(std::memory_order_relaxed) && dlss_running);
  // Until the DLSS evaluation (its callback clears it): log2(render / display width), minus 1 (sharper) or 2.
  float bias = 0.f;
  if (const int choice = std::clamp(static_cast<int>(aa_sliders.texture_lod_bias), 0, 3); scaling && choice != 0 && display[0] != 0) {
    bias = std::log2(static_cast<float>(render[0]) / static_cast<float>(display[0])) - static_cast<float>(choice - 1);
  }
  twattila::texture_lod_bias::SetTracking(queue, bias != 0.f);
  twattila::texture_lod_bias::SetBias(bias);
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
  const auto desc = swapchain->get_device()->get_resource_desc(swapchain->get_back_buffer(0));
  render_scale::back_buffer_width = desc.texture.width;
  render_scale::back_buffer_height = desc.texture.height;

  auto peak = renodx::utils::swapchain::GetPeakNits(swapchain);
  if (!peak.has_value()) {
    peak = 1000.f;
  }

  for (auto& setting : settings) {
    if (setting->binding != &shader_injection.peak_white_nits) continue;
    setting->default_value = peak.value();
    setting->max = std::max(peak.value(), setting->max);
    setting->can_reset = true;
    break;
  }
}

void ConfigureAddon() {
  if (configured) return;

  BuildRuntimeData();

  // Shaders cb
  renodx::mods::shader::expected_constant_buffer_space = 50;
  renodx::mods::shader::expected_constant_buffer_index = 13;
  render_scale::Set(1.f, 1.f);

  // Swapchain cb
  renodx::mods::swapchain::expected_constant_buffer_index = 13;
  renodx::mods::swapchain::expected_constant_buffer_space = 50;

  renodx::mods::swapchain::use_resource_cloning = true;
  renodx::mods::swapchain::force_borderless = false;
  renodx::mods::swapchain::prevent_full_screen = true;

  renodx::mods::swapchain::swap_chain_proxy_shaders = {
      {
          reshade::api::device_api::d3d11,
          {
              .vertex_shader = __swap_chain_proxy_vertex_shader_dx11,
              .pixel_shader = __swap_chain_proxy_pixel_shader_dx11,
          },
      },
  };

  // The tonemap's output, the UI target and the frame after them: 8-bit (the game's HDR: 10-bit) at back-buffer
  // aspect, upgraded at creation.
  for (auto format : {
           reshade::api::format::r8g8b8a8_typeless,
           reshade::api::format::r8g8b8a8_unorm,
           reshade::api::format::r8g8b8a8_unorm_srgb,
           reshade::api::format::b8g8r8a8_typeless,
           reshade::api::format::b8g8r8a8_unorm,
           reshade::api::format::b8g8r8a8_unorm_srgb,
           reshade::api::format::r10g10b10a2_typeless,
           reshade::api::format::r10g10b10a2_unorm,
       }) {
    renodx::mods::swapchain::resource_upgrade_infos.push_back({
        .old_format = format,
        .new_format = reshade::api::format::r16g16b16a16_float,
        .use_resource_view_cloning = true,
        .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
        .usage_include = reshade::api::resource_usage::render_target,
    });
  }

  {
    auto* setting = new renodx::utils::settings::Setting{
        .key = "SwapChainEncoding",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 3.f,
        .label = "Output",
        .section = "Output (Restart Required)",
        .tooltip = "Auto: scRGB with Windows HDR on, else SDR. Default. Frame generation uses HDR10 for scRGB.\n"
                   "HDR10: 10-bit. scRGB: 16-bit. SDR: displays without HDR.",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    const int value = static_cast<int>(setting->GetValue());
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = value,
        .hdr_encoding = [](int output) {
          // The helper's frame generation (HelperFrameGeneration) takes no scRGB: HDR10 with it.
          const bool frame_generation = twattila::dlaa_bridge::HelperInstalled() && aa_sliders.frame_generation != 0.f;
          return (output == twattila::output_encoding::HDR10 || frame_generation) ? twattila::output_encoding::HDR10 : twattila::output_encoding::SCRGB;
        },
        .first_start_without_hdr = &ApplySdrPreset,
        .recommended_preset = &ApplyRecommendedPreset,
    });
    settings.push_back(setting);
  }

  // Bend Studio's screen-space shadows (shipped in the game's shaders.pack, changed here) are Apache 2.0: its license
  // ships as APACHE_LICENSE-2.0.txt (docs/licenses).
  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "RenoDX, PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom PsychoV31\n"
                   "and PsychoV35.\n"
                   "Screen-space shadows: Bend Studio, Copyright 2023 Sony Interactive Entertainment, Apache License 2.0\n"
                   "(APACHE_LICENSE-2.0.txt).",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::HELPER,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::DLSS_ADDON,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "ATN's Ko-fi",
          .section = "About",
          .group = "line_1",
          .tint = 0xFF5A16,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://ko-fi.com/atnthegreat"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "NexusMods (For RenoDX + DLSS)",
          .section = "About",
          .group = "line_1",
          .tint = 0x2196F3,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/totalwarshogun2completeedition/mods/3"); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "GitHub (Downloads)",
          .section = "About",
          .group = "line_1",
          .tint = 0x6E7681,
          .on_change = []() { renodx::utils::platform::LaunchURL("https://github.com/arshiatn/renodx/releases"); },
      },
  });

  // Section titles (ATN, as Attila): one colour, the AO's, GI's and Graphics 2 of 2's their own (settings_pages.hpp), the
  // DLSS rows' lavender and the ray tracing's teal as in the DLSS add-on's overlay. Then Settings Mode's pages.
  renodx::utils::settings::default_section_tint = twattila::feature_settings::BASE_TINT;
  const std::vector<std::string> engine_sections = {twattila::feature_settings::GRAPHICS_SECTION};
  twattila::feature_settings::SetTints({}, engine_sections);
  renodx::utils::settings::section_tints.emplace_back(twattila::graphics_page::AA_SECTION, 0xB39DDB);
  renodx::utils::settings::section_tints.emplace_back(RAYS_SECTION, RAYS_TINT);
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, engine_sections, {twattila::graphics_page::AA_SECTION, RAYS_SECTION});

  // The DLSS add-on's saved settings (ReShade.ini [Shogun 2 DLSS], until it became part of this add-on), once: each of
  // its keys this page has, into every preset that lacks it (they held for every preset), a global row's into the
  // add-on's own section if it lacks it; then SettingsRevision 1. Its section stays as it is.
  const std::string& global = renodx::utils::settings::global_name;
  if (int revision = 0; !reshade::get_config_value(nullptr, global.c_str(), "SettingsRevision", revision) || revision < 1) {
    int copied = 0;
    for (const auto* setting : settings) {
      if (setting->key.empty()) continue;
      std::array<char, 64> value = {};
      size_t size = value.size();
      if (!reshade::get_config_value(nullptr, "Shogun 2 DLSS", setting->key.c_str(), value.data(), &size)) continue;
      const std::vector<std::string> sections = (setting->is_global ? std::vector{global}
                                                                    : std::vector{global + "-preset1", global + "-preset2", global + "-preset3"});
      for (const auto& section : sections) {
        size_t saved = 0;
        if (reshade::get_config_value(nullptr, section.c_str(), setting->key.c_str(), nullptr, &saved)) continue;
        reshade::set_config_value(nullptr, section.c_str(), setting->key.c_str(), static_cast<const char*>(value.data()));
      }
      ++copied;
    }
    reshade::set_config_value(nullptr, global.c_str(), "SettingsRevision", 1);
    if (copied != 0) renodx::utils::log::i(std::format("[Shogun 2] Settings: {} of the DLSS add-on's ([Shogun 2 DLSS]) copied into the presets.", copied));
  }

  configured = true;
}

}  // namespace

// The game's device is going away: frame generation's and Reflex's objects on it with it.
void OnDestroyHelperDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  twattila::frame_generation::OnDestroyDevice(native);
  twattila::reflex::OnDestroyDevice(native);
}

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Total War: SHOGUN 2 Complete Edition by ATN";

namespace {

bool AttachAddon() {
  if (attached) return true;

  ConfigureAddon();

  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::register_event<reshade::addon_event::map_buffer_region>(twshogun2::vfx_effects::OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(twshogun2::vfx_effects::OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(twshogun2::vfx_effects::OnUpdateBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region_command>(
      twshogun2::vfx_effects::OnUpdateBufferRegionCommand);
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  twshogun2::vfx_effects::LoadKinds();

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  twattila::draw_dispatch::Use(DLL_PROCESS_ATTACH);
  // Before the Light Bounce's callbacks on the same sun draws: this frame's trace first. Only with the helper (its D3D12
  // device traces).
  const auto rays = RaySettings();
  ray_tracing_installed = twattila::dlaa_bridge::HelperInstalled()
                          && (rays.sun_shadows || rays.ambient_occlusion || rays.light_bounce || rays.reflections || rays.local_lights
                              || rays.fog_shadows || rays.specular_occlusion);
  if (ray_tracing_installed) {
    twshogun2::ray_tracing::Install(rays.casters_out_of_view, rays.smoke_shadows);
    reshade::register_event<reshade::addon_event::destroy_device>(twshogun2::ray_tracing::OnDestroyDevice);
  }
  // Frame generation (the helper presents; HUD-less images through the swap chain shader, its constants at b13) and
  // Reflex for the game's own presents.
  twattila::frame_generation::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                      sizeof(shader_injection), __fg_hdr10);
  twattila::reflex::Install();
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyHelperDevice);
  // After the ray tracing's: the bridge released last.
  reshade::register_event<reshade::addon_event::destroy_device>(twshogun2::upscaling::OnDestroyDevice);
  // One line for a run's log (2026-11-14 data run): the options the shaders and the ray tracing read.
  renodx::utils::log::i(std::format(
      "[Shogun 2] Options: AO mode {} (temporal {}, edge fix {}, denoiser {}), Light Bounce strength {}, SSR resolution {}, SSS "
      "samples {} stability {}, shadow map stability {}, sun shadow softness {}; ray tracing {} (sun {}, AO {}, GI {}, reflections "
      "{}, point lights {}, paths {})",
      shader_injection.ssao_mode, ao_filter_rows.temporal != 0.f, ao_filter_rows.edge_fix != 0.f, ao_filter_rows.denoise != 0.f,
      shader_injection.light_bounce_strength, shader_injection.ssr_resolution, shader_injection.sss_samples, shader_injection.sss_stability,
      shader_injection.shadow_map_stability, shader_injection.sun_shadow_softness, (ray_tracing_installed ? "installed" : "not installed"),
      rays.sun_shadows, rays.ambient_occlusion, rays.light_bounce, rays.reflections, rays.local_lights, rays.path_tracing));
  // Light Through Foliage's G-buffer constants from the sun light draws: before the Light Bounce's (its own draw skips
  // the later callbacks).
  twshogun2::foliage_light::Install(twshogun2::ray_tracing::SUN_DRAWS);
  reshade::register_event<reshade::addon_event::destroy_device>(twshogun2::foliage_light::OnDestroyDevice);
  twshogun2::screen_space_shadows::Install(twshogun2::ray_tracing::SUN_DRAWS, twshogun2::ray_tracing::CLOTH_VERTEX_SHADER);
  reshade::register_event<reshade::addon_event::destroy_device>(twshogun2::screen_space_shadows::OnDestroyDevice);
  // The AO filter (its 3D size first, then the filter): before the Light Bounce, which draws the sun draws itself.
  for (const auto& draw : AO_FILTER_DRAWS) {
    renodx::utils::command_action::Register(
        OnAoFilterSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  twattila::ao_filter::Install(AO_FILTER_DRAWS);
  reshade::register_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  // Light Bounce (twattila/light_bounce.hpp): the sun light draws (ray_tracing.hpp's, replaced here).
  twattila::light_bounce::Install(twshogun2::ray_tracing::SUN_DRAWS);

  // Not with the old DLSS add-on in the folder: both would hook the game's DLSS, scale its viewports and patch its
  // engine values (the page's note says so; at the next start without it).
  game_hooks_attached = !old_dlss_addon_found;
  if (game_hooks_attached) {
    // The game's DLSS, render scale and engine values (the DLSS add-on's until it became part of this one): after all of
    // the above, as when that add-on loaded after this one (its present and draw callbacks ran after these).
    // Again when a new device attaches the addon in the same image (frame generation keeps it loaded).
    engine_options.clear();
    for (auto& test : EngineTests()) engine_options.push_back(&test.option);
    ApplyGameRows();
    twshogun2::game_dlss::SetEvaluateCallback([](ID3D11DeviceContext* context, NVSDK_NGX_D3D11_DLSS_Eval_Params* eval) {
      // RENODX_TW_SET's seconds count from the first DLSS evaluation (twattila/test_hooks.hpp).
      twattila::test_hooks::StartClock();
      if (const std::array<uint32_t, 2> size = {eval->InRenderSubrectDimensions.Width, eval->InRenderSubrectDimensions.Height};
          size != evaluated_size) {
        evaluated_size = size;
        const auto display = twshogun2::game_dlss::DisplaySize();
        renodx::utils::log::i(std::format("[Shogun 2 DLSS] Evaluating {}x{} -> {}x{}, jitter ({:.4f}, {:.4f})", size[0], size[1], display[0],
                                          display[1], eval->InJitterOffsetX, eval->InJitterOffsetY));
      }
      twshogun2::render_scale::EndScaling();
      twattila::texture_lod_bias::SetBias(0.f);
      twshogun2::object_velocity::OnEvaluate(context, eval);
      twshogun2::frame_trace::OnEvaluate(context, *eval);
      dlss_jitter_x.store(eval->InJitterOffsetX, std::memory_order_relaxed);
      dlss_jitter_y.store(eval->InJitterOffsetY, std::memory_order_relaxed);
      // The helper's upscaler in place of the game's DLSS, frame generation's inputs (false: the game's).
      if (!helper_evaluates.load(std::memory_order_relaxed)) return false;
      twshogun2::upscaling::UpscaleRequest request = {
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
      return twshogun2::upscaling::Evaluate(request, ray_tracing_installed && twshogun2::ray_tracing::GuidesWritten());
    });
    renodx::utils::log::i(std::format(
        "[Shogun 2 DLSS] Options: render scale {} ({}% custom), preset {}, texture LOD bias {}, jitter {}, object motion {}, "
        "upscaler {} RR {}, frame generation {} method {} Reflex {} limit {}, particle resolution {}, shadow map {}, grass {} model {} "
        "unit {} / {} / {} terrain {}",
        aa_sliders.dlss_mode, aa_sliders.render_scale, aa_sliders.preset, aa_sliders.texture_lod_bias, aa_sliders.jitter, aa_sliders.object_motion,
        aa_sliders.method, aa_sliders.ray_reconstruction, aa_sliders.frame_generation, aa_sliders.frame_backend, aa_sliders.reflex,
        aa_sliders.frame_limit, graphics_rows.particle_resolution, graphics_rows.shadow_map_resolution, graphics_rows.grass_distance,
        graphics_rows.model_detail, graphics_rows.unit_detail, graphics_rows.unit_animation, graphics_rows.unit_variety, graphics_rows.terrain_detail));
    for (const auto& shader : SUN_SHADERS) {
      renodx::utils::command_action::Register(
          OnSunDraw, {.shader_hash = shader.hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &shader);
    }
    twshogun2::object_velocity::Install();
    reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
    reshade::register_event<reshade::addon_event::present>(OnDlssPresent);
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
  }
  twattila::test_hooks::Install(&settings, false, "[Shogun 2]");

  reshade::register_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(tw::line_sweep_ao::OnDestroyDevice);
  twattila::test_hooks::Uninstall();
  if (game_hooks_attached) {
    // The game's DLSS, render scale and engine values.
    twshogun2::frame_trace::Stop();
    renodx::utils::command_action::Unregister(OnSunDraw);
    reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
    reshade::unregister_event<reshade::addon_event::present>(OnDlssPresent);
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
    tw::engine_tweaks::Uninstall(engine_options);
  }

  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(twshogun2::vfx_effects::OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(twshogun2::vfx_effects::OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(twshogun2::vfx_effects::OnUpdateBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(
      twshogun2::vfx_effects::OnUpdateBufferRegionCommand);
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::light_bounce::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::depth_levels::OnDestroyDevice);
  twattila::light_bounce::Uninstall();
  renodx::utils::command_action::Unregister(OnAoFilterSunDraw);
  twattila::ao_filter::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twattila::ao_filter::OnDestroyDevice);
  twshogun2::foliage_light::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twshogun2::foliage_light::OnDestroyDevice);
  twshogun2::screen_space_shadows::Uninstall();
  reshade::unregister_event<reshade::addon_event::destroy_device>(twshogun2::screen_space_shadows::OnDestroyDevice);
  if (ray_tracing_installed) {
    twshogun2::ray_tracing::Uninstall();
    reshade::unregister_event<reshade::addon_event::destroy_device>(twshogun2::ray_tracing::OnDestroyDevice);
  }
  reshade::unregister_event<reshade::addon_event::destroy_device>(twshogun2::upscaling::OnDestroyDevice);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyHelperDevice);
  twattila::reflex::Uninstall();
  twattila::frame_generation::Uninstall();

  twattila::draw_dispatch::Use(DLL_PROCESS_DETACH);
  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);
  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);
  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);

  attached = false;
}

}  // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE) {
  if (!addon_registered) {
    if (!reshade::register_addon(addon_module)) return false;
    addon_registered = true;
  }

  // The DLSS add-on (part of this one now) still next to it would hook the game's DLSS a second time: the page and the
  // log say so, and AttachAddon leaves that half out.
  std::wstring path(MAX_PATH, L'\0');
  path.resize(GetModuleFileNameW(addon_module, path.data(), MAX_PATH));
  std::error_code error;
  old_dlss_addon_found = std::filesystem::exists(std::filesystem::path(path).replace_filename(L"renodx-twshogun2completeedition_dlss.addon64"), error);
  if (old_dlss_addon_found) {
    renodx::utils::log::w("[Shogun 2] renodx-twshogun2completeedition_dlss.addon64 is still in the game folder: delete it (its DLSS options "
                          "are part of this add-on now). Until then this add-on's DLSS, render scale and engine values stay off.");
  }
  return AttachAddon();
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE) {
  DetachAddon();
  twattila::dlaa_bridge::Shutdown();
  if (addon_registered) {
    reshade::unregister_addon(addon_module);
    addon_registered = false;
  }
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(h_module)) return FALSE;
      addon_registered = true;
      break;

    case DLL_PROCESS_DETACH:
      // ReShade calls AddonUninit before an explicit unload. During process
      // termination, avoid calling into ReShade or RenoDX under the loader lock.
      if (lpv_reserved == nullptr && addon_registered) {
        reshade::unregister_addon(h_module);
        addon_registered = false;
      }
      break;
  }

  return TRUE;
}
