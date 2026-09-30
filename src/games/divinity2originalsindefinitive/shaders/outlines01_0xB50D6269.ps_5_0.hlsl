// ---- Created with 3Dmigoto v1.3.16 on Thu Sep 10 03:26:30 2026

SamplerState LinearClampSampler_s : register(s0);
Texture2D<float4> Base : register(t0);
Texture2D<float4> Base2 : register(t1);
Texture2D<float4> Base3 : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  const float outline_nits = DOS2_CORRECT_HDR ? SI.graphics_white_nits : 300.f;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r1.xyz = Base.Load(r0.xyw).xyz;
  r0.x = Base3.Load(r0.xyz).w;
  r0.yzw = log2(r1.xyz);
  r0.yzw = float3(0.0126833133,0.0126833133,0.0126833133) * r0.yzw;
  r0.yzw = exp2(r0.yzw);
  r1.xyz = float3(-0.8359375,-0.8359375,-0.8359375) + r0.yzw;
  r0.yzw = -r0.yzw * float3(18.6875,18.6875,18.6875) + float3(18.8515625,18.8515625,18.8515625);
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r0.yzw = r1.xyz / r0.yzw;
  r0.yzw = log2(r0.yzw);
  r0.yzw = float3(6.27739477,6.27739477,6.27739477) * r0.yzw;
  r0.yzw = exp2(r0.yzw);
  r0.yzw = float3(10000,10000,10000) * r0.yzw;
  r1.xyzw = Base2.Sample(LinearClampSampler_s, v1.xy).xyzw;
  r2.xyz = float3(0.0549999997,0.0549999997,0.0549999997) + r1.xyz;
  r2.xyz = float3(0.947867334,0.947867334,0.947867334) * r2.xyz;
  r2.xyz = log2(r2.xyz);
  r2.xyz = float3(2.4000001,2.4000001,2.4000001) * r2.xyz;
  r2.xyz = exp2(r2.xyz);
  r3.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r1.xyz;
  r1.xyz = cmp(float3(0.0404499993,0.0404499993,0.0404499993) >= r1.xyz);
  r1.xyz = r1.xyz ? r3.xyz : r2.xyz;
  r2.x = dot(float3(0.627399981,0.329299986,0.0432999991), r1.xyz);
  r2.y = dot(float3(0.0691,0.919499993,0.0114000002), r1.xyz);
  r2.z = dot(float3(0.0164000001,0.0879999995,0.895600021), r1.xyz);

  // r1.xyz = r2.xyz * float3(300,300,300) + -r0.yzw;
  r1.xyz = r2.xyz * outline_nits + -r0.yzw;

  r2.w = ceil(r0.x);
  r0.x = 1 + -r0.x;
  r0.x = r0.x * r2.w;
  r1.w = saturate(-r2.w + r1.w);
  r0.yzw = r1.www * r1.xyz + r0.yzw;
  r0.x = ceil(r0.x);
  r0.x = 0.150000006 * r0.x;

  // r1.xyz = r2.xyz * float3(300,300,300) + -r0.yzw;
  r1.xyz = r2.xyz * outline_nits + -r0.yzw;

  r0.xyz = r0.xxx * r1.xyz + r0.yzw;
  r0.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) * r0.xyz;
  r0.xyz = log2(r0.xyz);
  r0.xyz = float3(0.159301758,0.159301758,0.159301758) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r1.xyz = r0.xyz * float3(18.8515625,18.8515625,18.8515625) + float3(0.8359375,0.8359375,0.8359375);
  r0.xyz = r0.xyz * float3(18.6875,18.6875,18.6875) + float3(1,1,1);
  r0.xyz = r1.xyz / r0.xyz;
  r0.xyz = log2(r0.xyz);
  r0.xyz = float3(78.84375,78.84375,78.84375) * r0.xyz;
  o0.xyz = exp2(r0.xyz);
  o0.w = 1;
  return;
}