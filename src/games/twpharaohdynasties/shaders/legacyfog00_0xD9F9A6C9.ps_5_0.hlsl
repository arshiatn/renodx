// dx11_battlefog main (the legacy fog, 0xD9F9A6C9; in no trace of the maintainer's): the game's, Fog & Sky Smoothing on its sky cube
// (twattila/sky_smooth.hlsli, SI.sky_smooth) and Fog Strength on its opacity (cb8, the strength above one: 0 the game's).
// Drawn only with Fog & Sky Smoothing on (addon.cpp OnSkySmoothDraw); otherwise the game's own bytes (Fog Strength's patch)
// do. WARP: tools\tw\warp_tests\pharaoh\fog_v54 (legacy mode).
#include "../shared.h"
#include "../../twattila/sky_smooth.hlsli"

cbuffer rt_fog_strength : register(b8) {
  float rt_fog_strength_above_one;
};

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
  float g_overlay_text_lerp : packoffset(c47.y);
  float g_overlay_parchment_lerp : packoffset(c47.z);
  float g_overlay_palette_alpha : packoffset(c47.w);
  float4 g_overlay_outline_colour : packoffset(c48);
  float g_overlay_outline_border_width : packoffset(c49);
  float g_ui_overlay_brightness : packoffset(c49.y);
  float g_debug_tonemapping : packoffset(c49.z);
  float4 g_blood_remap : packoffset(c50);
  int g_editor_mode : packoffset(c51);
  float4 g_spec_gloss_tweaker : packoffset(c52);
}

cbuffer lighting_VS_PS : register(b1)
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
  float2 g_cloud_shadow_position : packoffset(c15);
  float g_cloud_shadow_scale : packoffset(c15.z);
  float2 g_cloud_shadow_lerp : packoffset(c16);
  float3 g_cloud_shadow_tiling_params : packoffset(c17);
  float2 g_noise_uv_shift : packoffset(c18);
  float2 g_skin_curvature_scale_bias : packoffset(c18.z);
  float2 g_skin_translucency_scale_bias : packoffset(c19);
  float3 g_skin_blood_colour : packoffset(c20);
  float4 g_world_bounds : packoffset(c21);
  float4 g_water_plane_bounds : packoffset(c22);
  float4 g_playable_area_bounds : packoffset(c23);
  float g_vegetation_wrap_lighting_bias : packoffset(c24);
  float g_spherical_harmonic_terms : packoffset(c24.y);
  float g_spherical_harmonic_fadeout : packoffset(c24.z);
  float g_debug_puddle_map : packoffset(c24.w);
  float g_debug_white_diffuse : packoffset(c25);
  float g_debug_light_diffuse_coef : packoffset(c25.y);
  float g_debug_light_ambient_coef : packoffset(c25.z);
  float g_debug_light_specular_coef : packoffset(c25.w);
  float g_debug_light_shadow_coef : packoffset(c26);
  float g_light_vegetation_backscattering_coef : packoffset(c26.y);
  float g_debug_light_bs_coef2 : packoffset(c26.z);
  float g_borders_colour_scale : packoffset(c26.w);
  float g_water_caustics_scale : packoffset(c27);
  float g_water_specular_scale : packoffset(c27.y);
  float g_campaign_water_attrition_strength : packoffset(c27.z);
  float g_water_light_scattering : packoffset(c27.w);
  float g_water_light_scattering_battle_rivers : packoffset(c28);
  float g_dynamic_light_scale : packoffset(c28.y);
  float4 g_skybox_cylinder_params : packoffset(c29);
  float4 g_fog_cell_shading : packoffset(c30);
  float2 g_aristeia_pos : packoffset(c31);
  float4 g_aristeia_effect : packoffset(c32);
  float g_campaign_flat_map : packoffset(c33);
  bool g_interactive_water_enabled : packoffset(c33.y);
  float4 g_interactive_water_bounds : packoffset(c34);
  float4 g_rain_camera_position : packoffset(c35);
  float4 g_rain_camera_mat_col1 : packoffset(c36);
  float4 g_rain_camera_mat_col2 : packoffset(c37);
  float4 g_rain_camera_mat_col3 : packoffset(c38);
  float4 g_rain_direction : packoffset(c39);
  float4 g_rain_color : packoffset(c40);
  float4 g_rain_lightning : packoffset(c41);
  float4 g_rain_puddles_params : packoffset(c42);
  float4 g_sky_correction_colour : packoffset(c43);
  float g_sky_correction_contrast : packoffset(c44);
  float g_sky_correction_brightness : packoffset(c44.y);
  float4 g_nile_colour : packoffset(c45);
  float g_nile_flow_speed : packoffset(c46);
  float4 g_puddle_map_bounds : packoffset(c47);
  int g_high_quality_puddle_sampling : packoffset(c48);
  float2 g_world_height_bounds : packoffset(c48.y);
  float4 g_screen_space_shadows_parameters : packoffset(c49);
  float g_generating_light_probe : packoffset(c50);
  int g_screen_space_shadows_step_count : packoffset(c50.y);
  int g_screen_space_environment_occlusion_quality : packoffset(c50.z);
  int g_screen_space_environment_occlusion_step_count : packoffset(c50.w);
  float g_screen_space_environment_occlusion_max_trace_distance : packoffset(c51);
  float2 g_screen_space_environment_smoothness_bounds : packoffset(c51.y);
  float2 g_nile_map_offset : packoffset(c52);
}

