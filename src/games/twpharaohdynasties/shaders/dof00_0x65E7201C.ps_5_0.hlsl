// ---- Created with 3Dmigoto v1.3.16 (RenoDX pack dump, 2026-10-20)

// hdr_to_screen.hlsl ps50_dof_preprocess: the DOF composite (Troy's dof00 / dof01 fixes)


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

cbuffer hdr_to_screen_PS : register(b3)
{
  int rain_planes_count : packoffset(c0);
  float radial_blur_strength : packoffset(c0.y);
  float2 radial_blur_position : packoffset(c0.z);
  float scene_max_distance : packoffset(c1);
  float focus_distance : packoffset(c1.y);
  float focal_length : packoffset(c1.z);
  float blur_kernel_scale : packoffset(c1.w);
  float4x4 colour_matrix : packoffset(c2);
  float4 pos_transform : packoffset(c6);
  float overscan : packoffset(c7);
  float god_rays_strength : packoffset(c7.y);
  float2 glow_strength : packoffset(c7.z);
  float glow_depth_masking : packoffset(c8);
  float post_processing_strength : packoffset(c8.y);
  float lut_strength : packoffset(c8.z);
  float3 grain_tex_offset_enabled : packoffset(c9);
}

SamplerState s_shroud_clouds_s : register(s0);
SamplerState god_rays_sampler_s : register(s1);
SamplerState glow_sampler_s : register(s2);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t0);
Texture2D<float4> gbuffer_channel_4_texture : register(t1);
Texture2D<float4> t_shroud_clouds : register(t2);
Texture2D<float4> god_rays_texture : register(t3);
Texture2D<float4> glow_texture1 : register(t4);
RWTexture2D<float4> frame_texture_rw : register(u0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

// Keeps view depth finite in FP16 (the frame is upgraded from 8-bit): +Inf in the sky turned the DOF spiral's running
// average NaN (Troy's dof00).
static const float kMaxFiniteDepth = 65000.f;


// Stock: one bilinear tap of the bloom; bloom_filter.hlsli: a cubic B-spline over 4 x 4 texels.
#define BLOOM_SAMPLE_ONLY
#include "../../twattila/bloom_filter.hlsli"
#include "../../twattila/render_scale.hlsli"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r0.xy = overscan * r0.xy;
  // God rays and glow are composited here when DOF is on (not in t05-t08).
  const float god_rays_multiplier = (HDR >= 0.5f) ? SI.godrays : 1.f;
  const float glow_multiplier = (HDR >= 0.5f) ? SI.bloom : 1.f;
  // Manual fix: resinfo on u0; stock's address is uv * dimensions, truncated (r1.xw).
  uint2 frame_dims;
  frame_texture_rw.GetDimensions(frame_dims.x, frame_dims.y);
  const int2 frame_pixel = (int2)(r0.xy * (float2)frame_dims);
  r2.xyz = frame_texture_rw[frame_pixel].xyz;  // manual fix: ld_uav_typed
  r0.z = dot(r2.xyz, float3(0.330000013,0.560000002,0.109999999));
  r0.z = saturate(1 + -r0.z);
  r3.xy = g_render_target_dimensions.xy * r0.xy;
  r3.xy = g_viewport_dimensions.zw * r3.xy;
  r0.w = god_rays_texture.SampleLevel(god_rays_sampler_s, r3.xy, 0).x;
  r4.xyz = sun_colour_unscaled.xyz * r0.www;
  r4.xyz = (god_rays_strength * god_rays_multiplier) * r4.xyz;
  r2.xyz = r4.xyz * r0.zzz + r2.xyz;
  r0.w = cmp(1 < g_num_of_samples);
  if (r0.w != 0) {
    gbuffer_channel_4_texture_ms.GetDimensions(uiDest.x, uiDest.y, uiDest.z);
    r3.zw = uiDest.xy;
  } else {
    gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r3.zw = uiDest.xy;
  }
  r4.xy = (uint2)r3.zw;
  r4.xy = r4.xy * r0.xy;
  r4.xy = floor(r4.xy);
  r4.xy = (int2)r4.xy;
  r3.zw = (int2)r3.zw + int2(-1,-1);
  r4.xy = max(int2(0,0), (int2)r4.xy);
  r4.xy = min((int2)r4.xy, (int2)r3.zw);
  if (r0.w != 0) {
    r4.z = 0;
    r5.z = gbuffer_channel_4_texture_ms.Load(RenderShareTexel(r4.xy), 0).x;
  } else {
    r4.w = 0;
    r5.z = gbuffer_channel_4_texture.Load(RenderShareTexel(r4.xyw)).x;
  }
  r5.xy = r3.xy * float2(2,-2) + float2(-1,1);
  r5.w = 1;
  r3.x = dot(r5.xyzw, inv_view_projection._m00_m10_m20_m30);
  r3.y = dot(r5.xyzw, inv_view_projection._m01_m11_m21_m31);
  r3.z = dot(r5.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r5.xyzw, inv_view_projection._m03_m13_m23_m33);
  r3.xyz = r3.xyz / r0.www;
  r4.xyz = -camera_position.xyz + r3.xyz;
  r0.w = dot(r4.xyz, r4.xyz);
  r6.z = sqrt(r0.w);
  r0.w = r4.y / r6.z;
  r2.w = g_fog_noise * -0.600000024 + 1;
  r4.x = g_fog_height_bottom + -g_fog_height_top;
  r4.x = g_height_fog_falloff * r4.x + g_fog_height_top;
  r4.y = g_atmosphere_bottom + -camera_position.y;
  r4.y = r4.y / r0.w;
  r4.y = max(0, r4.y);
  r3.z = min(r4.y, r6.z);
  r4.y = cmp(9.99999997e-007 < r0.w);
  r3.x = max(g_atmosphere_bottom, camera_position.y);
  r4.z = cmp(r0.w < -9.99999997e-007);
  r7.y = max(g_atmosphere_bottom, r3.y);
  r7.z = r6.z + -r3.z;
  r7.x = camera_position.y;
  r7.w = g_atmosphere_bottom;
  r6.xy = float2(0,0);
  r6.w = camera_position.y;
  r7.xyzw = r4.zzzz ? r7.xyzw : r6.xyzw;
  r3.w = g_atmosphere_bottom;
  r3.xyzw = r4.yyyy ? r3.xyzw : r7.xyzw;
  r7.xyzw = -g_atmosphere_bottom + r3.wwxy;
  r7.xyzw = max(float4(0,0,0,0), r7.xyzw);
  r8.xyzw = float4(6994,1200,6994,6994) * g_atmosphere_height_multiplier;
  r9.xyzw = -r7.xyzw / r8.xyzw;
  r9.xyzw = float4(1.44269502,1.44269502,1.44269502,1.44269502) * r9.xyzw;
  r9.xyzw = exp2(r9.xyzw);
  r3.xy = r9.xy * r3.zz;
  r4.yz = cmp(float2(9.99999997e-007,9.99999972e-010) < abs(r0.ww));
  r3.w = r9.z + -r9.w;
  r3.w = r3.w * r8.w;
  r3.w = r3.w / r0.w;
  r6.x = r9.x * r3.z + r3.w;
  r7.xy = -r7.zw / r8.yy;
  r7.xy = float2(1.44269502,1.44269502) * r7.xy;
  r7.xy = exp2(r7.xy);
  r3.w = r7.x + -r7.y;
  r3.w = r3.w * r8.y;
  r3.w = r3.w / r0.w;
  r6.y = r9.y * r3.z + r3.w;
  r3.xy = r4.yy ? r6.xy : r3.xy;
  r3.z = cmp(camera_position.y >= g_fog_height_bottom);
  r3.w = cmp(r4.x >= camera_position.y);
  r3.z = r3.w ? r3.z : 0;
  r3.w = -camera_position.y + r4.x;
  r3.w = r3.w / r0.w;
  r3.w = max(0, r3.w);
  r3.w = min(r3.w, r6.z);
  r3.z = r3.z ? r6.z : 0;
  r3.z = r4.y ? r3.w : r3.z;
  r6.xy = g_fog_height_bottom + -camera_position.yy;
  r6.xy = r6.xy / r0.ww;
  r6.xy = max(float2(0,0), r6.xy);
  r6.xy = min(r6.xy, r6.zz);
  r4.w = r4.y ? r6.x : 0;
  r3.z = -r4.w + r3.z;
  r2.w = g_fog_density_height * r2.w;
  r2.w = 0.0125000002 * r2.w;
  r4.w = cmp(camera_position.y >= r4.x);
  r6.x = cmp(g_fog_height_top >= camera_position.y);
  r4.w = r4.w ? r6.x : 0;
  r6.x = r4.w ? r6.z : 0;
  r6.x = r4.y ? r6.y : r6.x;
  r3.w = r4.y ? r3.w : 0;
  r4.y = max(r3.w, r6.x);
  r3.w = min(r3.w, r6.x);
  r6.x = r3.w * r0.w + camera_position.y;
  r3.w = r4.y + -r3.w;
  r4.x = g_fog_height_top + -r4.x;
  r4.y = cmp(0 != r4.x);
  r4.x = 1 / r4.x;
  r4.x = r4.y ? r4.x : 0;
  r4.y = g_fog_height_top + -r6.x;
  r0.w = r3.w * r0.w;
  r0.w = -r0.w * 0.5 + r4.y;
  r0.w = r0.w * r3.w;
  r0.w = r0.w * r4.x;
  r3.w = (int)r4.w | (int)r4.z;
  r0.w = r2.w * r0.w;
  r0.w = (int)r0.w & (int)r3.w;
  r0.w = r2.w * abs(r3.z) + r0.w;
  r2.w = 2.20000002e-005 * r3.y;
  r3.xyz = g_rayleigh_density.xyz * r3.xxx + r2.www;
  r3.xyz = g_fog_density_constant * r3.xyz + r0.www;
  r0.w = max(r3.x, r3.y);
  r0.w = max(r0.w, r3.z);
  r0.w = -1.44269502 * r0.w;
  r0.w = exp2(r0.w);
  r2.w = cmp(0 != g_campaign_map_size.x);
  if (r2.w != 0) {
    r2.w = t_shroud_clouds.SampleLevel(s_shroud_clouds_s, r0.xy, 0).w;
    r0.w = r2.w * r0.w;
  }
  r2.w = -0.999989986 + r5.z;
  r2.w = saturate(100000 * r2.w);
  r0.w = r2.w * r0.w;
  r3.xyz = SampleBloomSmooth(glow_texture1, r0.xy).xyz;
  r2.w = glow_strength.y + -glow_strength.x;
  r2.w = r0.w * r2.w + glow_strength.x;
  r3.xyz = r3.xyz * r2.www * glow_multiplier;
  r0.z = r0.z * r0.z;
  r3.xyz = r3.xyz * r0.zzz;
  r0.z = -r0.w * glow_depth_masking + 1;
  r2.xyz = r3.xyz * r0.zzz + r2.xyz;
  r5.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r0.x = dot(r5.xyzw, inv_projection._m02_m12_m22_m32);
  r0.y = dot(r5.xyzw, inv_projection._m03_m13_m23_m33);
  r0.x = r0.x / r0.y;
  r2.w = min(scene_max_distance, r0.x);
  r2.w = clamp(r2.w, 0.f, kMaxFiniteDepth);
  r2.w = (r2.w == r2.w) ? r2.w : kMaxFiniteDepth;
  r2.xyz = max(r2.xyz, 0.f);
  r2.xyz = (r2.xyz == r2.xyz) ? r2.xyz : 0.f;
  frame_texture_rw[frame_pixel] = r2.xyzw;  // manual fix: store_uav_typed
  return;
}