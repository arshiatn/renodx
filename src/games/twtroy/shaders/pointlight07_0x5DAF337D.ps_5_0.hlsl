// pointlightlinearfalloffshadow ps40_skinlm (the point lights: torches and fires), the game's 5DAF337D (3Dmigoto decompile) as
// pointlight00_0x143A02ED: Torch and Fire Shadows (twattila/rt_scene.hpp InstallPointLights):
// the rays' shadow in place of the point shadow map's where they trace the light at the pixel, the game's map
// where they don't (twattila/rt_local_lights.hlsli RtLocalLightTraced);
// its NDC over the draw's share below 100% (the game's offset / scale stays its 100% one). With nothing bound the
// game's result: WARP tools\tw\testbin\tw2_ point light ps_compare.
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
  float g_overlay_parchment_lerp : packoffset(c47.y);
  float g_overlay_palette_alpha : packoffset(c47.z);
  float g_debug_tonemapping : packoffset(c47.w);
  float4 g_blood_remap : packoffset(c48);
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

SamplerState s_point_light_shadow_map_s : register(s1);
SamplerComparisonState s_point_light_shadow_map_sm50_s : register(s0);
Texture2D<float4> gbuffer_channel_1_texture : register(t0);
Texture2D<float4> gbuffer_channel_2_texture : register(t1);
Texture2D<float4> gbuffer_channel_3_texture : register(t2);
Texture2D<float4> gbuffer_channel_4_texture : register(t3);
Texture2DArray<float> t_point_light_shadow_map : register(t4);


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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19;
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
  r0.w = gbuffer_channel_3_texture.Load(r1.xyw).z;
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r2.xy = float2(1023,1023) * r2.xy;
  r2.xy = round(r2.xy);
  r2.xy = (uint2)r2.xy;
  r2.xy = (uint2)r2.xy >> int2(2,2);
  r2.xy = (uint2)r2.xy;
  r2.xy = r2.xy * float2(0.00784313772,0.00784313772) + float2(-1,-1);
  r2.w = dot(r2.xy, r2.xy);
  r2.w = 1 + -r2.w;
  r2.w = max(0, r2.w);
  r4.y = r2.w * 2 + -1;
  r2.xy = r2.xy + r2.xy;
  r2.w = sqrt(r2.w);
  r4.xz = r2.xy * r2.ww;
  r0.yz = r0.yz * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.yz = r0.yz / g_viewport_offset_scale.zw;
  r1.xy = (r0.yz / DrawRenderShare()) * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r5.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r5.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r5.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.y = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r1.xyz = r5.xyz / r0.yyy;
  r2.xyw = -v3.xyz + r1.xyz;
  // The light's shadow: the rays' where they trace it, else the game's point shadow map (only then sampled).
  float traced_visibility;
  [branch]
  if (RtLocalLightTraced(int2(v0.xy), v3.xyz, traced_visibility)) {
    r5.x = traced_visibility;
  } else {
    r2.xyw = g_point_light_shadow_map_sample_bias * r2.xyw;
    r0.y = max(abs(r2.x), abs(r2.y));
    r0.y = max(r0.y, abs(r2.w));
    r0.y = g_light_proj_zw.y / r0.y;
    r0.y = g_light_proj_zw.x + r0.y;
    r0.z = dot(r2.xyw, r2.xyw);
    r0.z = rsqrt(r0.z);
    r2.xyw = r2.xyw * r0.zzz;
    r5.xzw = -r2.wxy;
    r6.xyzw = cmp(abs(r2.ywxw) < abs(r2.xxyy));
    r7.xyz = cmp(float3(0,0,0) < r2.xyw);
    r5.y = 0;
    r8.xw = float2(1,2);
    r8.yz = float2(1,-1) * r2.wy;
    r9.xzw = r7.xxx ? r5.yxw : r8.xyz;
    r6.xy = r6.yw ? r6.xz : 0;
    r7.x = 3;
    r7.w = r5.x;
    r8.xw = r7.yy ? r8.wy : r7.xw;
    r6.zw = cmp(abs(r2.yx) < abs(r2.ww));
    r0.z = r6.w ? r6.z : 0;
    r10.x = 4;
    r10.zw = float2(1,-1) * r2.xy;
    r5.x = 5;
    r5.xzw = r7.zzz ? r10.xzw : r5.xzw;
    r5.y = abs(r2.w);
    r5.xyzw = r0.zzzz ? r5.xyzw : float4(0,1,0,0);
    r8.y = abs(r2.y);
    r8.z = r10.z;
    r5.xyzw = r6.yyyy ? r8.xyzw : r5.xyzw;
    r9.y = abs(r2.x);
    r5.xyzw = r6.xxxx ? r9.xyzw : r5.xyzw;
    r2.xy = r5.zw / r5.yy;
    r2.xy = float2(1,1) + r2.xy;
    r6.xy = float2(0.5,0.5) * r2.xy;
    r5.w = (int)r5.x;
    r0.z = cmp((int)r0.x == -1);
    if (r0.z != 0) {
      r2.xy = g_shadow_buffer_size.xy * r6.xy + float2(0.5,0.5);
      r7.xy = floor(r2.xy);
      r2.xy = -r7.xy + r2.xy;
      r7.xy = -r2.xy * g_shadow_buffer_size.zw + r6.xy;
      r8.y = -3 * g_shadow_buffer_size.z;
      r8.xw = float2(0,0);
      r0.z = -3;
      while (true) {
        r1.w = cmp(3 < (int)r0.z);
        if (r1.w != 0) break;
        r1.w = (int)r0.z;
        r8.z = g_shadow_buffer_size.w * r1.w;
        r5.yz = r8.yz + r7.xy;
        r9.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.yzw).xyzw;
        r10.xyzw = cmp(r9.xyzw >= r0.yyyy);
        r10.xyzw = r10.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r1.w = dot(r10.xyzw, float4(1,1,1,1));
        r2.w = dot(r9.xyzw, r10.xyzw);
        r5.x = g_shadow_buffer_size.z * 2 + r5.y;
        r9.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.xzw).xyzw;
        r10.xyzw = cmp(r9.xyzw >= r0.yyyy);
        r10.xyzw = r10.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r3.w = dot(r10.xyzw, float4(1,1,1,1));
        r1.w = r3.w + r1.w;
        r3.w = dot(r9.xyzw, r10.xyzw);
        r2.w = r3.w + r2.w;
        r9.y = g_shadow_buffer_size.z * 2 + r5.x;
        r9.zw = r5.zw;
        r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r9.yzw).xyzw;
        r11.xyzw = cmp(r10.xyzw >= r0.yyyy);
        r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r3.w = dot(r11.xyzw, float4(1,1,1,1));
        r1.w = r3.w + r1.w;
        r3.w = dot(r10.xyzw, r11.xyzw);
        r2.w = r3.w + r2.w;
        r9.x = g_shadow_buffer_size.z * 2 + r9.y;
        r9.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r9.xzw).xyzw;
        r10.xyzw = cmp(r9.xyzw >= r0.yyyy);
        r10.xyzw = r10.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r3.w = dot(r10.xyzw, float4(1,1,1,1));
        r1.w = r3.w + r1.w;
        r3.w = dot(r9.xyzw, r10.xyzw);
        r2.w = r3.w + r2.w;
        r8.w = r8.w + r1.w;
        r8.x = r8.x + r2.w;
        r0.z = (int)r0.z + 2;
      }
      r0.z = cmp(r8.w < 64);
      if (r0.z != 0) {
        r0.z = cmp(0 < r8.w);
        r1.w = r8.x / r8.w;
        r1.w = r0.y / r1.w;
        r1.w = -1 + r1.w;
        r5.y = saturate(g_light_width * r1.w);
        r5.x = 0;
        r5.xy = r0.zz ? r5.xy : float2(1,0);
        if (r0.z != 0) {
          r0.z = r5.y * r5.y;
          r1.w = r0.z * r5.y;
          r2.w = 3 * r0.z;
          r3.w = 1 + -r5.y;
          r4.w = r3.w * r2.w;
          r5.z = r3.w * r3.w;
          r6.w = r5.z * r5.y;
          r7.w = 3 * r6.w;
          r8.x = r5.z * r3.w;
          r8.yz = float2(0,0);
          while (true) {
            r8.w = cmp((int)r8.y >= 11);
            if (r8.w != 0) break;
            r8.w = r0.z * r5.y + r8.z;
            r9.x = (int)r8.y * 11;
            r9.y = r4.w * icb[r9.x+1].x + r1.w;
            r8.w = r9.y + r8.w;
            r9.y = icb[r9.x+2].x * r4.w;
            r9.y = r7.w * icb[r9.x+2].y + r9.y;
            r9.y = r0.z * r5.y + r9.y;
            r8.w = r9.y + r8.w;
            r9.y = icb[r9.x+3].x * r4.w;
            r9.y = r7.w * icb[r9.x+3].y + r9.y;
            r9.y = r0.z * r5.y + r9.y;
            r8.w = r9.y + r8.w;
            r9.y = icb[r9.x+4].y * r7.w;
            r9.y = r8.x * icb[r9.x+4].z + r9.y;
            r9.y = r4.w * icb[r9.x+4].x + r9.y;
            r9.y = r0.z * r5.y + r9.y;
            r8.w = r9.y + r8.w;
            r9.y = icb[r9.x+5].y * r7.w;
            r9.y = r8.x * icb[r9.x+5].z + r9.y;
            r9.y = r4.w * icb[r9.x+5].x + r9.y;
            r9.y = r0.z * r5.y + r9.y;
            r8.w = r9.y + r8.w;
            r9.y = icb[r9.x+6].y * r7.w;
            r9.y = r8.x * icb[r9.x+6].z + r9.y;
            r9.y = r4.w * icb[r9.x+6].x + r9.y;
            r9.y = r0.z * r5.y + r9.y;
            r8.w = r9.y + r8.w;
            r9.y = icb[r9.x+7].x * r4.w;
            r9.y = r7.w * icb[r9.x+7].y + r9.y;
            r9.y = r0.z * r5.y + r9.y;
            r8.w = r9.y + r8.w;
            r9.y = icb[r9.x+8].x * r4.w;
            r9.y = r7.w * icb[r9.x+8].y + r9.y;
            r9.y = r0.z * r5.y + r9.y;
            r8.w = r9.y + r8.w;
            r9.x = r4.w * icb[r9.x+9].x + r1.w;
            r8.w = r9.x + r8.w;
            r8.z = r0.z * r5.y + r8.w;
            r8.y = (int)r8.y + 1;
          }
          r7.z = r5.w;
          r9.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-5,-5)).xyzw;
          r8.y = 1 + -r2.y;
          r8.w = -r1.w * r2.x + r1.w;
          r9.xw = r9.wx * r8.ww;
          r9.xz = r9.zy * r1.ww + r9.xw;
          r9.z = r9.z * r2.y;
          r9.x = r8.y * r9.x + r9.z;
          r10.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-3,-5)).xyzw;
          r9.z = dot(r10.wz, r1.ww);
          r9.x = r8.y * r9.z + r9.x;
          r9.z = dot(r10.xy, r1.ww);
          r9.x = r2.y * r9.z + r9.x;
          r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-1,-5)).xyzw;
          r9.z = dot(r11.wz, r1.ww);
          r9.x = r8.y * r9.z + r9.x;
          r9.z = dot(r11.xy, r1.ww);
          r9.x = r2.y * r9.z + r9.x;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(1,-5)).xyzw;
          r9.z = dot(r12.wz, r1.ww);
          r9.x = r8.y * r9.z + r9.x;
          r9.z = dot(r12.xy, r1.ww);
          r9.x = r2.y * r9.z + r9.x;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(3,-5)).xyzw;
          r9.z = dot(r13.wz, r1.ww);
          r9.x = r8.y * r9.z + r9.x;
          r9.z = dot(r13.xy, r1.ww);
          r9.x = r2.y * r9.z + r9.x;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(5,-5)).xyzw;
          r10.zw = r14.zy * r2.xx;
          r10.zw = r10.zw * r1.ww;
          r11.zw = r14.wx * r1.ww + r10.zw;
          r9.x = r8.y * r11.z + r9.x;
          r9.x = r2.y * r11.w + r9.x;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-5,-3)).xyzw;
          r9.z = 0.200000003 * r4.w;
          r10.z = r4.w * 0.200000003 + r1.w;
          r11.z = r0.z * r5.y + -r10.z;
          r11.z = r2.x * r11.z + r10.z;
          r12.zw = r15.yz * r11.zz;
          r12.zw = r15.xw * r8.ww + r12.zw;
          r9.x = r8.y * r12.w + r9.x;
          r9.x = r2.y * r12.z + r9.x;
          r9.y = r9.y * r11.z + r9.w;
          r9.x = r8.y * r9.y + r9.x;
          r9.x = r2.y * r12.w + r9.x;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-3,-3)).xyzw;
          r9.yw = r6.ww * float2(3,0.600000024) + r4.ww;
          r9.yw = r0.zz * r5.yy + r9.yw;
          r4.w = r10.z + -r9.w;
          r4.w = r2.x * r4.w + r9.w;
          r13.zw = r15.wx * r4.ww;
          r14.yz = r15.zy * r9.ww + r13.zw;
          r6.w = r8.y * r14.y + r9.x;
          r6.w = r2.y * r14.z + r6.w;
          r9.x = dot(r10.xy, r10.zz);
          r6.w = r8.y * r9.x + r6.w;
          r9.x = dot(r15.wz, r10.zz);
          r6.w = r2.y * r9.x + r6.w;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-1,-3)).xyzw;
          r9.x = dot(r16.wz, r9.ww);
          r6.w = r8.y * r9.x + r6.w;
          r9.x = dot(r16.xy, r9.ww);
          r6.w = r2.y * r9.x + r6.w;
          r9.x = dot(r11.xy, r10.zz);
          r6.w = r8.y * r9.x + r6.w;
          r9.x = dot(r16.wz, r10.zz);
          r6.w = r2.y * r9.x + r6.w;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(1,-3)).xyzw;
          r9.x = dot(r17.wz, r9.ww);
          r6.w = r8.y * r9.x + r6.w;
          r9.x = dot(r17.xy, r9.ww);
          r6.w = r2.y * r9.x + r6.w;
          r9.x = dot(r12.xy, r10.zz);
          r6.w = r8.y * r9.x + r6.w;
          r9.x = dot(r17.wz, r10.zz);
          r6.w = r2.y * r9.x + r6.w;
          r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(3,-3)).xyzw;
          r9.x = r9.w + -r10.z;
          r9.x = r2.x * r9.x + r10.z;
          r10.xy = r18.zy * r9.xx;
          r11.xy = r18.wx * r9.ww + r10.xy;
          r6.w = r8.y * r11.x + r6.w;
          r6.w = r2.y * r11.y + r6.w;
          r10.x = dot(r13.xy, r10.zz);
          r6.w = r8.y * r10.x + r6.w;
          r10.x = dot(r18.wz, r10.zz);
          r6.w = r2.y * r10.x + r6.w;
          r19.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(5,-3)).xyzw;
          r9.z = r2.x * r9.z + r1.w;
          r11.xy = r19.yz * r2.xx;
          r11.xy = r11.xy * r1.ww;
          r11.xy = r19.xw * r9.zz + r11.xy;
          r6.w = r8.y * r11.y + r6.w;
          r6.w = r2.y * r11.x + r6.w;
          r10.x = r14.x * r9.z + r10.w;
          r6.w = r8.y * r10.x + r6.w;
          r6.w = r2.y * r11.y + r6.w;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-5,-1)).xyzw;
          r10.xw = r14.yz * r11.zz;
          r10.xw = r14.xw * r8.ww + r10.xw;
          r6.w = r8.y * r10.w + r6.w;
          r6.w = r2.y * r10.x + r6.w;
          r6.w = r8.y * r12.z + r6.w;
          r6.w = r2.y * r10.w + r6.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-3,-1)).xyzw;
          r11.yw = r9.wy + -r9.yw;
          r11.yw = r2.xx * r11.yw + r9.yw;
          r12.yz = r12.yz * r11.yy;
          r12.xy = r12.xw * r4.ww + r12.yz;
          r6.w = r8.y * r12.y + r6.w;
          r6.w = r2.y * r12.x + r6.w;
          r10.w = r15.y * r11.y + r13.w;
          r6.w = r8.y * r10.w + r6.w;
          r6.w = r2.y * r12.y + r6.w;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-1,-1)).xyzw;
          r8.x = r8.x * 0.800000012 + r7.w;
          r8.x = r2.w * r3.w + r8.x;
          r8.x = r0.z * r5.y + r8.x;
          r10.w = r9.y + -r8.x;
          r10.w = r2.x * r10.w + r8.x;
          r12.yz = r13.wx * r10.ww;
          r12.yw = r13.zy * r8.xx + r12.yz;
          r6.w = r8.y * r12.y + r6.w;
          r6.w = r2.y * r12.w + r6.w;
          r12.y = dot(r16.xy, r9.yy);
          r6.w = r8.y * r12.y + r6.w;
          r12.y = dot(r13.wz, r9.yy);
          r6.w = r2.y * r12.y + r6.w;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(1,-1)).xyzw;
          r12.y = r8.x + -r9.y;
          r12.y = r2.x * r12.y + r9.y;
          r13.xz = r14.zy * r12.yy;
          r13.xw = r14.wx * r8.xx + r13.xz;
          r6.w = r8.y * r13.x + r6.w;
          r6.w = r2.y * r13.w + r6.w;
          r12.w = dot(r17.xy, r9.yy);
          r6.w = r8.y * r12.w + r6.w;
          r12.w = dot(r14.wz, r9.yy);
          r6.w = r2.y * r12.w + r6.w;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(3,-1)).xyzw;
          r13.xw = r15.yz * r9.xx;
          r13.xw = r15.xw * r11.ww + r13.xw;
          r6.w = r8.y * r13.w + r6.w;
          r6.w = r2.y * r13.x + r6.w;
          r10.y = r18.x * r11.w + r10.y;
          r6.w = r8.y * r10.y + r6.w;
          r6.w = r2.y * r13.w + r6.w;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(5,-1)).xyzw;
          r14.yz = r15.yz * r2.xx;
          r14.yz = r14.yz * r1.ww;
          r14.yz = r15.xw * r9.zz + r14.yz;
          r6.w = r8.y * r14.z + r6.w;
          r6.w = r2.y * r14.y + r6.w;
          r6.w = r8.y * r11.x + r6.w;
          r6.w = r2.y * r14.z + r6.w;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-5,1)).xyzw;
          r14.zw = r15.yz * r11.zz;
          r14.zw = r15.xw * r8.ww + r14.zw;
          r6.w = r8.y * r14.w + r6.w;
          r6.w = r2.y * r14.z + r6.w;
          r6.w = r8.y * r10.x + r6.w;
          r6.w = r2.y * r14.w + r6.w;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-3,1)).xyzw;
          r10.xy = r15.yz * r11.yy;
          r10.xy = r15.xw * r4.ww + r10.xy;
          r6.w = r8.y * r10.y + r6.w;
          r6.w = r2.y * r10.x + r6.w;
          r6.w = r8.y * r12.x + r6.w;
          r6.w = r2.y * r10.y + r6.w;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-1,1)).xyzw;
          r10.yw = r15.wx * r10.ww;
          r12.xw = r15.zy * r8.xx + r10.yw;
          r6.w = r8.y * r12.x + r6.w;
          r6.w = r2.y * r12.w + r6.w;
          r5.z = r5.z * r3.w + r7.w;
          r2.w = r2.w * r3.w + r5.z;
          r0.z = r0.z * r5.y + r2.w;
          r2.w = r8.x + -r0.z;
          r2.w = r2.x * r2.w + r0.z;
          r3.w = r13.y * r2.w + r12.z;
          r3.w = r8.y * r3.w + r6.w;
          r2.w = r15.z * r2.w + r10.y;
          r2.w = r2.y * r2.w + r3.w;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(1,1)).xyzw;
          r5.yz = r16.zy * r12.yy;
          r10.yw = r16.wx * r8.xx + r5.yz;
          r2.w = r8.y * r10.y + r2.w;
          r2.w = r2.y * r10.w + r2.w;
          r0.z = r0.z + -r8.x;
          r0.z = r2.x * r0.z + r8.x;
          r3.w = r14.x * r0.z + r13.z;
          r2.w = r8.y * r3.w + r2.w;
          r0.z = r16.w * r0.z + r5.y;
          r0.z = r2.y * r0.z + r2.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(3,1)).xyzw;
          r5.yz = r12.yz * r9.xx;
          r5.yz = r12.xw * r11.ww + r5.yz;
          r0.z = r8.y * r5.z + r0.z;
          r0.z = r2.y * r5.y + r0.z;
          r0.z = r8.y * r13.x + r0.z;
          r0.z = r2.y * r5.z + r0.z;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(5,1)).xyzw;
          r10.yw = r12.yz * r2.xx;
          r10.yw = r10.yw * r1.ww;
          r10.yw = r12.xw * r9.zz + r10.yw;
          r0.z = r8.y * r10.w + r0.z;
          r0.z = r2.y * r10.y + r0.z;
          r0.z = r8.y * r14.y + r0.z;
          r0.z = r2.y * r10.w + r0.z;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-5,3)).xyzw;
          r12.yz = r12.yz * r11.zz;
          r12.xy = r12.xw * r8.ww + r12.yz;
          r0.z = r8.y * r12.y + r0.z;
          r0.z = r2.y * r12.x + r0.z;
          r0.z = r8.y * r14.z + r0.z;
          r0.z = r2.y * r12.y + r0.z;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-3,3)).xyzw;
          r12.yz = r13.wx * r4.ww;
          r12.zw = r13.zy * r9.ww + r12.yz;
          r0.z = r8.y * r12.z + r0.z;
          r0.z = r2.y * r12.w + r0.z;
          r0.z = r8.y * r10.x + r0.z;
          r2.w = r13.z * r11.y + r12.y;
          r0.z = r2.y * r2.w + r0.z;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-1,3)).xyzw;
          r2.w = dot(r14.wz, r9.ww);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r14.xy, r9.ww);
          r0.z = r2.y * r2.w + r0.z;
          r2.w = dot(r15.xy, r9.yy);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r14.wz, r9.yy);
          r0.z = r2.y * r2.w + r0.z;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(1,3)).xyzw;
          r2.w = dot(r15.wz, r9.ww);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r15.xy, r9.ww);
          r0.z = r2.y * r2.w + r0.z;
          r2.w = dot(r16.xy, r9.yy);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r15.wz, r9.yy);
          r0.z = r2.y * r2.w + r0.z;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(3,3)).xyzw;
          r9.xy = r16.zy * r9.xx;
          r9.yw = r16.wx * r9.ww + r9.xy;
          r0.z = r8.y * r9.y + r0.z;
          r0.z = r2.y * r9.w + r0.z;
          r0.z = r8.y * r5.y + r0.z;
          r2.w = r16.w * r11.w + r9.x;
          r0.z = r2.y * r2.w + r0.z;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(5,3)).xyzw;
          r5.yz = r17.yz * r2.xx;
          r5.yz = r5.yz * r1.ww;
          r5.yz = r17.xw * r9.zz + r5.yz;
          r0.z = r8.y * r5.z + r0.z;
          r0.z = r2.y * r5.y + r0.z;
          r0.z = r8.y * r10.y + r0.z;
          r0.z = r2.y * r5.z + r0.z;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-5,5)).xyzw;
          r8.xw = r17.wx * r8.ww;
          r9.xy = r17.zy * r1.ww + r8.xw;
          r0.z = r8.y * r9.x + r0.z;
          r0.z = r2.y * r9.y + r0.z;
          r0.z = r8.y * r12.x + r0.z;
          r2.w = r17.z * r11.z + r8.x;
          r0.z = r2.y * r2.w + r0.z;
          r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-3,5)).xyzw;
          r2.w = dot(r11.wz, r1.ww);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r11.xy, r1.ww);
          r0.z = r2.y * r2.w + r0.z;
          r2.w = dot(r13.xy, r10.zz);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r11.wz, r10.zz);
          r0.z = r2.y * r2.w + r0.z;
          r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(-1,5)).xyzw;
          r2.w = dot(r11.wz, r1.ww);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r11.xy, r1.ww);
          r0.z = r2.y * r2.w + r0.z;
          r2.w = dot(r14.xy, r10.zz);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r11.wz, r10.zz);
          r0.z = r2.y * r2.w + r0.z;
          r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(1,5)).xyzw;
          r2.w = dot(r11.wz, r1.ww);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r11.xy, r1.ww);
          r0.z = r2.y * r2.w + r0.z;
          r2.w = dot(r15.xy, r10.zz);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r11.wz, r10.zz);
          r0.z = r2.y * r2.w + r0.z;
          r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(3,5)).xyzw;
          r2.w = dot(r11.wz, r1.ww);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r11.xy, r1.ww);
          r0.z = r2.y * r2.w + r0.z;
          r2.w = dot(r16.xy, r10.zz);
          r0.z = r8.y * r2.w + r0.z;
          r2.w = dot(r11.wz, r10.zz);
          r0.z = r2.y * r2.w + r0.z;
          r7.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r0.y, int2(5,5)).xyzw;
          r2.xw = r7.zy * r2.xx;
          r2.xw = r2.xw * r1.ww;
          r7.xy = r7.wx * r1.ww + r2.xw;
          r0.z = r8.y * r7.x + r0.z;
          r0.z = r2.y * r7.y + r0.z;
          r0.z = r8.y * r5.y + r0.z;
          r1.w = r7.w * r9.z + r2.x;
          r0.z = r2.y * r1.w + r0.z;
          r5.x = r0.z / r8.z;
        }
      } else {
        r5.x = 0;
      }
    } else {
      r5.x = 1;
    }
    if (r0.x == 0) {
      r6.z = r5.w;
      r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r6.xyz).x;
      r0.x = cmp(r0.y < r0.x);
      r5.x = r0.x ? 1.000000 : 0;
    }
  }
  r0.xyz = v3.xyz + -r1.xyz;
  r1.w = dot(r0.xyz, r0.xyz);
  r2.x = rsqrt(r1.w);
  r5.yzw = r2.xxx * r0.xyz;
  r1.w = sqrt(r1.w);
  r1.w = saturate(r1.w / v3.w);
  r1.w = 1 + -r1.w;
  r2.y = dot(r4.xyz, r5.yzw);
  r2.w = max(0, r2.y);
  r2.y = cmp(r2.y >= 0);
  if (r2.y != 0) {
    r2.y = 1023 * r2.z;
    r2.y = round(r2.y);
    r2.y = (uint)r2.y;
    r2.y = (uint)r2.y >> 2;
    r2.y = (uint)r2.y;
    r6.xyz = -camera_position.xyz + r1.xyz;
    r1.y = dot(r6.xyz, r6.xyz);
    r1.y = rsqrt(r1.y);
    r6.xyz = r6.xyz * r1.yyy;
    r1.y = dot(r6.xyz, r4.xyz);
    r2.z = r1.y + r1.y;
    r4.xyz = r4.xyz * -r2.zzz + r6.xyz;
    r2.y = -r2.y * 0.00392156886 + 1;
    r2.z = r2.y * r2.y;
    r3.w = r2.z * r2.y;
    r4.w = r3.w * r2.y;
    r6.w = r4.w * r2.y;
    r4.w = 64.5780029 * r4.w;
    r4.w = r6.w * 28.0900002 + -r4.w;
    r3.w = r3.w * 48.6290016 + r4.w;
    r2.z = -r2.z * 12.6590004 + r3.w;
    r2.y = saturate(r2.y * 1.54589999 + r2.z);
    r2.y = 1 + -r2.y;
    r2.z = dot(r5.yzw, r4.xyz);
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
    r5.y = r3.w * abs(r4.y);
    r5.z = r5.y * abs(r4.y);
    r4.y = abs(r4.y) * 0.278393 + 1;
    r3.w = r3.w * 0.230388999 + r4.y;
    r3.w = r5.y * 0.000972000009 + r3.w;
    r3.w = r5.z * 0.0781079978 + r3.w;
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
    r5.y = r4.z * -r4.x;
    r4.x = r5.y * -r4.x;
    r2.z = r2.z * 0.309216887 + 1;
    r5.zw = r4.zw * float2(0.230388999,0.230388999) + r2.zz;
    r2.z = r5.y * 0.000972000009 + r5.z;
    r2.z = r4.x * 0.0781079978 + r2.z;
    r2.z = r2.z * r2.z;
    r2.z = r2.z * r2.z;
    r2.z = 1 / r2.z;
    r2.yz = float2(1,1) + -r2.yz;
    r2.z = 1 + -r2.z;
    r2.z = 0.5 * r2.z;
    r4.x = r4.w * r4.y;
    r4.y = r4.x * r4.y;
    r4.x = r4.x * 0.000972000009 + r5.w;
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
    r1.y = saturate(r1.y);
    r2.y = r2.y * r2.y;
    r4.y = 0.5 * r2.y;
    r2.y = -r2.y * 0.5 + 1;
    r3.w = r3.w * r2.y + r4.y;
    r3.w = 1 / r3.w;
    r1.y = r1.y * r2.y + r4.y;
    r1.y = 1 / r1.y;
    r1.y = r3.w * r1.y;
    r1.y = 0.25 * r1.y;
    r1.y = min(1, r1.y);
    r1.y = r1.y * r4.x;
    r0.xyz = r0.xyz * r2.xxx + -r6.xyz;
    r2.x = dot(r0.xyz, r0.xyz);
    r2.x = rsqrt(r2.x);
    r0.xyz = r2.xxx * r0.xyz;
    r0.x = saturate(dot(-r6.xyz, r0.xyz));
    r0.y = r0.x * -5.55472994 + -6.98316002;
    r0.x = r0.y * r0.x;
    r0.x = exp2(r0.x);
    r0.y = 1 + -r0.w;
    r0.x = r0.y * r0.x + r0.w;
    r0.y = r2.z * r1.y;
    r0.xyz = r0.yyy * r0.xxx;
  } else {
    r0.xyz = float3(0,0,0);
  }
  r2.xyz = v2.xyz * r0.xyz;
  r0.xyz = max(r0.xyz, r0.www);
  r0.xyz = float3(1,1,1) + -r0.xyz;
  r3.xyz = v2.xyz * r3.xyz;
  r0.xyz = r3.xyz * r0.xyz;
  r0.xyz = r0.xyz * r2.www;
  r0.xyz = g_debug_light_diffuse_coef * r0.xyz;
  r0.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r0.xyz;
  r0.w = saturate(g_debug_light_shadow_coef + r5.x);
  r1.y = g_aristeia_effect.x * g_aristeia_effect.x;
  r1.xz = -g_aristeia_pos.xy + r1.xz;
  r1.x = dot(r1.xz, r1.xz);
  r1.x = r1.x / r1.y;
  r1.x = min(1, r1.x);
  r1.y = g_aristeia_effect.w + -g_aristeia_effect.z;
  r1.x = r1.x * r1.y + g_aristeia_effect.z;
  r1.x = -1 + r1.x;
  r1.y = g_aristeia_effect.y * r1.x;
  r1.x = g_aristeia_effect.y * r1.x + 1;
  r1.y = saturate(1.5 * r1.y);
  r1.z = 1 + -r0.w;
  r0.w = r1.y * r1.z + r0.w;
  r0.w = r1.x * r0.w;
  r0.xyz = r2.xyz * g_debug_light_specular_coef + r0.xyz;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = r1.www * r0.xyz;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.w = 1;
  return;
}