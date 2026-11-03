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
RWTexture2D<unorm float4> tint : register(u35);         // Coloured Shadows (PATH_TINT): 1 - the sun light's filter
Texture2D<float4> guide_albedo : register(t13);          // Ray Reconstruction's diffuse albedo guide (PATH_TINT_GUIDES)

// Coloured Shadows: the colour the sun's light took through cloth and sails on this ray (SunRay).
static float3 sun_tint = 1.f;

// The colour of cloth or a sail at `world` where the screen shows it (the diffuse albedo guide), as a filter brightest at
// 1; none elsewhere.
float3 ThinColour(float3 world) {
  if ((path_flags & PATH_TINT_GUIDES) == 0u) return 1.f;
  const float4 clip = ClipOf(world, view_projection);
  const float2 uv = clip.xy / max(clip.w, 9.998521e-7f) * float2(0.5f, -0.5f) + 0.5f;
  if (clip.w <= 0.f || any(uv < 0.f) || any(uv >= 1.f)) return 1.f;
  const uint2 pixel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1u);
  const float4 seen = positions[pixel];
  if (seen.w <= 0.f || distance(seen.xyz, world) > 0.05f + 0.009999653f * seen.w) return 1.f;
  const float3 albedo = guide_albedo[pixel].rgb;
  return albedo / max(max(albedo.r, max(albedo.g, albedo.b)), 1.0004024e-3f);
}

static const float AMBIENT_BLEND = 0.2f;
// The profile's split (RT_PROFILE, rt_d3d12.hpp): this dispatch the sun's part alone, or the ambient's.
static const uint PART_SUN = 1u << 30;
static const uint PART_AMBIENT = 1u << 31;
// The sun's rays meet grass only while they are within GRASS_SHADOW_HEIGHT of their start's height (no blade is taller:
// nothing past that can shade the start), at most GRASS_SHADOW_REACH along them (a sun near the horizon), and only past the
// pixel's own blade: GRASS_SELF_SURFACE_OFFSETS of SurfaceOffset, at most GRASS_SELF_REACH. The swaying copy lies exactly
// under its blades' pixels, whose jittered depth put a ray's start before or behind the blade from frame to frame.
static const float GRASS_SHADOW_HEIGHT = 2.5f;  // metres
static const float GRASS_SHADOW_REACH = 40.f;   // metres
static const float GRASS_SELF_SURFACE_OFFSETS = 4.f;
static const float GRASS_SELF_REACH = 0.25f;    // metres
// A surface this edge-on to the sun (the cosine; more with distance: the depth's normal gets worse) asks its triangle which
// way it faces (ShownNormal).
static const float EDGE_ON = 0.1f;
static const float EDGE_ON_PER_METRE = 0.002000572f;
// The ambient visibility at a pixel kept over frames: the last trace's history where it saw the same surface. Also under
// Ray Reconstruction (NVIDIA's guide gives it the raw signal, but RR alone left ~20% of the noise: ATN's debug33, AO
// noisier since v107 took the history away).
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
  query.TraceRayInline(scene, scene_ray_flags, LEAVES, ray);
  uint candidates = 0u;
  while (query.Proceed()) {
    if (++candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) query.CommitNonOpaqueTriangleHit();
  }
  return (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? query.CommittedRayT() : -1.f;
}

