// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 16 15:27:37 2026

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

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState g_black_and_white_points_sampler_s : register(s1);
SamplerState g_hdr_rgb_bloom_texture_sampler_s : register(s2);
SamplerState g_scurve_texture_sampler_s : register(s3);
Texture2D<float4> g_hdr_rgb_texture : register(t0);
Texture2D<float4> g_black_and_white_points : register(t1);
Texture2D<float4> g_hdr_rgb_bloom_texture : register(t2);
Texture2D<float4> g_scurve_texture : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// Literally the same as Attila
float3 ApplyWarhammerSCurve(float3 color)
{
  float3 output;
  output.r = g_scurve_texture.Sample(g_scurve_texture_sampler_s, float2(color.r, 0.5f)).r;
  output.g = g_scurve_texture.Sample(g_scurve_texture_sampler_s, float2(color.g, 0.5f)).g;
  output.b = g_scurve_texture.Sample(g_scurve_texture_sampler_s, float2(color.b, 0.5f)).b;
  return output;
}

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  // HDR replacement path:
  // Preserve Warhammer's adaptive exposure and S-curve as grading, but remove
  // the stock whiteout/desaturation and upper SDR clamp.
  // Made AI to use my ATTILA as ground truth because they are basically almost the same then I fixed the rest
  if (HDR == 1.f)
  {
    float2 bloom_uv = -g_viewport_origin.xy + v0.xy;
    bloom_uv = g_vpos_texel_offset + bloom_uv;
    bloom_uv = g_viewport_dimensions.zw * bloom_uv;

    float2 scene_uv = g_vpos_texel_offset + v0.xy;
    scene_uv = g_screen_size.zw * scene_uv;

    float4 bloom_sample = g_hdr_rgb_bloom_texture.SampleLevel( g_hdr_rgb_bloom_texture_sampler_s, bloom_uv, 0);
    float4 scene_sample = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, scene_uv, 0);


    // BLOOM IS BLACK FOR SOME REASON STEAM VS EPIC???
    // Probably it was just taken from Attila's engine and was never used.
    float3 hdr_color = max(scene_sample.xyz + bloom_sample.xyz, 0.f.xxx);

    // Keep the game's adaptive black/white-point exposure, but do not upper
    // clamp luminance to the white point. This retains highlight headroom.
    float2 black_white_points = g_black_and_white_points.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5f, 0.5f), 0).xy;

    float black_point = black_white_points.x;
    float white_point = black_white_points.y;
    float black_white_range = max(white_point - black_point, 0.000001f);

    float hdr_luminance = dot(hdr_color, float3(0.212599993f, 0.715200007f, 0.0722000003f));
    float normalized_hdr_luminance = max(hdr_luminance - black_point, 0.f) / black_white_range;

    float hdr_normalization_scale = hdr_luminance > 0.f ? normalized_hdr_luminance / hdr_luminance : 1.f;
    float3 untonemapped = hdr_color * hdr_normalization_scale;

    // Warhammer's S-curve is per-channel, like Attila's. Compress the unclipped signal into the LUT's valid 0-1 domain, apply the native
    // curve as artistic grading, then undo the same scalar compression.
    float curve_scale = renodx::tonemap::neutwo::ComputeMaxChannelScale(untonemapped);
    float3 curve_input = untonemapped * curve_scale;
    float3 graded_proxy = ApplyWarhammerSCurve(curve_input);
    float3 graded_hdr = renodx::math::DivideSafe(graded_proxy, curve_scale.xxx, graded_proxy);

    o0.xyz = graded_hdr;
    o0.w = scene_sample.w;
    return;
  }

  // Exact stock SDR path.
  r0.xy = -g_viewport_origin.xy + v0.xy;
  r0.xy = g_vpos_texel_offset + r0.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r0.xyzw = g_hdr_rgb_bloom_texture.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s, r0.xy, 0).xyzw;
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
  r2.xyzw = g_black_and_white_points.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5,0.5), 0).xyzw;
  r1.w = r2.y + -r2.x;
  r2.zw = r1.ww * g_whiteout_start + r2.yy;
  r3.x = -r2.z + r0.w;
  r2.z = r2.w + -r2.z;
  r2.z = saturate(r3.x / r2.z);
  r1.xyz = r2.zzz * r1.xyz;
  r0.xyz = r1.xyz * r0.www + r0.xyz;
  r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.x = max(r0.w, r2.x);
  r1.x = min(r1.x, r2.y);
  r1.x = r1.x + -r2.x;
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
  o0.xyzw = saturate(o0.xyzw); //blending etc. from resource upgrades
  return;
}
