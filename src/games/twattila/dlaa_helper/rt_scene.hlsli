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
RaytracingAccelerationStructure smoke_scene : register(t15);  // the smoke alone (SMOKE_MET: built)
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
  float grass_shadow_range;  // Grass Shadow Range (v18): rays from pixels farther (view depth, metres) pass grass; 0: none
  uint light_tile_row;       // the light mask's tiles' first row (v31, rt_lights; 0: none)
  uint settings_padding;
};

static const uint SUN_SHADOWS = 1u;
static const uint AMBIENT_OCCLUSION = 2u;
static const uint AMBIENT_QUARTER = 4u;  // one pixel of each 2 x 2 block traces (another each frame) for the block
static const uint LOCAL_LIGHTS = 8u;     // the local lights' shadows (rt_trace.cs_6_5.hlsl)
static const uint LEAF_LIGHT = 16u;      // leaves let a share of the sun through (rt_trace.cs_6_5.hlsl)
static const uint RECONSTRUCTED = 256u;  // Ray Reconstruction denoises the frame (dlaa_bridge.h RT_RECONSTRUCTED)
static const uint FOG_LIGHTS = 512u;     // the local lights' light in the fog too (rt_fog.cs_6_5.hlsl; v18)
// Fast Leaf Shadows (v18, dlaa_bridge.h RT_FAST_LEAVES): the rays toward the sun, the ambient's and the lights' take
// micromapped leaves' unknown states as known (no alpha tests for them: SceneRayFlags).
static const uint FAST_LEAVES = 8192u;
// Importance-Sampled Torch Shadows (v18, dlaa_bridge.h RT_LIGHT_IMPORTANCE): rt_lights traces a few lights a pixel, picked
// by what they give it (ReSTIR's candidates), the others keep their bits.
static const uint LIGHT_IMPORTANCE = 16384u;
static const uint LIGHT_STEADY = 268435456u;  // Steady Torch Shadows (dlaa_bridge.h RT_LIGHT_STEADY): rt_lights traces every reached light, hard (108 on)
#if defined(TORCHES_IN_BOUNCE)
// Torch and Fire Light in the Light Bounce (helper111, dlaa_bridge.h RT_LIGHTS_IN_BOUNCE): the first BOUNCE_LIGHTS lights
// go dark in rt_lights' bits, untraced; the paths' ReSTIR GI reservoir brings their light (rt_path.hlsli).
static const uint LIGHTS_IN_BOUNCE = 536870912u;
static const uint BOUNCE_LIGHTS = 32u;
// With LIGHTS_IN_BOUNCE, Torch and Fire Shadows from the Light Bounce (dlaa_bridge.h RT_LIGHT_RATIO): rt_lights writes each
// pixel's word as the last bounce's shadow ratio (rt_bounce's NRD_BOUNCE has this value in its own word).
static const uint LIGHT_RATIO = 4194304u;
#endif
static const uint NRD_SUN = 2097152u;      // RT_NRD: rt_trace's soft shadows for NRD (rt_nrd_front.hlsl)
static const uint NRD_BOUNCE = 4194304u;   // rt_bounce: its hit distance for NRD
static const uint NRD_AMBIENT = 8388608u;  // rt_trace: the ambient as traced for NRD, no history
static const uint SMOKE_MET = 262144u;  // Smoke Shadows' particles are in the scene (the helper: their instance built)
// Banners Without Self-Shadows (v29, dlaa_bridge.h RT_SHEETS_UNSELFED): cloth and sails don't shade themselves (rt_trace.cs_6_5.hlsl).
static const uint SHEETS_UNSELFED = 67108864u;
// Smoke Shadow Quality (dlaa_bridge.h RT_SMOKE_QUALITY_SHIFT, two bits): the particles a sun ray counts.
static const uint SMOKE_QUALITY_SHIFT = 19u;
static const uint RECORD_MASKED = 1u;  // flags: alpha-tested (leaves)
static const uint RECORD_THIN = 2u;    // cloth and sails
static const uint RECORD_SMOKE = 4u;   // smoke (Smoke Shadows): dims the sun's rays, blocks none
static const uint RECORD_GRASS = 8u;   // grass and units' cut-outs (with RECORD_MASKED): block the sun's rays (no light through them)
static const uint RECORD_LEAF = 16u;   // a leaves' mesh (its instances LEAF_INSTANCE), not a swaying copy
// The share of the sun a leaf lets through (Light Through Leaves; thin leaves pass about a quarter, each one met).
static const float LEAF_TRANSMITTANCE = 0.25f;
static const float THIN_TRANSMITTANCE = 0.35f;  // cloth and sails

// The local lights (bridge v9; their colours v11, for the paths; up to 128 v31, the paths and the fog the first 32):
// world position and reach (metres), their count.
cbuffer Lights : register(b4) {
  float4 lights[128];
  uint light_count;
  float4 light_colours[128];  // rgb
  uint4 light_previous[8];    // each light's place in the last trace's list, a byte each (255: new)
};

