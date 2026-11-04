// fx_battlefog, the fog's light (inscattering, ENABLE_SHADOWS): the game's, with rays in place of its cascade march
// (twwarhammer3/addon.cpp traced_lights; twattila/rt_fog.hlsli). Runs only with t43 bound; otherwise the game's own bytes do.
// The sky's light stays apart here, so the sun alone goes dark in shadow.
#define RT_FOG_SHADE 0.f
#include "../../twattila/rt_fog.hlsli"

cbuffer camera : register(b0)
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
  float real_time_in_sec : packoffset(c35.z);
  float update_time_in_sec : packoffset(c35.w);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float g_aspect_ratio : packoffset(c36.w);
  float4 g_screen_size : packoffset(c37);
  float g_vpos_texel_offset : packoffset(c38);
  float4 g_viewport_dimensions : packoffset(c39);
  float2 g_viewport_origin : packoffset(c40);
  float4 g_render_target_dimensions : packoffset(c41);
  float4 g_camera_temp0 : packoffset(c42);
  float4 g_camera_temp1 : packoffset(c43);
  float4 g_camera_temp2 : packoffset(c44);
  float4 g_clip_rect : packoffset(c45);
  float3 g_vr_head_rotation : packoffset(c46);
  int g_num_of_samples : packoffset(c46.w);
  float g_supersampling : packoffset(c47);
  float4 g_mouse_position : packoffset(c48);
  float3 g_frustum_points[8] : packoffset(c49);
  float4 g_frustum_planes[6] : packoffset(c57);
  float g_orthographic : packoffset(c63);
  float g_overlay_lerp : packoffset(c63.y);
  float g_overlay_parchment_lerp : packoffset(c63.z);
  float g_overlay_show_details : packoffset(c63.w);
  float g_amount_shadow_in_far_distance : packoffset(c64);
  float2 g_camera_jitter : packoffset(c64.y);
  float2 g_prev_camera_jitter : packoffset(c65);
  uint g_debug_visualization_flags : packoffset(c65.z);
  bool g_taa_is_enabled : packoffset(c65.w);
}

cbuffer environment_PS : register(b1)
{
  float g_environment_weights[8] : packoffset(c0);
  float g_environment_indices[8] : packoffset(c8);
  float g_environment_count : packoffset(c15.y);
  float g_overlapping_volume_weights[8] : packoffset(c16);
  uint g_overlapping_volume_indices[8] : packoffset(c24);
  uint g_overlapping_volume_count : packoffset(c31.y);
  float g_skydome_texture_weights[8] : packoffset(c32);
  uint g_skydome_texture_indices[8] : packoffset(c40);
  uint g_skydome_texture_count : packoffset(c47.y);
  bool g_skydome_texture_array_enabled : packoffset(c47.z);
}

cbuffer shadowmap_PS : register(b2)
{
  float4 g_vHardShadowBufferSize : packoffset(c0);
  float4x4 g_amHardSplit[4] : packoffset(c1);
  float4x4 g_amHardSplit_prewarp[4] : packoffset(c17);
  float2 g_fFadeRange : packoffset(c33);
  float g_sample_bias : packoffset(c33.z);
  float g_iSplitCount : packoffset(c33.w);
  float3 g_shadow_light_direction : packoffset(c34);
  float g_split_distances[5] : packoffset(c35);
  float g_screen_space_slope_bias : packoffset(c39.y);
  uint g_static_shadows_enabled : packoffset(c39.z);
}

cbuffer lighting_VS_PS : register(b3)
{
  bool g_apply_environment_specular : packoffset(c0);
  float3 sun_direction : packoffset(c0.y);
  float3 sun_colour : packoffset(c1);
  float sun_specular : packoffset(c1.w);
  float3 ambient_cube_lr[2] : packoffset(c2);
  float3 ambient_cube_tb[2] : packoffset(c4);
  float3 ambient_cube_fb[2] : packoffset(c6);
  float3 g_deep_water_colour : packoffset(c8);
  float3 g_shallow_water_colour : packoffset(c9);
  float3 g_sea_bed_light_scatter : packoffset(c10);
  float g_refraction_light_scatter : packoffset(c10.w);
  int g_ssr_enabled : packoffset(c11);
  bool g_gi_enabled : packoffset(c11.y);
  bool g_use_spherical_harmonics : packoffset(c11.z);
  bool g_use_spherical_harmonics_array : packoffset(c11.w);
  float2 g_cloud_shadow_direction : packoffset(c12);
  float g_cloud_shadow_speed : packoffset(c12.z);
  float g_cloud_shadow_scale : packoffset(c12.w);
  float g_cloud_shadow_lerp : packoffset(c13);
  float2 g_noise_uv_shift : packoffset(c13.y);
  bool g_skin_enable : packoffset(c13.w);
  float2 g_skin_curvature_scale_bias : packoffset(c14);
  float2 g_skin_translucency_scale_bias : packoffset(c14.z);
  float3 g_skin_blood_colour : packoffset(c15);
  float4 g_world_bounds : packoffset(c16);
  float4 g_playable_area_bounds : packoffset(c17);
  float g_vegetation_wrap_lighting_bias : packoffset(c18);
  float g_spherical_harmonic_terms : packoffset(c18.y);
  float g_spherical_harmonic_fadeout : packoffset(c18.z);
  float g_ambient_fudge_factor : packoffset(c18.w);
  float g_time_of_day_unary : packoffset(c19);
  float terrain_shadow_softening : packoffset(c19.y);
  float terrain_shadow_bias_depth_threshold : packoffset(c19.z);
  float terrain_shadow_full_bias : packoffset(c19.w);
  uint distortion_blendmode : packoffset(c20);
  float g_irradiance_voxel_size[4] : packoffset(c21);
  float3 g_irradiance_world_offsets[4] : packoffset(c25);
  uint g_debug_lighting : packoffset(c28.w);
  float g_environment_specular_shadow_ratio : packoffset(c29);
  float g_vfx_global_emissive_brightness_multiplier : packoffset(c29.y);
  float g_sun_scale : packoffset(c29.z);
  float g_sun_brightness_factor : packoffset(c29.w);
}

