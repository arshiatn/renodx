// Attila rigid in-draw motion (anti_aliasing.hpp): the game's 0xD830AD9B with its outputs as the game's (its own draw: object_motion.hpp, its pixel shader patched by
// dxbc.hpp), this frame's and last frame's clip positions in render pixels (MOTION0 / MOTION1).
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
  float4 g_render_target_dimensions : packoffset(c30);
  float4 g_camera_temp0 : packoffset(c31);
  float4 g_camera_temp1 : packoffset(c32);
  float4 g_camera_temp2 : packoffset(c33);
  float4 g_clip_rect : packoffset(c34);
  int g_num_of_samples : packoffset(c35);
}

cbuffer rigid_config_PS : register(b1)
{
  float g_unlit : packoffset(c0);
  float g_dissolve_enable : packoffset(c0.y);
  float g_untextured : packoffset(c0.z);
  float g_snow : packoffset(c0.w);
  float g_rain : packoffset(c1);
  float g_tesselation_factor : packoffset(c1.y);
  float g_tesselation_displacement_scale : packoffset(c1.z);
  float2 g_uv2_tile_interval : packoffset(c2);
  float2 g_texel_density_uv_scale : packoffset(c2.z);
  float4 g_decal_uv_rect_coords : packoffset(c3);
  float g_force_snow_shader : packoffset(c4);
  float g_is_second_class_object : packoffset(c4.y);
  int g_pixel_selection_active : packoffset(c4.z);
  int g_pixel_selection_start_pixel_index : packoffset(c4.w);
}

cbuffer wind_VS : register(b2)
{
  float2 wind_direction : packoffset(c0) = {0,0};
  float gust_start_time : packoffset(c0.z) = {0};
  float wind_vegetation_multiplier : packoffset(c0.w) = {0};
  float wind_vegetation_hstrength : packoffset(c1) = {0};
  float wind_vegetation_vstrength : packoffset(c1.y) = {0};
}

Texture2D<uint> t_pixel_selection_object_id_texture : register(t0);


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
  float4 v12,
  uint v13,
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
  inout float4 o10,
  inout float4 o11,
  inout uint4 o12)
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
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o4.xyz = r1.xyz * r0.xxx;
  o4.w = wind_direction.y;
  r0.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o5.xyz = r1.xyz * r0.xxx;
  o6.xyzw = v9.xyzw;
  o7.xyzw = v10.xyzw;
  o8.xyzw = v11.xyzw;
  o9.w = v9.y;
  o10.xyzw = v5.xyzw;
  o11.xyzw = v12.xyzw;
  r0.x = (int)v13.x + g_pixel_selection_start_pixel_index;
  r0.yzw = float3(0,0,0);
  r0.x = t_pixel_selection_object_id_texture.Load(r0.xyz).x;
  o12.x = r0.x;
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
  float4 v12 : POSITION7,
  uint v13 : SV_InstanceID0,
  out float4 draw_o0 : SV_Position0,
  out float4 draw_o1 : TEXCOORD0,
  out float4 draw_o2 : TEXCOORD1,
  out float4 draw_o3 : TEXCOORD2,
  out float4 draw_o4 : TEXCOORD3,
  out float4 draw_o5 : TEXCOORD4,
  out float4 draw_o6 : TEXCOORD5,
  out float4 draw_o7 : TEXCOORD6,
  out float4 draw_o8 : TEXCOORD7,
  out float4 draw_o9 : COLOR1,
  out float4 draw_o10 : COLOR2,
  out float4 draw_o11 : NORMAL0,
  out uint draw_o12 : POSITION1,
  out float4 motion_now : MOTION0,
  out float4 motion_before : MOTION1)
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
  float4 o11 = 0;
  uint4 o12 = 0;
  // This frame's: the game's outputs, with the game's rows (their jitter in them).
  motion_rows[0] = view_projection._m00_m10_m20_m30;
  motion_rows[1] = view_projection._m01_m11_m21_m31;
  motion_rows[2] = view_projection._m02_m12_m22_m32;
  motion_rows[3] = view_projection._m03_m13_m23_m33;
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  draw_o0 = o0.xyzw;
  draw_o1 = o1.xyzw;
  draw_o2 = o2.xyzw;
  draw_o3 = o3.xyzw;
  draw_o4 = o4.xyzw;
  draw_o5 = o5.xyzw;
  draw_o6 = o6.xyzw;
  draw_o7 = o7.xyzw;
  draw_o8 = o8.xyzw;
  draw_o9 = o9.xyzw;
  draw_o10 = o10.xyzw;
  draw_o11 = o11.xyzw;
  draw_o12 = o12.x;
  const float4 game_position = o0;
  // Last frame's: world positions (identity rows).
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  // Last frame's: at its time (wind), at the placement of this mesh where it was predicted to be now
  // (motion_rigid.cs; none: this one).
  float4 rows[3] = {v6, v7, v8};
  float4 previous_rows[3];
  MotionPreviousRigidPlacement(rows, previous_rows);
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, previous_rows[0], previous_rows[1], previous_rows[2], v9, v10, v11, v12, v13, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  const float4 previous_world = o0;
  // Both clip positions with x and y in render pixels; this frame's without the jitter (x w).
  const float2 motion_scale = float2(0.5f, -0.5f) * motion_times.zw;
  motion_now = float4((game_position.xy - motion_game_jitter.xy * game_position.w) * motion_scale, game_position.zw);
  const float4 previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                      dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
  motion_before = float4(previous_clip.xy * motion_scale, previous_clip.zw);
}
