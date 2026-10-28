/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// DLAA (anti_aliasing.hpp, dlaa_bridge.hpp): NVIDIA's inputs from the tone mapper's output (t0, 2.0 encoded, signed)
// and the scene's depth (t1): the colour linear for its HDR model (u0: squared, sign kept, in BT.2020 so wide colours
// stay positive), the depth (u1), and the motion in pixels to where each pixel was last frame (u2, x right, y down):
// objects' own where their depth is the final one (t3 / t4: written by the game's draws themselves, object_motion.hpp
// in-draw; else t2: drawn again, motion.hlsli MotionTexel), the rest the camera's, from the depth and both frames'
// cameras without the jitter (the game has no motion vectors of its own); 0 after a reset.
#include "./aa.hlsli"
#include "./motion.hlsli"

Texture2D<float4> t_current : register(t0);
Texture2D<float> t_depth : register(t1);
Texture2D<uint2> t_object_motion : register(t2);  // motion.hlsli MotionTexel (unbound: none)
Texture2D<float2> t_drawn_motion : register(t3);  // the in-draw motion in render pixels
Texture2D<float> t_drawn_depth : register(t4);    // its depth (0: not written; unbound: none)
RWTexture2D<float4> t_rw_color : register(u0);
RWTexture2D<float> t_rw_depth : register(u1);
RWTexture2D<float2> t_rw_motion : register(u2);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  const int2 size = int2(g_aa_metrics.zw);
  if (any(int2(pixel) >= size)) return;
  const float4 current = t_current.Load(int3(pixel, 0));
  const float3 bt709 = sign(current.rgb) * current.rgb * current.rgb;
  const float3 bt2020 = mul(float3x3(0.6274040f, 0.3292820f, 0.0433136f,
                                     0.0690970f, 0.9195400f, 0.0113612f,
                                     0.0163916f, 0.0880132f, 0.8955950f), bt709);
  t_rw_color[pixel] = float4(max(bt2020, 0.f), current.a);
  const float depth = t_depth.Load(int3(pixel, 0));
  t_rw_depth[pixel] = depth;

  const float2 uv = (float2(pixel) + 0.5f) * g_aa_metrics.xy;
  float4 world = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), g_taa_inverse_view_projection);
  world /= world.w;
  const float4 now = mul(world, g_taa_view_projection);
  const float4 before = mul(world, g_taa_previous_view_projection);
  float2 motion = (before.xy / before.w - now.xy / now.w) * float2(0.5f, -0.5f) * g_aa_metrics.zw;
  if (g_dlaa_debug.x == 3.f) motion = 0.f;  // the Debug View's No Camera Motion: the objects' own alone
  const float drawn_depth = t_drawn_depth.Load(int3(pixel, 0));
  const uint2 object_motion = t_object_motion.Load(int3(pixel, 0));
  if (drawn_depth != 0.f && MotionDepthMatches(drawn_depth, depth)) {
    motion = t_drawn_motion.Load(int3(pixel, 0));
  } else if (MotionTexelWritten(object_motion) && MotionDepthMatches(MotionTexelDepth(object_motion), depth)) {
    motion = MotionTexelMotion(object_motion);
  }
  t_rw_motion[pixel] = (g_taa_reset == 0.f && all(isfinite(motion))) ? motion : 0.f;
}
