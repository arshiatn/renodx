// pointlightlinearfalloff ps40_skinlm (the point lights: torches and fires), the game's 30261389 (3Dmigoto decompile) as
// fire02_0xE318CE77: Torch and Fire Shadows (twattila/rt_scene.hpp InstallPointLights):
// x the light's traced visibility at its position (POSITION2.xyz; nothing bound: 1);
// its NDC over the draw's share below 100% (the game's offset / scale stays its 100% one). With nothing bound the
// game's result: WARP tools\tw\testbin\tw2_ point light ps_compare.
#include "../shared.h"
#include "../../twattila/rt_local_lights.hlsli"
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

cbuffer dynamic_light_PS : register(b2)
{
  float4 g_viewport_offset_scale : packoffset(c0);
}

Texture2D<float4> gbuffer_channel_1_texture : register(t0);
Texture2D<float4> gbuffer_channel_2_texture : register(t1);
Texture2D<float4> gbuffer_channel_3_texture : register(t2);
Texture2D<float4> gbuffer_channel_4_texture : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  float4 v4 : POSITION3,
  float4 v5 : POSITION4,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  gbuffer_channel_1_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (uint2)r1.xy;
  r0.zw = r1.zw * r0.zw;
  r0.zw = floor(r0.zw);
  r0.zw = (int2)r0.zw;
  r1.xy = (int2)r1.xy + int2(-1,-1);
  r0.zw = max(int2(0,0), (int2)r0.zw);
  r1.xy = min((int2)r0.zw, (int2)r1.xy);
  r1.zw = float2(0,0);
  r2.xyz = gbuffer_channel_1_texture.Load(r1.xyw).xyz;
  r3.xyz = gbuffer_channel_2_texture.Load(r1.xyw).xyz;
  r0.z = gbuffer_channel_3_texture.Load(r1.xyw).z;
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r2.xy = float2(1023,1023) * r2.xy;
  r2.xy = round(r2.xy);
  r2.xy = (uint2)r2.xy;
  r2.xy = (uint2)r2.xy >> int2(2,2);
  r2.xy = (uint2)r2.xy;
  r2.xy = r2.xy * float2(0.00784313772,0.00784313772) + float2(-1,-1);
  r0.w = dot(r2.xy, r2.xy);
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r4.y = r0.w * 2 + -1;
  r2.xy = r2.xy + r2.xy;
  r0.w = sqrt(r0.w);
  r4.xz = r2.xy * r0.ww;
  r0.xy = r0.xy * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.xy = r0.xy / g_viewport_offset_scale.zw;
  r1.xy = (r0.xy / DrawRenderShare()) * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r5.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r5.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r5.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.x = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyw = r5.xyz / r0.xxx;
  r1.xyz = v3.xyz + -r0.xyw;
  r1.w = dot(r1.xyz, r1.xyz);
  r2.x = rsqrt(r1.w);
  r5.xyz = r2.xxx * r1.xyz;
  r1.w = sqrt(r1.w);
  r1.w = saturate(r1.w / v3.w);
  r1.w = 1 + -r1.w;
  r2.y = dot(r4.xyz, r5.xyz);
  r2.w = max(0, r2.y);
  r2.y = cmp(r2.y >= 0);
  if (r2.y != 0) {
    r2.y = 1023 * r2.z;
    r2.y = round(r2.y);
    r2.y = (uint)r2.y;
    r2.y = (uint)r2.y >> 2;
    r2.y = (uint)r2.y;
    r6.xyz = -camera_position.xyz + r0.xyw;
    r0.y = dot(r6.xyz, r6.xyz);
    r0.y = rsqrt(r0.y);
    r6.xyz = r6.xyz * r0.yyy;
    r0.y = dot(r6.xyz, r4.xyz);
    r2.z = r0.y + r0.y;
    r4.xyz = r4.xyz * -r2.zzz + r6.xyz;
    r2.y = -r2.y * 0.00392156886 + 1;
    r2.z = r2.y * r2.y;
    r3.w = r2.z * r2.y;
    r4.w = r3.w * r2.y;
    r5.w = r4.w * r2.y;
    r4.w = 64.5780029 * r4.w;
    r4.w = r5.w * 28.0900002 + -r4.w;
    r3.w = r3.w * 48.6290016 + r4.w;
    r2.z = -r2.z * 12.6590004 + r3.w;
    r2.y = saturate(r2.y * 1.54589999 + r2.z);
    r2.y = 1 + -r2.y;
    r2.z = dot(r5.xyz, r4.xyz);
    r2.z = max(-1, r2.z);
    r2.z = min(1, r2.z);
    r3.w = 1 + -abs(r2.z);
    r3.w = sqrt(r3.w);
    r4.x = abs(r2.z) * -0.0187292993 + 0.0742610022;
    r4.x = r4.x * abs(r2.z) + -0.212114394;
    r4.x = r4.x * abs(r2.z) + 1.57072878;
    r4.y = r4.x * r3.w;
    r4.y = r4.y * -2 + 3.14159274;
    r2.z = cmp(r2.z < -r2.z);
    r2.z = r2.z ? r4.y : 0;
    r2.z = r4.x * r3.w + r2.z;
    r3.w = r2.y * -1.12524891 + 1.12839997;
    r4.xyz = float3(-0.00872664619,0.00872664619,0.00872664619) + r2.zzz;
    r2.z = 1 / r3.w;
    r4.xyz = r4.xyz * r2.zzz;
    r4.yw = float2(0.707106769,0.707106769) * r4.xy;
    r3.w = abs(r4.y) * abs(r4.y);
    r5.x = r3.w * abs(r4.y);
    r5.y = r5.x * abs(r4.y);
    r4.y = abs(r4.y) * 0.278393 + 1;
    r3.w = r3.w * 0.230388999 + r4.y;
    r3.w = r5.x * 0.000972000009 + r3.w;
    r3.w = r5.y * 0.0781079978 + r3.w;
    r3.w = r3.w * r3.w;
    r3.w = r3.w * r3.w;
    r3.w = 1 / r3.w;
    r3.w = 1 + -r3.w;
    r4.y = cmp(0 < r4.x);
    r4.x = cmp(r4.x < 0);
    r4.x = (int)-r4.y + (int)r4.x;
    r4.x = (int)r4.x;
    r3.w = r4.x * r3.w + 1;
    r3.w = 0.5 * r3.w;
    r4.x = r4.w * r4.w;
    r4.y = r4.x * r4.w;
    r4.w = r4.y * r4.w;
    r4.z = r4.z * 0.196853578 + 1;
    r4.x = r4.x * 0.230388999 + r4.z;
    r4.x = r4.y * 0.000972000009 + r4.x;
    r4.x = r4.w * 0.0781079978 + r4.x;
    r4.x = r4.x * r4.x;
    r4.x = r4.x * r4.x;
    r4.x = 1 / r4.x;
    r4.x = 2 + -r4.x;
    r3.w = r4.x * 0.5 + -r3.w;
    r4.xy = float2(-1.11072075,1.11072075) * r2.zz;
    r4.zw = r4.xy * r4.xy;
    r5.x = r4.z * -r4.x;
    r4.x = r5.x * -r4.x;
    r2.z = r2.z * 0.309216887 + 1;
    r5.yz = r4.zw * float2(0.230388999,0.230388999) + r2.zz;
    r2.z = r5.x * 0.000972000009 + r5.y;
    r2.z = r4.x * 0.0781079978 + r2.z;
    r2.z = r2.z * r2.z;
    r2.z = r2.z * r2.z;
    r2.z = 1 / r2.z;
    r2.yz = float2(1,1) + -r2.yz;
    r2.z = 1 + -r2.z;
    r2.z = 0.5 * r2.z;
    r4.x = r4.w * r4.y;
    r4.y = r4.x * r4.y;
    r4.x = r4.x * 0.000972000009 + r5.z;
    r4.x = r4.y * 0.0781079978 + r4.x;
    r4.x = r4.x * r4.x;
    r4.x = r4.x * r4.x;
    r4.x = 1 / r4.x;
    r4.x = 2 + -r4.x;
    r2.z = r4.x * 0.5 + -r2.z;
    r2.z = abs(r3.w) / r2.z;
    r2.z = min(1, r2.z);
    r2.z = r2.z * r2.z;
    r2.z = 0.785398185 * r2.z;
    r3.w = min(1, r2.w);
    r4.x = 1.57079637 * r2.y;
    r4.x = sin(r4.x);
    r4.y = -1 + r3.w;
    r4.x = r4.x * r4.y + 1;
    r0.y = saturate(r0.y);
    r2.y = r2.y * r2.y;
    r4.y = 0.5 * r2.y;
    r2.y = -r2.y * 0.5 + 1;
    r3.w = r3.w * r2.y + r4.y;
    r3.w = 1 / r3.w;
    r0.y = r0.y * r2.y + r4.y;
    r0.y = 1 / r0.y;
    r0.y = r3.w * r0.y;
    r0.y = 0.25 * r0.y;
    r0.y = min(1, r0.y);
    r0.y = r0.y * r4.x;
    r1.xyz = r1.xyz * r2.xxx + -r6.xyz;
    r2.x = dot(r1.xyz, r1.xyz);
    r2.x = rsqrt(r2.x);
    r1.xyz = r2.xxx * r1.xyz;
    r1.x = saturate(dot(-r6.xyz, r1.xyz));
    r1.y = r1.x * -5.55472994 + -6.98316002;
    r1.x = r1.y * r1.x;
    r1.x = exp2(r1.x);
    r1.y = saturate(10 * r0.z);
    r1.x = r1.x * r1.y;
    r1.y = 1 + -r0.z;
    r1.x = r1.y * r1.x + r0.z;
    r0.y = r2.z * r0.y;
    r1.xyz = r0.yyy * r1.xxx;
  } else {
    r1.xyz = float3(0,0,0);
  }
  r2.xyz = v2.xyz * r1.xyz;
  r1.xyz = max(r1.xyz, r0.zzz);
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r3.xyz = v2.xyz * r3.xyz;
  r1.xyz = r3.xyz * r1.xyz;
  r1.xyz = r1.xyz * r2.www;
  r1.xyz = g_debug_light_diffuse_coef * r1.xyz;
  r1.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r1.xyz;
  r0.y = saturate(1 + g_debug_light_shadow_coef);
  r0.z = g_aristeia_effect.x * g_aristeia_effect.x;
  r0.xw = -g_aristeia_pos.xy + r0.xw;
  r0.x = dot(r0.xw, r0.xw);
  r0.x = r0.x / r0.z;
  r0.x = min(1, r0.x);
  r0.z = g_aristeia_effect.w + -g_aristeia_effect.z;
  r0.x = r0.x * r0.z + g_aristeia_effect.z;
  r0.x = -1 + r0.x;
  r0.z = g_aristeia_effect.y * r0.x;
  r0.x = g_aristeia_effect.y * r0.x + 1;
  r0.z = saturate(1.5 * r0.z);
  r0.w = 1 + -r0.y;
  r0.y = r0.z * r0.w + r0.y;
  r0.x = r0.x * r0.y;
  r0.yzw = r2.xyz * g_debug_light_specular_coef + r1.xyz;
  r0.xyz = r0.yzw * r0.xxx;
  r0.xyz = r1.www * r0.xyz;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  if (HDR >= 0.5f) o0.xyz *= VfxFireBrightness;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  o0.w = 1;
  return;
}