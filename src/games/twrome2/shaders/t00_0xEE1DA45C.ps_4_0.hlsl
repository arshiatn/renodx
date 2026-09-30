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
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  // FOR HDR: OTHERWISE DO SDR
  // SUMMARY OF HDR PATH:
  // ----------------------------------------------
  // 1) Grab the scene, bloom, hdr color and etc.
  // 2) measure the luminance
  // 3) Apply camera exposure (without clamp)
  // 4) Scale down with NeuTwo to get between 0 and 1 to apply LUT
  // 5) Apply the Cinematic Contrast
  // 6) Scale it back up
  // 7) Final stuff
  // ----------------------------------------------

  if (HDR == 1.f)
  {
    // This defines the standard mathematical weights used to calculate how bright a color appears 
    // to the human eye based on its Red, Green, and Blue values.
    const float3 kLuminance = float3(0.212599993f, 0.715200007f, 0.0722000003f);

    //Sample stuff
    float2 texcoord = g_screen_size.zw * (g_vpos_texel_offset + v0.xy);
    float4 scene_sample = g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, texcoord, 0);
    float4 bloom_sample = g_hdr_rgb_bloom_texture_sampler.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s, texcoord, 0);
    float4 black_white_points = g_black_and_white_points_sampler.SampleLevel(g_black_and_white_points_sampler_s, float2(0.5f, 0.5f), 0);

    // Vignette
    float2 vignette_offset = v0.xy - g_screen_size.xy * 0.5f;
    float vignette_position = g_vignette_enable * g_recip_half_screen_diag* length(vignette_offset);
    float vignette_position_2 = vignette_position * vignette_position;
    float vignette_scale =
        1.f
        + 1.60193861f * vignette_position_2 * vignette_position_2
        - 3.24679637f * vignette_position_2 * vignette_position
        + 1.24311411f * vignette_position_2
        - 0.219172657f * vignette_position;

    // If in-game Vignette is off, make Vignette slider do nothing
    // TODO: checek if is needed. It could be possible, game will do some weird stuff with math and Idk give vignette_scale 0.999999999
    if (g_vignette_enable == 1) {
        // Scales the darkening (same as lerp up to 1), clamped so 2 can reach black.
        vignette_scale = 1.f - saturate((1.f - vignette_scale) * SI.vignette);
    }

    // Removing Rome II's p.w scene-luminance cap makes the stock xyY
    // reconstruction equivalent to scaling scene RGB by the vignette.
    // Applying Bloom and Vignette
    float3 hdr_color = max(scene_sample.xyz * vignette_scale, 0.f.xxx);
    hdr_color = max(hdr_color + bloom_sample.xyz * SI.bloom,0.f.xxx);

    // Keep the stock black subtraction and adaptive exposure normalization,
    // but do not upper-clamp the scene or the normalized result.
    // SUMMARY: To apply color grading without messing up the actual colors (hue/saturation),
    // the shader separates the brightness from the color:

    // Calculates the total range of brightness in the scene using the black and white points.
    float linear_luminance_range = max(black_white_points.w - black_white_points.y, 0.000001f);
    // Calculates the brightness of the current pixel using the kLuminance weights we established at the start.
    float hdr_luminance = dot(hdr_color, kLuminance);
    // Shifts the pixel's brightness into a standardized scale based on the scene's overall exposure.
    float normalized_luminance = max(hdr_luminance - black_white_points.y, 0.f) / linear_luminance_range;
    // Separates the raw color from its brightness so they can be modified independently
    float3 untonemapped = hdr_luminance > 0.f
                              ? hdr_color * (normalized_luminance / hdr_luminance)
                              : 0.f.xxx;

    // Rome II grades scalar log luminance. A luminance-based NeuTwo proxy
    // preserves that behavior more faithfully than Attila's RGB proxy.
    float curve_scale = normalized_luminance > 0.f
                            ? renodx::tonemap::Neutwo(normalized_luminance) / normalized_luminance
                            : 1.f;
    float3 curve_input = untonemapped * curve_scale;
    float curve_input_luminance = normalized_luminance * curve_scale;

    // **Different compared to linear way of Attila**
    // Return the bounded proxy to Rome II's adaptive log-luminance domain.

    // Converts the linear brightness data into a logarithmic scale.
    float log_luminance_range = max(black_white_points.z - black_white_points.x, 0.000001f);
    float curve_domain_luminance = max(black_white_points.y + curve_input_luminance * linear_luminance_range, 0.000001f);

    // Calculates the exact coordinate needed to read the game's color grading texture.
    float curve_coordinate = (log2(curve_domain_luminance) * 0.30103001f - black_white_points.x) / log_luminance_range;
    // Reads the cinematic contrast value from the game's S-curve texture.
    float curve_value = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(curve_coordinate, 0.5f)).x;
    // Uses exp2 to convert the logarithmic data back into linear brightness now that the contrast has been applied.
    float mapped_luminance = exp2((curve_value * log_luminance_range + black_white_points.x)* 3.32192802f);
    float graded_proxy_luminance = max((mapped_luminance - black_white_points.y) / linear_luminance_range, 0.f);

    // Recombines the new, color-graded brightness with the pixel's original color.
    float3 graded_proxy = curve_input_luminance > 0.f
                              ? curve_input * (graded_proxy_luminance / curve_input_luminance)
                              : 0.f.xxx;

    // scale up safely 
    o0.xyz = renodx::math::DivideSafe(graded_proxy, curve_scale.xxx, graded_proxy);

    // get the alpha 0-1
    o0.w = saturate(scene_sample.w + bloom_sample.w);
    return;
  }

  // Got the comments from Gemini:
  // 1. SAMPLE TEXTURES
  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r1.xyzw = g_hdr_rgb_texture_sampler.SampleLevel(g_hdr_rgb_texture_sampler_s, r0.xy, 0).xyzw;
  r0.xyzw = g_hdr_rgb_bloom_texture_sampler.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s, r0.xy, 0).xyzw;

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