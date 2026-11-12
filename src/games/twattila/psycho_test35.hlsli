#ifndef PSYCHOV_CUSTOMTEST35_HLSLI_
#define PSYCHOV_CUSTOMTEST35_HLSLI_

// Total War: ATTILA and Warhammer III: copy of Musa's customtest35 (2026-11-12), only this include's path changed and
// the SM3 forms below (RUSE's DX9 shaders: no bit casts, no bitwise operations on bools). SM4 and later: unchanged.
// Needs renodx.hlsl (shared.h).

/*
 * Copyright (C) 2026 Carlos Lopez
 * Modifications Copyright (C) 2026 Musa Haji
 * SPDX-License-Identifier: MIT
 */

#include "../../shaders/tonemap/psychov/test35.hlsl"

// A failed pixel's NaN (black after ApplyPsychoV35, twattila/psycho35.hlsli): black at once on SM3; -infinity: the lowest
// float there.
#if __SHADER_TARGET_MAJOR >= 4
#define PSYCHO35_FAILED asfloat(0x7fc00000u)
#define PSYCHO35_NEGATIVE_INFINITY asfloat(0xff800000u)
#define PSYCHO35_AND(a, b) ((a) & (b))
#else
#define PSYCHO35_FAILED 0.f
#define PSYCHO35_NEGATIVE_INFINITY (-3.402823466e+38f)
#define PSYCHO35_AND(a, b) ((a) && (b))
#endif

