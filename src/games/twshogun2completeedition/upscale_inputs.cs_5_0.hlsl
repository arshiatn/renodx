/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper upscaler's inputs from the game's DLSS evaluation (upscaling.hpp), its top-left render size: the colour
// (linear before the tone mapper; nothing below 0: FSR's and XeSS's histories keep what they can't blend), the depth
// (D24's, 0 near) and the motion (the game's pixels x its NGX motion scale: to where each pixel was last frame).
// NVIDIA's L, M and Ray Reconstruction flatten what lies some 6 decades under the frame's brightest: they get the
// colour's square root (upscale_output squares it).
Texture2D<float4> t_color : register(t0);
Texture2D<float> t_depth : register(t1);
Texture2D<float2> t_motion : register(t2);
RWTexture2D<float4> u_color : register(u0);
RWTexture2D<float> u_depth : register(u1);
RWTexture2D<float2> u_motion : register(u2);
cbuffer upscale_settings : register(b0) {
  float g_square_root;    // c0.x
  float2 g_motion_scale;  // c0.yz
  uint2 g_render_size;    // c1.xy
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= g_render_size)) return;
  const float3 color = max(t_color[id.xy].rgb, 0.f);
  u_color[id.xy] = float4((g_square_root != 0.f) ? sqrt(color) : color, 1.f);
  u_depth[id.xy] = t_depth[id.xy];
  u_motion[id.xy] = t_motion[id.xy] * g_motion_scale;
}
