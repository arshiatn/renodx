// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 01 23:30:05 2026

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
  float4 g_render_target_dimensions : packoffset(c30);
  float4 g_camera_temp0 : packoffset(c31);
  float4 g_camera_temp1 : packoffset(c32);
  float4 g_camera_temp2 : packoffset(c33);
  float4 g_clip_rect : packoffset(c34);
  int g_num_of_samples : packoffset(c35);
}

cbuffer lighting_VS_PS : register(b1)
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

SamplerState gbuffer_channel_4_sampler_s : register(s0);
SamplerState indicator_texture_sampler_s : register(s1);
Texture2D<float4> gbuffer_channel_4_sampler : register(t0);
Texture2D<float4> indicator_texture_sampler : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = indicator_texture_sampler.Sample(indicator_texture_sampler_s, v1.xy).xyzw;
  r1.x = r0.w * v3.w + -0.00392156886;
  r0.xyzw = v3.xyzw * r0.xyzw;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.xy = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r1.xy;
  r2.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r1.xy, 0).yzxw;
  r2.xy = r1.xy * float2(2,-2) + float2(-1,1);
  r2.w = 1;
  r1.x = dot(r2.xyzw, inv_projection._m02_m12_m22_m32);
  r1.y = dot(r2.xyzw, inv_projection._m03_m13_m23_m33);
  r1.x = r1.x / r1.y;
  r1.y = v4.z / v4.w;
  r1.z = r1.y + -r1.x;
  r1.x = cmp(r1.x < r1.y);
  r1.yz = cmp(r1.zz < float2(0.5,5));
  r1.z = r1.z ? 1 : 0.0700000003;
  r1.y = r1.y ? r1.z : 0;
  r1.x = r1.x ? r1.y : 1;
  r0.w = r1.x * r0.w;
  o0.w = 0.5 * r0.w;
  r0.w = cmp(0 < g_hdr_on);
  r0.w = r0.w ? 30 : 1;
  // o0.xyz = saturate(r0.xyz * r0.www);
  o0.xyz = r0.xyz * r0.www;
  o0.w = saturate(o0.w);
  if (HDR == 1.f) o0.xyz *= SOMEINDICATORS;  // SDR stays stock

  return;
}