/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The ray-traced Light Bounce (dlaa_helper/rt_d3d12.hpp Bounce; the addon's light_bounce.hpp): for each texel of the
// bounce (one per bounce_scale x bounce_scale render pixels) cosine-weighted rays from its surface into the scene the
// addon captured; a ray's hit gives the light the prepared light holds where the camera sees that same surface (its
// pixel's world position within a few centimetres of the hit), else nothing (off screen, behind something, or the sky:
// as the screen-space trace, which this replaces). Out: the light arriving (rgb), the texel's distance from the camera
// (alpha; 0: the sky), as light_bounce_trace writes it, so the Light Bounce's resolve, denoiser and apply follow.
// World units: metres, y up.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_bounce.h -Vn RT_BOUNCE; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_BOUNCE_OMM.
#include "./rt_scene.hlsli"

Texture2D<float3> light : register(t9);     // the prepared light (the bounce's texels)
RWTexture2D<float4> bounce : register(u9);
RWTexture2D<float> nrd_hit_distance : register(u44);  // NRD_BOUNCE: the rays' mean, metres

cbuffer Bounce : register(b2) {
  float4 view_projection[4];  // the camera's registers: clip = (dot(position, [0]), ...)
  uint bounce_width;
  uint bounce_height;
  uint bounce_scale;
  uint bounce_padding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= bounce_width || id.y >= bounce_height) return;
  const int2 pixel = min(int2(id.xy * bounce_scale + bounce_scale / 2u), int2(width, height) - 1);
  const float4 position = positions[pixel];
  if (position.w <= 0.f) {
    bounce[id.xy] = 0.f;
    return;
  }
  const float3 normal = SurfaceNormal(pixel, position);
  const float3 origin = RayOrigin(position, normal, Planar(pixel, position, normal));
  const uint rays = max(ambient_rays, 1u);
  float3 arriving = 0.f;
  float hit_sum = 0.f;
  [loop]
  for (uint index = 0; index < rays; ++index) {
    const float3 direction = CosineDirection(normal, Random(uint3(id.xy, frame * 32u + index * 2u)),
                                             Random(uint3(id.xy, frame * 32u + index * 2u + 1u)));
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.f;
    ray.TMax = ambient_range;
    TRACE_SURFACE(query, ray)
    hit_sum += (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? query.CommittedRayT() : ambient_range;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) continue;
    const float4 hit = float4(origin + direction * query.CommittedRayT(), 1.f);
    const float4 clip = float4(dot(hit, view_projection[0]), dot(hit, view_projection[1]), dot(hit, view_projection[2]),
                               dot(hit, view_projection[3]));
    if (clip.w <= 0.f) continue;
    const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
    if (any(uv < 0.f) || any(uv >= 1.f)) continue;
    const uint2 hit_pixel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1u);
    const float4 seen = positions[hit_pixel];
    if (seen.w <= 0.f || distance(seen.xyz, hit.xyz) > 0.05f + 0.02f * seen.w) continue;
    arriving += light[min(hit_pixel / bounce_scale, uint2(bounce_width, bounce_height) - 1u)];
  }
  bounce[id.xy] = float4(arriving / rays, distance(position.xyz, sun[uint2(1, 0)].xyz));
  if ((flags & NRD_BOUNCE) != 0u) nrd_hit_distance[id.xy] = hit_sum / rays;
}
