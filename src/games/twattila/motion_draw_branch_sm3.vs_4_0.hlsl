// Attila wind in-draw motion (anti_aliasing.hpp): the game's 0x77B993FD with its outputs as the game's (its own draw: object_motion.hpp, its pixel shader patched by
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

cbuffer wind_VS : register(b1)
{
  float2 wind_direction : packoffset(c0) = {0,0};
  float gust_start_time : packoffset(c0.z) = {0};
  float wind_vegetation_multiplier : packoffset(c0.w) = {0};
  float wind_vegetation_hstrength : packoffset(c1) = {0};
  float wind_vegetation_vstrength : packoffset(c1.y) = {0};
}

cbuffer rigid_config_PS : register(b2)
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
  float2 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  float4 v13,
  float3 v14,
  uint v15,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 p3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout uint4 o7)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = v14.x * 4 + motion_time;
  r0.yz = float2(3,0.5) * motion_time;
  r0.xyz = sin(r0.xyz);
  r0.x = r0.x * r0.y;
  r0.y = r0.z * 0.5 + 0.75;
  r0.x = wind_vegetation_hstrength * r0.x;
  r0.x = r0.x * 0.5 + 1;
  r0.x = r0.x * r0.y;
  r0.x = wind_vegetation_hstrength * r0.x;
  r0.y = wind_vegetation_multiplier * v7.w;
  r0.y = v7.w * r0.y;
  r0.yz = wind_direction.xy * r0.yy;
  r0.xz = r0.yz * r0.xx;
  r1.x = dot(v11.xyzw, v1.xyzw);
  r1.y = dot(v12.xyzw, v1.xyzw);
  r1.z = dot(v13.xyzw, v1.xyzw);
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
  r2.x = v14.x * 2 + motion_time;
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
  o1.w = 1 + -v14.y;
  o3.xy = v5.xy;
  p3.x = v14.z;
  o4.x = dot(v8.xyzw, v2.xyzw);
  o4.y = dot(v9.xyzw, v2.xyzw);
  o4.z = dot(v10.xyzw, v2.xyzw);
  o5.x = dot(v8.xyzw, v3.xyzw);
  o5.y = dot(v9.xyzw, v3.xyzw);
  o5.z = dot(v10.xyzw, v3.xyzw);
  o6.x = dot(v8.xyzw, v4.xyzw);
  o6.y = dot(v9.xyzw, v4.xyzw);
  o6.z = dot(v10.xyzw, v4.xyzw);
  r0.x = (int)v15.x + g_pixel_selection_start_pixel_index;
  r0.yzw = float3(0,0,0);
  r0.x = t_pixel_selection_object_id_texture.Load(r0.xyz).x;
  o7.x = r0.x;
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
  float3 v14 : TEXCOORD6,
  uint v15 : SV_InstanceID0,
  out float4 draw_o0 : SV_Position0,
  out float4 draw_o1 : COLOR0,
  out float4 draw_o2 : TEXCOORD0,
  out float2 draw_o3 : TEXCOORD1,
  out float draw_p3 : TEXCOORD5,
  out float4 draw_o4 : TEXCOORD2,
  out float4 draw_o5 : TEXCOORD3,
  out float3 draw_o6 : TEXCOORD4,
  out uint draw_o7 : POSITION1,
  out float4 motion_now : MOTION0,
  out float4 motion_before : MOTION1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 p3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  uint4 o7 = 0;
  // This frame's: the game's outputs, with the game's rows (their jitter in them).
  motion_rows[0] = view_projection._m00_m10_m20_m30;
  motion_rows[1] = view_projection._m01_m11_m21_m31;
  motion_rows[2] = view_projection._m02_m12_m22_m32;
  motion_rows[3] = view_projection._m03_m13_m23_m33;
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, o3, p3, o4, o5, o6, o7);
  draw_o0 = o0.xyzw;
  draw_o1 = o1.xyzw;
  draw_o2 = o2.xyzw;
  draw_o3 = o3.xy;
  draw_p3 = p3.x;
  draw_o4 = o4.xyzw;
  draw_o5 = o5.xyzw;
  draw_o6 = o6.xyz;
  draw_o7 = o7.x;
  const float4 game_position = o0;
  // Last frame's: world positions (identity rows).
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, o3, p3, o4, o5, o6, o7);
  const float4 previous_world = o0;
  // Both clip positions with x and y in render pixels; this frame's without the jitter (x w).
  const float2 motion_scale = float2(0.5f, -0.5f) * motion_times.zw;
  motion_now = float4((game_position.xy - motion_game_jitter.xy * game_position.w) * motion_scale, game_position.zw);
  const float4 previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                      dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
  motion_before = float4(previous_clip.xy * motion_scale, previous_clip.zw);
}
