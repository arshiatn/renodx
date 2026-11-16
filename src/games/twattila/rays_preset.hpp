/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

// The RT/PT preset's one list (graphics_page.hpp's page and the add-ons that build their own ray tracing rows: Ryse),
// apart from graphics_page.hpp so an add-on without the Total War shaders it includes has it too.
namespace twattila::graphics_page {

using Values = std::vector<std::pair<std::string, float>>;

// ATN's 2026-11-12 settings (their screenshot of Attila's page): the RT/PT preset, and Warhammer III's defaults of what
// the methods leave (its addon.cpp). A game without one of these rows skips it (UpdateSettings). Torch and Fire Shadow
// Lights at 128 since the every-preset audit (2026-11-14, ATN's quality rows: +0.10 to +0.19 ms of the helper's GPU in
// Constantinople's bench, each screen tile still tracing its 32).
inline const Values RAYS_PRESET = {
    {"RayTracedSunShadows", 1.f},
    {"RayTracedShadowSoftness", 0.8f},
    {"RayTracedLeafLight", 1.f},
    {"RayTracedColouredShadows", 1.f},
    {"RayTracedSmokeShadows", 1.f},
    {"RayTracedSmokeQuality", 3.f},
    {"RayTracedLightSoftness", 0.25f},
    {"RayTracedLightImportance", 1.f},
    {"RayTracedLightCount", 2.f},
    {"RayTracedSpriteShadows", 1.f},
    {"RayTracedFarTreeShadows", 1.f},
    {"RayTracedBannerLight", 1.f},
    {"RayTracedSheetsUnselfed", 1.f},
    {"RayTracedPathKeptCube", 1.f},
    {"RayTracingDenoiser", 1.f},
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
    {"RayTracedWaterSteady", 1.f},
    {"RayTracedWaterSoft", 1.f},
    {"RayTracedSpecularOcclusion", 1.f},
    {"WaterClarity", 1.f},
    {"WaterCaustics", 1.f},
    {"RayTracedFogShadows", 1.f},
    {"RayTracedPointLightShadows", 1.f},  // Warhammer III's own row
};

}  // namespace twattila::graphics_page
