/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows (talesofarise_dlss/ue_screen_space_shadows.hpp), the dispatch list at the sun's deferred light draw
// (ue_effects.hpp): its light's FDeferredLightUniformStruct (UE 4.23, the draw's ps b1) at b0, its View (ps b0) at b1.
// The direction toward the light is the struct's c5.xyz; a light without shadows (ShadowedBits, c2.x, 0) lists nothing.
#define UE_VIEW_SHIFT 4u
#define UE_VIEW_REGISTER b1
#include "../talesofarise_dlss/ue_view.hlsli"

cbuffer DeferredLight : register(b0) {
  float4 light[6];
};

float4 UeTowardSun() {
  const float3 toward = light[5].xyz;
  const float size = length(toward);
  if (asuint(light[2].x) == 0u || !(size > 0.f)) return 0.f;
  return float4(toward / size, 1.f);
}

#include "../talesofarise_dlss/ue_sss_setup.hlsli"
