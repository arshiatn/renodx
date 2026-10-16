// particle_screen_splat.hlsl ps30_volumetric_particle_splat: the same as screen splats.
// HDR: x the draw's VFX Brightness (SI.vfx_glow_draw: Fire & Glow, Big Fires or Campfires, picked by the addon
// from the draw's effect, vfx_effects.hpp). Recompiled against the original: same signature and constant reads (the
// decompiler's int / float casts around the spherical harmonics loop).
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

cbuffer colorimetry_VS_PS : register(b2)
{
  float g_brightness : packoffset(c0) = {1.20000005};
  float g_gamma_output : packoffset(c0.y) = {2.20000005};
  float g_inv_gamma_output : packoffset(c0.z) = {0.454545468};
  int g_hdr_display : packoffset(c0.w) = {0};
  float g_hdr_peak_nits : packoffset(c1);
  float g_hdr_scene_nits : packoffset(c1.y);
  float g_hdr_ui_nits : packoffset(c1.z);
}

cbuffer shroud_PS : register(b3)
{
  float g_shroud_enabled : packoffset(c0) = {0};
  float g_campaign_autumn_timer : packoffset(c0.y) = {0};
  float4 g_campaign_world_bounds : packoffset(c1) = {0,0,1,1};
  float4 g_campaign_playable_area_bounds : packoffset(c2) = {0,0,1,1};
  float g_snow_height : packoffset(c3) = {1.39999998};
  float g_rock_slope_scale : packoffset(c3.y) = {0.400000006};
  float g_rock_height_min : packoffset(c3.z) = {0.400000006};
  float g_rock_height_max : packoffset(c3.w) = {1.39999998};
}

cbuffer lighting_VS_PS : register(b4)
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

cbuffer particle_PS : register(b5)
{
  float3 g_camera_aligned_x_axis_PS : packoffset(c0);
  float3 g_camera_aligned_y_axis_PS : packoffset(c1);
  float3 g_camera_aligned_z_axis_PS : packoffset(c2);
  float4 g_viewport_offset_scale : packoffset(c3) = {0,0,1,1};
  float g_readable_depth : packoffset(c4);
  float2 g_render_target_size : packoffset(c4.y);
}

cbuffer scripted_effect_info_constants_PS : register(b6)
{
  float g_shroud_alpha_min_value : packoffset(c0);
  float g_distance_based_distortion : packoffset(c0.y);
  float g_distortion_divider : packoffset(c0.z);
  float g_soft_edge_factor : packoffset(c0.w);
}

