/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows' Resolution below Full (screen_space_shadows.hpp): the march's low occlusion (t1), penumbra (t2:
// Softer Away from the Caster, in low pixels; b4's tangent above 0) and filter (t3: Coloured Shadows) up to the render
// pixels (u0-u2), each from the four low pixels around it by Toru's joint bilateral weights (svgf/joint_bilateral.hlsli):
// their render pixels' (sss_resolution.hlsli) positions and normals against its own, from the depth (t0: sss_depth's)
// and the camera (b0, Attila's layout). The sky stays unshadowed, as Bend leaves it. The penumbra over the taps that
// have one, its radius from low pixels to render pixels.
#include "./sss_resolution.hlsli"
#include "./svgf/joint_bilateral.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float4x4 inv_projection : packoffset(c17);
}

Texture2D<float> t_sss_depth : register(t0);
Texture2D<unorm float> t_sss_low_occlusion : register(t1);
Texture2D<float> t_sss_low_penumbra : register(t2);
Texture2D<unorm float4> t_sss_low_tint : register(t3);
RWTexture2D<unorm float> t_rw_occlusion : register(u0);
RWTexture2D<float> t_rw_penumbra : register(u1);
RWTexture2D<unorm float4> t_rw_tint : register(u2);

// A render pixel's view position (sss_bend.cs_5_0.hlsl's), from its depth (foliage's marked negative).
float3 ViewPosition(int2 pixel)
{
  const float2 uv = (float2(pixel) + 0.5f) / g_sss_render_size;
  const float4 view = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), abs(t_sss_depth[pixel]), 1.f), inv_projection);
  return view.xyz / view.w;
}

// Its surface's normal from the depth, facing the camera: across and down, the step to the neighbour nearer in depth (not
// over an edge); 0 where there is none (the sky on both sides).
float3 ViewNormal(int2 pixel, float3 position)
{
  const int2 last = int2(g_sss_render_size) - 1;
  const float3 left = ViewPosition(int2(max(pixel.x - 1, 0), pixel.y));
  const float3 right = ViewPosition(int2(min(pixel.x + 1, last.x), pixel.y));
  const float3 up = ViewPosition(int2(pixel.x, max(pixel.y - 1, 0)));
  const float3 down = ViewPosition(int2(pixel.x, min(pixel.y + 1, last.y)));
  const float3 across = (pixel.x == last.x || (pixel.x > 0 && abs(position.z - left.z) < abs(right.z - position.z))) ? position - left : right - position;
  const float3 along = (pixel.y == last.y || (pixel.y > 0 && abs(position.z - up.z) < abs(down.z - position.z))) ? position - up : down - position;
  const float3 normal = cross(across, along);
  const float length_squared = dot(normal, normal);
  return (length_squared > 0.f) ? normal * (rsqrt(length_squared) * ((dot(normal, position) > 0.f) ? -1.f : 1.f)) : 0.f;
}

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
  const int2 pixel = int2(id);
  if (any(pixel >= int2(g_sss_render_size))) return;
  // Its place in the low grid (low pixel q's centre at q + 0.5) and the four low pixels around it.
  const float2 place = (float2(pixel) + 0.5f) * (g_sss_low_size / g_sss_render_size) - 0.5f;
  const int2 corner = int2(floor(place));
  int2 taps[4];
  [unroll] for (int corner_tap = 0; corner_tap < 4; ++corner_tap)
  {
    taps[corner_tap] = clamp(corner + int2(corner_tap & 1, corner_tap >> 1), 0, int2(g_sss_low_size) - 1);
  }
  float4 weights = 0.f;
  [branch] if (abs(t_sss_depth[pixel]) < 1.f)
  {
    float3 tap_positions[4];
    float3 tap_normals[4];
    bool4 valid;
    [unroll] for (int surface_tap = 0; surface_tap < 4; ++surface_tap)
    {
      const int2 rendered = LowRenderPixel(taps[surface_tap]);
      valid[surface_tap] = abs(t_sss_depth[rendered]) < 1.f;
      tap_positions[surface_tap] = ViewPosition(rendered);
      tap_normals[surface_tap] = ViewNormal(rendered, tap_positions[surface_tap]);
    }
    const float3 position = ViewPosition(pixel);
    weights = JointBilateralWeights(place - corner, position, ViewNormal(pixel, position), tap_positions, tap_normals, valid);
  }
  float occlusion = 0.f;
  // The penumbra over the taps that have one (0: lit, no caster): not thinned by the lit pixels beside a shadow.
  float penumbra = 0.f;
  float penumbra_weight = 0.f;
  float4 tint = 0.f;
  [unroll] for (int tap = 0; tap < 4; ++tap)
  {
    occlusion += weights[tap] * t_sss_low_occlusion[taps[tap]];
    const float radius = t_sss_low_penumbra[taps[tap]];
    if (radius > 0.f)
    {
      penumbra += weights[tap] * radius;
      penumbra_weight += weights[tap];
    }
    tint += weights[tap] * t_sss_low_tint[taps[tap]];
  }
  t_rw_occlusion[pixel] = occlusion;
  [branch] if (g_sss_sun_tangent > 0.f) t_rw_penumbra[pixel] = (penumbra_weight > 0.f) ? penumbra / penumbra_weight * (g_sss_render_size.y / g_sss_low_size.y) : 0.f;
  [branch] if (g_sss_coloured != 0.f) t_rw_tint[pixel] = tint;
}
