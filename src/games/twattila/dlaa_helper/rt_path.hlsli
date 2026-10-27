/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's path tracing and reflections (rt_d3d12.hpp Paths, bridge v8; ported from Tales of Arise's rt_path.hlsli
// and rt_cache.hlsli, in metres): the light a ray brings back from the scene the addon captured.
// - The light leaving a surface a ray meets: the frame's light where the camera sees that surface now (the prepared
//   light, the Light Bounce's: one texel per bounce_scale x bounce_scale render pixels), else last frame's scene colour
//   where last frame saw it (the DLAA input, reprojected), else the world radiance cache's (cells of 25 cm near the
//   camera, doubling every 10 m further; filled from the prepared light every frame), else, for a surface nobody saw,
//   the sun through a shadow ray and the game's own lighting learned from the screen (light in shade, light the sun adds:
//   a line fitted each frame through light / base colour against the sun's visibility x N.L) times its base colour
//   (the cache's, from the G-buffer guides), and the path goes on from it (path_bounces surfaces past the first).
// - Path tracing (Light Bounce): cosine-weighted paths from each bounce texel; the Light Bounce's resolve, denoiser and
//   apply follow as with its screen-space trace (its texels: the light arriving, the distance from the camera).
// - Reflections: GGX samples of each pixel's lobe (a mirror ray for smooth surfaces) with the same paths; their light and
//   the share of samples that met a surface, accumulated over frames by world position; specular occlusion: the share
//   of the lobe open within reflection_range.
// World units: metres, y up.
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_PATH_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_PATH_HLSLI_

#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"

Texture2D<float3> light : register(t9);               // the prepared light (bounce texels)
Texture2D<float4> previous_color : register(t10);     // last frame's scene colour (render pixels)
Texture2D<float4> previous_positions : register(t11); // last frame's world positions
Texture2D<float4> guide_normals : register(t12);      // world normals (x 0: none)
Texture2D<float4> guide_albedo : register(t13);       // diffuse albedo
Texture2D<float> guide_roughness : register(t14);
RWTexture2D<unorm float> traced_visibility : register(u0);  // this frame's ray-traced sun visibility
RWTexture2D<float4> bounce : register(u9);
RWBuffer<uint> cache_keys : register(u10);            // 0: empty
RWBuffer<float4> cache_radiance : register(u11);      // rgb, the frame it was last seen + 1 (0: never)
RWBuffer<uint> cache_sums : register(u12);            // this frame's r, g, b (CACHE_SCALE fixed point), count
RWBuffer<uint2> cache_albedo : register(u13);         // rgba8 base colour (a 255; 0: none), that frame + 1
RWBuffer<uint> light_fit : register(u14);             // count, x, x^2, y rgb, x y rgb
RWBuffer<float4> light_kept : register(u15);          // [0] rgb in shade, [1] rgb the sun adds (w 1: solved)
RWTexture2D<float4> specular_accumulated : register(u16);
RWTexture2D<float4> specular_history : register(u17);
RWTexture2D<float2> specular_meta : register(u18);         // specular occlusion, frames
RWTexture2D<float2> specular_meta_history : register(u19);
RWTexture2D<float4> reflection : register(u20);            // out: reflected light, the share of samples that met a surface
RWTexture2D<unorm float> specular_occlusion : register(u21);
// ReSTIR GI (Ouyang et al. 2021, "ReSTIR GI: Path Resampling for Real-Time Path Tracing"): each bounce texel's paths
// become a reservoir of one surface their first rays met (its position, normal and the light leaving it toward the
// texel), merged with last frame's at the same surface (PathTraceTexel), then with neighbours' (ReuseTexel, which
// writes the bounce). Two sets, this frame's at PATH_REUSE_PARITY. As kajiya's ReSTIR GI (Embark Studios, MIT / Apache
// 2.0; compared, not copied): W clamped against fireflies, last frame's reservoir only on a surface facing the same way,
// and every third frame its sample re-checked (still in view, its light now): changed light shortens the history.
RWTexture2D<float4> reuse_samples[2] : register(u23);  // the surface's position, W (its unbiased contribution weight)
RWTexture2D<uint4> reuse_data[2] : register(u25);      // its light (f16 r | g, b | M), its normal, the texel's (octahedral)

