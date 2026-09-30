// ---- Created with 3Dmigoto v1.3.16 on Fri Sep 25 20:28:08 2026

// WARHAMMER 3 t00 - hdr_to_screen: tonemap + grading in one pass (WH2 splits it in t00/t01).
// Stock: distortion composite -> scene + bloom -> Extended Reinhard on luminance
// (exposure = brightness^2 from auto exposure or manual, white^2 = 1/burn - 0.999)
// -> saturate(g_brightness * x) -> pow(1/gamma) -> colour_matrix -> 3 weighted LUTs
// -> secondary LUT set blended in by camera distance on flagged G-buffer pixels.
// HDR 0: stock, then Paper White.
// HDR 1 (Extended): unclipped Reinhard -> HDR-safe native grade -> user grading -> shoulder.
// HDR 2 (Psycho): Reinhard dropped, scene-linear with SDR's mid grey pinned to 0.18
//        -> HDR-safe native grade -> PsychoV30.
// Sliders (HDR only): SI.bloom, SI.uvdistort.

struct Auto_exposure_output
{
    float tone_mapper_brightness;  // Offset:    0
};

cbuffer colorimetry_VS_PS : register(b0)
{
  float g_brightness : packoffset(c0);
  float g_gamma_output : packoffset(c0.y);
  float g_inv_gamma_output : packoffset(c0.z);
}

cbuffer camera : register(b1)
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

cbuffer lighting_VS_PS : register(b2)
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

cbuffer tone_mapping : register(b3)
{
  float g_tone_mapping_brightness : packoffset(c0);
  float g_tone_mapping_burn : packoffset(c0.y);
  int g_use_auto_exposure : packoffset(c0.z);
}

cbuffer hdr_to_screen_PS : register(b4)
{
  float4x4 colour_matrix : packoffset(c0);
  float lut_weights[3] : packoffset(c4);
  float overscan : packoffset(c6.y);
  int require_distortion_composition : packoffset(c6.z);
  float near_lut_strength : packoffset(c6.w);
  float far_lut_strength : packoffset(c7);
  float near_lut_distance : packoffset(c7.y);
  float far_lut_distance : packoffset(c7.z);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState g_hdr_rgb_bloom_texture_sampler_s : register(s1);
SamplerState distortion_sampler_s : register(s2);
SamplerState lut_sampler_s : register(s3);
Texture2D<float4> gbuffer_channel_3_texture : register(t0);
Texture2D<float4> gbuffer_channel_4_texture : register(t1);
Texture2D<float4> g_hdr_rgb_texture : register(t2);
Texture2D<float4> g_hdr_rgb_bloom_texture : register(t3);
StructuredBuffer<Auto_exposure_output> g_auto_exposure_input_buffer : register(t4);
Texture2D<float4> distortion_texture : register(t5);
Texture3D<float4> lut_texture_1 : register(t6);
Texture3D<float4> lut_texture_2 : register(t7);
Texture3D<float4> lut_texture_3 : register(t8);
Texture3D<float4> lut_secondary_texture_1 : register(t9);
Texture3D<float4> lut_secondary_texture_2 : register(t10);
Texture3D<float4> lut_secondary_texture_3 : register(t11);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"
#include "../psycho_test30.hlsli"


// Stock LUT stage: 3 weighted LUTs, secondary set lerped in (gamma domain) where flagged.
float3 SampleNativeLuts(float3 coord, bool use_secondary, float secondary_strength)
{
  float3 graded = lut_texture_1.SampleLevel(lut_sampler_s, coord, 0).xyz * lut_weights[0]
                  + lut_texture_2.SampleLevel(lut_sampler_s, coord, 0).xyz * lut_weights[1]
                  + lut_texture_3.SampleLevel(lut_sampler_s, coord, 0).xyz * lut_weights[2];
  [branch]
  if (use_secondary)
  {
    float3 secondary = lut_secondary_texture_1.SampleLevel(lut_sampler_s, coord, 0).xyz * lut_weights[0]
                       + lut_secondary_texture_2.SampleLevel(lut_sampler_s, coord, 0).xyz * lut_weights[1]
                       + lut_secondary_texture_3.SampleLevel(lut_sampler_s, coord, 0).xyz * lut_weights[2];
    graded = lerp(graded, secondary, secondary_strength);
  }
  return graded;
}

// Stock colour matrix on a gamma-encoded colour.
float3 ApplyColourMatrix(float3 color_gamma)
{
  float4 matrix_input = float4(color_gamma, 1.f);
  return float3(
      dot(matrix_input, colour_matrix._m00_m10_m20_m30),
      dot(matrix_input, colour_matrix._m01_m11_m21_m31),
      dot(matrix_input, colour_matrix._m02_m12_m22_m32));
}

// WH2 bridge: the LUTs are authored for 0..1, so normalize only when a channel is above 1,
// grade that proxy with the game's own brightness/gamma, then restore the magnitude.
// g_brightness is applied before the LUT, so the check includes it (no flat spot if raised).
float3 ApplyNativeGradeHDRSafe(float3 grade_source_linear, bool use_secondary, float secondary_strength)
{
  float grade_max_channel = g_brightness * max(grade_source_linear.r, max(grade_source_linear.g, grade_source_linear.b));
  float grade_scale = (grade_max_channel > 1.f) ? rcp(grade_max_channel) : 1.f;
  float3 grade_proxy_linear = grade_source_linear * grade_scale;

  float3 grade_proxy_gamma = pow(saturate(g_brightness * grade_proxy_linear), g_inv_gamma_output.xxx);
  float3 graded_gamma = SampleNativeLuts(ApplyColourMatrix(grade_proxy_gamma), use_secondary, secondary_strength);

  float3 graded_proxy_linear = renodx::color::gamma::DecodeSafe(graded_gamma, g_gamma_output);
  return renodx::math::DivideSafe(graded_proxy_linear, grade_scale.xxx, graded_proxy_linear);
}

// Extended user grading (WH2). Identity at defaults.
float3 ApplyHDR1UserGrading(float3 color)
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

  // Luma-preserving saturation. 1.0 is identity.
  float graded_y = dot(color, kLuminance);
  return lerp(graded_y.xxx, color, SI.saturation_tpm);
}

