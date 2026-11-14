/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's camera constants (b3): the paths' (rt_path.hlsli) and the trace's histories
// (rt_trace.cs_6_5.hlsl, rt_lights.cs_6_5.hlsl), with the projection of a world position by a camera's registers and
// where the last trace saw it (after rt_scene.hlsli: the size).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_CAMERA_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_CAMERA_HLSLI_

cbuffer Paths : register(b3) {
  float4 view_projection[4];           // this frame's camera registers: clip = (dot(position, [0]), ...)
  float4 previous_view_projection[4];  // last frame's
  uint bounce_width;
  uint bounce_height;
  uint bounce_scale;
  uint path_flags;  // PATH_* (rt_path.hlsli, and below)
  uint path_bounces;
  uint path_samples;
  float bounce_range;      // metres
  float reflection_range;  // metres (specular occlusion's reach)
  float sky_range;         // metres: what blocks Path-Traced Ambient's sky (the AO's reach, as Ray-Traced AO)
  uint previous_square_root;  // 1: last frame's colour holds the light's square root (rt_path.hlsli PreviousLight)
  uint previous_positions_slice;  // last frame's positions' slice of previous_positions (rt_path.hlsli)
#if defined(TORCHES_IN_BOUNCE)
  uint lights_in_bounce;          // Torch and Fire Light in the Light Bounce: LIGHTS_* below (rt_path.hlsli, rt_lights)
#endif
};
#if defined(TORCHES_IN_BOUNCE)
// lights_in_bounce (only in the TORCHES_IN_BOUNCE builds: every other is helper110's to the byte): its mode (bits 0-1: 1
// a local light's sample a bounce texel, its light in the bounce; 2 the bounce only theirs, a measurement; 3 Torch and
// Fire Shadows from the Light Bounce: their shadow ratio into light_ratio, the bounce without them), the light_ratio set
// written (a bounce request's) or read (a trace's), last frame's set valid.
static const uint LIGHTS_MODE = 3u;
static const uint LIGHTS_RATIO_MODE = 3u;
static const uint LIGHTS_RATIO_SET = 16u;
static const uint LIGHTS_RATIO_VALID = 32u;
static const float RATIO_FRAMES = 12.f;  // the shadow ratio's history (frames)
RWTexture2D<float4> light_ratio[2] : register(u56);  // at the bounce texels: shadowed, unshadowed torch light (luminance), frames, depth (0: none)
#endif
static const uint PATH_START_PAST = 2147483648u;  // RT_START_PAST: StartPast
static const uint PATH_AMBIENT_HISTORY = 16384u;  // path_flags in the trace: last trace's ambient and positions are valid
static const uint PATH_AMBIENT_PARITY = 32768u;   // its history read from set 1 (else 0)
static const uint PATH_LIGHT_HISTORY = 131072u;   // the last trace's positions and local lights' bits are valid
static const uint PATH_TINT = 1048576u;           // Coloured Shadows: the trace writes its filter (rt_trace's tint)
static const uint PATH_TINT_GUIDES = 2097152u;    // with the diffuse albedo guide's colours

RWTexture2D<float4> ambient_positions : register(u30);  // the last trace's positions (its histories')

float4 ClipOf(float3 world, float4 registers[4]) {
  const float4 position = float4(world, 1.f);
  return float4(dot(position, registers[0]), dot(position, registers[1]), dot(position, registers[2]), dot(position, registers[3]));
}

// Where the last trace saw `world` (its pixel), if it saw the same surface there.
bool SeenByLastTrace(float3 world, out uint2 seen_pixel) {
  seen_pixel = 0u;
  const float4 clip = ClipOf(world, previous_view_projection);
  const float2 uv = clip.xy / max(clip.w, 9.998928e-7f) * float2(0.5f, -0.5f) + 0.5f;
  if (clip.w <= 0.f || any(uv < 0.f) || any(uv >= 1.f)) return false;
  seen_pixel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1u);
  const float4 seen = ambient_positions[seen_pixel];
  return seen.w > 0.f && distance(seen.xyz, world) < 0.030005185f + 0.01f * seen.w;
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_CAMERA_HLSLI_

// How far along the way to the camera StartPast looks (metres): more than a trunk's difference, less than a wall's thickness matters.
static const float START_PAST_REACH = 1.5f;

// A pixel the camera sees has nothing before it on that way, but a placed mesh is one instance's shape at every place: another instance's trunk
// can stand thinner inside it (ATN's debug8: AO 0 on a trunk with a good normal, every ray hit; the same trunks without Placed Objects: none). A
// start inside sees only that shape's inside. With Rays Start Past Hidden Shapes it starts past it, the way the camera came.
float3 StartPast(float3 origin, float4 position) {
  if ((path_flags & PATH_START_PAST) == 0u) return origin;
  const float3 to_camera = normalize(sun[uint2(1, 0)].xyz - position.xyz);
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = to_camera;
  ray.TMin = 0.f;
  ray.TMax = min(START_PAST_REACH, 0.5f * position.w);
  // Solid shapes alone: no alpha test, and a leaf or blade that swayed before the pixel is nothing to start past.
  SCENE_QUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_NON_OPAQUE) query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, NO_GRASS_SMOKE & ~LEAVES, ray);
  query.Proceed();
  if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return origin;
  return origin + to_camera * (query.CommittedRayT() + SurfaceOffset(position.w));
}
