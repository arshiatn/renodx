/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing's inputs (ray_tracing.hpp; twattila/rt_core.hpp): each pixel's world position from Ryse's linear depth
// and the far plane's corners from the camera, its distance from the camera (0: the sky), as the tiled lighting
// (cs 0x897232B5) rebuilds them; the sun's direction (toward it) and the camera's position in the sun texture.
// Its constants are the tiled lighting's own (b0).
cbuffer PerBatch : register(b0) {
  float4 frustum_top_left;  // the far plane's corners from the camera (world)
  float4 frustum_bottom_left;
  float4 camera_position;
  float4 projection_parameters;
  float4 toward_sun;
  float4 ssdo_parameters;
  float4 frustum_top_right;
  float4 screen_size;  // width, height, 1 / width, 1 / height
};

Texture2D<float> depth_texture : register(t0);  // linear depth / far
RWTexture2D<float4> positions : register(u0);
RWTexture2D<float4> sun : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (all(id.xy == 0u)) {
    sun[uint2(0, 0)] = float4(normalize(toward_sun.xyz), 1.f);
    sun[uint2(1, 0)] = float4(camera_position.xyz, 1.f);
  }
  if (any(float2(id.xy) >= screen_size.xy)) return;
  const float depth = depth_texture.Load(int3(id.xy, 0));
  if (!(depth > 0.f && depth < 0.999f)) {
    positions[id.xy] = 0.f;
    return;
  }
  const float2 uv = (float2(id.xy) + 0.5f) * screen_size.zw;
  const float3 from_camera = (frustum_top_left.xyz + uv.x * (frustum_top_right.xyz - frustum_top_left.xyz)
                              + uv.y * (frustum_bottom_left.xyz - frustum_top_left.xyz)) * depth;
  positions[id.xy] = float4(camera_position.xyz + from_camera, length(from_camera));
}
