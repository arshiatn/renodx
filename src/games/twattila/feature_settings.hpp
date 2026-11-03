/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../../utils/settings.hpp"
#include "./anti_aliasing.hpp"
#include "./lighting_settings.hpp"
#include "./screen_space_shadows.hpp"
#include "./ssr_hiz.hpp"

// The Total War addons' Screen-Space Shadows and Anti-Aliasing settings (Graphics 1 of 2, after the AO and GI): their
// sliders, sections, settings and the modules' settings from them; and Settings Mode's pages (Attila's layout).
namespace twattila::feature_settings {

inline const std::string SSS_SECTION = "Screen-Space Shadows (Bend Studio, Experimental, needs in-game shadows)";
inline const std::string AA_SECTION = "Anti-Aliasing (Experimental, in-game AA off)";
// Warhammer III: DLSS in place of the game's TAA (its in-game TAA stays on).
inline const std::string DLSS_SECTION = "DLSS (Experimental, in-game TAA on)";
inline const std::string GRAPHICS_SECTION = "Graphics (Experimental)";
constexpr uint32_t SSS_TINT = 0xC08CFF;
constexpr uint32_t AA_TINT = 0x7FFFD4;
constexpr uint32_t GRAPHICS_TINT = 0xFF7FAF;
constexpr uint32_t BASE_TINT = 0x7EE08A;

struct SssSliders {
  float enabled = 0.f;
  float length = 2.f;  // 60 x 2^this samples
  float thickness = 0.005f;
  float contrast = 4.f;
  float edge_threshold = 0.02f;
  float hard_samples = 4.f;
  float fade_samples = 8.f;
  float ignore_edges = 0.f;
  float precision_offset = 0.f;
  float sample_along_ray = 0.f;
  float softness = 0.f;  // pixels
  float debug_view = 0.f;
  float mirror_edges = 0.f;
  float foliage_light = 0.f;  // Attila's (graphics_page.hpp)
};

struct AaSliders {
  float method = 0.f;   // 0 off, 1 SMAA, 2 CMAA2, 3 SMAA T2x, 4 TAA, 5 DLAA
  float quality = 2.f;  // low to ultra
  float extra_sharpness = 0.f;
  float taa_strength = 0.9f;
  float taa_motion = 0.f;  // Attila's (graphics_page.hpp): TAA's: the camera's motion, or the objects' own too
  float sharpening = 0.25f;
  float dlaa_preset = 2.f;  // K, M, L
  float dlss_mode = 0.f;       // DLAA's scale: DLSS_MODE_SCALES, the last Custom (render_scale)
  float render_scale = 100.f;  // Custom, in percent (below 100: DLSS)
  float effects_at_output = 1.f;   // DLSS: the effects keep their 100% detail (their resolutions follow the render scale)
  float ray_reconstruction = 0.f;  // DLAA's: off, NVIDIA's preset, D, E, F
  float frame_generation = 0.f;    // DLAA's: off, 2x-6x, Dynamic
  float frame_backend = 1.f;       // Attila's (graphics_page.hpp): frame generation's: Auto, DLSS, FSR, XeSS
  float reflex = 0.f;              // NVIDIA Reflex: off, on, on + boost (frame generation: always on)
  float frame_limit = 0.f;         // Reflex's limit, rendered frames per second (0: off)
  float frame_preset = 0.f;        // frame generation's: the driver's, A, B, C
  float hud_recomposition = 1.f;   // frame generation's: the UI laid over the generated frames
  float frame_debug_view = 0.f;    // frame generation's: off, the HUD-less image, the UI layer
  float texture_lod_bias = 2.f;    // DLSS's: off, matched, sharper, sharpest
  float object_motion = 1.f;       // DLAA's: units', props' and cloth's own motion
  float debug_view = 0.f;          // DLAA's: off, motion vectors, object motion, no camera motion
};

inline screen_space_shadows::Settings SssSettings(const SssSliders& sliders) {
  return {
      .enabled = (sliders.enabled != 0.f),
      .samples = static_cast<float>(60 << std::clamp(static_cast<int>(sliders.length), 0, 5)),
      .thickness = sliders.thickness,
      .contrast = sliders.contrast,
      .edge_threshold = sliders.edge_threshold,
      .hard_samples = sliders.hard_samples,
      .fade_samples = sliders.fade_samples,
      .ignore_edges = (sliders.ignore_edges != 0.f),
      .precision_offset = (sliders.precision_offset != 0.f),
      .sample_along_ray = (sliders.sample_along_ray != 0.f),
      // Effects at Output Resolution: as soft on screen as at 100% (the nearest whole pixel, 0 off).
      .softness = std::round(sliders.softness * frame_history::effect_share.load(std::memory_order_relaxed)),
      .debug_view = static_cast<int>(sliders.debug_view),
      .mirror_edges = (sliders.mirror_edges != 0.f),
      .foliage_light = sliders.foliage_light,
  };
}

// DLSS's modes, as Tales of Arise's and Shogun 2's: DLAA (100%), Ultra Quality, Quality, Balanced, Performance, Ultra
// Performance; then Custom.
constexpr std::array<float, 6> DLSS_MODE_SCALES = {1.f, 0.77f, 2.f / 3.f, 0.58f, 0.5f, 1.f / 3.f};

// About's line for what ships with the helper.
constexpr const char* HELPER_CREDITS =
    "NVIDIA DLSS (Super Resolution, Ray Reconstruction and Frame Generation), NVIDIA Reflex, Streamline, "
    "NVAPI, the Opacity Micro-Map SDK, SHaRC and NRD: NVIDIA Corporation, under NVIDIA's licenses (this software "
    "contains source code provided by NVIDIA Corporation). NVIDIA, GeForce "
    "RTX, DLSS and Reflex are trademarks and/or registered trademarks of NVIDIA Corporation. DirectX "
    "Agility SDK: Microsoft. Ray tracing: ideas from Embark Studios' kajiya (MIT) and Bustanity's "
    "Phantom FX (MIT). AMD FSR: Advanced Micro Devices, Inc. (MIT License). XeSS: Intel Corporation (Intel "
    "Simplified Software License). Every license is in the licenses folder.";

// An upscaler in the helper: DLSS, FSR or XeSS (only Attila's list has more than DLSS).
inline bool Upscaling(const AaSliders& sliders) { return sliders.method >= 5.f; }

inline float RenderScale(const AaSliders& sliders) {
  const auto mode = static_cast<size_t>(std::clamp(sliders.dlss_mode, 0.f, static_cast<float>(DLSS_MODE_SCALES.size())));
  return (mode < DLSS_MODE_SCALES.size()) ? DLSS_MODE_SCALES[mode] : std::clamp(sliders.render_scale, 1.f, 100.f) / 100.f;
}

inline anti_aliasing::Settings AaSettings(const AaSliders& sliders) {
  return {
      .method = static_cast<twattila::anti_aliasing::Method>(std::clamp(static_cast<int>(sliders.method), 0, 7)),
      .quality = static_cast<int>(sliders.quality),
      .extra_sharpness = (sliders.extra_sharpness != 0.f),
      .history_weight = sliders.taa_strength,
      .sharpening = sliders.sharpening,
      // NVSDK_NGX_DLSS_Hint_Render_Preset K, M, L.
      .dlaa_preset = std::array<uint32_t, 3>{11u, 13u, 12u}[std::clamp(static_cast<int>(sliders.dlaa_preset), 0, 2)],
      .render_scale = RenderScale(sliders),
      .effects_at_output = (sliders.effects_at_output != 0.f),
      .ray_reconstruction = sliders.method == 5.f && sliders.ray_reconstruction != 0.f,
      // NVSDK_NGX_RayReconstruction_Hint_Render_Preset Default, D, E, F.
      .ray_reconstruction_preset = std::array<uint32_t, 5>{0u, 0u, 4u, 5u, 6u}[std::clamp(static_cast<int>(sliders.ray_reconstruction), 0, 4)],
      // 2-6 frames shown per rendered frame, 1 Dynamic (frame_generation.hpp), 0 off.
      .frame_generation = (sliders.frame_generation >= 6.f   ? 1u
                           : sliders.frame_generation >= 1.f ? static_cast<uint32_t>(sliders.frame_generation) + 1u
                                                             : 0u),
      .frame_backend = std::array<uint32_t, 4>{dlaa_bridge::FG_BACKEND_AUTO, dlaa_bridge::FG_BACKEND_DLSS, dlaa_bridge::FG_BACKEND_FSR,
                                               dlaa_bridge::FG_BACKEND_XESS}[std::clamp(static_cast<int>(sliders.frame_backend), 0, 3)],
      .reflex = static_cast<uint32_t>(std::clamp(sliders.reflex, 0.f, 2.f)),
      .frame_limit = static_cast<uint32_t>(std::clamp(sliders.frame_limit, 0.f, 240.f)),
      .frame_preset = static_cast<uint32_t>(std::clamp(sliders.frame_preset, 0.f, 3.f)),
      .hud_recomposition = (sliders.hud_recomposition != 0.f),
      .frame_debug_view = static_cast<uint32_t>(std::clamp(sliders.frame_debug_view, 0.f, 2.f)),
      .texture_lod_bias = std::clamp(static_cast<int>(sliders.texture_lod_bias), 0, 3),
      .object_motion = ((sliders.method == 4.f ? sliders.taa_motion : sliders.object_motion) != 0.f),
      .debug_view = static_cast<uint32_t>(std::clamp(sliders.debug_view, 0.f, 3.f)),
  };
}

// Screen-Space Shadows' settings, an empty line before them (after the GI, ATN); Bend's options after its own defaults.
inline void AppendSss(std::vector<renodx::utils::settings::Setting*>* settings, SssSliders* sliders) {
  const auto sss_enabled = [sliders] { return sliders->enabled != 0.f; };
  const auto sss_toggle = [&](const std::string& key, float* binding, const std::string& label,
                              const std::string& tooltip) {
    return new renodx::utils::settings::Setting{
        .key = key,
        .binding = binding,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 0.f,
        .label = label,
        .section = SSS_SECTION,
        .tooltip = tooltip,
        .labels = {"Off", "On"},
        .is_enabled = sss_enabled,
    };
  };
  settings->insert(settings->end(), {
                                        new renodx::utils::settings::Setting{
                                            .value_type = renodx::utils::settings::SettingValueType::TEXT,
                                            .label = "",
                                            .section = twattila::lighting_settings::GI_SECTION,
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadows",
                                            .binding = &sliders->enabled,
                                            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                                            .default_value = 0.f,
                                            .label = "Screen-Space Shadows",
                                            .section = SSS_SECTION,
                                            .tooltip = "Fine sun shadows from what is on screen (Bend Studio's): soldiers, weapons, grass and small\n"
                                                       "details get contact shadows the shadow map is too coarse for. Needs in-game shadows.",
                                            .labels = {"Off", "On"},
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadowsLength",
                                            .binding = &sliders->length,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 2.f,
                                            .label = "Length",
                                            .section = SSS_SECTION,
                                            .tooltip = "How far the shadows reach, in screen pixels. Longer: heavier.",
                                            .labels = {"60", "120", "240", "480", "960", "1920"},
                                            .is_enabled = sss_enabled,
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadowsThickness",
                                            .binding = &sliders->thickness,
                                            .default_value = 0.005f,
                                            .label = "Thickness",
                                            .section = SSS_SECTION,
                                            .tooltip = "How thick objects are taken to be. Lower: thinner shadows, light gets behind things;\n"
                                                       "higher: fuller shadows, more halo-like shading behind objects.",
                                            .min = 0.001f,
                                            .max = 0.05f,
                                            .format = "%.3f",
                                            .is_enabled = sss_enabled,
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadowsSoftness",
                                            .binding = &sliders->softness,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 0.f,
                                            .label = "Softness",
                                            .section = SSS_SECTION,
                                            .tooltip = "Smooths the shadows' edges along surfaces, never across objects. Almost free.",
                                            .labels = {"Off", "1 px", "2 px", "3 px"},
                                            .is_enabled = sss_enabled,
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadowsContrast",
                                            .binding = &sliders->contrast,
                                            .default_value = 4.f,
                                            .label = "Contrast",
                                            .section = SSS_SECTION,
                                            .tooltip = "How quickly a pixel goes from lit to fully shadowed. Higher: harder, darker shadows.",
                                            .min = 1.f,
                                            .max = 16.f,
                                            .format = "%.1f",
                                            .is_enabled = sss_enabled,
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadowsHardSamples",
                                            .binding = &sliders->hard_samples,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 1.f,
                                            .label = "Hard Shadow Samples",
                                            .section = SSS_SECTION,
                                            .tooltip = "The samples next to each pixel that shadow it fully on their own: crisp contact shadows.\n"
                                                       "Higher: darker, harder near contacts.",
                                            .labels = {"0", "4", "8", "16", "32"},
                                            .is_enabled = sss_enabled,
                                            .parse = [](float index) { return (index < 0.5f) ? 0.f : 4.f * std::exp2(index - 1.f); },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadowsFadeSamples",
                                            .binding = &sliders->fade_samples,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 1.f,
                                            .label = "Fade-Out Samples",
                                            .section = SSS_SECTION,
                                            .tooltip = "The last samples, which fade the shadow out. Higher: softer shadow ends.",
                                            .labels = {"4", "8", "16", "32", "64"},
                                            .is_enabled = sss_enabled,
                                            .parse = [](float index) { return 4.f * std::exp2(index); },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadowsEdgeThreshold",
                                            .binding = &sliders->edge_threshold,
                                            .default_value = 0.02f,
                                            .label = "Edge Threshold",
                                            .section = SSS_SECTION,
                                            .tooltip = "A depth jump bigger than this is an edge (not smoothed across).\n"
                                                       "Lower: sharper silhouettes; higher: smoother, may bleed.",
                                            .min = 0.001f,
                                            .max = 0.2f,
                                            .format = "%.3f",
                                            .is_enabled = sss_enabled,
                                        },
                                        sss_toggle("ScreenSpaceShadowsIgnoreEdges", &sliders->ignore_edges, "Ignore Edge Pixels",
                                                   "Edge pixels cast no shadow: fewer stripes on flat ground under a low sun;\n"
                                                   "thinner shadows from leaves."),
                                        sss_toggle("ScreenSpaceShadowsPrecisionOffset", &sliders->precision_offset, "Precision Offset",
                                                   "A small offset for the depth buffer's precision: fewer speckles, slightly shorter shadows."),
                                        sss_toggle("ScreenSpaceShadowsAlongRay", &sliders->sample_along_ray, "Sample Along the Ray",
                                                   "Reads the depth along the shadow's ray instead of at pixel centres: better on grazing\n"
                                                   "surfaces, can be more jagged elsewhere."),
                                        sss_toggle("ScreenSpaceShadowsMirrorEdges", &sliders->mirror_edges, "Mirror Screen Edges",
                                                   "Near the screen's edges, shadows from what lies past them are guessed from what is just\n"
                                                   "inside (the grass goes on). Off: no shadows there when they would come from off screen."),
                                        new renodx::utils::settings::Setting{
                                            .key = "ScreenSpaceShadowsDebugView",
                                            .binding = &sliders->debug_view,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 0.f,
                                            .label = "Debug View",
                                            .section = SSS_SECTION,
                                            .tooltip = "Shows only the screen-space shadows (black: shadowed).\n"
                                                       "Screen Edge: red where a shadow would come from past the screen's edge (it cannot).",
                                            .labels = {"Off", "Shadows", "Shadows + Screen Edge"},
                                            .is_enabled = sss_enabled,
                                        },
                                        new renodx::utils::settings::Setting{
                                            .value_type = renodx::utils::settings::SettingValueType::TEXT,
                                            .label = "",
                                            .section = SSS_SECTION,
                                        },
                                    });
}

// Anti-Aliasing's settings (SMAA, CMAA2, SMAA T2x, TAA).
// `render_scale`: the game's camera layout is known to work with DLSS's render scale (Attila); `ray_reconstruction`:
// its G-buffer is the one aa_rr_guides reads (Attila's).
inline void AppendAa(std::vector<renodx::utils::settings::Setting*>* settings, AaSliders* sliders, bool render_scale = false,
                     bool ray_reconstruction = false) {
  settings->insert(settings->end(), {
                                        new renodx::utils::settings::Setting{
                                            .key = "AntiAliasing",
                                            .binding = &sliders->method,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 0.f,
                                            .label = "Anti-Aliasing",
                                            .section = AA_SECTION,
                                            .tooltip = "Smooths jagged edges. Turn the in-game anti-aliasing off.\n"
                                                       "SMAA: catches more edges. CMAA2: sharper, keeps more fine detail.\n"
                                                       "SMAA T2x: SMAA with two frames blended. TAA: blends frames, best on grass, softer.\n"
                                                       "DLAA: NVIDIA's (RTX); needs renodx-tw-dlaa64.exe and nvngx_dlss.dll in the game folder.",
                                            .labels = {"Off", "SMAA", "CMAA2", "SMAA T2x", "TAA", "DLAA"},
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "AntiAliasingQuality",
                                            .binding = &sliders->quality,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 2.f,
                                            .label = "Quality",
                                            .section = AA_SECTION,
                                            .tooltip = "Higher: finds fainter edges, a little heavier.",
                                            .labels = {"Low", "Medium", "High", "Ultra"},
                                            .is_enabled = [sliders] { return sliders->method == 1.f || sliders->method == 2.f || sliders->method == 3.f; },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "AntiAliasingExtraSharpness",
                                            .binding = &sliders->extra_sharpness,
                                            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                                            .default_value = 0.f,
                                            .label = "Extra Sharpness (CMAA2)",
                                            .section = AA_SECTION,
                                            .tooltip = "CMAA2 keeps even more fine detail and text sharp, smoothing a little less.",
                                            .labels = {"Off", "On"},
                                            .is_enabled = [sliders] { return sliders->method == 2.f; },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "AntiAliasingTaaStrength",
                                            .binding = &sliders->taa_strength,
                                            .default_value = 0.9f,
                                            .label = "TAA Strength",
                                            .section = AA_SECTION,
                                            .tooltip = "Higher: smoother edges and grass, more blur and trails in motion.",
                                            .min = 0.5f,
                                            .max = 0.97f,
                                            .format = "%.2f",
                                            .is_enabled = [sliders] { return sliders->method == 4.f; },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "AntiAliasingSharpening",
                                            .binding = &sliders->sharpening,
                                            .default_value = 0.25f,
                                            .label = "Sharpening (TAA, SMAA T2x)",
                                            .section = AA_SECTION,
                                            .tooltip = "Sharpens the blended image.",
                                            .max = 1.f,
                                            .format = "%.2f",
                                            .is_enabled = [sliders] { return sliders->method == 3.f || sliders->method == 4.f; },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "AntiAliasingDlaaPreset",
                                            .binding = &sliders->dlaa_preset,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 2.f,
                                            .label = "DLAA Preset",
                                            .section = AA_SECTION,
                                            .tooltip = "NVIDIA's models: K the first transformer, M the second, L the sharpest (default).",
                                            .labels = {"K", "M", "L"},
                                            .is_enabled = [sliders] { return sliders->method == 5.f; },
                                        },
                                    });
  if (render_scale) {
    settings->push_back(new renodx::utils::settings::Setting{
        .key = "AntiAliasingDlssMode",
        .binding = &sliders->dlss_mode,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "DLSS Mode (Experimental)",
        .section = AA_SECTION,
        .tooltip = "DLAA: the game at your resolution. The others draw it smaller and DLSS upscales it (faster).",
        .labels = {"DLAA (100%)", "Ultra Quality (77%)", "Quality (66.7%)", "Balanced (58%)", "Performance (50%)",
                   "Ultra Performance (33.3%)", "Custom"},
        .is_enabled = [sliders] { return Upscaling(*sliders); },
    });
    settings->push_back(new renodx::utils::settings::Setting{
        .key = "AntiAliasingRenderScale",
        .binding = &sliders->render_scale,
        .default_value = 50.f,
        .label = "Custom Render Scale",
        .section = AA_SECTION,
        .tooltip = "The size DLSS draws the game at (100%: DLAA). Below 33% NVIDIA's own modes end.",
        .min = 1.f,
        .max = 100.f,
        .format = "%.0f%%",
        .is_enabled = [sliders] { return Upscaling(*sliders); },
        .is_visible = [sliders] { return sliders->dlss_mode >= static_cast<float>(DLSS_MODE_SCALES.size()); },
    });
    settings->push_back(new renodx::utils::settings::Setting{
        .key = "AntiAliasingTextureLodBias",
        .binding = &sliders->texture_lod_bias,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 2.f,
        .label = "Texture LOD Bias",
        .section = AA_SECTION,
        .tooltip = "How sharp textures are while DLSS renders below 100%.\n"
                   "Matched: as sharp as at your display's resolution. Sharper / Sharpest: one / two steps more\n"
                   "(more detail, can shimmer).",
        .labels = {"Off (the game's own)", "Matched", "Sharper", "Sharpest"},
        .is_enabled = [sliders] { return Upscaling(*sliders) && RenderScale(*sliders) < 1.f; },
    });
    settings->push_back(new renodx::utils::settings::Setting{
        .key = "AntiAliasingEffectsAtOutput",
        .binding = &sliders->effects_at_output,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Effects at Output Resolution (Recommended)",
        .section = AA_SECTION,
        .tooltip = "While DLSS renders below 100%, ambient occlusion, Light Bounce, reflections and shadow softness\n"
                   "keep their 100% detail. Looks better, can cost some performance.",
        .labels = {"Off", "On"},
        .is_enabled = [sliders] { return Upscaling(*sliders) && RenderScale(*sliders) < 1.f; },
    });
    settings->push_back(new renodx::utils::settings::Setting{
        .key = "AntiAliasingObjectMotion",
        .binding = &sliders->object_motion,
        .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
        .default_value = 1.f,
        .label = "Object Motion Vectors",
        .section = AA_SECTION,
        .tooltip = "Soldiers, props and cloth tell DLSS how they move: sharper while they move.\n"
                   "Off: faster with many soldiers on screen (they can smear a little when moving).",
        .labels = {"Off", "On"},
        .is_enabled = [sliders] { return Upscaling(*sliders); },
    });
    settings->push_back(new renodx::utils::settings::Setting{
        .key = "AntiAliasingDebugView",
        .binding = &sliders->debug_view,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "DLSS Debug View",
        .section = AA_SECTION,
        .tooltip = "Motion Vectors: the motion DLSS gets (colour: direction, brighter: faster).\n"
                   "Object Motion: the soldiers', props' and cloth's own (grey: still, yellow: drawn twice, red: hidden).\n"
                   "No Camera Motion: DLSS gets the objects' motion alone.",
        .labels = {"Off", "Motion Vectors", "Object Motion", "No Camera Motion"},
        .is_enabled = [sliders] { return Upscaling(*sliders); },
    });
  }
  if (ray_reconstruction) {
    settings->push_back(new renodx::utils::settings::Setting{
        .key = "AntiAliasingRayReconstruction",
        .binding = &sliders->ray_reconstruction,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Ray Reconstruction (Experimental)",
        .section = AA_SECTION,
        .tooltip = "NVIDIA's denoising DLAA / DLSS (Ray Reconstruction) in place of the usual one.\n"
                   "Needs nvngx_dlssd.dll next to nvngx_dlss.dll.",
        .labels = {"Off", "NVIDIA's", "D", "E", "F"},
        .is_enabled = [sliders] { return sliders->method == 5.f; },
    });
  }
  // Frame generation (frame_generation.hpp: the helper presents).
  settings->insert(settings->end(), {
                                        new renodx::utils::settings::Setting{
                                            .key = "FrameGeneration",
                                            .binding = &sliders->frame_generation,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 0.f,
                                            .label = "Frame Generation (Experimental)",
                                            .section = AA_SECTION,
                                            .tooltip = "NVIDIA DLSS Frame Generation: frames shown per rendered frame. Turn the game's VSync off.\n"
                                                       "2x: RTX 40 or newer. 3x to 6x and Dynamic (as many as the display's refresh rate takes): RTX 50.\n"
                                                       "Needs DLAA, a windowed or borderless game,\n"
                                                       "and Streamline's files next to renodx-tw-dlaa64.exe.",
                                            .labels = {"Off", "2x", "3x", "4x", "5x", "6x", "Dynamic"},
                                            .is_enabled = [sliders] { return Upscaling(*sliders); },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "Reflex",
                                            .binding = &sliders->reflex,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 0.f,
                                            .label = "NVIDIA Reflex",
                                            .section = AA_SECTION,
                                            .tooltip = "Less input lag on NVIDIA GPUs, with any anti-aliasing: each frame starts only as the GPU\n"
                                                       "needs it. Boost also keeps the GPU's clocks up. Always on with frame generation.",
                                            .labels = {"Off", "On", "On + Boost"},
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "FrameGenerationFrameLimit",
                                            .binding = &sliders->frame_limit,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 0.f,
                                            .label = "Frame Rate Limit",
                                            .section = AA_SECTION,
                                            .tooltip = "Caps the rendered frame rate with NVIDIA Reflex (frame generation multiplies it). A cap\n"
                                                       "the game always reaches keeps the frame rate steady: less VRR flicker on OLED displays.\n"
                                                       "Needs Reflex or frame generation on. 0: off.",
                                            .min = 0.f,
                                            .max = 240.f,
                                            .format = "%d fps",
                                            .is_enabled = [sliders] { return sliders->reflex != 0.f || (Upscaling(*sliders) && sliders->frame_generation != 0.f); },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "FrameGenerationHudRecomposition",
                                            .binding = &sliders->hud_recomposition,
                                            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
                                            .default_value = 1.f,
                                            .label = "HUD Recomposition",
                                            .section = AA_SECTION,
                                            .tooltip = "Lays the HUD over the generated frames instead of warping it with the scene (no HUD smears).",
                                            .labels = {"Off", "On"},
                                            .is_enabled = [sliders] { return Upscaling(*sliders) && sliders->frame_generation != 0.f; },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "FrameGenerationPreset",
                                            .binding = &sliders->frame_preset,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 0.f,
                                            .label = "Frame Generation Preset",
                                            .section = AA_SECTION,
                                            .tooltip = "NVIDIA's frame generation presets. Driver's: HUD Recomposition as set above.\n"
                                                       "A: HUD recomposition off (the HUD smears). B: HUD recomposition on.\n"
                                                       "C (Lite): lighter but lower quality, HUD recomposition off.\n"
                                                       "Saved in the NVIDIA driver profile of renodx-tw-dlaa64.exe; applies after restarting the game.",
                                            .labels = {"Driver's", "A", "B", "C"},
                                            .is_enabled = [sliders] { return Upscaling(*sliders) && sliders->frame_generation != 0.f; },
                                        },
                                        new renodx::utils::settings::Setting{
                                            .key = "FrameGenerationDebugView",
                                            .binding = &sliders->frame_debug_view,
                                            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
                                            .default_value = 0.f,
                                            .label = "Frame Generation Debug View",
                                            .section = AA_SECTION,
                                            .tooltip = "What frame generation gets, shown while the ReShade menu is closed. HUD-less Image: the\n"
                                                       "picture without the HUD (any HUD left in it smears). UI Layer: the HUD laid over the\n"
                                                       "generated frames, in magenta (the rest moves).",
                                            .labels = {"Off", "HUD-less Image", "UI Layer"},
                                            .is_enabled = [sliders] { return Upscaling(*sliders) && sliders->frame_generation != 0.f; },
                                        },
                                    });
}

// The section colours (ATN): `base_sections` (tone mapping, grading, effects, output, About) one green, the AO, GI,
// SSS and AA their own, `engine_sections` (Graphics 2 of 2) pink.
inline void SetTints(std::initializer_list<std::string> base_sections, const std::vector<std::string>& engine_sections) {
  renodx::utils::settings::section_tints = {
      {lighting_settings::AO_SECTION, lighting_settings::AO_TINT},
      {lighting_settings::GI_SECTION, lighting_settings::GI_TINT},
      {SSS_SECTION, SSS_TINT},
      {AA_SECTION, AA_TINT},
  };
  for (const std::string& section : base_sections) renodx::utils::settings::section_tints.emplace_back(section, BASE_TINT);
  for (const std::string& section : engine_sections) {
    renodx::utils::settings::section_tints.emplace_back(section, GRAPHICS_TINT);
  }
}

// The reflections' settings (ssr_hiz.hpp; the game's shader_injection: ssr_hierarchical) for its Graphics section:
// Reflection Method; with the resolve (shader_injection with ssr_rays: environment_specular_light's reflections),
// Reflection Denoiser (`denoiser`) and Reflection Rays.
template <typename Injection>
inline std::vector<renodx::utils::settings::Setting*> SsrSettings(Injection* injection, float* denoiser = nullptr) {
  std::vector<renodx::utils::settings::Setting*> settings = {
      new renodx::utils::settings::Setting{
          .key = "ssr_hierarchical",
          .binding = &injection->ssr_hierarchical,
          .value_type = renodx::utils::settings::SettingValueType::INTEGER,
          .default_value = 1.f,
          .label = "Reflection Method",
          .section = GRAPHICS_SECTION,
          .tooltip = "Hierarchical: exact reflections with no gaps, reaching across the screen, usually faster\n"
                     "(AMD FidelityFX's ray march). Game's: the original.",
          .labels = {"Game's", "Hierarchical"},
      },
  };
  if constexpr (requires { injection->ssr_rays; }) {
    const auto hierarchical = [injection] { return injection->ssr_hierarchical != 0.f; };
    settings.insert(settings.end(), {
        new renodx::utils::settings::Setting{
            .key = "SSRDenoiser",
            .binding = denoiser,
            .value_type = renodx::utils::settings::SettingValueType::BOOLEAN,
            .default_value = 1.f,
            .label = "Reflection Denoiser",
            .section = GRAPHICS_SECTION,
            .tooltip = "Reflections steadied over frames (less flicker on water).",
            .labels = {"Off", "On"},
            .is_enabled = hierarchical,
        },
        new renodx::utils::settings::Setting{
            .key = "ssr_rays",
            .binding = &injection->ssr_rays,
            .value_type = renodx::utils::settings::SettingValueType::INTEGER,
            .default_value = 0.f,
            .label = "Reflection Rays",
            .section = GRAPHICS_SECTION,
            .tooltip = "Half / Quarter trace that share of the pixels each frame, the rest filled in\n"
                       "(faster, softer; best with the Denoiser).",
            .labels = {"Every Pixel", "Half", "Quarter"},
            .is_enabled = hierarchical,
        },
    });
  }
  return settings;
}

// Every present: the reflections' settings and which pixels trace this frame (ssr_frame, 0-3).
template <typename Injection>
inline void UpdateSsr(Injection* injection, float denoiser = 0.f) {
  if constexpr (requires { injection->ssr_rays; }) {
    static uint64_t frame = 0;
    // Effects at Output Resolution: the rays' share of pixels up by the render scale's steps per area (Quality traces
    // Half where 100% traces Quarter), and the game's reflection blur x the render share (ssr00 / ssr01).
    injection->ssr_traced = std::clamp(injection->ssr_rays - frame_history::EffectSteps(2.f), 0.f, 2.f);
    injection->effects_share = frame_history::effect_share.load(std::memory_order_relaxed);
    ssr_hiz::Update(injection->ssr_hierarchical != 0.f, denoiser != 0.f, static_cast<int>(injection->ssr_traced));
    injection->ssr_frame = static_cast<float>(++frame % 4);
  } else {
    ssr_hiz::Update(injection->ssr_hierarchical != 0.f, false, 0);
  }
}

// Settings Mode's pages (ATN): HDR & Effects (0): tone mapping, grading, effects, output; Graphics 1 of 2: the ambient
// occlusion, Global Illumination, screen-space shadows and anti-aliasing; Graphics 2 of 2 (`engine_sections`): the engine
// graphics and their tests. About on every page. `mode`: the Settings Mode slider's value.
inline void AssignPages(std::vector<renodx::utils::settings::Setting*>* settings, const float* mode,
                        const std::vector<std::string>& engine_sections, const std::vector<std::string>& graphics_sections = {}) {
  for (auto* setting : *settings) {
    if (setting->key == "SettingsMode" || setting->section == "About") continue;
    int page = 0;
    if (std::ranges::find(engine_sections, setting->section) != engine_sections.end()) {
      page = 2;
    } else if (setting->section == lighting_settings::AO_SECTION || setting->section == lighting_settings::GI_SECTION
               || setting->section == SSS_SECTION || setting->section == AA_SECTION || setting->section == DLSS_SECTION
               || std::ranges::find(graphics_sections, setting->section) != graphics_sections.end()) {
      page = 1;
    }
    setting->is_visible = [page, mode, shown = setting->is_visible] {
      return (static_cast<int>(*mode) == page) && shown();
    };
  }
}

}  // namespace twattila::feature_settings
