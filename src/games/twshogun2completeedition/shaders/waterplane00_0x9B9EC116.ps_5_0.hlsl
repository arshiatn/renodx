// waterplane.hlsl ps30_battle_high / medium / ultra_water_quality (decompiled): rivers and lakes in battles.
// Ray-Traced Water Reflections (Experimental; ray_tracing.hpp WaterSurface / OnWaterDraw): where the rays left from the
// water's own surface (b10, and rt_water_plane's mask at t45), their reflection replaces the environment probes' (and the
// sky's where the game reads the sky alone) for the share of rays that met something; the rest as the game's.
// Water Clarity and Water Caustics (Experimental, the DLSS addon's WaterClarity / WaterCaustics; Attila's water_effects.hpp):
// the water's depth (t_terrain_refraction's w) before it takes the deep water colour divided by the clarity; the refracted bed
// x (1 + Attila's caustics tile at the surface's world position x the strength), worked out here (no tile texture).
#include "../shared.h"

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

cbuffer WATER_PLANE_SCALARS : register(b3)
{
  float g_campaign_water_scalar : packoffset(c0);
  float g_campaign_water_multiplier_scalar : packoffset(c0.y);
  float g_campaign_water_froth_scalar : packoffset(c0.z);
  float g_campaign_water_ks_scalar : packoffset(c0.w);
  float g_campaign_water_exps_scalar : packoffset(c1);
  float g_campaign_type_value : packoffset(c1.y);
  float g_sea_roughness_scale_campaign : packoffset(c1.z);
  float g_black_border_x_min : packoffset(c1.w);
  float g_black_border_x_max : packoffset(c2);
  float g_black_border_z_min : packoffset(c2.y);
  float g_black_border_z_max : packoffset(c2.z);
}

SamplerState s_sky_s : register(s0);
SamplerState s_environment_array_s : register(s1);
SamplerState s_ibl_brdf_lut_s : register(s2);
SamplerState s_cloud_shadow_s : register(s3);
SamplerState s_diffuse_s : register(s4);
SamplerState s_normal_s : register(s5);
SamplerState s_terrain_refraction_s : register(s6);
TextureCube<float4> t_sky : register(t0);
TextureCubeArray<float4> t_environment_array : register(t1);
TextureCubeArray<float4> t_prev_environment_array : register(t2);
Texture2D<float4> t_world_spherical_harmonics : register(t3);
Texture2D<float4> t_ibl_brdf_lut : register(t4);
Texture2D<float4> t_cloud_shadow : register(t5);
Texture2D<float4> t_diffuse : register(t6);
Texture2D<float4> t_normal : register(t7);
Texture2D<float4> t_terrain_refraction : register(t8);
Texture2D<float> t_rt_water : register(t45);  // rt_water_plane's mask (unbound: 0)

#include "../../twattila/rt_reflection.hlsli"


// 3Dmigoto declarations
#define cmp -

