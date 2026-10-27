// Attila world motion (anti_aliasing.hpp): the game's 0x4A82B8C2 with its world position for ray tracing's stream output
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

cbuffer cb_environment : register(b1)
{
  float2 g_wind_direction : packoffset(c0);
  float2 g_wind_vector : packoffset(c0.z);
  float2 g_wind_vector_main : packoffset(c1);
  float g_wind_gust_start_time : packoffset(c1.z);
  float g_wind_vegetation_multiplier : packoffset(c1.w);
  float g_wind_vegetation_hstrength : packoffset(c2);
  float g_wind_vegetation_vstrength : packoffset(c2.y);
  float2 g_wind_vegetation_tremble : packoffset(c2.z);
  float2 g_wind_vegetation_weaviness : packoffset(c3);
  float g_wind_vegetation_trunk_movement : packoffset(c3.z);
  float g_wind_vegetation_lerp : packoffset(c3.w);
  float2 g_wind_offset : packoffset(c4);
  float g_snowing_intensity : packoffset(c4.z);
  int g_weather_type : packoffset(c4.w);
  int g_season_type : packoffset(c5);
  float g_snow_mask : packoffset(c5.y);
  float g_creep_intensity : packoffset(c5.z);
  float g_cubemap_sun_colour_scale : packoffset(c5.w);
  float g_campaign_vegetation_tint_scale : packoffset(c6);
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
  float g_amount_blendshapes[128] : packoffset(c5);
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
  float4 v14,
  float3 v15,
  uint v16,
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
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 0;
  r0.w = v15.x;
  r1.xyzw = v8.xyzw + r0.xxxw;
  r2.x = 0;
  r2.w = v15.y;
  r3.xyzw = v10.xywz + r2.xxwx;
  r0.xyzw = v11.xyzw + r0.xxxw;
  r2.xyzw = v13.xyzw + r2.xxxw;
  r4.x = dot(r1.xyzw, v0.xyzw);
  r4.y = dot(v9.xyzw, v0.xyzw);
  r4.z = dot(r3.xywz, v0.xyzw);
  r0.x = dot(r0.xyzw, v1.xyzw);
  r0.y = dot(v12.xyzw, v1.xyzw);
  r0.z = dot(r2.xyzw, v1.xyzw);
  r4.w = v0.w;
  r0.w = v1.w;
  r0.xyzw = v6.yyyy * r0.xyzw;
  r0.xyzw = v6.xxxx * r4.xyzw + r0.xyzw;
  r1.x = dot(v8.xyz, v2.xyz);
  r1.y = dot(v9.xyz, v2.xyz);
  r1.z = dot(v10.xyz, v2.xyz);
  r2.x = dot(v11.xyz, v2.xyz);
  r2.y = dot(v12.xyz, v2.xyz);
  r2.z = dot(v13.xyz, v2.xyz);
  r2.xyz = v6.yyy * r2.xyz;
  r1.xyz = v6.xxx * r1.xyz + r2.xyz;
  r2.x = dot(r1.xyz, r1.xyz);
  r2.x = rsqrt(r2.x);
  o4.xyz = r2.xxx * r1.xyz;
  r1.x = dot(v8.xyz, v3.xyz);
  r1.y = dot(v9.xyz, v3.xyz);
  r1.z = dot(v10.xyz, v3.xyz);
  r2.x = dot(v11.xyz, v3.xyz);
  r2.y = dot(v12.xyz, v3.xyz);
  r2.z = dot(v13.xyz, v3.xyz);
  r2.xyz = v6.yyy * r2.xyz;
  r1.xyz = v6.xxx * r1.xyz + r2.xyz;
  r2.x = dot(r1.xyz, r1.xyz);
  r2.x = rsqrt(r2.x);
  o5.xyz = r2.xxx * r1.xyz;
  r1.x = dot(v8.xyz, v4.xyz);
  r1.y = dot(v9.xyz, v4.xyz);
  r1.z = dot(v10.xyz, v4.xyz);
  r2.x = dot(v11.xyz, v4.xyz);
  r2.y = dot(v12.xyz, v4.xyz);
  r2.z = dot(v13.xyz, v4.xyz);
  r2.xyz = v6.yyy * r2.xyz;
  r1.xyz = v6.xxx * r1.xyz + r2.xyz;
  r2.x = dot(r1.xyz, r1.xyz);
  r2.x = rsqrt(r2.x);
  o6.xyz = r2.xxx * r1.xyz;
  r1.x = -v9.w + r0.y;
  r1.x = saturate(0.200000003 * r1.x);
  r1.y = v15.z * v7.x;
  r1.y = g_wind_vegetation_trunk_movement * r1.y;
  r1.x = r1.y * r1.x;
  r1.y = saturate(0.200000003 * v14.x);
  r1.x = r1.x * r1.y;
  r3.x = r1.w;
  r3.y = v9.w;
  r1.yzw = -r3.xyz + r0.xyz;
  r1.y = dot(r1.yzw, r1.yzw);
  r1.y = sqrt(r1.y);
  r0.xz = -g_wind_direction.xy * r1.xx + r0.xz;
  r1.xzw = r0.xyz + -r3.xyz;
  r2.x = dot(r1.xzw, r1.xzw);
  r2.x = rsqrt(r2.x);
  r1.xzw = r2.xxx * r1.xzw;
  r0.xyz = r1.xzw * r1.yyy + r3.xyz;
  o0.x = dot(r0.xyzw, motion_rows[0]);
  o0.y = dot(r0.xyzw, motion_rows[1]);
  o0.z = dot(r0.xyzw, motion_rows[2]);
  o0.w = dot(r0.xyzw, motion_rows[3]);
  if (g_pixel_selection_active != 0) {
    r1.x = (int)v16.x + g_pixel_selection_start_pixel_index;
    r1.yzw = float3(0,0,0);
    r1.x = t_pixel_selection_object_id_texture.Load(r1.xyz).x;
  }
  o7.x = g_pixel_selection_active ? r1.x : -1;
  o1.xyz = v7.xyz;
  o1.w = 1 + -v14.y;
  o2.xyzw = r0.xyzw;
  o3.xy = v5.xy;
  p3.x = v14.z;
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
  float4 v14 : TEXCOORD6,
  float3 v15 : TEXCOORD7,
  uint v16 : SV_InstanceID0,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
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
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, o0, o1, o2, o3, p3, o4, o5, o6, o7);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
