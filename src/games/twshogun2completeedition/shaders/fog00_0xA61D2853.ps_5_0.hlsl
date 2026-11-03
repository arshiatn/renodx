// battlefog.hlsl ps40_main_extinction: the battle fog's extinction, multiplied onto the scene (already blended
// from 1 by g_fog_lerp), and the fog's distance blur for the depth of field (o1). HDR: Battle Fog x the fog's
// density (extinction ^ Battle Fog), the distance blur follows.
#include "../shared.h"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
  float g_texel_offset : packoffset(c35.z);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);
  float2 g_viewport_origin : packoffset(c39);
  float4 g_render_target_dimensions : packoffset(c40);
  float4 g_camera_temp0 : packoffset(c41);
  float2 g_inv_screen_size_minus_one : packoffset(c42);
  float2 g_camera_jitter : packoffset(c42.z);
  float2 g_prev_camera_jitter : packoffset(c43);
}

cbuffer fog : register(b1)
{
  float g_fog_density_constant : packoffset(c0);
  float g_fog_density_height : packoffset(c0.y);
  float g_fog_height_bottom : packoffset(c0.z);
  float g_fog_height_top : packoffset(c0.w);
  float g_fog_density_mie : packoffset(c1);
  float g_fog_noise : packoffset(c1.y);
  float3 g_fog_colour : packoffset(c2);
  uint g_fog_mode : packoffset(c2.w);
  float g_fog_lerp : packoffset(c3);
  float g_fog_clear_distance : packoffset(c3.y);
  float g_fog_y_multiplier : packoffset(c3.z);
  float g_campaign_density_scaling : packoffset(c3.w);
  float g_height_fog_clear_distance : packoffset(c4);
  float g_clear_distance_fade_out : packoffset(c4.y);
  float g_fog_shadow_strength : packoffset(c4.z);
  float g_fog_blur_offset : packoffset(c4.w);
  float g_fog_blur_strength : packoffset(c5);
  float2 g_force_fog : packoffset(c5.y);
}

