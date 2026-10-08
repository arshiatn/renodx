// Expeditions: Rome fog composite: exponential height fog plus volumetric fog, directional in-scattering variant.
// From 0xBCD37CAF.ps_5_0.cso (3Dmigoto). Volumetric Fog slider (HDR only, experimental) scales
// the volumetric part; height fog, light shaft occlusion and the rest stay stock.
#include "../shared.h"
Texture2D<float4> t2 : register(t2);

Texture3D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[8];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[186];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[1];
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

  r0.x = t0.SampleLevel(s1_s, v0.xy, 0).x;
  r0.y = r0.x * cb1[61].x + cb1[61].y;
  r0.z = r0.x * cb1[61].z + -cb1[61].w;
  r0.z = 1 / r0.z;
  r0.y = r0.y + r0.z;
  r1.xyz = v1.xyz * r0.yyy;
  r0.z = dot(r1.xyz, r1.xyz);
  r0.z = sqrt(r0.z);
  r0.w = dot(r1.xyz, cb1[56].xyz);
  r0.z = r0.z / r0.w;
  r0.z = cb1[185].z * r0.z;
  r0.z = max(0, r0.z);
  r2.z = min(cb2[0].z, cb1[63].z);
  r0.w = cb1[63].z + -r2.z;
  r1.w = v1.z * r0.y + r0.w;
  r0.w = dot(r1.xyw, r1.xyw);
  r1.z = rsqrt(r0.w);
  r0.w = r1.z * r0.w;
  r0.z = max(cb2[0].w, r0.z);
  r3.x = dot(cb1[123].xyz, float3(1,1,1));
  r3.x = cmp(0 < r3.x);
  if (r3.x != 0) {
    r2.xy = cb1[63].xy;
    r2.w = 1;
    r2.x = dot(cb1[123].xyzw, r2.xyzw);
    r2.y = dot(r1.xyw, cb1[123].xyz);
    r2.x = -r2.x / r2.y;
    r2.y = cmp(0 < r2.x);
    r2.w = cmp(r2.x < 1);
    r2.y = r2.w ? r2.y : 0;
    r2.x = r2.x * r0.w;
    r2.x = max(r2.x, r0.z);
    r0.z = r2.y ? r2.x : r0.z;
  }
  r2.x = cmp(0 < r0.z);
  r2.y = r0.z * r1.z;
  r2.z = r2.y * r1.w + r2.z;
  r2.y = -r2.y * r1.w + r1.w;
  r0.z = -r0.z * r1.z + 1;
  r0.z = r0.z * r0.w;
  r2.w = -cb2[3].y + r2.z;
  r2.w = cb2[0].y * r2.w;
  r2.w = max(-127, r2.w);
  r2.w = exp2(-r2.w);
  r2.w = cb2[3].x * r2.w;
  r2.z = -cb2[1].w + r2.z;
  r2.z = cb2[1].y * r2.z;
  r2.z = max(-127, r2.z);
  r2.z = exp2(-r2.z);
  r2.z = cb2[1].z * r2.z;
  r2.w = r2.x ? r2.w : cb2[0].x;
  r2.z = r2.x ? r2.z : cb2[1].x;
  r0.z = r2.x ? r0.z : r0.w;
  r2.x = r2.x ? r2.y : r1.w;
  r2.y = cb2[0].y * r2.x;
  r2.y = max(-127, r2.y);
  r3.x = exp2(-r2.y);
  r3.x = 1 + -r3.x;
  r3.x = r3.x / r2.y;
  r3.y = -r2.y * 0.240226507 + 0.693147182;
  r2.y = cmp(0.00999999978 < abs(r2.y));
  r2.y = r2.y ? r3.x : r3.y;
  r2.x = cb2[1].y * r2.x;
  r2.x = max(-127, r2.x);
  r3.x = exp2(-r2.x);
  r3.x = 1 + -r3.x;
  r3.x = r3.x / r2.x;
  r3.y = -r2.x * 0.240226507 + 0.693147182;
  r2.x = cmp(0.00999999978 < abs(r2.x));
  r2.x = r2.x ? r3.x : r3.y;
  r2.x = r2.z * r2.x;
  r2.x = r2.w * r2.y + r2.x;
  r2.y = r2.x * r0.z;
  r2.z = cmp(cb2[4].w >= 0);
  if (r2.z != 0) {
    r1.xyz = r1.xyw * r1.zzz;
    r1.x = saturate(dot(r1.xyz, cb2[4].xyz));
    r1.x = log2(r1.x);
    r1.x = cb2[5].w * r1.x;
    r1.x = exp2(r1.x);
    r1.xyz = cb2[5].xyz * r1.xxx;
    r0.z = -cb2[4].w + r0.z;
    r0.z = max(0, r0.z);
    r0.z = r2.x * r0.z;
    r0.z = exp2(-r0.z);
    r0.z = min(1, r0.z);
    r0.z = 1 + -r0.z;
    r1.xyz = r1.xyz * r0.zzz;
  } else {
    r1.xyz = float3(0,0,0);
  }
  r0.z = exp2(-r2.y);
  r0.z = min(1, r0.z);
  r1.w = max(cb2[2].w, r0.z);
  r0.z = cmp(0 < cb2[3].w);
  r0.w = cmp(cb2[3].w < r0.w);
  r0.z = r0.w ? r0.z : 0;
  r1.xyzw = r0.zzzz ? float4(0,0,0,1) : r1.xyzw;
  r0.z = 1 + -r1.w;
  r1.xyz = cb2[2].xyz * r0.zzz + r1.xyz;
  r0.z = cmp(0 < cb2[7].w);
  if (r0.z != 0) {
    r0.y = r0.y * cb1[184].x + cb1[184].y;
    r0.y = log2(r0.y);
    r0.y = cb1[184].z * r0.y;
    r2.z = cb1[183].z * r0.y;
    r0.yz = -cb1[125].xy + v2.xy;
    r2.xy = cb1[185].xy * r0.yz;
    r2.xyzw = t1.SampleLevel(s0_s, r2.xyz, 0).xyzw;
    if (HDR >= 1.f) r2.xyzw = ScaleVolumetricFog(r2.xyzw, SI.volumetric_fog);
  } else {
    r2.xyzw = float4(0,0,0,1);
  }
  r0.yzw = r1.xyz * r2.www + r2.xyz;
  r1.w = r2.w * r1.w;
  r2.x = t2.Sample(s2_s, v0.xy).x;
  r0.yzw = r2.xxx * r0.yzw;
  r1.xyz = cb1[129].yyy * r0.yzw;
  r0.y = cmp(0 < cb0[0].x);
  r0.x = cmp(r0.x == 0.000000);
  r0.x = r0.x ? r0.y : 0;
  o0.xyzw = r0.xxxx ? float4(0,0,0,1) : r1.xyzw;
  return;
}