static const uint PATH_TRACING = 1u;          // the bounce texels' light path traced
static const uint PATH_REFLECTIONS = 2u;
static const uint PATH_SPECULAR_OCCLUSION = 4u;
static const uint PATH_HISTORY_VALID = 8u;    // last frame's colour and positions are last frame's
static const uint PATH_CACHE_VALID = 16u;     // the radiance cache has been filled
static const uint PATH_SPECULAR_HISTORY = 32u;
static const uint PATH_GUIDES = 64u;          // the G-buffer guides hold this frame's
static const uint PATH_SUN_TRACED = 128u;     // traced_visibility is this frame's
static const uint PATH_LIGHT = 256u;          // the prepared light is this frame's
static const uint PATH_RAW = 512u;            // the reflections as traced (Ray Reconstruction denoises them)
static const uint PATH_REUSE = 1024u;         // ReSTIR GI
static const uint PATH_REUSE_HISTORY = 2048u; // last frame's reservoirs are last frame's
static const uint PATH_REUSE_PARITY = 4096u;  // this frame's set: 1 (else 0)
static const uint PATH_LOCAL_LIGHTS = 8192u;  // the local lights light the surfaces the screen never saw (b4, v11)
static const float REUSE_HISTORY = 20.f;      // last frame's samples at most this many times this frame's
static const float REUSE_W_CLAMP = 10.f;      // W at most (kajiya's RESTIR_RESERVOIR_W_CLAMP)
static const uint REUSE_VALIDATION_PERIOD = 3u;
static const uint REUSE_NEIGHBOURS = 5u;
static const float REUSE_RADIUS = 12.f;       // bounce texels

static const uint CACHE_ENTRIES = 1u << 20;
static const uint CACHE_PROBES = 8u;
static const float CACHE_CELL = 0.25f;
static const float CACHE_LEVEL_DISTANCE = 10.f;
static const uint CACHE_LEVELS = 8u;
static const float CACHE_SCALE = 256.f;
static const float CACHE_MAX = 1024.f;
static const float CACHE_BLEND = 0.2f;
static const float CACHE_LIFETIME = 1800.f;
static const float LIGHT_FIT_X = 4096.f;
static const float LIGHT_FIT_Y = 64.f;
static const float LIGHT_FIT_MAX_Y = 16.f;
static const float LIGHT_FIT_BLEND = 0.1f;
static const float UNSEEN_ALBEDO = 0.35f;
static const float ROUGH_LIMIT = 0.75f;  // rougher surfaces keep the game's reflection: only their occlusion is traced
static const float MAX_SPECULAR_FRAMES = 12.f;

#if defined(PATH_REORDER)
// Shader execution reordering (DXR 1.2): threads regroup by what their first hit is before shading it.
#define REORDER(query, hint)                                        \
  {                                                                 \
    dx::HitObject reorder_hit = dx::HitObject::FromRayQuery(query); \
    dx::MaybeReorderThread(reorder_hit, (hint), 4);                 \
  }
#else
#define REORDER(query, hint)
#endif

float3 Camera() {
  return sun[uint2(1, 0)].xyz;
}

uint CacheHash(uint4 value) {
  uint hash = value.x * 0x8DA6B343u ^ value.y * 0xD8163841u ^ value.z * 0xCB1AB31Fu ^ value.w * 0x165667B1u;
  hash ^= hash >> 15;
  hash *= 0x2C1B3C6Du;
  hash ^= hash >> 12;
  return hash;
}

void CacheCell(float3 world, out uint slot, out uint key) {
  const float distance_to_camera = length(world - Camera());
  const uint level = min((uint)floor(log2(max(distance_to_camera / CACHE_LEVEL_DISTANCE, 1.f))), CACHE_LEVELS - 1u);
  const int3 cell = int3(floor(world / (CACHE_CELL * exp2((float)level))));
  const uint4 value = uint4(asuint(cell), level);
  slot = CacheHash(value) & (CACHE_ENTRIES - 1u);
  key = CacheHash(value.yzwx + 0x9E3779B9u) | 1u;
}

