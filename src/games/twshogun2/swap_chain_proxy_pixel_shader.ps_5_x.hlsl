#include "./shared.h"

Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

float4 main(float4 vpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  float3 x = t0.Sample(s0, uv).xyz;

  // SDR: gamma 2.2 as the game's, white = the display's.
  if (SHOGUN_SDR_OUTPUT) return float4(saturate(x), 1.f);

  // Gamma 2.2 encoded, 1 = UI white. Signed BT.709 can carry BT.2020 colors.
  x = renodx::color::gamma::DecodeSafe(x);

  // Clamp in the target gamut, then to Peak (SDR: to Paper White / UI) by the max channel.
  float3 x_bt2020 = max(renodx::color::bt2020::from::BT709(x), 0.f);
  float peak = ((HDR >= 0.5f) ? SI.peak_white_nits : max(SI.diffuse_white_nits, SI.graphics_white_nits))
               / max(SI.graphics_white_nits, 1.f);
  float mx = max(x_bt2020.r, max(x_bt2020.g, x_bt2020.b));
  if (mx > peak) x_bt2020 *= peak / mx;

  if (SI.swap_chain_encoding == 0) {
    // HDR10
    x = renodx::color::pq::EncodeSafe(x_bt2020 * (SI.graphics_white_nits / 10000.f));
  } else {
    // scRGB
    x = renodx::color::bt709::from::BT2020(x_bt2020) * (SI.graphics_white_nits / 80.f);
  }

  return float4(x, 1.f);
}
