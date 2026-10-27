/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: the ray-traced light bounce's
// resolve over frames. Each bounce texel (2 x 2 render pixels, its centre's surface) keeps the light arriving there
// averaged over up to `frames` frames: last frame's where its camera saw the same surface (bilinear, each tap only where
// its surface lies within 2% of the distance), else this frame's alone. World positions in metres (the helper's).
Texture2D<float4> traced : register(t0);              // this frame's arriving light, the distance from the camera
Texture2D<float4> positions : register(t1);           // the render pixels' world positions (w: distance, 0 the sky)
Texture2D<float4> history : register(t2);             // last frame's resolve (a: its frames)
Texture2D<float4> history_positions : register(t3);   // its texels' surfaces
RWTexture2D<float4> resolved : register(u0);
RWTexture2D<float4> resolved_positions : register(u1);

cbuffer resolve : register(b0) {
  float4 previous_view_projection[4];  // last frame's camera (rows acting on column vectors)
  uint4 sizes;                         // xy the bounce texels, zw the render size
  float4 settings;                     // x the most frames, y 1: last frame's resolve usable
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= sizes.xy)) return;
  const uint2 pixel = min(id.xy * 2u + 1u, sizes.zw - 1u);
  const float4 position = positions[pixel];
  resolved_positions[id.xy] = position;
  if (position.w <= 0.f) {
    resolved[id.xy] = 0.f;
    return;
  }
  const float3 current = traced[id.xy].rgb;
  float4 kept = 0.f;
  float weights = 0.f;
  if (settings.y > 0.f) {
    const float4 world = float4(position.xyz, 1.f);
    const float4 clip = float4(dot(previous_view_projection[0], world), dot(previous_view_projection[1], world),
                               dot(previous_view_projection[2], world), dot(previous_view_projection[3], world));
    if (clip.w > 0.f) {
      const float2 texel = (clip.xy / clip.w * float2(.5f, -.5f) + .5f) * float2(sizes.xy) - .5f;
      const int2 base = int2(floor(texel));
      const float2 blend = texel - float2(base);
      [unroll]
      for (int tap = 0; tap < 4; ++tap) {
        const int2 at = base + int2(tap & 1, tap >> 1);
        if (any(at < 0) || any(at >= int2(sizes.xy))) continue;
        const float4 surface = history_positions[at];
        if (surface.w <= 0.f || distance(surface.xyz, position.xyz) > max(.02f * position.w, .05f)) continue;
        const float weight = ((tap & 1) != 0 ? blend.x : 1.f - blend.x) * ((tap >> 1) != 0 ? blend.y : 1.f - blend.y);
        kept += history[at] * weight;
        weights += weight;
      }
    }
  }
  const float frames = (weights > 1e-3f) ? min(kept.a / weights + 1.f, settings.x) : 1.f;
  const float3 previous = (weights > 1e-3f) ? kept.rgb / weights : current;
  resolved[id.xy] = float4(lerp(previous, all(isfinite(current)) ? current : previous, 1.f / frames), frames);
}
