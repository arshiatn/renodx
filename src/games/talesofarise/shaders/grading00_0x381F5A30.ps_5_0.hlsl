// Tales of Arise stage grading and sharpening (Bandai, before the TAA: a 5-tap sharpen 0.5 pixels out, strength
// cb0[34].x). From 0x381F5A30.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps).
// SI.game_sharpening 0 (DLSS addon: the game's sharpening off under DLAA / DLSS) keeps the grading only. Below 100%
// DLSS, SI.filter_scale (render / output) spaces the taps in output pixels.
#include "../shared.h"

Texture2D<float4> t3 : register(t3);

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
  float4 cb0[35];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t1.Sample(s1_s, v0.xy).xyzw;
  r1.x = t0.SampleLevel(s0_s, v0.xy, 0).w;
  r1.x = 255 * r1.x;
  r1.x = round(r1.x);
  r1.x = (uint)r1.x;
  r1.x = (int)r1.x & 15;
  r1.y = t2.Sample(s2_s, v0.xy).w;
  r1.z = log2(r1.y);
  r1.z = 0.529999971 * r1.z;
  r1.z = exp2(r1.z);
  r1.z = min(0.600000024, r1.z);
  r1.y = r1.y + r1.z;
  r1.y = min(0.680000007, r1.y);
  r1.z = cmp(0.00999999978 < r1.y);
  r1.w = saturate(dot(float3(0.200000003,0.699999988,0.100000001), r0.xyz));
  r2.x = 1.5 + -cb0[32].z;
  r2.x = r1.w * r2.x + cb0[32].z;
  r2.yzw = max(float3(0,0,0), r0.xyz);
  r3.xyz = cb0[32].xxx + r2.yzw;
  r3.xyz = log2(r3.xyz);
  r3.xyz = r3.xyz * r2.xxx;
  r3.xyz = exp2(r3.xyz);
  r2.x = 1.5 + -cb0[33].z;
  r2.x = r1.w * r2.x + cb0[33].z;
  r2.yzw = cb0[33].xxx + r2.yzw;
  r2.yzw = log2(r2.yzw);
  r2.xyz = r2.xxx * r2.yzw;
  r2.xyz = exp2(r2.xyz);
  r2.xyz = cb0[33].yyy * r2.xyz;
  r1.w = 5 * r1.w;
  r1.w = min(1, r1.w);
  r2.w = r1.w * r1.w;
  r1.w = r2.w * r1.w;
  r3.xyz = cb0[32].yyy * r3.xyz + -r2.xyz;
  r2.xyz = r1.www * r3.xyz + r2.xyz;
  r2.w = 1;
  r2.xyzw = r2.xyzw + -r0.xyzw;
  r2.xyzw = r1.yyyy * r2.xyzw + r0.xyzw;
  r2.xyzw = r1.zzzz ? r2.xyzw : r0.xyzw;
  r0.w = cmp((int)r1.x == 6);
  r1.xw = float2(0.5,0.5) * SI.filter_scale * cb0[0].zw;
  r1.yz = float2(0,0);
  r3.xyzw = v0.xyxy + -r1.xyzw;
  r1.xyzw = v0.xyxy + r1.xyzw;
  r4.xy = float2(-0.5,-0.5) + cb1[122].xy;
  r4.xy = r4.xy / cb1[123].xy;
  r3.xyzw = min(r4.xyxy, r3.xyzw);
  r1.xyzw = min(r4.xyxy, r1.xyzw);
  r4.xyz = t1.Sample(s1_s, r3.xy).xyz;
  r3.xyz = t1.Sample(s1_s, r3.zw).xyz;
  r5.xyz = t1.Sample(s1_s, r1.xy).xyz;
  r1.xyz = t1.Sample(s1_s, r1.zw).xyz;
  if (r0.w != 0) {
    r6.xyzw = t3.Sample(s1_s, v0.xy).xyzw;
    r6.xyz = r6.xyz * r6.www;
    o0.xyz = r6.xyz * float3(1.29999995,1.29999995,1.29999995) + r2.xyz;
    o0.w = r2.w;
    return;
  }
  r0.xyz = r0.xyz * float3(4,4,4) + r2.xyz;
  r0.xyz = r0.xyz + -r4.xyz;
  r0.xyz = r0.xyz + -r3.xyz;
  r0.xyz = r0.xyz + -r5.xyz;
  r0.xyz = r0.xyz + -r1.xyz;
  r1.xyz = cmp(r0.xyz >= float3(0,0,0));
  r1.xyz = r1.xyz ? float3(1,1,1) : 0;
  r3.xyz = float3(0.800000012,0.800000012,0.800000012) * r2.xyz;
  r0.xyz = -r2.xyz * float3(0.800000012,0.800000012,0.800000012) + r0.xyz;
  r0.xyz = r1.xyz * r0.xyz + r3.xyz;
  r0.xyz = r0.xyz + -r2.xyz;
  o0.xyz = (cb0[34].x * SI.game_sharpening) * r0.xyz + r2.xyz;
  o0.w = 1;
  return;
}