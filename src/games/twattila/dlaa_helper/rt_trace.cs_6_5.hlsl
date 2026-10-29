/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing (dlaa_helper/rt_d3d12.hpp; D3D12 inline ray queries) against the scene the addon captured
// (rt_scene.hpp: static terrain, buildings and props, tree trunks, leaves with their opacity masks, the units of the
// frame), per pixel from its world position: the sun's visibility (one ray toward the sun: 0 blocked, 1 lit; 1 for the
// sky, planar surfaces facing away and frames without a sun; for soft shadows also the blocker's distance into
// sun_rays, which rt_soft.cs_6_5.hlsl turns into penumbras) and the ambient visibility (cosine-weighted rays around the
// normal, as far as the range: the share that escapes; at a quarter rate one pixel of each 2 x 2 block, another each
// frame, traces for the block); the local lights' shadows are a pass of their own after it (rt_lights.cs_6_5.hlsl).
// Leaves are alpha-tested (their holes let light through); with Light Through Leaves (v11) each leaf the sun's ray
// meets also lets LEAF_TRANSMITTANCE of it through, and each cloth or sail THIN_TRANSMITTANCE.
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
static const float GRASS_AMBIENT_RANGE = 0.5f;  // metres: grass's share of the ambient rays (Occluded)
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

