// fx_battlefog, legacy fog (ps40_main; the Darklands' and other maps with is_3d 0): the game's, Fog Strength on its opacity. Runs only with
// the slider off 1 (twwarhammer3/addon.cpp traced_lights); otherwise the game's own bytes do. Above 1 the opacity stops at full.
cbuffer rt_fog_strength : register(b8) {
  float rt_fog_strength_above_one;
};

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

cbuffer lighting_VS_PS : register(b1)
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
Texture2D<float4> gbuffer_channel_4_texture : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_screen_size.zw * v0.xy;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
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
  o0.w = min(o0.w * (1.f + rt_fog_strength_above_one), max(o0.w, 1.f));
  r1.w = max(0, r1.y);
  r0.yzw = t_sky.Sample(s_sky_s, r1.xwz).xyz;
  r1.xyz = g_legacy_volume_fog_colour.xyz * sun_colour.xyz;
  r1.xyz = float3(1.5,1.5,1.5) * r1.xyz;
  r1.xyz = abs(sun_direction.y) * r1.xyz;
  r0.yzw = -r1.xyz * float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) + r0.yzw;
  r1.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r1.xyz;
  o0.xyz = r0.xxx * r0.yzw + r1.xyz;
  return;
}