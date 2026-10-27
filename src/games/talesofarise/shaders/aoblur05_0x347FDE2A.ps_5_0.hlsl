// Tales of Arise ambient occlusion blur (Bandai, pixel shader permutation 05). From 0x347FDE2A.ps_5_0.cso
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


void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  float4 v2 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
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
  o0.x = r2.x / r2.y;
  o0.y = r0.y;
  o0.zw = float2(0,0);
  return;
}