SamplerState s_volume_noise_s : register(s0);
SamplerState s_2d_noise_s : register(s1);
Texture3D<float4> t_volume_noise : register(t0);
Texture2D<float4> t_2d_noise : register(t1);
Texture2D<float4> gbuffer_colour_texture : register(t2);
Texture2D<float4> gbuffer_readable_depth_texture : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0,
  out float2 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = screen_size.zw * v0.xy;
  gbuffer_colour_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r1.xy = (uint2)r0.zw;
  r1.xy = r1.xy * r0.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r0.zw);
  r1.zw = float2(0,0);
  r1.z = gbuffer_readable_depth_texture.Load(r1.xyz).x;
  r0.z = cmp(r1.z >= 1);
  if (r0.z != 0) discard;
  r0.xy = r0.xy * g_render_target_dimensions.xy + -g_viewport_origin.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r1.xy = float2(0.00200000009,0.00200000009) * r0.xz;
  r0.w = t_2d_noise.SampleLevel(s_2d_noise_s, r1.xy, 0).x;
  r0.w = g_force_fog.x * r0.w;
  r1.xyz = -camera_position.xyz + r0.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r2.x = sqrt(r1.w);
  r2.y = min(g_height_fog_clear_distance, g_fog_clear_distance);
  r0.w = r0.w * 0.5 + r2.x;
  r2.z = g_force_fog.y + -g_force_fog.x;
  r0.w = -g_force_fog.x + r0.w;
  r2.z = 1 / r2.z;
  r0.w = saturate(r2.z * r0.w);
  r2.z = r0.w * -2 + 3;
  r0.w = r0.w * r0.w;
  r0.w = r2.z * r0.w;
  r2.z = 9.99999975e-005 + r2.y;
  r2.z = cmp(r2.z < r2.x);
  if (r2.z != 0) {
    r1.w = rsqrt(r1.w);
    r1.xyz = r1.xyz * r1.www;
    r1.xyz = r1.xyz * r2.yyy + camera_position.xyz;
    r1.xyz = -r1.xyz + r0.xyz;
    r1.w = dot(r1.xyz, r1.xyz);
    r1.w = sqrt(r1.w);
    r2.y = g_campaign_density_scaling * r1.w;
    r2.z = max(0, r0.y);
    r3.xyzw = float4(-0.000206276105,-0.000206276105,-0.000206276105,-0.00120224594) * r2.zzzz;
    r3.xyzw = exp2(r3.xyzw);
    r3.xyzw = r3.xyzw * r2.yyyy;
    r1.xyz = r1.xyz / r1.www;
    r1.w = -g_height_fog_clear_distance + r2.x;
    r2.yzw = float3(0.00999999978,-0.00999999978,0.00999999978) * time_in_sec;
    r0.xyz = r0.xyz * float3(0.00100000005,0.00100000005,0.00100000005) + r2.yzw;
    r0.x = t_volume_noise.SampleLevel(s_volume_noise_s, r0.xyz, 0).x;
    r0.x = -0.5 + r0.x;
    r0.y = -0.0799999982 * r0.x;
    r0.xz = float2(0,0);
    r0.xyz = r1.xyz + r0.xyz;
    r0.x = dot(r0.xyz, r0.xyz);
    r0.x = rsqrt(r0.x);
    r0.x = r0.y * r0.x;
    r0.yz = -float2(g_fog_height_top, g_fog_height_bottom) + camera_position.yy;
    r0.yz = r0.yz / -r0.xx;
    r1.x = 10000 + camera_position.y;
    r1.x = r1.x / -r0.x;
    r1.y = min(r0.y, r0.z);
    r0.y = max(r0.y, r0.z);
    r1.z = min(r1.x, r0.z);
    r0.z = max(r1.x, r0.z);
    r1.xyw = max(float3(0,0,0), r1.zyw);
    r0.yz = max(float2(0,0), r0.yz);
    r1.z = min(r1.y, r0.y);
    r2.y = max(r1.w, r1.y);
    r0.y = min(r2.y, r0.y);
    r2.y = cmp(r1.z != r0.y);
    r2.z = r1.z * r0.x + camera_position.y;
    r0.y = r0.y + -r1.y;
    r1.y = r1.z + -r1.y;
    r1.z = r1.y + -r0.y;
    r2.z = r2.z + r2.z;
    r0.y = r1.y + r0.y;
    r0.x = r0.x * r0.y + r2.z;
    r0.x = -g_fog_height_top * 2 + r0.x;
    r0.x = r1.z * r0.x;
    r0.y = g_fog_height_top + -g_fog_height_bottom;
    r0.y = r0.y + r0.y;
    r0.x = r0.x / r0.y;
    r0.x = r2.y ? r0.x : 0;
    r0.y = min(r1.x, r0.z);
    r1.y = max(r1.w, r1.x);
    r0.z = min(r1.y, r0.z);
    r1.y = cmp(r0.y != r0.z);
    r0.yz = r0.yz + -r1.xx;
    r0.y = r0.z + -r0.y;
    r0.y = r0.x + r0.y;
    r0.x = r1.y ? r0.y : r0.x;
    r0.x = g_fog_density_height * r0.x;
    r0.y = -g_fog_clear_distance + r2.x;
    r0.y = max(0, r0.y);
    r0.y = g_fog_density_constant * r0.y;
    r0.x = max(r0.x, r0.y);
    r1.xyzw = float4(5.80000005e-006,1.35e-005,3.3100001e-005,2.20000002e-005) * r3.xyzw;
    r1.xyz = r1.xyz + r1.www;
    r0.xyz = r0.xxx * float3(0.0125000002,0.0125000002,0.0125000002) + r1.xyz;
    r0.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r0.xyz;
    r0.xyz = exp2(r0.xyz);
  } else {
    r0.xyz = float3(1,1,1);
  }
  r0.xyz = r0.www * -r0.xyz + r0.xyz;
  r0.xyz = float3(-1,-1,-1) + r0.xyz;
  r0.xyz = g_fog_lerp * r0.xyz + float3(1,1,1);
  r0.xyz = pow(max(r0.xyz, 0.000001f), (HDR >= 1.f) ? SI.battle_fog : 1.f);
  r0.w = 1 + -r0.x;
  r0.w = -g_fog_blur_offset + r0.w;
  o1.x = saturate(g_fog_blur_strength * r0.w);
  o0.w = g_fog_lerp;
  o0.xyz = r0.xyz;
  o1.y = 0;
  return;
}