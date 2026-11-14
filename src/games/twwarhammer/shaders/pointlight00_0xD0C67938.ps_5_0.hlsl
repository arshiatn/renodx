// pointlightsmoothfalloff (D0C67938) and, with POINT_LIGHT_LINEAR, pointlightlinearfalloff (5D1695BC: the falloff not
// squared, their only difference) slm, VS B5B5BFA4 (Warhammer II's D2A0FF23: a light an instance, POSITION1 its colour,
// POSITION2 its place and reach): the game's (3Dmigoto), x the light's traced visibility (Torch and Fire Shadows:
// twattila/rt_scene.hpp InstallPointLights, twattila/rt_local_lights.hlsli). Runs only with the rays' mask bound
// (addon.cpp PointLightRays); otherwise the game's own bytes do. test/tw_warhammer_shaders part 7 draws both against them.
#include "../../twattila/rt_local_lights.hlsli"

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

cbuffer colorimetry_VS_PS : register(b1)
{
  float g_brightness : packoffset(c0);
  float g_gamma_output : packoffset(c0.y);
  float g_inv_gamma_output : packoffset(c0.z);
}

cbuffer lighting_VS_PS : register(b2)
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

cbuffer dynamic_light_PS : register(b3)
{
  float4 g_viewport_offset_scale : packoffset(c0);
}

