/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's scene (dlaa_helper/rt_d3d12.hpp; D3D12): the TLAS, each geometry's record (its instance's
// InstanceID plus the geometry's index: static meshes one each, the frame's units one per draw), the leaves' UVs and
// opacity masks, every triangle's normal; traversal with the leaves' alpha test, and the passes' shared helpers. With
// DXR 1.2 (shader model 6.9) the leaves' opacity micromaps answer most of a leaf in hardware.
// World units: metres, y up.
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SCENE_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SCENE_HLSLI_

RaytracingAccelerationStructure scene : register(t0);
Texture2D<float4> positions : register(t1);  // world position, view depth (0: sky)
Texture2D<float4> sun : register(t2);        // [0] toward the sun (w 1: valid), [1] the camera
// Per record: UV offset in uv_pool, normal offset in normal_pool, atlas slice | flags << 16, the alpha threshold (asfloat).
Buffer<uint4> records : register(t3);
Buffer<uint> uv_pool : register(t4);      // per vertex (three per triangle): the UV as two halves
Buffer<uint> normal_pool : register(t5);  // per triangle: its normal, octahedral, two 16-bit snorms
Texture2DArray<float> alpha_atlas : register(t6);
SamplerState alpha_sampler : register(s0);  // linear, wrapping

cbuffer Settings : register(b0) {
  uint frame;
  uint flags;  // SUN_SHADOWS, AMBIENT_OCCLUSION
  float ambient_range;
  uint ambient_rays;
  uint width;
  uint height;
  float sun_cone;     // tan of the sun's angular radius for soft shadows (0: hard)
  uint shadow_rays;   // rays a pixel toward the sun and each light (v18; Ray Tracing at Output Resolution)
  float light_size;   // the local lights' radius in metres for soft shadows under Ray Reconstruction (v18; 0: hard)
  uint3 settings_padding;
};

static const uint SUN_SHADOWS = 1u;
static const uint AMBIENT_OCCLUSION = 2u;
static const uint AMBIENT_QUARTER = 4u;  // one pixel of each 2 x 2 block traces (another each frame) for the block
static const uint LOCAL_LIGHTS = 8u;     // the local lights' shadows (rt_trace.cs_6_5.hlsl)
static const uint LEAF_LIGHT = 16u;      // leaves let a share of the sun through (rt_trace.cs_6_5.hlsl)
static const uint RECONSTRUCTED = 256u;  // Ray Reconstruction denoises the frame (dlaa_bridge.h RT_RECONSTRUCTED)
static const uint FOG_LIGHTS = 512u;     // the local lights' light in the fog too (rt_fog.cs_6_5.hlsl; v18)
static const uint RECORD_MASKED = 1u;  // flags: alpha-tested (leaves)
static const uint RECORD_THIN = 2u;    // cloth and sails
static const uint RECORD_SMOKE = 4u;   // smoke (Smoke Shadows): dims the sun's rays, blocks none
// The share of the sun a leaf lets through (Light Through Leaves; thin leaves pass about a quarter, each one met).
static const float LEAF_TRANSMITTANCE = 0.25f;
static const float THIN_TRANSMITTANCE = 0.35f;  // cloth and sails

// The local lights (bridge v9; their colours v11, for the paths): world position and reach (metres), their count.
cbuffer Lights : register(b4) {
  float4 lights[32];
  uint light_count;
  float4 light_colours[32];  // rgb
  uint4 light_previous[2];   // each light's place in the last trace's list, a byte each (255: new)
};

// Where the rays toward a local light stop before it (metres; at most half the way): its lamp, torch or fire's wood.
static const float LIGHT_CLEARANCE = 0.5f;


#if defined(OPACITY_MICROMAPS)
#define SCENE_QUERY(query_flags) RayQuery<(query_flags), RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS>
#else
#define SCENE_QUERY(query_flags) RayQuery<(query_flags)>
#endif

float2 UnpackUv(uint packed) {
  return f16tof32(uint2(packed, packed >> 16));
}

// The share of the sun's light past the smoke a ray met so far (SunRay starts it; AlphaPasses dims it).
static float smoke_transmittance = 1.f;

// Where a ray meets a smoke particle: its opacity (its UVs' scale less one) over a soft disk.
float SmokeDensity(uint first, float2 barycentrics) {
  const float2 corner = UnpackUv(uv_pool[first]);
  const float2 uv = corner * (1.f - barycentrics.x - barycentrics.y) + UnpackUv(uv_pool[first + 1u]) * barycentrics.x
                    + UnpackUv(uv_pool[first + 2u]) * barycentrics.y;
  const float scale = abs(corner.x);
  return (scale - 1.f) * (1.f - smoothstep(0.25f, 1.f, length(uv) / scale));
}

// Whether a candidate (a leaf) is solid where the ray meets it; smoke never is (it dims smoke_transmittance).
bool AlphaPasses(uint record_index, uint primitive, float2 barycentrics) {
  const uint4 record = records[record_index];
  if (((record.z >> 16) & RECORD_SMOKE) != 0u) {
    smoke_transmittance *= 1.f - SmokeDensity(record.x + primitive * 3u, barycentrics);
    return false;
  }
  if (((record.z >> 16) & RECORD_MASKED) == 0u) return true;
  const uint first = record.x + primitive * 3u;
  const float2 uv = UnpackUv(uv_pool[first]) * (1.f - barycentrics.x - barycentrics.y)
                    + UnpackUv(uv_pool[first + 1u]) * barycentrics.x + UnpackUv(uv_pool[first + 2u]) * barycentrics.y;
  return alpha_atlas.SampleLevel(alpha_sampler, float3(uv, (float)(record.z & 0xFFFFu)), 0.f) >= asfloat(record.w);
}

#define CANDIDATE_PASSES(query) \
  AlphaPasses((query).CandidateInstanceID() + (query).CandidateGeometryIndex(), (query).CandidatePrimitiveIndex(), (query).CandidateTriangleBarycentrics())

// Alpha-tested surfaces a ray toward a light meets at most before it counts as blocked (dense grass at a grazing angle:
// each ray's cost bounded).
static const uint MAX_CANDIDATES = 64u;

// The share of the sun's light left at which smoke counts as dark: its ray goes no further (each particle a candidate;
// at 4K through 1000 particles the trace took 80 ms a frame, each sun ray dozens).
static const float SMOKE_DARK = 0.05f;

// The TLAS's instance masks (rt_d3d12.hpp: solids 1, the smoke's own instance 2, the leaves' meshes 4): the rays toward
// the sun meet all (ALL_INSTANCES); the others (the ambient, the local lights, the paths, the reflections) leave the
// smoke out in the hardware (NO_SMOKE); Light Through Leaves meets the leaves apart (NO_LEAVES, LEAVES).
static const uint ALL_INSTANCES = 0xFFu;
static const uint NO_SMOKE = 0xFDu;
static const uint NO_LEAVES = 0xFBu;
static const uint LEAVES = 0x04u;

// Whether anything among `instances` lies along `direction` from `origin` within `range` (alpha-tested: leaves, grass
// and cloth by their masks, smoke dimming smoke_transmittance).
bool Occluded(float3 origin, float3 direction, float range, uint instances) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, instances, ray);
  uint candidates = 0u;
  while (query.Proceed()) {
    if (++candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) {
      query.CommitNonOpaqueTriangleHit();
    } else if (smoke_transmittance < SMOKE_DARK) {
      query.Abort();
    }
  }
  return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// The nearest surface among `instances` along a ray toward the sun (alpha-tested as Occluded; its query, committed:
