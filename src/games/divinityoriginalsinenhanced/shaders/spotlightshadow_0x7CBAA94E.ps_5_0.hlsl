// ---- Created with 3Dmigoto v1.3.16 on Sun Nov 15 18:24:31 2026
// DeferredSpotLightShadow: Player Light Intensity (the light's intensity lightInfo.y times the row).

#include "../shared.h"

cbuffer _Globals : register(b0)
{
  row_major float4x4 ViewProjMatrix : packoffset(c0);
  float3 lightColor : packoffset(c4);
  float4 lightInfo : packoffset(c5);
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
Texture2D<float4> Shadow : register(t4);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TexCoord0,
  float3 v2 : TexCoord1,
  float3 v3 : TexCoord2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = global_ViewInfo.x / v1.z;
  r0.xyz = v1.xyz * r0.xxx;
  r1.xy = (int2)v0.xy;
  r1.zw = float2(0,0);
  r0.w = GBuffer1.Load(r1.xyw).x;
  r2.xyz = -r0.xyz * r0.www + v2.xyz;
  r2.w = dot(r2.xyz, r2.xyz);
  r3.x = rsqrt(r2.w);
  r2.w = sqrt(r2.w);
  r2.w = r2.w / lightInfo.x;
  r2.w = saturate(1 + -r2.w);
  r2.xyz = r3.xxx * r2.xyz;
  r3.xyz = r0.xyz * r0.www;
  r0.xyz = r0.xyz * r0.www + -v2.xyz;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.y = dot(-r3.xyz, -r3.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = -r3.xyz * r0.yyy + r2.xyz;
  r4.x = dot(r0.yzw, r0.yzw);
  r4.x = rsqrt(r4.x);
  r0.yzw = r4.xxx * r0.yzw;
  r4.z = 1;
  r5.xy = GBuffer2.Load(r1.xyw).xy;
  r4.xy = r5.xy * float2(3.55539989,3.55539989) + float2(-1.77769995,-1.77769995);
  r4.z = dot(r4.xyz, r4.xyz);
  r4.z = 2 / r4.z;
  r5.xy = r4.zz * r4.xy;
  r5.z = -1 + r4.z;
  r0.y = dot(r0.yzw, r5.xyz);
  r0.z = saturate(dot(r2.xyz, r5.xyz));
  r0.y = max(9.99999975e-005, abs(r0.y));
  r0.y = log2(r0.y);
  r4.xyzw = GBuffer4.Load(r1.xyz).xyzw;
  r1.xyz = GBuffer3.Load(r1.xyw).xyz;
  r0.w = 255 * r4.w;
  r0.y = r0.w * r0.y;
  r0.y = exp2(r0.y);
  r0.w = 4 * r0.z;
  r0.w = min(1, r0.w);
  r0.y = r0.y * r0.w;
  r0.w = cmp(0 < r0.z);
  r0.z = (lightInfo.y * PLAYER_LIGHT_INTENSITY) * r0.z;
  r0.y = r0.w ? r0.y : 0;
  r0.y = (lightInfo.y * PLAYER_LIGHT_INTENSITY) * r0.y;
  r0.w = dot(v3.xyz, v3.xyz);
  r0.w = rsqrt(r0.w);
  r5.xyz = v3.xyz * r0.www;
  r0.w = dot(-r2.xyz, r5.xyz);
  r1.w = -lightInfo.z + r0.w;
  r0.w = cmp(lightInfo.z < r0.w);
  r2.x = lightInfo.w + -lightInfo.z;
  r2.x = max(9.99999975e-006, r2.x);
  r1.w = saturate(r1.w / r2.x);
  r1.w = r2.w * r1.w;
  r0.yz = r1.ww * r0.yz;
  r2.xyz = lightColor.xyz * r0.zzz;
  r2.xyz = r0.www ? r2.xyz : 0;
  r5.xyz = lightColor.xyz * r0.yyy;
  r0.yzw = r0.www ? r5.xyz : 0;
  r0.yzw = r4.xyz * r0.yzw;
  r0.yzw = r1.xyz * r2.xyz + r0.yzw;
  r1.xyz = max(float3(0,0,0), r0.yzw);
  r3.w = 1;
  r2.x = dot(ViewProjMatrix._m00_m01_m02_m03, r3.xyzw);
  r2.y = dot(ViewProjMatrix._m10_m11_m12_m13, r3.xyzw);
  r0.y = dot(ViewProjMatrix._m30_m31_m32_m33, r3.xyzw);
  r0.yz = r2.xy / r0.yy;
  r0.yz = Shadow.Sample(LinearSampler_s, r0.yz).xy;
  r0.w = r0.x + -r0.y;
  r0.x = cmp(r0.y >= r0.x);
  r0.y = -r0.y * r0.y + r0.z;
  r0.y = max(1.00000001e-007, r0.y);
  r0.x = r0.x ? 1.000000 : 0;
  r0.z = r0.w * r0.w + r0.y;
  r0.y = r0.y / r0.z;
  r0.y = -0.400000006 + r0.y;
  r0.y = saturate(1.66666663 * r0.y);
  r0.x = max(r0.x, r0.y);
  r1.w = 1;
  o0.xyzw = r1.xyzw * r0.xxxx;
  return;
}