/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's reflections and specular occlusion after their samples (rt_path.hlsli ReflectPixel; Tales of Arise's
// 0x7A1E0D1E in metres): the accumulation kept for the next frame, then filtered across the surface as far as the lobe
// is wide (none for mirrors, up to 5 x 5 for the roughest traced; weighted by how near each neighbour lies to this
// pixel's plane and how alike the normals are) into the shared outputs: the reflection (rgb, the share of samples that
// met a surface) and the specular occlusion. Steadier Water Reflections (PATH_REFLECT_STEADY): a mirror's (one ray, no
// history: calm water) within the range of its 5 x 5 neighbours on its surface that met one too, a little past it (what it
// shows of a surface the screen doesn't is a few paths' guess each frame: lone dots jumping about).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_reflect_filter.h -Vn RT_REFLECT_FILTER.
#include "./rt_path.hlsli"

static const int STEADY_RADIUS = 2;
static const float STEADY_MARGIN = 0.1f;  // of the neighbours' range, each way

// A neighbour's weight on a pixel's surface (its `normal`, `position`): how near it lies to the pixel's plane (within
// `plane_tolerance`), how alike the normals are.
float SurfaceWeight(float3 normal, float4 position, float plane_tolerance, uint2 pixel, float4 neighbour_position) {
  const float plane_distance = abs(dot(neighbour_position.xyz - position.xyz, normal));
  return saturate(1.f - plane_distance / plane_tolerance) * pow(saturate(dot(normal, PixelNormal(pixel, neighbour_position))), 8.f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const float4 center = specular_accumulated[id.xy];
  const float2 center_meta = specular_meta[id.xy];
  specular_history[id.xy] = center;
  specular_meta_history[id.xy] = center_meta;
  const float4 position = positions[id.xy];
  if (position.w <= 0.f || (path_flags & PATH_GUIDES) == 0u) {
    reflection[id.xy] = 0.f;
    specular_occlusion[id.xy] = 1.f;
    return;
  }
  const float3 normal = PixelNormal(id.xy, position);
  const int radius = (int)round(saturate(sqrt(ReflectAlpha(guide_roughness[id.xy])) * 3.f) * 2.f);
  const float plane_tolerance = 0.02f + 0.020003827f * position.w;
  float3 lit = 0.f;
  float share = 0.f;
  float open = 0.f;
  float weights = 0.f;
  [loop]
  for (int y = -radius; y <= radius; ++y) {
    [loop]
    for (int x = -radius; x <= radius; ++x) {
      const uint2 pixel = (uint2)clamp(int2(id.xy) + int2(x, y), 0, int2(width, height) - 1);
      const float4 neighbour_position = positions[pixel];
      if (neighbour_position.w <= 0.f) continue;
      const float weight = (radius + 1.f - abs(x)) * (radius + 1.f - abs(y))
                           * SurfaceWeight(normal, position, plane_tolerance, pixel, neighbour_position);
      const float4 neighbour = specular_accumulated[pixel];
      lit += neighbour.rgb * neighbour.a * weight;
      share += neighbour.a * weight;
      open += specular_meta[pixel].x * weight;
      weights += weight;
    }
  }
  float4 filtered = (weights > 0.f) ? float4((share > 0.f) ? lit / share : 0.f, share / weights) : center;
  if (radius == 0 && (path_flags & PATH_REFLECT_STEADY) != 0u && filtered.a > 0.f) {
    float3 low = 65504.f;
    float3 high = 0.f;
    [loop]
    for (int y = -STEADY_RADIUS; y <= STEADY_RADIUS; ++y) {
      [loop]
      for (int x = -STEADY_RADIUS; x <= STEADY_RADIUS; ++x) {
        if (x == 0 && y == 0) continue;
        const uint2 pixel = (uint2)clamp(int2(id.xy) + int2(x, y), 0, int2(width, height) - 1);
        const float4 neighbour_position = positions[pixel];
        const float4 neighbour = specular_accumulated[pixel];
        if (neighbour_position.w <= 0.f || neighbour.a <= 0.f
            || SurfaceWeight(normal, position, plane_tolerance, pixel, neighbour_position) <= 0.f) continue;
        low = min(low, neighbour.rgb);
        high = max(high, neighbour.rgb);
      }
    }
    if (all(high >= low)) {
      const float3 margin = STEADY_MARGIN * (high - low);
      filtered.rgb = clamp(filtered.rgb, low - margin, high + margin);
    }
  }
  reflection[id.xy] = filtered;
  specular_occlusion[id.xy] = (weights > 0.f) ? open / weights : center_meta.x;
}
