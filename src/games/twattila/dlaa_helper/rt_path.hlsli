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
RWBuffer<uint> light_fit : register(u14);             // count, x, x^2, y rgb, x y rgb, base colour rgb
RWBuffer<float4> light_kept : register(u15);          // [0] rgb in shade, [1] rgb the sun adds, [2] the ambient, [3] base colour
                                                      // cube's scale to them (w 1: solved)
RWTexture2D<float4> specular_accumulated : register(u16);
RWTexture2D<float4> specular_history : register(u17);
RWTexture2D<float2> specular_meta : register(u18);         // specular occlusion, frames
RWTexture2D<float2> specular_meta_history : register(u19);
RWTexture2D<float4> reflection : register(u20);            // out: reflected light, the share of samples that met a surface
RWTexture2D<unorm float> specular_occlusion : register(u21);
RWTexture2D<float> specular_hit_distance : register(u31);  // out (PATH_RAW): Ray Reconstruction's guide, metres
// ReSTIR GI (Ouyang et al. 2021, "ReSTIR GI: Path Resampling for Real-Time Path Tracing"): each bounce texel's paths
// become a reservoir of one surface their first rays met (its position, normal and the light leaving it toward the
// texel), merged with last frame's at the same surface (PathTraceTexel), then with neighbours' (ReuseTexel, which
// writes the bounce). Two sets, this frame's at PATH_REUSE_PARITY. As kajiya's ReSTIR GI (Embark Studios, MIT / Apache
// 2.0; compared, not copied): W clamped against fireflies, last frame's reservoir only on a surface facing the same way,
// and every third frame its sample re-checked (still in view, its light now): changed light shortens the history.
RWTexture2D<float4> reuse_samples[2] : register(u23);  // the surface's position, W (its unbiased contribution weight)
RWTexture2D<uint4> reuse_data[2] : register(u25);      // its light (f16 r | g, b | M), its normal, the texel's (octahedral)
// Own ReSTIR (Reflection Reuse): the reflections' own reservoirs at half the render pixels (ReflectReservoirTexel), laid
// out as ReSTIR GI's; two sets, this frame's at PATH_REFLECT_PARITY.
RWTexture2D<float4> reflect_samples[2] : register(u36);
RWTexture2D<uint4> reflect_data[2] : register(u38);

static const uint PATH_TRACING = 1u;          // the bounce texels' light path traced
static const uint PATH_REFLECTIONS = 2u;
static const uint PATH_SPECULAR_OCCLUSION = 4u;
static const uint PATH_HISTORY_VALID = 8u;    // last frame's colour and positions are last frame's
static const uint PATH_CACHE_VALID = 16u;     // the radiance cache has been filled
static const uint PATH_SPECULAR_HISTORY = 32u;
static const uint PATH_GUIDES = 64u;          // the G-buffer guides hold this frame's
static const uint PATH_SUN_TRACED = 128u;     // traced_visibility is this frame's
static const uint PATH_LIGHT = 256u;          // the prepared light is this frame's
static const uint PATH_RAW = 512u;            // Ray Reconstruction denoises the reflections: their hit distance for it
static const uint PATH_REUSE = 1024u;         // ReSTIR GI
static const uint PATH_REUSE_HISTORY = 2048u; // last frame's reservoirs are last frame's
static const uint PATH_REUSE_PARITY = 4096u;  // this frame's set: 1 (else 0)
static const uint PATH_LOCAL_LIGHTS = 8192u;  // the local lights light the surfaces the screen never saw (b4, v11)
static const uint PATH_PERMUTE = 65536u;      // last frame's reservoir from a neighbour texel (Ray Reconstruction)
static const uint PATH_AMBIENT = 262144u;     // Path-Traced Ambient: a camera ray that escapes brings the sky's light
static const uint PATH_KEEP_LIGHT = 524288u;  // the learned lighting kept (the game's ambient away: Path-Traced Ambient)
static const uint PATH_REFLECT_REUSE = 4194304u;  // the reflections resample ReSTIR GI's reservoirs (PATH_REUSE_PARITY: its set)
static const float REFLECT_REUSE_ROUGHNESS = 0.3f;  // the lobes broad enough for the bounce's samples to land in
static const uint PATH_REFLECT_OWN = 8388608u;          // Own ReSTIR: the reflections' own reservoirs
static const uint PATH_REFLECT_OWN_HISTORY = 16777216u; // last frame's are last frame's
static const uint PATH_REFLECT_PARITY = 33554432u;      // this frame's set: 1 (else 0)
static const uint REFLECT_NEIGHBOURS = 4u;
static const float REFLECT_RADIUS = 12.f;  // half-resolution texels at roughness 1
static const float REFLECT_SHIFT = 1.f;    // a neighbour at most this x alpha x its sample's distance away
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
static const float SKY_HIT_DISTANCE = 65504.f;  // NVIDIA's for the sky (FP16's most)
static const uint SKY_DIRECTIONS = 16u;  // SkyCalibration's fixed rays

