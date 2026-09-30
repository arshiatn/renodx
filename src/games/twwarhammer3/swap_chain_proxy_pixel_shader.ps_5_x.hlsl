#include "./shared.h"

Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

// rect is top left (x,y), bottom right (x,y)
float3 DrawRect(float2 uv, float4 rect, float3 color, float3 rectColor) {
	float r = step(rect.x, uv.x) * step(uv.x, rect.z) * step(rect.y, uv.y) * step(uv.y, rect.w);
  return r == 0 ? color : rectColor;
}

float4 main(float4 vpos: SV_POSITION, float2 uv: TEXCOORD0)
    : SV_TARGET {
  float3 x = t0.Sample(s0, uv).xyz;

  // intermediate encode inv
  //  x = max(0, x); // Do not clamp here: PsychoV uses signed BT.709 to represent BT.2020.
  x = renodx::color::gamma::DecodeSafe(x);

  // EOTF Emulate
  // if (SI.gamma_correction > 0.f) {
  //   x *= SI.graphics_white_nits / SI.diffuse_white_nits;

  //   float3 corrected = renodx::color::correct::GammaSafe(x, false, 2.2f);

  //   // Apply the below-white correction symmetrically.
  //   x = renodx::math::Select(abs(x) < 1.f, corrected, x);

  //   x *= SI.diffuse_white_nits / SI.graphics_white_nits;
  // }

  // Peak Test
  // if (SI.peak_test)
  // {
  //   x = 0;
  //   x = DrawRect(uv, float4(0.35,  0.47,  0.65,  0.53),  x, 10000.f                /* / 203.f */);
  //   x = DrawRect(uv, float4(0.365, 0.483, 0.448, 0.517), x, SI.peak_white_nits * 2 /* / 203.f */);
  //   x = DrawRect(uv, float4(0.458, 0.483, 0.542, 0.517), x, SI.peak_white_nits     /* / 203.f */);
  //   x = DrawRect(uv, float4(0.552, 0.483, 0.635, 0.517), x, SI.peak_white_nits / 2 /* / 203.f */);
  //   x /= SI.graphics_white_nits;
  // }

  // // Clamp Peak
  // // For Total Wars: "HDR" instead of rendering mode
  // float ceiling_nits = (HDR >= 0.5f) ? SI.peak_white_nits : max(SI.diffuse_white_nits, SI.graphics_white_nits);
  // x = min(x, ceiling_nits / SI.graphics_white_nits);       // TODO: renable when publishing mod

  // //HDR10
  // if (SI.swap_chain_encoding == 0) {
  //   x *= SI.graphics_white_nits / 10000.f;
  //   x = renodx::color::bt2020::from::BT709(x);
  //   x = renodx::color::pq::EncodeSafe(x);
  // }
  // //scRGB
  // else {
  //   x *= SI.graphics_white_nits / 80.f;
  // }

  // get rid of "negative" nits of UI:
  // Clamp in the TARGET gamut, not in signed BT.709 representation.
  float3 x_bt2020 = renodx::color::bt2020::from::BT709(x);
  x_bt2020 = max(x_bt2020, 0.f);

  // Clamp Peak
  // TODO: renable when publishing
  float peak = ((HDR >= 0.5f) ? SI.peak_white_nits: max(SI.diffuse_white_nits, SI.graphics_white_nits))
               / SI.graphics_white_nits;
  float mx = max(x_bt2020.r, max(x_bt2020.g, x_bt2020.b));

  if (mx > peak)
  {
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