bool CacheFind(float3 world, out uint entry) {
  uint slot, key;
  CacheCell(world, slot, key);
  entry = 0u;
  [loop]
  for (uint probe = 0u; probe < CACHE_PROBES; ++probe) {
    entry = (slot + probe) & (CACHE_ENTRIES - 1u);
    const uint found = cache_keys[entry];
    if (found == 0u) return false;
    if (found == key) return true;
  }
  return false;
}

bool CacheClaim(float3 world, out uint entry) {
  uint slot, key;
  CacheCell(world, slot, key);
  entry = 0u;
  [loop]
  for (uint probe = 0u; probe < CACHE_PROBES; ++probe) {
    entry = (slot + probe) & (CACHE_ENTRIES - 1u);
    uint previous_key;
    InterlockedCompareExchange(cache_keys[entry], 0u, key, previous_key);
    if (previous_key == 0u || previous_key == key) return true;
  }
  return false;
}

bool CacheLookup(float3 world, out float3 radiance) {
  radiance = 0.f;
  uint entry;
  if (!CacheFind(world, entry)) return false;
  const float4 kept = cache_radiance[entry];
  radiance = kept.rgb;
  return kept.w > 0.f;
}

bool CacheAdd(float3 world, float3 light_there) {
  uint entry;
  if (!CacheClaim(world, entry)) return false;
  const uint3 fixed_point = (uint3)(clamp(light_there, 0.f, CACHE_MAX) * CACHE_SCALE + 0.5f);
  InterlockedAdd(cache_sums[entry * 4u], fixed_point.r);
  InterlockedAdd(cache_sums[entry * 4u + 1u], fixed_point.g);
  InterlockedAdd(cache_sums[entry * 4u + 2u], fixed_point.b);
  InterlockedAdd(cache_sums[entry * 4u + 3u], 1u);
  return true;
}

bool CacheAlbedo(float3 world, out float3 albedo) {
  albedo = 0.f;
  uint entry;
  if (!CacheFind(world, entry)) return false;
  const uint packed = cache_albedo[entry].x;
  albedo = float3(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu) / 255.f;
  return (packed >> 24) != 0u;
}

void CacheSetAlbedo(float3 world, float3 albedo) {
  uint entry;
  if (!CacheClaim(world, entry)) return;
  const uint3 bytes = (uint3)(saturate(albedo) * 255.f + 0.5f);
  cache_albedo[entry] = uint2(bytes.r | (bytes.g << 8) | (bytes.b << 16) | (255u << 24), frame + 1u);
}

// A world position's clip coordinates through a camera's registers.
// This frame's pixel of a world position where the camera sees that same surface (false: off screen or hidden).
bool SeenNow(float3 world, out uint2 pixel) {
  pixel = 0u;
  const float4 clip = ClipOf(world, view_projection);
  if (clip.w <= 0.f) return false;
  const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
  if (any(uv < 0.f) || any(uv >= 1.f)) return false;
  pixel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1u);
  const float4 seen = positions[pixel];
  return seen.w > 0.f && distance(seen.xyz, world) < 0.05f + 0.02f * seen.w;
}

// Last frame's pixel of a world position where last frame saw that same surface (false: not).
bool SeenLastFrame(float3 world, out uint2 pixel) {
  pixel = 0u;
  if ((path_flags & PATH_HISTORY_VALID) == 0u) return false;
  const float4 clip = ClipOf(world, previous_view_projection);
  if (clip.w <= 0.f) return false;
  const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
  if (any(uv < 0.f) || any(uv >= 1.f)) return false;
  pixel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1u);
  const float4 seen = previous_positions[pixel];
  return seen.w > 0.f && distance(seen.xyz, world) < 0.03f + 0.01f * seen.w;
}

// The light leaving a surface a ray met (false: nobody knows it).
bool HitLight(float3 hit, out float3 light_there) {
  light_there = 0.f;
  uint2 pixel;
  if ((path_flags & PATH_LIGHT) != 0u && SeenNow(hit, pixel)) {
    light_there = max(light[min(pixel / max(bounce_scale, 1u), uint2(bounce_width, bounce_height) - 1u)], 0.f);
    return true;
  }
  if (SeenLastFrame(hit, pixel)) {
    light_there = max(previous_color[pixel].rgb, 0.f);
    return true;
  }
  return (path_flags & PATH_CACHE_VALID) != 0u && CacheLookup(hit, light_there);
}

