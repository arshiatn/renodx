// AA off/FXAA + DOF off + Distortion on

// ALL  T02-T08 ARE BASED OF T01 FILE AND AI DID THOSE

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
}

cbuffer colorimetry_VS_PS : register(b1)
{
  float g_brightness : packoffset(c0);
  float g_gamma_output : packoffset(c0.y);
  float g_inv_gamma_output : packoffset(c0.z);
}

cbuffer shared_fog_of_war_PS : register(b2)
{
  float g_fog_of_war_blend : packoffset(c0);
  float g_fog_of_war_outfield_blend : packoffset(c0.y);
}

cbuffer hdr_to_screen_PS : register(b3)
{
  float radial_blur_strength : packoffset(c0);
  float2 radial_blur_position : packoffset(c0.y);
  float focus_distance : packoffset(c0.w);
  float focal_length : packoffset(c1);
  float blur_kernel_scale : packoffset(c1.y);
  float4x4 colour_matrix : packoffset(c2);
  float4 pos_transform : packoffset(c6);
  float4 sample_data_9x9[81] : packoffset(c7);
  float4 sample_data_5x5[25] : packoffset(c88);
  float4 sample_data_3x3[9] : packoffset(c113);
  float lut_weights[3] : packoffset(c122);
  float overscan : packoffset(c124.y);
}

