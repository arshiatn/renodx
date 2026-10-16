// sun.hlsl ps30_main: the sun disc. HDR: x Sun.
#include "../shared.h"

cbuffer sun_VS : register(b0)
{
  float4x4 g_world : packoffset(c0);
  float4x4 g_view : packoffset(c4);
  float4x4 g_projection : packoffset(c8);
  float g_sun_scale : packoffset(c12);
  float g_sun_brightness_factor : packoffset(c12.y);
  float3 g_sun_colour : packoffset(c13);
}

SamplerState s_diffuse_s : register(s0);
Texture2D<float4> t_diffuse : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t_diffuse.Sample(s_diffuse_s, v1.xy).xyzw;
  r0.xyz = g_sun_colour.xyz * r0.xyz;
  o0.w = r0.w;
  o0.xyz = g_sun_brightness_factor * r0.xyz * ((HDR >= 1.f) ? SI.sun : 1.f);
  return;
}