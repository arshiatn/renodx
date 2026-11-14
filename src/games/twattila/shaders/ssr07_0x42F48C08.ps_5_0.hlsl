/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// environment_specular_light.hlsl ps40_standard_lighting_model_full_res_ms (sm_5_0; 0x97F6C9FC is its sm_4_1 build):
// the reflections of shiny surfaces with the game's MSAA (per pixel, sample 0 of the multisampled G-buffer; Attila's and
// Thrones' bytes). The 3Dmigoto decompile with ssr00's edits that work under the game's MSAA: SSR Quality (x the march's
// samples), sun_direction.y fixed, the AO read / the draw's share. The rays, the hierarchical march and the render scale
// have nothing to do here (no 2D depth, no pyramid, no camera for the upscalers). With SSR Quality 1: the game's
// (test/tw_thrones_ssr). Made by tools/tw/attila_gaps/atg_ssr_make.py.
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
SamplerState s_terrain_s : register(s2);
TextureCube<float4> s_environment : register(t0);
Texture2D<float4> s_terrain : register(t1);
Texture2D<float4> s_ssao : register(t2);
Texture2DMS<float4> gbuffer_channel_1_texture_ms : register(t3);
Texture2DMS<float4> gbuffer_channel_2_texture_ms : register(t4);
Texture2DMS<float4> gbuffer_channel_3_texture_ms : register(t5);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t6);
Texture2DMS<float4> t_light_accumulation_buffer_ms : register(t7);
Texture2DMS<uint4> t_depth_stencil_int_ms : register(t8);
Texture2D<float4> t_jitter : register(t9);


