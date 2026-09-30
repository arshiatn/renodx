// ---- Created with 3Dmigoto v1.3.16 on Thu Sep 24 18:36:25 2026

// ROME REMASTERED distortion00 - heat haze (scrolling noise, strongest near the horizon)
// plus the screen-space distortion buffer (t2), then resamples the scene at the offset UV.
// Slider (HDR only) scales the whole offset.

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[7];
}




// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = -0.5 + v1.x;
  r0.yz = cb0[3].xy + cb0[3].xy;
  r0.x = r0.y * r0.x;
  r0.xyw = r0.xxx * cb0[5].xyz + cb0[4].xyz;
  r1.x = 0.5 + -v1.y;
  r0.z = r1.x * r0.z;
  r0.xyz = r0.zzz * cb0[6].xyz + r0.xyw;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r0.xyz * r0.www;
  r0.x = r0.y * r0.w + -cb0[2].w;
  r0.x = r0.x / cb0[2].z;
  r0.x = r0.x * r0.x;
  r0.x = -0.721347511 * r0.x;
  r0.x = exp2(r0.x);
  r0.x = cb0[0].y * r0.x;
  r0.xy = cb0[1].xy * r0.xx;
  r0.z = dot(r1.xz, r1.xz);
  r0.z = sqrt(r0.z);
  r0.w = -r1.z / r0.z;
  r0.z = r1.y / r0.z;
  r1.y = abs(r0.w) * -0.0187292993 + 0.0742610022;
  r1.y = r1.y * abs(r0.w) + -0.212114394;
  r1.y = r1.y * abs(r0.w) + 1.57072878;
  r1.z = 1 + -abs(r0.w);
  r0.w = cmp(r0.w < -r0.w);
  r1.z = sqrt(r1.z);
  r1.w = r1.y * r1.z;
  r1.w = r1.w * -2 + 3.14159274;
  r0.w = r0.w ? r1.w : 0;
  r0.w = r1.y * r1.z + r0.w;
  r1.y = cmp(0 < r1.x);
  r1.x = cmp(r1.x < 0);
  r1.x = (int)-r1.y + (int)r1.x;
  r1.x = (int)r1.x;
  r0.w = r1.x * r0.w;
  r0.w = cb0[2].y * r0.w;
  r1.x = r0.w * 0.159154937 + 0.5;
  r0.w = cb0[2].x + cb0[2].x;
  r0.z = r0.z / r0.w;
  r0.z = 0.5 + r0.z;
  r1.y = -cb0[0].x * cb0[0].z + r0.z;
  r0.zw = t1.Sample(s1_s, r1.xy).xy;
  r0.zw = r0.zw * float2(2,2) + float2(-1,-1);
  r0.zw = float2(3.14159274,3.14159274) * r0.zw;
  r0.zw = sin(r0.zw);
  r1.xy = cmp(float2(0,0) < r0.zw);
  r1.zw = cmp(r0.zw < float2(0,0));
  r0.zw = r0.zw * r0.zw;
  r1.xy = (int2)-r1.xy + (int2)r1.zw;
  r1.xy = (int2)r1.xy;
  r0.zw = r1.xy * r0.zw;
  r1.xy = t2.Sample(s0_s, v1.xy).xy;
  r0.xy = r0.zw * r0.xy + r1.xy;
  r0.xy *= (HDR >= 0.5f) ? SI.uvdistort : 1.f;  // distortion slider, HDR only
  r0.xy = v1.xy + r0.xy;
  o0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  return;
}
