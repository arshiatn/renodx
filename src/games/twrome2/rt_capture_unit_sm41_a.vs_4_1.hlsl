// Attila world motion (anti_aliasing.hpp): the game's 0x4AA6E3C2 with its world position for ray tracing's stream output
// (rt_scene.hpp).
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view_projection : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4 camera_near_far : packoffset(c21);
  float time_in_sec : packoffset(c22);
  float2 g_inverse_focal_length : packoffset(c22.y);
  float g_vertical_fov : packoffset(c22.w);
  float4 g_screen_size : packoffset(c23);
  float g_vpos_texel_offset : packoffset(c24);
  float4 g_viewport_dimensions : packoffset(c25);
  float4 g_camera_temp0 : packoffset(c26);
  float4 g_camera_temp1 : packoffset(c27);
  float4 g_camera_temp2 : packoffset(c28);
}

cbuffer variant_animation_size : register(b1)
{
  float2 g_animation_texture_height : packoffset(c0);
  float2 g_animation_texture_width : packoffset(c0.z);
}

SamplerState s_animation_map_s : register(s0);
Texture2D<float4> s_animation_map : register(t0);


// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float4 v0,
  float4 v1,
  float4 v2,
  float2 v3,
  float4 v4,
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7,
  inout float4 o8)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(255.009995,255.009995) * v1.zy;
  r0.xy = floor(r0.xy);
  r0.xy = (int2)r0.xy;
  r0.xy = (int2)r0.xy * int2(3,3);
  r0.xy = (int2)r0.xy;
  r1.y = r0.y * g_animation_texture_width.y + g_animation_texture_width.y;
  r1.x = g_animation_texture_width.y + r1.y;
  r2.w = g_animation_texture_height.y * v9.x;
  r1.w = r2.w;
  r3.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.xw, 0).xyzw;
  r4.xyz = v0.xyz;
  r4.w = 1;
  r5.z = dot(r4.xyzw, r3.xyzw);
  r1.z = g_animation_texture_width.y * r0.y;
  r6.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.zw, 0).xyzw;
  r1.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.yw, 0).xyzw;
  r5.x = dot(r4.xyzw, r6.xyzw);
  r5.y = dot(r4.xyzw, r1.xyzw);
  r0.y = 1 + -v1.x;
  r5.xyz = r0.yyy * r5.xyz;
  r2.y = r0.x * g_animation_texture_width.y + g_animation_texture_width.y;
  r2.z = g_animation_texture_width.y * r0.x;
  r7.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r2.zw, 0).xyzw;
  r2.x = g_animation_texture_width.y + r2.y;
  r8.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r2.yw, 0).xyzw;
  r2.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r2.xw, 0).xyzw;
  r9.z = dot(r4.xyzw, r2.xyzw);
  r9.x = dot(r4.xyzw, r7.xyzw);
  r9.y = dot(r4.xyzw, r8.xyzw);
  r4.xyz = v1.xxx * r9.xyz + r5.xyz;
  r4.w = 1;
  r5.x = dot(r4.xyzw, v6.xyzw);
  r5.y = dot(r4.xyzw, v7.xyzw);
  r5.z = dot(r4.xyzw, v8.xyzw);
  r5.w = 1;
  o0.x = dot(r5.xyzw, motion_rows[0]);
  o0.y = dot(r5.xyzw, motion_rows[1]);
  o0.z = dot(r5.xyzw, motion_rows[2]);
  o0.w = dot(r5.xyzw, motion_rows[3]);
  o1.xyzw = r5.xyzw;
  o2.xyzw = v3.xyxy;
  r0.xzw = v2.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r4.z = dot(r0.xzw, r3.xyz);
  r4.x = dot(r0.xzw, r6.xyz);
  r4.y = dot(r0.xzw, r1.xyz);
  r4.xyz = r4.xyz * r0.yyy;
  r5.z = dot(r0.xzw, r2.xyz);
  r5.x = dot(r0.xzw, r7.xyz);
  r5.y = dot(r0.xzw, r8.xyz);
  r0.xzw = v1.xxx * r5.xyz + r4.xyz;
  o3.x = dot(r0.xzw, v6.xyz);
  o3.y = dot(r0.xzw, v7.xyz);
  o3.z = dot(r0.xzw, v8.xyz);
  o3.w = v3.x;
  r0.xzw = v5.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r4.z = dot(r0.xzw, r3.xyz);
  r4.x = dot(r0.xzw, r6.xyz);
  r4.y = dot(r0.xzw, r1.xyz);
  r4.xyz = r4.xyz * r0.yyy;
  r5.z = dot(r0.xzw, r2.xyz);
  r5.x = dot(r0.xzw, r7.xyz);
  r5.y = dot(r0.xzw, r8.xyz);
  r0.xzw = v1.xxx * r5.xyz + r4.xyz;
  o4.x = dot(r0.xzw, v6.xyz);
  o4.y = dot(r0.xzw, v7.xyz);
  o4.z = dot(r0.xzw, v8.xyz);
  o4.w = v3.y;
  o5.xy = v3.xy;
  o5.z = v9.y;
  r0.xzw = v4.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r3.z = dot(r0.xzw, r3.xyz);
  r3.x = dot(r0.xzw, r6.xyz);
  r3.y = dot(r0.xzw, r1.xyz);
  r1.xyz = r3.xyz * r0.yyy;
  r2.z = dot(r0.xzw, r2.xyz);
  r2.x = dot(r0.xzw, r7.xyz);
  r2.y = dot(r0.xzw, r8.xyz);
  r0.xyz = v1.xxx * r2.xyz + r1.xyz;
  o6.x = dot(r0.xyz, v6.xyz);
  o6.y = dot(r0.xyz, v7.xyz);
  o6.z = dot(r0.xyz, v8.xyz);
  o6.w = 1;
  o7.xy = v9.zw;
  o7.zw = v10.xy;
  o8.xy = v10.zw;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float2 v3 : POSITION3,
  float4 v4 : POSITION4,
  float4 v5 : POSITION5,
  float4 v6 : TEXCOORD0,
  float4 v7 : TEXCOORD1,
  float4 v8 : TEXCOORD2,
  float4 v9 : TEXCOORD3,
  float4 v10 : TEXCOORD4,
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
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, o0, o1, o2, o3, o4, o5, o6, o7, o8);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
