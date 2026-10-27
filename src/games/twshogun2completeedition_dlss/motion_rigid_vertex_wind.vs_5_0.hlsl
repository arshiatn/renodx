// Attila rigid motion (anti_aliasing.hpp): the game's 0x0798A33F with its placement and wind last frame
// (motion.hlsli, motion_rigid.cs); outputs SV_Position as the game's, this frame's and last frame's clip
// positions without the jitter.
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.

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

cbuffer wind_VS : register(b1)
{
  float2 g_wind_direction : packoffset(c0) = {0,0};
  float2 g_prev_wind_direction : packoffset(c0.z) = {0,0};
  float gust_start_time : packoffset(c1) = {0};
  float wind_particle_multiplier : packoffset(c1.y) = {1};
  float wind_vegetation_multiplier : packoffset(c1.z) = {0};
  float wind_vegetation_hstrength : packoffset(c1.w) = {0};
  float wind_vegetation_vstrength : packoffset(c2) = {0};
  float g_wind_influence_max_distance : packoffset(c2.y) = {300};
  float g_wind_influence_offset : packoffset(c2.z) = {200};
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
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = motion_time * 10 + v0.x;
  r0.y = sin(r0.x);
  r0.x = 0.800000012 * r0.x;
  r0.x = sin(r0.x);
  r0.y = -0.300000012 + r0.y;
  r0.yzw = float3(0.0199999996,0.0700000003,0.0299999993) * r0.yyy;
  r0.xyz = r0.yzw * r0.xxx;
  r1.y = 0;
  r2.y = 0;
  r3.xy = float2(10,5) * motion_time;
  r4.xyz = v0.xyz;
  r4.w = 1;
  r5.y = dot(r4.xyzw, v7.xyzw);
  r3.xy = r5.yy * float2(10,100) + r3.xy;
  r3.xy = sin(r3.xy);
  r3.xz = g_wind_direction.xy * r3.xx;
  r3.xy = r3.xz * r3.yy;
  r3.xy = v5.ww * r3.xy;
  r1.xz = float2(0.100000001,0.100000001) * r3.xy;
  r3.xy = g_wind_direction.xy * v5.ww;
  r0.w = motion_time * 20 + r5.y;
  r0.w = sin(r0.w);
  r0.w = r0.w * 0.00999999978 + 0.00999999978;
  r3.xy = r3.xy * r0.ww;
  r2.xz = float2(0.349999994,0.349999994) * r3.xy;
  r1.xyz = r2.xyz + r1.xyz;
  r0.xyz = r0.xyz * v5.www + r1.xyz;
  r5.x = dot(r4.xyzw, v6.xyzw);
  r5.z = dot(r4.xyzw, v8.xyzw);
  r0.xyz = r5.xyz + r0.xyz;
  r0.w = 1;
  r1.z = dot(r0.xyzw, motion_rows[2]);
  r1.x = dot(r0.xyzw, motion_rows[0]);
  r1.y = dot(r0.xyzw, motion_rows[1]);
  r1.w = dot(r0.xyzw, motion_rows[3]);
  o0.xyzw = r1.xyzw;
  o8.xyzw = r1.xyzw;
  o7.xyz = r1.xyw;
  o1.xyzw = r0.xyzw;
  o2.xyzw = v1.xyzw;
  r1.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.xyz * r1.www;
  r2.x = dot(r1.xyz, v6.xyz);
  r2.y = dot(r1.xyz, v7.xyz);
  r2.z = dot(r1.xyz, v8.xyz);
  r1.x = dot(r2.xyz, r2.xyz);
  r1.x = rsqrt(r1.x);
  o3.xyz = r2.xyz * r1.xxx;
  o3.w = g_wind_direction.x;
  r1.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.xyz * r1.www;
  r2.x = dot(r1.xyz, v6.xyz);
  r2.y = dot(r1.xyz, v7.xyz);
  r2.z = dot(r1.xyz, v8.xyz);
  r1.x = dot(r2.xyz, r2.xyz);
  r1.x = rsqrt(r1.x);
  o4.xyz = r2.xyz * r1.xxx;
  o4.w = g_wind_direction.y;
  r1.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.xyz * r1.www;
  r2.x = dot(r1.xyz, v6.xyz);
  r2.y = dot(r1.xyz, v7.xyz);
  r2.z = dot(r1.xyz, v8.xyz);
  r1.x = dot(r2.xyz, r2.xyz);
  r1.x = rsqrt(r1.x);
  o5.xyz = r2.xyz * r1.xxx;
  o6.xyzw = v9.xyzw;
  o9.x = dot(r0.xyzw, view._m00_m10_m20_m30);
  o9.y = dot(r0.xyzw, view._m01_m11_m21_m31);
  o9.z = dot(r0.xyzw, view._m02_m12_m22_m32);
  o9.w = dot(r0.xyzw, view._m03_m13_m23_m33);
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
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10);
  const float4 world = o0;
  // Last frame's: at its time (wind), at the placement of this mesh where it was predicted to be now
  // (motion_rigid.cs; none: this one).
  float4 rows[3] = {v6, v7, v8};
  float4 previous_rows[3];
  MotionPreviousRigidPlacement(rows, previous_rows);
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, previous_rows[0], previous_rows[1], previous_rows[2], v9, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
