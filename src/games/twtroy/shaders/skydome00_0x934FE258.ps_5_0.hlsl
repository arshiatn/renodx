// ---- Created with 3Dmigoto v1.3.16 on Mon Oct 26 09:37:07 2026
// troy_battle_skydome.xml (ps_main_deferred_alpha): the battle sky. Sky Debanding as Pharaoh's (its
// sky_deband.hlsli: the sky texture filtered and dithered right after its sample).
#include "../shared.h"

cbuffer camera : register(b0)
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
  float g_overlay_palette_alpha : packoffset(c47.z);
  float g_debug_tonemapping : packoffset(c47.w);
  float4 g_blood_remap : packoffset(c48);
}

cbuffer environment_PS : register(b1)
{
  bool g_apply_cubemap_interpolation : packoffset(c0);
  float g_skydome_lerp : packoffset(c0.y);
  float4 g_skydome_params : packoffset(c1);
  float4 g_skydome_shading : packoffset(c2);
  float g_environment_weights[8] : packoffset(c3);
  float g_environment_indices[8] : packoffset(c11);
  float g_environment_count : packoffset(c18.y);
  float g_environment_scale : packoffset(c18.z);
  float g_porthole_env_scale : packoffset(c18.w);
  float g_porthole_pp_saturation : packoffset(c19);
  float g_porthole_pp_contrast : packoffset(c19.y);
}

cbuffer lighting_VS_PS : register(b2)
{
  bool g_apply_environment_specular : packoffset(c0);
  bool g_contact_shadows : packoffset(c0.y);
  float3 sun_direction : packoffset(c1);
  float3 sun_disk_direction : packoffset(c2);
  float3 sun_colour : packoffset(c3);
  float3 sun_colour_unscaled : packoffset(c4);
  float2 sun_angular_radius : packoffset(c5);
  float sky_colour_scale : packoffset(c5.z);
  float3 ambient_cube_lr[2] : packoffset(c6);
  float3 ambient_cube_tb[2] : packoffset(c8);
  float3 ambient_cube_fb[2] : packoffset(c10);
  float3 g_deep_water_colour : packoffset(c12);
  float3 g_shallow_water_colour : packoffset(c13);
  float3 g_sea_bed_light_scatter : packoffset(c14);
  int g_ssr_enabled : packoffset(c14.w);
  float2 g_cloud_shadow_direction : packoffset(c15);
  float g_cloud_shadow_speed : packoffset(c15.z);
  float g_cloud_shadow_scale : packoffset(c15.w);
  float2 g_cloud_shadow_lerp : packoffset(c16);
  float2 g_noise_uv_shift : packoffset(c16.z);
  float2 g_skin_curvature_scale_bias : packoffset(c17);
  float2 g_skin_translucency_scale_bias : packoffset(c17.z);
  float3 g_skin_blood_colour : packoffset(c18);
  float4 g_world_bounds : packoffset(c19);
  float4 g_water_plane_bounds : packoffset(c20);
  float4 g_playable_area_bounds : packoffset(c21);
  float g_vegetation_wrap_lighting_bias : packoffset(c22);
  float g_spherical_harmonic_terms : packoffset(c22.y);
  float g_spherical_harmonic_fadeout : packoffset(c22.z);
  float g_debug_white_diffuse : packoffset(c22.w);
  float g_debug_light_diffuse_coef : packoffset(c23);
  float g_debug_light_ambient_coef : packoffset(c23.y);
  float g_debug_light_specular_coef : packoffset(c23.z);
  float g_debug_light_shadow_coef : packoffset(c23.w);
  float g_light_vegetation_backscattering_coef : packoffset(c24);
  float g_debug_light_bs_coef2 : packoffset(c24.y);
  float g_borders_colour_scale : packoffset(c24.z);
  float g_water_caustics_scale : packoffset(c24.w);
  float g_water_specular_scale : packoffset(c25);
  float g_campaign_water_attrition_strength : packoffset(c25.y);
  float g_dynamic_light_scale : packoffset(c25.z);
  float4 g_skybox_cylinder_params : packoffset(c26);
  float4 g_fog_cell_shading : packoffset(c27);
  float2 g_aristeia_pos : packoffset(c28);
  float4 g_aristeia_effect : packoffset(c29);
  float g_campaign_flat_map : packoffset(c30);
  bool g_interactive_water_enabled : packoffset(c30.y);
  float4 g_interactive_water_bounds : packoffset(c31);
  float4 g_rain_camera_position : packoffset(c32);
  float4 g_rain_camera_mat_col1 : packoffset(c33);
  float4 g_rain_camera_mat_col2 : packoffset(c34);
  float4 g_rain_camera_mat_col3 : packoffset(c35);
  float4 g_rain_direction : packoffset(c36);
  float4 g_rain_color : packoffset(c37);
  float4 g_rain_lightning : packoffset(c38);
  float4 g_rain_puddles_params : packoffset(c39);
}