float3 HitAlbedo(float3 hit) {
  float3 albedo;
  return CacheAlbedo(hit, albedo) ? albedo : UNSEEN_ALBEDO;
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

// The pixel's normal: the G-buffer's when the guides hold it, else from the positions.
float3 PixelNormal(uint2 pixel, float4 position) {
  if ((path_flags & PATH_GUIDES) != 0u) {
    const float3 guide = guide_normals[pixel].xyz;
    if (dot(guide, guide) > 0.5f) return normalize(guide);
  }
  return SurfaceNormal(int2(pixel), position);
}

// The local lights' light on a surface the screen never saw: one of them picked by what it would give (its colour, the
// games' smooth falloff (1 - distance / reach)^2, its angle; reservoir sampling with one number) and weighted back by its
// odds, if a ray reaches it.
float3 LocalLight(float3 surface, float3 normal, float u) {
  float total = 0.f;
  float picked_weight = 0.f;
  float3 picked = 0.f;
  float3 picked_direction = 0.f;
  float picked_distance = 0.f;
  [loop]
  for (uint index = 0u; index < min(light_count, 32u); ++index) {
    const float3 to_light = lights[index].xyz - surface;
    const float light_distance = length(to_light);
    if (light_distance >= lights[index].w || light_distance <= 1e-3f) continue;
    const float3 direction = to_light / light_distance;
    const float n_dot_l = dot(normal, direction);
    if (n_dot_l <= 0.f) continue;
    const float falloff = 1.f - light_distance / lights[index].w;
    const float3 arriving = max(light_colours[index].rgb, 0.f) * (falloff * falloff * n_dot_l);
    const float weight = dot(arriving, float3(0.2126f, 0.7152f, 0.0722f));
    if (weight <= 0.f) continue;
    total += weight;
    const float odds = weight / total;
    if (u < odds) {
      picked = arriving;
      picked_weight = weight;
      picked_direction = direction;
      picked_distance = light_distance;
      u /= odds;
    } else {
      u = saturate((u - odds) / max(1.f - odds, 1e-6f));
    }
  }
  if (picked_weight <= 0.f || Occluded(surface, picked_direction, picked_distance - min(LIGHT_CLEARANCE, 0.5f * picked_distance))) {
    return 0.f;
  }
  return picked * (total / picked_weight);
}

// The light arriving along a ray (`first_distance`: how far its first surface is, 1e30: none; `first_normal` its normal).
float3 PathRadiance(float3 origin, float3 direction, float range, uint2 seed, uint path, out float first_distance, out float3 first_normal) {
  first_distance = 1e30f;
  first_normal = 0.f;
  float3 radiance = 0.f;
  float3 throughput = 1.f;
  const float4 toward_sun = sun[uint2(0, 0)];
  const float4 shade = light_kept[0];
  const float3 sunlit = light_kept[1].rgb;
  bool unseen = false;
  float3 unseen_position = 0.f;
  float3 unseen_throughput = 1.f;
  float3 radiance_before = 0.f;
  [loop]
  for (uint vertex = 0u; vertex <= path_bounces; ++vertex) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.f;
    ray.TMax = (vertex == 0u) ? range : bounce_range;
    TRACE_NEAREST(query, ray)
    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (vertex == 0u) {
      REORDER(query, hit ? 1u + (query.CommittedInstanceID() & 7u) : 0u)
    }
    if (!hit) {
      // Past the first surface the learned shade light stands for the sky; from the camera's pixel the game's own
      // ambient covers it.
      if (vertex > 0u) radiance += throughput * shade.rgb;
      break;
    }
    const float3 position = origin + direction * query.CommittedRayT();
    if (vertex == 0u) first_distance = query.CommittedRayT();
    float3 light_there;
    if (HitLight(position, light_there)) {
      radiance += throughput * light_there;
      break;
    }
    const float3 normal = HitNormal(query.CommittedInstanceID() + query.CommittedGeometryIndex(), query.CommittedPrimitiveIndex(), direction,
                                    query.CommittedObjectToWorld3x4());
    if (vertex == 0u) first_normal = normal;
    const float3 albedo = HitAlbedo(position);
    const float3 surface = position + normal * SurfaceOffset(distance(position, Camera()));
    float3 emitted = 0.f;
    const float n_dot_l = dot(normal, toward_sun.xyz);
    if (toward_sun.w != 0.f && n_dot_l > 0.f && shade.w > 0.f && !Occluded(surface, toward_sun.xyz, 1.0e4f)) {
      emitted = albedo * sunlit * n_dot_l;
    }
    if ((path_flags & PATH_LOCAL_LIGHTS) != 0u) {
      emitted += albedo * LocalLight(surface, normal, Random(uint3(seed, frame * 64u + path * 8u + vertex * 2u + 16384u)));
    }
    if (!unseen) {
      unseen = true;
      unseen_position = position;
      unseen_throughput = throughput;
      radiance_before = radiance;
    }
    if (vertex == path_bounces) {
      radiance += throughput * (emitted + albedo * shade.rgb);
      break;
    }
    radiance += throughput * emitted;
    throughput *= albedo;
    origin = surface;
    direction = CosineDirection(normal, Random(uint3(seed, frame * 64u + path * 8u + vertex * 2u + 8192u)),
                                Random(uint3(seed, frame * 64u + path * 8u + vertex * 2u + 8193u)));
  }
  // The first unseen surface's light feeds the cache once the learned lighting is solved.
  if (unseen && shade.w > 0.f && all(unseen_throughput > 1e-3f)) {
    CacheAdd(unseen_position, (radiance - radiance_before) / unseen_throughput);
  }
  return radiance;
}