namespace renodx {
namespace tonemap {
namespace psychov {

// Map a finite input white to peak while leaving values through anchor intact.
// Requires anchor < peak, compression_strength >= 1, and anchor < clip.
#define CUSTOM_PSYCHO35_C_INFINITY_ROLLOFF_GENERATOR(T)                                                                         \
  T custom_psycho35_CInfinityRollOff(T color, T peak, T anchor, float compression_strength, T clip) {                           \
    T shoulder_range = peak - anchor;                                                                                           \
    T clip_range = clip - anchor;                                                                                               \
    T clipped_distance = min(max(color - anchor, (T)0.f), clip_range);                                                          \
    T remaining_distance = clip_range - clipped_distance;                                                                       \
    T inverse_position = renodx::math::DivideSafe(                                                                              \
        shoulder_range * remaining_distance, abs(clip - peak) * clipped_distance, (T)0.f);                                      \
    T flat_weight = exp2(-inverse_position * rcp(compression_strength));                                                        \
    T denominator = flat_weight + inverse_position;                                                                             \
    T transition = clipped_distance * flat_weight * rcp(clip_range * denominator);                                              \
    T exponent = log(2.f) * inverse_position * rcp(compression_strength);                                                       \
    T one_minus_weight = renodx::math::Select(exponent < 0.01f,                                                                 \
                                              exponent * (1.f + exponent * (-0.5f + exponent * (1.f / 6.f - exponent / 24.f))), \
                                              1.f - flat_weight);                                                               \
    T deficit = renodx::math::Select(peak < clip, inverse_position - one_minus_weight,                                          \
                                     denominator + sign(peak - clip));                                                          \
    T headroom = shoulder_range * (remaining_distance / clip_range) * (deficit / denominator);                                  \
    T response = renodx::math::Select(clipped_distance > 0.5f * shoulder_range, peak - headroom,                                \
                                      mad(peak - clip, transition, min(color, clip)));                                          \
    return renodx::math::Select(color >= clip, peak, response);                                                                 \
  }

CUSTOM_PSYCHO35_C_INFINITY_ROLLOFF_GENERATOR(float)
CUSTOM_PSYCHO35_C_INFINITY_ROLLOFF_GENERATOR(float3)
#undef CUSTOM_PSYCHO35_C_INFINITY_ROLLOFF_GENERATOR

// Derivatives accompany the grade because test35's optional boundary trade
// must use the response actually rendered, not the original sigmoid's speed.
float3 custom_psycho35_CInfinityTransition(float3 position, out float3 derivative) {
  const bool3 interior = PSYCHO35_AND(position > 0.f, position < 1.f);
  const float3 t = renodx::math::Select(interior, position, 0.5f);
  const float3 value = rcp(1.f + exp2((1.f - 2.f * t) / (t * (1.f - t))));
  derivative = renodx::math::Select(interior, log(2.f) * value * (1.f - value) * (rcp(t * t) + rcp((1.f - t) * (1.f - t))), 0.f);
  return renodx::math::Select(interior, value, saturate(position));
}

// Input and output are adaptation-relative cone signals. logarithmic_slope
// is d log(output) / d log(input), separately for each cone.
float3 custom_psycho35_ApplyAnchoredTonalGrading(
    float3 normalized, float contrast, float flare,
    float highlight_contrast, float shadow_contrast, float highlights, float shadows,
    out float3 logarithmic_slope) {
  float3 graded = normalized;
  logarithmic_slope = 1.f;
  if (contrast != 1.f || flare > 0.f) {
    float3 exponent = contrast;
    float3 exponent_derivative = 0.f;
    if (flare > 0.f) {
      const bool3 shadow = normalized < 1.f;
      const float3 distance = renodx::math::Select(shadow, 1.f - normalized, 1.f);
      const float3 weight = renodx::math::Select(shadow, exp2(-normalized / distance), 0.f);
      const float3 flare_weight = weight * flare / (normalized + flare);
      exponent *= 1.f + flare_weight;
      exponent_derivative = -contrast * flare_weight * normalized
                            * (log(2.f) / (distance * distance) + rcp(normalized + flare));
    }
    // Exact zero cones stay zero; they do not acquire an arbitrary log floor.
    const float3 stops = log2(renodx::math::Select(normalized == 0.f, 1.f, normalized));
    const float3 highlight_stops = max(stops, 0.f);
    const float3 displacement = (contrast - 1.f) * highlight_stops;
    const float3 magnitude = abs(displacement);
    const float3 weight = renodx::math::Select(magnitude > 0.f, exp2(-rcp(magnitude)), 0.f);
    const float3 denominator = mad(magnitude, weight, 1.f);
    const float3 output_highlights = highlight_stops + displacement / denominator;
    graded = exp2(mad(exponent, min(stops, 0.f), output_highlights));
    logarithmic_slope = renodx::math::Select(normalized < 1.f,
                                             exponent + log(2.f) * stops * exponent_derivative,
                                             1.f + (contrast - 1.f) * (1.f - log(2.f) * weight) / (denominator * denominator));
    graded = renodx::math::Select(normalized == 0.f, 0.f, graded);
  }
  if (highlight_contrast != 1.f) {
    const float3 distance = max(graded - 1.f, 0.f);
    const float3 squared = distance * distance;
    const float3 weight = renodx::math::Select(squared > 0.f, exp2(-rcp(squared)), 0.f);
    const float3 flat_distance = (1.f + squared) * weight;
    const float3 weighted_inverse = renodx::math::Select(weight > 0.f, weight / squared, 0.f);
    const float3 flat_derivative = 2.f * distance * weight
                                   + renodx::math::DivideSafe(2.f * log(2.f) * (1.f + squared) * weighted_inverse, distance, 0.f);
    const float power = 0.5f * (highlight_contrast - 1.f);
    const float3 factor = pow(1.f + flat_distance, power);
    const float3 adjusted = graded + distance * (factor - 1.f);
    logarithmic_slope *= renodx::math::DivideSafe(graded, adjusted, 1.f)
                         * (factor + distance * factor * power * flat_derivative / (1.f + flat_distance));
    graded = adjusted;
  }
  if (shadow_contrast != 1.f) {
    const float3 distance = saturate(1.f - graded);
    const float3 squared = distance * distance;
    const float3 weight = renodx::math::Select(squared > 0.f, exp2(1.f - rcp(squared)), 0.f);
    const float3 flat_distance = squared * distance * weight;
    const float3 flat_derivative = -weight * (3.f * squared + 2.f * log(2.f));
    logarithmic_slope *= 1.f + graded * (1.f - shadow_contrast) * flat_derivative / (1.f + flat_distance);
    graded *= pow(1.f + flat_distance, 1.f - shadow_contrast);
  }
  if (highlights != 1.f || shadows != 1.f) {
    const float inverse_range = 1.f / 7.f;
    const float3 stops = log2(renodx::math::Select(graded == 0.f, 1.f, graded));
    float3 displacement = 0.f;
    float3 slope = 1.f;
    if (highlights != 1.f) {
      const float adjustment = highlights - 1.f;
      const float amount = adjustment * mad(1.5f, abs(adjustment), 0.5f);
      float3 derivative;
      displacement += amount * custom_psycho35_CInfinityTransition((stops - 1.f) * inverse_range, derivative);
      slope += amount * derivative * inverse_range;
    }
    if (shadows != 1.f) {
      const float adjustment = shadows - 1.f;
      const float amount = adjustment * mad(1.5f, abs(adjustment), 0.5f);
      float3 derivative;
      displacement += amount * custom_psycho35_CInfinityTransition((-1.f - stops) * inverse_range, derivative);
      slope -= amount * derivative * inverse_range;
    }
    graded *= exp2(displacement);
    logarithmic_slope *= slope;
  }
  logarithmic_slope = renodx::math::Select(normalized == 0.f, 0.f, logarithmic_slope);
  return graded;
}

// Return normalized cone response and its derivative with respect to log(q).
// Compression is the supplied shoulder strength; there is no automatic fit.
float3 custom_psycho35_Response(
    float3 source_ratio, float3 anchor_out, float peak, float compression, float clip_point,
    float contrast, float flare, float highlight_contrast, float shadow_contrast, float highlights, float shadows,
    out float3 derivative) {
  float3 grade_slope;
  const float3 graded = custom_psycho35_ApplyAnchoredTonalGrading(
                            source_ratio, contrast, flare, highlight_contrast, shadow_contrast, highlights, shadows, grade_slope)
                        * anchor_out;
  const float3 peak_lms = peak * PSYCHO35_D65_WHITE_LMS;
  const float3 clip_lms = clip_point * PSYCHO35_D65_WHITE_LMS;
  const float3 response = custom_psycho35_CInfinityRollOff(graded, peak_lms, anchor_out, compression, clip_lms);
  const float3 range = clip_lms - anchor_out;
  const float3 distance = min(max(graded - anchor_out, 0.f), range);
  const float3 inverse = renodx::math::DivideSafe((peak_lms - anchor_out) * (range - distance), abs(clip_lms - peak_lms) * distance, 0.f);
  const float3 weight = exp2(-inverse / compression);
  const float3 denominator = weight + inverse;
  const float3 inverse_derivative = renodx::math::DivideSafe((peak_lms - anchor_out) * range, abs(clip_lms - peak_lms) * distance * distance, 0.f);
  const float3 transition_derivative = (weight / denominator
                                        + distance * weight * inverse_derivative * (1.f + log(2.f) * inverse / compression)
                                              / (denominator * denominator))
                                       / range;
  float3 response_slope = mad(peak_lms - clip_lms, transition_derivative, 1.f);
  response_slope = renodx::math::Select(graded <= anchor_out, 1.f, renodx::math::Select(graded >= clip_lms, 0.f, response_slope));
  derivative = response_slope * graded * grade_slope / peak_lms;
  return renodx::math::Select(response == peak_lms, 1.f, response / peak_lms);
}

float3 custom_psychotm_test35(
    float3 bt709_linear_input,
    float peak_value = 1000.f / 203.f,
    float exposure = 1.f,
    float highlights = 1.f,
    float shadows = 1.f,
    float contrast = 1.f,
    float flare = 0.f,
    float highlight_contrast = 1.f,
    float shadow_contrast = 1.f,
    float purity_scale = 1.f,
    float highlight_saturation = 1.f,
    float dechroma = 0.f,
    float3 current_adaptive_state_bt709 = 0.18f,
    float3 current_background_state_bt709 = 0.18f,
    float sdr_eotf_emulation = 0.f,
    float gamut_compression = 1.f,
    int gamut_compression_mode = 0,
    float compression = 1.5f,
    float clip_point = 100.f,
    bool human_gamut_conditioning = false,
    bool gamut_fill = false,
    int fit_mode = 2,
    float fit_power = 0.f) {
  // Convert extended BT.709 directly without source-gamut contraction.
  const float3 input_lms = mul(PSYCHO35_BT709_TO_LMS_MAT, bt709_linear_input * exposure);
  const float3 anchor_in = mul(PSYCHO35_BT709_TO_LMS_MAT, current_adaptive_state_bt709);
  const float3 anchor_out = all(current_background_state_bt709 == current_adaptive_state_bt709)
                                ? anchor_in
                                : mul(PSYCHO35_BT709_TO_LMS_MAT, current_background_state_bt709);
  float3 source_ratio = input_lms / anchor_in;
  if (all(source_ratio == 0.f)) return 0.f;
  if (purity_scale != 1.f || highlight_saturation != 1.f || dechroma != 0.f) {
    float effective_purity = purity_scale;
    if (highlight_saturation != 1.f || dechroma != 0.f) {
      const float relative_yf = max(dot(PSYCHO35_YF_WEIGHTS, source_ratio) / dot(PSYCHO35_YF_WEIGHTS, 1.f.xxx), 0.f);
      const float t = saturate(log2(max(relative_yf, 1.f)) / (2.75f * log2(10.f)));
      const float rolloff = t * t * t * mad(t, mad(6.f, t, -15.f), 10.f);
      effective_purity *= mad(-dechroma, rolloff, 1.f);
      effective_purity *= mad(highlight_saturation - 1.f, rolloff * rolloff * mad(0.5f, rolloff, 0.5f) * (2.f / 3.f), 1.f);
    }
    const float neutral = dot(PSYCHO35_YF_WEIGHTS, input_lms) / dot(PSYCHO35_YF_WEIGHTS, anchor_in);
    const float3 direction = source_ratio - neutral;
    // Keep test35's common purity bound, rather than clipping cones separately.
    if (all(source_ratio >= 0.f) && neutral > 0.f && effective_purity > 1.f) {
      [unroll]
      for (int cone = 0; cone < 3; ++cone) {
        if (direction[cone] < 0.f) effective_purity = min(effective_purity, neutral / -direction[cone]);
      }
      source_ratio = max(neutral + effective_purity * direction, 0.f);
    } else {
      source_ratio = neutral + effective_purity * direction;
    }
  }
  if (sdr_eotf_emulation != 0.f) {
    source_ratio = renodx::color::correct::GammaSafe(source_ratio * anchor_in / PSYCHO35_D65_WHITE_LMS)
                   * PSYCHO35_D65_WHITE_LMS / anchor_in;
  }
  if (human_gamut_conditioning) {
    // Retain test35's homogeneous human-boundary intersection and local A/C
    // trade, evaluating the substituted anchored response and its derivative.
    const float mean_q = psycho35_Mean(source_ratio);
    if (!(mean_q > 0.f) || isinf(mean_q)) return PSYCHO35_FAILED.xxx;
    const float3 direction_q = source_ratio - mean_q;
    float3 origin_weighted = (anchor_in * mean_q) * PSYCHO35_MB_WEIGHTS;
    float3 direction_weighted = (anchor_in * direction_q) * PSYCHO35_MB_WEIGHTS;
    const float origin_y = origin_weighted.x + origin_weighted.y;
    if (!(origin_y > 0.f) || isinf(origin_y)) return PSYCHO35_FAILED.xxx;
    origin_weighted /= origin_y;
    direction_weighted /= origin_y;
    if (any(isnan(origin_weighted)) || any(isinf(origin_weighted))
        || any(isnan(direction_weighted)) || any(isinf(direction_weighted))) return PSYCHO35_FAILED.xxx;
    const float direction_y = direction_weighted.x + direction_weighted.y;
    float boundary_t = 1.f;
    [unroll]
    for (int edge = 0; edge < renodx::color::gamut::CIE1702_EDGE_COUNT; ++edge) {
      const float2 normal = renodx::color::gamut::CIE1702_HALFSPACE_NORMALS[edge];
      const float offset = renodx::color::gamut::CIE1702_HALFSPACE_NUMERATORS[edge] + dot(normal, PSYCHO35_MB_WHITE);
      const float margin = offset * (origin_weighted.x + origin_weighted.y) - dot(normal, origin_weighted.xz);
      const float denominator = dot(normal, direction_weighted.xz) - offset * direction_y;
      if (!(margin >= 0.f) || isnan(denominator) || isinf(denominator)) return PSYCHO35_FAILED.xxx;
      if (denominator > 0.f) boundary_t = min(boundary_t, margin / denominator);
    }
    [unroll]
    for (int cone = 0; cone < 3; ++cone) {
      if (direction_q[cone] < 0.f) boundary_t = min(boundary_t, mean_q / -direction_q[cone]);
    }
    if (!(origin_weighted.x + origin_weighted.y + boundary_t * direction_y > 0.f)) return PSYCHO35_FAILED.xxx;
    if (boundary_t < 1.f) {
      float3 boundary_q = mean_q + boundary_t * direction_q;
      if (any(boundary_q < -1e-6f * mean_q)) return PSYCHO35_FAILED.xxx;
      if (any(boundary_q <= 0.f)) boundary_q = lerp(boundary_q, mean_q.xxx, 1e-6f);
      if (any(boundary_q <= 0.f) || any(isnan(boundary_q)) || any(isinf(boundary_q))) return PSYCHO35_FAILED.xxx;
      float3 exposure_derivative;
      const float3 response = custom_psycho35_Response(
          boundary_q, anchor_out, peak_value, compression, clip_point, contrast,
          flare, highlight_contrast, shadow_contrast, highlights, shadows, exposure_derivative);
      const float derivative_scale = max(exposure_derivative.x, max(exposure_derivative.y, exposure_derivative.z));
      if (!(derivative_scale > 0.f) || isinf(derivative_scale)) return PSYCHO35_FAILED.xxx;
      exposure_derivative /= derivative_scale;
      const float3 incoming_derivative = exposure_derivative * direction_q / boundary_q;
      const float3 residual = response - psycho35_Mean(response);
      const float radius = length(residual);
      if (!(radius > 0.f) || isinf(radius)
          || any(isnan(exposure_derivative)) || any(isinf(exposure_derivative))
          || any(isnan(incoming_derivative)) || any(isinf(incoming_derivative))) return PSYCHO35_FAILED.xxx;
      const float incoming_speed = length(float2(sqrt(3.f) * psycho35_Mean(incoming_derivative), dot(residual / radius, incoming_derivative)));
      const float exposure_speed = length(float2(sqrt(3.f) * psycho35_Mean(exposure_derivative), dot(residual / radius, exposure_derivative)));
      if (!(exposure_speed > 0.f) || isnan(exposure_speed) || isinf(exposure_speed)
          || isnan(incoming_speed) || isinf(incoming_speed)) return PSYCHO35_FAILED.xxx;
      boundary_q *= 1.f + (1.f - boundary_t) * incoming_speed / exposure_speed;
      if (any(isnan(boundary_q)) || any(isinf(boundary_q))) return PSYCHO35_FAILED.xxx;
      source_ratio = boundary_q;
    }
    source_ratio = max(source_ratio, 0.f);
  }
  if (all(source_ratio == 0.f)) return 0.f;
  float3 unused_derivative;
  const float3 response = custom_psycho35_Response(
      source_ratio, anchor_out, peak_value, compression, clip_point, contrast,
      flare, highlight_contrast, shadow_contrast, highlights, shadows, unused_derivative);
  const float3 deficit = 1.f - response;
  const float response_mean = psycho35_Mean(response);
  const float mean = peak_value * response_mean;
  const float headroom = peak_value * psycho35_Mean(deficit);
  float3 residual = response_mean > 0.5f ? psycho35_Mean(deficit) - deficit : response - response_mean;
  const float scale = max(abs(residual.x), max(abs(residual.y), abs(residual.z)));
  const float3 scaled = scale > 0.f ? residual / scale : 0.f;
  const float radius = length(scaled);
  float3 direction = radius > 0.f ? scaled / radius : 0.f;
  // The mean-A2 source direction stays adaptation-relative, before the tonal
  // response. Normalize its exposure scale before taking a length, as in v35.
  const float drive = max(abs(source_ratio.x), max(abs(source_ratio.y), abs(source_ratio.z)));
  const float3 normalized_source = source_ratio / drive;
  const float3 source_residual = normalized_source - psycho35_Mean(normalized_source);
  const float source_radius = length(source_residual);
  if (source_radius > 0.f && radius > 0.f) {
    const float3 midpoint = source_residual / source_radius + direction;
    const float midpoint_length = length(midpoint);
    if (midpoint_length > 0.f) direction = midpoint / midpoint_length;
  }
  residual = peak_value * scale * radius * direction;
  if (gamut_compression == 0.f || fit_mode == 0) return mean + psycho35_TargetResidual(residual, 0);
  if (all(residual == 0.f)) return mean.xxx;
  const float3 target_displacement = psycho35_TargetResidual(residual, gamut_compression_mode);
  if (fit_mode == 1) {
    float3 clipped = clamp(mean + target_displacement, 0.f, peak_value);
    if (gamut_compression_mode != 0) clipped = mul(renodx::color::BT2020_TO_BT709_MAT, clipped);
    return lerp(mean + psycho35_TargetResidual(residual, 0), clipped, gamut_compression);
  }
  if (fit_mode == 4) {
    // Keep test35's current local-shoulder diagnostic unchanged.
    const float knee = 0.95f;
    float support = rcp(knee);
    [unroll]
    for (int channel = 0; channel < 3; ++channel) {
      if (target_displacement[channel] > 0.f)
        support = min(support, headroom / target_displacement[channel]);
      else if (target_displacement[channel] < 0.f)
        support = min(support, mean / -target_displacement[channel]);
    }
    if (support >= rcp(knee)) return mean + psycho35_TargetResidual(residual, 0);
    const float excess = max(0.f, 1.f - knee * support);
    const float shoulder = support * (knee + (1.f - knee) * excess / (excess + (1.f - knee) * support));
    return mean + psycho35_TargetResidual(residual * lerp(1.f, shoulder, gamut_compression), 0);
  }
  if (fit_power == 0.f || (fit_mode != 4 && (gamut_fill || fit_mode == 2))) {
    float support = 1.f;
    [unroll]
    for (int channel = 0; channel < 3; ++channel) {
      if (target_displacement[channel] > 0.f)
        support = min(support, headroom / target_displacement[channel]);
      else if (target_displacement[channel] < 0.f)
        support = min(support, mean / -target_displacement[channel]);
    }
    return mean + psycho35_TargetResidual(residual * lerp(1.f, support, gamut_compression), 0);
  }
  if (fit_power == 2.f) {
    float3 pressure = 0.f;
    bool endpoint_contact = false;
    [unroll]
    for (int channel = 0; channel < 3; ++channel) {
      if (target_displacement[channel] != 0.f) {
        const float available = target_displacement[channel] > 0.f ? headroom : mean;
        if (available == 0.f)
          endpoint_contact = true;
        else
          pressure[channel] = abs(target_displacement[channel]) / available;
      }
    }
    if (endpoint_contact) return mean + psycho35_TargetResidual(residual * (1.f - gamut_compression), 0);
    const float normalization = max(1.f, max(pressure.x, max(pressure.y, pressure.z)));
    if (all(isfinite(pressure))) {
      const float inverse = rcp(normalization);
      const float3 normalized_pressure = pressure / normalization;
      const float support = inverse * rsqrt(inverse * inverse + dot(normalized_pressure, normalized_pressure));
      return mean + psycho35_TargetResidual(residual * lerp(1.f, support, gamut_compression), 0);
    }
  }
  float3 log_pressure = PSYCHO35_NEGATIVE_INFINITY;
  bool endpoint_contact = false;
  [unroll]
  for (int channel = 0; channel < 3; ++channel) {
    if (target_displacement[channel] != 0.f) {
      const float available = target_displacement[channel] > 0.f ? headroom : mean;
      if (available == 0.f)
        endpoint_contact = true;
      else
        log_pressure[channel] = log(abs(target_displacement[channel])) - log(available);
    }
  }
  const float largest_log = max(0.f, max(log_pressure.x, max(log_pressure.y, log_pressure.z)));
  const float3 scaled_pressure = exp(fit_power * (log_pressure - largest_log));
  const float support = endpoint_contact ? 0.f
                                         : exp(-largest_log - log(exp(-fit_power * largest_log) + scaled_pressure.x + scaled_pressure.y + scaled_pressure.z) / fit_power);
  return mean + psycho35_TargetResidual(residual * lerp(1.f, support, gamut_compression), 0);
}

}  // namespace psychov
}  // namespace tonemap
}  // namespace renodx

#endif  // PSYCHOV_CUSTOMTEST35_HLSLI_