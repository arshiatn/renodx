// environment_specular_light.hlsl cs50_resolve (3Dmigoto decompile, fixed against the disassembly): the
// reflections of the screen-space march (cs50_raycast's hits in t7, the lit scene's mips in t8) and the environment
// cubes; out: what the march adds over the cubes (the scene's light already has those), water the whole reflection.
// RenoDX: the ray-traced reflections and specular occlusion (twattila/rt_reflection.hlsli, rt_scene.hpp).

cbuffer environment_PS : register(b0)
{
  float g_environment_weights[8] : packoffset(c0);
  float g_environment_indices[8] : packoffset(c8);
  float g_environment_count : packoffset(c15.y);
}

cbuffer camera : register(b1)
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

cbuffer lighting_VS_PS : register(b2)
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
  int g_ssr_enabled : packoffset(c11);
  int g_use_spherical_harmonics : packoffset(c11.y);
  float2 g_cloud_shadow_direction : packoffset(c11.z);
  float g_cloud_shadow_speed : packoffset(c12);
  float g_cloud_shadow_scale : packoffset(c12.y);
  float g_cloud_shadow_lerp : packoffset(c12.z);
  float2 g_noise_uv_shift : packoffset(c13);
  float2 g_skin_curvature_scale_bias : packoffset(c13.z);
  float2 g_skin_translucency_scale_bias : packoffset(c14);
  float3 g_skin_blood_colour : packoffset(c15);
  float4 g_world_bounds : packoffset(c16);
  float4 g_playable_area_bounds : packoffset(c17);
  float g_vegetation_wrap_lighting_bias : packoffset(c18);
  float g_spherical_harmonic_terms : packoffset(c18.y);
  float g_spherical_harmonic_fadeout : packoffset(c18.z);
}

SamplerState s_environment_array_s : register(s0);
SamplerState s_ssao_s : register(s1);
SamplerState s_cloud_shadow_s : register(s2);
SamplerState s_point_sampler_s : register(s3);
SamplerState s_trilinear_sampler_s : register(s4);
TextureCubeArray<float4> t_environment_array : register(t0);
Texture2D<float4> t_ssao : register(t1);
Texture2D<float4> t_world_spherical_harmonics : register(t2);
Texture2D<float4> t_cloud_shadow : register(t3);
Texture2D<float4> gbuffer_channel_1_texture : register(t4);
Texture2D<float4> gbuffer_channel_3_texture : register(t5);
Texture2D<float4> gbuffer_channel_4_texture : register(t6);
Texture2D<float4> t_hit : register(t7);
Texture2D<float4> t_in_radiance : register(t8);
RWTexture2D<float3> t_out_radiance : register(u0);


// 3Dmigoto declarations
#define cmp -

#include "../../twattila/rt_reflection.hlsli"