SamplerState s_shroud_s : register(s0);
SamplerState s_texture_s : register(s1);
SamplerState s_normalmap_texture_s : register(s2);
Texture2D<float4> s_texture : register(t0);
Texture2D<float4> s_shroud : register(t1);
Texture2D<float4> s_normalmap_texture : register(t2);
Texture2D<float4> t_world_spherical_harmonics : register(t3);
Texture2D<float4> t_prev_world_spherical_harmonics : register(t4);
Texture2D<float4> gbuffer_readable_depth_texture : register(t5);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  out float4 o0 : SV_Target0)
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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = s_texture.Sample(s_texture_s, v2.xy).xyzw;
  r1.xyzw = s_texture.Sample(s_texture_s, v3.xy).xyzw;
  r1.xyzw = r1.xyzw + -r0.xyzw;
  r0.xyzw = v4.wwww * r1.xyzw + r0.xyzw;
  r0.xyzw = v1.xyzw * r0.xyzw;
  r1.x = cmp(0 != g_shroud_enabled);
  if (r1.x != 0) {
    r1.xy = -g_campaign_world_bounds.xy + v6.xz;
    r1.zw = g_campaign_world_bounds.zw + -g_campaign_world_bounds.xy;
    r1.xy = r1.xy / r1.zw;
    r1.x = s_shroud.SampleLevel(s_shroud_s, r1.xy, r1.y).x;
    r1.x = saturate(-0.5 + r1.x);
    r1.x = r1.x + r1.x;
  } else {
    r1.x = 1;
  }
  r1.x = max(g_shroud_alpha_min_value, r1.x);
  r1.x = min(1, r1.x);
  r0.w = saturate(v5.z * r0.w);
  r2.w = r0.w * r1.x;
  r0.w = r0.w * r1.x + -0.000980392215;
  r0.w = cmp(r0.w < 0);
  if (r0.w != 0) discard;
  r0.w = cmp(g_readable_depth != 0.000000);
  if (r0.w != 0) {
    r1.xy = g_render_target_dimensions.zw * v0.xy;
    r1.zw = float2(-1,-1) + screen_size.xy;
    r1.zw = r1.xy * r1.zw;
    r3.xy = (int2)r1.zw;
    r3.zw = float2(0,0);
    r3.z = gbuffer_readable_depth_texture.Load(r3.xyz).x;
    r3.xy = r1.xy * float2(2,-2) + float2(-1,1);
    r3.w = 1;
    r0.w = dot(r3.xyzw, inv_projection._m02_m12_m22_m32);
    r1.x = dot(r3.xyzw, inv_projection._m03_m13_m23_m33);
    r0.w = r0.w / r1.x;
    r1.x = cmp(r0.w < v4.z);
    if (r1.x != 0) discard;
    r1.x = 0.5 * g_soft_edge_factor;
    r0.w = -v4.z + r0.w;
    r0.w = saturate(r0.w / r1.x);
    r2.w = r2.w * r0.w;
  }
  r1.xyz = s_normalmap_texture.Sample(s_normalmap_texture_s, v2.zw).xzw;
  r3.xyz = s_normalmap_texture.Sample(s_normalmap_texture_s, v3.zw).xzw;
  r3.xyz = r3.xzy + -r1.xzy;
  r1.xyz = v4.www * r3.xyz + r1.xzy;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  sincos(-v5.y, r3.x, r4.x);
  r5.x = -r3.x;
  r5.y = r4.x;
  r5.z = r3.x;
  r0.w = dot(r1.xy, r5.yz);
  r1.x = dot(r1.xy, r5.xy);
  r1.xyw = g_camera_aligned_y_axis_PS.xyz * r1.xxx;
  r1.xyw = r0.www * g_camera_aligned_x_axis_PS.xyz + r1.xyw;
  r1.xyz = -r1.zzz * g_camera_aligned_z_axis_PS.xyz + r1.xyw;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r3.xyzw = float4(1.90384671e-005,2.85577016e-006,2.85577016e-006,2.85577016e-006) * sun_colour.xxyz;
  r0.w = dot(-sun_direction.xyz, r1.xyz);
  r0.w = r0.w * 0.5 + 0.5;
  r0.w = r0.w * r0.w;
  r0.w = r3.x * r0.w;
  r1.w = max(0, v5.w);
  r1.w = r2.w * r1.w;
  r0.w = r1.w * r0.w;
  r3.xyz = r0.www * float3(0.850000024,0.850000024,0.850000024) + r3.yzw;
  if (g_use_spherical_harmonics != 0) {
    r4.xy = -g_probe_grid_area.xy + v6.xz;
    r4.zw = g_probe_grid_area.zw + -g_probe_grid_area.xy;
    r4.xy = r4.xy / r4.zw;
    r0.w = g_probe_grid_size;
    r4.xy = r4.xy * r0.ww + float2(-0.5,-0.5);
    r0.w = -1 + r0.w;
    r4.xy = max(float2(0,0), r4.xy);
    r4.xy = min(r4.xy, r0.ww);
    r4.zw = floor(r4.xy);
    r4.xy = frac(r4.xy);
    r4.zw = (int2)r4.zw;
    r5.xy = (int2)r4.zw * int2(3,3);
    r0.w = mad(g_probe_grid_size, 3, -3);
    r5.xy = min((uint2)r5.xy, (uint2)r0.ww);
    r4.zw = mad((int2)r4.zw, int2(3,3), int2(3,3));
    r4.zw = min((uint2)r4.zw, (uint2)r0.ww);
    r6.xyzw = r1.xzyz * r1.zyxz;
    r0.w = r6.w * 3 + -1;
    r1.w = r1.y * r1.y;
    r1.w = r1.x * r1.x + -r1.w;
    r3.w = round(g_spherical_harmonic_terms);
    r3.w = (int)r3.w;
    r7.w = 0;
    r8.xyz = float3(0,0,0);
    r5.z = 0;
    while (true) {
      r5.w = cmp((int)r5.z >= (int)r3.w);
      if (r5.w != 0) break;
      r5.w = (uint)r5.z % 3;
      r7.y = (int)r5.w + (int)r5.x;
      r7.z = (int)r5.y + (int)icb[r5.z+0].y;
      r9.xyz = t_world_spherical_harmonics.Load(r7.yzw).xyz;
      r7.x = (int)r4.z + (int)r5.w;
      r10.xyz = t_world_spherical_harmonics.Load(r7.xzw).xyz;
      r11.y = (int)r4.w + (int)icb[r5.z+0].y;
      r11.xzw = r7.yww;
      r12.xyz = t_world_spherical_harmonics.Load(r11.xyz).xyz;
      r11.xzw = r7.xww;
      r7.xyz = t_world_spherical_harmonics.Load(r11.xyz).xyz;
      r10.xyz = r10.xyz + -r9.xyz;
      r9.xyz = r4.xxx * r10.xyz + r9.xyz;
      r7.xyz = r7.xyz + -r12.xyz;
      r7.xyz = r4.xxx * r7.xyz + r12.xyz;
      r7.xyz = r7.xyz + -r9.xyz;
      r7.xyz = r4.yyy * r7.xyz + r9.xyz;
      r9.xyzw = cmp((int4)r5.zzzz == int4(0,1,2,3));
      r10.xyzw = cmp((int4)r5.zzzz == int4(4,5,6,7));
      r5.w = cmp((int)r5.z == 8);
      r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
      r10.xyzw = r10.xyzw ? float4(1,1,1,1) : 0;
      r5.w = r5.w ? 1.000000 : 0;
      r6.w = r9.y * r1.x + r9.x;
      r6.w = r9.z * r1.y + r6.w;
      r6.w = r9.w * r1.z + r6.w;
      r6.w = r10.x * r6.x + r6.w;
      r6.w = r10.y * r6.y + r6.w;
      r6.w = r10.z * r6.z + r6.w;
      r6.w = r10.w * r0.w + r6.w;
      r5.w = r5.w * r1.w + r6.w;
      r8.xyz = r7.xyz * r5.www + r8.xyz;
      r5.z = (int)r5.z + 1;
    }
    r7.xyz = max(float3(0,0,0), r8.xyz);
    r5.z = cmp(g_specular_probe_lerp < 0.999000013);
    if (r5.z != 0) {
      r8.w = 0;
      r9.xyz = float3(0,0,0);
      r5.w = 0;
      while (true) {
        r6.w = cmp((int)r5.w >= (int)r3.w);
        if (r6.w != 0) break;
        r6.w = (uint)r5.w % 3;
        r8.y = (int)r5.x + (int)r6.w;
        r8.z = (int)r5.y + (int)icb[r5.w+0].y;
        r10.xyz = t_prev_world_spherical_harmonics.Load(r8.yzw).xyz;
        r8.x = (int)r4.z + (int)r6.w;
        r11.xyz = t_prev_world_spherical_harmonics.Load(r8.xzw).xyz;
        r12.y = (int)r4.w + (int)icb[r5.w+0].y;
        r12.xzw = r8.yww;
        r13.xyz = t_prev_world_spherical_harmonics.Load(r12.xyz).xyz;
        r12.xzw = r8.xww;
        r8.xyz = t_prev_world_spherical_harmonics.Load(r12.xyz).xyz;
        r11.xyz = r11.xyz + -r10.xyz;
        r10.xyz = r4.xxx * r11.xyz + r10.xyz;
        r8.xyz = r8.xyz + -r13.xyz;
        r8.xyz = r4.xxx * r8.xyz + r13.xyz;
        r8.xyz = r8.xyz + -r10.xyz;
        r8.xyz = r4.yyy * r8.xyz + r10.xyz;
        r10.xyzw = cmp((int4)r5.wwww == int4(0,1,2,3));
        r11.xyzw = cmp((int4)r5.wwww == int4(4,5,6,7));
        r6.w = cmp((int)r5.w == 8);
        r10.xyzw = r10.xyzw ? float4(1,1,1,1) : 0;
        r11.xyzw = r11.xyzw ? float4(1,1,1,1) : 0;
        r6.w = r6.w ? 1.000000 : 0;
        r7.w = r10.y * r1.x + r10.x;
        r7.w = r10.z * r1.y + r7.w;
        r7.w = r10.w * r1.z + r7.w;
        r7.w = r11.x * r6.x + r7.w;
        r7.w = r11.y * r6.y + r7.w;
        r7.w = r11.z * r6.z + r7.w;
        r7.w = r11.w * r0.w + r7.w;
        r6.w = r6.w * r1.w + r7.w;
        r9.xyz = r8.xyz * r6.www + r9.xyz;
        r5.w = (int)r5.w + 1;
      }
      r4.xyz = max(float3(0,0,0), r9.xyz);
      r5.xyw = r7.xyz + -r4.xyz;
      r4.xyz = g_specular_probe_lerp * r5.xyw + r4.xyz;
    }
    r4.xyz = r5.zzz ? r4.xyz : r7.xyz;
    r4.xyz = g_ambient_fudge_factor * r4.xyz;
  } else {
    r5.xyz = cmp(r1.xyz < float3(0,0,0));
    r6.xyz = r5.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
    r5.xyw = r5.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
    r7.xyz = r5.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
    r1.xyz = r1.xyz * r1.xyz;
    r5.xyz = r1.yyy * r5.xyw;
    r1.xyw = r1.xxx * r6.xyz + r5.xyz;
    r1.xyz = r1.zzz * r7.xyz + r1.xyw;
    r4.xyz = g_ambient_fudge_factor * r1.xyz;
  }
  r1.xyz = r4.xyz + r3.xyz;
  r0.w = max(0, -v5.w);
  r1.xyz = r1.xyz + r0.www;
  r0.xyz = saturate(r1.xyz * r0.xyz);
  r0.xyz = log2(r0.xyz);
  r0.xyz = g_inv_gamma_output * r0.xyz;
  r2.xyz = exp2(r0.xyz);
  o0.xyzw = r2.xyzw;
  if (HDR >= 1.f) o0.xyz *= SI.vfx_glow_draw;
  return;
}