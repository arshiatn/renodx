// ---- Created with 3Dmigoto v1.3.16 on Fri Nov 13 23:10:39 2026
// 0x7CB7BB8A (pointlightsmoothfalloff skinlm highdef, point shadows hardware gather 5.0): deferred point lights, one
// draw a light (point_light_PS b2: g_light_position, g_light_colour, g_light_radius). Ray tracing: x the
// light's traced visibility (twattila/rt_scene.hpp InstallPointLights with b2; rt_local_lights.hlsli), drawn only where
// the rays are bound (addon.cpp OnLightShadowsDraw); else the game's own bytes.
#include "../../twattila/rt_local_lights.hlsli"

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

cbuffer point_light_PS : register(b2)
{
  float3 g_light_position : packoffset(c0);
  float3 g_light_colour : packoffset(c1);
  float g_light_radius : packoffset(c1.w);
  float4 g_viewport_offset_scale : packoffset(c2);
  float g_clip : packoffset(c3);
}

SamplerState gbuffer_channel_1_sampler_s : register(s0);
SamplerState gbuffer_channel_2_sampler_s : register(s1);
SamplerState gbuffer_channel_3_sampler_s : register(s2);
SamplerState gbuffer_channel_4_sampler_s : register(s3);
Texture2D<float4> gbuffer_channel_1_sampler : register(t0);
Texture2D<float4> gbuffer_channel_2_sampler : register(t1);
Texture2D<float4> gbuffer_channel_3_sampler : register(t2);
Texture2D<float4> gbuffer_channel_4_sampler : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(0 != g_clip);
  r1.xyz = v1.xyz;
  r1.w = 1;
  r0.y = dot(r1.xyzw, view_projection._m02_m12_m22_m32);
  r0.z = dot(r1.xyzw, view_projection._m03_m13_m23_m33);
  r0.y = r0.y / r0.z;
  r0.zw = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r0.zw;
  r0.zw = r0.zw * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.zw = r0.zw / g_viewport_offset_scale.zw;
  r2.xy = r0.zw * float2(2,-2) + float2(-1,1);
  r0.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r1.xy, r1.y).x;
  r0.y = r0.z + -r0.y;
  r2.z = r0.z;
  r0.y = cmp(r0.y < 0);
  r0.x = r0.x ? r0.y : 0;
  if (r0.x != 0) discard;
  r2.w = 1;
  r0.x = dot(r2.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r2.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r2.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r2.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r2.xyz = -g_light_position.xyz + r0.xyz;
  r0.xyz = -camera_position.xyz + r0.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r1.z = rsqrt(r0.w);
  r0.w = sqrt(r0.w);
  r0.w = saturate(r0.w / g_light_radius);
  r0.w = 1 + -r0.w;
  r0.w = r0.w * r0.w;
  r2.xyz = r2.xyz * r1.zzz;
  r3.xyzw = gbuffer_channel_1_sampler.SampleLevel(gbuffer_channel_1_sampler_s, r1.xy, r1.y).xyzw;
  r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.z = r3.w * 63 + 1.60000002;
  r1.w = dot(r3.xyz, r3.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = r3.xyz * r1.www;
  r1.w = dot(r3.xyz, -r2.xyz);
  r4.xyz = r1.www * float3(0.800000012,0.899999976,0.300000012) + float3(0.300000012,0.5,0.300000012);
  r4.xyz = saturate(float3(0.694444418,0.694444418,0.444444448) * r4.xyz);
  r2.w = r1.w * r4.x + 0.300000012;
  r2.w = saturate(1 + -r2.w);
  r5.xyz = float3(0.800000012,0.699999988,0.200000003) * r4.yyy;
  r6.xyz = r5.xyz * r2.www;
  r5.xyz = -r5.xyz * r2.www + float3(1,1,1);
  r6.xyz = r1.www * r4.xxx + r6.xyz;
  r2.w = -r1.w * r4.x + 1;
  r4.xyz = float3(0.600000024,0.200000003,0.100000001) * r4.zzz;
  r4.xyz = r4.xyz * r2.www;
  r4.xyz = r4.xyz * r5.xyz + r6.xyz;
  r4.xyz = r4.xyz + -r1.www;
  r2.w = gbuffer_channel_3_sampler.SampleLevel(gbuffer_channel_3_sampler_s, r1.xy, r1.y).y;
  r5.xyzw = gbuffer_channel_2_sampler.SampleLevel(gbuffer_channel_2_sampler_s, r1.xy, r1.y).xyzw;
  r4.xyz = saturate(r2.www * r4.xyz + r1.www);
  r1.x = max(0, r1.w);
  r4.xyz = r5.xyz * r4.xyz;
  r4.xyz = g_light_colour.xyz * r4.xyz;
  r1.y = cmp(0 < g_hdr_on);
  r1.y = r1.y ? 0.00400000019 : 1;
  r4.xyz = r4.xyz * r1.yyy;
  r4.xyz = float3(0.899999976,0.899999976,0.899999976) * r4.xyz;
  r1.w = dot(r2.xyz, r3.xyz);
  r1.w = r1.w + r1.w;
  r2.xyz = r3.xyz * -r1.www + r2.xyz;
  r1.w = dot(r0.xyz, r0.xyz);
  r1.w = rsqrt(r1.w);
  r0.xyz = r1.www * r0.xyz;
  r0.x = dot(r2.xyz, -r0.xyz);
  r0.x = max(0, r0.x);
  r0.x = log2(r0.x);
  r0.x = r1.z * r0.x;
  r0.x = exp2(r0.x);
  r0.x = min(1, r0.x);
  r0.x = r5.w * r0.x;
  r0.x = r0.x * r1.x;
  r0.xyz = g_light_colour.xyz * r0.xxx;
  r0.xyz = r0.xyz * r1.yyy + r4.xyz;
  o0.xyz = r0.www * r0.xyz;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), g_light_position);
  o0.w = 1;
  return;
}