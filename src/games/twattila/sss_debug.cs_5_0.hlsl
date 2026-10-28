/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows' Debug View (screen_space_shadows.hpp): this frame's occlusion (t0) as the picture, white lit,
// black shadowed; with Screen Edge (2) red where the shadow's ray towards the sun (the light's screen point, Bend's
// light coordinate in t1: away from it when the sun is behind the camera) leaves the screen before its length (towards
// the sun: half the way to its point, bend_sss_gpu.hlsli's): nothing past the edge can cast a shadow there. Into u0,
// copied over the tone mapper's output before the anti-aliasing: below 100% into its render-size part, which DLSS then
// upscales as it does the shadows in the picture.
#include "./sss.hlsli"

Texture2D<float> t_sss_occlusion : register(t0);
StructuredBuffer<int4> t_sss_parameters : register(t1);
RWTexture2D<float4> t_rw_debug : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(float2(pixel) >= g_sss_render_size)) return;
  const float lit = 0.8f * (1.f - t_sss_occlusion.Load(int3(pixel, 0)));
  float cut = 0.f;
  if (g_sss_debug_view >= 2.f)
  {
    const float4 light = asfloat(t_sss_parameters[0]);
    const float2 position = float2(pixel) + 0.5f;
    const float2 towards = (light.xy - position) * light.w;
    if (dot(towards, towards) > 1e-6f)
    {
      const float2 direction = normalize(towards);
      // The ray's length on screen before an edge, along each axis.
      const float2 room = (direction > 0.f) ? (g_sss_render_size - position) : position;
      const float2 reach = (abs(direction) > 1e-6f) ? room / abs(direction) : 1e9f;
      const float ray_length = (light.w > 0.f) ? min(g_sss_samples, 0.5f * length(towards)) : g_sss_samples;
      cut = saturate(1.f - min(reach.x, reach.y) / max(ray_length, 1.f));
    }
  }
  const float3 colour = lerp(lit.xxx, float3(1.f, 0.f, 0.f) * max(lit, 0.35f), 0.6f * cut);
  t_rw_debug[pixel] = float4(colour, 1.f);
}
