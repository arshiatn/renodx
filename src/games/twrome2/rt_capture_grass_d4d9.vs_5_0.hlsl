// Attila world_uv_o3 motion (anti_aliasing.hpp): the game's 0xD4D9A7CE with its world position and UV for ray tracing's stream output
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
  float2 v1,
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
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = saturate(-0.699999988 + v4.w);
  r0.y = max(0.100000001, wind_vegetation_multiplier);
  r0.y = r0.y + r0.y;
  r0.x = r0.x * r0.y;
  r0.y = v4.w * r0.y;
  r0.yz = wind_direction.xy * r0.yy;
  r1.z = dot(v7.xyzw, v0.xyzw);
  r1.x = dot(v5.xyzw, v0.xyzw);
  r2.xy = float2(0.25,0.25) * r1.xz;
  r2.xy = sin(r2.xy);
  r0.w = r2.x * r2.y + motion_time;
  r1.w = 0.300000012 * r0.w;
  r0.w = sin(r0.w);
  r1.w = sin(r1.w);
  r1.w = wind_vegetation_vstrength * r1.w;
  r2.x = motion_time + r1.x;
  r2.x = 5 * r2.x;
  r2.x = cos(r2.x);
  r1.w = r2.x * r1.w;
  r0.x = r1.w * r0.x;
  r1.w = 3 * motion_time;
  r1.w = sin(r1.w);
  r0.w = r1.w * r0.w;
  r0.w = wind_vegetation_hstrength * r0.w;
  r0.w = r0.w * 0.5 + 0.5;
  r0.w = wind_vegetation_hstrength * r0.w;
  r0.yz = r0.yz * r0.ww;
  r2.xz = float2(0.5,0.5) * r0.yz;
  r2.y = 0;
  r0.xyz = r0.xxx * float3(0,2.33333325,0) + r2.xyz;
  r1.y = dot(v6.xyzw, v0.xyzw);
  r2.xyz = r1.xyz + r0.xyz;
  r1.xy = r1.xz * float2(0.25,0.25) + motion_time;
  r1.xy = sin(r1.xy);
  r0.w = r1.x + r1.y;
  r2.w = v0.w;
  o0.x = dot(r2.xyzw, motion_rows[0]);
  o0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  r1.x = dot(r2.xyzw, motion_rows[3]);
  o0.w = r1.x;
  o2.w = r1.x;
  r1.x = v7.w * v5.w;
  r1.x = 0.100000001 * r1.x;
  r1.x = sin(r1.x);
  o1.z = r1.x * 0.300000012 + 0.699999988;
  o1.xyw = v4.xyw;
  o2.xyz = r2.xyz;
  r1.xyz = -camera_position.xyz + r2.xyz;
  o3.xy = v1.xy;
  r1.w = sin(motion_time);
  r0.w = r1.w * r0.w;
  r0.w = 0.300000012 * r0.w;
  r2.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r3.x = dot(v5.xyz, r2.xyz);
  r3.y = dot(v6.xyz, r2.xyz);
  r3.z = dot(v7.xyz, r2.xyz);
  r0.xyz = r0.xyz * r0.www + r3.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  o4.xyz = r0.xyz * r0.www;
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o5.x = dot(v5.xyz, r0.xyz);
  o5.y = dot(v6.xyz, r0.xyz);
  o5.z = dot(v7.xyz, r0.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.y = rsqrt(r0.x);
  o6.w = sqrt(r0.x);
  o6.xyz = r1.xyz * r0.yyy;
  o7.x = v5.w;
  o7.y = v6.w;
  o7.z = v7.w;
  o7.w = 1;
  return;
}

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : COLOR0,
  float4 v5 : TEXCOORD1,
  float4 v6 : TEXCOORD2,
  float4 v7 : TEXCOORD3,
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
  float4 o7 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, o0, o1, o2, o3, o4, o5, o6, o7);
  const float4 world = o0;
  motion_world = float4(world.xyz / world.w, asfloat(MotionHalfBits(o3.x) | (MotionHalfBits(o3.y) << 16)));
  motion_position = 0.f;
}
