/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's radiance cache, filled (rt_path.hlsli; before the paths). With the frame's prepared light (PATH_LIGHT):
// one bounce texel of every 2 x 2 (another each frame) adds it (with Path-Traced Ambient and the game's sun light its
// light of no view: SeenDiffuseLight) to its world cell at the face its normal turns to (rt_path.hlsli CacheCell); with
// the G-buffer guides it also writes its base colour there, and (without the game's sun light) one in eight adds to the
// learned lighting's sums: y = the light / the base colour, x = the
// sun's visibility (the ray-traced one when traced) x N.L, and the base colour. A reflections request's light buffer
// (PATH_LIGHT_FINAL) with no prepared light this frame comes as bounce texels of one render pixel. Without any (reflections
// only, a game that passes no light buffer): one render pixel of every 2 x 2 adds last frame's scene colour where last
// frame saw it.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_cache_fill.h -Vn RT_CACHE_FILL.
#include "./rt_path.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint2 cell = id.xy * 2u + uint2(frame & 1u, (frame >> 1u) & 1u);
  if ((path_flags & PATH_LIGHT) == 0u) {
    if (cell.x >= width || cell.y >= height || (path_flags & PATH_HISTORY_VALID) == 0u) return;
    const float4 seen = previous_positions[uint3(cell, previous_positions_slice)];
    if (seen.w <= 0.f) return;
    // Its cell's face by the normal across last frame's positions there (toward the camera).
    const uint2 last = uint2(width, height) - 1u;
    const float3 along_x = previous_positions[uint3(min(cell + uint2(1u, 0u), last), previous_positions_slice)].xyz - seen.xyz;
    const float3 along_y = previous_positions[uint3(min(cell + uint2(0u, 1u), last), previous_positions_slice)].xyz - seen.xyz;
    const float3 normal = cross(along_y, along_x);
    CacheAdd(seen.xyz, (dot(normal, Camera() - seen.xyz) < 0.f) ? -normal : normal, PreviousLight(cell));
    return;
  }
  if (cell.x >= bounce_width || cell.y >= bounce_height) return;
  const uint2 pixel = min(cell * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u);
  const float4 position = positions[pixel];
  if (position.w <= 0.f) return;
  const float3 light_there = max(light[cell], 0.f);
  const float3 base = ((path_flags & PATH_GUIDES) != 0u) ? guide_albedo[pixel].rgb : UNSEEN_ALBEDO;
  const float3 normal = PixelNormal(pixel, position);
  // Path-Traced Ambient took the game's ambient away from the screen's light: with the game's sun light its light of no
  // view (rt_path.hlsli SeenDiffuseLight) as the paths' hits take it, else the base colour x the kept shade in the
  // ambient's place.
  const bool sun_light_given = SunLightGiven();
  if (sun_light_given) {
    CacheAdd(position.xyz, normal, SeenDiffuseLight(pixel, position.xyz, normal, base));
  } else {
    CacheAdd(position.xyz, normal, light_there + (((path_flags & PATH_KEEP_LIGHT) != 0u) ? base * KeptShade() : 0.f));
  }
  if ((path_flags & PATH_GUIDES) == 0u) return;
  CacheSetAlbedo(position.xyz, normal, base);

  // With the game's sun light the learned lighting stays as learned (rt_cache_resolve.cs_6_5.hlsl).
  if (sun_light_given) return;
  if (((id.x ^ id.y ^ (frame >> 2u)) & 1u) != 0u) return;
  if (dot(base, float3(0.2126f, 0.7152f, 0.0722f)) < 0.040005647f) return;
  const float4 toward_sun = sun[uint2(0, 0)];
  const float visibility = ((path_flags & PATH_SUN_TRACED) != 0u) ? traced_visibility[pixel] : 1.f;
  const float x = (toward_sun.w != 0.f) ? visibility * saturate(dot(normal, toward_sun.xyz)) : 0.f;
  const float3 y = min(light_there / max(base, 0.04999349f), LIGHT_FIT_MAX_Y);
  const uint3 fixed_y = (uint3)(y * LIGHT_FIT_Y + 0.5f);
  const uint3 fixed_xy = (uint3)(x * y * LIGHT_FIT_Y + 0.5f);
  const uint3 fixed_base = (uint3)(saturate(base) * LIGHT_FIT_X + 0.5f);
  InterlockedAdd(light_fit[0], 1u);
  InterlockedAdd(light_fit[1], (uint)(x * LIGHT_FIT_X + 0.5f));
  InterlockedAdd(light_fit[2], (uint)(x * x * LIGHT_FIT_X + 0.5f));
  InterlockedAdd(light_fit[3], fixed_y.r);
  InterlockedAdd(light_fit[4], fixed_y.g);
  InterlockedAdd(light_fit[5], fixed_y.b);
  InterlockedAdd(light_fit[6], fixed_xy.r);
  InterlockedAdd(light_fit[7], fixed_xy.g);
  InterlockedAdd(light_fit[8], fixed_xy.b);
  InterlockedAdd(light_fit[9], fixed_base.r);
  InterlockedAdd(light_fit[10], fixed_base.g);
  InterlockedAdd(light_fit[11], fixed_base.b);
}
