/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's reference build (tests only: build.ps1 with RENODX_TW_REFERENCE_BUILD; a player's helper has none; the
// design in tools/tw/round_notes/plan_1117/reference_suite.md). Brute-force integrals of the effects on frozen copies of a
// frame's inputs, the candidate's own output accumulated beside them, a census of every output, a digest of the scene.
// Its own code apart from the estimators': its own random stream (RefRandom's own inputs, lane .x of the shared Pcg4d in
// rt_scene.hlsli; the estimators' Random reads lane .y), its own sampling (concentric disk and Malley, Duff et al.'s
// basis, Heitz 2018's visible normals), its own starts (Waechter and Binder 2019) and uncapped visibility loops; shared
// with the helper only the scene (both TLASes, records, pools, the alpha atlas) and the model terms of the design's table
// 1.1 (the ambient cube, OpenSky, LightArriving, HitAlbedo, ReflectAlpha). rt_d3d12.hpp binds the frozen copies at the
// live registers for its slices (a second descriptor table), so every function here and every shared model term reads
// the frozen frame and nothing the helper rewrites.
// World units: metres, y up.
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_REFERENCE_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_REFERENCE_HLSLI_

#include "./rt_path.hlsli"

// A dispatch's constants (b1): the entry's word (REF_ACCUMULATE: the output; REF_CENSUS: the output; REF_DIGEST: its lane;
// REF_STATE: the operation; a slice: the variant), then the entry's own.
cbuffer Reference : register(b1) {
  uint ref_word;
  uint ref_a;  // a slice: its samples a texel; REF_ACCUMULATE: the frame in its batch; REF_DIGEST: the words
  uint ref_b;  // a slice: its first sample in the group; REF_ACCUMULATE: the batch; REF_DIGEST: the CPU's hash
  uint ref_c;  // a slice: the group; REF_GROUP: the groups finished with it
  uint ref_d;  // a slice: the samples a group; REF_GROUP: the slices it should hold
  uint ref_e;  // a slice: the run's seed
  uint ref_f;  // the grid's width
  uint ref_g;  // the grid's height
};
// More of a slice's (b2): the variant's parameters (asfloat).
cbuffer ReferenceParameters : register(b2) {
  float4 ref_parameters[5];
};

// The candidate's statistics (B1): per texel the current 16-frame batch's sum, then Welford's mean and M2 over the batch
// means shifted by the first batch's (no E[x^2] - E[x]^2 cancellation where a history flattens the series), the sum of
// products of consecutive shifted batch means (their lag-1 correlation), the last batch's shifted mean, the last frame;
// a colour's w its luminance (not the bounce's camera distance).
RWTexture2D<float4> ref_candidate_batch : register(u60);
RWTexture2D<float4> ref_candidate_shift : register(u61);
RWTexture2D<float4> ref_candidate_mean : register(u62);
RWTexture2D<float4> ref_candidate_m2 : register(u63);
RWTexture2D<float4> ref_candidate_cross : register(u64);
RWTexture2D<float4> ref_candidate_previous : register(u65);
RWTexture2D<float4> ref_candidate_last : register(u66);
// The reference's: the current group's sum, Welford's mean and M2 over the group means, each group's mean (grids up to
// 1920 x 1080), the aux counts (x the primary surfaces met, y the rays past a cap or an overflow, z the paths cut at the
// vertex cap, w the slices' sample count).
RWTexture2D<float4> ref_group_sum : register(u67);
RWTexture2D<float4> ref_mean : register(u68);
RWTexture2D<float4> ref_m2 : register(u69);
RWTexture2DArray<float4> ref_groups : register(u70);
RWTexture2D<uint4> ref_aux : register(u71);
// The run's state words (REF_STATE_*).
RWBuffer<uint> ref_state : register(u72);
// REF_DIGEST's input: a raw range of a buffer (the TLAS's instance descs, the records) at the capture table's root SRV.
ByteAddressBuffer ref_digest_input : register(t8);
// The trace's AO output (rt_trace.cs_6_5.hlsl's u1): accumulated and counted.
RWTexture2D<unorm float> ref_ambient_output : register(u1);

