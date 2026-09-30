// R.U.S.E. final post process, in-game HDR on (10-bit scene + bloom). Writes the back buffer.
// Hand-decompiled from 0x232EB5DF.ps_3_0.cso (DX9), verified against it.
// Stock: directional blur + bloom -> x HDRParam.x -> desaturation, tint, dominant colour,
//        vignette -> luminance gamma, brightness. No tonemapper: the 8-bit back buffer clips at 1.
// HDR 0 (SDR): stock, then paper white.
// HDR 1 (Extended): no clip. Unchanged up to SDR white, then a shoulder into the peak.
// HDR 2 (PsychoV30): the unclipped image through PsychoV30.
// HDR 3 (PsychoV30 Direct): replace the native luminance gamma/brightness
// response, keeping its neutral gray point and the native decoded RGB ratios.
// Native scene targets retain their 10-bit format and HDRParam range multiplier.
// Output uses a fixed 203-nit reference; UI brightness is applied in UI shaders.

#include "../shared.h"
#include "../psycho_test30.hlsli"

float4 PostProcess[11] : register(c0);           // one register per member:
#define NoiseLevel        PostProcess[0].x       // (unused here)
#define OverlayColor      PostProcess[1].xyz
#define MinVignettage     PostProcess[2].x
#define VignettageExtent  PostProcess[3].x
#define BloomFactor       PostProcess[4].x
#define Gamma             PostProcess[5].x
#define Brightness        PostProcess[6].x
#define SkyBrightness     PostProcess[7].x       // (unused here)
#define Desaturation      PostProcess[8].x
#define DominantColor     PostProcess[9].xyz
#define DominantColorProp PostProcess[10].x
float2 PrincipalMap_HDRParam : register(c11);

sampler2D PrincipalMap : register(s0);           // scene
sampler2D DownSizeMap : register(s1);            // bloom
sampler2D ShadowMap : register(s2);              // .yz: blur direction

static const float3 kLuma = float3(0.299f, 0.587f, 0.114f);
// Blur weights for +-1..8 steps (center 1). kBlurNormalize = 1 / (1 + 2 * sum).
static const float kBlurWeights[8] = {
  0.807401419f, 0.617947221f, 0.424969733f, 0.262608856f,
  0.145816088f, 0.0727522448f, 0.0326160975f, 0.0131390067f
};
static const float kBlurNormalize = 0.173777029f;

// Extended user grading (from Rome Remastered). Identity at defaults.
float3 ApplyExtendedUserGrading(float3 color)
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
float3 ApplyExtendedPeakShoulder(float3 color)
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

