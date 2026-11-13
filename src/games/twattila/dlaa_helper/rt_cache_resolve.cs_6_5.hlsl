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
  // With Path-Traced Ambient the screen's light holds no ambient but the game's sun and sky specular toward the camera:
  // refit from it, the sun's share followed the view (ATN's log: x1.57 in 6 minutes; the harness's pedestal 24 to 36 by
  // camera, the sun 20). With the game's sun light the sun's is that and the rest stays as learned with the game's ambient
  // (the fill adds nothing). Without it the refit goes on: frozen as learned with the game's ambient the sun came out 4 to
  // 11 there (one shade for all faces, the walls' less than the ground's).
  if (SunLightGiven()) {
    light_kept[1] = float4(sun[uint2(SUN_LIGHT_TEXEL, 0)].rgb, 1.f);
    return;
  }
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
  if (spread > 0.010000842f * count * count) slope = max((count * sum_xy - sum_x * sum_y) / spread, 0.f);
  const float3 intercept = max((sum_y - slope * sum_x) / count, 0.f);
  const float blend = (shade.w > 0.f) ? LIGHT_FIT_BLEND : 1.f;
  light_kept[1] = float4(lerp(sunlit.rgb, slope, blend), 1.f);
  const float3 mean_base = float3(light_fit[9], light_fit[10], light_fit[11]) / (LIGHT_FIT_X * count);
  light_kept[3] = float4(lerp(light_kept[3].rgb, mean_base, (light_kept[3].w > 0.f) ? LIGHT_FIT_BLEND : 1.f), 1.f);
  // With Path-Traced Ambient the screen's shade is the paths' own sky (learned from it, it would only darken): the shade
  // and the ambient cube's scale stay as learned with the game's ambient.
  if ((path_flags & PATH_KEEP_LIGHT) != 0u) return;
  const float3 kept = lerp(shade.rgb, intercept, blend);
  light_kept[0] = float4(kept, 1.f);
  if (sun[uint2(3, 0)].w != 0.f) light_kept[2] = float4(kept / max(AmbientCubeMean(), 1.0001452e-4f), 1.f);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint entry = id.x;
  if (entry == 0u) {
    light_kept[4] = float4(AmbientCubeMean() * sun[uint2(4, 0)].w, 1.f);  // for the helper's log at a freeze
    SolveLightFit();
    [unroll]
    for (uint sum = 0u; sum < 12u; ++sum) {
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
