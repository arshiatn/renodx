// Tales of Arise video (CriWare Sofdec in a UMG material: Y, U, V planes, BT.601 limited), UV region + faded look (80% toward gray, toward 0.1 by distance).
// From 0x5E300F3E.ps_5_0.cso (3Dmigoto). Same output as video00.
#include "../shared.h"

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[2];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[5];
}

#define cmp -

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : ORIGINAL_POSITION0,
  float2 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;

  r0.yz = float2(1,1);
  r0.xw = v4.zw;
  r1.x = cmp(0 < v4.z);
  r0.xy = r1.xx ? r0.xy : r0.zw;
  r0.xy = v4.xy * r0.xy;
  r0.xy = frac(r0.xy);
  r2.xz = r1.xx ? cb0[4].xy : float2(0,1);
  r2.yw = r1.xx ? float2(0,1) : cb0[4].zw;
  r0.zw = r2.zw + -r2.xy;
  r0.xy = r0.xy * r0.zw + r2.xy;
  r0.zw = cmp(v4.zw == float2(0,0));
  r0.z = r0.w ? r0.z : 0;
  r0.xy = r0.zz ? v4.xy : r0.xy;
  r0.xy = float2(0.999506593,0.996787369) * r0.xy;
  r0.z = t2.Sample(s2_s, r0.xy).x;
  r0.z = -0.5 + r0.z;
  r0.zw = float2(1.59602702,-0.812968016) * r0.zz;
  r1.x = t1.Sample(s1_s, r0.xy).x;
  r0.x = t0.Sample(s0_s, r0.xy).x;
  r0.x = -0.0625 + r0.x;
  r0.y = -0.5 + r1.x;
  r0.w = r0.y * -0.391761988 + r0.w;
  r1.x = r0.x * 1.16438305 + r0.z;
  r0.y = 2.01723194 * r0.y;
  r1.z = r0.x * 1.16438305 + r0.y;
  r1.y = r0.x * 1.16438305 + r0.w;
  r0.xyz = max(float3(6.1040002e-005,6.1040002e-005,6.1040002e-005), r1.xyz);
  r1.xyz = r0.xyz * float3(0.947867274,0.947867274,0.947867274) + float3(0.0521326996,0.0521326996,0.0521326996);
  r1.xyz = log2(r1.xyz);
  r1.xyz = float3(2.4000001,2.4000001,2.4000001) * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r2.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r0.xyz;
  r0.xyz = cmp(r0.xyz >= float3(0.0404499993,0.0404499993,0.0404499993));
  r0.xyz = r0.xyz ? r1.xyz : r2.xyz;
  r0.xyz = cb1[1].xyz + r0.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz);
  r1.xyz = v1.xyz * r0.xyz;
  r0.w = dot(float3(0.300000012,0.589999974,0.109999999), r1.xyz);
  r0.xyz = -r0.xyz * v1.xyz + r0.www;
  r0.xyz = r0.xyz * float3(0.800000012,0.800000012,0.800000012) + r1.xyz;
  r1.xyz = float3(-0.100000001,-0.100000001,-0.100000001) + r0.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = sqrt(r0.w);
  r0.w = min(0.800000012, r0.w);
  r1.xyz = float3(0.100000001,0.100000001,0.100000001) + -r0.xyz;
  r0.xyz = r0.www * r1.xyz + r0.xyz;
  r1.xyz = log2(r0.xyz);
  r1.xyz = cb0[3].xxx * r1.xyz;
  r2.xyz = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r2.xyz = exp2(r2.xyz);
  r2.xyz = r2.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  r3.xyz = float3(12.9200001,12.9200001,12.9200001) * r1.xyz;
  r1.xyz = cmp(r1.xyz >= float3(0.00313066994,0.00313066994,0.00313066994));
  r1.xyz = r1.xyz ? r2.xyz : r3.xyz;
  r0.w = cmp(cb0[3].y != 1.000000);
  o0.xyz = r0.www ? r1.xyz : r0.xyz;
  o0.w = v1.w;

  // Stock sRGB-encoded output only: Paper White instead of UI; HDR: Video Highlights.
  if (r0.w != 0) o0.xyz = VideoOutput(o0.xyz, v0.xy);
}