// ---- Created with 3Dmigoto v1.3.16 on Sun Oct 25 23:44:55 2026

// rigidgrass.hlsl vs30_main_grass (sm_5_0): blades shrink with distance, flat at 100 m x Grass Height Distance (SI.grass_height_distance; 1: the game's).
#include "../shared.h"


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



// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : COLOR0,
  float4 v5 : TEXCOORD1,
  float4 v6 : TEXCOORD2,
  float4 v7 : TEXCOORD3,
  float2 v8 : TEXCOORD4,
  out float4 o0 : SV_Position0,
  out float4 o1 : COLOR0,
  out float4 o2 : TEXCOORD0,
  out float2 o3 : TEXCOORD1,
  out float2 p3 : TEXCOORD6,
  out float4 o4 : TEXCOORD2,
  out float4 o5 : TEXCOORD3,
  out float4 o6 : TEXCOORD4,
  out float4 o7 : TEXCOORD5)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = v5.w;
  r0.y = v6.w;
  r0.z = v7.w;
  r0.xyz = -camera_position.xyz + r0.xyz;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.x = (0.00999999978 / SI.grass_height_distance) * r0.x;  // RenoDX: Grass Height Distance
  r0.x = min(1, r0.x);
  r0.y = saturate(v4.w);
  r0.x = r0.y * -r0.x + 1;
  r0.y = 0.600000024 + v0.y;
  r0.y = r0.x * r0.y + -0.600000024;
  r0.xz = v0.xz;
  r0.w = 1;
  r1.z = dot(v7.xyzw, r0.xyzw);
  r1.x = dot(v5.xyzw, r0.xyzw);
  r1.y = dot(v6.xyzw, r0.xyzw);
  r0.xy = r1.xz * float2(0.25,0.25) + time_in_sec;
  r0.z = 0.300000012 * r0.y;
  r0.xyz = sin(r0.xyz);
  r0.z = wind_vegetation_vstrength * r0.z;
  r0.w = time_in_sec + r1.x;
  r0.w = 5 * r0.w;
  r0.w = cos(r0.w);
  r0.z = r0.z * r0.w;
  r0.w = saturate(-0.699999988 + v4.w);
  r0.w = wind_vegetation_multiplier * r0.w;
  r0.z = r0.w * r0.z;
  r0.w = 3 * time_in_sec;
  r0.w = sin(r0.w);
  r0.w = r0.y * r0.w;
  r0.x = r0.x + r0.y;
  r0.y = wind_vegetation_hstrength * r0.w;
  r0.y = r0.y * 0.5 + 0.5;
  r0.y = wind_vegetation_hstrength * r0.y;
  r0.w = wind_vegetation_multiplier * v4.w;
  r2.xy = wind_direction.xy * r0.ww;
  r0.yw = r2.xy * r0.yy;
  r2.xz = float2(0.5,0.5) * r0.yw;
  r2.y = 0;
  r0.yzw = r0.zzz * float3(0,2.33333325,0) + r2.xyz;
  r1.xyz = r1.xyz + r0.yzw;
  r1.w = 1;
  o0.x = dot(r1.xyzw, view_projection._m00_m10_m20_m30);
  o0.y = dot(r1.xyzw, view_projection._m01_m11_m21_m31);
  o0.z = dot(r1.xyzw, view_projection._m02_m12_m22_m32);
  r1.w = dot(r1.xyzw, view_projection._m03_m13_m23_m33);
  o0.w = r1.w;
  o1.xyzw = v4.xyzw;
  o2.xyzw = r1.xyzw;
  r1.xyz = -camera_position.xyz + r1.xyz;
  o3.xy = v1.xy;
  p3.xy = v8.xy;
  r1.w = sin(time_in_sec);
  r0.x = r1.w * r0.x;
  r0.x = 0.300000012 * r0.x;
  r2.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r3.x = dot(v5.xyz, r2.xyz);
  r3.y = dot(v6.xyz, r2.xyz);
  r3.z = dot(v7.xyz, r2.xyz);
  r0.xyz = r0.yzw * r0.xxx + r3.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  o4.xyz = r0.xyz * r0.www;
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o5.x = dot(v5.xyz, r0.xyz);
  o5.y = dot(v6.xyz, r0.xyz);
  o5.z = dot(v7.xyz, r0.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.y = rsqrt(r0.x);
  o6.w = sqrt(r0.x);
  o6.xyz = r1.xyz * r0.yyy;
  o7.x = v5.w;
  o7.y = v6.w;
  o7.z = v7.w;
  o7.w = 1;
  return;
}