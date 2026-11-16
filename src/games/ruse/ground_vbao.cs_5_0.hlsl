/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// R.U.S.E.'s ground ambient occlusion without rays (ray_tracing.hpp, the terrain shaders' s14): GT-VBAO (Attila's
// visibility bitmask, twattila/bitmask.hlsli) over the ground's depth (the pre-pass's; with the object shadows last
// frame's objects over it) into the shared occlusion texture. No normals here: each pixel's from its nearer neighbours.
// With GROUND_LIGHT_BOUNCE (ground_bounce.cs_5_0): GT-VBGI from the same march, each sample's light (last frame's scene
// where that point was, in the shaders' own gamma 2.2 units) x the share of the sectors it is the first to cover, into
// the terrain shaders' s14 texel with the occlusion they multiply their ambient by (x; yzw the light).
#include "../twattila/bitmask.hlsli"

Texture2D<float> t_depth : register(t0);
#ifdef GROUND_LIGHT_BOUNCE
Texture2D<float4> t_light : register(t1);     // last frame's scene as light (the bridge's colour: linear)
Texture2D<float> t_occlusion : register(t2);  // the rays' ambient occlusion (options.x 2)
RWTexture2D<float4> u_ground_light : register(u0);
#else
RWTexture2D<float> u_occlusion : register(u0);
#endif

cbuffer vbao_settings : register(b0) {
  float4 projection;  // P00, P11, A, B: the depth is A + B / view z
  float4 size;        // width, height, 1 / width, 1 / height
  float4 range;       // radius (world units), thickness (world units), noise frame, the bounced light's strength
  row_major float4x4 to_previous;  // this frame's clip to last frame's (the bounce's light)
  float4 options;     // x: the occlusion beside the light: 0 none (1), 1 this march's (GT-VBAO), 2 the rays' (t2);
                      // y: 1 Exact Depth Samples
};

static const uint SLICES = 4u;
static const uint MOST_STEPS = 12u;

// The view-space position of `pixel`'s depth (x right, y up, z forward); z 0 where nothing was drawn (the far plane).
float3 ViewPosition(int2 pixel) {
  const float depth = t_depth.Load(int3(clamp(pixel, int2(0, 0), int2(size.xy) - 1), 0));
  if (depth >= 1.f) return 0.f;
  const float z = projection.w / min(depth - projection.z, -1e-9f);
  const float2 ndc = float2((pixel.x + 0.5f) * size.z * 2.f - 1.f, 1.f - (pixel.y + 0.5f) * size.w * 2.f);
  return float3(ndc * z / projection.xy, z);
}

