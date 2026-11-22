/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's path tracing and reflections (rt_d3d12.hpp Paths, bridge v8; ported from Tales of Arise's rt_path.hlsli
// and rt_cache.hlsli, in metres): the light a ray brings back from the scene the addon captured.
// - The light leaving a surface a ray meets: the frame's light where the camera sees that surface now (the prepared
//   light, the Light Bounce's: one texel per bounce_scale x bounce_scale render pixels; with Path-Traced Ambient and the
//   game's sun light, of no view, its base colour lit by them: SeenDiffuseLight), else last frame's scene colour
//   where last frame saw it (the DLAA input, reprojected; only without the prepared light), else the world radiance cache's
//   (cells of 25 cm near the camera, doubling with the distance past 20 m, one per face a surface turns to; filled from the
//   prepared light every frame), else, for a surface nobody saw,
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
#include "../rt_leg_fog.hlsli"

Texture2D<float3> light : register(t9);               // the prepared light (bounce texels; PATH_LIGHT_FINAL: the light buffer, render pixels)
Texture2D<float4> previous_color : register(t10);     // last frame's scene colour (render pixels)
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
RWBuffer<float4> light_kept : register(u15);          // [0] rgb in shade, [1] rgb the sun adds, [2] the ambient, [3] base colour, [4] the game's ambient
                                                      // cube's scale to them (w 1: solved)
RWTexture2D<float4> specular_accumulated : register(u16);
RWTexture2D<float4> specular_history : register(u17);
RWTexture2D<float2> specular_meta : register(u18);         // specular occlusion, frames
RWTexture2D<float2> specular_meta_history : register(u19);
RWTexture2D<float4> reflection : register(u20);            // out: reflected light, the share of samples that met a surface
RWTexture2D<unorm float> specular_occlusion : register(u21);
RWTexture2D<float> specular_hit_distance : register(u31);  // out (PATH_RAW): Ray Reconstruction's guide, metres
RWTexture2D<float> nrd_hit_distance : register(u44);       // out (PATH_NRD, the Light Bounce's): metres
RWTexture2D<float4> nrd_reflection : register(u50);        // out (PATH_NRD, the reflections'): light x the share that met, metres
// ReSTIR GI (Ouyang et al. 2021, "ReSTIR GI: Path Resampling for Real-Time Path Tracing"): each bounce texel's paths
// become a reservoir of one surface their first rays met (its position, normal and the light leaving it toward the
// texel), merged with last frame's at the same surface (PathTraceTexel), then with neighbours' (ReuseTexel, which
// writes the bounce). Two sets, this frame's at PATH_REUSE_PARITY. As kajiya's ReSTIR GI (Embark Studios, MIT / Apache
// 2.0; compared, not copied): W clamped against fireflies, last frame's reservoir only on a surface facing the same way,
// and every third frame its sample re-checked (still in view, its light now): changed light shortens the history.
RWTexture2D<float4> reuse_samples[2] : register(u23);  // the surface's position, W (its unbiased contribution weight)
RWTexture2D<uint4> reuse_data[2] : register(u25);      // its light (f16 r | g, b | M), its normal, the texel's (octahedral)
// This frame's surface of each bounce texel as PathTraceTexel found it (TexelSurface's), for ReuseTexel: its rays' origin
// (w: its depth; 0: the sky) and its normal. Found again there for itself and five neighbours (StartPast's ray, the
// normal's and planarity's fetches each), they cost more than the reuse's own work (Nsight at 4K: 2.5 -> 1.5 ms).
// Every texel ReuseTexel reads holds this request's: its paths and its reuse dispatch the same bounce_width x
// bounce_height with the same constants (rt_d3d12.hpp), and no mode traces a part of them; one that would must write the
// texels it leaves out as the sky (w 0).
RWTexture2D<float4> surface_origin : register(u58);
RWTexture2D<float4> surface_normal : register(u59);
// Own ReSTIR (Reflection Reuse): the reflections' own reservoirs at half the render pixels (ReflectReservoirTexel), laid
// out as ReSTIR GI's; two sets, this frame's at PATH_REFLECT_PARITY.
RWTexture2D<float4> reflect_samples[2] : register(u36);
RWTexture2D<uint4> reflect_data[2] : register(u38);
// Path-Traced Ambient's own sky (helpers 107 on): the sky a bounce texel's paths brought (rgb, w 1; 0: none), two sets, the
// paths' at PATH_SKY_PARITY, the other's the last paths' (PATH_SKY_HISTORY: valid at these texels).
RWTexture2D<float4> own_sky[2] : register(u54);

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
static const uint PATH_KEEP_CUBE = 1073741824u;  // with it: its shade from the game's cube (KeptShade)
static const uint PATH_REFLECT_REUSE = 4194304u;  // the reflections resample ReSTIR GI's reservoirs (PATH_REUSE_PARITY: its set)
static const float REFLECT_REUSE_ALPHA = 0.09f;  // the lobes broad enough for the bounce's samples to land in
static const uint PATH_REFLECT_OWN = 8388608u;          // Own ReSTIR: the reflections' own reservoirs
static const uint PATH_REFLECT_OWN_HISTORY = 16777216u; // last frame's are last frame's
static const uint PATH_REFLECT_PARITY = 33554432u;      // this frame's set: 1 (else 0)
static const uint PATH_LIGHT_FINAL = 67108864u;         // a reflections request's: `light` is the light buffer the game's pass reads
static const uint PATH_SQUARED_LOBE = 536870912u;       // reflections: alpha = roughness^2 (ReflectAlpha)
static const uint PATH_NRD = 268435456u;                // NVIDIA's REBLUR denoises this request's light (rt_nrd_front.hlsl)
static const uint PATH_SKY_APART = 134217728u;          // Own Sky Rays: Path-Traced AO's sky past the reservoirs
static const uint PATH_REFLECT_FOG = 16384u;            // reflections: the game's fog between a pixel and what its rays met
static const uint PATH_REFLECT_STEADY = 32768u;         // Steadier Water Reflections (rt_reflect_filter.cs_6_5.hlsl)
static const uint PATH_SKY_HISTORY = 131072u;  // own_sky's other set the last paths' (the trace's PATH_LIGHT_HISTORY: another request's word)
static const uint PATH_SKY_PARITY = 1048576u;  // the paths write own_sky's set 1, the other is read (else 0; the trace's PATH_TINT)
static const uint PATH_SUN_LIGHT = 2097152u;   // with PATH_KEEP_LIGHT: RT_PATH_SUN_LIGHT, SUN_LIGHT_TEXEL holds the game's sun light (the trace's PATH_TINT_GUIDES)
static const uint REFLECT_NEIGHBOURS = 4u;
static const float REFLECT_RADIUS = 12.f;  // half-resolution texels at roughness 1
static const float REFLECT_SHIFT = 1.f;    // a neighbour at most this x alpha x its sample's distance away
static const float REUSE_HISTORY = 20.f;      // last frame's samples at most this many times this frame's
static const float REUSE_W_CLAMP = 10.f;      // a sample's weight at most this many times one ray's (kajiya's RESTIR_RESERVOIR_W_CLAMP; the bounce's: W x cos / pi)
static const uint REUSE_VALIDATION_PERIOD = 3u;
#if defined(REUSE_NEAR)
// Near ReSTIR Reuse (only its rt_reuse_near build): three neighbours within half the reach (Lean Reuse's test with these
// scored better, its pass about half the cost).
static const uint REUSE_NEIGHBOURS = 3u;
static const float REUSE_RADIUS = 6.f;        // bounce texels
#else
static const uint REUSE_NEIGHBOURS = 5u;
static const float REUSE_RADIUS = 12.f;       // bounce texels
#endif

static const uint CACHE_ENTRIES = 1u << 20;
static const uint CACHE_PROBES = 8u;
static const float CACHE_CELL = 0.25f;
static const float CACHE_LEVEL_DISTANCE = 10.f;
static const uint CACHE_LEVELS = 8u;
static const float CACHE_SCALE = 256.f;
static const float CACHE_MAX = 1024.f;
static const float CACHE_BLEND = 0.2f;
static const float CACHE_LIFETIME = 1800.f;
static const float CACHE_RELEARN = 0.125f;  // the share of the paths that leaves the cache out (PathRadiance)
static const float LIGHT_FIT_X = 4096.f;
static const float LIGHT_FIT_Y = 64.f;
static const float LIGHT_FIT_MAX_Y = 16.f;
static const float LIGHT_FIT_BLEND = 0.1f;
static const float UNSEEN_ALBEDO = 0.35f;
static const float CONTACT_RANGE = 0.3f;  // metres (ContactShade)
static const float SEEN_FACE = 0.25f;  // SeenNow: a hit's normal within 75 degrees of the pixel's
static const float ROUGH_LIMIT = 0.75f;  // rougher surfaces keep the game's reflection: only their occlusion is traced
static const float MAX_SPECULAR_FRAMES = 12.f;
static const float SKY_HIT_DISTANCE = 65504.f;  // NVIDIA's for the sky (FP16's most)

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

// The radiance cache's cell size at a world position: CACHE_CELL out to twice CACHE_LEVEL_DISTANCE from the camera, doubling
// with each doubling of the distance past it.
float CacheCellSize(float3 world) {
  const float level = min(floor(log2(max(length(world - Camera()) / CACHE_LEVEL_DISTANCE, 1.f))), CACHE_LEVELS - 1.f);
  return CACHE_CELL * exp2(level);
}

// A cell's slot and key: its place and size, and the face its surface turns to (the normal's dominant axis and sign). Cells
// are 0.5 to 2 m where Total War's camera looks: by place alone a step's face in shade, a wall's back, took the light of
// the sunlit top or front beside it wherever the screen didn't see them (ATN: Path Tracing too bright out of sight).
void CacheCell(float3 world, float3 normal, out uint slot, out uint key) {
  const float size = CacheCellSize(world);
  const float3 magnitude = abs(normal);
  const uint axis = (magnitude.x >= max(magnitude.y, magnitude.z)) ? 0u : ((magnitude.y >= magnitude.z) ? 1u : 2u);
  // The axis's component by selects: indexed by a variable (normal[axis]) the normal went to a local array, a stack store
  // and load in every cache lookup (DXIL alloca; NVIDIA's local memory).
  const float along = (axis == 0u) ? normal.x : ((axis == 1u) ? normal.y : normal.z);
  const uint face = axis * 2u + ((along < 0.f) ? 1u : 0u);
  // The size is a power of two: its low mantissa bits are free for the face.
  const uint4 value = uint4(asuint(int3(floor(world / size))), asuint(size) | face);
  slot = CacheHash(value) & (CACHE_ENTRIES - 1u);
  key = CacheHash(value.yzwx + 0x9E3779B9u) | 1u;
}

