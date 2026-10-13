// Tales of Arise motion blur filter (27 samples). From 0x5622A37F.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps).
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
  float4 cb0[31];
}




// 3Dmigoto declarations
#define cmp -


void main(
  linear noperspective float4 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
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
  r0.zw = v0.xy * cb0[30].xy + cb0[30].zw;
  r2.xy = cb1[123].xy * r0.zw;
  r2.xy = floor(r2.xy);
  r2.xy = float2(0.5,0.5) + r2.xy;
  r2.xy = -cb1[121].xy + r2.xy;
  r0.xy = r2.xy * float2(0.0625,0.0625) + r0.xy;
  r0.xy = cb0[3].zw * r0.xy;
  r0.xy = min(cb0[29].xy, r0.xy);
  r2.xyzw = t2.SampleLevel(s2_s, r0.xy, 0).xyzw;
  r3.xy = (cb0[28].yy * MOTION_BLUR) * r2.zw;
  r0.x = dot(r3.xy, r3.xy);
  r4.xyz = t0.SampleLevel(s0_s, v0.xy, 0).xyz;
  r0.y = cmp(r0.x < 0.25);
  if (r0.y != 0) {
    o0.xyz = r4.xyz;
    o0.w = 0;
  }
  if (r0.y == 0) {
    r2.xy = (cb0[28].yy * MOTION_BLUR) * r2.xy;
    r0.y = dot(r2.xy, r2.xy);
    r2.xyzw = asuint(cb0[23].xyzw);
    r2.xyzw = float4(0.5,0.5,-0.5,-0.5) + r2.xyzw;
    r2.xyzw = cb0[0].zwzw * r2.xyzw;
    r3.zw = -r3.xy;
    r3.xyzw = cb0[0].zwzw * r3.xyzw;
    r1.y = 0.400000006 * r0.x;
    r0.y = cmp(r1.y < r0.y);
    if (r0.y != 0) {
      r1.z = -r1.x;
      r5.xyzw = float4(0,0,1,1) + r1.xxzz;
      r5.xyzw = float4(0.25,0.25,0.25,0.25) * r5.xyzw;
      r5.xyzw = r5.xyzw * r3.xyzw + v0.xyxy;
      r5.xyzw = max(r5.xyzw, r2.xyxy);
      r5.xyzw = min(r5.xyzw, r2.zwzw);
      r6.xyz = t0.SampleLevel(s0_s, r5.xy, 0).xyz;
      r5.xyz = t0.SampleLevel(s0_s, r5.zw, 0).xyz;
      r5.xyz = r6.xyz + r5.xyz;
      r6.xyzw = float4(1,1,2,2) + r1.xxzz;
      r6.xyzw = float4(0.25,0.25,0.25,0.25) * r6.xyzw;
      r6.xyzw = r6.xyzw * r3.xyzw + v0.xyxy;
      r6.xyzw = max(r6.xyzw, r2.xyxy);
      r6.xyzw = min(r6.xyzw, r2.zwzw);
      r7.xyz = t0.SampleLevel(s0_s, r6.xy, 0).xyz;
      r5.xyz = r7.xyz + r5.xyz;
      r6.xyz = t0.SampleLevel(s0_s, r6.zw, 0).xyz;
      r5.xyz = r6.xyz + r5.xyz;
      r6.xyzw = float4(2,2,3,3) + r1.xxzz;
      r6.xyzw = float4(0.25,0.25,0.25,0.25) * r6.xyzw;
      r6.xyzw = r6.xyzw * r3.xyzw + v0.xyxy;
      r6.xyzw = max(r6.xyzw, r2.xyxy);
      r6.xyzw = min(r6.xyzw, r2.zwzw);
      r7.xyz = t0.SampleLevel(s0_s, r6.xy, 0).xyz;
      r5.xyz = r7.xyz + r5.xyz;
      r6.xyz = t0.SampleLevel(s0_s, r6.zw, 0).xyz;
      r5.xyz = r6.xyz + r5.xyz;
      r6.xyzw = float4(3,3,4,4) + r1.xxzz;
      r6.xyzw = float4(0.25,0.25,0.25,0.25) * r6.xyzw;
      r6.xyzw = r6.xyzw * r3.xyzw + v0.xyxy;
      r6.xyzw = max(r6.xyzw, r2.xyxy);
      r6.xyzw = min(r6.xyzw, r2.zwzw);
      r7.xyz = t0.SampleLevel(s0_s, r6.xy, 0).xyz;
      r5.xyz = r7.xyz + r5.xyz;
      r6.xyz = t0.SampleLevel(s0_s, r6.zw, 0).xyz;
      r5.xyz = r6.xyz + r5.xyz;
      r5.xyz = float3(0.125,0.125,0.125) * r5.xyz;
    } else {
      r0.x = rsqrt(r0.x);
      r0.x = 4 * r0.x;
      r0.yz = t1.SampleLevel(s1_s, r0.zw, 0).xz;
      r0.y = (cb0[28].y * MOTION_BLUR) * r0.y;
      r6.y = min((cb0[28].w * MOTION_BLUR), r0.y);
      r1.w = -r1.x;
      r7.xyzw = float4(0,0,1,1) + r1.xxww;
      r7.xyzw = float4(0.25,0.25,0.25,0.25) * r7.xyzw;
      r7.xyzw = r7.xyzw * r3.xyzw + v0.xyxy;
      r7.xyzw = max(r7.xyzw, r2.xyxy);
      r7.xyzw = min(r7.xyzw, r2.zwzw);
      r8.xyzw = r7.xyzw * cb0[30].xyxy + cb0[30].zwzw;
      r0.yw = t1.SampleLevel(s1_s, r8.xy, 0).xz;
      r9.xyz = t0.SampleLevel(s0_s, r7.xy, 0).xyz;
      r0.y = (cb0[28].y * MOTION_BLUR) * r0.y;
      r6.x = min((cb0[28].w * MOTION_BLUR), r0.y);
      r0.y = r0.w + -r0.z;
      r1.yz = saturate(r0.yy * float2(1,-1) + float2(0.5,0.5));
      r7.xy = saturate(r6.yx * r0.xx);
      r0.y = dot(r1.yz, r7.xy);
      r1.yz = t1.SampleLevel(s1_s, r8.zw, 0).xz;
      r7.xyz = t0.SampleLevel(s0_s, r7.zw, 0).xyz;
      r1.y = (cb0[28].y * MOTION_BLUR) * r1.y;
      r6.z = min((cb0[28].w * MOTION_BLUR), r1.y);
      r1.y = r1.z + -r0.z;
      r8.xy = saturate(r1.yy * float2(1,-1) + float2(0.5,0.5));
      r8.zw = saturate(r6.yz * r0.xx);
      r1.y = dot(r8.xy, r8.zw);
      r0.w = cmp(r1.z < r0.w);
      r1.z = cmp(r6.x < r6.z);
      r4.w = r0.w ? r1.z : 0;
      r4.w = r4.w ? r1.y : r0.y;
      r0.w = (int)r0.w | (int)r1.z;
      r0.y = r0.w ? r1.y : r0.y;
      r7.xyz = r0.yyy * r7.xyz;
      r7.xyz = r4.www * r9.xyz + r7.xyz;
      r0.y = r4.w + r0.y;
      r8.xyzw = float4(1,1,2,2) + r1.xxww;
      r9.xyzw = float4(0.25,0.25,0.25,0.25) * r8.xyzw;
      r9.xyzw = r9.xyzw * r3.xyzw + v0.xyxy;
      r9.xyzw = max(r9.xyzw, r2.xyxy);
      r9.xyzw = min(r9.xyzw, r2.zwzw);
      r10.xyzw = r9.xyzw * cb0[30].xyxy + cb0[30].zwzw;
      r1.yz = t1.SampleLevel(s1_s, r10.xy, 0).xz;
      r8.xzw = t0.SampleLevel(s0_s, r9.xy, 0).xyz;
      r0.w = (cb0[28].y * MOTION_BLUR) * r1.y;
      r6.w = min((cb0[28].w * MOTION_BLUR), r0.w);
      r0.w = r1.z + -r0.z;
      r9.xy = saturate(r0.ww * float2(1,-1) + float2(0.5,0.5));
      r10.xy = saturate(r0.xx * r6.yw + -r1.xx);
      r0.w = dot(r9.xy, r10.xy);
      r9.xy = t1.SampleLevel(s1_s, r10.zw, 0).xz;
      r10.xyz = t0.SampleLevel(s0_s, r9.zw, 0).xyz;
      r1.y = (cb0[28].y * MOTION_BLUR) * r9.x;
      r6.x = min((cb0[28].w * MOTION_BLUR), r1.y);
      r1.y = r9.y + -r0.z;
      r9.xz = saturate(r1.yy * float2(1,-1) + float2(0.5,0.5));
      r11.xy = saturate(r0.xx * r6.yx + -r1.xx);
      r1.y = dot(r9.xz, r11.xy);
      r1.z = cmp(r9.y < r1.z);
      r4.w = cmp(r6.w < r6.x);
      r5.w = r1.z ? r4.w : 0;
      r5.w = r5.w ? r1.y : r0.w;
      r1.z = (int)r1.z | (int)r4.w;
      r0.w = r1.z ? r1.y : r0.w;
      r9.xyz = r0.www * r10.xyz;
      r8.xzw = r5.www * r8.xzw + r9.xyz;
      r7.xyz = r8.xzw + r7.xyz;
      r0.w = r5.w + r0.w;
      r0.y = r0.y + r0.w;
      r9.xyzw = float4(2,2,3,3) + r1.xxww;
      r10.xyzw = float4(0.25,0.25,0.25,0.25) * r9.xyzw;
      r10.xyzw = r10.xyzw * r3.xyzw + v0.xyxy;
      r10.xyzw = max(r10.xyzw, r2.xyxy);
      r10.xyzw = min(r10.xyzw, r2.zwzw);
      r11.xyzw = r10.xyzw * cb0[30].xyxy + cb0[30].zwzw;
      r1.yz = t1.SampleLevel(s1_s, r11.xy, 0).xz;
      r8.xzw = t0.SampleLevel(s0_s, r10.xy, 0).xyz;
      r0.w = (cb0[28].y * MOTION_BLUR) * r1.y;
      r6.z = min((cb0[28].w * MOTION_BLUR), r0.w);
      r0.w = r1.z + -r0.z;
      r9.xz = saturate(r0.ww * float2(1,-1) + float2(0.5,0.5));
      r10.xy = saturate(r0.xx * r6.yz + -r8.yy);
      r0.w = dot(r9.xz, r10.xy);
      r9.xz = t1.SampleLevel(s1_s, r11.zw, 0).xz;
      r10.xyz = t0.SampleLevel(s0_s, r10.zw, 0).xyz;
      r1.y = (cb0[28].y * MOTION_BLUR) * r9.x;
      r6.w = min((cb0[28].w * MOTION_BLUR), r1.y);
      r1.y = r9.z + -r0.z;
      r9.xw = saturate(r1.yy * float2(1,-1) + float2(0.5,0.5));
      r11.xy = saturate(r0.xx * r6.yw + -r8.yy);
      r1.y = dot(r9.xw, r11.xy);
      r1.z = cmp(r9.z < r1.z);
      r4.w = cmp(r6.z < r6.w);
      r5.w = r1.z ? r4.w : 0;
      r5.w = r5.w ? r1.y : r0.w;
      r1.z = (int)r1.z | (int)r4.w;
      r0.w = r1.z ? r1.y : r0.w;
      r9.xzw = r0.www * r10.xyz;
      r8.xyz = r5.www * r8.xzw + r9.xzw;
      r7.xyz = r8.xyz + r7.xyz;
      r0.w = r5.w + r0.w;
      r0.y = r0.y + r0.w;
      r1.xyzw = float4(3,3,4,4) + r1.xxww;
      r1.xyzw = float4(0.25,0.25,0.25,0.25) * r1.xyzw;
      r1.xyzw = r1.xyzw * r3.xyzw + v0.xyxy;
      r1.xyzw = max(r1.xyzw, r2.xyxy);
      r1.xyzw = min(r1.xyzw, r2.zwzw);
      r2.xyzw = r1.xyzw * cb0[30].xyxy + cb0[30].zwzw;
      r2.xy = t1.SampleLevel(s1_s, r2.xy, 0).xz;
      r3.xyz = t0.SampleLevel(s0_s, r1.xy, 0).xyz;
      r0.w = (cb0[28].y * MOTION_BLUR) * r2.x;
      r6.x = min((cb0[28].w * MOTION_BLUR), r0.w);
      r0.w = r2.y + -r0.z;
      r1.xy = saturate(r0.ww * float2(1,-1) + float2(0.5,0.5));
      r8.xy = saturate(r0.xx * r6.yx + -r9.yy);
      r0.w = dot(r1.xy, r8.xy);
      r1.xy = t1.SampleLevel(s1_s, r2.zw, 0).xz;
      r2.xzw = t0.SampleLevel(s0_s, r1.zw, 0).xyz;
      r1.x = (cb0[28].y * MOTION_BLUR) * r1.x;
      r6.z = min((cb0[28].w * MOTION_BLUR), r1.x);
      r0.z = r1.y + -r0.z;
      r1.xz = saturate(r0.zz * float2(1,-1) + float2(0.5,0.5));
      r0.xz = saturate(r0.xx * r6.yz + -r9.yy);
      r0.x = dot(r1.xz, r0.xz);
      r0.z = cmp(r1.y < r2.y);
      r1.x = cmp(r6.x < r6.z);
      r1.y = r0.z ? r1.x : 0;
      r1.y = r1.y ? r0.x : r0.w;
      r0.z = (int)r0.z | (int)r1.x;
      r0.x = r0.z ? r0.x : r0.w;
      r1.xzw = r0.xxx * r2.xzw;
      r1.xzw = r1.yyy * r3.xyz + r1.xzw;
      r1.xzw = r7.xyz + r1.xzw;
      r0.x = r1.y + r0.x;
      r0.x = r0.y + r0.x;
      r0.x = -r0.x * 0.125 + 1;
      r0.xyz = r0.xxx * r4.xyz;
      r5.xyz = r1.xzw * float3(0.125,0.125,0.125) + r0.xyz;
    }
    o0.xyz = r5.xyz;
    o0.w = 0;
  }
  return;
}