// ref_state's words (rt_d3d12.hpp's REF_STATE_* the same): the census (four counts an output: NaN, Inf, below 0, above
// FP16's most), each output's first texel counted (x | y << 16, + 1), the digest's lanes (this frame's, the freeze
// frame's, the last frame's), the digest's counts, the slices each group holds.
static const uint REF_STATE_CENSUS = 0u;
static const uint REF_CENSUS_OUTPUTS = 16u;
static const uint REF_STATE_FIRST_BAD = 64u;
static const uint REF_STATE_DIGEST = 128u;         // 4 words: this frame's
static const uint REF_STATE_DIGEST_FROZEN = 132u;  // 4 words: the freeze frame's
static const uint REF_STATE_DIGEST_LAST = 136u;    // 4 words: the frame before's
static const uint REF_STATE_DIGEST_FLAGS = 140u;   // 1: the frozen digest set, 2: the last one set
static const uint REF_STATE_DIGEST_CHANGES = 141u; // frames whose digest differed from the frame before's
static const uint REF_STATE_MISMATCH = 142u;       // this frame's digest differs from the freeze frame's
static const uint REF_STATE_DROPPED = 143u;        // slices dropped for it
static const uint REF_STATE_GROUP_SLICES = 144u;   // 64 words: the slices each group holds
static const uint REF_STATE_GROUPS_DONE = 208u;    // the groups finished (a group whose every slice was dropped is none)
static const uint REF_STATE_WORDS = 256u;
// REF_STATE's operations (its word).
static const uint REF_OP_CLEAR = 0u;         // words ref_a .. ref_a + ref_b - 1 to 0
static const uint REF_OP_DIGEST_BEGIN = 1u;  // this frame's digest lanes to 0
static const uint REF_OP_DIGEST_END = 2u;    // the CPU's hash (ref_a, ref_b) mixed in; compared with the frame before's and the frozen
static const uint REF_OP_DIGEST_FREEZE = 3u; // this frame's digest kept as the freeze frame's
static const uint REF_OP_GROUP_DONE = 4u;    // group ref_a counted as finished if any slice of it ran

// The census's outputs (REF_CENSUS's word).
static const uint REF_OUT_BOUNCE = 0u;
static const uint REF_OUT_REFLECTION = 1u;
static const uint REF_OUT_SPECULAR = 2u;   // specular_accumulated
static const uint REF_OUT_OWN_SKY = 3u;    // both sets: 3, 4
static const uint REF_OUT_REUSE = 5u;      // ReSTIR GI's W and light, both sets: 5, 6
static const uint REF_OUT_FOG_LIGHTS = 7u;
static const uint REF_OUT_REF_MEAN = 8u;
static const uint REF_OUT_CANDIDATE_MEAN = 9u;
static const uint REF_OUT_REF_M2 = 10u;
static const uint REF_OUT_CANDIDATE_M2 = 11u;

// REF_ACCUMULATE's sources (its word).
static const uint REF_SOURCE_BOUNCE = 0u;
static const uint REF_SOURCE_REFLECTION = 1u;
static const uint REF_SOURCE_SPECULAR_OCCLUSION = 2u;
static const uint REF_SOURCE_VISIBILITY = 3u;
static const uint REF_SOURCE_AMBIENT = 4u;
static const uint REF_BATCH = 16u;