bool CacheFind(float3 world, float3 normal, out uint entry) {
  uint slot, key;
  CacheCell(world, normal, slot, key);
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

bool CacheClaim(float3 world, float3 normal, out uint entry) {
  uint slot, key;
  CacheCell(world, normal, slot, key);
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

bool CacheLookup(float3 world, float3 normal, out float3 radiance) {
  radiance = 0.f;
  uint entry;
  if (!CacheFind(world, normal, entry)) return false;
  const float4 kept = cache_radiance[entry];
  radiance = kept.rgb;
  return kept.w > 0.f;
}

// The fill adds its texels' lights by wave as this adds one (rt_cache_fill.cs_6_5.hlsl CacheAddInWave).
bool CacheAdd(float3 world, float3 normal, float3 light_there) {
  uint entry;
  if (!CacheClaim(world, normal, entry)) return false;
  const uint3 fixed_point = (uint3)(clamp(light_there, 0.f, CACHE_MAX) * CACHE_SCALE + 0.5f);
  InterlockedAdd(cache_sums[entry * 4u], fixed_point.r);
  InterlockedAdd(cache_sums[entry * 4u + 1u], fixed_point.g);
  InterlockedAdd(cache_sums[entry * 4u + 2u], fixed_point.b);
  InterlockedAdd(cache_sums[entry * 4u + 3u], 1u);
  return true;
}

bool CacheAlbedo(float3 world, float3 normal, out float3 albedo) {
  albedo = 0.f;
  uint entry;
  if (!CacheFind(world, normal, entry)) return false;
  const uint packed = cache_albedo[entry].x;
  albedo = float3(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu) / 255.f;
  return (packed >> 24) != 0u;
}

// The pixel's normal: the G-buffer's (`guide`, read when the guides hold it) when it holds one, else from the positions.
float3 PixelNormalOf(uint2 pixel, float4 position, float3 guide, Neighbours neighbours) {
  if ((path_flags & PATH_GUIDES) != 0u && dot(guide, guide) > 0.5f) return normalize(guide);
  return SurfaceNormalOf(int2(pixel), position, neighbours);
}

// The G-buffer's normal at a pixel when the guides hold this frame's (else none).
float3 GuideNormal(uint2 pixel) {
  return ((path_flags & PATH_GUIDES) != 0u) ? guide_normals[pixel].xyz : 0.f;
}

// PixelNormalOf's with its loads where needed: the neighbours only without a guide normal.
float3 PixelNormal(uint2 pixel, float4 position) {
  if ((path_flags & PATH_GUIDES) != 0u) {
    const float3 guide = guide_normals[pixel].xyz;
    if (dot(guide, guide) > 0.5f) return normalize(guide);
  }
  return SurfaceNormal(int2(pixel), position);
}

// This frame's pixel of a world position where the camera sees that same surface (false: off screen or hidden), facing the
// same way as `normal` (0: any). Within 5 cm and 2% of the depth (0.95 m at 45 m) the face beside it is "the same surface"
// too: a wall's back or a step's face in shade took the light of the sunlit top the camera saw there.
bool SeenNow(float3 world, float3 normal, out uint2 pixel) {
  pixel = 0u;
  const float4 clip = ClipOf(world, view_projection);
  if (clip.w <= 0.f) return false;
  const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
  if (any(uv < 0.f) || any(uv >= 1.f)) return false;
  pixel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1u);
  const float4 seen = positions[pixel];
  if (seen.w <= 0.f || distance(seen.xyz, world) >= 0.05f + 0.02f * seen.w) return false;
  return all(normal == 0.f) || dot(normal, PixelNormal(pixel, seen)) > SEEN_FACE;
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
  const float4 seen = previous_positions[uint3(pixel, previous_positions_slice)];
  return seen.w > 0.f && distance(seen.xyz, world) < 0.03f + 0.01f * seen.w;
}

// A surface's base colour: its cell's, else (never seen; a grey made reflections go pale toward the screen's sides, as
// screen-space ones do, ATN's debug36) past the screen's edge the edge's on the way there (banks and walls go on past it),
// else the screen's mean.
float3 HitAlbedo(float3 hit, float3 hit_normal) {
  float3 albedo;
  if (CacheAlbedo(hit, hit_normal, albedo)) return albedo;
  const float4 clip = ClipOf(hit, view_projection);
  if ((path_flags & PATH_GUIDES) != 0u && clip.w > 0.f) {
    const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
    const uint2 edge = min(uint2(saturate(uv) * float2(width, height)), uint2(width, height) - 1u);
    if (any(uv != saturate(uv)) && positions[edge].w > 0.f) return guide_albedo[edge].rgb;
  }
  const float4 mean = light_kept[3];
  return (mean.w > 0.f) ? mean.rgb : UNSEEN_ALBEDO;
}

// The game's ambient cube's mean over its six directions (the sun texture's texels 3-8; rt_sun.cs_5_0.hlsl).
float3 AmbientCubeMean() {
  float3 mean = 0.f;
  [unroll]
  for (uint side = 3u; side < 9u; ++side) mean += sun[uint2(side, 0)].rgb;
  return mean / 6.f;
}

// Share of the game's ambient at its scale (texel 4's w) that Steady Bounce Light takes as the shade.
static const float KEPT_CUBE_SHARE = 1.f;

// The light in shade on a surface, with the game's ambient away: learned from the frames before it, so of whatever the view was
// then (ATN: too dark until toggled), or Steady Bounce Light's: the game's cube at its scale, of no view.
float3 KeptShade() {
  if ((path_flags & PATH_KEEP_CUBE) != 0u && sun[uint2(3, 0)].w != 0.f) return AmbientCubeMean() * sun[uint2(4, 0)].w * KEPT_CUBE_SHARE;
  return light_kept[0].rgb;
}

// Path-Traced Ambient with the game's sun light (RT_PATH_SUN_LIGHT, its texel written): the light a path's hit on the
// screen sends back is SeenDiffuseLight's, and the light learned from the screen stays as learned, the sun's the game's
// (rt_cache_resolve.cs_6_5.hlsl).
bool SunLightGiven() {
  return (path_flags & (PATH_KEEP_LIGHT | PATH_SUN_LIGHT)) == (PATH_KEEP_LIGHT | PATH_SUN_LIGHT) && sun[uint2(SUN_LIGHT_TEXEL, 0)].w != 0.f;
}

// Last frame's scene colour at a render pixel as light: squared back where the upscaler was given its square root.
float3 PreviousLight(uint2 pixel) {
  const float3 color = max(previous_color[pixel].rgb, 0.f);
  return (previous_square_root != 0u) ? color * color : color;
}

// The local lights' light on a surface the screen never saw: one of them picked by what it would give (LightArriving: its
// colour, its shader's falloff, its angle; reservoir sampling with one number) and weighted back by its odds, if a ray
// reaches it.
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

// The sun's light a surface sends back (its base colour lit) once the sun's light is known (light_kept[1]: the game's, else
// learned): by the screen's traced `visibility` there (0 to 1), else (below 0) through a shadow ray from its `surface` (or
// from `behind` it through leaves, cloth and sails at their `transmittance`).
float3 SunLit(float3 surface, float3 behind, float3 normal, float3 albedo, float transmittance, float visibility) {
  const float4 toward_sun = sun[uint2(0, 0)];
  const float n_dot_l = dot(normal, toward_sun.xyz);
  const float sun_share = (n_dot_l > 0.f) ? n_dot_l : (-n_dot_l * transmittance);
  if (toward_sun.w == 0.f || sun_share <= 0.f || light_kept[1].w <= 0.f) return 0.f;
  if (visibility >= 0.f) return albedo * light_kept[1].rgb * (sun_share * visibility);
  if (Occluded((n_dot_l > 0.f) ? surface : behind, toward_sun.xyz, 1.0e4f, NO_GRASS_SMOKE)) return 0.f;
  return albedo * light_kept[1].rgb * sun_share;
}

// Torch Light in Path-Traced GI (a paths request's LIGHTS_IN_PATHS): not in the reflections' builds, which never ask for
// it (its light loops in their code cost them 0.02 ms at 4K with the row off).
#if defined(REFLECT) || defined(REFLECT_RESERVOIR)
#define TORCH_LIGHT_IN_PATHS false
#else
#define TORCH_LIGHT_IN_PATHS ((lights_in_bounce & LIGHTS_IN_PATHS) != 0u)
#endif

// The local lights' light on a surface: two of them picked (less noise from fires and torches than one) by `seed`, Random's
// lanes (the second pick's dimension + 1).
float3 LocalLights(float3 surface, float3 normal, uint4 seed) {
  return 0.5f * (LocalLight(surface, normal, Random(seed)) + LocalLight(surface, normal, Random(seed + uint4(0u, 0u, 0u, 1u))));
}

// The light a surface the screen never saw sends back (its base colour lit): the sun's (SunLit) and the local lights'
// (LocalLights by `light_seed`). Not theirs where a surface the screen shows sends none of theirs back (SeenDiffuseLight,
// with the game's sun light, without Torch Light in Path-Traced GI): a surface out of sight sent more than it did once
// seen, the torches' share (the paths' probe, 640 x 360 at ATN's rows: the unseen route x1.17 the seen route's light over
// the screen's surfaces, x1.77 where the lights gave 30% of it, x0.98 without them; ATN 2026-11-16: wall corners out of
// sight too bright until seen, the radiance cache keeping their light).
float3 UnseenLight(float3 surface, float3 behind, float3 normal, float3 albedo, float transmittance, uint4 light_seed) {
  float3 emitted = SunLit(surface, behind, normal, albedo, transmittance, -1.f);
  const bool seen_without_lights = SunLightGiven() && (path_flags & (PATH_LIGHT | PATH_LIGHT_FINAL)) == PATH_LIGHT
                                   && !TORCH_LIGHT_IN_PATHS;
  if ((path_flags & PATH_LOCAL_LIGHTS) != 0u && !seen_without_lights) emitted += albedo * LocalLights(surface, normal, light_seed);
  return emitted;
}

// The game's ambient cube by a direction (the sun texture's texels 3-8: +x, -x, +y, -y, +z, -z; rt_sun.cs_5_0.hlsl).
float3 AmbientCube(float3 direction) {
  const float3 squared = direction * direction;
  return squared.x * sun[uint2((direction.x < 0.f) ? 4u : 3u, 0)].rgb + squared.y * sun[uint2((direction.y < 0.f) ? 6u : 5u, 0)].rgb
         + squared.z * sun[uint2((direction.z < 0.f) ? 8u : 7u, 0)].rgb;
}

// The game's ambient on a surface facing `normal` with all its sky open: the cube as the game's sun light shaders take it
// at the normal (Attila's: its y four times over, normalized; texel 6's w 0, Warhammer III's: as it is) x their scale
// (texel 4's w: 1.44 with c11.x, ATN's debug32 desert came out darker and warmer without).
float3 OpenSky(float3 normal) {
  const float3 cube_normal = (sun[uint2(6, 0)].w != 0.f) ? normalize(normal * float3(1.f, 4.f, 1.f)) : normal;
  return AmbientCube(cube_normal) * sun[uint2(4, 0)].w;
}

// The sky's light along a path that escapes: the game's own ambient cube by the direction, scaled so its mean over
// directions is the learned shade light (`shade`, which stood for the whole sky before: bluer from above, the ground's
// colour from below now); the shade where the game gave none. With Path-Traced Ambient (the screen's shade the paths' own
// sky then) the cube by the scale learned before it (light_kept[2]).
float3 SkyLight(float3 direction, float3 shade) {
  if (sun[uint2(3, 0)].w == 0.f) return shade;
  if ((path_flags & PATH_KEEP_CUBE) != 0u) return AmbientCube(direction) * sun[uint2(4, 0)].w * KEPT_CUBE_SHARE;
  const float4 scale = light_kept[2];
  if ((path_flags & PATH_AMBIENT) != 0u && scale.w > 0.f) return scale.rgb * AmbientCube(direction);
  return shade * AmbientCube(direction) / max(AmbientCubeMean(), 1.00006895e-4f);
}

// Path-Traced Ambient: the camera rays' sky scaled so an open surface gets the game's own ambient at its normal, in the
// game's colour and brightness (ATN's debug29-30: the learned scale's sky came out darker and warmer): OpenSky over the
// sky's light a cosine-weighted ray brings there on average. 1 without. SkyLight is the cube's faces by the direction's
// squared components (every branch), whose cosine means over the hemisphere around `normal` are (1 + n_a^2) / 8 + n_a / 4
// for face a+ and (1 + n_a^2) / 8 - n_a / 4 for a- (16 fixed directions left open walls 1.6% dark under the harness's
// cube, colours shifted up to 1.7%: reference suite I4, test/tw_rt_open_sky).
float3 SkyCalibration(float3 normal) {
  if ((path_flags & PATH_AMBIENT) == 0u || sun[uint2(3, 0)].w == 0.f) return 1.f;
  const float3 shade = light_kept[0].rgb;
  const float3 base = (1.f + normal * normal) * 0.125f;
  const float3 side = normal * 0.25f;
  const float3 open = SkyLight(float3(1.f, 0.f, 0.f), shade) * (base.x + side.x) + SkyLight(float3(-1.f, 0.f, 0.f), shade) * (base.x - side.x)
                      + SkyLight(float3(0.f, 1.f, 0.f), shade) * (base.y + side.y) + SkyLight(float3(0.f, -1.f, 0.f), shade) * (base.y - side.y)
                      + SkyLight(float3(0.f, 0.f, 1.f), shade) * (base.z + side.z) + SkyLight(float3(0.f, 0.f, -1.f), shade) * (base.z - side.z);
  return OpenSky(normal) / max(open, 1.0001094e-4f);
}

// Path-Traced Ambient's sky arriving at a surface the screen shows (`world`, facing `normal`): the last paths' own there
// (own_sky at its bounce texel where last frame saw it), else the open sky as the game's ambient (or the learned shade
// without the game's cube).
float3 ScreenSky(float3 world, float3 normal) {
  uint2 previous_pixel;
  if ((path_flags & PATH_SKY_HISTORY) != 0u && SeenLastFrame(world, previous_pixel)) {
    const float4 kept = own_sky[((path_flags & PATH_SKY_PARITY) != 0u) ? 0u : 1u][min(previous_pixel / max(bounce_scale, 1u), uint2(bounce_width, bounce_height) - 1u)];
    if (kept.w > 0.f) return kept.rgb;
  }
  return (sun[uint2(3, 0)].w != 0.f) ? OpenSky(normal) : KeptShade();
}

// With Path-Traced Ambient and the game's sun light (SunLightGiven) the light a surface the screen shows at `pixel` sends
// back, of no view, as a surface's nobody saw: its base colour `albedo` lit by the sun (by the traced visibility at
// `pixel` where this frame's) and Path-Traced Ambient's own sky there (ScreenSky). The screen's light holds the game's
// sun and sky specular toward the camera (Attila's sun light shaders add them to the diffuse light), not toward a path's
// start: taken as the light a surface sends everywhere, the faces in shade came out 14 to 22% brighter seen from a view
// toward the sun than from beside it (harness HARNESS_PEDESTAL; ATN 2026-11-13). With Torch Light in Path-Traced GI
// (LIGHTS_IN_PATHS) the local lights' too, as an unseen surface's (LocalLights by `light_seed`).
float3 SeenDiffuseLight(uint2 pixel, float3 world, float3 normal, float3 albedo, uint4 light_seed) {
  const float visibility = ((path_flags & PATH_SUN_TRACED) != 0u) ? traced_visibility[pixel] : -1.f;
  const float3 surface = world + normal * SurfaceOffset(distance(world, Camera()));
  float3 light_leaving = SunLit(surface, world, normal, albedo, 0.f, visibility) + albedo * ScreenSky(world, normal);
  if (TORCH_LIGHT_IN_PATHS && (path_flags & PATH_LOCAL_LIGHTS) != 0u) light_leaving += albedo * LocalLights(surface, normal, light_seed);
  return light_leaving;
}

// The light leaving a surface a ray met (false: nobody knows it): the screen's now where it shows that face, else (without
// this frame's light) last frame's, else the radiance cache's at the face `hit_normal` turns to (SHaRC's instead in
// PathRadiance with SHaRC Radiance Cache), unless `cached` is false.
bool HitLight(float3 hit, float3 hit_normal, out float3 light_there, bool cached = true) {
  light_there = 0.f;
  uint2 pixel;
  if ((path_flags & PATH_LIGHT) != 0u && SeenNow(hit, hit_normal, pixel)) {
    if ((path_flags & PATH_LIGHT_FINAL) != 0u) {
      light_there = max(light[pixel], 0.f);
      return true;
    }
    const float3 screen_light = max(light[min(pixel / max(bounce_scale, 1u), uint2(bounce_width, bounce_height) - 1u)], 0.f);
    if ((path_flags & PATH_KEEP_LIGHT) == 0u) {
      light_there = screen_light;
      return true;
    }
    const float3 albedo = ((path_flags & PATH_GUIDES) != 0u) ? guide_albedo[pixel].rgb : HitAlbedo(hit, hit_normal);
    if (SunLightGiven()) {
      // The lights picked by the hit's own position (no frame): Validate's look at a reservoir's surface picks what its
      // path did.
      light_there = SeenDiffuseLight(pixel, hit, hit_normal, albedo, uint4(asuint(hit), DIM_SEEN_LIGHTS));
    } else {
      // Path-Traced Ambient took the game's ambient away from the screen's light: the surface's base colour x the kept
      // shade (as an unseen surface's) in its place (walls a path met brought back almost nothing: ATN's debug29, dark).
      light_there = screen_light + albedo * KeptShade();
    }
    return true;
  }
  // Last frame's colour is the DLAA input: in Attila the tone mapper's output, in other units than this frame's light. Only
  // for games that pass none.
  if ((path_flags & (PATH_LIGHT | PATH_LIGHT_FINAL)) == 0u && SeenLastFrame(hit, pixel)) {
    light_there = PreviousLight(pixel);
    return true;
  }
#if defined(SHARC)
  return false;
#else
  return cached && (path_flags & PATH_CACHE_VALID) != 0u && CacheLookup(hit, hit_normal, light_there);
#endif
}

#if defined(REFLECT_ONE_SAMPLE)
// A path's first surface the screen never saw, for its radiance cache add when the path ends (PathRadiance). The
// reflections of one sample a pixel keep it in group memory, not in registers across every bounce (Nsight GPU Trace at 4K:
// 29 -> 36% occupancy). A thread's slot by its pixel (`seed` is ReflectPixel's id; groups of 8 x 8, rt_path.cs_6_5.hlsl).
// The paths' build gained nothing so (gpu2_'s c3: still 128 registers there): theirs stays in registers.
struct UnseenSurface {
  float3 position;
  float3 normal;
  float3 throughput;
  float3 radiance_before;
};
groupshared UnseenSurface unseen_surfaces[64];
#endif

#if defined(PATH_PROBE)
// RENODX_TW_PATH_PROBE (tests only: the helper built with RENODX_TW_PATH_PROBE_BUILD, its paths' build rt_path_probe):
// ProbeTexel's modes (probe_mode's low byte; its rays a texel probe_mode >> 8). The light leaving each bounce texel's own
// surface, met by a ray from 5 cm before it, by the route a path's hit on a surface the screen shows takes (HitLight), or
// by the route of one nobody saw (UnseenLight, the path going on) and that route's parts; the texel's arriving light as
// the paths take it, and a reference path tracer: every surface by the unseen route, no screen, no cache.
static const uint PROBE_SEEN = 1u;
static const uint PROBE_UNSEEN = 2u;
static const uint PROBE_EMITTED = 3u;   // the unseen route's part: the surface's own sun and local lights
static const uint PROBE_SKY = 4u;       // the sky its path goes on to
static const uint PROBE_SURFACES = 5u;  // surfaces past it the screen or the cache knew
static const uint PROBE_DEEPER = 6u;    // surfaces past it nobody knew: their own light, the last one's base colour x shade
static const uint PROBE_UNSEEN_NO_CACHE = 7u;
static const uint PROBE_ESTIMATOR = 8u;
static const uint PROBE_ESTIMATOR_NO_CACHE = 9u;
static const uint PROBE_REFERENCE = 10u;
static const uint PROBE_SEEN_SUN = 11u;  // SeenDiffuseLight's parts at the texel's pixel: the sun's
static const uint PROBE_SEEN_SKY = 12u;  // its base colour x ScreenSky
static const uint PROBE_ALBEDO_SEEN = 13u;    // the guide's base colour at the texel's pixel
static const uint PROBE_ALBEDO_UNSEEN = 14u;  // HitAlbedo's at its surface
static const uint PROBE_UNSEEN_SUN = 15u;     // UnseenLight's parts at its surface: the sun's (its shadow ray)
static const uint PROBE_UNSEEN_LOCAL = 16u;   // the local lights'
// The local lights at the texel's surface x its base colour: LocalLights' two picks (the paths' estimator), and every light
// with a shadow ray of its own, summed (pbrt's sum over all lights: for point lights the exact direct light).
static const uint PROBE_LIGHTS_PICKED = 17u;
static const uint PROBE_LIGHTS_ALL = 18u;
static const uint PROBE_REFERENCE_BOUNCES = 6u;
static bool probe_running = false;
static bool probe_seen = false;  // the first surface's light came by the screen's route
static float3 probe_emitted = 0.f;
static float3 probe_sky = 0.f;
static float3 probe_surfaces = 0.f;
static float3 probe_deeper = 0.f;
uint ProbeKind() {
  return probe_running ? (probe_mode & 0xFFu) : 0u;
}
// A surface the path meets at `vertex` may take the screen's (or the cache's) light.
bool ProbeScreen(uint vertex) {
  const uint kind = ProbeKind();
  if (kind == PROBE_REFERENCE) return false;
  return vertex > 0u || kind == 0u || kind == PROBE_SEEN || kind >= PROBE_ESTIMATOR;
}
#define PROBE_SCREEN(vertex) ProbeScreen(vertex)
#define PROBE_CACHE() (ProbeKind() != PROBE_UNSEEN_NO_CACHE && ProbeKind() != PROBE_ESTIMATOR_NO_CACHE)
#define PROBE_ADDS() (!probe_running)
#define PATH_BOUNCES ((ProbeKind() == PROBE_REFERENCE) ? PROBE_REFERENCE_BOUNCES : path_bounces)
#else
#define PROBE_SCREEN(vertex) true
#define PROBE_CACHE() true
#define PROBE_ADDS() true
#define PATH_BOUNCES path_bounces
#endif

// The light arriving along a ray (`first_distance`: how far its first surface is, 1e30: none; `first_normal` its normal;
// `sky_calibration`: SkyCalibration's, for the sky it meets first; `path`: its path id, rt_scene.hlsli DIM_*; `may_relearn`:
// CACHE_RELEARN of its hits leave the cache out).
float3 PathRadiance(float3 origin, float3 direction, float range, float3 sky_calibration, uint2 seed, uint path, bool may_relearn,
                    out float first_distance, out float3 first_normal) {
  first_distance = 1e30f;
  first_normal = 0.f;
  float3 radiance = 0.f;
  float3 throughput = 1.f;
  const float4 shade = float4(KeptShade(), light_kept[1].w);  // w: the sun's light known
  float3 leaving_normal = 0.f;  // past the first vertex: the surface the ray left (its sky's SkyCalibration)
  bool unseen = false;
#if defined(REFLECT_ONE_SAMPLE)
  const uint unseen_slot = (seed.y & 7u) * 8u + (seed.x & 7u);
#else
  float3 unseen_position = 0.f;
  float3 unseen_normal = 0.f;
  float3 unseen_throughput = 1.f;
  float3 radiance_before = 0.f;
#endif
  [loop]
  for (uint vertex = 0u; vertex <= PATH_BOUNCES; ++vertex) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.f;
    ray.TMax = (vertex == 0u) ? range : bounce_range;
    TRACE_SURFACE(query, ray)
    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
#if defined(PATH_REORDER)
    // Shader execution reordering (DXR 1.2, rt_path_ser.lib_6_9.hlsl), as RTXPT's: only a path's first surface, before
    // its state grows; a miss runs in place (RTXPT's inline miss). One bit: the surface is off the screen (the
    // projection alone), so it takes the unseen route (its own sun and lights, the path going on) where an on-screen hit
    // mostly ends on the screen's light. Every thread keeps its own path: the same light as without it.
    if (vertex == 0u && hit) {
      const float4 clip = ClipOf(origin + direction * query.CommittedRayT(), view_projection);
      const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
      dx::MaybeReorderThread((clip.w <= 0.f || any(uv < 0.f) || any(uv >= 1.f)) ? 1u : 0u, 1u);
    }
#endif
    if (!hit) {
      // Past the first surface the sky (the game's ambient by direction); from the camera's pixel the game's own
      // ambient covers it, but with Path-Traced Ambient (the game's taken away: the paths' the occluded sky).
      // No sky for the pixel itself through a solid cut-out either (TRACE_SURFACE culls what is alpha-tested: a thatched
      // house's parts without a known micromap state were holes to it). Only here: asked of every ray that escapes, the
      // bounce and the reflections cost 7 to 8% more (4K harness). Past it calibrated at the surface the ray left, as the
      // first ray's at its start and the screen's open sky (OpenSky): uncalibrated, Kept Cube's sky left the off-screen hits'
      // light 2.2% dark on box faces (reference suite step 2, contact_sky).
      if (vertex > 0u) {
        const float3 sky_there = throughput * SkyLight(direction, shade.rgb) * SkyCalibration(leaving_normal);
        radiance += sky_there;
#if defined(PATH_PROBE)
        probe_sky += sky_there;
#endif
      } else if ((path_flags & PATH_AMBIENT) != 0u && !Occluded(origin, direction, min(range, GRASS_AMBIENT_RANGE), GRASS_STILL)
#if defined(OPACITY_MICROMAPS)
                 && !Occluded(origin, direction, range, CUT_OUTS)
#endif
      ) {
        radiance += SkyLight(direction, shade.rgb) * sky_calibration;
      }
      break;
    }
    const float3 position = origin + direction * query.CommittedRayT();
    if (vertex == 0u) first_distance = query.CommittedRayT();
    const uint record_index = query.CommittedInstanceID() + query.CommittedGeometryIndex();
    const float3 normal = HitNormal(record_index, query.CommittedPrimitiveIndex(), direction, query.CommittedObjectToWorld3x4());
    float3 light_there;
    // CACHE_RELEARN of the paths leave the radiance cache out and go on: a cell nobody sees took its first frame's paths and
    // kept them until it was dropped, as only a path that found no light there adds to it. The light from out of sight never
    // followed a change, the count of bounces among them (ATN: Bounces showed nothing). Not the reflections': a mirror's one
    // ray a frame, no history, no filter, showed each re-rolled hit as a warm dot jumping about a paused picture (ATN: water).
    const bool relearn = may_relearn && Random(uint4(seed, frame, DIM_PATH_RELEARN + path * 32u + vertex * 2u)) < CACHE_RELEARN;
    // No cached answer for a ray shorter than its cell (as SHaRC's): the cell may be the ray's own start's.
    if (PROBE_SCREEN(vertex) && HitLight(position, normal, light_there, PROBE_CACHE() && !relearn && query.CommittedRayT() >= CacheCellSize(position))) {
      radiance += throughput * light_there;
#if defined(PATH_PROBE)
      if (vertex == 0u) {
        probe_seen = true;
      } else {
        probe_surfaces += throughput * light_there;
      }
#endif
      break;
    }
    if (vertex == 0u) first_normal = normal;
#if defined(SHARC)
    // SHaRC Radiance Cache: what the screen does not know from SHaRC (by the surface's normal; not for a ray shorter than
    // its voxel's diagonal, SHaRC 1.8.3's rule; false: responsive lighting not skipped, it is off).
    const SharcHitData sharc_hit = {position, normal};
    if (query.CommittedRayT() > SharcVoxelSize(position) * 1.7320508f && SharcGetCachedRadiance(Sharc(), sharc_hit, light_there, false)) {
      radiance += throughput * light_there;
      break;
    }
#endif
    const float3 albedo = HitAlbedo(position, normal);
    const float offset = SurfaceOffset(distance(position, Camera()));
    const float3 surface = position + normal * offset;
    // Leaves, cloth and sails pass their share of light through (the sun's from behind; the path goes on through them
    // as often): green light under the trees.
    const uint record_flags = records[record_index].z >> 16;
    const float transmittance = ((record_flags & (RECORD_MASKED | RECORD_GRASS)) == RECORD_MASKED) ? LEAF_TRANSMITTANCE : (((record_flags & RECORD_THIN) != 0u) ? THIN_TRANSMITTANCE : 0.f);
    const float3 behind = position - normal * offset;
    const float3 emitted = UnseenLight(surface, behind, normal, albedo, transmittance,
                                       uint4(seed, frame, DIM_PATH_LIGHTS + path * 32u + vertex * 2u));
#if defined(PATH_PROBE)
    if (vertex == 0u) {
      probe_emitted += throughput * emitted;
    } else {
      probe_deeper += throughput * emitted;
    }
    if (vertex == PATH_BOUNCES) probe_deeper += throughput * albedo * shade.rgb;
#endif
    if (!unseen) {
      unseen = true;
#if defined(REFLECT_ONE_SAMPLE)
      const UnseenSurface first_unseen = {position, normal, throughput, radiance};
      unseen_surfaces[unseen_slot] = first_unseen;
#else
      unseen_position = position;
      unseen_normal = normal;
      unseen_throughput = throughput;
      radiance_before = radiance;
#endif
    }
    if (vertex == PATH_BOUNCES) {
      radiance += throughput * (emitted + albedo * shade.rgb);
      break;
    }
    radiance += throughput * emitted;
    throughput *= albedo;
    const bool through = transmittance > 0.f && Random(uint4(seed, frame, DIM_PATH_THROUGH + path * 32u + vertex * 2u)) < transmittance;
    origin = through ? behind : surface;
    leaving_normal = through ? -normal : normal;
    direction = CosineDirection(leaving_normal, Random(uint4(seed, frame, DIM_PATH_DIRECTION + path * 32u + vertex * 2u)),
                                Random(uint4(seed, frame, DIM_PATH_DIRECTION + path * 32u + vertex * 2u + 1u)));
  }
  // The first unseen surface's light feeds the cache once the sun's light is known (the game's, or learned).
#if defined(REFLECT_ONE_SAMPLE)
  if (unseen && shade.w > 0.f) {
    const UnseenSurface unseen_surface = unseen_surfaces[unseen_slot];
    if (all(unseen_surface.throughput > 1.0000807e-3f)) {
      CacheAdd(unseen_surface.position, unseen_surface.normal, (radiance - unseen_surface.radiance_before) / unseen_surface.throughput);
    }
  }
#else
  if (PROBE_ADDS() && unseen && shade.w > 0.f && all(unseen_throughput > 1.0000807e-3f)) {
    CacheAdd(unseen_position, unseen_normal, (radiance - radiance_before) / unseen_throughput);
  }
#endif
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

// What a camera ray meets right beside its start lies in that start's own shade, which the light it brings back does not
// know: the underside of a dead body saw the ground under it as lit as the open ground, the body's lower edges glowed in
// the shade and no contact shadow showed (ATN's debug36 list).
float ContactShade(float hit_distance) {
  return saturate(hit_distance / CONTACT_RANGE);
}

// Validation of last frame's reservoir from `origin`: its surface out of view now drops it; its light now (where the
// screen or the cache knows it) takes the stored light's place, and a change shortens the history (bright changes
// dimmed until they settle). The change (0: none, 1: all of the history goes). `contact`: its light in ContactShade's.
float Validate(inout Reservoir previous, float3 origin, bool contact) {
  const float3 to_hit = previous.hit - origin;
  const float hit_distance = length(to_hit);
  if (hit_distance <= 1e-3f || Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE)) {
    previous.count = 0.f;
    return 0.f;
  }
  // The cache's answer at the face of its stored normal, from a cell's size away at least (a sample the screen answered stored
  // none: no face to look up).
  float3 light_now;
  const bool cached = dot(previous.hit_normal, previous.hit_normal) > 0.f && hit_distance >= CacheCellSize(previous.hit);
  if (!HitLight(previous.hit, previous.hit_normal, light_now, cached)) return 0.f;
  if (contact) {
    light_now *= ContactShade(hit_distance);
  }
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
  // Its screen loads at once (LoadNeighbours).
  const float4 position = positions[pixel];
  const float3 guide = GuideNormal(pixel);
  const Neighbours neighbours = LoadNeighbours(int2(pixel));
  normal = 0.f;
  origin = position.xyz;
  if (position.w <= 0.f) return position;
  normal = PixelNormalOf(pixel, position, guide, neighbours);
  origin = StartPast(RayOrigin(position, normal, PlanarOf(position, normal, neighbours)), position);
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
  if (reuse) {
    surface_origin[id] = float4(origin, position.w);
    surface_normal[id] = float4(normal, 0.f);
  }
  const uint set = ((path_flags & PATH_REUSE_PARITY) != 0u) ? 1u : 0u;
  // With the game's sun light the sky its rays bring is kept for the light this surface sends back as the next frame's
  // paths meet it (ScreenSky).
  const bool sky_kept = (path_flags & (PATH_AMBIENT | PATH_SUN_LIGHT)) == (PATH_AMBIENT | PATH_SUN_LIGHT);
  const uint sky_set = ((path_flags & PATH_SKY_PARITY) != 0u) ? 1u : 0u;
  Reservoir reservoir = (Reservoir)0;
  if (position.w <= 0.f) {
    bounce[id] = 0.f;
    if (reuse) StoreReservoir(reuse_samples[set], reuse_data[set], id, reservoir, 0.f);
    if (sky_kept) own_sky[sky_set][id] = 0.f;
    return;
  }
#if defined(PATH_ONE_SAMPLE)
  // Its build for a request of one ray a texel (rt_path_one): the loop's sums and the reservoir are not kept across the
  // path (128 registers a thread at 4K, 29% occupancy).
  const uint samples = 1u;
#else
  const uint samples = max(path_samples, 1u);
#endif
  const float3 sky_calibration = SkyCalibration(normal);
  // Path-Traced Ambient's sky blocked as Ray-Traced AO's (within its reach, grass within GRASS_AMBIENT_RANGE): past it the
  // sky (ATN 2026-10-30: the Light Bounce's 50 m made it far darker than RT AO's 6 m); the light bounced within it stays.
  const float first_range = ((path_flags & PATH_AMBIENT) != 0u) ? min(bounce_range, sky_range) : bounce_range;
  // Path-Traced Ambient: the sky's samples too (ATN's debug29-30: without them its ambient was the ground's bounce alone,
  // darker and warmer over the reservoirs' 1-2 s).
  // Own Sky Rays: a reservoir shows one sample, whatever the rays, and a neighbour sees a sky sample (a point at the AO
  // range) at another angle, noise even where nothing blocks the sky. So the sky stays out: stratified rays as Ray-Traced
  // AO's, their plain mean (kept in the bounce for ReuseTexel to add).
  const bool sky_apart = (path_flags & PATH_SKY_APART) != 0u;
  const bool sky_samples = (path_flags & PATH_AMBIENT) != 0u && !sky_apart;
  const float turn = InterleavedGradientNoise(float2(id));
  const float band_jitter = Random(uint4(id, frame, DIM_BAND_JITTER));
  float3 sky = 0.f;
  float3 arriving = 0.f;
  float hit_sum = 0.f;
  [loop]
  for (uint path = 0u; path < samples; ++path) {
    const float3 direction = CosineDirection(normal,
                                             sky_apart ? ((float)path + band_jitter) / (float)samples
                                                       : Random(uint4(id, frame, DIM_FIRST_DIRECTION + path * 2u)),
                                             sky_apart ? frac(turn + (float)path * 0.618034f)
                                                       : Random(uint4(id, frame, DIM_FIRST_DIRECTION + path * 2u + 1u)));
    float first_distance;
    float3 first_normal;
    const float3 sample_light = PathRadiance(origin, direction, first_range, sky_calibration, id, path, true, first_distance, first_normal)
                                * ContactShade(first_distance);
    arriving += sample_light;
    hit_sum += min(first_distance, first_range);
    if (first_distance >= 1e29f) sky += sample_light;
    // Cosine-weighted rays: the target over their density is pi times the light's luminance.
    Reservoir candidate;
    candidate.hit = origin + direction * min(first_distance, first_range);
    candidate.hit_normal = first_normal;
    candidate.light = sample_light;
    candidate.count = 1.f;
    candidate.weight = 0.f;
    Merge(reservoir, candidate, (first_distance < 1e29f || sky_samples) ? 3.14159265f * Luminance(sample_light) : 0.f,
          Random(uint4(id, frame, DIM_FIRST_MERGE + path * 2u)));
  }
  if (sky_kept) own_sky[sky_set][id] = float4(sky / samples, 1.f);
  if (!reuse) {
    bounce[id] = float4(arriving / samples, distance(position.xyz, Camera()));
    if ((path_flags & PATH_NRD) != 0u) nrd_hit_distance[id] = hit_sum / samples;
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
    if (previous.count > 0.f && previous.weight > 0.f && frame % REUSE_VALIDATION_PERIOD == 0u) invalidity = Validate(previous, origin, true);
    previous.count = min(previous.count, exp2(log2(REUSE_HISTORY) * (1.f - invalidity)) * samples);
    Merge(reservoir, previous, ReuseTarget(origin, normal, previous) * previous.weight * previous.count,
          Random(uint4(id, frame, DIM_HISTORY_MERGE)));
  }
  Finish(reservoir, ReuseTarget(origin, normal, reservoir));
  // REUSE_W_CLAMP in this receiver's units (W x cos / pi): a cosine ray's own W is pi / cos, so W <= 10 alone cut every
  // sample past 72 degrees and darkened the shade beside boxes and walls (the reference harness: ATN's boxes -3.0 %).
  reservoir.weight = min(reservoir.weight, REUSE_W_CLAMP * 3.14159265f / max(dot(normal, normalize(reservoir.hit - origin)), 1e-3f));
  StoreReservoir(reuse_samples[set], reuse_data[set], id, reservoir, normal);
  if (sky_apart) bounce[id] = float4(sky / samples, 0.f);
}