float4 main(float2 v0 : TEXCOORD0, float2 v1 : TEXCOORD1, float2 v2 : TEXCOORD2) : COLOR0
{
  // A live shader without the addon must retain the stock SDR output.
  bool injection_valid = SI.peak_white_nits > 0.f && SI.diffuse_white_nits > 0.f;
  int hdr_mode = injection_valid ? (int)clamp(floor(HDR + 0.5f), 0.f, 3.f) : 0;
  bool hdr = hdr_mode != 0;

  // Native scene and bloom storage are UNORM. Keep their original sample range.
  float tap_max = 1.f;

  // 1. Directional blur. Step = (ShadowMap.y - 0.502, -(ShadowMap.z - 0.502)) + v2, x 0.01.
  float3 color = clamp(tex2Dlod(PrincipalMap, float4(v1, 0, 0)).rgb, 0.f, tap_max);
  float2 offset_map = tex2D(ShadowMap, v1).yz - 0.501960814f;
  float2 step_uv = (float2(offset_map.x, -offset_map.y) + v2) * 0.00999999978f;
  // With exactly zero displacement all 17 taps sample the same location.
  // Keep the original accumulation in SDR and for every nonzero displacement;
  // do not use a threshold that could remove subtle heat haze or motion blur.
  [branch]
  if (!hdr || any(step_uv != 0.f)) {
    float4 step4 = float4(step_uv, -step_uv);
    float4 tap_uv = v1.xyxy + step4;
    [unroll]
    for (int i = 0; i < 8; ++i) {
      color += clamp(tex2Dlod(PrincipalMap, float4(tap_uv.xy, 0, 0)).rgb, 0.f, tap_max) * kBlurWeights[i];
      color += clamp(tex2Dlod(PrincipalMap, float4(tap_uv.zw, 0, 0)).rgb, 0.f, tap_max) * kBlurWeights[i];
      tap_uv += step4;
    }
    color *= kBlurNormalize;
  }
  float3 bloom = clamp(tex2D(DownSizeMap, v1).rgb, 0.f, tap_max) * BloomFactor;
  if (hdr) bloom *= SI.bloom;  // Bloom slider (HDR only)
  color += bloom;

  // 2. Scene range.
  color *= PrincipalMap_HDRParam.x;

  // 3. Grading.
  float lightness = 0.5f * (max(color.r, max(color.g, color.b)) + min(color.r, min(color.g, color.b)));
  color = lerp(color, lightness.xxx, Desaturation);
  color *= OverlayColor / dot(OverlayColor, kLuma);
  color = lerp(color, dot(color, 1.f) * DominantColor, DominantColorProp);

  float2 edge = min(1.f - v0, v0);  // distance to the nearest edges
  float t = saturate(sqrt(edge.x * edge.y) / VignettageExtent);
  float vignette = saturate((MinVignettage - 1.f) * t * t + (2.f - 2.f * MinVignettage) * t + MinVignettage);
  if (hdr) vignette = 1.f - saturate((1.f - vignette) * SI.vignette);  // Vignette slider (HDR only)
  color *= vignette;

  float psycho_anchor_in = 0.18f;
  bool direct_response = false;
  [branch]
  if (hdr_mode == 3 && Gamma > 0.0001f && Brightness > 0.0001f) {
    // Vanilla's decoded neutral is V(x) = (Brightness * x^Gamma)^2.2.
    // Direct still decodes channels, so its input anchor is x^2.2, where
    // V(x) = 0.18. Calibration and colour must use the same representation.
    // This retains native decoded RGB ratios without changing the cone slider;
    // it does not assert that the original texture is scene-linear.
    float log_anchor = (log2(0.18f) - 2.2f * log2(Brightness)) / Gamma;
    if (log_anchor >= -16.f && log_anchor <= 16.f) {
      psycho_anchor_in = exp2(log_anchor);
      direct_response = true;
    }
  }

  // 4. Preserve the native response in the original three modes. Degenerate
  // Direct calibration (including a black fade) also uses the existing path.
  [branch]
  if (!direct_response) {
    float luma = dot(color, kLuma);
    color = (luma > 0.f) ? color * pow(luma, Gamma - 1.f) : 0.f;
    color *= Brightness;
  }

  if (!hdr) {
    if (!injection_valid) return float4(saturate(color), 1.f);
    // SDR: the 8-bit back buffer's clip, then paper white.
    color = renodx::color::gamma::DecodeSafe(saturate(color));
    color *= SI.diffuse_white_nits / RUSE_REFERENCE_WHITE;
    return float4(renodx::color::gamma::EncodeSafe(color), 1.f);
  }

  // Decode the native gamma-domain RGB in every HDR mode, without clipping.
  // Direct removes the common luminance response, not this channel conversion.
  // For positive RGB, decoding (RGB * gain) differs from decoding RGB only by
  // gain^2.2. Both Psycho modes therefore receive the same input chromaticity.
  color = renodx::color::gamma::DecodeSafe(color);
  [branch]
  if (hdr_mode == 1) {
    [branch]
    if (SI.exposure_tpm != 1.f || SI.highlights_tpm != 1.f || SI.shadows_tpm != 1.f
        || SI.contrast_tpm != 1.f || SI.saturation_tpm != 1.f) {
      color = ApplyExtendedUserGrading(color);
    }
    color = ApplyExtendedPeakShoulder(color);
  } else {
    // One shared call keeps the large Psycho implementation out of duplicate
    // SM3 branches. Both Psycho modes intentionally share their user controls.
    color = ApplyPsychoV30Reference(color, psycho_anchor_in, SI.cone_response_exponent);
  }

  color *= SI.diffuse_white_nits / RUSE_REFERENCE_WHITE;
  return float4(renodx::color::gamma::EncodeSafe(color), 1.f);
}
