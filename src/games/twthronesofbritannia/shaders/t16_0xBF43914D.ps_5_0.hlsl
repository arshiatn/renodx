// AA 2x/4x/8x + DOF on + Distoriton on + Filmgrain on

cbuffer camera_VS_PS : register(b0)
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
  float4 g_render_target_dimensions : packoffset(c30);
  float4 g_camera_temp0 : packoffset(c31);
  float4 g_camera_temp1 : packoffset(c32);
  float4 g_camera_temp2 : packoffset(c33);
  float4 g_clip_rect : packoffset(c34);
  int g_num_of_samples : packoffset(c35);
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
  float2 grain_tex_offset : packoffset(c1.z);
  float4x4 colour_matrix : packoffset(c2);
  float4 pos_transform : packoffset(c6);
  float4 sample_data_9x9[81] : packoffset(c7);
  float4 sample_data_5x5[25] : packoffset(c88);
  float4 sample_data_3x3[9] : packoffset(c113);
}

SamplerState gbuffer_channel_4_sampler_s : register(s0);
SamplerState s_fog_of_war_mask_s : register(s1);
SamplerState frame_sampler_s : register(s2);
SamplerState distortion_sampler_s : register(s3);
SamplerState grain_sampler_s : register(s4);
SamplerState god_rays_sampler_s : register(s5);
Texture2D<float4> distortion_sampler : register(t0);
Texture2D<float4> frame_sampler : register(t1);
Texture2D<float4> god_rays_sampler : register(t2);
Texture2D<float4> gbuffer_channel_4_sampler : register(t3);
Texture2D<float4> s_fog_of_war_mask : register(t4);
Texture2D<float4> grain_sampler : register(t5);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t6);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"
#include "../psycho_test30.hlsli"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r0.zw = distortion_sampler.SampleLevel(distortion_sampler_s, r0.xy, r0.y).xy;
  r0.zw = float2(-0.498039216,-0.498039216) + r0.zw;
  float distortion_multiplier = (HDR == 1.f) ? SI.uvdistort : 1.f;
  r0.xy =
      r0.zw
      * (float2(0.0199999996,0.0199999996) * distortion_multiplier)
      + r0.xy;
  r1.xy = (int2)v0.xy;
  r1.zw = float2(0,0);
  r2.z = gbuffer_channel_4_texture_ms.Load(r1.xy, 0).x;
  r0.z = cmp(1 < g_num_of_samples);
  if (r0.z != 0) {
    r0.z = gbuffer_channel_4_texture_ms.Load(r1.xy, 1).x;
    r2.z = min(r2.z, r0.z);
    r0.z = 2;
  } else {
    r0.z = 1;
  }
  r0.w = cmp((uint)r0.z < g_num_of_samples);
  if (r0.w != 0) {
    r3.x = gbuffer_channel_4_texture_ms.Load(r1.xy, 2).x;
    r2.z = min(r3.x, r2.z);
    r0.z = 3;
  }
  r3.x = cmp((uint)r0.z < g_num_of_samples);
  r0.w = r0.w ? r3.x : 0;
  if (r0.w != 0) {
    r3.x = gbuffer_channel_4_texture_ms.Load(r1.xy, 3).x;
    r2.z = min(r3.x, r2.z);
    r0.z = 4;
  }
  r3.x = cmp((uint)r0.z < g_num_of_samples);
  r0.w = r0.w ? r3.x : 0;
  if (r0.w != 0) {
    r3.x = gbuffer_channel_4_texture_ms.Load(r1.xy, 4).x;
    r2.z = min(r3.x, r2.z);
    r0.z = 5;
  }
  r3.x = cmp((uint)r0.z < g_num_of_samples);
  r0.w = r0.w ? r3.x : 0;
  if (r0.w != 0) {
    r3.x = gbuffer_channel_4_texture_ms.Load(r1.xy, 5).x;
    r2.z = min(r3.x, r2.z);
    r0.z = 6;
  }
  r3.x = cmp((uint)r0.z < g_num_of_samples);
  r0.w = r0.w ? r3.x : 0;
  if (r0.w != 0) {
    r3.x = gbuffer_channel_4_texture_ms.Load(r1.xy, 6).x;
    r2.z = min(r3.x, r2.z);
    r0.z = 7;
  }
  r0.z = cmp((uint)r0.z < g_num_of_samples);
  r0.z = r0.z ? r0.w : 0;
  if (r0.z != 0) {
    r0.z = gbuffer_channel_4_texture_ms.Load(r1.xy, 7).x;
    r2.z = min(r2.z, r0.z);
  }
  r2.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r2.w = 1;
  r0.z = dot(r2.xyzw, inv_projection._m02_m12_m22_m32);
  r0.w = dot(r2.xyzw, inv_projection._m03_m13_m23_m33);
  r0.z = r0.z / r0.w;
  r0.w = cmp(0 >= r0.z);
  r0.z = r0.w ? 1 : r0.z;
  r0.w = min(500, focus_distance);
  r0.z = r0.z + -r0.w;
  r0.z = r0.z * r0.z;
  r0.w = dot(focal_length, focal_length);
  r0.z = r0.z / r0.w;
  r0.z = -1.44269502 * r0.z;
  r0.z = exp2(r0.z);
  r0.z = 1 + -r0.z;
  r1.xyzw = float4(0,0,0,0);
  r0.w = 0;
  r3.x = 0;
  while (true) {
    r3.y = cmp((int)r3.x >= 25);
    if (r3.y != 0) break;
    r3.yz = -sample_data_5x5[r3.x].xy * r0.zz + r0.xy;
    r4.xyzw = frame_sampler.SampleLevel(frame_sampler_s, r3.yz, r3.z).xyzw;
    float4 hdr_dof_sample = sample_data_5x5[r3.x].zzzz * r4.xyzw;
    r4.xyzw =
        (HDR == 1.f)
            ? max(hdr_dof_sample, 0.f.xxxx)
            : saturate(hdr_dof_sample);
    r1.xyzw = r4.xyzw + r1.xyzw;
    r0.w = sample_data_5x5[r3.x].z + r0.w;
    r3.x = (int)r3.x + 1;
  }
  r3.xyzw = frame_sampler.SampleLevel(frame_sampler_s, r0.xy, r0.y).xyzw;
  r1.xyzw = r1.xyzw / r0.wwww;
  r1.xyzw = r1.xyzw + -r3.xyzw;
  r1.xyzw = blur_kernel_scale * r1.xyzw + r3.xyzw;
  r0.z = god_rays_sampler.Sample(god_rays_sampler_s, r0.xy).x;
  float god_rays_multiplier = (HDR == 1.f) ? SI.godrays : 1.f;
  r3.xyz =
      r0.zzz
      * (float3(0.100000001,0.100000001,0.100000001) * god_rays_multiplier)
      + r1.xyz;
  r2.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.xy, r0.y).x;
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
    r2.x = s_fog_of_war_mask.SampleLevel(s_fog_of_war_mask_s, r0.xz, 0).x;
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
  r0.y = dot(r3.xyz, float3(0.270000011,0.670000017,0.0599999987));
  r1.xyz = float3(0.25,0.25,0.25) * r0.yyy;
  r2.xyz = -r0.yyy * float3(0.25,0.25,0.25) + r3.xyz;
  r2.w = 0;
  r0.xyzw = r0.xxxx * r2.xyzw + r1.xyzw;
  r1.xy = v0.xy * g_screen_size.ww + grain_tex_offset.xy;
  r1.xy = float2(8,8) * r1.xy;
  r1.x = grain_sampler.Sample(grain_sampler_s, r1.xy).y;
  r1.xyz = r1.xxx * r0.xyz;
  // SDR: preserve Britannia's stock film-grain, brightness, gamma and matrix.
  if (HDR != 1.f)
  {
    r0.xyz = r1.xyz * float3(3.5,3.5,3.5) + r0.xyz;
    r0.xyz = g_brightness * r0.xyz;
    r0.xyz = saturate(float3(0.613496959,0.613496959,0.613496959) * r0.xyz);
    r0.xyz = log2(r0.xyz);
    r0.xyz = g_inv_gamma_output * r0.xyz;
    r1.xyz = exp2(r0.xyz);
    r1.w = 1;
    o0.x = dot(r1.xyzw, colour_matrix._m00_m10_m20_m30);
    o0.y = dot(r1.xyzw, colour_matrix._m01_m11_m21_m31);
    o0.z = dot(r1.xyzw, colour_matrix._m02_m12_m22_m32);
    o0.w = r0.w;

    // Preserve the original normalized-target clamp after upgrading the target.
    o0.xyzw = saturate(o0.xyzw);

    o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz, g_gamma_output);
    o0.xyz *= (SI.diffuse_white_nits / SI.graphics_white_nits);
    o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz, 2.0f);
    return;
  }

  // HDR film grain: 0 = off. Stock centres it on 0.18 (0.6135 = 1/(1+3.5*0.18));
  // use the texture's real mean so grain doesn't brighten the image.
  uint grain_w, grain_h, grain_mips;
  grain_sampler.GetDimensions(0, grain_w, grain_h, grain_mips);
  float grain_mean = (grain_mips > 1) ? grain_sampler.Load(int3(0, 0, grain_mips - 1)).y : 0.5f;
  float3 hdr_base_before_grain = r0.xyz;
  float3 hdr_filmgrain =
      (r1.xyz * float3(3.5,3.5,3.5) + hdr_base_before_grain)
      / (1.f + 3.5f * grain_mean);
  r0.xyz =
      lerp(hdr_base_before_grain, hdr_filmgrain, SI.filmgrain);

  // ---------------------------------------------------------------------------
  // HDR path: matches the known-good t01 implementation.
  // Grading domain is gamma 2.0, the same as stock at in-game gamma 100.
  // ---------------------------------------------------------------------------
  float output_alpha = r0.w;
  float3 hdr_color = max(r0.xyz, 0.f.xxx);

  float matrix_scale =
      renodx::tonemap::neutwo::ComputeMaxChannelScale(
          hdr_color, 1.f, HDR_PEAK);
  float3 matrix_input_linear = hdr_color * matrix_scale;
  float3 matrix_input_gamma =
      renodx::color::gamma::EncodeSafe(matrix_input_linear, 2.0f);

  float4 matrix_input = float4(matrix_input_gamma, 1.f);

  float3 matrix_output_gamma;
  matrix_output_gamma.x =
      dot(matrix_input, colour_matrix._m00_m10_m20_m30);
  matrix_output_gamma.y =
      dot(matrix_input, colour_matrix._m01_m11_m21_m31);
  matrix_output_gamma.z =
      dot(matrix_input, colour_matrix._m02_m12_m22_m32);

  float3 matrix_output_linear =
      renodx::color::gamma::DecodeSafe(matrix_output_gamma, 2.0f);

  float3 graded_hdr =
      renodx::math::DivideSafe(
          matrix_output_linear,
          matrix_scale.xxx,
          matrix_output_linear);

  o0.xyz = ApplyPsychoV30(graded_hdr);
  o0.xyz *= (SI.diffuse_white_nits / SI.graphics_white_nits);
  o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz, 2.0f);
  o0.w = output_alpha;
  return;
}
