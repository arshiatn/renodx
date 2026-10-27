// Attila world_uv motion (anti_aliasing.hpp): the game's 0x929D693E with its world position and UV for ray tracing's stream output
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



// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

// A float as a half's bits (truncated; tiny values 0, huge ones infinite).
uint MotionHalfBits(float value)
{
  const uint bits = asuint(value);
  const uint sign = (bits >> 16) & 0x8000;
  const int exponent = int((bits >> 23) & 0xFF) - 112;
  if (exponent <= 0) return sign;
  if (exponent >= 31) return sign | 0x7C00;
  return sign | (uint(exponent) << 10) | ((bits >> 13) & 0x3FF);
}

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
  inout float4 o5)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(0 < v6.y);
  r0.y = cmp(v6.y < 1);
  r0.x = r0.y ? r0.x : 0;
  r0.y = 1 + -v6.y;
  r0.x = saturate(r0.x ? r0.y : v6.y);
  r1.x = dot(v12.xyzw, v1.xyzw);
  r1.y = dot(v13.xyzw, v1.xyzw);
  r1.z = dot(v14.xyzw, v1.xyzw);
  r1.w = v1.w;
  r2.x = dot(v8.xyzw, v0.xyzw);
  r2.y = dot(v9.xyzw, v0.xyzw);
  r2.z = dot(v10.xyzw, v0.xyzw);
  r2.w = v0.w;
  r1.xyzw = -r2.xyzw + r1.xyzw;
  r0.xyzw = r0.xxxx * r1.xyzw + r2.xyzw;
  o0.x = dot(r0.xyzw, motion_rows[0]);
  o0.y = dot(r0.xyzw, motion_rows[1]);
  o0.z = dot(r0.xyzw, motion_rows[2]);
  r0.w = dot(r0.xyzw, motion_rows[3]);
  o0.w = r0.w;
  o1.xyzw = r0.xyzw;
  o2.xy = v5.xy;
  o2.z = v11.x;
  o3.w = 1 + -v11.y;
  o3.x = dot(v8.xyzw, v2.xyzw);
  o3.y = dot(v9.xyzw, v2.xyzw);
  o3.z = dot(v10.xyzw, v2.xyzw);
  o4.xyzw = float4(0,0,0,0);
  o5.xyz = float3(0,0,0);
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
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, o0, o1, o2, o3, o4, o5);
  const float4 world = o0;
  motion_world = float4(world.xyz, asfloat(MotionHalfBits(o2.x) | (MotionHalfBits(o2.y) << 16)));
  motion_position = 0.f;
}
