#ifndef DIVINITY2_VANILLAPLUS_HLSLI_
#define DIVINITY2_VANILLAPLUS_HLSLI_

namespace dos2_vanillaplus {

float MaxChannel(float3 color) {
  return max(color.r, max(color.g, color.b));
}

float LuminanceBT2020(float3 color) {
  return dot(color, float3(0.2627f, 0.6780f, 0.0593f));
}

// Keep in-gamut values unchanged. Project out-of-gamut values toward a neutral
// of the same luminance, stopping at the selected BT.2020 RGB-cube boundary.
// If luminance itself exceeds peak, only peak white can represent the endpoint.
float3 FitBT2020(float3 color, float peak) {
  if (!all(isfinite(color))) return float3(0.f, 0.f, 0.f);
  float y = LuminanceBT2020(color);
  if (!(y > 0.f)) return float3(0.f, 0.f, 0.f);
  if (y >= peak) return float3(peak, peak, peak);

  float3 delta = color - y;
  float allowed = 1.f;
  for (int channel = 0; channel < 3; ++channel) {
    if (delta[channel] > 0.f) {
      allowed = min(allowed, (peak - y) / delta[channel]);
    } else if (delta[channel] < 0.f) {
      allowed = min(allowed, y / -delta[channel]);
    }
  }
  if (allowed >= 1.f) return color;
  return clamp(y + delta * saturate(allowed), 0.f, peak);
}

// A colour/luminance selection, not an object mask: other bright reddish
// materials can also qualify. Scene levels <= 1 are untouched; >= 4 receive
// full highlight weight. Neutral highlights remain neutral.
float3 ApplyFireColor(float3 bounded_bt2020, float3 exposed_scene_bt709,
                      float peak, float strength, float target_hue) {
  if (!(strength > 0.f)) return bounded_bt2020;
  float highlight_weight = smoothstep(1.f, 4.f, MaxChannel(exposed_scene_bt709));
  if (!(highlight_weight > 0.f)) return bounded_bt2020;

  float3 positive_bt709 = max(renodx::color::bt709::from::BT2020(bounded_bt2020), 0.f);
  float highest = MaxChannel(positive_bt709);
  if (!(highest > 1e-8f)) return bounded_bt2020;
  float redness = (positive_bt709.r - max(positive_bt709.g, positive_bt709.b)) / highest;
  float weight = saturate(strength) * highlight_weight * smoothstep(0.f, 0.15f, redness);
  if (!(weight > 0.f)) return bounded_bt2020;

  // Change the hue between red and yellow while retaining the RGB min/max
  // saturation of the source. This does not recolour white fire cores.
  float lowest = min(positive_bt709.r, min(positive_bt709.g, positive_bt709.b));
  float3 target_bt709 = float3(highest, lerp(lowest, highest, saturate(target_hue)), lowest);
  float3 target = renodx::color::bt2020::from::BT709(target_bt709);
  float y = LuminanceBT2020(bounded_bt2020);
  float target_y = LuminanceBT2020(target);
  if (!(y > 1e-8f) || !(target_y > 1e-8f)) return bounded_bt2020;
  target *= y / target_y;

  // Limit the colour change along a line of constant luminance, so the
  // correction cannot exceed peak or introduce negative output channels.
  float3 delta = target - bounded_bt2020;
  float allowed = 1.f;
  for (int channel = 0; channel < 3; ++channel) {
    if (delta[channel] > 0.f) {
      allowed = min(allowed, max(peak - bounded_bt2020[channel], 0.f) / delta[channel]);
    } else if (delta[channel] < 0.f) {
      allowed = min(allowed, max(bounded_bt2020[channel], 0.f) / -delta[channel]);
    }
  }
  return clamp(bounded_bt2020 + delta * (weight * saturate(allowed)), 0.f, peak);
}

// Full RenoDX ACES, with no native pre/post highlight matrices around it.
// Keep the supplied API's 48-unit normalization; calibrate exposure separately.
float3 ACES(float3 scene_bt709, float paper_white, float peak_nits) {
  float3x3 identity_ap1 = float3x3(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
  return renodx::tonemap::aces::RGCAndRRTAndODT(
      scene_bt709, (0.0001f / paper_white) * 48.f,
      (peak_nits / paper_white) * 48.f, identity_ap1) / 48.f;
}

// Solve against the actual RenoDX implementation linked by the user's build.
// An exposure change preserves ACES's selected peak; multiplying its already
// tonemapped output by a gray correction would also change that peak.
// Called once per 1024-thread LUT group, then shared by every thread in it.
float ACESGrayExposure(float paper_white, float peak_nits) {
  float lower_stops = -16.f;
  float upper_stops = 16.f;
  [loop]
  for (int iteration = 0; iteration < 20; ++iteration) {
    float middle_stops = 0.5f * (lower_stops + upper_stops);
    float gray = DOS2_GRAY_INPUT * exp2(middle_stops);
    float3 mapped_ap1 = ACES(float3(gray, gray, gray), paper_white, peak_nits);
    float mapped_y = LuminanceBT2020(renodx::color::bt2020::from::AP1(mapped_ap1));
    if (mapped_y < DOS2_GRAY_OUTPUT) {
      lower_stops = middle_stops;
    } else {
      upper_stops = middle_stops;
    }
  }
  return exp2(0.5f * (lower_stops + upper_stops));
}

}  // namespace dos2_vanillaplus

#endif
