// battlefog.hlsl ps40_main_inscattering: the battle fog's light (haze), added to the scene. HDR: x Battle Fog.
// Ray-traced fog shadows (ray_tracing.hpp OnFogDraw): the sun's part of the light x the sun's visibility along the
// pixel's ray (rt_fog.hlsli; nothing bound: 1); the sky's part as it was (RT_FOG_SHADE 0: the two are apart here).
// Torch and Fire Light in Fog (Experimental, RayTracedLightGlow): the local lights' light along the ray (t44, x the row at b9's
// y; nothing bound: 0) scattered as the sun's colour is (the same 1.90384671e-005: point light colours and sun_colour are
// both in the lit scene's units, sunlight00 and pointlight00 multiply them alike), as Attila's battlefog adds it.
#define RT_FOG_SHADE 0.f
#include "../shared.h"
#include "../../twattila/rt_fog.hlsli"

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

cbuffer fog : register(b3)
{
  float g_fog_density_constant : packoffset(c0);
  float g_fog_density_height : packoffset(c0.y);
  float g_fog_height_bottom : packoffset(c0.z);
  float g_fog_height_top : packoffset(c0.w);
  float g_fog_density_mie : packoffset(c1);
  float g_fog_noise : packoffset(c1.y);
  float3 g_fog_colour : packoffset(c2);
  uint g_fog_mode : packoffset(c2.w);
  float g_fog_lerp : packoffset(c3);
  float g_fog_clear_distance : packoffset(c3.y);
  float g_fog_y_multiplier : packoffset(c3.z);
  float g_campaign_density_scaling : packoffset(c3.w);
  float g_height_fog_clear_distance : packoffset(c4);
  float g_clear_distance_fade_out : packoffset(c4.y);
  float g_fog_shadow_strength : packoffset(c4.z);
  float g_fog_blur_offset : packoffset(c4.w);
  float g_fog_blur_strength : packoffset(c5);
  float2 g_force_fog : packoffset(c5.y);
}

