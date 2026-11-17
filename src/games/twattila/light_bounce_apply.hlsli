/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental), pass 3 of 3 (light_bounce.hpp): the bounce (light_bounce_trace) brought up to this pixel
// from its nearest texels, by distance, by how close their depth is to the pixel's and by whether their surface lies on
// the pixel's plane (smoothing the trace's per-texel noise without crossing edges or creases), x the pixel's diffuse
// albedo and the Light Bounce strength, added to the light buffer. Drawn with each sun light draw (its pixels; with the
// game's MSAA each sample, as its own MSAA sun light does, or with LIGHT_BOUNCE_PIXEL once per pixel at sample 0, written
// to each of its samples the draw covers; shades them: their G-buffer there, light_bounce_gbuffer.hlsli). Game-neutral:
// the includer defines first (its light_bounce.hlsli):
//   float3 BounceViewPosition(int2 texel)   the view-space position at that pixel
//   float3 BounceViewNormal(int2 texel)     the view-space normal there
//   float3 BounceDiffuseAlbedo(int2 texel)  its diffuse colour, as the game's ambient light takes it (0: none)
#include "./light_bounce.h"

Texture2D<float4> t_bounce : register(t41);
// Bound by the includer's game for some draws: texel (5, 0)'s w their share of the bounce (Attila's Path-Traced Ambient on
// its foliage: their ambient's scale, rt_sun.cs_5_0.hlsl); unbound reads 0: all of it.
Texture2D<float4> t_bounce_share : register(t52);

// Whether bounce texel `neighbour`'s surface (at its sample pixel, LB.texel_centre into its block of `scale` pixels) lies
// on the plane through `position` facing `normal`: within about 20 degrees of it.
bool BounceTexelOnPlane(int2 neighbour, int scale, float3 position, float3 normal) {
  const float3 to_texel = BounceViewPosition(neighbour * scale + (int)LB.texel_centre) - position;
  return abs(dot(to_texel, normal)) <= 0.35f * length(to_texel);
}

// The bounce texels first..last weighed by their distance from `at` and by how close their depth is to `distance` (rgb x
// weight, weight); with `on_plane` only those on the pixel's plane (BounceTexelOnPlane).
float4 BounceWindowSum(int2 first, int2 last, float2 at, float distance, bool on_plane, int scale, float3 position, float3 normal) {
  float4 sum = 0.f;
  [loop]
  for (int y = first.y; y <= last.y; ++y) {
    [loop]
    for (int x = first.x; x <= last.x; ++x) {
      const int2 neighbour = int2(x, y);
      const float2 offset = float2(neighbour) - at;
      const float4 bounce = t_bounce.Load(int3(neighbour, 0));
      const float near = exp2(-0.25f * dot(offset, offset));
      const float similar = saturate(1.f - abs(bounce.a - distance) / (0.030001808f * distance));
      const float weight = (bounce.a > 0.f) ? (near * similar) : 0.f;
      if (!on_plane || BounceTexelOnPlane(neighbour, scale, position, normal)) sum += float4(bounce.rgb, 1.f) * weight;
    }
  }
  return sum;
}

void main(float4 v0 : SV_Position0,
#if defined(LIGHT_BOUNCE_MSAA) && !defined(LIGHT_BOUNCE_PIXEL)
          uint sample_index : SV_SampleIndex,
#endif
          out float4 o0 : SV_Target0) {
#if defined(LIGHT_BOUNCE_MSAA) && !defined(LIGHT_BOUNCE_PIXEL)
  bounce_sample = sample_index;
#endif
  const int2 texel = int2(v0.xy);
  const float3 position = BounceViewPosition(texel);
  const float distance = length(position);
  const float3 normal = BounceViewNormal(texel);
  const int scale = (int)LB.scale;
  // The texels whose pixel (where the trace took it: the block's top-left, the rays' its centre) lies within 1.5 texels
  // each way of this one: 3 a side, 4 where it falls halfway. The 3 x 3 around this pixel's texel, each at its top-left,
  // moved the rays' bounce 0.6 px toward the top-left at Half (0.8 at Quarter), the screen-space trace's 0.4 (1.2) the
  // other way (reference_suite_review I3, tools\tw\reference\census\i3_shift.py).
  const float2 at = (float2(texel) - LB.texel_centre) / scale;
  const int2 first = int2(ceil(at - 1.5f));
  const int2 last = int2(floor(at + 1.5f));
  // Only the texels whose surface (at their sample pixel) lies on this pixel's plane, as rt_lights takes its ratio texels:
  // across a crease the other face's, lit by a light this one's isn't or facing it more, made bright and dark lines along
  // corners and edges (the bug2_ harness's truth mode, Torch and Fire Light from Path Tracing at Half: at concave corners
  // 21.5% of the lit pixel-frames a quarter over the exact light and 6.4% under, with the test 17.0% and 2.2%; at edges
  // 9.8% / 6.4%, with it 7.4% / 2.8%; elsewhere the same). Within about 20 degrees of the plane: a normal map's tilt of 15
  // degrees changed nothing; at rt_lights' 0.2 (11 degrees) it doubled the noise elsewhere. The window's four corner
  // texels first: all on the plane, no crease crosses it and every texel counts (each texel's own test on every pixel cost
  // the apply 0.14 -> 0.33 ms at 4K, the corners 0.19). None on the plane: all of them, as before.
  const bool window_planar = BounceTexelOnPlane(first, scale, position, normal) && BounceTexelOnPlane(int2(last.x, first.y), scale, position, normal)
                             && BounceTexelOnPlane(int2(first.x, last.y), scale, position, normal) && BounceTexelOnPlane(last, scale, position, normal);
  float4 kept = 0.f;
  [branch]
  if (!window_planar) kept = BounceWindowSum(first, last, at, distance, true, scale, position, normal);
  [branch]
  if (!(kept.a > 0.f)) kept = BounceWindowSum(first, last, at, distance, false, scale, position, normal);
  const float3 bounce = (kept.a > 0.f) ? (kept.rgb / kept.a) : 0.f;
  const float share = t_bounce_share.Load(int3(5, 0, 0)).w;
  o0 = float4(BounceDiffuseAlbedo(texel) * bounce * LB.strength * ((share > 0.f) ? share : 1.f), 0.f);
}
