// The ray-traced scene for the D3D12 passes (rt_shadows.hpp): the TLAS, each instance's record (InstanceID), the
// alpha-tested meshes' UVs and opacity masks, every triangle's normal; traversal with the alpha test, and the sampling
// helpers the passes share. World units: UE's centimetres.
//
// Alpha-tested meshes (the masked depth prepass: leaves, grass, fences, hair) are non-opaque: a candidate triangle is a
// hit where its opacity mask (the material's t0 alpha, copied into an atlas slice) passes the material's threshold at
// the hit's UV. With DXR 1.2 (shader model 6.9) their opacity micromaps (0x7A1E0D1A) answer for most of the triangle in
// hardware and only the micro-triangles the mask crosses come back as candidates.
#ifndef SRC_GAMES_TALESOFARISE_DLSS_RT_SCENE_HLSLI_
#define SRC_GAMES_TALESOFARISE_DLSS_RT_SCENE_HLSLI_

#include "./rt_surface.hlsli"

RaytracingAccelerationStructure scene : register(t0);
// Per record (an instance's InstanceID): UV offset in uv_pool, normal offset in normal_pool, atlas slice | flags << 16,
// the alpha threshold (asfloat).
Buffer<uint4> records : register(t7);
Buffer<uint> uv_pool : register(t8);      // per vertex (three per triangle): the UV as two halves
Buffer<uint> normal_pool : register(t9);  // per triangle: its normal, octahedral, two 16-bit snorms
Texture2DArray<float> alpha_atlas : register(t10);
SamplerState alpha_sampler : register(s0);  // linear, wrapping (as the materials' samplers)

static const uint RECORD_MASKED = 1u;  // flags: alpha-tested

#if __SHADER_TARGET_MAJOR > 6 || (__SHADER_TARGET_MAJOR == 6 && __SHADER_TARGET_MINOR >= 9)
#define SCENE_QUERY(query_flags) RayQuery<(query_flags), RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS>
#else
#define SCENE_QUERY(query_flags) RayQuery<(query_flags)>
#endif

float2 UnpackUv(uint packed) {
  return f16tof32(uint2(packed, packed >> 16));
}

// Whether an alpha-tested candidate is solid where the ray meets it (always for opaque records).
bool AlphaPasses(uint instance, uint primitive, float2 barycentrics) {
  const uint4 record = records[instance];
  if (((record.z >> 16) & RECORD_MASKED) == 0u) return true;
  const uint first = record.x + primitive * 3u;
  const float2 uv = UnpackUv(uv_pool[first]) * (1.f - barycentrics.x - barycentrics.y)
                    + UnpackUv(uv_pool[first + 1u]) * barycentrics.x + UnpackUv(uv_pool[first + 2u]) * barycentrics.y;
  return alpha_atlas.SampleLevel(alpha_sampler, float3(uv, (float)(record.z & 0xFFFFu)), 0.f) >= asfloat(record.w);
}

// Whether anything of `mask`'s instances lies along `direction` from `origin` within `range`.
bool Occluded(float3 origin, float3 direction, float range, uint mask) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, mask, ray);
  while (query.Proceed()) {
    if (AlphaPasses(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics())) {
      query.CommitNonOpaqueTriangleHit();
    }
  }
  return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// The nearest surface of `mask`'s instances along a ray (its query, committed: CommittedStatus says whether one was).
#define TRACE_NEAREST(query, ray, mask)                                                                                  \
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;                                                                  \
  query.TraceRayInline(scene, RAY_FLAG_NONE, (mask), (ray));                                                               \
  while (query.Proceed()) {                                                                                                \
    if (AlphaPasses(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics())) { \
      query.CommitNonOpaqueTriangleHit();                                                                                  \
    }                                                                                                                      \
  }

float3 OctahedralDecode(uint packed) {
  const float2 encoded = float2(int2(packed << 16, packed) >> 16) / 32767.f;
  float3 normal = float3(encoded, 1.f - abs(encoded.x) - abs(encoded.y));
  if (normal.z < 0.f) normal.xy = (1.f - abs(normal.yx)) * select(normal.xy >= 0.f, 1.f, -1.f);
  return normalize(normal);
}