// The ray toward the sun against `instances` (all, or all but grass): lit (1; 0 blocked; with Light Through Leaves the
// share the leaves met let through) and the nearest blocker's distance (soft shadows).
float2 SunRay(float3 origin, float3 direction, uint instances) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = 1.0e4f;
  if ((flags & LEAF_LIGHT) == 0u) {
    if (sun_cone == 0.f || (flags & RECONSTRUCTED) != 0u) return float2(Occluded(origin, direction, 1.0e4f, instances) ? 0.f : 1.f, 0.f);
    TRACE_NEAREST(query, ray, instances)
    return (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? float2(0.f, query.CommittedRayT()) : float2(1.f, 0.f);
  }
  // Light Through Leaves: one ray first against everything: none met, lit (most rays in the open; every lit pixel was
  // traced twice, the sun 10 of a frame's 34 ms in ATN's third profile); a solid or grass met first, blocked; a leaves'
  // mesh met first, that the first leaf. Otherwise the leaves' meshes (their own instances) apart from the rest: the rest
  // as without it (the first surface met: a solid or grass blocks); where that is cloth, a sail or a swaying leaf, the old
  // way: the nearest surface each time, each passing its share, a solid or grass blocking. Then the leaves: none, one or
  // more than one (any-hit rays around the first met), each LEAF_TRANSMITTANCE. Stepping through every leaf layer took
  // 16 ms at 4K in dense leaves (0.9 without Light Through Leaves; ATN's 17-18 ms traces panning over forests).
  float first_leaf = -1.f;
  {
    SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
    query.TraceRayInline(scene, scene_ray_flags, instances, ray);
    uint candidates = 0u;
    while (query.Proceed()) {
      if (++candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) {
        query.CommitNonOpaqueTriangleHit();
      }
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return float2(1.f, 0.f);
    const uint met_flags = records[query.CommittedInstanceID() + query.CommittedGeometryIndex()].z >> 16;
    if ((met_flags & (RECORD_MASKED | RECORD_THIN)) == 0u || (met_flags & RECORD_GRASS) != 0u) return float2(0.f, query.CommittedRayT());
    if ((met_flags & RECORD_LEAF) != 0u) first_leaf = query.CommittedRayT();
  }
  float lit = 1.f;
  float first = 0.f;
  {
    SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
    query.TraceRayInline(scene, scene_ray_flags, NO_LEAVES & instances, ray);
    uint candidates = 0u;
    while (query.Proceed()) {
      if (++candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) {
        query.CommitNonOpaqueTriangleHit();
      }
    }
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
      const uint met_flags = records[query.CommittedInstanceID() + query.CommittedGeometryIndex()].z >> 16;
      if ((met_flags & (RECORD_MASKED | RECORD_THIN)) == 0u || (met_flags & RECORD_GRASS) != 0u) return float2(0.f, query.CommittedRayT());
      [loop]
      for (uint layer = 0u; layer < 4u; ++layer) {
        TRACE_NEAREST(layered, ray, NO_LEAVES & instances)
        if (layered.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        if (layer == 0u) first = layered.CommittedRayT();
        const uint record_flags = records[layered.CommittedInstanceID() + layered.CommittedGeometryIndex()].z >> 16;
        lit *= ((record_flags & RECORD_GRASS) != 0u)    ? 0.f
               : ((record_flags & RECORD_MASKED) != 0u) ? LEAF_TRANSMITTANCE
               : ((record_flags & RECORD_THIN) != 0u)   ? THIN_TRANSMITTANCE
                                                        : 0.f;
        if (lit < 0.02f) return float2(0.f, first);
        if ((path_flags & PATH_TINT) != 0u) sun_tint *= ThinColour(ray.Origin + ray.Direction * layered.CommittedRayT());
        ray.TMin = layered.CommittedRayT() + 0.01f;
      }
    }
  }
  const float leaf = (first_leaf >= 0.f) ? first_leaf : LeafHit(origin, direction, 0.f, 1.0e4f);
  if (leaf < 0.f) return float2(lit, first);
  const bool more = (leaf > 0.02f && LeafHit(origin, direction, 0.f, leaf - 0.01f) >= 0.f) || LeafHit(origin, direction, leaf + 0.01f, 1.0e4f) >= 0.f;
  return float2(lit * (more ? LEAF_TRANSMITTANCE * LEAF_TRANSMITTANCE : LEAF_TRANSMITTANCE), (first > 0.f) ? min(first, leaf) : leaf);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const float4 position = positions[id.xy];
  // Soft sun shadows: the rays' penumbras filtered (rt_soft), or under Ray Reconstruction one ray toward a point of the
  // sun's disk (it denoises them), or with NRD that ray and its blocker's distance too (rt_nrd_front).
  const bool filtered = sun_cone > 0.f && (flags & RECONSTRUCTED) == 0u;
  const bool on_disk = sun_cone > 0.f && ((flags & NRD_SUN) != 0u || !filtered);
  if (position.w <= 0.f) {
    if ((flags & PART_AMBIENT) == 0u) {
      visibility[id.xy] = 1.f;
      if ((path_flags & PATH_TINT) != 0u) tint[id.xy] = 0.f;
      if (filtered) sun_rays[id.xy] = float2(1.f, 0.f);
    }
    if ((flags & PART_SUN) == 0u) ambient[id.xy] = 1.f;
    return;
  }
  SceneRayFlags();
  // Grass Shadow Range: pixels past it trace without grass (its shadows short, a field's blades around every ray's start).
  const bool near_grass = grass_shadow_range <= 0.f || position.w <= grass_shadow_range;
  const float4 toward_sun = sun[uint2(0, 0)];
  float3 normal = SurfaceNormal(int2(id.xy), position);
  // By the depth's normal such a surface turned to the sun and away pixel by pixel, frame by frame: whole facets of round
  // towers flickered between lit and shadowed, near and far (ATN's debug33-34).
  const bool exact = (flags & SUN_SHADOWS) != 0u && toward_sun.w != 0.f
                     && abs(dot(normal, toward_sun.xyz)) < EDGE_ON + (EDGE_ON_PER_METRE * position.w) && ShownNormal(position, normal);
  const bool planar = Planar(int2(id.xy), position, normal);
  const float3 origin = RayOrigin(position, normal, planar);
  const float offset = SurfaceOffset(position.w);
  const bool facing = dot(normal, toward_sun.xyz) > 0.f;
  float sun_visibility = 1.f;
  float blocker_distance = 0.f;
  float3 tint_out = 0.f;  // Coloured Shadows: 1 - the colour of the light that came through (0: none)
  // Every surface: facing the sun (by the depth's normal) with the sun's rays; facing away against solid surfaces only,
  // from past itself (a thick one meets itself, as its back faces in a shadow map; through a leaf or blade what stands
  // beyond: a leaf lit from behind stays lit), an edge-on one's start behind it (in front a ray at its tiny angle left the
  // facet before it crossed its plane: lit). Planar ones facing away kept 1 before, their shading's angle trusted to
  // darken them: grass shades with its own normal and normal-mapped walls light texels at grazing angles, both lit in
  // other objects' shadows (grass behind a city wall, a tower's edge, ATN 2026-10-29). Corners, cracks and edges (normal
  // from the depth wrong: the sun into the cracks where wall parts meet) start toward the camera.
  if ((flags & SUN_SHADOWS) != 0u && (flags & PART_AMBIENT) == 0u && toward_sun.w != 0.f) {
    // With several rays a pixel (Ray Tracing at Output Resolution) each from its own spot of a planar pixel, averaged.
    const uint rays = clamp(shadow_rays, 1u, 8u);
    const float turn = InterleavedGradientNoise(float2(id.xy) + 37.f);
    float2 sum = 0.f;
    float3 tint_sum = 0.f;
    [loop]
    for (uint ray = 0u; ray < rays; ++ray) {
      sun_tint = 1.f;
      float3 toward = toward_sun.xyz;
      if (on_disk) {
        // Uniform over the sun's disk (sun_cone: the tan of its angular radius). For NRD gradient noise, 8 patterns in
        // turn (its guide: SIGMA settles such noise far better than white).
        const float2 disk = ((flags & NRD_SUN) != 0u)
                                ? float2(InterleavedGradientNoise(float2(id.xy) + 5.588238f * float(frame & 7u) + float(ray) * 17.f),
                                         InterleavedGradientNoise(float2(id.yx) + 9.f + 7.137f * float(frame & 7u) + float(ray) * 29.f))
                                : float2(Random(uint3(id.xy, frame * 16u + 1000u + ray * 2u)), Random(uint3(id.xy, frame * 16u + 1001u + ray * 2u)));
        toward = normalize(toward + DiskPoint(toward, sun_cone, disk));
      }
      const float3 spot = (rays > 1u && planar) ? PixelSpot(int2(id.xy), SpotOf(ray, turn)) : 0.f;
      // Off along the normal and toward the sun; where not planar toward the camera. Grass apart, over the ray's first
      // stretch only (rays skimming whole fields toward a low sun cost the sun 22-27 ms zoomed into grass, ATN's debug29).
      const float3 start = (planar ? origin + toward * offset : origin) + spot;
      float2 result = facing ? SunRay(start, toward, NO_GRASS_SMOKE)
                             : float2(OccludedSolid(position.xyz + spot + (toward - (exact ? normal : 0.f)) * offset, toward, 1.0e4f) ? 0.f : 1.f, 0.f);
      if (facing && near_grass && result.x > 0.f) {
        RayDesc grass_ray;
        grass_ray.Origin = start;
        grass_ray.Direction = toward;
        grass_ray.TMin = min(GRASS_SELF_SURFACE_OFFSETS * offset, GRASS_SELF_REACH);
        grass_ray.TMax = min(GRASS_SHADOW_HEIGHT / max(toward.y, 1.0001413e-3f), GRASS_SHADOW_REACH);
        SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) grass;
        grass.TraceRayInline(scene, scene_ray_flags, GRASS, grass_ray);
        // Past MAX_CANDIDATES blades the grass counts as letting the sun through: grass whose micromaps are still on their
        // way (its set's corners, bake and templates: 1-2 s) makes every blade a candidate, and counted as blocked it went
        // black after each camera move until they came (ATN's debug29-30).
        uint candidates = 0u;
        while (grass.Proceed()) {
          if (++candidates > MAX_CANDIDATES) {
            grass.Abort();
          } else if (CANDIDATE_PASSES(grass)) {
            grass.CommitNonOpaqueTriangleHit();
          }
        }
        if (grass.CommittedStatus() == COMMITTED_TRIANGLE_HIT) result = float2(0.f, grass.CommittedRayT());
      }
      // The smoke apart where the sun still comes through: Smoke Shadow Quality's particles that dim it (8, 16 or 32).
      if (facing && result.x > 0.f) result.x *= SmokeTransmittance(start, toward, 1.0e4f, 8u << ((flags >> SMOKE_QUALITY_SHIFT) & 3u));
      sum += result;
      tint_sum += result.x * sun_tint;
    }
    sun_visibility = sum.x / rays;
    if (sum.x > 0.f) tint_out = 1.f - tint_sum / sum.x;
    blocker_distance = sum.y / rays;
  }
  if ((flags & PART_AMBIENT) == 0u) {
    if ((path_flags & PATH_TINT) != 0u) tint[id.xy] = float4(tint_out, 0.f);
    if (filtered) {
      sun_rays[id.xy] = float2(sun_visibility, blocker_distance);
    } else {
      visibility[id.xy] = sun_visibility;
    }
  }
  if ((flags & PART_SUN) != 0u) return;
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
      if (!Occluded(origin, direction, ambient_range, NO_GRASS_SMOKE)
          && !(near_grass && Occluded(origin, direction, min(ambient_range, GRASS_AMBIENT_RANGE), GRASS_STILL))) {
        ++escaped;
      }
    }
    ambient_visibility = (float)escaped / ambient_rays;
  }
  const bool kept = (flags & AMBIENT_OCCLUSION) != 0u && ambient_rays != 0u && (flags & NRD_AMBIENT) == 0u;
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