#if defined(PATH_REORDER)
// Shader execution reordering (DXR 1.2, rt_path_ser.lib_6_9.hlsl): threads regroup by what a ray met (none, or its
// instance's group of eight) before shading it (the screen lookup, the cache, a shadow ray and the next rays diverge
// most there).
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

#if defined(SHARC)
#include "./rt_sharc.hlsli"
#endif

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

// A surface's base colour: its cell's, else (never seen; a grey made reflections go pale toward the screen's sides, as
// screen-space ones do, ATN's debug36) past the screen's edge the edge's on the way there (banks and walls go on past it),
// else the screen's mean.
float3 HitAlbedo(float3 hit) {
  float3 albedo;
  if (CacheAlbedo(hit, albedo)) return albedo;
  const float4 clip = ClipOf(hit, view_projection);
  if ((path_flags & PATH_GUIDES) != 0u && clip.w > 0.f) {
    const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
    const uint2 edge = min(uint2(saturate(uv) * float2(width, height)), uint2(width, height) - 1u);
    if (any(uv != saturate(uv)) && positions[edge].w > 0.f) return guide_albedo[edge].rgb;
  }
  const float4 mean = light_kept[3];
  return (mean.w > 0.f) ? mean.rgb : UNSEEN_ALBEDO;
}

// The light leaving a surface a ray met (false: nobody knows it): the screen's now, else last frame's, else the
// radiance cache's (SHaRC's instead in PathRadiance with SHaRC Radiance Cache).
bool HitLight(float3 hit, out float3 light_there) {
  light_there = 0.f;
  uint2 pixel;
  if ((path_flags & PATH_LIGHT) != 0u && SeenNow(hit, pixel)) {
    light_there = max(light[min(pixel / max(bounce_scale, 1u), uint2(bounce_width, bounce_height) - 1u)], 0.f);
    // Path-Traced Ambient took the game's ambient away from the screen's light: the surface's base colour x the learned
    // shade (as an unseen surface's) in its place (walls a path met brought back almost nothing: ATN's debug29, dark).
    if ((path_flags & PATH_KEEP_LIGHT) != 0u) {
      light_there += (((path_flags & PATH_GUIDES) != 0u) ? guide_albedo[pixel].rgb : HitAlbedo(hit)) * light_kept[0].rgb;
    }
    return true;
  }
  if (SeenLastFrame(hit, pixel)) {
    light_there = max(previous_color[pixel].rgb, 0.f);
    return true;
  }
#if defined(SHARC)
  return false;
#else
  return (path_flags & PATH_CACHE_VALID) != 0u && CacheLookup(hit, light_there);
#endif
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
    float3 direction;
    float light_distance;
    const float3 arriving = LightArriving(index, surface, normal, direction, light_distance);
    const float weight = Luminance(arriving);
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
  if (picked_weight <= 0.f || Occluded(surface, picked_direction, picked_distance - min(LIGHT_CLEARANCE, 0.5f * picked_distance), NO_GRASS_SMOKE)) {
    return 0.f;
  }
  return picked * (total / picked_weight);
}

// The light a surface the screen never saw sends back (its base colour lit): the sun's through a shadow ray from its
// `surface` (or from `behind` it through leaves, cloth and sails at their `transmittance`) once the learned lighting is
// solved, and with the local lights two of them picked (less noise from fires and torches than one) by `light_seed`.
float3 UnseenLight(float3 surface, float3 behind, float3 normal, float3 albedo, float transmittance, uint2 seed, uint light_seed) {
  const float4 toward_sun = sun[uint2(0, 0)];
  const float n_dot_l = dot(normal, toward_sun.xyz);
  const float sun_share = (n_dot_l > 0.f) ? n_dot_l : (-n_dot_l * transmittance);
  float3 emitted = 0.f;
  if (toward_sun.w != 0.f && sun_share > 0.f && light_kept[0].w > 0.f
      && !Occluded((n_dot_l > 0.f) ? surface : behind, toward_sun.xyz, 1.0e4f, NO_GRASS_SMOKE)) {
    emitted = albedo * light_kept[1].rgb * sun_share;
  }
  if ((path_flags & PATH_LOCAL_LIGHTS) != 0u) {
    emitted += albedo * (0.5f * (LocalLight(surface, normal, Random(uint3(seed, light_seed + 16384u)))
                                 + LocalLight(surface, normal, Random(uint3(seed, light_seed + 24576u)))));
  }
  return emitted;
}

// The game's ambient cube's mean over its six directions (the sun texture's texels 3-8; rt_sun.cs_5_0.hlsl).
float3 AmbientCubeMean() {
  float3 mean = 0.f;
  [unroll]
  for (uint side = 3u; side < 9u; ++side) mean += sun[uint2(side, 0)].rgb;
  return mean / 6.f;
}

// The game's ambient cube by a direction (the sun texture's texels 3-8: +x, -x, +y, -y, +z, -z; rt_sun.cs_5_0.hlsl).
float3 AmbientCube(float3 direction) {
  const float3 squared = direction * direction;
  return squared.x * sun[uint2((direction.x < 0.f) ? 4u : 3u, 0)].rgb + squared.y * sun[uint2((direction.y < 0.f) ? 6u : 5u, 0)].rgb
         + squared.z * sun[uint2((direction.z < 0.f) ? 8u : 7u, 0)].rgb;
}

