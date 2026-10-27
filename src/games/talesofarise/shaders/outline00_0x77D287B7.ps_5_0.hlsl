// Tales of Arise character outlines (Bandai: material IDs 11 / 12; darkens where normals or IDs change).
// From 0x77D287B7.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps).
// Below 100% DLSS (SI.filter_scale = render / output, from the DLSS addon), the offset is cb0[62].x output pixels;
// below one render pixel (the G-buffer is point-sampled) it stays one pixel and the line fades by the same
// ratio: as dark on average as at 100% instead of a thick black line.
#include "../shared.h"

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[124];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[66];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t3.Sample(s3_s, v0.xy).xyzw;
  r1.x = t1.SampleLevel(s1_s, v0.xy, 0).w;
  r1.x = 255 * r1.x;
  r1.x = round(r1.x);
  r1.x = (uint)r1.x;
  r1.x = (int)r1.x & 15;
  r1.y = cmp((uint)r1.x < 11);
  if (r1.y != 0) {
    o0.xyzw = r0.xyzw;
    return;
  } else {
    r1.y = t2.SampleLevel(s2_s, v0.xy, 0).w;
  }
  r2.xyzw = cb0[65].xxxx * r0.xyzw;
  const float outline_scaled = cb0[62].x * SI.filter_scale;
  const float outline_offset = max(outline_scaled, min(cb0[62].x, 1.f));
  r3.xw = outline_offset;
  r3.yz = float2(0,-0);
  r1.zw = -r3.zw * cb0[0].zw + v0.xy;
  r4.xy = r3.xy * cb0[0].zw + v0.xy;
  r4.zw = float2(-0.5,-0.5) + cb1[122].xy;
  r4.zw = r4.zw / cb1[123].xy;
  r1.zw = min(r4.zw, r1.zw);
  r4.xy = min(r4.xy, r4.zw);
  r5.x = t1.SampleLevel(s1_s, r1.zw, 0).w;
  r5.x = 255 * r5.x;
  r5.x = round(r5.x);
  r5.x = (uint)r5.x;
  r5.x = (int)r5.x & 15;
  r5.y = t1.SampleLevel(s1_s, r4.xy, 0).w;
  r5.y = 255 * r5.y;
  r5.y = round(r5.y);
  r5.y = (uint)r5.y;
  r5.y = (int)r5.y & 15;
  r5.z = cmp((int)r1.x == 11);
  if (r5.z != 0) {
    r5.zw = cmp((int2)r5.xy == int2(12,12));
    r5.z = (int)r5.w | (int)r5.z;
    r5.w = -1;
  } else {
    r6.x = cmp((int)r1.x == 12);
    if (r6.x != 0) {
      r6.xy = cmp((int2)r5.xy == int2(11,11));
      r5.z = (int)r6.y | (int)r6.x;
      r5.w = -1;
    } else {
      r5.z = 0;
    }
  }
  r5.z = (int)r5.w & (int)r5.z;
  if (r5.z == 0) {
    r6.xyz = t0.SampleLevel(s0_s, v0.xy, 0).xyz;
    r6.xyz = r6.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r5.z = dot(r6.xyz, r6.xyz);
    r5.z = rsqrt(r5.z);
    r6.xyz = r6.xyz * r5.zzz;
    r3.zw = r3.zw * cb0[0].zw + v0.xy;
    r3.xy = -r3.xy * cb0[0].zw + v0.xy;
    r3.xyzw = min(r3.xyzw, r4.zwzw);
    r7.xyz = t0.SampleLevel(s0_s, r1.zw, 0).xyz;
    r7.xyz = r7.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r1.z = dot(r7.xyz, r7.xyz);
    r1.z = rsqrt(r1.z);
    r7.xyz = r7.xyz * r1.zzz;
    r1.z = t1.SampleLevel(s1_s, r3.zw, 0).w;
    r1.z = 255 * r1.z;
    r1.z = round(r1.z);
    r1.z = (uint)r1.z;
    r1.z = (int)r1.z & 15;
    r1.w = t1.SampleLevel(s1_s, r3.xy, 0).w;
    r1.w = 255 * r1.w;
    r1.w = round(r1.w);
    r1.w = (uint)r1.w;
    r1.w = (int)r1.w & 15;
    r3.xyz = t0.SampleLevel(s0_s, r4.xy, 0).xyz;
    r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r3.w = dot(r3.xyz, r3.xyz);
    r3.w = rsqrt(r3.w);
    r3.xyz = r3.xyz * r3.www;
    r3.w = dot(r6.xyz, r7.xyz);
    r3.w = 1 + -abs(r3.w);
    r3.x = dot(r6.xyz, r3.xyz);
    r3.x = r3.w + -abs(r3.x);
    r3.x = 1 + r3.x;
    r3.x = saturate(r3.x + r3.x);
    r3.x = rsqrt(r3.x);
    r3.x = 1 / r3.x;
    r3.y = (int)r1.x + (int)-r5.y;
    r3.y = max((int)r3.y, (int)-r3.y);
    r1.x = (int)r1.x + (int)-r5.x;
    r1.x = max((int)r1.x, (int)-r1.x);
    r1.x = (int)r1.x + (int)r3.y;
    r1.zw = cmp((int2)r1.zw < int2(11,11));
    r1.z = (int)r1.z | (int)r1.w;
    r1.x = (uint)r1.x;
    r1.x = r1.z ? 1 : r1.x;
    r1.x = r3.x + r1.x;
    r1.x = min(1, r1.x);
  } else {
    r1.x = 0;
  }
  r1.z = saturate(dot(float3(0.200000003,0.699999988,0.100000001), r2.xyz));
  r3.xy = r1.zz * float2(6.19999981,6.19999981) + float2(0.5,1.70000005);
  r1.w = r3.x * r1.z;
  r1.z = r1.z * r3.y + 0.0599999987;
  r1.z = r1.w / r1.z;
  r1.z = r1.z * r1.z;
  r3.xyz = -cb0[64].yzx + cb0[63].yzx;
  r3.xyz = r1.zzz * r3.xyz + cb0[64].yzx;
  r1.z = r1.z * 0.700000048 + 1.70000005;
  r4.xyz = r0.xyz * cb0[65].xxx + r3.xxx;
  r4.xyz = log2(r4.xyz);
  r4.xyz = r4.xyz * r1.zzz;
  r4.xyz = exp2(r4.xyz);
  r4.xyz = r4.xyz * r3.yyy;
  r1.x = r3.z * r1.x;
  r1.x = r1.x * r1.y;
  r1.x *= saturate(outline_scaled / max(outline_offset, 1e-6f));
  r4.w = 1;
  r0.xyzw = -r0.xyzw * cb0[65].xxxx + r4.xyzw;
  // Outlines: their opacity x the slider, at most opaque (above that the blend went negative).
  o0.xyzw = saturate(r1.x * SI.outlines) * r0.xyzw + r2.xyzw;
  return;
}