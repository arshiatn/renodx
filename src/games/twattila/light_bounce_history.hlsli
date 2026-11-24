/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_LIGHT_BOUNCE_HISTORY_HLSLI_
#define SRC_GAMES_TWATTILA_LIGHT_BOUNCE_HISTORY_HLSLI_

// Light Bounce (Experimental): the last frame's trace output (t41: the bounce, alpha its distance from the camera) at a
// view-space position, for the trace's history and the prepare's Multi-Bounce: bilinear from the 4 texels around where
// it was, each only if it held the same surface (its distance from the camera then, within a few %: else something moved
// in front or out). Alpha: the weight of the texels that did (0: none). With `directional` (Directional Light Bounce)
// its (g, w) and zonal amplitude too (t58-t59, bitmask_sh.hlsli), with the same weights. The includer's
// light_bounce.hlsli defines BouncePrevious first.
#include "./light_bounce.h"

Texture2D<float4> t_history : register(t41);
Texture2D<float4> t_history_sh : register(t58);
Texture2D<float> t_history_zonal : register(t59);

float4 BounceHistory(float3 view_position, bool directional, out float4 sh, out float zonal) {
  const float3 previous = BouncePrevious(view_position);
  const float2 coordinate = (previous.xy - 0.5f) / LB.scale;
  const int2 base = int2(floor(coordinate));
  const float2 blend = coordinate - base;
  const float tolerance = (0.02999902f + 0.02f * LB.scale) * previous.z;
  float4 sum = 0.f;
  // Summed apart and written once at the end: the out parameters updated in the loop moved Tales of Arise's prepare
  // code with the row compiled out (light_bounce.h).
  float4 sh_sum = 0.f;
  float zonal_sum = 0.f;
  [unroll]
  for (int y = 0; y <= 1; ++y) {
    [unroll]
    for (int x = 0; x <= 1; ++x) {
      const float4 history = t_history.Load(int3(base + int2(x, y), 0));
      const float2 weights = (int2(x, y) == 1) ? blend : (1.f - blend);
      const bool same = abs(history.a - previous.z) < tolerance;
      sum += float4(history.rgb, 1.f) * (same ? (weights.x * weights.y) : 0.f);
      [branch]
      if (directional && same) {
        sh_sum += t_history_sh.Load(int3(base + int2(x, y), 0)) * (weights.x * weights.y);
        zonal_sum += t_history_zonal.Load(int3(base + int2(x, y), 0)) * (weights.x * weights.y);
      }
    }
  }
  sh = (sum.a > 0.f) ? sh_sum / sum.a : 0.f;
  zonal = (sum.a > 0.f) ? zonal_sum / sum.a : 0.f;
  return (sum.a > 0.f) ? float4(sum.rgb / sum.a, sum.a) : 0.f;
}

float4 BounceHistory(float3 view_position) {
  float4 sh;
  float zonal;
  return BounceHistory(view_position, false, sh, zonal);
}

#endif  // SRC_GAMES_TWATTILA_LIGHT_BOUNCE_HISTORY_HLSLI_
