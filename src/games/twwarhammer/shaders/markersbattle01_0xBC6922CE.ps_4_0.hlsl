// 0xBC6922CE (gridspline ps40_main_fire_arc): the ranged units' fire arcs; x 12 with g_hdr_on. Unit/Building Indicator
// Brightness (SI.some_indicators): x the colour in HDR, as Attila's markersbattle01 (gridspline ps30_main_fire_arc).
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
  int g_ssr_enabled : packoffset(c11.y);
  int g_use_spherical_harmonics : packoffset(c11.z);
  float2 g_cloud_shadow_direction : packoffset(c12);
  float g_cloud_shadow_speed : packoffset(c12.z);
  float g_cloud_shadow_scale : packoffset(c12.w);
  float g_cloud_shadow_lerp : packoffset(c13);
  float2 g_noise_uv_shift : packoffset(c13.y);
  float2 g_skin_curvature_scale_bias : packoffset(c14);
  float2 g_skin_translucency_scale_bias : packoffset(c14.z);
  float3 g_skin_blood_colour : packoffset(c15);
  float4 g_world_bounds : packoffset(c16);
  float4 g_playable_area_bounds : packoffset(c17);
}

cbuffer grid_spline_PS : register(b1)
{
  float4 g_spline_colour : packoffset(c0);
  float3 g_mouse_intersected_position : packoffset(c1);
}

SamplerState s_diffuse_s : register(s0);
Texture2D<float4> t_diffuse : register(t0);


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
  r1.xyzw = t_diffuse.Sample(s_diffuse_s, v1.xy).xyzw;
  o0.xyzw = r1.xyzw * r0.xyzw;
  if (HDR >= 1.f) o0.xyz *= SI.some_indicators;  // SDR stays stock
  return;
}
