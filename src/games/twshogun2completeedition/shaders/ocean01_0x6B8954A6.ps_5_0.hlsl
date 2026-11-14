// ocean.hlsl ps30_main_ultra_water_quality (decompiled): the sea in sea battles and on coasts.
// Ray-Traced Water Reflections (Experimental; ray_tracing.hpp WaterSurface / OnSeaDraw): where the rays left from the
// sea's own surface (b10, and rt_water_plane's mask at t45), their reflection replaces the environment probes' (and the
// sky's where the game reads the sky alone) for the share of rays that met something; the rest as the game's.

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

cbuffer lighting_VS_PS : register(b2)
{
  float3 sun_direction : packoffset(c0);
  float3 sun_colour : packoffset(c1);
  float sun_specular : packoffset(c1.w);
  float g_cube_ambient_influence : packoffset(c2);
  float3 ambient_cube_lr[2] : packoffset(c3);
  float3 ambient_cube_tb[2] : packoffset(c5);
  float3 ambient_cube_fb[2] : packoffset(c7);
  float3 g_deep_water_colour : packoffset(c9);
  float3 g_shallow_water_colour : packoffset(c10);
  bool g_use_spherical_harmonics : packoffset(c10.w);
  float2 g_cloud_shadow_direction : packoffset(c11);
  float g_cloud_shadow_speed : packoffset(c11.z);
  float g_cloud_shadow_scale : packoffset(c11.w);
  float g_cloud_shadow_lerp : packoffset(c12);
  float4 g_playable_area_bounds : packoffset(c13);
  float g_vegetation_wrap_lighting_bias : packoffset(c14);
  float g_ambient_fudge_factor : packoffset(c14.y);
  float g_time_of_day_unary : packoffset(c14.z);
  float g_SSAO_low_clamp : packoffset(c14.w);
  float g_Shadow_low_clamp : packoffset(c15);
  float g_light_scale_for_units : packoffset(c15.y);
  float4 g_water_reflection_tint : packoffset(c16);
  float g_water_reflection_strength : packoffset(c17);
}

cbuffer wind_PS : register(b3)
{
  float2 wind_direction_PS : packoffset(c0) = {0,0};
}

cbuffer ocean_PS : register(b4)
{
  float g_fresnel_R0 : packoffset(c0);
  float g_sea_shininess : packoffset(c0.y);
  float g_froth_value : packoffset(c0.z);
  float g_froth_distortion_speed : packoffset(c0.w);
  float g_froth_distortion_amount : packoffset(c1);
  float g_refraction_depth : packoffset(c1.y);
}

SamplerState s_sky_s : register(s0);
SamplerState s_environment_array_s : register(s1);
SamplerState s_ibl_brdf_lut_s : register(s2);
SamplerState s_cloud_shadow_s : register(s3);
SamplerState sea_surface_sampler_s : register(s4);
SamplerState swell_surface_sampler_s : register(s5);
SamplerState combined_foam_texture_sampler_s : register(s6);
SamplerState refraction_texture_sampler_s : register(s7);
SamplerState colour_overlay_texture_sampler_s : register(s8);
Texture2D<float4> sea_surface_sampler : register(t0);
Texture2D<float4> swell_surface_sampler : register(t1);
Texture2D<float4> combined_foam_texture_sampler : register(t2);
Texture2D<float4> colour_overlay_texture_sampler : register(t3);
TextureCube<float4> t_sky : register(t4);
TextureCubeArray<float4> t_environment_array : register(t5);
TextureCubeArray<float4> t_prev_environment_array : register(t6);
Texture2D<float4> t_world_spherical_harmonics : register(t7);
Texture2D<float4> t_ibl_brdf_lut : register(t8);
Texture2D<float4> t_cloud_shadow : register(t9);
Texture2D<float4> gbuffer_colour_texture : register(t10);
Texture2D<float4> g_refraction_texture : register(t11);
Texture2D<float> g_refraction_normal_texture : register(t12);
Texture2D<float> t_rt_water : register(t45);  // rt_water_plane's mask (unbound: 0)

