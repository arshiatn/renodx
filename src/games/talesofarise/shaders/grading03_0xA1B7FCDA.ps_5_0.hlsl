// Tales of Arise stage grading and sharpening (Bandai, before the TAA: a 5-tap sharpen 0.5 pixels out, strength
// cb0[34].x). From 0xA1B7FCDA.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps).
// SI.game_sharpening 0 (DLSS addon: the game's sharpening off under DLAA / DLSS) keeps the grading only. Below 100%
// DLSS, SI.filter_scale (render / output) spaces the taps in output pixels.
#include "../shared.h"

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

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
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = t0.Sample(s0_s, v0.xy).xyz;
  r1.xyz = max(float3(0,0,0), r0.xyz);
  r2.xyz = cb0[32].xxx + r1.xyz;
  r1.xyz = cb0[33].xxx + r1.xyz;
  r1.xyz = log2(r1.xyz);
  r2.xyz = log2(r2.xyz);
  r0.w = 1.5 + -cb0[32].z;
  r1.w = saturate(dot(float3(0.200000003,0.699999988,0.100000001), r0.xyz));
  r0.w = r1.w * r0.w + cb0[32].z;
  r2.xyz = r0.www * r2.xyz;
  r2.xyz = exp2(r2.xyz);
  r0.w = 1.5 + -cb0[33].z;
  r0.w = r1.w * r0.w + cb0[33].z;
  r1.w = 5 * r1.w;
  r1.w = min(1, r1.w);
  r1.xyz = r0.www * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r1.xyz = cb0[33].yyy * r1.xyz;
  r2.xyz = cb0[32].yyy * r2.xyz + -r1.xyz;
  r0.w = r1.w * r1.w;
  r0.w = r0.w * r1.w;
  r1.xyz = r0.www * r2.xyz + r1.xyz;
  r1.xyz = r1.xyz + -r0.xyz;
  r0.w = t1.Sample(s1_s, v0.xy).w;
  r1.w = log2(r0.w);
  r1.w = 0.529999971 * r1.w;
  r1.w = exp2(r1.w);
  r1.w = min(0.600000024, r1.w);
  r0.w = r1.w + r0.w;
  r0.w = min(0.680000007, r0.w);
  r1.xyz = r0.www * r1.xyz + r0.xyz;
  r0.w = cmp(0.00999999978 < r0.w);
  r1.xyz = r0.www ? r1.xyz : r0.xyz;
  r0.xyz = r0.xyz * float3(4,4,4) + r1.xyz;
  r2.xw = float2(0.5,0.5) * SI.filter_scale * cb0[0].zw;
  r2.yz = float2(0,0);
  r3.xyzw = v0.xyxy + -r2.xyzw;
  r2.xyzw = v0.xyxy + r2.xyzw;
  r4.xyz = t0.Sample(s0_s, r3.xy).xyz;
  r3.xyz = t0.Sample(s0_s, r3.zw).xyz;
  r0.xyz = -r4.xyz + r0.xyz;
  r0.xyz = r0.xyz + -r3.xyz;
  r3.xyz = t0.Sample(s0_s, r2.xy).xyz;
  r2.xyz = t0.Sample(s0_s, r2.zw).xyz;
  r0.xyz = -r3.xyz + r0.xyz;
  r0.xyz = r0.xyz + -r2.xyz;
  r2.xyz = -r1.xyz * float3(0.800000012,0.800000012,0.800000012) + r0.xyz;
  r0.xyz = cmp(r0.xyz >= float3(0,0,0));
  r0.xyz = r0.xyz ? float3(1,1,1) : 0;
  r3.xyz = float3(0.800000012,0.800000012,0.800000012) * r1.xyz;
  r0.xyz = r0.xyz * r2.xyz + r3.xyz;
  r0.xyz = r0.xyz + -r1.xyz;
  o0.xyz = (cb0[34].x * SI.game_sharpening) * r0.xyz + r1.xyz;
  o0.w = 1;
  return;
}