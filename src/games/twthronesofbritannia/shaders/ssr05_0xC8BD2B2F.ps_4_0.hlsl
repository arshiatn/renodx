/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// environment_specular_light.hlsl ps40_standard_lighting_model_full_res (sm_4_0, every shadow method):
// the reflections of shiny surfaces, the screen-space march on the light buffer, the environment cube where it misses
// (the same bytes in Attila). The game's code (3Dmigoto decompile, sun_direction.y fixed as in ssr01) with ssr01's scale
// fix below 100%: the march in the view's uv, its reads x the view's share of the screen textures (else its bounds check
// and edge fade took the screen's uv: rays went on past the view's right and bottom, no fade there), the AO / the
// draw's share. At 100% the game's code.
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

cbuffer lighting_VS_PS : register(b1)
{
  bool g_apply_environment_specular : packoffset(c0);
  float3 sun_direction : packoffset(c0.y);
  float3 sun_colour : packoffset(c1);
  float3 ambient_cube_lr[2] : packoffset(c2);
  float3 ambient_cube_tb[2] : packoffset(c4);
  float3 ambient_cube_fb[2] : packoffset(c6);
  float3 g_deep_water_colour : packoffset(c8);
  float3 g_shallow_water_colour : packoffset(c9);
  float3 g_sea_bed_light_scatter : packoffset(c10);
  float g_refraction_light_scatter : packoffset(c10.w);
  float g_hdr_on : packoffset(c11);
  bool g_ssr_enabled : packoffset(c11.y);
}

cbuffer parameters : register(b2)
{
  int g_flags : packoffset(c0);
  float g_ray_length_scale : packoffset(c0.y);
  float g_ray_max_samples : packoffset(c0.z);
  float2 g_acceptance_depth_range : packoffset(c1);
  float2 g_border_falloff_range : packoffset(c1.z);
  float g_depth_bias_intensity : packoffset(c2);
  float2 g_depth_falloff_range : packoffset(c2.y);
  float2 g_blur_radius_multiplier : packoffset(c3);
  float g_steep_reflection_adjustment_intensity : packoffset(c3.z);
  float4 g_terrain_offset_scale : packoffset(c4);
  float g_buffers_scale : packoffset(c5);
}

SamplerState s_environment_s : register(s0);
SamplerState s_ssao_s : register(s1);
SamplerState gbuffer_channel_1_sampler_s : register(s2);
SamplerState gbuffer_channel_2_sampler_s : register(s3);
SamplerState gbuffer_channel_3_sampler_s : register(s4);
SamplerState gbuffer_channel_4_sampler_s : register(s5);
SamplerState s_light_accumulation_buffer_s : register(s6);
SamplerState s_terrain_s : register(s7);
Texture2D<float4> gbuffer_channel_1_sampler : register(t0);
Texture2D<float4> gbuffer_channel_2_sampler : register(t1);
Texture2D<float4> gbuffer_channel_3_sampler : register(t2);
Texture2D<float4> gbuffer_channel_4_sampler : register(t3);
TextureCube<float4> s_environment : register(t4);
Texture2D<float4> s_terrain : register(t5);
Texture2D<float4> s_light_accumulation_buffer : register(t6);
Texture2D<float4> s_ssao : register(t7);
Texture2D<uint4> t_depth_stencil_int : register(t8);
Texture2D<float4> t_jitter : register(t9);


