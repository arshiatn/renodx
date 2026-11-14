// Attila world motion (anti_aliasing.hpp): the game's 0x16AFAF14 with its world position for ray tracing's stream output
// (rt_scene.hpp).
// Made by tools\tw\attila_motion\make_wind_motion_vs.py. RT_CAPTURE_UV (rt_capture_rigid_standard_cut_out): the UV in w.
#include "../twattila/rt_capture.hlsli"

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
}

cbuffer cb_environment : register(b1)
{
  float2 g_wind_direction : packoffset(c0);
  float2 g_wind_vector : packoffset(c0.z);
  float g_wind_gust_start_time : packoffset(c1);
  float g_wind_vegetation_multiplier : packoffset(c1.y);
  float g_wind_vegetation_hstrength : packoffset(c1.z);
  float g_wind_vegetation_vstrength : packoffset(c1.w);
  float2 g_wind_offset : packoffset(c2);
  float g_snowing_intensity : packoffset(c2.z);
  int g_weather_type : packoffset(c2.w);
  int g_season_type : packoffset(c3);
  float g_snow_mask : packoffset(c3.y);
  float g_creep_intensity : packoffset(c3.z);
  float g_cubemap_sun_colour_scale : packoffset(c3.w);
}

cbuffer rigid_config_PS : register(b2)
{
  float g_unlit : packoffset(c0);
  float g_untextured : packoffset(c0.y);
  float g_snow : packoffset(c0.z);
  float g_rain : packoffset(c0.w);
  float g_tesselation_factor : packoffset(c1);
  float g_tesselation_displacement_scale : packoffset(c1.y);
  float2 g_uv2_tile_interval : packoffset(c1.z);
  float2 g_texel_density_uv_scale : packoffset(c2);
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
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v0.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, v6.xyzw);
  r1.y = dot(r0.xyzw, v7.xyzw);
  r1.z = dot(r0.xyzw, v8.xyzw);
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r2.x = dot(r0.xyz, v6.xyz);
  r2.y = dot(r0.xyz, v7.xyz);
  r2.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  o3.xyz = r2.xyz * r0.xxx;
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r2.x = dot(r0.xyz, v6.xyz);
  r2.y = dot(r0.xyz, v7.xyz);
  r2.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  o4.xyz = r2.xyz * r0.xxx;
  r0.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r2.x = dot(r0.xyz, v6.xyz);
  r2.y = dot(r0.xyz, v7.xyz);
  r2.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  o5.xyz = r2.xyz * r0.xxx;
  if (g_pixel_selection_active != 0) {
    r0.x = (int)v13.x + g_pixel_selection_start_pixel_index;
    r0.yzw = float3(0,0,0);
    r0.x = t_pixel_selection_object_id_texture.Load(r0.xyz).x;
  }
  o12.x = g_pixel_selection_active ? r0.x : -1;
  r1.w = 1;
  r0.x = dot(r1.xyzw, motion_rows[0]);
  r0.y = dot(r1.xyzw, motion_rows[1]);
  o0.z = dot(r1.xyzw, motion_rows[2]);
  r0.w = dot(r1.xyzw, motion_rows[3]);
  o0.xyw = r0.xyw;
  o1.xyzw = r1.xyzw;
  o2.xyzw = v1.xyzw;
  o3.w = g_wind_direction.x;
  o4.w = g_wind_direction.y;
  o6.xyzw = v9.xyzw;
  o7.xyzw = v10.xyzw;
  o8.xyzw = v11.xyzw;
  o9.xyz = r0.xyw;
  o9.w = v9.y;
  o10.xyzw = v5.xyzw;
  o11.xyzw = v12.xyzw;
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
  float4 o10 = 0;
  float4 o11 = 0;
  uint4 o12 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  const float4 world = o0;
#if defined(RT_CAPTURE_UV)
  motion_world = RtWorldUv(world.xyz, o2.xy);
#else
  motion_world = world;
#endif
  motion_position = 0.f;
}
