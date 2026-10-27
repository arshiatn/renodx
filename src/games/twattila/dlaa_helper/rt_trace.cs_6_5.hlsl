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
// alpha-tested (their holes let light through).
// World units: metres, y up. Noise moves every frame, which DLAA / DLSS (or Ray Reconstruction) settle.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_trace.h -Vn RT_TRACE; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_TRACE_OMM.
#include "./rt_scene.hlsli"

RWTexture2D<unorm float> visibility : register(u0);
RWTexture2D<unorm float> ambient : register(u1);
RWTexture2D<float2> sun_rays : register(u2);  // soft shadows: lit, blocker distance
RWTexture2D<uint> light_mask : register(u22);  // the local lights' lit bits (v9)

cbuffer Lights : register(b4) {
  float4 lights[32];  // world position, reach (metres)
  uint light_count;
};

// Where the rays toward a local light stop before it (metres; at most half the way): its lamp, torch or fire's wood.
static const float LIGHT_CLEARANCE = 0.5f;

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
    const float3 sun_origin = position.xyz + (normal + toward_sun.xyz) * offset;
    if (sun_cone > 0.f) {
      // Soft shadows need the nearest blocker's distance.
      RayDesc ray;
      ray.Origin = sun_origin;
      ray.Direction = toward_sun.xyz;
      ray.TMin = 0.f;
      ray.TMax = 1.0e4f;
      TRACE_NEAREST(query, ray)
      if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        sun_visibility = 0.f;
        blocker_distance = query.CommittedRayT();
      }
    } else {
      sun_visibility = Occluded(sun_origin, toward_sun.xyz, 1.0e4f) ? 0.f : 1.f;
    }
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
