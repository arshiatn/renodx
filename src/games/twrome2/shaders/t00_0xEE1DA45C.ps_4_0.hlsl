// ---- Created with 3Dmigoto v1.3.16 on Fri Sep 11 13:36:14 2026

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
  float4 g_camera_temp0 : packoffset(c30);
  float4 g_camera_temp1 : packoffset(c31);
  float4 g_camera_temp2 : packoffset(c32);
  float4 g_clip_rect : packoffset(c33);
  float g_hide_foliage : packoffset(c34);
}

cbuffer vignette_buffer : register(b1)
{
  float g_recip_half_screen_diag : packoffset(c0);
  float g_vignette_enable : packoffset(c0.y);
}

SamplerState g_hdr_rgb_texture_sampler_s : register(s0);
SamplerState g_black_and_white_points_sampler_s : register(s1);
SamplerState g_hdr_rgb_bloom_texture_sampler_s : register(s2);
SamplerState g_scurve_texture_sampler_s : register(s3);
Texture2D<float4> g_black_and_white_points_sampler : register(t0);
Texture2D<float4> g_hdr_rgb_texture_sampler : register(t1);
Texture2D<float4> g_hdr_rgb_bloom_texture_sampler : register(t2);
Texture2D<float4> g_scurve_texture_sampler : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "../tone_mapper.hlsli"
// The bloom: a smooth cubic sample of its small texture (bloom_filter.hlsli; stock: one bilinear tap).
#include "../bloom.hlsli"
#include "../../twattila/render_scale.hlsli"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  // HDR: tone_mapper.hlsli.
  if (HDR >= 1.f) {
    const float2 texcoord = g_screen_size.zw * (g_vpos_texel_offset + v0.xy);
    o0 = ToneMapperHdr(g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, texcoord, 0),
                       SampleBloomSmooth(g_hdr_rgb_bloom_texture_sampler, (texcoord) / DrawRenderShare()),
                       g_black_and_white_points_sampler.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5f, 0.5f), 0),
                       v0.xy);
    return;
  }

  // Got the comments from Gemini:
  // 1. SAMPLE TEXTURES
  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r1.xyzw = g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, r0.xy, 0).xyzw;
  r0.xyzw = SampleBloomSmooth(g_hdr_rgb_bloom_texture_sampler, (r0.xy) / DrawRenderShare()).xyzw;
  r0.xyz *= SI.bloom;

  // 2. CONVERT RGB TO CIE xyY COLOR SPACE
  // r2.x = Z, r2.y = X, r2.z = Y (Luminance)
  r2.x = dot(float3(0.0193000007,0.119199999,0.950500011), r1.xyz);
  r2.y = dot(float3(0.412400007,0.357600003,0.180500001), r1.xyz);
  r2.z = dot(float3(0.212599993,0.715200007,0.0722000003), r1.xyz);

  // Calculate the sum of X+Y+Z (r2.x) to normalize chromaticity
  r2.w = r2.y + r2.z;
  r2.x = r2.w + r2.x;
  r2.x = max(0.00100000005, r2.x);

  // Calculate x and y chromaticity coordinates. r2.w stores (1 - x - y) for later reconstruction.
  r2.y = r2.y / r2.x;
  r2.x = r2.z / r2.x;
  r2.w = 1 + -r2.y;
  r2.w = r2.w + -r2.x;
  r2.x = max(0.00100000005, r2.x);

  // 3. CALCULATE VIGNETTE AND EXPOSURE POLYNOMIAL
  // Determine distance from screen center and apply polynomial curve
  r3.xy = -g_screen_size.xy * float2(0.5,0.5) + v0.xy;
  r3.x = dot(r3.xy, r3.xy);
  r3.x = sqrt(r3.x);
  r3.x = g_recip_half_screen_diag * r3.x;
  r3.w = g_vignette_enable * r3.x;
  r3.z = r3.w * r3.w;
  r3.xy = r3.zz * r3.zw;
  r3.x = dot(r3.xyzw, float4(1.60193861,-3.24679637,1.24311411,-0.219172657));
  r3.x = 1 + r3.x;

  // Multiply scene luminance (Y) by the vignette/exposure factor
  r2.z = r3.x * r2.z;

  // 4. RECONSTRUCT EXPOSED RGB AND ADD BLOOM
  // Sample adaptive black/white points and clamp exposed luminance (r4.y) to the white point (r3.w)
  r3.xyzw = g_black_and_white_points_sampler.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5,0.5), 0).xyzw;
  r4.y = min(r3.w, r2.z);

  // Reconstruct XYZ from clamped xyY, then convert back to standard RGB
  r2.yz = r4.yy * r2.yw;
  r4.xz = r2.yz / r2.xx;
  r2.x = dot(float3(3.24049997,-1.53719997,-0.49849999), r4.xyz);
  r2.y = dot(float3(-0.969299972,1.87600005,0.0416000001), r4.xyz);
  r2.z = dot(float3(0.0555999987,-0.203999996,1.05719995), r4.xyz);

  // Clamp negative colors, add bloom, and ensure an absolute minimum brightness floor
  r1.xyz = max(float3(0,0,0), r2.xyz);
  r0.xyzw = r1.xyzw + r0.xyzw;
  r0.xyz = max(float3(0.0109999999,0.0109999999,0.0109999999), r0.xyz); // BLACK RAISE BABY
  o0.w = r0.w;

  // 5. SECOND xyY CONVERSION FOR TONEMAPPING
  // Convert the bloomed image back into xyY space to prepare for the S-Curve
  r0.w = dot(float3(0.412400007,0.357600003,0.180500001), r0.xyz);
  r1.x = dot(float3(0.0193000007,0.119199999,0.950500011), r0.xyz);
  r0.x = dot(float3(0.212599993,0.715200007,0.0722000003), r0.xyz);
  r0.y = r0.w + r0.x;
  r0.y = r0.y + r1.x;
  r0.z = r0.w / r0.y;
  r0.y = r0.x / r0.y;

  // 6. LOGARITHMIC S-CURVE APPLICATION
  // Convert luminance to base-10 log, offset by black point, and normalize to 0-1 (r2.x)
  r0.x = log2(r0.x);
  r0.x = r0.x * 0.30103001 + -r3.x;
  r0.w = 1 + -r0.z;
  r0.w = r0.w + -r0.y;
  r0.y = max(0.00100000005, r0.y);
  r1.xy = r3.zw + -r3.xy;
  r2.x = r0.x / r1.x;
  r2.y = 0.5;

  // Sample the 1D LUT S-Curve texture
  r2.xyzw = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, r2.xy).xyzw;

  // Convert mapped log result back to linear luminance space
  r0.x = r2.x * r1.x + r3.x;
  r0.x = 3.32192802 * r0.x;
  r0.x = exp2(r0.x);
  r0.x = r0.x + -r3.y;
  r1.y = r0.x / r1.y;

  // 7. FINAL RECONSTRUCTION
  // Reconstruct XYZ from mapped luminance and original chromaticity
  r0.x = r1.y * r0.w;
  r1.z = r0.x / r0.y;
  r0.x = r1.y * r0.z;
  r1.x = r0.x / r0.y;

  // Convert back to final RGB
  r0.x = dot(float3(3.24049997,-1.53719997,-0.49849999), r1.xyz);
  r0.y = dot(float3(-0.969299972,1.87600005,0.0416000001), r1.xyz);
  r0.z = dot(float3(0.0555999987,-0.203999996,1.05719995), r1.xyz);

  o0.xyz = max(float3(0, 0, 0), r0.xyz);
  // Restore the original UNORM write clamp (0 - 1.0) on the upgraded FP16 target.
  o0.xyzw = saturate(o0.xyzw);
  return;
}