// Where the rays toward a local light stop before it (metres; at most half the way): its lamp, torch or fire's wood.
static const float LIGHT_CLEARANCE = 0.5f;

float Luminance(float3 color) {
  return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// What local light `index` gives a surface at `surface` facing `normal` (its colour, the games' smooth falloff (1 -
// distance / reach)^2, its angle; 0 out of reach or behind), its direction and distance.
float3 LightArriving(uint index, float3 surface, float3 normal, out float3 direction, out float light_distance) {
  const float3 to_light = lights[index].xyz - surface;
  light_distance = length(to_light);
  direction = to_light / max(light_distance, 1e-6f);
  if (light_distance >= lights[index].w || light_distance <= 1e-3f) return 0.f;
  const float n_dot_l = dot(normal, direction);
  if (n_dot_l <= 0.f) return 0.f;
  const float falloff = 1.f - light_distance / lights[index].w;
  return max(light_colours[index].rgb, 0.f) * (falloff * falloff * n_dot_l);
}


#if defined(OPACITY_MICROMAPS)
#define SCENE_QUERY(query_flags) RayQuery<(query_flags), RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS>
#else
#define SCENE_QUERY(query_flags) RayQuery<(query_flags)>
#endif

float2 UnpackUv(uint packed) {
  return f16tof32(uint2(packed, packed >> 16));
}

// The runtime ray flags of Occluded and TRACE_NEAREST (the sun's, the ambient's and the lights' rays): with Fast Leaf
// Shadows 2-state micromaps (their unknown states opaque or transparent as they lean: no candidates from them).
static uint scene_ray_flags = RAY_FLAG_NONE;

void SceneRayFlags() {
#if defined(OPACITY_MICROMAPS)
  if ((flags & FAST_LEAVES) != 0u) scene_ray_flags = RAY_FLAG_FORCE_OMM_2_STATE;
#endif
}

// Whether a candidate (a leaf) is solid where the ray meets it; smoke never is (its UVs are no atlas's: SmokeTransmittance
// reads them; smoke before other dynamic captures stays in the units' BLAS, rt_d3d12.hpp).
bool AlphaPasses(uint record_index, uint primitive, float2 barycentrics) {
  const uint4 record = records[record_index];
  if (((record.z >> 16) & RECORD_SMOKE) != 0u) return false;
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
// A candidate dimming less counts against no ray's particles (corners outside the disk, rims, chords behind its start).
static const float SMOKE_FAINT = 0.05f;

// The TLAS's instance masks (rt_d3d12.hpp: solids 1, the smoke's own instance 2, the leaves' meshes 4): the rays toward
// the sun meet all (ALL_INSTANCES); the others (the ambient, the local lights, the paths, the reflections) leave the
// smoke out in the hardware; Light Through Leaves meets the leaves apart (NO_LEAVES, LEAVES).
static const uint ALL_INSTANCES = 0xFFu;
static const uint NO_LEAVES = 0xFBu;
static const uint SMOKE = 0x02u;
static const uint LEAVES = 0x04u;
// Grass's instances (0x08 placed and shown, 0x10 the swaying grass's cluster BLAS, 0x20 the still copies of what sways there)
// met only by the sun's rays (GRASS) and the ambient's short ones (GRASS_STILL: contact shade needs no sway, and these
// blades' rays cost a quarter of the clusters', ATN's river: 4 ms of the paths): the point lights', the fog's, the Light
// Bounce's, the paths' and the reflections' pass it (most of a frame's rays crossed whole fields of it: ATN's 2026-10-29
// profile, a 4K harness field of 3721 patches: reflections 11 ms).
static const uint NO_GRASS = 0xC7u;
static const uint NO_GRASS_SMOKE = 0xC5u;
static const uint GRASS = 0x18u;
static const uint GRASS_STILL = 0x28u;
// A static solid cut-out's instances (rt_d3d12.hpp CUT_OUT_INSTANCE: a rigid mesh the game alpha-tests): every ray that
// meets the solids meets them, alpha-tested; the rays that cull alpha tests ask them apart (OccludedSolid).
static const uint CUT_OUTS = 0x40u;
static const float GRASS_AMBIENT_RANGE = 0.5f;  // metres: grass's share of the ambient's rays (the AO's and PT AO's)

// The share of the sun's light the smoke along a ray lets through, in the smoke's own TLAS: of the first `particles` that
// dim it at most, twice as many met at most. The sun's and the fog's rays meet the smoke apart from the rest: each particle
// is a candidate, and with up to MAX_CANDIDATES of them a ray, 120K-235K particles of arrows' smoke a frame took the sun's
// rays from 14 to 138 ms and the fog's from 6 to 143 ms (ATN's 2026-11-01 Constantinople profile; its late traces left grass
// shadows flickering). Counting those that barely dim left holes and flicker in thick smoke (which a ray met first changes
// from frame to frame; ATN 2026-11-02).
float SmokeTransmittance(float3 origin, float3 direction, float range, uint particles) {
  if ((flags & SMOKE_MET) == 0u) return 1.f;
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
  query.TraceRayInline(smoke_scene, RAY_FLAG_NONE, SMOKE, ray);
  float transmittance = 1.f;
  uint dimming = 0u;
  uint met = 0u;
  // The sun-only cards apart from the particles' count: a far tree's card is mostly clear, and a low sun's ray through a wood
  // met more of them than the count allows before the one whose test passed (the fog's rays after 4), so it came out lit; which
  // ones came first followed the BLAS, rebuilt each frame: far trees' shadows changing shape all the time (ATN, v40). Past
  // MAX_CANDIDATES of them it counts as blocked, as Occluded's rays do: the same whatever their order.
  uint cards = 0u;
  while (query.Proceed()) {
    const uint4 record = records[query.CandidateInstanceID() + query.CandidateGeometryIndex()];
    if (((record.z >> 16) & RECORD_SMOKE) != 0u) {
      // rt_smoke: corners +-(1 + opacity, radius), the quad one radius toward the sun of the centre: a soft sphere, only
      // the share of its chord ahead of the ray's start dims (no straight edge where it meets the ground).
      const uint first = record.x + query.CandidatePrimitiveIndex() * 3u;
      const float2 hit = query.CandidateTriangleBarycentrics();
      const float2 corner = UnpackUv(uv_pool[first]);
      const float2 scale = abs(corner);
      const float across = length((corner * (1.f - hit.x - hit.y) + UnpackUv(uv_pool[first + 1u]) * hit.x + UnpackUv(uv_pool[first + 2u]) * hit.y) / scale);
      if (across < 1.f) {
        const float half_chord = scale.y * sqrt(1.f - across * across);
        const float density = (scale.x - 1.f) * (1.f - smoothstep(0.25f, 1.f, across))
                              * saturate((query.CandidateTriangleRayT() - scale.y + half_chord) / (2.f * half_chord));
        transmittance *= 1.f - density;
        if (density > SMOKE_FAINT) ++dimming;
      }
      if (dimming >= particles || ++met >= particles * 2u || transmittance < SMOKE_DARK) query.Abort();
    } else if (++cards > MAX_CANDIDATES || CANDIDATE_PASSES(query)) {
      // A sprite soldier or far tree with it (RT_SUN_ONLY) blocks where its alpha test passes.
      query.CommitNonOpaqueTriangleHit();
      query.Abort();
    }
  }
  return (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.f : transmittance;
}

// Whether anything among `instances` lies along `direction` from `origin` within `range` (alpha-tested: leaves, grass
// and cloth by their masks).
bool Occluded(float3 origin, float3 direction, float range, uint instances) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;
  query.TraceRayInline(scene, scene_ray_flags, instances, ray);
  uint candidates = 0u;
  while (query.Proceed()) {
    if (++candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) {
      query.CommitNonOpaqueTriangleHit();
    }
  }
  return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// The nearest surface among `instances` along a ray toward the sun (alpha-tested as Occluded; its query, committed:
// CommittedStatus says whether one was). Past MAX_CANDIDATES alpha-tested surfaces the last counts.
#define TRACE_NEAREST(query, ray, instances)                                                       \
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;                                            \
  query.TraceRayInline(scene, scene_ray_flags, (instances), (ray));                                  \
  {                                                                                                  \
    uint trace_candidates = 0u;                                                                      \
    while (query.Proceed()) {                                                                        \
      if (++trace_candidates > MAX_CANDIDATES || CANDIDATE_PASSES(query)) {                         \
        query.CommitNonOpaqueTriangleHit();                                                          \
      }                                                                                              \
    }                                                                                                \
  }

// Whether a solid surface lies along a ray: alpha-tested ones are culled in the hardware (the solid parts of leaves with
// opacity micromaps stay), the smoke's and the grass's instances left out (a culled blade blocks nothing, and the ray
// walked every field it crossed). A ray goes on through every culled one (through a canopy 5x the cost of alpha tests
// ending at the first leaf), so only from surfaces facing away from the sun. Solid cut-outs (CUT_OUTS: a fence, a thatched
// house) block where their alpha test passes, in a query of their own: culled with the leaves, every part of a building
// without a known micromap state (its bake on its way; a wall's triangle over more than a texture's width is unknown
// opaque, rt_omm_uvs) let the sun through onto what faces away in its shadow (ATN's debug41). Told apart from leaves'
// candidates in one query's loop, dense leaves cost the paths 2 to 4 times as much (4K harness leaf storm).
bool OccludedSolid(float3 origin, float3 direction, float range) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.f;
  ray.TMax = range;
  SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_NON_OPAQUE) query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, NO_GRASS_SMOKE, ray);
  query.Proceed();
  return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT || Occluded(origin, direction, range, CUT_OUTS);
}

