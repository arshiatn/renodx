// ---- Created with 3Dmigoto v1.3.16 on Mon Sep 07 09:44:18 2026

cbuffer _Globals : register(b0)
{
  float2 Params : packoffset(c0);
  float FadeValue : packoffset(c0.z);
}

SamplerState LinearClampSampler_s : register(s0);
Texture2D<float4> Base : register(t0);
Texture3D<float4> LUT : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;
  
  //load
  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.xyz = Base.Load(r0.xyz).xyz;

  // t00 tags only modified HDR LUTs with alpha 0. Its stock SDR/Off LUTs keep
  // alpha 1. RGB interpolation and this pass's output alpha remain unchanged.
  // This identifies the LUT itself, without guessing from Params or UI state.
  bool fixed_hdr_domain = DOS2_CORRECT_HDR && (LUT.Load(int4(0, 0, 0, 0)).w < 0.5f);
  float2 lut_shaper_domain = fixed_hdr_domain ? DOS2_HDR_LUT_SHAPER_DOMAIN : Params.xy;
  r0.w = lut_shaper_domain.y - lut_shaper_domain.x;
  r0.w = 10000 / r0.w;
  r1.x = lut_shaper_domain.x * -r0.w;
  
  //OG CODE:
  // r0.w = Params.y + -Params.x;
  // r0.w = 10000 / r0.w;
  // r1.x = Params.x * -r0.w;

  //rest of the code:
  r0.xyz = r0.xyz * r0.www + r1.xxx;
  r0.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) * r0.xyz;
  r0.xyz = log2(r0.xyz);
  r0.xyz = float3(0.159301758,0.159301758,0.159301758) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r1.xyz = r0.xyz * float3(18.8515625,18.8515625,18.8515625) + float3(0.8359375,0.8359375,0.8359375);
  r0.xyz = r0.xyz * float3(18.6875,18.6875,18.6875) + float3(1,1,1);
  r0.xyz = r1.xyz / r0.xyz;
  r0.xyz = log2(r0.xyz);
  r0.xyz = float3(78.84375,78.84375,78.84375) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r0.xyz = LUT.SampleLevel(LinearClampSampler_s, r0.xyz, 0).xyz;
  r0.w = 1;
  r1.xyzw = float4(0,0,0,1) + -r0.xyzw;
  o0.xyzw = FadeValue * r1.xyzw + r0.xyzw;
  return;
}
