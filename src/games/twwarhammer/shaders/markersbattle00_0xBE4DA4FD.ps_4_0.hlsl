// 0xBE4DA4FD (indicator ps40_main_deferred): the units' and buildings' ground markers, faded where the scene lies in front
// of them; x 30 with g_hdr_on. Unit/Building Indicator Brightness (SI.some_indicators): x the colour in HDR, as Attila's
// markersbattle00 (indicator ps30_main_deferred).
cbuffer camera : register(b0)
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
  float2 g_viewport_origin : packoffset(c30);
  float4 g_render_target_dimensions : packoffset(c31);
  float4 g_camera_temp0 : packoffset(c32);
  float4 g_camera_temp1 : packoffset(c33);
  float4 g_camera_temp2 : packoffset(c34);
  float4 g_clip_rect : packoffset(c35);
  float3 g_vr_head_rotation : packoffset(c36);
  int g_num_of_samples : packoffset(c36.w);
  float g_supersampling : packoffset(c37);
  float4 g_mouse_position : packoffset(c38);
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

SamplerState gbuffer_channel_4_sampler_s : register(s0);
SamplerState indicator_texture_sampler_s : register(s1);
Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<float4> g_indicator_texture : register(t1);


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

  r0.xyzw = g_indicator_texture.Sample(indicator_texture_sampler_s, v1.xy).xyzw;
  r1.x = r0.w * v3.w + -0.00392156886;
  r0.xyzw = v3.xyzw * r0.xyzw;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.xy = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r1.xy;
  r2.xyzw = gbuffer_channel_4_texture.SampleLevel(gbuffer_channel_4_sampler_s, r1.xy, 0).yzxw;
  r2.xy = r1.xy * float2(2,-2) + float2(-1,1);
  r2.w = 1;
  r1.x = dot(r2.xyzw, inv_projection._m02_m12_m22_m32);
  r1.y = dot(r2.xyzw, inv_projection._m03_m13_m23_m33);
  r1.x = r1.x / r1.y;
  r1.y = v4.z / v4.w;
  r1.z = cmp(r1.x < r1.y);
  r1.y = r1.y + -r1.x;
  r1.x = saturate(20 + -r1.x);
  r2.xy = cmp(r1.yy < float2(0.5,5));
  r1.y = 0.100000001 + r1.y;
  r1.y = saturate(3.33333325 * r1.y);
  r1.x = r1.x * -r1.y + 1;
  r1.y = r2.y ? 1 : 0.0700000003;
  r1.y = r2.x ? r1.y : 0;
  r1.y = r1.z ? r1.y : 1;
  r1.x = r1.x * r1.y;
  r0.w = r1.x * r0.w;
  o0.w = 0.5 * r0.w;
  r0.w = cmp(0 < g_hdr_on);
  r0.w = r0.w ? 30 : 1;
  o0.xyz = r0.xyz * r0.www;
  if (HDR >= 1.f) o0.xyz *= SI.some_indicators;  // SDR stays stock
  return;
}
