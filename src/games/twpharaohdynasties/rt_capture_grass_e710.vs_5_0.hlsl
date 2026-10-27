// Attila world_uv_o3 motion (anti_aliasing.hpp): the game's 0xE710D4E7 with its world position and UV for ray tracing's stream output
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

cbuffer grass_CB : register(b1)
{
  float2 g_fade_distances : packoffset(c0);
  float g_longgrass_tex_index : packoffset(c0.z);
  float g_shadow_normal_scale : packoffset(c0.w);
  float2 g_mid_distance_details : packoffset(c1);
  float4 g_viewport_offset_scale : packoffset(c2);
  float4 g_fade_params : packoffset(c3);
  float3 g_sun_direction : packoffset(c4);
  float g_mid_terrain_contrast : packoffset(c4.w);
  float4 g_grass_tint[10] : packoffset(c5);
  float g_grass_uniform_size[10] : packoffset(c15);
  float3 g_burnt_colour : packoffset(c24.y);
}



// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

// A float as a half's bits (truncated; tiny values 0, huge ones infinite).
uint MotionHalfBits(float value)
{
  const uint bits = asuint(value);
  const uint sign = (bits >> 16) & 0x8000;
  const int exponent = int((bits >> 23) & 0xFF) - 112;
  if (exponent <= 0) return sign;
  if (exponent >= 31) return sign | 0x7C00;
  return sign | (uint(exponent) << 10) | ((bits >> 13) & 0x3FF);
}