// CommittedStatus says whether one was). Past MAX_CANDIDATES alpha-tested surfaces the last counts; past dark smoke
// none further.
#define TRACE_NEAREST(query, ray, instances)                                                       \
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;                                            \
  query.TraceRayInline(scene, RAY_FLAG_NONE, (instances), (ray));                                    \
  {                                                                                                  \
    uint trace_candidates = 0u;                                                                      \
    while (query.Proceed()) {                                                                        \
      if (++trace_candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) {                         \
        query.CommitNonOpaqueTriangleHit();                                                          \
      } else if (smoke_transmittance < SMOKE_DARK) {                                                 \
        query.Abort();                                                                               \
      }                                                                                              \
    }                                                                                                \
  }

// Whether a solid surface lies along a ray: alpha-tested ones are culled in the hardware (the solid parts of leaves with
// opacity micromaps stay), the smoke's instance left out. A ray goes on through every culled one (through a canopy 5x
// the cost of alpha tests ending at the first leaf), so only from surfaces facing away from the sun.
bool OccludedSolid(float3 origin, float3 direction, float range) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_NON_OPAQUE) query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, NO_SMOKE, ray);
  query.Proceed();
  return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// The nearest surface along a ray of the Light Bounce, its paths or the reflections (its query, committed:
// CommittedStatus says whether one was; the smoke left out). With opacity micromaps solid surfaces only, the leaves'
// solid parts among them (an alpha test for every candidate nearer than the hit, grass by the dozen, made them cost up
// to 30 ms a frame, ATN 2026-10-29); else alpha-tested, at most MAX_CANDIDATES.
#if defined(OPACITY_MICROMAPS)
#define TRACE_SURFACE(query, ray)                                                      \
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_NON_OPAQUE) query;     \
  query.TraceRayInline(scene, RAY_FLAG_NONE, NO_SMOKE, (ray));                           \
  query.Proceed();