// The nearest surface along a ray of the Light Bounce, its paths or the reflections (its query, committed:
// CommittedStatus says whether one was; the smoke left out). With opacity micromaps solid surfaces only, the leaves'
// solid parts among them (an alpha test for every candidate nearer than the hit, grass by the dozen, made them cost up
// to 30 ms a frame, ATN 2026-10-29); else alpha-tested, at most MAX_CANDIDATES.
#if defined(OPACITY_MICROMAPS)
#define TRACE_SURFACE(query, ray)                                                        SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_NON_OPAQUE) query;       query.TraceRayInline(scene, RAY_FLAG_NONE, NO_GRASS_SMOKE, (ray));                       query.Proceed();
#else
#define TRACE_SURFACE(query, ray)                                         \
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) query;                   \
  query.TraceRayInline(scene, RAY_FLAG_NONE, NO_GRASS_SMOKE, (ray));        \
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

float3 OctahedralDecode(uint packed) {
  const float2 encoded = float2(int2(packed << 16, packed) >> 16) / 32767.f;
  float3 normal = float3(encoded, 1.f - abs(encoded.x) - abs(encoded.y));
  if (normal.z < 0.f) normal.xy = (1.f - abs(normal.yx)) * select(normal.xy >= 0.f, 1.f, -1.f);
  return normalize(normal);
}

