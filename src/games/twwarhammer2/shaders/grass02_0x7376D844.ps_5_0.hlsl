// rigidgrass ps40_gbuffer_zprefill_grass (7376D844, 139 draws in ATN's traces): the depth pre-pass drops grass past
// 100 m x Grass Height Distance (SI.grass_height_distance, 1: the game's).
// Decompiled from the game's 7376D844 by 3Dmigoto v1.3.16 (tools/tw/wh2/par_w2_make_grass.py);
// WARP tools/tw/warp_tests/par_w2/run_grass.sh.
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

cbuffer grass_PS : register(b1)
{
  float g_inv_lod_distance : packoffset(c0) = {0.00999999978};
  float2 g_alpha : packoffset(c0.y);
  float2 g_normal_scale_bias : packoffset(c1);
  float g_far_addition : packoffset(c1.z);
  float2 g_far_scale : packoffset(c2);
  float g_clip_bias : packoffset(c2.z);
}

SamplerState s_diffuse_map_s : register(s0);
SamplerState s_dissolve_map_s : register(s1);
SamplerState burn_sampler_s : register(s2);
Texture2D<float4> t_diffuse_map : register(t0);
Texture2D<float4> t_dissolve_map : register(t1);
Texture2D<float4> burn_texture : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float2 w3 : TEXCOORD6,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = ddx_coarse(v2.zxy);
  r1.xyz = ddy_coarse(v2.yzx);
  r2.xyz = r1.xyz * r0.xyz;
  r0.xyz = r0.zxy * r1.yzx + -r2.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.xyz = camera_position.xyz + -v2.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r0.x = dot(r0.xyz, r1.xyz);
  r0.x = saturate(r0.x + r0.x);
  r1.xyzw = float4(100,100,20,20) * v3.xyxy;
  r0.y = t_dissolve_map.Sample(s_dissolve_map_s, r1.zw).x;
  r0.z = t_dissolve_map.Sample(s_dissolve_map_s, r1.xy).x;
  r0.x = r0.x + -r0.y;
  r0.y = 1.10000002 + -r0.z;
  r0.w = saturate(w3.y);
  r0.y = r0.w * r0.y + r0.z;
  r0.z = burn_texture.Sample(burn_sampler_s, v3.xy).w;
  r1.x = t_diffuse_map.Sample(s_diffuse_map_s, v3.xy).w;
  r0.z = -r1.x + r0.z;
  r0.z = saturate(r0.w * r0.z + r1.x);
  r0.w = r0.w * r0.w + 0.00999999978;
  r0.y = r0.y * r0.z + -r0.w;
  r0.z = -0.5 + r0.z;
  r0.y = min(r0.y, r0.z);
  r0.x = min(r0.y, r0.x);
  r0.x = g_clip_bias + r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xyz = -camera_position.xyz + v2.xyz;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.y = 1 / GRASS_INV_DISTANCE(g_inv_lod_distance);  // RenoDX: Grass Height Distance
  r0.x = cmp(r0.y < r0.x);
  if (r0.x != 0) discard;
  o0.xyzw = float4(0,0,0,0);
  return;
}