cbuffer fog_legacy : register(b3)
{
  float3 g_legacy_volume_fog_colour : packoffset(c0);
  float g_legacy_fog_distance_start : packoffset(c0.w);
  float g_legacy_fog_distance_strength : packoffset(c1);
  float g_legacy_fog_distance_scale : packoffset(c1.y);
  float g_legacy_fog_height_bottom : packoffset(c1.z);
  float g_legacy_fog_height_top : packoffset(c1.w);
  float g_legacy_fog_height_strength : packoffset(c2);
  float g_legacy_fog_colour_blend : packoffset(c2.y);
  float g_legacy_fog_clear_distance : packoffset(c2.z);
  float2 g_legacy_force_fog : packoffset(c3);
  float g_legacy_fog_lerp : packoffset(c3.z);
}

cbuffer fog : register(b4)
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
  float3 g_height_fog_colour : packoffset(c5);
  float g_height_fog_falloff : packoffset(c5.w);
  float g_height_fog_clear_falloff : packoffset(c6);
}

cbuffer shader_params : register(b5)
{
  float sp_clouds_1_brightness : packoffset(c0);
  float sp_clouds_1_opacity : packoffset(c0.y);
  float2 sp_clouds_1_speed : packoffset(c0.z);
  float4 sp_clouds_1_tint : packoffset(c1);
  float sp_clouds_2_brightness : packoffset(c2);
  float sp_clouds_2_opacity : packoffset(c2.y);
  float2 sp_clouds_2_speed : packoffset(c2.z);
  float4 sp_clouds_2_tint : packoffset(c3);
  float sp_fog_gradient_exponent : packoffset(c4);
  float sp_fog_height_stretch : packoffset(c4.y);
  float sp_fog_opacity : packoffset(c4.z);
  float sp_sky_brightness : packoffset(c4.w);
  float4 sp_sky_tint : packoffset(c5);
}