// A slice's variants (its word's low byte): `full` (the effect's integral, the reference's own start and the game's values),
// `same_start` (the effect's own start and normal: isolates everything after the start), `furnace` (every surface albedo
// ref_parameters[0].x, lit by ref_parameters[0].y, no sun, sky or lights: G1), `lout` (the light leaving each texel's own
// surface under the screen route's cut: G5), `contact` (`full` with the helper's ContactShade on the first ray: the model
// variant that tells that named model delta from the estimator, I2), `same_route` (`contact` with the helper's route model:
// a surface the screen or the cache knows sends their light, an unseen one its sun (one ray to the centre) and its first
// 32 lights and goes on path_bounces surfaces, ending as its base colour x KeptShade; past the first vertex the sky is
// SkyLight's x SkyCalibration at the surface left; the design's 1.2: the candidate against it is the estimator alone,
// `contact` against it the routes' model), `contact_sky` (`contact` with the helper's sky past the first vertex,
// PathRadiance's SkyLight x SkyCalibration: the sky model alone, Kept Cube's or the learned scale's).
static const uint REF_FULL = 0u;
static const uint REF_SAME_START = 1u;
static const uint REF_FURNACE = 2u;
static const uint REF_LOUT = 3u;
static const uint REF_CONTACT = 4u;
static const uint REF_SAME_ROUTE = 5u;
static const uint REF_CONTACT_SKY = 6u;
// Its synthetic control (the word's bit 8): the reference x ref_parameters[1].x.
static const uint REF_SCALED = 256u;
// The word's bit 9 (RENODX_TW_REF_DIRECT): each sample also brings the texel's own direct light of the first BOUNCE_LIGHTS
// local lights (rt_scene.hlsli BOUNCE_LIGHTS, 32: their exact sum, each with its own ray): what Torch and Fire Light from
// Path Tracing adds to the bounce.
static const uint REF_DIRECT = 512u;

static const float REF_PI = 3.14159265f;
static const uint REF_MAX_VERTICES = 32u;  // a path's vertex cap (its paths cut there are counted)
static const uint REF_ROULETTE_FROM = 3u;  // Russian roulette after this many vertices
static const uint REF_MAX_CANDIDATES = 4096u;  // a visibility loop's candidates at most (past it counted, and blocked)

// A number in [0, 1) of a texel, a sample (its index over the whole reference: group and place in it), a dimension and the
// run's seed: every texel's sequence its own (G3 checks that neighbours share no noise).
float RefRandom(uint2 texel, uint sample_index, uint dimension) {
  const uint4 hashed = Pcg4d(uint4(texel, sample_index, dimension ^ (ref_e * 0x9E3779B9u)));
  return (float)(hashed.x >> 8) * (1.f / 16777216.f);
}

// Shirley and Chiu 1997's concentric map of the unit square onto the unit disk.
float2 RefConcentricDisk(float2 u) {
  const float2 offset = 2.f * u - 1.f;
  if (all(offset == 0.f)) return 0.f;
  if (abs(offset.x) > abs(offset.y)) {
    const float angle = (REF_PI / 4.f) * (offset.y / offset.x);
    return offset.x * float2(cos(angle), sin(angle));
  }
  const float angle = (REF_PI / 2.f) - (REF_PI / 4.f) * (offset.x / offset.y);
  return offset.y * float2(cos(angle), sin(angle));
}

// Duff et al. 2017 ("Building an Orthonormal Basis, Revisited"): two tangents of a unit `normal`.
void RefBasis(float3 normal, out float3 tangent, out float3 bitangent) {
  const float sign = (normal.z >= 0.f) ? 1.f : -1.f;
  const float a = -1.f / (sign + normal.z);
  const float b = normal.x * normal.y * a;
  tangent = float3(1.f + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
  bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);
}

// A cosine-weighted direction around `normal` (Malley's method over the concentric disk); its density is n.w / pi.
float3 RefCosineDirection(float3 normal, float2 u) {
  const float2 disk = RefConcentricDisk(u);
  float3 tangent;
  float3 bitangent;
  RefBasis(normal, tangent, bitangent);
  return normalize(tangent * disk.x + bitangent * disk.y + normal * sqrt(max(1.f - dot(disk, disk), 0.f)));
}

// The `index`th of `count` stratified points of the unit square (a grid of floor(sqrt(count)) a side, the rest jittered
// alone), its jitter `u`.
float2 RefStratified(uint index, uint count, float2 u) {
  const uint side = max((uint)floor(sqrt((float)max(count, 1u))), 1u);
  if (index >= side * side) return u;
  return (float2(index % side, index / side) + u) / (float)side;
}

