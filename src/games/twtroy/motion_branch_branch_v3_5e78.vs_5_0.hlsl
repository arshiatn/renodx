// Attila wind motion (anti_aliasing.hpp): the game's 0x5E788ED3 with its wind at this frame's and last frame's time; outputs SV_Position as the game's, this frame's
// and last frame's clip positions without the jitter.
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
  float g_campaign_vegetation_tint_scale : packoffset(c4);
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

  r0.x = dot(v8.xyzw, v0.xyzw);
  r0.y = dot(v9.xyzw, v0.xyzw);
  r0.z = dot(v10.xyzw, v0.xyzw);
  r1.x = dot(v11.xyzw, v1.xyzw);
  r1.y = dot(v12.xyzw, v1.xyzw);
  r1.z = dot(v13.xyzw, v1.xyzw);
  r0.w = v0.w;
  r1.w = v1.w;
  r1.xyzw = v6.yyyy * r1.xyzw;
  r0.xyzw = v6.xxxx * r0.xyzw + r1.xyzw;
  r1.x = dot(v8.xyz, v2.xyz);
  r1.y = dot(v9.xyz, v2.xyz);
  r1.z = dot(v10.xyz, v2.xyz);
  r2.x = dot(v11.xyz, v2.xyz);
  r2.y = dot(v12.xyz, v2.xyz);
  r2.z = dot(v13.xyz, v2.xyz);
  r2.xyz = v6.yyy * r2.xyz;
  r1.xyz = v6.xxx * r1.xyz + r2.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  o4.xyz = r1.xyz * r1.www;
  r1.x = dot(v8.xyz, v3.xyz);
  r1.y = dot(v9.xyz, v3.xyz);
  r1.z = dot(v10.xyz, v3.xyz);
  r2.x = dot(v11.xyz, v3.xyz);
  r2.y = dot(v12.xyz, v3.xyz);
  r2.z = dot(v13.xyz, v3.xyz);
  r2.xyz = v6.yyy * r2.xyz;
  r1.xyz = v6.xxx * r1.xyz + r2.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  o5.xyz = r1.xyz * r1.www;
  r1.x = dot(v8.xyz, v4.xyz);
  r1.y = dot(v9.xyz, v4.xyz);
  r1.z = dot(v10.xyz, v4.xyz);
  r2.x = dot(v11.xyz, v4.xyz);
  r2.y = dot(v12.xyz, v4.xyz);
  r2.z = dot(v13.xyz, v4.xyz);
  r2.xyz = v6.yyy * r2.xyz;
  r1.xyz = v6.xxx * r1.xyz + r2.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  o6.xyz = r1.xyz * r1.www;
  r1.x = v14.x * 4 + motion_time;
  r1.yz = float2(3,0.5) * motion_time;
  r1.xyz = sin(r1.xyz);
  r1.x = r1.x * r1.y;
  r1.x = g_wind_vegetation_hstrength * r1.x;
  r1.x = r1.x * 0.5 + 1;
  r1.y = r1.z * 0.5 + 0.75;
  r1.x = r1.x * r1.y;
  r1.y = v14.x * 2 + motion_time;
  r1.y = 0.300000012 * r1.y;
  r1.y = sin(r1.y);
  r1.xy = float2(g_wind_vegetation_hstrength, g_wind_vegetation_vstrength) * r1.xy;
  r1.z = motion_time + r0.x;
  r1.z = 6 * r1.z;
  r1.z = sin(r1.z);
  r1.y = r1.y * r1.z;
  r1.z = saturate(-0.699999988 + v7.w);
  r1.z = g_wind_vegetation_multiplier * r1.z;
  r1.w = g_wind_vegetation_multiplier * v7.w;
  r1.w = v7.w * r1.w;
  r2.xy = g_wind_direction.xy * r1.ww;
  r2.xz = r2.xy * r1.xx;
  r2.y = 0;
  r2.xyz = r2.xyz + r0.xyz;
  r1.x = r1.z * r1.y;
  r0.xyz = r1.xxx * float3(0,3.33333325,0) + r2.xyz;
  o0.x = dot(r0.xyzw, motion_rows[0]);
  o0.y = dot(r0.xyzw, motion_rows[1]);
  o0.z = dot(r0.xyzw, motion_rows[2]);
  o0.w = dot(r0.xyzw, motion_rows[3]);
  if (g_pixel_selection_active != 0) {
    r1.x = (int)v15.x + g_pixel_selection_start_pixel_index;
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
  float3 v14 : TEXCOORD6,
  uint v15 : SV_InstanceID0,
  out float4 motion_position : SV_Position0,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1)
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
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, o3, p3, o4, o5, o6, o7);
  const float4 world = o0;
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, o3, p3, o4, o5, o6, o7);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
