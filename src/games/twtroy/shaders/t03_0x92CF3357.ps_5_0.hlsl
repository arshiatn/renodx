// TROY t03 - hdr_to_screen. MSAA, DOF off, distortion off.
// Multipliers: SI.godrays, SI.bloom, SI.filmgrain.
// MSAA gbuffer; the glow depth mask averages coverage over g_num_of_samples.
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
SamplerState god_rays_sampler_s : register(s3);
SamplerState glow_sampler_s : register(s4);
SamplerState grain_sampler_s : register(s5);
SamplerState rain_sampler_s : register(s6);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t0);
Texture2D<float4> t_fog_of_war_mask : register(t1);
Texture2D<float4> frame_texture : register(t2);
Texture3D<float4> lut_texture : register(t3);
Texture2D<float4> god_rays_texture : register(t4);
Texture2D<float4> glow_texture1 : register(t5);
Texture2D<float4> grain_texture : register(t6);
Texture2D<float4> rain_texture : register(t7);

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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Threshold the float setting so a stray value cannot produce a hybrid mode.
  int hdr_mode = (HDR < 0.5f) ? 0 : ((HDR < 1.5f) ? 1 : 2);
  const bool is_hdr = (hdr_mode != 0);

  const float god_rays_multiplier    = is_hdr ? SI.godrays : 1.f;
  const float glow_multiplier        = is_hdr ? SI.bloom : 1.f;
  const float film_grain_multiplier  = is_hdr ? SI.filmgrain : 1.f;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r0.xy = overscan * r0.xy;
  r0.z = cmp(0 < disable_post_processing);
  if (r0.z != 0) {
    r1.xyzw = frame_texture.SampleLevel(frame_sampler_s, r0.xy, 0).xyzw;

    if (!is_hdr) {
      r1.xyz = saturate(g_brightness * r1.xyz);
      r1.xyz = log2(r1.xyz);
      r1.xyz = g_inv_gamma_output * r1.xyz;
      o0.xyz = exp2(r1.xyz);
      o0.w = r1.w;

      o0.xyzw = saturate(o0.xyzw);
      o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz, g_gamma_output);
      o0.xyz *= (SI.diffuse_white_nits / SI.graphics_white_nits);
      o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
      return;
    }

    // Nothing to bridge here, so the tone mapper runs on the raw handoff.
    float3 no_post_hdr = max(r1.xyz, 0.f.xxx);
    if (hdr_mode == 1) {
      no_post_hdr = ApplyTroyHDR1UserGrading(no_post_hdr);
      no_post_hdr = ApplyTroyHDR1PeakShoulder(no_post_hdr);
    } else {
      no_post_hdr = ApplyPsychoV30(no_post_hdr);
    }

    o0.xyz = no_post_hdr;
    o0.xyz *= (SI.diffuse_white_nits / SI.graphics_white_nits);
    o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
    o0.w = r1.w;
    return;
  }
  if (r0.z == 0) {
    r1.xyzw = frame_texture.SampleLevel(frame_sampler_s, r0.xy, 0).xyzw;
    r0.z = dot(r1.xyz, float3(0.330000013,0.560000002,0.109999999));
    r0.z = saturate(1 + -r0.z);
    r2.xy = g_render_target_dimensions.xy * r0.xy;
    r2.xy = g_viewport_dimensions.zw * r2.xy;
    r0.w = god_rays_texture.SampleLevel(god_rays_sampler_s, r2.xy, 0).x;
    r3.xyz = sun_colour_unscaled.xyz * r0.www;
    r3.xyz = (god_rays_strength * god_rays_multiplier) * r3.xyz;
    r3.xyz = r3.xyz * r0.zzz + r1.xyz;
    r2.zw = overscan * v0.xy;
    r4.xy = (int2)r2.zw;
    r4.zw = float2(0,0);
    r5.z = gbuffer_channel_4_texture_ms.Load(r4.xy, 0).x;
    r5.xy = r2.xy * float2(2,-2) + float2(-1,1);
    r5.w = 1;
    r2.x = dot(r5.xyzw, inv_view_projection._m00_m10_m20_m30);
    r2.y = dot(r5.xyzw, inv_view_projection._m01_m11_m21_m31);
    r2.z = dot(r5.xyzw, inv_view_projection._m02_m12_m22_m32);
    r0.w = dot(r5.xyzw, inv_view_projection._m03_m13_m23_m33);
    r2.xyz = r2.xyz / r0.www;
    r0.xyw = glow_texture1.SampleLevel(glow_sampler_s, r0.xy, 0).xyz;
    r0.z = r0.z * r0.z;
    r0.xyz = r0.xyw * r0.zzz * glow_multiplier;
    r0.w = cmp(0 != glow_depth_masking);
    if (r0.w != 0) {
      r0.w = cmp(r5.z == 1.000000);
      r0.w = r0.w ? 1.000000 : 0;
      r2.w = r0.w;
      r3.w = 1;
      while (true) {
        r5.x = cmp((int)r3.w >= g_num_of_samples);
        if (r5.x != 0) break;
        r5.x = gbuffer_channel_4_texture_ms.Load(r4.xy, r3.w).x;
        r5.x = cmp(r5.x == 1.000000);
        r5.x = r5.x ? 1.000000 : 0;
        r2.w = r5.x + r2.w;
        r3.w = (int)r3.w + 1;
      }
      r0.w = g_num_of_samples;
      r0.w = r2.w / r0.w;
      r0.w = 1 + -r0.w;
      r0.xyz = r0.xyz * r0.www;
    }
    r0.xyz = r3.xyz + r0.xyz;
    r0.w = cmp(0 < g_rain_color.w);
    if (r0.w != 0) {
      r3.xyz = -camera_position.xyz + r2.xyz;
      r0.w = dot(r3.xyz, r3.xyz);
      r2.y = sqrt(r0.w);
      r0.w = rsqrt(r0.w);
      r3.xyz = r3.xyz * r0.www;
      r4.x = dot(r3.xyz, g_rain_camera_mat_col1.xyz);
      r4.y = dot(r3.xyz, g_rain_camera_mat_col2.xyz);
      r4.z = dot(r3.xyz, g_rain_camera_mat_col3.xyz);
      r0.w = dot(r4.xyz, r4.xyz);
      r0.w = rsqrt(r0.w);
      r3.xyz = r4.xyz * r0.www;
      r4.xz = abs(r3.xz) * abs(r3.xz);
      r5.xy = cmp(float2(0,0) < r3.xz);
      r2.w = 3.5 * g_rain_direction.w;
      r0.w = saturate(-r4.y * r0.w + 0.899999976);
      r4.xy = -r4.xz * abs(r3.xz) + float2(1,1);
      r6.yzw = g_rain_camera_position.yxz;
      r3.w = 0;
      r4.zw = float2(0,0);
      while (true) {
        r5.z = cmp((int)r4.w >= rain_planes_count);
        if (r5.z != 0) break;
        r5.zw = frac(r6.zw);
        r7.xy = float2(1,1) + -r5.zw;
        r5.zw = r5.xy ? r7.xy : r5.zw;
        r7.xy = r5.zw * abs(r3.zx);
        r7.x = cmp(r7.y < r7.x);
        if (r7.x != 0) {
          r7.x = r5.w / abs(r3.z);
          r4.z = r7.x + r4.z;
          r7.xyz = r5.www * r3.xyx;
          r7.xyz = r7.xyz / abs(r3.zzz);
          r5.w = 0.00999999978 + r5.w;
          r5.w = r5.w * r3.z;
          r7.w = r5.w / abs(r3.z);
          r6.xyzw = r6.zyzw;
          r6.xyzw = r6.xyzw + r7.xyzw;
          r5.w = floor(r6.w);
          r5.w = 34 * r5.w;
          r5.w = sin(r5.w);
          r5.w = r5.w * 0.324000001 + 1;
          r7.xzw = float3(1.29999995,4.5999999,4.5999999) * r6.xzw;
          r8.x = r6.y * 0.899999976 + r2.w;
          r7.y = r8.x + r5.w;
          r5.w = rain_texture.Sample(rain_sampler_s, r7.xy).x;
          r7.xy = sin(r7.zw);
          r7.x = r7.x * r7.y;
          r7.y = g_rain_direction.w * 4.19999981 + r6.y;
          r7.y = 1.29999995 * r7.y;
          r7.y = sin(r7.y);
          r7.x = r7.x * r7.y;
          r7.x = max(0, r7.x);
          r7.x = r7.x * 0.800000012 + 0.200000003;
          r5.w = r7.x * r5.w;
          r7.x = 1.29999995 * r4.z;
          r7.x = min(1, r7.x);
          r7.y = saturate(r2.y * g_rain_camera_position.w + -r4.z);
          r7.x = r7.x * r7.y;
          r7.x = r7.x * r0.w;
          r7.x = r7.x * r4.x;
          r3.w = r5.w * r7.x + r3.w;
        } else {
          r5.w = r5.z / abs(r3.x);
          r4.z = r5.w + r4.z;
          r5.w = 0.00999999978 + r5.z;
          r5.w = r5.w * r3.x;
          r7.z = r5.w / abs(r3.x);
          r8.xyz = r5.zzz * r3.zyz;
          r7.xyw = r8.xyz / abs(r3.xxx);
          r6.xyzw = r6.wyzw;
          r6.xyzw = r6.xyzw + r7.xyzw;
          r5.z = floor(r6.z);
          r5.z = 34 * r5.z;
          r5.z = sin(r5.z);
          r5.z = r5.z * 0.324000001 + 1;
          r7.xzw = float3(1.29999995,4.5999999,4.5999999) * r6.xzw;
          r5.w = r6.y * 0.899999976 + r2.w;
          r7.y = r5.w + r5.z;
          r5.z = rain_texture.Sample(rain_sampler_s, r7.xy).x;
          r7.xy = sin(r7.zw);
          r5.w = r7.x * r7.y;
          r6.x = g_rain_direction.w * 4.19999981 + r6.y;
          r6.x = 1.29999995 * r6.x;
          r6.x = sin(r6.x);
          r5.w = r6.x * r5.w;
          r5.w = max(0, r5.w);
          r5.w = r5.w * 0.800000012 + 0.200000003;
          r5.z = r5.z * r5.w;
          r5.w = 1.29999995 * r4.z;
          r5.w = min(1, r5.w);
          r6.x = saturate(r2.y * g_rain_camera_position.w + -r4.z);
          r5.w = r6.x * r5.w;
          r5.w = r5.w * r0.w;
          r5.w = r5.w * r4.y;
          r3.w = r5.z * r5.w + r3.w;
        }
        r4.w = (int)r4.w + 1;
      }
      r0.w = 1 + g_rain_lightning.w;
      r0.w = r3.w * r0.w;
      r3.xyz = g_rain_color.xyz * r0.www;
      r0.xyz = r3.xyz * g_rain_color.www + r0.xyz;
    }
    r2.xw = r2.xz * float2(0.00048828125,0.00048828125) + float2(0.5,0.5);
    r0.w = 1 + -r2.w;
    r3.x = cmp(r2.x < 0);
    r3.y = cmp(1 < r2.x);
    r3.x = (int)r3.y | (int)r3.x;
    r3.y = cmp(r0.w < 0);
    r3.x = (int)r3.y | (int)r3.x;
    r0.w = cmp(1 < r0.w);
    r0.w = (int)r0.w | (int)r3.x;
    if (r0.w == 0) {
      r2.yz = float2(1,1) + -r2.xw;
      r0.w = t_fog_of_war_mask.SampleLevel(s_fog_of_war_mask_s, r2.xz, 0).x;
      r3.x = cmp(g_fog_of_war_outfield_blend < 1);
      r2.xyzw = float4(50,50,50,50) * r2.xyzw;
      r2.xyzw = min(float4(1,1,1,1), r2.xyzw);
      r2.xy = min(r2.xz, r2.yw);
      r2.x = min(r2.x, r2.y);
      r2.x = 1 + -r2.x;
      r2.x = max(r2.x, r0.w);
      r0.w = r3.x ? r2.x : r0.w;
      r2.x = 1 + -g_fog_of_war_blend;
      r0.w = max(r2.x, r0.w);
    } else {
      r0.w = -g_fog_of_war_outfield_blend * g_fog_of_war_blend + 1;
    }
    r2.x = dot(r0.xyz, float3(0.270000011,0.670000017,0.0599999987));
    r1.xyz = float3(0.25,0.25,0.25) * r2.xxx;
    r2.xyz = -r2.xxx * float3(0.25,0.25,0.25) + r0.xyz;
    r2.w = 0;
    r0.xyzw = r0.wwww * r2.xyzw + r1.xyzw;
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
  }
  return;
}