#include "../../twattila/render_scale.hlsli"

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float3 o0 : SV_Target0,
  out float2 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v0.xy / g_buffers_scale;
  r0.xy = (int2)r0.xy;
  r0.zw = float2(0,0);
  r0.xyzw = t_depth_stencil_int.Load(r0.xyz).xyzw;
  r0.x = (int)r0.y & 15;
  r0.x = cmp((int)r0.x != 1);
  r0.x = (int)r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r0.xyzw = r0.xyzw / g_buffers_scale;
  r1.xyzw = gbuffer_channel_1_sampler.SampleLevel(gbuffer_channel_1_sampler_s, r0.zw, r0.w).xyzw;
  r2.xyzw = gbuffer_channel_2_sampler.SampleLevel(gbuffer_channel_2_sampler_s, r0.zw, r0.w).xyzw;
  r3.xyzw = gbuffer_channel_3_sampler.SampleLevel(gbuffer_channel_3_sampler_s, r0.zw, r0.w).xyzw;
  r4.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.zw, r0.w).yzxw;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.x = dot(r1.xyz, r1.xyz);
  r2.x = rsqrt(r2.x);
  r1.xyz = r2.xxx * r1.xyz;
  r2.x = r3.w * 255 + 0.5;
  r2.x = floor(r2.x);
  r2.x = (uint)r2.x;
  r2.yz = g_flags & int2(4,16);
  r2.yz = r2.yz ? float2(1,1) : float2(0,0);
  r3.x = 1 + -r2.w;
  r2.y = r2.y * r3.x + r2.w;
  r2.w = 1 + -r1.w;
  r1.w = r2.z * r2.w + r1.w;
  r4.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r4.w = 1;
  r3.x = dot(r4.xyzw, inv_view_projection._m00_m10_m20_m30);
  r3.y = dot(r4.xyzw, inv_view_projection._m01_m11_m21_m31);
  r3.z = dot(r4.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.x = dot(r4.xyzw, inv_view_projection._m03_m13_m23_m33);
  r3.xyz = r3.xyz / r0.xxx;
  r3.w = 1;
  r0.x = dot(r3.xyzw, view_projection._m02_m12_m22_m32);
  r4.xyw = camera_position.xyz + -r3.xyz;
  r0.y = dot(r4.xyw, r4.xyw);
  r0.y = rsqrt(r0.y);
  r4.xyw = r4.xyw * r0.yyy;
  r0.y = dot(-r4.xyw, r1.xyz);
  r0.y = r0.y + r0.y;
  r5.xyz = r1.xyz * -r0.yyy + -r4.xyw;
  r0.y = 1 + -r1.w;
  r5.w = max(0, r5.y);
  r1.xyzw = s_environment.SampleLevel(s_environment_s, r5.xwz, 0).xyzw;
  r2.zw = r3.xz / g_terrain_offset_scale.wz;
  r2.zw = g_terrain_offset_scale.xy + r2.zw;
  r6.xyzw = s_terrain.Sample(s_terrain_s, r2.zw).xyzw;
  r6.xyz = r6.xyz * r6.xyz;
  r6.xyz = sun_colour.xyz * r6.xyz;
  r1.w = saturate(-sun_direction.y);
  r6.xyz = r6.xyz * r1.www;
  r1.w = cmp(0 < g_hdr_on);
  r1.w = r1.w ? 0.00400000019 : 1;
  r2.z = saturate(-r5.y);
  r6.xyz = r6.xyz * r1.www + -r1.xyz;
  r1.xyz = r2.zzz * r6.xyz + r1.xyz;
  r1.w = dot(r5.xyz, -r4.xyw);
  r1.w = max(0, r1.w);
  r1.w = log2(r1.w);
  r1.w = 10 * r1.w;
  r1.w = exp2(r1.w);
  r2.z = 1.53938043 * r0.y;
  r2.z = cos(r2.z);
  r2.z = rsqrt(r2.z);
  r2.z = 1 / r2.z;
  r2.w = saturate(60 * r2.y);
  r1.w = r2.z * r1.w;
  r2.z = r2.w + -r2.y;
  r1.w = r1.w * r2.z + r2.y;
  r2.y = cmp(0 < r1.w);
  if (r2.y != 0) {
    r2.y = g_depth_falloff_range.y + -r4.z;
    r2.z = g_depth_falloff_range.y + -g_depth_falloff_range.x;
    r2.y = saturate(r2.y / r2.z);
    r2.z = cmp(r2.y != 1.000000);
    if (r2.z != 0) {
      r6.x = dot(r3.xyzw, view._m00_m10_m20_m30);
      r6.y = dot(r3.xyzw, view._m01_m11_m21_m31);
      r2.z = dot(r3.xyzw, view._m02_m12_m22_m32);
      r3.xy = g_screen_size.xy * r0.zw;
      r3.xy = float2(0.015625,0.015625) * r3.xy;
      r3.zw = cmp(r3.xy >= -r3.xy);
      r3.xy = frac(abs(r3.xy));
      r3.xy = r3.zw ? r3.xy : -r3.xy;
      r3.xy = float2(64,64) * r3.xy;
      r3.xy = (int2)r3.xy;
      r3.zw = float2(0,0);
      r3.xyzw = t_jitter.Load(r3.xyz).xyzw;
      r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
      r3.xyz = r3.xyz * r0.yyy;
      r3.xyz = r3.xyz * float3(0.0500000007,0.0500000007,0.0500000007) + r5.xyz;
      r2.w = dot(r3.xyz, r3.xyz);
      r2.w = rsqrt(r2.w);
      r3.xyz = r3.xyz * r2.www;
      r5.x = dot(r3.xyz, view._m00_m10_m20);
      r5.y = dot(r3.xyz, view._m01_m11_m21);
      r5.z = dot(r3.xyz, view._m02_m12_m22);
      r6.z = -r4.z * g_depth_bias_intensity + r2.z;
      r6.w = 1;
      r3.x = dot(r6.xyzw, projection._m00_m10_m20_m30);
      r3.y = dot(r6.xyzw, projection._m01_m11_m21_m31);
      r3.z = dot(r6.xyzw, projection._m03_m13_m23_m33);
      r2.z = g_ray_length_scale * r6.z;
      r2.w = r5.z * r2.z + r6.z;
      r2.w = cmp(r2.w < camera_near_far.x);
      r3.w = -camera_near_far.x + r5.z;
      r3.w = r3.w / r5.z;
      r2.z = r2.w ? r3.w : r2.z;
      r2.w = r5.z * r2.z;
      r7.xyz = r5.xyz * r2.zzz + r6.xyz;
      r7.w = 1;
      r8.x = dot(r7.xyzw, projection._m00_m10_m20_m30);
      r8.y = dot(r7.xyzw, projection._m01_m11_m21_m31);
      r8.z = dot(r7.xyzw, projection._m03_m13_m23_m33);
      r2.z = -projection._m22 + r4.z;
      r2.z = projection._m32 / r2.z;
      r2.z = r2.z + -r6.z;
      r3.w = r5.z * r5.z;
      r3.w = g_steep_reflection_adjustment_intensity * r3.w;
      r4.x = g_buffers_scale * g_ray_max_samples;
      r4.yzw = r8.xyz + -r3.xyz;
      r5.x = g_acceptance_depth_range.y + -g_acceptance_depth_range.x;
      r7.xyzw = g_border_falloff_range.xxyy * g_viewport_dimensions.xyxy;
      r5.yz = g_viewport_dimensions.xy * g_border_falloff_range.yy + -r7.xy;
      r6.xyw = r1.xyz;
      // RenoDX: the march in the view's uv, its reads x the view's share of the screen textures.
      const float2 view_share = CameraRenderShare(g_screen_size, g_viewport_dimensions);
      r8.xy = r0.zw / view_share;
      r5.w = r6.z;
      r8.z = r2.z;
      r8.w = 0;
      r9.x = 1;
      while (true) {
        r9.y = cmp(r4.x < r9.x);
        if (r9.y != 0) break;
        r9.y = r9.x / r4.x;
        r9.y = r9.y * r9.y + -r8.w;
        r9.y = max(r9.y, r3.w);
        r8.w = r9.y + r8.w;
        r9.y = cmp(1 < r8.w);
        if (r9.y != 0) {
          break;
        }
        r9.yzw = r8.www * r4.yzw + r3.xyz;
        r9.yz = r9.yz / r9.ww;
        r9.yz = r9.yz * float2(0.5,-0.5) + float2(0.5,0.5);
        r10.xy = cmp(r9.yz < float2(0,0));
        r9.w = (int)r10.y | (int)r10.x;
        r10.xy = cmp(float2(1,1) < r9.yz);
        r10.x = (int)r10.y | (int)r10.x;
        r9.w = (int)r9.w | (int)r10.x;
        if (r9.w != 0) {
          break;
        }
        r9.w = r8.w * r2.w + r6.z;
        r10.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r9.yz * view_share, r9.z).xyzw;
        r10.x = -projection._m22 + r10.x;
        r10.x = projection._m32 / r10.x;
        r10.x = -r10.x + r9.w;
        r10.y = cmp(0 < r10.x);
        if (r10.y != 0) {
          r10.y = r10.x + abs(r8.z);
          r10.y = r10.x / r10.y;
          r10.z = -r9.w + r5.w;
          r10.z = r10.y * r10.z + r9.w;
          r11.xy = -r9.yz + r8.xy;
          r10.yw = r10.yy * r11.xy + r9.yz;
          r11.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r10.yw * view_share, r10.w).xyzw;
          r11.x = -projection._m22 + r11.x;
          r11.x = projection._m32 / r11.x;
          r11.y = r8.w * r5.x + g_acceptance_depth_range.x;
          r10.z = r11.x + -r10.z;
          r10.z = cmp(abs(r10.z) < r11.y);
          if (r10.z != 0) {
            r11.xy = r10.yw * float2(2,2) + float2(-1,-1);
            r11.xy = g_viewport_dimensions.xy * abs(r11.xy);
            r11.xy = max(r11.xy, r7.xy);
            r11.xy = min(r11.xy, r7.zw);
            r11.xy = -g_viewport_dimensions.xy * g_border_falloff_range.xx + r11.xy;
            r11.xy = r11.xy / r5.yz;
            r10.z = max(r11.x, r11.y);
            r10.z = max(r10.z, r2.y);
            r11.x = r8.w * r8.w;
            r10.z = max(r11.x, r10.z);
            r11.xyzw = s_light_accumulation_buffer.SampleLevel(s_light_accumulation_buffer_s, r10.yw * view_share, 0).xyzw;
            r12.xyz = -r11.xyz + r6.xyw;
            r10.yzw = r10.zzz * r12.xyz + r11.xyz;
            r6.xyw = r10.yzw;
            break;
          }
        }
        r8.xy = r9.yz;
        r5.w = r9.w;
        r8.z = r10.x;
        r9.x = 1 + r9.x;
      }
      o0.xyz = r6.xyw;
    } else {
      o0.xyz = r1.xyz;
    }
  } else {
    o0.xyz = r1.xyz;
  }
  // RenoDX: the AO blur draws its whole target below 100% (view_share.hpp RegionRead::whole).
  r1.x = (int)r2.x & 4;
  r2.xyzw = s_ssao.Sample(s_ssao_s, r0.zw / DrawRenderShare()).xyzw;
  if (r1.x != 0) {
    r2.x = 1;
  }
  o1.y = r2.x * r1.w;
  r0.z = g_blur_radius_multiplier.x + g_blur_radius_multiplier.y;
  r0.y = r0.y * r0.z;
  r0.x = saturate(r0.y / r0.x);
  r0.x = 1 + -r0.x;
  o1.x = -r0.x * r0.x + 1;
  return;
}