// Heitz 2018 ("Sampling the GGX Distribution of Visible Normals"): a microfacet normal of width `alpha` seen from `view`
// (local frame, z the surface normal).
float3 RefVisibleNormal(float3 view, float alpha, float2 u) {
  const float3 stretched = normalize(float3(alpha * view.x, alpha * view.y, view.z));
  const float length_squared = stretched.x * stretched.x + stretched.y * stretched.y;
  const float3 first = (length_squared > 0.f) ? float3(-stretched.y, stretched.x, 0.f) * rsqrt(length_squared) : float3(1.f, 0.f, 0.f);
  const float3 second = cross(stretched, first);
  const float radius = sqrt(u.x);
  const float angle = 2.f * REF_PI * u.y;
  const float along_first = radius * cos(angle);
  const float blend = 0.5f * (1.f + stretched.z);
  const float along_second = (1.f - blend) * sqrt(max(1.f - along_first * along_first, 0.f)) + blend * radius * sin(angle);
  const float3 hemisphere = along_first * first + along_second * second
                            + sqrt(max(1.f - along_first * along_first - along_second * along_second, 0.f)) * stretched;
  return normalize(float3(alpha * hemisphere.x, alpha * hemisphere.y, max(hemisphere.z, 0.f)));
}

// Smith's Lambda for GGX of width `alpha` at a direction of cosine `cosine` to the normal.
float RefSmithLambda(float cosine, float alpha) {
  const float c2 = max(cosine * cosine, 1e-12f);
  return 0.5f * (-1.f + sqrt(1.f + alpha * alpha * (1.f - c2) / c2));
}

// A visible-normal sample's weight: Smith's height-correlated G2 over the view's G1 (0 below the horizon).
float RefVisibleNormalWeight(float view_cosine, float light_cosine, float alpha) {
  if (light_cosine <= 0.f || view_cosine <= 0.f) return 0.f;
  const float lambda_view = RefSmithLambda(view_cosine, alpha);
  return (1.f + lambda_view) / (1.f + lambda_view + RefSmithLambda(light_cosine, alpha));
}

// Waechter and Binder 2019 ("A Fast and Robust Method for Avoiding Self-Intersection", Ray Tracing Gems ch. 6): a point off
// a surface along its normal by a few units in the last place (a fixed step near the origin).
float3 RefOffset(float3 position, float3 normal) {
  const int3 steps = int3(256.f * normal);
  const float3 stepped = asfloat(asint(position) + select(position < 0.f, -steps, steps));
  return select(abs(position) < (1.f / 32.f), position + (1.f / 65536.f) * normal, stepped);
}

// Whether an opaque or alpha-passing surface among `instances` lies along a ray within `range`: every candidate
// alpha-tested, no cap but REF_MAX_CANDIDATES (past it blocked and counted in `overflow`).
bool RefBlocked(float3 origin, float3 direction, float range, uint instances, inout uint overflow) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, instances, ray);
  uint candidates = 0u;
  while (query.Proceed()) {
    if (++candidates > REF_MAX_CANDIDATES) {
      ++overflow;
      query.CommitNonOpaqueTriangleHit();
    } else if (CANDIDATE_PASSES(query)) {
      query.CommitNonOpaqueTriangleHit();
    }
  }
  return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// The transmittance toward the sun along a ray: 0 at an opaque or alpha-passing solid, x LEAF_TRANSMITTANCE a leaf layer and
