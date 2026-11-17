/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// GT-VBAO (screen_space.hpp) in PPSAO_Reinterleave's place (2CA00ABD: the game's draw, its full-screen triangle and its
// RGB10A2 target, which PPSAO_Blur reads twice): what it writes, the visibility in red and LinearizeDepth's depth at the
// pixel in green, the visibility from the visibility bitmask (twattila/bitmask.hlsli, as Ryse's ssdo_bitmask.hlsli) on
// that depth (view z / the far plane) in PerView's camera space (x right, y up, z forward: the projection's rows 0 and 1
// undone at the depth, its jitter with them), the normal GBuffer1's (the G-buffer's view-space normal, PPSAO's t0).
#include "../twattila/bitmask.hlsli"

SamplerState ao_point : register(s0);
Texture2D<float> ao_linear_depth : register(t0);  // view z / the far plane (1: the far plane)
Texture2D<float2> ao_gbuffer1 : register(t5);     // the view-space normal, stereographic / 1.7777

cbuffer PerView : register(b12) {
  row_major float4x4 ao_view;
  row_major float4x4 ao_projection;
  row_major float4x4 ao_view_projection;
  float4 ao_view_position;
  float4 ao_view_info;  // x the far plane, y the near plane, zw the screen's size
  float4 ao_scale_and_bias;
};

cbuffer ao_settings : register(b1) {
  float ao_radius;     // metres
  float ao_strength;   // the visibility's exponent
  float ao_thickness;  // metres
  float ao_steps;      // samples per side of a slice, at most
  float ao_slices;
  float ao_auto_thickness;  // 1: each occluder as thick as it is wide
  float ao_frame;           // the noise's (0 without a temporal upscaler: still noise)
  float ao_exact_depth;     // 1: each sample's depth at its own place (Exact Depth Samples)
};

float3 AoPosition(float2 uv, float depth) {
  const float z = depth * ao_view_info.x;
  const float2 ndc = uv * float2(2.f, -2.f) + float2(-1.f, 1.f);
  const float x = (ndc.x * z - ao_projection[0].z * z - ao_projection[0].w) / ao_projection[0].x;
  const float y = (ndc.y * z - ao_projection[1].z * z - ao_projection[1].w - ao_projection[1].x * x) / ao_projection[1].y;
  return float3(x, y, z);
}

float2 AoUv(float3 position) {
  const float4 homogeneous = float4(position, 1.f);
  return float2(dot(ao_projection[0], homogeneous), dot(ao_projection[1], homogeneous)) / position.z * float2(0.5f, -0.5f) + 0.5f;
}