// The spherical harmonics' 9 terms: texel offsets and signs (the game's immediate constants).
static const float4 icb[9] = {
    {0, 0, 1, 0}, {1, 0, 0, 0}, {2, 0, 1, 0}, {0, 1, 0, 0}, {1, 1, 0, 0},
    {2, 1, 0, 0}, {0, 2, 0, 0}, {1, 2, -1, 0}, {2, 2, -1, 0},
};

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  gbuffer_channel_1_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (uint2)r1.xy;
  r1.zw = r1.zw * r0.zw;
  r1.zw = floor(r1.zw);
  r1.zw = (int2)r1.zw;
  r1.xy = (int2)r1.xy + int2(-1,-1);
  r1.zw = max(int2(0,0), (int2)r1.zw);
  r1.xy = min((int2)r1.zw, (int2)r1.xy);
  const int2 rt_texel = (int2)r1.xy;
  r1.zw = float2(0,0);
  r2.xyzw = gbuffer_channel_3_texture.Load(r1.xyw).xyzw;
  r2.w = 255 * r2.w;
  r2.w = round(r2.w);
  r2.w = (uint)r2.w;
  r3.x = (int)r2.w & 65;
  if (r3.x != 0) {
    r3.xy = g_viewport_dimensions.zw * r0.xy;
    r4.xyz = gbuffer_channel_1_texture.Load(r1.xyw).xyz;
    r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
    r3.zw = r4.xy * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
    sincos(r3.zw, r4.xy, r5.xy);
    r6.x = r5.y;
    r6.z = r4.y;
    r5.xz = r6.xz * r5.xx;
    r3.z = 1 + -r4.z;
    r1.xy = r3.xy * float2(2,-2) + float2(-1,1);
    r1.w = 1;
    r6.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
    r6.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
    r6.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
    r1.x = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
    r1.xyz = r6.xyz / r1.xxx;
    r1.w = 1;
    r6.x = dot(r1.xyzw, view._m00_m10_m20_m30);
    r6.y = dot(r1.xyzw, view._m01_m11_m21_m31);
    r6.z = dot(r1.xyzw, view._m02_m12_m22_m32);
    r3.xyw = -camera_position.xyz + r1.xyz;
    r1.y = dot(r3.xyw, r3.xyw);
    r1.y = rsqrt(r1.y);
    r7.xyz = r3.xyw * r1.yyy;
    r5.y = r4.x;
    r1.w = dot(r7.xyz, r5.xyz);
    r3.y = r1.w + r1.w;
    r4.xyw = r5.xyz * -r3.yyy;
    r8.xyz = r5.xyz * -r3.yyy + r7.xyz;
    r9.x = dot(r8.xyz, view._m00_m10_m20);
    r9.y = dot(r8.xyz, view._m01_m11_m21);
    r9.z = dot(r8.xyz, view._m02_m12_m22);
    r3.y = cmp(0 < r9.z);
    r10.xy = camera_near_far.yx + -r6.zz;
    r10.xy = r10.xy / r9.zz;
    r3.y = r3.y ? r10.x : r10.y;
    r6.xyz = r9.xyz * r3.yyy + r6.xyz;
    r6.w = 1;
    r9.x = dot(r6.xyzw, projection._m00_m10_m20_m30);
    r9.y = dot(r6.xyzw, projection._m01_m11_m21_m31);
    r3.y = dot(r6.xyzw, projection._m03_m13_m23_m33);
    r6.xy = r9.xy / r3.yy;
    r6.xy = r6.xy * float2(0.5,-0.5) + float2(0.5,0.5);
    r0.xy = -r0.xy * g_viewport_dimensions.zw + r6.xy;
    r3.y = dot(r0.xy, r0.xy);
    r3.y = rsqrt(r3.y);
    r0.xy = r3.yy * r0.xy;
    r6.xy = t_hit.SampleLevel(s_trilinear_sampler_s, r0.zw, 0).xy;
    r3.y = 1.52662433e-005 * r6.y;
    r3.y = log2(r3.y);
    r6.yz = float2(0.25,0.100000001) * r3.zz;
    sincos(r6.y, r9.x, r10.x);
    r5.w = r9.x / r10.x;
    r5.w = r5.w * -r3.y;
    r5.w = g_screen_size.x * r5.w;
    r5.w = max(1, r5.w);
    r5.w = log2(r5.w);
    r5.w = min(7, r5.w);
    r6.yw = t_hit.SampleLevel(s_trilinear_sampler_s, r0.zw, r5.w).xy;
    r5.w = saturate(-0.400000006 + r6.y);
    r6.y = 1.66666663 * r5.w;
    r6.w = 1.52662433e-005 * r6.w;
    r6.w = log2(r6.w);
    sincos(r6.z, r9.x, r10.x);
    r6.z = r9.x / r10.x;
    r6.z = r6.z * -r6.w;
    r6.z = g_screen_size.x * r6.z;
    r6.z = max(1, r6.z);
    r6.z = log2(r6.z);
    r6.z = min(5, r6.z);
    r0.xy = r0.xy * -r3.yy + r0.zw;
    r9.xyz = t_in_radiance.SampleLevel(s_trilinear_sampler_s, r0.xy, r6.z).xyz;
    // RenoDX: the rays' light in place of the march's hit, the share of rays that met a surface its confidence.
    const bool rt_reflected = RtReflectionsBound();
    const float4 rt_traced = rt_reflected ? t_rt_reflection.Load(int3(rt_texel, 0)) : 0.f;
    if (rt_reflected) r9.xyz = rt_traced.rgb;
    r0.x = (int)r2.w & 64;
    if (r0.x == 0) {
      r0.x = t_ssao.SampleLevel(s_ssao_s, r0.zw, 0).x;
      r0.y = t_in_radiance.SampleLevel(s_point_sampler_s, r0.zw, 0).w;
      r0.zw = r1.xz / g_cloud_shadow_scale;
      r2.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
      r6.zw = g_cloud_shadow_direction.xy * r2.ww;
      r0.zw = r6.zw * time_in_sec + r0.zw;
      r10.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r0.zw, 0).xyzw;
      r10.xyzw = float4(1,1,1,1) + -r10.xyzw;
      r0.z = 4 * g_cloud_shadow_lerp;
      r0.z = min(1, abs(r0.z));
      r0.z = 1 + -r0.z;
      r11.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
      r11.xyzw = min(float4(1,1,1,1), abs(r11.xyzw));
      r11.xyzw = float4(1,1,1,1) + -r11.xyzw;
      r0.z = r10.x * r11.x + r0.z;
      r0.z = r10.y * r11.y + r0.z;
      r0.z = r10.z * r11.z + r0.z;
      r0.z = r10.w * r11.w + r0.z;
      r0.y = min(r0.y, r0.z);
      if (g_use_spherical_harmonics != 0) {
        t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
        r0.zw = uiDest.xy;
        r10.xyzw = g_playable_area_bounds.xyxy + g_playable_area_bounds.zwzw;
        r11.xyzw = float4(0.5,0.5,0.5,0.5) * r10.xyzw;
        r10.xyzw = -r10.zwzw * float4(0.5,0.5,0.5,0.5) + g_playable_area_bounds.xyzw;
        r10.xyzw = r10.xyzw * float4(1.04999995,1.04999995,1.04999995,1.04999995) + r11.xyzw;
        r6.zw = cmp(r1.xz < r10.xy);
        r11.xy = cmp(r10.zw < r1.xz);
        r6.zw = (int2)r6.zw | (int2)r11.xy;
        r2.w = (int)r6.w | (int)r6.z;
        r3.xy = r3.xw * r1.yy;
        r6.zw = cmp(r3.xy < float2(0,0));
        r10.xy = camera_position.xz + -r10.xy;
        r10.zw = -camera_position.xz + r10.zw;
        r6.zw = r6.zw ? r10.xy : r10.zw;
        r10.xy = float2(9.99999975e-005,9.99999975e-005) + abs(r3.xy);
        r6.zw = r6.zw / r10.xy;
        r1.y = min(r6.z, r6.w);
        r3.xy = r3.xy * r1.yy + camera_position.xz;
        r1.xy = r2.ww ? r3.xy : r1.xz;
        r1.xy = -g_playable_area_bounds.xy + r1.xy;
        r3.xy = g_playable_area_bounds.zw + -g_playable_area_bounds.xy;
        r1.xy = r1.xy / r3.xy;
        r3.xy = float2(-1,-1) + r1.xy;
        r3.xy = max(r3.xy, -r1.xy);
        r3.xy = max(float2(0,0), r3.xy);
        r1.z = max(r3.x, r3.y);
        r1.xy = saturate(r1.xy);
        r1.xy = frac(r1.xy);
        r0.zw = (uint2)r0.zw / uint2(3,3);
        r0.zw = (uint2)r0.zw;
        r3.xy = r0.zw * r1.xy;
        r6.zw = trunc(r3.xy);
        r3.xy = frac(r3.xy);
        r10.xy = float2(1,1) / r0.zw;
        r1.xy = r10.xy + r1.xy;
        r1.xy = frac(r1.xy);
        r0.zw = r1.xy * r0.zw;
        r0.zw = trunc(r0.zw);
        r1.x = round(g_spherical_harmonic_terms);
        r1.x = (int)r1.x;
        r10.zw = float2(0,0);
        r11.zw = float2(0,0);
        r12.zw = float2(0,0);
        r13.zw = float2(0,0);
        r14.xyz = float3(0,0,0);
        r1.y = 0;
        while (true) {
          r2.w = cmp((int)r1.y >= (int)r1.x);
          if (r2.w != 0) break;
          r2.w = (uint)r1.y % 3;
          r2.w = (int)r2.w;
          r3.w = r6.z * 3 + r2.w;
          r7.w = (int)icb[(int)r1.y].y;
          r9.w = r6.w * 3 + r7.w;
          r10.x = (int)r3.w;
          r10.y = (int)r9.w;
          r15.xyz = t_world_spherical_harmonics.Load(r10.xyz).xyz;
          r2.w = r0.z * 3 + r2.w;
          r11.x = (int)r2.w;
          r11.y = (int)r9.w;
          r16.xyz = t_world_spherical_harmonics.Load(r11.xyz).xyz;
          r7.w = r0.w * 3 + r7.w;
          r12.x = (int)r3.w;
          r12.y = (int)r7.w;
          r17.xyz = t_world_spherical_harmonics.Load(r12.xyz).xyz;
          r13.x = (int)r2.w;
          r13.y = (int)r7.w;
          r18.xyz = t_world_spherical_harmonics.Load(r13.xyz).xyz;
          r16.xyz = r16.xyz + -r15.xyz;
          r15.xyz = r3.xxx * r16.xyz + r15.xyz;
          r16.xyz = r18.xyz + -r17.xyz;
          r16.xyz = r3.xxx * r16.xyz + r17.xyz;
          r16.xyz = r16.xyz + -r15.xyz;
          r15.xyz = r3.yyy * r16.xyz + r15.xyz;
          r14.xyz = r15.xyz * icb[(int)r1.y].zzz + r14.xyz;
          r1.y = (int)r1.y + 1;
        }
        r0.z = saturate(g_spherical_harmonic_fadeout * r1.z);
        r0.w = cmp(0 < r0.z);
        r1.xyz = ambient_cube_tb[0].xyz + -r14.xyz;
        r1.xyz = r0.zzz * r1.xyz + r14.xyz;
        r1.xyz = r0.www ? r1.xyz : r14.xyz;
      } else {
        r1.xyz = ambient_cube_tb[0].xyz;
      }
      r3.xyw = sun_colour.xyz * abs(sun_direction.y);
      r3.xyw = r3.xyw * float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) + r1.xyz;
      r0.z = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
      r0.w = dot(r3.xyw, float3(0.212599993,0.715200007,0.0722000003));
      r0.z = r0.z / r0.w;
      r0.w = 1 + -r3.z;
      r0.w = r0.w * r0.w;
      r0.w = log2(r0.w);
      r0.w = 1.79999995 * r0.w;
      r0.w = exp2(r0.w);
      r0.w = 1 + -r0.w;
      r0.w = 8 * r0.w;
      r1.xy = float2(0,0);
      while (true) {
        r1.z = cmp(r1.y >= g_environment_count);
        if (r1.z != 0) break;
        r1.z = (uint)r1.y;
        r8.w = g_environment_indices[(uint)r1.z];
        r2.w = t_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, r0.w).w;
        r1.x = r2.w * g_environment_weights[(uint)r1.z] + r1.x;
        r1.y = 1 + r1.y;
      }
      r0.y = r1.x + -r0.y;
      r0.y = saturate(1 + -r0.y);
      r1.x = 1 + -r0.z;
      r0.y = r0.y * r1.x + r0.z;
      r1.xyz = float3(0,0,0);
      r0.z = 0;
      while (true) {
        r2.w = cmp(r0.z >= g_environment_count);
        if (r2.w != 0) break;
        r2.w = (uint)r0.z;
        r8.w = g_environment_indices[(uint)r2.w];
        r3.xyw = t_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, r0.w).xyz;
        r1.xyz = r3.xyw * g_environment_weights[(uint)r2.w] + r1.xyz;
        r0.z = 1 + r0.z;
      }
      r0.yzw = r1.xyz * r0.yyy;
      r1.x = saturate(dot(r5.xyz, r8.xyz));
      r1.w = saturate(r1.w);
      r1.y = r3.z * r3.z;
      r1.z = 0.5 * r1.y;
      r1.y = -r1.y * 0.5 + 1;
      r1.x = r1.x * r1.y + r1.z;
      r1.x = 1 / r1.x;
      r1.y = r1.w * r1.y + r1.z;
      r1.y = 1 / r1.y;
      r1.x = r1.x * r1.y;
      r1.x = saturate(0.25 * r1.x);
      r1.y = 1.57079637 * r3.z;
      r1.y = sin(-r1.y);
      r1.y = 1 + r1.y;
      r1.z = dot(r4.xyw, r4.xyw);
      r1.z = rsqrt(r1.z);
      r3.xyw = r4.xyw * r1.zzz;
      r1.z = saturate(dot(-r7.xyz, r3.xyw));
      r1.w = r1.z * -5.55472994 + -6.98316002;
      r1.z = r1.w * r1.z;
      r1.z = exp2(r1.z);
      r3.xyw = float3(1,1,1) + -r2.xyz;
      r3.xyw = r3.xyw * r1.zzz;
      r1.yzw = r1.yyy * r3.xyw + r2.xyz;
      r1.xyz = r1.xxx * r1.yzw;
      r3.xyw = g_apply_environment_specular ? r1.xyz : r2.xyz;
      r0.yzw = r3.xyw * r0.yzw;
      r1.w = 1 + -r0.x;
      r1.w = r4.z * r1.w + r0.x;
      r0.yzw = r1.www * r0.yzw;
      r0.yzw = r0.xxx * r0.yzw;
      const float3 rt_environment = r0.yzw;
      if (!rt_reflected) r0.yzw = RtOccludedEnvironment(rt_texel, r0.yzw);
      const float3 rt_occluded = r0.yzw;
      r1.xyz = r9.xyz * r1.xyz;
      r0.x = -1 + r0.x;
      r0.x = r3.z * r0.x + 1;
      r1.w = 16.666666 * r5.w;
      r2.w = saturate(0.125 * r6.x);
      r1.w = r2.w * r1.w;
      r0.xyz = r1.xyz * r0.xxx + -r0.yzw;
      r0.w = rt_reflected ? rt_traced.a : min(1, r1.w);
      r0.xyz = r0.www * r0.xyz + (rt_occluded - rt_environment);
    } else {
      r1.xyz = float3(0,0,0);
      r0.w = 0;
      while (true) {
        r1.w = cmp(r0.w >= g_environment_count);
        if (r1.w != 0) break;
        r1.w = (uint)r0.w;
        r8.w = g_environment_indices[(uint)r1.w];
        r3.xyz = t_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, 0).xyz;
        r1.xyz = r3.xyz * g_environment_weights[(uint)r1.w] + r1.xyz;
        r0.w = 1 + r0.w;
      }
      if (!rt_reflected) r1.xyz = RtOccludedEnvironment(rt_texel, r1.xyz);
      r0.w = rt_reflected ? rt_traced.a : min(1, r6.y);
      r3.xyz = r9.xyz + -r1.xyz;
      r1.xyz = r0.www * r3.xyz + r1.xyz;
      r0.xyz = r1.xyz * r2.xyz;
    }
  } else {
    r0.xyz = float3(0,0,0);
  }
  t_out_radiance[vThreadID.xy] = r0.xyz;
  return;
}