#if defined(PATH_PROBE)
// RENODX_TW_PATH_PROBE (its build alone; after PathTraceTexel, whose paths keep the cache and own_sky as without it): the
// mode's light at each bounce texel into the bounce (rgb; w the share of rays that met the texel's own surface, 5 cm
// away, and with PROBE_SEEN took the screen's route there: 1 where the mode means what it says). No cache adds.
void ProbeTexel(uint2 id) {
  if (id.x >= bounce_width || id.y >= bounce_height) return;
  float3 normal;
  float3 origin;
  const float4 position = TexelSurface(id, normal, origin);
  if (position.w <= 0.f) {
    bounce[id] = 0.f;
    return;
  }
  const uint kind = probe_mode & 0xFFu;
  const uint rays = max(probe_mode >> 8, 1u);
  const uint2 pixel = min(id * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u);
  const float3 base = ((path_flags & PATH_GUIDES) != 0u) ? guide_albedo[pixel].rgb : UNSEEN_ALBEDO;
  if (kind >= PROBE_SEEN_SUN) {
    const float visibility = ((path_flags & PATH_SUN_TRACED) != 0u) ? traced_visibility[pixel] : -1.f;
    const float3 surface = position.xyz + normal * SurfaceOffset(distance(position.xyz, Camera()));
    float3 value = base;
    if (kind == PROBE_SEEN_SUN) value = SunLit(surface, position.xyz, normal, base, 0.f, visibility);
    if (kind == PROBE_SEEN_SKY) value = base * ScreenSky(position.xyz, normal);
    if (kind == PROBE_ALBEDO_UNSEEN) value = HitAlbedo(position.xyz, normal);
    if (kind == PROBE_UNSEEN_SUN) value = SunLit(surface, position.xyz, normal, base, 0.f, -1.f);
    if (kind == PROBE_UNSEEN_LOCAL) {
      value = 0.f;
      [loop]
      for (uint ray = 0u; ray < rays && (path_flags & PATH_LOCAL_LIGHTS) != 0u; ++ray) {
        value += UnseenLight(surface, position.xyz, normal, base, 0.f, uint4(id, frame, DIM_PROBE_LIGHTS + ray * 2u)) - SunLit(surface, position.xyz, normal, base, 0.f, -1.f);
      }
      value /= rays;
    }
    if (kind == PROBE_LIGHTS_PICKED) {
      value = 0.f;
      [loop]
      for (uint ray = 0u; ray < rays; ++ray) value += base * LocalLights(surface, normal, uint4(id, frame, DIM_PROBE_LIGHTS + ray * 2u));
      value /= rays;
    }
    if (kind == PROBE_LIGHTS_ALL) {
      value = 0.f;
      [loop]
      for (uint index = 0u; index < min(light_count, 32u); ++index) {
        float3 direction;
        float light_distance;
        const float3 arriving = LightArriving(index, surface, normal, direction, light_distance);
        if (Luminance(arriving) <= 0.f || Occluded(surface, direction, light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance), NO_GRASS_SMOKE)) continue;
        value += base * arriving;
      }
    }
    bounce[id] = float4(value, 1.f);
    return;
  }
  probe_running = true;
  const bool camera_rays = kind >= PROBE_ESTIMATOR;
  const float3 sky_calibration = SkyCalibration(normal);
  const float first_range = ((path_flags & PATH_AMBIENT) != 0u) ? min(bounce_range, sky_range) : bounce_range;
  float3 sum = 0.f;
  float valid = 0.f;
  [loop]
  for (uint ray = 0u; ray < rays; ++ray) {
    probe_seen = false;
    probe_emitted = 0.f;
    probe_sky = 0.f;
    probe_surfaces = 0.f;
    probe_deeper = 0.f;
    float first_distance;
    float3 first_normal;
    if (camera_rays) {
      const float3 direction = CosineDirection(normal, Random(uint4(id, frame, DIM_PROBE_DIRECTION + ray * 2u)), Random(uint4(id, frame, DIM_PROBE_DIRECTION + ray * 2u + 1u)));
      sum += PathRadiance(origin, direction, first_range, sky_calibration, id, 33u + ray, true, first_distance, first_normal) * ContactShade(first_distance);
      valid += 1.f;
      continue;
    }
    const float3 light_leaving = PathRadiance(position.xyz + normal * 0.05f, -normal, 0.1f, 1.f, id, 33u + ray, true, first_distance, first_normal);
    const bool met = first_distance > 0.03f && first_distance < 0.07f;
    if (!met || (kind == PROBE_SEEN && !probe_seen) || (kind != PROBE_SEEN && probe_seen)) continue;
    valid += 1.f;
    float3 value = light_leaving;
    if (kind == PROBE_EMITTED) value = probe_emitted;
    if (kind == PROBE_SKY) value = probe_sky;
    if (kind == PROBE_SURFACES) value = probe_surfaces;
    if (kind == PROBE_DEEPER) value = probe_deeper;
    sum += value;
  }
  probe_running = false;
  bounce[id] = float4(sum / max(valid, 1.f), valid / rays);
}
#endif