void GameMain(
  float3 v0,
  float2 v1,
  float4 v2,
  float2 v3,
  float2 v4,
  float4 v5,
  float4 v6,
  inout float4 o0,
  inout float4 o1,
  inout float4 p1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 p4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7,
  inout float4 o8)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v2.zx * float2(4096,4096) + v0.yx;
  r0.zw = floor(r0.yx);
  r0.zw = float2(0.103100002,0.103100002) * r0.zw;
  r0.zw = frac(r0.zw);
  r1.xyz = float3(19.1900005,19.1900005,19.1900005) + r0.wzz;
  r1.x = dot(r0.zwz, r1.xyz);
  r0.zw = r1.xx + r0.zw;
  r0.w = r0.z + r0.w;
  r0.z = r0.w * r0.z;
  r0.z = frac(r0.z);
  r1.yz = float2(0.699999988,0.100000001) * r0.zz;
  r2.xy = float2(0.103100002,0.103100002) * r0.yx;
  r0.xy = r0.xy + r0.xy;
  r0.xy = floor(r0.xy);
  r0.xy = float2(0.103100002,0.103100002) * r0.xy;
  r2.xy = frac(r2.xy);
  r3.xyz = float3(19.1900005,19.1900005,19.1900005) + r2.yxx;
  r0.w = dot(r2.xyx, r3.xyz);
  r2.xy = r2.xy + r0.ww;
  r0.w = r2.x + r2.y;
  r0.w = r0.w * r2.x;
  r0.xyw = frac(r0.xyw);
  r2.x = -0.5 + r0.w;
  r2.yz = float2(4096,4096) * v2.xz;
  r2.xz = r2.xx * float2(0.5,0.5) + r2.yz;
  r2.y = 2048 * v2.y;
  r3.xyz = -camera_position.xyz + r2.xyz;
  r3.w = dot(r3.xyz, r3.xyz);
  r4.x = rsqrt(r3.w);
  r5.w = sqrt(r3.w);
  r4.yzw = r4.xxx * r3.xzy;
  r3.y = v0.x * 2 + -1;
  r3.y = 0.78539753 * r3.y;
  sincos(r3.y, r6.x, r7.x);
  r3.yw = r7.xx * r4.yz;
  r5.xyz = -r4.ywz;
  r3.w = r5.x * r6.x + r3.w;
  r3.y = r4.z * r6.x + r3.y;
  r3.z = r3.z * r4.x + -r3.w;
  r3.x = r3.x * r4.x + -r3.y;
  r1.w = r1.y * r3.z + r3.w;
  r1.y = 0.699999988 * r0.w;
  r1.x = r1.y * r3.x + r3.y;
  r1.y = dot(r1.xzw, r1.xzw);
  r1.y = rsqrt(r1.y);
  r3.xyz = r1.xzw * r1.yyy + float3(-0,-1,-0);
  r4.yz = r1.wx;
  r1.x = saturate(r5.y);
  o6.xyzw = r5.xyzw;
  r1.y = v6.x + abs(v2.w);
  r1.z = cmp(0.999000013 >= r1.y);
  r1.y = -0.5 + r1.y;
  r1.z = r1.z ? 0.300000012 : 1.29999995;
  r1.y = r1.y * r1.z;
  r2.w = saturate(5 * r1.y);
  r1.x = r1.x * 0.5 + r2.w;
  r1.x = min(1, r1.x);
  r1.yzw = r1.xxx * r3.xyz + float3(0,1,0);
  r1.x = r1.x + r2.w;
  r1.x = min(1, r1.x);
  r3.xyz = r4.yzw * r1.zwy;
  r3.xyz = r4.wyz * r1.wyz + -r3.xyz;
  r3.w = dot(r3.xyz, r3.xyz);
  r3.w = rsqrt(r3.w);
  r3.xyz = r3.xyz * r3.www;
  r3.xyz = float3(1.20000005,1.20000005,1.20000005) * r3.xyz;
  r3.w = -0.5 + v1.x;
  r3.xyz = r3.xyz * r3.www;
  r4.xy = float2(255,3) * v6.wy;
  r3.w = 0.600000024 / r4.y;
  r3.w = min(1, abs(r3.w));
  r3.w = max(v5.w, r3.w);
  r3.w = r4.y * r3.w;
  r4.x = (uint)r4.x;
  r4.x = cmp(0 < g_grass_uniform_size[r4.x]);
  r4.y = max(0.300000012, r3.w);
  r4.z = saturate(3 * r3.w);
  r4.x = r4.x ? r4.y : r4.z;
  r4.y = g_fade_params.x * r5.w;
  r4.x = max(r4.x, r4.y);
  r3.w = max(r4.y, r3.w);
  r3.xyz = r4.xxx * r3.xyz;
  r4.x = v6.w * 255 + 9.99999975e-005;
  r4.x = cmp(r4.x >= g_longgrass_tex_index);
  r4.x = r4.x ? 3 : 1;
  r4.y = -g_fade_distances.x * r4.x + r5.w;
  r4.y = max(0, r4.y);
  r4.z = g_fade_distances.x * r4.x;
  r4.w = g_fade_distances.y * r4.x + -r4.z;
  r4.y = r4.y / r4.w;
  r0.w = saturate(-r0.w * 0.200000003 + r4.y);
  r0.w = 1 + -r0.w;
  r3.xyz = -r3.xyz * r0.www + r2.xyz;
  o1.xyzw = r2.xyzw;
  r2.xyz = float3(19.1900005,19.1900005,19.1900005) + r0.yxx;
  r2.x = dot(r0.xyx, r2.xyz);
  r0.xy = r2.xx + r0.xy;
  r0.y = r0.x + r0.y;
  r0.x = r0.y * r0.x;
  r0.x = frac(r0.x);
  r0.y = -0.5 + r0.x;
  r0.x = r0.x + r0.z;
  r0.x = -r0.x * 0.25 + 1;
  r0.x = -r2.w * 0.300000012 + r0.x;
  r2.xy = r2.ww * -v3.xy + v3.xy;
  r0.y = 0.5 * r0.y;
  r0.y = r3.w * r0.w + r0.y;
  r0.y = v6.z * r0.y;
  r0.z = 1 + -v1.y;
  r0.w = r0.y * r0.z;
  r0.y = saturate(r0.y);
  r0.z = r0.z * r0.z;
  r2.zw = r0.zz * r2.xy;
  r0.z = 1.20000005 * r0.w;
  r1.yzw = r1.yzw * r0.zzz + r3.xyz;
  r3.y = r1.x * 0.100000001 + r1.z;
  r3.xz = r2.zw * r0.yy + r1.yw;
  r3.w = 1;
  o0.x = dot(r3.xyzw, motion_rows[0]);
  o0.y = dot(r3.xyzw, motion_rows[1]);
  o0.z = dot(r3.xyzw, motion_rows[2]);
  o0.w = dot(r3.xyzw, motion_rows[3]);
  o2.xyz = r3.xyz;
  o3.xy = v1.xy;
  o3.z = 255 * v6.w;
  p4.xy = r2.xy;
  r0.y = dot(r2.xy, r2.xy);
  r0.y = sqrt(r0.y);
  r0.x = r0.y * 0.400000006 + r0.x;
  r0.y = cmp(v2.w < 0);
  o4.y = r0.y ? 1.000000 : 0;
  o4.x = 1 + -v1.y;
  o5.xy = v4.xy;
  r0.y = g_fade_params.y * r4.z;
  r0.z = -r4.z * g_fade_params.y + r5.w;
  r0.w = saturate(r5.w / g_mid_distance_details.x);
  o8.w = g_mid_distance_details.y * r0.w;
  r0.z = max(0, r0.z);
  r0.y = g_fade_distances.y * r4.x + -r0.y;
  r0.y = saturate(r0.z / r0.y);
  r0.y = 1 + -r0.y;
  o7.w = saturate(-camera_position.y * g_fade_params.z + r0.y);
  r0.yzw = g_burnt_colour.xyz + -r0.xxx;
  o7.xyz = v6.xxx * r0.yzw + r0.xxx;
  r0.xyz = float3(-0.5,-0.5,-0.5) + v5.xyz;
  o8.xyz = r0.xyz * g_mid_terrain_contrast + float3(0.5,0.5,0.5);
  return;
}

void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : POSITION1,
  float2 v3 : POSITION2,
  float2 v4 : NORMAL0,
  float4 v5 : COLOR0,
  float4 v6 : COLOR1,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 p1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 p4 = 0;
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
  GameMain(v0, v1, v2, v3, v4, v5, v6, o0, o1, p1, o2, o3, o4, p4, o5, o6, o7, o8);
  const float4 world = o0;
  motion_world = float4(world.xyz / world.w, asfloat(MotionHalfBits(o3.x) | (MotionHalfBits(o3.y) << 16)));
  motion_position = 0.f;
}
