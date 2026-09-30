// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 01 23:43:38 2026

cbuffer lighting_VS_PS : register(b0)
{
  bool g_apply_environment_specular : packoffset(c0);
  float3 sun_direction : packoffset(c0.y);
  float3 sun_colour : packoffset(c1);
  float3 ambient_cube_lr[2] : packoffset(c2);
  float3 ambient_cube_tb[2] : packoffset(c4);
  float3 ambient_cube_fb[2] : packoffset(c6);
  float3 g_deep_water_colour : packoffset(c8);
  float3 g_shallow_water_colour : packoffset(c9);
  float3 g_sea_bed_light_scatter : packoffset(c10);
  float g_refraction_light_scatter : packoffset(c10.w);
  float g_hdr_on : packoffset(c11);
  bool g_ssr_enabled : packoffset(c11.y);
}

cbuffer grid_spline_PS : register(b1)
{
  float4 g_spline_colour : packoffset(c0);
  float3 g_mouse_intersected_position : packoffset(c1);
}

SamplerState s_diffuse_s : register(s0);
Texture2D<float4> s_diffuse : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(0 < g_hdr_on);
  r0.xyz = r0.xxx ? float3(12,12,12) : float3(0.400000006,0.400000006,0.400000006);
  r0.w = 1;
  r0.xyzw = g_spline_colour.xyzw * r0.xyzw;
  r1.xyzw = s_diffuse.Sample(s_diffuse_s, v1.xy).xyzw;
  o0.xyzw = r1.xyzw * r0.xyzw;
  // o0.xyz = saturate(o0.xyz);
  o0.w = saturate(o0.w);
  if (HDR == 1.f) o0.xyz *= SOMEINDICATORS;  // SDR stays stock

  return;
}