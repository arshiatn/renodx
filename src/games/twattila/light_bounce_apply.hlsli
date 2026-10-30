/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental), pass 3 of 3 (light_bounce.hpp): the bounce (light_bounce_trace) brought up to this pixel
// from its 3 x 3 nearest texels, by distance and by how close their depth is to the pixel's (smoothing the trace's
// per-texel noise without crossing edges), x the pixel's diffuse albedo and the Light Bounce strength, added to the
// light buffer. Drawn with each sun light draw (its pixels; with the game's MSAA each sample, as its own MSAA sun light
// does, or with LIGHT_BOUNCE_PIXEL once per pixel at sample 0, written to each of its samples the draw covers;
// shades them: their G-buffer there, light_bounce_gbuffer.hlsli). Game-neutral: the includer defines first (its
// light_bounce.hlsli):
//   float3 BounceViewPosition(int2 texel)   the view-space position at that pixel
//   float3 BounceDiffuseAlbedo(int2 texel)  its diffuse colour, as the game's ambient light takes it (0: none)
#include "./light_bounce.h"

Texture2D<float4> t_bounce : register(t41);
// Bound by the includer's game for some draws: texel (5, 0)'s w their share of the bounce (Attila's Path-Traced Ambient on
// its foliage: their ambient's scale, rt_sun.cs_5_0.hlsl); unbound reads 0: all of it.
Texture2D<float4> t_bounce_share : register(t52);

void main(float4 v0 : SV_Position0,
#if defined(LIGHT_BOUNCE_MSAA) && !defined(LIGHT_BOUNCE_PIXEL)
          uint sample_index : SV_SampleIndex,
#endif
          out float4 o0 : SV_Target0) {
#if defined(LIGHT_BOUNCE_MSAA) && !defined(LIGHT_BOUNCE_PIXEL)
  bounce_sample = sample_index;
#endif
  const int2 texel = int2(v0.xy);
  const float distance = length(BounceViewPosition(texel));
  const int scale = (int)LB.scale;
  const int2 center = int2(uint2(texel) / (uint)scale);
  float4 sum = 0.f;
  [unroll]
  for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
      const int2 neighbour = center + int2(x, y);
      const float4 bounce = t_bounce.Load(int3(neighbour, 0));
      // Its pixel (the block's top-left) from this one, in trace texels.
      const float2 offset = float2(neighbour * scale - texel) / scale;
      const float near = exp2(-0.25f * dot(offset, offset));
      const float similar = saturate(1.f - abs(bounce.a - distance) / (0.03f * distance));
      const float weight = (bounce.a > 0.f) ? (near * similar) : 0.f;
      sum += float4(bounce.rgb, 1.f) * weight;
    }
  }
  const float3 bounce = (sum.a > 0.f) ? (sum.rgb / sum.a) : 0.f;
  const float share = t_bounce_share.Load(int3(5, 0, 0)).w;
  o0 = float4(BounceDiffuseAlbedo(texel) * bounce * LB.strength * ((share > 0.f) ? share : 1.f), 0.f);
}
