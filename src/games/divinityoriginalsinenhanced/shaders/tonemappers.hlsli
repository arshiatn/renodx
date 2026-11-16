#ifndef DIVINITY_TONEMAPPERS_HLSLI_
#define DIVINITY_TONEMAPPERS_HLSLI_

#include "./pragmap.hlsl"
#include "../psycho_test30.hlsli"
#include "../../twattila/psycho35.hlsli"

// UC2 Extended tone and grading paths; native SDR is handled by the pixel shader.
namespace divinity_tonemap {

// Calculate the SDR reference and UC2 Extended scene values.
void Rolloff(float3 color, float4 params, out float3 sdr, out float3 hdr)
{

  float A = params.x;
  float B = params.y;
  float C = 0.100000001;
  float D = params.z;
  float E = 0.00999999978;
  float F = 0.300000012;
  float W = 11.1999998;
  float white_precompute = 1.f / ApplyCurve(W, A, B, C, D, E, F);

  sdr = ApplyCurve(color, A, B, C, D, E, F) * white_precompute;
  sdr = max(0, sdr);

  float coeffs[6] = { A, B, C, D, E, F };
  Uncharted2::Config::Uncharted2ExtendedConfig uc2_config = Uncharted2::Config::CreateUncharted2ExtendedConfig(coeffs, white_precompute);
  hdr = Uncharted2::ApplyExtended(color, uc2_config);
}

// Gamma-encoded grading; alpha carries the game's AA luminance.
float4 ColorMatrixGrading(float3 color, row_major float4x4 mat)
{

    float4 color1 = float4(color, 1);
    float4 result = float4(
        dot(mat._m00_m01_m02_m03, color1),
        dot(mat._m10_m11_m12_m13, color1),
        dot(mat._m20_m21_m22_m23, color1),
        dot(mat._m30_m31_m32_m33, color1)
    );
    result = max(0, result);
    return result;
}

// Apply the game's selective-color tint in gamma space.
float3 Tint(float3 color, float3 targetColor, float3 sourceColor, float power = 2.0)
{

  float3 delta = color + targetColor;
  delta = max(-1, delta);
  delta = min(1, delta);
  delta = delta + -color;

  float distance = length(color - sourceColor);
  float weight = max(0, 1.0 - distance);
  weight = pow(weight, power);

  color = delta * weight + color;
  color = max(0, color);
  return color;
}

float EvalPolynomial(float x, float c5, float c4, float c3, float c2, float c1, float c0) {
  return ((((c5 * x + c4) * x + c3) * x + c2) * x + c1) * x + c0;
}

float4 Apply(float3 scene_linear,
             float4 Params,
             row_major float4x4 ColorMatrix,
             float3 SourceColor,
             float3 TargetColor,
             bool use_pragmap)
{
  float4 o0;
  float4 r0,r1,r2;

  r0.w = 1;

  r0.xyz = scene_linear;

  float p = HDR_PEAK;
  p = renodx::color::correct::Gamma(p, false, 2.2);

  float3 sdr, hdr;
  Rolloff(r0.xyz, Params, sdr, hdr);

  if (use_pragmap) {
    r0.xyz = hdr;

    r0.xyz = pow(max(hdr, 0.f), 1.f / 2.2f);
    r0 = ColorMatrixGrading(r0.xyz, ColorMatrix);
    r0.w = r0.w / (r0.w + 1);

    r0.xyz = pow(r0.xyz, 2.2);

    float stabilizeColor = EvalPolynomial(p, -0.0000048885271f, 0.0004223872835f, -0.0126676434676f, 0.1439545403157f, -0.3943482340705f, 0.857617690951f);
    stabilizeColor = clamp(stabilizeColor, 0.0f, 3.0f);

    float stabilizeBlowout = EvalPolynomial(p, 0.0f, -0.0001551226551f, 0.00556998557f, -0.0747691197691f, 0.3876443001443f, 0.08170995671f);
    stabilizeBlowout = clamp(stabilizeBlowout, 0.35f, 1.0f);

    float stabilizeHarshness = EvalPolynomial(p, 0.0f, -0.0004166666667f, 0.0041666666667f, -0.0095833333333f, 0.0058333333333f, 0.05f);
    stabilizeHarshness = clamp(stabilizeHarshness, 0.0f, 0.3f);

    float stabilizeLRCSC = EvalPolynomial(p, 0.0f, 0.0f, 0.0f, 0.0020833333333f, 0.09875f, -0.1958333333333f);
    stabilizeLRCSC = clamp(stabilizeLRCSC, 0.01f, 1.0f);

    r0.xyz = pragmap(r0.xyz,p,stabilizeColor,stabilizeBlowout,stabilizeHarshness,stabilizeLRCSC);

    r0.xyz = max(0, r0.xyz);
    r0.xyz = pow(r0.xyz, 1 / 2.2);

    float scale = 1;
    r0.xyz = pow(r0.xyz, 2.2);
    {

      float m = max(r0.x, max(r0.y, r0.z));
      float m1 = renodx::tonemap::Neutwo(m, 1, p);
      scale = m > 0 ? m1 / m : 1;
      r0.xyz *= scale;
    }
    r0.xyz = pow(r0.xyz, 1 / 2.2);

    r0.xyz = Tint(r0.xyz, TargetColor, SourceColor, 2.3);

    r0.xyz = pow(r0.xyz, 2.2);
    r0.xyz /= scale;
    r0.xyz = pow(r0.xyz, 1 / 2.2);
  } else {

    // Recover graded SDR chroma, restore UC2 Extended luminance, then apply PsychoV30.
    float3 stock_graded_gamma = pow(max(sdr, 0.f), 1.f / 2.2f);
    stock_graded_gamma = min(stock_graded_gamma, 1.f.xxx);
    float4 stock_graded = ColorMatrixGrading(stock_graded_gamma, ColorMatrix);
    stock_graded.xyz = Tint(stock_graded.xyz, TargetColor, SourceColor, 2.f);
    float3 stock_graded_linear = pow(max(stock_graded.xyz, 0.f), 2.2f);
    r0.w = stock_graded.w / (stock_graded.w + 1.f);

    r0.xyz = UpgradeToneMap(max(hdr, 0.f), max(sdr, 0.f), stock_graded_linear);

    r0.xyz = ApplyAttilaPsychoV30(r0.xyz);
    r0.xyz = renodx::color::gamma::EncodeSafe(r0.xyz, 2.2f);
  }

  r1.xyzw = float4(0,0,0,1) + -r0.xyzw;
  r2.x = saturate(Params.w);
  r0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;

  // Preserve signed BT.709 colors from PsychoV30 until the shared output stage.
  r0 = renodx::math::ZeroNaN(r0);
  if (use_pragmap) {
    r0 = max(0, r0);
  } else {
    r0.w = max(0, r0.w);
  }
  o0 = r0;
  return o0;
}

// The game's curve: Hable with the live A / B / D (Params.xyz; C 0.1, E 0.01, F 0.3) over Hable(W 11.2), unclamped.
float3 Hable(float3 x, float4 params) {
  return ApplyCurve(x, params.x, params.y, 0.1f, params.z, 0.01f, 0.3f)
         / ApplyCurve(11.2f, params.x, params.y, 0.1f, params.z, 0.01f, 0.3f);
}

// HDR (Extended): UC2 Extended (Musa & DabVeed) on the scene: the game's Hable up to its knee (third derivative root),
// its tangent there above it, per channel. Above 1 each channel rolls off to Peak on its own (Neutwo), as SDR clips
// each channel: bright fire turns yellow / white at its core (Ryse fix32; a brightest-channel roll-off kept fire salmon).
float3 ApplyExtendedOnScene(float3 scene, float4 params, float peak) {
  float coeffs[6] = { params.x, params.y, 0.1f, params.z, 0.01f, 0.3f };
  const float white_precompute = 1.f / ApplyCurve(11.2f, coeffs[0], coeffs[1], coeffs[2], coeffs[3], coeffs[4], coeffs[5]);
  Uncharted2::Config::Uncharted2ExtendedConfig uc2_config =
      Uncharted2::Config::CreateUncharted2ExtendedConfig(coeffs, white_precompute);
  // No usable knee (odd curve constants): plain Hable, only the shoulder below adds HDR.
  if (!(uc2_config.pivot_point > 0.f && Uncharted2::Derivative(uc2_config.pivot_point, coeffs[0], coeffs[1], coeffs[2],
                                                                coeffs[3], coeffs[4], coeffs[5]) > 0.f)) {
    uc2_config.pivot_point = 11.2f;
  }
  const float3 color = max(Uncharted2::ApplyExtended(scene, Hable(scene, params), uc2_config), 0.f);
  const float shoulder = max(peak, 1.0001f) - 1.f;
  const float3 over = max(color - 1.f, 0.f);
  return min(color, 1.f)
         + float3(renodx::tonemap::Neutwo(over.r, shoulder), renodx::tonemap::Neutwo(over.g, shoulder),
                  renodx::tonemap::Neutwo(over.b, shoulder));
}

// The game's grade as the stock runs it after its curve, on gamma 2.2 values within 0 - 1: ColorMatrix (its last row
// the alpha), then the selective colour (toward TargetColor near SourceColor, weight squared). The SDR LUT bridge: from
// the shoulder's start the brightest channel rolls off toward 1 (Musa's C-infinity roll-off: the identity below its
// start, smooth in every derivative), the colour is graded there and scaled back by max / roll_off(max): the grade's
// colour, the curve's brightness. Below the start it is the stock's grade itself; a hard knee at 1 bent every ramp's
// slope there. Below the stock's 0 clip a channel is 0, as in the game's UNORM target; out-of-gamut parts pass through.
float4 GradeAfterCurve(float3 color, row_major float4x4 color_matrix, float3 source_color, float3 target_color) {
  const float shoulder_start = 0.8f;
  const float channel_max = renodx::math::Max(color);
  const float scale = (channel_max > shoulder_start)
                          ? channel_max / renodx::tonemap::CInfinityRollOff(channel_max, 1.f, shoulder_start, 1.5f)
                          : 1.f;
  const float3 normalized = color / scale;
  const float3 inside = saturate(normalized);
  const float4 encoded = float4(pow(inside, 1.f / 2.2f), 1.f);
  float4 graded = float4(dot(color_matrix._m00_m01_m02_m03, encoded), dot(color_matrix._m10_m11_m12_m13, encoded),
                         dot(color_matrix._m20_m21_m22_m23, encoded), dot(color_matrix._m30_m31_m32_m33, encoded));
  const float3 toward_target = clamp(target_color + graded.rgb, -1.f, 1.f) - graded.rgb;
  const float weight = max(0.f, 1.f - length(graded.rgb - source_color));
  graded.rgb = max(toward_target * (weight * weight) + graded.rgb, 0.f);
  return float4((renodx::color::gamma::Decode(graded.rgb, 2.2f) + normalized - inside) * scale, graded.a);
}

// Within BT.2020 and Peak: past Peak, desaturate keeping luminance and hue; past it in luminance, Peak white (Ryse's).
float3 LimitToPeak(float3 color, float peak) {
  float3 color_bt2020 = max(renodx::color::bt2020::from::BT709(color), 0.f);
  const float y = min(renodx::color::y::from::BT2020(color_bt2020), peak);
  const float channel_max = renodx::math::Max(color_bt2020);
  [branch]
  if (channel_max > peak) {
    color_bt2020 = y + (color_bt2020 - y) * saturate((peak - y) / (channel_max - y));
  }
  return renodx::color::bt709::from::BT2020(color_bt2020);
}

// HDR (Extended) and HDR (PsychoV35): the curve once on the scene, where the stock calls its Hable, then the game's
// grade and fade. Fire and sky take their SDR hue through Hue Clip Emulation (toward the hue Hable clipped per channel
// gives; no per-channel boosts). PsychoV35 sits on Hable's mid gray for the live A / B / D (0.036 - 0.230 over the
// game's 243 atmospheres).
float4 ApplyOnScene(float3 scene, float4 params, row_major float4x4 color_matrix, float3 source_color, float3 target_color,
                    bool psycho35) {
  const float peak = DIVINITY_PEAK;
  const float hue_clip = (psycho35 ? SI.hue_clip : SI.ext_hue_clip);
  float3 untonemapped = scene;
  [branch]
  if (hue_clip > 0.f) {
    untonemapped = renodx::tonemap::psychov::HueTransfer(scene, saturate(Hable(scene, params)), hue_clip);
  }
  float3 tonemapped;
  [branch]
  if (psycho35) {
    const float mid_gray = Hable(0.18f, params).x;
    tonemapped = ApplyPsychoV35(untonemapped, peak, DIVINITY_SDR_OUTPUT, (mid_gray > 0.f) ? mid_gray : 0.18f);
  } else {
    tonemapped = ApplyExtendedOnScene(untonemapped, params, peak);
  }
  const float4 graded = GradeAfterCurve(tonemapped, color_matrix, source_color, target_color);
  // The stock's fade lerps its gamma values toward black (alpha toward 1).
  const float fade = saturate(params.w);
  const float3 color = LimitToPeak(graded.rgb * pow(1.f - fade, 2.2f), peak);
  return float4(renodx::color::gamma::EncodeSafe(color, 2.2f), lerp(saturate(graded.a), 1.f, fade));
}

}
#endif