// x THIN_TRANSMITTANCE a sheet's with Light Through Leaves (else they block), every layer met counted (no cap but
// REF_MAX_CANDIDATES); the smoke's TLAS's sun-only cards and sprites block where their alpha test passes. Opaque solids
// commit themselves; a layer beyond the final opaque hit would count but its product is multiplied by 0 then.
float RefSunTransmittance(float3 origin, float3 direction, float range, inout uint overflow) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, ALL_INSTANCES & ~SMOKE, ray);
  float transmittance = 1.f;
  uint candidates = 0u;
  while (query.Proceed()) {
    if (++candidates > REF_MAX_CANDIDATES) {
      ++overflow;
      query.CommitNonOpaqueTriangleHit();
      query.Abort();
      break;
    }
    const uint record_index = query.CandidateInstanceID() + query.CandidateGeometryIndex();
    if (!AlphaPasses(record_index, query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics())) continue;
    const uint record_flags = records[record_index].z >> 16;
    const bool leaf = (record_flags & (RECORD_MASKED | RECORD_GRASS)) == RECORD_MASKED;
    const bool sheet = (record_flags & RECORD_THIN) != 0u;
    if ((flags & LEAF_LIGHT) != 0u && (leaf || sheet)) {
      transmittance *= leaf ? LEAF_TRANSMITTANCE : THIN_TRANSMITTANCE;
    } else {
      query.CommitNonOpaqueTriangleHit();
    }
  }
  if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) return 0.f;
  if ((flags & SMOKE_MET) == 0u) return transmittance;
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> cards;
  cards.TraceRayInline(smoke_scene, RAY_FLAG_NONE, SMOKE, ray);
  while (cards.Proceed()) {
    const uint record_index = cards.CandidateInstanceID() + cards.CandidateGeometryIndex();
    if (((records[record_index].z >> 16) & RECORD_SMOKE) != 0u) continue;
    if (AlphaPasses(record_index, cards.CandidatePrimitiveIndex(), cards.CandidateTriangleBarycentrics())) cards.CommitNonOpaqueTriangleHit();
  }
  return (cards.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.f : transmittance;
}

// The nearest surface along a ray among `instances`, every candidate alpha-tested (no cap but REF_MAX_CANDIDATES): its
// distance (false: none), its record and normal facing back along the ray (the record's pool normal: the shared scene).
bool RefNearest(float3 origin, float3 direction, float range, uint instances, out float hit_distance, out uint record_index, out float3 hit_normal,
                inout uint overflow) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, instances, ray);
  uint candidates = 0u;
  while (query.Proceed()) {
    if (++candidates > REF_MAX_CANDIDATES) {
      ++overflow;
      query.Abort();
      break;
    }
    if (CANDIDATE_PASSES(query)) query.CommitNonOpaqueTriangleHit();
  }
  hit_distance = 0.f;
  record_index = 0u;
  hit_normal = 0.f;
  if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
  hit_distance = query.CommittedRayT();
  record_index = query.CommittedInstanceID() + query.CommittedGeometryIndex();
  hit_normal = HitNormal(record_index, query.CommittedPrimitiveIndex(), direction, query.CommittedObjectToWorld3x4());
  return true;
}

// A texel's primary surface as the reference sees it: the camera's ray toward its frozen position, to half a metre past it,
// against the solids (the G-buffer's surface): its exact point and geometric normal facing the camera (false: nothing
// there within the half metre either side, a coverage gap: the position and its depth normal then).
bool RefPrimary(uint2 pixel, float4 position, out float3 surface, out float3 normal, inout uint overflow) {
  const float3 camera = Camera();
  const float3 to_surface = position.xyz - camera;
  const float distance_to = length(to_surface);
  const float3 direction = to_surface / max(distance_to, 1e-6f);
  float hit_distance;
  uint record_index;
  float3 hit_normal;
  const bool met = RefNearest(camera + direction * max(distance_to - 0.5f, 0.f), direction, (distance_to > 0.5f) ? 1.f : distance_to + 0.5f, NO_GRASS_SMOKE,
                              hit_distance, record_index, hit_normal, overflow);
  surface = position.xyz;
  normal = SurfaceNormal(int2(pixel), position);
  if (!met) return false;
  surface = camera + direction * (max(distance_to - 0.5f, 0.f) + hit_distance);
  normal = hit_normal;
  return true;
}

