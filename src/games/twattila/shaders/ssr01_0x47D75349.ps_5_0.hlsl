// environment_specular_light.hlsl ps40_standard_lighting_model_full_res (sm_5_0): the reflections of shiny surfaces (stencil 1:
// armour, metal), SSR on the light buffer, the environment cube where it misses. SSR Quality: x the ray's samples (finer steps, fewer
// gaps; with the hierarchical march: x its most steps). From the 3Dmigoto decompile; sun_direction.y fixed
// (make_attila_light.py).

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


// Another game's shared.h first (Thrones of Britannia includes this file with its own).
#ifndef SI
#include "../shared.h"
#endif
#include "../ssr_hiz.hlsli"
#include "../rt_reflection.hlsli"

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float3 o0 : SV_Target0,
  out float2 o1 : SV_Target1,
  out float2 o2 : SV_Target2)  // RenoDX: the hierarchical march's hit (ssr_hiz.hpp binds it; ssr_resolve.cs)
{
  o2 = float2(0.f, 0.f);
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v0.xy / g_buffers_scale;
  r0.xy = (int2)r0.xy;
  r0.zw = float2(0,0);
  r0.x = t_depth_stencil_int.Load(r0.xyz).y;
  r0.x = (int)r0.x & 15;
  r0.x = cmp((int)r0.x != 1);
  r0.x = (int)r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r0.xyzw = r0.xyzw / g_buffers_scale;
  r1.xyzw = gbuffer_channel_1_sampler.SampleLevel(gbuffer_channel_1_sampler_s, r0.zw, r0.w).xyzw;
  r2.x = gbuffer_channel_2_sampler.SampleLevel(gbuffer_channel_2_sampler_s, r0.zw, r0.w).w;
  r2.y = gbuffer_channel_3_sampler.SampleLevel(gbuffer_channel_3_sampler_s, r0.zw, r0.w).w;
  r3.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.zw, r0.w).x;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.z = dot(r1.xyz, r1.xyz);
  r2.z = rsqrt(r2.z);
  r1.xyz = r2.zzz * r1.xyz;
  r2.y = r2.y * 255 + 0.5;
  r2.y = floor(r2.y);
  r2.y = (uint)r2.y;
  r2.zw = g_flags & int2(4,16);
  r2.zw = r2.zw ? float2(1,1) : float2(0,0);
  r4.x = 1 + -r2.x;
  r2.x = r2.z * r4.x + r2.x;
  r2.z = 1 + -r1.w;
  r1.w = r2.w * r2.z + r1.w;
  r3.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r3.w = 1;
  r4.x = dot(r3.xyzw, inv_view_projection._m00_m10_m20_m30);
  r4.y = dot(r3.xyzw, inv_view_projection._m01_m11_m21_m31);
  r4.z = dot(r3.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.x = dot(r3.xyzw, inv_view_projection._m03_m13_m23_m33);
  r4.xyz = r4.xyz / r0.xxx;
  r4.w = 1;
  r0.x = dot(r4.xyzw, view_projection._m02_m12_m22_m32);
  r3.xyw = camera_position.xyz + -r4.xyz;
  r0.y = dot(r3.xyw, r3.xyw);
  r0.y = rsqrt(r0.y);
  r3.xyw = r3.xyw * r0.yyy;
  r0.y = dot(-r3.xyw, r1.xyz);
  r0.y = r0.y + r0.y;
  r5.xyz = r1.xyz * -r0.yyy + -r3.xyw;
  r0.y = 1 + -r1.w;
  r5.w = max(0, r5.y);
  r1.xyz = s_environment.SampleLevel(s_environment_s, r5.xwz, 0).xyz;
  r2.zw = r4.xz / g_terrain_offset_scale.wz;
  r2.zw = g_terrain_offset_scale.xy + r2.zw;
  r6.xyz = s_terrain.Sample(s_terrain_s, r2.zw).xyz;
  r6.xyz = r6.xyz * r6.xyz;
  r6.xyz = sun_colour.xyz * r6.xyz;
  r1.w = saturate(-sun_direction.y);
  r6.xyz = r6.xyz * r1.www;
  r1.w = cmp(0 < g_hdr_on);
  r1.w = r1.w ? 0.00400000019 : 1;
  r2.z = saturate(-r5.y);
  r6.xyz = r6.xyz * r1.www + -r1.xyz;
  r1.xyz = r2.zzz * r6.xyz + r1.xyz;
  // RenoDX: ray-traced reflections (rt_reflection.hlsli): with them the screen-space march is skipped (their light
  // where the rays met something, this environment for the rest); with the specular occlusion alone the environment
  // is dimmed where something blocks it.
  const float2 rt_pixel = v0.xy / g_buffers_scale;
  const bool rt_reflected = RtReflectionsBound();
  if (!rt_reflected) r1.xyz = RtOccludedEnvironment(rt_pixel, r1.xyz);
  r1.w = dot(r5.xyz, -r3.xyw);
  r1.w = max(0, r1.w);
  r1.w = log2(r1.w);
  r1.w = 10 * r1.w;
  r1.w = exp2(r1.w);
  r2.z = 1.53938043 * r0.y;
  r2.z = cos(r2.z);
  r2.z = rsqrt(r2.z);
  r2.z = 1 / r2.z;
  r2.w = saturate(60 * r2.x);
  r1.w = r2.z * r1.w;
  r2.z = r2.w + -r2.x;
  r1.w = r1.w * r2.z + r2.x;
  r2.x = cmp(0 < r1.w);
  if (r2.x != 0 && !rt_reflected) {
    r2.x = g_depth_falloff_range.y + -r3.z;
    r2.z = g_depth_falloff_range.y + -g_depth_falloff_range.x;
    r2.x = saturate(r2.x / r2.z);
    r2.z = cmp(r2.x != 1.000000);
    if (r2.z != 0) {
      r6.x = dot(r4.xyzw, view._m00_m10_m20_m30);
      r6.y = dot(r4.xyzw, view._m01_m11_m21_m31);
      r2.z = dot(r4.xyzw, view._m02_m12_m22_m32);
      r3.xy = g_screen_size.xy * r0.zw;
      r3.xy = float2(0.015625,0.015625) * r3.xy;
      r4.xy = cmp(r3.xy >= -r3.xy);
      r3.xy = frac(abs(r3.xy));
      r3.xy = r4.xy ? r3.xy : -r3.xy;
      r3.xy = float2(64,64) * r3.xy;
      r4.xy = (int2)r3.xy;
      r4.zw = float2(0,0);
      r3.xyw = t_jitter.Load(r4.xyz).xyz;
      r3.xyw = r3.xyw * float3(2,2,2) + float3(-1,-1,-1);
      r3.xyw = r3.xyw * r0.yyy;
      r3.xyw = r3.xyw * float3(0.0500000007,0.0500000007,0.0500000007) + r5.xyz;
      r2.w = dot(r3.xyw, r3.xyw);
      r2.w = rsqrt(r2.w);
      r3.xyw = r3.xyw * r2.www;
      r4.x = dot(r3.xyw, view._m00_m10_m20);
      r4.y = dot(r3.xyw, view._m01_m11_m21);
      r4.z = dot(r3.xyw, view._m02_m12_m22);
      r6.z = -r3.z * g_depth_bias_intensity + r2.z;
      // RenoDX: the hierarchical march (ssr_hiz.hlsli; from the start with the game's depth bias, in its jittered
      // direction r4 (view) / r3.xyw (world)), else the game's.
      [branch]
      if (SsrHierarchicalReady()) {
        o0.xyz = SsrHierarchical(v0.xy, r6.xyz, r4.xyz, r3.xyw, r1.xyz, r2.x, o2);
      } else {
        r6.w = 1;
        r5.x = dot(r6.xyzw, projection._m00_m10_m20_m30);
        r5.y = dot(r6.xyzw, projection._m01_m11_m21_m31);
        r5.z = dot(r6.xyzw, projection._m03_m13_m23_m33);
        r2.z = g_ray_length_scale * r6.z;
        r2.w = r4.z * r2.z + r6.z;
        r2.w = cmp(r2.w < camera_near_far.x);
        r3.x = -camera_near_far.x + r4.z;
        r3.x = r3.x / r4.z;
        r2.z = r2.w ? r3.x : r2.z;
        r2.w = r4.z * r2.z;
        r7.xyz = r4.xyz * r2.zzz + r6.xyz;
        r7.w = 1;
        r8.x = dot(r7.xyzw, projection._m00_m10_m20_m30);
        r8.y = dot(r7.xyzw, projection._m01_m11_m21_m31);
        r8.z = dot(r7.xyzw, projection._m03_m13_m23_m33);
        r2.z = -projection._m22 + r3.z;
        r2.z = projection._m32 / r2.z;
        r2.z = r2.z + -r6.z;
        r3.x = r4.z * r4.z;
        r3.x = g_steep_reflection_adjustment_intensity * r3.x;
        r3.y = g_buffers_scale * g_ray_max_samples * SI.ssr_quality;
        r4.xyz = r8.xyz + -r5.xyz;
        r3.z = g_acceptance_depth_range.y + -g_acceptance_depth_range.x;
        r7.xyzw = g_border_falloff_range.xxyy * g_viewport_dimensions.xyxy;
        r6.xy = g_viewport_dimensions.xy * g_border_falloff_range.yy + -r7.xy;
        r8.xyz = r1.xyz;
        r9.xy = r0.zw;
        r3.w = r6.z;
        r4.w = r2.z;
        r5.w = 0;
        r6.w = 1;
        while (true) {
          r8.w = cmp(r3.y < r6.w);
          if (r8.w != 0) break;
          r8.w = r6.w / r3.y;
          r8.w = r8.w * r8.w + -r5.w;
          r8.w = max(r8.w, r3.x);
          r5.w = r8.w + r5.w;
          r8.w = cmp(1 < r5.w);
          if (r8.w != 0) {
            break;
          }
          r10.xyz = r5.www * r4.xyz + r5.xyz;
          r9.zw = r10.xy / r10.zz;
          r9.zw = r9.zw * float2(0.5,-0.5) + float2(0.5,0.5);
          r10.xy = cmp(r9.zw < float2(0,0));
          r8.w = (int)r10.y | (int)r10.x;
          r10.xy = cmp(float2(1,1) < r9.zw);
          r10.x = (int)r10.y | (int)r10.x;
          r8.w = (int)r8.w | (int)r10.x;
          if (r8.w != 0) {
            break;
          }
          r8.w = r5.w * r2.w + r6.z;
          r10.x = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r9.zw, r9.w).x;
          r10.x = -projection._m22 + r10.x;
          r10.x = projection._m32 / r10.x;
          r10.x = -r10.x + r8.w;
          r10.y = cmp(0 < r10.x);
          if (r10.y != 0) {
            r10.y = r10.x + abs(r4.w);
            r10.y = r10.x / r10.y;
            r10.z = -r8.w + r3.w;
            r10.z = r10.y * r10.z + r8.w;
            r11.xy = r9.xy + -r9.zw;
            r10.yw = r10.yy * r11.xy + r9.zw;
            r11.x = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r10.yw, r10.w).x;
            r11.x = -projection._m22 + r11.x;
            r11.x = projection._m32 / r11.x;
            r11.y = r5.w * r3.z + g_acceptance_depth_range.x;
            r10.z = r11.x + -r10.z;
            r10.z = cmp(abs(r10.z) < r11.y);
            if (r10.z != 0) {
              r11.xy = r10.yw * float2(2,2) + float2(-1,-1);
              r11.xy = g_viewport_dimensions.xy * abs(r11.xy);
              r11.xy = max(r11.xy, r7.xy);
              r11.xy = min(r11.xy, r7.zw);
              r11.xy = -g_viewport_dimensions.xy * g_border_falloff_range.xx + r11.xy;
              r11.xy = r11.xy / r6.xy;
              r10.z = max(r11.x, r11.y);
              r10.z = max(r10.z, r2.x);
              r11.x = r5.w * r5.w;
              r10.z = max(r11.x, r10.z);
              r11.xyz = s_light_accumulation_buffer.SampleLevel(s_light_accumulation_buffer_s, r10.yw, 0).xyz;
              r12.xyz = -r11.xyz + r8.xyz;
              r10.yzw = r10.zzz * r12.xyz + r11.xyz;
              r8.xyz = r10.yzw;
              break;
            }
          }
          r9.xy = r9.zw;
          r3.w = r8.w;
          r4.w = r10.x;
          r6.w = 1 + r6.w;
        }
        o0.xyz = r8.xyz;
      }
    } else {
      o0.xyz = r1.xyz;
    }
  } else {
    o0.xyz = rt_reflected ? RtReflection(rt_pixel, r1.xyz) : r1.xyz;
  }
  r1.x = (int)r2.y & 4;
  r0.z = s_ssao.Sample(s_ssao_s, r0.zw).x;
  if (r1.x != 0) {
    r0.z = 1;
  }
  o1.y = r0.z * r1.w;
  r0.z = g_blur_radius_multiplier.x + g_blur_radius_multiplier.y;
  r0.y = r0.y * r0.z;
  r0.x = saturate(r0.y / r0.x);
  r0.x = 1 + -r0.x;
  o1.x = -r0.x * r0.x + 1;
  return;
}