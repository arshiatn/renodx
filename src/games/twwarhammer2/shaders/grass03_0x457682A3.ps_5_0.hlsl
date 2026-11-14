// rigidgrass ps50_gbuffer_zprefill_grass_ms (MSAA pre-pass): in no trace of ATN's settings; the game's, with Grass Height Distance (SI.grass_height_distance), 1: the game's.
// Decompiled from the game's 457682A3 by 3Dmigoto v1.3.16 (tools/tw/wh2/par_w2_make_perms.py); WARP
// tools/tw/warp_tests/par_w2/run_perms.sh.
#include "../shared.h"

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
}

cbuffer grass_PS : register(b1)
{
  float g_inv_lod_distance : packoffset(c0) = {0.00999999978};
  float2 g_alpha : packoffset(c0.y);
  float2 g_normal_scale_bias : packoffset(c1);
  float g_far_addition : packoffset(c1.z);
  float2 g_far_scale : packoffset(c2);
  float g_clip_bias : packoffset(c2.z);
}

SamplerState s_diffuse_map_s : register(s0);
Texture2D<float4> t_diffuse_map : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  uint vCoverage : SV_Coverage,
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float2 w3 : TEXCOORD6,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  out uint oMask : SV_Coverage)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  float4 x0[9];
  float4 x1[27];
  x0[0].x = 0;
  x0[1].x = 0;
  x0[2].x = 0;
  x0[3].x = 0;
  x0[4].x = 1;
  x0[5].x = 0;
  x0[6].x = 0;
  x0[7].x = 0;
  x0[8].x = 2;
  x1[0].x = 0;
  x1[1].x = 2;
  x1[2].x = 3;
  x1[3].x = 0;
  x1[4].x = 0;
  x1[5].x = 0;
  x1[6].x = 0;
  x1[7].x = 0;
  x1[8].x = 0;
  x1[9].x = 0;
  x1[10].x = 4;
  x1[11].x = 3;
  x1[12].x = 7;
  x1[13].x = 15;
  x1[14].x = 0;
  x1[15].x = 0;
  x1[16].x = 0;
  x1[17].x = 0;
  x1[18].x = 0;
  x1[19].x = 128;
  x1[20].x = 96;
  x1[21].x = 28;
  x1[22].x = 15;
  x1[23].x = 218;
  x1[24].x = 246;
  x1[25].x = 127;
  x1[26].x = 255;
  r0.xyz = ddx_coarse(v2.zxy);
  r1.xyz = ddy_coarse(v2.yzx);
  r2.xyz = r1.xyz * r0.xyz;
  r0.xyz = r0.zxy * r1.yzx + -r2.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.xyz = camera_position.xyz + -v2.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r0.x = dot(r0.xyz, r1.xyz);
  r0.x = saturate(r0.x + r0.x);
  r0.yzw = -camera_position.xyz + v2.xyz;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = sqrt(r0.y);
  r0.y = saturate(GRASS_INV_DISTANCE(g_inv_lod_distance) * r0.y);  // RenoDX: Grass Height Distance
  r0.y = 1 + -r0.y;
  r0.z = saturate(v1.w * g_alpha.x + g_alpha.y);
  r0.y = r0.z * r0.y;
  r0.z = t_diffuse_map.Sample(s_diffuse_map_s, v3.xy).w;
  r0.y = r0.y * r0.z;
  r0.x = r0.y * r0.x;
  r0.y = g_num_of_samples;
  r0.x = r0.y * r0.x + 0.5;
  r0.x = floor(r0.x);
  r0.x = (uint)r0.x;
  r0.y = (uint)r0.x;
  r0.y = -1 + r0.y;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;
  r0.y = g_num_of_samples;
  r0.y = x0[r0.y+0].x;
  r0.x = mad((int)r0.y, 9, (int)r0.x);
  r0.x = x1[r0.x+0].x;
  oMask = (int)r0.x & (int)vCoverage.x;
  return;
}