cbuffer fog_legacy : register(b4)
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

cbuffer fog : register(b5)
{
  float g_fog_density_constant : packoffset(c0);
  float g_fog_density_height : packoffset(c0.y);
  float g_fog_height_bottom : packoffset(c0.z);
  float g_fog_height_top : packoffset(c0.w);
  float g_fog_noise_scroll_speed : packoffset(c1);
  float g_fog_density_mie : packoffset(c1.y);
  float g_fog_noise : packoffset(c1.z);
  float3 g_fog_colour : packoffset(c2);
  float3 g_height_colour : packoffset(c3);
  uint g_fog_mode : packoffset(c3.w);
  float g_fog_lerp : packoffset(c4);
  float g_fog_clear_distance : packoffset(c4.y);
  float g_fog_y_multiplier : packoffset(c4.z);
  float g_campaign_density_scaling : packoffset(c4.w);
  float g_height_fog_clear_distance : packoffset(c5);
  float g_clear_distance_fade_out : packoffset(c5.y);
  float g_fog_far_skybox_lerp : packoffset(c5.z);
  float g_fog_far_skybox_mip : packoffset(c5.w);
  float g_fog_shadow_strength : packoffset(c6);
  float g_fog_blur_offset : packoffset(c6.y);
  float g_fog_blur_strength : packoffset(c6.z);
}

