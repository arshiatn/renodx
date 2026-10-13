// Tales of Arise motion blur filter (15 samples). From 0x5D770744.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps).
// Motion Blur slider: the velocity scale (cb0[28].y) and maximum length (cb0[28].w) times its value; 1 is stock.
#include "../shared.h"

#define MOTION_BLUR SI.motion_blur

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[124];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[30];
}




// 3Dmigoto declarations
#define cmp -


void main(
  linear noperspective float4 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = floor(v1.xy);
  r0.z = dot(r0.xy, float2(0.0671105608,0.00583714992));
  r0.z = frac(r0.z);
  r0.z = 52.9829178 * r0.z;
  r0.xy = float2(32.6650009,11.8149996) + r0.xy;
  r0.x = dot(r0.xy, float2(0.0671105608,0.00583714992));
  r0.x = frac(r0.x);
  r0.x = 52.9829178 * r0.x;
  r1.xy = frac(r0.zx);
  r0.xy = float2(-0.5,-0.5) + r1.xy;
  r0.xy = float2(0.5,0.5) * r0.xy;
  r0.zw = cb1[123].xy * v0.xy;
  r0.zw = floor(r0.zw);
  r0.zw = float2(0.5,0.5) + r0.zw;
  r0.zw = -cb1[121].xy + r0.zw;
  r0.xy = r0.zw * float2(0.0625,0.0625) + r0.xy;
  r0.xy = cb0[3].zw * r0.xy;
  r0.xy = min(cb0[29].xy, r0.xy);
  r0.xyzw = t2.SampleLevel(s2_s, r0.xy, 0).xyzw;
  r2.xy = (cb0[28].yy * MOTION_BLUR) * r0.zw;
  r0.z = dot(r2.xy, r2.xy);
  r3.xyz = t0.SampleLevel(s0_s, v0.xy, 0).xyz;
  r0.w = cmp(r0.z < 0.25);
  if (r0.w != 0) {
    o0.xyz = r3.xyz;
    o0.w = 0;
  }
  if (r0.w == 0) {
    r0.xy = (cb0[28].yy * MOTION_BLUR) * r0.xy;
    r0.x = dot(r0.xy, r0.xy);
    r4.xyzw = asuint(cb0[23].xyzw);
    r4.xyzw = float4(0.5,0.5,-0.5,-0.5) + r4.xyzw;
    r4.xyzw = cb0[0].zwzw * r4.xyzw;
    r2.zw = -r2.xy;
    r2.xyzw = cb0[0].zwzw * r2.xyzw;
    r0.y = 0.400000006 * r0.z;
    r0.x = cmp(r0.y < r0.x);
    if (r0.x != 0) {
      r1.z = -r1.x;
      r5.xyzw = float4(0,0,1,1) + r1.xxzz;
      r5.xyzw = float4(0.5,0.5,0.5,0.5) * r5.xyzw;
      r5.xyzw = r5.xyzw * r2.xyzw + v0.xyxy;
      r5.xyzw = max(r5.xyzw, r4.xyxy);
      r5.xyzw = min(r5.xyzw, r4.zwzw);
      r0.xyw = t0.SampleLevel(s0_s, r5.xy, 0).xyz;
      r5.xyz = t0.SampleLevel(s0_s, r5.zw, 0).xyz;
      r0.xyw = r5.xyz + r0.xyw;
      r5.xyzw = float4(1,1,2,2) + r1.xxzz;
      r5.xyzw = float4(0.5,0.5,0.5,0.5) * r5.xyzw;
      r5.xyzw = r5.xyzw * r2.xyzw + v0.xyxy;
      r5.xyzw = max(r5.xyzw, r4.xyxy);
      r5.xyzw = min(r5.xyzw, r4.zwzw);
      r6.xyz = t0.SampleLevel(s0_s, r5.xy, 0).xyz;
      r0.xyw = r6.xyz + r0.xyw;
      r5.xyz = t0.SampleLevel(s0_s, r5.zw, 0).xyz;
      r0.xyw = r5.xyz + r0.xyw;
      r0.xyw = float3(0.25,0.25,0.25) * r0.xyw;
    } else {
      r0.z = rsqrt(r0.z);
      r0.z = r0.z + r0.z;
      r1.yz = t1.SampleLevel(s1_s, v0.xy, 0).xz;
      r1.y = (cb0[28].y * MOTION_BLUR) * r1.y;
      r5.x = min((cb0[28].w * MOTION_BLUR), r1.y);
      r1.w = -r1.x;
      r6.xyzw = float4(0,0,1,1) + r1.xxww;
      r6.xyzw = float4(0.5,0.5,0.5,0.5) * r6.xyzw;
      r6.xyzw = r6.xyzw * r2.xyzw + v0.xyxy;
      r6.xyzw = max(r6.xyzw, r4.xyxy);
      r6.xyzw = min(r6.xyzw, r4.zwzw);
      r7.xy = t1.SampleLevel(s1_s, r6.xy, 0).xz;
      r8.xyz = t0.SampleLevel(s0_s, r6.xy, 0).xyz;
      r1.y = (cb0[28].y * MOTION_BLUR) * r7.x;
      r5.y = min((cb0[28].w * MOTION_BLUR), r1.y);
      r1.y = r7.y + -r1.z;
      r6.xy = saturate(r1.yy * float2(1,-1) + float2(0.5,0.5));
      r7.xz = saturate(r5.xy * r0.zz);
      r1.y = dot(r6.xy, r7.xz);
      r6.xy = t1.SampleLevel(s1_s, r6.zw, 0).xz;
      r7.xzw = t0.SampleLevel(s0_s, r6.zw, 0).xyz;
      r3.w = (cb0[28].y * MOTION_BLUR) * r6.x;
      r5.z = min((cb0[28].w * MOTION_BLUR), r3.w);
      r3.w = r6.y + -r1.z;
      r6.xz = saturate(r3.ww * float2(1,-1) + float2(0.5,0.5));
      r9.xy = saturate(r5.xz * r0.zz);
      r3.w = dot(r6.xz, r9.xy);
      r6.x = cmp(r6.y < r7.y);
      r5.z = cmp(r5.y < r5.z);
      r6.y = r5.z ? r6.x : 0;
      r6.y = r6.y ? r3.w : r1.y;
      r5.z = (int)r5.z | (int)r6.x;
      r1.y = r5.z ? r3.w : r1.y;
      r6.xzw = r1.yyy * r7.xzw;
      r6.xzw = r6.yyy * r8.xyz + r6.xzw;
      r7.xyzw = float4(1,1,2,2) + r1.xxww;
      r7.xyzw = float4(0.5,0.5,0.5,0.5) * r7.xyzw;
      r2.xyzw = r7.xyzw * r2.xyzw + v0.xyxy;
      r2.xyzw = max(r2.xyzw, r4.xyxy);
      r2.xyzw = min(r2.xyzw, r4.zwzw);
      r4.xy = t1.SampleLevel(s1_s, r2.xy, 0).xz;
      r7.xyz = t0.SampleLevel(s0_s, r2.xy, 0).xyz;
      r1.w = (cb0[28].y * MOTION_BLUR) * r4.x;
      r5.w = min((cb0[28].w * MOTION_BLUR), r1.w);
      r1.w = r4.y + -r1.z;
      r2.xy = saturate(r1.ww * float2(1,-1) + float2(0.5,0.5));
      r4.xz = saturate(r0.zz * r5.xw + -r1.xx);
      r1.w = dot(r2.xy, r4.xz);
      r2.xy = t1.SampleLevel(s1_s, r2.zw, 0).xz;
      r4.xzw = t0.SampleLevel(s0_s, r2.zw, 0).xyz;
      r2.x = (cb0[28].y * MOTION_BLUR) * r2.x;
      r5.y = min((cb0[28].w * MOTION_BLUR), r2.x);
      r1.z = r2.y + -r1.z;
      r2.xz = saturate(r1.zz * float2(1,-1) + float2(0.5,0.5));
      r1.xz = saturate(r0.zz * r5.xy + -r1.xx);
      r0.z = dot(r2.xz, r1.xz);
      r1.x = cmp(r2.y < r4.y);
      r1.z = cmp(r5.w < r5.y);
      r2.x = r1.z ? r1.x : 0;
      r2.x = r2.x ? r0.z : r1.w;
      r1.x = (int)r1.z | (int)r1.x;
      r0.z = r1.x ? r0.z : r1.w;
      r1.xzw = r0.zzz * r4.xzw;
      r1.xzw = r2.xxx * r7.xyz + r1.xzw;
      r1.xyzw = r6.xyzw + r1.xyzw;
      r0.z = r2.x + r0.z;
      r0.z = r1.y + r0.z;
      r0.z = -r0.z * 0.25 + 1;
      r2.xyz = r0.zzz * r3.xyz;
      r0.xyw = r1.xzw * float3(0.25,0.25,0.25) + r2.xyz;
    }
    o0.xyz = r0.xyw;
    o0.w = 0;
  }
  return;
}