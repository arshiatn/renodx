// environment_specular_light.hlsl cs50_resolve: the reflection colour per render pixel from the hit buffer; its
// texture coordinate x 2 / SSRFactor() (SSR Resolution Full; ssr.hlsli).
// Render scale (DLSS addon): the reflected colour comes from the scene's mip chain, whose coarse levels (up to mip 5:
// 32 px texels, for long rays) also average the unrendered rest of the full-size texture: the distant sea's reflections
// turned bright along the right edge, ~32 render px at any scale (ATN debug22 / 23). Below 100% the lookup stays half a
// texel of its coarsest mip inside the rendered part.
// RenoDX: the ray-traced reflections (twattila/rt_reflection.hlsli, ray_tracing.hpp): where bound, the rays' light in
// place of the march's hit, the share of rays that met a surface its confidence. The pass adds (reflection -
// environment) x confidence onto the scene, whose sun light already holds the environment's specular: with Specular
// Occlusion bound (t46) the environment's closed share comes off too (scene + output: occluded environment +
// confidence x (reflection - occluded environment)); unbound, the same math as the game's.

cbuffer camera_VS_PS : register(b0)
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
  float g_texel_offset : packoffset(c35.z);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);
  float2 g_viewport_origin : packoffset(c39);
  float4 g_render_target_dimensions : packoffset(c40);
  float4 g_camera_temp0 : packoffset(c41);
  float2 g_inv_screen_size_minus_one : packoffset(c42);
  float2 g_camera_jitter : packoffset(c42.z);
  float2 g_prev_camera_jitter : packoffset(c43);
}

cbuffer environment_PS : register(b1)
{
  float3 g_sun_direction : packoffset(c0);
  float4 g_sky_exposure_clamp_threshold_falloff : packoffset(c1);
  float4 g_sky_gamma_etc : packoffset(c2);
  float4 g_probe_grid_area : packoffset(c3);
  uint g_probe_grid_size : packoffset(c4);
  float g_specular_probes_count : packoffset(c4.y);
  float g_specular_probe_lerp : packoffset(c4.z);
  float g_sky_fog_height_stretch : packoffset(c4.w);
  float g_sky_fog_height_offset : packoffset(c5);
  float g_sky_fog_gradient_exponent : packoffset(c5.y);
  float g_sky_fog_opacity : packoffset(c5.z);
  float g_spherical_harmonic_terms : packoffset(c5.w);
  float g_spherical_harmonic_fadeout : packoffset(c6);
}

SamplerState s_environment_array_s : register(s0);
SamplerState s_ibl_brdf_lut_s : register(s1);
SamplerState s_ssao_s : register(s2);
SamplerState s_trilinear_sampler_s : register(s3);
TextureCubeArray<float4> t_environment_array : register(t0);
TextureCubeArray<float4> t_prev_environment_array : register(t1);
Texture2D<float4> t_ibl_brdf_lut : register(t2);
Texture2D<float4> t_ssao : register(t3);
Texture2D<float4> gbuffer_normal_texture : register(t4);
Texture2D<float4> gbuffer_colour_texture : register(t5);
Texture2D<float4> gbuffer_constants_texture : register(t6);
Texture2D<float4> gbuffer_readable_depth_texture : register(t7);
Texture2D<float4> t_hit : register(t8);
Texture2D<float4> t_in_radiance : register(t9);
RWTexture2D<float3> t_out_radiance : register(u0);


// 3Dmigoto declarations
#define cmp -
#include "./ssr.hlsli"
#include "../../twattila/rt_reflection.hlsli"
// Ray-Traced Water Reflections' mask (ray_tracing.hpp WaterSurface; unbound: 0): there the rays left from the sea above
// this surface, so it keeps the environment alone (the sea drawn over it reflects them).
Texture2D<float> t_rt_water : register(t45);


