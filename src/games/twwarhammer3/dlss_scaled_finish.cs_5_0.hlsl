/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Below 100% (taa_dlss.hpp): the upscaler's result (t0, BT.2020: linear, or its square root) back to what the tone mapper
// writes (signed BT.709, gamma 2.2) over the whole display (u0), alpha from the matching pixel of its colour (t1). No result
// this frame (g_stretch): that colour stretched over the display, so the picture never stays in its top-left corner.
#include "../../shaders/color/gamma.hlsl"

Texture2D<float4> t_output : register(t0);
Texture2D<float4> t_color : register(t1);
SamplerState s_linear : register(s0);
RWTexture2D<float4> u_target : register(u0);
cbuffer dlss_settings : register(b0) {
  float g_square_root;
  float g_stretch;
  float2 g_render_size;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  u_target.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  const float2 display = float2(width, height);
  const float2 render_pixel = (float2(id.xy) + 0.5f) * g_render_size / display;
  // The stretch's taps stay inside the rendered part.
  float3 color = (g_stretch != 0.f) ? t_color.SampleLevel(s_linear, clamp(render_pixel, 0.5f, g_render_size - 0.5f) / display, 0).rgb
                                    : t_output[id.xy].rgb;
  if (g_square_root != 0.f) color *= abs(color);
  const float3 bt709 = mul(float3x3(1.6604910f, -0.5876411f, -0.0728499f,
                                    -0.1245505f, 1.1328999f, -0.0083494f,
                                    -0.0181508f, -0.1005789f, 1.1187297f),
                           color);
  u_target[id.xy] = float4(renodx::color::gamma::EncodeSafe(bt709), t_color[int2(render_pixel)].a);
}
