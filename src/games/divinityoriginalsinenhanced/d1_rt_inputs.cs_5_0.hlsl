/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing's inputs (ray_tracing.hpp; twattila/rt_core.hpp, Trace without a depth): each pixel's world position from
// gBuffer1's linear depth (t0: view z / far, cleared to 1: the sky) and the main view's camera without the jitter (the
// picture was drawn jittered: its point sits `g_jitter` away in NDC), its distance from the camera (0: the sky), as
// d1_upscale_inputs places it; the sun in the sun texture from the game's PerFrame (b13, the light pass's own: column-major
// global_LightPropertyMatrix, DeferredDirectionalWithSSAO's asm cb13[0-2].x the light vector toward the sun, .z its
// diffuse light): texel 0 toward the sun, texel 1 the camera, texel 2 the sun's light (w 1), as rt_core's WriteSun.
Texture2D<float> t_linear_depth : register(t0);
RWTexture2D<float4> u_positions : register(u0);
RWTexture2D<float4> u_sun : register(u1);

cbuffer rt_inputs : register(b0) {
  row_major float4x4 g_inverse_view;  // this frame's view to world (column vectors)
  float4 g_size;                      // render width, height, 1 / width, 1 / height
  float4 g_projection;                // A (P22), B (P23), the far plane
  float4 g_jitter;                    // the jitter in NDC (x, y)
  float4 g_camera;                    // ViewPos
  float4 g_lens;                      // the projection without the jitter: P00, P11, P02, P12
};

cbuffer PerFrame : register(b1) {
  float4 global_LightPropertyMatrix[4];
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (all(id.xy == 0u)) {
    const float3 toward_sun = float3(global_LightPropertyMatrix[0].x, global_LightPropertyMatrix[1].x, global_LightPropertyMatrix[2].x);
    u_sun[uint2(0, 0)] = float4(normalize(toward_sun), 1.f);
    u_sun[uint2(1, 0)] = float4(g_camera.xyz, 1.f);
    u_sun[uint2(2, 0)] = float4(global_LightPropertyMatrix[0].z, global_LightPropertyMatrix[1].z, global_LightPropertyMatrix[2].z, 1.f);
  }
  if (any(float2(id.xy) >= g_size.xy)) return;
  const float linear_depth = t_linear_depth.Load(int3(id.xy, 0));
  if (!(linear_depth > 0.f && linear_depth < 0.99894756f)) {
    u_positions[id.xy] = 0.f;
    return;
  }
  const float view_z = linear_depth * g_projection.z;
  const float2 uv = (float2(id.xy) + 0.5f) * g_size.zw;
  const float2 ndc = uv * float2(2.f, -2.f) + float2(-1.f, 1.f) - g_jitter.xy;
  // In view space from the depth and the lens, then the inverse view (d1_upscale_inputs: no far-plane cancellation).
  const float3 view = float3((ndc - g_lens.zw) * view_z / g_lens.xy, view_z);
  u_positions[id.xy] = float4(mul(g_inverse_view, float4(view, 1.f)).xyz, length(view));
}
