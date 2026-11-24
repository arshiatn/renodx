/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#include <include/reshade_api_resource.hpp>
#define ImTextureID ImU64

#define RENODX_MODS_SWAPCHAIN_VERSION 2
// F9 logs one frame of the HUD's flat-shape draws to ryse_ui_draws.txt (how the execution skull was found).
// #define RYSE_UI_DRAW_LOG

#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <format>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/bitwise.hpp"
#include "../../utils/command_action.hpp"
#include "../../utils/command_action_fast.hpp"
#include "../../utils/data.hpp"
#include "../../utils/format.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/resource.hpp"
#include "../../utils/settings.hpp"
#include "./contact_shadows.hpp"
#include "./graphics.hpp"
#include "./light_bounce.hpp"
#include "./ray_tracing.hpp"
#include "./shadow_skip.hpp"
#include "../twattila/psycho35_settings.hpp"
#include "../twattila/credits.hpp"
#include "../twattila/device_loss.hpp"
#include "../twattila/draw_dispatch.hpp"
#include "../twattila/output_encoding.hpp"
#include "../twattila/pass_timing.hpp"
#include "../twattila/rays_preset.hpp"
#include "../twattila/settings_pages.hpp"
// RENODX_TW_PROFILE's action: rt_core's Record Ray Tracing Profile (this add-on has no Total War ray-tracing scene).
#define RENODX_TEST_HOOKS_PROFILE twattila::rt_scene::StartProfile
#include "../twattila/test_hooks.hpp"
#include "./shared.h"

constexpr char BUILD_DATE[] = __DATE__;
constexpr char BUILD_TIME[] = __TIME__;

namespace {

ShaderInjectData shader_injection;
float current_settings_mode = 0.f;  // the page: 0 HDR & Effects, 1 Graphics 1 of 2, 2 Graphics 2 of 2
float advanced_settings = 0.f;
// Hotkeys (OnPresentHotkeys), not saved: the HUD shaders also draw the menus.
std::atomic_bool hud_hidden = false;
std::atomic_bool execution_highlights_hidden = false;
std::atomic_bool execution_skull_hidden = false;

bool IsAdvancedSettings() {
  return advanced_settings >= 1.f;
}

bool IsHDRMode() {
  return shader_injection.hdr >= 1.f;
}

bool IsExtendedMode() {
  return shader_injection.hdr == 1.f && IsAdvancedSettings();
}

bool IsPsychoMode() {
  return shader_injection.hdr == 2.f && IsAdvancedSettings();
}

bool IsPsychoV35Mode() {
  return shader_injection.hdr == 3.f && IsAdvancedSettings();
}

// Hue Clip Emulation's default (test/tw_psychov35's hue report: closest to the game's SDR picture; part 5, Hable on each channel).
constexpr float HUE_CLIP_DEFAULT = 80.f;

// PsychoV35's defaults that follow the game's response (shared.h): Hable's darker shadows and its colour.
const std::vector<std::pair<std::string, float>> V35_GAME_RESPONSE = {
    {"v35_contrast", RYSE_V35_CONTRAST},
    {"v35_shadows", RYSE_V35_SHADOWS},
    {"v35_saturation", RYSE_V35_SATURATION},
};

bool IsHdrOutput() {
  return shader_injection.swap_chain_encoding != 2.f;
}

// The fire settings (ATN): Option 1 (also the sliders' defaults, Reset and the Recommended presets) and Option 2, for
// PsychoV31 and for Extended (its own sliders; Dechroma and Highlight Saturation are V31's).
const std::vector<std::pair<std::string, float>> FIRE_OPTION_1 = {
    {"dechroma", 0.f},
    {"highlight_saturation", 1.f},
    {"highlight_hue_shift", 0.03f},
    {"highlight_saturation_shift", 0.1f},
    {"fire_hue", 0.f},
    {"fire_hue_focus", 0.5f},
};
const std::vector<std::pair<std::string, float>> FIRE_OPTION_2 = {
    {"dechroma", 0.f},
    {"highlight_saturation", 1.f},
    {"highlight_hue_shift", 0.11f},
    {"highlight_saturation_shift", 0.07f},
    {"fire_hue", -0.05f},
    {"fire_hue_focus", 0.75f},
};
const std::vector<std::pair<std::string, float>> EXT_FIRE_OPTION_1 = {
    {"ext_highlight_hue_shift", 0.03f},
    {"ext_highlight_saturation_shift", 0.1f},
    {"ext_fire_hue", 0.f},
    {"ext_fire_hue_focus", 0.5f},
};
const std::vector<std::pair<std::string, float>> EXT_FIRE_OPTION_2 = {
    {"ext_highlight_hue_shift", 0.11f},
    {"ext_highlight_saturation_shift", 0.07f},
    {"ext_fire_hue", -0.05f},
    {"ext_fire_hue_focus", 0.75f},
};
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
      {"exposure", 1.f},
      {"highlights", 1.f},
      {"shadows", 1.f},
      {"contrast", 1.f},
      {"saturation", 1.f},
      {"highlight_contrast", 1.f},
      {"shadow_contrast", 1.f},
      {"flare", 0.f},
      {"gamut_compression", 1.f},
      {"gamut_compression_mode", 1.f},
      {"roll_off", 1.f},
      {"game_shadows", 0.f},
      {"bloom", 1.f},
      {"vignette", 1.f},
      {"sun_shafts", 1.f},
      {"lens_flare", 1.f},
      {"chromatic_aberration_strength", 0.f},
      {"film_grain", 1.f},
      {"sharpening", 0.f},
      {"motion_blur_camera", 1.f},
      {"motion_blur_object", 1.f},
      {"depth_of_field", 1.f},
      {"video_highlights", 0.f},
      {"video_dithering", 1.f},
  });
  renodx::utils::settings::UpdateSettings(FIRE_OPTION_1);
  renodx::utils::settings::UpdateSettings(EXT_FIRE_OPTION_1);
  renodx::utils::settings::UpdateSettings(twattila::psycho35::Defaults(HUE_CLIP_DEFAULT));
  renodx::utils::settings::UpdateSettings(V35_GAME_RESPONSE);
}

// The game's own picture (Rendering Mode SDR); the button also sets Output SDR, a first start on a display without HDR
// gets it through Output Auto.
void ApplySdrPreset() {
  ApplyResetPreset();
  renodx::utils::settings::UpdateSettings({{"tonemapper", 0.f}});
}

// The Recommended button; Output Auto takes it once when Windows HDR comes on after a first start without HDR.
void ApplyRecommendedPreset() {
  ApplyResetPreset();
  // Recommended (ATN 2026-11-12: PsychoV35, its sliders at their defaults).
  renodx::utils::settings::UpdateSettings({
      {"tonemapper", 3.f},
      {"film_grain", 0.f},
      {"motion_blur_camera", 0.f},
      {"motion_blur_object", 0.f},
      {"sharpening", 0.25f},
      {"depth_of_field", 2.f},
      {"video_highlights", 0.2f},
  });
}

// This add-on alone (the upstream snapshot): the helper and the NVIDIA, AMD and Intel files come with the full download.
using twattila::graphics_page::FULL_DOWNLOAD_NOTE;
using twattila::graphics_page::Note;
using twattila::graphics_page::WARNING_TINT;
// Settings Mode's pages (twattila/settings_pages.hpp AssignPages): their sections (the Total War add-ons' names; the rest
// is HDR & Effects).
const std::string AA_SECTION = "Anti-Aliasing and Upscaling";
const std::vector<std::string> GRAPHICS_1_SECTIONS = {AA_SECTION, "Ambient Occlusion", "Contact Shadows", "Light Bounce",
                                                      "Ray Tracing (Experimental)"};
const std::vector<std::string> GRAPHICS_2_SECTIONS = {"Graphics", "Graphics (Experimental)", "Audio"};

bool HelperAbsent() {
  return !twattila::dlaa_bridge::HelperInstalled();
}

renodx::mods::shader::CustomShaders custom_shaders;
renodx::utils::settings::Settings settings;

// RT/PT's value of `key` (twattila/rays_preset.hpp): the traced Light Bounce's and the paths' sliders start and Reset at it
// (ATN 2026-11-22). 0 for a key the list lacks. Not during static initialization: the list may be empty then.
float RtPtValue(std::string_view key) {
  const auto value = std::ranges::find(twattila::graphics_page::RAYS_PRESET, key, &twattila::graphics_page::Values::value_type::first);
  return (value != twattila::graphics_page::RAYS_PRESET.end()) ? value->second : 0.f;
}

// Ray tracing (ray_tracing.hpp): its sliders; installed when on at launch (its scene's shaders are kept from the start).
struct RtSliders {
  float sun_shadows = 0.f;
  float softness = 0.5f;  // degrees of the sun's disk
  float replace_sun_maps = 0.f;  // the rays' sun shadow alone in the mask's sun channels (contact_shadows.hpp)
  float casters_out_of_view = 0.f;  // rt_core's cascade capture from the sun's maps (ray_tracing.hpp SceneDraw)
  float skip_sun_maps = 0.f;  // shadow_skip.hpp's Choice
  float ambient_occlusion = 0.f;
  float ambient_range = 2.f;  // metres
  float ambient_quarter = 0.f;
  float reflections = 0.f;
  float reflection_samples = 1.f;
  float specular_occlusion = 0.f;  // without the traced reflections: merged into the game's screen-space ones
  float light_bounce = 0.f;
  // rays, range, bounces and light_bounce_resolution: their rows' defaults (RT/PT's) when the settings load.
  float rays = 0.f;
  float range = 0.f;  // metres
  float path_tracing = 0.f;
  float bounces = 0.f;
  float path_ambient = 0.f;  // Path-Traced Ambient (ray_tracing.hpp): the paths' sky for the game's probes' ambient
  float path_sky_apart = 1.f;
  float path_kept_cube = 0.f;
  // Attila's rows (twattila/graphics_page.hpp's keys; Experimental here).
  float denoiser = 0.f;    // RayTracingDenoiser: built-in, NRD (with the helper's upscaler)
  float sharc = 0.f;       // with path tracing
  float reuse_lean = 1.f;  // with path tracing
  float reuse_near = 1.f;  // with path tracing
  float path_ser = 0.f;    // the paths' and reflections' rays
  float output_rays = 0.f;  // Rays a Pixel: 1, Match Output, 2-8
  float rays_shadows = 1.f;
  float rays_ambient = 1.f;
  float rays_reflections = 1.f;
  float rays_paths = 1.f;
  float debug_log = 0.f;
  float ambient_rays = 2.f;
  float light_bounce_resolution = 0.f;  // the ray-traced Light Bounce's: 0 full, 1 half, 2 quarter
  float reflection_reuse = 0.f;         // 0 off, 1 from ReSTIR GI (with path tracing), 2 Own ReSTIR
  float ao_reblur = 0.f;                // AO under Ray Reconstruction: 0 Light History, 1 REBLUR
  float debug_view = 0.f;               // rt_core's Debug View value (the row's parse)
  float units_range = 0.f;              // Characters Ray Tracing Range: metres (0: every character in the scene)
  float scene_test = 0.f;
} rt_sliders;
bool ray_tracing_installed = false;
// The rays' AO went into this frame's lighting (render thread; read at the present).
std::atomic_bool rays_ambient_applied = false;
// A near shadow mask permutation we don't replace (ShaderCache.pak: edgeMaskS at t5) drew since the start: the game's
// edge mask (ps 0x292F2865) is drawn again for good (render thread).
std::atomic_bool edge_mask_game_reader = false;
constexpr uint32_t RAYS_TINT = 0x6BE0D8;

// Path-Traced Ambient: with path tracing on the Light Bounce (its paths bring the sky), in place of the ray-traced AO too.
bool PathAmbientWanted() {
  return rt_sliders.path_ambient != 0.f && rt_sliders.path_tracing != 0.f && shader_injection.light_bounce > 0.f;
}

// With a ray-traced effect on (the rows for all of them; at launch: the ray tracing installed). Specular Occlusion alone
// is none: it counts only beside another (RaysBesideSpecularOcclusion).
bool AnyRays() {
  return rt_sliders.sun_shadows != 0.f || rt_sliders.ambient_occlusion != 0.f || rt_sliders.reflections != 0.f
         || rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f;
}

// A ray-traced effect traced this frame besides Specular Occlusion (Attila's rt_core.hpp RaysBesideSpecularOcclusion): alone
// it started the ray tracing, its guides and Ray Reconstruction for itself, so then it is off, the game's look.
bool RaysBesideSpecularOcclusion() {
  return rt_sliders.sun_shadows != 0.f || rt_sliders.ambient_occlusion != 0.f || rt_sliders.reflections != 0.f
         || (shader_injection.light_bounce > 0.f && (rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f));
}

