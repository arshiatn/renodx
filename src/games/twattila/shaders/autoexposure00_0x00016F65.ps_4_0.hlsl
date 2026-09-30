// ---- Created with 3Dmigoto v1.3.16 on Thu Sep 03 21:38:10 2026

cbuffer luma_stats_buffer : register(b0)
{
  float g_absolute_luma_black_point : packoffset(c0);
  float g_max_luma_black_point : packoffset(c0.y);
  float g_min_luma_white_point : packoffset(c0.z);
  float g_absolute_luma_white_point : packoffset(c0.w);
}

cbuffer auto_exposure_buffer : register(b1)
{
  float g_auto_exposure_speed : packoffset(c0);
  float g_auto_exposure_dt : packoffset(c0.y);
}

SamplerState g_luma_stats_texture_sampler_s : register(s0);
SamplerState g_prev_black_and_white_points_sampler_s : register(s1);
Texture2D<float4> g_prev_black_and_white_points_sampler : register(t0);
Texture2D<float4> g_luma_stats_texture_sampler : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = g_luma_stats_texture_sampler.SampleLevel(g_luma_stats_texture_sampler_s, float2(0.5,0.5), 0).xyzw;

  // The statistics texture stores minimum, maximum, and average luminance in
  // xyz. Attila's stock white-point calculation reacts strongly to an isolated
  // maximum, so very bright fire or the sun can darken the complete frame.
  // Limit only the maximum used for exposure metering relative to the average;
  // the HDR scene RGB itself is untouched, so visible highlight brightness is
  // not capped. A protection value of zero follows the stock math exactly.
  if (AutoExposureHighlightProtection > 0.f) {
    float highlight_protection = saturate(AutoExposureHighlightProtection);
    float highlight_headroom_stops =clamp(AutoExposureHighlightHeadroomStops, 0.f, 16.f);
    float frame_average_luma = max(r0.z, 0.000001f);
    float protected_max_luma = min(r0.y, frame_average_luma * exp2(highlight_headroom_stops));
    r0.y = lerp(r0.y, protected_max_luma, highlight_protection);
  }

  r1.xy = r0.yz + -r0.zx;
  r0.w = max(r1.x, r1.y);
  r1.x = min(r1.x, r1.y);
  r0.w = -r1.x + r0.w;
  r1.y = r1.x + r0.z;
  r1.y = r0.w * 0.860000014 + r1.y;
  r1.y = r1.x * 1.23000002 + r1.y;
  r0.y = r1.x * 1.23000002 + r0.y;
  r0.z = -r1.x + r0.z;
  r0.z = -r0.w * 0.860000014 + r0.z;
  r0.x = max(r0.z, r0.x);
  r0.x = max(g_absolute_luma_black_point, r0.x);
  r2.x = min(g_max_luma_black_point, r0.x);
  r0.x = min(r1.y, r0.y);
  r0.x = max(g_min_luma_white_point, r0.x);
  r2.y = min(g_absolute_luma_white_point, r0.x);
  r0.xyzw = g_prev_black_and_white_points_sampler.SampleLevel(g_prev_black_and_white_points_sampler_s, float2(0.5,0.5), 0).xyzw;
  r0.xy = r0.xy + -r2.xy;
  r0.z = 1 + -g_auto_exposure_speed;
  r0.z = log2(r0.z);
  r0.w = 30 * g_auto_exposure_dt;
  r0.z = r0.w * r0.z;
  r0.z = exp2(r0.z);
  o0.xy = r0.zz * r0.xy + r2.xy;
  o0.zw = float2(0,0);
  return;
}
