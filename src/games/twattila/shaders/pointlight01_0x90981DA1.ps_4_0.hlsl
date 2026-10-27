// 0x90981DA1 (pointlightsmoothfalloff skinlm highdef): deferred point lights, instanced (the light's position
// and reach per instance: POSITION2, v3). Ray tracing: x the light's traced visibility
// (twattila/rt_scene.hpp InstallPointLights; rt_local_lights.hlsli).
#include "../rt_local_lights.hlsli"

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

cbuffer point_light_PS : register(b2)
{
  float4 g_viewport_offset_scale : packoffset(c0);
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
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = r0.xy * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r0.zw = r0.zw / g_viewport_offset_scale.zw;
  r1.xy = r0.zw * float2(2,-2) + float2(-1,1);
  r2.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.xy, r0.y).xyzw;
  r1.z = r2.x;
  r1.w = 1;
  r2.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r2.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r2.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.z = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r1.xyz = r2.xyz / r0.zzz;
  r2.xyz = -camera_position.xyz + r1.xyz;
  r1.xyz = -v3.xyz + r1.xyz;
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  r2.xyz = r2.xyz * r0.zzz;
  r0.z = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.z);
  r0.z = sqrt(r0.z);
  r0.z = saturate(r0.z / v3.w);
  r0.z = 1 + -r0.z;
  r1.xyz = r1.xyz * r0.www;
  r3.xyzw = gbuffer_channel_1_sampler.SampleLevel(gbuffer_channel_1_sampler_s, r0.xy, r0.y).xyzw;
  r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = r3.w * 63 + 1.60000002;
  r1.w = dot(r3.xyz, r3.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = r3.xyz * r1.www;
  r1.w = dot(r1.xyz, r3.xyz);
  r1.w = r1.w + r1.w;
  r4.xyz = r3.xyz * -r1.www + r1.xyz;
  r1.x = dot(r3.xyz, -r1.xyz);
  r1.y = dot(r4.xyz, -r2.xyz);
  r1.y = max(0, r1.y);
  r1.y = log2(r1.y);
  r0.w = r1.y * r0.w;
  r0.w = exp2(r0.w);
  r0.w = min(1, r0.w);
  r2.xyzw = gbuffer_channel_2_sampler.SampleLevel(gbuffer_channel_2_sampler_s, r0.xy, r0.y).xyzw;
  r3.xyzw = gbuffer_channel_3_sampler.SampleLevel(gbuffer_channel_3_sampler_s, r0.xy, r0.y).xyzw;
  r0.x = r2.w * r0.w;
  r0.y = max(0, r1.x);
  r0.xz = r0.xz * r0.yz;
  r0.xyw = v2.xyz * r0.xxx;
  r1.yzw = r1.xxx * float3(0.800000012,0.899999976,0.300000012) + float3(0.300000012,0.5,0.300000012);
  r1.yzw = saturate(float3(0.694444418,0.694444418,0.444444448) * r1.yzw);
  r2.w = r1.x * r1.y + 0.300000012;
  r2.w = saturate(1 + -r2.w);
  r3.xzw = float3(0.800000012,0.699999988,0.200000003) * r1.zzz;
  r4.xyz = r3.xzw * r2.www;
  r3.xzw = -r3.xzw * r2.www + float3(1,1,1);
  r4.xyz = r1.xxx * r1.yyy + r4.xyz;
  r1.y = -r1.x * r1.y + 1;
  r5.xyz = float3(0.600000024,0.200000003,0.100000001) * r1.www;
  r1.yzw = r5.xyz * r1.yyy;
  r1.yzw = r1.yzw * r3.xzw + r4.xyz;
  r1.yzw = r1.yzw + -r1.xxx;
  r1.xyz = saturate(r3.yyy * r1.yzw + r1.xxx);
  r1.xyz = r2.xyz * r1.xyz;
  r1.xyz = v2.xyz * r1.xyz;
  r1.w = cmp(0 < g_hdr_on);
  r1.w = r1.w ? 0.00400000019 : 1;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = float3(0.899999976,0.899999976,0.899999976) * r1.xyz;
  r0.xyw = r0.xyw * r1.www + r1.xyz;
  o0.xyz = r0.zzz * r0.xyw;
  o0.w = 1;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  return;
}
