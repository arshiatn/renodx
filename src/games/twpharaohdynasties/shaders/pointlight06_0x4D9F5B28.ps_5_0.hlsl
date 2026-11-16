// pointlightlinearfalloffshadow ps40_slm (the point lights: torches and fires), the game's 4D9F5B28 (3Dmigoto decompile) as
// fire02_0xE318CE77: Torch and Fire Shadows (twattila/rt_scene.hpp InstallPointLights):
// the rays' shadow in place of the point shadow map's where they trace the light at the pixel, the game's map
// where they don't (twattila/rt_local_lights.hlsli RtLocalLightTraced);
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

cbuffer pointlight_shadowmap_cb : register(b3)
{
  float4 g_shadow_buffer_size : packoffset(c0);
  float g_shadow_sample_bias : packoffset(c1);
  float g_shadow_soft_edges : packoffset(c1.y);
  float2 g_light_proj_zw : packoffset(c1.z);
  float g_light_width : packoffset(c2);
  float g_point_light_shadow_map_sample_bias : packoffset(c2.y);
}

SamplerState s_sheen_lut_s : register(s0);
SamplerState s_point_light_shadow_map_s : register(s2);
SamplerComparisonState s_point_light_shadow_map_sm50_s : register(s1);
Texture2D<float4> gbuffer_channel_1_texture : register(t0);
Texture2D<float4> gbuffer_channel_2_texture : register(t1);
Texture2D<float4> gbuffer_channel_3_texture : register(t2);
Texture2D<float4> gbuffer_channel_4_texture : register(t3);
Texture2D<float4> t_sheen_lut : register(t4);
Texture2DArray<float> t_point_light_shadow_map : register(t5);


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
  const float4 icb[] = { { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 1.000000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20,r21;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(0 != g_shadow_soft_edges);
  r0.yz = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r0.yz;
  gbuffer_channel_1_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.zw = uiDest.xy;
  r2.xy = (uint2)r1.zw;
  r1.xy = r2.xy * r1.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r1.zw = (int2)r1.zw + int2(-1,-1);
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r1.zw);
  r1.zw = float2(0,0);
  r2.xyz = gbuffer_channel_1_texture.Load(r1.xyw).xyz;
  r3.xyz = gbuffer_channel_2_texture.Load(r1.xyw).xyz;
  r4.xyzw = gbuffer_channel_3_texture.Load(r1.xyw).zwxy;
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
  r0.w = dot(r2.xy, r2.xy);
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r5.y = r0.w * 2 + -1;
  r2.xy = r2.xy + r2.xy;
  r0.w = sqrt(r0.w);
  r5.xz = r2.xy * r0.ww;
  r0.w = 255 * r4.y;
  r0.w = round(r0.w);
  r0.w = (uint)r0.w;
  r6.xyzw = (int4)r0.wwww & int4(128,2,10,32);
  r2.x = r4.x;
  r2.y = 0;
  r4.xy = r6.xx ? r4.zx : r2.xy;
  r0.yz = r0.yz * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.yz = r0.yz / g_viewport_offset_scale.zw;
  r1.xy = (r0.yz / DrawRenderShare()) * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r7.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r7.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r7.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.y = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.yzw = r7.xyz / r0.yyy;
  r1.xyz = -v3.xyz + r0.yzw;
  // The light's shadow: the rays' where they trace it, else the game's point shadow map (only then sampled).
  float traced_visibility;
  [branch]
  if (RtLocalLightTraced(int2(v0.xy), v3.xyz, traced_visibility)) {
    r7.x = traced_visibility;
  } else {
    r1.xyz = g_point_light_shadow_map_sample_bias * r1.xyz;
    r1.w = max(abs(r1.x), abs(r1.y));
    r1.w = max(r1.w, abs(r1.z));
    r1.w = g_light_proj_zw.y / r1.w;
    r1.w = g_light_proj_zw.x + r1.w;
    r2.x = dot(r1.xyz, r1.xyz);
    r2.x = rsqrt(r2.x);
    r1.xyz = r2.xxx * r1.xyz;
    r7.xzw = -r1.zxy;
    r8.xyzw = cmp(abs(r1.yzxz) < abs(r1.xxyy));
    r2.xyw = cmp(float3(0,0,0) < r1.xyz);
    r7.y = 0;
    r9.xw = float2(1,2);
    r9.yz = float2(1,-1) * r1.zy;
    r10.xzw = r2.xxx ? r7.yxw : r9.xyz;
    r8.xy = r8.yw ? r8.xz : 0;
    r11.x = 3;
    r11.w = r7.x;
    r9.xw = r2.yy ? r9.wy : r11.xw;
    r2.xy = cmp(abs(r1.yx) < abs(r1.zz));
    r2.x = r2.y ? r2.x : 0;
    r11.x = 4;
    r11.zw = float2(1,-1) * r1.xy;
    r7.x = 5;
    r7.xzw = r2.www ? r11.xzw : r7.xzw;
    r7.y = abs(r1.z);
    r7.xyzw = r2.xxxx ? r7.xyzw : float4(0,1,0,0);
    r9.y = abs(r1.y);
    r9.z = r11.z;
    r7.xyzw = r8.yyyy ? r9.xyzw : r7.xyzw;
    r10.y = abs(r1.x);
    r7.xyzw = r8.xxxx ? r10.xyzw : r7.xyzw;
    r1.xy = r7.zw / r7.yy;
    r1.xy = float2(1,1) + r1.xy;
    r1.xy = float2(0.5,0.5) * r1.xy;
    r7.w = (int)r7.x;
    r2.x = cmp((int)r0.x == -1);
    if (r2.x != 0) {
      r2.xy = g_shadow_buffer_size.xy * r1.xy + float2(0.5,0.5);
      r8.xy = floor(r2.xy);
      r2.xy = -r8.xy + r2.xy;
      r8.xy = -r2.xy * g_shadow_buffer_size.zw + r1.xy;
      r9.y = -3 * g_shadow_buffer_size.z;
      r9.xw = float2(0,0);
      r2.w = -3;
      while (true) {
        r3.w = cmp(3 < (int)r2.w);
        if (r3.w != 0) break;
        r3.w = (int)r2.w;
        r9.z = g_shadow_buffer_size.w * r3.w;
        r7.yz = r9.yz + r8.xy;
        r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r7.yzw).xyzw;
        r11.xyzw = cmp(r10.xyzw >= r1.wwww);
        r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r3.w = dot(r11.xyzw, float4(1,1,1,1));
        r5.w = dot(r10.xyzw, r11.xyzw);
        r7.x = g_shadow_buffer_size.z * 2 + r7.y;
        r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r7.xzw).xyzw;
        r11.xyzw = cmp(r10.xyzw >= r1.wwww);
        r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r6.x = dot(r11.xyzw, float4(1,1,1,1));
        r3.w = r6.x + r3.w;
        r6.x = dot(r10.xyzw, r11.xyzw);
        r5.w = r6.x + r5.w;
        r10.y = g_shadow_buffer_size.z * 2 + r7.x;
        r10.zw = r7.zw;
        r11.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r10.yzw).xyzw;
        r12.xyzw = cmp(r11.xyzw >= r1.wwww);
        r12.xyzw = r12.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r6.x = dot(r12.xyzw, float4(1,1,1,1));
        r3.w = r6.x + r3.w;
        r6.x = dot(r11.xyzw, r12.xyzw);
        r5.w = r6.x + r5.w;
        r10.x = g_shadow_buffer_size.z * 2 + r10.y;
        r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r10.xzw).xyzw;
        r11.xyzw = cmp(r10.xyzw >= r1.wwww);
        r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r6.x = dot(r11.xyzw, float4(1,1,1,1));
        r3.w = r6.x + r3.w;
        r6.x = dot(r10.xyzw, r11.xyzw);
        r5.w = r6.x + r5.w;
        r9.w = r9.w + r3.w;
        r9.x = r9.x + r5.w;
        r2.w = (int)r2.w + 2;
      }
      r2.w = cmp(r9.w < 64);
      if (r2.w != 0) {
        r2.w = cmp(0 < r9.w);
        r3.w = r9.x / r9.w;
        r3.w = r1.w / r3.w;
        r3.w = -1 + r3.w;
        r7.y = saturate(g_light_width * r3.w);
        r7.x = 0;
        r7.xy = r2.ww ? r7.xy : float2(1,0);
        if (r2.w != 0) {
          r2.w = r7.y * r7.y;
          r3.w = r2.w * r7.y;
          r5.w = 3 * r2.w;
          r6.x = 1 + -r7.y;
          r7.z = r6.x * r5.w;
          r8.w = r6.x * r6.x;
          r9.x = r8.w * r7.y;
          r9.y = 3 * r9.x;
          r9.z = r8.w * r6.x;
          r9.w = 0;
          r10.x = 0;
          while (true) {
            r10.y = cmp((int)r9.w >= 11);
            if (r10.y != 0) break;
            r10.y = r2.w * r7.y + r10.x;
            r10.z = (int)r9.w * 11;
            r10.w = r7.z * icb[r10.z+1].x + r3.w;
            r10.y = r10.y + r10.w;
            r10.w = icb[r10.z+2].x * r7.z;
            r10.w = r9.y * icb[r10.z+2].y + r10.w;
            r10.w = r2.w * r7.y + r10.w;
            r10.y = r10.y + r10.w;
            r10.w = icb[r10.z+3].x * r7.z;
            r10.w = r9.y * icb[r10.z+3].y + r10.w;
            r10.w = r2.w * r7.y + r10.w;
            r10.y = r10.y + r10.w;
            r10.w = icb[r10.z+4].y * r9.y;
            r10.w = r9.z * icb[r10.z+4].z + r10.w;
            r10.w = r7.z * icb[r10.z+4].x + r10.w;
            r10.w = r2.w * r7.y + r10.w;
            r10.y = r10.y + r10.w;
            r10.w = icb[r10.z+5].y * r9.y;
            r10.w = r9.z * icb[r10.z+5].z + r10.w;
            r10.w = r7.z * icb[r10.z+5].x + r10.w;
            r10.w = r2.w * r7.y + r10.w;
            r10.y = r10.y + r10.w;
            r10.w = icb[r10.z+6].y * r9.y;
            r10.w = r9.z * icb[r10.z+6].z + r10.w;
            r10.w = r7.z * icb[r10.z+6].x + r10.w;
            r10.w = r2.w * r7.y + r10.w;
            r10.y = r10.y + r10.w;
            r10.w = icb[r10.z+7].x * r7.z;
            r10.w = r9.y * icb[r10.z+7].y + r10.w;
            r10.w = r2.w * r7.y + r10.w;
            r10.y = r10.y + r10.w;
            r10.w = icb[r10.z+8].x * r7.z;
            r10.w = r9.y * icb[r10.z+8].y + r10.w;
            r10.w = r2.w * r7.y + r10.w;
            r10.y = r10.y + r10.w;
            r10.z = r7.z * icb[r10.z+9].x + r3.w;
            r10.y = r10.y + r10.z;
            r10.x = r2.w * r7.y + r10.y;
            r9.w = (int)r9.w + 1;
          }
          r8.z = r7.w;
          r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-5,-5)).xyzw;
          r9.w = 1 + -r2.y;
          r10.y = -r3.w * r2.x + r3.w;
          r10.zw = r11.wx * r10.yy;
          r11.xz = r11.zy * r3.ww + r10.zw;
          r10.z = r11.z * r2.y;
          r10.z = r9.w * r11.x + r10.z;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-3,-5)).xyzw;
          r11.x = dot(r12.wz, r3.ww);
          r10.z = r9.w * r11.x + r10.z;
          r11.x = dot(r12.xy, r3.ww);
          r10.z = r2.y * r11.x + r10.z;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-1,-5)).xyzw;
          r11.x = dot(r13.wz, r3.ww);
          r10.z = r9.w * r11.x + r10.z;
          r11.x = dot(r13.xy, r3.ww);
          r10.z = r2.y * r11.x + r10.z;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(1,-5)).xyzw;
          r11.x = dot(r14.wz, r3.ww);
          r10.z = r9.w * r11.x + r10.z;
          r11.x = dot(r14.xy, r3.ww);
          r10.z = r2.y * r11.x + r10.z;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(3,-5)).xyzw;
          r11.x = dot(r15.wz, r3.ww);
          r10.z = r9.w * r11.x + r10.z;
          r11.x = dot(r15.xy, r3.ww);
          r10.z = r2.y * r11.x + r10.z;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(5,-5)).xyzw;
          r11.xz = r16.zy * r2.xx;
          r11.xz = r11.xz * r3.ww;
          r11.xw = r16.wx * r3.ww + r11.xz;
          r10.z = r9.w * r11.x + r10.z;
          r10.z = r2.y * r11.w + r10.z;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-5,-3)).xyzw;
          r11.x = 0.200000003 * r7.z;
          r11.w = r7.z * 0.200000003 + r3.w;
          r12.z = r2.w * r7.y + -r11.w;
          r12.z = r2.x * r12.z + r11.w;
          r13.zw = r17.yz * r12.zz;
          r13.zw = r17.xw * r10.yy + r13.zw;
          r10.z = r9.w * r13.w + r10.z;
          r10.z = r2.y * r13.z + r10.z;
          r10.w = r11.y * r12.z + r10.w;
          r10.z = r9.w * r10.w + r10.z;
          r10.z = r2.y * r13.w + r10.z;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-3,-3)).xyzw;
          r14.zw = r9.xx * float2(3,0.600000024) + r7.zz;
          r14.zw = r2.ww * r7.yy + r14.zw;
          r7.z = -r14.w + r11.w;
          r7.z = r2.x * r7.z + r14.w;
          r15.zw = r17.wx * r7.zz;
          r16.yz = r17.zy * r14.ww + r15.zw;
          r9.x = r9.w * r16.y + r10.z;
          r9.x = r2.y * r16.z + r9.x;
          r10.z = dot(r12.xy, r11.ww);
          r9.x = r9.w * r10.z + r9.x;
          r10.z = dot(r17.wz, r11.ww);
          r9.x = r2.y * r10.z + r9.x;
          r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-1,-3)).xyzw;
          r10.z = dot(r18.wz, r14.ww);
          r9.x = r9.w * r10.z + r9.x;
          r10.z = dot(r18.xy, r14.ww);
          r9.x = r2.y * r10.z + r9.x;
          r10.z = dot(r13.xy, r11.ww);
          r9.x = r9.w * r10.z + r9.x;
          r10.z = dot(r18.wz, r11.ww);
          r9.x = r2.y * r10.z + r9.x;
          r19.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(1,-3)).xyzw;
          r10.z = dot(r19.wz, r14.ww);
          r9.x = r9.w * r10.z + r9.x;
          r10.z = dot(r19.xy, r14.ww);
          r9.x = r2.y * r10.z + r9.x;
          r10.z = dot(r14.xy, r11.ww);
          r9.x = r9.w * r10.z + r9.x;
          r10.z = dot(r19.wz, r11.ww);
          r9.x = r2.y * r10.z + r9.x;
          r20.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(3,-3)).xyzw;
          r10.z = r14.w + -r11.w;
          r10.z = r2.x * r10.z + r11.w;
          r12.xy = r20.zy * r10.zz;
          r12.xw = r20.wx * r14.ww + r12.xy;
          r9.x = r9.w * r12.x + r9.x;
          r9.x = r2.y * r12.w + r9.x;
          r10.w = dot(r15.xy, r11.ww);
          r9.x = r9.w * r10.w + r9.x;
          r10.w = dot(r20.wz, r11.ww);
          r9.x = r2.y * r10.w + r9.x;
          r21.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(5,-3)).xyzw;
          r10.w = r2.x * r11.x + r3.w;
          r11.xy = r21.yz * r2.xx;
          r11.xy = r11.xy * r3.ww;
          r11.xy = r21.xw * r10.ww + r11.xy;
          r9.x = r9.w * r11.y + r9.x;
          r9.x = r2.y * r11.x + r9.x;
          r11.z = r16.x * r10.w + r11.z;
          r9.x = r9.w * r11.z + r9.x;
          r9.x = r2.y * r11.y + r9.x;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-5,-1)).xyzw;
          r11.yz = r16.yz * r12.zz;
          r11.yz = r16.xw * r10.yy + r11.yz;
          r9.x = r9.w * r11.z + r9.x;
          r9.x = r2.y * r11.y + r9.x;
          r9.x = r9.w * r13.z + r9.x;
          r9.x = r2.y * r11.z + r9.x;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-3,-1)).xyzw;
          r12.xw = r14.wz + -r14.zw;
          r12.xw = r2.xx * r12.xw + r14.zw;
          r13.yz = r13.yz * r12.xx;
          r13.xy = r13.xw * r7.zz + r13.yz;
          r9.x = r9.w * r13.y + r9.x;
          r9.x = r2.y * r13.x + r9.x;
          r11.z = r17.y * r12.x + r15.w;
          r9.x = r9.w * r11.z + r9.x;
          r9.x = r2.y * r13.y + r9.x;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-1,-1)).xyzw;
          r9.z = r9.z * 0.800000012 + r9.y;
          r9.z = r5.w * r6.x + r9.z;
          r9.z = r2.w * r7.y + r9.z;
          r11.z = r14.z + -r9.z;
          r11.z = r2.x * r11.z + r9.z;
          r13.yz = r15.wx * r11.zz;
          r13.yw = r15.zy * r9.zz + r13.yz;
          r9.x = r9.w * r13.y + r9.x;
          r9.x = r2.y * r13.w + r9.x;
          r13.y = dot(r18.xy, r14.zz);
          r9.x = r9.w * r13.y + r9.x;
          r13.y = dot(r15.wz, r14.zz);
          r9.x = r2.y * r13.y + r9.x;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(1,-1)).xyzw;
          r13.y = r9.z + -r14.z;
          r13.y = r2.x * r13.y + r14.z;
          r14.xy = r16.zy * r13.yy;
          r15.xz = r16.wx * r9.zz + r14.xy;
          r9.x = r9.w * r15.x + r9.x;
          r9.x = r2.y * r15.z + r9.x;
          r13.w = dot(r19.xy, r14.zz);
          r9.x = r9.w * r13.w + r9.x;
          r13.w = dot(r16.wz, r14.zz);
          r9.x = r2.y * r13.w + r9.x;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(3,-1)).xyzw;
          r15.xz = r17.yz * r10.zz;
          r15.xz = r17.xw * r12.ww + r15.xz;
          r9.x = r9.w * r15.z + r9.x;
          r9.x = r2.y * r15.x + r9.x;
          r12.y = r20.x * r12.w + r12.y;
          r9.x = r9.w * r12.y + r9.x;
          r9.x = r2.y * r15.z + r9.x;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(5,-1)).xyzw;
          r15.zw = r17.yz * r2.xx;
          r15.zw = r15.zw * r3.ww;
          r15.zw = r17.xw * r10.ww + r15.zw;
          r9.x = r9.w * r15.w + r9.x;
          r9.x = r2.y * r15.z + r9.x;
          r9.x = r9.w * r11.x + r9.x;
          r9.x = r2.y * r15.w + r9.x;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-5,1)).xyzw;
          r16.yz = r17.yz * r12.zz;
          r16.yz = r17.xw * r10.yy + r16.yz;
          r9.x = r9.w * r16.z + r9.x;
          r9.x = r2.y * r16.y + r9.x;
          r9.x = r9.w * r11.y + r9.x;
          r9.x = r2.y * r16.z + r9.x;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-3,1)).xyzw;
          r11.xy = r17.yz * r12.xx;
          r11.xy = r17.xw * r7.zz + r11.xy;
          r9.x = r9.w * r11.y + r9.x;
          r9.x = r2.y * r11.x + r9.x;
          r9.x = r9.w * r13.x + r9.x;
          r9.x = r2.y * r11.y + r9.x;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-1,1)).xyzw;
          r11.yz = r17.wx * r11.zz;
          r13.xw = r17.zy * r9.zz + r11.yz;
          r9.x = r9.w * r13.x + r9.x;
          r9.x = r2.y * r13.w + r9.x;
          r8.w = r8.w * r6.x + r9.y;
          r5.w = r5.w * r6.x + r8.w;
          r2.w = r2.w * r7.y + r5.w;
          r5.w = r9.z + -r2.w;
          r5.w = r2.x * r5.w + r2.w;
          r6.x = r15.y * r5.w + r13.z;
          r6.x = r9.w * r6.x + r9.x;
          r5.w = r17.z * r5.w + r11.y;
          r5.w = r2.y * r5.w + r6.x;
          r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(1,1)).xyzw;
          r9.xy = r18.zy * r13.yy;
          r11.yz = r18.wx * r9.zz + r9.xy;
          r5.w = r9.w * r11.y + r5.w;
          r5.w = r2.y * r11.z + r5.w;
          r2.w = r2.w + -r9.z;
          r2.w = r2.x * r2.w + r9.z;
          r6.x = r16.x * r2.w + r14.y;
          r5.w = r9.w * r6.x + r5.w;
          r2.w = r18.w * r2.w + r9.x;
          r2.w = r2.y * r2.w + r5.w;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(3,1)).xyzw;
          r9.xy = r13.yz * r10.zz;
          r9.xy = r13.xw * r12.ww + r9.xy;
          r2.w = r9.w * r9.y + r2.w;
          r2.w = r2.y * r9.x + r2.w;
          r2.w = r9.w * r15.x + r2.w;
          r2.w = r2.y * r9.y + r2.w;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(5,1)).xyzw;
          r9.yz = r13.yz * r2.xx;
          r9.yz = r9.yz * r3.ww;
          r9.yz = r13.xw * r10.ww + r9.yz;
          r2.w = r9.w * r9.z + r2.w;
          r2.w = r2.y * r9.y + r2.w;
          r2.w = r9.w * r15.z + r2.w;
          r2.w = r2.y * r9.z + r2.w;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-5,3)).xyzw;
          r11.yz = r13.yz * r12.zz;
          r11.yz = r13.xw * r10.yy + r11.yz;
          r2.w = r9.w * r11.z + r2.w;
          r2.w = r2.y * r11.y + r2.w;
          r2.w = r9.w * r16.y + r2.w;
          r2.w = r2.y * r11.z + r2.w;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-3,3)).xyzw;
          r7.yz = r13.wx * r7.zz;
          r14.xy = r13.zy * r14.ww + r7.yz;
          r2.w = r9.w * r14.x + r2.w;
          r2.w = r2.y * r14.y + r2.w;
          r2.w = r9.w * r11.x + r2.w;
          r5.w = r13.z * r12.x + r7.y;
          r2.w = r2.y * r5.w + r2.w;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-1,3)).xyzw;
          r5.w = dot(r15.wz, r14.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r15.xy, r14.ww);
          r2.w = r2.y * r5.w + r2.w;
          r5.w = dot(r17.xy, r14.zz);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r15.wz, r14.zz);
          r2.w = r2.y * r5.w + r2.w;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(1,3)).xyzw;
          r5.w = dot(r16.wz, r14.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r16.xy, r14.ww);
          r2.w = r2.y * r5.w + r2.w;
          r5.w = dot(r18.xy, r14.zz);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r16.wz, r14.zz);
          r2.w = r2.y * r5.w + r2.w;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(3,3)).xyzw;
          r7.yz = r17.zy * r10.zz;
          r11.xz = r17.wx * r14.ww + r7.yz;
          r2.w = r9.w * r11.x + r2.w;
          r2.w = r2.y * r11.z + r2.w;
          r2.w = r9.w * r9.x + r2.w;
          r5.w = r17.w * r12.w + r7.y;
          r2.w = r2.y * r5.w + r2.w;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(5,3)).xyzw;
          r7.yz = r14.yz * r2.xx;
          r7.yz = r7.yz * r3.ww;
          r7.yz = r14.xw * r10.ww + r7.yz;
          r2.w = r9.w * r7.z + r2.w;
          r2.w = r2.y * r7.y + r2.w;
          r2.w = r9.w * r9.y + r2.w;
          r2.w = r2.y * r7.z + r2.w;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-5,5)).xyzw;
          r9.xy = r14.wx * r10.yy;
          r9.yz = r14.zy * r3.ww + r9.xy;
          r2.w = r9.w * r9.y + r2.w;
          r2.w = r2.y * r9.z + r2.w;
          r2.w = r9.w * r11.y + r2.w;
          r5.w = r14.z * r12.z + r9.x;
          r2.w = r2.y * r5.w + r2.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-3,5)).xyzw;
          r5.w = dot(r12.wz, r3.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r12.xy, r3.ww);
          r2.w = r2.y * r5.w + r2.w;
          r5.w = dot(r13.xy, r11.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r12.wz, r11.ww);
          r2.w = r2.y * r5.w + r2.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(-1,5)).xyzw;
          r5.w = dot(r12.wz, r3.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r12.xy, r3.ww);
          r2.w = r2.y * r5.w + r2.w;
          r5.w = dot(r15.xy, r11.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r12.wz, r11.ww);
          r2.w = r2.y * r5.w + r2.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(1,5)).xyzw;
          r5.w = dot(r12.wz, r3.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r12.xy, r3.ww);
          r2.w = r2.y * r5.w + r2.w;
          r5.w = dot(r16.xy, r11.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r12.wz, r11.ww);
          r2.w = r2.y * r5.w + r2.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(3,5)).xyzw;
          r5.w = dot(r12.wz, r3.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r12.xy, r3.ww);
          r2.w = r2.y * r5.w + r2.w;
          r5.w = dot(r17.xy, r11.ww);
          r2.w = r9.w * r5.w + r2.w;
          r5.w = dot(r12.wz, r11.ww);
          r2.w = r2.y * r5.w + r2.w;
          r8.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r1.w, int2(5,5)).xyzw;
          r8.yz = r8.zy * r2.xx;
          r8.yz = r8.yz * r3.ww;
          r8.xz = r8.wx * r3.ww + r8.yz;
          r2.x = r9.w * r8.x + r2.w;
          r2.x = r2.y * r8.z + r2.x;
          r2.x = r9.w * r7.y + r2.x;
          r2.w = r8.w * r10.w + r8.y;
          r2.x = r2.y * r2.w + r2.x;
          r7.x = r2.x / r10.x;
        }
      } else {
        r7.x = 0;
      }
    } else {
      r7.x = 1;
    }
    if (r0.x == 0) {
      r1.z = r7.w;
      r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r1.xyz).x;
      r0.x = cmp(r1.w < r0.x);
      r7.x = r0.x ? 1.000000 : 0;
    }
  }
  r1.xyz = -camera_position.xyz + r0.yzw;
  r0.x = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r0.x);
  r1.xyz = r1.xyz * r1.www;
  r2.xyw = v3.xyz + -r0.yzw;
  r0.z = dot(r2.xyw, r2.xyw);
  r1.w = rsqrt(r0.z);
  r7.yzw = r2.xyw * r1.www;
  r0.z = sqrt(r0.z);
  r0.z = saturate(r0.z / v3.w);
  r0.z = 1 + -r0.z;
  r3.w = cmp(0 != v5.x);
  r5.w = dot(r7.yzw, v4.xyz);
  r5.w = saturate(r5.w * v5.x + v5.y);
  r5.w = r5.w * r0.z;
  r0.z = r3.w ? r5.w : r0.z;
  r6.xy = cmp((int2)r6.yz != int2(0,10));
  r3.w = r6.y ? r6.x : 0;
  r5.w = dot(r5.xyz, r7.yzw);
  r6.xyz = r5.xyz * r5.www + -r7.yzw;
  r5.w = dot(r6.xyz, r6.xyz);
  r5.w = rsqrt(r5.w);
  r6.xyz = r6.xyz * r5.www;
  r5.xyz = r3.www ? r6.xyz : r5.xyz;
  r3.w = dot(r5.xyz, r7.yzw);
  r5.w = max(0, r3.w);
  r6.x = cmp(r3.w >= 0);
  if (r6.x != 0) {
    r6.x = dot(r1.xyz, r5.xyz);
    r6.y = r6.x + r6.x;
    r8.xyz = r5.xyz * -r6.yyy + r1.xyz;
    r2.z = 1 + -r2.z;
    r6.y = r2.z * r2.z;
    r6.z = r6.y * r2.z;
    r8.w = r6.z * r2.z;
    r9.x = r8.w * r2.z;
    r8.w = 64.5780029 * r8.w;
    r8.w = r9.x * 28.0900002 + -r8.w;
    r6.z = r6.z * 48.6290016 + r8.w;
    r6.y = -r6.y * 12.6590004 + r6.z;
    r2.z = saturate(r2.z * 1.54589999 + r6.y);
    r2.z = 1 + -r2.z;
    r6.y = dot(r7.yzw, r8.xyz);
    r6.y = max(-1, r6.y);
    r6.y = min(1, r6.y);
    r6.z = 1 + -abs(r6.y);
    r6.z = sqrt(r6.z);
    r7.y = abs(r6.y) * -0.0187292993 + 0.0742610022;
    r7.y = r7.y * abs(r6.y) + -0.212114394;
    r7.y = r7.y * abs(r6.y) + 1.57072878;
    r7.z = r7.y * r6.z;
    r7.z = r7.z * -2 + 3.14159274;
    r6.y = cmp(r6.y < -r6.y);
    r6.y = r6.y ? r7.z : 0;
    r6.y = r7.y * r6.z + r6.y;
    r6.z = r2.z * -1.12524891 + 1.12839997;
    r7.yzw = float3(-0.00872664619,0.00872664619,0.00872664619) + r6.yyy;
    r6.y = 1 / r6.z;
    r7.yzw = r7.yzw * r6.yyy;
    r8.xy = float2(0.707106769,0.707106769) * r7.yz;
    r6.z = abs(r8.x) * abs(r8.x);
    r7.z = r6.z * abs(r8.x);
    r8.z = r7.z * abs(r8.x);
    r8.x = abs(r8.x) * 0.278393 + 1;
    r6.z = r6.z * 0.230388999 + r8.x;
    r6.z = r7.z * 0.000972000009 + r6.z;
    r6.z = r8.z * 0.0781079978 + r6.z;
    r6.z = r6.z * r6.z;
    r6.z = r6.z * r6.z;
    r6.z = 1 / r6.z;
    r6.z = 1 + -r6.z;
    r7.z = cmp(0 < r7.y);
    r7.y = cmp(r7.y < 0);
    r7.y = (int)-r7.z + (int)r7.y;
    r7.y = (int)r7.y;
    r6.z = r7.y * r6.z + 1;
    r6.z = 0.5 * r6.z;
    r7.y = r8.y * r8.y;
    r7.z = r7.y * r8.y;
    r8.x = r7.z * r8.y;
    r7.w = r7.w * 0.196853578 + 1;
    r7.y = r7.y * 0.230388999 + r7.w;
    r7.y = r7.z * 0.000972000009 + r7.y;
    r7.y = r8.x * 0.0781079978 + r7.y;
    r7.y = r7.y * r7.y;
    r7.y = r7.y * r7.y;
    r7.y = 1 / r7.y;
    r7.y = 2 + -r7.y;
    r6.z = r7.y * 0.5 + -r6.z;
    r7.yz = float2(-1.11072075,1.11072075) * r6.yy;
    r8.xy = r7.yz * r7.yz;
    r7.w = r8.x * -r7.y;
    r7.y = r7.w * -r7.y;
    r6.y = r6.y * 0.309216887 + 1;
    r8.xz = r8.xy * float2(0.230388999,0.230388999) + r6.yy;
    r6.y = r7.w * 0.000972000009 + r8.x;
    r6.y = r7.y * 0.0781079978 + r6.y;
    r6.y = r6.y * r6.y;
    r6.y = r6.y * r6.y;
    r6.y = 1 / r6.y;
    r6.y = 1 + -r6.y;
    r6.y = 1 + -r6.y;
    r6.y = 0.5 * r6.y;
    r7.y = r8.y * r7.z;
    r7.z = r7.y * r7.z;
    r7.y = r7.y * 0.000972000009 + r8.z;
    r7.y = r7.z * 0.0781079978 + r7.y;
    r7.y = r7.y * r7.y;
    r7.y = r7.y * r7.y;
    r7.y = 1 / r7.y;
    r7.y = 2 + -r7.y;
    r6.y = r7.y * 0.5 + -r6.y;
    r6.y = abs(r6.z) / r6.y;
    r6.y = min(1, r6.y);
    r6.y = r6.y * r6.y;
    r6.y = 0.785398185 * r6.y;
    r2.z = 1 + -r2.z;
    r6.z = min(1, r5.w);
    r7.y = 1.57079637 * r2.z;
    r7.y = sin(r7.y);
    r7.z = -1 + r6.z;
    r7.y = r7.y * r7.z + 1;
    r6.x = saturate(r6.x);
    r2.z = r2.z * r2.z;
    r7.z = 0.5 * r2.z;
    r2.z = -r2.z * 0.5 + 1;
    r6.z = r6.z * r2.z + r7.z;
    r6.z = 1 / r6.z;
    r2.z = r6.x * r2.z + r7.z;
    r2.z = 1 / r2.z;
    r2.z = r6.z * r2.z;
    r2.z = 0.25 * r2.z;
    r2.z = min(1, r2.z);
    r2.z = r2.z * r7.y;
    r2.xyw = r2.xyw * r1.www + -r1.xyz;
    r1.w = dot(r2.xyw, r2.xyw);
    r1.w = rsqrt(r1.w);
    r2.xyw = r2.xyw * r1.www;
    r1.w = saturate(dot(-r1.xyz, r2.xyw));
    r2.x = r1.w * -5.55472994 + -6.98316002;
    r1.w = r2.x * r1.w;
    r1.w = exp2(r1.w);
    r2.x = saturate(10 * r4.z);
    r1.w = r2.x * r1.w;
    r2.xyw = float3(1,1,1) + -r4.zwx;
    r2.xyw = r2.xyw * r1.www + r4.zwx;
    r1.w = r6.y * r2.z;
    r2.xyz = r1.www * r2.xyw;
  } else {
    r2.xyz = float3(0,0,0);
  }
  r6.xyz = v2.xyz * r2.xyz;
  r2.xyz = max(r4.zwx, r2.xyz);
  r2.xyz = float3(1,1,1) + -r2.xyz;
  r4.xzw = v2.xyz * r3.xyz;
  r2.xyz = r4.xzw * r2.xyz;
  if (r6.w != 0) {
    r4.xz = float2(-0.000144269507,0.00111111114) * r0.xx;
    r0.x = exp2(r4.x);
    r1.w = cmp(r5.z >= 0);
    r4.xw = r1.ww ? float2(1,-1) : float2(-1,1);
    r1.w = r4.x + r5.z;
    r1.w = -1 / r1.w;
    r7.yzw = r5.xxy * r5.yxy;
    r8.x = r7.y * r1.w;
    r2.w = r7.z * r4.x;
    r9.x = r2.w * r1.w + 1;
    r9.y = r8.x * r4.x;
    r9.z = r4.w * r5.x;
    r8.z = r7.w * r1.w + r4.x;
    r8.w = -r5.y;
    r10.z = dot(r9.xyz, -sun_direction.xyz);
    r10.x = dot(r8.xzw, -sun_direction.xyz);
    r10.y = dot(r5.xyz, -sun_direction.xyz);
    r1.w = dot(r9.xyz, -r1.xyz);
    r2.w = dot(r8.xzw, -r1.xyz);
    r1.x = dot(r5.xyz, -r1.xyz);
    r1.x = max(0, r1.x);
    r1.y = min(0.999999881, r1.x);
    r1.x = 0.800000012;
    r1.xyz = t_sheen_lut.SampleLevel(s_sheen_lut_s, r1.xy, 0).xyz;
    r1.xyz = r1.xyz * float3(0.879580379,0.183283925,0.749730527) + float3(0,-0.181325451,0);
    r4.x = min(abs(r2.w), abs(r1.w));
    r4.w = max(abs(r2.w), abs(r1.w));
    r4.w = 1 / r4.w;
    r4.x = r4.x * r4.w;
    r4.w = r4.x * r4.x;
    r5.x = r4.w * 0.0208350997 + -0.0851330012;
    r5.x = r4.w * r5.x + 0.180141002;
    r5.x = r4.w * r5.x + -0.330299497;
    r4.w = r4.w * r5.x + 0.999866009;
    r5.x = r4.x * r4.w;
    r5.y = cmp(abs(r1.w) < abs(r2.w));
    r5.x = r5.x * -2 + 1.57079637;
    r5.x = r5.y ? r5.x : 0;
    r4.x = r4.x * r4.w + r5.x;
    r4.w = cmp(r1.w < -r1.w);
    r4.w = r4.w ? -3.14159274 : 0;  // the game's l(0xc0490fdb): -pi to the bit (the decompile printed -3.141593, one ulp off)
    r4.x = r4.x + r4.w;
    r4.w = min(r2.w, r1.w);
    r1.w = max(r2.w, r1.w);
    r2.w = cmp(r4.w < -r4.w);
    r1.w = cmp(r1.w >= -r1.w);
    r1.w = r1.w ? r2.w : 0;
    r1.w = r1.w ? -r4.x : r4.x;
    r2.w = cmp(r1.w < 0);
    r4.x = 6.28318548 + r1.w;
    r1.w = r2.w ? r4.x : r1.w;
    sincos(-r1.w, r4.x, r5.x);
    r7.yzw = float3(0,0,1) * r10.yyy;
    r1.w = 1 + -r5.x;
    r7.yzw = r7.yzw * r1.www;
    r5.xyz = r10.zxy * r5.xxx + r7.yzw;
    r7.yzw = float3(1,0,0) * r10.xyz;
    r7.yzw = r10.yzx * float3(0,1,0) + -r7.yzw;
    r5.xyz = r4.xxx * r7.yzw + r5.xyz;
    r5.x = dot(r1.xy, r5.xz);
    r5.y = r5.y * r1.x;
    r1.y = dot(r5.xyz, r5.xyz);
    r1.w = sqrt(r1.y);
    r2.w = r5.z / r1.w;
    r1.xy = r1.xy * r1.xw;
    r1.x = r1.x / r1.y;
    r1.x = r1.x * r2.w;
    r1.x = r1.x * r1.z;
    r1.x = 0.318309873 * r1.x;
    r1.y = cmp(0 < r10.y);
    r1.x = r1.y ? r1.x : 0;
    r1.y = 0.25 + r3.w;
    r1.y = max(0, r1.y);
    r1.y = 0.800000012 * r1.y;
    r1.yzw = r2.xyz * r1.yyy;
    r2.w = max(0, -r3.w);
    r2.w = r2.w + r2.w;
    r5.xyz = r1.xxx * r2.xyz;
    r1.x = log2(r4.z);
    r1.x = 0.200000003 * r1.x;
    r1.x = exp2(r1.x);
    r1.x = min(1, r1.x);
    r4.xzw = r5.xyz * float3(0.00100000005,0.00100000005,0.00100000005) + -r2.www;
    r4.xzw = r1.xxx * r4.xzw + r2.www;
    r5.xyz = float3(1,1,1) + -r3.xyz;
    r3.xyz = r5.xyz * float3(0.200000003,0.200000003,0.100000001) + r3.xyz;
    r3.xyz = r4.xzw * r3.xyz;
    r3.xyz = r3.xyz * r0.xxx;
    r3.xyz = v2.xyz * r3.xyz;
    r3.xyz = r4.yyy * r3.xyz;
    r3.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r3.xyz;
  } else {
    r1.yzw = r2.xyz * r5.www;
    r3.xyz = float3(0,0,0);
  }
  r1.xyz = g_debug_light_diffuse_coef * r1.yzw;
  r1.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r1.xyz;
  r0.x = saturate(g_debug_light_shadow_coef + r7.x);
  r1.w = g_aristeia_effect.x * g_aristeia_effect.x;
  r0.yw = -g_aristeia_pos.xy + r0.yw;
  r0.y = dot(r0.yw, r0.yw);
  r0.y = r0.y / r1.w;
  r0.y = min(1, r0.y);
  r0.w = g_aristeia_effect.w + -g_aristeia_effect.z;
  r0.y = r0.y * r0.w + g_aristeia_effect.z;
  r0.y = -1 + r0.y;
  r0.w = g_aristeia_effect.y * r0.y;
  r0.y = g_aristeia_effect.y * r0.y + 1;
  r0.w = saturate(1.5 * r0.w);
  r1.w = 1 + -r0.x;
  r0.x = r0.w * r1.w + r0.x;
  r0.x = r0.y * r0.x;
  r1.xyz = r6.xyz * g_debug_light_specular_coef + r1.xyz;
  r1.xyz = r3.xyz * g_light_vegetation_backscattering_coef + r1.xyz;
  r0.xyw = r1.xyz * r0.xxx;
  r0.xyz = r0.zzz * r0.xyw;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  if (HDR >= 0.5f) o0.xyz *= VfxFireBrightness;
  o0.w = 1;
  return;
}