// The sky's light along a path that escapes: the game's own ambient cube by the direction, scaled so its mean over
// directions is the learned shade light (`shade`, which stood for the whole sky before: bluer from above, the ground's
// colour from below now); the shade where the game gave none. With Path-Traced Ambient (the screen's shade the paths' own
// sky then) the cube by the scale learned before it (light_kept[2]).
float3 SkyLight(float3 direction, float3 shade) {
  if (sun[uint2(3, 0)].w == 0.f) return shade;
  const float4 scale = light_kept[2];
  if ((path_flags & PATH_AMBIENT) != 0u && scale.w > 0.f) return scale.rgb * AmbientCube(direction);
  return shade * AmbientCube(direction) / max(AmbientCubeMean(), 1e-4f);
}

// Path-Traced Ambient: the camera rays' sky scaled so an open surface gets the game's own ambient at its normal, in the
// game's colour and brightness (ATN's debug29-30: the learned scale's sky came out darker and warmer): the cube at
// `normal` as Attila's sun light shaders take it (its y four times over, normalized; x their scale, texel 4's w: 1.44
// with c11.x, ATN's debug32 desert came out darker and warmer without) over the sky's light a cosine-weighted ray
// brings there on average (SKY_DIRECTIONS fixed rays). 1 without.
float3 SkyCalibration(float3 normal) {
  if ((path_flags & PATH_AMBIENT) == 0u || sun[uint2(3, 0)].w == 0.f) return 1.f;
  float3 open = 0.f;
  [unroll]
  for (uint ray = 0u; ray < SKY_DIRECTIONS; ++ray) {
    open += SkyLight(CosineDirection(normal, (ray + 0.5f) / SKY_DIRECTIONS, frac(ray * 0.618034f)), light_kept[0].rgb);
  }
  return AmbientCube(normalize(normal * float3(1.f, 4.f, 1.f))) * sun[uint2(4, 0)].w / max(open / SKY_DIRECTIONS, 1e-4f);
}