SamplerState gbuffer_channel_1_sampler_s : register(s0);
SamplerState gbuffer_channel_2_sampler_s : register(s1);
SamplerState gbuffer_channel_3_sampler_s : register(s2);
SamplerState gbuffer_channel_4_sampler_s : register(s3);
Texture2D<float4> gbuffer_channel_1_texture : register(t0);
Texture2D<float4> gbuffer_channel_2_texture : register(t1);
Texture2D<float4> gbuffer_channel_3_texture : register(t2);
Texture2D<float4> gbuffer_channel_4_texture : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  r1.xyzw = gbuffer_channel_1_texture.SampleLevel(gbuffer_channel_1_sampler_s, r0.zw, 0).zxyw;
  r2.xyzw = gbuffer_channel_2_texture.SampleLevel(gbuffer_channel_2_sampler_s, r0.zw, 0).xyzw;
  r3.xyzw = gbuffer_channel_3_texture.SampleLevel(gbuffer_channel_3_sampler_s, r0.zw, 0).xyzw;
  r4.xyzw = gbuffer_channel_4_texture.SampleLevel(gbuffer_channel_4_sampler_s, r0.zw, 0).yzxw;
  r0.zw = r1.yz * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
  sincos(r0.zw, r5.xy, r6.xy);
  r7.x = r6.y;
  r7.z = r5.y;
  r6.xz = r7.xz * r6.xx;
  r0.z = 255 * r3.w;
  r0.z = round(r0.z);
  r0.z = (uint)r0.z;
  r0.xy = r0.xy * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.xy = r0.xy / g_viewport_offset_scale.zw;
  r4.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r4.w = 1;
  r7.x = dot(r4.xyzw, inv_view_projection._m00_m10_m20_m30);
  r7.y = dot(r4.xyzw, inv_view_projection._m01_m11_m21_m31);
  r7.z = dot(r4.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.x = dot(r4.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyw = r7.xyz / r0.xxx;
  r1.yzw = -camera_position.xyz + r0.xyw;
  r2.w = dot(r1.yzw, r1.yzw);
  r2.w = rsqrt(r2.w);
  r1.yzw = r2.www * r1.yzw;
  r0.xyw = -v3.xyz + r0.xyw;
  r2.w = dot(r0.xyw, r0.xyw);
  r3.w = rsqrt(r2.w);
  r0.xyw = r3.www * r0.xyw;
  r2.w = sqrt(r2.w);
  r2.w = saturate(r2.w / v3.w);
  r2.w = 1 + -r2.w;
#ifndef POINT_LIGHT_LINEAR
  r2.w = r2.w * r2.w;
#endif
  r4.xy = (int2)r0.zz & int2(2,128);
  r6.y = r5.x;
  r0.z = dot(r6.xyz, r0.xyw);
  r5.xyz = r6.xyz * r0.zzz + -r0.xyw;
  r0.z = dot(r5.xyz, r5.xyz);
  r0.z = rsqrt(r0.z);
  r5.xyz = r5.xyz * r0.zzz;
  r4.xzw = r4.xxx ? r5.xyz : r6.xyz;
  r0.z = dot(r4.xzw, -r0.xyw);
  r5.xy = max(float2(0,0.100000001), r0.zz);
  r3.w = cmp(0 < r0.z);
  if (r3.w != 0) {
    r3.w = dot(r1.yzw, r4.xzw);
    r3.w = r3.w + r3.w;
    r6.xyz = r4.xzw * -r3.www + r1.yzw;
    r1.x = saturate(r1.x);
    r1.x = log2(r1.x);
    r1.x = g_gamma_output * r1.x;
    r1.x = exp2(r1.x);
    r3.w = r1.x * r1.x;
    r5.z = dot(-r0.xyw, r6.xyz);
    r5.z = max(-1, r5.z);
    r5.z = min(1, r5.z);
    r5.w = 1 + -abs(r5.z);
    r5.w = sqrt(r5.w);
    r6.x = abs(r5.z) * -0.0187292993 + 0.0742610022;
    r6.x = r6.x * abs(r5.z) + -0.212114394;
    r6.x = r6.x * abs(r5.z) + 1.57072878;
    r6.y = r6.x * r5.w;
    r6.y = r6.y * -2 + 3.14159274;
    r5.z = cmp(r5.z < -r5.z);
    r5.z = r5.z ? r6.y : 0;
    r5.z = r6.x * r5.w + r5.z;
    r5.w = cmp(0 < g_hdr_on);
    r3.w = r3.w * r3.w;
    r6.xy = r5.ww ? float2(0.00999999978,0.989899993) : float2(1,-0.000100016594);
    r3.w = r3.w * r6.y + r6.x;
    r3.w = r3.w * 0.5 + 0.5;
    r3.w = r3.w * 2 + -1;
    r5.w = -r3.w * r3.w + 1;
    r5.w = max(0.00100000005, r5.w);
    r5.w = log2(r5.w);
    r6.x = 4.95061684 * r5.w;
    r5.w = r5.w * 0.346573591 + 4.54688501;
    r6.y = cmp(0 < r3.w);
    r3.w = cmp(r3.w < 0);
    r3.w = (int)-r6.y + (int)r3.w;
    r3.w = (int)r3.w;
    r6.x = r5.w * r5.w + -r6.x;
    r6.x = sqrt(r6.x);
    r5.w = r6.x + -r5.w;
    r5.w = max(0, r5.w);
    r5.w = sqrt(r5.w);
    r3.w = r5.w * r3.w;
    r3.w = 1.41421354 * r3.w;
    r3.w = 0.00872664619 / r3.w;
    r3.w = max(9.99999975e-005, r3.w);
    r5.zw = float2(-0.00872664619,0.00872664619) + r5.zz;
    r3.w = 1 / r3.w;
    r5.zw = r5.zw * r3.ww;
    r6.xy = float2(0.707106769,0.707106769) * r5.zw;
    r6.xy = r6.xy * r6.xy;
    r7.xyzw = r6.xxyy * float4(0.140012279,0.140012279,0.140012279,0.140012279) + float4(1.27323949,1,1.27323949,1);
    r6.zw = r7.xz / r7.yw;
    r6.xy = -r6.xy * r6.zw;
    r6.zw = cmp(float2(0,0) < r5.zw);
    r3.w = cmp(r5.z < 0);
    r3.w = (int)-r6.z + (int)r3.w;
    r3.w = (int)r3.w;
    r5.zw = float2(1.44269502,1.44269502) * r6.xy;
    r5.zw = exp2(r5.zw);
    r5.zw = float2(1,1) + -r5.zw;
    r5.zw = sqrt(r5.zw);
    r3.w = r3.w * r5.z + 1;
    r3.w = 0.5 * r3.w;
    r5.z = (int)-r6.w;
    r5.z = r5.z * r5.w + 1;
    r3.w = r5.z * 0.5 + -r3.w;
    r1.x = -r1.x * r1.x + 1;
    r5.z = min(1, r5.x);
    r6.xy = float2(1.57079637,1.53938043) * r1.xx;
    r1.x = sin(r6.x);
    r5.z = -1 + r5.z;
    r1.x = r1.x * r5.z + 1;
    r5.z = dot(-r0.xyw, r1.yzw);
    r5.z = max(0, r5.z);
    r5.z = log2(r5.z);
    r5.z = 10 * r5.z;
    r5.z = exp2(r5.z);
    r5.w = cos(r6.y);
    r5.w = max(0, r5.w);
    r5.w = rsqrt(r5.w);
    r5.w = 1 / r5.w;
    r6.xyz = saturate(float3(60,60,60) * r3.xyz);
    r5.z = r5.z * r5.w;
    r6.xyz = r6.xyz + -r3.xyz;
    r6.xyz = r5.zzz * r6.xyz + r3.xyz;
    r1.x = abs(r3.w) * r1.x;
    r6.xyz = r1.xxx * r6.xyz;
  } else {
    r6.xyz = float3(0,0,0);
  }
  r3.xyz = max(r6.xyz, r3.xyz);
  r3.xyz = float3(1,1,1) + -r3.xyz;
  r5.xzw = r5.xxx * r2.xyz;
  r5.xzw = v2.xyz * r5.xzw;
  r3.xyz = r5.xzw * r3.xyz;
  r3.xyz = float3(0.318309873,0.318309873,0.318309873) * r3.xyz;
  r1.x = cmp(0 < g_hdr_on);
  r1.x = r1.x ? 0.00999999978 : 1;
  r3.xyz = r3.xyz * r1.xxx;
  r1.y = dot(r0.xyw, -r1.yzw);
  r1.z = 0.00100000005 / r5.y;
  r0.z = cmp(r0.z < 0);
  r0.z = r0.z ? 1.000000 : 0;
  r0.z = r1.z * r0.z;
  r5.xyz = float3(0.800000012,0.899999976,0.800000012) + -r2.xyz;
  r5.xyz = r5.xyz * float3(0.100000001,0.100000001,0.100000001) + r2.xyz;
  r5.xyz = float3(150,150,150) * r5.xyz;
  r0.x = dot(r0.xyw, r4.xzw);
  r0.x = max(0, r0.x);
  r0.yzw = -r5.xyz * r0.zzz;
  r0.yzw = float3(1.44269502,1.44269502,1.44269502) * r0.yzw;
  r0.yzw = exp2(r0.yzw);
  r4.xzw = r0.xxx * r0.yzw;
  r4.xzw = float3(0.318309873,0.318309873,0.318309873) * r4.xzw;
  r5.xyz = float3(1,1,1) + -r2.xyz;
  r0.x = r1.y * -0.958400011 + 1;
  r0.x = r0.x * r0.x;
  r0.x = 12.566371 * r0.x;
  r0.x = 0.0814694166 / r0.x;
  r0.yzw = float3(1,1,1) + -r0.yzw;
  r0.xyz = r0.xxx * r0.yzw;
  r1.xyz = v2.xyz * r1.xxx;
  r1.xyz = r1.xyz * r2.xyz;
  r0.xyz = r4.xzw * r5.xyz + r0.xyz;
  r0.xyz = r1.xyz * r0.xyz;
  r0.xyz = r4.yyy ? r0.xyz : float3(0,0,0);
  r1.xyz = r6.xyz * v2.xyz + r3.xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r0.xyz = r2.www * r0.xyz;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  o0.w = 1;
  return;
}
