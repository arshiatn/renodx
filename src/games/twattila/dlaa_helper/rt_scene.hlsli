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
  float sun_cone;  // tan of the sun's angular radius for soft shadows (0: hard)
  uint settings_padding;
};

static const uint SUN_SHADOWS = 1u;
static const uint AMBIENT_OCCLUSION = 2u;
static const uint AMBIENT_QUARTER = 4u;  // one pixel of each 2 x 2 block traces (another each frame) for the block
static const uint LOCAL_LIGHTS = 8u;     // the local lights' shadows (rt_trace.cs_6_5.hlsl)
static const uint LEAF_LIGHT = 16u;      // leaves let a share of the sun through (rt_trace.cs_6_5.hlsl)
static const uint RECORD_MASKED = 1u;  // flags: alpha-tested (leaves)
static const uint RECORD_THIN = 2u;    // cloth and sails
// The share of the sun a leaf lets through (Light Through Leaves; thin leaves pass about a quarter, each one met).
static const float LEAF_TRANSMITTANCE = 0.25f;
static const float THIN_TRANSMITTANCE = 0.35f;  // cloth and sails

// The local lights (bridge v9; their colours v11, for the paths): world position and reach (metres), their count.
cbuffer Lights : register(b4) {
  float4 lights[32];
  uint light_count;
  float4 light_colours[32];  // rgb
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

// Whether a candidate (a leaf) is solid where the ray meets it.
bool AlphaPasses(uint record_index, uint primitive, float2 barycentrics) {
  const uint4 record = records[record_index];
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

// Whether anything lies along `direction` from `origin` within `range`.
bool Occluded(float3 origin, float3 direction, float range) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
  uint candidates = 0u;
  while (query.Proceed()) {
    if (++candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) query.CommitNonOpaqueTriangleHit();
  }
  return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// The nearest surface along a ray (its query, committed: CommittedStatus says whether one was).
#define TRACE_NEAREST(query, ray)                                     \
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;               \
  query.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, (ray));              \
  while (query.Proceed()) {                                             \
    if (CANDIDATE_PASSES(query)) query.CommitNonOpaqueTriangleHit();    \
  }

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
