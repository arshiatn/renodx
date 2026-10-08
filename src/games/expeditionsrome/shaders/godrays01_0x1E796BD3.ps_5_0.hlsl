// Expeditions: Rome light shafts (godrays): bloom setup. From 0x1E796BD3.ps_5_0.cso (3Dmigoto).
// Scene bright pass x tint, depth mask, edge and radial fade. The radial blur and the additive
// apply follow, so scaling this output scales the light shafts. Godrays slider (HDR only).
#include "../shared.h"

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[62];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

void main(
  float2 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;

  r0.xyz = t0.SampleLevel(s0_s, v0.xy, 0).xyz;
  r1.xyz = cb0[7].yyy * r0.xyz;
  r0.x = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
  r0.x = max(6.10351999e-005, r0.x);
  r0.yzw = r1.xyz / r0.xxx;
  r0.x = -cb0[8].w + r0.x;
  r0.x = max(0, r0.x);
  r0.xyz = r0.yzw * r0.xxx;
  r0.xyz = cb0[8].xyz * r0.xyz;
  r0.w = t1.SampleLevel(s1_s, v0.xy, 0).x;
  r1.x = r0.w * cb1[61].z + -cb1[61].w;
  r0.w = r0.w * cb1[61].x + cb1[61].y;
  r1.x = 1 / r1.x;
  r0.w = r1.x + r0.w;
  r1.x = 0.5 / cb0[7].x;
  r0.w = -r1.x + r0.w;
  r0.w = saturate(cb0[7].x * r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = r0.xyz + r0.xyz;
  r1.xy = -cb0[5].xy + v0.xy;
  r1.xy = r1.xy / cb0[5].zw;
  r1.zw = float2(1,1) + -r1.xy;
  r0.w = r1.x * r1.z;
  r0.w = r0.w * r1.y;
  r0.w = r0.w * r1.w;
  r0.w = -r0.w * 8 + 1;
  r0.w = r0.w * r0.w;
  r0.w = -r0.w * r0.w + 1;
  r0.xyz = r0.xyz * r0.www;
  r1.xy = -v0.xy * cb0[6].zw + cb0[0].xy;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = sqrt(r0.w);
  r0.w = r0.w + r0.w;
  r0.w = min(1, r0.w);
  r0.w = 1 + -r0.w;
  r0.w = r0.w * r0.w;
  o0.xyz = r0.www * r0.xyz;
  if (HDR >= 1.f) o0.xyz *= SI.godrays;
  o0.w = 1;
}
