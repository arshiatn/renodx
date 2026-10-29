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
// the block); the local lights' shadows are a pass of their own after it (rt_lights.cs_6_5.hlsl). Leaves are
// alpha-tested (their holes let light through); with Light Through Leaves (v11) each leaf the sun's ray meets also lets
// LEAF_TRANSMITTANCE of it through, and each cloth or sail THIN_TRANSMITTANCE.
// The ambient visibility is kept over frames (v11): where the last trace saw the same surface (its positions, the last
// trace's camera) a fifth of the new one goes into its history.
// World units: metres, y up. Noise moves every frame, which DLAA / DLSS (or Ray Reconstruction) settle.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_trace.h -Vn RT_TRACE; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_TRACE_OMM.
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"

RWTexture2D<unorm float> visibility : register(u0);
RWTexture2D<unorm float> ambient : register(u1);
RWTexture2D<float2> sun_rays : register(u2);  // soft shadows: lit, blocker distance
RWTexture2D<float> ambient_history[2] : register(u28);  // the ambient visibility kept over frames (v11)

static const float AMBIENT_BLEND = 0.2f;
// Interleaved gradient noise (Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare"): a value
// per pixel that is even over every few neighbours, moved each frame.
float InterleavedGradientNoise(float2 pixel) {
  pixel += 5.588238f * (float)(frame & 63u);
  return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

// The ambient visibility at a pixel kept over frames: the last trace's history where it saw the same surface.
float KeptAmbient(uint2 pixel, float3 world, float ambient_now) {
  float kept = ambient_now;
  uint2 seen_pixel;
  if ((path_flags & PATH_AMBIENT_HISTORY) != 0u && SeenByLastTrace(world, seen_pixel)) {
    kept = lerp(ambient_history[(path_flags & PATH_AMBIENT_PARITY) != 0u ? 1u : 0u][seen_pixel], ambient_now, AMBIENT_BLEND);
  }
  ambient_history[(path_flags & PATH_AMBIENT_PARITY) != 0u ? 0u : 1u][pixel] = kept;
  return kept;
}

// The ray toward the sun: lit (1; 0 blocked; with Light Through Leaves the share the leaves met let through; the share
// the smoke it met let through) and the nearest blocker's distance (soft shadows).
float2 SunRay(float3 origin, float3 direction) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = 1.0e4f;
  smoke_transmittance = 1.f;
  if ((flags & LEAF_LIGHT) == 0u) {
    if (sun_cone == 0.f || (flags & RECONSTRUCTED) != 0u) return float2(Occluded(origin, direction, 1.0e4f) ? 0.f : smoke_transmittance, 0.f);
    TRACE_NEAREST(query, ray)
    return (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? float2(0.f, query.CommittedRayT()) : float2(smoke_transmittance, 0.f);
  }
  // The nearest surface each time: a leaf, cloth or sail lets its share through and the ray goes on past it (a leaf's
  // solid parts are opaque with opacity micromaps, so it comes as a hit, not a candidate); a solid surface, or layers
  // too many to let any through, block it.
  float lit = 1.f;
  float first = 0.f;
  float smoke = 1.f;  // the first pass's (the later ones may meet its smoke again)
  [loop]
  for (uint layer = 0u; layer < 4u; ++layer) {
    TRACE_NEAREST(query, ray)
    if (layer == 0u) smoke = smoke_transmittance;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return float2(lit * smoke, first);
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
  // Soft sun shadows: the rays' penumbras filtered (rt_soft), or under Ray Reconstruction one ray toward a point of the
  // sun's disk (it denoises them).
  const bool filtered = sun_cone > 0.f && (flags & RECONSTRUCTED) == 0u;
  if (position.w <= 0.f) {
    visibility[id.xy] = 1.f;
    ambient[id.xy] = 1.f;
    if (filtered) sun_rays[id.xy] = float2(1.f, 0.f);
    return;
  }
  const float3 normal = SurfaceNormal(int2(id.xy), position);
  const float offset = SurfaceOffset(position.w);
  const float4 toward_sun = sun[uint2(0, 0)];
  float sun_visibility = 1.f;
  float blocker_distance = 0.f;
  if ((flags & SUN_SHADOWS) != 0u && toward_sun.w != 0.f && dot(normal, toward_sun.xyz) > 0.f) {
    float3 toward = toward_sun.xyz;
    if (sun_cone > 0.f && !filtered) {
      // Uniform over the sun's disk (sun_cone: the tan of its angular radius).
      const float3 tangent = normalize(cross((abs(toward.y) < 0.999f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f), toward));
      const float radius = sqrt(Random(uint3(id.xy, frame * 16u + 1000u))) * sun_cone;
      const float angle = Random(uint3(id.xy, frame * 16u + 1001u)) * 6.28318531f;
      toward = normalize(toward + (tangent * cos(angle) + cross(toward, tangent) * sin(angle)) * radius);
    }
    const float2 sun_ray = SunRay(position.xyz + (normal + toward) * offset, toward);
    sun_visibility = sun_ray.x;
    blocker_distance = sun_ray.y;
  }
  if (filtered) {
    sun_rays[id.xy] = float2(sun_visibility, blocker_distance);
  } else {
    visibility[id.xy] = sun_visibility;
  }
  // At a quarter rate the block's other pixels leave their ambient to the one tracing this frame.
  const bool quarter = (flags & AMBIENT_QUARTER) != 0u;
  if (quarter && any(id.xy != (id.xy & ~1u) + uint2(frame & 1u, (frame >> 1) & 1u))) return;
  float ambient_visibility = 1.f;
  if ((flags & AMBIENT_OCCLUSION) != 0u && ambient_rays != 0u) {
    const float3 origin = position.xyz + normal * offset;
    // The rays spread over the hemisphere: each its band of the cosine-weighted disk's radius, the golden angle from the
    // last, the set turned per pixel by interleaved gradient noise and jittered within the bands. Less noise than
    // independent rays (Ray Reconstruction left ~20% of theirs), and what is left settles faster.
    const float turn = InterleavedGradientNoise(float2(id.xy));
    const float band_jitter = Random(uint3(id.xy, frame * 16u));
    uint escaped = 0u;
    [loop]
    for (uint ray = 0; ray < ambient_rays; ++ray) {
      const float3 direction = CosineDirection(normal, ((float)ray + band_jitter) / (float)ambient_rays, frac(turn + (float)ray * 0.618034f));
      if (!Occluded(origin, direction, ambient_range)) ++escaped;
    }
    ambient_visibility = (float)escaped / ambient_rays;
  }
  const bool kept = (flags & AMBIENT_OCCLUSION) != 0u && ambient_rays != 0u;
  if (!quarter) {
    ambient[id.xy] = kept ? KeptAmbient(id.xy, position.xyz, ambient_visibility) : ambient_visibility;
    return;
  }
  [unroll]
  for (uint corner = 0u; corner < 4u; ++corner) {
    const uint2 pixel = (id.xy & ~1u) + uint2(corner & 1u, corner >> 1);
    if (pixel.x >= width || pixel.y >= height) continue;
    const float4 corner_position = positions[pixel];
    ambient[pixel] = (kept && corner_position.w > 0.f) ? KeptAmbient(pixel, corner_position.xyz, ambient_visibility) : ambient_visibility;
  }
}
