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

  //HDR10
  if (SI.swap_chain_encoding == 0) { return float4(x, 1.0);}

  //scRGB 
  x = max(0, x);
  x = renodx::color::pq::DecodeSafe(x, 1.f);

  //EOTF Emulate
  // if (SI.gamma_correction > 0.f) {
  //   x *= SI.graphics_white_nits / SI.diffuse_white_nits;
  //   // x *= SI.graphics_white_nits / SI.gamma_correction;
    
  //   float3 x1 = renodx::color::correct::Gamma(x, false, 2.2);
  //   x = x < 1 ? x1 : x;

  //   // x *= SI.gamma_correction / SI.graphics_white_nits;
  //   x *= SI.diffuse_white_nits / SI.graphics_white_nits;
  // }

  // Clamp Peak
  // x = min(x, SI.peak_white_nits / SI.graphics_white_nits); //TODO: renable when publishing mod

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

  //scRGB
    x *= SI.diffuse_white_nits / 8000.f;
  
  return float4(x, 1.0);
}