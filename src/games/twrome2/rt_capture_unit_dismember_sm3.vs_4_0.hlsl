// Attila world motion (anti_aliasing.hpp): the game's 0x1675B565 with its world position for ray tracing's stream output
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
  float4 g_camera_temp0 : packoffset(c30);
  float4 g_camera_temp1 : packoffset(c31);
  float4 g_camera_temp2 : packoffset(c32);
  float4 g_clip_rect : packoffset(c33);
  float g_hide_foliage : packoffset(c34);
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
  float2 v11,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7,
  inout float4 o8,
  inout float4 o9)
{
  const float4 icb[] = { { 1.000000, 0, 0, 0},
                              { 0, 1.000000, 0, 0},
                              { 0, 0, 1.000000, 0},
                              { 0, 0, 0, 1.000000} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(255.009995,255.009995) * v1.zy;
  r0.xy = floor(r0.xy);
  r0.zw = (int2)r0.xy;
  r0.zw = (int2)r0.zw * int2(3,3);
  r0.zw = (int2)r0.zw;
  r1.w = g_animation_texture_width.y * r0.z;
  r1.y = r0.z * g_animation_texture_width.y + g_animation_texture_width.y;
  r1.x = g_animation_texture_width.y + r1.y;
  r1.z = g_animation_texture_height.y * v9.x;
  r2.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.wz, 0).xyzw;
  r3.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.yz, 0).xyzw;
  r4.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.xz, 0).xyzw;
  r1.w = g_animation_texture_width.y * r0.w;
  r1.y = r0.w * g_animation_texture_width.y + g_animation_texture_width.y;
  r1.x = g_animation_texture_width.y + r1.y;
  r5.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.wz, 0).xyzw;
  r6.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.yz, 0).xyzw;
  r7.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.xz, 0).xyzw;
  r0.z = cmp(0.100000001 < v11.x);
  if (r0.z != 0) {
    r0.xy = float2(0.00999999978,0.00999999978) + r0.xy;
    r0.xy = float2(0.25,0.25) * r0.xy;
    r0.zw = floor(r0.xy);
    r0.zw = v11.xx * float2(3,3) + r0.zw;
    r1.xy = g_animation_texture_width.yy * r0.wz;
    r8.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.yz, 0).xyzw;
    r0.zw = cmp(r0.xy >= -r0.xy);
    r0.xy = frac(abs(r0.xy));
    r0.xy = r0.zw ? r0.xy : -r0.xy;
    r0.xy = float2(4,4) * r0.xy;
    r0.xy = floor(r0.xy);
    r0.xy = (int2)r0.xy;
    r0.x = dot(r8.xyzw, icb[r0.x+0].xyzw);
    r1.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.xz, 0).xyzw;
    r0.y = dot(r1.xyzw, icb[r0.y+0].xyzw);
    r0.zw = saturate(r0.yx);
    r0.w = r0.w + -r0.z;
    r1.x = v1.x * r0.w + r0.z;
    r0.x = r0.x + -r0.y;
    r1.y = v1.x * r0.x + r0.y;
  } else {
    r1.xy = float2(1,1);
  }
  r0.x = 1 + -v1.x;
  r8.xyz = v0.xyz;
  r8.w = 1;
  r9.x = dot(r8.xyzw, r2.xyzw);
  r9.y = dot(r8.xyzw, r3.xyzw);
  r9.z = dot(r8.xyzw, r4.xyzw);
  r10.x = dot(r8.xyzw, r5.xyzw);
  r10.y = dot(r8.xyzw, r6.xyzw);
  r10.z = dot(r8.xyzw, r7.xyzw);
  r0.yzw = r10.xyz * r0.xxx;
  r8.xyz = v1.xxx * r9.xyz + r0.yzw;
  r0.yzw = v4.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r9.x = dot(r0.yzw, r2.xyz);
  r9.y = dot(r0.yzw, r3.xyz);
  r9.z = dot(r0.yzw, r4.xyz);
  r10.x = dot(r0.yzw, r5.xyz);
  r10.y = dot(r0.yzw, r6.xyz);
  r10.z = dot(r0.yzw, r7.xyz);
  r0.yzw = r10.xyz * r0.xxx;
  r0.yzw = v1.xxx * r9.xyz + r0.yzw;
  r9.xyz = v2.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r10.x = dot(r9.xyz, r2.xyz);
  r10.y = dot(r9.xyz, r3.xyz);
  r10.z = dot(r9.xyz, r4.xyz);
  r11.x = dot(r9.xyz, r5.xyz);
  r11.y = dot(r9.xyz, r6.xyz);
  r11.z = dot(r9.xyz, r7.xyz);
  r9.xyz = r11.xyz * r0.xxx;
  r9.xyz = v1.xxx * r10.xyz + r9.xyz;
  r10.xyz = v5.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r2.x = dot(r10.xyz, r2.xyz);
  r2.y = dot(r10.xyz, r3.xyz);
  r2.z = dot(r10.xyz, r4.xyz);
  r3.x = dot(r10.xyz, r5.xyz);
  r3.y = dot(r10.xyz, r6.xyz);
  r3.z = dot(r10.xyz, r7.xyz);
  r3.xyz = r3.xyz * r0.xxx;
  r2.xyz = v1.xxx * r2.xyz + r3.xyz;
  r8.w = 1;
  r3.x = dot(r8.xyzw, v6.xyzw);
  r3.y = dot(r8.xyzw, v7.xyzw);
  r3.z = dot(r8.xyzw, v8.xyzw);
  r3.w = 1;
  o0.x = dot(r3.xyzw, motion_rows[0]);
  o0.y = dot(r3.xyzw, motion_rows[1]);
  o0.z = dot(r3.xyzw, motion_rows[2]);
  o0.w = dot(r3.xyzw, motion_rows[3]);
  o3.x = dot(r9.xyz, v6.xyz);
  o3.y = dot(r9.xyz, v7.xyz);
  o3.z = dot(r9.xyz, v8.xyz);
  o6.x = dot(r0.yzw, v6.xyz);
  o6.y = dot(r0.yzw, v7.xyz);
  o6.z = dot(r0.yzw, v8.xyz);
  o4.x = dot(r2.xyz, v6.xyz);
  o4.y = dot(r2.xyz, v7.xyz);
  o4.z = dot(r2.xyz, v8.xyz);
  o1.xyzw = r3.xyzw;
  o2.xyzw = v3.xyxy;
  o3.w = v3.x;
  o4.w = v3.y;
  o6.w = 1;
  o7.xy = v9.zw;
  o7.zw = v10.xy;
  o5.xy = v3.xy;
  o5.z = v9.y;
  r1.z = v11.y;
  o9.xyz = r1.xyz;
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
  float2 v11 : TEXCOORD5,
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
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
