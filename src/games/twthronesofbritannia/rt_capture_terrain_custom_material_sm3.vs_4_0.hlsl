// Attila world motion (anti_aliasing.hpp): the game's 0x44EBD517 with its world position for ray tracing's stream output
// (rt_scene.hpp).
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

cbuffer vs_height_map_cb : register(b1)
{
  float g_height_map_lf_scale : packoffset(c0);
  float g_height_map_hf_scale : packoffset(c0.y);
  float g_height_map_vertical_offset : packoffset(c0.z);
  float2 g_blend_lf_min_max : packoffset(c1);
  float4 g_height_map_dimension : packoffset(c2);
  float2 g_world_dimensions : packoffset(c3);
  float2 g_infield_dimensions : packoffset(c3.z);
  float g_lf_uv_offset : packoffset(c4);
  float g_layer1_texture_index : packoffset(c4.y);
  float g_layer2_texture_index : packoffset(c4.z);
  float g_far_terrain_scale : packoffset(c4.w);
  float2 g_far_render_bounds : packoffset(c5);
  float2 g_seabed_height : packoffset(c5.z);
}

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
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(1,1) + -g_height_map_dimension.zw;
  r0.xy = max(float2(0,0), r0.xy);
  r1.xyz = float3(1,2,-1) * v0.xyz;
  r1.w = 1;
  r2.x = dot(r1.xyzw, v5.xyzw);
  r2.z = dot(r1.xyzw, v7.xyzw);
  r0.z = dot(r1.xyzw, v6.xyzw);
  r0.z = g_height_map_hf_scale * r0.z;
  r1.xy = r2.xz / g_world_dimensions.xy;
  r1.xy = g_lf_uv_offset + r1.xy;
  r1.z = 1 + -r1.y;
  r0.xy = min(r1.xz, r0.xy);
  r0.xy = g_height_map_dimension.xy * r0.xy;
  r1.xy = (uint2)r0.xy;
  r0.xy = frac(r0.xy);
  r3.xy = (int2)r1.xy + int2(1,1);
  r1.w = r3.y;
  r1.z = 0;
  r4.xyzw = t_low_res_height_map.Load(r1.xwz).xyzw;
  r3.zw = r1.yz;
  r1.xyzw = t_low_res_height_map.Load(r1.xyz).xyzw;
  r5.xyzw = t_low_res_height_map.Load(r3.xyw).xyzw;
  r3.xyzw = t_low_res_height_map.Load(r3.xzw).xyzw;
  r0.w = r3.x + -r1.x;
  r0.w = r0.x * r0.w + r1.x;
  r1.x = r5.x + -r4.x;
  r0.x = r0.x * r1.x + r4.x;
  r0.x = r0.x + -r0.w;
  r0.x = r0.y * r0.x + r0.w;
  r0.x = r0.x * g_height_map_lf_scale + r0.z;
  r0.x = -g_height_map_vertical_offset + r0.x;
  r0.y = cmp(0 < r0.x);
  r0.z = cmp(r0.x < 0);
  r0.y = (int)-r0.y + (int)r0.z;
  r0.y = (int)r0.y;
  r0.zw = -camera_position.xz + r2.xz;
  r0.z = dot(r0.zw, r0.zw);
  r0.z = sqrt(r0.z);
  r0.z = -20 + r0.z;
  r0.z = saturate(0.100000001 * r0.z);
  r0.y = r0.z * r0.y;
  r2.y = r0.y * 0.0900000036 + r0.x;
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
  r0.xyz = float3(1,1,-1) * r0.xyz;
  o4.x = dot(r0.xyz, v5.xyz);
  o4.y = dot(r0.xyz, v6.xyz);
  o4.z = dot(r0.xyz, v7.xyz);
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  o5.x = dot(r0.xyz, v5.xyz);
  o5.y = dot(r0.xyz, v6.xyz);
  o5.z = dot(r0.xyz, v7.xyz);
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
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
