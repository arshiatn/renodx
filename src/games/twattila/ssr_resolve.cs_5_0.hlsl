/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space reflections' resolve (ssr_hiz.hpp, after the game's reflection draw, on the hierarchical march's
// output): the pixels not traced this frame (Reflection Rays Half / Quarter, t1.y -1) filled from the traced ones
// around them (by depth), then with the Reflection Denoiser every reflection blended with the last frame's: its history
// read where the reflected point was (the surface plus the reflection's length along the view ray, the mirror image a
// flat reflection shows; a miss: far away), kept inside this frame's colours around it (from traced pixels), so the
// water's reflections stop flickering without trailing. Pixels the draw left alone (t1.y -2) pass through. Into u0.
cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 projection : packoffset(c5);
  float4x4 inv_view_projection : packoffset(c21);
}

// The last frame's camera_VS_PS (a copy).
cbuffer previous_camera : register(b1)
{
  float4x4 previous_view_projection : packoffset(c9);
}

cbuffer ssr_resolve : register(b2)
{
  float2 g_size;           // the view's (DLSS below 100%: the render size, the top-left of the targets)
  float2 g_inverse_size;
  float g_denoise;         // 1: blended with the history
  float g_reset;           // 1: no history (the first frame, a new size)
  float g_history_weight;  // the history's share
  float g_padding;
  float2 g_texture_inverse_size;  // 1 / the targets' size
  float2 g_padding2;
}

Texture2D<float4> t_reflection : register(t0);  // this frame's (a copy of the draw's target)
Texture2D<float2> t_hit : register(t1);         // the reflected distance, the confidence (-1 not traced, -2 not drawn)
Texture2D<float> t_depth : register(t2);        // the scene's (the draw's t3)
Texture2D<float4> t_history : register(t3);     // the last frame's result
SamplerState s_linear : register(s0);
RWTexture2D<float4> t_rw_result : register(u0);

float Distance(float depth)
{
  return projection._m32 / (depth - projection._m22);
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(float2(pixel) >= g_size)) return;
  const float2 hit = t_hit.Load(int3(pixel, 0));
  float3 current = t_reflection.Load(int3(pixel, 0)).rgb;
  if (hit.y < -1.5f)
  {
    t_rw_result[pixel] = float4(current, 1.f);
    return;
  }
  const float depth = t_depth.Load(int3(pixel, 0));
  const float distance = Distance(depth);

  // The traced pixels around: the fill (by depth) and the colours the history is kept inside.
  float3 filled = 0.f;
  float filled_weight = 0.f;
  float3 low = 1e9f;
  float3 high = -1e9f;
  [unroll]
  for (int y = -1; y <= 1; ++y)
  {
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
      const int2 texel = clamp(int2(pixel) + int2(x, y), 0, int2(g_size) - 1);
      const float2 around = t_hit.Load(int3(texel, 0));
      if (around.y < -0.5f) continue;
      const float3 color = t_reflection.Load(int3(texel, 0)).rgb;
      const float weight = exp(-abs(Distance(t_depth.Load(int3(texel, 0))) - distance) / (0.05f * distance + 0.1f));
      filled += color * weight;
      filled_weight += weight;
      low = min(low, color);
      high = max(high, color);
    }
  }
  if (hit.y < -0.5f && filled_weight > 0.f) current = filled / filled_weight;
  if (g_denoise == 0.f || filled_weight == 0.f)
  {
    t_rw_result[pixel] = float4(current, 1.f);
    return;
  }
  low = min(low, current);
  high = max(high, current);

  // Where the reflected point was last frame.
  const float2 uv = (float2(pixel) + 0.5f) * g_inverse_size;
  float4 world = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), inv_view_projection);
  world /= world.w;
  const float3 view_ray = normalize(world.xyz - camera_position);
  const float reflected = (hit.y > 0.f) ? hit.x : 1000.f;
  const float4 before = mul(float4(world.xyz + view_ray * reflected, 1.f), previous_view_projection);
  const float2 history_uv = before.xy / before.w * float2(0.5f, -0.5f) + 0.5f;
  const bool valid = g_reset == 0.f && before.w > 0.f && all(history_uv >= 0.f) && all(history_uv <= 1.f);
  const float3 history = clamp(t_history.SampleLevel(s_linear, history_uv * g_size * g_texture_inverse_size, 0).rgb, low, high);
  t_rw_result[pixel] = float4(lerp(current, history, valid ? g_history_weight : 0.f), 1.f);
}