// A hit triangle's normal (the record's pool, in its BLAS's space: turned by the instance's transform), facing back
// along the ray.
float3 HitNormal(uint record_index, uint primitive, float3 direction, float3x4 object_to_world) {
  const float3 normal = normalize(mul((float3x3)object_to_world, OctahedralDecode(normal_pool[records[record_index].y + primitive])));
  return (dot(normal, direction) > 0.f) ? -normal : normal;
}

// The surface's normal from the neighbour nearer in depth on each axis (none across depth edges), facing the camera.
float3 SurfaceNormal(int2 pixel, float4 position) {
  const int2 last = int2(width, height) - 1;
  const float4 left = positions[clamp(pixel - int2(1, 0), 0, last)];
  const float4 right = positions[clamp(pixel + int2(1, 0), 0, last)];
  const float4 above = positions[clamp(pixel - int2(0, 1), 0, last)];
  const float4 below = positions[clamp(pixel + int2(0, 1), 0, last)];
  // At the screen's edge the neighbour past it is the pixel itself: the one inside (else no normal, and a reflection's ray turned
  // back toward the camera: a black line down each side of the water, ATN 2026-11-03).
  const float3 along_x = (pixel.x == 0 || (pixel.x != last.x && abs(right.w - position.w) < abs(left.w - position.w))) ? right.xyz - position.xyz : position.xyz - left.xyz;
  const float3 along_y = (pixel.y == 0 || (pixel.y != last.y && abs(below.w - position.w) < abs(above.w - position.w))) ? below.xyz - position.xyz : position.xyz - above.xyz;
  const float3 to_camera = sun[uint2(1, 0)].xyz - position.xyz;
  float3 normal = cross(along_y, along_x);
  const float normal_length = length(normal);
  normal = (normal_length > 0.f) ? normal / normal_length : normalize(to_camera);
  return (dot(normal, to_camera) < 0.f) ? -normal : normal;
}

// Off a surface by 3 mm plus 0.1% of the depth (depth precision and coarser far geometry grow with distance). The rays leave
// by this along the normal and again toward the sun: at 2 cm they started past cheek guards and helmet rims, the sun came
// through them (ATN's debug35).
float SurfaceOffset(float depth) {
  return 0.0029994224f + depth * 0.001f;
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

// The solid triangle a pixel shows: its normal into `normal`, facing the camera (false: none there, a leaf or a blade).
// Exact where the depth's normal is noise: every frame's jitter and the depth's steps turn that by a degree or more.
bool ShownNormal(float4 position, inout float3 normal) {
  const float3 to_camera = normalize(sun[uint2(1, 0)].xyz - position.xyz);
  const float reach = 2.f * SurfaceOffset(position.w);
  RayDesc ray;
  ray.Origin = position.xyz + to_camera * reach;
  ray.Direction = -to_camera;
  ray.TMin = 0.f;
  ray.TMax = 2.f * reach;
  TRACE_SURFACE(query, ray)
  if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
  normal = HitNormal(query.CommittedInstanceID() + query.CommittedGeometryIndex(), query.CommittedPrimitiveIndex(), ray.Direction,
                     query.CommittedObjectToWorld3x4());
  return true;
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
