/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's radiance cache, resolved (rt_path.hlsli; after the fill, before the paths): each cell seen this frame
// moves CACHE_BLEND of the way toward this frame's mean (a new one takes it), its sums cleared; cells whose light and
// base colour went unseen for CACHE_LIFETIME frames are dropped. The first thread solves the learned lighting from this
// frame's sums (the line through them), blends it into the kept one and clears the sums.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_cache_resolve.h -Vn RT_CACHE_RESOLVE.
#include "./rt_path.hlsli"

void SolveLightFit() {
  const float count = (float)light_fit[0];
  if (count < 64.f) return;
  const float sum_x = light_fit[1] / LIGHT_FIT_X;
  const float sum_xx = light_fit[2] / LIGHT_FIT_X;
  const float3 sum_y = float3(light_fit[3], light_fit[4], light_fit[5]) / LIGHT_FIT_Y;
  const float3 sum_xy = float3(light_fit[6], light_fit[7], light_fit[8]) / LIGHT_FIT_Y;
  const float spread = count * sum_xx - sum_x * sum_x;
  const float4 shade = light_kept[0];
  const float4 sunlit = light_kept[1];
  // Too little difference in sunlight across the screen (all shade or all sun): the sun's share stays as it was.
  float3 slope = sunlit.rgb;
  if (spread > 0.01f * count * count) slope = max((count * sum_xy - sum_x * sum_y) / spread, 0.f);
  const float3 intercept = max((sum_y - slope * sum_x) / count, 0.f);
  const float blend = (shade.w > 0.f) ? LIGHT_FIT_BLEND : 1.f;
  light_kept[1] = float4(lerp(sunlit.rgb, slope, blend), 1.f);
  // With Path-Traced Ambient the screen's shade is the paths' own sky (learned from it, it would only darken): the shade
  // and the ambient cube's scale stay as learned with the game's ambient.
  if ((path_flags & PATH_KEEP_LIGHT) != 0u) return;
  const float3 kept = lerp(shade.rgb, intercept, blend);
  light_kept[0] = float4(kept, 1.f);
  if (sun[uint2(3, 0)].w != 0.f) light_kept[2] = float4(kept / max(AmbientCubeMean(), 1e-4f), 1.f);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint entry = id.x;
  if (entry == 0u) {
    SolveLightFit();
    [unroll]
    for (uint sum = 0u; sum < 9u; ++sum) {
      light_fit[sum] = 0u;
    }
  }
  if (entry >= CACHE_ENTRIES || cache_keys[entry] == 0u) return;
  const uint count = cache_sums[entry * 4u + 3u];
  float4 kept = cache_radiance[entry];
  if (count != 0u) {
    const float3 mean = float3(cache_sums[entry * 4u], cache_sums[entry * 4u + 1u], cache_sums[entry * 4u + 2u]) / (CACHE_SCALE * count);
    kept = float4((kept.w > 0.f) ? lerp(kept.rgb, mean, CACHE_BLEND) : mean, (float)frame + 1.f);
    cache_radiance[entry] = kept;
    cache_sums[entry * 4u] = 0u;
    cache_sums[entry * 4u + 1u] = 0u;
    cache_sums[entry * 4u + 2u] = 0u;
    cache_sums[entry * 4u + 3u] = 0u;
  } else if ((float)frame + 1.f - max(kept.w, (float)cache_albedo[entry].y) > CACHE_LIFETIME) {
    cache_keys[entry] = 0u;
    cache_radiance[entry] = 0.f;
    cache_albedo[entry] = 0u;
  }
}
