#include "./shared.h"

// DX11 proxy (the game is DX9): the game's back buffer (FP16 copy) to the HDR swap chain.
// Back buffer: gamma 2.2, fixed 203-nit reference. Scene and UI are scaled at their draws.

Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

float4 main(float4 vpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  float3 x = t0.Sample(s0, uv).xyz;

  // Signed: PsychoV uses signed BT.709 to represent BT.2020.
  x = renodx::color::gamma::DecodeSafe(x);

  // The UI may still use shaders authored for UNORM blending. Apply the final
  // output safeguards in BT.2020, preserving signed BT.709 wide-gamut colours.
  // This bounds the final composite; it is not a substitute for fixing an
  // individual UI shader if its blending math is incompatible with FP16.
  float3 bt2020 = max(renodx::color::bt2020::from::BT709(x), 0.f);
  // The SDR mode already clips scene highlights in postprocess00. Keep this
  // display-output bound independent of UI in every mode, including SDR.
  float peak = max(SI.peak_white_nits, 0.f) / RUSE_REFERENCE_WHITE;
  float max_channel = max(bt2020.r, max(bt2020.g, bt2020.b));
  if (max_channel > peak) bt2020 *= peak / max_channel;

  // HDR10
  if (SI.swap_chain_encoding == 0) {
    x = bt2020 * (RUSE_REFERENCE_WHITE / 10000.f);
    x = renodx::color::pq::EncodeSafe(x);  // clamps below 0 in BT.2020
  }
  // scRGB
  else {
    x = renodx::color::bt709::from::BT2020(bt2020) * (RUSE_REFERENCE_WHITE / 80.f);
  }

  return float4(x, 1.f);
}
