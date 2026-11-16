/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// R.U.S.E.'s prepared light for the helper's ray-traced Light Bounce (ray_tracing.hpp): the light leaving the surface
// each bounce texel shows (its centre pixel of this frame's start depth): last frame's scene where that point was (the
// bridge's colour, linear), as the helper's light lookup reads it (a texel each `scale` x `scale` render pixels).
Texture2D<float> t_depth : register(t0);   // this frame's start depth
Texture2D<float4> t_light : register(t1);  // last frame's scene as light
RWTexture2D<float3> u_prepared : register(u0);  // R11G11B10F

cbuffer bounce_light_settings : register(b0) {
  row_major float4x4 to_previous;  // this frame's clip to last frame's
  float4 size;                     // width, height, 1 / width, 1 / height
  uint4 texels;                    // bounce width, height, scale
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= texels.xy)) return;
  const uint2 pixel = min(id.xy * texels.z + texels.z / 2u, (uint2)size.xy - 1u);
  const float depth = t_depth.Load(int3(pixel, 0));
  float3 light = 0.f;
  if (depth < 1.f) {
    const float2 ndc = float2((pixel.x + 0.5f) * size.z * 2.f - 1.f, 1.f - (pixel.y + 0.5f) * size.w * 2.f);
    const float4 previous = mul(to_previous, float4(ndc, depth, 1.f));
    const float2 uv = previous.xy / previous.w * float2(0.5f, -0.5f) + 0.5f;
    if (previous.w > 0.f && all(uv >= 0.f) && all(uv < 1.f)) light = max(t_light.Load(int3(uv * size.xy, 0)).rgb, 0.f);
  }
  u_prepared[id.xy] = light;
}