struct Reservoir {
  float3 hit;
  float3 hit_normal;
  float3 light;   // leaving the surface toward the texel
  float weight;   // the resampling weights' sum while merging; W once finished
  float count;    // M: the samples it stands for
};

float Luminance(float3 color) {
  return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// The target function at a receiver: the sample's light (luminance) by the cosine toward it (the integrand without 1/pi).
float ReuseTarget(float3 receiver, float3 receiver_normal, Reservoir reservoir) {
  return Luminance(reservoir.light) * max(dot(receiver_normal, normalize(reservoir.hit - receiver)), 0.f);
}

// Takes `other`'s sample with probability `weight` over the sum so far; M adds up.
void Merge(inout Reservoir reservoir, Reservoir other, float weight, float random) {
  reservoir.weight += weight;
  reservoir.count += other.count;
  if (weight > 0.f && random * reservoir.weight < weight) {
    reservoir.hit = other.hit;
    reservoir.hit_normal = other.hit_normal;
    reservoir.light = other.light;
  }
}

// W = the weights' sum over M times the target at the receiver (0: nothing to show).
void Finish(inout Reservoir reservoir, float3 receiver, float3 receiver_normal) {
  const float target = ReuseTarget(receiver, receiver_normal, reservoir);
  reservoir.weight = (reservoir.count > 0.f && target > 0.f) ? reservoir.weight / (reservoir.count * target) : 0.f;
}

// The texel's own normal its reservoir was made for (0: none).
float3 LoadReceiverNormal(uint set, uint2 texel) {
  const uint packed = reuse_data[set][texel].w;
  return (packed != 0u) ? OctahedralDecode(packed) : 0.f;
}

Reservoir LoadReservoir(uint set, uint2 texel) {
  const float4 sample_point = reuse_samples[set][texel];
  const uint4 data = reuse_data[set][texel];
  Reservoir reservoir;
  reservoir.hit = sample_point.xyz;
  reservoir.weight = sample_point.w;
  reservoir.light = float3(f16tof32(data.x), f16tof32(data.x >> 16), f16tof32(data.y));
  reservoir.count = (float)(data.y >> 16);
  reservoir.hit_normal = (data.z != 0u) ? OctahedralDecode(data.z) : 0.f;
  return reservoir;
}

void StoreReservoir(uint set, uint2 texel, Reservoir reservoir, float3 receiver_normal) {
  reuse_samples[set][texel] = float4(reservoir.hit, reservoir.weight);
  reuse_data[set][texel] = uint4(f32tof16(reservoir.light.r) | (f32tof16(reservoir.light.g) << 16),
                                 f32tof16(reservoir.light.b) | (min((uint)reservoir.count, 65535u) << 16),
                                 (dot(reservoir.hit_normal, reservoir.hit_normal) > 0.f) ? OctahedralEncode(reservoir.hit_normal) : 0u,
                                 (dot(receiver_normal, receiver_normal) > 0.f) ? OctahedralEncode(receiver_normal) : 0u);
}

// The surface a bounce texel shows: its render pixel, world position (w: depth; 0: the sky), normal, rays' origin.
float4 TexelSurface(uint2 id, out float3 normal, out float3 origin) {
  const uint2 pixel = min(id * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u);
  const float4 position = positions[pixel];
  normal = 0.f;
  origin = position.xyz;
  if (position.w <= 0.f) return position;
  normal = PixelNormal(pixel, position);
  origin = position.xyz + normal * SurfaceOffset(position.w);
  return position;
}

// Path tracing: a bounce texel's arriving light (the Light Bounce's trace output); with ReSTIR GI its paths' reservoir,
// merged with last frame's at the same surface (ReuseTexel writes the bounce).
void PathTraceTexel(uint2 id) {
  if (id.x >= bounce_width || id.y >= bounce_height) return;
  float3 normal;
  float3 origin;
  const float4 position = TexelSurface(id, normal, origin);
  const bool reuse = (path_flags & PATH_REUSE) != 0u;
  const uint set = ((path_flags & PATH_REUSE_PARITY) != 0u) ? 1u : 0u;
  Reservoir reservoir = (Reservoir)0;
  if (position.w <= 0.f) {
    bounce[id] = 0.f;
    if (reuse) StoreReservoir(set, id, reservoir, 0.f);
    return;
  }
  const uint samples = max(path_samples, 1u);
  float3 arriving = 0.f;
  [loop]
  for (uint path = 0u; path < samples; ++path) {
    const float3 direction = CosineDirection(normal, Random(uint3(id, frame * 16u + path * 2u + 1024u)),
                                             Random(uint3(id, frame * 16u + path * 2u + 1025u)));
    float first_distance;
    float3 first_normal;
    const float3 sample_light = PathRadiance(origin, direction, bounce_range, id, path, first_distance, first_normal);
    arriving += sample_light;
    // Cosine-weighted rays: the target over their density is pi times the light's luminance.
    Reservoir candidate;
    candidate.hit = origin + direction * min(first_distance, bounce_range);
    candidate.hit_normal = first_normal;
    candidate.light = sample_light;
    candidate.count = 1.f;
    candidate.weight = 0.f;
    Merge(reservoir, candidate, (first_distance < 1e29f) ? 3.14159265f * Luminance(sample_light) : 0.f,
          Random(uint3(id, frame * 16u + path * 2u + 5120u)));
  }
  if (!reuse) {
    bounce[id] = float4(arriving / samples, distance(position.xyz, Camera()));
    return;
  }
  uint2 previous_pixel;
  if ((path_flags & PATH_REUSE_HISTORY) != 0u && SeenLastFrame(position.xyz, previous_pixel)) {
    const uint2 previous_texel = min(previous_pixel / bounce_scale, uint2(bounce_width, bounce_height) - 1u);
    Reservoir previous = LoadReservoir(1u - set, previous_texel);
    // Last frame's reservoir was made for a surface facing about the same way (else a new history starts).
    if (dot(LoadReceiverNormal(1u - set, previous_texel), normal) < 0.8f) previous.count = 0.f;
    float invalidity = 0.f;
    if (previous.count > 0.f && previous.weight > 0.f && frame % REUSE_VALIDATION_PERIOD == 0u) {
      // Validation: its surface out of view now drops it; its light now (where the screen or the cache knows it) takes
      // the stored light's place, and a change shortens the history (bright changes dimmed until they settle).
      const float3 to_hit = previous.hit - origin;
      const float hit_distance = length(to_hit);
      float3 light_now;
      if (hit_distance <= 1e-3f || Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f)) {
        previous.count = 0.f;
      } else if (HitLight(previous.hit, light_now)) {
        const float3 change = abs(previous.light - light_now) / max(previous.light + light_now, 1e-3f);
        invalidity = smoothstep(0.1f, 0.5f, length(change) / 1.7320508f);
        const float ratio = Luminance(previous.light) / max(Luminance(light_now), 1e-8f);
        previous.count *= clamp(ratio, 0.03f, 1.f);
        previous.weight *= clamp(ratio * 10.f, 0.01f, 1.f);
        previous.light = light_now;
      }
    }
    previous.count = min(previous.count, exp2(log2(REUSE_HISTORY) * (1.f - invalidity)) * samples);
    Merge(reservoir, previous, ReuseTarget(origin, normal, previous) * previous.weight * previous.count,
          Random(uint3(id, frame * 16u + 6144u)));
  }
  Finish(reservoir, origin, normal);
  reservoir.weight = min(reservoir.weight, REUSE_W_CLAMP);
  StoreReservoir(set, id, reservoir, normal);
}