// ReSTIR GI's spatial reuse: a texel's reservoir merged with neighbours' on the same surface (their samples' density
// moved to this texel: the Jacobian of their surfaces' solid angles); the chosen surface's light, if in view, is the
// bounce.
void ReuseTexel(uint2 id) {
  if (id.x >= bounce_width || id.y >= bounce_height) return;
  const float4 position = positions[min(id * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u)];
  if (position.w <= 0.f) {
    bounce[id] = 0.f;
#if defined(TORCHES_IN_BOUNCE)
    if ((lights_in_bounce & LIGHTS_MODE) == LIGHTS_RATIO_MODE) light_ratio[((lights_in_bounce & LIGHTS_RATIO_SET) != 0u) ? 1u : 0u][id] = 0.f;
#endif
    return;
  }
  const float3 normal = surface_normal[id].xyz;
  const float3 origin = surface_origin[id].xyz;
  const uint set = ((path_flags & PATH_REUSE_PARITY) != 0u) ? 1u : 0u;
  const Reservoir own = LoadReservoir(reuse_samples[set], reuse_data[set], id);
  Reservoir combined = (Reservoir)0;
  Merge(combined, own, ReuseTarget(origin, normal, own) * own.weight * own.count, Random(uint4(id, frame, DIM_REUSE_OWN)));
  [loop]
  for (uint neighbour = 0u; neighbour < REUSE_NEIGHBOURS; ++neighbour) {
    const float angle = 6.2831853f * Random(uint4(id, frame, DIM_REUSE_NEIGHBOUR + neighbour * 2u));
    const float radius = REUSE_RADIUS * sqrt(Random(uint4(id, frame, DIM_REUSE_NEIGHBOUR + neighbour * 2u + 1u)));
    const int2 texel = int2(id) + int2(round(float2(cos(angle), sin(angle)) * radius));
    if (any(texel < 0) || texel.x >= (int)bounce_width || texel.y >= (int)bounce_height || all(texel == int2(id))) continue;
#if defined(REUSE_LEAN)
    // Lean Reuse (only its rt_reuse_lean build): the neighbour's surface from its position and the normal its reservoir was
    // made for (one fetch besides the reservoir's, not the normal's and planarity's five), its rays' origin that position.
    const float4 other_position = positions[min(uint2(texel) * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u)];
    if (other_position.w <= 0.f || abs(other_position.w - position.w) > 0.1f * position.w
        || dot(LoadReceiverNormal(reuse_data[set], uint2(texel)), normal) < 0.9f) {
      continue;
    }
    const float3 other_origin = other_position.xyz;
#else
    const float4 other_surface = surface_origin[uint2(texel)];
    if (other_surface.w <= 0.f || abs(other_surface.w - position.w) > 0.1f * position.w || dot(surface_normal[uint2(texel)].xyz, normal) < 0.9f) continue;
    const float3 other_origin = other_surface.xyz;
#endif
    const Reservoir other = LoadReservoir(reuse_samples[set], reuse_data[set], uint2(texel));
    if (other.count <= 0.f) continue;
    const float jacobian = ReuseJacobian(other, other_origin, origin);
    if (!(jacobian > 0.1f && jacobian < 10.f)) continue;
    Merge(combined, other, ReuseTarget(origin, normal, other) * other.weight * other.count * jacobian,
          Random(uint4(id, frame, DIM_REUSE_MERGE + neighbour)));
  }
  Finish(combined, ReuseTarget(origin, normal, combined));
  // The chosen surface in view of this one (no light through walls); the integrand over its density.
  const float3 to_hit = combined.hit - origin;
  const float hit_distance = length(to_hit);
#if defined(TORCHES_IN_BOUNCE)
  // Torch and Fire Light in the Light Bounce (helper111; 2: the local lights' light alone, a measurement) and Torch and
  // Fire Shadows from the Light Bounce (3), only in this build (rt_reuse_lights: every other is helper110's to the byte):
  // one of the first BOUNCE_LIGHTS local lights a texel each frame, picked by the light it would give unshadowed, apart
  // from the reservoir (in it the sun's bounce outweighed the torches: their samples seldom chosen); its shadow ray to a
  // point of its disk under Ray Reconstruction (light_size: the helper sets it then, as rt_lights' rays), stopping short
  // of its lamp.
  const uint lights_mode = lights_in_bounce & LIGHTS_MODE;
  float3 arriving = ((path_flags & PATH_SKY_APART) != 0u && lights_mode != 2u) ? bounce[id].rgb : 0.f;
  if (lights_mode != 2u && combined.weight > 0.f && hit_distance > 1e-3f && !Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE)) {
    arriving += combined.light * (max(dot(normal, to_hit / hit_distance), 0.f) / 3.14159265f) * combined.weight;
  }
  float total = 0.f;  // the lights' unshadowed light here (luminance)
  bool lit = false;
  if (lights_mode != 0u && (path_flags & PATH_LOCAL_LIGHTS) != 0u) {
    const uint light_total = min(light_count, BOUNCE_LIGHTS);
    float3 direction;
    float light_distance;
    [loop]
    for (uint index = 0u; index < light_total; ++index) total += Luminance(LightArriving(index, origin, normal, direction, light_distance));
    if (total > 0.f) {
      const float next = Random(uint4(id, frame, DIM_REUSE_LIGHT)) * total;
      float running = 0.f;
      uint picked = 0u;
      float3 picked_light = 0.f;
      [loop]
      for (uint index = 0u; index < light_total && running <= next; ++index) {
        const float3 here = LightArriving(index, origin, normal, direction, light_distance);
        if (Luminance(here) <= 0.f) continue;
        running += Luminance(here);
        picked = index;
        picked_light = here;
      }
      float3 to_lamp = lights[picked].xyz - origin;
      if (light_size > 0.f) {
        to_lamp += DiskPoint(normalize(to_lamp), light_size, float2(Random(uint4(id, frame, DIM_REUSE_LIGHT + 1u)), Random(uint4(id, frame, DIM_REUSE_LIGHT + 2u))));
      }
      const float lamp_distance = length(to_lamp);
      lit = lamp_distance > 1e-3f && !Occluded(origin, to_lamp / lamp_distance, lamp_distance - min(LIGHT_CLEARANCE, 0.5f * lamp_distance), NO_GRASS_SMOKE);
      // Its light over its odds (its luminance over the sum).
      if (lit && lights_mode != LIGHTS_RATIO_MODE) arriving += picked_light * (total / Luminance(picked_light));
    }
  }
  if (lights_mode == LIGHTS_RATIO_MODE) {
    // The shadow ratio's sums (the lights' light here, shadowed by the picked one's ray, and unshadowed) kept over frames at
    // the surface last frame's texel showed; while the camera moves only a few (each frame's read of the nearest texel
    // smears the history: at Half a moving view trailed by 2.3 times the still one's error, the scorer's torch feature).
    const uint written = ((lights_in_bounce & LIGHTS_RATIO_SET) != 0u) ? 1u : 0u;
    float4 kept = 0.f;
    uint2 previous_pixel;
    if ((lights_in_bounce & LIGHTS_RATIO_VALID) != 0u && SeenLastFrame(position.xyz, previous_pixel)) {
      kept = light_ratio[1u - written][min(previous_pixel / bounce_scale, uint2(bounce_width, bounce_height) - 1u)];
    }
    const float most_frames = ((lights_in_bounce & LIGHTS_RATIO_FOLLOW) != 0u) ? RATIO_FRAMES_MOVING / (float)min(bounce_scale, 2u) : RATIO_FRAMES;
    const float frames = min(kept.z + 1.f, most_frames);
    light_ratio[written][id] = float4(lerp(kept.xy, float2(lit ? total : 0.f, total), 1.f / frames), frames, position.w);
  }
