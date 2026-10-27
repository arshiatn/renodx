// The ray-traced passes' per-pixel surface (rt_shadows.hpp's D3D12 passes): world positions from the sun shadow mask's
// depth (0x7A1E0D12), and the camera and last frame's camera in the sun texture; their settings. World units: UE's
// centimetres.
#ifndef SRC_GAMES_TALESOFARISE_DLSS_RT_SURFACE_HLSLI_
#define SRC_GAMES_TALESOFARISE_DLSS_RT_SURFACE_HLSLI_

Texture2D<float4> positions : register(t1);  // world position, scene depth (0: sky)
// [0] toward the sun (w 1: valid), [1] the camera, [2-5] TranslatedWorldToClip, [6] PreViewTranslation, [7] view size.
Texture2D<float4> sun : register(t2);
Texture2D<float4> previous_color : register(t3);      // last frame's scene colour (the TAA's input)
Texture2D<float4> previous_positions : register(t4);  // last frame's positions
Texture2D<float4> previous_sun : register(t5);        // last frame's sun texture (its camera)

// The passes' settings (root constants).
cbuffer Settings : register(b0) {
  uint frame;
  uint flags;  // SUN_SHADOWS ... LIGHT_FIT_VALID
  float ambient_range;  // centimetres
  uint ambient_rays;
  float bounce_range;  // centimetres
  uint bounce_rays;
  float bounce_strength;
  float sun_cone;  // tan of the sun's angular radius for soft shadows (0: hard)
  uint path_bounces;  // path tracing: path vertices after the first hit
  uint path_samples;  // path tracing: paths per pixel
  float reflection_range;  // centimetres (specular occlusion's reach)
  uint settings_padding0;
  uint settings_padding1;
  uint settings_padding2;
  uint settings_padding3;
  uint settings_padding4;
};

static const uint SUN_SHADOWS = 1u;
static const uint AMBIENT_OCCLUSION = 2u;
static const uint BOUNCE_LIGHT = 4u;
static const uint HISTORY_VALID = 8u;  // last frame's positions and scene colour are last frame's
static const uint REFLECTIONS = 16u;
static const uint CACHE_VALID = 32u;  // the radiance cache has been filled
static const uint BOUNCE_HISTORY = 64u;  // last frame's bounce light accumulation is last frame's
static const uint CHARACTER_SHADOWS = 256u;  // characters cast sun shadows (not on themselves)
static const uint PATH_TRACING = 512u;  // the bounce light and reflections are path traced
static const uint SPECULAR_HISTORY = 1024u;  // last frame's reflection accumulation is last frame's
static const uint SPECULAR_OCCLUSION = 2048u;
static const uint LIGHT_FIT_VALID = 4096u;  // the learned lighting (rt_cache.hlsli) has been solved once

// The scene's instance masks: static meshes, characters (and their alpha-tested parts), alpha-tested static meshes.
static const uint STATIC_MESHES = 1u;
static const uint CHARACTERS = 2u;
static const uint FOLIAGE = 4u;
static const uint WORLD = STATIC_MESHES | FOLIAGE;
static const uint EVERYTHING = STATIC_MESHES | CHARACTERS | FOLIAGE;

// The surface's normal from the neighbour nearer in depth on each axis (none across depth edges), facing the camera.
float3 SurfaceNormal(int2 pixel, int2 size, float4 position) {
  const int2 last = size - 1;
  const float4 left = positions[clamp(pixel - int2(1, 0), 0, last)];
  const float4 right = positions[clamp(pixel + int2(1, 0), 0, last)];
  const float4 above = positions[clamp(pixel - int2(0, 1), 0, last)];
  const float4 below = positions[clamp(pixel + int2(0, 1), 0, last)];
  const float3 along_x = (abs(right.w - position.w) < abs(left.w - position.w)) ? right.xyz - position.xyz
                                                                                 : position.xyz - left.xyz;
  const float3 along_y = (abs(below.w - position.w) < abs(above.w - position.w)) ? below.xyz - position.xyz
                                                                                 : position.xyz - above.xyz;
  const float3 to_camera = sun[uint2(1, 0)].xyz - position.xyz;
  float3 normal = cross(along_y, along_x);
  const float normal_length = length(normal);
  normal = (normal_length > 0.f) ? normal / normal_length : normalize(to_camera);
  return (dot(normal, to_camera) < 0.f) ? -normal : normal;
}

// Last frame's pixel of a world position (false: outside its view).
bool PreviousPixel(float3 world, out int2 pixel) {
  pixel = 0;
  const float3 translated = world + previous_sun[uint2(6, 0)].xyz;
  const float4 clip = translated.x * previous_sun[uint2(2, 0)] + translated.y * previous_sun[uint2(3, 0)]
                      + translated.z * previous_sun[uint2(4, 0)] + previous_sun[uint2(5, 0)];
  if (clip.w <= 0.f) return false;
  const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
  if (any(uv < 0.f) || any(uv >= 1.f)) return false;
  pixel = int2(uv * previous_sun[uint2(7, 0)].xy);
  return true;
}

// Whether last frame saw this world position at `pixel` (not something in front of it, nor another surface).
bool SeenLastFrame(float3 world, int2 pixel) {
  const float4 seen = previous_positions[pixel];
  return seen.w > 0.f && distance(seen.xyz, world) < 3.f + 0.01f * seen.w;
}

// Off a surface by 1 cm plus 0.1% of the depth (depth precision and coarser far geometry grow with distance).
float SurfaceOffset(float depth) {
  return 1.f + depth * 0.001f;
}

#endif  // SRC_GAMES_TALESOFARISE_DLSS_RT_SURFACE_HLSLI_
