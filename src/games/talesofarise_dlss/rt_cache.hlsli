// The ray-traced passes' world radiance cache (rt_shadows.hpp): the light leaving surfaces where the camera saw them,
// kept by world cell (25 cm near the camera, doubling every 10 m further) in a hash table, so rays hitting what is off
// screen now still find the light there (the bounce light, the reflections, path tracing). Every frame the screen's
// pixels add last frame's scene colour to their cells (0x7A1E0D15), which then move a fifth of the way toward that
// frame's mean (0x7A1E0D16); path-traced hits the screen never saw add the light they computed. Cells unseen for 30 s
// are dropped. Each cell also keeps the base colour last seen there (the G-buffer's, 0x7A1E0D1D) for path tracing.
//
// The learned lighting: Bandai's lighting is hand-tuned (not a physical model), so path tracing does not shade what
// the screen never saw with a model of its own; it fits the game's: per frame, over the screen, scene colour / base
// colour against the sun's visibility x N.L (0x7A1E0D1D adds the sums, 0x7A1E0D16 solves the line once a frame and
// keeps it smoothed): its intercept is the light a surface gets in shade (sky, ambient, bounces), its slope what the
// sun adds. World units: UE's centimetres.
#ifndef SRC_GAMES_TALESOFARISE_DLSS_RT_CACHE_HLSLI_
#define SRC_GAMES_TALESOFARISE_DLSS_RT_CACHE_HLSLI_

#include "./rt_surface.hlsli"

RWBuffer<uint> cache_keys : register(u5);        // 0: empty
RWBuffer<float4> cache_radiance : register(u6);  // rgb, the frame it was last seen (0: never)
RWBuffer<uint> cache_sums : register(u7);        // this frame's r, g, b (CACHE_SCALE fixed point) and count per entry
RWBuffer<uint2> cache_albedo : register(u19);    // rgba8 the base colour last seen in the cell (a 255; 0: none), that frame + 1
// The learned lighting's sums this frame: count, x, x^2 (LIGHT_FIT_X fixed point), y rgb, x y rgb (LIGHT_FIT_Y).
RWBuffer<uint> light_fit : register(u20);
RWBuffer<float4> light_kept : register(u22);  // [0] rgb in shade, [1] rgb the sun adds (scene colour / base colour)

static const uint CACHE_ENTRIES = 1u << 20;
static const uint CACHE_PROBES = 8u;
static const float CACHE_CELL = 25.f;
static const float CACHE_LEVEL_DISTANCE = 1000.f;
static const uint CACHE_LEVELS = 8u;
static const float CACHE_SCALE = 256.f;
static const float CACHE_MAX = 1024.f;  // brighter light is clamped there (sums stay in 32 bits)
static const float CACHE_BLEND = 0.2f;
static const float CACHE_LIFETIME = 1800.f;
static const float LIGHT_FIT_X = 4096.f;
static const float LIGHT_FIT_Y = 64.f;
static const float LIGHT_FIT_MAX_Y = 16.f;
static const float LIGHT_FIT_BLEND = 0.1f;

uint CacheHash(uint4 value) {
  uint hash = value.x * 0x8DA6B343u ^ value.y * 0xD8163841u ^ value.z * 0xCB1AB31Fu ^ value.w * 0x165667B1u;
  hash ^= hash >> 15;
  hash *= 0x2C1B3C6Du;
  hash ^= hash >> 12;
  return hash;
}

// The cell of a world position seen from `camera`: its table slot (first probe) and key (never 0).
void CacheCell(float3 world, float3 camera, out uint slot, out uint key) {
  const float distance_to_camera = length(world - camera);
  const uint level = min((uint)floor(log2(max(distance_to_camera / CACHE_LEVEL_DISTANCE, 1.f))), CACHE_LEVELS - 1u);
  const int3 cell = int3(floor(world / (CACHE_CELL * exp2((float)level))));
  const uint4 value = uint4(asuint(cell), level);
  slot = CacheHash(value) & (CACHE_ENTRIES - 1u);
  key = CacheHash(value.yzwx + 0x9E3779B9u) | 1u;
}

// The entry of a world position's cell (false: none).
bool CacheFind(float3 world, float3 camera, out uint entry) {
  uint slot, key;
  CacheCell(world, camera, slot, key);
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

// The entry of a world position's cell, made when new (false: the table is full there).
bool CacheClaim(float3 world, float3 camera, out uint entry) {
  uint slot, key;
  CacheCell(world, camera, slot, key);
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

// The light the cache keeps at a world position (false: never seen).
bool CacheLookup(float3 world, float3 camera, out float3 radiance) {
  radiance = 0.f;
  uint entry;
  if (!CacheFind(world, camera, entry)) return false;
  const float4 kept = cache_radiance[entry];
  radiance = kept.rgb;
  return kept.w > 0.f;
}

// Adds light seen at a world position to its cell's sums (false: the table is full there).
bool CacheAdd(float3 world, float3 camera, float3 light) {
  uint entry;
  if (!CacheClaim(world, camera, entry)) return false;
  const uint3 fixed_point = (uint3)(clamp(light, 0.f, CACHE_MAX) * CACHE_SCALE + 0.5f);
  InterlockedAdd(cache_sums[entry * 4u], fixed_point.r);
  InterlockedAdd(cache_sums[entry * 4u + 1u], fixed_point.g);
  InterlockedAdd(cache_sums[entry * 4u + 2u], fixed_point.b);
  InterlockedAdd(cache_sums[entry * 4u + 3u], 1u);
  return true;
}

// The base colour last seen at a world position (false: none).
bool CacheAlbedo(float3 world, float3 camera, out float3 albedo) {
  albedo = 0.f;
  uint entry;
  if (!CacheFind(world, camera, entry)) return false;
  const uint packed = cache_albedo[entry].x;
  albedo = float3(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu) / 255.f;
  return (packed >> 24) != 0u;
}

void CacheSetAlbedo(float3 world, float3 camera, float3 albedo) {
  uint entry;
  if (!CacheClaim(world, camera, entry)) return;
  const uint3 bytes = (uint3)(saturate(albedo) * 255.f + 0.5f);
  cache_albedo[entry] = uint2(bytes.r | (bytes.g << 8) | (bytes.b << 16) | (255u << 24), frame + 1u);
}

// The light leaving a surface a ray hit: last frame's scene colour where last frame saw it, else the cache's (false:
// neither knows it).
bool HitLight(float3 hit, out float3 light) {
  light = 0.f;
  int2 pixel;
  if ((flags & HISTORY_VALID) != 0u && PreviousPixel(hit, pixel) && SeenLastFrame(hit, pixel)) {
    light = max(previous_color[pixel].rgb, 0.f);
    return true;
  }
  return (flags & CACHE_VALID) != 0u && CacheLookup(hit, sun[uint2(1, 0)].xyz, light);
}

#endif  // SRC_GAMES_TALESOFARISE_DLSS_RT_CACHE_HLSLI_