// The cosine mean of the game's ambient cube over the hemisphere around `normal`: AmbientCube is each face's colour by the
// direction's squared component on that face's side, and under cosine-weighted directions E[d_a^2] = (1 + n_a^2) / 4 and
// E[d_a |d_a|] = n_a / 2, so a+ weighs their half sum and a- their half difference.
float3 RefCubeCosineMean(float3 normal) {
  const float3 square_mean = (1.f + normal * normal) * 0.25f;
  const float3 signed_mean = normal * 0.5f;
  const float3 positive = 0.5f * (square_mean + signed_mean);
  const float3 negative = 0.5f * (square_mean - signed_mean);
  return sun[uint2(3, 0)].rgb * positive.x + sun[uint2(4, 0)].rgb * negative.x + sun[uint2(5, 0)].rgb * positive.y + sun[uint2(6, 0)].rgb * negative.y
         + sun[uint2(7, 0)].rgb * positive.z + sun[uint2(8, 0)].rgb * negative.z;
}

// The sky's light along `direction` seen from a surface facing `normal`, as the game gives it (`full`): the cube by the
// direction, scaled so the surface's open sky is OpenSky(normal) (the game's ambient there; no learned value); the learned
// shade where the game gave no cube.
float3 RefSky(float3 direction, float3 normal) {
  if (sun[uint2(3, 0)].w == 0.f) return light_kept[0].rgb;
  return AmbientCube(direction) * OpenSky(normal) / max(RefCubeCosineMean(normal), 1e-6f);
}

// The game's sun light (`full`: the sun texture's texel 2 where the addon wrote it, else the learned one).
float3 RefSunLight() {
  const float4 given = sun[uint2(SUN_LIGHT_TEXEL, 0)];
  return (given.w != 0.f) ? given.rgb : light_kept[1].rgb;
}

// The sun's visibility from a surface: one point of its disk (radius sun_cone, by `u`) through RefSunTransmittance.
float RefSunVisibility(float3 surface, float3 normal, float2 u, inout uint overflow) {
  const float4 toward_sun = sun[uint2(0, 0)];
  if (toward_sun.w == 0.f || dot(normal, toward_sun.xyz) <= 0.f) return 0.f;
  float3 tangent;
  float3 bitangent;
  RefBasis(toward_sun.xyz, tangent, bitangent);
  const float2 disk = RefConcentricDisk(u) * sun_cone;
  const float3 direction = normalize(toward_sun.xyz + tangent * disk.x + bitangent * disk.y);
  if (dot(normal, direction) <= 0.f) return 0.f;
  return RefSunTransmittance(surface, direction, 1.0e4f, overflow);
}

// Every local light's direct light at a surface (pbrt's sum over all lights, each with its own ray; the first `most` of b4's:
// all 128 in `full`, the paths' first 32 in `same_route`): the game's falloff by LightArriving, the ray stopping
// LIGHT_CLEARANCE short; leaves block, grass and smoke pass.
float3 RefLights(float3 surface, float3 normal, uint most, inout uint overflow) {
  float3 total = 0.f;
  [loop]
  for (uint index = 0u; index < min(light_count, most); ++index) {
    float3 direction;
    float light_distance;
    const float3 arriving = LightArriving(index, surface, normal, direction, light_distance);
    if (all(arriving <= 0.f)) continue;
    if (!RefBlocked(surface, direction, light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance), NO_GRASS_SMOKE, overflow)) total += arriving;
  }
  return total;
}

// A record's share of light through it (the helper's model, 1.3): a leaf's, a sheet's, else none.
float RefTransmittance(uint record_index) {
  const uint record_flags = records[record_index].z >> 16;
  if ((record_flags & (RECORD_MASKED | RECORD_GRASS)) == RECORD_MASKED) return LEAF_TRANSMITTANCE;
  return ((record_flags & RECORD_THIN) != 0u) ? THIN_TRANSMITTANCE : 0.f;
}

