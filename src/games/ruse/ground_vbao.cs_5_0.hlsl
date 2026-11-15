/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// R.U.S.E.'s ground ambient occlusion without rays (ray_tracing.hpp, the terrain shaders' s14): GT-VBAO (Attila's
// visibility bitmask, twattila/bitmask.hlsli) over the ground's depth (the pre-pass's; with the object shadows last
// frame's objects over it) into the shared occlusion texture. No normals here: each pixel's from its nearer neighbours.
#include "../twattila/bitmask.hlsli"

Texture2D<float> t_depth : register(t0);
RWTexture2D<float> u_occlusion : register(u0);

cbuffer vbao_settings : register(b0) {
  float4 projection;  // P00, P11, A, B: the depth is A + B / view z
  float4 size;        // width, height, 1 / width, 1 / height
  float4 range;       // radius (world units), thickness (world units), noise frame, 0
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

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= (uint2)size.xy)) return;
  const int2 pixel = int2(id.xy);
  const float3 position = ViewPosition(pixel);
  const float radius = range.x;
  // The radius on screen in pixels; below one, nothing to sample.
  const float2 radius_pixels = 0.5f * radius * projection.xy / max(position.z, 1e-6f) * size.xy;
  float value = 1.f;
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
          const float3 sample_position = ViewPosition(int2(sample_pixel));
          if (sample_position.z <= 0.f) continue;
          const float3 delta = sample_position - position;
          const float delta_length = length(delta);
          if (!(delta_length > 1e-4f) || delta_length > radius) continue;
          // Its back a constant thickness along its own camera ray.
          const float3 back = normalize(delta + normalize(sample_position) * range.y);
          occluded |= BitmaskSectors(delta / delta_length, back, view, side, slice_info, slice_jitter);
        }
      }
      visibility += 1.f - countbits(occluded) / (float)BITMASK_SECTORS;
    }
    value = saturate(visibility / SLICES);
  }
  u_occlusion[pixel] = value;
}
