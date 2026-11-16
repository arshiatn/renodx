// pointlightsmoothfalloffshadow ps40_slm (the point lights: torches and fires), the game's C8345238 (3Dmigoto decompile) as
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
  r4.xyzw = gbuffer_channel_3_texture.Load(r1.xyw).xyzw;
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
  r0.w = dot(r2.xy, r2.xy);
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r5.y = r0.w * 2 + -1;
  r2.xy = r2.xy + r2.xy;
  r0.w = sqrt(r0.w);
  r5.xz = r2.xy * r0.ww;
  r0.w = 255 * r4.w;
  r0.w = round(r0.w);
  r0.w = (uint)r0.w;
  r2.xy = (int2)r0.ww & int2(128,2);
  r4.z = r2.x ? r4.x : r4.z;
  r0.yz = r0.yz * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.yz = r0.yz / g_viewport_offset_scale.zw;
  r1.xy = (r0.yz / DrawRenderShare()) * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r6.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r6.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r6.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.y = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.yzw = r6.xyz / r0.yyy;
  r1.xyz = -v3.xyz + r0.yzw;
  // The light's shadow: the rays' where they trace it, else the game's point shadow map (only then sampled).
  float traced_visibility;
  [branch]
  if (RtLocalLightTraced(int2(v0.xy), v3.xyz, traced_visibility)) {
    r6.x = traced_visibility;
  } else {
    r1.xyz = g_point_light_shadow_map_sample_bias * r1.xyz;
    r1.w = max(abs(r1.x), abs(r1.y));
    r1.w = max(r1.w, abs(r1.z));
    r1.w = g_light_proj_zw.y / r1.w;
    r1.w = g_light_proj_zw.x + r1.w;
    r2.x = dot(r1.xyz, r1.xyz);
    r2.x = rsqrt(r2.x);
    r1.xyz = r2.xxx * r1.xyz;
    r6.xzw = -r1.zxy;
    r7.xyzw = cmp(abs(r1.yzxz) < abs(r1.xxyy));
    r8.xyz = cmp(float3(0,0,0) < r1.xyz);
    r6.y = 0;
    r9.xw = float2(1,2);
    r9.yz = float2(1,-1) * r1.zy;
    r10.xzw = r8.xxx ? r6.yxw : r9.xyz;
    r2.xw = r7.yw ? r7.xz : 0;
    r7.x = 3;
    r7.w = r6.x;
    r7.xw = r8.yy ? r9.wy : r7.xw;
    r8.xy = cmp(abs(r1.yx) < abs(r1.zz));
    r3.w = r8.y ? r8.x : 0;
    r9.x = 4;
    r9.zw = float2(1,-1) * r1.xy;
    r6.x = 5;
    r6.xzw = r8.zzz ? r9.xzw : r6.xzw;
    r6.y = abs(r1.z);
    r6.xyzw = r3.wwww ? r6.xyzw : float4(0,1,0,0);
    r7.y = abs(r1.y);
    r7.z = r9.z;
    r6.xyzw = r2.wwww ? r7.xyzw : r6.xyzw;
    r10.y = abs(r1.x);
    r6.xyzw = r2.xxxx ? r10.xyzw : r6.xyzw;
    r1.xy = r6.zw / r6.yy;
    r1.xy = float2(1,1) + r1.xy;
    r1.xy = float2(0.5,0.5) * r1.xy;
    r6.w = (int)r6.x;
    r2.x = cmp((int)r0.x == -1);
    if (r2.x != 0) {
      r2.xw = g_shadow_buffer_size.xy * r1.xy + float2(0.5,0.5);
      r7.xy = floor(r2.xw);
      r2.xw = -r7.xy + r2.xw;
      r7.xy = -r2.xw * g_shadow_buffer_size.zw + r1.xy;
      r8.y = -3 * g_shadow_buffer_size.z;
      r8.xw = float2(0,0);
      r3.w = -3;
      while (true) {
        r4.w = cmp(3 < (int)r3.w);
        if (r4.w != 0) break;
        r4.w = (int)r3.w;
        r8.z = g_shadow_buffer_size.w * r4.w;
        r6.yz = r8.yz + r7.xy;
        r9.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r6.yzw).xyzw;
        r10.xyzw = cmp(r9.xyzw >= r1.wwww);
        r10.xyzw = r10.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r4.w = dot(r10.xyzw, float4(1,1,1,1));
        r5.w = dot(r9.xyzw, r10.xyzw);
        r6.x = g_shadow_buffer_size.z * 2 + r6.y;
        r9.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r6.xzw).xyzw;
        r10.xyzw = cmp(r9.xyzw >= r1.wwww);
        r10.xyzw = r10.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r6.y = dot(r10.xyzw, float4(1,1,1,1));
        r4.w = r6.y + r4.w;
        r6.y = dot(r9.xyzw, r10.xyzw);
        r5.w = r6.y + r5.w;
        r9.y = g_shadow_buffer_size.z * 2 + r6.x;
        r9.zw = r6.zw;
        r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r9.yzw).xyzw;
        r11.xyzw = cmp(r10.xyzw >= r1.wwww);
        r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r6.x = dot(r11.xyzw, float4(1,1,1,1));
        r4.w = r6.x + r4.w;
        r6.x = dot(r10.xyzw, r11.xyzw);
        r5.w = r6.x + r5.w;
        r9.x = g_shadow_buffer_size.z * 2 + r9.y;
        r9.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r9.xzw).xyzw;
        r10.xyzw = cmp(r9.xyzw >= r1.wwww);
        r10.xyzw = r10.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
        r6.x = dot(r10.xyzw, float4(1,1,1,1));
        r4.w = r6.x + r4.w;
        r6.x = dot(r9.xyzw, r10.xyzw);
        r5.w = r6.x + r5.w;
        r8.w = r8.w + r4.w;
        r8.x = r8.x + r5.w;
        r3.w = (int)r3.w + 2;
      }
      r3.w = cmp(r8.w < 64);
      if (r3.w != 0) {
        r3.w = cmp(0 < r8.w);
        r4.w = r8.x / r8.w;
        r4.w = r1.w / r4.w;
        r4.w = -1 + r4.w;
        r6.y = saturate(g_light_width * r4.w);
        r6.x = 0;
        r6.xy = r3.ww ? r6.xy : float2(1,0);
        if (r3.w != 0) {
          r3.w = r6.y * r6.y;
          r4.w = r3.w * r6.y;
          r5.w = 3 * r3.w;
          r6.z = 1 + -r6.y;
          r7.w = r6.z * r5.w;
          r8.x = r6.z * r6.z;
          r8.y = r8.x * r6.y;
          r8.z = 3 * r8.y;
          r8.w = r8.x * r6.z;
          r9.xy = float2(0,0);
          while (true) {
            r9.z = cmp((int)r9.x >= 11);
            if (r9.z != 0) break;
            r9.z = r3.w * r6.y + r9.y;
            r9.w = (int)r9.x * 11;
            r10.x = r7.w * icb[r9.w+1].x + r4.w;
            r9.z = r10.x + r9.z;
            r10.x = icb[r9.w+2].x * r7.w;
            r10.x = r8.z * icb[r9.w+2].y + r10.x;
            r10.x = r3.w * r6.y + r10.x;
            r9.z = r10.x + r9.z;
            r10.x = icb[r9.w+3].x * r7.w;
            r10.x = r8.z * icb[r9.w+3].y + r10.x;
            r10.x = r3.w * r6.y + r10.x;
            r9.z = r10.x + r9.z;
            r10.x = icb[r9.w+4].y * r8.z;
            r10.x = r8.w * icb[r9.w+4].z + r10.x;
            r10.x = r7.w * icb[r9.w+4].x + r10.x;
            r10.x = r3.w * r6.y + r10.x;
            r9.z = r10.x + r9.z;
            r10.x = icb[r9.w+5].y * r8.z;
            r10.x = r8.w * icb[r9.w+5].z + r10.x;
            r10.x = r7.w * icb[r9.w+5].x + r10.x;
            r10.x = r3.w * r6.y + r10.x;
            r9.z = r10.x + r9.z;
            r10.x = icb[r9.w+6].y * r8.z;
            r10.x = r8.w * icb[r9.w+6].z + r10.x;
            r10.x = r7.w * icb[r9.w+6].x + r10.x;
            r10.x = r3.w * r6.y + r10.x;
            r9.z = r10.x + r9.z;
            r10.x = icb[r9.w+7].x * r7.w;
            r10.x = r8.z * icb[r9.w+7].y + r10.x;
            r10.x = r3.w * r6.y + r10.x;
            r9.z = r10.x + r9.z;
            r10.x = icb[r9.w+8].x * r7.w;
            r10.x = r8.z * icb[r9.w+8].y + r10.x;
            r10.x = r3.w * r6.y + r10.x;
            r9.z = r10.x + r9.z;
            r9.w = r7.w * icb[r9.w+9].x + r4.w;
            r9.z = r9.z + r9.w;
            r9.y = r3.w * r6.y + r9.z;
            r9.x = (int)r9.x + 1;
          }
          r7.z = r6.w;
          r10.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-5,-5)).xyzw;
          r9.x = 1 + -r2.w;
          r9.z = -r4.w * r2.x + r4.w;
          r10.xw = r10.wx * r9.zz;
          r10.xz = r10.zy * r4.ww + r10.xw;
          r9.w = r10.z * r2.w;
          r9.w = r9.x * r10.x + r9.w;
          r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-3,-5)).xyzw;
          r10.x = dot(r11.wz, r4.ww);
          r9.w = r9.x * r10.x + r9.w;
          r10.x = dot(r11.xy, r4.ww);
          r9.w = r2.w * r10.x + r9.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-1,-5)).xyzw;
          r10.x = dot(r12.wz, r4.ww);
          r9.w = r9.x * r10.x + r9.w;
          r10.x = dot(r12.xy, r4.ww);
          r9.w = r2.w * r10.x + r9.w;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(1,-5)).xyzw;
          r10.x = dot(r13.wz, r4.ww);
          r9.w = r9.x * r10.x + r9.w;
          r10.x = dot(r13.xy, r4.ww);
          r9.w = r2.w * r10.x + r9.w;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(3,-5)).xyzw;
          r10.x = dot(r14.wz, r4.ww);
          r9.w = r9.x * r10.x + r9.w;
          r10.x = dot(r14.xy, r4.ww);
          r9.w = r2.w * r10.x + r9.w;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(5,-5)).xyzw;
          r10.xz = r15.zy * r2.xx;
          r10.xz = r10.xz * r4.ww;
          r11.zw = r15.wx * r4.ww + r10.xz;
          r9.w = r9.x * r11.z + r9.w;
          r9.w = r2.w * r11.w + r9.w;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-5,-3)).xyzw;
          r10.x = 0.200000003 * r7.w;
          r11.z = r7.w * 0.200000003 + r4.w;
          r11.w = r3.w * r6.y + -r11.z;
          r11.w = r2.x * r11.w + r11.z;
          r12.zw = r16.yz * r11.ww;
          r12.zw = r16.xw * r9.zz + r12.zw;
          r9.w = r9.x * r12.w + r9.w;
          r9.w = r2.w * r12.z + r9.w;
          r10.y = r10.y * r11.w + r10.w;
          r9.w = r9.x * r10.y + r9.w;
          r9.w = r2.w * r12.w + r9.w;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-3,-3)).xyzw;
          r10.yw = r8.yy * float2(3,0.600000024) + r7.ww;
          r10.yw = r3.ww * r6.yy + r10.yw;
          r7.w = r11.z + -r10.w;
          r7.w = r2.x * r7.w + r10.w;
          r13.zw = r16.wx * r7.ww;
          r14.zw = r16.zy * r10.ww + r13.zw;
          r8.y = r9.x * r14.z + r9.w;
          r8.y = r2.w * r14.w + r8.y;
          r9.w = dot(r11.xy, r11.zz);
          r8.y = r9.x * r9.w + r8.y;
          r9.w = dot(r16.wz, r11.zz);
          r8.y = r2.w * r9.w + r8.y;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-1,-3)).xyzw;
          r9.w = dot(r17.wz, r10.ww);
          r8.y = r9.x * r9.w + r8.y;
          r9.w = dot(r17.xy, r10.ww);
          r8.y = r2.w * r9.w + r8.y;
          r9.w = dot(r12.xy, r11.zz);
          r8.y = r9.x * r9.w + r8.y;
          r9.w = dot(r17.wz, r11.zz);
          r8.y = r2.w * r9.w + r8.y;
          r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(1,-3)).xyzw;
          r9.w = dot(r18.wz, r10.ww);
          r8.y = r9.x * r9.w + r8.y;
          r9.w = dot(r18.xy, r10.ww);
          r8.y = r2.w * r9.w + r8.y;
          r9.w = dot(r13.xy, r11.zz);
          r8.y = r9.x * r9.w + r8.y;
          r9.w = dot(r18.wz, r11.zz);
          r8.y = r2.w * r9.w + r8.y;
          r19.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(3,-3)).xyzw;
          r9.w = r10.w + -r11.z;
          r9.w = r2.x * r9.w + r11.z;
          r11.xy = r19.zy * r9.ww;
          r12.xy = r19.wx * r10.ww + r11.xy;
          r8.y = r9.x * r12.x + r8.y;
          r8.y = r2.w * r12.y + r8.y;
          r11.x = dot(r14.xy, r11.zz);
          r8.y = r9.x * r11.x + r8.y;
          r11.x = dot(r19.wz, r11.zz);
          r8.y = r2.w * r11.x + r8.y;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(5,-3)).xyzw;
          r10.x = r2.x * r10.x + r4.w;
          r12.xy = r14.yz * r2.xx;
          r12.xy = r12.xy * r4.ww;
          r12.xy = r14.xw * r10.xx + r12.xy;
          r8.y = r9.x * r12.y + r8.y;
          r8.y = r2.w * r12.x + r8.y;
          r10.z = r15.x * r10.x + r10.z;
          r8.y = r9.x * r10.z + r8.y;
          r8.y = r2.w * r12.y + r8.y;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-5,-1)).xyzw;
          r12.yw = r14.yz * r11.ww;
          r12.yw = r14.xw * r9.zz + r12.yw;
          r8.y = r9.x * r12.w + r8.y;
          r8.y = r2.w * r12.y + r8.y;
          r8.y = r9.x * r12.z + r8.y;
          r8.y = r2.w * r12.w + r8.y;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-3,-1)).xyzw;
          r12.zw = r10.wy + -r10.yw;
          r12.zw = r2.xx * r12.zw + r10.yw;
          r13.xy = r14.yz * r12.zz;
          r13.xy = r14.xw * r7.ww + r13.xy;
          r8.y = r9.x * r13.y + r8.y;
          r8.y = r2.w * r13.x + r8.y;
          r10.z = r16.y * r12.z + r13.w;
          r8.y = r9.x * r10.z + r8.y;
          r8.y = r2.w * r13.y + r8.y;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-1,-1)).xyzw;
          r8.w = r8.w * 0.800000012 + r8.z;
          r8.w = r5.w * r6.z + r8.w;
          r8.w = r3.w * r6.y + r8.w;
          r10.z = r10.y + -r8.w;
          r10.z = r2.x * r10.z + r8.w;
          r13.yz = r14.wx * r10.zz;
          r13.yw = r14.zy * r8.ww + r13.yz;
          r8.y = r9.x * r13.y + r8.y;
          r8.y = r2.w * r13.w + r8.y;
          r11.x = dot(r17.xy, r10.yy);
          r8.y = r9.x * r11.x + r8.y;
          r11.x = dot(r14.wz, r10.yy);
          r8.y = r2.w * r11.x + r8.y;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(1,-1)).xyzw;
          r11.x = r8.w + -r10.y;
          r11.x = r2.x * r11.x + r10.y;
          r13.yw = r15.zy * r11.xx;
          r14.xz = r15.wx * r8.ww + r13.yw;
          r8.y = r9.x * r14.x + r8.y;
          r8.y = r2.w * r14.z + r8.y;
          r13.y = dot(r18.xy, r10.yy);
          r8.y = r9.x * r13.y + r8.y;
          r13.y = dot(r15.wz, r10.yy);
          r8.y = r2.w * r13.y + r8.y;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(3,-1)).xyzw;
          r14.xz = r16.yz * r9.ww;
          r14.xz = r16.xw * r12.ww + r14.xz;
          r8.y = r9.x * r14.z + r8.y;
          r8.y = r2.w * r14.x + r8.y;
          r11.y = r19.x * r12.w + r11.y;
          r8.y = r9.x * r11.y + r8.y;
          r8.y = r2.w * r14.z + r8.y;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(5,-1)).xyzw;
          r14.zw = r16.yz * r2.xx;
          r14.zw = r14.zw * r4.ww;
          r14.zw = r16.xw * r10.xx + r14.zw;
          r8.y = r9.x * r14.w + r8.y;
          r8.y = r2.w * r14.z + r8.y;
          r8.y = r9.x * r12.x + r8.y;
          r8.y = r2.w * r14.w + r8.y;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-5,1)).xyzw;
          r15.yz = r16.yz * r11.ww;
          r15.yz = r16.xw * r9.zz + r15.yz;
          r8.y = r9.x * r15.z + r8.y;
          r8.y = r2.w * r15.y + r8.y;
          r8.y = r9.x * r12.y + r8.y;
          r8.y = r2.w * r15.z + r8.y;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-3,1)).xyzw;
          r12.xy = r16.yz * r12.zz;
          r12.xy = r16.xw * r7.ww + r12.xy;
          r8.y = r9.x * r12.y + r8.y;
          r8.y = r2.w * r12.x + r8.y;
          r8.y = r9.x * r13.x + r8.y;
          r8.y = r2.w * r12.y + r8.y;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-1,1)).xyzw;
          r13.xy = r16.wx * r10.zz;
          r15.zw = r16.zy * r8.ww + r13.xy;
          r8.y = r9.x * r15.z + r8.y;
          r8.y = r2.w * r15.w + r8.y;
          r8.x = r8.x * r6.z + r8.z;
          r5.w = r5.w * r6.z + r8.x;
          r3.w = r3.w * r6.y + r5.w;
          r5.w = r8.w + -r3.w;
          r5.w = r2.x * r5.w + r3.w;
          r6.y = r14.y * r5.w + r13.z;
          r6.y = r9.x * r6.y + r8.y;
          r5.w = r16.z * r5.w + r13.x;
          r5.w = r2.w * r5.w + r6.y;
          r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(1,1)).xyzw;
          r6.yz = r17.zy * r11.xx;
          r8.xy = r17.wx * r8.ww + r6.yz;
          r5.w = r9.x * r8.x + r5.w;
          r5.w = r2.w * r8.y + r5.w;
          r3.w = r3.w + -r8.w;
          r3.w = r2.x * r3.w + r8.w;
          r6.z = r15.x * r3.w + r13.w;
          r5.w = r9.x * r6.z + r5.w;
          r3.w = r17.w * r3.w + r6.y;
          r3.w = r2.w * r3.w + r5.w;
          r8.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(3,1)).xyzw;
          r6.yz = r8.yz * r9.ww;
          r6.yz = r8.xw * r12.ww + r6.yz;
          r3.w = r9.x * r6.z + r3.w;
          r3.w = r2.w * r6.y + r3.w;
          r3.w = r9.x * r14.x + r3.w;
          r3.w = r2.w * r6.z + r3.w;
          r8.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(5,1)).xyzw;
          r8.yz = r8.yz * r2.xx;
          r8.yz = r8.yz * r4.ww;
          r8.xy = r8.xw * r10.xx + r8.yz;
          r3.w = r9.x * r8.y + r3.w;
          r3.w = r2.w * r8.x + r3.w;
          r3.w = r9.x * r14.z + r3.w;
          r3.w = r2.w * r8.y + r3.w;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-5,3)).xyzw;
          r8.yz = r13.yz * r11.ww;
          r8.yz = r13.xw * r9.zz + r8.yz;
          r3.w = r9.x * r8.z + r3.w;
          r3.w = r2.w * r8.y + r3.w;
          r3.w = r9.x * r15.y + r3.w;
          r3.w = r2.w * r8.z + r3.w;
          r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-3,3)).xyzw;
          r8.zw = r13.wx * r7.ww;
          r11.xy = r13.zy * r10.ww + r8.zw;
          r3.w = r9.x * r11.x + r3.w;
          r3.w = r2.w * r11.y + r3.w;
          r3.w = r9.x * r12.x + r3.w;
          r5.w = r13.z * r12.z + r8.z;
          r3.w = r2.w * r5.w + r3.w;
          r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-1,3)).xyzw;
          r5.w = dot(r14.wz, r10.ww);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r14.xy, r10.ww);
          r3.w = r2.w * r5.w + r3.w;
          r5.w = dot(r16.xy, r10.yy);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r14.wz, r10.yy);
          r3.w = r2.w * r5.w + r3.w;
          r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(1,3)).xyzw;
          r5.w = dot(r15.wz, r10.ww);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r15.xy, r10.ww);
          r3.w = r2.w * r5.w + r3.w;
          r5.w = dot(r17.xy, r10.yy);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r15.wz, r10.yy);
          r3.w = r2.w * r5.w + r3.w;
          r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(3,3)).xyzw;
          r8.zw = r16.zy * r9.ww;
          r10.yz = r16.wx * r10.ww + r8.zw;
          r3.w = r9.x * r10.y + r3.w;
          r3.w = r2.w * r10.z + r3.w;
          r3.w = r9.x * r6.y + r3.w;
          r5.w = r16.w * r12.w + r8.z;
          r3.w = r2.w * r5.w + r3.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(5,3)).xyzw;
          r6.yz = r12.yz * r2.xx;
          r6.yz = r6.yz * r4.ww;
          r6.yz = r12.xw * r10.xx + r6.yz;
          r3.w = r9.x * r6.z + r3.w;
          r3.w = r2.w * r6.y + r3.w;
          r3.w = r9.x * r8.x + r3.w;
          r3.w = r2.w * r6.z + r3.w;
          r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-5,5)).xyzw;
          r8.xz = r12.wx * r9.zz;
          r8.zw = r12.zy * r4.ww + r8.xz;
          r3.w = r9.x * r8.z + r3.w;
          r3.w = r2.w * r8.w + r3.w;
          r3.w = r9.x * r8.y + r3.w;
          r5.w = r12.z * r11.w + r8.x;
          r3.w = r2.w * r5.w + r3.w;
          r8.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-3,5)).xyzw;
          r5.w = dot(r8.wz, r4.ww);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r8.xy, r4.ww);
          r3.w = r2.w * r5.w + r3.w;
          r5.w = dot(r13.xy, r11.zz);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r8.wz, r11.zz);
          r3.w = r2.w * r5.w + r3.w;
          r8.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(-1,5)).xyzw;
          r5.w = dot(r8.wz, r4.ww);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r8.xy, r4.ww);
          r3.w = r2.w * r5.w + r3.w;
          r5.w = dot(r14.xy, r11.zz);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r8.wz, r11.zz);
          r3.w = r2.w * r5.w + r3.w;
          r8.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(1,5)).xyzw;
          r5.w = dot(r8.wz, r4.ww);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r8.xy, r4.ww);
          r3.w = r2.w * r5.w + r3.w;
          r5.w = dot(r15.xy, r11.zz);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r8.wz, r11.zz);
          r3.w = r2.w * r5.w + r3.w;
          r8.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(3,5)).xyzw;
          r5.w = dot(r8.wz, r4.ww);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r8.xy, r4.ww);
          r3.w = r2.w * r5.w + r3.w;
          r5.w = dot(r16.xy, r11.zz);
          r3.w = r9.x * r5.w + r3.w;
          r5.w = dot(r8.wz, r11.zz);
          r3.w = r2.w * r5.w + r3.w;
          r7.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r7.xyz, r1.w, int2(5,5)).xyzw;
          r7.yz = r7.zy * r2.xx;
          r7.yz = r7.yz * r4.ww;
          r7.xz = r7.wx * r4.ww + r7.yz;
          r2.x = r9.x * r7.x + r3.w;
          r2.x = r2.w * r7.z + r2.x;
          r2.x = r9.x * r6.y + r2.x;
          r3.w = r7.w * r10.x + r7.y;
          r2.x = r2.w * r3.w + r2.x;
          r6.x = r2.x / r9.y;
        }
      } else {
        r6.x = 0;
      }
    } else {
      r6.x = 1;
    }
    if (r0.x == 0) {
      r1.z = r6.w;
      r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r1.xyz).x;
      r0.x = cmp(r1.w < r0.x);
      r6.x = r0.x ? 1.000000 : 0;
    }
  }
  r1.xyz = v3.xyz + -r0.yzw;
  r0.x = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r0.x);
  r6.yzw = r1.xyz * r1.www;
  r0.x = sqrt(r0.x);
  r0.x = saturate(r0.x / v3.w);
  r0.x = 1 + -r0.x;
  r0.x = r0.x * r0.x;
  r2.x = cmp(0 != v5.x);
  r2.w = dot(r6.yzw, v4.xyz);
  r2.w = saturate(r2.w * v5.x + v5.y);
  r2.w = r2.w * r0.x;
  r0.x = r2.x ? r2.w : r0.x;
  r2.x = dot(r5.xyz, r6.yzw);
  r7.xyz = r5.xyz * r2.xxx + -r6.yzw;
  r2.x = dot(r7.xyz, r7.xyz);
  r2.x = rsqrt(r2.x);
  r7.xyz = r7.xyz * r2.xxx;
  r2.xyw = r2.yyy ? r7.xyz : r5.xyz;
  r3.w = dot(r2.xyw, r6.yzw);
  r4.w = max(0, r3.w);
  r3.w = cmp(r3.w >= 0);
  if (r3.w != 0) {
    r5.xyz = -camera_position.xyz + r0.yzw;
    r0.z = dot(r5.xyz, r5.xyz);
    r0.z = rsqrt(r0.z);
    r5.xyz = r5.xyz * r0.zzz;
    r0.z = dot(r5.xyz, r2.xyw);
    r3.w = r0.z + r0.z;
    r2.xyw = r2.xyw * -r3.www + r5.xyz;
    r2.z = 1 + -r2.z;
    r3.w = r2.z * r2.z;
    r5.w = r3.w * r2.z;
    r7.x = r5.w * r2.z;
    r7.y = r7.x * r2.z;
    r7.x = 64.5780029 * r7.x;
    r7.x = r7.y * 28.0900002 + -r7.x;
    r5.w = r5.w * 48.6290016 + r7.x;
    r3.w = -r3.w * 12.6590004 + r5.w;
    r2.z = saturate(r2.z * 1.54589999 + r3.w);
    r2.z = 1 + -r2.z;
    r2.x = dot(r6.yzw, r2.xyw);
    r2.x = max(-1, r2.x);
    r2.x = min(1, r2.x);
    r2.y = 1 + -abs(r2.x);
    r2.y = sqrt(r2.y);
    r2.w = abs(r2.x) * -0.0187292993 + 0.0742610022;
    r2.w = r2.w * abs(r2.x) + -0.212114394;
    r2.w = r2.w * abs(r2.x) + 1.57072878;
    r3.w = r2.w * r2.y;
    r3.w = r3.w * -2 + 3.14159274;
    r2.x = cmp(r2.x < -r2.x);
    r2.x = r2.x ? r3.w : 0;
    r2.x = r2.w * r2.y + r2.x;
    r2.y = r2.z * -1.12524891 + 1.12839997;
    r6.yzw = float3(-0.00872664619,0.00872664619,0.00872664619) + r2.xxx;
    r2.x = 1 / r2.y;
    r6.yzw = r6.yzw * r2.xxx;
    r2.yw = float2(0.707106769,0.707106769) * r6.yz;
    r3.w = abs(r2.y) * abs(r2.y);
    r5.w = r3.w * abs(r2.y);
    r6.z = r5.w * abs(r2.y);
    r2.y = abs(r2.y) * 0.278393 + 1;
    r2.y = r3.w * 0.230388999 + r2.y;
    r2.y = r5.w * 0.000972000009 + r2.y;
    r2.y = r6.z * 0.0781079978 + r2.y;
    r2.y = r2.y * r2.y;
    r2.y = r2.y * r2.y;
    r2.y = 1 / r2.y;
    r2.y = 1 + -r2.y;
    r3.w = cmp(0 < r6.y);
    r5.w = cmp(r6.y < 0);
    r3.w = (int)-r3.w + (int)r5.w;
    r3.w = (int)r3.w;
    r2.y = r3.w * r2.y + 1;
    r2.y = 0.5 * r2.y;
    r3.w = r2.w * r2.w;
    r5.w = r3.w * r2.w;
    r2.w = r5.w * r2.w;
    r6.y = r6.w * 0.196853578 + 1;
    r3.w = r3.w * 0.230388999 + r6.y;
    r3.w = r5.w * 0.000972000009 + r3.w;
    r2.w = r2.w * 0.0781079978 + r3.w;
    r2.w = r2.w * r2.w;
    r2.w = r2.w * r2.w;
    r2.w = 1 / r2.w;
    r2.w = 2 + -r2.w;
    r2.y = r2.w * 0.5 + -r2.y;
    r6.yz = float2(-1.11072075,1.11072075) * r2.xx;
    r7.xy = r6.yz * r6.yz;
    r2.w = r7.x * -r6.y;
    r3.w = r2.w * -r6.y;
    r2.x = r2.x * 0.309216887 + 1;
    r6.yw = r7.xy * float2(0.230388999,0.230388999) + r2.xx;
    r2.x = r2.w * 0.000972000009 + r6.y;
    r2.x = r3.w * 0.0781079978 + r2.x;
    r2.x = r2.x * r2.x;
    r2.x = r2.x * r2.x;
    r2.x = 1 / r2.x;
    r2.x = 1 + -r2.x;
    r2.x = 1 + -r2.x;
    r2.x = 0.5 * r2.x;
    r2.w = r7.y * r6.z;
    r3.w = r2.w * r6.z;
    r2.w = r2.w * 0.000972000009 + r6.w;
    r2.w = r3.w * 0.0781079978 + r2.w;
    r2.w = r2.w * r2.w;
    r2.w = r2.w * r2.w;
    r2.w = 1 / r2.w;
    r2.w = 2 + -r2.w;
    r2.x = r2.w * 0.5 + -r2.x;
    r2.x = abs(r2.y) / r2.x;
    r2.x = min(1, r2.x);
    r2.x = r2.x * r2.x;
    r2.y = 1 + -r2.z;
    r2.z = min(1, r4.w);
    r2.xw = float2(0.785398185,1.57079637) * r2.xy;
    r2.w = sin(r2.w);
    r3.w = -1 + r2.z;
    r2.w = r2.w * r3.w + 1;
    r0.z = saturate(r0.z);
    r2.y = r2.y * r2.y;
    r3.w = 0.5 * r2.y;
    r2.y = -r2.y * 0.5 + 1;
    r2.z = r2.z * r2.y + r3.w;
    r2.z = 1 / r2.z;
    r0.z = r0.z * r2.y + r3.w;
    r0.z = 1 / r0.z;
    r0.z = r2.z * r0.z;
    r0.z = 0.25 * r0.z;
    r0.z = min(1, r0.z);
    r0.z = r0.z * r2.w;
    r1.xyz = r1.xyz * r1.www + -r5.xyz;
    r1.w = dot(r1.xyz, r1.xyz);
    r1.w = rsqrt(r1.w);
    r1.xyz = r1.xyz * r1.www;
    r1.x = saturate(dot(-r5.xyz, r1.xyz));
    r1.y = r1.x * -5.55472994 + -6.98316002;
    r1.x = r1.y * r1.x;
    r1.x = exp2(r1.x);
    r1.yzw = float3(1,1,1) + -r4.xyz;
    r1.xyz = r1.yzw * r1.xxx + r4.xyz;
    r0.z = r2.x * r0.z;
    r1.xyz = r0.zzz * r1.xyz;
  } else {
    r1.xyz = float3(0,0,0);
  }
  r2.xyz = v2.xyz * r1.xyz;
  r1.xyz = max(r4.xyz, r1.xyz);
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r3.xyz = v2.xyz * r3.xyz;
  r1.xyz = r3.xyz * r1.xyz;
  r1.xyz = r1.xyz * r4.www;
  r1.xyz = g_debug_light_diffuse_coef * r1.xyz;
  r1.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r1.xyz;
  r0.z = saturate(g_debug_light_shadow_coef + r6.x);
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
  r1.w = 1 + -r0.z;
  r0.z = r0.w * r1.w + r0.z;
  r0.y = r0.y * r0.z;
  r1.xyz = r2.xyz * g_debug_light_specular_coef + r1.xyz;
  r0.yzw = r1.xyz * r0.yyy;
  r0.xyz = r0.xxx * r0.yzw;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.w = 1;
  return;
}