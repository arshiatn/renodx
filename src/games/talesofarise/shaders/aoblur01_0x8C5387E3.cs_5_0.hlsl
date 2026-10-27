// Tales of Arise ambient occlusion blur (vertical + temporal, Bandai, compute permutation 01). From 0x8C5387E3.cs_5_0.cso
// (3Dmigoto, 2026-10-09 dumps). AoBlurWeight (shared.h): the AO halo fix.
#include "../shared.h"

groupshared struct { float val[2]; } g0[144];
Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[123];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[35];
}




// 3Dmigoto declarations
#define cmp -


RWTexture2D<float4> u0 : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID, uint3 vThreadIDInGroup : SV_GroupThreadID)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (uint2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r0.xy = cb0[34].xy * r0.xy;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r1.yzw = cb1[49].xyz * r1.yyy;
  r1.xyz = r1.xxx * cb1[48].xyz + r1.yzw;
  r1.xyz = cb1[50].xyz + r1.xyz;
  r2.xy = t0.SampleLevel(s0_s, r0.xy, 0).xy;
  r0.w = mad((int)vThreadIDInGroup.y, 12, (int)vThreadIDInGroup.x);
  r0.w = (int)r0.w + 26;
  g0[r0.w].val[0/4] = r2.x;
  g0[r0.w].val[0/4+1] = r2.y;
  r1.w = cmp(1 >= (uint)vThreadIDInGroup.y);
  r3.x = cmp((uint)vThreadIDInGroup.y >= 6);
  r3.x = (int)r1.w | (int)r3.x;
  if (r3.x != 0) {
    r3.xy = r1.ww ? float2(-2,-24) : float2(2,24);
    r0.z = r3.x * cb0[34].y + r0.y;
    r0.xy = t0.SampleLevel(s0_s, r0.xz, 0).xy;
    r0.z = (int)r0.w + (int)r3.y;
    g0[r0.z].val[0/4] = r0.x;
    g0[r0.z].val[0/4+1] = r0.y;
  }
  GroupMemoryBarrierWithGroupSync();
  r0.x = r2.x;
  r0.yz = float2(1,2);
  while (true) {
    r1.w = cmp(0 >= (uint)r0.z);
    if (r1.w != 0) break;
    r1.w = (int)r0.z * 12;
    r1.w = (int)r0.w + (int)-r1.w;
    r3.x = mad((int)r0.z, 12, (int)r0.w);
    r3.y = g0[r1.w].val[0/4];
    r3.z = g0[r1.w].val[0/4+1];
    r3.w = g0[r3.x].val[0/4+1];
    r3.x = g0[r3.x].val[0/4];
    r1.w = r3.z + -r2.y;
    r1.w = 1 + abs(r1.w);
    r1.w = 1 / r1.w;
    r1.w = AoBlurWeight(r1.w, r2.y);
    r1.w = min(1, r1.w);
    r3.y = r1.w * r3.y + r0.x;
    r1.w = r1.w + r0.y;
    r3.z = r3.w + -r2.y;
    r3.z = 1 + abs(r3.z);
    r3.z = 1 / r3.z;
    r3.z = AoBlurWeight(r3.z, r2.y);
    r3.z = min(1, r3.z);
    r0.x = r3.z * r3.x + r3.y;
    r0.y = r3.z + r1.w;
    r0.z = (int)r0.z + -1;
  }
  r0.x = r0.x / r0.y;
  r0.yzw = r2.yyy * r1.xyz + -cb1[62].xyz;
  r3.xyz = cb1[105].xyz + r0.yzw;
  r4.xyz = cb1[84].xyw * r3.yyy;
  r3.xyw = r3.xxx * cb1[83].xyw + r4.xyz;
  r3.xyz = r3.zzz * cb1[85].xyw + r3.xyw;
  r3.xyz = cb1[86].xyw + r3.xyz;
  r3.xy = r3.xy / r3.zz;
  r3.zw = r3.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  r3.xy = cb0[29].xy * r3.xy + cb0[29].zw;
  r1.w = t1.SampleLevel(s1_s, r3.zw, 0).x;
  r3.x = t2.SampleLevel(s1_s, r3.xy, 0).x;
  r1.xyz = r3.xxx * r1.xyz + -cb1[105].xyz;
  r0.yzw = -r1.xyz + r0.yzw;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = 9.99999975e-005 * r0.y;
  r0.y = min(1, r0.y);
  r0.y = 1 + -r0.y;
  r4.xyzw = cb1[122].zwzw * float4(1,-1.5,1.5,1) + r3.zwzw;
  r0.z = t1.SampleLevel(s1_s, r4.xy, 0).x;
  r0.w = t1.SampleLevel(s1_s, r4.zw, 0).x;
  r1.xy = cb1[122].zw * float2(-1.5,0.300000012) + r3.zw;
  r1.x = t1.SampleLevel(s1_s, r1.xy, 0).x;
  r0.z = r1.w + r0.z;
  r0.z = r0.z + r0.w;
  r0.z = r0.z + r1.x;
  r0.w = -r0.z * 0.25 + r0.x;
  r0.w = 1 + -abs(r0.w);
  r0.w = r0.w * r0.w;
  r0.y = r0.y * r0.w;
  r0.y = 0.5 * r0.y;
  r0.z = r0.z * 0.25 + -r0.x;
  r2.x = r0.y * r0.z + r0.x;
  r2.zw = float2(0,0);
u0[vThreadID.xy] = r2.xyzw;
  return;
}