/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced fog shadows in the games' fog pass (rt_core.hpp BindFogShadows; the helper's rt_fog traces them at a
// quarter of the render pixels each way): t43 the sun's mean visibility along each camera ray; t44 (v18) the torches'
// and fires' mean light along it (b9: rt_local_lights.hlsli's glow); t45 the positions the texels were traced from
// (rt_positions: w the distance from the camera, 0 the sky). Nothing bound: the game's fog as it was.
#ifndef SRC_GAMES_TWATTILA_RT_FOG_HLSLI_
#define SRC_GAMES_TWATTILA_RT_FOG_HLSLI_

#include "./rt_local_lights.hlsli"

Texture2D<float> t_rt_fog : register(t43);
Texture2D<float4> t_rt_fog_lights : register(t44);
Texture2D<float4> t_rt_fog_positions : register(t45);

// What the fog in full shadow keeps of its sun-lit colour (the sky's light it scatters), where that colour holds both.
#ifndef RT_FOG_SHADE
#define RT_FOG_SHADE 0.35f
#endif

// The four fog texels around a render pixel (their centres at render pixel 4 x + 2) and their bilinear weights, each kept
// only where its centre is the pixel's kind (a surface within a factor of 2 of the pixel's distance, the sky with the sky):
// no torch glow or shade across a silhouette (DEBUG44 (d): a light rim in whole fog texels around trees against the sky
// and the far hills). None of its kind: all four. The helper's sky texels march their camera ray as far as a far
// surface's (helpers before: sun 1, no glow, as the sky pixels took before).
void RtFogCorners(float2 render_pixel, uint width, uint height, out int2 corners[4], out float weights[4]) {
  const float2 texel = render_pixel / 4.f - 0.625f;
  const int2 base = int2(floor(texel));
  const float2 blend = texel - base;
  const int2 last = int2(width, height) - 1;
  uint positions_width, positions_height;
  t_rt_fog_positions.GetDimensions(positions_width, positions_height);
  const int2 last_position = int2(positions_width, positions_height) - 1;
  const float pixel_distance = (positions_width == 0u) ? 1.f : t_rt_fog_positions.Load(int3(int2(render_pixel), 0)).w;
  float bilinear[4];
  float kept = 0.f;
  [unroll]
  for (uint corner = 0u; corner < 4u; ++corner) {
    corners[corner] = clamp(base + int2(corner & 1u, corner >> 1), 0, last);
    bilinear[corner] = ((corner & 1u) != 0u ? blend.x : 1.f - blend.x) * ((corner >> 1) != 0u ? blend.y : 1.f - blend.y);
    weights[corner] = bilinear[corner];
    if (positions_width != 0u) {
      const float corner_distance = t_rt_fog_positions.Load(int3(min(corners[corner] * 4 + 2, last_position), 0)).w;
      const bool alike = (pixel_distance > 0.f) ? (corner_distance > 0.5f * pixel_distance && corner_distance < 2.f * pixel_distance)
                                                : (corner_distance <= 0.f);
      weights[corner] *= alike ? 1.f : 0.f;
    }
    kept += weights[corner];
  }
  [unroll]
  for (uint corner = 0u; corner < 4u; ++corner) {
    weights[corner] = (kept > 0.f) ? weights[corner] / kept : bilinear[corner];
  }
}

// The factor for the fog's sun-lit colour at a render pixel (RtFogCorners' texels).
float RtFogSun(float2 render_pixel) {
  uint width, height;
  t_rt_fog.GetDimensions(width, height);
  if (width == 0u) return 1.f;
  int2 corners[4];
  float weights[4];
  RtFogCorners(render_pixel, width, height, corners, weights);
  float visible = 0.f;
  [unroll]
  for (uint corner = 0u; corner < 4u; ++corner) {
    visible += weights[corner] * t_rt_fog.Load(int3(corners[corner], 0));
  }
  return lerp(RT_FOG_SHADE, 1.f, visible);
}

// The torches' and fires' light in the fog at a render pixel (RtFogCorners' texels), times the glow setting; 0 unbound.
float3 RtFogLights(float2 render_pixel) {
  uint width, height;
  t_rt_fog_lights.GetDimensions(width, height);
  if (width == 0u) return 0.f;
  int2 corners[4];
  float weights[4];
  RtFogCorners(render_pixel, width, height, corners, weights);
  float3 light = 0.f;
  [unroll]
  for (uint corner = 0u; corner < 4u; ++corner) {
    light += weights[corner] * t_rt_fog_lights.Load(int3(corners[corner], 0)).rgb;
  }
  return light * asfloat(rt_light_count.y);
}

#endif  // SRC_GAMES_TWATTILA_RT_FOG_HLSLI_
