// indicator.hlsl ps40_main_to_overlay (the indicators into the overlay; in no trace): the game's, its colour x Unit/Building Indicator Brightness in HDR
// (SI.someindicators, 1 the game's; Attila's some_indicators).
// Decompiled from the game's E82416BE by 3Dmigoto v1.3.16; WARP tools/tw/warp_tests/par_w2/run_perms.sh.

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

SamplerState indicator_texture_sampler_s : register(s0);
Texture2D<float4> g_indicator_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = camera_position.y + -v2.y;
  r0.x = r0.x * r0.x;
  r0.x = 0.00039999999 * r0.x;
  r0.x = min(1, r0.x);
  r0.x = max(0.300000012, r0.x);
  r1.xyzw = g_indicator_texture.Sample(indicator_texture_sampler_s, v1.xy).xyzw;
  r1.xyzw = v3.xyzw * r1.xyzw;
  r0.y = r1.w * r0.x + -0.00392156886;
  r0.x = r1.w * r0.x;
  o0.xyz = r1.xyz;
  o0.w = r0.x;
  r0.x = cmp(r0.y < 0);
  if (r0.x != 0) discard;
  if (SI.hdr >= 1.f) o0.xyz *= SI.someindicators;  // Unit/Building Indicator Brightness; SDR stays the game's
  return;
}
