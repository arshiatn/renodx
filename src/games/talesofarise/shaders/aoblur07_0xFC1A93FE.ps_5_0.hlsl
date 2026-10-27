// Tales of Arise ambient occlusion blur (Bandai, pixel shader permutation 07). From 0xFC1A93FE.ps_5_0.cso
// (3Dmigoto, 2026-10-09 dumps). AoBlurWeight (shared.h): the AO halo fix.
#include "../shared.h"

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[106];
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
  float4 v2 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = t0.Sample(s0_s, v0.xy).xy;
  r0.z = 1.5 * cb0[34].y;
  r1.xz = v0.xx;
  r2.x = r0.x;
  r2.y = 1;
  r0.w = 2;
  while (true) {
    r2.z = cmp(0 >= (uint)r0.w);
    if (r2.z != 0) break;
    r2.z = (uint)r0.w;
    r1.y = -r0.z * r2.z + v0.y;
    r1.w = r0.z * r2.z + v0.y;
    r2.zw = t0.Sample(s0_s, r1.xy).xy;
    r1.yw = t0.Sample(s0_s, r1.zw).xy;
    r2.w = r2.w + -r0.y;
    r2.w = 1 + abs(r2.w);
    r2.w = 1 / r2.w;
    r2.w = AoBlurWeight(r2.w, r0.y);
    r2.w = min(1, r2.w);
    r2.z = r2.w * r2.z + r2.x;
    r2.w = r2.y + r2.w;
    r1.w = r1.w + -r0.y;
    r1.w = 1 + abs(r1.w);
    r1.w = 1 / r1.w;
    r1.w = AoBlurWeight(r1.w, r0.y);
    r1.w = min(1, r1.w);
    r2.x = r1.w * r1.y + r2.z;
    r2.y = r2.w + r1.w;
    r0.w = (int)r0.w + -1;
  }
  r0.x = r2.x / r2.y;
  r1.xyz = r0.yyy * v1.xyz + -cb1[62].xyz;
  r2.xyz = cb1[105].xyz + r1.xyz;
  r3.xyz = cb1[84].xyw * r2.yyy;
  r2.xyw = r2.xxx * cb1[83].xyw + r3.xyz;
  r2.xyz = r2.zzz * cb1[85].xyw + r2.xyw;
  r2.xyz = cb1[86].xyw + r2.xyz;
  r2.xyzw = r2.xyxy / r2.zzzz;
  r2.xyzw = r2.xyzw * float4(0.5,-0.5,0.5,-0.5) + float4(0.5,0.5,0.5,0.5);
  r0.zw = t1.Sample(s1_s, r2.zw).xy;
  r3.xyz = r0.www * v1.xyz + -cb1[105].xyz;
  r1.xyz = -r3.xyz + r1.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = 9.99999975e-005 * r0.w;
  r0.w = min(1, r0.w);
  r0.w = 1 + -r0.w;
  r1.xy = cb0[34].xy * float2(1,-1.5) + r2.zw;
  r1.x = t1.Sample(s1_s, r1.xy).x;
  r2.xyzw = cb0[34].xyxy * float4(1.5,1,-1.5,0.300000012) + r2.xyzw;
  r1.y = t1.Sample(s1_s, r2.xy).x;
  r1.z = t1.Sample(s1_s, r2.zw).x;
  r0.z = r1.x + r0.z;
  r0.z = r0.z + r1.y;
  r0.z = r0.z + r1.z;
  r1.x = -r0.z * 0.25 + r0.x;
  r1.x = 1 + -abs(r1.x);
  r1.x = r1.x * r1.x;
  r0.w = r1.x * r0.w;
  r0.w = 0.699999988 * r0.w;
  r0.z = r0.z * 0.25 + -r0.x;
  o0.x = r0.w * r0.z + r0.x;
  o0.y = r0.y;
  o0.zw = float2(0,0);
  return;
}