// Some leaf (the leaves' meshes alone: LEAF_INSTANCE) along `direction` from `origin` between `near` and `far`: its
// distance (whichever the hardware met first, not the nearest), -1 none.
float LeafHit(float3 origin, float3 direction, float near, float far) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = near;
  ray.TMax = far;
  SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, LEAVES, ray);
  uint candidates = 0u;
  while (query.Proceed()) {
    if (++candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) query.CommitNonOpaqueTriangleHit();
  }
  return (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? query.CommittedRayT() : -1.f;
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
    if (sun_cone == 0.f || (flags & RECONSTRUCTED) != 0u) return float2(Occluded(origin, direction, 1.0e4f, ALL_INSTANCES) ? 0.f : smoke_transmittance, 0.f);
    TRACE_NEAREST(query, ray, ALL_INSTANCES)
    return (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? float2(0.f, query.CommittedRayT()) : float2(smoke_transmittance, 0.f);
  }
  // Light Through Leaves: the leaves' meshes (their own instances) apart from the rest. The rest first, as without it
  // (the first surface met: a solid or grass blocks); where that is cloth, a sail or a swaying leaf, the old way: the
  // nearest surface each time, each passing its share, a solid or grass blocking. Then the leaves: none, one or more than one (any-hit rays around
  // the first met), each LEAF_TRANSMITTANCE. Stepping through every leaf layer took 16 ms at 4K in dense leaves (0.9
  // without Light Through Leaves; ATN's 17-18 ms traces, the stutters panning over forests).
  float lit = 1.f;
  float first = 0.f;
  {
    SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, NO_LEAVES, ray);
    uint candidates = 0u;
    while (query.Proceed()) {
      if (++candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) {
        query.CommitNonOpaqueTriangleHit();
      } else if (smoke_transmittance < SMOKE_DARK) {
        query.Abort();
      }
    }
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
      const uint met_flags = records[query.CommittedInstanceID() + query.CommittedGeometryIndex()].z >> 16;
      if ((met_flags & (RECORD_MASKED | RECORD_THIN)) == 0u || (met_flags & RECORD_GRASS) != 0u) return float2(0.f, query.CommittedRayT());
      smoke_transmittance = 1.f;
      [loop]
      for (uint layer = 0u; layer < 4u; ++layer) {
        TRACE_NEAREST(layered, ray, NO_LEAVES)
        if (layered.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        if (layer == 0u) first = layered.CommittedRayT();
        const uint record_flags = records[layered.CommittedInstanceID() + layered.CommittedGeometryIndex()].z >> 16;
        lit *= ((record_flags & RECORD_GRASS) != 0u)    ? 0.f
               : ((record_flags & RECORD_MASKED) != 0u) ? LEAF_TRANSMITTANCE
               : ((record_flags & RECORD_THIN) != 0u)   ? THIN_TRANSMITTANCE
                                                        : 0.f;
        if (lit < 0.02f) return float2(0.f, first);
        ray.TMin = layered.CommittedRayT() + 0.01f;
      }
    }
  }
  const float smoke = smoke_transmittance;
  const float leaf = LeafHit(origin, direction, 0.f, 1.0e4f);
  if (leaf < 0.f) return float2(lit * smoke, first);
  const bool more = (leaf > 0.02f && LeafHit(origin, direction, 0.f, leaf - 0.01f) >= 0.f) || LeafHit(origin, direction, leaf + 0.01f, 1.0e4f) >= 0.f;
  return float2(lit * smoke * (more ? LEAF_TRANSMITTANCE * LEAF_TRANSMITTANCE : LEAF_TRANSMITTANCE), (first > 0.f) ? min(first, leaf) : leaf);
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
  const bool planar = Planar(int2(id.xy), position, normal);
  const float3 origin = RayOrigin(position, normal, planar);
  const float offset = SurfaceOffset(position.w);
  const float4 toward_sun = sun[uint2(0, 0)];
  const bool facing = dot(normal, toward_sun.xyz) > 0.f;
  float sun_visibility = 1.f;
  float blocker_distance = 0.f;
  // A planar surface facing away keeps 1 (its shading's angle darkens it; a leaf lit from behind stays lit, as with the
  // game's shadow maps). Corners, cracks and edges are traced either way: their normal can be wrong (left lit, they let
  // the sun into the cracks where wall parts meet, ATN 2026-10-29); those facing away against solid surfaces only, from
  // past themselves (a leaf or blade does not shadow itself).
  if ((flags & SUN_SHADOWS) != 0u && toward_sun.w != 0.f && (facing || !planar)) {
    // With several rays a pixel (Ray Tracing at Output Resolution) each from its own spot of a planar pixel, averaged.
    const uint rays = clamp(shadow_rays, 1u, 4u);
    const float turn = InterleavedGradientNoise(float2(id.xy) + 37.f);
    float2 sum = 0.f;
    [loop]
    for (uint ray = 0u; ray < rays; ++ray) {
      float3 toward = toward_sun.xyz;
      if (sun_cone > 0.f && !filtered) {
        // Uniform over the sun's disk (sun_cone: the tan of its angular radius).
        toward = normalize(toward + DiskPoint(toward, sun_cone, float2(Random(uint3(id.xy, frame * 16u + 1000u + ray * 2u)),
                                                                        Random(uint3(id.xy, frame * 16u + 1001u + ray * 2u)))));
      }
      const float3 spot = (rays > 1u && planar) ? PixelSpot(int2(id.xy), SpotOf(ray, turn)) : 0.f;
      // Off along the normal and toward the sun; where not planar toward the camera.
      sum += facing ? SunRay((planar ? origin + toward * offset : origin) + spot, toward)
                    : float2(OccludedSolid(position.xyz + spot + toward * offset, toward, 1.0e4f) ? 0.f : 1.f, 0.f);
    }
    sun_visibility = sum.x / rays;
    blocker_distance = sum.y / rays;
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
    // The rays spread over the hemisphere: each its band of the cosine-weighted disk's radius, the golden angle from the
    // last, the set turned per pixel by interleaved gradient noise and jittered within the bands. Less noise than
    // independent rays (Ray Reconstruction left ~20% of theirs), and what is left settles faster.
    const float turn = InterleavedGradientNoise(float2(id.xy));
    const float band_jitter = Random(uint3(id.xy, frame * 16u));
    uint escaped = 0u;
    [loop]
    for (uint ray = 0; ray < ambient_rays; ++ray) {
      const float3 direction = CosineDirection(normal, ((float)ray + band_jitter) / (float)ambient_rays, frac(turn + (float)ray * 0.618034f));
      // Grass only within GRASS_AMBIENT_RANGE: its blades darken what they stand on, farther ones barely, and a ray
      // escaping through a field met every blade within the range.
      if (!Occluded(origin, direction, ambient_range, NO_GRASS_SMOKE) && !Occluded(origin, direction, min(ambient_range, GRASS_AMBIENT_RANGE), GRASS)) {
        ++escaped;
      }
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
