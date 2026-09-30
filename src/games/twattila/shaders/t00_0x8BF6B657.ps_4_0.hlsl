// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 30 02:52:12 2026

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
#include "../shared.h"

float3 ApplyAttilaSCurve(float3 color)
{
  float3 output;
  output.r = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(color.r, 0.5f)).r;
  output.g = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(color.g, 0.5f)).g;
  output.b = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(color.b, 0.5f)).b;
  return output;
}

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  // Get coordinates and sample the scene and bloom buffers.
  float2 texcoord = g_screen_size.zw * (g_vpos_texel_offset + v0.xy);
  float4 bloom_sample = g_hdr_rgb_bloom_texture_sampler.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s, texcoord, 0);
  float4 scene_sample = g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, texcoord, 0);

  float output_alpha = scene_sample.w;

  // Use Attila's complete stock SDR tonemapper when HDR is disabled. This
  // intentionally restores the original bloom strength, adaptive exposure,
  // highlight whitening, SDR clamps, and S-curve without any HDR controls.
  if (!HDR)
  {
    float3 sdr_color = scene_sample.xyz;
    float scene_luminance = dot(sdr_color, float3(0.212599993, 0.715200007, 0.0722000003));

    float2 sdr_black_white_points = g_black_and_white_points_sampler.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5f, 0.5f),0).xy;
    float sdr_black_point = sdr_black_white_points.x;
    float sdr_white_point = sdr_black_white_points.y;
    float sdr_black_white_range = sdr_white_point - sdr_black_point;

    // Stock pre-bloom scene highlight clamp.
    float scene_highlight_limit = (sdr_black_white_range * -0.340000004f) + sdr_white_point;
    if (scene_highlight_limit < scene_luminance)
    {
      sdr_color *= scene_highlight_limit / scene_luminance;
    }

    // Stock bloom is added at full strength.
    sdr_color += bloom_sample.xyz;

    // Stock highlight whitening/desaturation.
    float sdr_luminance = dot(sdr_color, float3(0.212599993, 0.715200007, 0.0722000003));
    float highlight_whitening = sdr_luminance - sdr_black_point;
    highlight_whitening /= sdr_black_white_range;
    highlight_whitening = saturate(1.17647052f * (highlight_whitening - 1.f));
    float max_channel = max(sdr_color.x, sdr_color.y);
    max_channel = max(max_channel, sdr_color.z);
    sdr_color += highlight_whitening * (max_channel.xxx - sdr_color);

    // Stock black/white-point normalization and SDR clamp.
    sdr_luminance = dot(sdr_color, float3(0.212599993, 0.715200007, 0.0722000003));
    float normalized_sdr_luminance = max(sdr_luminance, sdr_black_point);
    normalized_sdr_luminance = min(normalized_sdr_luminance, sdr_white_point);
    normalized_sdr_luminance -= sdr_black_point;
    normalized_sdr_luminance /= sdr_black_white_range;
    float sdr_normalization_scale = sdr_luminance > 0.f? normalized_sdr_luminance / sdr_luminance : normalized_sdr_luminance;
    sdr_color = saturate(sdr_color * sdr_normalization_scale);

    o0.xyz = ApplyAttilaSCurve(sdr_color);
    o0.w = output_alpha;
    o0.xyzw = saturate(o0.xyzw);
    return;
  }

  // Attila adds this texture at full strength. SI.bloom therefore uses an
  // intuitive scale: 0 disables bloom, 1 is stock, and 2 is twice stock.
  float3 bloom_color = bloom_sample.xyz * SI.bloom;

  // Capture scene+bloom before Attila's SDR highlight clamp and forced
  // desaturation. This is the signal that the later PsychoV30 pass tone maps.
  float3 hdr_color = scene_sample.xyz + bloom_color;

  // Preserve Attila's adaptive black/white-point exposure, but do not apply an
  // upper clamp. Highlights and bloom therefore remain available to PsychoV30.
  float2 black_white_points = g_black_and_white_points_sampler.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5f, 0.5f), 0).xy;
  float black_point = black_white_points.x;
  float white_point = black_white_points.y;
  float black_white_range = max(white_point - black_point, 0.000001f);

  float hdr_luminance = dot(hdr_color, float3(0.212599993, 0.715200007, 0.0722000003));
  float normalized_hdr_luminance =max(hdr_luminance - black_point, 0.f) / black_white_range;
  float hdr_normalization_scale = hdr_luminance > 0.f? normalized_hdr_luminance / hdr_luminance : 1.f;
  float3 untonemapped = hdr_color * hdr_normalization_scale;

  // Retain Attila's S-curve as a color grade, not as the final tone mapper.
  // NeuTwo moves the unclipped signal into the LUT's 0-1 domain without the
  // per-channel saturation that created white bloom spots. Undoing the same
  // scalar afterwards restores the HDR range for PsychoV30 in t01-t08.
  float lut_scale =renodx::tonemap::neutwo::ComputeMaxChannelScale(untonemapped);
  float3 lut_input = untonemapped * lut_scale;
  float3 graded_hdr = ApplyAttilaSCurve(lut_input);
  graded_hdr = renodx::math::DivideSafe(graded_hdr, lut_scale.xxx, graded_hdr);

  o0.xyz = graded_hdr;
  o0.w = saturate(output_alpha);  // UNORM clamp lost to the FP16 upgrade
  return;
}
