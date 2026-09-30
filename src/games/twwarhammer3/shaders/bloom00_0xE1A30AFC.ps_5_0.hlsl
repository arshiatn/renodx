// Bloom bright pass (stock).
// DLSS mod: the scene is already upscaled here, the gbuffer / particle mask are still
// render size (top-left part). The addon passes that fraction (1 = stock).

#include "../shared.h"

cbuffer camera : register(b0)
{
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
  float real_time_in_sec : packoffset(c35.z);
  float update_time_in_sec : packoffset(c35.w);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float g_aspect_ratio : packoffset(c36.w);
  float4 g_screen_size : packoffset(c37);
  float g_vpos_texel_offset : packoffset(c38);
  float4 g_viewport_dimensions : packoffset(c39);
  float2 g_viewport_origin : packoffset(c40);
  float4 g_render_target_dimensions : packoffset(c41);
  float4 g_camera_temp0 : packoffset(c42);
  float4 g_camera_temp1 : packoffset(c43);
  float4 g_camera_temp2 : packoffset(c44);
  float4 g_clip_rect : packoffset(c45);
  float3 g_vr_head_rotation : packoffset(c46);
  int g_num_of_samples : packoffset(c46.w);
  float g_supersampling : packoffset(c47);
  float4 g_mouse_position : packoffset(c48);
  float3 g_frustum_points[8] : packoffset(c49);
  float4 g_frustum_planes[6] : packoffset(c57);
  float g_orthographic : packoffset(c63);
  float g_overlay_lerp : packoffset(c63.y);
  float g_overlay_parchment_lerp : packoffset(c63.z);
  float g_overlay_show_details : packoffset(c63.w);
  float g_amount_shadow_in_far_distance : packoffset(c64);
  float2 g_camera_jitter : packoffset(c64.y);
  float2 g_prev_camera_jitter : packoffset(c65);
  uint g_debug_visualization_flags : packoffset(c65.z);
  bool g_taa_is_enabled : packoffset(c65.w);
}

cbuffer bloom_buffer : register(b1)
{
  float g_bloom_point_above_white_point : packoffset(c0);
  float g_bloom_threshold : packoffset(c0.y);
  float g_bloom_strength : packoffset(c0.z);
  float g_bloom_particle_strength : packoffset(c0.w);
  float g_bloom_emissive_strength : packoffset(c1);
  float g_bloom_water_strength : packoffset(c1.y);
  float g_whiteout_start : packoffset(c1.z);
  float g_whiteout_end : packoffset(c1.w);
}

cbuffer render_target_dims_buffer : register(b2)
{
  float2 g_render_target_recip_size : packoffset(c0);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
Texture2D<float4> gbuffer_channel_1_texture : register(t0);
Texture2D<float4> gbuffer_channel_3_texture : register(t1);
Texture2D<float4> g_hdr_rgb_texture : register(t2);
Texture2D<float4> g_particle_mask_texture : register(t3);


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float2 remap = float2(SI.bloom_remap_x, SI.bloom_remap_y);
  remap = (remap.x > 0.f && remap.y > 0.f) ? remap : float2(1.f, 1.f);

  // Strength: water / emissive from the gbuffer (stock).
  int3 pixel = int3(int2(v0.xy * remap), 0);
  float flags = gbuffer_channel_3_texture.Load(pixel).w;
  float emissive = gbuffer_channel_1_texture.Load(pixel).z;
  bool water = (((uint)round(flags * 255.f)) & 64u) != 0u;
  float strength = water ? g_bloom_water_strength : g_bloom_strength;
  strength = emissive * (g_bloom_emissive_strength - strength) + strength;

  // Particles (stock).
  float2 uv = (g_vpos_texel_offset + v0.xy) * g_render_target_recip_size.xy;
  float particle = g_particle_mask_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, uv * remap, 0).w;
  float3 scene = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, uv, 0).xyz;
  strength = particle * (g_bloom_particle_strength - strength) + strength;

  // Threshold (stock).
  float bright = saturate(dot(scene, float3(0.212599993,0.715200007,0.0722000003)) - max(0, g_bloom_threshold));
  o0.xyz = (scene * bright) * strength;
  o0.w = 1;
  return;
}
