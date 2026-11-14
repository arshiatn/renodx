// depth_of_field pixel_dof_coc with distortion: in no trace of ATN's settings; the game's, with Depth of Field (SI.dof: x its amount, 0 = off), 1: the game's.
// Decompiled from the game's 91B4F89A by 3Dmigoto v1.3.16 (tools/tw/wh2/par_w2_make_perms.py); WARP
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

cbuffer depth_of_field_coc_PS : register(b1)
{
  float g_focus_distance : packoffset(c0);
  float g_focal_length : packoffset(c0.y);
  float g_coc_blur_kernel_scale : packoffset(c0.z);
}

SamplerState input_sampler_s : register(s0);
SamplerState distortion_sampler_s : register(s1);
Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<float4> input_texture : register(t1);
Texture2D<float4> distortion_texture : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r1.xyz = distortion_texture.SampleLevel(distortion_sampler_s, r0.xy, 0).xyz;
  r0.zw = float2(-0.5,-0.5) + r1.xy;
  r0.zw = r0.zw * r1.zz;
  r0.xy = r0.zw * float2(0.0199999996,0.0199999996) + r0.xy;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r1.xy = (uint2)r0.zw;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r1.xy = r1.xy * r0.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r0.zw);
  r1.zw = float2(0,0);
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r0.xyz = input_texture.SampleLevel(input_sampler_s, r0.xy, 0).xyz;
  o0.xyz = r0.xyz;
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_projection._m02_m12_m22_m32);
  r0.y = dot(r1.xyzw, inv_projection._m03_m13_m23_m33);
  r0.x = r0.x / r0.y;
  r0.y = cmp(0 >= r0.x);
  r0.x = r0.y ? 1 : r0.x;
  r0.y = min(500, g_focus_distance);
  r0.x = r0.x + -r0.y;
  r0.x = r0.x * r0.x;
  r0.y = dot(g_focal_length.xx, g_focal_length.xx);  // dp2 of .yyyy (the decompile wrote dot of the scalar)
  r0.x = r0.x / r0.y;
  r0.x = -1.44269502 * r0.x;
  r0.x = exp2(r0.x);
  r0.x = 1 + -r0.x;
  r0.y = 0.600000024 * g_coc_blur_kernel_scale;
  precise float coc = r0.y * r0.x;
  o0.w = saturate(coc * SI.dof);  // RenoDX: Depth of Field
  return;
}