uint OctahedralEncode(float3 normal) {
  normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
  float2 encoded = normal.xy;
  if (normal.z < 0.f) encoded = (1.f - abs(normal.yx)) * select(normal.xy >= 0.f, 1.f, -1.f);
  const int2 quantized = int2(round(clamp(encoded, -1.f, 1.f) * 32767.f));
  return (uint(quantized.x) & 0xFFFFu) | (uint(quantized.y) << 16);
}

// A hit triangle's normal, facing back along the ray.
float3 HitNormal(uint instance, uint primitive, float3 direction) {
  const float3 normal = OctahedralDecode(normal_pool[records[instance].y + primitive]);
  return (dot(normal, direction) > 0.f) ? -normal : normal;
}

// A per-pixel, per-frame, per-ray value in 0-1 (PCG hash).
float Random(uint3 seed) {
  uint value = seed.x * 1664525u + seed.y * 22695477u + seed.z * 2891336453u + 1013904223u;
  value = value * 747796405u + 2891336453u;
  value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
  return ((value >> 22u) ^ value) / 4294967295.f;
}

// Tangent, bitangent and `normal`.
float3x3 Basis(float3 normal) {
  const float3 helper = (abs(normal.z) < 0.999f) ? float3(0.f, 0.f, 1.f) : float3(1.f, 0.f, 0.f);
  const float3 tangent = normalize(cross(helper, normal));
  return float3x3(tangent, cross(normal, tangent), normal);
}

// A cosine-weighted direction around `normal` (a disk sample lifted onto the hemisphere).
float3 CosineDirection(float3 normal, float u, float v) {
  const float3x3 basis = Basis(normal);
  const float radius = sqrt(u);
  const float angle = 6.2831853f * v;
  return normalize(basis[0] * (radius * cos(angle)) + basis[1] * (radius * sin(angle)) + normal * sqrt(saturate(1.f - u)));
}

// A direction within the cone of half-angle tangent `cone` around `axis` (uniform over its disk).
float3 ConeDirection(float3 axis, float cone, float u, float v) {
  const float3x3 basis = Basis(axis);
  const float radius = cone * sqrt(u);
  const float angle = 6.2831853f * v;
  return normalize(axis + basis[0] * (radius * cos(angle)) + basis[1] * (radius * sin(angle)));
}

// A GGX microfacet normal seen from `view` (toward the eye, in the basis of the surface normal: z up) for roughness
// `alpha` (visible normals, Dupuy and Benyoub 2023's spherical caps).
float3 GgxVisibleNormal(float3 view, float alpha, float u, float v) {
  const float3 stretched = normalize(float3(alpha * view.xy, view.z));
  const float angle = 6.2831853f * u;
  const float z = (1.f - v) * (1.f + stretched.z) - stretched.z;
  const float sin_theta = sqrt(saturate(1.f - z * z));
  const float3 cap = float3(sin_theta * cos(angle), sin_theta * sin(angle), z) + stretched;
  return normalize(float3(alpha * cap.xy, max(cap.z, 0.f)));
}

// Smith's height-correlated masking-shadowing over the view's masking (the weight of a visible-normal sample).
float GgxSampleWeight(float n_dot_v, float n_dot_l, float alpha) {
  const float alpha2 = alpha * alpha;
  const float lambda_v = n_dot_l * sqrt(alpha2 + (1.f - alpha2) * n_dot_v * n_dot_v);
  const float lambda_l = n_dot_v * sqrt(alpha2 + (1.f - alpha2) * n_dot_l * n_dot_l);
  const float g2 = 2.f * n_dot_l * n_dot_v / max(lambda_v + lambda_l, 1e-6f);
  const float g1 = 2.f * n_dot_v / max(n_dot_v + sqrt(alpha2 + (1.f - alpha2) * n_dot_v * n_dot_v), 1e-6f);
  return g2 / max(g1, 1e-6f);
}

#endif  // SRC_GAMES_TALESOFARISE_DLSS_RT_SCENE_HLSLI_
