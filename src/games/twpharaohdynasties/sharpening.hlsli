#ifndef SRC_GAMES_TWPHARAOHDYNASTIES_SHARPENING_HLSLI_
#define SRC_GAMES_TWPHARAOHDYNASTIES_SHARPENING_HLSLI_

#include "./shared.h"

// Sharpening (HDR): PHARAOH has no sharpening setting, so the game's g_sharpening_strength is 0. RenoDX's own strength
// (Troy's 0.10) x the Sharpening slider, in both passes that can end the frame: ps40_sharpen and ps40_chromatic_aberration.
static const float OWN_SHARPENING_STRENGTH = 0.10f;

// Stock kernel, stock order: 4*centre - up - left - right - down.
float3 SharpenLaplacian(float3 c, float3 up, float3 left, float3 right, float3 down) {
  return (((c * 5.f + (-up - left)) - right) - down) - c;
}

// Taps from the frame copy (gamma-encoded, Paper White scaled). The stock result on SDR-clipped taps in Paper White
// units; values outside 0..1 (highlights, wide gamut) pass through untouched.
float3 SharpenOwn(Texture2D<float3> frame, int2 pixel, float4 render_target_dimensions, float3 centre) {
  int2 last = int2(render_target_dimensions.xy) - 1;
  float pw_encoded = renodx::color::gamma::EncodeSafe(SI.diffuse_white_nits / SI.graphics_white_nits);
  float3 c = centre / pw_encoded;
  float3 up = frame.Load(int3(clamp(pixel + int2(0, -1), 0, last), 0)) / pw_encoded;
  float3 left = frame.Load(int3(clamp(pixel + int2(-1, 0), 0, last), 0)) / pw_encoded;
  float3 right = frame.Load(int3(clamp(pixel + int2(1, 0), 0, last), 0)) / pw_encoded;
  float3 down = frame.Load(int3(clamp(pixel + int2(0, 1), 0, last), 0)) / pw_encoded;
  float strength = OWN_SHARPENING_STRENGTH * SI.sharpening;
  float3 sdr_c = saturate(c);
  float3 sdr_sharpened = saturate(
      strength * SharpenLaplacian(sdr_c, saturate(up), saturate(left), saturate(right), saturate(down)) + sdr_c);
  return renodx::math::Select(saturate(c) == c, sdr_sharpened, c) * pw_encoded;
}

#endif  // SRC_GAMES_TWPHARAOHDYNASTIES_SHARPENING_HLSLI_