cbuffer fog_legacy : register(b2)
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

SamplerState s_sky_s : register(s0);
TextureCube<float4> t_sky : register(t0);
Texture2D<float4> gbuffer_channel_1_texture : register(t1);
Texture2D<float4> gbuffer_channel_4_texture : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  gbuffer_channel_1_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r1.xy = (uint2)r0.zw;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r0.xy = r1.xy * r0.xy;
  r0.xy = floor(r0.xy);
  r0.xy = (int2)r0.xy;
  r0.xy = max(int2(0,0), (int2)r0.xy);
  r0.xy = min((int2)r0.xy, (int2)r0.zw);
  r0.zw = float2(0,0);
  r0.z = gbuffer_channel_4_texture.Load(r0.xyz).x;
  r1.xy = -g_viewport_origin.xy + v0.xy;
  r1.xy = g_vpos_texel_offset + r1.xy;
  r1.xy = g_viewport_dimensions.zw * r1.xy;
  r0.xy = r1.xy * float2(2,-2) + float2(-1,1);
  r0.w = 1;
  r1.x = dot(r0.xyzw, inv_view_projection._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, inv_view_projection._m01_m11_m21_m31);
  r1.z = dot(r0.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.x = dot(r0.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r1.xyz / r0.xxx;
  r0.w = g_legacy_fog_height_top + -r0.y;
  r1.xyz = -camera_position.xyz + r0.xyz;
  r0.x = -g_legacy_fog_height_bottom + r0.w;
  r0.y = g_legacy_fog_height_top + -g_legacy_fog_height_bottom;
  r0.y = 1 / r0.y;
  r0.x = saturate(r0.x * r0.y);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = r0.y * r0.x;
  r0.y = dot(r1.xyz, r1.xyz);
  r0.y = sqrt(r0.y);
  r0.z = saturate(g_legacy_fog_distance_strength);
  r0.z = 1 + -r0.z;
  r0.z = 1000 * r0.z;
  r0.z = r0.y / r0.z;
  r0.y = -g_legacy_force_fog.x + r0.y;
  r0.w = saturate(g_legacy_fog_distance_start);
  r0.w = 1 + -r0.w;
  r0.w = r0.w * 8 + -4;
  r0.z = r0.w + r0.z;
  r0.z = 1.44269502 * r0.z;
  r0.z = exp2(r0.z);
  r0.z = g_legacy_fog_distance_scale / r0.z;
  r0.z = saturate(g_legacy_fog_distance_scale + -r0.z);
  r0.x = g_legacy_fog_height_strength * r0.x + r0.z;
  r0.z = dot(r1.xz, r1.xz);
  r0.z = sqrt(r0.z);
  r0.w = max(0.00100000005, g_legacy_fog_clear_distance);
  r0.w = 1 / r0.w;
  r0.z = r0.z * r0.w;
  r0.z = min(1, r0.z);
  r0.w = r0.z * -2 + 3;
  r0.z = r0.z * r0.z;
  r0.z = r0.w * r0.z;
  r0.z = saturate(r0.z * r0.x);
  r0.x = saturate(dot(r0.xx, g_legacy_fog_colour_blend));
  r0.w = 1 + -r0.z;
  r2.x = g_legacy_force_fog.y + -g_legacy_force_fog.x;
  r2.x = 1 / r2.x;
  r0.y = saturate(r2.x * r0.y);
  r2.x = r0.y * -2 + 3;
  r0.y = r0.y * r0.y;
  r0.y = r2.x * r0.y;
  r0.y = r0.y * r0.w + r0.z;
  o0.w = g_legacy_fog_lerp * r0.y;
  // Fog Strength (the addon's cb8, as its patch of the game's bytes): the opacity x the strength, up to 1 (or its own).
  if (rt_fog_strength_above_one != 0.f) o0.w = min(o0.w * (1.f + rt_fog_strength_above_one), max(o0.w, 1.f));
  r1.w = max(0, r1.y);
  [branch]
  if (SI.sky_smooth != 0.f) {
    r0.yzw = SkySmooth(t_sky, s_sky_s, r1.xwz);
  } else {
    r0.yzw = t_sky.Sample(s_sky_s, r1.xwz).xyz;
  }
  r1.xyz = g_legacy_volume_fog_colour.xyz * sun_colour.xyz;
  r1.xyz = float3(1.5,1.5,1.5) * r1.xyz;
  r1.xyz = abs(sun_direction.yyy) * r1.xyz;
  r0.yzw = -r1.xyz * float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) + r0.yzw;
  r1.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r1.xyz;
  o0.xyz = r0.xxx * r0.yzw + r1.xyz;
  return;
}