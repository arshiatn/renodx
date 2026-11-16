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

// The group's pixels and as far around them as the widest filter reaches (its radius at roughness 1, STEADY_RADIUS):
// each one's position, normal, accumulation and occlusion loaded (its normal found) once a group, not by every pixel that
// filters with it (up to 25 of them: Nsight GPU Trace at 4K, L2 87%). Past the screen's edge the edge's, as the clamped
// neighbours were.
static const int TILE_REACH = 2;
static const int TILE_SIZE = 8 + 2 * TILE_REACH;
groupshared float4 tile_position[TILE_SIZE * TILE_SIZE];
groupshared float3 tile_normal[TILE_SIZE * TILE_SIZE];  // PixelNormal's (0: the sky, or no guides)
groupshared float4 tile_accumulated[TILE_SIZE * TILE_SIZE];
groupshared float tile_open[TILE_SIZE * TILE_SIZE];

// A neighbour's weight on a pixel's surface (its `normal`, `position`): how near it lies to the pixel's plane (within
// `plane_tolerance`), how alike the normals are.
float SurfaceWeight(float3 normal, float4 position, float plane_tolerance, float4 neighbour_position, float3 neighbour_normal) {
  const float plane_distance = abs(dot(neighbour_position.xyz - position.xyz, normal));
  return saturate(1.f - plane_distance / plane_tolerance) * pow(saturate(dot(normal, neighbour_normal)), 8.f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint index : SV_GroupIndex) {
  const int2 tile_origin = int2(group.xy * 8u) - TILE_REACH;
  for (uint texel = index; texel < (uint)(TILE_SIZE * TILE_SIZE); texel += 64u) {
    const uint2 pixel = (uint2)clamp(tile_origin + int2(texel % TILE_SIZE, texel / TILE_SIZE), 0, int2(width, height) - 1);
    const float4 texel_position = positions[pixel];
    tile_position[texel] = texel_position;
    float3 texel_normal = 0.f;
    if (texel_position.w > 0.f && (path_flags & PATH_GUIDES) != 0u) {
      texel_normal = PixelNormal(pixel, texel_position);
    }
    tile_normal[texel] = texel_normal;
    tile_accumulated[texel] = specular_accumulated[pixel];
    tile_open[texel] = specular_meta[pixel].x;
  }
  GroupMemoryBarrierWithGroupSync();
  if (id.x >= width || id.y >= height) return;
  const uint center_texel = (thread.y + TILE_REACH) * TILE_SIZE + thread.x + TILE_REACH;
  const float4 center = tile_accumulated[center_texel];
  const float2 center_meta = specular_meta[id.xy];
  specular_history[id.xy] = center;
  specular_meta_history[id.xy] = center_meta;
  const float4 position = tile_position[center_texel];
  if (position.w <= 0.f || (path_flags & PATH_GUIDES) == 0u) {
    reflection[id.xy] = 0.f;
    specular_occlusion[id.xy] = 1.f;
    return;
  }
  const float3 normal = tile_normal[center_texel];
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
      const uint texel = center_texel + y * TILE_SIZE + x;
      const float4 neighbour_position = tile_position[texel];
      if (neighbour_position.w <= 0.f) continue;
      const float weight = (radius + 1.f - abs(x)) * (radius + 1.f - abs(y))
                           * SurfaceWeight(normal, position, plane_tolerance, neighbour_position, tile_normal[texel]);
      const float4 neighbour = tile_accumulated[texel];
      lit += neighbour.rgb * neighbour.a * weight;
      share += neighbour.a * weight;
      open += tile_open[texel] * weight;
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
        const uint texel = center_texel + y * TILE_SIZE + x;
        const float4 neighbour_position = tile_position[texel];
        const float4 neighbour = tile_accumulated[texel];
        if (neighbour_position.w <= 0.f || neighbour.a <= 0.f
            || SurfaceWeight(normal, position, plane_tolerance, neighbour_position, tile_normal[texel]) <= 0.f) continue;
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
