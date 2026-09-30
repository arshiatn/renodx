// ROME REMASTERED lensflare00 - premultiplied flare sprite into the R11G11B10 scene buffer.
// Slider (HDR only) scales colour and alpha, so 0 removes it under any blend mode.

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb7 : register(b7) {
  float4 cb7[5];
}

#define cmp -
#include "../shared.h"

void main(
    float4 v0 : SV_POSITION0,
    float2 v1 : TEXCOORD0,
    out float4 o0 : SV_TARGET0) {
  float4 flare = t0.Sample(s0_s, v1.xy);
  flare.w *= cb7[4].x;
  if (cb7[4].y >= flare.w) discard;

  float lensflare = (HDR >= 0.5f) ? SI.lensflare : 1.f;
  float3 color = flare.xyz * flare.w * lensflare + 1.f;

  // stock triangular dither at R11G11B10 precision
  float noise = t1.Load(int3(uint2(v0.xy) & 63u, 0)).x;
  float2 tri_sign = (noise < 0.5f) ? float2(1.f, -1.f) : float2(-1.f, 1.f);
  float tri = tri_sign.x * sqrt(1.f - abs(noise * 2.f - 1.f)) + tri_sign.y;
  float3 ulp = max(asfloat((asuint(color) & 0x7F800000u) + uint3(0xFD000000u, 0xFD000000u, 0xFD800000u)), 0.f);
  color += float3(ulp.x, -ulp.y, ulp.z) * tri;

  o0.xyz = color - 1.f;
  o0.w = flare.w * saturate(lensflare);
}
