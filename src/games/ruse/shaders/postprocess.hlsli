#ifndef RUSE_POSTPROCESS_HLSLI_
#define RUSE_POSTPROCESS_HLSLI_

// Native final-postprocess permutations share grading and Psycho controls, but
// have different blur kernels and s1/s2 bindings. Bindings/constants come from CSO/ASM.
#ifndef RUSE_POSTPROCESS_BLUR_RADIUS
#define RUSE_POSTPROCESS_BLUR_RADIUS 8
#endif
#ifndef RUSE_POSTPROCESS_BLOOM
#define RUSE_POSTPROCESS_BLOOM 1
#endif
#ifndef RUSE_POSTPROCESS_SIMPLE
#define RUSE_POSTPROCESS_SIMPLE 0
#endif

#include "../shared.h"
#include "../psycho_test31.hlsli"

float4 PostProcess[11] : register(c0);
#define OverlayColor      PostProcess[1].xyz
#define MinVignettage     PostProcess[2].x
#define VignettageExtent  PostProcess[3].x
#define BloomFactor       PostProcess[4].x
#define Gamma             PostProcess[5].x
#define Brightness        PostProcess[6].x
#define Desaturation      PostProcess[8].x
#define DominantColor     PostProcess[9].xyz
#define DominantColorProp PostProcess[10].x

sampler2D PrincipalMap : register(s0);
#if !RUSE_POSTPROCESS_SIMPLE
float2 PrincipalMap_HDRParam : register(c11);
#if RUSE_POSTPROCESS_BLOOM
sampler2D DownSizeMap : register(s1);
sampler2D ShadowMap : register(s2);
#else
sampler2D ShadowMap : register(s1);
#endif
#endif

static const float3 kLuma = float3(0.299f, 0.587f, 0.114f);
#if !RUSE_POSTPROCESS_SIMPLE
#if RUSE_POSTPROCESS_BLUR_RADIUS == 8
static const float kBlurWeights[8] = {
  0.807401419f, 0.617947221f, 0.424969733f, 0.262608856f,
  0.145816088f, 0.0727522448f, 0.0326160975f, 0.0131390067f
};
static const float kBlurNormalize = 0.173777029f;
static const float kBlurStep = 0.00999999978f;
#elif RUSE_POSTPROCESS_BLUR_RADIUS == 6
static const float kBlurNormalize = 0.0769230798f;
static const float kBlurStep = 0.0133333337f;
#elif RUSE_POSTPROCESS_BLUR_RADIUS == 4
static const float kBlurNormalize = 0.111111112f;
static const float kBlurStep = 0.0199999996f;
#else
#error Unsupported native postprocess blur radius
#endif
#endif

