/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper upscaler's inputs (graphics.hpp's TAA hook) from the preparation pass's textures (0x52594501), its top-left
// render size: the linear colour (nothing below 0: FSR's and XeSS's histories keep what they can't blend; its square root
// for Ray Reconstruction, which upscale_output squares back), the depth (the bridge's 0 near: the preparation's is
// reversed, 1 near) and the motion (pixels to where each pixel was last frame, as NVIDIA's).
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
  u_depth[id.xy] = 1.f - t_depth[id.xy];
  u_motion[id.xy] = t_motion[id.xy] * g_motion_scale;
}
