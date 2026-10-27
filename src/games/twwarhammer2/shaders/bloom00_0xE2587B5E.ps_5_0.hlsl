// hdr_tone_mapper.hlsl ps_high_pass_luminance_cutoff (the bloom source; cutoff from the auto exposure)

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

cbuffer bloom_buffer : register(b1)
{
  float g_bloom_point_above_white_point : packoffset(c0);
  float g_bloom_strength : packoffset(c0.y);
  float g_whiteout_start : packoffset(c0.z);
  float g_whiteout_end : packoffset(c0.w);
}

cbuffer render_target_dims_buffer : register(b2)
{
  float2 g_render_target_recip_size : packoffset(c0);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState g_black_and_white_points_sampler_s : register(s1);
Texture2D<float4> g_hdr_rgb_texture : register(t0);
Texture2D<float4> g_black_and_white_points : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../bloom.hlsli"
#include "../../twattila/bloom_high_pass.hlsli"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_black_and_white_points.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5,0.5), 0).xy;
  r0.x = r0.y + -r0.x;
  r0.x = r0.x * g_bloom_point_above_white_point + r0.y;
  // Stock: one tap of the source per bloom texel; bloom_filter.hlsli: its every pixel there, averaged.
  o0 = BloomHighPassAveraged(g_hdr_rgb_texture, g_vpos_texel_offset + v0.xy, g_render_target_recip_size.xy, r0.x);
  return;
}