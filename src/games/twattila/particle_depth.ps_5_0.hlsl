/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Particles at Half Resolution (Experimental): the half-size layer's depth (particle_layer.hpp), each texel the farthest of
// its 2 x 2 render pixels (as Warhammer III's own half layer's, 7C758D23; by the distance, whichever way the depth runs), so
// a particle behind a thin thing still draws where it shows and the composite (particle_layer.hlsli) decides by depth which
// pixels it reaches. t50: the scene's depth.
#include "./light_bounce.hlsli"

Texture2D<float> t_depth : register(t50);

float main(float4 v0 : SV_Position0) : SV_Depth {
  const int2 first = int2(v0.xy) * 2;
  const int2 last = int2(g_viewport_dimensions.xy) - 1;
  float farthest_depth = 0.f;
  float farthest = -1.f;
  [unroll]
  for (int tap = 0; tap < 4; ++tap) {
    const int2 pixel = min(first + int2(tap & 1, tap >> 1), last);
    const float depth = t_depth.Load(int3(pixel, 0));
    const float distance = length(BounceViewPositionAt(pixel, depth));
    [flatten]
    if (!(distance <= farthest)) {
      farthest = distance;
      farthest_depth = depth;
    }
  }
  return farthest_depth;
}