#include "../shared.h"
#include "../render_scale.hlsli"

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float3 o0 : SV_Target0,
  out float2 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v0.xy / g_buffers_scale;
  r0.xy = (int2)r0.xy;
  r0.zw = float2(0,0);
  r1.x = t_depth_stencil_int_ms.Load(r0.xy, 0).y;
  r1.x = (int)r1.x & 15;
  r1.x = cmp((int)r1.x != 1);
  r1.x = (int)r1.x;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.xy = g_vpos_texel_offset + v0.xy;
  r1.zw = g_screen_size.zw * r1.xy;
  r1.xy = g_viewport_dimensions.zw * r1.xy;
  r1.xyzw = r1.xyzw / g_buffers_scale;
  r2.xyzw = gbuffer_channel_1_texture_ms.Load(r0.xy, 0).xyzw;
  r3.x = gbuffer_channel_2_texture_ms.Load(r0.xy, 0).w;
  r3.y = gbuffer_channel_3_texture_ms.Load(r0.xy, 0).w;
  r0.z = gbuffer_channel_4_texture_ms.Load(r0.xy, 0).x;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r3.z = dot(r2.xyz, r2.xyz);
  r3.z = rsqrt(r3.z);
  r2.xyz = r3.zzz * r2.xyz;
  r3.y = r3.y * 255 + 0.5;
  r3.y = floor(r3.y);
  r3.y = (uint)r3.y;
  r3.zw = g_flags & int2(4,16);
  r3.zw = r3.zw ? float2(1,1) : float2(0,0);
  r4.x = 1 + -r3.x;
  r3.x = r3.z * r4.x + r3.x;
  r3.z = 1 + -r2.w;
  r2.w = r3.w * r3.z + r2.w;
  r0.xy = r1.xy * float2(2,-2) + float2(-1,1);
  r0.w = 1;
  r4.x = dot(r0.xyzw, inv_view_projection._m00_m10_m20_m30);
  r4.y = dot(r0.xyzw, inv_view_projection._m01_m11_m21_m31);
  r4.z = dot(r0.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.x = dot(r0.xyzw, inv_view_projection._m03_m13_m23_m33);
  r4.xyz = r4.xyz / r0.xxx;
  r4.w = 1;
  r0.x = dot(r4.xyzw, view_projection._m02_m12_m22_m32);
  r5.xyz = camera_position.xyz + -r4.xyz;
  r0.y = dot(r5.xyz, r5.xyz);
  r0.y = rsqrt(r0.y);
  r5.xyz = r5.xyz * r0.yyy;
  r0.y = dot(-r5.xyz, r2.xyz);
  r0.y = r0.y + r0.y;
  r6.xyz = r2.xyz * -r0.yyy + -r5.xyz;
  r0.y = 1 + -r2.w;
  r6.w = max(0, r6.y);
  r2.xyz = s_environment.SampleLevel(s_environment_s, r6.xwz, 0).xyz;
  r1.xy = r4.xz / g_terrain_offset_scale.wz;
  r1.xy = g_terrain_offset_scale.xy + r1.xy;
  r7.xyz = s_terrain.Sample(s_terrain_s, r1.xy).xyz;
  r7.xyz = r7.xyz * r7.xyz;
  r7.xyz = sun_colour.xyz * r7.xyz;
  // The game's |cb1[0].z| (sun_direction.y); the decompiler wrote g_apply_environment_specular (ssr00's fix).
  r0.w = saturate(-sun_direction.y);
  r7.xyz = r7.xyz * r0.www;
  r0.w = cmp(0 < g_hdr_on);
  r0.w = r0.w ? 0.00400000019 : 1;
  r1.x = saturate(-r6.y);
  r7.xyz = r7.xyz * r0.www + -r2.xyz;
  r2.xyz = r1.xxx * r7.xyz + r2.xyz;
  r0.w = dot(r6.xyz, -r5.xyz);
  r0.w = max(0, r0.w);
  r0.w = log2(r0.w);
  r0.w = 10 * r0.w;
  r0.w = exp2(r0.w);
  r1.x = 1.53938043 * r0.y;
  r1.x = cos(r1.x);
  r1.x = rsqrt(r1.x);
  r1.x = 1 / r1.x;
  r1.y = saturate(60 * r3.x);
  r0.w = r1.x * r0.w;
  r1.x = r1.y + -r3.x;
  r0.w = r0.w * r1.x + r3.x;
  r1.x = cmp(0 < r0.w);
  if (r1.x != 0) {
    r1.x = g_depth_falloff_range.y + -r0.z;
    r1.y = g_depth_falloff_range.y + -g_depth_falloff_range.x;
    r1.x = saturate(r1.x / r1.y);
    r1.y = cmp(r1.x != 1.000000);
    if (r1.y != 0) {
      r5.x = dot(r4.xyzw, view._m00_m10_m20_m30);
      r5.y = dot(r4.xyzw, view._m01_m11_m21_m31);
      r1.y = dot(r4.xyzw, view._m02_m12_m22_m32);
      r3.xz = g_screen_size.xy * r1.zw;
      r4.xy = float2(0.015625,0.015625) * r3.xz;
      r4.zw = cmp(r4.xy >= -r4.xy);
      r4.xy = frac(abs(r4.xy));
      r4.xy = r4.zw ? r4.xy : -r4.xy;
      r4.xy = float2(64,64) * r4.xy;
      r4.xy = (int2)r4.xy;
      r4.zw = float2(0,0);
      r4.xyz = t_jitter.Load(r4.xyz).xyz;
      r4.xyz = r4.xyz * float3(2,2,2) + float3(-1,-1,-1);
      r4.xyz = r4.xyz * r0.yyy;
      r4.xyz = r4.xyz * float3(0.0500000007,0.0500000007,0.0500000007) + r6.xyz;
      r2.w = dot(r4.xyz, r4.xyz);
      r2.w = rsqrt(r2.w);
      r4.xyz = r4.xyz * r2.www;
      r6.x = dot(r4.xyz, view._m00_m10_m20);
      r6.y = dot(r4.xyz, view._m01_m11_m21);
      r6.z = dot(r4.xyz, view._m02_m12_m22);
      r5.z = -r0.z * g_depth_bias_intensity + r1.y;
      r5.w = 1;
      r4.x = dot(r5.xyzw, projection._m00_m10_m20_m30);
      r4.y = dot(r5.xyzw, projection._m01_m11_m21_m31);
      r4.z = dot(r5.xyzw, projection._m03_m13_m23_m33);
      r0.z = g_ray_length_scale * r5.z;
      r1.y = r6.z * r0.z + r5.z;
      r1.y = cmp(r1.y < camera_near_far.x);
      r2.w = -camera_near_far.x + r6.z;
      r2.w = r2.w / r6.z;
      r0.z = r1.y ? r2.w : r0.z;
      r1.y = r6.z * r0.z;
      r7.xyz = r6.xyz * r0.zzz + r5.xyz;
      r7.w = 1;
      r8.x = dot(r7.xyzw, projection._m00_m10_m20_m30);
      r8.y = dot(r7.xyzw, projection._m01_m11_m21_m31);
      r8.z = dot(r7.xyzw, projection._m03_m13_m23_m33);
      r7.xy = (int2)r3.xz;
      r7.zw = float2(0,0);
      r0.z = gbuffer_channel_4_texture_ms.Load(r7.xy, 0).x;
      r0.z = -projection._m22 + r0.z;
      r0.z = projection._m32 / r0.z;
      r0.z = r0.z + -r5.z;
      r2.w = r6.z * r6.z;
      r2.w = g_steep_reflection_adjustment_intensity * r2.w;
      r3.x = g_buffers_scale * g_ray_max_samples * SI.ssr_quality;  // SSR Quality (ssr00's)
      r5.xyw = r8.xyz + -r4.xyz;
      r3.z = g_acceptance_depth_range.y + -g_acceptance_depth_range.x;
      r6.xyzw = g_border_falloff_range.xxyy * g_viewport_dimensions.xyxy;
      r7.xy = g_viewport_dimensions.xy * g_border_falloff_range.yy + -r6.xy;
      r8.zw = float2(0,0);
      r9.zw = float2(0,0);
      r10.xyz = r2.xyz;
      r7.zw = r1.zw;
      r3.w = r5.z;
      r4.w = r0.z;
      r10.w = 0;
      r11.x = 1;
      while (true) {
        r11.y = cmp(r3.x < r11.x);
        if (r11.y != 0) break;
        r11.y = r11.x / r3.x;
        r11.y = r11.y * r11.y + -r10.w;
        r11.y = max(r11.y, r2.w);
        r10.w = r11.y + r10.w;
        r11.y = cmp(1 < r10.w);
        if (r11.y != 0) {
          break;
        }
        r11.yzw = r10.www * r5.xyw + r4.xyz;
        r11.yz = r11.yz / r11.ww;
        r11.yz = r11.yz * float2(0.5,-0.5) + float2(0.5,0.5);
        r12.xy = cmp(r11.yz < float2(0,0));
        r11.w = (int)r12.y | (int)r12.x;
        r12.xy = cmp(float2(1,1) < r11.yz);
        r12.x = (int)r12.y | (int)r12.x;
        r11.w = (int)r11.w | (int)r12.x;
        if (r11.w != 0) {
          break;
        }
        r11.w = r10.w * r1.y + r5.z;
        r12.xy = g_screen_size.xy * r11.yz;
        r8.xy = (int2)r12.xy;
        r8.x = gbuffer_channel_4_texture_ms.Load(r8.xy, 0).x;
        r8.x = -projection._m22 + r8.x;
        r8.x = projection._m32 / r8.x;
        r8.x = r11.w + -r8.x;
        r8.y = cmp(0 < r8.x);
        if (r8.y != 0) {
          r8.y = r8.x + abs(r4.w);
          r8.y = r8.x / r8.y;
          r12.x = -r11.w + r3.w;
          r12.x = r8.y * r12.x + r11.w;
          r12.yz = -r11.yz + r7.zw;
          r12.yz = r8.yy * r12.yz + r11.yz;
          r13.xy = g_screen_size.xy * r12.yz;
          r9.xy = (int2)r13.xy;
          r8.y = gbuffer_channel_4_texture_ms.Load(r9.xy, 0).x;
          r8.y = -projection._m22 + r8.y;
          r8.y = projection._m32 / r8.y;
          r12.w = r10.w * r3.z + g_acceptance_depth_range.x;
          r8.y = r8.y + -r12.x;
          r8.y = cmp(abs(r8.y) < r12.w);
          if (r8.y != 0) {
            r12.xy = r12.yz * float2(2,2) + float2(-1,-1);
            r12.xy = g_viewport_dimensions.xy * abs(r12.xy);
            r12.xy = max(r12.xy, r6.xy);
            r12.xy = min(r12.xy, r6.zw);
            r12.xy = -g_viewport_dimensions.xy * g_border_falloff_range.xx + r12.xy;
            r12.xy = r12.xy / r7.xy;
            r8.y = max(r12.x, r12.y);
            r8.y = max(r8.y, r1.x);
            r12.x = r10.w * r10.w;
            r8.y = max(r12.x, r8.y);
            r12.xyz = t_light_accumulation_buffer_ms.Load(r9.xy, 0).xyz;
            r13.xyz = -r12.xyz + r10.xyz;
            r12.xyz = r8.yyy * r13.xyz + r12.xyz;
            r10.xyz = r12.xyz;
            break;
          }
        }
        r7.zw = r11.yz;
        r3.w = r11.w;
        r4.w = r8.x;
        r11.x = 1 + r11.x;
      }
      o0.xyz = r10.xyz;
    } else {
      o0.xyz = r2.xyz;
    }
  } else {
    o0.xyz = r2.xyz;
  }
  r0.z = (int)r3.y & 4;
  // The AO blur draws its whole target below 100% (view_share.hpp RegionRead::whole; 1 under the game's MSAA).
  r1.x = s_ssao.Sample(s_ssao_s, r1.zw / DrawRenderShare()).x;
  if (r0.z != 0) {
    r1.x = 1;
  }
  o1.y = r1.x * r0.w;
  r0.z = g_blur_radius_multiplier.x + g_blur_radius_multiplier.y;
  r0.y = r0.y * r0.z;
  r0.x = saturate(r0.y / r0.x);
  r0.x = 1 + -r0.x;
  o1.x = -r0.x * r0.x + 1;
  return;
}
