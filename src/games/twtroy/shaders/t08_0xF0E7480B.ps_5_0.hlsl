// TROY t08 - hdr_to_screen. Non-MSAA, DOF on, distortion off.
// Multipliers: SI.filmgrain.
// No god rays, no glow, no disable_post_processing branch. DOF spiral is stock.
// HDR paths identical to t01. Needs in-game Brightness 10, Gamma 22.

cbuffer colorimetry_VS_PS : register(b0)
{
  float g_brightness : packoffset(c0);
  float g_gamma_output : packoffset(c0.y);
  float g_inv_gamma_output : packoffset(c0.z);
}

cbuffer camera : register(b1)
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

cbuffer shared_fog_of_war_PS : register(b3)
{
  float g_fog_of_war_blend : packoffset(c0);
  float g_fog_of_war_outfield_blend : packoffset(c0.y);
}

cbuffer hdr_to_screen_PS : register(b4)
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
  float disable_post_processing : packoffset(c8.y);
  float lut_strength : packoffset(c8.z);
  float3 grain_tex_offset_enabled : packoffset(c9);
}

SamplerState s_fog_of_war_mask_s : register(s0);
SamplerState frame_sampler_s : register(s1);
SamplerState lut_sampler_s : register(s2);
SamplerState grain_sampler_s : register(s3);
SamplerState rain_sampler_s : register(s4);
Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<float4> t_fog_of_war_mask : register(t1);
Texture2D<float4> frame_texture : register(t2);
Texture3D<float4> lut_texture : register(t3);
Texture2D<float4> grain_texture : register(t4);
Texture2D<float4> rain_texture : register(t5);

// 3Dmigoto declarations
#define cmp -
#include "../shared.h"
#include "../psycho_test30.hlsli"

// The native matrix + LUT are authored for a bounded gamma-domain signal. Compress
// reversibly, grade in that domain, expand back. Same bridge as Warhammer 1/2.
float3 ApplyTroyNativeGradeHDRSafe(float3 grade_source_linear)
{
  float grade_scale =
      renodx::tonemap::neutwo::ComputeMaxChannelScale(grade_source_linear, 1.f, HDR_PEAK);
  float3 grade_proxy_linear = grade_source_linear * grade_scale;

  // Stock's saturate(g_brightness * c) is dropped: the proxy is already bounded, and
  // re-clamping would only reintroduce clipping ahead of the matrix and LUT.
  float3 grade_proxy_gamma = renodx::color::gamma::EncodeSafe(grade_proxy_linear);

  float4 matrix_input = float4(grade_proxy_gamma, 1.f);
  float3 matrix_output_gamma;
  matrix_output_gamma.x = dot(matrix_input, colour_matrix._m00_m10_m20_m30);
  matrix_output_gamma.y = dot(matrix_input, colour_matrix._m01_m11_m21_m31);
  matrix_output_gamma.z = dot(matrix_input, colour_matrix._m02_m12_m22_m32);

  // No saturate on the LUT coordinate - stock samples the raw matrix output.
  float3 lut_output_gamma =
      lut_texture.SampleLevel(lut_sampler_s, matrix_output_gamma, 0).xyz;
  float3 graded_gamma = lerp(matrix_output_gamma, lut_output_gamma, lut_strength);

  float3 graded_proxy_linear = renodx::color::gamma::DecodeSafe(graded_gamma);
  return renodx::math::DivideSafe(graded_proxy_linear, grade_scale.xxx, graded_proxy_linear);
}

// Mode 1 user grading. Identity at default settings.
float3 ApplyTroyHDR1UserGrading(float3 color)
{
  const float3 kLuminance = float3(0.212599993f, 0.715200007f, 0.0722000003f);
  const float mid_gray = 0.18f;

  float luminance = max(dot(color, kLuminance), 0.f);
  float graded_luminance = luminance * SI.exposure_tpm;
  graded_luminance = renodx::color::grade::Highlights(graded_luminance, SI.highlights_tpm, mid_gray);
  graded_luminance = renodx::color::grade::Shadows(graded_luminance, SI.shadows_tpm, mid_gray);

  if (SI.contrast_tpm != 1.f)
  {
    float normalized = max(graded_luminance / mid_gray, 0.f);
    graded_luminance = pow(normalized, SI.contrast_tpm) * mid_gray;
  }

  float tonal_scale = luminance > 0.000001f ? graded_luminance / luminance : 1.f;
  color *= tonal_scale;

  float graded_y = dot(color, kLuminance);
  return lerp(graded_y.xxx, color, SI.saturation_tpm);
}

