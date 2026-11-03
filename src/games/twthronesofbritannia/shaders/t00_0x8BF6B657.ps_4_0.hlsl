// ---- Created with 3Dmigoto v1.3.16 on Sat Sep 19 13:07:29 2026

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

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState g_black_and_white_points_sampler_s : register(s1);
SamplerState g_hdr_rgb_bloom_texture_sampler_s : register(s2);
SamplerState g_scurve_texture_sampler_s : register(s3);
Texture2D<float4> g_black_and_white_points_sampler : register(t0);
Texture2D<float4> g_hdr_rgb_texture_sampler : register(t1);
Texture2D<float4> g_hdr_rgb_bloom_texture_sampler : register(t2);
Texture2D<float4> g_scurve_texture_sampler : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "../tone_mapper.hlsli"
// The bloom: a smooth cubic sample of its small texture (bloom_filter.hlsli; stock: one bilinear tap).
#include "../bloom.hlsli"
#include "../../twattila/render_scale.hlsli"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  // ---------------------------------------------------------------------------
  // SDR: keep Britannia's original tonemapper exactly.
  // ---------------------------------------------------------------------------
  if (HDR < 1.f)
  {
    r0.xy = g_vpos_texel_offset + v0.xy;
    r0.xy = g_screen_size.zw * r0.xy;
    r1.xyzw = SampleBloomSmooth(g_hdr_rgb_bloom_texture_sampler, (r0.xy) / DrawRenderShare()).xyzw;
    r0.xyzw = g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, r0.xy, 0).xyzw;
    r1.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
    r2.xyzw = g_black_and_white_points_sampler.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5,0.5), 0).xyzw;
    r2.z = r2.y + -r2.x;
    r2.w = r2.z * -0.340000004 + r2.y;
    r3.x = r2.w / r1.w;
    r1.w = cmp(r2.w < r1.w);
    r3.xyz = r3.xxx * r0.xyz;
    r0.xyz = r1.www ? r3.xyz : r0.xyz;
    o0.w = r0.w;
    r0.xyz = r0.xyz + r1.xyz * SI.bloom;
    r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
    r0.w = r0.w + -r2.x;
    r0.w = r0.w / r2.z;
    r0.w = -1 + r0.w;
    r0.w = saturate(1.17647052 * r0.w);
    r1.x = max(r0.x, r0.y);
    r1.x = max(r1.x, r0.z);
    r1.xyz = r1.xxx + -r0.xyz;
    r0.xyz = r0.www * r1.xyz + r0.xyz;
    r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
    r1.x = max(r0.w, r2.x);
    r1.x = min(r1.x, r2.y);
    r1.x = r1.x + -r2.x;
    r1.x = r1.x / r2.z;
    r1.y = cmp(0 < r0.w);
    r0.w = 1 / r0.w;
    r0.w = r1.y ? r0.w : 1;
    r0.w = r1.x * r0.w;
    r0.xyz = saturate(r0.xyz * r0.www);
    r0.w = 0.5;
    r1.xyzw = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, r0.xw).xyzw;
    o0.x = r1.x;
    r1.xyzw = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, r0.yw).xyzw;
    r0.xyzw = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, r0.zw).xyzw;
    o0.z = r0.z;
    o0.y = r1.y;
    return;
  }

  // HDR: tone_mapper.hlsli.
  const float2 texcoord = g_screen_size.zw * (g_vpos_texel_offset + v0.xy);
  o0 = ToneMapperHdr(g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, texcoord, 0),
                     SampleBloomSmooth(g_hdr_rgb_bloom_texture_sampler, (texcoord) / DrawRenderShare()),
                     g_black_and_white_points_sampler.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5f, 0.5f), 0).xy);
  return;
}
