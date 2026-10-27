// Attila world motion (anti_aliasing.hpp): the game's 0x562E1161 with its world position for ray tracing's stream output
// (rt_scene.hpp).
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view_projection : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4 camera_near_far : packoffset(c21);
  float time_in_sec : packoffset(c22);
  float2 g_inverse_focal_length : packoffset(c22.y);
  float g_vertical_fov : packoffset(c22.w);
  float4 g_screen_size : packoffset(c23);
  float g_vpos_texel_offset : packoffset(c24);
  float4 g_viewport_dimensions : packoffset(c25);
  float4 g_camera_temp0 : packoffset(c26);
  float4 g_camera_temp1 : packoffset(c27);
  float4 g_camera_temp2 : packoffset(c28);
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
Texture2D<float4> t_low_res_height_map : register(t0);


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
  float4 v5,
  float4 v6,
  float4 v7,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 p3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = float3(1,2,-1) * v0.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, v5.xyzw);
  r1.z = dot(r0.xyzw, v7.xyzw);
  r0.x = dot(r0.xyzw, v6.xyzw);
  r0.x = g_height_map_hf_scale * r0.x;
  r0.yz = g_ws_lf_aabb.xy + r1.xz;
  r2.xy = r0.yz / g_ws_lf_aabb.zw;
  r2.z = 1 + -r2.y;
  r0.yz = r2.xz * g_lf_aabb.zw + g_lf_aabb.xy;
  r0.yz = r0.yz * g_normal_lf_aabb.zw + g_normal_lf_aabb.xy;
  r2.xy = r0.yz / g_height_map_dimension.zw;
  r0.yz = g_height_map_dimension.xy * r0.yz;
  r0.yz = frac(r0.yz);
  r2.xy = floor(r2.xy);
  r2.xy = float2(1,1) + r2.xy;
  r2.xy = r2.xy / g_height_map_dimension.xy;
  r2.xyzw = t_low_res_height_map.Gather(s_low_res_height_map_s, r2.xy).xyzw;
  r2.yz = r2.zy + -r2.wx;
  r0.yw = r0.yy * r2.yz + r2.wx;
  r0.w = r0.w + -r0.y;
  r0.y = r0.z * r0.w + r0.y;
  r0.x = r0.y * g_height_map_lf_scale + r0.x;
  r0.x = -g_height_map_vertical_offset + r0.x;
  r0.y = cmp(0 < r0.x);
  r0.z = cmp(r0.x < 0);
  r0.y = (int)r0.z + (int)-r0.y;
  r0.y = (int)r0.y;
  r0.zw = -camera_position.xz + r1.xz;
  r0.z = dot(r0.zw, r0.zw);
  r0.z = sqrt(r0.z);
  r0.z = -20 + r0.z;
  r0.z = saturate(0.100000001 * r0.z);
  r0.y = r0.z * r0.y;
  r1.y = r0.y * 0.0900000036 + r0.x;
  r1.w = 1;
  o0.x = dot(r1.xyzw, motion_rows[0]);
  o0.y = dot(r1.xyzw, motion_rows[1]);
  o0.z = dot(r1.xyzw, motion_rows[2]);
  o0.w = dot(r1.xyzw, motion_rows[3]);
  o1.xyzw = r1.xyzw;
  o2.xyzw = v4.xyzw;
  o3.x = v1.w;
  p3.xy = v4.xy;
  r0.xyz = v1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.xyz = float3(1,1,-1) * r0.xyz;
  o4.x = dot(r0.xyz, v5.xyz);
  o4.y = dot(r0.xyz, v6.xyz);
  o4.z = dot(r0.xyz, v7.xyz);
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  o5.x = dot(r0.xyz, v5.xyz);
  o5.y = dot(r0.xyz, v6.xyz);
  o5.z = dot(r0.xyz, v7.xyz);
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  o6.x = dot(r0.xyz, v5.xyz);
  o6.y = dot(r0.xyz, v6.xyz);
  o6.z = dot(r0.xyz, v7.xyz);
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float4 v2 : NORMAL1,
  float4 v3 : NORMAL2,
  float4 v4 : TEXCOORD0,
  float4 v5 : POSITION1,
  float4 v6 : POSITION2,
  float4 v7 : POSITION3,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 p3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, o0, o1, o2, o3, p3, o4, o5, o6);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