// ReSTIR GI's spatial reuse: a texel's reservoir merged with neighbours' on the same surface (their samples' density
// moved to this texel: the Jacobian of their surfaces' solid angles); the chosen surface's light, if in view, is the
// bounce.
void ReuseTexel(uint2 id) {
  if (id.x >= bounce_width || id.y >= bounce_height) return;
  float3 normal;
  float3 origin;
  const float4 position = TexelSurface(id, normal, origin);
  if (position.w <= 0.f) {
    bounce[id] = 0.f;
    return;
  }
  const uint set = ((path_flags & PATH_REUSE_PARITY) != 0u) ? 1u : 0u;
  const Reservoir own = LoadReservoir(set, id);
  Reservoir combined = (Reservoir)0;
  Merge(combined, own, ReuseTarget(origin, normal, own) * own.weight * own.count, Random(uint3(id, frame * 16u + 7168u)));
  [loop]
  for (uint neighbour = 0u; neighbour < REUSE_NEIGHBOURS; ++neighbour) {
    const float angle = 6.2831853f * Random(uint3(id, frame * 16u + neighbour * 2u + 7200u));
    const float radius = REUSE_RADIUS * sqrt(Random(uint3(id, frame * 16u + neighbour * 2u + 7201u)));
    const int2 texel = int2(id) + int2(round(float2(cos(angle), sin(angle)) * radius));
    if (any(texel < 0) || texel.x >= (int)bounce_width || texel.y >= (int)bounce_height || all(texel == int2(id))) continue;
    float3 other_normal;
    float3 other_origin;
    const float4 other_position = TexelSurface(uint2(texel), other_normal, other_origin);
    if (other_position.w <= 0.f || abs(other_position.w - position.w) > 0.1f * position.w || dot(other_normal, normal) < 0.9f) continue;
    const Reservoir other = LoadReservoir(set, uint2(texel));
    if (other.count <= 0.f) continue;
    const float3 to_other = other_origin - other.hit;
    const float3 to_here = origin - other.hit;
    const float jacobian = abs(dot(other.hit_normal, normalize(to_here))) * dot(to_other, to_other)
                           / max(abs(dot(other.hit_normal, normalize(to_other))) * dot(to_here, to_here), 1e-8f);
    if (!(jacobian > 0.1f && jacobian < 10.f)) continue;
    Merge(combined, other, ReuseTarget(origin, normal, other) * other.weight * other.count * jacobian,
          Random(uint3(id, frame * 16u + neighbour + 7300u)));
  }
  Finish(combined, origin, normal);
  // The chosen surface in view of this one (no light through walls); the integrand over its density.
  const float3 to_hit = combined.hit - origin;
  const float hit_distance = length(to_hit);
  float3 arriving = 0.f;
  if (combined.weight > 0.f && hit_distance > 1e-3f && !Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f)) {
    arriving = combined.light * (max(dot(normal, to_hit / hit_distance), 0.f) / 3.14159265f) * combined.weight;
  }
  bounce[id] = float4(arriving, distance(position.xyz, Camera()));
}

