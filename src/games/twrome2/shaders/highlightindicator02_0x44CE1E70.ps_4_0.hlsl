// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 23 23:23:09 2026

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
  float4 g_viewport_dimensions : packoffset(c29);
  float4 g_camera_temp0 : packoffset(c30);
  float4 g_camera_temp1 : packoffset(c31);
  float4 g_camera_temp2 : packoffset(c32);
  float4 g_clip_rect : packoffset(c33);
  float g_hide_foliage : packoffset(c34);
}

cbuffer lighting_VS_PS : register(b1)
{
  float3 sun_direction : packoffset(c0);
  float3 sun_colour : packoffset(c1);
  float3 ambient_cube_lr[2] : packoffset(c2);
  float3 ambient_cube_tb[2] : packoffset(c4);
  float3 ambient_cube_fb[2] : packoffset(c6);
  float3 g_deep_water_colour : packoffset(c8);
  float3 g_shallow_water_colour : packoffset(c9);
  float3 g_sea_bed_light_scatter : packoffset(c10);
  float g_refraction_light_scatter : packoffset(c10.w);
  float g_hdr_on : packoffset(c11);
}

cbuffer grid_spline_PS : register(b2)
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

  r0.xy = camera_position.xz + -v2.xz;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = sqrt(r0.x);
  r0.x = -r0.x * 0.000500000024 + 1;
  r0.x = max(0, r0.x);
  r1.xyzw = s_diffuse.Sample(s_diffuse_s, v1.xy).xyzw;
  o0.w = r1.w * r0.x;
  r0.x = cmp(0 < g_hdr_on);
  r0.x = r0.x ? 0.00400000019 : 1;
  r0.yzw = g_spline_colour.xyz * sun_colour.xyz;
  r0.xyz = r0.yzw * r0.xxx;
  o0.xyz = float3(0.5, 0.5, 0.5) * r0.xyz;
  o0.xyz *= SI.someindicators;
  return;
}