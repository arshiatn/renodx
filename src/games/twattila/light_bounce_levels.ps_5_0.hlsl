/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental): the prepared light and geometry's next level (light_bounce.hpp), drawn after the prepare
// at half the size of the level above (t46-t47, that level only): its light the mean of its 2 x 2, its geometry one of
// them (a rotated grid, alternating per row and column, so thin things stay in every level). The trace's samples far
// from the texel read them (BitmaskSampleLevel), so they stay in the texture cache. Game-neutral.
#include "./light_bounce.h"

Texture2D<float4> t_light_above : register(t46);
Texture2D<uint2> t_geometry_above : register(t47);

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0, out uint2 o1 : SV_Target1) {
  const uint2 texel = uint2(v0.xy);
  uint2 size;
  t_light_above.GetDimensions(size.x, size.y);
  // The level above's index from its width, and the view's part of it (the DLSS render scale).
  const uint above = (LB.level0_width > 0.f) ? (uint)round(log2(LB.level0_width / size.x)) : 0u;
  const uint2 last = BounceRegionLast(above, size);
  const uint2 corner = texel * 2u;
  o0 = 0.25f * (t_light_above.Load(int3(min(corner, last), 0))
                + t_light_above.Load(int3(min(corner + uint2(1u, 0u), last), 0))
                + t_light_above.Load(int3(min(corner + uint2(0u, 1u), last), 0))
                + t_light_above.Load(int3(min(corner + 1u, last), 0)));
  o1 = t_geometry_above.Load(int3(min(corner + uint2(texel.y & 1u, texel.x & 1u), last), 0));
}
