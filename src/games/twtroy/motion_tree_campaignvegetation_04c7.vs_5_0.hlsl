// Attila wind motion (anti_aliasing.hpp): the game's 0x04C7C807 with its wind at this frame's and last frame's time; outputs SV_Position as the game's, this frame's
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
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xz = v3.xy;
  r0.y = v4.x;
  r1.xyz = camera_position.xyz + -r0.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r0.w = r1.y * r0.w;
  r1.x = max(0, r1.y);
  r0.w = max(0, r0.w);
  r0.w = r0.w * r0.w;
  r0.w = 0.449999988 * r0.w;
  r0.w = r0.w * r1.x;
  r0.w = 0.0225000009 * r0.w;
  sincos(r0.w, r1.x, r2.x);
  r2.y = -r1.x;
  r1.yzw = v4.zzz * v0.xyz;
  r3.y = dot(r1.zw, r2.xy);
  o2.y = dot(-v1.yz, r2.xy);
  sincos(v4.y, r4.x, r5.x);
  r5.y = r4.x * r1.x;
  r5.z = r4.x * r2.x;
  r3.x = dot(r1.yzw, r5.xyz);
  o2.x = dot(-v1.xyz, r5.xyz);
  r6.y = r5.x * r1.x;
  r6.z = r5.x * r2.x;
  r6.x = -r4.x;
  r3.z = dot(r1.yzw, r6.xyz);
  o2.z = dot(-v1.xyz, r6.xyz);
  r0.xyz = r3.xyz + r0.xyz;
  r0.w = motion_time + r0.x;
  r0.w = 5 * r0.w;
  r0.w = cos(r0.w);
  r1.x = r0.z * 0.25 + motion_time;
  r1.y = 0.300000012 * r1.x;
  r1.xy = sin(r1.xy);
  r1.y = g_wind_vegetation_vstrength * r1.y;
  r0.w = r1.y * r0.w;
  r1.y = saturate(-0.699999988 + v2.w);
  r1.y = g_wind_vegetation_multiplier * r1.y;
  r0.w = r1.y * r0.w;
  r1.y = 3 * motion_time;
  r1.y = sin(r1.y);
  r1.x = r1.x * r1.y;
  r1.x = g_wind_vegetation_hstrength * r1.x;
  r1.x = r1.x * 0.5 + 0.5;
  r1.x = g_wind_vegetation_hstrength * r1.x;
  r1.y = g_wind_vegetation_multiplier * v2.w;
  r1.yz = g_wind_direction.xy * r1.yy;
  r1.xy = r1.yz * r1.xx;
  r1.xz = float2(0.5,0.5) * r1.xy;
  r1.y = 0;
  r1.xyz = r0.www * float3(0,2.33333325,0) + r1.xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r0.w = 1;
  o0.x = dot(r0.xyzw, motion_rows[0]);
  o0.y = dot(r0.xyzw, motion_rows[1]);
  o0.z = dot(r0.xyzw, motion_rows[2]);
  o0.w = dot(r0.xyzw, motion_rows[3]);
  o1.xyz = r0.xyz;
  o1.w = v0.w;
  o2.w = v1.w;
  r0.x = (uint)v3.x >> 8;
  r0.x = (int)r0.x + (int)v3.x;
  r0.y = (uint)r0.x >> 16;
  r0.x = (int)r0.y ^ (int)r0.x;
  r0.x = (int)r0.x * 0x07d3adeb;
  r0.x = (int)r0.x ^ (int)v3.y;
  r0.y = (uint)r0.x >> 4;
  r0.x = (int)r0.y ^ (int)r0.x;
  r0.y = (uint)r0.x >> 8;
  r0.x = (int)r0.y + (int)r0.x;
  r0.y = (uint)r0.x >> 16;
  r0.x = (int)r0.y ^ (int)r0.x;
  r0.x = (int)r0.x * 0x07d3adeb;
  r0.yz = (uint2)r0.xx >> int2(10,20);
  r0.x = (int)r0.x & 1023;
  r0.x = (int)r0.x;
  r1.x = r0.x * 0.000977517106 + -0.5;
  r0.xy = (int2)r0.yz & int2(1023,1023);
  r0.xy = (int2)r0.xy;
  r1.yz = r0.xy * float2(0.000977517106,0.000977517106) + float2(-0.5,-0.5);
  r0.x = 0.0299999993 * g_campaign_vegetation_tint_scale;
  o3.xyz = r1.xyz * r0.xxx;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float4 v2 : COLOR0,
  float2 v3 : POSITION1,
  float4 v4 : POSITION2,
  out float4 motion_position : SV_Position0,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, o0, o1, o2, o3);
  const float4 world = o0;
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, o0, o1, o2, o3);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
