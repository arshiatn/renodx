// battlefog extinction (16AB86F4: what the fog leaves of the picture, drawn over it as a factor before its light, the
// in-scattering 492B87C7, is added; ATN's 2026-11-14 session: 37 of 41 traces, battle and campaign): the game's, its
// opacity x Fog Strength (SI.fog_strength, as Warhammer III's 34719E09; at 1 the game's). Above 1 the opacity stops
// at full. Decompiled from the game's 16AB86F4 by 3Dmigoto v1.3.16 (tools/tw/wh2/par_w2_make_fog.py); WARP
// tools/tw/warp_tests/par_w2/run_fog.sh.
#include "../shared.h"

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
  float3 g_frustum_points[8] : packoffset(c39);
  float g_orthographic : packoffset(c46.w);
  float g_overlay_lerp : packoffset(c47);
  float g_overlay_parchment_lerp : packoffset(c47.y);
}

cbuffer fog_legacy : register(b1)
{
  float3 g_legacy_volume_fog_colour : packoffset(c0);
  float g_legacy_fog_distance_start : packoffset(c0.w);
  float g_legacy_fog_distance_strength : packoffset(c1);
  float g_legacy_fog_distance_scale : packoffset(c1.y);
  float g_legacy_fog_height_bottom : packoffset(c1.z);
  float g_legacy_fog_height_top : packoffset(c1.w);
  float g_legacy_fog_height_strength : packoffset(c2);
  float g_legacy_fog_colour_blend : packoffset(c2.y);
  float g_legacy_fog_clear_distance : packoffset(c2.z);
  float2 g_legacy_force_fog : packoffset(c3);
  float g_legacy_fog_lerp : packoffset(c3.z);
}

cbuffer fog : register(b2)
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
}

SamplerState s_2d_noise_s : register(s0);
Texture2D<float4> t_2d_noise : register(t0);
Texture2D<float4> gbuffer_channel_4_texture : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r1.xy = (uint2)r0.zw;
  r1.xy = r1.xy * r0.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r0.zw);
  r1.zw = float2(0,0);
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r0.xy = g_render_target_dimensions.xy * r0.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r1.xy = float2(0.00200000009,0.00200000009) * r0.xz;
  r0.w = t_2d_noise.Sample(s_2d_noise_s, r1.xy).x;
  r0.w = g_legacy_force_fog.x * r0.w;
  r1.xyz = -camera_position.xyz + r0.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r2.x = sqrt(r1.w);
  r2.y = min(g_height_fog_clear_distance, g_fog_clear_distance);
  r0.w = r0.w * 0.5 + r2.x;
  r2.z = g_legacy_force_fog.y + -g_legacy_force_fog.x;
  r0.w = -g_legacy_force_fog.x + r0.w;
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
    r0.x = dot(r1.xyz, r1.xyz);
    r0.x = sqrt(r0.x);
    r0.z = g_campaign_density_scaling * r0.x;
    r0.y = max(0, r0.y);
    r3.xyzw = float4(-0.000206276105,-0.000206276105,-0.000206276105,-0.00120224594) * r0.yyyy;
    r3.xyzw = exp2(r3.xyzw);
    r3.xyzw = r3.xyzw * r0.zzzz;
    r0.x = r1.y / r0.x;
    r0.y = -g_height_fog_clear_distance + r2.x;
    r1.xy = -float2(g_fog_height_top, g_fog_height_bottom) + camera_position.yy;  // the decompile wrote cb2[0].wz as one
    r1.xy = r1.xy / -r0.xx;
    r0.z = 10000 + camera_position.y;
    r0.z = r0.z / -r0.x;
    r1.z = min(r1.x, r1.y);
    r1.x = max(r1.x, r1.y);
    r1.w = min(r1.y, r0.z);
    r0.z = max(r1.y, r0.z);
    r1.xyz = max(float3(0,0,0), r1.xwz);
    r0.yz = max(float2(0,0), r0.yz);
    r1.w = min(r1.z, r1.x);
    r2.y = max(r0.y, r1.z);
    r1.x = min(r2.y, r1.x);
    r2.y = cmp(r1.w != r1.x);
    r2.z = r1.w * r0.x + camera_position.y;
    r1.x = r1.x + -r1.z;
    r1.z = r1.w + -r1.z;
    r1.w = r1.z + -r1.x;
    r2.z = r2.z + r2.z;
    r1.x = r1.z + r1.x;
    r0.x = r0.x * r1.x + r2.z;
    r0.x = -g_fog_height_top * 2 + r0.x;
    r0.x = r1.w * r0.x;
    r1.x = g_fog_height_top + -g_fog_height_bottom;
    r1.x = r1.x + r1.x;
    r0.x = r0.x / r1.x;
    r0.x = r2.y ? r0.x : 0;
    r1.x = min(r1.y, r0.z);
    r0.y = max(r0.y, r1.y);
    r0.y = min(r0.y, r0.z);
    r0.z = cmp(r1.x != r0.y);
    r0.y = r0.y + -r1.y;
    r1.x = r1.x + -r1.y;
    r0.y = -r1.x + r0.y;
    r0.y = r0.x + r0.y;
    r0.x = r0.z ? r0.y : r0.x;
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
  o0.xyz = g_fog_lerp * max(SI.fog_strength * r0.xyz, float3(-1,-1,-1)) + float3(1,1,1);  // RenoDX: Fog Strength
  o0.w = g_fog_lerp;
  return;
}
