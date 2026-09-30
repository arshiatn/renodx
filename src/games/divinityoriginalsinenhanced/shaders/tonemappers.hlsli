#ifndef DIVINITY_TONEMAPPERS_HLSLI_
#define DIVINITY_TONEMAPPERS_HLSLI_

#include "./pragmap.hlsl"
#include "../psycho_test30.hlsli"

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

}
#endif