// The light leaving a surface at `hit` (normal `normal` facing the ray that met it, `record_index`'s flags) by its own
// emission, as the game's model gives it (`full`): its base colour (HitAlbedo, frozen) x the sun's light (from behind it
// through a leaf or a sheet at their transmittance: the design's 1.3) and every local light's; the furnace's a x E.
float3 RefEmitted(float3 hit, float3 normal, uint record_index, float3 albedo, bool furnace, float2 u, inout uint overflow) {
  if (furnace) return albedo * ref_parameters[0].y;
  const float transmittance = RefTransmittance(record_index);
  const float3 toward_sun = sun[uint2(0, 0)].xyz;
  const float n_dot_l = dot(normal, toward_sun);
  float3 emitted = 0.f;
  if (n_dot_l > 0.f) {
    emitted = RefSunLight() * n_dot_l * RefSunVisibility(RefOffset(hit, normal), normal, u, overflow);
  } else if (transmittance > 0.f) {
    emitted = RefSunLight() * (-n_dot_l * transmittance) * RefSunVisibility(RefOffset(hit, -normal), -normal, u, overflow);
  }
  if ((path_flags & PATH_LOCAL_LIGHTS) != 0u) emitted += RefLights(RefOffset(hit, normal), normal, 128u, overflow);
  return albedo * emitted;
}

// The light arriving at `origin` along `direction` (the integral's Li): Russian roulette after REF_ROULETTE_FROM vertices,
// REF_MAX_VERTICES at most (a path cut there counted in `cut`); one sun disk point and every local light at each vertex;
// an escape brings the game's sky at the surface it left (RefSky; none from the first ray without Path-Traced Ambient,
// whose screen pixel the game's ambient lights then, or where grass within GRASS_AMBIENT_RANGE blocks it, as the AO's;
// the furnace's box has no sky). The first ray reaches `first_range` (the integral's), the rest 10 km. `contact`: the
// first ray's hit's light (and all beyond it) x ContactShade (the `contact` variant). `helper_sky`: past the first vertex
// the sky as the helper's paths take it (PathRadiance: SkyLight x SkyCalibration at the surface left; `contact_sky`).
float3 RefIncoming(float3 origin, float3 direction, float3 origin_normal, float first_range, bool furnace, bool contact, bool helper_sky,
                   uint2 texel, uint sample_index, inout uint overflow, inout uint cut) {
  float3 radiance = 0.f;
  float3 throughput = 1.f;
  float3 leaving_normal = origin_normal;
  [loop]
  for (uint vertex = 0u; vertex < REF_MAX_VERTICES; ++vertex) {
    const uint dimension = 8u + vertex * 8u;
    float hit_distance;
    uint record_index;
    float3 normal;
    if (!RefNearest(origin, direction, (vertex == 0u) ? first_range : 1.0e4f, NO_GRASS_SMOKE, hit_distance, record_index, normal, overflow)) {
      const bool sky_seen = !furnace && (vertex > 0u || ((path_flags & PATH_AMBIENT) != 0u
                                                          && !RefBlocked(origin, direction, min(first_range, GRASS_AMBIENT_RANGE), GRASS_STILL, overflow)));
      if (sky_seen) {
        radiance += throughput * ((helper_sky && vertex > 0u) ? SkyLight(direction, KeptShade()) * SkyCalibration(leaving_normal) : RefSky(direction, leaving_normal));
      }
      break;
    }
    const float3 hit = origin + direction * hit_distance;
    if (contact && vertex == 0u) throughput *= ContactShade(hit_distance);
    const float3 albedo = furnace ? ref_parameters[0].xxx : HitAlbedo(hit, normal);
    const float2 sun_point = float2(RefRandom(texel, sample_index, dimension), RefRandom(texel, sample_index, dimension + 1u));
    radiance += throughput * RefEmitted(hit, normal, record_index, albedo, furnace, sun_point, overflow);
    throughput *= albedo;
    if (vertex + 1u >= REF_ROULETTE_FROM) {
      const float survival = min(max(throughput.r, max(throughput.g, throughput.b)), 0.95f);
      if (RefRandom(texel, sample_index, dimension + 4u) >= survival) break;
      throughput /= survival;
    }
    if (vertex + 1u == REF_MAX_VERTICES) {
      ++cut;
      break;
    }
    // Through a leaf or a sheet at its transmittance (the helper's model, 1.3): the path goes on from behind it.
    const float transmittance = furnace ? 0.f : RefTransmittance(record_index);
    const bool through = transmittance > 0.f && RefRandom(texel, sample_index, dimension + 5u) < transmittance;
    leaving_normal = through ? -normal : normal;
    origin = RefOffset(hit, leaving_normal);
    direction = RefCosineDirection(leaving_normal, float2(RefRandom(texel, sample_index, dimension + 2u), RefRandom(texel, sample_index, dimension + 3u)));
  }
  return radiance;
}

