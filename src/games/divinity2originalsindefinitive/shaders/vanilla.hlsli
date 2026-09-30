#ifndef DIVINITY2_VANILLA_HLSLI_
#define DIVINITY2_VANILLA_HLSLI_

namespace dos2_vanilla {

static const float REFERENCE_PEAK_NITS = 1000.f;
static const float REFERENCE_BLACK_NITS = 9.999999747e-5f;

// Calibration convention: 203 on the scene-brightness slider reproduces the
// captured native brightness. This is not a measurement of native paper white.
static const float REFERENCE_PAPER_WHITE = 203.f;

// Native Params[0..5], captured 2026-09-29. Coefficients are unchanged between
// Contrast 0.85 and 0.9992969. Use this curve with final exposure/contrast 1.
// Decimal literals round to the uint32 bit patterns shown in the captures.
// c26.x/w and c27.z: 3232970575, 3213550328, 1077267198.
static const float LOG_MIN_X = -5.600501537f;
static const float LOG_MID_X = -1.085051537f;
static const float LOG_MAX_X = 2.840514660f;

// c28.yzw, c29.xyz; bits: 3229614080, 3229614080, 3226079861,
// 3203953328, 1072464512, 1072464512.
static const float LOW_COEFFICIENTS[6] = {
    -4.f, -4.f, -3.157376528f, -0.4852499962f, 1.847732544f, 1.847732544f};

// c29.w, c30.xyzw, c31.x; bits: 3198840072, 1071186200, 1076921627,
// 1077936128, 1077936128, 1077936128.
static const float HIGH_COEFFICIENTS[6] = {
    -0.3328630924f, 1.695345879f, 2.758124113f, 3.f, 3.f, 3.f};

float EvaluateSegment(float position, float3 coefficients) {
  float t = position - trunc(position);
  float3 powers = float3(t * t, t, 1.f);
  float3 polynomial = float3(
      dot(coefficients.xzy, float3(0.5f, 0.5f, -1.f)),
      dot(coefficients.xy, float2(-1.f, 1.f)),
      dot(coefficients.xy, float2(0.5f, 0.5f)));
  return dot(powers, polynomial);
}

// Same scalar spline and log-input floor as the decompiled stock shader.
// Input is scene-linear AP1 after native ACES colour processing; output is nits.
float ToneMap(float value) {
  float log_input = log2(max(value, 6.10351563e-5f)) * 0.30103001f;
  float log_output;
  if (log_input <= LOG_MIN_X) {
    log_output = -4.f;  // Captured low-end slope is zero.
  } else if (log_input < LOG_MID_X) {
    float position = 3.f * (log_input - LOG_MIN_X) / (LOG_MID_X - LOG_MIN_X);
    int segment = (int)position;
    log_output = EvaluateSegment(position, float3(
        LOW_COEFFICIENTS[segment], LOW_COEFFICIENTS[segment + 1], LOW_COEFFICIENTS[segment + 2]));
  } else if (log_input < LOG_MAX_X) {
    float position = 3.f * (log_input - LOG_MID_X) / (LOG_MAX_X - LOG_MID_X);
    int segment = (int)position;
    log_output = EvaluateSegment(position, float3(
        HIGH_COEFFICIENTS[segment], HIGH_COEFFICIENTS[segment + 1], HIGH_COEFFICIENTS[segment + 2]));
  } else {
    log_output = 3.f;  // Captured high-end slope is zero.
  }
  return exp2(3.32192802f * log_output);
}

float3 ToneMap(float3 color) {
  return float3(ToneMap(color.x), ToneMap(color.y), ToneMap(color.z));
}

// Calibrate the captured native output without replacing its middle-gray point.
// At Paper White 203 / Peak 1000 this is identity for in-range native colours.
// Other paper-white values scale the scene. Other peaks remap the upper range
// smoothly; values below the knee keep their scaled native brightness.
float3 Calibrate(float3 native_nits, float paper_white, float peak_nits) {
  float scene_scale = paper_white / REFERENCE_PAPER_WHITE;
  float3 output_nits = native_nits * scene_scale;
  float highest_channel = max(output_nits.r, max(output_nits.g, output_nits.b));
  if (highest_channel <= 0.f) return output_nits;

  float source_peak = REFERENCE_PEAK_NITS * scene_scale;
  float mapped_channel = highest_channel;
  if (source_peak != peak_nits) {
    // The curve's scene-linear 1.0 output is about 107.437 reference nits.
    // Keep a positive shoulder interval even for a low peak / high paper white.
    float knee = min(ToneMap(1.f) * scene_scale, peak_nits * 0.5f);
    if (highest_channel > knee) {
      float source_range = source_peak - knee;
      float target_range = peak_nits - knee;
      float t = saturate((highest_channel - knee) / source_range);
      // Rational shoulder: slope 1 at the knee, exact target at source_peak.
      float denominator = target_range * (1.f - t) + source_range * t;
      mapped_channel = knee + source_range * (target_range * t / denominator);
    }
  }

  // Native luminance mapping/post-matrix may exceed its nominal peak. Keep
  // the user's final peak as a ceiling; scale RGB together to retain ratios.
  mapped_channel = min(mapped_channel, peak_nits);
  if (mapped_channel != highest_channel) {
    output_nits *= mapped_channel / highest_channel;
  }
  return output_nits;
}

}  // namespace dos2_vanilla

#endif  // DIVINITY2_VANILLA_HLSLI_
