// gridspline.hlsl ps40_main_deployment (the deployment zone's line, 20 x its colour; in no trace): the game's, its colour x Unit/Building Indicator Brightness in HDR
// (SI.someindicators, 1 the game's; Attila's some_indicators).
// Decompiled from the game's 13728DC3 by 3Dmigoto v1.3.16; WARP tools/tw/warp_tests/par_w2/run_indicators.sh.

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

cbuffer grid_spline_PS : register(b1)
{
  float4 g_spline_colour : packoffset(c0);
  float3 g_mouse_intersected_position : packoffset(c1);
}

SamplerState s_diffuse_s : register(s0);
Texture2D<float4> t_diffuse : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = camera_position.xz + -v2.xz;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = sqrt(r0.x);
  r0.x = -r0.x * 0.000500000024 + 1;
  r0.x = max(0, r0.x);
  r0.y = t_diffuse.Sample(s_diffuse_s, v1.xy).w;
  o0.w = r0.y * r0.x;
  o0.xyz = float3(20,20,20) * g_spline_colour.xyz;
  if (SI.hdr >= 1.f) o0.xyz *= SI.someindicators;  // Unit/Building Indicator Brightness; SDR stays the game's
  return;
}