SamplerState s_sky_s : register(s0);
SamplerState s_volume_noise_s : register(s1);
SamplerState s_2d_noise_s : register(s2);
SamplerState s_diffuse_s : register(s3);
TextureCube<float4> t_sky : register(t0);
Texture3D<float4> t_volume_noise : register(t1);
Texture2D<float4> t_2d_noise : register(t2);
Texture2D<float4> t_diffuse : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "../../twpharaohdynasties/sky_deband.hlsli"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7,
  float4 v9 : COLOR1,
  float4 v10 : COLOR2,
  float4 v11 : NORMAL0,
  uint v12 : NORMAL1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = -g_skydome_params.x + v2.x;
  r0.y = v2.y;
  float2 skyUV = r0.xy;
  float2 skyDx = ddx(skyUV);
  float2 skyDy = ddy(skyUV);
  r0.xyzw = t_diffuse.Sample(s_diffuse_s, r0.xy).xyzw;
  r0.rgb = SkyDeband(r0.rgb, skyUV, skyDx, skyDy, v0.xy);
  r0.xyz = sp_sky_tint.xyz * r0.xyz;
  r1.xyz = sky_colour_scale * r0.xyz;
  r1.w = log2(abs(v2.y));
  r1.w = sp_fog_gradient_exponent * r1.w;
  r1.w = exp2(r1.w);
  r1.w = sp_fog_opacity * r1.w;
  if (g_fog_mode == 0) {
    r2.xyz = -camera_position.xyz + v1.xyz;
    r3.x = dot(r2.xyz, r2.xyz);
    r3.x = sqrt(r3.x);
    r3.y = saturate(g_legacy_fog_distance_start);
    r3.y = 1 + -r3.y;
    r3.y = r3.y * 8 + -4;
    r3.z = saturate(g_legacy_fog_distance_strength);
    r3.z = 1 + -r3.z;
    r3.z = 1000 * r3.z;
    r3.z = r3.x / r3.z;
    r3.y = r3.y + r3.z;
    r3.y = 1.44269502 * r3.y;
    r3.y = exp2(r3.y);
    r3.y = g_legacy_fog_distance_scale / r3.y;
    r3.y = saturate(g_legacy_fog_distance_scale + -r3.y);
    r3.z = g_legacy_fog_height_top + -v1.y;
    r3.w = g_legacy_fog_height_top + -g_legacy_fog_height_bottom;
    r3.z = -g_legacy_fog_height_bottom + r3.z;
    r3.w = 1 / r3.w;
    r3.z = saturate(r3.z * r3.w);
    r3.w = r3.z * -2 + 3;
    r3.z = r3.z * r3.z;
    r3.z = r3.w * r3.z;
    r3.w = dot(r2.xz, r2.xz);
    r3.w = sqrt(r3.w);
    r4.x = max(0.00100000005, g_legacy_fog_clear_distance);
    r4.x = 1 / r4.x;
    r3.w = r4.x * r3.w;
    r3.w = min(1, r3.w);
    r4.x = r3.w * -2 + 3;
    r3.w = r3.w * r3.w;
    r3.w = r4.x * r3.w;
    r3.y = g_legacy_fog_height_strength * r3.z + r3.y;
    r4.xyz = g_legacy_volume_fog_colour.xyz * sun_colour.xyz;
    r4.xyz = float3(1.5,1.5,1.5) * r4.xyz;
    r4.xyz = abs(sun_direction.yyy) * r4.xyz;
    r5.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r4.xyz;
    r2.w = max(0, r2.y);
    r2.xyz = t_sky.Sample(s_sky_s, r2.xwz).xyz;
    r2.w = saturate(dot(r3.yy, g_legacy_fog_colour_blend));
    r2.xyz = -r4.xyz * float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) + r2.xyz;
    r2.xyz = r2.www * r2.xyz + r5.xyz;
    r2.w = saturate(r3.w * r3.y);
    r3.y = g_legacy_force_fog.y + -g_legacy_force_fog.x;
    r3.x = -g_legacy_force_fog.x + r3.x;
    r3.y = 1 / r3.y;
    r3.x = saturate(r3.x * r3.y);
    r3.y = r3.x * -2 + 3;
    r3.x = r3.x * r3.x;
    r3.x = r3.y * r3.x;
    r3.y = 1 + -r2.w;
    r2.w = r3.x * r3.y + r2.w;
    r2.xyz = r2.xyz * r1.www + -r1.xyz;
    r2.xyz = r2.www * r2.xyz + r1.xyz;
  } else {
    r3.xy = float2(0.00200000009,0.00200000009) * v1.xz;
    r2.w = t_2d_noise.Sample(s_2d_noise_s, r3.xy).x;
    r2.w = g_legacy_force_fog.x * r2.w;
    r3.xyz = -camera_position.xyz + v1.xyz;
    r3.w = dot(r3.xyz, r3.xyz);
    r4.x = sqrt(r3.w);
    r4.y = min(g_height_fog_clear_distance, g_fog_clear_distance);
    r2.w = r2.w * 0.5 + r4.x;
    r4.z = g_legacy_force_fog.y + -g_legacy_force_fog.x;
    r2.w = -g_legacy_force_fog.x + r2.w;
    r4.z = 1 / r4.z;
    r2.w = saturate(r4.z * r2.w);
    r4.z = r2.w * -2 + 3;
    r2.w = r2.w * r2.w;
    r2.w = r4.z * r2.w;
    r4.z = 9.99999975e-005 + r4.y;
    r4.z = cmp(r4.z < r4.x);
    if (r4.z != 0) {
      r4.w = rsqrt(r3.w);
      r5.xyz = r4.www * r3.xyz;
      r5.xyz = r5.xyz * r4.yyy + camera_position.xyz;
      r5.xyz = v1.xyz + -r5.xyz;
      r4.w = dot(r5.xyz, r5.xyz);
      r4.w = sqrt(r4.w);
      r5.x = g_campaign_density_scaling * r4.w;
      r5.z = max(0, v1.y);
      r6.xyzw = float4(-0.000206276105,-0.000206276105,-0.000206276105,-0.00120224594) * r5.zzzz;
      r6.xyzw = exp2(r6.xyzw);
      r6.xyzw = r6.xyzw * r5.xxxx;
      r4.w = r5.y / r4.w;
      r5.x = -g_height_fog_clear_distance + r4.x;
      r5.yz = -float2(g_fog_height_top, g_fog_height_bottom) + camera_position.yy;
      r5.w = 10000 + camera_position.y;
      r5.yzw = r5.yzw / -r4.www;
      r7.xy = min(r5.yz, r5.zw);
      r5.y = max(r5.y, r5.z);
      r7.x = max(0, r7.x);
      r5.z = max(r5.z, r5.w);
      r5.w = max(0, r7.y);
      r5.xyz = max(float3(0,0,0), r5.xyz);
      r7.y = min(r7.x, r5.y);
      r7.z = max(r5.x, r7.x);
      r5.y = min(r7.z, r5.y);
      r7.z = cmp(r7.y != r5.y);
      r7.w = r7.y * r4.w + camera_position.y;
      r5.y = r5.y + -r7.x;
      r7.x = r7.y + -r7.x;
      r7.y = r7.x + -r5.y;
      r7.w = r7.w + r7.w;
      r5.y = r7.x + r5.y;
      r4.w = r4.w * r5.y + r7.w;
      r4.w = -g_fog_height_top * 2 + r4.w;
      r4.w = r7.y * r4.w;
      r5.y = g_fog_height_top + -g_fog_height_bottom;
      r5.y = r5.y + r5.y;
      r4.w = r4.w / r5.y;
      r4.w = r7.z ? r4.w : 0;
      r5.x = max(r5.x, r5.w);
      r5.xy = min(r5.xw, r5.zz);
      r5.z = cmp(r5.y != r5.x);
      r5.xy = r5.xy + -r5.ww;
      r5.x = r5.x + -r5.y;
      r5.x = r5.x + r4.w;
      r4.w = r5.z ? r5.x : r4.w;
      r4.w = g_fog_density_height * r4.w;
      r5.x = -g_fog_clear_distance + r4.x;
      r5.x = max(0, r5.x);
      r5.x = g_fog_density_constant * r5.x;
      r4.w = max(r5.x, r4.w);
      r5.xyzw = float4(5.80000005e-006,1.35e-005,3.3100001e-005,2.20000002e-005) * r6.xyzw;
      r5.xyz = r5.xyz + r5.www;
      r5.xyz = r4.www * float3(0.0125000002,0.0125000002,0.0125000002) + r5.xyz;
      r5.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r5.xyz;
      r5.xyz = exp2(r5.xyz);
    } else {
      r5.xyz = float3(1,1,1);
    }
    r5.xyz = r2.www * -r5.xyz + r5.xyz;
    r5.xyz = float3(-1,-1,-1) + r5.xyz;
    r5.xyz = g_fog_lerp * r5.xyz + float3(1,1,1);
    r6.xyz = r3.xyz / r4.xxx;
    r4.w = dot(-r6.xyz, sun_direction.xyz);
    if (r4.z != 0) {
      r3.w = rsqrt(r3.w);
      r3.xyz = r3.xyz * r3.www;
      r3.xyz = r3.xyz * r4.yyy + camera_position.xyz;
      r7.xyz = v1.xyz + -r3.xyz;
      r3.w = dot(r7.xyz, r7.xyz);
      r3.w = sqrt(r3.w);
      r4.yz = r3.xz * float2(0.00100000005,0.00100000005) + v0.xy;
      r4.yz = r6.xy * float2(0.100000001,0.100000001) + r4.yz;
      r4.yz = float2(0.103100002,0.103100002) * r4.yz;
      r4.yz = frac(r4.yz);
      r7.xyz = float3(19.1900005,19.1900005,19.1900005) + r4.zyy;
      r5.w = dot(r4.yzy, r7.xyz);
      r4.yz = r5.ww + r4.yz;
      r4.z = r4.y + r4.z;
      r4.y = r4.z * r4.y;
      r4.y = frac(r4.y);
      r4.y = frac(r4.y);
      r4.z = -5000 + r3.w;
      r4.z = saturate(0.000125000006 * r4.z);
      r5.w = r4.z * -2 + 3;
      r4.z = r4.z * r4.z;
      r4.z = r5.w * r4.z;
      r5.w = 0.25 + -r4.y;
      r4.y = r4.z * r5.w + r4.y;
      r7.xy = float2(0.03125,0.015625) * r3.ww;
      r4.z = cmp(0 < g_fog_noise);
      r8.xyz = float3(0,0,0);
      r9.xyz = float3(0,0,0);
      r7.zw = float2(0,0);
      r5.w = 0;
      r6.w = 0;
      while (true) {
        r8.w = cmp((int)r6.w >= 32);
        if (r8.w != 0) break;
        r8.w = (int)r6.w;
        r8.w = r8.w + r4.y;
        r8.w = r7.x * r8.w + r7.y;
        r8.w = min(r8.w, r3.w);
        r10.xyz = r6.xyz * r8.www + r3.xyz;
        r8.w = cmp(r8.w < 400);
        r8.w = r4.z ? r8.w : 0;
        if (r8.w != 0) {
          r10.xzw = time_in_sec * float3(4.4000001,-4.4000001,4.4000001) + r10.xyz;
          r10.xzw = float3(0.00999999978,0.00999999978,0.00999999978) * r10.xzw;
          r8.w = t_volume_noise.Sample(s_volume_noise_s, r10.xzw).x;
          r8.w = r8.w * 2 + -1;
          r8.w = r8.w * g_fog_noise + 1;
        } else {
          r8.w = 1;
        }
        r5.w = r8.w + r5.w;
        r8.w = 60000 + -r10.y;
        r9.w = r8.w * 0.5 + r10.y;
        r9.w = max(0, r9.w);
        r10.xz = float2(-0.000206276105,-0.00120224594) * r9.ww;
        r10.xz = exp2(r10.xz);
        r9.w = g_fog_density_mie * r10.z;
        r10.y = max(0, r10.y);
        r10.yz = float2(-0.000206276105,-0.00120224594) * r10.yy;
        r10.yz = exp2(r10.yz);
        r10.w = r10.y * r7.x;
        r7.z = r10.y * r7.x + r7.z;
        r10.y = g_fog_density_mie * r10.z;
        r10.z = r10.y * r7.x;
        r7.w = r10.y * r7.x + r7.w;
        r10.x = r10.x * r8.w + r7.z;
        r8.w = r9.w * r8.w + r7.w;
        r8.w = 2.20000002e-005 * r8.w;
        r11.xyz = r10.xxx * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r8.www;
        r11.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r11.xyz;
        r11.xyz = exp2(r11.xyz);
        r8.xyz = r11.xyz * r10.www + r8.xyz;
        r9.xyz = r11.xyz * r10.zzz + r9.xyz;
        r6.w = (int)r6.w + 1;
      }
      r3.x = r4.w * r4.w + 1;
      r3.y = 0.5 * r3.x;
      r3.yzw = r8.xyz * r3.yyy;
      r4.y = -r4.w * 1.51999998 + 1.5776;
      r4.y = log2(abs(r4.y));
      r4.y = 1.5 * r4.y;
      r4.y = exp2(r4.y);
      r3.x = r3.x / r4.y;
      r3.x = 0.000978047145 * r3.x;
      r6.xzw = r9.xyz * r3.xxx;
      r6.xzw = float3(1.99999995e-005,1.99999995e-005,1.99999995e-005) * r6.xzw;
      r3.xyz = r3.yzw * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r6.xzw;
      r3.w = 0.03125 * r5.w;
      r4.y = -g_height_fog_clear_distance + r4.x;
      r4.y = max(0, r4.y);
      r4.z = r6.y * g_height_fog_clear_distance + camera_position.y;
      r5.w = cmp(r4.z >= g_fog_height_top);
      r6.x = cmp(0 < r6.y);
      r5.w = r5.w ? r6.x : 0;
      if (r5.w == 0) {
        r6.xz = -float2(g_fog_height_top, g_fog_height_bottom) + r4.zz;
        r6.xz = r6.xz / -r6.yy;
        r5.w = 10000 + r4.z;
        r5.w = r5.w / -r6.y;
        r6.w = min(r6.x, r6.z);
        r6.x = max(r6.x, r6.z);
        r6.xw = max(float2(0,0), r6.xw);
        r7.x = min(r6.z, r5.w);
        r5.w = max(r6.z, r5.w);
        r6.z = max(0, r7.x);
        r5.w = max(0, r5.w);
        r7.x = g_fog_density_height * r3.w;
        r7.x = 0.0125000002 * r7.x;
        r7.y = min(r6.w, r6.x);
        r7.z = max(r6.w, r4.y);
        r7.z = min(r7.z, r6.x);
        r7.w = cmp(r7.y != r7.z);
        r4.z = r6.w * r6.y + r4.z;
        r7.z = r7.z + -r6.w;
        r8.x = r7.y + -r6.w;
        r8.y = g_fog_height_top + -g_fog_height_bottom;
        r8.y = r8.y + r8.y;
        r4.z = r4.z + r4.z;
        r8.z = r8.x * r6.y + r4.z;
        r8.z = -g_fog_height_top * 2 + r8.z;
        r8.x = r8.x * r8.z;
        r8.x = r8.x * r7.x;
        r8.x = r8.x / r8.y;
        r8.x = 1.44269502 * r8.x;
        r8.x = exp2(r8.x);
        r8.z = r7.z * r6.y + r4.z;
        r8.z = -g_fog_height_top * 2 + r8.z;
        r7.z = r8.z * r7.z;
        r7.z = r7.z * r7.x;
        r7.z = r7.z / r8.y;
        r7.z = 1.44269502 * r7.z;
        r7.z = exp2(r7.z);
        r7.z = r8.x + -r7.z;
        r8.x = min(r6.z, r5.w);
        r8.z = max(r7.y, r6.z);
        r8.z = min(r8.z, r5.w);
        r8.w = cmp(r8.x != r8.z);
        r8.z = r8.z + -r6.z;
        r8.z = -r8.z * r7.x;
        r8.z = 1.44269502 * r8.z;
        r8.z = exp2(r8.z);
        r8.z = r8.z * r7.z;
        r7.z = r8.w ? r8.z : r7.z;
        r7.z = r7.w ? r7.z : 0;
        r7.w = max(r6.z, r4.y);
        r5.w = min(r7.w, r5.w);
        r7.w = cmp(r8.x != r5.w);
        r8.z = r5.w + -r6.z;
        r6.z = r8.x + -r6.z;
        r6.z = -r6.z * r7.x;
        r6.z = 1.44269502 * r6.z;
        r6.z = exp2(r6.z);
        r8.x = -r8.z * r7.x;
        r8.x = 1.44269502 * r8.x;
        r8.x = exp2(r8.x);
        r6.z = -r8.x + r6.z;
        r5.w = max(r5.w, r6.w);
        r5.w = min(r5.w, r6.x);
        r6.x = cmp(r7.y != r5.w);
        r5.w = r5.w + -r6.w;
        r4.z = r5.w * r6.y + r4.z;
        r4.z = -g_fog_height_top * 2 + r4.z;
        r4.z = r5.w * r4.z;
        r4.z = r4.z * r7.x;
        r4.z = r4.z / r8.y;
        r4.z = 1.44269502 * r4.z;
        r4.z = exp2(r4.z);
        r4.z = r6.z * r4.z;
        r4.z = r6.x ? r4.z : r6.z;
        r4.z = r7.z + r4.z;
        r6.xyz = r7.www ? r4.zzz : r7.zzz;
      } else {
        r6.xyz = float3(0,0,0);
      }
      r4.z = 1 / g_clear_distance_fade_out;
      r4.y = saturate(r4.y * r4.z);
      r4.z = r4.y * -2 + 3;
      r4.y = r4.y * r4.y;
      r4.y = r4.z * r4.y;
      r6.xyz = r6.xyz * r4.yyy;
      r4.x = -g_fog_clear_distance + r4.x;
      r4.x = max(0, r4.x);
      r4.y = cmp(0.00999999978 < g_clear_distance_fade_out);
      r4.z = cmp(0.00999999978 < r4.x);
      r4.y = r4.z ? r4.y : 0;
      r4.z = min(g_clear_distance_fade_out, r4.x);
      r5.w = r4.z * r4.z;
      r6.w = g_clear_distance_fade_out + g_clear_distance_fade_out;
      r5.w = r5.w / r6.w;
      r4.z = r4.x + -r4.z;
      r4.z = r5.w + r4.z;
      r4.z = r4.z / r4.x;
      r4.y = r4.y ? r4.z : 1;
      r3.w = g_fog_density_constant * r3.w;
      r3.w = r3.w * r4.y;
      r3.w = r3.w * r4.x;
      r3.w = 0.0180336889 * r3.w;
      r4.x = exp2(r3.w);
      r3.w = exp2(-r3.w);
      r4.y = r4.x + r3.w;
      r3.w = r4.x + -r3.w;
      r3.w = 0.5 * r3.w;
      r3.w = saturate(r4.y * 0.5 + -r3.w);
      r3.w = 1 + -r3.w;
      r4.xyz = max(r6.xyz, r3.www);
    } else {
      r4.xyz = float3(0,0,0);
      r3.xyz = float3(0,0,0);
    }
    r4.xyz = max(r4.xyz, r2.www);
    r2.w = r4.w * r4.w + 1;
    r2.w = 0.5 * r2.w;
    r3.xyz = r4.xyz * r2.www + r3.xyz;
    r3.xyz = g_fog_colour.xyz * r3.xyz;
    r4.xyz = sun_colour.xyz * float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) + ambient_cube_tb[0].xyz;
    r3.xyz = r4.xyz * r3.xyz;
    r3.xyz = g_fog_lerp * r3.xyz;
    r3.xyz = r3.xyz * r1.www;
    r2.xyz = r1.xyz * r5.xyz + r3.xyz;
  }
  r0.xyz = -r0.xyz * sky_colour_scale + r2.xyz;
  r0.xyz = r1.www * r0.xyz + r1.xyz;
  r1.x = dot(r0.xyz, float3(0.330000013,0.550000012,0.119999997));
  r1.xyz = r1.xxx + -r0.xyz;
  o0.xyz = g_skydome_shading.xxx * r1.xyz + r0.xyz;
  o0.w = -r0.w * 0.100000001 + 1;
  return;
}