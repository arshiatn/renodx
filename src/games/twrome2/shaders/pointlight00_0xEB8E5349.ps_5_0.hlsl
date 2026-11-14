// ---- Created with 3Dmigoto v1.3.16 on Fri Nov 13 23:10:39 2026
// 0xEB8E5349 (pointlightsmoothfalloff slm highdef, point shadows hardware gather 5.0): deferred point lights, one
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
  float4 r0,r1,r2,r3,r4,r5;
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
  r2.xyz = -camera_position.xyz + r0.xyz;
  r0.xyz = -g_light_position.xyz + r0.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r3.xyzw = gbuffer_channel_1_sampler.SampleLevel(gbuffer_channel_1_sampler_s, r1.xy, r1.y).xyzw;
  r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = r3.w * r3.w;
  r0.w = r0.w * 0.979900002 + 0.0199999996;
  r0.w = r0.w * 0.5 + 0.5;
  r0.w = r0.w * 2 + -1;
  r1.z = dot(r3.xyz, r3.xyz);
  r1.z = rsqrt(r1.z);
  r3.xyz = r3.xyz * r1.zzz;
  r1.z = dot(r2.xyz, r3.xyz);
  r1.z = r1.z + r1.z;
  r4.xyz = r3.xyz * -r1.zzz + r2.xyz;
  r1.z = dot(r0.xyz, r0.xyz);
  r1.w = rsqrt(r1.z);
  r1.z = sqrt(r1.z);
  r1.z = saturate(r1.z / g_light_radius);
  r1.z = 1 + -r1.z;
  r1.z = r1.z * r1.z;
  r0.xyz = r1.www * r0.xyz;
  r1.w = dot(-r0.xyz, r4.xyz);
  r1.w = max(-1, r1.w);
  r1.w = min(1, r1.w);
  r2.w = abs(r1.w) * -0.0187292993 + 0.0742610022;
  r2.w = r2.w * abs(r1.w) + -0.212114394;
  r2.w = r2.w * abs(r1.w) + 1.57072878;
  r3.w = 1 + -abs(r1.w);
  r1.w = cmp(r1.w < -r1.w);
  r3.w = sqrt(r3.w);
  r4.x = r3.w * r2.w;
  r4.x = r4.x * -2 + 3.14159274;
  r1.w = r1.w ? r4.x : 0;
  r1.w = r2.w * r3.w + r1.w;
  r4.xy = float2(-0.0174532924,0.0174532924) + r1.ww;
  r1.w = -r0.w * r0.w + 1;
  r0.w = cmp(0 < r0.w);
  r0.w = (int)-r0.w;
  r1.w = max(0.00100000005, r1.w);
  r1.w = log2(r1.w);
  r2.w = 4.95061684 * r1.w;
  r1.w = r1.w * 0.346573591 + 4.54688501;
  r2.w = r1.w * r1.w + -r2.w;
  r2.w = sqrt(r2.w);
  r1.w = r2.w + -r1.w;
  r1.w = max(0, r1.w);
  r1.w = sqrt(r1.w);
  r0.w = r1.w * r0.w;
  r0.w = 1.41421354 * r0.w;
  r0.w = 0.0174532924 / r0.w;
  r0.w = max(9.99999975e-005, r0.w);
  r0.w = 1 / r0.w;
  r4.xy = r4.xy * r0.ww;
  r0.w = cmp(r4.x < 0);
  r4.zw = cmp(float2(0,0) < r4.xy);
  r4.xy = float2(0.707106769,0.707106769) * r4.xy;
  r4.xy = r4.xy * r4.xy;
  r0.w = (int)r0.w + (int)-r4.z;
  r1.w = (int)-r4.w;
  r0.w = (int)r0.w;
  r5.xyzw = r4.xxyy * float4(0.140012279,0.140012279,0.140012279,0.140012279) + float4(1.27323949,1,1.27323949,1);
  r4.zw = r5.xz / r5.yw;
  r4.xy = -r4.xy * r4.zw;
  r4.xy = float2(1.44269502,1.44269502) * r4.xy;
  r4.xy = exp2(r4.xy);
  r4.xy = float2(1,1) + -r4.xy;
  r4.xy = sqrt(r4.xy);
  r0.w = r0.w * r4.x + 1;
  r1.w = r1.w * r4.y + 1;
  r0.w = 0.5 * r0.w;
  r0.w = r1.w * 0.5 + -r0.w;
  r1.w = dot(r3.xyz, -r0.xyz);
  r0.x = dot(-r0.xyz, r2.xyz);
  r0.x = max(9.99999975e-005, r0.x);
  r0.x = log2(r0.x);
  r0.x = 60 * r0.x;
  r0.x = exp2(r0.x);
  r0.y = 0.99984771 + r1.w;
  r0.z = max(0, r1.w);
  r0.y = saturate(1.00015235 * r0.y);
  r0.y = r0.y * r0.w;
  r2.xyzw = gbuffer_channel_2_sampler.SampleLevel(gbuffer_channel_2_sampler_s, r1.xy, r1.y).xyzw;
  r1.xyw = gbuffer_channel_3_sampler.SampleLevel(gbuffer_channel_3_sampler_s, r1.xy, r1.y).xyz;
  r0.w = 1 + -r2.w;
  r0.x = r0.x * r0.w + r2.w;
  r0.x = r0.y * r0.x;
  r0.y = cmp(0 < r2.w);
  r0.y = r0.y ? 1.000000 : 0;
  r0.x = r0.x * r0.y;
  r0.y = max(r0.x, r2.w);
  r2.xyz = r2.xyz * r2.xyz;
  r2.xyz = r2.xyz * r0.zzz;
  r2.xyz = g_light_colour.xyz * r2.xyz;
  r0.y = 1 + -r0.y;
  r0.yzw = r2.xyz * r0.yyy;
  r2.x = cmp(0 < g_hdr_on);
  r2.xy = r2.xx ? float2(0.00400000019,1) : float2(1,273);
  r0.yzw = r2.xxx * r0.yzw;
  r0.x = saturate(r2.y * r0.x);
  r1.xyw = r0.xxx * r1.xyw;
  r0.xyz = r1.xyw * g_light_colour.xyz + r0.yzw;
  o0.xyz = r1.zzz * r0.xyz;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), g_light_position);
  o0.w = 1;
  return;
}