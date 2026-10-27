// Attila wind motion (anti_aliasing.hpp): the game's 0xEF212374 with its wind at this frame's and last frame's time; outputs SV_Position as the game's, this frame's
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

cbuffer rigid_config_PS : register(b1)
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

cbuffer cb_environment : register(b2)
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
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  uint v13,
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
  inout float4 o10,
  inout float4 o11,
  inout uint4 o12)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v0.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, v6.xyzw);
  r2.y = dot(r0.xyzw, v7.xyzw);
  r1.z = dot(r0.xyzw, v8.xyzw);
  r0.x = saturate(-0.200000003 + v5.w);
  r0.x = 1.25 * r0.x;
  r0.x = r0.x * r0.x;
  r0.x = g_wind_vegetation_multiplier * r0.x;
  r0.x = 0.0199999996 * r0.x;
  r0.y = 0.125 * motion_time;
  r0.y = frac(r0.y);
  r0.z = motion_time * 0.125 + -r0.y;
  r0.z = (uint)r0.z;
  r0.w = (int)r0.z * 0x07d3adeb;
  if (10 == 0) r0.w = 0; else if (10+13 < 32) {   r0.w = (uint)r0.w << (32-(10 + 13)); r0.w = (uint)r0.w >> (32-10);  } else r0.w = (uint)r0.w >> 13;
  r0.z = mad((int)r0.z, 0x07d3adeb, 0x07d3adeb);
  if (10 == 0) r0.z = 0; else if (10+13 < 32) {   r0.z = (uint)r0.z << (32-(10 + 13)); r0.z = (uint)r0.z >> (32-10);  } else r0.z = (uint)r0.z >> 13;
  r0.zw = (uint2)r0.zw;
  r1.y = r0.y * r0.y;
  r0.y = -r0.y * 2 + 3;
  r0.y = r1.y * r0.y;
  r0.z = r0.z + -r0.w;
  r0.y = r0.z * r0.y + r0.w;
  r0.y = r0.y * 0.00782013685 + motion_time;
  r0.zw = r1.xz + r1.xz;
  r0.zw = r0.yy * float2(2,2) + r0.zw;
  r1.yw = frac(r0.zw);
  r0.zw = -r1.yw + r0.zw;
  r0.zw = (uint2)r0.zw;
  r3.xy = (int2)r0.zw * int2(0x7d3adeb,0x7d3adeb);
  if (10 == 0) r3.x = 0; else if (10+13 < 32) {   r3.x = (uint)r3.x << (32-(10 + 13)); r3.x = (uint)r3.x >> (32-10);  } else r3.x = (uint)r3.x >> 13;
  if (10 == 0) r3.y = 0; else if (10+13 < 32) {   r3.y = (uint)r3.y << (32-(10 + 13)); r3.y = (uint)r3.y >> (32-10);  } else r3.y = (uint)r3.y >> 13;
  r3.xy = (uint2)r3.xy;
  r0.zw = mad((int2)r0.zw, int2(0x7d3adeb,0x7d3adeb), int2(0x7d3adeb,0x7d3adeb));
  if (10 == 0) r0.z = 0; else if (10+13 < 32) {   r0.z = (uint)r0.z << (32-(10 + 13)); r0.z = (uint)r0.z >> (32-10);  } else r0.z = (uint)r0.z >> 13;
  if (10 == 0) r0.w = 0; else if (10+13 < 32) {   r0.w = (uint)r0.w << (32-(10 + 13)); r0.w = (uint)r0.w >> (32-10);  } else r0.w = (uint)r0.w >> 13;
  r0.zw = (uint2)r0.zw;
  r3.zw = r1.yw * r1.yw;
  r1.yw = -r1.yw * float2(2,2) + float2(3,3);
  r1.yw = r3.zw * r1.yw;
  r0.zw = r0.zw + -r3.xy;
  r0.zw = r0.zw * r1.yw + r3.xy;
  r0.zw = r0.zw * float2(0.00195503421,0.00195503421) + float2(-1,-1);
  r0.zw = r0.zw * r0.xx + r1.xz;
  r1.xy = float2(0.150000006,0.150000006) * r1.xz;
  r1.xy = r0.yy * float2(0.5,0.5) + r1.xy;
  r1.zw = frac(r1.xy);
  r1.xy = r1.xy + -r1.zw;
  r1.xy = (uint2)r1.xy;
  r3.xy = (int2)r1.xy * int2(0x7d3adeb,0x7d3adeb);
  if (10 == 0) r3.x = 0; else if (10+13 < 32) {   r3.x = (uint)r3.x << (32-(10 + 13)); r3.x = (uint)r3.x >> (32-10);  } else r3.x = (uint)r3.x >> 13;
  if (10 == 0) r3.y = 0; else if (10+13 < 32) {   r3.y = (uint)r3.y << (32-(10 + 13)); r3.y = (uint)r3.y >> (32-10);  } else r3.y = (uint)r3.y >> 13;
  r3.xy = (uint2)r3.xy;
  r1.xy = mad((int2)r1.xy, int2(0x7d3adeb,0x7d3adeb), int2(0x7d3adeb,0x7d3adeb));
  if (10 == 0) r1.x = 0; else if (10+13 < 32) {   r1.x = (uint)r1.x << (32-(10 + 13)); r1.x = (uint)r1.x >> (32-10);  } else r1.x = (uint)r1.x >> 13;
  if (10 == 0) r1.y = 0; else if (10+13 < 32) {   r1.y = (uint)r1.y << (32-(10 + 13)); r1.y = (uint)r1.y >> (32-10);  } else r1.y = (uint)r1.y >> 13;
  r1.xy = (uint2)r1.xy;
  r3.zw = r1.zw * r1.zw;
  r1.zw = -r1.zw * float2(2,2) + float2(3,3);
  r1.zw = r3.zw * r1.zw;
  r1.xy = r1.xy + -r3.xy;
  r1.xy = r1.xy * r1.zw + r3.xy;
  r1.xy = r1.xy * float2(0.00195503421,0.00195503421) + float2(-1,-1);
  r0.xy = r1.xy * r0.xx;
  r2.xz = r0.xy * float2(5,5) + r0.zw;
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o3.xyz = r1.xyz * r0.xxx;
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o4.xyz = r1.xyz * r0.xxx;
  r0.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o5.xyz = r1.xyz * r0.xxx;
  if (g_pixel_selection_active != 0) {
    r0.x = (int)v13.x + g_pixel_selection_start_pixel_index;
    r0.yzw = float3(0,0,0);
    r0.x = t_pixel_selection_object_id_texture.Load(r0.xyz).x;
  }
  o12.x = g_pixel_selection_active ? r0.x : -1;
  r2.w = 1;
  r0.x = dot(r2.xyzw, motion_rows[0]);
  r0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  r0.w = dot(r2.xyzw, motion_rows[3]);
  r0.z = cmp(0 != v9.z);
  if (r0.z != 0) {
    r1.xyz = camera_position.xyz + -r2.xyz;
    r0.z = dot(r1.xyz, r1.xyz);
    r0.z = sqrt(r0.z);
    r0.z = 80 + -r0.z;
    r1.x = saturate(0.0250000004 * r0.z);
  } else {
    r1.x = 0;
  }
  o0.xyw = r0.xyw;
  o1.xyzw = r2.xyzw;
  o2.xyzw = v1.xyzw;
  o3.w = g_wind_direction.x;
  o4.w = g_wind_direction.y;
  r1.yzw = v9.xyw;
  o6.xyzw = r1.yzxw;
  o7.xyzw = v10.xyzw;
  o8.xyzw = v11.xyzw;
  o9.xyz = r0.xyw;
  o9.w = v9.y;
  o10.xyzw = v5.xyzw;
  o11.xyzw = v12.xyzw;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : POSITION1,
  float4 v7 : POSITION2,
  float4 v8 : POSITION3,
  float4 v9 : POSITION4,
  float4 v10 : POSITION5,
  float4 v11 : POSITION6,
  float4 v12 : POSITION7,
  uint v13 : SV_InstanceID0,
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
  float4 o11 = 0;
  uint4 o12 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  const float4 world = o0;
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
