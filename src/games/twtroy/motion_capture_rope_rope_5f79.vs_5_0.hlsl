// Attila capture motion (anti_aliasing.hpp): the game's 0x5F79A748 with its world position by vertex, one point per index on one pixel for
// motion_capture.ps.
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.

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
  float g_overlay_palette_alpha : packoffset(c47.z);
  float g_debug_tonemapping : packoffset(c47.w);
  float4 g_blood_remap : packoffset(c48);
}



// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float3 v0,
  float3 v1,
  float4 v2,
  float4 v3,
  float2 v4,
  inout float4 o0,
  inout float4 o1,
  inout float4 p1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = camera_position.xyz + -v0.xyz;
  r1.xyz = v1.yzx * r0.zxy;
  r1.xyz = r0.yzx * v1.zxy + -r1.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r2.xyz = r1.xyz * v3.xxx + v0.xyz;
  r2.w = 1;
  o0.x = dot(r2.xyzw, motion_rows[0]);
  o0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  o0.w = dot(r2.xyzw, motion_rows[3]);
  o6.xyzw = r2.xyzw;
  p1.x = 0;
  r0.w = dot(v1.xyz, v1.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = v1.xyz * r0.www;
  r3.xyz = r2.zxy * r1.yzx;
  r3.xyz = r2.yzx * r1.zxy + -r3.xyz;
  o2.xyz = r1.xyz;
  o1.xyz = r3.xyz;
  o3.xyz = r2.xyz;
  o4.xy = v2.xy;
  o4.zw = v4.xy;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o5.z = dot(r0.xyz, r2.xyz);
  r0.x = abs(r3.y) * -0.0187292993 + 0.0742610022;
  r0.x = r0.x * abs(r3.y) + -0.212114394;
  r0.x = r0.x * abs(r3.y) + 1.57072878;
  r0.y = 1 + -abs(r3.y);
  r0.y = sqrt(r0.y);
  r0.z = r0.x * r0.y;
  r0.z = r0.z * -2 + 3.14159274;
  r0.w = cmp(r3.y < -r3.y);
  r1.x = cmp(r3.x < 0);
  r0.z = r0.w ? r0.z : 0;
  r0.x = r0.x * r0.y + r0.z;
  r0.y = 6.28318548 + -r0.x;
  r0.x = r1.x ? r0.y : r0.x;
  o5.x = 0.159154937 * r0.x;
  o5.y = v3.y;
  return;
}

void main(
  float3 v0 : POSITION0,
  float3 v1 : NORMAL0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float2 v4 : TEXCOORD2,
  uint motion_vertex_id : SV_VertexID,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0,
  out uint motion_vertex : TEXCOORD1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 p1 = 0;
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
  GameMain(v0, v1, v2, v3, v4, o0, o1, p1, o2, o3, o4, o5, o6);
  const float4 world = o0;
  motion_world = world;
  motion_vertex = motion_vertex_id - motion_draw.z;
  motion_position = float4(0.f, 0.f, 0.5f, 1.f);
}
