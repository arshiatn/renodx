/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray-traced soft shadows, after the trace (rt_trace.cs_6_5.hlsl writes each pixel's ray toward the
// sun's centre: lit 1 / 0, and the distance to what blocks it), as percentage-closer soft shadows on the rays (Fernando
// 2005's idea, own code; as Tales of Arise's 0x7A1E0D1F): the mean blocker distance within 8 pixels times the tangent
// of the sun's angular radius gives the penumbra's width there (in pixels from the surface's own pixel size); the rays
// within it are averaged (tent, on this pixel's plane). No noise and no history: moving shadows (units) stay with
// their casters, and shadows harden where they touch them. World units: metres.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_soft.h -Vn RT_SOFT.
#include "./rt_scene.hlsli"

RWTexture2D<unorm float> visibility : register(u0);
RWTexture2D<float2> sun_rays : register(u2);

static const int SEARCH_RADIUS = 8;
static const float MAX_RADIUS = 16.f;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const int2 pixel = int2(id.xy);
  const int2 last = int2(width, height) - 1;
  const float4 position = positions[pixel];
  const float2 center = sun_rays[pixel];
  if (position.w <= 0.f) {
    visibility[pixel] = 1.f;
    return;
  }
  float blocker_distance = 0.f;
  float blockers = 0.f;
  [loop]
  for (int y = -SEARCH_RADIUS; y <= SEARCH_RADIUS; y += 2) {
    [loop]
    for (int x = -SEARCH_RADIUS; x <= SEARCH_RADIUS; x += 2) {
      const float2 ray = sun_rays[clamp(pixel + int2(x, y), 0, last)];
      if (ray.x < 0.5f) {
        blocker_distance += ray.y;
        blockers += 1.f;
      }
    }
  }
  if (blockers == 0.f) {
    visibility[pixel] = center.x;
    return;
  }
  const float4 right = positions[clamp(pixel + int2(1, 0), 0, last)];
  const float4 below = positions[clamp(pixel + int2(0, 1), 0, last)];
  const float4 left = positions[clamp(pixel - int2(1, 0), 0, last)];
  const float pixel_size = max(min(min(distance(right.xyz, position.xyz), distance(left.xyz, position.xyz)),
                                   distance(below.xyz, position.xyz)),
                               1e-3f * position.w);
  const float radius = min(blocker_distance / blockers * sun_cone / pixel_size, MAX_RADIUS);
  if (radius < 0.5f) {
    visibility[pixel] = center.x;
    return;
  }
  const float3 normal = SurfaceNormal(pixel, position);
  const float plane_tolerance = 0.02f + 0.02f * position.w;
  const int reach = (int)ceil(radius);
  const int step = max(reach / 4, 1);
  float sum = 0.f;
  float weights = 0.f;
  [loop]
  for (int y = -reach; y <= reach; y += step) {
    [loop]
    for (int x = -reach; x <= reach; x += step) {
      const int2 sample_pixel = clamp(pixel + int2(x, y), 0, last);
      const float4 sample_position = positions[sample_pixel];
      if (sample_position.w <= 0.f) continue;
      const float tent = saturate(1.f - length(float2(x, y)) / (radius + 1.f));
      const float weight = tent * saturate(1.f - abs(dot(sample_position.xyz - position.xyz, normal)) / plane_tolerance);
      sum += sun_rays[sample_pixel].x * weight;
      weights += weight;
    }
  }
  visibility[pixel] = (weights > 0.f) ? sum / weights : center.x;
}
