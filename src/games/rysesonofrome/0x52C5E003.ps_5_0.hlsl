/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Contact shadows (contact_shadows.hpp): a short march toward the sun through the scene's depth from each pixel; where
// something covers the ray (in front of it by less than the thickness) the pixel is in shadow. Max-blended into the
// sun's shadow mask slice before the tiled lighting (cs 0x897232B5) reads it (1 = shadow), in the sun's channels only.
// Its constants are the tiled lighting's own (b0) and the addon's (b1).
cbuffer PerBatch : register(b0) {
  float4 frustum_top_left;     // the far plane's corners from the camera (world)
  float4 frustum_bottom_left;
  float4 camera_position;
  float4 projection_parameters;
  float4 toward_sun;
  float4 ssdo_parameters;
  float4 frustum_top_right;
  float4 screen_size;          // width, height, 1 / width, 1 / height
};

cbuffer ContactShadows : register(b1) {
  float march_length;  // metres
  float thickness;     // metres
  float strength;      // 0-1
  float frame;         // the noise's frame
};

Texture2D<float> depth_texture : register(t0);  // linear depth / far
Buffer<uint> sun : register(t1);                // shadow mask slice, channels (0x52C5E001)

// World position (from the camera) at uv and depth / far, as the tiled lighting rebuilds it.
float3 FromCamera(float2 uv, float depth) {
  return (frustum_top_left.xyz + uv.x * (frustum_top_right.xyz - frustum_top_left.xyz)
          + uv.y * (frustum_bottom_left.xyz - frustum_top_left.xyz)) * depth;
}

float4 main(float4 position : SV_Position) : SV_Target {
  const uint channels = sun[1];
  // Only slice 0's target is bound: a sun elsewhere (or none) gets nothing.
  if (sun[0] != 0u || channels == 0u || strength <= 0.f) return 0.f;
  const uint2 pixel = uint2(position.xy);
  const float depth = depth_texture.Load(int3(pixel, 0));
  if (!(depth < 0.999f)) return 0.f;
  const float2 uv = (float2(pixel) + 0.5f) * screen_size.zw;
  const float3 across = frustum_top_right.xyz - frustum_top_left.xyz;
  const float3 down = frustum_bottom_left.xyz - frustum_top_left.xyz;
  // The camera's forward axis (the far plane's normal, away from the camera) and the far plane's distance.
  float3 forward = normalize(cross(down, across));
  if (dot(forward, frustum_top_left.xyz) < 0.f) {
    forward = -forward;
  }
  const float far_distance = dot(frustum_top_left.xyz, forward);
  const float3 start = FromCamera(uv, depth);
  const float3 light = normalize(toward_sun.xyz);
  // Off the surface a little (its depth precision) and jittered per pixel and frame (TAA / DLSS smooth it).
  const float noise = frac(52.9829189f * frac(dot(float2(pixel) + 5.588238f * (frame % 64.f), float2(0.06711056f, 0.00583715f))));
  const float distance_to_camera = length(start);
  const float bias = 0.02f + 0.002f * distance_to_camera;
  const uint STEPS = 16;
  float shadow = 0.f;
  [loop]
  for (uint step = 0; step < STEPS; ++step) {
    const float t = bias + march_length * (step + noise) / STEPS;
    const float3 point_from_camera = start + light * t;
    // Back onto the screen: its depth / far along the camera's forward axis, then its place on the far plane.
    const float point_depth = dot(point_from_camera, forward) / far_distance;
    if (point_depth <= 0.f) break;
    const float3 on_far = point_from_camera / point_depth - frustum_top_left.xyz;
    const float2 point_uv = float2(dot(on_far, across) / dot(across, across), dot(on_far, down) / dot(down, down));
    if (any(point_uv != saturate(point_uv))) break;
    const float scene_depth = depth_texture.Load(int3(point_uv * screen_size.xy, 0));
    const float in_front = (point_depth - scene_depth) * far_distance;
    if (in_front > 0.f && in_front < thickness) {
      // Fading out over the march's last quarter (no hard cut where it stops).
      shadow = saturate(4.f * (STEPS - step) / STEPS);
      break;
    }
  }
  const float value = shadow * strength;
  return float4((channels & 1u) ? value : 0.f, (channels & 2u) ? value : 0.f, (channels & 4u) ? value : 0.f,
                (channels & 8u) ? value : 0.f);
}