#ifdef GROUND_LIGHT_BOUNCE
// Last frame's light where `pixel`'s point was then (0 off screen or at the sky).
float3 PreviousLight(int2 pixel) {
  const float depth = t_depth.Load(int3(pixel, 0));
  if (depth >= 1.f) return 0.f;
  const float2 ndc = float2((pixel.x + 0.5f) * size.z * 2.f - 1.f, 1.f - (pixel.y + 0.5f) * size.w * 2.f);
  const float4 previous = mul(to_previous, float4(ndc, depth, 1.f));
  if (previous.w <= 0.f) return 0.f;
  const float2 uv = previous.xy / previous.w * float2(0.5f, -0.5f) + 0.5f;
  if (any(uv < 0.f) || any(uv >= 1.f)) return 0.f;
  return pow(max(t_light.Load(int3(uv * size.xy, 0)).rgb, 0.f), 1.f / 2.2f);
}
#endif

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= (uint2)size.xy)) return;
  const int2 pixel = int2(id.xy);
  const float3 position = ViewPosition(pixel);
  const float radius = range.x;
  // The radius on screen in pixels; below one, nothing to sample.
  const float2 radius_pixels = 0.5f * radius * projection.xy / max(position.z, 1e-6f) * size.xy;
  float value = 1.f;
  float3 light = 0.f;
  [branch]
  if (position.z > 0.f && min(radius_pixels.x, radius_pixels.y) >= 1.f) {
    // The normal from the nearer neighbour on each axis (no step across an edge).
    const float3 right = ViewPosition(pixel + int2(1, 0));
    const float3 left = ViewPosition(pixel - int2(1, 0));
    const float3 down = ViewPosition(pixel + int2(0, 1));
    const float3 up = ViewPosition(pixel - int2(0, 1));
    const float3 tangent_x = (right.z > 0.f && (left.z <= 0.f || abs(right.z - position.z) < abs(position.z - left.z))) ? (right - position) : (position - left);
    const float3 tangent_y = (down.z > 0.f && (up.z <= 0.f || abs(down.z - position.z) < abs(position.z - up.z))) ? (down - position) : (position - up);
    const float3 view = normalize(-position);
    float3 normal = normalize(cross(tangent_x, tangent_y));
    if (dot(normal, view) < 0.f) normal = -normal;

    const float3 noises = BitmaskNoise(id.xy, (uint)range.z);
    const uint steps = min(MOST_STEPS, max(4u, (uint)min(radius_pixels.x, radius_pixels.y)));
    float visibility = 0.f;
    [loop]
    for (uint slice = 0; slice < SLICES; ++slice) {
      const BitmaskSlice slice_info = BitmaskSliceAt((slice + noises.x) / SLICES, normal, view, position);
      const float slice_jitter = frac(noises.z + 0.618034f * slice);
      const float2 step_pixels = slice_info.screen_direction * radius_pixels;
      uint occluded = 0u;
      [loop]
      for (uint step = 0; step < steps && occluded != 0xFFFFFFFFu; ++step) {
        float distance = (step + frac(noises.y + 0.618034f * (step + slice * steps))) / steps;
        // Denser near the pixel; at least a pixel away (the centre is no occluder).
        distance = max(distance * distance, 1.f / min(radius_pixels.x, radius_pixels.y));
        const float2 offset = step_pixels * distance;
        [unroll]
        for (int side = -1; side <= 1; side += 2) {
          const float2 sample_pixel = pixel + 0.5f + side * offset;
          if (any(sample_pixel < 0.f) || any(sample_pixel >= size.xy)) continue;
          float3 sample_position = ViewPosition(int2(sample_pixel));
          // Exact Depth Samples (options.y 1): at the sample's own place, its depth between the four texels around it
          // where they lie on one plane (twattila/bitmask.hlsli BitmaskPlaneDepth); else its texel's, as before.
          [branch]
          if (options.y != 0.f) {
            const float2 place = sample_pixel - 0.5f;
            const int2 base = int2(floor(place));
            const int2 last = int2(size.xy) - 1;
            const float4 depths = float4(t_depth.Load(int3(clamp(base, 0, last), 0)), t_depth.Load(int3(clamp(base + int2(1, 0), 0, last), 0)),
                                         t_depth.Load(int3(clamp(base + int2(0, 1), 0, last), 0)), t_depth.Load(int3(clamp(base + 1, 0, last), 0)));
            float depth;
            if (BitmaskPlaneDepth(depths, place - base, depth) && depth < 1.f) {
              const float z = projection.w / min(depth - projection.z, -1e-9f);
              const float2 ndc = float2(sample_pixel.x * size.z * 2.f - 1.f, 1.f - sample_pixel.y * size.w * 2.f);
              sample_position = float3(ndc * z / projection.xy, z);
            }
          }
          if (sample_position.z <= 0.f) continue;
          const float3 delta = sample_position - position;
          const float delta_length = length(delta);
          if (!(delta_length > 1e-4f) || delta_length > radius) continue;
          // Its back a constant thickness along its own camera ray.
          const float3 back = normalize(delta + normalize(sample_position) * range.y);
          const uint covered = BitmaskSectors(delta / delta_length, back, view, side, slice_info, slice_jitter);
#ifdef GROUND_LIGHT_BOUNCE
          const uint first = covered & ~occluded;
          if (first != 0u) light += PreviousLight(int2(sample_pixel)) * (countbits(first) / (float)BITMASK_SECTORS);
#endif
          occluded |= covered;
        }
      }
      visibility += 1.f - countbits(occluded) / (float)BITMASK_SECTORS;
    }
    value = saturate(visibility / SLICES);
    light *= range.w / SLICES;
  }
#ifdef GROUND_LIGHT_BOUNCE
  const float occlusion = (options.x == 2.f) ? t_occlusion.Load(int3(pixel, 0)) : ((options.x == 1.f) ? value : 1.f);
  u_ground_light[pixel] = float4(occlusion, light);
#else
  u_occlusion[pixel] = value;
#endif
}