#else
#define TRACE_SURFACE(query, ray)                                         \
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;                   \
  query.TraceRayInline(scene, RAY_FLAG_NONE, NO_SMOKE, (ray));              \
  {                                                                         \
    uint surface_candidates = 0u;                                           \
    while (query.Proceed()) {                                               \
      if (++surface_candidates > MAX_CANDIDATES) {                          \
        query.Abort();                                                      \
      } else if (CANDIDATE_PASSES(query)) {                                 \
        query.CommitNonOpaqueTriangleHit();                                 \
      }                                                                     \
    }                                                                       \
  }
#endif

uint OctahedralEncode(float3 normal) {
  normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
  float2 encoded = normal.xy;
  if (normal.z < 0.f) encoded = (1.f - abs(normal.yx)) * select(normal.xy >= 0.f, 1.f, -1.f);
  const int2 quantized = int2(round(clamp(encoded, -1.f, 1.f) * 32767.f));
  return (uint(quantized.x) & 0xFFFFu) | (uint(quantized.y) << 16);
}

// The surface's normal from the neighbour nearer in depth on each axis (none across depth edges), facing the camera.
float3 SurfaceNormal(int2 pixel, float4 position) {
  const int2 last = int2(width, height) - 1;
  const float4 left = positions[clamp(pixel - int2(1, 0), 0, last)];
  const float4 right = positions[clamp(pixel + int2(1, 0), 0, last)];
  const float4 above = positions[clamp(pixel - int2(0, 1), 0, last)];
  const float4 below = positions[clamp(pixel + int2(0, 1), 0, last)];
  const float3 along_x = (abs(right.w - position.w) < abs(left.w - position.w)) ? right.xyz - position.xyz : position.xyz - left.xyz;
  const float3 along_y = (abs(below.w - position.w) < abs(above.w - position.w)) ? below.xyz - position.xyz : position.xyz - above.xyz;
  const float3 to_camera = sun[uint2(1, 0)].xyz - position.xyz;
  float3 normal = cross(along_y, along_x);
  const float normal_length = length(normal);
  normal = (normal_length > 0.f) ? normal / normal_length : normalize(to_camera);
  return (dot(normal, to_camera) < 0.f) ? -normal : normal;
}

// Off a surface by 2 cm plus 0.1% of the depth (depth precision and coarser far geometry grow with distance).
float SurfaceOffset(float depth) {
  return 0.02f + depth * 0.001f;
}

