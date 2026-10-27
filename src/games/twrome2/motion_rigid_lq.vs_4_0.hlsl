// Attila rigid motion (anti_aliasing.hpp): the game's 0x3E086AFB with its placement and wind last frame
// (motion.hlsli, motion_rigid.cs); outputs SV_Position as the game's, this frame's and last frame's clip
// positions without the jitter.
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
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7,
  inout float4 o8,
  inout float4 o9,
  inout float4 o10)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v0.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, v6.xyzw);
  r1.y = dot(r0.xyzw, v7.xyzw);
  r1.z = dot(r0.xyzw, v8.xyzw);
  r1.w = 1;
  r0.x = dot(r1.xyzw, motion_rows[0]);
  r0.y = dot(r1.xyzw, motion_rows[1]);
  r0.w = dot(r1.xyzw, motion_rows[3]);
  o0.xyw = r0.xyw;
  o9.xyz = r0.xyw;
  o0.z = dot(r1.xyzw, motion_rows[2]);
  o1.xyzw = r1.xyzw;
  o2.xyzw = v1.xyzw;
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o3.xyz = r1.xyz * r0.xxx;
  o3.w = wind_direction.x;
  o4.xyz = float3(0,0,0);
  o4.w = wind_direction.y;
  o5.xyz = float3(0,0,0);
  o6.xyzw = v9.xyzw;
  o7.xyzw = v10.xyzw;
  o8.xyzw = v11.xyzw;
  o10.xyzw = v5.xyzw;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : POSITION1,
  float4 v7 : POSITION2,
  float4 v8 : POSITION3,
  float4 v9 : POSITION4,
  float4 v10 : POSITION5,
  float4 v11 : POSITION6,
  out float4 motion_position : SV_Position0,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  float4 o8 = 0;
  float4 o9 = 0;
  float4 o10 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10);
  const float4 world = o0;
  // Last frame's: at its time (wind), at the placement of this mesh where it was predicted to be now
  // (motion_rigid.cs; none: this one).
  float4 rows[3] = {v6, v7, v8};
  float4 previous_rows[3];
  MotionPreviousRigidPlacement(rows, previous_rows);
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, previous_rows[0], previous_rows[1], previous_rows[2], v9, v10, v11, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
