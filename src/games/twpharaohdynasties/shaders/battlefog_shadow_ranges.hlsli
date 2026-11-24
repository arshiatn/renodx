#ifndef SRC_GAMES_TWPHARAOHDYNASTIES_SHADERS_BATTLEFOG_SHADOW_RANGES_HLSLI_
#define SRC_GAMES_TWPHARAOHDYNASTIES_SHADERS_BATTLEFOG_SHADOW_RANGES_HLSLI_

// battlefog.hlsl ps40_filter_shadow_ranges: the game's (3Dmigoto's decompile, the same for its shader model 5 and 4.1
// builds), its 5x5 filter of the fog's shadow ranges spread over the view's pixels. The game steps its taps by the
// G-buffer's texel (its size; 0.1 of a texel apart in NDC): below 100% (DLSS, FSR, XeSS) the view fills only the frame's
// share of that target, so its taps reached a third as far at 33%. Now the step is the view's texel: the size x the
// share (render_scale.hlsli; unbound, 100%: 1, the game's bits).
#include "../../twattila/render_scale.hlsli"

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

cbuffer fog : register(b2)
{
  float g_fog_density_constant : packoffset(c0);
  float g_fog_density_height : packoffset(c0.y);
  float g_fog_height_bottom : packoffset(c0.z);
  float g_fog_height_top : packoffset(c0.w);
  float3 g_fog_noise_position : packoffset(c1);
  float g_fog_density_mie : packoffset(c1.w);
  float g_fog_noise : packoffset(c2);
  float3 g_fog_colour : packoffset(c2.y);
  uint g_fog_mode : packoffset(c3);
  float g_fog_lerp : packoffset(c3.y);
  float g_fog_clear_distance : packoffset(c3.z);
  float g_fog_y_multiplier : packoffset(c3.w);
  float g_campaign_density_scaling : packoffset(c4);
  float g_height_fog_clear_distance : packoffset(c4.y);
  float g_clear_distance_fade_out : packoffset(c4.z);
  float g_fog_far_skybox_lerp : packoffset(c4.w);
  float g_fog_far_skybox_mip : packoffset(c5);
  float2 g_fog_shadow_strength : packoffset(c5.y);
  float g_fog_blur_offset : packoffset(c5.w);
  float g_fog_blur_strength : packoffset(c6);
  float3 g_height_fog_colour : packoffset(c6.y);
  float g_height_fog_falloff : packoffset(c7);
  float g_height_fog_clear_falloff : packoffset(c7.y);
  float g_height_fog_forward_scattering : packoffset(c7.z);
  float2 g_campaign_map_size : packoffset(c8);
  float3 g_rayleigh_density : packoffset(c9);
  float g_atmosphere_height_multiplier : packoffset(c9.w);
  float g_atmosphere_bottom : packoffset(c10);
  float g_sun_disc_color_scale : packoffset(c10.y);
  float g_sun_scale : packoffset(c10.z);
  float g_fog_resolution : packoffset(c10.w);
  float g_height_occlusion : packoffset(c11);
  float g_density_sum : packoffset(c11.y);
  float g_skybox_size : packoffset(c11.z);
}

Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t0);
Texture2D<float4> gbuffer_channel_4_texture : register(t1);
Texture2D<float2> t_distance_buffer : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float2 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (uint2)v0.xy;
  r0.zw = float2(0,0);
  r0.xy = t_distance_buffer.Load(r0.xyz).xy;
  r0.z = cmp(r0.x != 1.000000);
  r0.x = camera_near_far.x / r0.x;
  r0.x = r0.z ? r0.x : 0;
  r1.x = dot(-sun_disk_direction.xyz, view._m00_m10_m20);
  r1.y = dot(-sun_disk_direction.xyz, view._m01_m11_m21);
  r1.z = dot(-sun_disk_direction.xyz, view._m02_m12_m22);
  r1.w = dot(-sun_disk_direction.xyz, view._m03_m13_m23);
  r2.x = dot(r1.xyzw, projection._m00_m10_m20_m30);
  r2.y = dot(r1.xyzw, projection._m01_m11_m21_m31);
  r2.z = dot(r1.xyzw, projection._m03_m13_m23_m33);
  r0.zw = g_fog_resolution * g_screen_size.xy;
  r3.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = r3.xy / r0.zw;
  r3.xyzw = g_render_target_dimensions.xyzw * g_viewport_dimensions.zwxy;
  r0.zw = r3.xy * r0.zw;
  r4.xy = r0.zw * float2(2,-2) + float2(-1,1);
  r4.zw = float2(1,1);
  r5.x = dot(r4.xyww, inv_projection._m00_m10_m20_m30);
  r5.y = dot(r4.xyww, inv_projection._m01_m11_m21_m31);
  r5.z = dot(r4.xyww, inv_projection._m02_m12_m22_m32);
  r0.z = dot(r4.xyzw, inv_projection._m03_m13_m23_m33);
  r4.xyz = r5.xyz / r0.zzz;
  r0.z = dot(r4.xyz, r4.xyz);
  r0.z = rsqrt(r0.z);
  r4.xyz = r4.xyz * r0.zzz;
  r1.xyz = r1.xyz * r0.yyy;
  r1.xyz = r4.xyz * r0.xxx + r1.xyz;
  r1.w = 1;
  r3.x = dot(r1.xyzw, projection._m00_m10_m20_m30);
  r3.y = dot(r1.xyzw, projection._m01_m11_m21_m31);
  r1.z = dot(r1.xyzw, projection._m03_m13_m23_m33);
  r0.z = cmp(1 < g_num_of_samples);
  if (r0.z != 0) {
    gbuffer_channel_4_texture_ms.GetDimensions(uiDest.x, uiDest.y, uiDest.z);
    r0.zw = uiDest.xy;
  } else {
    gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r0.zw = uiDest.xy;
  }
  r4.xy = (uint2)r0.zw;
  r4.zw = float2(2,2) / (r4.xy * FrameRenderShare());
  r4.zw = float2(0.100000001,0.100000001) * r4.zw;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r5.zw = float2(0,0);
  r6.xy = float2(0,0);
  r7.y = -2;
  while (true) {
    r1.w = cmp(2 < r7.y);
    if (r1.w != 0) break;
    r1.w = 2 + -abs(r7.y);
    r1.w = exp2(r1.w);
    r8.xy = r6.xy;
    r8.z = -2;
    while (true) {
      r2.w = cmp(2 < r8.z);
      if (r2.w != 0) break;
      r7.x = r8.z;
      r6.zw = r7.xy * r4.zw;
      r1.xy = r6.zw * r1.zz + r3.xy;
      r7.xzw = -r0.yyy * r2.xyz + r1.xyz;
      r1.xy = r7.xz / r7.ww;
      r1.xy = r1.xy * float2(0.5,-0.5) + float2(0.5,0.5);
      r1.xy = r1.xy * r3.zw;
      r1.xy = r1.xy * r4.xy;
      r1.xy = floor(r1.xy);
      r1.xy = (int2)r1.xy;
      r1.xy = max(int2(0,0), (int2)r1.xy);
      r1.xy = min((int2)r1.xy, (int2)r0.zw);
      r1.xy = (int2)r1.xy;
      r1.xy = g_fog_resolution * r1.xy;
      r5.xy = (int2)r1.xy;
      r1.xy = t_distance_buffer.Load(r5.xyz).xy;
      r2.w = cmp(r1.x != 1.000000);
      r1.x = camera_near_far.x / r1.x;
      r1.x = r2.w ? r1.x : 0;
      r1.y = cmp(r1.y < 65000);
      r1.x = max(r1.x, r0.x);
      r1.x = r1.y ? r1.x : r0.x;
      r1.y = 2 + -abs(r8.z);
      r1.y = exp2(r1.y);
      r2.w = r1.y * r1.w;
      r8.x = r2.w * r1.x + r8.x;
      r8.y = r1.y * r1.w + r8.y;
      r8.z = 1 + r8.z;
    }
    r6.xy = r8.xy;
    r7.y = 1 + r7.y;
  }
  r0.x = r6.x / r6.y;
  r0.x = max(camera_near_far.x, r0.x);
  o0.x = camera_near_far.x / r0.x;
  o0.y = r0.y;
  return;
}

#endif  // SRC_GAMES_TWPHARAOHDYNASTIES_SHADERS_BATTLEFOG_SHADOW_RANGES_HLSLI_