#else
  float3 arriving = ((path_flags & PATH_SKY_APART) != 0u) ? bounce[id].rgb : 0.f;
  if (combined.weight > 0.f && hit_distance > 1e-3f && !Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE)) {
    arriving += combined.light * (max(dot(normal, to_hit / hit_distance), 0.f) / 3.14159265f) * combined.weight;
  }
#endif
  bounce[id] = float4(arriving, distance(position.xyz, Camera()));
  if ((path_flags & PATH_NRD) != 0u) nrd_hit_distance[id] = (combined.weight > 0.f) ? hit_distance : bounce_range;
}

// Fog in Reflections (PATH_REFLECT_FOG): the light a reflection's ray brings from `hit` to the pixel's `origin` through the
// game's fog on that leg (rt_leg_fog.hlsli; the fog from the pixel to the camera is the game's own, drawn over the pixel
// after). The hits' light is the scene's before its fog: far hills in a river came out darker than the hills themselves.
float3 ThroughFog(float3 light_there, float3 origin, float3 hit) {
  if ((path_flags & PATH_REFLECT_FOG) == 0u) return light_there;
  const float4 texels[5] = {sun[uint2(LEG_FOG_TEXEL, 0)], sun[uint2(LEG_FOG_TEXEL + 1u, 0)], sun[uint2(LEG_FOG_TEXEL + 2u, 0)],
                            sun[uint2(LEG_FOG_TEXEL + 3u, 0)], sun[uint2(LEG_FOG_TEXEL + 4u, 0)]};
  const LegFog fog = LegFogOf(texels);
  if (fog.set == 0.f) return light_there;
  const float3 left = LegFogLeft(fog, origin, hit, distance(origin, Camera()));
  return light_there * left + fog.source * (1.f - left);
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

// A reflection's lobe by the G-buffer's roughness (1 - gloss): GGX as wide as the game's own lights' highlight on it (a
// Gaussian whose share within 0.5 degrees is gloss^2).
float ReflectAlpha(float roughness) {
  if ((path_flags & PATH_SQUARED_LOBE) != 0u) return max(roughness * roughness, 0.0064f);
  const float gloss = 1.f - roughness;
  return 0.0064f / max(gloss * gloss, 0.0625f);
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
  if (position.w <= 0.f || ReflectAlpha(roughness) < REFLECT_REUSE_ALPHA || roughness > ROUGH_LIMIT) {
    StoreReservoir(reflect_samples[set], reflect_data[set], id, reservoir, 0.f);
    return;
  }
  const float3 normal = PixelNormal(pixel, position);
  const float3 view = normalize(Camera() - position.xyz);
  const float n_dot_v = max(dot(normal, view), 1e-3f);
  const float3 origin = RayOrigin(position, normal, Planar(int2(pixel), position, normal));
  const float alpha = ReflectAlpha(roughness);
  const float3x3 basis = Basis(normal);
  const float3 direction = GgxDirection(basis, view, float3(dot(view, basis[0]), dot(view, basis[1]), n_dot_v), alpha,
                                        float2(Random(uint4(pixel, frame, DIM_TEXEL_DIRECTION)), Random(uint4(pixel, frame, DIM_TEXEL_DIRECTION + 1u))));
  if (dot(normal, direction) > 0.f) {
    float first_distance;
    float3 first_normal;
    const float3 sample_light = PathRadiance(origin, direction, 1.0e4f, 1.f, pixel, 32u, false, first_distance, first_normal);
    if (first_distance < 1e29f) {
      Reservoir candidate;
      candidate.hit = origin + direction * first_distance;
      candidate.hit_normal = first_normal;
      candidate.light = sample_light;
      candidate.count = 1.f;
      candidate.weight = 0.f;
      Merge(reservoir, candidate, Luminance(sample_light), Random(uint4(pixel, frame, DIM_TEXEL_MERGE)));
    }
  }
  uint2 previous_pixel;
  if ((path_flags & PATH_REFLECT_OWN_HISTORY) != 0u && SeenLastFrame(position.xyz, previous_pixel)) {
    const uint2 previous_texel = min(previous_pixel / 2u, (uint2(width, height) + 1u) / 2u - 1u);
    Reservoir previous = LoadReservoir(reflect_samples[1u - set], reflect_data[1u - set], previous_texel);
    if (dot(LoadReceiverNormal(reflect_data[1u - set], previous_texel), normal) < 0.8f) previous.count = 0.f;
    float invalidity = 0.f;
    if (previous.count > 0.f && previous.weight > 0.f && frame % REUSE_VALIDATION_PERIOD == 0u) invalidity = Validate(previous, origin, false);
    previous.count = min(previous.count, exp2(log2(REUSE_HISTORY) * (1.f - invalidity)));
    if (LobeDensity(origin, normal, view, alpha, previous.hit) > 0.f) {
      Merge(reservoir, previous, previous.weight * previous.count, Random(uint4(pixel, frame, DIM_TEXEL_MERGE + 1u)));
    }
  }
  reservoir.weight = (reservoir.count > 0.f) ? reservoir.weight / reservoir.count : 0.f;
  StoreReservoir(reflect_samples[set], reflect_data[set], id, reservoir, normal);
}

// Reflections and specular occlusion of a render pixel (accumulated by world position).
void ReflectPixel(uint2 id) {
  if (id.x >= width || id.y >= height) return;
  // Its screen loads at once (LoadNeighbours).
  const float4 position = positions[id];
  const float3 guide = GuideNormal(id);
  const float raw_roughness = ((path_flags & PATH_GUIDES) != 0u) ? guide_roughness[id] : 0.f;
  const Neighbours neighbours = LoadNeighbours(int2(id));
  if (position.w <= 0.f || (path_flags & PATH_GUIDES) == 0u) {
    specular_accumulated[id] = 0.f;
    specular_meta[id] = float2(1.f, 0.f);
    if ((path_flags & PATH_RAW) != 0u) specular_hit_distance[id] = SKY_HIT_DISTANCE;
    if ((path_flags & PATH_NRD) != 0u) nrd_reflection[id] = float4(0.f, 0.f, 0.f, SKY_HIT_DISTANCE);
    return;
  }
  const float3 normal = PixelNormalOf(id, position, guide, neighbours);
  const float3 surface_normal = SurfaceNormalOf(int2(id), position, neighbours);
  const float3 view = normalize(Camera() - position.xyz);
  const float n_dot_v = max(dot(normal, view), 1e-3f);
  const float3 origin = RayOrigin(position, normal, PlanarOf(position, normal, neighbours));
  const float roughness = saturate(raw_roughness);
  const float alpha = ReflectAlpha(roughness);
  const bool mirror = roughness < 0.05f;
  const bool traced = (path_flags & PATH_REFLECTIONS) != 0u && roughness <= ROUGH_LIMIT;
  const bool occlusion = (path_flags & PATH_SPECULAR_OCCLUSION) != 0u && !traced;
#if defined(REFLECT_ONE_SAMPLE)
  // Its build for a request of one sample a pixel (rt_reflect_one): the loop's sums and the lobe's basis are not kept across
  // the sample's path, and the pass fits the registers (the general build spilled them to local memory at 4K).
  const uint samples = 1u;
#else
  const uint samples = mirror ? 1u : max(path_samples, 1u);
#endif
  const float3x3 basis = Basis(normal);
  const float3 view_local = float3(dot(view, basis[0]), dot(view, basis[1]), n_dot_v);
  float3 gathered = 0.f;
  float weights = 0.f;
  float distances = 0.f;  // of the samples that met a surface
  uint met = 0u;
  // The shares (met, open) are of the samples' G2 / G1 weights, the lobe's own (0 below the horizon: the lobe has none
  // there). Of the sample count they were the lobe's share above the horizon: an open plane's specular occlusion 0.965 to
  // 0.990 at the default lobe's rough end, 0.50 at Squared Lobe's roughness 1 head-on, and under a roof the game's sky in
  // the rest (reference suite I5, test/tw_rt_open_sky).
  float lobe = 0.f;
  float open = 0.f;
  [loop]
  for (uint sample = 0u; sample < samples; ++sample) {
    float3 direction = mirror ? reflect(-view, normal)
                              : GgxDirection(basis, view, view_local, alpha,
                                             float2(Random(uint4(id, frame, DIM_GGX + sample * 2u)),
                                                    Random(uint4(id, frame, DIM_GGX + sample * 2u + 1u))));
    float n_dot_l = dot(normal, direction);
    // A draw below the horizon drawn again, twice at most: the draws kept above it have the lobe's distribution there, so
    // the weighted shares stay exact (at the default lobe at most 4e-5 of the samples still below).
    [loop]
    for (uint redraw = 1u; redraw <= 2u && n_dot_l <= 0.f; ++redraw) {
      direction = GgxDirection(basis, view, view_local, alpha,
                               float2(Random(uint4(id, frame, DIM_GGX + redraw * 32u + sample * 2u)),
                                      Random(uint4(id, frame, DIM_GGX + redraw * 32u + sample * 2u + 1u))));
      n_dot_l = dot(normal, direction);
    }
    if (n_dot_l <= 0.f) continue;
    const float weight = mirror ? 1.f : GgxSampleWeight(n_dot_v, n_dot_l, alpha);
    lobe += weight;
    // A normal map can turn a ray into its own surface: on waves, the more the view grazes them (the sides of the screen),
    // the riverbed under the water showed in them (ATN's debug36). Such a ray bounces off the surface, as off the next wave.
    direction -= 2.f * min(dot(direction, surface_normal), 0.f) * surface_normal;
    if (traced) {
      float first_distance;
      float3 first_normal;
      const float3 sample_light = PathRadiance(origin, direction, 1.0e4f, 1.f, id, 16u + sample, false, first_distance, first_normal);
      if (first_distance < 1e29f) {
        gathered += ThroughFog(sample_light, origin, origin + direction * first_distance) * weight;
        weights += weight;
        distances += first_distance;
        ++met;
      }
    } else if (occlusion && !Occluded(origin, direction, reflection_range, NO_GRASS_SMOKE)) {
      open += weight;
    }
  }
  float met_weights = weights;  // before the reuses below set `weights` to 1
  // Reflection Reuse from ReSTIR GI (PATH_REFLECT_REUSE, rougher lobes): the Light Bounce's reservoir at this pixel's
  // texel as a second candidate, resampled with the rays' own light (generalized RIS, its weights 1/2: the rays' light
  // by its target over their density, the reservoir's by the lobe's density at it times its W); one ray to the chosen
  // reservoir sample.
  if ((path_flags & PATH_REFLECT_REUSE) != 0u && traced && !mirror && alpha >= REFLECT_REUSE_ALPHA) {
    const uint set = ((path_flags & PATH_REUSE_PARITY) != 0u) ? 1u : 0u;
    const Reservoir bounce = LoadReservoir(reuse_samples[set], reuse_data[set], min(id / bounce_scale, uint2(bounce_width, bounce_height) - 1u));
    const float3 to_hit = bounce.hit - origin;
    const float hit_distance = length(to_hit);
    const float3 own = (weights > 0.f) ? gathered / weights : 0.f;
    const float3 bounce_light = ThroughFog(bounce.light, origin, bounce.hit);
    const float bounce_weight = (bounce.weight > 0.f && hit_distance > 1e-3f)
                                    ? Luminance(bounce_light) * LobeDensity(origin, normal, view, alpha, bounce.hit) * bounce.weight
                                    : 0.f;
    const float own_weight = Luminance(own);
    const float total = own_weight + bounce_weight;
    if (total > 0.f) {
      const bool bounce_chosen = Random(uint4(id, frame, DIM_REFLECT_CHOICE)) * total < bounce_weight
                                 && !Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE);
      const float3 chosen = bounce_chosen ? bounce_light : own;
      gathered = chosen / max(Luminance(chosen), 1e-6f) * (0.5f * total);
      weights = 1.f;
      if (bounce_chosen) {
        distances += hit_distance;
        ++met;
        met_weights += lobe / samples;
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
  if ((path_flags & PATH_REFLECT_OWN) != 0u && traced && !mirror && alpha >= REFLECT_REUSE_ALPHA) {
    const uint set = ((path_flags & PATH_REFLECT_PARITY) != 0u) ? 1u : 0u;
    // A neighbour's reservoir is loaded again to be merged, not kept from its tests: five of them with their receivers did
    // not fit the registers (Nsight GPU Trace at 4K: spilled to local memory).
    uint2 texels[REFLECT_NEIGHBOURS + 1u];
    float counts[REFLECT_NEIGHBOURS + 1u];  // 0: not a candidate
    float3 receivers[REFLECT_NEIGHBOURS + 1u];
    float3 receiver_normals[REFLECT_NEIGHBOURS + 1u];
    float3 receiver_views[REFLECT_NEIGHBOURS + 1u];
    float receiver_alphas[REFLECT_NEIGHBOURS + 1u];
    float total_count = 0.f;
    [unroll]
    for (uint neighbour = 0u; neighbour <= REFLECT_NEIGHBOURS; ++neighbour) {
      texels[neighbour] = 0u;
      counts[neighbour] = 0.f;
      receivers[neighbour] = origin;
      receiver_normals[neighbour] = normal;
      receiver_views[neighbour] = view;
      receiver_alphas[neighbour] = alpha;
      int2 texel = int2(id / 2u);
      if (neighbour != 0u) {
        const float angle = 6.2831853f * Random(uint4(id, frame, DIM_REFLECT_NEIGHBOUR + neighbour * 2u));
        const float radius = max(REFLECT_RADIUS * roughness, 1.f) * sqrt(Random(uint4(id, frame, DIM_REFLECT_NEIGHBOUR + neighbour * 2u + 1u)));
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
      texels[neighbour] = uint2(texel);
      counts[neighbour] = other.count;
      receivers[neighbour] = other_position.xyz;
      receiver_normals[neighbour] = other_normal;
      receiver_views[neighbour] = normalize(Camera() - other_position.xyz);
      receiver_alphas[neighbour] = ReflectAlpha(other_roughness);
      total_count += counts[neighbour];
    }
    Reservoir combined = (Reservoir)0;
    float weight_sum = 0.f;
    float counted = 0.f;
    [unroll]
    for (uint index = 0u; index <= REFLECT_NEIGHBOURS; ++index) {
      if (counts[index] <= 0.f) continue;
      const Reservoir other = LoadReservoir(reflect_samples[set], reflect_data[set], texels[index]);
      if (other.weight <= 0.f) continue;
      const float jacobian = ReuseJacobian(other, receivers[index], origin);
      if (!(jacobian > 0.1f && jacobian < 10.f)) continue;
      float densities = 0.f;
      [unroll]
      for (uint source = 0u; source <= REFLECT_NEIGHBOURS; ++source) {
        densities += counts[source]
                     * LobeDensity(receivers[source], receiver_normals[source], receiver_views[source], receiver_alphas[source], other.hit);
      }
      if (densities <= 0.f) continue;
      const float ratio = LobeDensity(origin, normal, view, alpha, other.hit) * jacobian * total_count / densities;
      Merge(combined, other, other.weight * other.count * min(ratio, REUSE_W_CLAMP), Random(uint4(id, frame, DIM_REFLECT_MERGE + index)));
      weight_sum += other.weight * other.count;
      counted += other.count;
    }
    const float3 to_hit = combined.hit - origin;
    const float hit_distance = length(to_hit);
    if (combined.weight > 0.f && hit_distance > 1e-3f && !Occluded(origin, to_hit / hit_distance, hit_distance * 0.99f, NO_GRASS_SMOKE)) {
      const float3 reused = ThroughFog(combined.light / max(Luminance(combined.light), 1e-6f) * (weight_sum / counted), origin, combined.hit);
      const float own_count = (weights > 0.f) ? (float)met : 0.f;
      gathered = (((weights > 0.f) ? gathered / weights : 0.f) * own_count + reused * counted) / (own_count + counted);
      weights = 1.f;
    }
  }
  float4 current = float4((weights > 0.f) ? gathered / weights : 0.f, (lobe > 0.f) ? min(met_weights / lobe, 1.f) : 0.f);
  // The share of the lobe open to the game's environment: where the rays were traced what they met stands in for it
  // (the reflection pass dims the rest by this too: 1), without Specular Occlusion 1.
  float current_open = occlusion ? ((lobe > 0.f) ? open / lobe : 0.f) : 1.f;
  // Ray Reconstruction's specular hit distance: the mean distance to what the samples met (the sky's where none did, as
  // where none were traced: the game's reflection is the sky's or a cube map's).
  if ((path_flags & PATH_RAW) != 0u) specular_hit_distance[id] = (met != 0u) ? distances / met : SKY_HIT_DISTANCE;
  if ((path_flags & PATH_NRD) != 0u) nrd_reflection[id] = float4(current.rgb * current.a, (met != 0u) ? distances / met : SKY_HIT_DISTANCE);
  // Mirrors keep no history; rougher lobes up to MAX_SPECULAR_FRAMES (last frame's where it saw this surface).
  float4 previous = 0.f;
  float2 previous_meta = float2(1.f, 0.f);
  uint2 previous_pixel;
  if ((path_flags & PATH_SPECULAR_HISTORY) != 0u && !mirror && SeenLastFrame(position.xyz, previous_pixel)) {
    previous = specular_history[previous_pixel];
    previous_meta = specular_meta_history[previous_pixel];
  }
  // Every draw below the horizon (Squared Lobe's widest lobes, few samples): nothing known this frame, last frame's shares
  // stay (else none met and none open: an open plane blocked that frame).
  if (lobe <= 0.f && !mirror && previous_meta.y > 0.f) {
    current.a = previous.a;
    if (occlusion) current_open = previous_meta.x;
  }
  const float frames = min(previous_meta.y + 1.f, mirror ? 1.f : lerp(2.f, MAX_SPECULAR_FRAMES, saturate(sqrt(alpha) * 3.f)));
  // rgb: the light of what met something (the share-weighted mean over the frames, as fast as the share itself); a: the
  // share that met.
  const float share = lerp(previous.a, current.a, 1.f / frames);
  specular_accumulated[id] = float4(lerp(previous.rgb, current.rgb, saturate(current.a / (frames * max(share, 1e-4f)))), share);
  specular_meta[id] = float2(lerp(previous_meta.x, current_open, 1.f / frames), frames);
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_PATH_HLSLI_
