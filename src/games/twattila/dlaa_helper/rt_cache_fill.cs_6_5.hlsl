/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's radiance cache, filled (rt_path.hlsli; before the paths). With the frame's prepared light (PATH_LIGHT):
// one bounce texel of every 2 x 2 (another each frame) adds it to its world cell; with the G-buffer guides it also writes
// its base colour there, and one in eight adds to the learned lighting's sums: y = the light / the base colour, x = the
// sun's visibility (the ray-traced one when traced) x N.L. Without it (reflections only): one render pixel of every
// 2 x 2 adds last frame's scene colour where last frame saw it.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_cache_fill.h -Vn RT_CACHE_FILL.
#include "./rt_path.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint2 cell = id.xy * 2u + uint2(frame & 1u, (frame >> 1u) & 1u);
  if ((path_flags & PATH_LIGHT) == 0u) {
    if (cell.x >= width || cell.y >= height || (path_flags & PATH_HISTORY_VALID) == 0u) return;
    const float4 seen = previous_positions[cell];
    if (seen.w > 0.f) CacheAdd(seen.xyz, max(previous_color[cell].rgb, 0.f));
    return;
  }
  if (cell.x >= bounce_width || cell.y >= bounce_height) return;
  const uint2 pixel = min(cell * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u);
  const float4 position = positions[pixel];
  if (position.w <= 0.f) return;
  const float3 light_there = max(light[cell], 0.f);
  CacheAdd(position.xyz, light_there);
  if ((path_flags & PATH_GUIDES) == 0u) return;
  const float3 base = guide_albedo[pixel].rgb;
  CacheSetAlbedo(position.xyz, base);

  if (((id.x ^ id.y ^ (frame >> 2u)) & 1u) != 0u) return;
  if (dot(base, float3(0.2126f, 0.7152f, 0.0722f)) < 0.04f) return;
  const float4 toward_sun = sun[uint2(0, 0)];
  const float visibility = ((path_flags & PATH_SUN_TRACED) != 0u) ? traced_visibility[pixel] : 1.f;
  const float x = (toward_sun.w != 0.f) ? visibility * saturate(dot(PixelNormal(pixel, position), toward_sun.xyz)) : 0.f;
  const float3 y = min(light_there / max(base, 0.05f), LIGHT_FIT_MAX_Y);
  const uint3 fixed_y = (uint3)(y * LIGHT_FIT_Y + 0.5f);
  const uint3 fixed_xy = (uint3)(x * y * LIGHT_FIT_Y + 0.5f);
  InterlockedAdd(light_fit[0], 1u);
  InterlockedAdd(light_fit[1], (uint)(x * LIGHT_FIT_X + 0.5f));
  InterlockedAdd(light_fit[2], (uint)(x * x * LIGHT_FIT_X + 0.5f));
  InterlockedAdd(light_fit[3], fixed_y.r);
  InterlockedAdd(light_fit[4], fixed_y.g);
  InterlockedAdd(light_fit[5], fixed_y.b);
  InterlockedAdd(light_fit[6], fixed_xy.r);
  InterlockedAdd(light_fit[7], fixed_xy.g);
  InterlockedAdd(light_fit[8], fixed_xy.b);
}
