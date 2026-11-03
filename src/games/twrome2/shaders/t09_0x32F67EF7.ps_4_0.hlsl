// ---- Created with 3Dmigoto v1.3.16 (RenoDX pack dump, 2026-10-20)

// hdr_tone_mapper.hlsl ps_constant_tone_mapper_with_bloom (sm_3_0)

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
  float4 g_camera_temp0 : packoffset(c30);
  float4 g_camera_temp1 : packoffset(c31);
  float4 g_camera_temp2 : packoffset(c32);
  float4 g_clip_rect : packoffset(c33);
  float g_hide_foliage : packoffset(c34);
}

cbuffer constant_tone_mapping_buffer : register(b1)
{
  float g_log_Y_black_point : packoffset(c0);
  float g_black_point : packoffset(c0.y);
  float g_log_Y_white_point : packoffset(c0.z);
  float g_white_point : packoffset(c0.w);
}

cbuffer vignette_buffer : register(b2)
{
  float g_recip_half_screen_diag : packoffset(c0);
  float g_vignette_enable : packoffset(c0.y);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState g_hdr_rgb_bloom_texture_sampler_s : register(s1);
SamplerState g_scurve_texture_sampler_s : register(s2);
Texture2D<float4> g_hdr_rgb_texture_sampler : register(t0);
Texture2D<float4> g_hdr_rgb_bloom_texture_sampler : register(t1);
Texture2D<float4> g_scurve_texture_sampler : register(t2);


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

  // HDR: tone_mapper.hlsli, with the constant points.
  if (HDR >= 1.f) {
    const float2 texcoord = g_screen_size.zw * (g_vpos_texel_offset + v0.xy);
    o0 = ToneMapperHdr(g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, texcoord, 0),
                       SampleBloomSmooth(g_hdr_rgb_bloom_texture_sampler, (texcoord) / DrawRenderShare()),
                       float4(g_log_Y_black_point, g_black_point, g_log_Y_white_point, g_white_point), v0.xy);
    return;
  }

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r1.xyzw = g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, r0.xy, 0).xyzw;
  r0.xyzw = SampleBloomSmooth(g_hdr_rgb_bloom_texture_sampler, (r0.xy) / DrawRenderShare()).xyzw;
  r0.xyz *= SI.bloom;
  r2.x = dot(float3(0.0193000007,0.119199999,0.950500011), r1.xyz);
  r2.y = dot(float3(0.412400007,0.357600003,0.180500001), r1.xyz);
  r2.z = dot(float3(0.212599993,0.715200007,0.0722000003), r1.xyz);
  r2.w = r2.y + r2.z;
  r2.x = r2.w + r2.x;
  r2.x = max(0.00100000005, r2.x);
  r2.y = r2.y / r2.x;
  r2.x = r2.z / r2.x;
  r2.w = 1 + -r2.y;
  r2.w = r2.w + -r2.x;
  r2.x = max(0.00100000005, r2.x);
  r3.xy = -g_screen_size.xy * float2(0.5,0.5) + v0.xy;
  r3.x = dot(r3.xy, r3.xy);
  r3.x = sqrt(r3.x);
  r3.x = g_recip_half_screen_diag * r3.x;
  r3.w = g_vignette_enable * r3.x;
  r3.z = r3.w * r3.w;
  r3.xy = r3.zz * r3.zw;
  r3.x = dot(r3.xyzw, float4(1.60193861,-3.24679637,1.24311411,-0.219172657));
  r3.x = 1 + r3.x;
  r2.z = r3.x * r2.z;
  r3.y = min(g_white_point, r2.z);
  r2.yz = r3.yy * r2.yw;
  r3.xz = r2.yz / r2.xx;
  r2.x = dot(float3(3.24049997,-1.53719997,-0.49849999), r3.xyz);
  r2.y = dot(float3(-0.969299972,1.87600005,0.0416000001), r3.xyz);
  r2.z = dot(float3(0.0555999987,-0.203999996,1.05719995), r3.xyz);
  r1.xyz = max(float3(0,0,0), r2.xyz);
  r0.xyzw = r1.xyzw + r0.xyzw;
  r0.xyz = max(float3(0.0109999999,0.0109999999,0.0109999999), r0.xyz);
  o0.w = r0.w;
  r0.w = dot(float3(0.412400007,0.357600003,0.180500001), r0.xyz);
  r1.x = dot(float3(0.0193000007,0.119199999,0.950500011), r0.xyz);
  r0.x = dot(float3(0.212599993,0.715200007,0.0722000003), r0.xyz);
  r0.y = r0.w + r0.x;
  r0.y = r0.y + r1.x;
  r0.z = r0.w / r0.y;
  r0.y = r0.x / r0.y;
  r0.x = log2(r0.x);
  r0.x = r0.x * 0.30103001 + -g_log_Y_black_point;
  r0.w = 1 + -r0.z;
  r0.w = r0.w + -r0.y;
  r0.y = max(0.00100000005, r0.y);
  r1.xy = float2(g_log_Y_white_point, g_white_point) + -float2(g_log_Y_black_point, g_black_point);
  r2.x = r0.x / r1.x;
  r2.y = 0.5;
  r2.xyzw = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, r2.xy).xyzw;
  r0.x = r2.x * r1.x + g_log_Y_black_point;
  r0.x = 3.32192802 * r0.x;
  r0.x = exp2(r0.x);
  r0.x = -g_black_point + r0.x;
  r1.y = r0.x / r1.y;
  r0.x = r1.y * r0.w;
  r1.z = r0.x / r0.y;
  r0.x = r1.y * r0.z;
  r1.x = r0.x / r0.y;
  r0.x = dot(float3(3.24049997,-1.53719997,-0.49849999), r1.xyz);
  r0.y = dot(float3(-0.969299972,1.87600005,0.0416000001), r1.xyz);
  r0.z = dot(float3(0.0555999987,-0.203999996,1.05719995), r1.xyz);
  o0.xyz = max(float3(0,0,0), r0.xyz);
  // The original 8-bit target's clip, on the upgraded 16-bit one.
  o0.xyzw = saturate(o0.xyzw);
  return;
}
