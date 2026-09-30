//LOCAL LIGHT

#include "../shared.h"

cbuffer _Globals : register(b0)
{
  float3 lightColor : packoffset(c0);
  float2 lightInfo : packoffset(c1);
}

cbuffer PerView : register(b12)
{
  row_major float4x4 global_View : packoffset(c0);
  row_major float4x4 global_Projection : packoffset(c4);
  row_major float4x4 global_ViewProjection : packoffset(c8);
  float4 global_ViewPos : packoffset(c12);
  float4 global_ViewInfo : packoffset(c13);
}

Texture2D<float4> GBuffer1 : register(t0);
Texture2D<float4> GBuffer2 : register(t1);
Texture2D<float4> GBuffer3 : register(t2);
Texture2D<float4> GBuffer4 : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TexCoord0,
  float3 v2 : TexCoord1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  // --- NEW CODE: Grab slider values ---
  float customRadius = lightInfo.x;
  float customIntensity = lightInfo.y * LOCAL_LIGHT_INTENSITY;
  // ------------------------------------

  r0.x = global_ViewInfo.x / v1.z;
  r0.xyz = v1.xyz * r0.xxx;
  r1.xy = (int2)v0.xy;
  r1.zw = float2(0,0);
  r0.w = GBuffer1.Load(r1.xyw).x;
  r2.xyz = r0.xyz * r0.www + -v2.xyz;
  r2.x = dot(r2.xyz, r2.xyz);
  r2.x = sqrt(r2.x);
  
  // Replaced lightInfo.x
  r2.x = customRadius + -r2.x; 
  r2.x = cmp(r2.x < 0);
  if (r2.x != 0) discard;
  
  r2.xyz = r0.xyz * r0.www;
  r0.xyz = -r0.xyz * r0.www + v2.xyz;
  r0.w = dot(-r2.xyz, -r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.w = dot(r0.xyz, r0.xyz);
  r3.x = rsqrt(r2.w);
  r2.w = sqrt(r2.w);
  
  // Replaced lightInfo.x
  r2.w = r2.w / customRadius; 
  r2.w = saturate(1 + -r2.w);
  
  r0.xyz = r3.xxx * r0.xyz;
  r2.xyz = -r2.xyz * r0.www + r0.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r3.z = 1;
  r4.xy = GBuffer2.Load(r1.xyw).xy;
  r3.xy = r4.xy * float2(3.55539989,3.55539989) + float2(-1.77769995,-1.77769995);
  r0.w = dot(r3.xyz, r3.xyz);
  r0.w = 2 / r0.w;
  r3.xy = r0.ww * r3.xy;
  r3.z = -1 + r0.w;
  r0.w = dot(r2.xyz, r3.xyz);
  r0.x = saturate(dot(r0.xyz, r3.xyz));
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
  
  // Replaced lightInfo.y
  r0.x = customIntensity * r0.x; 
  r0.x = r0.x * r2.w;
  
  r2.xyz = lightColor.xyz * r0.xxx;
  r0.x = r0.z ? r0.y : 0;
  
  // Replaced lightInfo.y
  r0.x = customIntensity * r0.x; 
  r0.x = r0.x * r2.w;
  
  r0.xyz = lightColor.xyz * r0.xxx;
  r0.xyz = r3.xyz * r0.xyz;
  r0.xyz = r1.xyz * r2.xyz + r0.xyz;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.w = 1;
  return;
}