// For Godrays and some blurs but I only change the Godrays here


cbuffer _Globals : register(b0)
{
  float PassIndex : packoffset(c0);
  float Power : packoffset(c0.y);
  float RayIntensity : packoffset(c0.z);
  float Threshold : packoffset(c0.w);
}

cbuffer PerFrame : register(b13)
{
  float4x4 global_LightPropertyMatrix : packoffset(c0);
  float4x3 global_FogPropertyMatrix : packoffset(c4);
  float4 global_Data : packoffset(c7);
}

SamplerState PointClampSampler_s : register(s0);
SamplerState LinearClampSampler_s : register(s1);
Texture2D<float4> Base : register(t0);
Texture2D<float4> Base2 : register(t1);
Texture2D<float4> Base3 : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  float2 w1 : TexCoord1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 1 + PassIndex;
  r0.x = 0.055555556 * r0.x;
  r0.yz = w1.xy + -v1.xy;
  r0.w = cmp(PassIndex == 0.000000);
  r1.xyzw = float4(0,0,0,0);
  while (true) {
    r2.x = (int)r1.w;
    r2.y = cmp(r2.x >= 6);
    if (r2.y != 0) break;
    r2.x = r2.x * r0.x;
    r2.xy = r0.yz * r2.xx + v1.xy;
    if (r0.w != 0) {
      r2.z = Base2.Sample(PointClampSampler_s, r2.xy).x;
      r2.z = cmp(r2.z == 1.000000);
      r3.xyz = Base3.Sample(LinearClampSampler_s, r2.xy).xyz;
      if (r2.z != 0) {
        r2.z = dot(float3(0.298999995,0.587000012,0.114), r3.xyz);
        r2.z = cmp(r2.z >= Threshold);
        r2.z = r2.z ? 1.000000 : 0;
        r1.xyz = r2.zzz * r3.xyz + r1.xyz;
      }
    } else {
      r2.xyz = Base3.Sample(LinearClampSampler_s, r2.xy).xyz;
      r1.xyz = r2.xyz + r1.xyz;
    }
    r1.w = (int)r1.w + 1;
  }
  r1.xyz = float3(0.166666672,0.166666672,0.166666672) * r1.xyz;
  r0.x = dot(r0.yz, r0.yz);
  r0.x = sqrt(r0.x);
  r0.x = min(1, r0.x);
  r0.x = 1 + -r0.x;
  r0.x = log2(r0.x);
  r0.x = Power * r0.x;
  r0.x = exp2(r0.x);
  r0.xyz = r1.xyz * r0.xxx;
  r0.xyz = RayIntensity * r0.xyz;
  r1.x = global_LightPropertyMatrix._m20 * r0.x;
  r1.y = global_LightPropertyMatrix._m21 * r0.y;
  r1.z = global_LightPropertyMatrix._m22 * r0.z;

  //GODRAYS BABY
  if (HDR == 1.f) r1.xyz *= GODRAYS;  // 1.0 is stock; no hidden HDR doubling
  

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.xyz = Base.Load(r0.xyz).xyz;
  o0.xyz = r0.xyz + r1.xyz;
  o0.w = 2;
  return;
}