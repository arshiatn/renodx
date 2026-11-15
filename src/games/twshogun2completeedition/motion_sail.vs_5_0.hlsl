// Attila deformed motion (anti_aliasing.hpp): the game's 0xD5EA871E with its vertex's world position last frame (motion.hlsli, motion_capture.ps);
// outputs SV_Position as the game's, this frame's and last frame's clip positions without the jitter.
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

cbuffer sail_VS : register(b1)
{
  float4x3 world_matrix : packoffset(c0);
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
  float v3,
  float4 v4,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 p4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.w = v0.w;
  r0.x = dot(v0.xyzw, world_matrix._m00_m10_m20_m30);
  r0.y = dot(v0.xyzw, world_matrix._m01_m11_m21_m31);
  r0.z = dot(v0.xyzw, world_matrix._m02_m12_m22_m32);
  r1.x = dot(r0.xyzw, motion_rows[0]);
  r1.y = dot(r0.xyzw, motion_rows[1]);
  r1.z = dot(r0.xyzw, motion_rows[2]);
  r1.w = dot(r0.xyzw, motion_rows[3]);
  o0.xyzw = r1.xyzw;
  o5.xyzw = r1.xyzw;
  r1.xyz = camera_position.xyz + -r0.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  o1.xyz = r1.xyz * r1.www;
  o2.xyzw = r0.xyzw;
  r1.x = dot(v1.xyz, v1.xyz);
  r1.x = rsqrt(r1.x);
  r1.xyz = v1.xyz * r1.xxx;
  o3.x = dot(r1.xyz, world_matrix._m00_m10_m20);
  o3.y = dot(r1.xyz, world_matrix._m01_m11_m21);
  o3.z = dot(r1.xyz, world_matrix._m02_m12_m22);
  o4.xy = v2.xy;
  p4.x = v3.x;
  o6.x = dot(r0.xyzw, view._m00_m10_m20_m30);
  o6.y = dot(r0.xyzw, view._m01_m11_m21_m31);
  o6.z = dot(r0.xyzw, view._m02_m12_m22_m32);
  o6.w = dot(r0.xyzw, view._m03_m13_m23_m33);
  o7.xyzw = v4.xyzw;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float v3 : TEXCOORD2,
  float4 v4 : COLOR0,
  uint motion_vertex_id : SV_VertexID,
  out float4 motion_position : SV_Position0,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 p4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, o0, o1, o2, o3, o4, p4, o5, o6, o7);
  const float4 world = o0;
  // Last frame's position of this vertex (motion.hlsli; none: this one).
  float4 previous_world;
  if (!MotionPreviousDeformed(motion_vertex_id - motion_draw.z, previous_world)) previous_world = world;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
