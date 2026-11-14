// Ray tracing's capture of the campaign's trees (rigidcampaigntrees vs40_main, the game's 0x07321725: rigid, instanced, its
// rows in POSITION1-3): its world position and its UV (o3.xy: what its G-buffer 07295514 tests t_diffuse's alpha at) as two
// halves in w, for the stream output (rt_scene.hpp). Made by tools\tw\attila_motion\make_wind_motion_vs.py (world_uv_o3)
// from the 3Dmigoto decompile (tools\tw\troy\casters). WARP: test\tw_troy_shaders.
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
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7,
  inout float4 o8,
  inout float4 o9)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.w = v0.w;
  r0.x = dot(v7.xyzw, v0.xyzw);
  r0.y = dot(v8.xyzw, v0.xyzw);
  r0.z = dot(v9.xyzw, v0.xyzw);
  o0.x = dot(r0.xyzw, motion_rows[0]);
  o0.y = dot(r0.xyzw, motion_rows[1]);
  o0.z = dot(r0.xyzw, motion_rows[2]);
  o0.w = dot(r0.xyzw, motion_rows[3]);
  o2.xyzw = r0.xyzw;
  r0.xyz = -camera_position.xyz + r0.xyz;
  o1.xyzw = v5.xyzw;
  o3.xy = v1.xy;
  r1.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  o4.xyz = r1.xyz * r0.www;
  r1.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r2.xyz = v8.xyz * r1.yyy;
  r1.xyw = r1.xxx * v7.xyz + r2.xyz;
  r1.xyz = r1.zzz * v9.xyz + r1.xyw;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  o5.xyz = r1.xyz * r0.www;
  r1.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r2.xyz = v8.xyz * r1.yyy;
  r1.xyw = r1.xxx * v7.xyz + r2.xyz;
  r1.xyz = r1.zzz * v9.xyz + r1.xyw;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  o6.xyz = r1.xyz * r0.www;
  r1.xyz = v6.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  o7.x = dot(v7.xyz, r1.xyz);
  o7.y = dot(v8.xyz, r1.xyz);
  o7.z = dot(v9.xyz, r1.xyz);
  r0.w = dot(r0.xyz, r0.xyz);
  r1.x = rsqrt(r0.w);
  o8.w = sqrt(r0.w);
  o8.xyz = r1.xxx * r0.xyz;
  o9.x = v7.w;
  o9.y = v8.w;
  o9.z = v9.w;
  o9.w = 1;
  return;
}

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : TEXCOORD1,
  float4 v7 : POSITION1,
  float4 v8 : POSITION2,
  float4 v9 : POSITION3,
  float4 v10 : POSITION4,
  float4 v11 : POSITION5,
  float4 v12 : POSITION6,
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
  float4 o8 = 0;
  float4 o9 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9);
  const float4 world = o0;
  motion_world = float4(world.xyz / world.w, asfloat(MotionHalfBits(o3.x) | (MotionHalfBits(o3.y) << 16)));
  motion_position = 0.f;
}