// Attila's water_caustics.cs at `uv` (the tile's 0..1, repeated every WATER_CAUSTICS_TILE metres): bright borders between
// drifting cells, `cells` across, their points circling at `speed` radians a second.
float CausticsBorders(float2 uv, int cells, float speed)
{
  const float tau = 6.2831853f;
  const float2 bend = float2(sin(tau * (3.f * uv.y + uv.x) + time_in_sec * 0.7f * speed),
                             sin(tau * (2.f * uv.x - 3.f * uv.y) - time_in_sec * 0.9f * speed));
  const float2 place = uv * cells + 0.3f * bend;
  const int2 base = int2(floor(place));
  float nearest = 9.f;
  float second = 9.f;
  [unroll]
  for (int y = -1; y <= 1; ++y)
  {
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
      const int2 cell = base + int2(x, y);
      const int2 wrapped = (cell % cells + cells) % cells;
      uint n = uint(wrapped.x) * 1973u + uint(wrapped.y) * 9277u + 26699u;
      n = (n << 13u) ^ n;
      n = n * (n * n * 15731u + 789221u) + 1376312589u;
      const float2 random = float2(n & 0xFFFFu, (n >> 16) & 0xFFFFu) / 65536.f;
      const float angle = tau * random.x + time_in_sec * speed * (0.75f + 0.5f * random.y);
      const float distance_to = distance(place, float2(cell) + 0.5f + 0.35f * float2(cos(angle), sin(angle)));
      second = (distance_to < nearest) ? nearest : min(second, distance_to);
      nearest = min(nearest, distance_to);
    }
  }
  return pow(saturate(1.f - (second - nearest) * 2.5f), 6.f);
}


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15;
  uint4 bitmask, uiDest;
  float4 fDest;

  const bool rt_water = RtReflectionsBound() && RtWaterReflections() && t_rt_water.Load(int3(v0.xy, 0)) > 0.5f;
  r0.xy = g_texel_offset + v0.xy;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.zw = float2(-1,-1) + screen_size.xy;
  r0.xy = r0.xy / r0.zw;
  r0.zw = float2(0.5,0.5) * v1.zw;
  r1.xy = v1.xy * float2(-1.10000002,-1.20000005) + float2(0.100000001,0.200000003);
  r1.zw = float2(0.100000001,0.100000001) * v2.zw;
  r2.xy = v2.xy * float2(0.899999976,0.899999976) + float2(0.200000003,0.300000012);
  r2.zw = float2(8,8) * r1.xy;
  r3.xyz = t_normal.Sample(s_normal_s, r2.zw).xzw;
  r3.xyz = r3.xzy * float3(2,2,2) + float3(-1,-1,-1);
  r2.z = dot(r3.xyz, r3.xyz);
  r2.z = rsqrt(r2.z);
  r4.xyz = t_normal.Sample(s_normal_s, r0.zw).xzw;
  r4.xyz = r4.xzy * float3(2,2,2) + float3(-1,-1,-1);
  r0.z = dot(r4.xyz, r4.xyz);
  r0.z = rsqrt(r0.z);
  r4.xyz = r4.xyz * r0.zzz;
  r0.zw = float2(4,4) * r2.xy;
  r5.xyz = t_normal.Sample(s_normal_s, r0.zw).xzw;
  r5.xyz = r5.xzy * float3(2,2,2) + float3(-1,-1,-1);
  r0.z = dot(r5.xyz, r5.xyz);
  r0.z = rsqrt(r0.z);
  r6.xyz = t_normal.Sample(s_normal_s, r1.zw).xzw;
  r6.xyz = r6.xzy * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r6.xyz, r6.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = r3.xyz * r2.zzz + r4.xyz;
  r3.xyz = r5.xyz * r0.zzz + r3.xyz;
  r3.xyz = r6.xyz * r0.www + r3.xyz;
  r0.z = dot(r3.xyz, r3.xyz);
  r0.z = rsqrt(r0.z);
  r0.zw = r3.xy * r0.zz;
  r1.zw = float2(8,8) * v1.zw;
  r1.z = t_diffuse.SampleLevel(s_diffuse_s, r1.zw, 0).w;
  r1.xy = r1.zz * float2(0.0199999996,0.0199999996) + r1.xy;
  r1.xy = time_in_sec * float2(0.00999999978,0.00999999978) + r1.xy;
  r1.zw = r1.zz * float2(0.00999999978,0.00999999978) + r2.xy;
  r1.zw = time_in_sec * float2(0.00999999978,0.00999999978) + r1.zw;
  r1.xy = float2(0.400000006,0.400000006) * r1.xy;
  r1.x = t_diffuse.Sample(s_diffuse_s, r1.xy).x;
  r1.yz = float2(1.60000002,1.60000002) * r1.zw;
  r1.y = t_diffuse.Sample(s_diffuse_s, r1.yz).x;
  r1.z = r1.x * r1.y;
  r1.z = 3 * r1.z;
  r1.x = r1.y + r1.x;
  r1.x = dot(r1.xx, r1.zz);
  r1.y = t_terrain_refraction.SampleLevel(s_terrain_refraction_s, r0.xy, 0).w;
  r2.xz = r1.yy * r0.zw;
  r2.y = 1;
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  r2.xzw = r2.xyz * r0.zzz;
  r1.zw = r2.xw * r1.yy;
  r0.xy = r1.zw * float2(0.0500000007,0.0500000007) + r0.xy;
  r3.xyzw = t_terrain_refraction.SampleLevel(s_terrain_refraction_s, r0.xy, 0).xyzw;
  r0.w = cmp(0.00100000005 < r3.w);
  if (r0.w != 0) {
    r1.zw = g_texel_offset / screen_size.xy;
    r0.xy = r1.zw + r0.xy;
    r3.xyz = t_terrain_refraction.SampleLevel(s_terrain_refraction_s, r0.xy, 0).xyz;
    r1.y = r3.w;
  }
  if (SI.water_caustics > 0.f) {
    const float2 tile = frac(v4.xz / 4.f);
    r3.xyz *= 1.f + saturate(CausticsBorders(tile, 7, 0.6f) + 0.5f * CausticsBorders(tile, 11, -0.8f)) * SI.water_caustics;
  }
  r0.xyw = -camera_position.xyz + v4.xyz;
  r1.z = dot(r0.xyw, r0.xyw);
  r1.z = rsqrt(r1.z);
  r4.xyz = r1.zzz * r0.xyw;
  r1.w = saturate(4 * r1.y / ((SI.water_clarity > 0.f) ? SI.water_clarity : 1.f));
  r5.xyz = g_deep_water_colour.xyz + -r3.xyz;
  r3.xyz = r1.www * r5.xyz + r3.xyz;
  r1.w = dot(r4.xyz, -r2.xzw);
  r1.w = 1 + -r1.w;
  r3.w = r1.w * r1.w;
  r3.w = r3.w * r3.w;
  r1.w = r3.w * r1.w;
  r1.w = saturate(r1.w * 0.979981303 + 0.0200186726);
  r1.w = r1.w * r1.y;
  r1.w = saturate(0.899999976 * r1.w);
  r5.xyz = r1.www * -r3.xyz + r3.xyz;
  r1.w = cmp(r1.y < g_campaign_water_multiplier_scalar);
  r1.w = r1.w ? r1.y : 0;
  r1.x = r1.x * r1.w;
  r1.w = g_campaign_water_froth_scalar + -r1.w;
  r5.xyz = r1.xxx * r1.www + r5.xyz;
  r1.x = cmp(g_black_border_x_min >= v4.x);
  r6.xy = cmp(v4.xz >= g_black_border_x_max);
  r1.x = (int)r1.x | (int)r6.x;
  r1.w = cmp(g_black_border_z_min >= v4.z);
  r1.x = (int)r1.w | (int)r1.x;
  r1.x = (int)r6.y | (int)r1.x;
  r1.w = cmp(g_specular_probes_count >= 1);
  if (r1.w != 0) {
    r1.w = cmp(g_cloud_shadow_scale != 0.000000);
    if (r1.w != 0) {
      r6.xy = v4.xz / g_cloud_shadow_scale;
      r1.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
      r6.zw = g_cloud_shadow_direction.xy * r1.ww;
      r6.xy = r6.zw * time_in_sec + r6.xy;
      r6.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r6.xy, 0).xyzw;
      r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
      r1.w = 4 * g_cloud_shadow_lerp;
      r1.w = min(1, abs(r1.w));
      r1.w = 1 + -r1.w;
      r7.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
      r7.xyzw = min(float4(1,1,1,1), abs(r7.xyzw));
      r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
      r1.w = r6.x * r7.x + r1.w;
      r1.w = r6.y * r7.y + r1.w;
      r1.w = r6.z * r7.z + r1.w;
      r1.w = r6.w * r7.w + r1.w;
    } else {
      r1.w = 1;
    }
    r1.w = min(1, r1.w);
    r3.w = dot(r4.xyz, r2.xzw);
    r4.w = r3.w + r3.w;
    r6.xyz = r2.xzw * -r4.www + r4.xyz;
    r4.w = dot(r2.xzw, -r4.xyz);
    r7.x = max(0, r4.w);
    r7.y = 0.949999988;
    r7.xy = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r7.xy, 0).xy;
    r4.w = r7.x + r7.y;
    r7.xy = -g_probe_grid_area.xy + v4.xz;
    r7.zw = g_probe_grid_area.zw + -g_probe_grid_area.xy;
    r7.xy = r7.xy / r7.zw;
    r5.w = g_probe_grid_size;
    r7.xy = r7.xy * r5.ww + float2(-0.5,-0.5);
    r7.z = -1 + r5.w;
    r7.xy = max(float2(0,0), r7.xy);
    r7.xy = min(r7.xy, r7.zz);
    r7.zw = floor(r7.xy);
    r7.xy = frac(r7.xy);
    r6.w = r7.w * r5.w + r7.z;
    r8.xyz = t_environment_array.SampleLevel(s_environment_array_s, r6.xyzw, 0.400000095).xyz;
    r9.xyzw = float4(1,0,0,1) + r7.zwzw;
    r9.xw = r9.yw * r5.ww + r9.xz;
    r10.xyz = r6.xyz;
    r10.w = r9.x;
    r11.xyz = t_environment_array.SampleLevel(s_environment_array_s, r10.xyzw, 0.400000095).xyz;
    r9.xyz = r10.xyz;
    r12.xyz = t_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, 0.400000095).xyz;
    r7.zw = float2(1,1) + r7.zw;
    r13.w = r7.w * r5.w + r7.z;
    r13.xyz = r9.xyz;
    r14.xyz = t_environment_array.SampleLevel(s_environment_array_s, r13.xyzw, 0.400000095).xyz;
    r11.xyz = r11.xyz + -r8.xyz;
    r8.xyz = r7.xxx * r11.xyz + r8.xyz;
    r11.xyz = r14.xyz + -r12.xyz;
    r11.xyz = r7.xxx * r11.xyz + r12.xyz;
    r11.xyz = r11.xyz + -r8.xyz;
    r8.xyz = r7.yyy * r11.xyz + r8.xyz;
    r5.w = cmp(g_specular_probe_lerp < 0.999000013);
    if (r5.w != 0) {
      r6.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r6.xyzw, 0.400000095).xyz;
      r11.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r10.xyzw, 0.400000095).xyz;
      r9.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, 0.400000095).xyz;
      r12.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r13.xyzw, 0.400000095).xyz;
      r11.xyz = r11.xyz + -r6.xyz;
      r6.xyz = r7.xxx * r11.xyz + r6.xyz;
      r11.xyz = r12.xyz + -r9.xyz;
      r7.xzw = r7.xxx * r11.xyz + r9.xyz;
      r7.xzw = r7.xzw + -r6.xyz;
      r6.xyz = r7.yyy * r7.xzw + r6.xyz;
      r7.xyz = r8.xyz + -r6.xyz;
      r6.xyz = g_specular_probe_lerp * r7.xyz + r6.xyz;
    }
    r6.xyz = r5.www ? r6.xyz : r8.xyz;
    if (rt_water) r6.xyz = RtReflection(v0.xy, r6.xyz);
    r5.w = cmp(0 < sun_specular);
    r6.w = dot(-sun_direction.xyz, r10.xyz);
    r6.w = max(-1, r6.w);
    r6.w = min(1, r6.w);
    r7.x = 1 + -abs(r6.w);
    r7.x = sqrt(r7.x);
    r7.y = abs(r6.w) * -0.0187292993 + 0.0742610022;
    r7.y = r7.y * abs(r6.w) + -0.212114394;
    r7.y = r7.y * abs(r6.w) + 1.57072878;
    r7.z = r7.y * r7.x;
    r7.z = r7.z * -2 + 3.14159274;
    r6.w = cmp(r6.w < -r6.w);
    r6.w = r6.w ? r7.z : 0;
    r6.w = r7.y * r7.x + r6.w;
    r7.xyz = float3(-0.0043633231,0.0043633231,0.0043633231) + r6.www;
    r8.xyzw = r5.wwww ? float4(16.8311768,18.6947365,349.493164,6.20448399) : float4(1.10699046,1.22955728,1.51181114,1.34230018);
    r7.xyz = r8.xxx * r7.xyz;
    r7.yw = float2(0.707106769,0.707106769) * r7.xy;
    r6.w = abs(r7.y) * abs(r7.y);
    r8.x = r6.w * abs(r7.y);
    r9.x = r8.x * abs(r7.y);
    r7.y = abs(r7.y) * 0.278393 + 1;
    r6.w = r6.w * 0.230388999 + r7.y;
    r6.w = r8.x * 0.000972000009 + r6.w;
    r6.w = r9.x * 0.0781079978 + r6.w;
    r6.w = r6.w * r6.w;
    r6.w = r6.w * r6.w;
    r6.w = 1 / r6.w;
    r6.w = 1 + -r6.w;
    r7.y = cmp(0 < r7.x);
    r7.x = cmp(r7.x < 0);
    r7.x = (int)-r7.y + (int)r7.x;
    r7.x = (int)r7.x;
    r6.w = r7.x * r6.w + 1;
    r6.w = 0.5 * r6.w;
    r7.x = r7.w * r7.w;
    r7.y = r7.x * r7.w;
    r7.w = r7.y * r7.w;
    r7.z = r7.z * 0.196853578 + 1;
    r7.x = r7.x * 0.230388999 + r7.z;
    r7.x = r7.y * 0.000972000009 + r7.x;
    r7.x = r7.w * 0.0781079978 + r7.x;
    r7.x = r7.x * r7.x;
    r7.x = r7.x * r7.x;
    r7.x = 1 / r7.x;
    r7.x = 2 + -r7.x;
    r6.w = r7.x * 0.5 + -r6.w;
    r7.x = r8.z * r8.y;
    r7.y = r7.x * r8.y;
    r9.xyzw = r5.wwww ? float4(80.5193787,0.0784591213,0.00125000055,0.998749971) : float4(0.348304659,0.95105654,0.320000023,0.679999948);
    r5.w = r9.x + r8.w;
    r5.w = r7.x * 0.000972000009 + r5.w;
    r5.w = r7.y * 0.0781079978 + r5.w;
    r5.w = r5.w * r5.w;
    r5.w = r5.w * r5.w;
    r5.w = 1 / r5.w;
    r7.xy = float2(1,2) + -r5.ww;
    r5.w = 1 + -r7.x;
    r5.w = 0.5 * r5.w;
    r5.w = r7.y * 0.5 + -r5.w;
    r5.w = abs(r6.w) / r5.w;
    r5.w = min(1, r5.w);
    r5.w = r5.w * r5.w;
    r5.w = 0.785398185 * r5.w;
    r6.w = saturate(dot(r2.xzw, -sun_direction.xyz));
    r7.x = -1 + r6.w;
    r7.x = r9.y * r7.x + 1;
    r3.w = saturate(r3.w);
    r6.w = r6.w * r9.w + r9.z;
    r6.w = 1 / r6.w;
    r3.w = r3.w * r9.w + r9.z;
    r3.w = 1 / r3.w;
    r3.w = r6.w * r3.w;
    r3.w = 0.25 * r3.w;
    r3.w = min(1, r3.w);
    r3.w = r3.w * r7.x;
    r3.w = r5.w * r3.w;
    r7.xyz = sun_colour.xyz * r3.www;
    r7.xyz = r7.xyz * r1.www;
    r6.xyz = r6.xyz * r4.www + r7.xyz;
  } else {
    r1.w = dot(r4.xyz, r2.xzw);
    r1.w = r1.w + r1.w;
    r7.xyz = r2.xzw * -r1.www + r4.xyz;
    r1.w = dot(g_sun_direction.xz, g_sun_direction.xz);
    r1.w = rsqrt(r1.w);
    r8.xy = g_sun_direction.xz * r1.ww;
    r9.x = dot(r7.xz, r8.xy);
    r8.z = -r8.x;
    r9.z = dot(r7.xz, r8.yz);
    r9.y = r7.y;
    r7.xyz = t_sky.Sample(s_sky_s, r9.xyz).xyz;
    r7.xyz = g_sky_exposure_clamp_threshold_falloff.xxx * r7.xyz;
    r7.xyz = log2(r7.xyz);
    r7.xyz = g_sky_gamma_etc.xxx * r7.xyz;
    r7.xyz = exp2(r7.xyz);
    r8.xyz = float3(1,1,1) + r7.xyz;
    r8.xyz = -g_sky_exposure_clamp_threshold_falloff.zzz + r8.xyz;
    r8.xyz = log2(abs(r8.xyz));
    r8.xyz = g_sky_exposure_clamp_threshold_falloff.www * r8.xyz;
    r8.xyz = exp2(r8.xyz);
    r8.xyz = g_sky_exposure_clamp_threshold_falloff.zzz + r8.xyz;
    r8.xyz = float3(-1,-1,-1) + r8.xyz;
    r9.xyz = cmp(g_sky_exposure_clamp_threshold_falloff.zzz < r7.xyz);
    r9.xyz = r9.xyz ? float3(1,1,1) : 0;
    r8.xyz = r8.xyz + -r7.xyz;
    r7.xyz = r9.xyz * r8.xyz + r7.xyz;
    r6.xyz = min(g_sky_exposure_clamp_threshold_falloff.yyy, r7.xyz);
    if (rt_water) r6.xyz = RtReflection(v0.xy, r6.xyz);
  }
  r1.w = dot(r4.xyz, r2.xzw);
  r3.w = r1.w + r1.w;
  r7.xyz = -r3.www * r2.xzw;
  r8.xyz = r2.xzw * -r3.www + r4.xyz;
  r3.w = dot(r7.xyz, r7.xyz);
  r3.w = rsqrt(r3.w);
  r7.xyz = r7.xyz * r3.www;
  r3.w = saturate(dot(r7.xyz, r8.xyz));
  r4.w = r3.w * -5.55472994 + -6.98316002;
  r3.w = r4.w * r3.w;
  r3.w = exp2(r3.w);
  r3.w = r3.w * 0.979667783 + 0.0203322452;
  r3.w = saturate(r3.w * r1.y);
  r5.xyz = float3(1,1,1) * r5.xyz;
  r5.xyz = r1.xxx ? float3(0,0,0) : r5.xyz;
  r1.x = cmp(g_cloud_shadow_scale != 0.000000);
  if (r1.x != 0) {
    r7.xy = v4.xz / g_cloud_shadow_scale;
    r1.x = g_cloud_shadow_speed / g_cloud_shadow_scale;
    r7.zw = g_cloud_shadow_direction.xy * r1.xx;
    r7.xy = r7.zw * time_in_sec + r7.xy;
    r7.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r7.xy, 0).xyzw;
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r1.x = 4 * g_cloud_shadow_lerp;
    r1.x = min(1, abs(r1.x));
    r1.x = 1 + -r1.x;
    r9.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
    r9.xyzw = min(float4(1,1,1,1), abs(r9.xyzw));
    r9.xyzw = float4(1,1,1,1) + -r9.xyzw;
    r1.x = r7.x * r9.x + r1.x;
    r1.x = r7.y * r9.y + r1.x;
    r1.x = r7.z * r9.z + r1.x;
    r1.x = r7.w * r9.w + r1.x;
  } else {
    r1.x = 1;
  }
  if (g_use_spherical_harmonics != 0) {
    t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r7.xy = uiDest.xy;
    uiDest.xy = (uint2)r7.xy / int2(3,3);
    r7.xy = uiDest.xy;
    r7.xy = (uint2)r7.xy;
    r7.xy = float2(0.5,0.5) * r7.xy;
    r7.xy = trunc(r7.xy);
    r9.xyzw = r2.xwzw * r2.wzxw;
    r4.w = r9.w * 3 + -1;
    r5.w = r2.z * r2.z;
    r5.w = r2.x * r2.x + -r5.w;
    r6.w = round(g_spherical_harmonic_terms);
    r6.w = (int)r6.w;
    r10.zw = float2(0,0);
    r11.xyz = float3(0,0,0);
    r7.z = 0;
    while (true) {
      r7.w = cmp((int)r7.z >= (int)r6.w);
      if (r7.w != 0) break;
      r7.w = (uint)r7.z % 3;
      r7.w = (int)r7.w;
      r7.w = r7.x * 3 + r7.w;
      r9.w = (int)icb[r7.z+0].y;
      r9.w = r7.y * 3 + r9.w;
      r10.x = (int)r7.w;
      r10.y = (int)r9.w;
      r12.xyz = t_world_spherical_harmonics.Load(r10.xyz).xyz;
      r13.xyzw = cmp((int4)r7.zzzz == int4(0,1,2,3));
      r14.xyzw = cmp((int4)r7.zzzz == int4(4,5,6,7));
      r7.w = cmp((int)r7.z == 8);
      r13.xyzw = r13.xyzw ? float4(1,1,1,1) : 0;
      r14.xyzw = r14.xyzw ? float4(1,1,1,1) : 0;
      r7.w = r7.w ? 1.000000 : 0;
      r9.w = r13.y * r2.x + r13.x;
      r9.w = r13.z * r2.z + r9.w;
      r9.w = r13.w * r2.w + r9.w;
      r9.w = r14.x * r9.x + r9.w;
      r9.w = r14.y * r9.y + r9.w;
      r9.w = r14.z * r9.z + r9.w;
      r9.w = r14.w * r4.w + r9.w;
      r7.w = r7.w * r5.w + r9.w;
      r11.xyz = r12.xyz * r7.www + r11.xyz;
      r7.z = (int)r7.z + 1;
    }
    r7.xyz = max(float3(0,0,0), r11.xyz);
    r7.xyz = g_ambient_fudge_factor * r7.xyz;
  } else {
    r9.xy = cmp(r2.xw < float2(0,0));
    r9.xzw = r9.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
    r10.xyz = r9.yyy ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
    r11.xyz = r2.xzw * r2.xzw;
    r12.xyz = ambient_cube_tb[0].xyz * r11.yyy;
    r9.xyz = r11.xxx * r9.xzw + r12.xyz;
    r9.xyz = r11.zzz * r10.xyz + r9.xyz;
    r7.xyz = g_ambient_fudge_factor * r9.xyz;
  }
  r7.xyz = r7.xyz * r5.xyz;
  r4.x = dot(r2.xzw, -r4.xyz);
  r4.xy = max(float2(0,-1), r4.xx);
  r4.yw = min(float2(1,1), r4.xy);
  r5.w = r4.y * -5.55472994 + -6.98316002;
  r4.y = r5.w * r4.y;
  r4.y = exp2(r4.y);
  r4.y = r4.y * 0.909999967 + 0.0399999991;
  r4.z = 0.949999988;
  r4.xz = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r4.xz, 0).xy;
  r4.x = r4.y * r4.x + r4.z;
  r4.yz = -g_probe_grid_area.xy + v4.xz;
  r9.xy = g_probe_grid_area.zw + -g_probe_grid_area.xy;
  r4.yz = r4.yz / r9.xy;
  r5.w = g_probe_grid_size;
  r4.yz = r4.yz * r5.ww + float2(-0.5,-0.5);
  r6.w = -1 + r5.w;
  r4.yz = max(float2(0,0), r4.yz);
  r4.yz = min(r4.yz, r6.ww);
  r9.xy = floor(r4.yz);
  r4.yz = frac(r4.yz);
  r8.w = r9.y * r5.w + r9.x;
  r10.xyz = t_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, 0.400000095).xyz;
  r11.xyzw = float4(1,0,0,1) + r9.xyxy;
  r11.xw = r11.yw * r5.ww + r11.xz;
  r12.xyz = r8.xyz;
  r12.w = r11.x;
  r13.xyz = t_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, 0.400000095).xyz;
  r11.xyz = r12.xyz;
  r14.xyz = t_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, 0.400000095).xyz;
  r9.xy = float2(1,1) + r9.xy;
  r9.w = r9.y * r5.w + r9.x;
  r9.xyz = r11.xyz;
  r15.xyz = t_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, 0.400000095).xyz;
  r13.xyz = r13.xyz + -r10.xyz;
  r10.xyz = r4.yyy * r13.xyz + r10.xyz;
  r13.xyz = r15.xyz + -r14.xyz;
  r13.xyz = r4.yyy * r13.xyz + r14.xyz;
  r13.xyz = r13.xyz + -r10.xyz;
  r10.xyz = r4.zzz * r13.xyz + r10.xyz;
  r5.w = cmp(g_specular_probe_lerp < 0.999000013);
  if (r5.w != 0) {
    r8.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r8.xyzw, 0.400000095).xyz;
    r13.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, 0.400000095).xyz;
    r11.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, 0.400000095).xyz;
    r9.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, 0.400000095).xyz;
    r13.xyz = r13.xyz + -r8.xyz;
    r8.xyz = r4.yyy * r13.xyz + r8.xyz;
    r9.xyz = r9.xyz + -r11.xyz;
    r9.xyz = r4.yyy * r9.xyz + r11.xyz;
    r9.xyz = r9.xyz + -r8.xyz;
    r8.xyz = r4.zzz * r9.xyz + r8.xyz;
    r9.xyz = r10.xyz + -r8.xyz;
    r8.xyz = g_specular_probe_lerp * r9.xyz + r8.xyz;
  }
  r8.xyz = r5.www ? r8.xyz : r10.xyz;
  r4.xyz = r8.xyz * r4.xxx;
  r4.xyz = r7.xyz * float3(0.959999979,0.959999979,0.959999979) + r4.xyz;
  r5.w = dot(r2.xzw, -sun_direction.xyz);
  r6.w = saturate(r5.w);
  r7.x = cmp(0 < sun_specular);
  r7.y = dot(-sun_direction.xyz, r12.xyz);
  r7.y = max(-1, r7.y);
  r7.y = min(1, r7.y);
  r7.z = 1 + -abs(r7.y);
  r7.z = sqrt(r7.z);
  r7.w = abs(r7.y) * -0.0187292993 + 0.0742610022;
  r7.w = r7.w * abs(r7.y) + -0.212114394;
  r7.w = r7.w * abs(r7.y) + 1.57072878;
  r8.x = r7.w * r7.z;
  r8.x = r8.x * -2 + 3.14159274;
  r7.y = cmp(r7.y < -r7.y);
  r7.y = r7.y ? r8.x : 0;
  r7.y = r7.w * r7.z + r7.y;
  r7.yzw = float3(-0.0043633231,0.0043633231,0.0043633231) + r7.yyy;
  r8.xyzw = r7.xxxx ? float4(16.8311768,18.6947365,349.493164,6.20448399) : float4(1.10699046,1.22955728,1.51181114,1.34230018);
  r7.yzw = r8.xxx * r7.yzw;
  r9.xy = float2(0.707106769,0.707106769) * r7.yz;
  r7.z = abs(r9.x) * abs(r9.x);
  r8.x = r7.z * abs(r9.x);
  r9.z = r8.x * abs(r9.x);
  r9.x = abs(r9.x) * 0.278393 + 1;
  r7.z = r7.z * 0.230388999 + r9.x;
  r7.z = r8.x * 0.000972000009 + r7.z;
  r7.z = r9.z * 0.0781079978 + r7.z;
  r7.z = r7.z * r7.z;
  r7.z = r7.z * r7.z;
  r7.z = 1 / r7.z;
  r7.z = 1 + -r7.z;
  r8.x = cmp(0 < r7.y);
  r7.y = cmp(r7.y < 0);
  r7.y = (int)-r8.x + (int)r7.y;
  r7.y = (int)r7.y;
  r7.y = r7.y * r7.z + 1;
  r7.y = 0.5 * r7.y;
  r7.z = r9.y * r9.y;
  r8.x = r7.z * r9.y;
  r9.x = r8.x * r9.y;
  r7.w = r7.w * 0.196853578 + 1;
  r7.z = r7.z * 0.230388999 + r7.w;
  r7.z = r8.x * 0.000972000009 + r7.z;
  r7.z = r9.x * 0.0781079978 + r7.z;
  r7.z = r7.z * r7.z;
  r7.z = r7.z * r7.z;
  r7.z = 1 / r7.z;
  r7.z = 2 + -r7.z;
  r7.y = r7.z * 0.5 + -r7.y;
  r7.z = r8.z * r8.y;
  r7.w = r7.z * r8.y;
  r9.xyzw = r7.xxxx ? float4(80.5193787,0.0784591213,0.00125000055,0.998749971) : float4(0.348304659,0.95105654,0.320000023,0.679999948);
  r7.x = r9.x + r8.w;
  r7.x = r7.z * 0.000972000009 + r7.x;
  r7.x = r7.w * 0.0781079978 + r7.x;
  r7.x = r7.x * r7.x;
  r7.x = r7.x * r7.x;
  r7.x = 1 / r7.x;
  r7.xz = float2(1,2) + -r7.xx;
  r7.x = 1 + -r7.x;
  r7.x = 0.5 * r7.x;
  r7.x = r7.z * 0.5 + -r7.x;
  r7.x = abs(r7.y) / r7.x;
  r7.x = min(1, r7.x);
  r7.x = r7.x * r7.x;
  r7.x = 0.785398185 * r7.x;
  r7.y = -1 + r6.w;
  r7.y = r9.y * r7.y + 1;
  r1.w = saturate(r1.w);
  r7.z = r6.w * r9.w + r9.z;
  r7.z = 1 / r7.z;
  r1.w = r1.w * r9.w + r9.z;
  r1.w = 1 / r1.w;
  r1.w = r7.z * r1.w;
  r1.w = 0.25 * r1.w;
  r1.w = min(1, r1.w);
  r1.w = r1.w * r7.y;
  r0.xyw = -r0.xyw * r1.zzz + -sun_direction.xyz;
  r1.z = dot(r0.xyw, r0.xyw);
  r1.z = rsqrt(r1.z);
  r0.xyw = r1.zzz * r0.xyw;
  r0.x = saturate(dot(r0.xyw, -sun_direction.xyz));
  r0.y = r0.x * -5.55472994 + -6.98316002;
  r0.x = r0.y * r0.x;
  r0.x = exp2(r0.x);
  r0.x = r0.x * 0.959999979 + 0.0399999991;
  r0.y = r7.x * r1.w;
  r0.x = r0.y * r0.x;
  r0.xyw = sun_colour.xyz * r0.xxx;
  r7.xyz = float3(-1,-1,-1) + g_water_reflection_tint.xyz;
  r7.xyz = g_water_reflection_tint.www * r7.xyz + float3(1,1,1);
  r0.xyw = r7.xyz * r0.xyw;
  r1.z = r6.w * -5.55472994 + -6.98316002;
  r1.z = r1.z * r6.w;
  r1.z = exp2(r1.z);
  r1.z = r1.z * 0.959999979 + 0.0399999991;
  r1.z = 1 + -r1.z;
  r1.w = max(-1, r5.w);
  r1.xw = min(float2(1,1), r1.xw);
  r5.w = 1 + -abs(r1.w);
  r5.w = sqrt(r5.w);
  r7.x = abs(r1.w) * -0.0187292993 + 0.0742610022;
  r7.x = r7.x * abs(r1.w) + -0.212114394;
  r7.x = r7.x * abs(r1.w) + 1.57072878;
  r7.y = r7.x * r5.w;
  r7.y = r7.y * -2 + 3.14159274;
  r7.z = cmp(r1.w < -r1.w);
  r7.y = r7.z ? r7.y : 0;
  r5.w = r7.x * r5.w + r7.y;
  r7.x = 1 + -abs(r4.w);
  r7.x = sqrt(r7.x);
  r7.y = abs(r4.w) * -0.0187292993 + 0.0742610022;
  r7.y = r7.y * abs(r4.w) + -0.212114394;
  r7.y = r7.y * abs(r4.w) + 1.57072878;
  r7.z = r7.y * r7.x;
  r7.z = r7.z * -2 + 3.14159274;
  r4.w = cmp(r4.w < -r4.w);
  r4.w = r4.w ? r7.z : 0;
  r4.w = r7.y * r7.x + r4.w;
  r7.x = max(r4.w, r5.w);
  r7.y = min(r4.w, r5.w);
  r4.w = r5.w + -r4.w;
  r4.w = cos(r4.w);
  r4.w = max(0, r4.w);
  r4.w = 0.0169837158 * r4.w;
  r5.w = sin(r7.x);
  r4.w = r5.w * r4.w;
  sincos(r7.y, r7.x, r8.x);
  r5.w = r7.x / r8.x;
  r4.w = r4.w * r5.w + 0.99470818;
  r1.w = max(0, r1.w);
  r1.w = r4.w * r1.w;
  r7.xyz = sun_colour.xyz * r1.zzz;
  r5.xyz = r7.xyz * r5.xyz;
  r1.z = cmp(0.5 < g_camera_temp0.x);
  r1.z = r1.z ? r6.w : r1.w;
  r5.xyz = r5.xyz * r1.zzz;
  r5.xyz = float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) * r5.xyz;
  r0.xyw = r0.xyw * g_water_reflection_strength + r5.xyz;
  r0.xyw = r1.xxx * r0.xyw + r4.xyz;
  r1.x = rsqrt(r1.y);
  r1.x = 1 / r1.x;
  r0.xyw = r0.xyw + -r3.xyz;
  r0.xyw = r1.xxx * r0.xyw + r3.xyz;
  r1.xyz = r6.xyz * r3.www;
  o0.xyz = r1.xyz * g_water_reflection_strength + r0.xyw;
  r0.x = min(abs(r2.w), abs(r2.x));
  r0.y = max(abs(r2.w), abs(r2.x));
  r0.y = 1 / r0.y;
  r0.x = r0.x * r0.y;
  r0.y = r0.x * r0.x;
  r0.w = r0.y * 0.0208350997 + -0.0851330012;
  r0.w = r0.y * r0.w + 0.180141002;
  r0.w = r0.y * r0.w + -0.330299497;
  r0.y = r0.y * r0.w + 0.999866009;
  r0.w = r0.x * r0.y;
  r1.x = cmp(abs(r2.x) < abs(r2.w));
  r0.w = r0.w * -2 + 1.57079637;
  r0.w = r1.x ? r0.w : 0;
  r0.x = r0.x * r0.y + r0.w;
  r0.y = cmp(r2.x < -r2.x);
  r0.y = r0.y ? -3.141593 : 0;
  r0.x = r0.x + r0.y;
  r0.y = min(r2.w, r2.x);
  r0.w = max(r2.w, r2.x);
  r0.y = cmp(r0.y < -r0.y);
  r0.w = cmp(r0.w >= -r0.w);
  r0.y = r0.w ? r0.y : 0;
  r0.x = r0.y ? -r0.x : r0.x;
  r0.y = -r2.y * r0.z + 1;
  r0.y = sqrt(r0.y);
  r0.z = r0.z * -0.0187292993 + 0.0742610022;
  r0.z = r0.z * r2.z + -0.212114394;
  r0.z = r0.z * r2.z + 1.57072878;
  r0.y = -r0.z * r0.y + 3.14159274;
  r0.x = 3.14159274 + r0.x;
  o1.xy = float2(0.318309873,0.159154937) * r0.yx;
  o0.w = 1;
  o1.zw = float2(0,0);
  o2.x = r3.w;
  o2.yzw = float3(0.0500000119,1,0.250980407);
  o3.x = 0;
  o4.x = 1;
  return;
}