float AoVisibility(uint2 pixel) {
  uint2 size;
  ao_linear_depth.GetDimensions(size.x, size.y);
  const float2 texel = 1.f / float2(size);
  const float2 uv = (float2(pixel) + 0.5f) * texel;
  const float depth = ao_linear_depth.Load(int3(pixel, 0));
  const float far = ao_view_info.x;
  const float z = depth * far;
  // The radius in pixels at the pixel's depth.
  const float2 radius_texels = ao_radius / max(z, 1e-3f) * abs(float2(ao_projection[0].x, ao_projection[1].y)) * 0.5f * float2(size);
  const float radius_pixels = min(radius_texels.x, radius_texels.y);
  if (!(depth < 1.f) || radius_pixels < 1.f) return 1.f;
  const float3 encoded = float3(ao_gbuffer1.Load(int3(pixel, 0)) * 3.5554f - 1.7777f, 1.f);
  const float g = 2.f / dot(encoded, encoded);
  float3 normal = float3(g * encoded.xy, g - 1.f);
  if (!(dot(normal, normal) > 1e-6f)) return 1.f;
  normal = normalize(normal);
  const float3 position = AoPosition(uv, depth);
  const float3 view = normalize(-position);
  if (dot(normal, view) < 0.f) normal = -normal;
  // A constant thickness behind each depth sample, growing with distance.
  const float thickness = ao_thickness * (1.f + 0.02f * z);
  const float3 noises = BitmaskNoise(pixel, (uint)ao_frame);
  const uint slices = (uint)ao_slices;
  const uint steps = min((uint)ao_steps, max(4u, (uint)radius_pixels));
  float visibility = 0.f;
  [loop]
  for (uint slice = 0; slice < slices; ++slice) {
    const BitmaskSlice slice_info = BitmaskSliceAt((slice + noises.x) / slices, normal, view, position);
    const float slice_jitter = frac(noises.z + 0.618034f * slice);
    // The slice's direction on screen: a short step along it projected, scaled to the radius.
    const float2 step_uv = (AoUv(position + slice_info.direction * (0.01f * z)) - uv) * (ao_radius / (0.01f * z));
    BitmaskOccluder occluder_minus = {position, position};
    BitmaskOccluder occluder_plus = {position, position};
    const float occluder_scale = exp2(2.f * frac(noises.z + 0.5f + 0.754877666f * slice) - 1.f);
    uint occluded = 0u;
    // Until every sector is covered (nothing farther can change it).
    [loop]
    for (uint step = 0; step < steps && occluded != 0xFFFFFFFFu; ++step) {
      float distance = (step + frac(noises.y + 0.618034f * (step + slice * steps))) / steps;
      // Denser near the pixel; at least one texel away (the centre itself is no occluder).
      distance = max(distance * distance, 1.f / radius_pixels);
      [unroll]
      for (int side = -1; side <= 1; side += 2) {
        const float2 sample_uv = (floor((uv + side * step_uv * distance) * size) + 0.5f) * texel;
        // Off the screen: nothing known there (the edge's depth would make a false occluder).
        if (any(sample_uv != saturate(sample_uv))) continue;
        const float sample_depth = ao_linear_depth.SampleLevel(ao_point, sample_uv, 0);
        if (!(sample_depth < 1.f)) continue;
        float3 sample_position = AoPosition(sample_uv, sample_depth);
        // Exact Depth Samples: at the sample's own place (unsnapped), its depth between the four texels around it where they
        // lie on one plane (twattila/bitmask.hlsli BitmaskPlaneDepth); else its texel's, as before.
        [branch]
        if (ao_exact_depth != 0.f) {
          const float2 place_uv = uv + side * step_uv * distance;
          const float2 place = place_uv * size - 0.5f;
          const int2 base = int2(floor(place));
          const int2 last = int2(size) - 1;
          const float4 depths = float4(ao_linear_depth.Load(int3(clamp(base, 0, last), 0)), ao_linear_depth.Load(int3(clamp(base + int2(1, 0), 0, last), 0)),
                                       ao_linear_depth.Load(int3(clamp(base + int2(0, 1), 0, last), 0)), ao_linear_depth.Load(int3(clamp(base + 1, 0, last), 0)));
          float plane_depth;
          if (BitmaskPlaneDepth(depths, place - base, plane_depth) && plane_depth < 1.f) sample_position = AoPosition(place_uv, plane_depth);
        }
        float sample_thickness = thickness;
        BitmaskOccluder occluder = occluder_minus;
        if (side > 0) occluder = occluder_plus;
        [branch]
        if (ao_auto_thickness != 0.f) {
          sample_thickness = BitmaskOccluderThickness(occluder, sample_position, view, thickness, occluder_scale);
          if (side > 0) {
            occluder_plus = occluder;
          } else {
            occluder_minus = occluder;
          }
        }
        const float3 delta = sample_position - position;
        const float delta_length = length(delta);
        if (!(delta_length > 1e-4f) || delta_length > ao_radius) continue;
        // Its thickness along its own camera ray (Auto Thickness: its occluder's).
        float3 back;
        [branch]
        if (ao_auto_thickness != 0.f) {
          back = BitmaskOccluderBack(occluder, delta, position, view, sample_thickness);
        } else {
          back = normalize(delta + normalize(sample_position) * thickness);
        }
        occluded |= BitmaskSectors(delta / delta_length, back, view, side, slice_info, slice_jitter);
      }
    }
    visibility += 1.f - countbits(occluded) / (float)BITMASK_SECTORS;
  }
  return exp2(log2(max(visibility / slices, 1e-4f)) * ao_strength);
}

float2 main(float4 v0 : SV_Position0, float2 v1 : TexCoord0) : SV_Target0 {
  const uint2 pixel = uint2(v0.xy);
  return float2(AoVisibility(pixel), ao_linear_depth.SampleLevel(ao_point, v1, 0));
}