// Whether the four neighbours lie on the pixel's surface (`normal`: its plane): not a corner, crack or edge, where the
// normal from the depth can be wrong.
bool Planar(int2 pixel, float4 position, float3 normal) {
  static const int2 SIDES[4] = {int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
  const int2 last = int2(width, height) - 1;
  [unroll]
  for (uint side = 0u; side < 4u; ++side) {
    const float4 neighbour = positions[clamp(pixel + SIDES[side], 0, last)];
    const float3 to_neighbour = neighbour.xyz - position.xyz;
    if (neighbour.w <= 0.f || abs(dot(to_neighbour, normal)) > 0.2f * length(to_neighbour)) return false;
  }
  return true;
}

// Where a pixel's own rays leave from: off its surface along the normal where it is planar, else toward the camera,
// whose way is clear (along a wrong normal a start can land in or past the geometry beside it: light in the cracks
// where wall parts meet, ATN 2026-10-29).
float3 RayOrigin(float4 position, float3 normal, bool planar) {
  return position.xyz + (planar ? normal : normalize(sun[uint2(1, 0)].xyz - position.xyz)) * SurfaceOffset(position.w);
}

// Interleaved gradient noise (Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare"): a value
// per pixel that is even over every few neighbours, moved each frame.
float InterleavedGradientNoise(float2 pixel) {
  pixel += 5.588238f * (float)(frame & 63u);
  return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

// A point of a disk of `radius` facing `axis` (`u` in 0-1 each: uniform over it), from its centre: the sun's disk (its
// tan) for a jittered sun ray, a light's for a soft shadow.
float3 DiskPoint(float3 axis, float radius, float2 u) {
  const float3 tangent = normalize(cross((abs(axis.y) < 0.999f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f), axis));
  const float angle = u.y * 6.28318531f;
  return (tangent * cos(angle) + cross(axis, tangent) * sin(angle)) * (sqrt(u.x) * radius);
}

// A spot of the surface a planar pixel shows, `spot` (each -0.5 to 0.5) across it, from its centre: several rays a pixel
// (Ray Tracing at Output Resolution) start from the output pixels it stands for. Half its neighbours' span each way.
float3 PixelSpot(int2 pixel, float2 spot) {
  const int2 last = int2(width, height) - 1;
  const float3 across = positions[clamp(pixel + int2(1, 0), 0, last)].xyz - positions[clamp(pixel - int2(1, 0), 0, last)].xyz;
  const float3 down = positions[clamp(pixel + int2(0, 1), 0, last)].xyz - positions[clamp(pixel - int2(0, 1), 0, last)].xyz;
  return (across * spot.x + down * spot.y) * 0.5f;
}

// The `ray`th of a pixel's spots (R2 sequence turned by `turn`), each axis -0.5 to 0.5.
float2 SpotOf(uint ray, float turn) {
  return frac(float2(0.5f, 0.5f) + (float)ray * float2(0.7548777f, 0.5698403f) + turn) - 0.5f;
}

// A per-pixel, per-frame, per-ray value in 0-1 (PCG hash).
float Random(uint3 seed) {
  uint value = seed.x * 1664525u + seed.y * 22695477u + seed.z * 2891336453u + 1013904223u;
  value = value * 747796405u + 2891336453u;
  value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
  return ((value >> 22u) ^ value) / 4294967295.f;
}

// A cosine-weighted direction around `normal`.
float3 CosineDirection(float3 normal, float u, float v) {
  const float3 helper = (abs(normal.y) < 0.999f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f);
  const float3 tangent = normalize(cross(helper, normal));
  const float3 bitangent = cross(normal, tangent);
  const float radius = sqrt(u);
  const float angle = 6.2831853f * v;
  return normalize(tangent * (radius * cos(angle)) + bitangent * (radius * sin(angle)) + normal * sqrt(saturate(1.f - u)));
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SCENE_HLSLI_
