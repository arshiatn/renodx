/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smoke shadows without rays (smoke_map.hpp), last of a frame's passes: the sun's occlusion for the patched sun shaders
// (t49): each pixel's own (Bend's or the rays', t2; unbound: 0) with what the smoke along its ray to the sun lets through
// multiplied in. The sun shaders multiply their shadow by 1 - this: the sun's term alone, by a factor, nothing compressed.
// Also where the screen's middle lies in the world, for the next frame's view (smoke_map_setup).
#include "./smoke_map.hlsli"

Texture2D<float4> map : register(t0);
#ifdef SMOKE_MAP_MS
Texture2DMS<float4> depth : register(t1);
#else
Texture2D<float4> depth : register(t1);
#endif
Texture2D<float> occlusion : register(t2);
StructuredBuffer<SmokeView> view : register(t3);
RWTexture2D<unorm float> dimmed : register(u0);
RWStructuredBuffer<float4> anchor : register(u1);
SamplerState linear_clamp : register(s0);

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4 inverse_view_projection[4] : packoffset(c21);  // rows acting on column vectors (rt_positions)
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  uint2 target;
  dimmed.GetDimensions(target.x, target.y);
  if (any(id.xy >= target)) return;
  const uint2 render_size = uint2(g_smoke_render_size);
  const uint2 middle = render_size / 2u;
  const float kept = occlusion[id.xy];
  const SmokeView smoke = view[0];
  if (any(id.xy >= render_size)) {
    dimmed[id.xy] = kept;
    return;
  }
#ifdef SMOKE_MAP_MS
  const float z = depth.Load(int2(id.xy), 0).x;
#else
  const float z = depth.Load(int3(id.xy, 0)).x;
#endif
  if (z <= 0.f || z >= 1.f) {
    dimmed[id.xy] = kept;
    if (all(id.xy == middle)) anchor[1] = 0.f;
    return;
  }
  const float2 ndc = (float2(id.xy) + 0.5f) / g_smoke_render_size * float2(2.f, -2.f) + float2(-1.f, 1.f);
  const float4 clip = float4(ndc, z, 1.f);
  const float4 world = float4(dot(inverse_view_projection[0], clip), dot(inverse_view_projection[1], clip),
                              dot(inverse_view_projection[2], clip), dot(inverse_view_projection[3], clip));
  const float3 position = world.xyz / world.w;
  if (all(id.xy == middle)) {
    // Looking along the ground: not past what the widest map (1024 m) covers well.
    const float3 offset = position - camera_position;
    anchor[1] = float4(camera_position + offset * min(340.f / max(length(offset), 0.001000075f), 1.f), 1.f);
  }
  if (smoke.coverage == 0.f) {
    dimmed[id.xy] = kept;
    return;
  }
  const float2 at = (float2(dot(position, smoke.right), dot(position, smoke.up)) - smoke.origin) / smoke.coverage;
  const float2 uv = float2(0.5f + at.x, 0.5f - at.y);
  const float4 depths = map.SampleLevel(linear_clamp, uv, 0.f);
  // The share of this texel's smoke above the pixel: spread evenly over the height its mean and variance give (one puff:
  // exactly its own height, its radius either side). At least 1 m either side.
  float optical_depth = 0.f;
  if (depths.x > 0.00009999709f) {
    const float mean_height = depths.y / depths.x;
    const float half_height = sqrt(3.f * max((depths.z / depths.x) - (mean_height * mean_height), 1.f / (3.f * SMOKE_MAP_HEIGHT_SCALE * SMOKE_MAP_HEIGHT_SCALE)));
    optical_depth = depths.x * saturate((mean_height + half_height - ((position.y - smoke.height) / SMOKE_MAP_HEIGHT_SCALE)) / (2.f * half_height));
  }
  // Toward the map's edge the smoke fades out, no hard line.
  const float edge = saturate((0.5f - max(abs(at.x), abs(at.y))) * 20.f);
  dimmed[id.xy] = 1.f - (1.f - kept) * exp(-optical_depth * g_smoke_strength * edge);
}
