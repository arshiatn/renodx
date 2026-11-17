/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's radiance cache, filled (rt_path.hlsli; before the paths). With the frame's prepared light (PATH_LIGHT):
// one bounce texel of every 2 x 2 (another each frame) adds it (with Path-Traced Ambient and the game's sun light its
// light of no view: SeenDiffuseLight) to its world cell at the face its normal turns to (rt_path.hlsli CacheCell); with
// the G-buffer guides the lowest texel that added to a cell becomes its owner (rt_cache_resolve writes the owner's base
// colour there), and (without the game's sun light) one in eight adds to the learned lighting's sums: y = the light / the base colour, x = the
// sun's visibility (the ray-traced one when traced) x N.L, and the base colour. A reflections request's light buffer
// (PATH_LIGHT_FINAL) with no prepared light this frame comes as bounce texels of one render pixel. Without any (reflections
// only, a game that passes no light buffer): one render pixel of every 2 x 2 adds last frame's scene colour where last
// frame saw it.
// The sums are integers: a wave adds its lanes' together, one lane each (the same sums as an add a lane, without a cell's
// lanes, most of a wave's, or the whole screen's for the learned lighting queuing on the same addresses).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_cache_fill.h -Vn RT_CACHE_FILL.
#include "./rt_path.hlsli"

// A cell's owner, ~texel (0: none; rt_cache_resolve writes its base colour and clears it). Every texel that added to a
// cell wrote its colour there before, the last one's stayed: another texel's colour each run, and two plays of one
// recording differed in the bounce, the reflections, the cache and ReSTIR GI's reservoirs.
RWByteAddressBuffer cache_owners : register(u0, space1);

// CacheAdd's (rt_path.hlsli), the lanes of a wave that claimed the same entry by one add of their sums: its last lane's, after
// the sum of the lanes before it. No loop over the entries: a loop on WaveReadLaneFirst's value is the same value to the
// compiler in every round and was taken as done after the first (DXC: one sum over all the lanes). A kept cell is found by
// loads before any exchange: a key, once in its entry, stays there for the dispatch (only the resolve frees one), so the
// claim would end at the same entry, and the lanes of a cell (most of a wave's) do not queue on one atomic. False: no entry.
bool CacheAddInWave(float3 world, float3 normal, float3 light_there, out uint entry) {
  if (!CacheFind(world, normal, entry) && !CacheClaim(world, normal, entry)) return false;
  const uint3 fixed_point = (uint3)(clamp(light_there, 0.f, CACHE_MAX) * CACHE_SCALE + 0.5f);
  const uint4 same_entry = WaveMatch(entry);
  const uint3 sum = WaveMultiPrefixSum(fixed_point, same_entry) + fixed_point;
  const uint count = WaveMultiPrefixCountBits(true, same_entry) + 1u;
  const uint4 lanes = countbits(same_entry);
  if (count == lanes.x + lanes.y + lanes.z + lanes.w) {
    InterlockedAdd(cache_sums[entry * 4u], sum.r);
    InterlockedAdd(cache_sums[entry * 4u + 1u], sum.g);
    InterlockedAdd(cache_sums[entry * 4u + 2u], sum.b);
    InterlockedAdd(cache_sums[entry * 4u + 3u], count);
  }
  return true;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint2 cell = id.xy * 2u + uint2(frame & 1u, (frame >> 1u) & 1u);
  uint entry;
  if ((path_flags & PATH_LIGHT) == 0u) {
    if (cell.x >= width || cell.y >= height || (path_flags & PATH_HISTORY_VALID) == 0u) return;
    const float4 seen = previous_positions[uint3(cell, previous_positions_slice)];
    if (seen.w <= 0.f) return;
    // Its cell's face by the normal across last frame's positions there (toward the camera).
    const uint2 last = uint2(width, height) - 1u;
    const float3 along_x = previous_positions[uint3(min(cell + uint2(1u, 0u), last), previous_positions_slice)].xyz - seen.xyz;
    const float3 along_y = previous_positions[uint3(min(cell + uint2(0u, 1u), last), previous_positions_slice)].xyz - seen.xyz;
    const float3 normal = cross(along_y, along_x);
    CacheAddInWave(seen.xyz, (dot(normal, Camera() - seen.xyz) < 0.f) ? -normal : normal, PreviousLight(cell), entry);
    return;
  }
  if (cell.x >= bounce_width || cell.y >= bounce_height) return;
  const uint2 pixel = min(cell * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u);
  // The texel's loads at once, before the sky test (one memory wait, not one after another).
  const float4 position = positions[pixel];
  const float3 light_there = max(light[cell], 0.f);
  const float3 base = ((path_flags & PATH_GUIDES) != 0u) ? guide_albedo[pixel].rgb : UNSEEN_ALBEDO;
  const float3 guide = GuideNormal(pixel);
  if (position.w <= 0.f) return;
  const float3 normal = (dot(guide, guide) > 0.5f) ? normalize(guide) : SurfaceNormal(int2(pixel), position);
  // Path-Traced Ambient took the game's ambient away from the screen's light: with the game's sun light its light of no
  // view (rt_path.hlsli SeenDiffuseLight) as the paths' hits take it, else the base colour x the kept shade in the
  // ambient's place.
  const bool sun_light_given = SunLightGiven();
  float3 cell_light;
  if (sun_light_given) {
    cell_light = SeenDiffuseLight(pixel, position.xyz, normal, base, uint3(cell, frame + 52000u));
  } else {
    cell_light = light_there + (((path_flags & PATH_KEEP_LIGHT) != 0u) ? base * KeptShade() : 0.f);
  }
  const bool claimed = CacheAddInWave(position.xyz, normal, cell_light, entry);
  if ((path_flags & PATH_GUIDES) == 0u) return;
  if (claimed) cache_owners.InterlockedMax(entry * 4u, ~(cell.x + cell.y * bounce_width));

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
  const uint2 fixed_x = uint2((uint)(x * LIGHT_FIT_X + 0.5f), (uint)(x * x * LIGHT_FIT_X + 0.5f));
  const uint count = WaveActiveCountBits(true);
  const uint2 sum_x = WaveActiveSum(fixed_x);
  const uint3 sum_y = WaveActiveSum(fixed_y);
  const uint3 sum_xy = WaveActiveSum(fixed_xy);
  const uint3 sum_base = WaveActiveSum(fixed_base);
  if (!WaveIsFirstLane()) return;
  InterlockedAdd(light_fit[0], count);
  InterlockedAdd(light_fit[1], sum_x.x);
  InterlockedAdd(light_fit[2], sum_x.y);
  InterlockedAdd(light_fit[3], sum_y.r);
  InterlockedAdd(light_fit[4], sum_y.g);
  InterlockedAdd(light_fit[5], sum_y.b);
  InterlockedAdd(light_fit[6], sum_xy.r);
  InterlockedAdd(light_fit[7], sum_xy.g);
  InterlockedAdd(light_fit[8], sum_xy.b);
  InterlockedAdd(light_fit[9], sum_base.r);
  InterlockedAdd(light_fit[10], sum_base.g);
  InterlockedAdd(light_fit[11], sum_base.b);
}
