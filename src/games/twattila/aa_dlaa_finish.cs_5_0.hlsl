/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// DLAA (anti_aliasing.hpp): NVIDIA's result (t0, linear BT.2020) back to the tone mapper's signed BT.709 and its 2.0
// encoding (square root, sign kept), the frame's own alpha (t1), into the result (u0). Its Debug View instead: the motion
// NVIDIA got (1: t2) or the objects' own (2: t3, motion.hlsli MotionTexel; t4 the depth), at the input size (top-left),
// stretched over the picture: the direction as the hue, brighter the faster (32 pixels a frame: full). Object Motion
// (t5 / t6 the in-draw motion and depth too): grey where an object's is the final surface but still (yellowish: drawn
// again, not in-draw), dark red where it is hidden (not used), the scene dimmed where there is none.
#include "./aa.hlsli"
#include "./motion.hlsli"

Texture2D<float4> t_dlaa : register(t0);
Texture2D<float4> t_current : register(t1);
Texture2D<float2> t_motion : register(t2);
Texture2D<uint2> t_object_motion : register(t3);
Texture2D<float> t_depth : register(t4);
Texture2D<float2> t_drawn_motion : register(t5);
Texture2D<float> t_drawn_depth : register(t6);
RWTexture2D<float4> t_rw_result : register(u0);

float3 MotionColour(float2 motion)
{
  const float hue = atan2(motion.y, motion.x) * (0.5f / 3.14159265f) + 0.5f;
  const float3 rgb = saturate(abs(frac(hue + float3(0.f, 2.f / 3.f, 1.f / 3.f)) * 6.f - 3.f) - 1.f);
  return rgb * saturate(log2(1.f + length(motion)) / 5.f);
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(int2(pixel) >= int2(g_aa_metrics.zw))) return;
  const float4 current = t_current.Load(int3(pixel, 0));
  if (g_dlaa_debug.x == 1.f || g_dlaa_debug.x == 2.f) {
    const int2 input = min(int2((float2(pixel) + 0.5f) * g_dlaa_debug.yz * g_aa_metrics.xy), int2(g_dlaa_debug.yz) - 1);
    const float3 scene = 0.1f * dot(abs(t_current.Load(int3(input, 0)).rgb), float3(0.2126f, 0.7152f, 0.0722f));
    float3 colour = max(MotionColour(t_motion.Load(int3(input, 0))), scene);
    if (g_dlaa_debug.x == 2.f) {
      const float depth = t_depth.Load(int3(input, 0));
      const float drawn_depth = t_drawn_depth.Load(int3(input, 0));
      const uint2 object_motion = t_object_motion.Load(int3(input, 0));
      colour = scene;
      if (drawn_depth != 0.f && MotionDepthMatches(drawn_depth, depth)) {
        colour = max(MotionColour(t_drawn_motion.Load(int3(input, 0))), 0.25f);
      } else if (MotionTexelWritten(object_motion) && MotionDepthMatches(MotionTexelDepth(object_motion), depth)) {
        colour = max(MotionColour(MotionTexelMotion(object_motion)), float3(0.3f, 0.25f, 0.1f));
      } else if (drawn_depth != 0.f || MotionTexelWritten(object_motion)) {
        colour = float3(0.4f, 0.f, 0.f);
      }
    }
    t_rw_result[pixel] = float4(colour, current.a);
    return;
  }
  const float3 linear_color = mul(float3x3(1.6604910f, -0.5876411f, -0.0728499f,
                                           -0.1245505f, 1.1328999f, -0.0083494f,
                                           -0.0181508f, -0.1005789f, 1.1187297f),
                                  t_dlaa.Load(int3(pixel, 0)).rgb);
  t_rw_result[pixel] = float4(sign(linear_color) * sqrt(abs(linear_color)), current.a);
}
