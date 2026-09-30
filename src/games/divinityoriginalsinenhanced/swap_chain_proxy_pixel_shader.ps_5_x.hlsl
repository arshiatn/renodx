#include "./shared.h"

Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

float4 main(float4 vpos: SV_POSITION, float2 uv: TEXCOORD0)
    : SV_TARGET {
  float3 x = t0.Sample(s0, uv).xyz;

  // Preserve signed BT.709 values while decoding the intermediate.
  x = renodx::color::gamma::DecodeSafe(x);  // game is gamma 2.2

  // Clamp in the target gamut to retain signed BT.709 wide-gamut colors.
  float3 x_bt2020 = renodx::color::bt2020::from::BT709(x);
  x_bt2020 = max(x_bt2020, 0.f);

  // Limit peak with a uniform RGB scale.
  float peak = ((HDR == 1.f) ? SI.peak_white_nits: max(SI.diffuse_white_nits, SI.graphics_white_nits))
               / SI.graphics_white_nits;
  float mx = max(x_bt2020.r, max(x_bt2020.g, x_bt2020.b));

  if (mx > peak) {
    x_bt2020 *= peak / mx;
  }

  // HDR10
  if (SI.swap_chain_encoding == 0) {
    x = x_bt2020 * (SI.graphics_white_nits / 10000.f);
    x = renodx::color::pq::EncodeSafe(x);
  }
  // scRGB
  else {
    x = renodx::color::bt709::from::BT2020(x_bt2020);
    x *= SI.graphics_white_nits / 80.f;
  }

  return float4(x, 1.0);
}
