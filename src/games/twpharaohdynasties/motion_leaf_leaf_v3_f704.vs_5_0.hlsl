// Attila wind motion (anti_aliasing.hpp): the game's 0xF704C8EC with its wind at this frame's and last frame's time; outputs SV_Position as the game's, this frame's
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

cbuffer vegetation_tint_colours : register(b3)
{
  float4 g_tint_colours[3] : packoffset(c0);
  float4 g_billboard_params : packoffset(c3);
  float g_leaf_cull_distance : packoffset(c4);
  float3 g_leaf_cull_camera_position : packoffset(c4.y);
  float g_has_diffuse_burn_texture : packoffset(c5);
  float g_leaf_automatic_normals : packoffset(c5.y);
  float2 g_tint_params : packoffset(c5.z);
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
  inout float4 p2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 p5,
  inout float4 o6,
  inout float4 p6,
  inout float4 o7,
  inout uint4 o8,
  inout float4 p8,
  inout float4 o9)
{
  float4 r0,r1,r2,r3,r4,r5;
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
  r5.x = dot(r0.xyzw, v1.xyzw);
  r5.y = dot(v12.xyzw, v1.xyzw);
  r5.z = dot(r2.xyzw, v1.xyzw);
  r4.w = v0.w;
  r5.w = v1.w;
  r5.xyzw = v6.yyyy * r5.xyzw;
  r4.xyzw = v6.xxxx * r4.xyzw + r5.xyzw;
  r0.x = dot(v8.xyz, v2.xyz);
  r0.y = dot(v9.xyz, v2.xyz);
  r0.z = dot(v10.xyz, v2.xyz);
  r1.x = dot(v11.xyz, v2.xyz);
  r1.y = dot(v12.xyz, v2.xyz);
  r1.z = dot(v13.xyz, v2.xyz);
  r1.xyz = v6.yyy * r1.xyz;
  r0.xyz = v6.xxx * r0.xyz + r1.xyz;
  r1.x = dot(r0.xyz, r0.xyz);
  r1.x = rsqrt(r1.x);
  o3.xyz = r1.xxx * r0.xyz;
  r0.x = dot(v8.xyz, v3.xyz);
  r0.y = dot(v9.xyz, v3.xyz);
  r0.z = dot(v10.xyz, v3.xyz);
  r1.x = dot(v11.xyz, v3.xyz);
  r1.y = dot(v12.xyz, v3.xyz);
  r1.z = dot(v13.xyz, v3.xyz);
  r1.xyz = v6.yyy * r1.xyz;
  r0.xyz = v6.xxx * r0.xyz + r1.xyz;
  r1.x = dot(r0.xyz, r0.xyz);
  r1.x = rsqrt(r1.x);
  o4.xyz = r1.xxx * r0.xyz;
  r0.x = dot(v8.xyz, v4.xyz);
  r0.y = dot(v9.xyz, v4.xyz);
  r0.z = dot(v10.xyz, v4.xyz);
  r1.x = dot(v11.xyz, v4.xyz);
  r1.y = dot(v12.xyz, v4.xyz);
  r1.z = dot(v13.xyz, v4.xyz);
  r1.xyz = v6.yyy * r1.xyz;
  r0.xyz = v6.xxx * r0.xyz + r1.xyz;
  r1.x = dot(r0.xyz, r0.xyz);
  r1.x = rsqrt(r1.x);
  o5.xyz = r1.xxx * r0.xyz;
  r0.x = -v9.w + r4.y;
  r0.x = saturate(0.200000003 * r0.x);
  r0.y = v15.z * v7.x;
  r0.y = g_wind_vegetation_trunk_movement * r0.y;
  r0.y = r0.y * r0.x;
  r0.z = saturate(0.200000003 * v14.x);
  r0.y = r0.y * r0.z;
  r3.x = r1.w;
  r3.y = v9.w;
  r1.xyz = r4.xyz + -r3.xyz;
  r0.z = dot(r1.xyz, r1.xyz);
  r0.z = sqrt(r0.z);
  r4.xz = -g_wind_direction.xy * r0.yy + r4.xz;
  r1.xyz = r4.xyz + -r3.xyz;
  r0.y = dot(r1.xyz, r1.xyz);
  r0.y = rsqrt(r0.y);
  r1.xyz = r1.xyz * r0.yyy;
  r0.y = r1.x * r0.z;
  r1.xyz = r1.xyz * r0.zzz + r3.xyz;
  r0.z = v9.w + r1.w;
  r0.z = frac(r0.z);
  r0.yz = g_wind_vegetation_weaviness.xy * r0.yy + r0.zz;
  r0.yz = v7.yy + r0.yz;
  r0.yz = frac(r0.yz);
  r2.xy = -g_wind_direction.xy * r0.xx;
  r2.xy = v15.zz * r2.xy;
  r2.xy = v7.zz * r2.xy;
  r4.xz = r2.xy * g_wind_vegetation_hstrength + r1.xz;
  r0.yz = float2(14,14) * r0.yz;
  r0.yz = motion_time * g_wind_vegetation_tremble.xy + r0.yz;
  r0.yz = sin(r0.yz);
  r0.z = r0.z + -r0.y;
  r0.y = g_wind_vegetation_lerp * r0.z + r0.y;
  r0.x = r0.x * r0.y;
  r0.x = v7.z * r0.x;
  r4.y = r0.x * g_wind_vegetation_vstrength + r1.y;
  o0.x = dot(r4.xyzw, motion_rows[0]);
  o0.y = dot(r4.xyzw, motion_rows[1]);
  o0.z = dot(r4.xyzw, motion_rows[2]);
  r0.x = dot(r4.xyzw, motion_rows[3]);
  o3.w = 1 + -v14.y;
  p5.x = 0.100000001 * v1.y;
  r0.yz = float2(0.100000001,0.100000001) * r4.xz;
  r0.yz = frac(r0.yz);
  r1.xy = float2(1,1) + -r0.yz;
  r0.yz = min(r1.xy, r0.yz);
  o2.z = r0.y + r0.z;
  if (g_pixel_selection_active != 0) {
    r1.x = (int)v16.x + g_pixel_selection_start_pixel_index;
    r1.yzw = float3(0,0,0);
    r0.y = t_pixel_selection_object_id_texture.Load(r1.xyz).x;
  }
  o8.x = g_pixel_selection_active ? r0.y : -1;
  r0.y = 264 * abs(v8.w);
  r0.y = (uint)r0.y;
  r0.z = (int)r0.y * 0xbcb9b7eb;
  r0.z = (uint)r0.z >> 11;
  r0.y = mad((int)r0.y, 0xbcb9b7eb, (int)r0.z);
  r0.z = (int)r0.y & 31;
  r1.y = (uint)r0.z;
  if (5 == 0) r0.y = 0; else if (5+5 < 32) {   r0.y = (uint)r0.y << (32-(5 + 5)); r0.y = (uint)r0.y >> (32-5);  } else r0.y = (uint)r0.y >> 5;
  if (5 == 0) r0.z = 0; else if (5+10 < 32) {   r0.z = (uint)r0.y << (32-(5 + 10)); r0.z = (uint)r0.z >> (32-5);  } else r0.z = (uint)r0.y >> 10;
  r1.zw = (uint2)r0.yz;
  r0.y = r1.y + r1.z;
  r0.y = r0.y + r1.w;
  r0.z = 0.333333343 * r0.y;
  r1.xyz = -r0.yyy * float3(0.333333343,0.333333343,0.333333343) + r1.yzw;
  r1.xyz = r1.xyz * g_tint_params.yyy + r0.zzz;
  r1.xyz = r1.xyz * float3(0.0645161271,0.0645161271,0.0645161271) + float3(-1,-1,-1);
  p8.xyz = r1.xyz * g_tint_params.xxx + float3(1,1,1);
  r1.xyz = float3(1,0.100000001,1) * v1.xyz;
  r2.x = dot(v11.xyz, r1.xyz);
  r2.y = dot(v12.xyz, r1.xyz);
  r2.z = dot(v13.xyz, r1.xyz);
  r0.y = dot(r2.xyz, r2.xyz);
  r0.y = rsqrt(r0.y);
  o6.xyz = r2.xyz * r0.yyy;
  o0.w = r0.x;
  o1.xyz = r4.xyz;
  o1.w = r0.x;
  o2.xy = v5.xy;
  p2.x = v14.z;
  o4.w = v7.w;
  p6.x = v14.w;
  o7.xyz = v7.xyz;
  o9.x = r0.w;
  o9.y = v12.w;
  o9.z = r2.w;
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
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 p2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 p5 = 0;
  float4 o6 = 0;
  float4 p6 = 0;
  float4 o7 = 0;
  uint4 o8 = 0;
  float4 p8 = 0;
  float4 o9 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, o0, o1, o2, p2, o3, o4, o5, p5, o6, p6, o7, o8, p8, o9);
  const float4 world = o0;
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, o0, o1, o2, p2, o3, o4, o5, p5, o6, p6, o7, o8, p8, o9);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