SamplerState gbuffer_channel_4_sampler_s : register(s0);
SamplerState s_fog_of_war_mask_s : register(s1);
SamplerState frame_sampler_s : register(s2);
SamplerState distortion_sampler_s : register(s3);
SamplerState lut_sampler_1_s : register(s4);
SamplerState lut_sampler_2_s : register(s5);
SamplerState lut_sampler_3_s : register(s6);
Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<float4> t_fog_of_war_mask : register(t1);
Texture2D<float4> frame_texture : register(t2);
Texture2D<float4> distortion_texture : register(t3);
Texture3D<float4> lut_texture_1 : register(t4);
Texture3D<float4> lut_texture_2 : register(t5);
Texture3D<float4> lut_texture_3 : register(t6);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"
#include "../psycho_test30.hlsli"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r0.xy = overscan * r0.xy;
  r1.xyzw = distortion_texture.SampleLevel(distortion_sampler_s, r0.xy, 0).xyzw;
  r0.zw = float2(-0.498039216,-0.498039216) + r1.xy;
  // RenoDX distortion control: 0 = off, 1 = stock strength.
  // Keep the SDR fallback exact; only HDR uses the user-controlled multiplier.
  float uv_distortion_multiplier = (HDR == 1.f) ? SI.uvdistort : 1.f;
  r0.xy = r0.zw * (float2(0.0199999996,0.0199999996) * uv_distortion_multiplier) + r0.xy;
  r1.xyzw = frame_texture.SampleLevel(frame_sampler_s, r0.xy, 0).xyzw;
  r2.xyzw = gbuffer_channel_4_texture.SampleLevel(gbuffer_channel_4_sampler_s, r0.xy, 0).yzxw;
  r2.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r2.w = 1;
  r0.x = dot(r2.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.w = dot(r2.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.y = dot(r2.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xy = r0.xw / r0.yy;
  r0.xw = r0.xy * float2(0.00048828125,0.00048828125) + float2(0.5,0.5);
  r2.x = 1 + -r0.w;
  r2.y = cmp(r0.x < 0);
  r2.z = cmp(1 < r0.x);
  r2.y = (int)r2.z | (int)r2.y;
  r2.z = cmp(r2.x < 0);
  r2.y = (int)r2.z | (int)r2.y;
  r2.x = cmp(1 < r2.x);
  r2.x = (int)r2.x | (int)r2.y;
  if (r2.x == 0) {
    r0.yz = float2(1,1) + -r0.xw;
    r2.xyzw = t_fog_of_war_mask.SampleLevel(s_fog_of_war_mask_s, r0.xz, 0).xyzw;
    r2.y = cmp(g_fog_of_war_outfield_blend < 1);
    r0.xyzw = float4(50,50,50,50) * r0.xyzw;
    r0.xyzw = min(float4(1,1,1,1), r0.xyzw);
    r0.xy = min(r0.xz, r0.yw);
    r0.x = min(r0.x, r0.y);
    r0.x = 1 + -r0.x;
    r0.x = max(r2.x, r0.x);
    r0.x = r2.y ? r0.x : r2.x;
    r0.y = 1 + -g_fog_of_war_blend;
    r0.x = max(r0.x, r0.y);
  } else {
    r0.x = -g_fog_of_war_outfield_blend * g_fog_of_war_blend + 1;
  }
  r0.y = dot(r1.xyz, float3(0.270000011,0.670000017,0.0599999987));
  r2.xyz = float3(0.25,0.25,0.25) * r0.yyy;
  r2.w = r1.w;
  r1.xyzw = -r2.zzzw + r1.xyzw;
  r0.xyzw = r0.xxxx * r1.xyzw + r2.xyzw;
  // Exact stock SDR output path.
  if (HDR == 0)
  {
    r0.xyz = saturate(g_brightness * r0.xyz);
    r0.xyz = log2(r0.xyz);
    r0.xyz = g_inv_gamma_output * r0.xyz;
    r1.xyz = exp2(r0.xyz);
    r1.w = 1;
    r0.x = dot(r1.xyzw, colour_matrix._m00_m10_m20_m30);
    r0.y = dot(r1.xyzw, colour_matrix._m01_m11_m21_m31);
    r0.z = dot(r1.xyzw, colour_matrix._m02_m12_m22_m32);
    r1.xyzw = lut_texture_1.SampleLevel(lut_sampler_1_s, r0.xyz, 0).xyzw;
    r2.xyzw = lut_texture_2.SampleLevel(lut_sampler_2_s, r0.xyz, 0).xyzw;
    r2.xyz = lut_weights[1] * r2.xyz;
    r1.xyz = r1.xyz * lut_weights[0] + r2.xyz;
    r2.xyzw = lut_texture_3.SampleLevel(lut_sampler_3_s, r0.xyz, 0).xyzw;
    o0.xyz = r2.xyz * lut_weights[2] + r1.xyz;
    o0.w = r0.w;

    o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz, g_gamma_output);
    o0.xyz *= (SI.diffuse_white_nits / SI.graphics_white_nits);
    o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
    return;
  }

  // HDR path.
  float output_alpha = r0.w;
  float3 hdr_color = max(r0.xyz, 0.f.xxx);

  // The stock matrix and 3D LUTs expect bounded gamma-domain coordinates.
  // Compress the HDR signal reversibly, apply both native grading stages in
  // their expected domain, then expand back to HDR before PsychoV30.
  float grade_scale =
      renodx::tonemap::neutwo::ComputeMaxChannelScale(hdr_color, 1.f, HDR_PEAK);
  float3 grade_input_linear = hdr_color * grade_scale;
  float3 grade_input_gamma =
      renodx::color::gamma::EncodeSafe(grade_input_linear);

  float4 matrix_input = float4(grade_input_gamma, 1.f);
  float3 matrix_output_gamma;
  matrix_output_gamma.x =
      dot(matrix_input, colour_matrix._m00_m10_m20_m30);
  matrix_output_gamma.y =
      dot(matrix_input, colour_matrix._m01_m11_m21_m31);
  matrix_output_gamma.z =
      dot(matrix_input, colour_matrix._m02_m12_m22_m32);

  float3 lut_1 =
      lut_texture_1.SampleLevel(lut_sampler_1_s, matrix_output_gamma, 0).xyz;
  float3 lut_2 =
      lut_texture_2.SampleLevel(lut_sampler_2_s, matrix_output_gamma, 0).xyz;
  float3 lut_3 =
      lut_texture_3.SampleLevel(lut_sampler_3_s, matrix_output_gamma, 0).xyz;

  float3 graded_gamma =
      lut_1 * lut_weights[0]
      + lut_2 * lut_weights[1]
      + lut_3 * lut_weights[2];

  float3 graded_proxy =
      renodx::color::gamma::DecodeSafe(graded_gamma);
  float3 graded_hdr =
      renodx::math::DivideSafe(graded_proxy, grade_scale.xxx, graded_proxy);

  // Keep Warhammer's matrix/LUT grade before the final HDR tone mapper.
  o0.xyz = ApplyPsychoV30(graded_hdr);
  o0.xyz *= (SI.diffuse_white_nits / SI.graphics_white_nits);
  o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
  o0.w = output_alpha;
  return;
}