// Extended peak: rolls everything above 1.0 into the headroom. At or below 1.0 untouched.
float3 ApplyHDR1PeakShoulder(float3 color)
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
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Threshold the float setting so a stray value cannot produce a hybrid mode.
  int hdr_mode = (HDR < 0.5f) ? 0 : ((HDR < 1.5f) ? 1 : 2);
  float distortion_scale = (hdr_mode != 0) ? SI.uvdistort : 1.f;
  float bloom_scale = (hdr_mode != 0) ? SI.bloom : 1.f;

  // Distortion composite (stock, offsets scaled by the slider).
  r0.xy = g_screen_size.zw * v0.xy;
  r0.zw = overscan * r0.xy;
  r1.xy = overscan * g_screen_size.zw;
  r1.zw = float2(0.5,0.5) * r1.xy;
  r0.xy = r0.xy * overscan + -r1.zw;
  r2.xyzw = distortion_texture.SampleLevel(distortion_sampler_s, r0.xy, 0).xyzw;
  r1.zw = r2.ww * -r2.xy + r2.xy;
  r1.zw = r1.zw * (0.0199999996 * distortion_scale) + r0.xy;
  r3.x = cmp(distortion_blendmode == 1);
  r3.y = cmp(r2.z < 0.00100000005);
  r2.z = r3.y ? 1 : r2.z;
  r2.xy = r2.xy / r2.zz;
  r2.z = 1 + -r2.w;
  r2.xy = r2.zz * r2.xy;
  r2.xy = r2.ww * -r2.xy + r2.xy;
  r2.xy = r2.xy * (0.0199999996 * distortion_scale) + r0.xy;
  r0.xy = r3.xx ? r2.xy : r0.xy;
  r0.xy = distortion_blendmode ? r0.xy : r1.zw;
  r0.xy = r1.xy * float2(0.5,0.5) + r0.xy;
  r0.xy = require_distortion_composition ? r0.xy : r0.zw;
  float2 uv = r0.xy;

  // Scene + bloom (bloom slider, HDR only).
  float3 scene = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, uv, 0).xyz;
  float3 bloom = g_hdr_rgb_bloom_texture.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s, uv, 0).xyz;
  float3 color = bloom * bloom_scale + scene;

  // Exposure: auto exposure or manual, squared (stock).
  float brightness = g_use_auto_exposure ? g_auto_exposure_input_buffer[0].tone_mapper_brightness
                                         : g_tone_mapping_brightness;
  float exposure = brightness * brightness;
  float y = dot(color, float3(0.212599993,0.715200007,0.0722000003));
  float white_sq = 1 / g_tone_mapping_burn + -0.999000013;

  // Stock alpha: 0 where the scene is black, else 1.
  float output_alpha = (y == 0.f) ? 0.f : 1.f;

  float3 hdr_color;
  if (hdr_mode != 2) {
    // Extended Reinhard on luminance (stock). Unclipped it continues above 1 past white.
    float x = exposure * y;
    float mapped = x * (1 + x / white_sq) / (exposure * y + 1);
    hdr_color = (y == 0.f) ? 0.f : color * mapped / y;
  } else {
    // Reinhard dropped. Solves x(1 + x/W)/(1 + x) = 0.18 so SDR's mid grey arrives at 0.18.
    float inv_w = 1.f / max(white_sq, 0.0001f);
    float x_mid = 0.36f / (0.82f + sqrt(0.6724f + 0.72f * inv_w));
    hdr_color = color * (exposure * 0.18f / x_mid);
  }

  // Secondary LUT strength: flagged pixels only, smoothstep over camera distance (stock).
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r2.xy = (uint2)r0.zw;
  r2.xy = r2.xy * uv;
  r2.xy = floor(r2.xy);
  r2.xy = (int2)r2.xy;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r2.xy = max(int2(0,0), (int2)r2.xy);
  r2.xy = min((int2)r2.xy, (int2)r0.zw);
  r2.zw = float2(0,0);
  r0.z = gbuffer_channel_3_texture.Load(r2.xyw).w;
  r0.z = 255 * r0.z;
  r0.z = round(r0.z);
  r0.z = (uint)r0.z;
  r0.z = (int)r0.z & 2;
  bool use_secondary = (r0.z != 0);
  float secondary_strength = 0.f;
  if (use_secondary) {
    r2.z = gbuffer_channel_4_texture.Load(r2.xyz).x;
    r2.xy = uv * float2(2,-2) + float2(-1,1);
    r2.w = 1;
    r0.x = dot(r2.xyzw, inv_view_projection._m00_m10_m20_m30);
    r0.y = dot(r2.xyzw, inv_view_projection._m01_m11_m21_m31);
    r0.z = dot(r2.xyzw, inv_view_projection._m02_m12_m22_m32);
    r0.w = dot(r2.xyzw, inv_view_projection._m03_m13_m23_m33);
    r0.xyz = r0.xyz / r0.www;
    r0.xyz = -camera_position.xyz + r0.xyz;
    r0.x = dot(r0.xyz, r0.xyz);
    r0.x = sqrt(r0.x);
    r0.y = far_lut_distance + -near_lut_distance;
    r0.x = -near_lut_distance + r0.x;
    r0.y = 1 / r0.y;
    r0.x = saturate(r0.x * r0.y);
    r0.y = r0.x * -2 + 3;
    r0.x = r0.x * r0.x;
    r0.x = r0.y * r0.x;
    r0.y = far_lut_strength + -near_lut_strength;
    secondary_strength = r0.x * r0.y + near_lut_strength;
  }

  if (hdr_mode == 0) {
    // Stock SDR grade.
    r3.xyz = saturate(g_brightness * hdr_color);
    r3.xyz = log2(r3.xyz);
    r3.xyz = g_inv_gamma_output * r3.xyz;
    r3.xyz = exp2(r3.xyz);
    o0.xyz = SampleNativeLuts(ApplyColourMatrix(r3.xyz), use_secondary, secondary_strength);
    o0.w = output_alpha;

    // Undo the game's gamma, scene to Paper White, encode for the 2.2 swapchain.
    o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz, g_gamma_output);
    o0.xyz *= SI.diffuse_white_nits / SI.graphics_white_nits;
    o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
    return;
  }

  float3 graded_hdr = ApplyNativeGradeHDRSafe(hdr_color, use_secondary, secondary_strength);

  if (hdr_mode == 1) {
    graded_hdr = ApplyHDR1UserGrading(graded_hdr);
    graded_hdr = ApplyHDR1PeakShoulder(graded_hdr);
  } else {
    graded_hdr = ApplyPsychoV30(graded_hdr);
  }

  o0.xyz = graded_hdr * (SI.diffuse_white_nits / SI.graphics_white_nits);
  o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
  o0.w = output_alpha;
  return;
}