[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r0.zw = screen_size.zw * r0.xy;
  gbuffer_colour_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (uint2)r1.xy;
  r1.zw = r1.zw * r0.zw;
  r1.zw = floor(r1.zw);
  r1.zw = (int2)r1.zw;
  r1.xy = (int2)r1.xy + int2(-1,-1);
  r1.zw = max(int2(0,0), (int2)r1.zw);
  r1.xy = min((int2)r1.zw, (int2)r1.xy);
  const int2 rt_texel = r1.xy;
  r1.zw = float2(0,0);
  r2.xyzw = gbuffer_constants_texture.Load(r1.xyw).xyzw;
  r2.w = 255 * r2.w;
  r2.w = round(r2.w);
  r3.y = (uint)r2.w;
  r2.w = (int)r3.y & 65;
  if (r2.w != 0) {
    r3.zw = g_viewport_dimensions.zw * r0.xy;
    r4.xy = gbuffer_normal_texture.Load(r1.xyw).xy;
    r5.xyz = gbuffer_colour_texture.Load(r1.xyw).xyz;
    r1.z = gbuffer_readable_depth_texture.Load(r1.xyz).x;
    r4.xy = r4.xy * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
    sincos(r4.xy, r4.xy, r6.xy);
    r7.x = r6.y;
    r7.z = r4.y;
    r6.xz = r7.xz * r6.xx;
    r4.yzw = float3(-0.0399999991,-0.0399999991,-0.0399999991) + r5.xyz;
    r4.yzw = r2.xxx * r4.yzw + float3(0.0399999991,0.0399999991,0.0399999991);
    r5.y = saturate(1 + -r2.y);
    r2.y = (int)r3.y & 64;
    r2.y = cmp(0 < (uint)r2.y);
    r4.yzw = r2.yyy ? float3(1,1,1) : r4.yzw;
    r2.w = 1 + -r5.y;
    r1.xy = r3.zw * float2(2,-2) + float2(-1,1);
    r1.w = 1;
    r7.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
    r7.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
    r7.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
    r1.x = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
    r1.xyz = r7.xyz / r1.xxx;
    r1.w = 1;
    r7.x = dot(r1.xyzw, view._m00_m10_m20_m30);
    r7.y = dot(r1.xyzw, view._m01_m11_m21_m31);
    r7.z = dot(r1.xyzw, view._m02_m12_m22_m32);
    r8.xyz = -camera_position.xyz + r1.xyz;
    r1.y = dot(r8.xyz, r8.xyz);
    r1.y = rsqrt(r1.y);
    r8.xyz = r8.xyz * r1.yyy;
    r6.y = r4.x;
    r1.y = dot(r8.xyz, r6.xyz);
    r1.y = r1.y + r1.y;
    r9.xyz = r6.xyz * -r1.yyy + r8.xyz;
    r10.x = dot(r9.xyz, view._m00_m10_m20);
    r10.y = dot(r9.xyz, view._m01_m11_m21);
    r10.z = dot(r9.xyz, view._m02_m12_m22);
    r1.y = cmp(0 < r10.z);
    r3.zw = camera_near_far.yx + -r7.zz;
    r3.zw = r3.zw / r10.zz;
    r1.y = r1.y ? r3.z : r3.w;
    r7.xyz = r10.xyz * r1.yyy + r7.xyz;
    r7.w = 1;
    r10.x = dot(r7.xyzw, projection._m00_m10_m20_m30);
    r10.y = dot(r7.xyzw, projection._m01_m11_m21_m31);
    r1.y = dot(r7.xyzw, projection._m03_m13_m23_m33);
    r1.yw = r10.xy / r1.yy;
    r1.yw = r1.yw * float2(0.5,-0.5) + float2(0.5,0.5);
    r0.xy = -r0.xy * g_viewport_dimensions.zw + r1.yw;
    r1.y = dot(r0.xy, r0.xy);
    r1.y = rsqrt(r1.y);
    r0.xy = r1.yy * r0.xy;
    r1.yw = t_hit.SampleLevel(s_trilinear_sampler_s, r0.zw * (2.f / SSRFactor()), 0).xy;
    r1.yw = float2(0.125,1.52662433e-005) * r1.yw;
    r1.w = log2(r1.w);
    r7.xyz = float3(0.25,0.100000001,8) * r2.www;
    sincos(r7.x, r4.x, r7.x);
    r3.z = r4.x / r7.x;
    r3.z = r3.z * -r1.w;
    r3.z = screen_size.x * r3.z;
    r3.z = max(1, r3.z);
    r3.z = log2(r3.z);
    r3.z = min(7, r3.z);
    r3.zw = t_hit.SampleLevel(s_trilinear_sampler_s, r0.zw * (2.f / SSRFactor()), r3.z).xy;
    r3.z = saturate(-0.400000006 + r3.z);
    r3.w = 1.52662433e-005 * r3.w;
    r3.w = log2(r3.w);
    sincos(r7.y, r4.x, r7.x);
    r4.x = r4.x / r7.x;
    r3.w = r4.x * -r3.w;
    r3.w = screen_size.x * r3.w;
    r3.w = max(1, r3.w);
    r3.w = log2(r3.w);
    r3.w = min(5, r3.w);
    r0.xy = r0.xy * -r1.ww + r0.zw;
    if (any(g_viewport_dimensions.xy < screen_size.xy)) {
      r0.xy = min(r0.xy, (g_viewport_dimensions.xy - 0.5f * exp2(ceil(r3.w))) * screen_size.zw);
    }
    r7.xyw = t_in_radiance.SampleLevel(s_trilinear_sampler_s, r0.xy, r3.w).xyz;
    const bool rt_reflected = RtReflectionsBound();
    const float4 rt_traced = (rt_reflected && t_rt_water.Load(int3(rt_texel, 0)) < 0.5f) ? t_rt_reflection.Load(int3(rt_texel, 0)) : 0.f;
    if (rt_reflected) r7.xyw = rt_traced.rgb;
    r3.x = t_ssao.SampleLevel(s_ssao_s, r0.zw, 0).x;
    r0.y = (int)r3.y | 1;
    r0.x = 1;
    r0.xy = r2.yy ? r0.xy : r3.xy;
    r0.z = dot(r6.xyz, -r8.xyz);
    r5.x = max(0, r0.z);
    r0.z = min(1, r5.x);
    r0.w = r0.z * -5.55472994 + -6.98316002;
    r0.z = r0.w * r0.z;
    r0.z = exp2(r0.z);
    r0.w = 1 + -r2.w;
    r3.xyw = max(r0.www, r4.yzw);
    r3.xyw = r3.xyw + -r4.yzw;
    r3.xyw = r3.xyw * r0.zzz + r4.yzw;
    r0.zw = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r5.xy, 0).xy;
    r4.xy = (int2)r0.yy & int2(128,64);
    r0.y = r4.x ? 1 : 0;
    r1.w = 1 + -r5.x;
    r0.y = r1.w * r0.y;
    r0.y = r0.y * -r0.z + r0.z;
    r0.yzw = r3.xyw * r0.yyy + r0.www;
    r1.w = min(7, r7.z);
    r1.xz = -g_probe_grid_area.xy + r1.xz;
    r3.xy = g_probe_grid_area.zw + -g_probe_grid_area.xy;
    r1.xz = r1.xz / r3.xy;
    r2.y = g_probe_grid_size;
    r1.xz = r1.xz * r2.yy + float2(-0.5,-0.5);
    r3.x = -1 + r2.y;
    r1.xz = max(float2(0,0), r1.xz);
    r1.xz = min(r1.xz, r3.xx);
    r3.xy = floor(r1.xz);
    r1.xz = frac(r1.xz);
    r9.w = r3.y * r2.y + r3.x;
    r4.xzw = t_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, r1.w).xyz;
    r6.xyzw = float4(1,0,0,1) + r3.xyxy;
    r6.xw = r6.yw * r2.yy + r6.xz;
    r8.xyz = r9.xyz;
    r8.w = r6.x;
    r5.xzw = t_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, r1.w).xyz;
    r6.xyz = r8.xyz;
    r10.xyz = t_environment_array.SampleLevel(s_environment_array_s, r6.xyzw, r1.w).xyz;
    r3.xy = float2(1,1) + r3.xy;
    r11.w = r3.y * r2.y + r3.x;
    r11.xyz = r6.xyz;
    r3.xyw = t_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, r1.w).xyz;
    r5.xzw = r5.xzw + -r4.xzw;
    r4.xzw = r1.xxx * r5.xzw + r4.xzw;
    r3.xyw = r3.xyw + -r10.xyz;
    r3.xyw = r1.xxx * r3.xyw + r10.xyz;
    r3.xyw = r3.xyw + -r4.xzw;
    r3.xyw = r1.zzz * r3.xyw + r4.xzw;
    r2.y = cmp(g_specular_probe_lerp < 0.999000013);
    if (r2.y != 0) {
      r4.xzw = t_prev_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, r1.w).xyz;
      r5.xzw = t_prev_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, r1.w).xyz;
      r6.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r6.xyzw, r1.w).xyz;
      r8.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, r1.w).xyz;
      r5.xzw = r5.xzw + -r4.xzw;
      r4.xzw = r1.xxx * r5.xzw + r4.xzw;
      r5.xzw = r8.xyz + -r6.xyz;
      r5.xzw = r1.xxx * r5.xzw + r6.xyz;
      r5.xzw = r5.xzw + -r4.xzw;
      r1.xzw = r1.zzz * r5.xzw + r4.xzw;
      r4.xzw = r3.xyw + -r1.xzw;
      r1.xzw = g_specular_probe_lerp * r4.xzw + r1.xzw;
    }
    r1.xzw = r2.yyy ? r1.xzw : r3.xyw;
    r1.xzw = r1.xzw * r0.yzw;
    r0.x = min(r0.x, r2.z);
    r2.y = 1 + -r0.x;
    r2.y = r5.y * r2.y + r0.x;
    r1.xzw = r2.yyy * r1.xzw;
    r1.xzw = r1.xzw * r0.xxx;
    if (r4.y != 0) {
      r3.xyw = r7.xyw * r2.xxx;
      r1.xzw = r1.xzw * r2.xxx;
    } else {
      r3.xyw = r7.xyw * r0.yzw;
    }
    const float3 rt_open_environment = RtOccludedEnvironment(rt_texel, r1.xzw);
    r0.x = -1 + r0.x;
    r0.x = r2.w * r0.x + 1;
    r0.y = 16.666666 * r3.z;
    r1.y = saturate(r1.y);
    r0.y = r1.y * r0.y;
    r0.xzw = r3.xyw * r0.xxx + -rt_open_environment;
    r0.y = min(1, r0.y);
    if (rt_reflected) r0.y = rt_traced.a;
    r0.xyz = r0.xzw * r0.yyy - (r1.xzw - rt_open_environment);
  } else {
    r0.xyz = float3(0,0,0);
  }
  t_out_radiance[vThreadID.xy] = r0.xyz;
  return;
}