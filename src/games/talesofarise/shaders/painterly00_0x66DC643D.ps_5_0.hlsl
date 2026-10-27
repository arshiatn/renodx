// Tales of Arise painterly filter (Bandai: picks the closer of each tap pair up to two radii away, then sharpens with
// four diagonal taps 0.75 pixels away). From 0x66DC643D.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps).
// Below 100% DLSS, SI.filter_scale (render / output, from the DLSS addon) spaces the taps in output pixels: the
// same size on screen as at 100% (in render pixels it grew as the scale dropped).
// SI.game_sharpening 0 (DLSS addon: the game's sharpening off under DLAA / DLSS) drops the sharpening.
#include "../shared.h"

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[58];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[29];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = t0.SampleLevel(s0_s, v0.xy, 0).x;
  r0.y = r0.x * cb1[57].z + -cb1[57].w;
  r0.x = r0.x * cb1[57].x + cb1[57].y;
  r0.y = 1 / r0.y;
  r0.x = r0.x + r0.y;
  r0.x = saturate(0.00066666666 * r0.x);
  r0.xy = r0.xx * float2(-0.800000012,-0.199999988) + float2(1,0.349999994);
  r0.xz = cb0[27].xy * r0.xx * SI.filter_scale;
  r1.xw = cb0[0].zw * r0.xz;
  r1.z = -r1.x;
  r1.y = -2 * r1.w;
  r2.xyzw = -r1.xyzw * float4(-1,1,1,-1) + v0.xyxy;
  r0.xzw = t1.Sample(s1_s, r2.xy).xyz;
  r2.xyz = t1.Sample(s1_s, r2.zw).xyz;
  r3.xyzw = v0.xyxy + r1.zyxy;
  r4.xw = float2(1,-1) * r1.zw;
  r5.xyz = t1.Sample(s1_s, r3.xy).xyz;
  r3.xyz = t1.Sample(s1_s, r3.zw).xyz;
  r1.z = dot(r5.xzy, float3(0.25,0.25,0.5));
  r6.xyzw = t1.Sample(s1_s, v0.xy).xyzw;
  r2.w = dot(r6.xyz, float3(0.200000003,0.699999988,0.100000001));
  r1.z = -r2.w + r1.z;
  r3.w = saturate(r2.w);
  r3.w = 5 * r3.w;
  r3.w = max(9.99999975e-006, r3.w);
  r4.z = abs(r1.z) / r3.w;
  r4.z = 1 + -r4.z;
  r4.z = max(0, r4.z);
  r4.z = r4.z * r4.z;
  r5.w = r4.z * r4.z;
  r4.z = r5.w * r4.z;
  r7.w = 0.800000012 * r4.z;
  r8.xyz = r7.www * r0.xzw;
  r0.x = dot(r0.xwz, float3(0.25,0.25,0.5));
  r0.x = r0.x + -r2.w;
  r0.x = cmp(abs(r1.z) < abs(r0.x));
  r7.xyz = r7.www * r5.xyz;
  r8.w = r7.w;
  r5.xyzw = r0.xxxx ? r7.xyzw : r8.xyzw;
  r0.x = r5.w + r5.w;
  r0.zw = r1.xw * float2(-2,-2) + v0.xy;
  r7.xyz = t1.Sample(s1_s, r0.zw).xyz;
  r0.z = dot(r7.xzy, float3(0.25,0.25,0.5));
  r8.xy = float2(-2,-2) * r1.wx;
  r8.z = 0;
  r9.xyzw = v0.xyxy + -r8.yxyz;
  r10.xyz = t1.Sample(s1_s, r9.xy).xyz;
  r9.xyz = t1.Sample(s1_s, r9.zw).xyz;
  r0.w = dot(r10.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.w = cmp(abs(r0.z) < abs(r0.w));
  r0.z = abs(r0.z) / r3.w;
  r0.z = 1 + -r0.z;
  r0.z = max(0, r0.z);
  r0.z = r0.z * r0.z;
  r1.z = r0.z * r0.z;
  r11.w = r1.z * r0.z;
  r11.xyz = r11.www * r7.xyz;
  r7.xyz = r11.www * r10.xyz;
  r7.w = r11.w;
  r7.xyzw = r0.wwww ? r11.xyzw : r7.xyzw;
  r0.z = r7.w + r7.w;
  r7.xyz = r0.zzz * r7.xyz + r6.xyz;
  r0.z = r7.w * 2 + 1;
  r0.z = r5.w * 2 + r0.z;
  r5.xyz = r0.xxx * r5.xyz + r7.xyz;
  r8.w = r4.w;
  r7.xyzw = v0.xyxy + -r8.zxzw;
  r10.xyz = t1.Sample(s1_s, r7.xy).xyz;
  r7.xyz = t1.Sample(s1_s, r7.zw).xyz;
  r0.x = dot(r10.xzy, float3(0.25,0.25,0.5));
  r11.xyzw = v0.xyxy + r8.zxyz;
  r12.xyzw = v0.xyxy + r8.ywzw;
  r4.zw = v0.xy + -r8.yw;
  r8.xyz = t1.Sample(s1_s, r4.zw).xyz;
  r13.xyz = t1.Sample(s1_s, r11.xy).xyz;
  r11.xyz = t1.Sample(s1_s, r11.zw).xyz;
  r0.w = dot(r13.xzy, float3(0.25,0.25,0.5));
  r0.xw = r0.xw + -r2.ww;
  r0.x = cmp(abs(r0.w) < abs(r0.x));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.z = r0.w * r0.w;
  r0.w = r1.z * r0.w;
  r14.w = 0.5 * r0.w;
  r10.xyz = r14.www * r10.xyz;
  r14.xyz = r14.www * r13.xyz;
  r10.w = r14.w;
  r10.xyzw = r0.xxxx ? r14.xyzw : r10.xyzw;
  r0.x = r10.w + r10.w;
  r5.xyz = r0.xxx * r10.xyz + r5.xyz;
  r0.x = r10.w * 2 + r0.z;
  r10.xyzw = v0.xyxy + -r1.xyxw;
  r13.xyz = t1.Sample(s1_s, r10.xy).xyz;
  r10.xyz = t1.Sample(s1_s, r10.zw).xyz;
  r0.z = dot(r13.xzy, float3(0.25,0.25,0.5));
  r0.w = dot(r3.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.z = r0.w * r0.w;
  r0.w = r1.z * r0.w;
  r14.w = 0.800000012 * r0.w;
  r13.xyz = r14.www * r13.xyz;
  r14.xyz = r14.www * r3.xyz;
  r13.w = r14.w;
  r13.xyzw = r0.zzzz ? r14.xyzw : r13.xyzw;
  r0.z = r13.w + r13.w;
  r3.xyz = r0.zzz * r13.xyz + r5.xyz;
  r0.x = r13.w * 2 + r0.x;
  r5.xyzw = -r1.xyxw * float4(2,1,2,-1) + v0.xyxy;
  r13.xyzw = r1.xyxw * float4(2,1,1,-1) + v0.xyxy;
  r14.xyz = t1.Sample(s1_s, r5.xy).xyz;
  r5.xyz = t1.Sample(s1_s, r5.zw).xyz;
  r0.z = dot(r14.xzy, float3(0.25,0.25,0.5));
  r15.xyz = t1.Sample(s1_s, r13.xy).xyz;
  r13.xyz = t1.Sample(s1_s, r13.zw).xyz;
  r0.w = dot(r15.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.y = r0.w * r0.w;
  r16.w = r1.y * r0.w;
  r14.xyz = r16.www * r14.xyz;
  r16.xyz = r16.www * r15.xyz;
  r14.w = r16.w;
  r14.xyzw = r0.zzzz ? r16.xyzw : r14.xyzw;
  r0.z = r14.w + r14.w;
  r3.xyz = r0.zzz * r14.xyz + r3.xyz;
  r0.x = r14.w * 2 + r0.x;
  r0.z = dot(r8.xzy, float3(0.25,0.25,0.5));
  r14.xyz = t1.Sample(s1_s, r12.xy).xyz;
  r12.xyz = t1.Sample(s1_s, r12.zw).xyz;
  r0.w = dot(r14.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.y = r0.w * r0.w;
  r0.w = r1.y * r0.w;
  r15.w = 0.800000012 * r0.w;
  r8.xyz = r15.www * r8.xyz;
  r15.xyz = r15.www * r14.xyz;
  r8.w = r15.w;
  r8.xyzw = r0.zzzz ? r15.xyzw : r8.xyzw;
  r0.z = r8.w + r8.w;
  r3.xyz = r0.zzz * r8.xyz + r3.xyz;
  r0.x = r8.w * 2 + r0.x;
  r0.z = dot(r2.xzy, float3(0.25,0.25,0.5));
  r0.w = dot(r10.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.y = r0.w * r0.w;
  r0.w = r1.y * r0.w;
  r8.w = 0.640000045 * r0.w;
  r14.xyz = r8.www * r2.xyz;
  r8.xyz = r8.www * r10.xyz;
  r14.w = r8.w;
  r8.xyzw = r0.zzzz ? r8.xyzw : r14.xyzw;
  r0.z = r8.w + r8.w;
  r2.xyz = r0.zzz * r8.xyz + r3.xyz;
  r0.x = r8.w * 2 + r0.x;
  r0.z = dot(r7.xzy, float3(0.25,0.25,0.5));
  r0.w = dot(r12.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.y = r0.w * r0.w;
  r0.w = r1.y * r0.w;
  r8.w = 0.400000006 * r0.w;
  r8.xyz = r8.www * r12.xyz;
  r7.xyz = r8.www * r7.xyz;
  r7.w = r8.w;
  r7.xyzw = r0.zzzz ? r8.xyzw : r7.xyzw;
  r0.z = r7.w + r7.w;
  r2.xyz = r0.zzz * r7.xyz + r2.xyz;
  r0.x = r7.w * 2 + r0.x;
  r0.zw = -r1.xw * float2(1,-1) + v0.xy;
  r1.xy = r1.xw * float2(2,-1) + v0.xy;
  r1.xyz = t1.Sample(s1_s, r1.xy).xyz;
  r3.xyz = t1.Sample(s1_s, r0.zw).xyz;
  r0.z = dot(r3.xzy, float3(0.25,0.25,0.5));
  r0.w = dot(r13.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.w = r0.w * r0.w;
  r0.w = r1.w * r0.w;
  r7.w = 0.640000045 * r0.w;
  r8.xyz = r7.www * r3.xyz;
  r7.xyz = r7.www * r13.xyz;
  r8.w = r7.w;
  r7.xyzw = r0.zzzz ? r7.xyzw : r8.xyzw;
  r0.z = r7.w + r7.w;
  r2.xyz = r0.zzz * r7.xyz + r2.xyz;
  r0.x = r7.w * 2 + r0.x;
  r0.z = dot(r5.xzy, float3(0.25,0.25,0.5));
  r0.w = dot(r1.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.w = r0.w * r0.w;
  r0.w = r1.w * r0.w;
  r7.w = 0.800000012 * r0.w;
  r5.xyz = r7.www * r5.xyz;
  r7.xyz = r7.www * r1.xyz;
  r5.w = r7.w;
  r1.xyzw = r0.zzzz ? r7.xyzw : r5.xyzw;
  r0.z = r1.w + r1.w;
  r1.xyz = r0.zzz * r1.xyz + r2.xyz;
  r0.x = r1.w * 2 + r0.x;
  r0.z = dot(r9.xzy, float3(0.25,0.25,0.5));
  r0.w = dot(r11.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.w = r0.w * r0.w;
  r5.w = r1.w * r0.w;
  r5.xyz = r5.www * r11.xyz;
  r7.xyz = r5.www * r9.xyz;
  r7.w = r5.w;
  r5.xyzw = r0.zzzz ? r5.xyzw : r7.xyzw;
  r0.z = r5.w + r5.w;
  r1.xyz = r0.zzz * r5.xyz + r1.xyz;
  r0.x = r5.w * 2 + r0.x;
  r4.y = 0;
  r0.zw = v0.xy + -r4.xy;
  r2.xy = v0.xy + r4.xy;
  r2.xyz = t1.Sample(s1_s, r2.xy).xyz;
  r3.xyz = t1.Sample(s1_s, r0.zw).xyz;
  r0.z = dot(r3.xzy, float3(0.25,0.25,0.5));
  r0.w = dot(r2.xzy, float3(0.25,0.25,0.5));
  r0.zw = r0.zw + -r2.ww;
  r0.z = cmp(abs(r0.w) < abs(r0.z));
  r0.w = abs(r0.w) / r3.w;
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r1.w = r0.w * r0.w;
  r0.w = r1.w * r0.w;
  r4.w = 0.800000012 * r0.w;
  r3.xyz = r4.www * r3.xyz;
  r4.xyz = r4.www * r2.xyz;
  r3.w = r4.w;
  r3.xyzw = r0.zzzz ? r4.xyzw : r3.xyzw;
  r0.z = r3.w + r3.w;
  r1.xyz = r0.zzz * r3.xyz + r1.xyz;
  r0.x = r3.w * 2 + r0.x;
  r0.xzw = r1.xyz / r0.xxx;
  r1.xyz = float3(1.20000005,1.20000005,1.20000005) * r0.xzw;
  r1.w = 0;
  r1.xyzw = r1.xyzw + -r6.xyzw;
  r1.xyzw = cb0[28].xxxx * r1.xyzw + r6.xyzw;
  r3.xyzw = cb0[0].zwzw * (float4(0.75,-0.75,-0.75,0.75) * SI.filter_scale) + v0.xyxy;
  r0.xzw = t1.Sample(s1_s, r3.xy).xyz;
  r2.xyz = t1.Sample(s1_s, r3.zw).xyz;
  r3.xyz = r6.xyz + -r0.xzw;
  r0.x = dot(r0.xzw, float3(0.200000003,0.699999988,0.100000001));
  r0.x = r2.w + -r0.x;
  r0.x = min(1, abs(r0.x));
  r0.xzw = r0.xxx * -r3.xyz + r3.xyz;
  r3.xyz = r6.xyz + -r2.xyz;
  r2.x = dot(r2.xyz, float3(0.200000003,0.699999988,0.100000001));
  r2.x = r2.w + -r2.x;
  r2.x = min(1, abs(r2.x));
  r2.xyz = r2.xxx * -r3.xyz + r3.xyz;
  r0.xzw = r2.xyz + r0.xzw;
  r3.xyzw = cb0[0].zwzw * (float4(0.75,0.75,-0.75,-0.75) * SI.filter_scale) + v0.xyxy;
  r2.xyz = t1.Sample(s1_s, r3.xy).xyz;
  r3.xyz = t1.Sample(s1_s, r3.zw).xyz;
  r4.xyz = r6.xyz + -r2.xyz;
  r5.xyz = r6.xyz + -r3.xyz;
  r3.x = dot(r3.xyz, float3(0.200000003,0.699999988,0.100000001));
  r3.x = -r3.x + r2.w;
  r3.x = min(1, abs(r3.x));
  r3.xyz = r3.xxx * -r5.xyz + r5.xyz;
  r2.x = dot(r2.xyz, float3(0.200000003,0.699999988,0.100000001));
  r2.x = r2.w + -r2.x;
  r2.x = min(1, abs(r2.x));
  r2.xyz = r2.xxx * -r4.xyz + r4.xyz;
  r0.xzw = r2.xyz + r0.xzw;
  r0.xzw = r0.xzw + r3.xyz;
  // Painterly: the filtered color blended with the unfiltered pixel (t1, the lit scene), 1 the game's.
  r1.xyz = lerp(t1.Sample(s1_s, v0.xy).xyz, r1.xyz, SI.painterly);
  o0.xyz = (r0.y * SI.game_sharpening) * r0.xzw + r1.xyz;
  o0.w = r1.w;
  o1.xyzw = float4(0,0,0,0);
  return;
}