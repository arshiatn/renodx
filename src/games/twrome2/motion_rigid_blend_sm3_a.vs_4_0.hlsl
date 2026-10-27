// Attila rigid motion (anti_aliasing.hpp): the game's 0x54FB9355 with its placement and wind last frame
// (motion.hlsli, motion_rigid.cs); outputs SV_Position as the game's, this frame's and last frame's clip
// positions without the jitter.
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

cbuffer texture_blend_PS : register(b1)
{
  float2 texture0_blend_height : packoffset(c0);
  float2 texture1_blend_height : packoffset(c0.z);
  float2 texture2_blend_height : packoffset(c1);
  float2 texture3_blend_height : packoffset(c1.z);
  float2 texture4_blend_height : packoffset(c2);
  float2 texture5_blend_height : packoffset(c2.z);
  float2 texture6_blend_height : packoffset(c3);
  float2 texture7_blend_height : packoffset(c3.z);
  float3 colour0 : packoffset(c4);
  float3 colour1 : packoffset(c5);
  float3 colour2 : packoffset(c6);
  float3 colour3 : packoffset(c7);
  float3 colour4 : packoffset(c8);
  float3 colour5 : packoffset(c9);
  float3 colour6 : packoffset(c10);
  float3 colour7 : packoffset(c11);
  float global_alpha0 : packoffset(c11.w);
  float global_alpha1 : packoffset(c12);
  float global_alpha2 : packoffset(c12.y);
  float global_alpha3 : packoffset(c12.z);
  float global_alpha4 : packoffset(c12.w);
  float global_alpha5 : packoffset(c13);
  float global_alpha6 : packoffset(c13.y);
  float global_alpha7 : packoffset(c13.z);
  float height0_vertical : packoffset(c13.w);
  float height1_vertical : packoffset(c14);
  float height2_vertical : packoffset(c14.y);
  float height3_vertical : packoffset(c14.z);
  float height4_vertical : packoffset(c14.w);
  float height5_vertical : packoffset(c15);
  float height6_vertical : packoffset(c15.y);
  float height7_vertical : packoffset(c15.z);
  float normal_and_gloss : packoffset(c15.w);
  float3 fake_ui_light_direction : packoffset(c16);
  float4 g_texture0_output_offset_scale : packoffset(c17);
  float4 g_texture1_output_offset_scale : packoffset(c18);
  float4 g_texture2_output_offset_scale : packoffset(c19);
  float4 g_texture3_output_offset_scale : packoffset(c20);
  float4 g_texture4_output_offset_scale : packoffset(c21);
  float4 g_texture5_output_offset_scale : packoffset(c22);
  float4 g_texture6_output_offset_scale : packoffset(c23);
  float4 g_texture7_output_offset_scale : packoffset(c24);
}



// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float4 v0,
  float2 v1,
  float4 v2,
  float4 v3,
  float4 v4,
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
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
  inout float4 o10)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v0.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, v6.xyzw);
  r1.y = dot(r0.xyzw, v7.xyzw);
  r1.z = dot(r0.xyzw, v8.xyzw);
  r1.w = 1;
  o0.x = dot(r1.xyzw, motion_rows[0]);
  o0.y = dot(r1.xyzw, motion_rows[1]);
  o0.z = dot(r1.xyzw, motion_rows[2]);
  o0.w = dot(r1.xyzw, motion_rows[3]);
  o1.xyzw = r1.xyzw;
  o4.w = dot(r1.xyzw, view._m02_m12_m22_m32);
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o2.xyz = r1.xyz * r0.xxx;
  o2.w = v1.y;
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o3.xyz = r1.xyz * r0.xxx;
  o3.w = v9.y;
  r0.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o4.xyz = r1.xyz * r0.xxx;
  o5.xy = v1.xy;
  o6.xy = v5.wx;
  o6.zw = v9.zw;
  r0.xy = -g_texture0_output_offset_scale.xy + v1.xy;
  o7.xy = r0.xy / g_texture0_output_offset_scale.zw;
  r0.xy = -g_texture1_output_offset_scale.xy + v1.xy;
  o7.zw = r0.xy / g_texture1_output_offset_scale.zw;
  r0.xy = -g_texture2_output_offset_scale.xy + v1.xy;
  o8.xy = r0.xy / g_texture2_output_offset_scale.zw;
  r0.xy = -g_texture3_output_offset_scale.xy + v1.xy;
  o8.zw = r0.xy / g_texture3_output_offset_scale.zw;
  r0.xy = -g_texture4_output_offset_scale.xy + v1.xy;
  o9.xy = r0.xy / g_texture4_output_offset_scale.zw;
  r0.xy = -g_texture5_output_offset_scale.xy + v1.xy;
  o9.zw = r0.xy / g_texture5_output_offset_scale.zw;
  r0.xy = -g_texture6_output_offset_scale.xy + v1.xy;
  o10.xy = r0.xy / g_texture6_output_offset_scale.zw;
  r0.xy = -g_texture7_output_offset_scale.xy + v1.xy;
  o10.zw = r0.xy / g_texture7_output_offset_scale.zw;
  return;
}

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : POSITION1,
  float4 v7 : POSITION2,
  float4 v8 : POSITION3,
  float4 v9 : TEXCOORD1,
  out float4 motion_position : SV_Position0,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1)
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
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10);
  const float4 world = o0;
  // Last frame's: at its time (wind), at the placement of this mesh where it was predicted to be now
  // (motion_rigid.cs; none: this one).
  float4 rows[3] = {v6, v7, v8};
  float4 previous_rows[3];
  MotionPreviousRigidPlacement(rows, previous_rows);
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, previous_rows[0], previous_rows[1], previous_rows[2], v9, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
