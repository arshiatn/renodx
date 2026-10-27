// Attila world motion (anti_aliasing.hpp): the game's 0x1FC50EA9 with its world position for ray tracing's stream output
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
  float g_mid_distance_normal_strength : packoffset(c1);
  float2 g_mid_distance_slope : packoffset(c1.y);
  float3 g_normal_scales : packoffset(c2);
  float g_far_tile_distance : packoffset(c2.w);
  float2 g_far_blend_range : packoffset(c3);
  float4 g_far_world_write_uv_offset : packoffset(c4);
  float g_far_world_read_uv_offset : packoffset(c5);
  float4 g_far_world_uvs : packoffset(c6);
  float4x4 g_grid_transform : packoffset(c7);
  float4 g_grid_colour0 : packoffset(c11);
  float4 g_grid_colour1 : packoffset(c12);
  float2 g_xz_offset : packoffset(c13);
  float g_create_skirt : packoffset(c13.z);
  float4 g_tile_aabb : packoffset(c14);
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
  inout float4 o0,
  inout float4 o1,
  inout float4 o2)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  t_low_res_height_map.GetDimensions(0, fDest.x, fDest.y, fDest.z);
  r0.xy = fDest.xy;
  r0.zw = float2(1,1) / r0.xy;
  r1.xy = float2(1,1) + -r0.zw;
  r1.xy = max(float2(0,0), r1.xy);
  r2.xyz = v0.xyz;
  r2.w = 1;
  r3.x = dot(r2.xyzw, v1.xyzw);
  r3.z = dot(r2.xyzw, v3.xyzw);
  r1.z = dot(r2.xyzw, v2.xyzw);
  r1.z = g_height_map_hf_scale * r1.z;
  r2.xy = -g_xz_offset.xy + r3.xz;
  r2.xy = r2.xy / g_world_dimensions.xy;
  r2.xy = g_lf_uv_offset + r2.xy;
  r2.z = 1 + -r2.y;
  r1.xy = min(r2.xz, r1.xy);
  r0.zw = r0.zw * float2(0.5,0.5) + r1.xy;
  r0.xy = r1.xy * r0.xy + float2(9.99999975e-005,9.99999975e-005);
  r0.xy = frac(r0.xy);
  r2.xyzw = t_low_res_height_map.Gather(s_low_res_height_map_s, r0.zw).xyzw;
  r0.zw = r2.zy + -r2.wx;
  r0.xz = r0.xx * r0.zw + r2.wx;
  r0.z = r0.z + -r0.x;
  r0.x = r0.y * r0.z + r0.x;
  r0.x = r0.x * g_height_map_lf_scale + r1.z;
  r3.y = -g_height_map_vertical_offset + r0.x;
  r3.w = 1;
  o0.x = dot(r3.xyzw, motion_rows[0]);
  o0.y = dot(r3.xyzw, motion_rows[1]);
  o0.z = dot(r3.xyzw, motion_rows[2]);
  o0.w = dot(r3.xyzw, motion_rows[3]);
  o1.xyzw = r3.xyzw;
  o2.x = dot(r3.xyzw, view._m02_m12_m22_m32);
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, o0, o1, o2);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
