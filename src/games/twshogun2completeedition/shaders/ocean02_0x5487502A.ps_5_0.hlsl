// ocean.hlsl ps30_main_low_water_quality (decompiled): the sea in sea battles and on coasts at low water quality.
// Ray-Traced Water Reflections (Experimental; ray_tracing.hpp WaterSurface / OnSeaDraw) as ocean00 (high): where the rays left
// from the sea's own surface (b10, and rt_water_plane's mask at t45), their reflection replaces the environment probes' (and the
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

SamplerState s_sky_s : register(s0);
SamplerState s_environment_array_s : register(s1);
SamplerState s_ibl_brdf_lut_s : register(s2);
SamplerState s_cloud_shadow_s : register(s3);
SamplerState sea_surface_sampler_s : register(s4);
SamplerState swell_surface_sampler_s : register(s5);
SamplerState refraction_texture_sampler_s : register(s6);
SamplerState colour_overlay_texture_sampler_s : register(s7);
Texture2D<float4> sea_surface_sampler : register(t0);
Texture2D<float4> swell_surface_sampler : register(t1);
Texture2D<float4> colour_overlay_texture_sampler : register(t2);
TextureCube<float4> t_sky : register(t3);
TextureCubeArray<float4> t_environment_array : register(t4);
TextureCubeArray<float4> t_prev_environment_array : register(t5);
Texture2D<float4> t_ibl_brdf_lut : register(t6);
Texture2D<float4> t_cloud_shadow : register(t7);
Texture2D<float4> g_refraction_texture : register(t8);
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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10;
  uint4 bitmask, uiDest;
  float4 fDest;

  const bool rt_water = RtReflectionsBound() && RtWaterReflections() && t_rt_water.Load(int3(v0.xy, 0)) > 0.5f;
  r0.xy = sea_surface_sampler.Sample(sea_surface_sampler_s, v6.xy).xy;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.xy = v5.xx * r0.xy;
  r0.xy = v1.zz * r0.xy;
  r0.zw = swell_surface_sampler.Sample(swell_surface_sampler_s, v6.zw).xy;
  r0.zw = float2(-0.5,-0.5) + r0.zw;
  r0.xy = float2(6.28318548,6.28318548) * r0.xy;
  r0.xy = r0.zw * float2(6.28318548,6.28318548) + r0.xy;
  sincos(r0.xy, r0.xy, r1.xy);
  r1.z = r1.x * r1.y;
  r1.xy = r0.yx;
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r1.xyz * r0.xxx;
  r1.xyz = saturate(g_deep_water_colour.xyz * float3(0.5,0.5,0.5) + float3(-0.00999999978,-0.00999999978,-0.00999999978));
  r2.xy = g_texel_offset + v0.xy;
  r2.xy = r2.xy / screen_size.xy;
  r2.xyzw = g_refraction_texture.Sample(refraction_texture_sampler_s, r2.xy).xyzw;
  r3.xy = float2(0.600000024,1) + -r2.ww;
  r4.xyz = saturate(g_shallow_water_colour.xyz * float3(6,6,6) + r3.yyy);
  r0.w = saturate(3 * r3.y);
  r2.xyz = r2.xyz * r4.xyz + -r1.xyz;
  r1.xyz = r0.www * r2.xyz + r1.xyz;
  r0.w = cmp(g_specular_probes_count >= 1);
  if (r0.w != 0) {
    r0.w = cmp(g_cloud_shadow_scale != 0.000000);
    if (r0.w != 0) {
      r2.xy = v2.xz / g_cloud_shadow_scale;
      r0.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
      r2.zw = g_cloud_shadow_direction.xy * r0.ww;
      r2.xy = r2.zw * time_in_sec + r2.xy;
      r2.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r2.xy, 0).xyzw;
      r2.xyzw = float4(1,1,1,1) + -r2.xyzw;
      r0.w = 4 * g_cloud_shadow_lerp;
      r0.w = min(1, abs(r0.w));
      r0.w = 1 + -r0.w;
      r4.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
      r4.xyzw = min(float4(1,1,1,1), abs(r4.xyzw));
      r4.xyzw = float4(1,1,1,1) + -r4.xyzw;
      r0.w = r2.x * r4.x + r0.w;
      r0.w = r2.y * r4.y + r0.w;
      r0.w = r2.z * r4.z + r0.w;
      r0.w = r2.w * r4.w + r0.w;
    } else {
      r0.w = 1;
    }
    r0.w = min(1, r0.w);
    r1.w = dot(v7.zxy, r0.xyz);
    r2.x = r1.w + r1.w;
    r2.xyz = r0.yzx * -r2.xxx + v7.xyz;
    r3.y = dot(r0.yzx, -v7.xyz);
    r4.x = max(0, r3.y);
    r4.y = 1;
    r3.yz = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r4.xy, 0).xy;
    r3.y = r3.y + r3.z;
    r3.zw = -g_probe_grid_area.xy + v2.xz;
    r4.xy = g_probe_grid_area.zw + -g_probe_grid_area.xy;
    r3.zw = r3.zw / r4.xy;
    r4.x = g_probe_grid_size;
    r3.zw = r3.zw * r4.xx + float2(-0.5,-0.5);
    r4.y = -1 + r4.x;
    r3.zw = max(float2(0,0), r3.zw);
    r3.zw = min(r3.zw, r4.yy);
    r4.yz = floor(r3.zw);
    r3.zw = frac(r3.zw);
    r2.w = r4.z * r4.x + r4.y;
    r5.xyz = t_environment_array.SampleLevel(s_environment_array_s, r2.xyzw, 0).xyz;
    r6.xyzw = float4(1,0,0,1) + r4.yzyz;
    r6.xw = r6.yw * r4.xx + r6.xz;
    r7.xyz = r2.xyz;
    r7.w = r6.x;
    r8.xyz = t_environment_array.SampleLevel(s_environment_array_s, r7.xyzw, 0).xyz;
    r6.xyz = r7.xyz;
    r9.xyz = t_environment_array.SampleLevel(s_environment_array_s, r6.xyzw, 0).xyz;
    r4.yz = float2(1,1) + r4.yz;
    r4.w = r4.z * r4.x + r4.y;
    r4.xyz = r6.xyz;
    r10.xyz = t_environment_array.SampleLevel(s_environment_array_s, r4.xyzw, 0).xyz;
    r8.xyz = r8.xyz + -r5.xyz;
    r5.xyz = r3.zzz * r8.xyz + r5.xyz;
    r8.xyz = r10.xyz + -r9.xyz;
    r8.xyz = r3.zzz * r8.xyz + r9.xyz;
    r8.xyz = r8.xyz + -r5.xyz;
    r5.xyz = r3.www * r8.xyz + r5.xyz;
    r5.w = cmp(g_specular_probe_lerp < 0.999000013);
    if (r5.w != 0) {
      r2.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r2.xyzw, 0).xyz;
      r8.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r7.xyzw, 0).xyz;
      r6.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r6.xyzw, 0).xyz;
      r4.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r4.xyzw, 0).xyz;
      r8.xyz = r8.xyz + -r2.xyz;
      r2.xyz = r3.zzz * r8.xyz + r2.xyz;
      r4.xyz = r4.xyz + -r6.xyz;
      r4.xyz = r3.zzz * r4.xyz + r6.xyz;
      r4.xyz = r4.xyz + -r2.xyz;
      r2.xyz = r3.www * r4.xyz + r2.xyz;
      r4.xyz = r5.xyz + -r2.xyz;
      r2.xyz = g_specular_probe_lerp * r4.xyz + r2.xyz;
    }
    r2.xyz = r5.www ? r2.xyz : r5.xyz;
    if (rt_water) r2.xyz = RtReflection(v0.xy, r2.xyz);
    r2.w = cmp(0 < sun_specular);
    r3.z = dot(-sun_direction.xyz, r7.xyz);
    r3.z = max(-1, r3.z);
    r3.z = min(1, r3.z);
    r3.w = 1 + -abs(r3.z);
    r3.w = sqrt(r3.w);
    r4.x = abs(r3.z) * -0.0187292993 + 0.0742610022;
    r4.x = r4.x * abs(r3.z) + -0.212114394;
    r4.x = r4.x * abs(r3.z) + 1.57072878;
    r4.y = r4.x * r3.w;
    r4.y = r4.y * -2 + 3.14159274;
    r3.z = cmp(r3.z < -r3.z);
    r3.z = r3.z ? r4.y : 0;
    r3.z = r4.x * r3.w + r3.z;
    r4.xyz = float3(-0.0043633231,0.0043633231,0.0043633231) + r3.zzz;
    r5.xyzw = r2.wwww ? float4(317.353607,352.491241,124250.078,99.1310959) : float4(1.10699046,1.22955728,1.51181114,1.34230018);
    r4.xyz = r5.xxx * r4.xyz;
    r3.zw = float2(0.707106769,0.707106769) * r4.xy;
    r4.y = abs(r3.z) * abs(r3.z);
    r4.w = r4.y * abs(r3.z);
    r5.x = r4.w * abs(r3.z);
    r3.z = abs(r3.z) * 0.278393 + 1;
    r3.z = r4.y * 0.230388999 + r3.z;
    r3.z = r4.w * 0.000972000009 + r3.z;
    r3.z = r5.x * 0.0781079978 + r3.z;
    r3.z = r3.z * r3.z;
    r3.z = r3.z * r3.z;
    r3.z = 1 / r3.z;
    r3.z = 1 + -r3.z;
    r4.y = cmp(0 < r4.x);
    r4.x = cmp(r4.x < 0);
    r4.x = (int)-r4.y + (int)r4.x;
    r4.x = (int)r4.x;
    r3.z = r4.x * r3.z + 1;
    r3.z = 0.5 * r3.z;
    r4.x = r3.w * r3.w;
    r4.y = r4.x * r3.w;
    r3.w = r4.y * r3.w;
    r4.z = r4.z * 0.196853578 + 1;
    r4.x = r4.x * 0.230388999 + r4.z;
    r4.x = r4.y * 0.000972000009 + r4.x;
    r3.w = r3.w * 0.0781079978 + r4.x;
    r3.w = r3.w * r3.w;
    r3.w = r3.w * r3.w;
    r3.w = 1 / r3.w;
    r3.w = 2 + -r3.w;
    r3.z = r3.w * 0.5 + -r3.z;
    r3.w = r5.z * r5.y;
    r4.x = r3.w * r5.y;
    r4.yzw = r2.www ? float3(28625.8516,0,1) : float3(0.348304659,0.320000023,0.679999948);
    r4.y = r5.w + r4.y;
    r3.w = r3.w * 0.000972000009 + r4.y;
    r3.w = r4.x * 0.0781079978 + r3.w;
    r3.w = r3.w * r3.w;
    r3.w = r3.w * r3.w;
    r3.w = 1 / r3.w;
    r4.xy = float2(1,2) + -r3.ww;
    r3.w = 1 + -r4.x;
    r3.w = 0.5 * r3.w;
    r3.w = r4.y * 0.5 + -r3.w;
    r3.z = abs(r3.z) / r3.w;
    r3.z = min(1, r3.z);
    r3.z = r3.z * r3.z;
    r3.z = 0.785398185 * r3.z;
    r3.w = saturate(dot(r0.yzx, -sun_direction.xyz));
    r4.x = -1 + r3.w;
    r4.x = r4.x * 0.95105654 + 1;
    r2.w = r2.w ? 1 : r4.x;
    r1.w = saturate(r1.w);
    r3.w = r3.w * r4.w + r4.z;
    r3.w = 1 / r3.w;
    r1.w = r1.w * r4.w + r4.z;
    r1.w = 1 / r1.w;
    r1.w = r3.w * r1.w;
    r1.w = 0.25 * r1.w;
    r1.w = min(1, r1.w);
    r1.w = r1.w * r2.w;
    r1.w = r3.z * r1.w;
    r4.xyz = sun_colour.xyz * r1.www;
    r4.xyz = r4.xyz * r0.www;
    r2.xyz = r2.xyz * r3.yyy + r4.xyz;
  } else {
    r0.w = dot(v7.zxy, r0.xyz);
    r0.w = r0.w + r0.w;
    r3.yzw = r0.yzx * -r0.www + v7.xyz;
    r0.w = dot(g_sun_direction.xz, g_sun_direction.xz);
    r0.w = rsqrt(r0.w);
    r4.xy = g_sun_direction.xz * r0.ww;
    r5.x = dot(r3.yw, r4.xy);
    r4.z = -r4.x;
    r5.z = dot(r3.yw, r4.yz);
    r5.y = r3.z;
    r3.yzw = t_sky.Sample(s_sky_s, r5.xyz).xyz;
    r3.yzw = g_sky_exposure_clamp_threshold_falloff.xxx * r3.yzw;
    r3.yzw = log2(r3.yzw);
    r3.yzw = g_sky_gamma_etc.xxx * r3.yzw;
    r3.yzw = exp2(r3.yzw);
    r4.xyz = float3(1,1,1) + r3.yzw;
    r4.xyz = -g_sky_exposure_clamp_threshold_falloff.zzz + r4.xyz;
    r4.xyz = log2(abs(r4.xyz));
    r4.xyz = g_sky_exposure_clamp_threshold_falloff.www * r4.xyz;
    r4.xyz = exp2(r4.xyz);
    r4.xyz = g_sky_exposure_clamp_threshold_falloff.zzz + r4.xyz;
    r4.xyz = float3(-1,-1,-1) + r4.xyz;
    r5.xyz = cmp(g_sky_exposure_clamp_threshold_falloff.zzz < r3.yzw);
    r5.xyz = r5.xyz ? float3(1,1,1) : 0;
    r4.xyz = r4.xyz + -r3.yzw;
    r3.yzw = r5.xyz * r4.xyz + r3.yzw;
    r2.xyz = min(g_sky_exposure_clamp_threshold_falloff.yyy, r3.yzw);
    if (rt_water) r2.xyz = RtReflection(v0.xy, r2.xyz);
  }
  r0.w = dot(v7.zxy, r0.xyz);
  r0.w = r0.w + r0.w;
  r3.yzw = r0.yzx * -r0.www;
  r4.xyz = r0.yzx * -r0.www + v7.xyz;
  r0.w = dot(r3.yzw, r3.yzw);
  r0.w = rsqrt(r0.w);
  r3.yzw = r3.yzw * r0.www;
  r0.w = saturate(dot(r3.yzw, r4.xyz));
  r1.w = r0.w * -5.55472994 + -6.98316002;
  r0.w = r1.w * r0.w;
  r0.w = exp2(r0.w);
  r0.w = r0.w * 0.979667783 + 0.0203322452;
  r2.xyz = r2.xyz + -r1.xyz;
  r2.xyz = r0.www * r2.xyz + r1.xyz;
  r3.yz = v4.xy / v4.ww;
  r3.yz = float2(0.5,0.5) + r3.yz;
  r4.xyzw = colour_overlay_texture_sampler.Sample(colour_overlay_texture_sampler_s, r3.yz).xyzw;
  r2.xyz = r4.xyz + r2.xyz;
  r0.w = r4.w * r0.w;
  r3.x = saturate(r3.x);
  r1.xyz = -r2.xyz + r1.xyz;
  o0.xyz = r3.xxx * r1.xyz + r2.xyz;
  r1.x = 1 + -r3.x;
  o2.x = r1.x * r0.w;
  r0.w = min(abs(r0.x), abs(r0.y));
  r1.x = max(abs(r0.x), abs(r0.y));
  r1.x = 1 / r1.x;
  r0.w = r1.x * r0.w;
  r1.x = r0.w * r0.w;
  r1.y = r1.x * 0.0208350997 + -0.0851330012;
  r1.y = r1.x * r1.y + 0.180141002;
  r1.y = r1.x * r1.y + -0.330299497;
  r1.x = r1.x * r1.y + 0.999866009;
  r1.y = r1.x * r0.w;
  r1.z = cmp(abs(r0.y) < abs(r0.x));
  r1.y = r1.y * -2 + 1.57079637;
  r1.y = r1.z ? r1.y : 0;
  r0.w = r0.w * r1.x + r1.y;
  r1.xy = cmp(r0.yz < -r0.yz);
  r1.x = r1.x ? -3.14159274 : 0;  // the game's l(0xc0490fdb): -pi to the bit (the decompile printed -3.141593, one ulp off)
  r0.w = r1.x + r0.w;
  r1.x = min(r0.x, r0.y);
  r0.x = max(r0.x, r0.y);
  r0.y = cmp(r1.x < -r1.x);
  r0.x = cmp(r0.x >= -r0.x);
  r0.x = r0.x ? r0.y : 0;
  r0.x = r0.x ? -r0.w : r0.w;
  r0.y = 1 + -abs(r0.z);
  r0.y = sqrt(r0.y);
  r0.w = abs(r0.z) * -0.0187292993 + 0.0742610022;
  r0.w = r0.w * abs(r0.z) + -0.212114394;
  r0.z = r0.w * abs(r0.z) + 1.57072878;
  r0.w = r0.z * r0.y;
  r0.w = r0.w * -2 + 3.14159274;
  r0.w = r1.y ? r0.w : 0;
  r0.y = r0.z * r0.y + r0.w;
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