#if RUSE_POSTPROCESS_SIMPLE
float4 main(float2 v1 : TEXCOORD1) : COLOR0
#else
float4 main(float2 v0 : TEXCOORD0, float2 v1 : TEXCOORD1, float2 v2 : TEXCOORD2) : COLOR0
#endif
{
  bool injection_valid = SI.peak_white_nits > 0.f && SI.diffuse_white_nits > 0.f;
  int hdr_mode = injection_valid ? (int)clamp(floor(HDR + 0.5f), 0.f, 2.f) : 0;
  bool hdr = hdr_mode != 0;

  // Native scene/bloom storage is UNORM. Keep its range multiplier after sampling.
  float tap_max = 1.f;
#if RUSE_POSTPROCESS_SIMPLE
  float3 color = clamp(tex2D(PrincipalMap, v1).rgb, 0.f, tap_max);
#else
  float3 color = clamp(tex2Dlod(PrincipalMap, float4(v1, 0, 0)).rgb, 0.f, tap_max);
  float2 offset_map = tex2D(ShadowMap, v1).yz - 0.501960814f;
  float2 step_uv = (float2(offset_map.x, -offset_map.y) + v2) * kBlurStep;
  // Preserve all native taps in SDR and for any nonzero displacement.
  [branch]
  if (!hdr || any(step_uv != 0.f)) {
    float4 step4 = float4(step_uv, -step_uv);
    float4 tap_uv = v1.xyxy + step4;
    [unroll]
    for (int i = 0; i < RUSE_POSTPROCESS_BLUR_RADIUS; ++i) {
#if RUSE_POSTPROCESS_BLUR_RADIUS == 8
      color += clamp(tex2Dlod(PrincipalMap, float4(tap_uv.xy, 0, 0)).rgb, 0.f, tap_max) * kBlurWeights[i];
      color += clamp(tex2Dlod(PrincipalMap, float4(tap_uv.zw, 0, 0)).rgb, 0.f, tap_max) * kBlurWeights[i];
#else
      color += clamp(tex2Dlod(PrincipalMap, float4(tap_uv.xy, 0, 0)).rgb, 0.f, tap_max);
      color += clamp(tex2Dlod(PrincipalMap, float4(tap_uv.zw, 0, 0)).rgb, 0.f, tap_max);
#endif
      tap_uv += step4;
    }
    color *= kBlurNormalize;
  }
#if RUSE_POSTPROCESS_BLOOM
  float3 bloom = clamp(tex2D(DownSizeMap, v1).rgb, 0.f, tap_max) * BloomFactor;
  if (hdr) bloom *= SI.bloom;
  color += bloom;
#endif
  color *= PrincipalMap_HDRParam.x;
#endif

  float lightness = 0.5f * (max(color.r, max(color.g, color.b)) + min(color.r, min(color.g, color.b)));
  color = lerp(color, lightness.xxx, Desaturation);
  color *= OverlayColor / dot(OverlayColor, kLuma);
  color = lerp(color, dot(color, 1.f) * DominantColor, DominantColorProp);

#if !RUSE_POSTPROCESS_SIMPLE
  float2 edge = min(1.f - v0, v0);
  float t = saturate(sqrt(edge.x * edge.y) / VignettageExtent);
  // The CSO's dp2add has no _sat modifier; the FX decompilation adds one incorrectly.
  float vignette = (MinVignettage - 1.f) * t * t + (2.f - 2.f * MinVignettage) * t + MinVignettage;
  if (hdr) vignette = 1.f - saturate((1.f - vignette) * SI.vignette);
  color *= vignette;
#endif

  float psycho_anchor_in = 0.18f;
  bool direct_response = false;
  [branch]
  if (hdr_mode >= 2 && Gamma > 0.0001f && Brightness > 0.0001f) {
    // Under the retained gamma-2.2 output assumption, the decoded neutral is
    // V(x) = (Brightness * x^Gamma)^2.2. Calibrate in the same domain as Psycho input.
    float log_anchor = (log2(0.18f) - 2.2f * log2(Brightness)) / Gamma;
    if (log_anchor >= -16.f && log_anchor <= 16.f) {
      psycho_anchor_in = exp2(log_anchor);
      direct_response = true;
    }
  }

  // SDR and Native Tone keep the native response; degenerate anchors/fades do too.
  [branch]
  if (!direct_response) {
    float luma = dot(color, kLuma);
    color = (luma > 0.f) ? color * pow(luma, Gamma - 1.f) : 0.f;
    color *= Brightness;
  }

  if (!hdr) {
    if (!injection_valid) return float4(saturate(color), 1.f);
    color = renodx::color::gamma::DecodeSafe(saturate(color));
    color *= SI.diffuse_white_nits / RUSE_REFERENCE_WHITE;
    return float4(renodx::color::gamma::EncodeSafe(color), 1.f);
  }

  // Both HDR modes use the supplied V31 response and retain decoded RGB ratios.
  color = renodx::color::gamma::DecodeSafe(color);
  color = ApplyPsychoV31Reference(color, psycho_anchor_in);
  color *= SI.diffuse_white_nits / RUSE_REFERENCE_WHITE;
  return float4(renodx::color::gamma::EncodeSafe(color), 1.f);
}

#endif  // RUSE_POSTPROCESS_HLSLI_
