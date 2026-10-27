/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing (dlaa_helper/rt_d3d12.hpp; D3D12 inline ray queries) against the scene the addon captured
// (rt_scene.hpp: static terrain, buildings and props, tree trunks, leaves with their opacity masks, the units of the
// frame), per pixel from its world position: the sun's visibility (one ray toward the sun: 0 blocked, 1 lit; 1 for the
// sky, surfaces facing away and frames without a sun; for soft shadows also the blocker's distance into sun_rays, which
// rt_soft.cs_6_5.hlsl turns into penumbras) and the ambient visibility (cosine-weighted rays around the normal, as far
// as the range: the share that escapes; at a quarter rate one pixel of each 2 x 2 block, another each frame, traces for
// the block), and with the local lights (bridge v9) a ray toward each one within reach (bit i of light_mask: light i
// reaches the pixel unblocked; hard shadows, the rays stopping short of the light's own fixture). Leaves are
// alpha-tested (their holes let light through); with Light Through Leaves (v11) each leaf the sun's ray meets also lets
// LEAF_TRANSMITTANCE of it through, and each cloth or sail THIN_TRANSMITTANCE.
// World units: metres, y up. Noise moves every frame, which DLAA / DLSS (or Ray Reconstruction) settle.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_trace.h -Vn RT_TRACE; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_TRACE_OMM.
#include "./rt_scene.hlsli"

RWTexture2D<unorm float> visibility : register(u0);
RWTexture2D<unorm float> ambient : register(u1);
RWTexture2D<float2> sun_rays : register(u2);  // soft shadows: lit, blocker distance
RWTexture2D<uint> light_mask : register(u22);  // the local lights' lit bits (v9)

// The ray toward the sun: lit (1; 0 blocked; with Light Through Leaves the share the leaves met let through) and the
// nearest blocker's distance (soft shadows).
float2 SunRay(float3 origin, float3 direction) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = 1.0e4f;
  if ((flags & LEAF_LIGHT) == 0u) {
    if (sun_cone == 0.f) return float2(Occluded(origin, direction, 1.0e4f) ? 0.f : 1.f, 0.f);
    TRACE_NEAREST(query, ray)
    return (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? float2(0.f, query.CommittedRayT()) : float2(1.f, 0.f);
  }
  // The nearest surface each time: a leaf, cloth or sail lets its share through and the ray goes on past it (a leaf's
  // solid parts are opaque with opacity micromaps, so it comes as a hit, not a candidate); a solid surface, or layers
  // too many to let any through, block it.
  float lit = 1.f;
  float first = 0.f;
  [loop]
  for (uint layer = 0u; layer < 4u; ++layer) {
    TRACE_NEAREST(query, ray)
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return float2(lit, first);
    if (layer == 0u) first = query.CommittedRayT();
    const uint record_flags = records[query.CommittedInstanceID() + query.CommittedGeometryIndex()].z >> 16;
    if ((record_flags & (RECORD_MASKED | RECORD_THIN)) == 0u) break;
    lit *= ((record_flags & RECORD_MASKED) != 0u) ? LEAF_TRANSMITTANCE : THIN_TRANSMITTANCE;
    if (lit < 0.02f) break;
    ray.TMin = query.CommittedRayT() + 0.01f;
  }
  return float2(0.f, first);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const float4 position = positions[id.xy];
  if (position.w <= 0.f) {
    visibility[id.xy] = 1.f;
    ambient[id.xy] = 1.f;
    if (sun_cone > 0.f) sun_rays[id.xy] = float2(1.f, 0.f);
    if ((flags & LOCAL_LIGHTS) != 0u) light_mask[id.xy] = 0xFFFFFFFFu;
    return;
  }
  const float3 normal = SurfaceNormal(int2(id.xy), position);
  const float offset = SurfaceOffset(position.w);
  const float4 toward_sun = sun[uint2(0, 0)];
  float sun_visibility = 1.f;
  float blocker_distance = 0.f;
  if ((flags & SUN_SHADOWS) != 0u && toward_sun.w != 0.f && dot(normal, toward_sun.xyz) > 0.f) {
    const float2 sun_ray = SunRay(position.xyz + (normal + toward_sun.xyz) * offset, toward_sun.xyz);
    sun_visibility = sun_ray.x;
    blocker_distance = sun_ray.y;
  }
  if (sun_cone > 0.f) {
    sun_rays[id.xy] = float2(sun_visibility, blocker_distance);
  } else {
    visibility[id.xy] = sun_visibility;
  }
  if ((flags & LOCAL_LIGHTS) != 0u) {
    // Lights out of reach or behind the surface keep their bit: their own shading leaves those pixels dark.
    const float3 light_origin = position.xyz + normal * offset;
    uint lit = 0xFFFFFFFFu;
    [loop]
    for (uint light = 0u; light < light_count; ++light) {
      const float3 to_light = lights[light].xyz - light_origin;
      const float light_distance = length(to_light);
      if (light_distance >= lights[light].w || light_distance <= 1e-3f) continue;
      const float3 direction = to_light / light_distance;
      if (dot(direction, normal) <= 0.f) continue;
      if (Occluded(light_origin, direction, light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance))) lit &= ~(1u << light);
    }
    light_mask[id.xy] = lit;
  }
  // At a quarter rate the block's other pixels leave their ambient to the one tracing this frame.
  const bool quarter = (flags & AMBIENT_QUARTER) != 0u;
  if (quarter && any(id.xy != (id.xy & ~1u) + uint2(frame & 1u, (frame >> 1) & 1u))) return;
  float ambient_visibility = 1.f;
  if ((flags & AMBIENT_OCCLUSION) != 0u && ambient_rays != 0u) {
    const float3 origin = position.xyz + normal * offset;
    uint escaped = 0u;
    [loop]
    for (uint ray = 0; ray < ambient_rays; ++ray) {
      const float3 direction = CosineDirection(normal, Random(uint3(id.xy, frame * 16u + ray * 2u)), Random(uint3(id.xy, frame * 16u + ray * 2u + 1u)));
      if (!Occluded(origin, direction, ambient_range)) ++escaped;
    }
    ambient_visibility = (float)escaped / ambient_rays;
  }
  if (!quarter) {
    ambient[id.xy] = ambient_visibility;
    return;
  }
  [unroll]
  for (uint corner = 0u; corner < 4u; ++corner) {
    const uint2 pixel = (id.xy & ~1u) + uint2(corner & 1u, corner >> 1);
    if (pixel.x < width && pixel.y < height) ambient[pixel] = ambient_visibility;
  }
}