// Tangent, bitangent and `normal`.
float3x3 Basis(float3 normal) {
  const float3 helper = (abs(normal.y) < 0.999f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f);
  const float3 tangent = normalize(cross(helper, normal));
  return float3x3(tangent, cross(normal, tangent), normal);
}

// A GGX microfacet normal seen from `view` (in the basis of the surface normal: z up) for roughness `alpha` (visible
// normals, Dupuy and Benyoub 2023's spherical caps).
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

// Reflections and specular occlusion of a render pixel (accumulated by world position).
void ReflectPixel(uint2 id) {
  if (id.x >= width || id.y >= height) return;
  const float4 position = positions[id];
  if (position.w <= 0.f || (path_flags & PATH_GUIDES) == 0u) {
    specular_accumulated[id] = 0.f;
    specular_meta[id] = float2(1.f, 0.f);
    return;
  }
  const float3 normal = PixelNormal(id, position);
  const float3 view = normalize(Camera() - position.xyz);
  const float n_dot_v = max(dot(normal, view), 1e-3f);
  const float3 origin = position.xyz + normal * SurfaceOffset(position.w);
  const float roughness = saturate(guide_roughness[id]);
  const float alpha = max(roughness * roughness, 1e-3f);
  const bool mirror = roughness < 0.05f;
  const bool traced = (path_flags & PATH_REFLECTIONS) != 0u && roughness <= ROUGH_LIMIT;
  const uint samples = mirror ? 1u : max(path_samples, 1u);
  const float3x3 basis = Basis(normal);
  const float3 view_local = float3(dot(view, basis[0]), dot(view, basis[1]), n_dot_v);
  float3 gathered = 0.f;
  float weights = 0.f;
  uint met = 0u;
  uint open = 0u;
  [loop]
  for (uint sample = 0u; sample < samples; ++sample) {
    const float3 half_local = mirror ? float3(0.f, 0.f, 1.f)
                                     : GgxVisibleNormal(view_local, alpha, Random(uint3(id, frame * 16u + sample * 2u + 3072u)),
                                                        Random(uint3(id, frame * 16u + sample * 2u + 3073u)));
    const float3 half_vector = half_local.x * basis[0] + half_local.y * basis[1] + half_local.z * normal;
    const float3 direction = reflect(-view, half_vector);
    const float n_dot_l = dot(normal, direction);
    if (n_dot_l <= 0.f) continue;
    if (traced) {
      float first_distance;
      float3 first_normal;
      const float3 sample_light = PathRadiance(origin, direction, 1.0e4f, id, sample + 4u, first_distance, first_normal);
      if (first_distance < 1e29f) {
        const float weight = mirror ? 1.f : GgxSampleWeight(n_dot_v, n_dot_l, alpha);
        gathered += sample_light * weight;
        weights += weight;
        ++met;
      }
      if (first_distance > reflection_range) ++open;
    } else if (!Occluded(origin, direction, reflection_range)) {
      ++open;
    }
  }
  const float4 current = float4((weights > 0.f) ? gathered / weights : 0.f, (float)met / samples);
  const float current_open = (float)open / samples;
  // Mirrors keep no history; rougher lobes up to MAX_SPECULAR_FRAMES (last frame's where it saw this surface).
  float4 previous = 0.f;
  float2 previous_meta = float2(1.f, 0.f);
  uint2 previous_pixel;
  if ((path_flags & PATH_SPECULAR_HISTORY) != 0u && !mirror && SeenLastFrame(position.xyz, previous_pixel)) {
    previous = specular_history[previous_pixel];
    previous_meta = specular_meta_history[previous_pixel];
  }
  const float frames = min(previous_meta.y + 1.f, mirror ? 1.f : lerp(2.f, MAX_SPECULAR_FRAMES, saturate(roughness * 3.f)));
  specular_accumulated[id] = lerp(previous, current, 1.f / frames);
  specular_meta[id] = float2(lerp(previous_meta.x, current_open, 1.f / frames), frames);
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_PATH_HLSLI_
