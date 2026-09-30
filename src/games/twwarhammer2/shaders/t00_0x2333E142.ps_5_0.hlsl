// ---- Created with 3Dmigoto v1.3.16 on Fri Sep 18 12:16:27 2026

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

cbuffer tone_mapping : register(b1)
{
  float g_tone_mapping_brightness : packoffset(c0);
  float g_tone_mapping_burn : packoffset(c0.y);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState g_hdr_rgb_bloom_texture_sampler_s : register(s1);
Texture2D<float4> g_hdr_rgb_texture : register(t0);
Texture2D<float4> g_hdr_rgb_bloom_texture : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"
#include "../psycho_test30.hlsli"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Rendering modes:
  //   0: native SDR (native Extended Reinhard)
  //   1: native Extended Reinhard handoff; HDR grading/shoulder happen in t01
  //   2: scene-linear handoff for PsychoV30 in t01, mid-grey anchored to SDR
  // Thresholding avoids fragile exact float comparisons and prevents hybrid modes.
  int hdr_mode = (HDR < 0.5f) ? 0 : ((HDR < 1.5f) ? 1 : 2);

  // Keep the game's original Extended Reinhard in this pass for SDR and HDR 1.
  if (hdr_mode != 2)
  {
    r0.x = 1 / g_tone_mapping_burn;
    r0.x = -0.999000013 + r0.x;
    r0.yz = -g_viewport_origin.xy + v0.xy;
    r0.yz = g_vpos_texel_offset + r0.yz;
    r0.yz = g_viewport_dimensions.zw * r0.yz;
    r0.yzw = g_hdr_rgb_bloom_texture.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s, r0.yz, 0).xyz;
    r1.xy = g_vpos_texel_offset + v0.xy;
    r1.xy = g_screen_size.zw * r1.xy;
    r1.xyzw = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, r1.xy, 0).xyzw;
    r0.yzw = r1.xyz + r0.yzw;
    r2.x = dot(r0.yzw, float3(0.212599993,0.715200007,0.0722000003));
    r2.y = g_tone_mapping_brightness * g_tone_mapping_brightness;
    r2.z = r2.y * r2.x;
    r2.y = r2.y * r2.x + 1;
    r0.x = r2.z / r0.x;
    r0.x = 1 + r0.x;
    r0.x = r2.z * r0.x;
    r0.x = r0.x / r2.y;
    r0.xyz = r0.yzw * r0.xxx;
    r1.xyz = r0.xyz / r2.xxx;
    r0.x = cmp(r2.x != 0.000000);
    o0.xyzw = r0.xxxx ? r1.xyzw : 0;

    return;
  }

  // HDR 2: preserve scene-linear scene+bloom for fog, native-grade bridge,
  // and PsychoV30 in t01.

  // Sample the same scene and bloom inputs as the native path.
  float2 bloom_texcoord = -g_viewport_origin.xy + v0.xy;
  bloom_texcoord = g_vpos_texel_offset + bloom_texcoord;
  bloom_texcoord = g_viewport_dimensions.zw * bloom_texcoord;
  float2 scene_texcoord = g_vpos_texel_offset + v0.xy;
  scene_texcoord = g_screen_size.zw * scene_texcoord;
  float4 bloom_sample = g_hdr_rgb_bloom_texture.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s, bloom_texcoord, 0);
  float4 scene_sample = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, scene_texcoord, 0);
  float3 hdr_color = scene_sample.xyz + bloom_sample.xyz;  // stock (was * 1000)

  // Preserve Warhammer II's native tone-mapping brightness as pre-tonemap exposure.
  float game_exposure = g_tone_mapping_brightness * g_tone_mapping_brightness;
  hdr_color *= game_exposure;

  // Mid-grey anchor: the scene value SDR shows at 0.18 arrives at PsychoV as 0.18.
  // Reinhard darkens midtones slightly, so without this HDR 2 runs 0.2-0.28 stops
  // brighter than SDR. Solves x(1 + x/W)/(1 + x) = 0.18 with stock's white point
  // W = 1/burn - 0.999; this form of the root stays stable as burn -> 0.
  float inv_w = 1.f / max(1.f / g_tone_mapping_burn - 0.999f, 0.0001f);
  float x_mid = 0.36f / (0.82f + sqrt(0.6724f + 0.72f * inv_w));
  hdr_color *= 0.18f / x_mid;

  o0.xyz = hdr_color;
  o0.w = scene_sample.w;
  return;
}
