// Attila world motion (anti_aliasing.hpp): the game's 0x3333EFDC with its world position for ray tracing's stream output
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

cbuffer wind_VS : register(b1)
{
  float2 wind_direction : packoffset(c0);
  float gust_start_time : packoffset(c0.z);
  float wind_vegetation_multiplier : packoffset(c0.w);
  float wind_vegetation_hstrength : packoffset(c1);
  float wind_vegetation_vstrength : packoffset(c1.y);
}



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
  float2 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  float4 v13,
  float4 v14,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = v11.x * 4 + motion_time;
  r0.yz = float2(3,0.5) * motion_time;
  r0.xyz = sin(r0.xyz);
  r0.x = r0.x * r0.y;
  r0.y = r0.z * 0.5 + 0.75;
  r0.x = wind_vegetation_hstrength * r0.x;
  r0.x = r0.x * 0.5 + 1;
  r0.x = r0.x * r0.y;
  r0.x = wind_vegetation_hstrength * r0.x;
  r0.y = v7.w * v7.w;
  r0.y = wind_vegetation_multiplier * r0.y;
  r0.yz = wind_direction.xy * r0.yy;
  r0.xz = r0.yz * r0.xx;
  r1.x = dot(v12.xyzw, v1.xyzw);
  r1.y = dot(v13.xyzw, v1.xyzw);
  r1.z = dot(v14.xyzw, v1.xyzw);
  r1.w = v1.w;
  r1.xyzw = v6.yyyy * r1.xyzw;
  r2.x = dot(v8.xyzw, v0.xyzw);
  r2.y = dot(v9.xyzw, v0.xyzw);
  r2.z = dot(v10.xyzw, v0.xyzw);
  r2.w = v0.w;
  r1.xyzw = r2.xyzw * v6.xxxx + r1.xyzw;
  r0.y = 0;
  r0.xyz = r1.xyz + r0.xyz;
  r0.w = motion_time + r1.x;
  r0.w = 6 * r0.w;
  r0.w = sin(r0.w);
  r2.x = v11.x * 2 + motion_time;
  r2.x = 0.300000012 * r2.x;
  r2.x = sin(r2.x);
  r2.x = wind_vegetation_vstrength * r2.x;
  r0.w = r2.x * r0.w;
  r2.x = saturate(-0.699999988 + v7.w);
  r2.x = wind_vegetation_multiplier * r2.x;
  r0.w = r2.x * r0.w;
  r1.xyz = r0.www * float3(0,3.33333325,0) + r0.xyz;
  o0.x = dot(r1.xyzw, motion_rows[0]);
  o0.y = dot(r1.xyzw, motion_rows[1]);
  o0.z = dot(r1.xyzw, motion_rows[2]);
  o0.w = dot(r1.xyzw, motion_rows[3]);
  o2.xyzw = r1.xyzw;
  o1.xyz = v7.xyz;
  o1.w = 1 + -v11.y;
  o3.xy = v5.xy;
  o4.x = dot(v8.xyzw, v2.xyzw);
  o4.y = dot(v9.xyzw, v2.xyzw);
  o4.z = dot(v10.xyzw, v2.xyzw);
  o5.x = dot(v8.xyzw, v3.xyzw);
  o5.y = dot(v9.xyzw, v3.xyzw);
  o5.z = dot(v10.xyzw, v3.xyzw);
  o6.x = dot(v8.xyzw, v4.xyzw);
  o6.y = dot(v9.xyzw, v4.xyzw);
  o6.z = dot(v10.xyzw, v4.xyzw);
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float4 v4 : POSITION4,
  float2 v5 : POSITION5,
  float4 v6 : POSITION6,
  float4 v7 : POSITION7,
  float4 v8 : TEXCOORD0,
  float4 v9 : TEXCOORD1,
  float4 v10 : TEXCOORD2,
  float4 v11 : TEXCOORD3,
  float4 v12 : TEXCOORD4,
  float4 v13 : TEXCOORD5,
  float4 v14 : TEXCOORD6,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, o0, o1, o2, o3, o4, o5, o6);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