SamplerState s_environment_array_s : register(s0);
SamplerState s_volume_noise_s : register(s1);
SamplerState s_2d_noise_s : register(s2);
TextureCubeArray<float4> t_environment_array : register(t0);
Texture2DArray<float4> t_dynamic_shadows : register(t1);
Texture2DArray<float4> t_static_shadows : register(t2);
Texture2D<float4> t_world_spherical_harmonics : register(t3);
Texture3D<float4> t_irradiance_x_color_texture : register(t4);
Texture3D<float4> t_irradiance_y_color_texture : register(t5);
Texture3D<float4> t_irradiance_z_color_texture : register(t6);
Texture3D<float4> t_volume_noise : register(t7);
Texture2D<float4> t_2d_noise : register(t8);
Texture2D<float4> gbuffer_channel_4_texture : register(t9);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20,r21,r22,r23,r24;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_render_target_dimensions.zw * v0.xy;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r1.xy = (uint2)r0.zw;
  r1.xy = r1.xy * r0.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r0.zw);
  r1.zw = float2(0,0);
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r0.xy = g_render_target_dimensions.xy * r0.xy;
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
  r0.w = g_legacy_force_fog.x * r0.w;
  r1.xyz = -camera_position.xyz + r0.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r2.x = sqrt(r1.w);
  r2.y = min(g_height_fog_clear_distance, g_fog_clear_distance);
  r0.w = r0.w * 0.5 + r2.x;
  r2.z = g_legacy_force_fog.y + -g_legacy_force_fog.x;
  r0.w = -g_legacy_force_fog.x + r0.w;
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
    r2.w = rsqrt(r1.w);
    r4.xyz = r2.www * r1.xyz;
    r4.xyz = r4.xyz * r2.yyy + camera_position.xyz;
    r5.xyz = -r4.xyz + r0.xyz;
    r2.y = dot(r5.xyz, r5.xyz);
    r2.y = sqrt(r2.y);
    r5.xy = r4.xz * float2(0.00100000005,0.00100000005) + v0.xy;
    r5.xy = r3.xy * float2(0.100000001,0.100000001) + r5.xy;
    r5.xy = g_camera_jitter.xy + r5.xy;
    r5.xy = float2(0.103100002,0.103100002) * r5.xy;
    r5.xy = frac(r5.xy);
    r6.xyz = float3(19.1900005,19.1900005,19.1900005) + r5.yxx;
    r2.w = dot(r5.xyx, r6.xyz);
    r5.xy = r5.xy + r2.ww;
    r2.w = r5.x + r5.y;
    r2.w = r2.w * r5.x;
    r2.w = frac(r2.w);
    r2.w = frac(r2.w);
    r4.w = -5000 + r2.y;
    r4.w = saturate(0.000125000006 * r4.w);
    r5.x = r4.w * -2 + 3;
    r4.w = r4.w * r4.w;
    r4.w = r5.x * r4.w;
    r5.x = 0.25 + -r2.w;
    r2.w = r4.w * r5.x + r2.w;
    r5.xy = float2(0.0625,0.03125) * r2.yy;
    uint rt_fog_width, rt_fog_height;
    t_rt_fog.GetDimensions(rt_fog_width, rt_fog_height);
    r4.w = cmp(0 < g_fog_shadow_strength) & cmp(rt_fog_width == 0u);
    r5.z = cmp(0 < g_fog_noise);
    r5.w = g_fog_noise_scroll_speed * time_in_sec;
    r6.xyz = float3(4.4000001,-4.4000001,4.4000001) * r5.www;
    r7.w = 1;
    r8.w = 1;
    r9.w = 0;
    r11.xyz = float3(0,0,0);
    r12.xy = float2(0,0);
    r5.w = 0;
    r6.w = 0;
    r10.xyzw = float4(0,0,0,0);
    while (true) {
      r11.w = cmp((int)r10.w >= 16);
      if (r11.w != 0) break;
      r11.w = (int)r10.w;
      r11.w = r11.w + r2.w;
      r11.w = r5.x * r11.w + r5.y;
      r11.w = min(r11.w, r2.y);
      r7.xyz = r3.xyz * r11.www + r4.xyz;
      if (r4.w != 0) {
        r12.z = dot(r7.xyzw, view_projection._m03_m13_m23_m33);
        r12.w = cmp(r11.w < 1000);
        r13.x = cmp(1 < (int)r10.w);
        r12.w = r12.w ? r13.x : 0;
        r8.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r7.xyz;
        r13.x = cmp(r12.z >= g_split_distances[1]);
        r13.y = cmp(r12.z >= g_split_distances[2]);
        r13.z = cmp(r12.z >= g_split_distances[3]);
        r13.w = cmp(r12.z >= g_split_distances[4]);
        r13.xyzw = r13.xyzw ? float4(1,1,1,1) : 0;
        r12.z = dot(r13.xyzw, r13.xyzw);
        r12.z = 0.5 + r12.z;
        r12.z = floor(r12.z);
        r9.z = (int)r12.z;
        r13.x = (uint)r9.z << 2;
        r14.x = dot(r8.xyzw, g_amHardSplit[r13.x/4]._m00_m10_m20_m30);
        r14.y = dot(r8.xyzw, g_amHardSplit[r13.x/4]._m01_m11_m21_m31);
        r14.z = dot(r8.xyzw, g_amHardSplit[r13.x/4]._m02_m12_m22_m32);
        r8.x = dot(r8.xyzw, g_amHardSplit[r13.x/4]._m03_m13_m23_m33);
        r13.xyz = r14.xyz / r8.xxx;
        r8.y = cmp(r12.z >= g_iSplitCount);
        r15.xyz = cmp(r13.xyz < float3(0,0,0));
        r8.z = (int)r15.y | (int)r15.x;
        r8.z = (int)r15.z | (int)r8.z;
        r13.xyw = cmp(float3(1,1,1) < r13.xyz);
        r12.z = (int)r13.y | (int)r13.x;
        r12.z = (int)r13.w | (int)r12.z;
        r8.z = (int)r8.z | (int)r12.z;
        r8.y = (int)r8.z | (int)r8.y;
        if (r8.y != 0) {
          r8.y = 1;
        } else {
          r8.xz = r14.xy / r8.xx;
          r8.xz = g_vHardShadowBufferSize.xy * r8.xz;
          r8.xz = floor(r8.xz);
          r9.xy = (int2)r8.xz;
          r8.x = t_dynamic_shadows.Load(r9.xyzw).x;
          r8.z = t_static_shadows.Load(r9.xyzw).x;
          r8.z = g_static_shadows_enabled ? r8.z : 1;
          r8.x = min(r8.x, r8.z);
          r8.x = cmp(r13.z < r8.x);
          r8.y = r8.x ? 1.000000 : 0;
        }
        r8.x = r12.w ? r8.y : 1;
      } else {
        r8.x = 1;
      }
      r8.y = cmp(r11.w < 400);
      r8.y = r5.z ? r8.y : 0;
      if (r8.y != 0) {
        r9.xyz = r7.xyz * float3(0.00999999978,0.00999999978,0.00999999978) + r6.xyz;
        r7.x = t_volume_noise.SampleLevel(s_volume_noise_s, r9.xyz, 0).x;
        r7.x = r7.x * 2 + -1;
        r7.x = r7.x * g_fog_noise + 1;
      } else {
        r7.x = 1;
      }
      r6.w = r7.x + r6.w;
      r7.x = 60000 + -r7.y;
      r7.z = r7.x * 0.5 + r7.y;
      r7.yz = max(float2(0,0), r7.yz);
      r8.yz = float2(-0.000206276105,-0.00120224594) * r7.zz;
      r8.yz = exp2(r8.yz);
      r7.z = g_fog_density_mie * r8.z;
      r9.xy = float2(-0.000206276105,-0.00120224594) * r7.yy;
      r9.xy = exp2(r9.xy);
      r7.y = r9.x * r5.x;
      r12.x = r9.x * r5.x + r12.x;
      r8.z = g_fog_density_mie * r9.y;
      r9.x = r8.z * r5.x;
      r12.y = r8.z * r5.x + r12.y;
      r8.y = r8.y * r7.x + r12.x;
      r7.x = r7.z * r7.x + r12.y;
      r7.x = 2.20000002e-005 * r7.x;
      r13.xyz = r8.yyy * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r7.xxx;
      r13.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r13.xyz;
      r13.xyz = exp2(r13.xyz);
      r7.xyz = r13.xyz * r7.yyy;
      r10.xyz = r7.xyz * r8.xxx + r10.xyz;
      r7.xyz = r13.xyz * r9.xxx;
      r11.xyz = r7.xyz * r8.xxx + r11.xyz;
      r5.w = r8.x + r5.w;
      r10.w = (int)r10.w + 1;
    }
    r2.y = r2.z * r2.z + 1;
    r2.w = 0.5 * r2.y;
    r4.xyz = r10.xyz * r2.www;
    r2.w = -r2.z * 1.51999998 + 1.5776;
    r2.w = log2(abs(r2.w));
    r2.w = 1.5 * r2.w;
    r2.w = exp2(r2.w);
    r2.y = r2.y / r2.w;
    r2.y = 0.000978047145 * r2.y;
    r5.xyz = r11.xyz * r2.yyy;
    r5.xyz = float3(1.99999995e-005,1.99999995e-005,1.99999995e-005) * r5.xyz;
    r4.xyz = r4.xyz * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r5.xyz;
    r2.y = 0.0625 * r5.w;
    r2.w = 0.0625 * r6.w;
    r4.w = -g_height_fog_clear_distance + r2.x;
    r4.w = max(0, r4.w);
    r5.xz = float2(1,1);
    r5.y = g_fog_noise_scroll_speed;
    r5.xyz = time_in_sec * r5.xyz;
    r6.xz = g_fog_noise_scroll_speed;
    r6.y = -1;
    r5.xyz = r6.xyz * r5.xyz;
    r5.xyz = r0.xyz * float3(0.00100000005,0.00100000005,0.00100000005) + r5.xyz;
    r0.y = t_volume_noise.SampleLevel(s_volume_noise_s, r5.xyz, 0).x;
    r0.y = -0.5 + r0.y;
    r5.y = -0.0799999982 * r0.y;
    r5.xz = float2(0,0);
    r5.xyz = r5.xyz + r3.xyz;
    r0.y = dot(r5.xyz, r5.xyz);
    r0.y = rsqrt(r0.y);
    r3.xyz = r5.xyz * r0.yyy;
    r0.y = r3.y * g_height_fog_clear_distance + camera_position.y;
    r5.x = cmp(r0.y >= g_fog_height_top);
    r5.y = cmp(0 < r3.y);
    r5.x = r5.y ? r5.x : 0;
    if (r5.x != 0) {
      r5.xyz = float3(0,0,0);
    } else {
      r6.xy = -float2(g_fog_height_top, g_fog_height_bottom) + r0.yy;
      r6.z = 10000 + r0.y;
      r6.xyz = r6.xyz / -r3.yyy;
      r6.w = min(r6.x, r6.y);
      r6.x = max(r6.x, r6.y);
      r7.x = min(r6.y, r6.z);
      r6.y = max(r6.y, r6.z);
      r6.z = max(0, r7.x);
      r6.xyw = max(float3(0,0,0), r6.xyw);
      r7.x = g_fog_density_height * r2.w;
      r7.x = 0.0125000002 * r7.x;
      r7.y = min(r6.w, r6.x);
      r7.z = max(r6.w, r4.w);
      r7.z = min(r7.z, r6.x);
      r7.w = cmp(r7.y != r7.z);
      r0.y = r6.w * r3.y + r0.y;
      r7.z = r7.z + -r6.w;
      r8.x = r7.y + -r6.w;
      r8.y = g_fog_height_top + -g_fog_height_bottom;
      r8.y = r8.y + r8.y;
      r0.y = r0.y + r0.y;
      r8.z = r8.x * r3.y + r0.y;
      r8.z = -g_fog_height_top * 2 + r8.z;
      r8.x = r8.x * r8.z;
      r8.x = r8.x * r7.x;
      r8.x = r8.x / r8.y;
      r8.x = 1.44269502 * r8.x;
      r8.x = exp2(r8.x);
      r8.z = r7.z * r3.y + r0.y;
      r8.z = -g_fog_height_top * 2 + r8.z;
      r7.z = r8.z * r7.z;
      r7.z = r7.z * r7.x;
      r7.z = r7.z / r8.y;
      r7.z = 1.44269502 * r7.z;
      r7.z = exp2(r7.z);
      r7.z = r8.x + -r7.z;
      r8.x = min(r6.z, r6.y);
      r8.z = max(r7.y, r6.z);
      r8.z = min(r8.z, r6.y);
      r8.w = cmp(r8.x != r8.z);
      r8.z = r8.z + -r6.z;
      r8.z = -r8.z * r7.x;
      r8.z = 1.44269502 * r8.z;
      r8.z = exp2(r8.z);
      r8.z = r8.z * r7.z;
      r7.z = r8.w ? r8.z : r7.z;
      r7.z = r7.w ? r7.z : 0;
      r7.w = max(r6.z, r4.w);
      r6.y = min(r7.w, r6.y);
      r7.w = cmp(r8.x != r6.y);
      r8.z = r6.y + -r6.z;
      r6.z = r8.x + -r6.z;
      r6.z = -r6.z * r7.x;
      r6.z = 1.44269502 * r6.z;
      r6.z = exp2(r6.z);
      r8.x = -r8.z * r7.x;
      r8.x = 1.44269502 * r8.x;
      r8.x = exp2(r8.x);
      r6.z = -r8.x + r6.z;
      r6.y = max(r6.y, r6.w);
      r6.x = min(r6.y, r6.x);
      r6.y = cmp(r7.y != r6.x);
      r6.x = r6.x + -r6.w;
      r0.y = r6.x * r3.y + r0.y;
      r0.y = -g_fog_height_top * 2 + r0.y;
      r0.y = r6.x * r0.y;
      r0.y = r0.y * r7.x;
      r0.y = r0.y / r8.y;
      r0.y = 1.44269502 * r0.y;
      r0.y = exp2(r0.y);
      r0.y = r6.z * r0.y;
      r0.y = r6.y ? r0.y : r6.z;
      r0.y = r7.z + r0.y;
      r5.xyz = r7.www ? r0.yyy : r7.zzz;
    }
    r5.xyz = r5.xyz * r2.yyy;
    r0.y = 1 / g_clear_distance_fade_out;
    r0.y = saturate(r4.w * r0.y);
    r2.y = r0.y * -2 + 3;
    r0.y = r0.y * r0.y;
    r0.y = r2.y * r0.y;
    r5.xyz = r5.xyz * r0.yyy;
    r0.y = -g_fog_clear_distance + r2.x;
    r0.y = max(0, r0.y);
    r2.x = cmp(0.00999999978 < g_clear_distance_fade_out);
    r2.y = cmp(0.00999999978 < r0.y);
    r2.x = r2.y ? r2.x : 0;
    r2.y = min(g_clear_distance_fade_out, r0.y);
    r4.w = r2.y * r2.y;
    r6.x = g_clear_distance_fade_out + g_clear_distance_fade_out;
    r4.w = r4.w / r6.x;
    r2.y = -r2.y + r0.y;
    r2.y = r4.w + r2.y;
    r2.y = r2.y / r0.y;
    r2.x = r2.x ? r2.y : 1;
    r2.y = g_fog_density_constant * r2.w;
    r2.x = r2.y * r2.x;
    r0.y = r2.x * r0.y;
    r0.y = 0.0180336889 * r0.y;
    r2.x = exp2(r0.y);
    r0.y = exp2(-r0.y);
    r2.y = r2.x + r0.y;
    r0.y = r2.x + -r0.y;
    r0.y = 0.5 * r0.y;
    r0.y = saturate(r2.y * 0.5 + -r0.y);
    r0.y = 1 + -r0.y;
    r2.x = r5.w * 0.0625 + -1;
    r2.x = g_fog_shadow_strength * r2.x + 1;
    r0.y = r2.x * r0.y;
    r2.xyw = max(r5.xyz, r0.yyy);
    r0.y = max(9.99999997e-007, r2.x);
    r0.y = saturate(r5.x / r0.y);
    r5.xyz = g_height_colour.xyz + -g_fog_colour.xyz;
    r5.xyz = r0.yyy * r5.xyz + g_fog_colour.xyz;
  } else {
    r2.xyw = float3(0,0,0);
    r4.xyz = float3(0,0,0);
    r5.xyz = g_fog_colour.xyz;
  }
  r2.xyw = max(r2.xyw, r0.www);
  r0.y = r2.z * r2.z + 1;
  r0.y = 0.5 * r0.y;
  r4.xyz = r2.xyw * r0.yyy + r4.xyz;
  r6.xyz = r2.xyw * r0.yyy + -r4.xyz;
  r4.xyz = r2.xyw * r6.xyz + r4.xyz;
  r4.xyz = r4.xyz * r5.xyz;
  if (g_use_spherical_harmonics != 0) {
    r5.xyzw = g_playable_area_bounds.xyxy + g_playable_area_bounds.zwzw;
    r6.xyzw = float4(0.5,0.5,0.5,0.5) * r5.xyzw;
    r5.xyzw = -r5.zwzw * float4(0.5,0.5,0.5,0.5) + g_playable_area_bounds.xyzw;
    r5.xyzw = r5.xyzw * float4(1.04999995,1.04999995,1.04999995,1.04999995) + r6.xyzw;
    r0.yw = cmp(r0.xz < r5.xy);
    r6.xy = cmp(r5.zw < r0.xz);
    r0.yw = (int2)r0.yw | (int2)r6.xy;
    r0.y = (int)r0.w | (int)r0.y;
    r0.w = rsqrt(r1.w);
    r6.xy = r1.xz * r0.ww;
    r6.zw = cmp(r6.xy < float2(0,0));
    r5.xy = camera_position.xz + -r5.xy;
    r5.zw = -camera_position.xz + r5.zw;
    r5.xy = r6.zw ? r5.xy : r5.zw;
    r5.zw = float2(9.99999975e-005,9.99999975e-005) + abs(r6.xy);
    r5.xy = r5.xy / r5.zw;
    r0.w = min(r5.x, r5.y);
    r5.xy = r6.xy * r0.ww + camera_position.xz;
    r0.xy = r0.yy ? r5.xy : r0.xz;
    r0.xy = -g_playable_area_bounds.xy + r0.xy;
    r0.zw = g_playable_area_bounds.zw + -g_playable_area_bounds.xy;
    r0.xy = r0.xy / r0.zw;
    r0.zw = float2(-1,-1) + r0.xy;
    r0.zw = max(-r0.xy, r0.zw);
    r0.zw = max(float2(0,0), r0.zw);
    r0.z = max(r0.z, r0.w);
    r0.z = saturate(g_spherical_harmonic_fadeout * r0.z);
    t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r5.xy = uiDest.xy;
    uiDest.xy = (uint2)r5.xy / int2(3,3);
    r5.xy = uiDest.xy;
    r0.xy = saturate(r0.xy);
    r5.xy = (int2)r5.xy + int2(-1,-1);
    r5.zw = (uint2)r5.xy;
    r0.xy = r5.zw * r0.xy;
    r5.zw = frac(r0.xy);
    r0.xy = trunc(r0.xy);
    r0.xy = (int2)r0.xy;
    r6.xy = (int2)r0.xy + int2(1,1);
    r5.xy = min((int2)r6.xy, (int2)r5.xy);
    r6.xyzw = r3.xzyz * r3.zyxz;
    r0.w = r6.w * 3 + -1;
    r1.w = r3.y * r3.y;
    r1.w = r3.x * r3.x + -r1.w;
    r2.z = round(g_spherical_harmonic_terms);
    r2.z = (int)r2.z;
    r7.w = 0;
    r8.xyz = float3(0,0,0);
    r4.w = 0;
    while (true) {
      r6.w = cmp((int)r4.w >= (int)r2.z);
      if (r6.w != 0) break;
      r6.w = (uint)r4.w % 3;
      r7.y = mad((int)r0.x, 3, (int)r6.w);
      r7.z = mad((int)r0.y, 3, (int)icb[r4.w+0].y);
      r9.xyz = t_world_spherical_harmonics.Load(r7.yzw).xyz;
      r7.x = mad((int)r5.x, 3, (int)r6.w);
      r10.xyz = t_world_spherical_harmonics.Load(r7.xzw).xyz;
      r11.y = mad((int)r5.y, 3, (int)icb[r4.w+0].y);
      r11.xzw = r7.yww;
      r12.xyz = t_world_spherical_harmonics.Load(r11.xyz).xyz;
      r11.xzw = r7.xww;
      r7.xyz = t_world_spherical_harmonics.Load(r11.xyz).xyz;
      r10.xyz = r10.xyz + -r9.xyz;
      r9.xyz = r5.zzz * r10.xyz + r9.xyz;
      r7.xyz = r7.xyz + -r12.xyz;
      r7.xyz = r5.zzz * r7.xyz + r12.xyz;
      r7.xyz = r7.xyz + -r9.xyz;
      r7.xyz = r5.www * r7.xyz + r9.xyz;
      r9.xyzw = cmp((int4)r4.wwww == int4(0,1,2,3));
      r10.xyzw = cmp((int4)r4.wwww == int4(4,5,6,7));
      r6.w = cmp((int)r4.w == 8);
      r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
      r10.xyzw = r10.xyzw ? float4(1,1,1,1) : 0;
      r6.w = r6.w ? 1.000000 : 0;
      r8.w = r9.y * r3.x + r9.x;
      r8.w = r9.z * r3.y + r8.w;
      r8.w = r9.w * r3.z + r8.w;
      r8.w = r10.x * r6.x + r8.w;
      r8.w = r10.y * r6.y + r8.w;
      r8.w = r10.z * r6.z + r8.w;
      r8.w = r10.w * r0.w + r8.w;
      r6.w = r6.w * r1.w + r8.w;
      r8.xyz = r7.xyz * r6.www + r8.xyz;
      r4.w = (int)r4.w + 1;
    }
    r0.x = cmp(0 < r0.z);
    r5.xyz = cmp(r3.xyz < float3(0,0,0));
    r6.xyz = r5.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
    r5.xyw = r5.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
    r7.xyz = r5.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
    r9.xyz = r3.xyz * r3.xyz;
    r5.xyz = r9.yyy * r5.xyw;
    r5.xyz = r9.xxx * r6.xyz + r5.xyz;
    r5.xyz = r9.zzz * r7.xyz + r5.xyz;
    r5.xyz = r5.xyz + -r8.xyz;
    r0.yzw = r0.zzz * r5.xyz + r8.xyz;
    r0.xyz = r0.xxx ? r0.yzw : r8.xyz;
    r0.xyz = max(float3(0,0,0), r0.xyz);
  } else {
    r5.xyz = cmp(r3.xyz < float3(0,0,0));
    r6.xyz = r5.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
    r5.xyw = r5.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
    r7.xyz = r5.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
    r8.xyz = r3.xyz * r3.xyz;
    r5.xyz = r8.yyy * r5.xyw;
    r5.xyz = r8.xxx * r6.xyz + r5.xyz;
    r5.xyz = r8.zzz * r7.xyz + r5.xyz;
    r0.xyz = g_ambient_fudge_factor * r5.xyz;
  }
  r0.w = cmp(g_gi_enabled != 0);
  r5.xyz = r3.xyz * r3.xyz;
  r6.xyz = cmp(r3.xyz < float3(0,0,0));
  r7.xyzw = r6.xxyy ? float4(0,64,0,64) : 0;
  r6.xyz = r6.zzz ? float3(0,0,64) : 0;
  r8.xy = float2(0,0);
  r9.w = 0;
  r10.w = 0;
  r11.w = 0;
  r12.xy = float2(0,0);
  r13.w = 0;
  r14.w = 0;
  r15.w = 0;
  r16.xyz = float3(0,0,0);
  r1.w = 1;
  r2.z = 0;
  r4.w = 0;
  r5.w = 0;
  while (true) {
    r6.w = cmp((int)r4.w >= 4);
    r5.w = 0;
    if (r6.w != 0) break;
    r17.xyz = r1.xyz / g_irradiance_voxel_size[r4.w];
    r18.xyz = float3(31,15,31) + r17.xyz;
    r18.xyz = saturate(float3(0.142857149,0.142857149,0.142857149) * r18.xyz);
    r19.xyz = r18.xyz * float3(-2,-2,-2) + float3(3,3,3);
    r18.xyz = r18.xyz * r18.xyz;
    r20.xyz = float3(-24,-8,-24) + r17.xyz;
    r20.xyz = saturate(float3(0.142857149,0.142857149,0.142857149) * r20.xyz);
    r21.xyz = r20.xyz * float3(-2,-2,-2) + float3(3,3,3);
    r20.xyz = r20.xyz * r20.xyz;
    r20.xyz = r21.xyz * r20.xyz;
    r18.xyz = r19.xyz * r18.xyz + -r20.xyz;
    r6.w = r18.x * r18.y;
    r6.w = r6.w * r18.z;
    r8.w = cmp(0 < r6.w);
    if (r8.w != 0) {
      r17.xyz = g_irradiance_world_offsets[r4.w].xyz + r17.xyz;
      r17.xyz = float3(-0.5,-0.5,-0.5) + r17.xyz;
      r18.xyz = float3(0.015625,0.03125,0.015625) * r17.xyz;
      r18.xyz = floor(r18.xyz);
      r18.xyz = -r18.xyz * float3(64,32,64) + r17.xyz;
      r12.w = (uint)r4.w << 7;
      r17.xyz = frac(r17.xyz);
      r19.xyz = float3(1,1,1) + -r17.xyz;
      r17.xyz = -r19.xyz + r17.xyz;
      r8.z = (uint)r12.w;
      r20.xyz = float3(0,0,0);
      r12.w = 0;
      r21.x = 0;
      while (true) {
        r16.w = cmp((int)r21.x >= 8);
        if (r16.w != 0) break;
        r21.yz = (uint2)r21.xx >> int2(1,2);
        r21.yzw = (int3)r21.xyz & int3(1,1,1);
        r21.yzw = (int3)r21.yzw;
        r22.xyz = r21.yzw * r17.xyz + r19.xyz;
        r16.w = r22.x * r22.y;
        r17.w = r16.w * r22.z;
        r21.yzw = r21.yzw + r18.xyz;
        r22.xyw = float3(64,32,64) * r21.yzw;
        r22.xyw = cmp(r22.xyw >= -r22.xyw);
        r23.xyz = r22.xyw ? float3(64,32,64) : float3(-64,-32,-64);
        r22.xyw = r22.xyw ? float3(0.015625,0.03125,0.015625) : float3(-0.015625,-0.03125,-0.015625);
        r21.yzw = r22.xyw * r21.yzw;
        r21.yzw = frac(r21.yzw);
        r21.yzw = r23.xyz * r21.yzw + r8.xyz;
        r21.yzw = r21.yzw + r7.xxy;
        r9.xyz = (uint3)r21.yzw;
        r9.xyz = t_irradiance_x_color_texture.Load(r9.xyzw).xyz;
        r12.w = r16.w * r22.z + r12.w;
        r20.xyz = r9.xyz * r17.www + r20.xyz;
        r21.x = (int)r21.x + 1;
      }
      r9.xyz = r20.xyz / r12.www;
      r21.xyz = float3(0,0,0);
      r16.w = 0;
      r22.x = 0;
      while (true) {
        r17.w = cmp((int)r22.x >= 8);
        if (r17.w != 0) break;
        r22.yz = (uint2)r22.xx >> int2(1,2);
        r22.yzw = (int3)r22.xyz & int3(1,1,1);
        r22.yzw = (int3)r22.yzw;
        r23.xyz = r22.yzw * r17.xyz + r19.xyz;
        r17.w = r23.x * r23.y;
        r18.w = r17.w * r23.z;
        r22.yzw = r22.yzw + r18.xyz;
        r23.xyw = float3(64,32,64) * r22.yzw;
        r23.xyw = cmp(r23.xyw >= -r23.xyw);
        r24.xyz = r23.xyw ? float3(64,32,64) : float3(-64,-32,-64);
        r23.xyw = r23.xyw ? float3(0.015625,0.03125,0.015625) : float3(-0.015625,-0.03125,-0.015625);
        r22.yzw = r23.xyw * r22.yzw;
        r22.yzw = frac(r22.yzw);
        r22.yzw = r24.xyz * r22.yzw + r8.xyz;
        r22.yzw = r22.yzw + r7.zzw;
        r10.xyz = (uint3)r22.yzw;
        r10.xyz = t_irradiance_y_color_texture.Load(r10.xyzw).xyz;
        r16.w = r17.w * r23.z + r16.w;
        r21.xyz = r10.xyz * r18.www + r21.xyz;
        r22.x = (int)r22.x + 1;
      }
      r10.xyz = r21.xyz / r16.www;
      r10.xyz = r10.xyz * r5.yyy;
      r9.xyz = r5.xxx * r9.xyz + r10.xyz;
      r10.xyz = float3(0,0,0);
      r17.w = 0;
      r22.x = 0;
      while (true) {
        r18.w = cmp((int)r22.x >= 8);
        if (r18.w != 0) break;
        r22.yz = (uint2)r22.xx >> int2(1,2);
        r22.yzw = (int3)r22.xyz & int3(1,1,1);
        r22.yzw = (int3)r22.yzw;
        r23.xyz = r22.yzw * r17.xyz + r19.xyz;
        r18.w = r23.x * r23.y;
        r19.w = r18.w * r23.z;
        r22.yzw = r22.yzw + r18.xyz;
        r23.xyw = float3(64,32,64) * r22.yzw;
        r23.xyw = cmp(r23.xyw >= -r23.xyw);
        r24.xyz = r23.xyw ? float3(64,32,64) : float3(-64,-32,-64);
        r23.xyw = r23.xyw ? float3(0.015625,0.03125,0.015625) : float3(-0.015625,-0.03125,-0.015625);
        r22.yzw = r23.xyw * r22.yzw;
        r22.yzw = frac(r22.yzw);
        r22.yzw = r24.xyz * r22.yzw + r8.xyz;
        r22.yzw = r22.yzw + r6.xyz;
        r11.xyz = (uint3)r22.yzw;
        r11.xyz = t_irradiance_z_color_texture.Load(r11.xyzw).xyz;
        r17.w = r18.w * r23.z + r17.w;
        r10.xyz = r11.xyz * r19.www + r10.xyz;
        r22.x = (int)r22.x + 1;
      }
      r11.xyz = r10.xyz / r17.www;
      r16.xyz = r5.zzz * r11.xyz + r9.xyz;
      r8.z = cmp(r6.w < 1);
      if (r8.z != 0) {
        r8.z = cmp((int)r4.w < 3);
        if (r8.z != 0) {
          r8.z = (int)r4.w + 1;
          r9.xyz = r1.xyz / g_irradiance_voxel_size[r8.z];
          r9.xyz = g_irradiance_world_offsets[r8.z].xyz + r9.xyz;
          r9.xyz = float3(-0.5,-0.5,-0.5) + r9.xyz;
          r11.xyz = float3(0.015625,0.03125,0.015625) * r9.xyz;
          r11.xyz = floor(r11.xyz);
          r11.xyz = -r11.xyz * float3(64,32,64) + r9.xyz;
          r8.z = (uint)r8.z << 7;
          r9.xyz = frac(r9.xyz);
          r17.xyz = float3(1,1,1) + -r9.xyz;
          r9.xyz = -r17.xyz + r9.xyz;
          r12.z = (uint)r8.z;
          r18.xyz = float3(0,0,0);
          r8.z = 0;
          r19.x = 0;
          while (true) {
            r18.w = cmp((int)r19.x >= 8);
            if (r18.w != 0) break;
            r19.yz = (uint2)r19.xx >> int2(1,2);
            r19.yzw = (int3)r19.xyz & int3(1,1,1);
            r19.yzw = (int3)r19.yzw;
            r22.xyz = r19.yzw * r9.xyz + r17.xyz;
            r18.w = r22.x * r22.y;
            r20.w = r18.w * r22.z;
            r19.yzw = r19.yzw + r11.xyz;
            r22.xyw = float3(64,32,64) * r19.yzw;
            r22.xyw = cmp(r22.xyw >= -r22.xyw);
            r23.xyz = r22.xyw ? float3(64,32,64) : float3(-64,-32,-64);
            r22.xyw = r22.xyw ? float3(0.015625,0.03125,0.015625) : float3(-0.015625,-0.03125,-0.015625);
            r19.yzw = r22.xyw * r19.yzw;
            r19.yzw = frac(r19.yzw);
            r19.yzw = r23.xyz * r19.yzw + r12.xyz;
            r19.yzw = r19.yzw + r7.xxy;
            r13.xyz = (uint3)r19.yzw;
            r13.xyz = t_irradiance_x_color_texture.Load(r13.xyzw).xyz;
            r8.z = r18.w * r22.z + r8.z;
            r18.xyz = r13.xyz * r20.www + r18.xyz;
            r19.x = (int)r19.x + 1;
          }
          r13.xyz = r18.xyz / r8.zzz;
          r19.xyz = float3(0,0,0);
          r18.w = 0;
          r22.x = 0;
          while (true) {
            r19.w = cmp((int)r22.x >= 8);
            if (r19.w != 0) break;
            r22.yz = (uint2)r22.xx >> int2(1,2);
            r22.yzw = (int3)r22.xyz & int3(1,1,1);
            r22.yzw = (int3)r22.yzw;
            r23.xyz = r22.yzw * r9.xyz + r17.xyz;
            r19.w = r23.x * r23.y;
            r20.w = r19.w * r23.z;
            r22.yzw = r22.yzw + r11.xyz;
            r23.xyw = float3(64,32,64) * r22.yzw;
            r23.xyw = cmp(r23.xyw >= -r23.xyw);
            r24.xyz = r23.xyw ? float3(64,32,64) : float3(-64,-32,-64);
            r23.xyw = r23.xyw ? float3(0.015625,0.03125,0.015625) : float3(-0.015625,-0.03125,-0.015625);
            r22.yzw = r23.xyw * r22.yzw;
            r22.yzw = frac(r22.yzw);
            r22.yzw = r24.xyz * r22.yzw + r12.xyz;
            r22.yzw = r22.yzw + r7.zzw;
            r14.xyz = (uint3)r22.yzw;
            r14.xyz = t_irradiance_y_color_texture.Load(r14.xyzw).xyz;
            r18.w = r19.w * r23.z + r18.w;
            r19.xyz = r14.xyz * r20.www + r19.xyz;
            r22.x = (int)r22.x + 1;
          }
          r14.xyz = r19.xyz / r18.www;
          r14.xyz = r14.xyz * r5.yyy;
          r13.xyz = r5.xxx * r13.xyz + r14.xyz;
          r14.xyz = float3(0,0,0);
          r19.w = 0;
          r22.x = 0;
          while (true) {
            r20.w = cmp((int)r22.x >= 8);
            if (r20.w != 0) break;
            r22.yz = (uint2)r22.xx >> int2(1,2);
            r22.yzw = (int3)r22.xyz & int3(1,1,1);
            r22.yzw = (int3)r22.yzw;
            r23.xyz = r22.yzw * r9.xyz + r17.xyz;
            r20.w = r23.x * r23.y;
            r21.w = r20.w * r23.z;
            r22.yzw = r22.yzw + r11.xyz;
            r23.xyw = float3(64,32,64) * r22.yzw;
            r23.xyw = cmp(r23.xyw >= -r23.xyw);
            r24.xyz = r23.xyw ? float3(64,32,64) : float3(-64,-32,-64);
            r23.xyw = r23.xyw ? float3(0.015625,0.03125,0.015625) : float3(-0.015625,-0.03125,-0.015625);
            r22.yzw = r23.xyw * r22.yzw;
            r22.yzw = frac(r22.yzw);
            r22.yzw = r24.xyz * r22.yzw + r12.xyz;
            r22.yzw = r22.yzw + r6.xyz;
            r15.xyz = (uint3)r22.yzw;
            r15.xyz = t_irradiance_z_color_texture.Load(r15.xyzw).xyz;
            r19.w = r20.w * r23.z + r19.w;
            r14.xyz = r15.xyz * r21.www + r14.xyz;
            r22.x = (int)r22.x + 1;
          }
          r9.xyz = r14.xyz / r19.www;
          r9.xyz = r5.zzz * r9.xyz + r13.xyz;
          r11.xyz = r16.xyz + -r9.xyz;
          r16.xyz = r6.www * r11.xyz + r9.xyz;
        } else {
          r1.w = r6.w;
        }
      }
      r2.z = -1;
      r5.w = -1;
      break;
    }
    r4.w = (int)r4.w + 1;
    r5.w = r8.w;
    r16.xyz = float3(0,0,0);
    r1.w = 1;
    r2.z = 0;
  }
  r1.x = (int)r2.z & (int)r5.w;
  r0.w = r0.w ? r1.x : 0;
  r1.xyz = r16.xyz + -r0.xyz;
  r1.xyz = r1.www * r1.xyz + r0.xyz;
  r0.xyz = r0.www ? r1.xyz : r0.xyz;
  r0.xyz = sun_colour.xyz * float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * RtFogSun(v0.xy * g_render_target_dimensions.zw * g_screen_size.xy) + r0.xyz;
  r0.xyz += RtFogLights(v0.xy * g_render_target_dimensions.zw * g_screen_size.xy);
  r1.xyz = r4.xyz * r0.xyz;
  if (g_skydome_texture_array_enabled != 0) {
    r5.xyz = float3(0,0,0);
    r0.w = g_overlapping_volume_indices[0];
    r1.w = g_overlapping_volume_indices[1];
    r2.z = g_overlapping_volume_indices[2];
    r4.w = 0;
    while (true) {
      r5.w = cmp((uint)r4.w >= g_overlapping_volume_count);
      if (r5.w != 0) break;
      r6.xyzw = cmp((int4)r4.wwww == int4(0,1,2,3));
      r7.xyzw = cmp((int4)r4.wwww == int4(4,5,6,7));
      r5.w = r6.x ? r0.w : 0;
      r6.x = r6.y ? r1.w : 0;
      r5.w = (int)r5.w | (int)r6.x;
      r6.x = r6.z ? r2.z : 0;
      r5.w = (int)r5.w | (int)r6.x;
      r6.x = r6.w ? g_overlapping_volume_indices[3] : 0;
      r5.w = (int)r5.w | (int)r6.x;
      r6.x = r7.x ? g_overlapping_volume_indices[4] : 0;
      r5.w = (int)r5.w | (int)r6.x;
      r6.x = r7.y ? g_overlapping_volume_indices[5] : 0;
      r5.w = (int)r5.w | (int)r6.x;
      r6.x = r7.z ? g_overlapping_volume_indices[6] : 0;
      r5.w = (int)r5.w | (int)r6.x;
      r6.x = r7.w ? g_overlapping_volume_indices[7] : 0;
      r5.w = (int)r5.w | (int)r6.x;
      r6.x = cmp((int)r5.w == -1);
      if (r6.x != 0) {
        r5.xyz = g_overlapping_volume_weights[r4.w] * float3(1,0.409999996,0.699999988) + r5.xyz;
      } else {
        r3.w = (uint)r5.w;
        r6.xyz = t_environment_array.SampleLevel(s_environment_array_s, r3.xyzw, g_fog_far_skybox_mip).xyz;
        r5.xyz = r6.xyz * g_overlapping_volume_weights[r4.w] + r5.xyz;
      }
      r4.w = (int)r4.w + 1;
    }
  } else {
    r3.w = 0;
    r5.xyz = t_environment_array.SampleLevel(s_environment_array_s, r3.xyzw, g_fog_far_skybox_mip).xyz;
  }
  r2.xyz = saturate(g_fog_far_skybox_lerp * r2.xyw);
  r0.xyz = -r4.xyz * r0.xyz + r5.xyz;
  r0.xyz = r2.xyz * r0.xyz + r1.xyz;
  o0.xyz = g_fog_lerp * r0.xyz;
  o0.w = 0;
  return;
}