#include "../../twattila/rt_reflection.hlsli"


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float3 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  float4 v8 : TEXCOORD6,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float o3 : SV_Target3,
  out float o4 : SV_Target4)
{
  const float4 icb[] = { { 0, 0, 0, 0},
                              { 1, 0, 0, 0},
                              { 2, 0, 0, 0},
                              { 0, 1, 0, 0},
                              { 1, 1, 0, 0},
                              { 2, 1, 0, 0},
                              { 0, 2, 0, 0},
                              { 1, 2, 0, 0},
                              { 2, 2, 0, 0} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18;
  uint4 bitmask, uiDest;
  float4 fDest;

  const bool rt_water = RtReflectionsBound() && RtWaterReflections() && t_rt_water.Load(int3(v0.xy, 0)) > 0.5f;
  r0.xy = wind_direction_PS.xy * time_in_sec;
  r0.zw = float2(0.400000006,0.400000006) * r0.xy;
  r1.xy = -r0.xy * float2(0.0399999991,0.0399999991) + v6.xy;
  r1.xy = sea_surface_sampler.Sample(sea_surface_sampler_s, r1.xy).xy;
  r1.xy = float2(-0.5,-0.5) + r1.xy;
  r0.zw = v6.xy * float2(20,20) + -r0.zw;
  r0.zw = sea_surface_sampler.Sample(sea_surface_sampler_s, r0.zw).xy;
  r0.zw = float2(-0.5,-0.5) + r0.zw;
  r0.zw = v5.xx * r0.zw;
  r0.zw = float2(3.76991153,3.76991153) * r0.zw;
  r0.zw = r1.xy * float2(6.28318548,6.28318548) + r0.zw;
  r0.xy = -r0.xy * float2(0.00666666683,0.00666666683) + v6.zw;
  r0.xy = swell_surface_sampler.Sample(swell_surface_sampler_s, r0.xy).xy;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.xy = float2(6.28318548,6.28318548) * r0.xy;
  r0.zw = r0.zw * v1.zz + r0.xy;
  sincos(r0.zw, r1.xy, r2.xy);
  r2.z = r2.x * r2.y;
  r2.xy = r1.yx;
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  r1.xyz = r2.xyz * r0.zzz;
  sincos(r0.xy, r0.xy, r2.xy);
  r2.y = r2.x * r2.y;
  r2.xz = r0.xy;
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  r0.xz = r0.xy * r0.zz;
  r2.xyz = -camera_position.xyz + v2.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = r2.xyz * r0.www;
  r0.y = 0.349999994;
  r1.w = dot(r0.xyz, r0.xyz);
  r1.w = rsqrt(r1.w);
  r0.xyz = r1.www * r0.xyz;
  r0.x = saturate(dot(-r3.xyz, r0.xyz));
  r0.y = 0.200000003 * v2.y;
  r0.x = saturate(r0.x * 2 + -r0.y);
  r4.xyz = -g_shallow_water_colour.xyz + g_deep_water_colour.xyz;
  r0.xyz = r0.xxx * r4.xyz + g_shallow_water_colour.xyz;
  r0.xyz = saturate(r0.xyz * float3(0.5,0.5,0.5) + float3(-0.00999999978,-0.00999999978,-0.00999999978));
  r4.xy = g_texel_offset + v0.xy;
  r4.xy = r4.xy / screen_size.xy;
  gbuffer_colour_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r4.zw = uiDest.xy;
  r5.xy = (uint2)r4.zw;
  r5.xy = r5.xy * r4.xy;
  r5.xy = floor(r5.xy);
  r5.xy = (int2)r5.xy;
  r4.zw = (int2)r4.zw + int2(-1,-1);
  r5.xy = max(int2(0,0), (int2)r5.xy);
  r5.xy = min((int2)r5.xy, (int2)r4.zw);
  r5.zw = float2(0,0);
  r1.w = gbuffer_colour_texture.Load(r5.xyz).w;
  r1.w = r1.w * 2 + -1;
  r1.w = cmp(r1.w < 0);
  r1.w = r1.w ? 1.000000 : 0;
  r5.xyzw = g_refraction_texture.Sample(refraction_texture_sampler_s, r4.xy).xyzw;
  r2.w = g_refraction_normal_texture.Sample(refraction_texture_sampler_s, r4.xy).x;
  r4.xy = r1.yx * float2(0.0199999996,0.0199999996) + r4.xy;
  r4.xyzw = g_refraction_texture.Sample(refraction_texture_sampler_s, r4.xy).xyzw;
  r3.w = cmp(0.00100000005 < r4.w);
  r4.xyzw = r4.wxyz * float4(-1,1,1,1) + float4(1,0,0,0);
  r5.xyzw = r5.wxyz * float4(-1,1,1,1) + float4(1,0,0,0);
  r4.xyzw = r3.wwww ? r4.xyzw : r5.xyzw;
  r5.xyz = saturate(g_shallow_water_colour.xyz * float3(6,6,6) + r4.xxx);
  r3.w = saturate(3 * r4.x);
  r4.yzw = r4.yzw * r5.xyz + -r0.xyz;
  r0.xyz = r3.www * r4.yzw + r0.xyz;
  r3.w = cmp(g_specular_probes_count >= 1);
  if (r3.w != 0) {
    r3.w = cmp(g_cloud_shadow_scale != 0.000000);
    if (r3.w != 0) {
      r4.yz = v2.xz / g_cloud_shadow_scale;
      r3.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
      r5.xy = g_cloud_shadow_direction.xy * r3.ww;
      r4.yz = r5.xy * time_in_sec + r4.yz;
      r5.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r4.yz, 0).xyzw;
      r5.xyzw = float4(1,1,1,1) + -r5.xyzw;
      r3.w = 4 * g_cloud_shadow_lerp;
      r3.w = min(1, abs(r3.w));
      r3.w = 1 + -r3.w;
      r6.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
      r6.xyzw = min(float4(1,1,1,1), abs(r6.xyzw));
      r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
      r3.w = r5.x * r6.x + r3.w;
      r3.w = r5.y * r6.y + r3.w;
      r3.w = r5.z * r6.z + r3.w;
      r3.w = r5.w * r6.w + r3.w;
    } else {
      r3.w = 1;
    }
    r3.w = min(1, r3.w);
    r4.y = dot(v7.zxy, r1.xyz);
    r4.z = r4.y + r4.y;
    r5.xyz = r1.yzx * -r4.zzz + v7.xyz;
    r4.z = dot(r1.yzx, -v7.xyz);
    r6.x = max(0, r4.z);
    r6.y = 1;
    r4.zw = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r6.xy, 0).xy;
    r4.z = r4.z + r4.w;
    r6.xy = -g_probe_grid_area.xy + v2.xz;
    r6.zw = g_probe_grid_area.zw + -g_probe_grid_area.xy;
    r6.xy = r6.xy / r6.zw;
    r4.w = g_probe_grid_size;
    r6.xy = r6.xy * r4.ww + float2(-0.5,-0.5);
    r6.z = -1 + r4.w;
    r6.xy = max(float2(0,0), r6.xy);
    r6.xy = min(r6.xy, r6.zz);
    r6.zw = floor(r6.xy);
    r6.xy = frac(r6.xy);
    r5.w = r6.w * r4.w + r6.z;
    r7.xyz = t_environment_array.SampleLevel(s_environment_array_s, r5.xyzw, 0).xyz;
    r8.xyzw = float4(1,0,0,1) + r6.zwzw;
    r8.xw = r8.yw * r4.ww + r8.xz;
    r9.xyz = r5.xyz;
    r9.w = r8.x;
    r10.xyz = t_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, 0).xyz;
    r8.xyz = r9.xyz;
    r11.xyz = t_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, 0).xyz;
    r6.zw = float2(1,1) + r6.zw;
    r12.w = r6.w * r4.w + r6.z;
    r12.xyz = r8.xyz;
    r13.xyz = t_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, 0).xyz;
    r10.xyz = r10.xyz + -r7.xyz;
    r7.xyz = r6.xxx * r10.xyz + r7.xyz;
    r10.xyz = r13.xyz + -r11.xyz;
    r10.xyz = r6.xxx * r10.xyz + r11.xyz;
    r10.xyz = r10.xyz + -r7.xyz;
    r7.xyz = r6.yyy * r10.xyz + r7.xyz;
    r4.w = cmp(g_specular_probe_lerp < 0.999000013);
    if (r4.w != 0) {
      r5.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r5.xyzw, 0).xyz;
      r10.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, 0).xyz;
      r8.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, 0).xyz;
      r11.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, 0).xyz;
      r10.xyz = r10.xyz + -r5.xyz;
      r5.xyz = r6.xxx * r10.xyz + r5.xyz;
      r10.xyz = r11.xyz + -r8.xyz;
      r6.xzw = r6.xxx * r10.xyz + r8.xyz;
      r6.xzw = r6.xzw + -r5.xyz;
      r5.xyz = r6.yyy * r6.xzw + r5.xyz;
      r6.xyz = r7.xyz + -r5.xyz;
      r5.xyz = g_specular_probe_lerp * r6.xyz + r5.xyz;
    }
    r5.xyz = r4.www ? r5.xyz : r7.xyz;
    if (rt_water) r5.xyz = RtReflection(v0.xy, r5.xyz);
    r4.w = cmp(0 < sun_specular);
    r5.w = dot(-sun_direction.xyz, r9.xyz);
    r5.w = max(-1, r5.w);
    r5.w = min(1, r5.w);
    r6.x = 1 + -abs(r5.w);
    r6.x = sqrt(r6.x);
    r6.y = abs(r5.w) * -0.0187292993 + 0.0742610022;
    r6.y = r6.y * abs(r5.w) + -0.212114394;
    r6.y = r6.y * abs(r5.w) + 1.57072878;
    r6.z = r6.y * r6.x;
    r6.z = r6.z * -2 + 3.14159274;
    r5.w = cmp(r5.w < -r5.w);
    r5.w = r5.w ? r6.z : 0;
    r5.w = r6.y * r6.x + r5.w;
    r6.xyz = float3(-0.0043633231,0.0043633231,0.0043633231) + r5.www;
    r7.xyzw = r4.wwww ? float4(317.353607,352.491241,124250.078,99.1310959) : float4(1.10699046,1.22955728,1.51181114,1.34230018);
    r6.xyz = r7.xxx * r6.xyz;
    r6.yw = float2(0.707106769,0.707106769) * r6.xy;
    r5.w = abs(r6.y) * abs(r6.y);
    r7.x = r5.w * abs(r6.y);
    r8.x = r7.x * abs(r6.y);
    r6.y = abs(r6.y) * 0.278393 + 1;
    r5.w = r5.w * 0.230388999 + r6.y;
    r5.w = r7.x * 0.000972000009 + r5.w;
    r5.w = r8.x * 0.0781079978 + r5.w;
    r5.w = r5.w * r5.w;
    r5.w = r5.w * r5.w;
    r5.w = 1 / r5.w;
    r5.w = 1 + -r5.w;
    r6.y = cmp(0 < r6.x);
    r6.x = cmp(r6.x < 0);
    r6.x = (int)-r6.y + (int)r6.x;
    r6.x = (int)r6.x;
    r5.w = r6.x * r5.w + 1;
    r5.w = 0.5 * r5.w;
    r6.x = r6.w * r6.w;
    r6.y = r6.x * r6.w;
    r6.w = r6.y * r6.w;
    r6.z = r6.z * 0.196853578 + 1;
    r6.x = r6.x * 0.230388999 + r6.z;
    r6.x = r6.y * 0.000972000009 + r6.x;
    r6.x = r6.w * 0.0781079978 + r6.x;
    r6.x = r6.x * r6.x;
    r6.x = r6.x * r6.x;
    r6.x = 1 / r6.x;
    r6.x = 2 + -r6.x;
    r5.w = r6.x * 0.5 + -r5.w;
    r6.x = r7.z * r7.y;
    r6.y = r6.x * r7.y;
    r7.xyz = r4.www ? float3(28625.8516,0,1) : float3(0.348304659,0.320000023,0.679999948);
    r6.z = r7.w + r7.x;
    r6.x = r6.x * 0.000972000009 + r6.z;
    r6.x = r6.y * 0.0781079978 + r6.x;
    r6.x = r6.x * r6.x;
    r6.x = r6.x * r6.x;
    r6.x = 1 / r6.x;
    r6.xy = float2(1,2) + -r6.xx;
    r6.x = 1 + -r6.x;
    r6.x = 0.5 * r6.x;
    r6.x = r6.y * 0.5 + -r6.x;
    r5.w = abs(r5.w) / r6.x;
    r5.w = min(1, r5.w);
    r5.w = r5.w * r5.w;
    r5.w = 0.785398185 * r5.w;
    r6.x = saturate(dot(r1.yzx, -sun_direction.xyz));
    r6.y = -1 + r6.x;
    r6.y = r6.y * 0.95105654 + 1;
    r4.w = r4.w ? 1 : r6.y;
    r4.y = saturate(r4.y);
    r6.x = r6.x * r7.z + r7.y;
    r6.x = 1 / r6.x;
    r4.y = r4.y * r7.z + r7.y;
    r4.y = 1 / r4.y;
    r4.y = r6.x * r4.y;
    r4.y = 0.25 * r4.y;
    r4.y = min(1, r4.y);
    r4.y = r4.y * r4.w;
    r4.y = r5.w * r4.y;
    r6.xyz = sun_colour.xyz * r4.yyy;
    r6.xyz = r6.xyz * r3.www;
    r4.yzw = r5.xyz * r4.zzz + r6.xyz;
  } else {
    r3.w = dot(v7.zxy, r1.xyz);
    r3.w = r3.w + r3.w;
    r5.xyz = r1.yzx * -r3.www + v7.xyz;
    r3.w = dot(g_sun_direction.xz, g_sun_direction.xz);
    r3.w = rsqrt(r3.w);
    r6.xy = g_sun_direction.xz * r3.ww;
    r7.x = dot(r5.xz, r6.xy);
    r6.z = -r6.x;
    r7.z = dot(r5.xz, r6.yz);
    r7.y = r5.y;
    r5.xyz = t_sky.Sample(s_sky_s, r7.xyz).xyz;
    r5.xyz = g_sky_exposure_clamp_threshold_falloff.xxx * r5.xyz;
    r5.xyz = log2(r5.xyz);
    r5.xyz = g_sky_gamma_etc.xxx * r5.xyz;
    r5.xyz = exp2(r5.xyz);
    r6.xyz = float3(1,1,1) + r5.xyz;
    r6.xyz = -g_sky_exposure_clamp_threshold_falloff.zzz + r6.xyz;
    r6.xyz = log2(abs(r6.xyz));
    r6.xyz = g_sky_exposure_clamp_threshold_falloff.www * r6.xyz;
    r6.xyz = exp2(r6.xyz);
    r6.xyz = g_sky_exposure_clamp_threshold_falloff.zzz + r6.xyz;
    r6.xyz = float3(-1,-1,-1) + r6.xyz;
    r7.xyz = cmp(g_sky_exposure_clamp_threshold_falloff.zzz < r5.xyz);
    r7.xyz = r7.xyz ? float3(1,1,1) : 0;
    r6.xyz = r6.xyz + -r5.xyz;
    r5.xyz = r7.xyz * r6.xyz + r5.xyz;
    r4.yzw = min(g_sky_exposure_clamp_threshold_falloff.yyy, r5.xyz);
    if (rt_water) r4.yzw = RtReflection(v0.xy, r4.yzw);
  }
  r3.w = dot(v7.zxy, r1.xyz);
  r3.w = r3.w + r3.w;
  r5.xyz = -r3.www * r1.yzx;
  r6.xyz = r1.yzx * -r3.www + v7.xyz;
  r3.w = dot(r5.xyz, r5.xyz);
  r3.w = rsqrt(r3.w);
  r5.xyz = r5.xyz * r3.www;
  r3.w = saturate(dot(r5.xyz, r6.xyz));
  r5.x = r3.w * -5.55472994 + -6.98316002;
  r3.w = r5.x * r3.w;
  r3.w = exp2(r3.w);
  r3.w = r3.w * 0.979667783 + 0.0203322452;
  r4.yzw = r4.yzw + -r0.xyz;
  r4.yzw = r3.www * r4.yzw + r0.xyz;
  r5.x = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, v6.xy).x;
  r5.yzw = float3(-0.00999999978,0.0166666675,0.000416666677) * time_in_sec;
  r6.xy = v6.xy * float2(0.25,0.25) + r5.yy;
  r5.y = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r6.xy).x;
  r5.x = dot(r5.xx, r5.yy);
  r5.y = min(500, v7.w);
  r5.y = -r5.y * 0.00200000009 + 1;
  r5.x = r5.x * r5.y;
  r6.xy = float2(15,15) * v6.xy;
  r7.y = time_in_sec * g_froth_distortion_speed + r6.y;
  r7.x = 15 * v6.x;
  r5.y = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r7.xy).w;
  r5.y = r5.y * 2 + -1;
  r6.xy = r5.yy * g_froth_distortion_amount + r6.xy;
  r5.y = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r6.xy).w;
  r6.x = cmp(r5.x >= g_froth_value);
  if (r6.x != 0) {
    r6.x = r5.x * r5.y;
  } else {
    r6.yz = time_in_sec * float2(-0.00499999989,-0.00499999989) + v6.xy;
    r6.yz = float2(15,15) * r6.yz;
    r6.y = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r6.yz).z;
    r6.z = 1 / g_froth_value;
    r6.z = r6.z * r5.x;
    r6.w = r6.z * r6.z;
    r6.z = r6.z * r6.w;
    r5.y = -r6.y + r5.y;
    r5.y = r6.z * r5.y + r6.y;
    r6.x = r5.x * r5.y;
  }
  r5.x = 1 + -r6.x;
  r5.x = r5.x * r5.x;
  r5.y = cmp(g_cloud_shadow_scale != 0.000000);
  if (r5.y != 0) {
    r6.yz = v2.xz / g_cloud_shadow_scale;
    r6.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
    r7.xy = g_cloud_shadow_direction.xy * r6.ww;
    r6.yz = r7.xy * time_in_sec + r6.yz;
    r7.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r6.yz, 0).xyzw;
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r6.y = 4 * g_cloud_shadow_lerp;
    r6.y = min(1, abs(r6.y));
    r6.y = 1 + -r6.y;
    r8.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
    r8.xyzw = min(float4(1,1,1,1), abs(r8.xyzw));
    r8.xyzw = float4(1,1,1,1) + -r8.xyzw;
    r6.y = r7.x * r8.x + r6.y;
    r6.y = r7.y * r8.y + r6.y;
    r6.y = r7.z * r8.z + r6.y;
    r6.y = r7.w * r8.w + r6.y;
  } else {
    r6.y = 1;
  }
  r6.z = dot(r3.zxy, r1.xyz);
  r6.w = r6.z + r6.z;
  r7.xyz = r1.yzx * -r6.www + r3.xyz;
  if (g_use_spherical_harmonics != 0) {
    t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r8.xy = uiDest.xy;
    uiDest.xy = (uint2)r8.xy / int2(3,3);
    r8.xy = uiDest.xy;
    r8.xy = (uint2)r8.xy;
    r8.xy = float2(0.5,0.5) * r8.xy;
    r8.xy = trunc(r8.xy);
    r9.xyzw = r1.yxzx * r1.xzyx;
    r6.w = r9.w * 3 + -1;
    r8.z = r1.z * r1.z;
    r8.z = r1.y * r1.y + -r8.z;
    r8.w = round(g_spherical_harmonic_terms);
    r8.w = (int)r8.w;
    r10.zw = float2(0,0);
    r11.xyz = float3(0,0,0);
    r9.w = 0;
    while (true) {
      r11.w = cmp((int)r9.w >= (int)r8.w);
      if (r11.w != 0) break;
      r11.w = (uint)r9.w % 3;
      r11.w = (int)r11.w;
      r11.w = r8.x * 3 + r11.w;
      r12.x = (int)icb[r9.w+0].y;
      r12.x = r8.y * 3 + r12.x;
      r10.x = (int)r11.w;
      r10.y = (int)r12.x;
      r12.xyz = t_world_spherical_harmonics.Load(r10.xyz).xyz;
      r13.xyzw = cmp((int4)r9.wwww == int4(0,1,2,3));
      r14.xyzw = cmp((int4)r9.wwww == int4(4,5,6,7));
      r10.x = cmp((int)r9.w == 8);
      r13.xyzw = r13.xyzw ? float4(1,1,1,1) : 0;
      r14.xyzw = r14.xyzw ? float4(1,1,1,1) : 0;
      r10.x = r10.x ? 1.000000 : 0;
      r10.y = r13.y * r1.y + r13.x;
      r10.y = r13.z * r1.z + r10.y;
      r10.y = r13.w * r1.x + r10.y;
      r10.y = r14.x * r9.x + r10.y;
      r10.y = r14.y * r9.y + r10.y;
      r10.y = r14.z * r9.z + r10.y;
      r10.y = r14.w * r6.w + r10.y;
      r10.x = r10.x * r8.z + r10.y;
      r11.xyz = r12.xyz * r10.xxx + r11.xyz;
      r9.w = (int)r9.w + 1;
    }
    r8.xyz = max(float3(0,0,0), r11.xyz);
    r8.xyz = g_ambient_fudge_factor * r8.xyz;
  } else {
    r9.xyz = cmp(r1.yzx < float3(0,0,0));
    r10.xyz = r9.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
    r9.xyw = r9.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
    r11.xyz = r9.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
    r12.xyz = r1.yzx * r1.yzx;
    r9.xyz = r12.yyy * r9.xyw;
    r9.xyz = r12.xxx * r10.xyz + r9.xyz;
    r9.xyz = r12.zzz * r11.xyz + r9.xyz;
    r8.xyz = g_ambient_fudge_factor * r9.xyz;
  }
  r3.x = dot(r1.yzx, -r3.xyz);
  r3.xy = max(float2(0,-1), r3.xx);
  r3.z = 0;
  r3.xz = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r3.xz, 0).xy;
  r3.x = r3.x * 0.0399999991 + r3.z;
  r9.xy = -g_probe_grid_area.xy + v2.xz;
  r9.zw = g_probe_grid_area.zw + -g_probe_grid_area.xy;
  r9.xy = r9.xy / r9.zw;
  r3.z = g_probe_grid_size;
  r9.xy = r9.xy * r3.zz + float2(-0.5,-0.5);
  r6.w = -1 + r3.z;
  r9.xy = max(float2(0,0), r9.xy);
  r9.xy = min(r9.xy, r6.ww);
  r9.zw = floor(r9.xy);
  r9.xy = frac(r9.xy);
  r7.w = r9.w * r3.z + r9.z;
  r10.xyz = t_environment_array.SampleLevel(s_environment_array_s, r7.xyzw, 7).xyz;
  r11.xyzw = float4(1,0,0,1) + r9.zwzw;
  r11.xw = r11.yw * r3.zz + r11.xz;
  r12.xyz = r7.xyz;
  r12.w = r11.x;
  r13.xyz = t_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, 7).xyz;
  r11.xyz = r12.xyz;
  r14.xyz = t_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, 7).xyz;
  r9.zw = float2(1,1) + r9.zw;
  r15.w = r9.w * r3.z + r9.z;
  r15.xyz = r11.xyz;
  r16.xyz = t_environment_array.SampleLevel(s_environment_array_s, r15.xyzw, 7).xyz;
  r13.xyz = r13.xyz + -r10.xyz;
  r10.xyz = r9.xxx * r13.xyz + r10.xyz;
  r13.xyz = r16.xyz + -r14.xyz;
  r13.xyz = r9.xxx * r13.xyz + r14.xyz;
  r13.xyz = r13.xyz + -r10.xyz;
  r10.xyz = r9.yyy * r13.xyz + r10.xyz;
  r3.z = cmp(g_specular_probe_lerp < 0.999000013);
  if (r3.z != 0) {
    r13.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r7.xyzw, 7).xyz;
    r14.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, 7).xyz;
    r16.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, 7).xyz;
    r17.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r15.xyzw, 7).xyz;
    r14.xyz = r14.xyz + -r13.xyz;
    r13.xyz = r9.xxx * r14.xyz + r13.xyz;
    r14.xyz = r17.xyz + -r16.xyz;
    r14.xyz = r9.xxx * r14.xyz + r16.xyz;
    r14.xyz = r14.xyz + -r13.xyz;
    r13.xyz = r9.yyy * r14.xyz + r13.xyz;
    r14.xyz = -r13.xyz + r10.xyz;
    r13.xyz = g_specular_probe_lerp * r14.xyz + r13.xyz;
  }
  r13.xyz = r3.zzz ? r13.xyz : r10.xyz;
  r13.xyz = r13.xyz * r3.xxx;
  r8.xyz = r8.xyz * float3(0.768000007,0.959999979,0.863999963) + r13.xyz;
  r6.w = dot(r1.yzx, -sun_direction.xyz);
  r8.w = saturate(r6.w);
  r9.z = dot(-sun_direction.xyz, r12.xyz);
  r9.z = max(-1, r9.z);
  r9.z = min(1, r9.z);
  r9.w = 1 + -abs(r9.z);
  r9.w = sqrt(r9.w);
  r10.w = abs(r9.z) * -0.0187292993 + 0.0742610022;
  r10.w = r10.w * abs(r9.z) + -0.212114394;
  r10.w = r10.w * abs(r9.z) + 1.57072878;
  r13.x = r10.w * r9.w;
  r13.x = r13.x * -2 + 3.14159274;
  r9.z = cmp(r9.z < -r9.z);
  r9.z = r9.z ? r13.x : 0;
  r9.z = r10.w * r9.w + r9.z;
  r13.xyz = float3(-0.0043633231,0.0043633231,0.0043633231) + r9.zzz;
  r9.zw = float2(0.626645505,0.626645505) * r13.xy;
  r10.w = abs(r9.z) * abs(r9.z);
  r13.y = r10.w * abs(r9.z);
  r13.w = r13.y * abs(r9.z);
  r9.z = abs(r9.z) * 0.278393 + 1;
  r9.z = r10.w * 0.230388999 + r9.z;
  r9.z = r13.y * 0.000972000009 + r9.z;
  r9.z = r13.w * 0.0781079978 + r9.z;
  r9.z = r9.z * r9.z;
  r9.z = r9.z * r9.z;
  r9.z = 1 / r9.z;
  r9.z = 1 + -r9.z;
  r10.w = cmp(0 < r13.x);
  r13.x = cmp(r13.x < 0);
  r10.w = (int)-r10.w + (int)r13.x;
  r10.w = (int)r10.w;
  r9.z = r10.w * r9.z + 1;
  r9.z = 0.5 * r9.z;
  r10.w = r9.w * r9.w;
  r13.x = r10.w * r9.w;
  r9.w = r13.x * r9.w;
  r13.y = r13.z * 0.17445372 + 1;
  r10.w = r10.w * 0.230388999 + r13.y;
  r10.w = r13.x * 0.000972000009 + r10.w;
  r9.w = r9.w * 0.0781079978 + r10.w;
  r9.w = r9.w * r9.w;
  r9.w = r9.w * r9.w;
  r9.w = 1 / r9.w;
  r9.w = 2 + -r9.w;
  r9.z = r9.w * 0.5 + -r9.z;
  r9.z = 1.19611061 * abs(r9.z);
  r9.z = min(1, r9.z);
  r9.z = r9.z * r9.z;
  r9.z = 0.785398185 * r9.z;
  r6.z = saturate(r6.z);
  r13.xy = r8.ww * float2(0.5,-5.55472994) + float2(0.5,-6.98316002);
  r9.w = 1 / r13.x;
  r6.z = r6.z * 0.5 + 0.5;
  r6.z = 1 / r6.z;
  r6.z = r9.w * r6.z;
  r6.z = r6.z * r8.w;
  r6.z = r6.z * r9.z;
  r2.xyz = -r2.xyz * r0.www + -sun_direction.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r0.w = saturate(dot(r2.xyz, -sun_direction.xyz));
  r2.x = r0.w * -5.55472994 + -6.98316002;
  r0.w = r2.x * r0.w;
  r0.w = exp2(r0.w);
  r0.w = r0.w * 0.959999979 + 0.0399999991;
  r0.w = r0.w * r6.z;
  r0.w = 0.25 * r0.w;
  r2.xyz = sun_colour.xyz * r0.www;
  r0.w = r13.y * r8.w;
  r0.w = exp2(r0.w);
  r0.w = r0.w * 0.959999979 + 0.0399999991;
  r0.w = 1 + -r0.w;
  r6.z = max(-1, r6.w);
  r6.yz = min(float2(1,1), r6.yz);
  r3.y = min(1, r3.y);
  r6.w = 1 + -abs(r6.z);
  r6.w = sqrt(r6.w);
  r9.z = abs(r6.z) * -0.0187292993 + 0.0742610022;
  r9.z = r9.z * abs(r6.z) + -0.212114394;
  r9.z = r9.z * abs(r6.z) + 1.57072878;
  r9.w = r9.z * r6.w;
  r9.w = r9.w * -2 + 3.14159274;
  r10.w = cmp(r6.z < -r6.z);
  r9.w = r10.w ? r9.w : 0;
  r6.w = r9.z * r6.w + r9.w;
  r9.z = 1 + -abs(r3.y);
  r9.z = sqrt(r9.z);
  r9.w = abs(r3.y) * -0.0187292993 + 0.0742610022;
  r9.w = r9.w * abs(r3.y) + -0.212114394;
  r9.w = r9.w * abs(r3.y) + 1.57072878;
  r10.w = r9.w * r9.z;
  r10.w = r10.w * -2 + 3.14159274;
  r3.y = cmp(r3.y < -r3.y);
  r3.y = r3.y ? r10.w : 0;
  r3.y = r9.w * r9.z + r3.y;
  r9.z = max(r3.y, r6.w);
  r9.w = min(r3.y, r6.w);
  r3.y = r6.w + -r3.y;
  r3.y = cos(r3.y);
  r3.y = max(0, r3.y);
  r3.y = 0.420292377 * r3.y;
  r6.w = sin(r9.z);
  r3.y = r6.w * r3.y;
  sincos(r9.w, r13.x, r14.x);
  r6.w = r13.x / r14.x;
  r3.y = r3.y * r6.w + 0.602913618;
  r6.z = max(0, r6.z);
  r3.y = r6.z * r3.y;
  r13.xyz = sun_colour.xyz * r0.www;
  r14.xyz = float3(0.800000012,1,0.899999976) * r13.xyz;
  r0.w = cmp(0.5 < g_camera_temp0.x);
  r0.w = r0.w ? r8.w : r3.y;
  r14.xyz = r14.xyz * r0.www;
  r14.xyz = r14.xyz * float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) + r2.xyz;
  r6.yzw = r6.yyy * r14.xyz + r8.xyz;
  r6.yzw = r6.yzw + -r4.yzw;
  r4.yzw = r6.xxx * r6.yzw + r4.yzw;
  r3.y = r5.x * r3.w;
  r6.xy = -time_in_sec * float2(0.0250000004,0.0250000004) + v6.xy;
  r6.xyz = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r6.xy).xyz;
  r5.xz = v6.xy * float2(2,2) + r5.zz;
  r8.xyz = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r5.xz).xyz;
  r5.xz = v6.xy * float2(0.0625,0.0625) + r5.ww;
  r3.w = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r5.xz).x;
  r5.xzw = float3(0.00555555569,0.0285714287,0.0434782617) * time_in_sec;
  r14.xyzw = v6.xyxy * float4(0.125,0.125,8,8) + -r5.xxzz;
  r5.x = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r14.xy).x;
  r5.z = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r14.zw).y;
  r9.zw = v6.xy * float2(6,6) + r5.ww;
  r5.w = combined_foam_texture_sampler.Sample(combined_foam_texture_sampler_s, r9.zw).y;
  r3.w = r3.w * r5.x + -0.5;
  r3.w = saturate(r3.w + r3.w);
  r5.x = r3.w * -2 + 3;
  r3.w = r3.w * r3.w;
  r3.w = r5.x * r3.w;
  r5.x = r6.x * r2.w;
  r9.zw = r8.xz * r2.ww;
  r6.w = r8.x + r6.z;
  r6.x = r8.z * r6.x;
  r6.x = r6.w * r6.x;
  r6.x = saturate(0.5 * r6.x);
  r5.z = r6.x * r5.z;
  r5.z = r5.z * r5.w;
  r2.w = r6.z * r2.w + r9.z;
  r5.x = r9.w * r5.x;
  r2.w = r5.x * r2.w;
  r2.w = 0.5 * r2.w;
  r5.x = saturate(1 + -r4.x);
  r5.x = 1 + -r5.x;
  r5.x = -r5.x * 1.5 + 1;
  r5.x = r5.x * r1.w;
  r5.w = saturate(-v2.y * 0.400000006 + 1.60000002);
  r5.x = r5.x * r5.w;
  r3.w = r5.z * r3.w + -r2.w;
  r2.w = r5.x * r3.w + r2.w;
  r2.w = r2.w * r6.y;
  r2.w = r2.w * r8.y;
  if (r5.y != 0) {
    r5.xy = v2.xz / g_cloud_shadow_scale;
    r3.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
    r5.zw = g_cloud_shadow_direction.xy * r3.ww;
    r5.xy = r5.zw * time_in_sec + r5.xy;
    r5.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r5.xy, 0).xyzw;
    r5.xyzw = float4(1,1,1,1) + -r5.xyzw;
    r3.w = 4 * g_cloud_shadow_lerp;
    r3.w = min(1, abs(r3.w));
    r3.w = 1 + -r3.w;
    r6.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
    r6.xyzw = min(float4(1,1,1,1), abs(r6.xyzw));
    r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
    r3.w = r5.x * r6.x + r3.w;
    r3.w = r5.y * r6.y + r3.w;
    r3.w = r5.z * r6.z + r3.w;
    r3.w = r5.w * r6.w + r3.w;
  } else {
    r3.w = 1;
  }
  r3.w = min(1, r3.w);
  if (g_use_spherical_harmonics != 0) {
    t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r5.xy = uiDest.xy;
    uiDest.xy = (uint2)r5.xy / int2(3,3);
    r5.xy = uiDest.xy;
    r5.xy = (uint2)r5.xy;
    r5.xy = float2(0.5,0.5) * r5.xy;
    r5.xy = trunc(r5.xy);
    r6.xyzw = r1.yxzx * r1.xzyx;
    r5.z = r6.w * 3 + -1;
    r5.w = r1.z * r1.z;
    r5.w = r1.y * r1.y + -r5.w;
    r6.w = round(g_spherical_harmonic_terms);
    r6.w = (int)r6.w;
    r8.zw = float2(0,0);
    r14.xyz = float3(0,0,0);
    r9.z = 0;
    while (true) {
      r9.w = cmp((int)r9.z >= (int)r6.w);
      if (r9.w != 0) break;
      r9.w = (uint)r9.z % 3;
      r9.w = (int)r9.w;
      r9.w = r5.x * 3 + r9.w;
      r10.w = (int)icb[r9.z+0].y;
      r10.w = r5.y * 3 + r10.w;
      r8.x = (int)r9.w;
      r8.y = (int)r10.w;
      r16.xyz = t_world_spherical_harmonics.Load(r8.xyz).xyz;
      r17.xyzw = cmp((int4)r9.zzzz == int4(0,1,2,3));
      r18.xyzw = cmp((int4)r9.zzzz == int4(4,5,6,7));
      r8.x = cmp((int)r9.z == 8);
      r17.xyzw = r17.xyzw ? float4(1,1,1,1) : 0;
      r18.xyzw = r18.xyzw ? float4(1,1,1,1) : 0;
      r8.x = r8.x ? 1.000000 : 0;
      r8.y = r17.y * r1.y + r17.x;
      r8.y = r17.z * r1.z + r8.y;
      r8.y = r17.w * r1.x + r8.y;
      r8.y = r18.x * r6.x + r8.y;
      r8.y = r18.y * r6.y + r8.y;
      r8.y = r18.z * r6.z + r8.y;
      r8.y = r18.w * r5.z + r8.y;
      r8.x = r8.x * r5.w + r8.y;
      r14.xyz = r16.xyz * r8.xxx + r14.xyz;
      r9.z = (int)r9.z + 1;
    }
    r5.xyz = max(float3(0,0,0), r14.xyz);
    r5.xyz = g_ambient_fudge_factor * r5.xyz;
  } else {
    r6.xyz = cmp(r1.yzx < float3(0,0,0));
    r8.xyz = r6.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
    r6.xyw = r6.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
    r14.xyz = r6.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
    r16.xyz = r1.yzx * r1.yzx;
    r6.xyz = r16.yyy * r6.xyw;
    r6.xyz = r16.xxx * r8.xyz + r6.xyz;
    r6.xyz = r16.zzz * r14.xyz + r6.xyz;
    r5.xyz = g_ambient_fudge_factor * r6.xyz;
  }
  r5.xyz = r5.xyz * r2.www;
  if (r3.z != 0) {
    r6.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r7.xyzw, 7).xyz;
    r7.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, 7).xyz;
    r8.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, 7).xyz;
    r11.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r15.xyzw, 7).xyz;
    r7.xyz = r7.xyz + -r6.xyz;
    r6.xyz = r9.xxx * r7.xyz + r6.xyz;
    r7.xyz = r11.xyz + -r8.xyz;
    r7.xyz = r9.xxx * r7.xyz + r8.xyz;
    r7.xyz = r7.xyz + -r6.xyz;
    r6.xyz = r9.yyy * r7.xyz + r6.xyz;
    r7.xyz = r10.xyz + -r6.xyz;
    r6.xyz = g_specular_probe_lerp * r7.xyz + r6.xyz;
  }
  r6.xyz = r3.zzz ? r6.xyz : r10.xyz;
  r6.xyz = r6.xyz * r3.xxx;
  r5.xyz = r5.xyz * float3(0.959999979,0.959999979,0.959999979) + r6.xyz;
  r6.xyz = r13.xyz * r2.www;
  r6.xyz = r6.xyz * r0.www;
  r2.xyz = r6.xyz * float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) + r2.xyz;
  r2.xyz = r3.www * r2.xyz + r5.xyz;
  r2.xyz = r2.xyz * r1.www + r4.yzw;
  r3.xz = v4.xy / v4.ww;
  r3.xz = float2(0.5,0.5) + r3.xz;
  r5.xyzw = colour_overlay_texture_sampler.Sample(colour_overlay_texture_sampler_s, r3.xz).xyzw;
  r2.xyz = r5.xyz + r2.xyz;
  r0.w = r5.w * r3.y;
  r1.w = saturate(-0.400000006 + r4.x);
  r0.xyz = -r2.xyz + r0.xyz;
  o0.xyz = r1.www * r0.xyz + r2.xyz;
  r0.x = 1 + -r1.w;
  o2.x = r0.w * r0.x;
  r0.x = min(abs(r1.x), abs(r1.y));
  r0.y = max(abs(r1.x), abs(r1.y));
  r0.y = 1 / r0.y;
  r0.x = r0.x * r0.y;
  r0.y = r0.x * r0.x;
  r0.z = r0.y * 0.0208350997 + -0.0851330012;
  r0.z = r0.y * r0.z + 0.180141002;
  r0.z = r0.y * r0.z + -0.330299497;
  r0.y = r0.y * r0.z + 0.999866009;
  r0.z = r0.x * r0.y;
  r0.w = cmp(abs(r1.y) < abs(r1.x));
  r0.z = r0.z * -2 + 1.57079637;
  r0.z = r0.w ? r0.z : 0;
  r0.x = r0.x * r0.y + r0.z;
  r0.yz = cmp(r1.yz < -r1.yz);
  r0.y = r0.y ? -3.141593 : 0;
  r0.x = r0.x + r0.y;
  r0.y = min(r1.x, r1.y);
  r0.w = max(r1.x, r1.y);
  r0.y = cmp(r0.y < -r0.y);
  r0.w = cmp(r0.w >= -r0.w);
  r0.y = r0.w ? r0.y : 0;
  r0.x = r0.y ? -r0.x : r0.x;
  r0.y = 1 + -abs(r1.z);
  r0.y = sqrt(r0.y);
  r0.w = abs(r1.z) * -0.0187292993 + 0.0742610022;
  r0.w = r0.w * abs(r1.z) + -0.212114394;
  r0.w = r0.w * abs(r1.z) + 1.57072878;
  r1.x = r0.w * r0.y;
  r1.x = r1.x * -2 + 3.14159274;
  r0.z = r0.z ? r1.x : 0;
  r0.y = r0.w * r0.y + r0.z;
  r0.y = 3.14159274 + -r0.y;
  r0.x = 3.14159274 + r0.x;
  o1.xy = float2(0.318309873,0.159154937) * r0.yx;
  o0.w = 1;
  o1.zw = float2(0,0);
  o2.yzw = float3(0,1,0.250980407);
  o3.x = 0;
  o4.x = 1;
  return;
}