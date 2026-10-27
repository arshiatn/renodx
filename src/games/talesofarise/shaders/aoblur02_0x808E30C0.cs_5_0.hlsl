// Tales of Arise ambient occlusion blur (vertical + temporal, Bandai, compute permutation 02). From 0x808E30C0.cs_5_0.cso
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


RWTexture2D<float4> u0 : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (uint2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r1.xy = cb0[34].xy * r0.xy;
  r0.xz = r1.xy * float2(2,-2) + float2(-1,1);
  r2.xyz = cb1[49].xyz * r0.zzz;
  r0.xzw = r0.xxx * cb1[48].xyz + r2.xyz;
  r0.xzw = cb1[50].xyz + r0.xzw;
  r2.xy = t0.SampleLevel(s0_s, r1.xy, 0).xy;
  r1.y = 1.5 * cb0[34].y;
  r3.x = r2.x;
  r3.yz = float2(1,2);
  while (true) {
    r3.w = cmp(0 >= (uint)r3.z);
    if (r3.w != 0) break;
    r3.w = (uint)r3.z;
    r3.w = r3.w * r1.y;
    r1.z = r0.y * cb0[34].y + -r3.w;
    r1.w = r0.y * cb0[34].y + r3.w;
    r4.xy = t0.SampleLevel(s0_s, r1.xz, 0).xy;
    r1.zw = t0.SampleLevel(s0_s, r1.xw, 0).xy;
    r3.w = r4.y + -r2.y;
    r3.w = 1 + abs(r3.w);
    r3.w = 1 / r3.w;
    r3.w = AoBlurWeight(r3.w, r2.y);
    r3.w = min(1, r3.w);
    r4.x = r3.w * r4.x + r3.x;
    r3.w = r3.y + r3.w;
    r1.w = r1.w + -r2.y;
    r1.w = 1 + abs(r1.w);
    r1.w = 1 / r1.w;
    r1.w = AoBlurWeight(r1.w, r2.y);
    r1.w = min(1, r1.w);
    r3.x = r1.w * r1.z + r4.x;
    r3.y = r3.w + r1.w;
    r3.z = (int)r3.z + -1;
  }
  r0.y = r3.x / r3.y;
  r1.xyz = r2.yyy * r0.xzw + -cb1[62].xyz;
  r3.xyz = cb1[105].xyz + r1.xyz;
  r4.xyz = cb1[84].xyw * r3.yyy;
  r3.xyw = r3.xxx * cb1[83].xyw + r4.xyz;
  r3.xyz = r3.zzz * cb1[85].xyw + r3.xyw;
  r3.xyz = cb1[86].xyw + r3.xyz;
  r3.xyzw = r3.xyxy / r3.zzzz;
  r3.xyzw = r3.xyzw * float4(0.5,-0.5,0.5,-0.5) + float4(0.5,0.5,0.5,0.5);
  r4.xy = t1.SampleLevel(s1_s, r3.zw, 0).xy;
  r0.xzw = r4.yyy * r0.xzw + -cb1[105].xyz;
  r0.xzw = r1.xyz + -r0.xzw;
  r0.x = dot(r0.xzw, r0.xzw);
  r0.x = 9.99999975e-005 * r0.x;
  r0.x = min(1, r0.x);
  r0.x = 1 + -r0.x;
  r0.zw = cb0[34].xy * float2(1,-1.5) + r3.zw;
  r0.z = t1.SampleLevel(s1_s, r0.zw, 0).x;
  r1.xyzw = cb0[34].xyxy * float4(1.5,1,-1.5,0.300000012) + r3.xyzw;
  r0.w = t1.SampleLevel(s1_s, r1.xy, 0).x;
  r1.x = t1.SampleLevel(s1_s, r1.zw, 0).x;
  r0.z = r4.x + r0.z;
  r0.z = r0.z + r0.w;
  r0.z = r0.z + r1.x;
  r0.w = -r0.z * 0.25 + r0.y;
  r0.w = 1 + -abs(r0.w);
  r0.w = r0.w * r0.w;
  r0.x = r0.x * r0.w;
  r0.x = 0.699999988 * r0.x;
  r0.z = r0.z * 0.25 + -r0.y;
  r2.x = r0.x * r0.z + r0.y;
  r2.zw = float2(0,0);
u0[vThreadID.xy] = r2.xyzw;
  return;
}