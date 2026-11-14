/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper upscaler's result into the game's DLSS output (upscaling.hpp; the tone mapper reads its colour alone).
// The Upscaling Debug View instead (1): the motion the upscaler got (t1, the render size top-left) stretched over the
// picture, as Attila's DLAA Debug View: the direction as the hue, brighter the faster; the scene dimmed under it (t2,
// the upscaler's colour input).
#include "../twattila/motion.hlsli"

Texture2D<float4> t_output : register(t0);
Texture2D<float2> t_motion : register(t1);
Texture2D<float4> t_input : register(t2);
RWTexture2D<float4> u_target : register(u0);
cbuffer upscale_settings : register(b0) {
  float g_square_root;    // upscale_inputs'
  float2 g_motion_scale;  // c0.yz
  uint2 g_render_size;    // c1.xy
  uint g_debug_view;      // c1.z
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  u_target.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  if (g_debug_view == 1u) {
    const int2 input = min(int2((float2(id.xy) + 0.5f) * float2(g_render_size) / float2(width, height)), int2(g_render_size) - 1);
    float3 scene = t_input.Load(int3(input, 0)).rgb;
    if (g_square_root != 0.f) scene *= abs(scene);
    const float3 dim = 0.1f * dot(abs(scene), float3(0.2126f, 0.7152f, 0.0722f));
    u_target[id.xy] = float4(max(MotionColour(t_motion.Load(int3(input, 0))), dim), 1.f);
    return;
  }
  float3 color = t_output[id.xy].rgb;
  if (g_square_root != 0.f) color *= abs(color);
  u_target[id.xy] = float4(color, 1.f);
}
