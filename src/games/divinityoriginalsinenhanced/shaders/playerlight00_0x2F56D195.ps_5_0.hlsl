//PLAYER LIGHT

cbuffer _Globals : register(b0)
{
  row_major float3x3 invViewMatrix : packoffset(c0);
  float3 lightColor : packoffset(c3);
  float2 lightInfo : packoffset(c4);
}

cbuffer PerView : register(b12)
{
  row_major float4x4 global_View : packoffset(c0);
  row_major float4x4 global_Projection : packoffset(c4);
  row_major float4x4 global_ViewProjection : packoffset(c8);
  float4 global_ViewPos : packoffset(c12);
  float4 global_ViewInfo : packoffset(c13);
}

SamplerState LinearSampler_s : register(s0);
Texture2D<float4> GBuffer1 : register(t0);
Texture2D<float4> GBuffer2 : register(t1);
Texture2D<float4> GBuffer3 : register(t2);
Texture2D<float4> GBuffer4 : register(t3);
TextureCube<float4> Shadow : register(t4);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

void main(
    float4 v0: SV_Position0,
    float4 v1: TexCoord0,
    float3 v2: TexCoord1,
    out float4 o0: SV_Target0)
{
  float4 r0, r1, r2, r3, r4, r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  float customRadius = lightInfo.x;
  float customIntensity = lightInfo.y * PLAYER_LIGHT_INTENSITY;

  r0.x = global_ViewInfo.x / v1.z;
  r0.xyz = v1.xyz * r0.xxx;
  r1.xy = (int2)v0.xy;
  r1.zw = float2(0, 0);
  r0.w = GBuffer1.Load(r1.xyw).x;
  r2.xyz = r0.xyz * r0.www + -v2.xyz;
  r2.w = dot(r2.xyz, r2.xyz);
  r2.w = sqrt(r2.w);
  r2.w = customRadius + -r2.w;
  r2.w = cmp(r2.w < 0);
  if (r2.w != 0) discard;
  r3.xyz = r0.xyz * r0.www;
  r0.xyz = -r0.xyz * r0.www + v2.xyz;
  r0.w = dot(-r3.xyz, -r3.xyz);
  r0.w = rsqrt(r0.w);
  r2.w = dot(r0.xyz, r0.xyz);
  r3.w = rsqrt(r2.w);
  r2.w = sqrt(r2.w);
  r2.w = r2.w / customRadius;
  r2.w = saturate(1 + -r2.w);
  r0.xyz = r3.www * r0.xyz;
  r3.xyz = -r3.xyz * r0.www + r0.xyz;
  r0.w = dot(r3.xyz, r3.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = r3.xyz * r0.www;
  r4.z = 1;
  r5.xy = GBuffer2.Load(r1.xyw).xy;
  r4.xy = r5.xy * float2(3.55539989, 3.55539989) + float2(-1.77769995, -1.77769995);
  r0.w = dot(r4.xyz, r4.xyz);
  r0.w = 2 / r0.w;
  r4.xy = r0.ww * r4.xy;
  r4.z = -1 + r0.w;
  r0.w = dot(r3.xyz, r4.xyz);
  r0.x = saturate(dot(r0.xyz, r4.xyz));
  r0.y = max(9.99999975e-005, abs(r0.w));
  r0.y = log2(r0.y);
  r3.xyzw = GBuffer4.Load(r1.xyz).xyzw;
  r1.xyz = GBuffer3.Load(r1.xyw).xyz;
  r0.z = 255 * r3.w;
  r0.y = r0.z * r0.y;
  r0.y = exp2(r0.y);
  r0.z = 4 * r0.x;
  r0.z = min(1, r0.z);
  r0.y = r0.y * r0.z;
  r0.z = cmp(0 < r0.x);
  r0.x = customIntensity * r0.x;
  r0.x = r0.x * r2.w;
  r4.xyz = lightColor.xyz * r0.xxx;
  r0.x = r0.z ? r0.y : 0;
  r0.x = customIntensity * r0.x;
  r0.x = r0.x * r2.w;
  r0.xyz = lightColor.xyz * r0.xxx;
  r0.xyz = r3.xyz * r0.xyz;
  r0.xyz = r1.xyz * r4.xyz + r0.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz);
  r1.x = dot(invViewMatrix._m00_m01_m02, r2.xyz);
  r1.y = dot(invViewMatrix._m10_m11_m12, r2.xyz);
  r1.z = dot(invViewMatrix._m20_m21_m22, r2.xyz);
  r1.w = dot(r1.xyz, r1.xyz);
  r2.x = rsqrt(r1.w);
  r1.w = sqrt(r1.w);
  r1.xyz = r2.xxx * r1.xyz;
  r1.xy = Shadow.Sample(LinearSampler_s, r1.xyz).xy;
  r1.z = r1.w + -r1.x;
  r1.w = cmp(r1.x >= r1.w);
  r1.x = -r1.x * r1.x + r1.y;
  r1.x = max(1.00000001e-007, r1.x);
  r1.y = r1.w ? 1.000000 : 0;
  r1.z = r1.z * r1.z + r1.x;
  r1.x = r1.x / r1.z;
  r1.x = -0.400000006 + r1.x;
  r1.x = saturate(1.66666663 * r1.x);
  r1.x = max(r1.y, r1.x);
  r0.w = 1;
  o0.xyzw = r1.xxxx * r0.xyzw;
  return;
}