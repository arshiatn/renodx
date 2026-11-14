// Attila world_uv motion (anti_aliasing.hpp): the game's 0x2C7FBC7D with its world position and UV for ray tracing's stream output
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
  float4 v3,
  float2 v4,
  float4 v5,
  inout float4 o0,
  inout float4 o1,
  inout float4 p1,
  inout float4 o2,
  inout float4 o3,
  inout float4 p3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
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
  r2.yzw = float3(4096,4096,3) * v2.xzw;
  r3.xz = r2.xx * float2(0.5,0.5) + r2.yz;
  r3.y = 2048 * v2.y;
  r2.xyz = -camera_position.xyz + r3.xyz;
  r4.x = v0.x * 2 + -1;
  r4.x = 0.78539753 * r4.x;
  sincos(r4.x, r4.x, r5.x);
  r4.y = dot(r2.xyz, r2.xyz);
  r4.z = rsqrt(r4.y);
  r6.w = sqrt(r4.y);
  r5.yzw = r4.zzz * r2.xzy;
  r4.yw = r5.yz * r5.xx;
  r6.xyz = -r5.ywz;
  r2.y = r6.x * r4.x + r4.w;
  r4.x = r5.z * r4.x + r4.y;
  r2.z = r2.z * r4.z + -r2.y;
  r2.x = r2.x * r4.z + -r4.x;
  r1.w = r1.y * r2.z + r2.y;
  r1.y = 0.699999988 * r0.w;
  r1.x = r1.y * r2.x + r4.x;
  r1.y = dot(r1.xzw, r1.xzw);
  r1.y = rsqrt(r1.y);
  r2.xyz = r1.xzw * r1.yyy + float3(-0,-1,-0);
  r5.yz = r1.wx;
  r1.x = saturate(r6.y);
  o5.xyzw = r6.xyzw;
  r1.y = cmp(0.999000013 >= abs(v3.z));
  r1.y = r1.y ? 0.300000012 : 1.29999995;
  r1.z = -0.5 + abs(v3.z);
  r1.y = r1.z * r1.y;
  r3.w = saturate(5 * r1.y);
  r1.x = r1.x * 0.5 + r3.w;
  r1.x = min(1, r1.x);
  r1.yzw = r1.xxx * r2.xyz + float3(0,1,0);
  r1.x = r1.x + r3.w;
  r1.x = min(1, r1.x);
  r2.xyz = r5.yzw * r1.zwy;
  r2.xyz = r5.wyz * r1.wyz + -r2.xyz;
  r4.x = dot(r2.xyz, r2.xyz);
  r4.x = rsqrt(r4.x);
  r2.xyz = r4.xxx * r2.xyz;
  r2.xyz = float3(1.20000005,1.20000005,1.20000005) * r2.xyz;
  r4.x = -0.5 + v1.x;
  r2.xyz = r4.xxx * r2.xyz;
  r4.x = 0.600000024 / r2.w;
  r4.x = min(1, abs(r4.x));
  r4.x = max(v5.w, r4.x);
  r4.x = r4.x * r2.w;
  r2.w = cmp(r4.x < 0);
  r4.y = saturate(3 * r4.x);
  r5.x = -r4.x;
  r5.y = max(0.300000012, r5.x);
  r4.xy = r2.ww ? r5.xy : r4.xy;
  r2.w = g_fade_params.x * r6.w;
  r4.xy = max(r4.xy, r2.ww);
  r2.xyz = r4.yyy * r2.xyz;
  r2.w = v3.w * 32767 + 9.99999975e-005;
  r2.w = cmp(r2.w >= g_longgrass_tex_index);
  r2.w = r2.w ? 3 : 1;
  r4.y = -g_fade_distances.x * r2.w + r6.w;
  r4.y = max(0, r4.y);
  r4.z = g_fade_distances.x * r2.w;
  r4.w = g_fade_distances.y * r2.w + -r4.z;
  r4.y = r4.y / r4.w;
  r0.w = saturate(-r0.w * 0.200000003 + r4.y);
  r0.w = 1 + -r0.w;
  r2.xyz = -r2.xyz * r0.www + r3.xyz;
  o1.xyzw = r3.xyzw;
  r3.xyz = float3(19.1900005,19.1900005,19.1900005) + r0.yxx;
  r3.x = dot(r0.xyx, r3.xyz);
  r0.xy = r3.xx + r0.xy;
  r0.y = r0.x + r0.y;
  r0.x = r0.y * r0.x;
  r0.x = frac(r0.x);
  r0.y = -0.5 + r0.x;
  r0.x = r0.x + r0.z;
  r0.x = -r0.x * 0.25 + 1;
  r0.x = -r3.w * 0.300000012 + r0.x;
  r3.xy = r3.ww * -v3.xy + v3.xy;
  r0.y = 0.5 * r0.y;
  r0.y = r4.x * r0.w + r0.y;
  r0.z = 1 + -v1.y;
  r0.w = r0.y * r0.z;
  r0.y = saturate(r0.y);
  r0.z = r0.z * r0.z;
  r3.zw = r0.zz * r3.xy;
  r0.z = 1.20000005 * r0.w;
  r1.yzw = r1.yzw * r0.zzz + r2.xyz;
  r5.y = r1.x * 0.100000001 + r1.z;
  r5.xz = r3.zw * r0.yy + r1.yw;
  r5.w = 1;
  o0.x = dot(r5.xyzw, motion_rows[0]);
  o0.y = dot(r5.xyzw, motion_rows[1]);
  o0.z = dot(r5.xyzw, motion_rows[2]);
  o0.w = dot(r5.xyzw, motion_rows[3]);
  o2.xy = v1.xy;
  o2.z = 32767 * v3.w;
  p3.xy = r3.xy;
  r0.y = dot(r3.xy, r3.xy);
  r0.y = sqrt(r0.y);
  o6.xyz = r0.yyy * float3(0.400000006,0.400000006,0.400000006) + r0.xxx;
  r0.x = cmp(v3.z < 0);
  o3.y = r0.x ? 1.000000 : 0;
  o3.x = 1 + -v1.y;
  o4.xy = v4.xy;
  r0.x = g_fade_params.y * r4.z;
  r0.y = -r4.z * g_fade_params.y + r6.w;
  r0.z = saturate(r6.w / g_mid_distance_details.x);
  o7.w = g_mid_distance_details.y * r0.z;
  r0.y = max(0, r0.y);
  r0.x = g_fade_distances.y * r2.w + -r0.x;
  r0.x = saturate(r0.y / r0.x);
  r0.x = 1 + -r0.x;
  o6.w = saturate(-camera_position.y * g_fade_params.z + r0.x);
  r0.xyz = float3(-0.5,-0.5,-0.5) + v5.xyz;
  o7.xyz = r0.xyz * g_mid_terrain_contrast + float3(0.5,0.5,0.5);
  return;
}

void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  float2 v4 : NORMAL0,
  float4 v5 : COLOR0,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 p1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 p3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, o0, o1, p1, o2, o3, p3, o4, o5, o6, o7);
  const float4 world = o0;
  motion_world = float4(world.xyz / world.w, asfloat(MotionHalfBits(o2.x) | (MotionHalfBits(o2.y) << 16)));
  motion_position = 0.f;
}
