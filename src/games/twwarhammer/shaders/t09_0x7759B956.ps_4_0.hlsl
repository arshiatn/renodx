// ---- Created with 3Dmigoto v1.3.16 (RenoDX pack dump, 2026-10-20)

// hdr_tone_mapper.hlsl ps_constant_tone_mapper_with_bloom (sm_4_0)

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
}

cbuffer bloom_buffer : register(b1)
{
  float g_bloom_point_above_white_point : packoffset(c0);
  float g_bloom_strength : packoffset(c0.y);
  float g_whiteout_start : packoffset(c0.z);
  float g_whiteout_end : packoffset(c0.w);
}

cbuffer constant_tone_mapping_buffer : register(b2)
{
  float g_luma_black_point : packoffset(c0);
  float g_luma_white_point : packoffset(c0.y);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState g_hdr_rgb_bloom_texture_sampler_s : register(s1);
SamplerState g_scurve_texture_sampler_s : register(s2);
Texture2D<float4> g_hdr_rgb_texture : register(t0);
Texture2D<float4> g_hdr_rgb_bloom_texture : register(t1);
Texture2D<float4> g_scurve_texture : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "../tone_mapper.hlsli"


// Stock: one bilinear tap of the bloom; bloom_filter.hlsli: a cubic B-spline over 4 x 4 texels.
#define BLOOM_SAMPLE_ONLY
#include "../../twattila/bloom_filter.hlsli"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // HDR: tone_mapper.hlsli, with the constant points.
  if (HDR >= 1.f) {
    o0 = ToneMapperHdr(g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, g_screen_size.zw * (g_vpos_texel_offset + v0.xy), 0),
                       g_hdr_rgb_bloom_texture.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s,
                           g_viewport_dimensions.zw * (g_vpos_texel_offset + v0.xy - g_viewport_origin.xy), 0),
                       float2(g_luma_black_point, g_luma_white_point));
    return;
  }

  r0.xy = -g_viewport_origin.xy + v0.xy;
  r0.xy = g_vpos_texel_offset + r0.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r0.xyzw = SampleBloomSmooth(g_hdr_rgb_bloom_texture, r0.xy).xyzw;
  r1.xy = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r1.xy;
  r1.xyzw = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, r1.xy, 0).xyzw;
  r0.xyz = r1.xyz + r0.xyz;
  o0.w = r1.w;
  r0.w = max(r0.x, r0.y);
  r0.w = max(r0.w, r0.z);
  r0.w = max(0.00100000005, r0.w);
  r1.xyz = r0.xyz / r0.www;
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.w = g_luma_white_point + -g_luma_black_point;
  r2.xy = r1.ww * float2(g_whiteout_start, g_whiteout_end) + g_luma_white_point;
  r2.z = -r2.x + r0.w;
  r2.x = r2.y + -r2.x;
  r2.x = saturate(r2.z / r2.x);
  r1.xyz = r2.xxx * r1.xyz;
  r0.xyz = r1.xyz * r0.www + r0.xyz;
  r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.x = max(g_luma_black_point, r0.w);
  r1.x = min(g_luma_white_point, r1.x);
  r1.x = -g_luma_black_point + r1.x;
  r1.x = r1.x / r1.w;
  r1.y = cmp(0 < r0.w);
  r0.w = 1 / r0.w;
  r0.w = r1.y ? r0.w : 1;
  r0.w = r1.x * r0.w;
  r0.xyz = saturate(r0.xyz * r0.www);
  r0.w = 0.5;
  r1.xyzw = g_scurve_texture.Sample(g_scurve_texture_sampler_s, r0.xw).xyzw;
  o0.x = r1.x;
  r1.xyzw = g_scurve_texture.Sample(g_scurve_texture_sampler_s, r0.yw).xyzw;
  r0.xyzw = g_scurve_texture.Sample(g_scurve_texture_sampler_s, r0.zw).xyzw;
  o0.z = r0.z;
  o0.y = r1.y;
  // The original 8-bit target's clip, on the upgraded 16-bit one.
  o0.xyzw = saturate(o0.xyzw);
  return;
}
