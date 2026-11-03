/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// TAA / SMAA T2x (anti_aliasing.hpp): this frame (t0, its camera jittered; SMAA T2x: already SMAA'd) blended with the
// history (t1, the last result) where that point was last frame. The game has no motion vectors: the motion is the
// camera's (the depth, t2, reprojected with both frames' cameras), from the nearest depth around the pixel (edges move
// with the nearer surface); with TAA Motion Vectors on Objects what moves on its own takes its own there (t4-t6, as
// DLSS's). The rest relies on the clip: the history is kept inside the colours around the pixel
// this frame (YCoCg: their range, at most mean +- 1.5 deviations; WARP: the best on thin stripes and a moving box), so
// it cannot drag a soldier's old colours along. Where the history's
// surface was not there last frame (its distance, t3, against the expected one) it counts less. The history is read
// with a Catmull-Rom filter (no softening). This frame is taken at the pixel's centre: its 3 x 3 texels (each showing
// the scene g_taa_jitter away from where it sits) weighted by their distance to it (Blackman-Harris as a Gaussian, as
// UE's TAA): where the history is cut away (swaying trees and grass: the camera's motion is not theirs) the picture
// no longer jitters (ATN). Out: the result (u0) and this frame's distances (u1) for the next.
#include "./aa.hlsli"
#include "./motion.hlsli"

Texture2D<float4> t_current : register(t0);
Texture2D<float4> t_history : register(t1);
Texture2D<float> t_depth : register(t2);
Texture2D<float> t_previous_distance : register(t3);
Texture2D<uint2> t_object_motion : register(t4);  // motion.hlsli MotionTexel (unbound: none)
Texture2D<float2> t_drawn_motion : register(t5);  // the in-draw motion in pixels
Texture2D<float> t_drawn_depth : register(t6);    // its depth (0: not written; unbound: none)
RWTexture2D<float4> t_rw_result : register(u0);
RWTexture2D<float> t_rw_distance : register(u1);

float3 ToYCoCg(float3 rgb)
{
  return float3(dot(rgb, float3(0.25f, 0.5f, 0.25f)), dot(rgb, float3(0.5f, 0.f, -0.5f)),
                dot(rgb, float3(-0.25f, 0.5f, -0.25f)));
}

float3 FromYCoCg(float3 ycocg)
{
  return float3(ycocg.x + ycocg.y - ycocg.z, ycocg.x + ycocg.z, ycocg.x - ycocg.y - ycocg.z);
}

// The history at `uv`: 4 x 4 texels, Catmull-Rom weights.
float4 SampleHistory(float2 uv, int2 size)
{
  const float2 position = uv * float2(size) - 0.5f;
  const float2 base = floor(position);
  const float2 f = position - base;
  const float2 weights[4] = {
    f * (-0.5f + f * (1.f - 0.5f * f)),
    1.f + f * f * (-2.5f + 1.5f * f),
    f * (0.5f + f * (2.f - 1.5f * f)),
    f * f * (-0.5f + 0.5f * f),
  };
  float4 sum = 0.f;
  [unroll]
  for (int y = 0; y < 4; ++y)
  {
    [unroll]
    for (int x = 0; x < 4; ++x)
    {
      const int2 texel = clamp(int2(base) + int2(x - 1, y - 1), 0, size - 1);
      sum += t_history.Load(int3(texel, 0)) * (weights[x].x * weights[y].y);
    }
  }
  return sum;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  const int2 size = int2(g_aa_metrics.zw);
  if (any(int2(pixel) >= size)) return;
  const float own_depth = t_depth.Load(int3(pixel, 0));
  t_rw_distance[pixel] = g_taa_projection_z.y / (own_depth - g_taa_projection_z.x);

  // The colours around the pixel this frame, and the nearest depth among them.
  const float4 current = t_current.Load(int3(pixel, 0));
  float3 mean = 0.f;
  float3 square = 0.f;
  float3 minimum = 1e9f;
  float3 maximum = -1e9f;
  float3 centred = 0.f;
  float centred_weight = 0.f;
  float nearest_depth = own_depth;
  int2 nearest = int2(pixel);
  [unroll]
  for (int y = -1; y <= 1; ++y)
  {
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
      const int2 texel = clamp(int2(pixel) + int2(x, y), 0, size - 1);
      const float3 rgb = t_current.Load(int3(texel, 0)).rgb;
      const float2 offset = float2(x, y) - g_taa_jitter;
      const float weight = exp(-2.29f * dot(offset, offset));
      centred += rgb * weight;
      centred_weight += weight;
      const float3 color = ToYCoCg(rgb);
      mean += color;
      square += color * color;
      minimum = min(minimum, color);
      maximum = max(maximum, color);
      const float depth = t_depth.Load(int3(texel, 0));
      if (depth < nearest_depth)
      {
        nearest_depth = depth;
        nearest = texel;
      }
    }
  }
  mean /= 9.f;
  const float3 deviation = sqrt(max(square / 9.f - mean * mean, 0.f));

  // Where the nearest surface was last frame: its position from this frame's (jittered) camera, both cameras without
  // the jitter for the motion.
  const float2 nearest_uv = (float2(nearest) + 0.5f) * g_aa_metrics.xy;
  float4 world = mul(float4(nearest_uv * float2(2.f, -2.f) + float2(-1.f, 1.f), nearest_depth, 1.f),
                     g_taa_inverse_view_projection);
  world /= world.w;
  const float4 now = mul(world, g_taa_view_projection);
  const float4 before = mul(world, g_taa_previous_view_projection);
  float2 motion = (now.xy / now.w - before.xy / before.w) * float2(0.5f, -0.5f);
  // TAA Motion Vectors on Objects: what moves on its own takes its own (in pixels, to where it was; its draw can divide by 0).
  [branch] if (g_taa_object_motion != 0.f)
  {
    float2 object_motion;
    const bool found = ObjectMotionAt(t_object_motion, t_drawn_motion, t_drawn_depth, nearest, nearest_depth, object_motion);
    if (found && all(isfinite(object_motion))) motion = -object_motion * g_aa_metrics.xy;
  }
  const float2 history_uv = (float2(pixel) + 0.5f) * g_aa_metrics.xy - motion;

  float weight = g_taa_history_weight;
  if (g_taa_reset != 0.f || any(history_uv < 0.f) || any(history_uv > 1.f)) weight = 0.f;
  // A surface that was not there last frame (more than 5% nearer or farther than expected): less history.
  const float previous_distance = t_previous_distance.Load(int3(clamp(int2(history_uv * float2(size)), 0, size - 1), 0));
  const float expected = before.w;
  if (abs(previous_distance - expected) > 0.049998403f * expected) weight *= 0.5f;

  // The history inside this frame's colours (clipped towards their box's centre).
  const float3 history = ToYCoCg(SampleHistory(history_uv, size).rgb);
  const float3 low = max(minimum, mean - 1.5f * deviation);
  const float3 high = min(maximum, mean + 1.5f * deviation);
  const float3 centre = 0.5f * (high + low);
  const float3 offset = history - centre;
  const float3 extent = max(0.5f * (high - low), 9.999235e-7f);
  const float3 units = abs(offset / extent);
  const float largest = max(units.x, max(units.y, units.z));
  const float3 clipped = (largest > 1.f) ? centre + offset / largest : history;

  const float3 result = lerp(ToYCoCg(centred / centred_weight), clipped, weight);
  t_rw_result[pixel] = float4(FromYCoCg(result), current.a);
}
