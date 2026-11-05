/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// An upscaler's inputs from Warhammer III's TAA pass (taa_dlss.hpp): this frame's colour (jittered; nothing below 0: FSR's
// and XeSS's histories keep what they can't blend), its depth (the G-buffer depth's x) and its velocity (clip-space now -
// before) as motion in pixels to where each pixel was last frame (x right, y down: the TAA's own reprojection), at the
// render size (the colour's). NVIDIA's L, M and Ray Reconstruction flatten what lies some 6 decades under the frame's
// brightest: they get the colour's square root (dlss_output squares it).
Texture2D<float4> t_depth : register(t0);
Texture2D<float4> t_velocity : register(t1);
Texture2D<float4> t_color : register(t2);
RWTexture2D<float4> u_color : register(u0);
RWTexture2D<float> u_depth : register(u1);
RWTexture2D<float2> u_motion : register(u2);
cbuffer dlss_settings : register(b0) {
  float g_square_root;
}

#include "./particle_smear.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  t_color.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  const float3 color = max(t_color[id.xy].rgb, 0.f);
  u_color[id.xy] = float4((g_square_root != 0.f) ? sqrt(color) : color, 1.f);
  u_depth[id.xy] = t_depth[id.xy].x;
  u_motion[id.xy] = ParticleSmearMotion(id.xy, t_velocity[id.xy].xy * float2(-0.5f, 0.5f) * float2(width, height), float2(width, height));
}
