/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Below 100% (taa_dlss.hpp): the tone mapper's picture (t0, at the render size, top-left; gamma 2.2, sign kept) as the
// upscaler's colour (u0): linear, in BT.2020 so wide colours stay positive; NVIDIA's L, M and Ray Reconstruction take its
// square root (dlss_scaled_finish squares it). Its alpha kept.
#include "../../shaders/color/gamma.hlsl"

Texture2D<float4> t_picture : register(t0);
RWTexture2D<float4> u_color : register(u0);
cbuffer dlss_settings : register(b0) {
  float g_square_root;
  float g_stretch;  // dlss_scaled_finish's
  float2 g_render_size;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(float2(id.xy) >= g_render_size)) return;
  const float4 picture = t_picture[id.xy];
  const float3 bt2020 = mul(float3x3(0.6274040f, 0.3292820f, 0.0433136f,
                                     0.0690970f, 0.9195400f, 0.0113612f,
                                     0.0163916f, 0.0880132f, 0.8955950f),
                            renodx::color::gamma::DecodeSafe(picture.rgb));
  const float3 colour = max(bt2020, 0.f);
  u_color[id.xy] = float4((g_square_root != 0.f) ? sqrt(colour) : colour, picture.a);
}
