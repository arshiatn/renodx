/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The upscaler's result into the TAA pass's output (taa_dlss.hpp; its alpha kept). Its Motion Debug View instead: the
// motion the upscaler got (t1) over the tone mapper's picture (t0) dimmed, into a texture the target takes after.
#include "../twattila/motion.hlsli"

Texture2D<float4> t_output : register(t0);
Texture2D<float2> t_motion : register(t1);
RWTexture2D<float4> u_target : register(u0);
cbuffer dlss_settings : register(b0) {
  float g_square_root;  // dlss_inputs'
  float g_motion_view;
  float2 g_motion_share;  // the motion's part of its texture (below 100%: its top-left)
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  u_target.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  if (g_motion_view != 0.f) {
    const float4 picture = t_output[id.xy];
    const float scene = 0.1f * dot(abs(picture.rgb), float3(0.2126f, 0.7152f, 0.0722f));
    u_target[id.xy] = float4(max(MotionColour(t_motion[int2((float2(id.xy) + 0.5f) * g_motion_share)]), scene), picture.a);
    return;
  }
  float3 color = t_output[id.xy].rgb;
  if (g_square_root != 0.f) color *= abs(color);
  u_target[id.xy] = float4(color, u_target[id.xy].a);
}
