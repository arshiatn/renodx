/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// test/tw_sssr_hits: the screen-space reflections' march (twattila/ssr_march.hlsli over FidelityFX SSSR's traversal) on a
// ground plane's pixels, each its mirror ray at g_cap steps. Per pixel (u0): x 1 where the ray ran out of steps (it never
// went below mip 0; FFX_SSSR_HierarchicalRaymarch's valid_hit as this build defines it), y SsrTrace's confidence, z 1 where
// the g_cap march's end is the g_long_cap march's (a ray that found its hit stops there), w 1 where valid_hit was true;
// x -1: not traced (not the ground). FFX_SSSR_CONTROL_CAP_ENDED_HITS: AMD's valid_hit (the control).
cbuffer test : register(b0)
{
  float4 g_projection;  // tan(half fov x), tan(half fov y), A, B: depth = A + B / view z (0 near, 1 far)
  uint g_width;
  uint g_height;
  uint g_cap;
  uint g_long_cap;
}

Texture2D<float4> t_normals : register(t0);
RWStructuredBuffer<float4> u_out : register(u0);

struct TestSettings
{
  float ssr_hierarchical;
};
static const TestSettings SI = {1.f};

float3 FFX_SSSR_LoadWorldSpaceNormal(int2 pixel)
{
  return t_normals.Load(int3(pixel, 0)).xyz;
}

float3 FFX_SSSR_ScreenSpaceToViewSpace(float3 screen_position)
{
  const float z = g_projection.w / (screen_position.z - g_projection.z);
  return float3((screen_position.x * 2.f - 1.f) * g_projection.x * z, (1.f - 2.f * screen_position.y) * g_projection.y * z, z);
}

#include "../../src/games/twattila/ssr_march.hlsli"

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (pixel.x >= g_width || pixel.y >= g_height) return;
  const uint index = pixel.y * g_width + pixel.x;
  const float2 size = float2(g_width, g_height);
  const float depth = t_ssr_hiz.Load(int3(pixel, 0));
  const float3 normal = t_normals.Load(int3(pixel, 0)).xyz;
  if (depth >= 1.f || normal.y < 0.5f)
  {
    u_out[index] = float4(-1.f, 0.f, 0.f, 0.f);
    return;
  }
  const float3 origin = float3((float2(pixel) + 0.5f) / size, depth);
  const float3 view = FFX_SSSR_ScreenSpaceToViewSpace(origin);
  const float3 reflected = reflect(normalize(view), normal);
  const float3 end_view = view + reflected * 100.f;
  const float3 end = float3(0.5f + 0.5f * end_view.x / (end_view.z * g_projection.x), 0.5f - 0.5f * end_view.y / (end_view.z * g_projection.y),
                            g_projection.z + g_projection.w / end_view.z);
  const float3 direction = end - origin;
  bool valid_hit;
  const float3 capped = FFX_SSSR_HierarchicalRaymarch(origin, direction, true, size, 0, 0, g_cap, valid_hit);
  bool long_valid_hit;
  const float3 longer = FFX_SSSR_HierarchicalRaymarch(origin, direction, true, size, 0, 0, g_long_cap, long_valid_hit);
  float3 hit;
  const float confidence = SsrTrace(origin, direction, reflected, g_cap, float3(0.f, 0.f, 0.f), hit);
#ifdef FFX_SSSR_CONTROL_CAP_ENDED_HITS
  const float ran_out = 0.f;  // AMD's valid_hit says nothing about it
#else
  const float ran_out = valid_hit ? 0.f : 1.f;
#endif
  u_out[index] = float4(ran_out, confidence, all(capped == longer) ? 1.f : 0.f, valid_hit ? 1.f : 0.f);
}