// Mode 1 peak management: rolls everything above 1.0 into the display's headroom.
// Runs last, and leaves everything at or below 1.0 alone - that is the SDR image.
float3 ApplyTroyHDR1PeakShoulder(float3 color)
{
  float peak = max(HDR_PEAK, 1.0001f);
  float max_channel = max(color.r, max(color.g, color.b));

  if (max_channel > 1.f)
  {
    float headroom = peak - 1.f;
    float excess = max_channel - 1.f;
    float mapped_max = 1.f + renodx::tonemap::Neutwo(excess, headroom);
    color *= mapped_max / max_channel;
  }

  return color;
}

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Threshold the float setting so a stray value cannot produce a hybrid mode.
  int hdr_mode = (HDR < 0.5f) ? 0 : ((HDR < 1.5f) ? 1 : 2);
  const bool is_hdr = (hdr_mode != 0);

  const float film_grain_multiplier  = is_hdr ? SI.filmgrain : 1.f;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r0.xy = overscan * r0.xy;
  r1.xyzw = frame_texture.SampleLevel(frame_sampler_s, r0.xy, 0).xyzw;
  r0.z = -focus_distance + r1.w;
  r0.z = r0.z * r0.z;
  r0.w = dot(focal_length, focal_length);
  r0.z = r0.z / r0.w;
  r0.z = -1.44269502 * r0.z;
  r0.z = exp2(r0.z);
  r0.z = 1 + -r0.z;
  r2.x = 0.00066666666 * g_viewport_dimensions.x;
  r0.z = r0.z + r0.z;
  r3.xyzw = r1.xyzw;
  r2.yzw = float3(1,0.5,0);
  while (true) {
    r4.x = cmp(r2.z >= 4);
    if (r4.x != 0) break;
    sincos(r2.w, r4.x, r5.x);
    r5.y = r4.x;
    r4.xy = g_viewport_dimensions.zw * r5.xy;
    r4.xy = r4.xy * r2.zz;
    r4.xy = r4.xy * r2.xx + r0.xy;
    r4.xyzw = frame_texture.SampleLevel(frame_sampler_s, r4.xy, 0).xyzw;
    r5.x = -focus_distance + r4.w;
    r5.x = r5.x * r5.x;
    r5.x = r5.x / r0.w;
    r5.x = -1.44269502 * r5.x;
    r5.x = exp2(r5.x);
    r5.x = 1 + -r5.x;
    r5.y = cmp(r1.w < r4.w);
    r5.z = min(r5.x, r0.z);
    r5.x = r5.y ? r5.z : r5.x;
    r5.y = -0.5 + r2.z;
    r5.x = saturate(r5.x * 4 + -r5.y);
    r5.y = r5.x * -2 + 3;
    r5.x = r5.x * r5.x;
    r5.x = r5.y * r5.x;
    r6.xyzw = r3.xyzw / r2.yyyy;
    r4.xyzw = -r6.xyzw + r4.xyzw;
    r4.xyzw = r5.xxxx * r4.xyzw + r6.xyzw;
    r3.xyzw = r4.xyzw + r3.xyzw;
    r2.yw = float2(1,2.39996314) + r2.yw;
    r4.x = 0.5 / r2.z;
    r2.z = r4.x + r2.z;
  }
  r0.z = saturate(blur_kernel_scale);
  r2.xyzw = r3.xyzw * float4(0.0625,0.0625,0.0625,0.0625) + -r1.xyzw;
  r1.xyzw = r0.zzzz * r2.xyzw + r1.xyzw;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r2.xy = (uint2)r0.zw;
  r2.xy = r2.xy * r0.xy;
  r2.xy = floor(r2.xy);
  r2.xy = (int2)r2.xy;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r2.xy = max(int2(0,0), (int2)r2.xy);
  r2.xy = min((int2)r2.xy, (int2)r0.zw);
  r2.zw = float2(0,0);
  r2.z = gbuffer_channel_4_texture.Load(r2.xyz).x;
  r0.xy = g_render_target_dimensions.xy * r0.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r2.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r2.w = 1;
  r0.x = dot(r2.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r2.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r2.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r2.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r0.w = cmp(0 < g_rain_color.w);
  if (r0.w != 0) {
    r2.xyz = -camera_position.xyz + r0.xyz;
    r0.y = dot(r2.xyz, r2.xyz);
    r0.w = sqrt(r0.y);
    r0.y = rsqrt(r0.y);
    r2.xyz = r2.xyz * r0.yyy;
    r3.x = dot(r2.xyz, g_rain_camera_mat_col1.xyz);
    r3.y = dot(r2.xyz, g_rain_camera_mat_col2.xyz);
    r3.z = dot(r2.xyz, g_rain_camera_mat_col3.xyz);
    r0.y = dot(r3.xyz, r3.xyz);
    r0.y = rsqrt(r0.y);
    r2.xyz = r3.xyz * r0.yyy;
    r3.xz = abs(r2.xz) * abs(r2.xz);
    r4.xy = cmp(float2(0,0) < r2.xz);
    r2.w = 3.5 * g_rain_direction.w;
    r0.y = saturate(-r3.y * r0.y + 0.899999976);
    r3.xy = -r3.xz * abs(r2.xz) + float2(1,1);
    r5.yzw = g_rain_camera_position.yxz;
    r3.zw = float2(0,0);
    r4.z = 0;
    while (true) {
      r4.w = cmp((int)r4.z >= rain_planes_count);
      if (r4.w != 0) break;
      r6.xy = frac(r5.zw);
      r6.zw = float2(1,1) + -r6.xy;
      r6.xy = r4.xy ? r6.zw : r6.xy;
      r6.zw = r6.xy * abs(r2.zx);
      r4.w = cmp(r6.w < r6.z);
      if (r4.w != 0) {
        r4.w = r6.y / abs(r2.z);
        r3.w = r4.w + r3.w;
        r7.xyz = r6.yyy * r2.xyx;
        r7.xyz = r7.xyz / abs(r2.zzz);
        r4.w = 0.00999999978 + r6.y;
        r4.w = r4.w * r2.z;
        r7.w = r4.w / abs(r2.z);
        r5.xyzw = r5.zyzw;
        r5.xyzw = r5.xyzw + r7.xyzw;
        r4.w = floor(r5.w);
        r4.w = 34 * r4.w;
        r4.w = sin(r4.w);
        r4.w = r4.w * 0.324000001 + 1;
        r7.xzw = float3(1.29999995,4.5999999,4.5999999) * r5.xzw;
        r6.y = r5.y * 0.899999976 + r2.w;
        r7.y = r6.y + r4.w;
        r4.w = rain_texture.Sample(rain_sampler_s, r7.xy).x;
        r6.yz = sin(r7.zw);
        r6.y = r6.y * r6.z;
        r6.z = g_rain_direction.w * 4.19999981 + r5.y;
        r6.z = 1.29999995 * r6.z;
        r6.z = sin(r6.z);
        r6.y = r6.y * r6.z;
        r6.y = max(0, r6.y);
        r6.y = r6.y * 0.800000012 + 0.200000003;
        r4.w = r6.y * r4.w;
        r6.y = 1.29999995 * r3.w;
        r6.y = min(1, r6.y);
        r6.z = saturate(r0.w * g_rain_camera_position.w + -r3.w);
        r6.y = r6.y * r6.z;
        r6.y = r6.y * r0.y;
        r6.y = r6.y * r3.x;
        r3.z = r4.w * r6.y + r3.z;
      } else {
        r4.w = r6.x / abs(r2.x);
        r3.w = r4.w + r3.w;
        r4.w = 0.00999999978 + r6.x;
        r4.w = r4.w * r2.x;
        r7.z = r4.w / abs(r2.x);
        r6.xyz = r6.xxx * r2.zyz;
        r7.xyw = r6.xyz / abs(r2.xxx);
        r5.xyzw = r5.wyzw;
        r5.xyzw = r5.xyzw + r7.xyzw;
        r4.w = floor(r5.z);
        r4.w = 34 * r4.w;
        r4.w = sin(r4.w);
        r4.w = r4.w * 0.324000001 + 1;
        r6.xzw = float3(1.29999995,4.5999999,4.5999999) * r5.xzw;
        r5.x = r5.y * 0.899999976 + r2.w;
        r6.y = r5.x + r4.w;
        r4.w = rain_texture.Sample(rain_sampler_s, r6.xy).x;
        r6.xy = sin(r6.zw);
        r5.x = r6.x * r6.y;
        r6.x = g_rain_direction.w * 4.19999981 + r5.y;
        r6.x = 1.29999995 * r6.x;
        r6.x = sin(r6.x);
        r5.x = r6.x * r5.x;
        r5.x = max(0, r5.x);
        r5.x = r5.x * 0.800000012 + 0.200000003;
        r4.w = r5.x * r4.w;
        r5.x = 1.29999995 * r3.w;
        r5.x = min(1, r5.x);
        r6.x = saturate(r0.w * g_rain_camera_position.w + -r3.w);
        r5.x = r6.x * r5.x;
        r5.x = r5.x * r0.y;
        r5.x = r5.x * r3.y;
        r3.z = r4.w * r5.x + r3.z;
      }
      r4.z = (int)r4.z + 1;
    }
    r0.y = 1 + g_rain_lightning.w;
    r0.y = r3.z * r0.y;
    r2.xyz = g_rain_color.xyz * r0.yyy;
    r2.xyz = r2.xyz * g_rain_color.www + r1.xyz;
  } else {
    r2.xyz = r1.xyz;
  }
  r0.xw = r0.xz * float2(0.00048828125,0.00048828125) + float2(0.5,0.5);
  r2.w = 1 + -r0.w;
  r3.x = cmp(r0.x < 0);
  r3.y = cmp(1 < r0.x);
  r3.x = (int)r3.y | (int)r3.x;
  r3.y = cmp(r2.w < 0);
  r3.x = (int)r3.y | (int)r3.x;
  r2.w = cmp(1 < r2.w);
  r2.w = (int)r2.w | (int)r3.x;
  if (r2.w == 0) {
    r0.yz = float2(1,1) + -r0.xw;
    r2.w = t_fog_of_war_mask.SampleLevel(s_fog_of_war_mask_s, r0.xz, 0).x;
    r3.x = cmp(g_fog_of_war_outfield_blend < 1);
    r0.xyzw = float4(50,50,50,50) * r0.xyzw;
    r0.xyzw = min(float4(1,1,1,1), r0.xyzw);
    r0.xy = min(r0.xz, r0.yw);
    r0.x = min(r0.x, r0.y);
    r0.x = 1 + -r0.x;
    r0.x = max(r2.w, r0.x);
    r0.x = r3.x ? r0.x : r2.w;
    r0.y = 1 + -g_fog_of_war_blend;
    r0.x = max(r0.x, r0.y);
  } else {
    r0.x = -g_fog_of_war_outfield_blend * g_fog_of_war_blend + 1;
  }
  r0.y = dot(r2.xyz, float3(0.270000011,0.670000017,0.0599999987));
  r1.xyz = float3(0.25,0.25,0.25) * r0.yyy;
  r2.xyz = -r0.yyy * float3(0.25,0.25,0.25) + r2.xyz;
  r2.w = 0;
  r0.xyzw = r0.xxxx * r2.xyzw + r1.xyzw;
  r1.x = cmp(0 < grain_tex_offset_enabled.z);
  if (r1.x != 0) {
    r1.xy = v0.xy / g_screen_size.yy;
    r1.xy = grain_tex_offset_enabled.xy + r1.xy;
    r1.xy = float2(6,6) * r1.xy;
    r1.x = grain_texture.SampleLevel(grain_sampler_s, r1.xy, 0).x;
    r1.x = r1.x * 2 + -1;
    r1.xyz = r1.xxx * r0.xyz;
    r0.xyz = r1.xyz * (grain_tex_offset_enabled.zzz * film_grain_multiplier) + r0.xyz;
  }
  if (!is_hdr)
  {
    r0.xyz = saturate(g_brightness * r0.xyz);
    r0.xyz = log2(r0.xyz);
    r0.xyz = g_inv_gamma_output * r0.xyz;
    r1.xyz = exp2(r0.xyz);
    r1.w = 1;
    r0.x = dot(r1.xyzw, colour_matrix._m00_m10_m20_m30);
    r0.y = dot(r1.xyzw, colour_matrix._m01_m11_m21_m31);
    r0.z = dot(r1.xyzw, colour_matrix._m02_m12_m22_m32);
    r1.xyz = lut_texture.SampleLevel(lut_sampler_s, r0.xyz, 0).xyz;
    r1.xyz = r1.xyz + -r0.xyz;
    o0.xyz = lut_strength * r1.xyz + r0.xyz;
    o0.w = r0.w;

    // Restore the UNORM write clamp on the upgraded target.
    o0.xyzw = saturate(o0.xyzw);

    o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz, g_gamma_output);
    o0.xyz *= (SI.diffuse_white_nits / SI.graphics_white_nits);
    o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
    return;
  }

  // HDR: native matrix + LUT in their own domain, then the mode's tone mapper.
  float output_alpha = r0.w;
  float3 graded_hdr = ApplyTroyNativeGradeHDRSafe(max(r0.xyz, 0.f.xxx));

  if (hdr_mode == 1)
  {
    graded_hdr = ApplyTroyHDR1UserGrading(graded_hdr);
    graded_hdr = ApplyTroyHDR1PeakShoulder(graded_hdr);
  }
  else
  {
    graded_hdr = ApplyPsychoV30(graded_hdr);
  }

  o0.xyz = graded_hdr;
  o0.xyz *= (SI.diffuse_white_nits / SI.graphics_white_nits);
  o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
  o0.w = output_alpha;
  return;
  return;
}