SamplerState s_volume_noise_s : register(s0);
SamplerState s_2d_noise_s : register(s1);
Texture2D<float4> t_world_spherical_harmonics : register(t0);
Texture2D<float4> t_prev_world_spherical_harmonics : register(t1);
Texture3D<float4> t_volume_noise : register(t2);
Texture2D<float4> t_2d_noise : register(t3);
Texture2D<float4> gbuffer_colour_texture : register(t4);
Texture2D<float4> gbuffer_readable_depth_texture : register(t5);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 0, 0, 1.000000, 0},
                              { 1, 0, 0, 0},
                              { 2, 0, 1.000000, 0},
                              { 0, 1, 0, 0},
                              { 1, 1, 0, 0},
                              { 2, 1, 0, 0},
                              { 0, 2, 0, 0},
                              { 1, 2, -1.000000, 0},
                              { 2, 2, -1.000000, 0} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = screen_size.zw * v0.xy;
  gbuffer_colour_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r1.xy = (uint2)r0.zw;
  r1.xy = r1.xy * r0.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r0.zw);
  r1.zw = float2(0,0);
  r1.z = gbuffer_readable_depth_texture.Load(r1.xyz).x;
  r0.z = cmp(r1.z >= 1);
  if (r0.z != 0) discard;
  r0.xy = r0.xy * g_render_target_dimensions.xy + -g_viewport_origin.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r1.xy = float2(0.00200000009,0.00200000009) * r0.xz;
  r0.w = t_2d_noise.SampleLevel(s_2d_noise_s, r1.xy, 0).x;
  r0.w = g_force_fog.x * r0.w;
  r1.xyz = -camera_position.xyz + r0.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r2.x = sqrt(r1.w);
  r2.y = min(g_height_fog_clear_distance, g_fog_clear_distance);
  r0.w = r0.w * 0.5 + r2.x;
  r2.z = g_force_fog.y + -g_force_fog.x;
  r0.w = -g_force_fog.x + r0.w;
  r2.z = 1 / r2.z;
  r0.w = saturate(r2.z * r0.w);
  r2.z = r0.w * -2 + 3;
  r0.w = r0.w * r0.w;
  r0.w = r2.z * r0.w;
  r3.xyz = r1.xyz / r2.xxx;
  r2.z = dot(-r3.xyz, sun_direction.xyz);
  r2.w = 9.99999975e-005 + r2.y;
  r2.w = cmp(r2.w < r2.x);
  if (r2.w != 0) {
    r1.w = rsqrt(r1.w);
    r4.xyz = r1.xyz * r1.www;
    r4.xyz = r4.xyz * r2.yyy + camera_position.xyz;
    r5.xyz = -r4.xyz + r0.xyz;
    r1.y = dot(r5.xyz, r5.xyz);
    r1.y = sqrt(r1.y);
    r2.yw = r4.xz * float2(0.00100000005,0.00100000005) + v0.xy;
    r2.yw = r3.xy * float2(0.100000001,0.100000001) + r2.yw;
    r2.yw = g_camera_jitter.xy + r2.yw;
    r2.yw = float2(0.103100002,0.103100002) * r2.yw;
    r2.yw = frac(r2.yw);
    r5.xyz = float3(19.1900005,19.1900005,19.1900005) + r2.wyy;
    r1.w = dot(r2.ywy, r5.xyz);
    r2.yw = r2.yw + r1.ww;
    r1.w = r2.y + r2.w;
    r1.w = r1.w * r2.y;
    r1.w = frac(r1.w);
    r1.w = frac(r1.w);
    r2.y = -5000 + r1.y;
    r2.y = saturate(0.000125000006 * r2.y);
    r2.w = r2.y * -2 + 3;
    r2.y = r2.y * r2.y;
    r2.y = r2.w * r2.y;
    r2.w = 0.25 + -r1.w;
    r1.w = r2.y * r2.w + r1.w;
    r2.yw = float2(0.0625,0.03125) * r1.yy;
    r3.w = cmp(0 < g_fog_noise);
    r5.xy = float2(0.00999999978,-0.00999999978) * time_in_sec;
    r7.xyz = float3(0,0,0);
    r5.zw = float2(0,0);
    r4.w = 0;
    r6.xyzw = float4(0,0,0,0);
    while (true) {
      r7.w = cmp((int)r6.w >= 16);
      if (r7.w != 0) break;
      r7.w = (int)r6.w;
      r7.w = r7.w + r1.w;
      r7.w = r2.y * r7.w + r2.w;
      r7.w = min(r7.w, r1.y);
      r8.xyz = r3.xyz * r7.www + r4.xyz;
      r7.w = cmp(r7.w < 400);
      r7.w = r3.w ? r7.w : 0;
      if (r7.w != 0) {
        r8.xzw = time_in_sec * float3(4.4000001,-4.4000001,4.4000001) + r8.xyz;
        r8.xzw = float3(0.00999999978,0.00999999978,0.00999999978) * r8.xzw;
        r7.w = t_volume_noise.SampleLevel(s_volume_noise_s, r8.xzw, 0).x;
        r7.w = r7.w * 2 + -1;
        r7.w = r7.w * g_fog_noise + 1;
      } else {
        r7.w = 1;
      }
      r4.w = r7.w + r4.w;
      r7.w = 60000 + -r8.y;
      r8.x = r7.w * 0.5 + r8.y;
      r8.xy = max(float2(0,0), r8.xy);
      r8.xyzw = float4(-0.000206276105,-0.000206276105,-0.00120224594,-0.00120224594) * r8.xyxy;
      r8.xz = exp2(r8.xz);
      r8.yw = exp2(r8.yw);
      r9.x = r8.y * r2.y;
      r5.z = r8.y * r2.y + r5.z;
      r8.yz = g_fog_density_mie * r8.wz;
      r8.w = r8.y * r2.y;
      r5.w = r8.y * r2.y + r5.w;
      r8.x = r8.x * r7.w + r5.z;
      r7.w = r8.z * r7.w + r5.w;
      r7.w = 2.20000002e-005 * r7.w;
      r8.xyz = r8.xxx * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r7.www;
      r8.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r8.xyz;
      r8.xyz = exp2(r8.xyz);
      r6.xyz = r8.xyz * r9.xxx + r6.xyz;
      r7.xyz = r8.xyz * r8.www + r7.xyz;
      r6.w = (int)r6.w + 1;
    }
    r1.y = r2.z * r2.z + 1;
    r1.w = 0.5 * r1.y;
    r4.xyz = r6.xyz * r1.www;
    r1.w = -r2.z * 1.51999998 + 1.5776;
    r1.w = log2(r1.w);
    r1.w = 1.5 * r1.w;
    r1.w = exp2(r1.w);
    r1.y = r1.y / r1.w;
    r1.y = 0.000978047145 * r1.y;
    r6.xyz = r7.xyz * r1.yyy;
    r6.xyz = float3(1.99999995e-005,1.99999995e-005,1.99999995e-005) * r6.xyz;
    r4.xyz = r4.xyz * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r6.xyz;
    r1.y = 0.0625 * r4.w;
    r1.w = -g_height_fog_clear_distance + r2.x;
    r1.w = max(0, r1.w);
    r0.xyz = r0.xyz * float3(0.00100000005,0.00100000005,0.00100000005) + r5.xyx;
    r0.x = t_volume_noise.SampleLevel(s_volume_noise_s, r0.xyz, 0).x;
    r0.x = -0.5 + r0.x;
    r0.y = -0.0799999982 * r0.x;
    r0.xz = float2(0,0);
    r0.xyz = r3.xyz + r0.xyz;
    r0.x = dot(r0.xyz, r0.xyz);
    r0.x = rsqrt(r0.x);
    r0.x = r0.y * r0.x;
    r0.y = r0.x * g_height_fog_clear_distance + camera_position.y;
    r0.z = cmp(r0.y >= g_fog_height_top);
    r2.y = cmp(0 < r0.x);
    r0.z = r0.z ? r2.y : 0;
    if (r0.z == 0) {
      r2.yw = -float2(g_fog_height_top, g_fog_height_bottom) + r0.yy;
      r2.yw = r2.yw / -r0.xx;
      r0.z = 10000 + r0.y;
      r0.z = r0.z / -r0.x;
      r3.x = min(r2.y, r2.w);
      r2.y = max(r2.y, r2.w);
      r3.x = max(0, r3.x);
      r2.y = max(0, r2.y);
      r3.y = min(r2.w, r0.z);
      r0.z = max(r2.w, r0.z);
      r2.w = max(0, r3.y);
      r0.z = max(0, r0.z);
      r3.y = g_fog_density_height * r1.y;
      r3.y = 0.0125000002 * r3.y;
      r3.w = max(r3.x, r1.w);
      r3.zw = min(r3.xw, r2.yy);
      r4.w = cmp(r3.z != r3.w);
      r0.y = r3.x * r0.x + r0.y;
      r3.w = r3.w + -r3.x;
      r5.x = r3.z + -r3.x;
      r5.y = g_fog_height_top + -g_fog_height_bottom;
      r5.y = r5.y + r5.y;
      r0.y = r0.y + r0.y;
      r5.z = r5.x * r0.x + r0.y;
      r5.z = -g_fog_height_top * 2 + r5.z;
      r5.x = r5.x * r5.z;
      r5.x = r5.x * r3.y;
      r5.x = r5.x / r5.y;
      r5.x = 1.44269502 * r5.x;
      r5.x = exp2(r5.x);
      r5.z = r3.w * r0.x + r0.y;
      r5.z = -g_fog_height_top * 2 + r5.z;
      r3.w = r5.z * r3.w;
      r3.w = r3.w * r3.y;
      r3.w = r3.w / r5.y;
      r3.w = 1.44269502 * r3.w;
      r3.w = exp2(r3.w);
      r3.w = r5.x + -r3.w;
      r5.x = min(r2.w, r0.z);
      r5.z = max(r3.z, r2.w);
      r5.z = min(r5.z, r0.z);
      r5.w = cmp(r5.x != r5.z);
      r5.z = r5.z + -r2.w;
      r5.z = -r5.z * r3.y;
      r5.z = 1.44269502 * r5.z;
      r5.z = exp2(r5.z);
      r5.z = r5.z * r3.w;
      r3.w = r5.w ? r5.z : r3.w;
      r3.w = r4.w ? r3.w : 0;
      r4.w = max(r2.w, r1.w);
      r0.z = min(r4.w, r0.z);
      r4.w = cmp(r5.x != r0.z);
      r5.z = r0.z + -r2.w;
      r2.w = r5.x + -r2.w;
      r2.w = -r2.w * r3.y;
      r2.w = 1.44269502 * r2.w;
      r2.w = exp2(r2.w);
      r5.x = -r5.z * r3.y;
      r5.x = 1.44269502 * r5.x;
      r5.x = exp2(r5.x);
      r2.w = -r5.x + r2.w;
      r0.z = max(r0.z, r3.x);
      r0.z = min(r0.z, r2.y);
      r2.y = cmp(r3.z != r0.z);
      r0.z = r0.z + -r3.x;
      r0.x = r0.z * r0.x + r0.y;
      r0.x = -g_fog_height_top * 2 + r0.x;
      r0.x = r0.z * r0.x;
      r0.x = r0.x * r3.y;
      r0.x = r0.x / r5.y;
      r0.x = 1.44269502 * r0.x;
      r0.x = exp2(r0.x);
      r0.x = r2.w * r0.x;
      r0.x = r2.y ? r0.x : r2.w;
      r0.x = r3.w + r0.x;
      r0.xyz = r4.www ? r0.xxx : r3.www;
    } else {
      r0.xyz = float3(0,0,0);
    }
    r2.y = 1 / g_clear_distance_fade_out;
    r1.w = saturate(r2.y * r1.w);
    r2.y = r1.w * -2 + 3;
    r1.w = r1.w * r1.w;
    r1.w = r2.y * r1.w;
    r0.xyz = r1.www * r0.xyz;
    r1.w = -g_fog_clear_distance + r2.x;
    r1.w = max(0, r1.w);
    r2.y = cmp(0.00999999978 < g_clear_distance_fade_out);
    r2.w = cmp(0.00999999978 < r1.w);
    r2.y = r2.w ? r2.y : 0;
    r2.w = min(g_clear_distance_fade_out, r1.w);
    r3.x = r2.w * r2.w;
    r3.y = g_clear_distance_fade_out + g_clear_distance_fade_out;
    r3.x = r3.x / r3.y;
    r2.w = -r2.w + r1.w;
    r2.w = r3.x + r2.w;
    r2.w = r2.w / r1.w;
    r2.y = r2.y ? r2.w : 1;
    r1.y = g_fog_density_constant * r1.y;
    r1.y = r1.y * r2.y;
    r1.y = r1.y * r1.w;
    r1.y = 0.0180336889 * r1.y;
    r1.w = exp2(r1.y);
    r1.y = exp2(-r1.y);
    r2.y = r1.w + r1.y;
    r1.y = r1.w + -r1.y;
    r1.y = 0.5 * r1.y;
    r1.y = saturate(r2.y * 0.5 + -r1.y);
    r1.y = 1 + -r1.y;
    r0.xyz = max(r1.yyy, r0.xyz);
  } else {
    r0.xyz = float3(0,0,0);
    r4.xyz = float3(0,0,0);
  }
  r0.xyz = max(r0.xyz, r0.www);
  r0.w = r2.z * r2.z + 1;
  r0.w = 0.5 * r0.w;
  r2.yzw = r0.xyz * r0.www + r4.xyz;
  r3.xyz = r0.xyz * r0.www + -r2.yzw;
  r0.xyz = r0.xyz * r3.xyz + r2.yzw;
  r0.xyz = g_fog_colour.xyz * r0.xyz;
  if (g_use_spherical_harmonics != 0) {
    r0.w = 1 / r2.x;
    r0.w = min(1, r0.w);
    r1.xy = r0.ww * r1.xz + camera_position.xz;
    r1.xy = -g_probe_grid_area.xy + r1.xy;
    r1.zw = g_probe_grid_area.zw + -g_probe_grid_area.xy;
    r1.xy = r1.xy / r1.zw;
    r0.w = g_probe_grid_size;
    r1.xy = r1.xy * r0.ww + float2(-0.5,-0.5);
    r0.w = -1 + r0.w;
    r1.xy = max(float2(0,0), r1.xy);
    r1.xy = min(r1.xy, r0.ww);
    r1.zw = floor(r1.xy);
    r1.xy = frac(r1.xy);
    r1.zw = (int2)r1.zw;
    r2.xy = (int2)r1.zw * int2(3,3);
    r0.w = mad(g_probe_grid_size, 3, -3);
    r2.xy = min((uint2)r2.xy, (uint2)r0.ww);
    r1.zw = mad((int2)r1.zw, int2(3,3), int2(3,3));
    r1.zw = min((uint2)r1.zw, (uint2)r0.ww);
    r0.w = round(g_spherical_harmonic_terms);
    r0.w = (int)r0.w;
    r3.w = 0;
    r4.xyz = float3(0,0,0);
    r2.z = 0;
    while (true) {
      r2.w = cmp((int)r2.z >= (int)r0.w);
      if (r2.w != 0) break;
      r2.w = (uint)r2.z % 3;
      r3.y = (int)r2.w + (int)r2.x;
      r3.z = (int)r2.y + (int)icb[r2.z+0].y;
      r5.xyz = t_world_spherical_harmonics.Load(r3.yzw).xyz;
      r3.x = (int)r1.z + (int)r2.w;
      r6.xyz = t_world_spherical_harmonics.Load(r3.xzw).xyz;
      r7.y = (int)r1.w + (int)icb[r2.z+0].y;
      r7.xzw = r3.yww;
      r8.xyz = t_world_spherical_harmonics.Load(r7.xyz).xyz;
      r7.xzw = r3.xww;
      r3.xyz = t_world_spherical_harmonics.Load(r7.xyz).xyz;
      r6.xyz = r6.xyz + -r5.xyz;
      r5.xyz = r1.xxx * r6.xyz + r5.xyz;
      r3.xyz = r3.xyz + -r8.xyz;
      r3.xyz = r1.xxx * r3.xyz + r8.xyz;
      r3.xyz = r3.xyz + -r5.xyz;
      r3.xyz = r1.yyy * r3.xyz + r5.xyz;
      r4.xyz = r3.xyz * icb[r2.z+0].zzz + r4.xyz;
      r2.z = (int)r2.z + 1;
    }
    r3.xyz = max(float3(0,0,0), r4.xyz);
    r2.z = cmp(g_specular_probe_lerp < 0.999000013);
    if (r2.z != 0) {
      r4.w = 0;
      r5.xyz = float3(0,0,0);
      r2.w = 0;
      while (true) {
        r3.w = cmp((int)r2.w >= (int)r0.w);
        if (r3.w != 0) break;
        r3.w = (uint)r2.w % 3;
        r4.y = (int)r2.x + (int)r3.w;
        r4.z = (int)r2.y + (int)icb[r2.w+0].y;
        r6.xyz = t_prev_world_spherical_harmonics.Load(r4.yzw).xyz;
        r4.x = (int)r1.z + (int)r3.w;
        r7.xyz = t_prev_world_spherical_harmonics.Load(r4.xzw).xyz;
        r8.y = (int)r1.w + (int)icb[r2.w+0].y;
        r8.xzw = r4.yww;
        r9.xyz = t_prev_world_spherical_harmonics.Load(r8.xyz).xyz;
        r8.xzw = r4.xww;
        r4.xyz = t_prev_world_spherical_harmonics.Load(r8.xyz).xyz;
        r7.xyz = r7.xyz + -r6.xyz;
        r6.xyz = r1.xxx * r7.xyz + r6.xyz;
        r4.xyz = r4.xyz + -r9.xyz;
        r4.xyz = r1.xxx * r4.xyz + r9.xyz;
        r4.xyz = r4.xyz + -r6.xyz;
        r4.xyz = r1.yyy * r4.xyz + r6.xyz;
        r5.xyz = r4.xyz * icb[r2.w+0].zzz + r5.xyz;
        r2.w = (int)r2.w + 1;
      }
      r1.xyz = max(float3(0,0,0), r5.xyz);
      r2.xyw = r3.xyz + -r1.xyz;
      r1.xyz = g_specular_probe_lerp * r2.xyw + r1.xyz;
    }
    r1.xyz = r2.zzz ? r1.xyz : r3.xyz;
    r1.xyz = g_ambient_fudge_factor * r1.xyz;
  } else {
    r1.xyz = g_ambient_fudge_factor * ambient_cube_tb[0].xyz;
  }
  r1.xyz = sun_colour.xyz * float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) * RtFogSun(v0.xy) + r1.xyz;
  r1.xyz += RtFogLights(v0.xy) * 1.90384671e-005f;
  r0.xyz = r1.xyz * r0.xyz;
  o0.xyz = g_fog_lerp * r0.xyz * ((HDR >= 1.f) ? SI.battle_fog : 1.f);
  o0.w = g_fog_lerp;
  return;
}