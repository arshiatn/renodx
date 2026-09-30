// ---- Created with 3Dmigoto v1.3.16 on Fri Sep 11 23:11:45 2026

cbuffer log_Y_stats_buffer : register(b0)
{
  float g_absolute_log_Y_black_point : packoffset(c0);
  float g_max_log_Y_black_point : packoffset(c0.y);
  float g_min_log_Y_white_point : packoffset(c0.z);
  float g_absolute_log_Y_white_point : packoffset(c0.w);
}

cbuffer auto_exposure_buffer : register(b1)
{
  float g_auto_exposure_speed : packoffset(c0);
  float g_auto_exposure_dt : packoffset(c0.y);
}

SamplerState g_logY_values_texture_sampler_s : register(s0);
SamplerState g_prev_black_and_white_points_sampler_s : register(s1);
Texture2D<float4> g_logY_values_texture_sampler : register(t0);
Texture2D<float4> g_prev_black_and_white_points_sampler : register(t1);


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

  r0.xyzw = g_logY_values_texture_sampler.SampleLevel(g_logY_values_texture_sampler_s, float2(0.5,0.5), 0).xyzw;

  // CUSTOM ROME 2 HIGHLIGHT PROTECTION
  if (AutoExposureHighlightProtection > 0.f) {
    float highlight_protection = saturate(AutoExposureHighlightProtection);
    float highlight_headroom_stops = clamp(AutoExposureHighlightHeadroomStops, 0.f, 16.f);

    // Rome 2 is already in Log2 space unlike Attila so no need for exp(...)
    float frame_average_logY = r0.z;
    float protected_max_logY = min(r0.y, frame_average_logY + highlight_headroom_stops);

    r0.y = lerp(r0.y, protected_max_logY, highlight_protection);
  }
  r0.y = r0.y + -r0.x;
  r0.w = -r0.y * 0.5 + r0.z;
  r0.y = r0.y * 0.524999976 + r0.z;
  r0.x = max(r0.w, r0.x);
  r0.x = max(g_absolute_log_Y_black_point, r0.x);
  r1.x = min(g_max_log_Y_black_point, r0.x);
  r0.x = max(g_min_log_Y_white_point, r0.y);
  r1.z = min(g_absolute_log_Y_white_point, r0.x);
  r0.xy = float2(3.32192802,3.32192802) * r1.xz;
  r1.yw = exp2(r0.xy);
  r0.xyzw = g_prev_black_and_white_points_sampler.SampleLevel(g_prev_black_and_white_points_sampler_s, float2(0.5,0.5), 0).xyzw;
  r1.xyzw = r1.xyzw + -r0.xyzw;
  r2.x = 1 + -g_auto_exposure_speed;
  r2.x = log2(r2.x);
  r2.y = 30 * g_auto_exposure_dt;
  r2.x = r2.y * r2.x;
  r2.x = exp2(r2.x);
  r2.x = 1 + -r2.x;
  o0.xyzw = r1.xyzw * r2.xxxx + r0.xyzw;
  return;
}