// Tales of Arise motion blur filter (21 samples). From 0x521A2C2A.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps).
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
    r2.zw = -r2.xy;
    r2.xyzw = cb0[0].zwzw * r2.xyzw;
    r0.y = 0.400000006 * r0.z;
    r0.x = cmp(r0.y < r0.x);
    if (r0.x != 0) {
      r1.z = -r1.x;
      r4.xyzw = float4(0,0,1,1) + r1.xxzz;
      r4.xyzw = float4(0.333333343,0.333333343,0.333333343,0.333333343) * r4.xyzw;
      r4.xyzw = r4.xyzw * r2.xyzw + v0.xyxy;
      r0.xyw = t0.SampleLevel(s0_s, r4.xy, 0).xyz;
      r4.xyz = t0.SampleLevel(s0_s, r4.zw, 0).xyz;
      r0.xyw = r4.xyz + r0.xyw;
      r4.xyzw = float4(1,1,2,2) + r1.xxzz;
      r4.xyzw = float4(0.333333343,0.333333343,0.333333343,0.333333343) * r4.xyzw;
      r4.xyzw = r4.xyzw * r2.xyzw + v0.xyxy;
      r5.xyz = t0.SampleLevel(s0_s, r4.xy, 0).xyz;
      r0.xyw = r5.xyz + r0.xyw;
      r4.xyz = t0.SampleLevel(s0_s, r4.zw, 0).xyz;
      r0.xyw = r4.xyz + r0.xyw;
      r4.xyzw = float4(2,2,3,3) + r1.xxzz;
      r4.xyzw = float4(0.333333343,0.333333343,0.333333343,0.333333343) * r4.xyzw;
      r4.xyzw = r4.xyzw * r2.xyzw + v0.xyxy;
      r5.xyz = t0.SampleLevel(s0_s, r4.xy, 0).xyz;
      r0.xyw = r5.xyz + r0.xyw;
      r4.xyz = t0.SampleLevel(s0_s, r4.zw, 0).xyz;
      r0.xyw = r4.xyz + r0.xyw;
      r0.xyw = float3(0.166666672,0.166666672,0.166666672) * r0.xyw;
    } else {
      r0.z = rsqrt(r0.z);
      r0.z = 3 * r0.z;
      r1.yz = t1.SampleLevel(s1_s, v0.xy, 0).xz;
      r1.y = (cb0[28].y * MOTION_BLUR) * r1.y;
      r4.y = min((cb0[28].w * MOTION_BLUR), r1.y);
      r1.w = -r1.x;
      r5.xyzw = float4(0,0,1,1) + r1.xxww;
      r5.xyzw = float4(0.333333343,0.333333343,0.333333343,0.333333343) * r5.xyzw;
      r5.xyzw = r5.xyzw * r2.xyzw + v0.xyxy;
      r6.xy = t1.SampleLevel(s1_s, r5.xy, 0).xz;
      r7.xyz = t0.SampleLevel(s0_s, r5.xy, 0).xyz;
      r1.y = (cb0[28].y * MOTION_BLUR) * r6.x;
      r4.x = min((cb0[28].w * MOTION_BLUR), r1.y);
      r1.y = r6.y + -r1.z;
      r5.xy = saturate(r1.yy * float2(1,-1) + float2(0.5,0.5));
      r6.xz = saturate(r4.yx * r0.zz);
      r1.y = dot(r5.xy, r6.xz);
      r5.xy = t1.SampleLevel(s1_s, r5.zw, 0).xz;
      r6.xzw = t0.SampleLevel(s0_s, r5.zw, 0).xyz;
      r3.w = (cb0[28].y * MOTION_BLUR) * r5.x;
      r4.z = min((cb0[28].w * MOTION_BLUR), r3.w);
      r3.w = r5.y + -r1.z;
      r5.xz = saturate(r3.ww * float2(1,-1) + float2(0.5,0.5));
      r8.xy = saturate(r4.yz * r0.zz);
      r3.w = dot(r5.xz, r8.xy);
      r5.x = cmp(r5.y < r6.y);
      r5.y = cmp(r4.x < r4.z);
      r5.z = r5.y ? r5.x : 0;
      r5.z = r5.z ? r3.w : r1.y;
      r5.x = (int)r5.y | (int)r5.x;
      r1.y = r5.x ? r3.w : r1.y;
      r5.xyw = r1.yyy * r6.xzw;
      r5.xyw = r5.zzz * r7.xyz + r5.xyw;
      r1.y = r5.z + r1.y;
      r6.xyzw = float4(1,1,2,2) + r1.xxww;
      r7.xyzw = float4(0.333333343,0.333333343,0.333333343,0.333333343) * r6.xyzw;
      r7.xyzw = r7.xyzw * r2.xyzw + v0.xyxy;
      r6.xz = t1.SampleLevel(s1_s, r7.xy, 0).xz;
      r8.xyz = t0.SampleLevel(s0_s, r7.xy, 0).xyz;
      r3.w = (cb0[28].y * MOTION_BLUR) * r6.x;
      r4.w = min((cb0[28].w * MOTION_BLUR), r3.w);
      r3.w = r6.z + -r1.z;
      r6.xw = saturate(r3.ww * float2(1,-1) + float2(0.5,0.5));
      r7.xy = saturate(r0.zz * r4.yw + -r1.xx);
      r3.w = dot(r6.xw, r7.xy);
      r6.xw = t1.SampleLevel(s1_s, r7.zw, 0).xz;
      r7.xyz = t0.SampleLevel(s0_s, r7.zw, 0).xyz;
      r5.z = (cb0[28].y * MOTION_BLUR) * r6.x;
      r4.x = min((cb0[28].w * MOTION_BLUR), r5.z);
      r5.z = r6.w + -r1.z;
      r9.xy = saturate(r5.zz * float2(1,-1) + float2(0.5,0.5));
      r9.zw = saturate(r0.zz * r4.yx + -r1.xx);
      r5.z = dot(r9.xy, r9.zw);
      r6.x = cmp(r6.w < r6.z);
      r4.x = cmp(r4.w < r4.x);
      r6.z = r4.x ? r6.x : 0;
      r6.z = r6.z ? r5.z : r3.w;
      r4.x = (int)r4.x | (int)r6.x;
      r3.w = r4.x ? r5.z : r3.w;
      r7.xyz = r3.www * r7.xyz;
      r7.xyz = r6.zzz * r8.xyz + r7.xyz;
      r5.xyz = r7.xyz + r5.xyw;
      r3.w = r6.z + r3.w;
      r1.y = r3.w + r1.y;
      r7.xyzw = float4(2,2,3,3) + r1.xxww;
      r7.xyzw = float4(0.333333343,0.333333343,0.333333343,0.333333343) * r7.xyzw;
      r2.xyzw = r7.xyzw * r2.xyzw + v0.xyxy;
      r1.xw = t1.SampleLevel(s1_s, r2.xy, 0).xz;
      r6.xzw = t0.SampleLevel(s0_s, r2.xy, 0).xyz;
      r1.x = (cb0[28].y * MOTION_BLUR) * r1.x;
      r4.z = min((cb0[28].w * MOTION_BLUR), r1.x);
      r1.x = r1.w + -r1.z;
      r2.xy = saturate(r1.xx * float2(1,-1) + float2(0.5,0.5));
      r7.xy = saturate(r0.zz * r4.yz + -r6.yy);
      r1.x = dot(r2.xy, r7.xy);
      r2.xy = t1.SampleLevel(s1_s, r2.zw, 0).xz;
      r7.xyz = t0.SampleLevel(s0_s, r2.zw, 0).xyz;
      r2.x = (cb0[28].y * MOTION_BLUR) * r2.x;
      r4.w = min((cb0[28].w * MOTION_BLUR), r2.x);
      r1.z = r2.y + -r1.z;
      r2.xz = saturate(r1.zz * float2(1,-1) + float2(0.5,0.5));
      r4.xy = saturate(r0.zz * r4.yw + -r6.yy);
      r0.z = dot(r2.xz, r4.xy);
      r1.z = cmp(r2.y < r1.w);
      r1.w = cmp(r4.z < r4.w);
      r2.x = r1.w ? r1.z : 0;
      r2.x = r2.x ? r0.z : r1.x;
      r1.z = (int)r1.w | (int)r1.z;
      r0.z = r1.z ? r0.z : r1.x;
      r1.xzw = r0.zzz * r7.xyz;
      r1.xzw = r2.xxx * r6.xzw + r1.xzw;
      r1.xzw = r5.xyz + r1.xzw;
      r0.z = r2.x + r0.z;
      r0.z = r1.y + r0.z;
      r0.z = -r0.z * 0.166666672 + 1;
      r2.xyz = r0.zzz * r3.xyz;
      r0.xyw = r1.xzw * float3(0.166666672,0.166666672,0.166666672) + r2.xyz;
    }
    o0.xyz = r0.xyw;
    o0.w = 0;
  }
  return;
}