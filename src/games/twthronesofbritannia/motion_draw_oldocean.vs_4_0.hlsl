// Attila wind in-draw motion (anti_aliasing.hpp): the game's 0xF80C6EEE with its outputs as the game's (its own draw: object_motion.hpp, its pixel shader patched by
// dxbc.hpp), this frame's and last frame's clip positions in render pixels (MOTION0 / MOTION1).
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

cbuffer ocean_VS : register(b1)
{
  float g_sea_decay : packoffset(c0);
  float g_sea_uv_scale : packoffset(c0.y);
  float g_swell_uv_scale : packoffset(c0.z);
  float4 g_vs_sea_info : packoffset(c1);
}

SamplerState sea_bed_info_texture_sampler_s : register(s0);
Texture2D<float4> sea_bed_info_texture_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float4 v0,
  float4 v1,
  float v2,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(1024,1024) + v0.xz;
  r0.zw = cmp(r0.xy < float2(0,0));
  r1.xy = cmp(float2(2048,2048) < r0.xy);
  r0.z = (int)r0.z | (int)r1.x;
  r0.z = (int)r0.w | (int)r0.z;
  r0.z = (int)r1.y | (int)r0.z;
  if (r0.z == 0) {
    r0.xy = float2(0.00048828125,0.00048828125) * r0.xy;
    r0.xyzw = sea_bed_info_texture_sampler.SampleLevel(sea_bed_info_texture_sampler_s, r0.xy, 0).xyzw;
    r0.x = r0.x * g_vs_sea_info.y + g_vs_sea_info.x;
    r0.yz = float2(0.0399999991,0.0399999991) * v0.xz;
    r1.xy = v0.xz * float2(0.0199999996,0.0199999996) + motion_time;
    r0.yz = motion_time * float2(2,2) + r0.yz;
    r1.xy = sin(r1.xy);
    r0.yz = sin(r0.yz);
    r0.y = r1.x + r0.y;
    r0.y = r0.y + r1.y;
    r0.y = r0.y + r0.z;
    r0.y = 4 + r0.y;
    r0.y = -0.125 * r0.y;
    r0.z = 1;
  } else {
    r0.xyz = float3(0,0,0);
  }
  r0.z = cmp(0 != r0.z);
  r0.x = r0.y + -r0.x;
  r0.x = saturate(0.0500000007 * r0.x);
  r0.w = v0.y + -r0.y;
  r0.x = r0.x * r0.w + r0.y;
  r0.y = r0.z ? r0.x : v0.y;
  r0.xzw = v0.xzw;
  o0.x = dot(r0.xyzw, motion_rows[0]);
  o0.y = dot(r0.xyzw, motion_rows[1]);
  o0.z = dot(r0.xyzw, motion_rows[2]);
  r0.w = dot(r0.xyzw, motion_rows[3]);
  r1.xzw = v1.xzw;
  r1.y = g_vs_sea_info.z;
  r2.x = dot(r1.xyzw, motion_rows[0]);
  r2.y = dot(r1.xyzw, motion_rows[1]);
  o3.z = dot(r1.xyzw, motion_rows[2]);
  o3.w = dot(r1.xyzw, motion_rows[3]);
  o3.xy = float2(0.5,-0.5) * r2.xy;
  r1.xzw = v0.xzw;
  r1.y = g_vs_sea_info.z;
  r2.x = dot(r1.xyzw, motion_rows[0]);
  r2.y = dot(r1.xyzw, motion_rows[1]);
  o4.z = dot(r1.xyzw, motion_rows[2]);
  o4.w = dot(r1.xyzw, motion_rows[3]);
  o4.xy = float2(0.5,-0.5) * r2.xy;
  o6.xyzw = float4(g_sea_uv_scale, g_sea_uv_scale, g_swell_uv_scale, g_swell_uv_scale) * v1.xzxz;  // the bytecode: mul o6, v1.xzxz, cb1[0].yyzz (3Dmigoto: g_sea_uv_scale alone)
  r1.x = -g_sea_decay * r0.w + 1;
  o5.x = max(0, r1.x);
  r1.xyz = -camera_position.xyz + r0.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = sqrt(r1.w);
  o7.xyz = r1.xyz / r1.www;
  o0.w = r0.w;
  o2.xyzw = r0.xyzw;
  o7.w = r1.w;
  o1.xy = v1.xz;
  o1.z = v2.x;
  o5.yz = v1.xz;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float v2 : TEXCOORD0,
  out float4 draw_o0 : SV_POSITION0,
  out float3 draw_o1 : COLOR0,
  out float4 draw_o2 : TEXCOORD1,
  out float4 draw_o3 : TEXCOORD2,
  out float4 draw_o4 : TEXCOORD3,
  out float4 draw_o5 : TEXCOORD4,
  out float4 draw_o6 : TEXCOORD5,
  out float4 draw_o7 : TEXCOORD6,
  out float4 motion_now : MOTION0,
  out float4 motion_before : MOTION1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  // This frame's: the game's outputs, with the game's rows (their jitter in them).
  motion_rows[0] = view_projection._m00_m10_m20_m30;
  motion_rows[1] = view_projection._m01_m11_m21_m31;
  motion_rows[2] = view_projection._m02_m12_m22_m32;
  motion_rows[3] = view_projection._m03_m13_m23_m33;
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, o0, o1, o2, o3, o4, o5, o6, o7);
  draw_o0 = o0.xyzw;
  draw_o1 = o1.xyz;
  draw_o2 = o2.xyzw;
  draw_o3 = o3.xyzw;
  draw_o4 = o4.xyzw;
  draw_o5 = o5.xyzw;
  draw_o6 = o6.xyzw;
  draw_o7 = o7.xyzw;
  const float4 game_position = o0;
  // Last frame's: world positions (identity rows).
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, o0, o1, o2, o3, o4, o5, o6, o7);
  const float4 previous_world = o0;
  // Both clip positions with x and y in render pixels; this frame's without the jitter (x w).
  const float2 motion_scale = float2(0.5f, -0.5f) * motion_times.zw;
  motion_now = float4((game_position.xy - motion_game_jitter.xy * game_position.w) * motion_scale, game_position.zw);
  const float4 previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                      dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
  motion_before = float4(previous_clip.xy * motion_scale, previous_clip.zw);
}
