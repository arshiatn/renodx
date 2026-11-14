// Attila world_view motion (anti_aliasing.hpp): the game's 0x655AB38D with its world position (its view-space one through inv_view) for ray tracing's stream output
// (rt_scene.hpp).
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
  float g_overlay_text_lerp : packoffset(c47.y);
  float g_overlay_parchment_lerp : packoffset(c47.z);
  float g_overlay_palette_alpha : packoffset(c47.w);
  float4 g_overlay_outline_colour : packoffset(c48);
  float g_overlay_outline_border_width : packoffset(c49);
  float g_ui_overlay_brightness : packoffset(c49.y);
  float g_debug_tonemapping : packoffset(c49.z);
  float4 g_blood_remap : packoffset(c50);
  int g_editor_mode : packoffset(c51);
  float4 g_spec_gloss_tweaker : packoffset(c52);
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
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
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

  r0.x = v5.x;
  r0.y = v6.x;
  r0.z = v7.x;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.y = dot(v1.xyz, v1.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = v1.xyz * r0.yyy;
  r1.x = dot(r0.yzw, v5.xyz);
  r1.y = dot(r0.yzw, v6.xyz);
  r1.z = dot(r0.yzw, v7.xyz);
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.xyz * r1.www;
  r2.y = dot(r1.xyz, view._m00_m10_m20);
  r2.z = dot(r1.xyz, view._m01_m11_m21);
  r1.x = dot(r2.yz, r2.yz);
  r1.x = rsqrt(r1.x);
  r1.yz = r2.yz * r1.xx;
  r2.xz = float2(1,0) * r1.zy;
  r2.yw = float2(0,1);
  r1.xyz = -r1.xyz * float3(0,1,0) + r2.xyz;
  r1.w = dot(r1.xy, r1.xy);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = v3.xxx * r1.xyz;
  r2.xyz = v0.xyz;
  r3.x = dot(r2.xyzw, v5.xyzw);
  r3.y = dot(r2.xyzw, v6.xyzw);
  r3.z = dot(r2.xyzw, v7.xyzw);
  r3.w = 1;
  r2.x = dot(r3.xyzw, view._m00_m10_m20_m30);
  r2.y = dot(r3.xyzw, view._m01_m11_m21_m31);
  r2.z = dot(r3.xyzw, view._m02_m12_m22_m32);
  r1.xyz = r1.xyz * r0.xxx + r2.xyz;
  r1.w = dot(r3.xyzw, view._m03_m13_m23_m33);
  o6.xyzw = r3.xyzw;
  r2.xyz = -camera_position.yzx + r3.yzx;
  o0.x = dot(r1.xyzw, motion_rows[0]);
  o0.y = dot(r1.xyzw, motion_rows[1]);
  o0.z = dot(r1.xyzw, motion_rows[2]);
  o0.w = dot(r1.xyzw, motion_rows[3]);
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  r1.xyz = r2.xyz * r0.xxx;
  r2.xyz = r1.xyz * r0.wyz;
  r1.xyz = r0.zwy * r1.yzx + -r2.xyz;
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r1.xyz = r1.xyz * r0.xxx;
  r2.xyz = r1.zxy * r0.zwy;
  r2.xyz = r1.yzx * r0.wyz + -r2.xyz;
  o2.xyz = r1.xyz;
  o3.xyz = -r0.yzw;
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  o1.xyz = r2.xyz * r0.xxx;
  p1.x = v8.w;
  o4.xy = v2.xy;
  return;
}

void main(
  float3 v0 : POSITION0,
  float3 v1 : NORMAL0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float2 v4 : TEXCOORD2,
  float4 v5 : POSITION1,
  float4 v6 : POSITION2,
  float4 v7 : POSITION3,
  float4 v8 : POSITION4,
  float4 v9 : POSITION5,
  float4 v10 : POSITION6,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
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
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, o0, o1, p1, o2, o3, o4, o5, o6);
  const float4 world = o0;
  // The view-space position the game projects, back to the world.
  motion_world = float4(dot(world, inv_view._m00_m10_m20_m30), dot(world, inv_view._m01_m11_m21_m31),
                        dot(world, inv_view._m02_m12_m22_m32), dot(world, inv_view._m03_m13_m23_m33));
  motion_position = 0.f;
}
