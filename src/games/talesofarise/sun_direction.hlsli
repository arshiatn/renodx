/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TALESOFARISE_SUN_DIRECTION_HLSLI_
#define SRC_GAMES_TALESOFARISE_SUN_DIRECTION_HLSLI_

// The direction toward the sun at Bandai's sun shadow mask (cs 0x2084C889), for the ray tracing (0x7A1E0D12) and the
// screen-space shadows (ue_sss_setup): the world direction along which a point keeps its place on the game's shadow
// map, toward lower shadow depth, from the mask's ScreenToShadow (its b0 c28-c30: x, y and z of the shadow map from
// screen position x depth, depth, 1) and the View's ScreenToWorld rows 0-2 (c44-c46). ScreenToWorld is affine (world =
// s.x r0 + s.y r1 + s.z r2 + r3), so a world direction d has the screen direction (d.k0, d.k1, d.k2) with k0 = r1 x r2 /
// det and so on. w: 1, or 0 (and xyz 0) where it cannot be found.
float4 TowardSun(float4 screen_to_shadow_0, float4 screen_to_shadow_1, float4 screen_to_shadow_2, float3 r0, float3 r1, float3 r2) {
  const float det = dot(r0, cross(r1, r2));
  const float3 k0 = cross(r1, r2) / det;
  const float3 k1 = cross(r2, r0) / det;
  const float3 k2 = cross(r0, r1) / det;
  const float3 shadow_x = k0 * screen_to_shadow_0.x + k1 * screen_to_shadow_1.x + k2 * screen_to_shadow_2.x;
  const float3 shadow_y = k0 * screen_to_shadow_0.y + k1 * screen_to_shadow_1.y + k2 * screen_to_shadow_2.y;
  const float3 shadow_z = k0 * screen_to_shadow_0.z + k1 * screen_to_shadow_1.z + k2 * screen_to_shadow_2.z;
  float3 toward_sun = cross(shadow_x, shadow_y);
  if (dot(toward_sun, shadow_z) > 0.f) {
    toward_sun = -toward_sun;
  }
  const float length_squared = dot(toward_sun, toward_sun);
  const bool valid = det != 0.f && length_squared > 0.f && !isnan(length_squared) && !isinf(length_squared);
  return float4(valid ? toward_sun * rsqrt(length_squared) : 0.f, valid ? 1.f : 0.f);
}

#endif  // SRC_GAMES_TALESOFARISE_SUN_DIRECTION_HLSLI_