// The Ray Tracing presets: RT/PT writes RaysReset(), then the one list every game's RT/PT writes
// (twattila/rays_preset.hpp; the keys this section lacks are skipped), then RAYS_OWN_KEYS; No RT/PT RAYS_OFF and the
// screen-space methods; Reset RAYS_OFF and RaysReset().
using PresetValues = twattila::graphics_page::Values;
const PresetValues RAYS_OFF = {
    {"RayTracedSunShadows", 0.f}, {"RayTracedAmbientOcclusion", 0.f}, {"RayTracedReflections", 0.f},
    {"RayTracedSpecularOcclusion", 0.f}, {"RayTracedLightBounce", 0.f}, {"PathTracing", 0.f}, {"RayTracedPathAmbient", 0.f},
};
// The list's methods in this section's keys: the Light Bounce on and traced (one row here, Attila's 2 the paths), path
// tracing its own row, the Light Bounce's strength `light_bounce`; Path-Traced Ambient off (Experimental, until it is tested
// in game): ray-traced AO in place of the game's SSDO amount.
const PresetValues RAYS_OWN_KEYS = {
    {"RayTracedLightBounce", 1.f}, {"PathTracing", 1.f}, {"light_bounce", 1.f}, {"RayTracedAmbientOcclusion", 1.f}, {"ssao_mode", 0.f},
    {"RayTracedPathAmbient", 0.f},
};
// Built at its first use, after static initialization (RtPtValue).
const PresetValues& RaysReset() {
  static const PresetValues RAYS_RESET = {
      {"RayTracedShadowSoftness", 0.5f},
      {"RayTracedReplaceShadowMaps", 0.f},
      {"RayTracedUnitsOutOfView", 0.f},
      {"RayTracedSkipSunShadowMaps", 0.f},
      {"RayTracedAmbientRange", 2.f},
      {"RayTracedAOQuarter", 0.f},
      {"RayTracedReflectionSamples", 1.f},
      {"RayTracedLightBounceRays", RtPtValue("RayTracedLightBounceRays")},
      {"RayTracedLightBounceRange", RtPtValue("RayTracedLightBounceRange")},
      {"PathTracingBounces", RtPtValue("PathTracingBounces")},
      {"RayTracedPathSkyApart", 1.f},
      {"RayTracedPathKeptCube", 0.f},
      {"RayTracingDenoiser", 0.f},
      {"RayTracedSharc", 0.f},
      {"RayTracedReuseLean", 1.f},
      {"RayTracedReuseNear", 1.f},
      {"RayTracedPathSer", 0.f},
      {"RayTracingOutputResolution", 0.f},
      {"RayTracingRaysShadows", 1.f},
      {"RayTracingRaysAmbient", 1.f},
      {"RayTracingRaysReflections", 1.f},
      {"RayTracingRaysPaths", 1.f},
      {"RayTracedDebugLog", 0.f},
      {"RayTracedAORays", 2.f},
      {"RayTracedLightBounceResolution", RtPtValue("RayTracedLightBounceResolution")},
      {"RayTracedReflectionReuse", 0.f},
      {"RayTracedAoReblur", 0.f},
      {"RayTracedDebugView", 0.f},
      {"RayTracedSceneTest", 0.f},
      {"RayTracedUnitsRange", 0.f},
  };
  return RAYS_RESET;
}