// The light arriving along a ray (`first_distance`: how far its first surface is, 1e30: none; `first_normal` its normal;
// `sky_calibration`: SkyCalibration's, for the sky it meets first).
float3 PathRadiance(float3 origin, float3 direction, float range, float3 sky_calibration, uint2 seed, uint path, out float first_distance,
                    out float3 first_normal) {
  first_distance = 1e30f;
  first_normal = 0.f;
  float3 radiance = 0.f;
  float3 throughput = 1.f;
  const float4 shade = light_kept[0];
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
    TRACE_SURFACE(query, ray)
    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    REORDER(query, hit ? 1u + (query.CommittedInstanceID() & 7u) : 0u)
    if (!hit) {
      // Past the first surface the sky (the game's ambient by direction); from the camera's pixel the game's own
      // ambient covers it, but with Path-Traced Ambient (the game's taken away: the paths' the occluded sky).
      if (vertex > 0u) {
        radiance += throughput * SkyLight(direction, shade.rgb);
      } else if ((path_flags & PATH_AMBIENT) != 0u && !Occluded(origin, direction, min(range, GRASS_AMBIENT_RANGE), GRASS_STILL)) {
        radiance += SkyLight(direction, shade.rgb) * sky_calibration;
      }
      break;
    }
    const float3 position = origin + direction * query.CommittedRayT();
    if (vertex == 0u) first_distance = query.CommittedRayT();
    float3 light_there;
    if (HitLight(position, light_there)) {
      radiance += throughput * light_there;
      break;
    }
    const uint record_index = query.CommittedInstanceID() + query.CommittedGeometryIndex();
    const float3 normal = HitNormal(record_index, query.CommittedPrimitiveIndex(), direction, query.CommittedObjectToWorld3x4());
    if (vertex == 0u) first_normal = normal;
#if defined(SHARC)
    // SHaRC Radiance Cache: what the screen does not know from SHaRC (by the surface's normal; not for a ray shorter than
    // its voxel).
    const SharcHitData sharc_hit = {position, normal};
    if (query.CommittedRayT() >= SharcVoxelSize(position) && SharcGetCachedRadiance(Sharc(), sharc_hit, light_there, false)) {
      radiance += throughput * light_there;
      break;
    }
#endif
    const float3 albedo = HitAlbedo(position);
    const float offset = SurfaceOffset(distance(position, Camera()));
    const float3 surface = position + normal * offset;
    // Leaves, cloth and sails pass their share of light through (the sun's from behind; the path goes on through them
    // as often): green light under the trees.
    const uint record_flags = records[record_index].z >> 16;
    const float transmittance = ((record_flags & RECORD_MASKED) != 0u) ? LEAF_TRANSMITTANCE : (((record_flags & RECORD_THIN) != 0u) ? THIN_TRANSMITTANCE : 0.f);
    const float3 behind = position - normal * offset;
    const float3 emitted = UnseenLight(surface, behind, normal, albedo, transmittance, seed, frame * 64u + path * 8u + vertex * 2u);
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
    const bool through = transmittance > 0.f && Random(uint3(seed, frame * 64u + path * 8u + vertex * 2u + 32768u)) < transmittance;
    origin = through ? behind : surface;
    direction = CosineDirection(through ? -normal : normal, Random(uint3(seed, frame * 64u + path * 8u + vertex * 2u + 8192u)),
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

// W = the weights' sum over M times `target` (the sample kept's at the receiver; 0: nothing to show).
void Finish(inout Reservoir reservoir, float target) {
  reservoir.weight = (reservoir.count > 0.f && target > 0.f) ? reservoir.weight / (reservoir.count * target) : 0.f;
}

// The texel's own normal its reservoir was made for (0: none).
float3 LoadReceiverNormal(RWTexture2D<uint4> data, uint2 texel) {
  const uint packed = data[texel].w;
  return (packed != 0u) ? OctahedralDecode(packed) : 0.f;
}

Reservoir LoadReservoir(RWTexture2D<float4> samples, RWTexture2D<uint4> data, uint2 texel) {
  const float4 sample_point = samples[texel];
  const uint4 packed = data[texel];
  Reservoir reservoir;
  reservoir.hit = sample_point.xyz;
  reservoir.weight = sample_point.w;
  reservoir.light = float3(f16tof32(packed.x), f16tof32(packed.x >> 16), f16tof32(packed.y));
  reservoir.count = (float)(packed.y >> 16);
  reservoir.hit_normal = (packed.z != 0u) ? OctahedralDecode(packed.z) : 0.f;
  return reservoir;
}

void StoreReservoir(RWTexture2D<float4> samples, RWTexture2D<uint4> data, uint2 texel, Reservoir reservoir, float3 receiver_normal) {
  samples[texel] = float4(reservoir.hit, reservoir.weight);
  data[texel] = uint4(f32tof16(reservoir.light.r) | (f32tof16(reservoir.light.g) << 16),
                      f32tof16(reservoir.light.b) | (min((uint)reservoir.count, 65535u) << 16),
                      (dot(reservoir.hit_normal, reservoir.hit_normal) > 0.f) ? OctahedralEncode(reservoir.hit_normal) : 0u,
                      (dot(receiver_normal, receiver_normal) > 0.f) ? OctahedralEncode(receiver_normal) : 0u);
}

// Validation of last frame's reservoir from `origin`: its surface out of view now drops it; its light now (where the
// screen or the cache knows it) takes the stored light's place, and a change shortens the history (bright changes
// dimmed until they settle). The change (0: none, 1: all of the history goes).
float Validate(inout Reservoir previous, float3 origin) {
  const float3 to_hit = previous.hit - origin;
  const float hit_distance = length(to_hit);
  if (hit_distance <= 1e-3f || Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE)) {
    previous.count = 0.f;
    return 0.f;
  }
  float3 light_now;
  if (!HitLight(previous.hit, light_now)) return 0.f;
  const float3 change = abs(previous.light - light_now) / max(previous.light + light_now, 1e-3f);
  const float ratio = Luminance(previous.light) / max(Luminance(light_now), 1e-8f);
  previous.count *= clamp(ratio, 0.03f, 1.f);
  previous.weight *= clamp(ratio * 10.f, 0.01f, 1.f);
  previous.light = light_now;
  return smoothstep(0.1f, 0.5f, length(change) / 1.7320508f);
}

// A neighbour's sample moved to this receiver: the Jacobian of its surface's solid angles from here over from there (the
// sky's samples, no surface, as far as the sky: the same solid angle from either).
float ReuseJacobian(Reservoir other, float3 other_origin, float3 origin) {
  if (all(other.hit_normal == 0.f)) return 1.f;
  const float3 to_other = other_origin - other.hit;
  const float3 to_here = origin - other.hit;
  return abs(dot(other.hit_normal, normalize(to_here))) * dot(to_other, to_other)
         / max(abs(dot(other.hit_normal, normalize(to_other))) * dot(to_here, to_here), 1e-8f);
}

// The surface a bounce texel shows: its render pixel, world position (w: depth; 0: the sky), normal, rays' origin.
float4 TexelSurface(uint2 id, out float3 normal, out float3 origin) {
  const uint2 pixel = min(id * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u);
  const float4 position = positions[pixel];
  normal = 0.f;
  origin = position.xyz;
  if (position.w <= 0.f) return position;
  normal = PixelNormal(pixel, position);
  origin = RayOrigin(position, normal, Planar(int2(pixel), position, normal));
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
    if (reuse) StoreReservoir(reuse_samples[set], reuse_data[set], id, reservoir, 0.f);
    return;
  }
  const uint samples = max(path_samples, 1u);
  const float3 sky_calibration = SkyCalibration(normal);
  // Path-Traced Ambient's sky blocked as Ray-Traced AO's (within its reach, grass within GRASS_AMBIENT_RANGE): past it the
  // sky (ATN 2026-10-30: the Light Bounce's 50 m made it far darker than RT AO's 6 m); the light bounced within it stays.
  const float first_range = ((path_flags & PATH_AMBIENT) != 0u) ? min(bounce_range, sky_range) : bounce_range;
  // Path-Traced Ambient: the sky's samples too (ATN's debug29-30: without them its ambient was the ground's bounce alone,
  // darker and warmer over the reservoirs' 1-2 s).
  const bool sky_samples = (path_flags & PATH_AMBIENT) != 0u;
  float3 arriving = 0.f;
  [loop]
  for (uint path = 0u; path < samples; ++path) {
    const float3 direction = CosineDirection(normal, Random(uint3(id, frame * 16u + path * 2u + 1024u)),
                                             Random(uint3(id, frame * 16u + path * 2u + 1025u)));
    float first_distance;
    float3 first_normal;
    const float3 sample_light = PathRadiance(origin, direction, first_range, sky_calibration, id, path, first_distance, first_normal);
    arriving += sample_light;
    // Cosine-weighted rays: the target over their density is pi times the light's luminance.
    Reservoir candidate;
    candidate.hit = origin + direction * min(first_distance, first_range);
    candidate.hit_normal = first_normal;
    candidate.light = sample_light;
    candidate.count = 1.f;
    candidate.weight = 0.f;
    Merge(reservoir, candidate, (first_distance < 1e29f || sky_samples) ? 3.14159265f * Luminance(sample_light) : 0.f,
          Random(uint3(id, frame * 16u + path * 2u + 5120u)));
  }
  if (!reuse) {
    bounce[id] = float4(arriving / samples, distance(position.xyz, Camera()));
    return;
  }
  uint2 previous_pixel;
  if ((path_flags & PATH_REUSE_HISTORY) != 0u && SeenLastFrame(position.xyz, previous_pixel)) {
    uint2 previous_texel = previous_pixel / bounce_scale;
    // Under Ray Reconstruction a neighbour's in its 2 x 2 texels, a different one each frame (permutation sampling: it
    // takes neighbouring texels' samples as independent).
    if ((path_flags & PATH_PERMUTE) != 0u) previous_texel ^= uint2(frame & 1u, (frame >> 1) & 1u);
    previous_texel = min(previous_texel, uint2(bounce_width, bounce_height) - 1u);
    Reservoir previous = LoadReservoir(reuse_samples[1u - set], reuse_data[1u - set], previous_texel);
    // Last frame's reservoir was made for a surface facing about the same way (else a new history starts).
    if (dot(LoadReceiverNormal(reuse_data[1u - set], previous_texel), normal) < 0.8f) previous.count = 0.f;
    float invalidity = 0.f;
    if (previous.count > 0.f && previous.weight > 0.f && frame % REUSE_VALIDATION_PERIOD == 0u) invalidity = Validate(previous, origin);
    previous.count = min(previous.count, exp2(log2(REUSE_HISTORY) * (1.f - invalidity)) * samples);
    Merge(reservoir, previous, ReuseTarget(origin, normal, previous) * previous.weight * previous.count,
          Random(uint3(id, frame * 16u + 6144u)));
  }
  Finish(reservoir, ReuseTarget(origin, normal, reservoir));
  reservoir.weight = min(reservoir.weight, REUSE_W_CLAMP);
  StoreReservoir(reuse_samples[set], reuse_data[set], id, reservoir, normal);
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
  const Reservoir own = LoadReservoir(reuse_samples[set], reuse_data[set], id);
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
    const Reservoir other = LoadReservoir(reuse_samples[set], reuse_data[set], uint2(texel));
    if (other.count <= 0.f) continue;
    const float jacobian = ReuseJacobian(other, other_origin, origin);
    if (!(jacobian > 0.1f && jacobian < 10.f)) continue;
    Merge(combined, other, ReuseTarget(origin, normal, other) * other.weight * other.count * jacobian,
          Random(uint3(id, frame * 16u + neighbour + 7300u)));
  }
  Finish(combined, ReuseTarget(origin, normal, combined));
  // The chosen surface in view of this one (no light through walls); the integrand over its density.
  const float3 to_hit = combined.hit - origin;
  const float hit_distance = length(to_hit);
  float3 arriving = 0.f;
  if (combined.weight > 0.f && hit_distance > 1e-3f && !Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE)) {
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

// The density of a reflected direction under visible-normal sampling (GgxVisibleNormal, then reflected): G1(v) D(h) /
// (4 n.v).
float GgxReflectionDensity(float n_dot_v, float n_dot_h, float alpha) {
  const float alpha2 = alpha * alpha;
  const float denominator = n_dot_h * n_dot_h * (alpha2 - 1.f) + 1.f;
  const float d = alpha2 / max(3.14159265f * denominator * denominator, 1e-8f);
  const float g1 = 2.f * n_dot_v / max(n_dot_v + sqrt(alpha2 + (1.f - alpha2) * n_dot_v * n_dot_v), 1e-6f);
  return g1 * d / max(4.f * n_dot_v, 1e-6f);
}

// A reflected direction of a pixel's GGX lobe (`basis`: Basis(normal); `view_local`: the view in it).
float3 GgxDirection(float3x3 basis, float3 view, float3 view_local, float alpha, float2 random) {
  return reflect(-view, mul(GgxVisibleNormal(view_local, alpha, random.x, random.y), basis));
}

// A pixel's GGX lobe's density toward a point (of its reflected directions; 0: below the surface). Reflection Reuse's
// target: a sample's light (luminance) by it.
float LobeDensity(float3 receiver, float3 normal, float3 view, float alpha, float3 target) {
  const float3 direction = normalize(target - receiver);
  if (dot(normal, direction) <= 0.f) return 0.f;
  return GgxReflectionDensity(max(dot(normal, view), 1e-3f), saturate(dot(normal, normalize(view + direction))), alpha);
}

// The render pixel an Own ReSTIR texel stands for this frame: one of its 2 x 2, a different one each frame.
uint2 ReservoirPixel(uint2 texel) {
  return min(texel * 2u + uint2(frame & 1u, (frame >> 1) & 1u), uint2(width, height) - 1u);
}

// Own ReSTIR's reservoirs (at half the render pixels, for the lobes Reflection Reuse takes): a GGX sample of the lobe a
// frame (its target over its density: its light's luminance; the samples that met a surface only, as the reflections'
// light: the sky's is the game's), merged with last frame's at the same surface as the texel's lobe (its pixel changes
// each frame: near walls a pixel's shift moves the lobe's density at a sample far); kept with the weights' mean in
// place of W (ReflectPixel merges neighbours').
void ReflectReservoirTexel(uint2 id) {
  if (any(id >= (uint2(width, height) + 1u) / 2u)) return;
  const uint set = ((path_flags & PATH_REFLECT_PARITY) != 0u) ? 1u : 0u;
  const uint2 pixel = ReservoirPixel(id);
  const float4 position = positions[pixel];
  const float roughness = saturate(guide_roughness[pixel]);
  Reservoir reservoir = (Reservoir)0;
  if (position.w <= 0.f || roughness < REFLECT_REUSE_ROUGHNESS || roughness > ROUGH_LIMIT) {
    StoreReservoir(reflect_samples[set], reflect_data[set], id, reservoir, 0.f);
    return;
  }
  const float3 normal = PixelNormal(pixel, position);
  const float3 view = normalize(Camera() - position.xyz);
  const float n_dot_v = max(dot(normal, view), 1e-3f);
  const float3 origin = RayOrigin(position, normal, Planar(int2(pixel), position, normal));
  const float alpha = roughness * roughness;
  const float3x3 basis = Basis(normal);
  const float3 direction = GgxDirection(basis, view, float3(dot(view, basis[0]), dot(view, basis[1]), n_dot_v), alpha,
                                        float2(Random(uint3(pixel, frame * 16u + 9216u)), Random(uint3(pixel, frame * 16u + 9217u))));
  if (dot(normal, direction) > 0.f) {
    float first_distance;
    float3 first_normal;
    const float3 sample_light = PathRadiance(origin, direction, 1.0e4f, 1.f, pixel, 24u, first_distance, first_normal);
    if (first_distance < 1e29f) {
      Reservoir candidate;
      candidate.hit = origin + direction * first_distance;
      candidate.hit_normal = first_normal;
      candidate.light = sample_light;
      candidate.count = 1.f;
      candidate.weight = 0.f;
      Merge(reservoir, candidate, Luminance(sample_light), Random(uint3(pixel, frame * 16u + 9218u)));
    }
  }
  uint2 previous_pixel;
  if ((path_flags & PATH_REFLECT_OWN_HISTORY) != 0u && SeenLastFrame(position.xyz, previous_pixel)) {
    const uint2 previous_texel = min(previous_pixel / 2u, (uint2(width, height) + 1u) / 2u - 1u);
    Reservoir previous = LoadReservoir(reflect_samples[1u - set], reflect_data[1u - set], previous_texel);
    if (dot(LoadReceiverNormal(reflect_data[1u - set], previous_texel), normal) < 0.8f) previous.count = 0.f;
    float invalidity = 0.f;
    if (previous.count > 0.f && previous.weight > 0.f && frame % REUSE_VALIDATION_PERIOD == 0u) invalidity = Validate(previous, origin);
    previous.count = min(previous.count, exp2(log2(REUSE_HISTORY) * (1.f - invalidity)));
    if (LobeDensity(origin, normal, view, alpha, previous.hit) > 0.f) {
      Merge(reservoir, previous, previous.weight * previous.count, Random(uint3(pixel, frame * 16u + 9219u)));
    }
  }
  reservoir.weight = (reservoir.count > 0.f) ? reservoir.weight / reservoir.count : 0.f;
  StoreReservoir(reflect_samples[set], reflect_data[set], id, reservoir, normal);
}

// Reflections and specular occlusion of a render pixel (accumulated by world position).
void ReflectPixel(uint2 id) {
  if (id.x >= width || id.y >= height) return;
  const float4 position = positions[id];
  if (position.w <= 0.f || (path_flags & PATH_GUIDES) == 0u) {
    specular_accumulated[id] = 0.f;
    specular_meta[id] = float2(1.f, 0.f);
    if ((path_flags & PATH_RAW) != 0u) specular_hit_distance[id] = SKY_HIT_DISTANCE;
    return;
  }
  const float3 normal = PixelNormal(id, position);
  const float3 surface_normal = SurfaceNormal(int2(id), position);
  const float3 view = normalize(Camera() - position.xyz);
  const float n_dot_v = max(dot(normal, view), 1e-3f);
  const float3 origin = RayOrigin(position, normal, Planar(int2(id), position, normal));
  const float roughness = saturate(guide_roughness[id]);
  const float alpha = max(roughness * roughness, 1e-3f);
  const bool mirror = roughness < 0.05f;
  const bool traced = (path_flags & PATH_REFLECTIONS) != 0u && roughness <= ROUGH_LIMIT;
  const bool occlusion = (path_flags & PATH_SPECULAR_OCCLUSION) != 0u && !traced;
  const uint samples = mirror ? 1u : max(path_samples, 1u);
  const float3x3 basis = Basis(normal);
  const float3 view_local = float3(dot(view, basis[0]), dot(view, basis[1]), n_dot_v);
  float3 gathered = 0.f;
  float weights = 0.f;
  float distances = 0.f;  // of the samples that met a surface
  uint met = 0u;
  uint open = 0u;
  [loop]
  for (uint sample = 0u; sample < samples; ++sample) {
    float3 direction = mirror ? reflect(-view, normal)
                              : GgxDirection(basis, view, view_local, alpha,
                                             float2(Random(uint3(id, frame * 16u + sample * 2u + 3072u)),
                                                    Random(uint3(id, frame * 16u + sample * 2u + 3073u))));
    const float n_dot_l = dot(normal, direction);
    if (n_dot_l <= 0.f) continue;
    // A normal map can turn a ray into its own surface: on waves, the more the view grazes them (the sides of the screen),
    // the riverbed under the water showed in them (ATN's debug36). Such a ray bounces off the surface, as off the next wave.
    direction -= 2.f * min(dot(direction, surface_normal), 0.f) * surface_normal;
    if (traced) {
      float first_distance;
      float3 first_normal;
      const float3 sample_light = PathRadiance(origin, direction, 1.0e4f, 1.f, id, sample + 4u, first_distance, first_normal);
      if (first_distance < 1e29f) {
        const float weight = mirror ? 1.f : GgxSampleWeight(n_dot_v, n_dot_l, alpha);
        gathered += sample_light * weight;
        weights += weight;
        distances += first_distance;
        ++met;
      }
    } else if (occlusion && !Occluded(origin, direction, reflection_range, NO_GRASS_SMOKE)) {
      ++open;
    }
  }
  // Reflection Reuse from ReSTIR GI (PATH_REFLECT_REUSE, rougher lobes): the Light Bounce's reservoir at this pixel's
  // texel as a second candidate, resampled with the rays' own light (generalized RIS, its weights 1/2: the rays' light
  // by its target over their density, the reservoir's by the lobe's density at it times its W); one ray to the chosen
  // reservoir sample.
  if ((path_flags & PATH_REFLECT_REUSE) != 0u && traced && !mirror && roughness >= REFLECT_REUSE_ROUGHNESS) {
    const uint set = ((path_flags & PATH_REUSE_PARITY) != 0u) ? 1u : 0u;
    const Reservoir bounce = LoadReservoir(reuse_samples[set], reuse_data[set], min(id / bounce_scale, uint2(bounce_width, bounce_height) - 1u));
    const float3 to_hit = bounce.hit - origin;
    const float hit_distance = length(to_hit);
    const float3 own = (weights > 0.f) ? gathered / weights : 0.f;
    const float bounce_weight = (bounce.weight > 0.f && hit_distance > 1e-3f)
                                    ? Luminance(bounce.light) * LobeDensity(origin, normal, view, alpha, bounce.hit) * bounce.weight
                                    : 0.f;
    const float own_weight = Luminance(own);
    const float total = own_weight + bounce_weight;
    if (total > 0.f) {
      const bool bounce_chosen = Random(uint3(id, frame * 16u + 3584u)) * total < bounce_weight
                                 && !Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE);
      const float3 chosen = bounce_chosen ? bounce.light : own;
      gathered = chosen / max(Luminance(chosen), 1e-6f) * (0.5f * total);
      weights = 1.f;
      if (bounce_chosen) {
        distances += hit_distance;
        ++met;
      }
    }
  }
  // Own ReSTIR (PATH_REFLECT_OWN, rougher lobes): the reflections' own reservoirs at this pixel's texel and neighbours' on
  // a surface alike (normal, roughness) resampled by this lobe's target, each by the generalized balance heuristic (this
  // lobe's density at its sample over the reservoirs' lobes' mean there; moved here by the Jacobian; at most
  // REUSE_W_CLAMP: no fireflies). The chosen sample's colour at the reservoirs' mean weight (their luminance: by the
  // lobes' ratios it was far noisier, near walls a pixel's shift moves a lobe's density at a sample far), if in view,
  // joins the rays' own by the samples each stands for (the share that met a surface stays the rays': the reservoirs
  // keep only those).
  if ((path_flags & PATH_REFLECT_OWN) != 0u && traced && !mirror && roughness >= REFLECT_REUSE_ROUGHNESS) {
    const uint set = ((path_flags & PATH_REFLECT_PARITY) != 0u) ? 1u : 0u;
    Reservoir candidates[REFLECT_NEIGHBOURS + 1u];
    float3 receivers[REFLECT_NEIGHBOURS + 1u];
    float3 receiver_normals[REFLECT_NEIGHBOURS + 1u];
    float3 receiver_views[REFLECT_NEIGHBOURS + 1u];
    float receiver_alphas[REFLECT_NEIGHBOURS + 1u];
    float total_count = 0.f;
    [unroll]
    for (uint neighbour = 0u; neighbour <= REFLECT_NEIGHBOURS; ++neighbour) {
      candidates[neighbour] = (Reservoir)0;
      receivers[neighbour] = origin;
      receiver_normals[neighbour] = normal;
      receiver_views[neighbour] = view;
      receiver_alphas[neighbour] = alpha;
      int2 texel = int2(id / 2u);
      if (neighbour != 0u) {
        const float angle = 6.2831853f * Random(uint3(id, frame * 16u + neighbour * 2u + 9300u));
        const float radius = max(REFLECT_RADIUS * roughness, 1.f) * sqrt(Random(uint3(id, frame * 16u + neighbour * 2u + 9301u)));
        texel += int2(round(float2(cos(angle), sin(angle)) * radius));
      }
      if (any(texel < 0) || any(texel >= int2((uint2(width, height) + 1u) / 2u))) continue;
      const uint2 other_pixel = ReservoirPixel(uint2(texel));
      const float4 other_position = positions[other_pixel];
      if (other_position.w <= 0.f || abs(other_position.w - position.w) > 0.1f * position.w) continue;
      const float3 other_normal = PixelNormal(other_pixel, other_position);
      const float other_roughness = saturate(guide_roughness[other_pixel]);
      if (dot(other_normal, normal) < 0.95f || abs(other_roughness - roughness) > 0.1f) continue;
      const Reservoir other = LoadReservoir(reflect_samples[set], reflect_data[set], uint2(texel));
      // Its sample seen from here at an angle within this lobe's width (near a wall neighbours a few pixels apart see it
      // far apart).
      if (neighbour != 0u && distance(other_position.xyz, position.xyz) > REFLECT_SHIFT * alpha * distance(other.hit, origin)) continue;
      candidates[neighbour] = other;
      receivers[neighbour] = other_position.xyz;
      receiver_normals[neighbour] = other_normal;
      receiver_views[neighbour] = normalize(Camera() - other_position.xyz);
      receiver_alphas[neighbour] = max(other_roughness * other_roughness, 1e-3f);
      total_count += candidates[neighbour].count;
    }
    Reservoir combined = (Reservoir)0;
    float weight_sum = 0.f;
    float counted = 0.f;
    [unroll]
    for (uint index = 0u; index <= REFLECT_NEIGHBOURS; ++index) {
      const Reservoir other = candidates[index];
      if (other.count <= 0.f || other.weight <= 0.f) continue;
      const float jacobian = ReuseJacobian(other, receivers[index], origin);
      if (!(jacobian > 0.1f && jacobian < 10.f)) continue;
      float densities = 0.f;
      [unroll]
      for (uint source = 0u; source <= REFLECT_NEIGHBOURS; ++source) {
        densities += candidates[source].count
                     * LobeDensity(receivers[source], receiver_normals[source], receiver_views[source], receiver_alphas[source], other.hit);
      }
      if (densities <= 0.f) continue;
      const float ratio = LobeDensity(origin, normal, view, alpha, other.hit) * jacobian * total_count / densities;
      Merge(combined, other, other.weight * other.count * min(ratio, REUSE_W_CLAMP), Random(uint3(id, frame * 16u + index + 9400u)));
      weight_sum += other.weight * other.count;
      counted += other.count;
    }
    const float3 to_hit = combined.hit - origin;
    const float hit_distance = length(to_hit);
    if (combined.weight > 0.f && hit_distance > 1e-3f && !Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE)) {
      const float3 reused = combined.light / max(Luminance(combined.light), 1e-6f) * (weight_sum / counted);
      const float own_count = (weights > 0.f) ? (float)met : 0.f;
      gathered = (((weights > 0.f) ? gathered / weights : 0.f) * own_count + reused * counted) / (own_count + counted);
      weights = 1.f;
    }
  }
  const float4 current = float4((weights > 0.f) ? gathered / weights : 0.f, min((float)met / samples, 1.f));
  // The share of the lobe open to the game's environment: where the rays were traced what they met stands in for it
  // (the reflection pass dims the rest by this too: 1), without Specular Occlusion 1.
  const float current_open = occlusion ? (float)open / samples : 1.f;
  // Ray Reconstruction's specular hit distance: the mean distance to what the samples met (the sky's where none did, as
  // where none were traced: the game's reflection is the sky's or a cube map's).
  if ((path_flags & PATH_RAW) != 0u) specular_hit_distance[id] = (met != 0u) ? distances / met : SKY_HIT_DISTANCE;
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
