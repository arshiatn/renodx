// Tales of Arise ambient occlusion blur (horizontal, Bandai, compute permutation 00). From 0x8D9AC9A4.cs_5_0.cso
// (3Dmigoto, 2026-10-09 dumps). AoBlurWeight (shared.h): the AO halo fix.
#include "../shared.h"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

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
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (uint2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r1.zw = cb0[34].xy * r0.xy;
  r2.xy = t0.SampleLevel(s0_s, r1.zw, 0).xy;
  r0.y = 1.5 * cb0[34].x;
  r3.x = r2.x;
  r3.y = 1;
  r0.z = 2;
  while (true) {
    r0.w = cmp(0 >= (uint)r0.z);
    if (r0.w != 0) break;
    r0.w = (uint)r0.z;
    r0.w = r0.y * r0.w;
    r1.y = r0.x * cb0[34].x + -r0.w;
    r1.x = r0.x * cb0[34].x + r0.w;
    r1.yz = t0.SampleLevel(s0_s, r1.yw, 0).xy;
    r3.zw = t0.SampleLevel(s0_s, r1.xw, 0).xy;
    r0.w = r1.z + -r2.y;
    r0.w = 1 + abs(r0.w);
    r0.w = 1 / r0.w;
    r0.w = AoBlurWeight(r0.w, r2.y);
    r0.w = min(1, r0.w);
    r1.x = r0.w * r1.y + r3.x;
    r0.w = r3.y + r0.w;
    r1.y = r3.w + -r2.y;
    r1.y = 1 + abs(r1.y);
    r1.y = 1 / r1.y;
    r1.y = AoBlurWeight(r1.y, r2.y);
    r1.y = min(1, r1.y);
    r3.x = r1.y * r3.z + r1.x;
    r3.y = r1.y + r0.w;
    r0.z = (int)r0.z + -1;
  }
  r2.x = r3.x / r3.y;
  r2.zw = float2(0,0);
u0[vThreadID.xy] = r2.xyzw;
  return;
}