// `same_route`'s light arriving at `origin` along `direction`: the helper's route model (rt_path.hlsli PathRadiance's, the
// design's 1.2) with the reference's own numbers, starts and uncapped visibility. An escape: the sky as SkyLight x
// SkyCalibration at the surface the ray left (the first ray's at `origin_normal`, blocked by grass within
// GRASS_AMBIENT_RANGE); a hit x ContactShade on
// the first ray; a surface the screen shows (or the cache knows, but CACHE_RELEARN of them, from a cell's size away)
// sends HitLight's; an unseen one SunLit's (its one ray to the sun's centre) and its first 32 lights (where UnseenLight
// takes them: their exact sum, LocalLights' two picks' mean) and goes on path_bounces surfaces, the last x KeptShade.
float3 RefRouteIncoming(float3 origin, float3 direction, float3 origin_normal, float first_range, uint2 texel, uint sample_index, inout uint overflow) {
  float3 radiance = 0.f;
  float3 throughput = 1.f;
  float3 leaving_normal = 0.f;  // past the first vertex: the surface the ray left
  const float3 shade = KeptShade();
  const bool unseen_lights = (path_flags & PATH_LOCAL_LIGHTS) != 0u
                             && !(SunLightGiven() && (path_flags & (PATH_LIGHT | PATH_LIGHT_FINAL)) == PATH_LIGHT && !TORCH_LIGHT_IN_PATHS);
  [loop]
  for (uint vertex = 0u; vertex <= path_bounces; ++vertex) {
    const uint dimension = 8u + vertex * 8u;
    float hit_distance;
    uint record_index;
    float3 normal;
    if (!RefNearest(origin, direction, (vertex == 0u) ? first_range : bounce_range, NO_GRASS_SMOKE, hit_distance, record_index, normal, overflow)) {
      if (vertex > 0u) {
        radiance += throughput * SkyLight(direction, shade) * SkyCalibration(leaving_normal);
      } else if ((path_flags & PATH_AMBIENT) != 0u && !RefBlocked(origin, direction, min(first_range, GRASS_AMBIENT_RANGE), GRASS_STILL, overflow)) {
        radiance += SkyLight(direction, shade) * SkyCalibration(origin_normal);
      }
      break;
    }
    const float3 hit = origin + direction * hit_distance;
    if (vertex == 0u) throughput *= ContactShade(hit_distance);
    const bool relearn = RefRandom(texel, sample_index, dimension + 6u) < CACHE_RELEARN;
    float3 light_there;
    if (HitLight(hit, normal, light_there, !relearn && hit_distance >= CacheCellSize(hit))) {
      radiance += throughput * light_there;
      break;
    }
    const float3 albedo = HitAlbedo(hit, normal);
    const float transmittance = RefTransmittance(record_index);
    float3 emitted = SunLit(RefOffset(hit, normal), RefOffset(hit, -normal), normal, albedo, transmittance, -1.f);
    if (unseen_lights) emitted += albedo * RefLights(RefOffset(hit, normal), normal, 32u, overflow);
    if (vertex == path_bounces) {
      radiance += throughput * (emitted + albedo * shade);
      break;
    }
    radiance += throughput * emitted;
    throughput *= albedo;
    const bool through = transmittance > 0.f && RefRandom(texel, sample_index, dimension + 5u) < transmittance;
    leaving_normal = through ? -normal : normal;
    origin = RefOffset(hit, leaving_normal);
    direction = RefCosineDirection(leaving_normal, float2(RefRandom(texel, sample_index, dimension + 2u), RefRandom(texel, sample_index, dimension + 3u)));
  }
  return radiance;
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_REFERENCE_HLSLI_
