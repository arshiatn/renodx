#ifndef SRC_TWWARHAMMER_AUTOHDR_HLSLI_
#define SRC_TWWARHAMMER_AUTOHDR_HLSLI_

#include "./shared.h"

// AutoHDR for 8-bit SDR sources (Tales of Arise's Video AutoHDR, from Ryse: Son of Rome): linear luminance above mid
// gray stretched (smooth start, hue kept) so SDR white (1) reaches lerp(1, Peak / Paper White, amount). HDR only.
float3 AutoHdrHighlights(float3 color, float amount) {
  if (HDR < 1.f || amount <= 0.f) return color;
  float y = renodx::color::y::from::BT709(color);
  if (y <= 0.18f) return color;
  float t = saturate((y - 0.18f) / (1.f - 0.18f));
  float white = lerp(1.f, HDR_PEAK, amount);
  return color * ((y + (white - 1.f) * t * t) / y);
}

#endif  // SRC_TWWARHAMMER_AUTOHDR_HLSLI_