void BuildRuntimeData() {
  custom_shaders = {
      CustomShaderEntry(0xB9B672AA),  // tonemap00 (HDRFinalPass)
      CustomShaderEntry(0xF2D692D1),  // smaa00 (SMAA neighborhood blending)
      CustomShaderEntry(0x121C45E4),  // taa00
      CustomShaderEntry(0xD0B247FF),  // post00 (sharpening, grain)
      CustomShaderEntry(0xB022AFEA),  // post01 (sharpening, grain, lens flares)
      CustomShaderEntry(0xC3A469C1),  // post02 (grain)
      CustomShaderEntry(0xCC6DF78E),  // post03 (grain, lens flares)
      CustomShaderEntry(0xCD8548F6),  // upscale00 (native upscaling)
      CustomShaderEntry(0x799A5EB8),  // mb00 (motion blur velocity pack)
      CustomShaderEntry(0x910E0E0C),  // dof00 (depth of field composite)
      CustomShaderEntry(0x4DFAE399),  // shafts00 (sun shafts mask)
      CustomShaderEntry(0xF1FCED4E),  // shafts01 (sun shafts radial blur)
      CustomShaderEntry(0x37ABF6CF),  // video00
      CustomShaderEntry(0xC1F889A4),  // video01 (alpha)
      CustomShaderEntry(0xD1106E8B),  // ssdo00 (SSDO; Ambient Occlusion GT-VBAO)
      CustomShaderEntry(0xC17645D2),  // ssdo01 (ssdo00 with its depth samples from t5)
      CustomShaderEntry(0xA65B60AF),  // ssdo02 (SSDO without the terrain's LSDO)
      CustomShaderEntry(0xE9264ADE),  // sunshadow00 (sun shadow mask, near cascades; Sun Shadow Softness)
      CustomShaderEntry(0xB5DC6E36),  // sunshadow01
      CustomShaderEntry(0xF2B4EF30),  // sunshadow02 (middle cascade)
      CustomShaderEntry(0xF864A048),  // sunshadow03
      CustomShaderEntry(0xF81C7168),  // sunshadow04 (far cascade)
  };
  // Not replaced; their draws skipped while the hotkeys hide them. Scaleform's text (0x96675B77) and flat shapes
  // (0x1B59F534, in OnDrawUi / OnDrawIndexedUi: the execution skull is one of them): the HUD, also the menus. The
  // execution highlights on enemies: highlight_shaders.
  const auto keep_shader = [](reshade::api::command_list*) { return false; };
  const auto draw_hud = [](reshade::api::command_list*) { return !hud_hidden; };
  custom_shaders[0x96675B77] = {.crc32 = 0x96675B77, .on_replace = keep_shader, .on_draw = draw_hud};
  // The one-frame trace: its PER_BATCH (the PsychoV35 fit's constants).
  custom_shaders[0xB9B672AA].on_draw = [](reshade::api::command_list* cmd_list) {
    rysesonofrome::graphics::TraceTonemapConstants(cmd_list);
    return true;
  };
  // The tiled lighting (kept): this frame's ray tracing, then the contact shadows (and the ray-traced sun's) into the
  // sun's shadow mask and the ray-traced ambient into its occlusion before it runs, the traced reflections bound for it.
  const auto contact_shadows = [](reshade::api::command_list* cmd_list) {
    ID3D11ShaderResourceView* sun_visibility = nullptr;
    ID3D11ShaderResourceView* ambient_visibility = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (ray_tracing_installed && cmd_list->get_device()->get_api() == reshade::api::device_api::d3d11) {
      context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&context);
      if (rysesonofrome::ray_tracing::Trace(context)) {
        const auto [sun, ambient] = twattila::rt_scene::Visibilities();
        if (rt_sliders.sun_shadows != 0.f) sun_visibility = sun;
        if (rt_sliders.ambient_occlusion != 0.f && !PathAmbientWanted()) ambient_visibility = ambient;
      }
    }
    // The rays' AO in place of the game's SSDO amount while ssdo00 left it out (the AO method 2), else the darker; their
    // sun shadow in place of the game's cascades' with Rays Replace Shadow Maps.
    const auto applied = rysesonofrome::contact_shadows::Apply(cmd_list,
                                                               {
                                                                   .length = shader_injection.contact_shadow_length,
                                                                   .thickness = shader_injection.contact_shadow_thickness,
                                                                   .strength = shader_injection.contact_shadows,
                                                                   .frame = shader_injection.ssao_frame,
                                                               },
                                                               sun_visibility, ambient_visibility, shader_injection.ao_method == 2.f,
                                                               rt_sliders.replace_sun_maps != 0.f);
    // Skip the Game's Sun Shadow Maps: the next frame's maps only with the rays' in this one's mask.
    if (applied.sun) rysesonofrome::shadow_skip::NoteRays();
    rays_ambient_applied.store(applied.ambient, std::memory_order_relaxed);
    if (context != nullptr) rysesonofrome::ray_tracing::BindLightingInputs(context);
    return true;
  };
  // After it: the Light Bounce added to the scene it lit. Traced by rays: at their own resolution, every texel each frame
  // (no Checkerboard), as Attila's (twattila/light_bounce.hpp SetRayTraced).
  const auto light_bounce = [](reshade::api::command_list* cmd_list) {
    const auto power_of_two = [](float exponent) { return static_cast<float>(1u << static_cast<uint32_t>(std::clamp(exponent, 0.f, 3.f))); };
    if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    if (ray_tracing_installed) rysesonofrome::ray_tracing::RestoreLightingInputs(context);
    const bool traced = ray_tracing_installed && (rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f);
    const bool temporal = shader_injection.light_bounce_temporal != 0.f;
    rysesonofrome::light_bounce::Apply(context,
                                       {
                                           .strength = shader_injection.light_bounce,
                                           .radius = shader_injection.light_bounce_radius,
                                           .thickness = shader_injection.ssao_thickness,
                                           .steps = 8.f * power_of_two(shader_injection.light_bounce_quality),
                                           .slices = power_of_two(shader_injection.light_bounce_directions),
                                           .scale = power_of_two(traced ? rt_sliders.light_bounce_resolution : shader_injection.light_bounce_resolution),
                                           .history = (temporal ? 0.95f : 0.f),
                                           .frame = (temporal ? shader_injection.ssao_frame : 0.f),
                                           .bounces = shader_injection.light_bounce_multi,
                                           .checkerboard = ((temporal && !traced && shader_injection.light_bounce_checkerboard != 0.f) ? 1.f : 0.f),
                                           .backface = ((shader_injection.light_bounce_backface != 0.f) ? 1.f : 0.f),
                                           .auto_thickness = ((shader_injection.light_bounce_auto_thickness != 0.f) ? 1.f : 0.f),
                                       },
                                       shader_injection.light_bounce_denoise != 0.f);
  };
  // The same before and after its two permutations in ShaderCache.pak (rym_ census; not in a frame trace yet):
  // 0x488BF732 (another sun specular term) and 0xCDB59F36 (no screen-space reflections: t6 unread). Their resources,
  // slots and PER_BATCH are 0x897232B5's (fxc /dumpbin declarations and the cbuffer compared: only t6 missing in the
  // second). The raised light limit's own lighting cs stays 0x897232B5's alone (graphics.hpp OnLightingDispatch).
  for (const uint32_t tiled_lighting : {0x897232B5u, 0x488BF732u, 0xCDB59F36u}) {
    custom_shaders[tiled_lighting] = {.crc32 = tiled_lighting, .on_replace = keep_shader, .on_draw = contact_shadows, .on_drawn = light_bounce};
  }
  // ps 0x292F2865 (R0 traces: three draws a frame at a quarter of the render size, before the near cascades' masks; two
  // blurs after): the game's contact-hardening estimate, the blockers' mean distance over 16 map taps, into the edge mask
  // that only the near mask passes read (t5 edgeMaskS) to widen their kernel. Under Sun Shadow Softness our sunshadow00 /
  // 01 take PCSS's lit share in place of that kernel's, so the estimate goes unread: left out. Its permutations' unreplaced
  // readers (the near masks with 8 / 4 taps or a per-pixel kernel scale) keep it for good once one draws.
  const auto game_edge_reader = [](reshade::api::command_list*) {
    if (!edge_mask_game_reader.exchange(true, std::memory_order_relaxed)) {
      renodx::utils::log::i("[Ryse] A sun shadow mask pass the add-on doesn't replace reads the edge mask: ps 0x292F2865 draws always now.");
    }
    return true;
  };
  for (const uint32_t game_reader : {0x361344F1u, 0x3017167Du, 0x3D8309D7u, 0x51BA8CEAu, 0x954DEA3Cu, 0xC7EEE343u}) {
    custom_shaders[game_reader] = {.crc32 = game_reader, .on_replace = keep_shader, .on_draw = game_edge_reader};
  }
  custom_shaders[0x292F2865] = {.crc32 = 0x292F2865, .on_replace = keep_shader, .on_draw = [](reshade::api::command_list*) {
                                  return shader_injection.sun_shadow_softness == 0.f || edge_mask_game_reader.load(std::memory_order_relaxed);
                                }};
  // The screen-space reflections' trace: left out while the traced reflections take its place.
  const auto reflection_trace = [](reshade::api::command_list*) {
    return !ray_tracing_installed || rysesonofrome::ray_tracing::OnReflectionTrace();
  };
  custom_shaders[0xE0B7E115] = {.crc32 = 0xE0B7E115, .on_replace = keep_shader, .on_draw = reflection_trace};
  // The game's copy of its HDR scene (also other copies; ray_tracing.hpp picks the scene's): the helper's last frame's
  // colour.
  const auto keep_color = [](reshade::api::command_list* cmd_list) {
    if (!ray_tracing_installed || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&context);
    rysesonofrome::ray_tracing::KeepColor(context);
  };
  custom_shaders[0x3BA5B162] = {.crc32 = 0x3BA5B162, .on_replace = keep_shader, .on_drawn = keep_color};

  // Ray Tracing's preset buttons and Attila's rows (an off / on or a list, default 0 or as RaysReset()'s), with the helper
  // and a ray-traced effect on; `enabled`: when they act.
  const auto ray_button = [](const std::string& label, const std::string& tooltip, std::function<void()> on_change) {
    return new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = label,
        .section = "Ray Tracing (Experimental)",
        .group = "rays-presets",
        .tooltip = tooltip,
        .tint = RAYS_TINT,
        // ATN 2026-11-17 "For every single game, the LOD bias for everything should be on": each of them turns Texture LOD
        // Bias on All Textures on (as Attila's page presets; the bias's own level stays the player's).
        .on_change = [on_change = std::move(on_change)] {
          on_change();
          renodx::utils::settings::UpdateSettings({{"AntiAliasingTextureLodBiasTrilinear", 1.f}});
        },
        .is_visible = twattila::dlaa_bridge::HelperInstalled,
    };
  };
  const auto ray_choice = [](const std::string& key, float* binding, const std::string& label, std::vector<std::string> labels,
                             const std::string& tooltip, std::function<bool()> enabled = [] { return true; }) {
    const auto reset = std::ranges::find(RaysReset(), key, &PresetValues::value_type::first);
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = (reset != RaysReset().end()) ? reset->second : 0.f,
        .label = label,
        .section = "Ray Tracing (Experimental)",
        .tooltip = tooltip,
        .labels = std::move(labels),
        .is_enabled = [enabled] { return AnyRays() && enabled(); },
        .is_visible = [] { return twattila::dlaa_bridge::HelperInstalled() && AnyRays(); },
    };
  };
  const std::vector<std::string> off_on = {"Off", "On"};
  const auto paths = [] { return (rt_sliders.light_bounce != 0.f || rt_sliders.reflections != 0.f) && rt_sliders.path_tracing != 0.f; };
  const auto extra_rays = [] { return rt_sliders.output_rays != 0.f; };
  // 0-max slider (default 2), 1 = the game's.
  auto slider = [](const std::string& key, float* binding, const std::string& label, const std::string& section,
                   const std::string& tooltip, bool (*is_enabled)(), bool (*is_visible)(), float default_value = 1.f,
                   float max = 2.f) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .default_value = default_value,
        .label = label,
        .section = section,
        .tooltip = tooltip,
        .min = 0.f,
        .max = max,
        .format = "%.2f",
        .is_enabled = is_enabled,
        .is_visible = is_visible,
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
          .tooltip = "HDR & Effects: the picture. Graphics 1 of 2: anti-aliasing, DLSS, frame generation, shadows, ambient\n"
                     "occlusion, Light Bounce and ray tracing. Graphics 2 of 2: the game's settings above its High.",
          .labels = {"HDR & Effects", "Graphics 1 of 2", "Graphics 2 of 2"},
          .is_global = true,
      },
      // Simple / Advanced, once Settings Mode's own (until the one add-on; ConfigureAddon moves a saved Advanced here once).
      new renodx::utils::settings::Setting{
          .key = "SettingsAdvanced",
          .binding = &advanced_settings,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .can_reset = false,
          .label = "Options",
          .tooltip = "Advanced: every slider, the output and the diagnostics.",
          .labels = {"Simple", "Advanced"},
          .is_global = true,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = "Hotkeys (reset at start): F6 / Caps Lock: HUD (menus too). F7 / Tab: execution highlights.\n"
                   "F5: execution skull. Controller: map them in Steam Input (e.g. back buttons).",
          .section = "General",
      },
      Note(FULL_DOWNLOAD_NOTE, "General", WARNING_TINT, HelperAbsent),
      new renodx::utils::settings::Setting{
          .key = "peak_white_nits",
          .binding = &shader_injection.peak_white_nits,
          .default_value = 1000.f,
          .can_reset = false,
          .label = "Peak",
          .section = "General",
          .tooltip = "Maximum brightness output in nits.",
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
          .tooltip = "Brightness of the scene/game and videos.",
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
          .tooltip = "Brightness of UI and HUD in nits.",
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
          .tooltip = "SDR: the game's native SDR tonemapper.\n"
                     "HDR (Extended): the game's native Filmic (Uncharted 2) tonemapper, extended into HDR\n"
                     "(UC2 Extended). As close as possible to the original SDR look (default).\n"
                     "HDR (PsychoV31): replaces the Filmic tonemapper with PsychoV31, keeping the game's\n"
                     "grading (exposure, bloom, color balance, sun shafts, color charts).\n"
                     "HDR (PsychoV35): the same with Musa's PsychoV35 and Hue Clip Emulation (Recommended).",
          .labels = {"SDR", "HDR (Extended)", "HDR (PsychoV31)", "HDR (PsychoV35)"},
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended (All Post Processing On)",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV35), with all vanilla post processing, \n"
                     "hidden chromatic aberration, sharpening at 0.5, depth of field at 2 and Video AutoHDR at 0.2.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() {
            ApplyResetPreset();
            renodx::utils::settings::UpdateSettings({
                {"tonemapper", 3.f},
                {"film_grain", 0.25f},
                {"motion_blur_camera", 0.20f},
                {"motion_blur_object", 0.50f},
                {"chromatic_aberration_strength", 0.35f},
                {"depth_of_field", 2.f},
                {"sharpening", 0.5f},
                // {"sun_shafts", 0.5f},
                {"video_highlights", 0.2f},
            });
          },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Recommended",
          .section = "Presets",
          .group = "preset-line-1",
          .tooltip = "Recommended look: HDR (PsychoV35) with the game's shadows and colours, no film grain,\n"
                     "no motion blur, no chromatic aberration. Sharpening at 0.25, depth of field at 2, Video AutoHDR at 0.2.\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() { ApplyRecommendedPreset(); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Reset",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "Resets image controls to their defaults (HDR Extended).\n"
                     "Peak, Paper White, UI brightness, Settings Mode and output are kept.",
          .on_change = []() { ApplyResetPreset(); },
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "SDR (monitors without HDR)",
          .section = "Presets",
          .group = "preset-line-2",
          .tooltip = "For SDR monitors: Rendering Mode SDR (the game's own picture) and Output SDR (restart the game if it\n"
                     "was HDR). Peak, Paper White, UI brightness and Settings Mode are kept.",
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
      slider("ext_shadows", &shader_injection.ext_shadows, "Shadows", "Extended", "", IsExtendedMode,
             IsExtendedMode),
      slider("ext_contrast", &shader_injection.ext_contrast, "Contrast", "Extended",
             "1 = the game's own contrast.", IsExtendedMode, IsExtendedMode),
      slider("ext_saturation", &shader_injection.ext_saturation, "Saturation / Purity", "Extended",
             "1 = like SDR.", IsExtendedMode, IsExtendedMode),
      slider("ext_highlight_saturation", &shader_injection.ext_highlight_saturation, "Highlight Saturation",
             "Extended", "How much brighter colors desaturate. 0 = more, 1 = default, 2 = less.", IsExtendedMode,
             IsExtendedMode),
      Note("Fire too yellow, green or pink? Pick one or change the settings below.", "Extended", 0xFFD23C, IsExtendedMode),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Option 1 (Default)",
          .section = "Extended",
          .group = "extended-fire",
          .tooltip = "Highlight Hue Shift 0.03, Highlight Saturation Shift 0.10, Fire Hue Focus 0.50.",
          .on_change = []() { renodx::utils::settings::UpdateSettings(EXT_FIRE_OPTION_1); },
          .is_visible = IsExtendedMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Option 2",
          .section = "Extended",
          .group = "extended-fire",
          .tooltip = "Highlight Hue Shift 0.11, Highlight Saturation Shift 0.07, Fire Hue -0.05, Fire Hue Focus 0.75.",
          .on_change = []() { renodx::utils::settings::UpdateSettings(EXT_FIRE_OPTION_2); },
          .is_visible = IsExtendedMode,
      },
      slider("ext_highlight_hue_shift", &shader_injection.ext_highlight_hue_shift, "Highlight Hue Shift", "Extended",
             "Bright colors take the game's SDR hues: fire more orange, less yellow / green or pink.\n"
             "0 = off, 1 = SDR hues.",
             IsExtendedMode, IsExtendedMode, 0.03f, 1.f),
      slider("ext_highlight_saturation_shift", &shader_injection.ext_highlight_saturation_shift,
             "Highlight Saturation Shift", "Extended",
             "Gives bright colors the saturation of the game's SDR look.\n0 = off, 1 = SDR's.", IsExtendedMode,
             IsExtendedMode, 0.1f, 1.f),
      new renodx::utils::settings::Setting{
          .key = "ext_fire_hue",
          .binding = &shader_injection.ext_fire_hue,
          .default_value = 0.f,
          .label = "Fire Hue (OkLCh)",
          .section = "Extended",
          .tooltip = "Rotates the OkLCh hue of bright warm colors (fire): below 0 redder, above 0 more yellow.\n"
                     "0 = off.",
          .min = -1.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsExtendedMode,
          .is_visible = IsExtendedMode,
      },
      slider("ext_fire_hue_focus", &shader_injection.ext_fire_hue_focus, "Fire Hue Focus (OkLCh)", "Extended",
             "Pulls pink and yellow fire toward orange (OkLCh hue). 0 = off, 1 = all orange.", IsExtendedMode,
             IsExtendedMode, 0.5f, 1.f),

      // PsychoV31 ///////////////////////////////////////////////////////////////////////////////////
      slider("exposure", &shader_injection.exposure, "Exposure", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("highlights", &shader_injection.highlights, "Highlights", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("shadows", &shader_injection.shadows, "Shadows", "PsychoV31", "", IsPsychoMode, IsPsychoMode),
      slider("contrast", &shader_injection.contrast, "Contrast", "PsychoV31", "1 = PsychoV31's own.", IsPsychoMode,
             IsPsychoMode),
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
      slider("roll_off", &shader_injection.roll_off, "Highlight Roll-off", "PsychoV31",
             "How soon highlights start to compress toward Peak.\n0 = latest, 1 = default, 2 = soonest.",
             IsPsychoMode, IsPsychoMode),
      slider("gamut_compression", &shader_injection.gamut_compression, "Gamut Compression", "PsychoV31",
             "Pulls colors your display can't show back inside its color space.\n"
             "0 = off (the display clips them), 1 = full (default).",
             IsPsychoMode, IsPsychoMode, 1.f, 1.f),
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
          .key = "game_shadows",
          .binding = &shader_injection.game_shadows,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Game Shadows (experimental)",
          .section = "PsychoV31",
          .tooltip = "Off: PsychoV31's own shadows.\n"
                     "On: the game's darker shadows (Filmic toe) below mid gray, faded in over the stop\n"
                     "below it.",
          .labels = {"Off", "On"},
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      Note("Fire too pink, too yellow or too pale? Pick one or change the settings below.", "PsychoV31", 0xFFD23C, IsPsychoMode),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Option 1 (Default)",
          .section = "PsychoV31",
          .group = "psychov31-fire",
          .tooltip = "Highlight Hue Shift 0.03, Highlight Saturation Shift 0.10, Fire Hue Focus 0.50.",
          .on_change = []() { renodx::utils::settings::UpdateSettings(FIRE_OPTION_1); },
          .is_visible = IsPsychoMode,
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Option 2",
          .section = "PsychoV31",
          .group = "psychov31-fire",
          .tooltip = "Highlight Hue Shift 0.11, Highlight Saturation Shift 0.07, Fire Hue -0.05, Fire Hue Focus 0.75.",
          .on_change = []() { renodx::utils::settings::UpdateSettings(FIRE_OPTION_2); },
          .is_visible = IsPsychoMode,
      },
      slider("dechroma", &shader_injection.dechroma, "Dechroma", "PsychoV31",
             "Very bright colors fade toward white, as if overexposed. 0 = off.", IsPsychoMode, IsPsychoMode, 0.f, 1.f),
      slider("highlight_saturation", &shader_injection.highlight_saturation, "Highlight Saturation", "PsychoV31",
             "Saturation of bright colors. 0 = less, 1 = PsychoV31's own (default), 2 = more.", IsPsychoMode,
             IsPsychoMode),
      slider("highlight_hue_shift", &shader_injection.highlight_hue_shift, "Highlight Hue Shift", "PsychoV31",
             "Bright colors take the game's SDR hues: fire more orange / yellow, less pink.\n"
             "0 = off, 1 = SDR hues.",
             IsPsychoMode, IsPsychoMode, 0.03f, 1.f),
      slider("highlight_saturation_shift", &shader_injection.highlight_saturation_shift, "Highlight Saturation Shift",
             "PsychoV31",
             "Gives bright colors the saturation of the game's SDR look (fire cores less pale and pink).\n"
             "0 = off, 1 = SDR's.",
             IsPsychoMode, IsPsychoMode, 0.1f, 1.f),
      new renodx::utils::settings::Setting{
          .key = "fire_hue",
          .binding = &shader_injection.fire_hue,
          .default_value = 0.f,
          .label = "Fire Hue (OkLCh)",
          .section = "PsychoV31",
          .tooltip = "Rotates the OkLCh hue of bright warm colors (fire): below 0 redder, above 0 more yellow.\n"
                     "0 = off.",
          .min = -1.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = IsPsychoMode,
          .is_visible = IsPsychoMode,
      },
      slider("fire_hue_focus", &shader_injection.fire_hue_focus, "Fire Hue Focus (OkLCh)", "PsychoV31",
             "Pulls pink and yellow fire toward orange (OkLCh hue). 0 = off, 1 = all orange.", IsPsychoMode,
             IsPsychoMode, 0.5f, 1.f),

      // Extra (HDR only) /////////////////////////////////////////////////////////////////////////
      slider("bloom", &shader_injection.bloom, "Bloom", "Extra", "Bloom multiplier.", IsHDRMode, IsAdvancedSettings),
      slider("vignette", &shader_injection.vignette, "Vignette", "Extra", "Vignette multiplier.", IsHDRMode,
             IsAdvancedSettings),
      slider("sun_shafts", &shader_injection.sun_shafts, "Sun Shafts", "Extra", "Sun shaft (godray) multiplier.",
             IsHDRMode, IsAdvancedSettings),
      slider("lens_flare", &shader_injection.lens_flare, "Lens Flares", "Extra", "Lens flare multiplier.", IsHDRMode,
             IsAdvancedSettings),
      slider("chromatic_aberration_strength", &shader_injection.chromatic_aberration, "Chromatic Aberration", "Extra",
             "The game's own full-screen chromatic aberration (red and blue color fringes toward the\n"
             "screen edges). The game ships with it off (0).\n"
             "1 = twice the strength the game gives its lens flares' color shift.",
             IsHDRMode, IsAdvancedSettings, 0.f),
      slider("film_grain", &shader_injection.film_grain, "Film Grain", "Extra", "Film grain multiplier.", IsHDRMode,
             IsAdvancedSettings),
      slider("sharpening", &shader_injection.sharpening, "Sharpening", "Extra",
             "Multiplier on the game's sharpening (after TAA).\n0 = off (default), 1 = the game's own.", IsHDRMode,
             IsAdvancedSettings, 0.f),
      slider("motion_blur_camera", &shader_injection.motion_blur_camera, "Motion Blur (Camera)", "Extra",
             "Blur from camera movement. 0 = off.\nThe game's maximum blur length still applies.", IsHDRMode,
             IsAdvancedSettings),
      slider("motion_blur_object", &shader_injection.motion_blur_object, "Motion Blur (Objects)", "Extra",
             "Blur from objects' own movement (characters, weapons). 0 = off.\n"
             "The game's maximum blur length still applies.",
             IsHDRMode, IsAdvancedSettings),
      slider("depth_of_field", &shader_injection.depth_of_field, "Depth of Field", "Extra",
             "Depth of field blur multiplier. 0 = off.", IsHDRMode, IsAdvancedSettings),
      new renodx::utils::settings::Setting{
          .key = "video_highlights",
          .binding = &shader_injection.video_highlights,
          .default_value = 0.f,
          .label = "Video AutoHDR",
          .section = "Extra",
          .tooltip = "Makes the bright parts of the game's (SDR) videos brighter: only what is above mid gray,\n"
                     "the brightest the most. 0 = off, 1 = video white reaches Peak.\n"
                     "High values can show banding (see Video Dithering).",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = [] { return IsHDRMode() && IsHdrOutput(); },
          .is_visible = IsAdvancedSettings,
      },
      new renodx::utils::settings::Setting{
          .key = "video_dithering",
          .binding = &shader_injection.video_dithering,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Video Dithering",
          .section = "Extra",
          .tooltip = "Fine noise that hides banding in the brightened videos.\n"
                     "Only with Video AutoHDR above 0.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return IsHDRMode() && IsHdrOutput() && shader_injection.video_highlights > 0.f; },
          .is_visible = IsAdvancedSettings,
      },
      // Ambient Occlusion (ssdo00): the game's SSDO or GT-VBAO in its place (ssdo_bitmask.hlsli).
      new renodx::utils::settings::Setting{
          .key = "ssao_mode",
          .binding = &shader_injection.ssao_mode,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ambient Occlusion",
          .section = "Ambient Occlusion",
          .tooltip = "GT-VBAO: contact shading that lets light pass behind thin things (railings, spears, grass)\n"
                     "instead of the game's dark halos. Experimental. Ray-traced AO and Path-Traced Ambient replace it.",
          .labels = {"Game's (SSDO)", "GT-VBAO"},
          .is_enabled = [] { return !(ray_tracing_installed && (rt_sliders.ambient_occlusion != 0.f || PathAmbientWanted())); },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_radius",
          .binding = &shader_injection.ssao_radius,
          .default_value = 1.f,
          .label = "AO Radius",
          .section = "Ambient Occlusion",
          .tooltip = "How far around a point other surfaces shade it (metres).",
          .min = 0.25f,
          .max = 4.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.ao_method == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_strength",
          .binding = &shader_injection.ssao_strength,
          .default_value = 1.f,
          .label = "AO Strength",
          .section = "Ambient Occlusion",
          .tooltip = "How dark the shading gets.",
          .min = 0.f,
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = [] { return shader_injection.ao_method == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_thickness",
          .binding = &shader_injection.ssao_thickness,
          .default_value = 0.1f,
          .label = "AO Thickness",
          .section = "Ambient Occlusion",
          .tooltip = "How thick things are taken to be: thicker shades more behind them (metres).",
          .min = 0.01f,
          .max = 1.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.ao_method == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_quality",
          .binding = &shader_injection.ssao_quality,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "AO Quality",
          .section = "Ambient Occlusion",
          .tooltip = "Samples per side: more catch thin things. Heavier.",
          .labels = {"8", "16", "32", "64"},
          .is_enabled = [] { return shader_injection.ao_method == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_directions",
          .binding = &shader_injection.ssao_directions,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "AO Directions",
          .section = "Ambient Occlusion",
          .tooltip = "Directions per pixel: less noise. Heavier.",
          .labels = {"1", "2", "4", "8"},
          .is_enabled = [] { return shader_injection.ao_method == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_auto_thickness",
          .binding = &shader_injection.ssao_auto_thickness,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "AO Auto Thickness",
          .section = "Ambient Occlusion",
          .tooltip = "Guesses each object's thickness from its width, AO Thickness as the least:\n"
                     "spears stay thin, walls and pillars block fully.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return shader_injection.ao_method == 1.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "ssao_exact_depth",
          .binding = &shader_injection.ssao_exact_depth,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "AO Exact Depth Samples (Experimental)",
          .section = "Ambient Occlusion",
          .tooltip = "Each sample takes the depth at its own spot between pixels:\n"
                     "open ground seen at a low angle no longer shades itself. Off: the nearest pixel's depth.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return shader_injection.ao_method == 1.f; },
      },
      // Contact Shadows (contact_shadows.hpp): short screen-space shadows toward the sun, into its shadow mask.
      new renodx::utils::settings::Setting{
          .key = "contact_shadows",
          .binding = &shader_injection.contact_shadows,
          .default_value = 0.f,
          .label = "Contact Shadows",
          .section = "Contact Shadows",
          .tooltip = "Small sharp sun shadows where the game's shadow map is too coarse (feet, folds of armour,\n"
                     "stones on the ground). 0 = off. Experimental.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
      },
      new renodx::utils::settings::Setting{
          .key = "contact_shadow_length",
          .binding = &shader_injection.contact_shadow_length,
          .default_value = 0.5f,
          .label = "Contact Shadow Length",
          .section = "Contact Shadows",
          .tooltip = "How far toward the sun each pixel looks for something in the way (metres).",
          .min = 0.1f,
          .max = 2.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.contact_shadows > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "contact_shadow_thickness",
          .binding = &shader_injection.contact_shadow_thickness,
          .default_value = 0.2f,
          .label = "Contact Shadow Thickness",
          .section = "Contact Shadows",
          .tooltip = "How thick things are taken to be: thicker casts more, thinner leaves gaps behind edges (metres).",
          .min = 0.05f,
          .max = 1.f,
          .format = "%.2f m",
          .is_enabled = [] { return shader_injection.contact_shadows > 0.f; },
      },
      // Sun Shadow Softness (sunshadow00-04, sun_shadow_pcss.hlsli): contact-hardening sun shadows (PCSS).
      new renodx::utils::settings::Setting{
          .key = "sun_shadow_softness",
          .binding = &shader_injection.sun_shadow_softness,
          .default_value = 0.f,
          .label = "Sun Shadow Softness",
          .section = "Contact Shadows",
          .tooltip = "Sun shadows sharp where things touch and softer farther away (the sun's size in degrees;\n"
                     "the real sun 0.5). 0 = the game's. Experimental.",
          .min = 0.f,
          .max = 5.f,
          .format = "%.2f",
      },
      // Light Bounce (light_bounce.hpp): GT-VBGI after the tiled lighting, added to the scene it lit.
      new renodx::utils::settings::Setting{
          .key = "light_bounce",
          .binding = &shader_injection.light_bounce,
          .default_value = 0.f,
          .label = "Light Bounce",
          .section = "Light Bounce",
          .tooltip = "Light bouncing off what you see onto things around it (colour bleeding, brighter corners).\n"
                     "0 = off. Experimental.",
          .min = 0.f,
          .max = 2.f,
          .format = "%.2f",
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_radius",
          .binding = &shader_injection.light_bounce_radius,
          .default_value = 3.f,
          .label = "Light Bounce Radius",
          .section = "Light Bounce",
          .tooltip = "How far light travels to bounce (metres).",
          .min = 0.5f,
          .max = 10.f,
          .format = "%.1f m",
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_quality",
          .binding = &shader_injection.light_bounce_quality,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Light Bounce Quality",
          .section = "Light Bounce",
          .tooltip = "Samples per direction: finer detail. Heavier.",
          .labels = {"8", "16", "32"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_directions",
          .binding = &shader_injection.light_bounce_directions,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Light Bounce Directions",
          .section = "Light Bounce",
          .tooltip = "Directions per pixel: less noise. Heavier.",
          .labels = {"1", "2", "4", "8"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_resolution",
          .binding = &shader_injection.light_bounce_resolution,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Light Bounce Resolution",
          .section = "Light Bounce",
          .tooltip = "Traced at full, half or quarter resolution (lower is faster, softer).",
          .labels = {"Full", "Half", "Quarter"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_multi",
          .binding = &shader_injection.light_bounce_multi,
          .default_value = 0.f,
          .label = "Multi-Bounce",
          .section = "Light Bounce",
          .tooltip = "Bounced light bouncing again (brighter, softer interiors). 0 = one bounce.",
          .min = 0.f,
          .max = 1.f,
          .format = "%.2f",
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_denoise",
          .binding = &shader_injection.light_bounce_denoise,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Light Bounce Denoiser",
          .section = "Light Bounce",
          .tooltip = "Smooths the bounce along surfaces (less noise, never across edges).",
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_temporal",
          .binding = &shader_injection.light_bounce_temporal,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Light Bounce Temporal Smoothing",
          .section = "Light Bounce",
          .tooltip = "Averages the last frames: much less noise. Off: the noise stands still.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_checkerboard",
          .binding = &shader_injection.light_bounce_checkerboard,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Light Bounce Checkerboard (Experimental)",
          .section = "Light Bounce",
          .tooltip = "Half the screen traced each frame, the rest kept from the last frames.\n"
                     "About 2x faster; can lag a little behind fast motion. Not with the ray-traced Light Bounce.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f && shader_injection.light_bounce_temporal != 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_backface",
          .binding = &shader_injection.light_bounce_backface,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Light Bounce Back-Face Rejection (Experimental)",
          .section = "Light Bounce",
          .tooltip = "Ignores light from surfaces seen from behind. Heavier; not always better.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      new renodx::utils::settings::Setting{
          .key = "light_bounce_auto_thickness",
          .binding = &shader_injection.light_bounce_auto_thickness,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Light Bounce Auto Thickness",
          .section = "Light Bounce",
          .tooltip = "Guesses each object's thickness from its width, AO Thickness as the least:\n"
                     "spears stay thin, walls and pillars block fully.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return shader_injection.light_bounce > 0.f; },
      },
      // Ray tracing (ray_tracing.hpp): through renodx-tw-dlaa64.exe; on at launch only (restart).
      Note(FULL_DOWNLOAD_NOTE, "Ray Tracing (Experimental)", WARNING_TINT, HelperAbsent),
      ray_button("RT/PT",
                 "Every ray-traced effect and path tracing on, the rest of this section at its default (restart the\n"
                 "game). Turns the Light Bounce on, and Ray Reconstruction where DLSS runs without it.",
                 [] {
                   renodx::utils::settings::UpdateSettings(RaysReset());
                   renodx::utils::settings::UpdateSettings(twattila::graphics_page::RAYS_PRESET);
                   renodx::utils::settings::UpdateSettings(RAYS_OWN_KEYS);
                   // Without Ray Reconstruction the rays have only the built-in denoisers (Attila's preset: F).
                   if (twattila::dlaa_bridge::OnNvidia() && rysesonofrome::graphics::ray_reconstruction.load() == 0) {
                     rysesonofrome::graphics::ray_reconstruction.store(4);
                     reshade::set_config_value(nullptr, "Ryse AA", "RayReconstruction", 4);
                   }
                 }),
      // Attila's and Tales' No RT/PT: the screen-space methods in the rays' place (ATN 2026-11-16).
      ray_button("No RT/PT (Recommended)",
                 "Every ray-traced effect off, their sliders kept (restart the game); in their place GT-VBAO,\n"
                 "the Light Bounce and the Contact Shadows on.",
                 [] {
                   renodx::utils::settings::UpdateSettings(RAYS_OFF);
                   renodx::utils::settings::UpdateSettings({{"ssao_mode", 1.f}, {"light_bounce", 1.f}, {"contact_shadows", 1.f}});
                 }),
      ray_button("Reset", "Every ray-traced effect off, this section's sliders at their defaults (restart the game).", [] {
        renodx::utils::settings::UpdateSettings(RAYS_OFF);
        renodx::utils::settings::UpdateSettings(RaysReset());
      }),
      new renodx::utils::settings::Setting{
          .key = "RayTracedSunShadows",
          .binding = &rt_sliders.sun_shadows,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Sun Shadows (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Sun shadows from rays in place of the game's shadow maps.\n"
                     "Needs an RTX GPU and renodx-tw-dlaa64.exe next to the addon. Restart the game after turning on.",
          .labels = {"Off", "On"},
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedShadowSoftness",
          .binding = &rt_sliders.softness,
          .default_value = 0.5f,
          .label = "Ray-Traced Shadow Softness",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "The sun's size in degrees: shadows softer away from what casts them. 0: sharp.",
          .min = 0.f,
          .max = 3.f,
          .format = "%.2f",
          .is_enabled = [] { return rt_sliders.sun_shadows != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      // contact_shadows.hpp's replaces_sun (test/tw_ryse_sun_replace): off until an in-game coverage run shows the
      // rays keep the casters out of view (the rays see the G-buffer's draws only); off, the darker of the two.
      new renodx::utils::settings::Setting{
          .key = "RayTracedReplaceShadowMaps",
          .binding = &rt_sliders.replace_sun_maps,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Rays Replace Shadow Maps",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "On: the sun's shadow from the rays alone. Off: the darker of the rays' and the game's.\n"
                     "On, what is out of view casts no sun shadow, unless Shadow Casters Out of View is on.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return rt_sliders.sun_shadows != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      // ray_tracing.hpp SceneDraw: the sun's casters from the game's own maps, every frame (the Total War games' key).
      // Experimental until an in-game run measures its cost and shows the coverage; then Rays Replace Shadow Maps on.
      new renodx::utils::settings::Setting{
          .key = "RayTracedUnitsOutOfView",
          .binding = &rt_sliders.casters_out_of_view,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Shadow Casters Out of View (Experimental)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Rays' sun shadows: what is behind the camera or off screen keeps its shadow,\n"
                     "as far as the game's sun shadows reach. Costs GPU time.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return rt_sliders.sun_shadows != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      // shadow_skip.hpp: acts live, with the ray tracing on at launch.
      new renodx::utils::settings::Setting{
          .key = "RayTracedSkipSunShadowMaps",
          .binding = &rt_sliders.skip_sun_maps,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Skip the Game's Sun Shadow Maps (Experimental)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "The game stops drawing its sun shadow maps: the rays alone shadow the sun. Faster.\n"
                     "Past the Near Ones: hair and fog keep the game's shadows near you.\n"
                     "All: hair strands and fog lose the sun's shadow; out of view only Shadow Casters Out of View casts one.",
          .labels = {"Off", "Past the Near Ones", "All"},
          .is_enabled = [] { return rt_sliders.sun_shadows != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAmbientOcclusion",
          .binding = &rt_sliders.ambient_occlusion,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Ambient Occlusion (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Shade in corners and under things from rays, in place of the game's.\n"
                     "Restart the game after turning on.",
          .labels = {"Off", "On"},
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAmbientRange",
          .binding = &rt_sliders.ambient_range,
          .default_value = 2.f,
          .label = "Ray-Traced AO Range",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "How far its rays look for what shades a point.",
          .min = 0.25f,
          .max = 5.f,
          .format = "%.2f m",
          .is_enabled = [] { return rt_sliders.ambient_occlusion != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAOQuarter",
          .binding = &rt_sliders.ambient_quarter,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced AO Rate",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Quarter: one pixel of each 2 x 2 traced a frame, in turn (DLSS smooths it). Faster.",
          .labels = {"Every pixel", "Quarter"},
          .is_enabled = [] { return rt_sliders.ambient_occlusion != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedAORays",
          .binding = &rt_sliders.ambient_rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 2.f,
          .label = "Ray-Traced AO Rays",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "More rays: less noise. Heavier.",
          .min = 1.f,
          .max = 8.f,
          .format = "%d",
          .is_enabled = [] { return rt_sliders.ambient_occlusion != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedReflections",
          .binding = &rt_sliders.reflections,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Reflections (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Reflections from rays in place of the screen-space ones: also what is off screen.\n"
                     "Restart the game after turning on.",
          .labels = {"Off", "On"},
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedReflectionSamples",
          .binding = &rt_sliders.reflection_samples,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Ray-Traced Reflection Samples",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Rays per pixel on rough surfaces: less noise, heavier.",
          .min = 1.f,
          .max = 8.f,
          .format = "%d",
          .is_enabled = [] { return rt_sliders.reflections != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedReflectionReuse",
          .binding = &rt_sliders.reflection_reuse,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Reflection Reuse (Experimental)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Less noise on rough reflections.\n"
                     "From ReSTIR GI: also the path-traced Light Bounce's samples (needs Path Tracing).\n"
                     "Own ReSTIR: their own samples, reused over frames and between neighbours.",
          .labels = {"Off", "From ReSTIR GI", "Own ReSTIR"},
          .is_enabled = [] { return rt_sliders.reflections != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedSpecularOcclusion",
          .binding = &rt_sliders.specular_occlusion,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Specular Occlusion (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "The sky's and the probes' reflection taken off shiny surfaces where something stands over them\n"
                     "(doorways, under roofs). With the screen-space reflections; the ray-traced ones have it already.\n"
                     "Works with another ray-traced effect on. Restart the game after turning on.",
          .labels = {"Off", "On"},
          .is_enabled = RaysBesideSpecularOcclusion,
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounce",
          .binding = &rt_sliders.light_bounce,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Ray-Traced Light Bounce (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "The Light Bounce's light found with rays: no guessing behind what you see, light from farther.\n"
                     "Needs the Light Bounce on. Restart the game after turning on.",
          .labels = {"Off", "On"},
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceRays",
          .binding = &rt_sliders.rays,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = RtPtValue("RayTracedLightBounceRays"),
          .label = "Ray-Traced Light Bounce Rays",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Rays per texel: less noise, heavier.",
          .min = 1.f,
          .max = 16.f,
          .format = "%d",
          .is_enabled = [] { return rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceRange",
          .binding = &rt_sliders.range,
          .default_value = RtPtValue("RayTracedLightBounceRange"),
          .label = "Ray-Traced Light Bounce Range",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "How far its rays look for light.",
          .min = 1.f,
          .max = 50.f,
          .format = "%.0f m",
          .is_enabled = [] { return rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "RayTracedLightBounceResolution",
          .binding = &rt_sliders.light_bounce_resolution,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = RtPtValue("RayTracedLightBounceResolution"),
          .label = "Ray-Traced Light Bounce Resolution",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Its own resolution (the screen-space Light Bounce's resolution and Checkerboard no longer apply to it).\n"
                     "Full: sharpest, about 4x Half's cost.",
          .labels = {"Full", "Half", "Quarter"},
          .is_enabled = [] { return rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "PathTracing",
          .binding = &rt_sliders.path_tracing,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path Tracing (restart)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "The Light Bounce's and the reflections' rays go on bouncing: lit by what the screen never saw.\n"
                     "Needs the Light Bounce or the reflections on. Heavy. Restart the game after turning on.",
          .labels = {"Off", "On"},
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      new renodx::utils::settings::Setting{
          .key = "PathTracingBounces",
          .binding = &rt_sliders.bounces,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = RtPtValue("PathTracingBounces"),
          .label = "Path Tracing Bounces",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Bounces past the first. More: brighter shade, heavier.",
          .min = 1.f,
          .max = 4.f,
          .format = "%d",
          .is_enabled = [] { return rt_sliders.path_tracing != 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      // Path-Traced Ambient (ray_tracing.hpp; Attila's keys): live.
      new renodx::utils::settings::Setting{
          .key = "RayTracedPathAmbient",
          .binding = &rt_sliders.path_ambient,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 0.f,
          .label = "Path-Traced Ambient (Experimental)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "With Path Tracing: the paths bring the sky's light too, in place of the game's ambient light and the\n"
                     "ray-traced AO. Darker where little sky reaches. Needs the Light Bounce on.",
          .labels = {"Off", "On"},
          .is_enabled = [] { return rt_sliders.path_tracing != 0.f && shader_injection.light_bounce > 0.f; },
          .is_visible = twattila::dlaa_bridge::HelperInstalled,
      },
      ray_choice("RayTracedPathSkyApart", &rt_sliders.path_sky_apart, "Path-Traced AO: Own Sky Rays", off_on,
                 "The sky's light averaged from each pixel's own rays, as in Ray-Traced AO:\n"
                 "less noise, and more rays help. Off: through ReSTIR GI.",
                 PathAmbientWanted),
      ray_choice("RayTracedPathKeptCube", &rt_sliders.path_kept_cube, "Path-Traced AO: Steady Bounce Light (Experimental)", off_on,
                 "Light bouncing off walls and ground follows the game's sky, not the view Path-Traced AO started in.",
                 PathAmbientWanted),
      // Attila's (twattila/graphics_page.hpp's keys), with a ray-traced effect on.
      ray_choice("RayTracedSharc", &rt_sliders.sharc, "SHaRC radiance cache (Experimental)", off_on,
                 "NVIDIA's SHaRC for the light of what the screen does not show (more bounces off-screen, paths end sooner).", paths),
      ray_choice("RayTracedReuseLean", &rt_sliders.reuse_lean, "Lean ReSTIR reuse", off_on,
                 "A faster ReSTIR GI for path tracing, with the same look.", paths),
      ray_choice("RayTracedReuseNear", &rt_sliders.reuse_near, "Near ReSTIR reuse", off_on,
                 "A faster ReSTIR GI for path tracing: fewer, nearer neighbours share their light (also a little cleaner).", paths),
      ray_choice("RayTracedPathSer", &rt_sliders.path_ser, "Path tracing SER (Experimental)", off_on,
                 "Shader execution reordering for path tracing and ray-traced reflections (DXR 1.2, RTX 40 / 50 series): may be faster.",
                 [] { return rt_sliders.light_bounce != 0.f || rt_sliders.reflections != 0.f; }),
      // rt_core's units_range: the skinned characters' compact draws (ray_tracing.hpp's dynamic scene draws) past it out.
      new renodx::utils::settings::Setting{
          .key = "RayTracedUnitsRange",
          .binding = &rt_sliders.units_range,
          .default_value = 0.f,
          .label = "Characters Ray Tracing Range (Experimental)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "Characters farther than this are left out of the ray tracing (their shadows there are a few pixels):\n"
                     "faster in big battles. 0 m: all characters.",
          .min = 0.f,
          .max = 500.f,
          .format = "%.0f m",
          .is_enabled = [] { return ray_tracing_installed; },
          .is_visible = [] { return twattila::dlaa_bridge::HelperInstalled() && AnyRays(); },
      },
      ray_choice("RayTracingDenoiser", &rt_sliders.denoiser, "Ray tracing denoiser (Experimental)", {"Built-in", "NRD"},
                 "Cleans the ray-traced shadows, AO, Light Bounce and reflections (with an AA mode on, without Ray Reconstruction).\n"
                 "Built-in: the mod's own filters. NRD: NVIDIA's denoisers, steadier, uses more memory."),
      ray_choice("RayTracedAoReblur", &rt_sliders.ao_reblur, "AO under Ray Reconstruction", {"Light History", "REBLUR"},
                 "Light History: the AO keeps a little of its last frames, Ray Reconstruction cleans the rest.\n"
                 "REBLUR: NVIDIA's denoiser cleans it first: steadier when moving, uses more memory, can darken corners.",
                 [] {
                   return rt_sliders.ambient_occlusion != 0.f && rysesonofrome::graphics::ray_reconstruction.load() != 0
                          && rysesonofrome::graphics::requested_mode.load() != rysesonofrome::graphics::AAMode::OFF;
                 }),
      ray_choice("RayTracingOutputResolution", &rt_sliders.output_rays, "Rays a pixel (Experimental)",
                 {"1", "Match Output", "2", "3", "4", "5", "6", "7", "8"},
                 "More rays for every rendered pixel, each from its own spot of it: less noise, finer shadow edges.\n"
                 "The cost grows with the rays. Match Output: as many rays on screen as at 100% (up to 8 a pixel)."),
      ray_choice("RayTracingRaysShadows", &rt_sliders.rays_shadows, "Rays a pixel: shadows", off_on,
                 "The sun's shadows take the extra rays.", extra_rays),
      ray_choice("RayTracingRaysAmbient", &rt_sliders.rays_ambient, "Rays a pixel: ray-traced AO", off_on,
                 "Ray-traced AO takes the extra rays.", extra_rays),
      ray_choice("RayTracingRaysReflections", &rt_sliders.rays_reflections, "Rays a pixel: reflections", off_on,
                 "Ray-traced reflections take the extra rays.", extra_rays),
      ray_choice("RayTracingRaysPaths", &rt_sliders.rays_paths, "Rays a pixel: Light Bounce", off_on,
                 "The ray-traced and path-traced Light Bounce take the extra rays (up to 16 a texel). The heaviest.", extra_rays),
      ray_choice("RayTracedDebugLog", &rt_sliders.debug_log, "Log details", off_on,
                 "Ray tracing's numbers in the log every 10 s, for bug reports. Off: nothing of this runs."),
      // Drawn over the tone mapper's output (tonemap00's on_drawn, with the ray tracing installed); rt_core's values of the
      // effects this host makes (its last label All, 9).
      new renodx::utils::settings::Setting{
          .key = "RayTracedDebugView",
          .binding = &rt_sliders.debug_view,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 0.f,
          .label = "Ray tracing debug view",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "A ray-traced effect's raw result as the picture (All: every one tiled), to check they work.\n"
                     "Magenta: it made nothing (off or not traced).",
          .labels = {"Off", "Sun Shadows", "Ambient Occlusion", "Light Bounce", "Reflections", "Specular Occlusion", "All"},
          .is_enabled = [] { return ray_tracing_installed; },
          .parse = [](float value) { return (value == 6.f ? 9.f : value); },
          .is_visible = [] { return twattila::dlaa_bridge::HelperInstalled() && AnyRays(); },
      },
      // Ryse's parts of rt_core's scene tests (SCENE_TESTS): no draw is marked terrain (its Terrain Only would be empty),
      // no placed rows (Objects and Trees Only: what is neither terrain nor skinned), no leaves apart from alpha tests.
      ray_choice("RayTracedSceneTest", &rt_sliders.scene_test, "Ray tracing scene (test)",
                 {"Everything", "Static Only (no characters)", "No Alpha-Tested", "Nothing Kept Off-Screen"},
                 "While the Debug View is on: one part of the scene alone, to find what breaks an effect.",
                 [] { return ray_tracing_installed && rt_sliders.debug_view != 0.f; }),
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Record ray tracing profile (30 s)",
          .section = "Ray Tracing (Experimental)",
          .tooltip = "30 s of every frame's ray tracing costs into tw_rt_profile.csv and tw_rt_profile_helper.csv\n"
                     "(next to the game's exe). Turn the effects on, press, then move the camera around.",
          .on_change = [] {
            if (ray_tracing_installed) twattila::rt_scene::StartProfile();
          },
          .is_visible = [] { return twattila::dlaa_bridge::HelperInstalled() && AnyRays(); },
      },
  };
  auto v35_rows = twattila::psycho35::Rows(&shader_injection, IsPsychoV35Mode, HUE_CLIP_DEFAULT);
  for (auto* row : v35_rows) {
    for (const auto& [key, value] : V35_GAME_RESPONSE) {
      if (row->key == key) {
        row->default_value = value;
      }
    }
  }
  twattila::psycho35::InsertBefore(&settings, "PsychoV31", v35_rows);
}

// The execution skull above enemies: the largest 0x1B59F534 draw, missing from frames without it (F9 logs, 4K).
// Scaleform triangulates it for its size on screen: 3744 indices far (scale 2.07e-6, alpha 0.5) to 4401-4539 near
// (scale 4.2e-6), about size^0.23. The other HUD shapes stay at or below 3219 at 4K. The cut between, 3500 at 2160
// lines, follows the same law with the back buffer height (lower resolutions: fewer triangles everywhere).
uint32_t execution_skull_min_indices = 3500;  // render thread

#ifdef RYSE_UI_DRAW_LOG
// F9 appends the next frame's 0x1B59F534 draws (indices, VS cCompositeMat, PS cBitmapColorTransform) to
// ryse_ui_draws.txt next to the exe (ReShade.log is replaced at every start). Render thread only.
std::FILE* ui_log = nullptr;
uint32_t logged_ui_draws = 0;
#endif

// The execution highlights on enemies (yellow / blue): every pixel shader with VisionMtlParams in its constant
// buffers (seen: body 0xD2063417, head 0xF495DD17, 0x6B40240A), found as the game creates them, so the material
// permutations not seen yet are covered too. Added on any thread, read on the render thread.
renodx::utils::data::ParallelFlatHashMap<uint32_t, bool, std::shared_mutex> highlight_shaders;

void OnInitPipelineHighlights(reshade::api::device*, reshade::api::pipeline_layout, uint32_t subobject_count,
                              const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline) {
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
    const auto* shader = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
    if (shader == nullptr || shader->code == nullptr) continue;
    if (std::string_view(static_cast<const char*>(shader->code), shader->code_size).find("VisionMtlParams") == std::string_view::npos) {
      continue;
    }
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(static_cast<const uint8_t*>(shader->code), shader->code_size);
    if (highlight_shaders.insert({hash, true}).second) {
      reshade::log::message(reshade::log::level::info, std::format("[Ryse UI] Execution highlight shader 0x{:08X}", hash).c_str());
    }
  }
}

bool IsHiddenHighlight(reshade::api::command_list* cmd_list) {
  return execution_highlights_hidden
         && highlight_shaders.contains(renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader));
}

// Scaleform's flat shapes (0x1B59F534) while the HUD is hidden (the skull, indexed, by its own hotkey), the execution
// highlights while they are: left out through command_action's bypass, not a draw event's skip (a draw that a
// command_action callback gives a post callback is drawn by command_action itself, past any later draw event's skip; the
// rays' capture runs before the draw since 300fd8323). Gated on the hotkeys' state.
constexpr auto OnDrawUi = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  auto* cmd_list = command.cmd_list;
  if (IsHiddenHighlight(cmd_list)) return {.bypass = true};
  bool skip = hud_hidden.load();
  if constexpr (std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    skip = (command.arguments.index_count >= execution_skull_min_indices ? execution_skull_hidden.load() : hud_hidden.load());
  }
#ifdef RYSE_UI_DRAW_LOG
  if (ui_log == nullptr && !skip) return {};
#else
  if (!skip) return {};
#endif
  if (renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader) != 0x1B59F534) {
    return {};
  }
#ifdef RYSE_UI_DRAW_LOG
  if constexpr (std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    if (ui_log != nullptr) {
      const auto& [index_count, instance_count, first_index, vertex_offset, first_instance] = command.arguments;
      // b0 of both stages read back (stalls, one frame): VS cCompositeMat (4x4), PS cBitmapColorTransform (multiply,
      // add) and cPremultiplyAlpha.
      auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
      ID3D11DeviceContext1* context1 = nullptr;
      std::array<float, 16> vertex_constants = {};
      std::array<float, 12> pixel_constants = {};
      const auto read_constants = [&](bool vertex_stage, std::span<float> values) {
        ID3D11Buffer* buffer = nullptr;
        UINT first_constant = 0;
        UINT constant_count = 0;
        if (vertex_stage) {
          context1->VSGetConstantBuffers1(0, 1, &buffer, &first_constant, &constant_count);
        } else {
          context1->PSGetConstantBuffers1(0, 1, &buffer, &first_constant, &constant_count);
        }
        if (buffer == nullptr) return;
        D3D11_BUFFER_DESC desc = {};
        buffer->GetDesc(&desc);
        ID3D11Device* device = nullptr;
        buffer->GetDevice(&device);
        const D3D11_BUFFER_DESC staging_desc = {
            .ByteWidth = desc.ByteWidth,
            .Usage = D3D11_USAGE_STAGING,
            .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
        };
        ID3D11Buffer* staging = nullptr;
        if (SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, &staging))) {
          context->CopyResource(staging, buffer);
          D3D11_MAPPED_SUBRESOURCE mapped = {};
          const size_t offset = size_t{first_constant} * 16;
          if (offset < desc.ByteWidth && SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            std::memcpy(values.data(), static_cast<const std::byte*>(mapped.pData) + offset,
                        std::min(values.size_bytes(), desc.ByteWidth - offset));
            context->Unmap(staging, 0);
          }
          staging->Release();
        }
        device->Release();
        buffer->Release();
      };
      if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {
        read_constants(true, vertex_constants);
        read_constants(false, pixel_constants);
        context1->Release();
      }
      const auto& m = vertex_constants;
      const auto& p = pixel_constants;
      std::fprintf(ui_log,
                   "%u: indices %u, instances %u, first index %u, base vertex %d | mat %g %g %g %g / %g %g %g %g / "
                   "%g %g %g %g / %g %g %g %g | mul %g %g %g %g add %g %g %g %g pm %g\n",
                   logged_ui_draws++, index_count, instance_count, first_index, vertex_offset, m[0], m[1], m[2], m[3],
                   m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15], p[0], p[1], p[2], p[3],
                   p[4], p[5], p[6], p[7], p[8]);
    }
  }
#endif
  if (!skip) return {};
  return {.bypass = true};
};

// Polled at Present, while the game window is in front.
std::array<bool, 256> keys_down = {};

// True at the Present where `key` went down.
bool KeyWentDown(int key) {
  const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
  const bool went_down = down && !keys_down[key];
  keys_down[key] = down;
  return went_down;
}

void OnPresentHotkeys(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  // GT-VBAO's noise moves every frame (the game's SSDO blur and TAA / DLSS settle it).
  shader_injection.ssao_frame = static_cast<float>((static_cast<uint32_t>(shader_injection.ssao_frame) + 1u) % 4096u);
  // The next frame's one AO method (ours replaces the game's; a saved GT-VBAO with a ray-traced one runs the rays alone):
  // Path-Traced Ambient, else the ray-traced AO (the game's SSDO amount left out once the rays' went into a lighting,
  // the game's SSDO back after a frame without them), else the row's.
  if (ray_tracing_installed && PathAmbientWanted()) {
    shader_injection.ao_method = 3.f;
  } else if (ray_tracing_installed && rt_sliders.ambient_occlusion != 0.f) {
    shader_injection.ao_method = (rays_ambient_applied.exchange(false, std::memory_order_relaxed) ? 2.f : 0.f);
  } else {
    shader_injection.ao_method = shader_injection.ssao_mode;
  }
  // The Light Bounce in place of the game's indirect light (graphics.hpp's r_SSII row) while it runs.
  rysesonofrome::graphics::own_light_bounce.store(shader_injection.light_bounce > 0.f, std::memory_order_relaxed);
  // What the helper serves this frame (graphics.hpp adds the upscaler and frame generation): the ray tracing, its guides
  // with the reflections or paths, Ray Reconstruction while a ray-traced effect is on.
  const auto back_buffer = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
  rysesonofrome::upscaling::BridgeUse use;
  if (ray_tracing_installed) {
    const bool bounce = shader_injection.light_bounce > 0.f && (rt_sliders.light_bounce != 0.f || rt_sliders.path_tracing != 0.f);
    const bool reflections = rt_sliders.reflections != 0.f;
    const bool path_tracing = (bounce || reflections) && rt_sliders.path_tracing != 0.f;
    const bool other_rays = RaysBesideSpecularOcclusion();
    const bool specular_occlusion = rt_sliders.specular_occlusion != 0.f && other_rays;
    // The specular occlusion comes with the reflections' trace (their textures and guides).
    use = {
        .ray_tracing = true,
        .guides = reflections || specular_occlusion || path_tracing,
        .reflections = reflections || specular_occlusion,
        .ray_reconstruction = other_rays ? 1u : 0u,
    };
    const bool path_ambient = path_tracing && bounce && PathAmbientWanted();
    twattila::rt_scene::Settings rays = {
        .sun_shadows = rt_sliders.sun_shadows != 0.f,
        .ambient_occlusion = rt_sliders.ambient_occlusion != 0.f && !path_ambient,
        .ambient_range = rt_sliders.ambient_range,
        .ambient_rays = static_cast<uint32_t>(std::clamp(rt_sliders.ambient_rays, 1.f, 8.f)),
        .ambient_quarter = rt_sliders.ambient_quarter != 0.f,
        .sun_softness = rt_sliders.softness,
        .light_bounce = bounce,
        .light_bounce_rays = static_cast<uint32_t>(rt_sliders.rays),
        .light_bounce_range = rt_sliders.range,
        .light_bounce_resolution = rt_sliders.light_bounce_resolution,
        .path_tracing = path_tracing,
        .path_bounces = static_cast<uint32_t>(rt_sliders.bounces),
        .path_ambient = path_ambient,
        .path_sky_apart = path_ambient && rt_sliders.path_sky_apart != 0.f,
        .path_kept_cube = path_ambient && rt_sliders.path_kept_cube != 0.f,
        .reflections = reflections,
        .specular_occlusion = specular_occlusion,
        .reflection_samples = static_cast<uint32_t>(rt_sliders.reflection_samples),
        .reflection_reuse = rt_sliders.reflection_reuse == 1.f && path_tracing,
        .reflection_own = rt_sliders.reflection_reuse == 2.f,
    };
    // Attila's rows. NRD reads the upscaler's motion and depth: with an AA mode on only (the helper's).
    rays.path_ser = rt_sliders.path_ser != 0.f;
    rays.sharc = path_tracing && rt_sliders.sharc != 0.f;
    rays.reuse_lean = path_tracing && rt_sliders.reuse_lean != 0.f;
    rays.reuse_near = path_tracing && rt_sliders.reuse_near != 0.f;
    rays.nrd = rt_sliders.denoiser != 0.f && rysesonofrome::graphics::requested_mode.load() != rysesonofrome::graphics::AAMode::OFF;
    rays.ao_reblur = rt_sliders.ao_reblur != 0.f;
    rays.debug_view = static_cast<uint32_t>(rt_sliders.debug_view);
    constexpr std::array<uint32_t, 4> SCENE_TESTS = {0u, 2u, 3u, 4u};  // the row's items as rt_core's scene tests
    rays.scene_test = ((rt_sliders.debug_view != 0.f) ? SCENE_TESTS[static_cast<size_t>(std::clamp(rt_sliders.scene_test, 0.f, 3.f))] : 0u);
    rays.debug_log = rt_sliders.debug_log != 0.f;
    rays.units_range = rt_sliders.units_range;
    rays.casters_out_of_view = rays.sun_shadows && rt_sliders.casters_out_of_view != 0.f;
    // Rays a Pixel: Match Output, those of the output pixels a render pixel stands for (at most 8).
    const auto render = rysesonofrome::ray_tracing::HOST.render_size();
    const float scale = (render[0] != 0 && back_buffer.texture.width != 0)
                            ? std::min(static_cast<float>(render[0]) / static_cast<float>(back_buffer.texture.width), 1.f)
                            : 1.f;
    const auto choice = static_cast<uint32_t>(std::clamp(rt_sliders.output_rays, 0.f, 8.f));
    const auto density = (choice == 1) ? static_cast<uint32_t>(std::clamp(std::lround(1.f / (scale * scale)), 1l, 8l)) : std::clamp(choice, 1u, 8u);
    const auto rays_of = [density](float on) { return (on != 0.f) ? density : 1u; };
    rays.shadow_rays = rays_of(rt_sliders.rays_shadows);
    rays.ambient_rays *= rays_of(rt_sliders.rays_ambient);
    rays.reflection_samples *= rays_of(rt_sliders.rays_reflections);
    rays.light_bounce_rays = std::min(rays.light_bounce_rays * rays_of(rt_sliders.rays_paths), 16u);
    rysesonofrome::ray_tracing::Update(rays);
    rysesonofrome::shadow_skip::OnPresent(
        (rt_sliders.sun_shadows != 0.f) ? static_cast<rysesonofrome::shadow_skip::Choice>(std::clamp(rt_sliders.skip_sun_maps, 0.f, 2.f))
                                        : rysesonofrome::shadow_skip::OFF);
  }
  rysesonofrome::graphics::UpdateHelper(swapchain, use, !IsHdrOutput());
#ifdef RYSE_UI_DRAW_LOG
  if (ui_log != nullptr) {
    std::fputs("frame end\n", ui_log);
    std::fclose(ui_log);
    ui_log = nullptr;
  }
#endif
  execution_skull_min_indices =
      static_cast<uint32_t>(3500.f * std::pow(static_cast<float>(back_buffer.texture.height) / 2160.f, 0.23f));
  if (GetForegroundWindow() != static_cast<HWND>(swapchain->get_hwnd())) return;
#ifdef RYSE_UI_DRAW_LOG
  if (KeyWentDown(VK_F9)) {
    std::array<wchar_t, MAX_PATH> path = {};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), MAX_PATH);
    std::wstring file(path.data(), length);
    file = file.substr(0, file.find_last_of(L'\\') + 1) + L"ryse_ui_draws.txt";
    if (_wfopen_s(&ui_log, file.c_str(), L"a") == 0) {
      logged_ui_draws = 0;
      std::fprintf(ui_log, "frame start (F9, hud %s, highlights %s, skull %s)\n", (hud_hidden ? "hidden" : "shown"),
                   (execution_highlights_hidden ? "hidden" : "shown"), (execution_skull_hidden ? "hidden" : "shown"));
      reshade::log::message(reshade::log::level::info, "[Ryse UI] F9: logging one frame to ryse_ui_draws.txt");
    }
  }
#endif
  const bool hud_key = KeyWentDown(VK_F6);
  if (KeyWentDown(VK_CAPITAL) || hud_key) {
    hud_hidden = !hud_hidden;
  }
  const bool highlights_key = KeyWentDown(VK_F7);
  if (KeyWentDown(VK_TAB) || highlights_key) {
    execution_highlights_hidden = !execution_highlights_hidden;
  }
  // F5 only: the key above Tab is the game's command key, and F8 is Print on some keyboards.
  if (KeyWentDown(VK_F5)) {
    execution_skull_hidden = !execution_skull_hidden;
  }
}

// RenoDX makes the swap chain flip model, where Present unbinds the back buffer from the pipeline
// (the game's blt model kept it bound). The menu binds its back buffer once and keeps drawing:
// after the first Present into nothing (black windowed; one frozen frame after an alt-tab, which
// rebinds). Gameplay rebinds every frame. So at the first draw after Present, if the game's last
// binding was the back buffer and nothing is bound, it is bound again. Back buffer binds, current or
// older (kept after a resize), go to RenoDX's FP16 clone of the current one, which the proxy presents.
namespace back_buffer_fix {
struct Data {
  reshade::api::device* device = nullptr;
  std::vector<uint64_t> old_buffers;  // earlier back buffers, the newest last
  std::vector<uint64_t> views;        // the game's render target views of the back buffers (current and earlier)
  reshade::api::resource current = {0u};
  reshade::api::resource target = {0u};  // RenoDX's clone of the current back buffer, else itself
  reshade::api::resource_view rtv = {0u};
  std::vector<uint64_t> logged;
  int lines = 0;
};

Data data;
std::mutex mutex;
std::atomic_bool check_after_present = false;
// Read without the mutex on every bind and draw (the mutex only for back buffer binds): the current and earlier back
// buffers (data.current, data.old_buffers), the game's views of them (data.views: a bind is matched by its view, no
// lookup of the view's resource a bind), the immediate context and the game's last binding on it.
constexpr size_t MAX_BACK_BUFFERS = 16;
constexpr size_t MAX_BACK_BUFFER_VIEWS = 32;
std::array<std::atomic<uint64_t>, MAX_BACK_BUFFERS> back_buffers = {};
std::array<std::atomic<uint64_t>, MAX_BACK_BUFFER_VIEWS> back_buffer_views = {};
// More of the game's back buffer views than back_buffer_views holds: each bind's view looked up again (as before).
std::atomic_bool too_many_views = false;
std::atomic<uint64_t> immediate_context = 0u;
std::atomic_bool bound_back_buffer = false;

// Caller holds the mutex: back_buffers and back_buffer_views from data (the oldest earlier buffers dropped beyond
// MAX_BACK_BUFFERS).
void PublishBackBuffers() {
  if (data.old_buffers.size() >= MAX_BACK_BUFFERS) {
    data.old_buffers.erase(data.old_buffers.begin(), data.old_buffers.end() - (MAX_BACK_BUFFERS - 1));
  }
  size_t index = 0;
  back_buffers[index++].store(data.current.handle, std::memory_order_relaxed);
  for (const uint64_t buffer : data.old_buffers) back_buffers[index++].store(buffer, std::memory_order_relaxed);
  while (index < MAX_BACK_BUFFERS) back_buffers[index++].store(0u, std::memory_order_relaxed);
  too_many_views.store(data.views.size() > MAX_BACK_BUFFER_VIEWS, std::memory_order_relaxed);
  for (index = 0; index < MAX_BACK_BUFFER_VIEWS; ++index) {
    back_buffer_views[index].store((index < data.views.size() ? data.views[index] : 0u), std::memory_order_relaxed);
  }
}

// Caller holds the mutex.
void Log(const char* text) {
  if (data.lines++ < 32) reshade::log::message(reshade::log::level::info, text);
}

// Caller holds the mutex. Logs the first time per key.
void LogOnce(uint64_t key, const char* what, uint64_t from) {
  if (std::find(data.logged.begin(), data.logged.end(), key) != data.logged.end()) return;
  data.logged.push_back(key);
  char text[128];
  std::snprintf(text, sizeof(text), "[Ryse] %s 0x%llx, bound 0x%llx instead", what,
                static_cast<unsigned long long>(from), static_cast<unsigned long long>(data.target.handle));
  Log(text);
}

// Caller holds the mutex.
void DestroyView() {
  if (data.device != nullptr && data.rtv.handle != 0u) data.device->destroy_resource_view(data.rtv);
  data.rtv = {0u};
  data.target = {0u};
}

bool IsBackBuffer(uint64_t resource) {
  return resource != 0u && std::ranges::any_of(back_buffers, [&](const std::atomic<uint64_t>& buffer) {
           return buffer.load(std::memory_order_relaxed) == resource;
         });
}

bool IsBackBufferView(uint64_t view) {
  return view != 0u && std::ranges::any_of(back_buffer_views, [&](const std::atomic<uint64_t>& known) {
           return known.load(std::memory_order_relaxed) == view;
         });
}

// Binds our view of the clone (else the current back buffer) to slot 0. Caller holds the mutex.
bool BindTarget(reshade::api::device* device, ID3D11DeviceContext* context,
                ID3D11RenderTargetView* const* views, uint32_t count, ID3D11DepthStencilView* dsv) {
  if (data.device != device || data.current.handle == 0u) return false;
  if (data.rtv.handle == 0u) {
    data.target = data.current;
    renodx::utils::resource::GetResourceInfo(data.current, [&](const renodx::utils::resource::ResourceInfo& info) {
      if (info.clone_enabled && info.clone.handle != 0u) data.target = info.clone;
    });
    const reshade::api::resource_view_desc desc(
        reshade::api::format_to_default_typed(device->get_resource_desc(data.target).texture.format));
    if (!device->create_resource_view(data.target, reshade::api::resource_usage::render_target, desc, &data.rtv)) {
      data.rtv = {0u};
      Log("[Ryse] could not create a view on the back buffer");
      return false;
    }
  }
  ID3D11RenderTargetView* moved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  for (uint32_t i = 0; i < count; ++i) moved[i] = views[i];
  moved[0] = reinterpret_cast<ID3D11RenderTargetView*>(data.rtv.handle);
  context->OMSetRenderTargets(std::max(count, 1u), moved, dsv);
  return true;
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
  const std::scoped_lock lock(mutex);
  if (data.current.handle != 0u) data.old_buffers.push_back(data.current.handle);
  DestroyView();
  data.device = swapchain->get_device();
  data.current = swapchain->get_current_back_buffer();
  std::erase(data.old_buffers, data.current.handle);  // may reuse an old address
  PublishBackBuffers();
  ID3D11DeviceContext* immediate = nullptr;
  reinterpret_cast<ID3D11Device*>(data.device->get_native())->GetImmediateContext(&immediate);
  if (immediate != nullptr) {
    immediate_context.store(reinterpret_cast<uint64_t>(immediate), std::memory_order_relaxed);
    immediate->Release();
  }
  char text[96];
  std::snprintf(text, sizeof(text), "[Ryse] swap chain %s: back buffer 0x%llx", resize ? "resized" : "created",
                static_cast<unsigned long long>(data.current.handle));
  Log(text);
}

// Before a resize: our view must not hold the back buffer.
void OnDestroySwapchain(reshade::api::swapchain*, bool) {
  const std::scoped_lock lock(mutex);
  DestroyView();
}

// A new resource at an old back buffer's address is not that buffer.
void OnInitResource(
    reshade::api::device*, const reshade::api::resource_desc&, const reshade::api::subresource_data*,
    reshade::api::resource_usage, reshade::api::resource resource) {
  if (!IsBackBuffer(resource.handle)) return;
  const std::scoped_lock lock(mutex);
  std::erase(data.old_buffers, resource.handle);
  PublishBackBuffers();
}

// The game's render target views of a back buffer: its binds are known by the view.
void OnInitResourceView(reshade::api::device*, reshade::api::resource resource, reshade::api::resource_usage usage_type,
                        const reshade::api::resource_view_desc&, reshade::api::resource_view view) {
  if (usage_type != reshade::api::resource_usage::render_target || !IsBackBuffer(resource.handle)) return;
  const std::scoped_lock lock(mutex);
  if (std::ranges::find(data.views, view.handle) == data.views.end()) data.views.push_back(view.handle);
  PublishBackBuffers();
}

// A view's address may come back as another resource's view.
void OnDestroyResourceView(reshade::api::device*, reshade::api::resource_view view) {
  if (!IsBackBufferView(view.handle) && !too_many_views.load(std::memory_order_relaxed)) return;
  const std::scoped_lock lock(mutex);
  if (std::erase(data.views, view.handle) != 0) PublishBackBuffers();
}

void OnBindRenderTargets(
    reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view* rtvs,
    reshade::api::resource_view dsv) {
  auto* device = cmd_list->get_device();
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const uint64_t view = (count != 0 ? rtvs[0].handle : 0u);
  const bool back_buffer = (too_many_views.load(std::memory_order_relaxed)
                                ? (view != 0u && IsBackBuffer(device->get_resource_from_view(rtvs[0]).handle))
                                : IsBackBufferView(view));
  if (cmd_list->get_native() == immediate_context.load(std::memory_order_relaxed)) {
    bound_back_buffer.store(back_buffer, std::memory_order_relaxed);
  }
  if (!back_buffer) return;
  // A view of a back buffer dropped from back_buffers (beyond MAX_BACK_BUFFERS) is no back buffer any more.
  const uint64_t bound = device->get_resource_from_view(rtvs[0]).handle;
  if (!IsBackBuffer(bound)) return;

  const std::scoped_lock lock(mutex);

  ID3D11RenderTargetView* views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  const uint32_t view_count = std::min<uint32_t>(count, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
  for (uint32_t i = 0; i < view_count; ++i) views[i] = reinterpret_cast<ID3D11RenderTargetView*>(rtvs[i].handle);
  // RenoDX binds the clone itself when it can; then this is the same view.
  if (BindTarget(device, reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()), views, view_count,
                 reinterpret_cast<ID3D11DepthStencilView*>(dsv.handle))) {
    LogOnce(bound, bound == data.current.handle ? "game binds back buffer" : "game binds old back buffer", bound);
  }
}

void OnPresent(
    reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
    uint32_t, const reshade::api::rect*) {
  check_after_present = true;
}

// First draw on the immediate context after Present. The draw events stay registered: they are the one point after
// Present and before the game's next draw on that context (finish_present is not proven to come after the unbind with
// frame generation's swap chain in between), and every other draw returns at the first atomic load.
void CheckBound(reshade::api::command_list* cmd_list) {
  if (!check_after_present.load()) return;
  auto* device = cmd_list->get_device();
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  if (cmd_list->get_native() != immediate_context.load(std::memory_order_relaxed)) return;
  if (!check_after_present.exchange(false)) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());

  ID3D11RenderTargetView* views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* dsv = nullptr;
  context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, views, &dsv);
  uint64_t bound = 0u;
  if (views[0] != nullptr) {
    ID3D11Resource* resource = nullptr;
    views[0]->GetResource(&resource);
    bound = reinterpret_cast<uint64_t>(resource);
    if (resource != nullptr) resource->Release();
  }
  uint32_t count = 0;
  while (count < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && views[count] != nullptr) ++count;
  {
    const std::scoped_lock lock(mutex);
    if (bound == 0u && bound_back_buffer.load(std::memory_order_relaxed)) {
      // Unbound by Present: the game still draws to its back buffer.
      if (BindTarget(device, context, views, count, dsv)) LogOnce(1u, "back buffer unbound by Present, game draws to", data.current.handle);
    } else if (IsBackBuffer(bound)) {
      if (BindTarget(device, context, views, count, dsv)) LogOnce(bound, "back buffer still bound", bound);
    }
  }
  for (auto* view : views) {
    if (view != nullptr) view->Release();
  }
  if (dsv != nullptr) dsv->Release();
}

bool OnDraw(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, uint32_t) {
  CheckBound(cmd_list);
  return false;
}

bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  CheckBound(cmd_list);
  return false;
}

void OnDestroyDevice(reshade::api::device* device) {
  rysesonofrome::contact_shadows::Release();
  rysesonofrome::light_bounce::Release();
  if (ray_tracing_installed) {
    twattila::rt_scene::OnDestroyDevice(device);
    rysesonofrome::ray_tracing::Release();
    rysesonofrome::shadow_skip::Release();
  }
  const std::scoped_lock lock(mutex);
  if (data.device != device) return;
  DestroyView();
  data = {};
  PublishBackBuffers();
  immediate_context.store(0u, std::memory_order_relaxed);
  bound_back_buffer.store(false, std::memory_order_relaxed);
}

}  // namespace back_buffer_fix

// One log line per render target the resource upgrades take (all view clones). The handle is the game's resource, the
// one the frame trace prints as "clone of <handle>" for a RenoDX clone view, so the reader census can match the two.
// Runs after RenoDX's init_resource, which marks the targets; capped so a game recreating targets can't fill the log.
constexpr int MAX_UPGRADE_LOG_LINES = 256;
std::atomic_int upgrade_log_lines = 0;

void OnInitResourceLogUpgrade(
    reshade::api::device*, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*,
    reshade::api::resource_usage, reshade::api::resource resource) {
  if (desc.type != reshade::api::resource_type::texture_2d) return;
  if (!renodx::utils::bitwise::HasFlag(desc.usage, reshade::api::resource_usage::render_target)) return;
  if (upgrade_log_lines.load(std::memory_order_relaxed) > MAX_UPGRADE_LOG_LINES) return;
  reshade::api::format new_format = reshade::api::format::unknown;
  renodx::utils::resource::GetResourceInfo(resource, [&](const renodx::utils::resource::ResourceInfo& info) {
    if (info.clone_target != nullptr) {
      new_format = info.clone_target->new_format;
    }
  });
  if (new_format == reshade::api::format::unknown) return;
  const int line_index = upgrade_log_lines.fetch_add(1, std::memory_order_relaxed);
  if (line_index > MAX_UPGRADE_LOG_LINES) return;
  std::ostringstream text;
  if (line_index == MAX_UPGRADE_LOG_LINES) {
    text << "[Ryse] more than " << MAX_UPGRADE_LOG_LINES << " upgrades: no more upgrade lines";
  } else {
    text << "[Ryse] upgrade 0x" << std::hex << resource.handle << std::dec << ' ' << desc.texture.width << 'x'
         << desc.texture.height << ' ' << desc.texture.format << " -> " << new_format << " view clone";
  }
  reshade::log::message(reshade::log::level::info, text.str().c_str());
}

bool configured = false;
bool attached = false;
bool addon_registered = false;

}  // namespace

// Lifecycle /////////////////////////////////////////////////////////////////////////////////////////

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool) {
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

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX - Ryse: Son of Rome by ATN";

namespace {

void ConfigureAddon() {
  if (configured) return;

  BuildRuntimeData();

  // Shaders cb
  renodx::mods::shader::expected_constant_buffer_space = 50;
  renodx::mods::shader::expected_constant_buffer_index = 13;

  // Swapchain cb
  renodx::mods::swapchain::expected_constant_buffer_index = 13;
  renodx::mods::swapchain::expected_constant_buffer_space = 50;

  renodx::mods::swapchain::use_resource_cloning = true;
  // Game renders into a persistent FP16 clone of the back buffer. scRGB's default converts the
  // back buffer in place at Present: menus redraw over the last frame, so it was converted again
  // every frame (black/posterized menu, overlay trails).
  renodx::mods::swapchain::swapchain_proxy_compatibility_mode = false;
  // CryEngine caches D3D state too: restore it after the proxy draw.
  renodx::mods::swapchain::swapchain_proxy_revert_state = true;
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

  // Post-tonemap frame and everything else RGBA8 at back-buffer aspect, upgraded at creation.
  // No hot swap: CryEngine copies between these and the back buffer (CopyResource), which
  // needs the same format on both sides. sRGB views become FP16 (no decode): the TAA and the
  // upscaler decode by hand. The aspect tolerance takes the game's rounding of its reduced
  // targets (half, quarter, ...: 2960x1665 -> 740x416, off by 1.78 / height): an exact match
  // left the sun shafts' quarter target 8-bit (clipped at white) at DLSS sizes like 77%.
  // Edit. added view cloning to avoid some textures/stuff getting destroyed.
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
        .aspect_ratio_tolerance = 0.030002723f,
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
                   "HDR10: 10-bit. scRGB: 16-bit. SDR: displays without HDR (all fixes and options stay).",
        .labels = {"HDR10", "scRGB", "SDR", "Auto"},
        .is_global = true,
        .is_visible = IsAdvancedSettings,
    };
    renodx::utils::settings::LoadSetting(renodx::utils::settings::global_name, setting);
    // Decided at each new swap chain (device_loss's create_swapchain, before the swap chain mod's), Auto by Windows HDR
    // for the game's display.
    twattila::output_encoding::Configure({
        .shader_encoding = &shader_injection.swap_chain_encoding,
        .setting = static_cast<int>(setting->GetValue()),
        .hdr_encoding = [](int output) {
          // The DLSS addon's frame generation takes no scRGB: HDR10 with it. Auto's HDR is scRGB.
          bool frame_generation = false;
          reshade::get_config_value(nullptr, "Ryse AA", "FrameGeneration", frame_generation);
          return (output == twattila::output_encoding::HDR10 || frame_generation) ? twattila::output_encoding::HDR10 : twattila::output_encoding::SCRGB;
        },
        .first_start_without_hdr = &ApplySdrPreset,
        .recommended_preset = &ApplyRecommendedPreset,
        .log_name = "[Ryse]",
    });
    settings.push_back(setting);
  }

  settings.insert(settings.end(), {
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::Methods({.psycho_v30 = false, .gtvbao = true, .gtvbgi = true, .auto_thickness = true,
                                               .detours = true}) +
                   "\nUC2 Extended by Musa and Jon.",
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::HELPER,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = twattila::credits::NGX_HEADER,
          .section = "About",
      },
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::TEXT,
          .label = std::string("Build Date: ") + BUILD_DATE + " - " + BUILD_TIME,
          .section = "About",
          .is_visible = IsAdvancedSettings,
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
          .on_change = []() { renodx::utils::platform::LaunchURL("https://www.nexusmods.com/rysesonofrome/mods/59"); },
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

  // Section titles: one colour (as Attila's), Output and the game's graphics the engine sections' pink, About and the
  // anti-aliasing their own.
  twattila::feature_settings::SetTints({"General", "Presets", "Extended", "PsychoV31", "Extra", "Ambient Occlusion", "Contact Shadows", "Audio"},
                                       {"Output (Restart Required)", "Graphics", "Graphics (Experimental)"});
  renodx::utils::settings::section_tints.insert(renodx::utils::settings::section_tints.end(), {{"About", 0xE6C27A}, {AA_SECTION, 0xB39DDB}});

  // Graphics 1 of 2 and 2 of 2 (graphics.hpp; once the DLSS add-on's overlay, which still writes its own ReShade.ini
  // keys: [Ryse AA], [Ryse Game Settings]), before About.
  const auto custom_row = [](const std::string& label, const std::string& section, std::function<void()> draw) {
    return new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::CUSTOM,
        .label = label,
        .section = section,
        .on_draw = [draw = std::move(draw)] {
          draw();
          return false;
        },
    };
  };
  const auto about = std::ranges::find(settings, std::string("About"), &renodx::utils::settings::Setting::section);
  settings.insert(about, {
      Note(FULL_DOWNLOAD_NOTE, AA_SECTION, WARNING_TINT, HelperAbsent),
      custom_row("##Anti-Aliasing", AA_SECTION, [] { rysesonofrome::graphics::DrawAntiAliasing(IsAdvancedSettings()); }),
      new renodx::utils::settings::Setting{
          .key = "AntiAliasingTextureLodBiasTrilinear",
          .binding = &rysesonofrome::graphics::lod_bias_all_textures,
          .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
          .default_value = 1.f,
          .label = "Texture LOD Bias on All Textures",
          .section = AA_SECTION,
          .tooltip = "Texture LOD Bias also on the textures the game filters without anisotropy (terrain, buildings).\n"
                     "Off: those stay blurrier while the game renders below 100%.",
          .labels = {"Off", "On"},
          .is_visible = [] { return twattila::dlaa_bridge::HelperInstalled() && IsAdvancedSettings(); },
      },
      // One frame's GPU time of every pass (twattila/pass_timing.hpp, the add-on's first draw hooks).
      new renodx::utils::settings::Setting{
          .value_type = renodx::utils::settings::SettingValueType::BUTTON,
          .label = "Time Each Pass (One Frame)",
          .section = AA_SECTION,
          .tooltip = "The GPU's time of every pass of the next frame into ryse_pass_timing.txt next to the game's exe.",
          .on_change = [] { twattila::pass_timing::Request(); },
          .is_visible = IsAdvancedSettings,
      },
      custom_row("##Graphics", "Graphics", [] { rysesonofrome::graphics::DrawGameSettings(false, IsAdvancedSettings()); }),
      custom_row("##Graphics (Experimental)", "Graphics (Experimental)",
                 [] { rysesonofrome::graphics::DrawGameSettings(true, IsAdvancedSettings()); }),
      custom_row("##Audio", "Audio", [] { rysesonofrome::graphics::DrawAudio(IsAdvancedSettings()); }),
  });

  // Settings Mode's pages (the Total War add-ons'; About and the two mode rows on every page).
  twattila::feature_settings::AssignPages(&settings, &current_settings_mode, GRAPHICS_2_SECTIONS, GRAPHICS_1_SECTIONS);

  // Simple / Advanced was Settings Mode's (renodx) and the DLSS add-on's own ([Ryse AA] SettingsMode) until the one
  // add-on: either one's Advanced becomes SettingsAdvanced, once, and Settings Mode starts on HDR & Effects.
  const char* global = renodx::utils::settings::global_name.c_str();
  if (float saved = 0.f; !reshade::get_config_value(nullptr, global, "SettingsAdvanced", saved)) {
    float mode = 0.f;
    int overlay_mode = 0;
    const bool advanced = (reshade::get_config_value(nullptr, global, "SettingsMode", mode) && mode == 1.f)
                          || (reshade::get_config_value(nullptr, "Ryse AA", "SettingsMode", overlay_mode) && overlay_mode == 1);
    reshade::set_config_value(nullptr, global, "SettingsAdvanced", advanced ? 1 : 0);
    if (mode == 1.f) reshade::set_config_value(nullptr, global, "SettingsMode", 0);
  }

  // Rows saved at an old default, cleared once before settings::Use: both Auto Thickness rows off and Texture LOD Bias on
  // All Textures off (on since ATN 2026-11-22 and 11-17).
  twattila::graphics_page::ClearSavedRows(1, {"ssao_auto_thickness", "light_bounce_auto_thickness", "AntiAliasingTextureLodBiasTrilinear"});

  configured = true;
}

bool AttachAddon() {
  if (attached) return true;
  // Time Each Pass: before every other draw hook (a skipped draw still gets its stamp).
  twattila::pass_timing::Install("ryse");

  ConfigureAddon();

  // Before the swap chain mod: a removed device's chain let go, each new chain's output decided.
  twattila::device_loss::Install();
  reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

  renodx::utils::settings::Use(DLL_PROCESS_ATTACH, &settings, nullptr);
  // Scripted runs (RENODX_TW_SET / SHOT / PROFILE, twattila/test_hooks.hpp): nothing without them. Their "trace" key writes
  // the renodx_trace_now file the frame trace looks for (talesofarise_dlss/frame_trace.hpp).
  twattila::test_hooks::Install(&settings, true, "[Ryse]");
  // Ray tracing on at launch: its scene's shaders kept from the start, the Light Bounce's trace through rays. Without the
  // helper nothing could be traced: the scene capture stays out (the game's own lighting, as with the rows off).
  ray_tracing_installed = AnyRays() && !HelperAbsent();
  if (ray_tracing_installed) {
    rysesonofrome::ray_tracing::Install();
    rysesonofrome::shadow_skip::Install(&custom_shaders);
    rysesonofrome::light_bounce::ray_traced_trace = twattila::rt_scene::internal::TraceLightBounce;
    // The Debug View over the tone mapper's output (its RGBA16F target at the render size; rt_core's DrawDebugView).
    custom_shaders[0xB9B672AA].on_drawn = [](reshade::api::command_list* cmd_list) {
      if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
      auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&context);
      Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_view;
      context->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
      if (!target_view) return;
      Microsoft::WRL::ComPtr<ID3D11Resource> resource;
      target_view->GetResource(resource.GetAddressOf());
      if (Microsoft::WRL::ComPtr<ID3D11Texture2D> target; SUCCEEDED(resource.As(&target))) {
        twattila::rt_scene::DrawDebugView(context, target.Get());
      }
    };
  }
  // The hotkeys' hidden UI draws; on the fast dispatch called only while something is hidden.
  renodx::utils::command_action::Register(OnDrawUi, {.command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  renodx::utils::command_action::fast::SetGate(OnDrawUi, {.active = [](reshade::api::command_list*) {
#ifdef RYSE_UI_DRAW_LOG
                                                  if (ui_log != nullptr) return true;
#endif
                                                  return hud_hidden.load(std::memory_order_relaxed)
                                                         || execution_highlights_hidden.load(std::memory_order_relaxed)
                                                         || execution_skull_hidden.load(std::memory_order_relaxed);
                                                }});
  // Before RenoDX's draw callbacks (command_action's): the first draw after Present rebinds the back buffer before any
  // custom shader callback sees or skips the draw (a skip ends ReShade's callback list). Holds while this add-on is
  // command_action's event handler; with the DevKit loaded first, its draw callback still runs before this one.
  reshade::register_event<reshade::addon_event::draw>(back_buffer_fix::OnDraw);
  reshade::register_event<reshade::addon_event::draw_indexed>(back_buffer_fix::OnDrawIndexed);
  renodx::mods::swapchain::Use(DLL_PROCESS_ATTACH, &shader_injection);
  renodx::mods::shader::Use(DLL_PROCESS_ATTACH, custom_shaders, &shader_injection);
  // The fast draw dispatch with the resource upgrades' gate (the swap chain mod's view cloning registers them).
  twattila::draw_dispatch::Use(DLL_PROCESS_ATTACH);
  reshade::register_event<reshade::addon_event::present>(OnPresentHotkeys);
  reshade::register_event<reshade::addon_event::init_pipeline>(OnInitPipelineHighlights);

  // After RenoDX (its back buffer clone exists, its target rewrites ran).
  reshade::register_event<reshade::addon_event::init_swapchain>(back_buffer_fix::OnInitSwapchain);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(back_buffer_fix::OnDestroySwapchain);
  reshade::register_event<reshade::addon_event::init_resource>(back_buffer_fix::OnInitResource);
  reshade::register_event<reshade::addon_event::init_resource>(OnInitResourceLogUpgrade);
  reshade::register_event<reshade::addon_event::init_resource_view>(back_buffer_fix::OnInitResourceView);
  reshade::register_event<reshade::addon_event::destroy_resource_view>(back_buffer_fix::OnDestroyResourceView);
  reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(back_buffer_fix::OnBindRenderTargets);
  reshade::register_event<reshade::addon_event::present>(back_buffer_fix::OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(back_buffer_fix::OnDestroyDevice);
  twattila::device_loss::InstallLast();

  attached = true;
  return true;
}

void DetachAddon() {
  if (!attached) return;

  twattila::device_loss::Uninstall();
  reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::present>(OnPresentHotkeys);
  reshade::unregister_event<reshade::addon_event::init_pipeline>(OnInitPipelineHighlights);
  renodx::utils::command_action::fast::ClearGate(OnDrawUi);
  renodx::utils::command_action::Unregister(OnDrawUi);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(back_buffer_fix::OnInitSwapchain);
  reshade::unregister_event<reshade::addon_event::destroy_swapchain>(back_buffer_fix::OnDestroySwapchain);
  reshade::unregister_event<reshade::addon_event::init_resource>(back_buffer_fix::OnInitResource);
  reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResourceLogUpgrade);
  reshade::unregister_event<reshade::addon_event::init_resource_view>(back_buffer_fix::OnInitResourceView);
  reshade::unregister_event<reshade::addon_event::destroy_resource_view>(back_buffer_fix::OnDestroyResourceView);
  reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(back_buffer_fix::OnBindRenderTargets);
  reshade::unregister_event<reshade::addon_event::present>(back_buffer_fix::OnPresent);
  reshade::unregister_event<reshade::addon_event::draw>(back_buffer_fix::OnDraw);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(back_buffer_fix::OnDrawIndexed);
  reshade::unregister_event<reshade::addon_event::destroy_device>(back_buffer_fix::OnDestroyDevice);

  if (ray_tracing_installed) {
    rysesonofrome::light_bounce::ray_traced_trace = nullptr;
    rysesonofrome::shadow_skip::Uninstall();
    rysesonofrome::ray_tracing::Uninstall();
  }
  twattila::draw_dispatch::Use(DLL_PROCESS_DETACH);
  renodx::mods::shader::Use(DLL_PROCESS_DETACH, custom_shaders, &shader_injection);
  renodx::mods::swapchain::Use(DLL_PROCESS_DETACH, &shader_injection);
  twattila::test_hooks::Uninstall();
  renodx::utils::settings::Use(DLL_PROCESS_DETACH, &settings, nullptr);
  twattila::pass_timing::Uninstall();

  attached = false;
}

}  // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE) {
  if (!addon_registered) {
    if (!reshade::register_addon(addon_module)) return false;
    addon_registered = true;
  }

  if (!AttachAddon()) return false;
  // The anti-aliasing, frame generation and game settings (once the DLSS add-on) after the HDR part, as by name.
  rysesonofrome::graphics::Install(__swap_chain_proxy_vertex_shader_dx11, __swap_chain_proxy_pixel_shader_dx11, &shader_injection,
                                   sizeof(shader_injection));
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE) {
  rysesonofrome::graphics::Uninstall();
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
