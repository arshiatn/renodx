/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper upscaler's inputs (upscaling.hpp, at the tone mapper's draw) from the game's exposed scene (t0, the tone
// mapper's Base: linear BT.709, R11G11B10F, nothing below 0) and its linear depth (t1, gBuffer1: view z / far, cleared to
// 1): the colour (its square root for DLSS's L / M presets and Ray Reconstruction, which d1_upscale_output squares back),
// the depth as the game's projection writes it (A + B / z: 0 near, 1 far) and the camera's motion in pixels to where each
// pixel was last frame (x right, y down, as NVIDIA's): the pixel's world point from this frame's camera without the jitter
// (the picture was drawn jittered: its point sits `g_jitter` away in NDC), projected by last frame's. The point is placed
// in view space from the linear depth and the lens, then turned by the inverse view around the camera, and last frame's
// view-projection moved to this camera (upscaling.hpp, doubles): the inverse view-projection loses its precision at the
// far plane's cancellation (0.08 px at 128 px wide, test/tw_d1_rays), world coordinates of ~600 in last frame's
// projection 0.003 px. The game has no motion vectors: what moves on its own (characters) gets the camera's motion alone.
Texture2D<float4> t_scene : register(t0);
Texture2D<float> t_linear_depth : register(t1);
RWTexture2D<float4> u_color : register(u0);
RWTexture2D<float> u_depth : register(u1);
RWTexture2D<float2> u_motion : register(u2);

cbuffer upscale_inputs : register(b0) {
  row_major float4x4 g_inverse_view;              // this frame's view to world around its camera (no translation)
  row_major float4x4 g_previous_view_projection;  // last frame's without the jitter, moved to this frame's camera
  float4 g_size;                                  // render width, height, 1 / width, 1 / height
  float4 g_projection;                            // A (P22), B (P23), the far plane, the square root (0 / 1)
  float4 g_jitter;                                // the jitter in NDC (x, y), reset (0 / 1)
  float4 g_lens;                                  // the projection without the jitter: P00, P11, P02, P12
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(float2(id.xy) >= g_size.xy)) return;
  const float3 color = max(t_scene.Load(int3(id.xy, 0)).rgb, 0.f);
  u_color[id.xy] = float4((g_projection.w != 0.f) ? sqrt(color) : color, 1.f);

  const float view_z = max(t_linear_depth.Load(int3(id.xy, 0)), 9.998239e-7f) * g_projection.z;
  const float device_z = g_projection.x + g_projection.y / view_z;
  u_depth[id.xy] = saturate(device_z);

  const float2 uv = (float2(id.xy) + 0.5f) * g_size.zw;
  const float2 ndc = uv * float2(2.f, -2.f) + float2(-1.f, 1.f) - g_jitter.xy;
  const float3 view = float3((ndc - g_lens.zw) * view_z / g_lens.xy, view_z);
  const float4 before = mul(g_previous_view_projection, float4(mul(g_inverse_view, float4(view, 1.f)).xyz, 1.f));
  const float2 motion = (before.xy / before.w - ndc) * float2(0.5f, -0.5f) * g_size.xy;
  // Not finite (a bit test: fxc folds isfinite away) or after a reset: none.
  u_motion[id.xy] = (g_jitter.z == 0.f && all((asuint(motion) & 0x7f800000u) != 0x7f800000u)) ? motion : 0.f;
}
