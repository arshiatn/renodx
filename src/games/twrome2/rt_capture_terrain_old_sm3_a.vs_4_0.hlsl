// Attila world motion (anti_aliasing.hpp): the game's 0x2898B8F6 with its world position for ray tracing's stream output
// (rt_scene.hpp).
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.

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

cbuffer lf_block_cb : register(b1)
{
  float4 g_height_map_dimension : packoffset(c0);
  float4 g_ws_lf_aabb : packoffset(c1);
  float4 g_lf_aabb : packoffset(c2);
  float4 g_normal_lf_aabb : packoffset(c3);
}

cbuffer vs_height_map_cb : register(b2)
{
  float g_height_map_lf_scale : packoffset(c0);
  float g_height_map_hf_scale : packoffset(c0.y);
  float g_height_map_vertical_offset : packoffset(c0.z);
  float2 g_blend_lf_min_max : packoffset(c1);
}

SamplerState s_low_res_height_map_s : register(s0);
Texture2D<float4> s_low_res_height_map : register(t0);


// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float4 v0,
  float4 v1,
  float4 v2,
  float4 v3,
  float4 v4,
  float v5,
  inout float4 o0,
  inout float4 o1)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(1,1) + -g_height_map_dimension.zw;
  r0.xy = max(float2(0,0), r0.xy);
  r1.xyz = v0.xyz;
  r1.w = 1;
  r2.x = dot(r1.xyzw, v2.xyzw);
  r2.z = dot(r1.xyzw, v4.xyzw);
  r0.z = dot(r1.xyzw, v3.xyzw);
  r0.z = g_height_map_hf_scale * r0.z;
  r1.xy = g_ws_lf_aabb.xy + r2.xz;
  r1.xy = r1.xy / g_ws_lf_aabb.zw;
  r1.z = 1 + -r1.y;
  r1.xy = r1.xz * g_lf_aabb.zw + g_lf_aabb.xy;
  r1.xy = r1.xy * g_normal_lf_aabb.zw + g_normal_lf_aabb.xy;
  r1.yz = min(r1.xy, r0.xy);
  r1.xw = g_height_map_dimension.zw + r1.yz;
  r3.xyzw = s_low_res_height_map.SampleLevel(s_low_res_height_map_s, r1.yw, 0).xyzw;
  r4.xyzw = s_low_res_height_map.SampleLevel(s_low_res_height_map_s, r1.xw, 0).xyzw;
  r5.xyzw = s_low_res_height_map.SampleLevel(s_low_res_height_map_s, r1.xz, 0).xyzw;
  r0.x = r4.x + -r3.x;
  r0.yw = g_height_map_dimension.xy * r1.yz;
  r1.xyzw = s_low_res_height_map.SampleLevel(s_low_res_height_map_s, r1.yz, 0).xyzw;
  r0.yw = frac(r0.yw);
  r0.x = r0.y * r0.x + r3.x;
  r1.y = r5.x + -r1.x;
  r0.y = r0.y * r1.y + r1.x;
  r0.x = r0.x + -r0.y;
  r0.x = r0.w * r0.x + r0.y;
  r0.x = r0.x * g_height_map_lf_scale + r0.z;
  r0.x = -g_height_map_vertical_offset + r0.x;
  r0.y = cmp(0 < r0.x);
  r0.z = cmp(r0.x < 0);
  r0.y = (int)r0.z + (int)-r0.y;
  r0.y = (int)r0.y;
  r0.zw = -camera_position.xz + r2.xz;
  r0.z = dot(r0.zw, r0.zw);
  r0.z = sqrt(r0.z);
  r0.z = -20 + r0.z;
  r0.z = saturate(0.100000001 * r0.z);
  r0.y = r0.z * r0.y;
  r2.y = r0.y * 0.0900000036 + r0.x;
  r2.w = 1;
  o0.x = dot(r2.xyzw, motion_rows[0]);
  o0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  o0.w = dot(r2.xyzw, motion_rows[3]);
  o1.xyzw = r2.xyzw;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  float4 v4 : POSITION3,
  float v5 : POSITION4,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
{
  float4 o0 = 0;
  float4 o1 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, o0, o1);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
