// Attila world motion (anti_aliasing.hpp): the game's 0x8BE644E7 with its world position for ray tracing's stream output
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
  float g_overlay_parchment_lerp : packoffset(c47.y);
  float g_overlay_palette_alpha : packoffset(c47.z);
  float g_debug_tonemapping : packoffset(c47.w);
  float4 g_blood_remap : packoffset(c48);
}

cbuffer vs_height_map_cb : register(b1)
{
  float g_height_map_lf_scale : packoffset(c0);
  float g_height_map_hf_scale : packoffset(c0.y);
  float g_height_map_vertical_offset : packoffset(c0.z);
  float2 g_world_dimensions : packoffset(c1);
  float g_lf_uv_offset : packoffset(c1.z);
  float4 g_layer0_texture_index : packoffset(c2);
  float4 g_layer1_texture_index : packoffset(c3);
  float4 g_layer2_texture_index : packoffset(c4);
  float4 g_layer3_texture_index : packoffset(c5);
  float g_far_terrain_scale : packoffset(c6);
  float2 g_far_render_bounds : packoffset(c6.y);
  float2 g_custom_tile_size : packoffset(c7);
}

cbuffer ps_height_map_cb : register(b2)
{
  float4 g_mid_distance_details : packoffset(c0);
  float4 g_mid_texture_rotation : packoffset(c1);
  float g_mid_distance_normal_strength : packoffset(c2);
  float2 g_mid_distance_slope : packoffset(c2.y);
  float3 g_normal_scales : packoffset(c3);
  float g_far_tile_distance : packoffset(c3.w);
  float3 g_closeup_noise_params : packoffset(c4);
  float2 g_far_blend_range : packoffset(c5);
  float4 g_far_world_write_uv_offset : packoffset(c6);
  float g_far_world_read_uv_offset : packoffset(c7);
  float4 g_far_world_uvs : packoffset(c8);
  float4x4 g_grid_transform : packoffset(c9);
  float4 g_grid_colour0 : packoffset(c13);
  float4 g_grid_colour1 : packoffset(c14);
  float2 g_xz_offset : packoffset(c15);
  float g_create_skirt : packoffset(c15.z);
  float4 g_tile_aabb : packoffset(c16);
  float g_use_global_diffuse : packoffset(c17);
  float4 g_color_overlay_2_weight_1 : packoffset(c18);
  float4 g_color_overlay_2_weight_2 : packoffset(c19);
  float g_is_campaign : packoffset(c20);
}

SamplerState s_low_res_height_map_s : register(s0);
Texture2D<float4> t_low_res_height_map : register(t0);


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
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 p3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.z = g_custom_tile_size.y * 128 + v0.z;
  r0.xy = v0.xy;
  r0.w = 1;
  r1.x = dot(r0.xyzw, v6.xyzw);
  r1.x = g_height_map_hf_scale * r1.x;
  r2.x = dot(r0.xyzw, v5.xyzw);
  r2.z = dot(r0.xyzw, v7.xyzw);
  r0.xy = -g_xz_offset.xy + r2.xz;
  r0.xy = r0.xy / g_world_dimensions.xy;
  r0.xy = g_lf_uv_offset + r0.xy;
  r0.z = 1 + -r0.y;
  r0.x = t_low_res_height_map.SampleLevel(s_low_res_height_map_s, r0.xz, 0).x;
  r0.x = r0.x * g_height_map_lf_scale + r1.x;
  r2.y = -g_height_map_vertical_offset + r0.x;
  r2.w = 1;
  o0.x = dot(r2.xyzw, motion_rows[0]);
  o0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  o0.w = dot(r2.xyzw, motion_rows[3]);
  o1.xyzw = r2.xyzw;
  o2.xyzw = v4.xyzw;
  o3.x = v1.w;
  p3.xy = v4.xy;
  r0.xyz = v1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o4.x = dot(r0.xyz, v5.xyz);
  o4.y = dot(r0.xyz, v6.xyz);
  o4.z = dot(r0.xyz, v7.xyz);
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o5.x = dot(r0.xyz, v5.xyz);
  o5.y = dot(r0.xyz, v6.xyz);
  o5.z = dot(r0.xyz, v7.xyz);
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o6.x = dot(r0.xyz, v5.xyz);
  o6.y = dot(r0.xyz, v6.xyz);
  o6.z = dot(r0.xyz, v7.xyz);
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float4 v2 : NORMAL1,
  float4 v3 : NORMAL2,
  float4 v4 : TEXCOORD0,
  float4 v5 : POSITION1,
  float4 v6 : POSITION2,
  float4 v7 : POSITION3,
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
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, o0, o1, o2, o3, p3, o4, o5, o6);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
