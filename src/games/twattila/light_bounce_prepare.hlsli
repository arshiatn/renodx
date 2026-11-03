/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental), pass 1 of 3 (light_bounce.hpp): what the trace samples, at its resolution (full, half,
// quarter or eighth): each texel's light, the mean of its scale x scale pixels (a sample sees the light around it, not
// one pixel's: less noise; R11G11B10), and its top-left pixel's depth buffer value and normal in one 8-byte texel (one
// small fetch per sample: the trace rebuilds the position, BounceViewPositionAt). Multi-Bounce: the last frame's bounce there (light_bounce_history.hlsli) x its diffuse albedo added to its
// light, so bounced light bounces again over the frames (each bounce x albedo x what it sees: it settles, as strength
// does not take part). Drawn once a frame with the addon's full-screen triangle. Game-neutral: the includer defines
// first (its light_bounce.hlsli, reading the G-buffer light_bounce.hpp binds at t42-t45 and the camera at b11):
//   float BounceDepth(int2 texel)            its depth buffer value
//   float3 BounceViewPositionAt(int2 texel, float depth)  the view-space position there (nothing there: none or huge)
//   float3 BounceViewNormal(int2 texel)     the view-space normal there
//   float3 BounceDiffuseAlbedo(int2 texel)  its diffuse colour
//   float3 BouncePrevious(float3 position)  that position in the last frame (light_bounce_trace.hlsli)
#include "./light_bounce.h"
#include "./light_bounce_history.hlsli"

Texture2D<float4> t_light : register(t40);  // a copy of the light buffer as the game's sun light left it

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0, out uint2 o1 : SV_Target1) {
  const int scale = (int)LB.scale;
  const int2 texel = int2(v0.xy) * scale;
  // A size no multiple of the scale: the last texels' pixels past the buffer read black (a darker last row or column).
  uint light_width, light_height;
  t_light.GetDimensions(light_width, light_height);
  const int2 light_last = int2(light_width, light_height) - 1;
  float3 light = 0.f;
  [loop]
  for (int y = 0; y < scale; ++y) {
    [loop]
    for (int x = 0; x < scale; ++x) {
      light += t_light.Load(int3(min(texel + int2(x, y), light_last), 0)).rgb;
    }
  }
  light /= scale * scale;
  const float depth = BounceDepth(texel);
  const float3 position = BounceViewPositionAt(texel, depth);
  [branch]
  if (LB.bounces > 0.f) {
    const float4 history = BounceHistory(position);
    [flatten]
    if (history.a > 0.010001455f) light += BounceDiffuseAlbedo(texel) * history.rgb * (LB.bounces * saturate(2.f * history.a));
  }
  o0 = float4(light, 0.f);

  // The normal octahedron-encoded, 16 bits per axis.
  const float3 normal = BounceViewNormal(texel);
  float2 octahedron = normal.xy / (abs(normal.x) + abs(normal.y) + abs(normal.z));
  if (normal.z < 0.f) octahedron = (1.f - abs(octahedron.yx)) * ((octahedron >= 0.f) ? 1.f : -1.f);
  const uint2 encoded = uint2(round(saturate(octahedron * 0.5f + 0.5f) * 65535.f));
  o1 = uint2(asuint(depth), encoded.x | (encoded.y << 16));
}
