// hdr_tone_mapper.hlsl ps_high_pass_luminance_cutoff_constant_tone_map (the bloom source)

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view_projection : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4 camera_near_far : packoffset(c21);
  float time_in_sec : packoffset(c22);
  float2 g_inverse_focal_length : packoffset(c22.y);
  float g_vertical_fov : packoffset(c22.w);
  float4 g_screen_size : packoffset(c23);
  float g_vpos_texel_offset : packoffset(c24);
  float4 g_viewport_dimensions : packoffset(c25);
  float4 g_camera_temp0 : packoffset(c26);
  float4 g_camera_temp1 : packoffset(c27);
  float4 g_camera_temp2 : packoffset(c28);
}

cbuffer constant_tone_mapping_buffer : register(b1)
{
  float g_log_Y_black_point : packoffset(c0);
  float g_black_point : packoffset(c0.y);
  float g_log_Y_white_point : packoffset(c0.z);
  float g_white_point : packoffset(c0.w);
}

cbuffer render_target_dims_buffer : register(b2)
{
  float2 g_render_target_recip_size : packoffset(c0);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
Texture2D<float4> g_hdr_rgb_texture_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../bloom.hlsli"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Stock: one tap of the source per bloom texel; bloom_filter.hlsli: its every pixel there, averaged.
  o0 = BloomHighPassAveraged(g_hdr_rgb_texture_sampler, g_vpos_texel_offset + v0.xy, g_render_target_recip_size.xy,
                             g_white_point);
  return;
}