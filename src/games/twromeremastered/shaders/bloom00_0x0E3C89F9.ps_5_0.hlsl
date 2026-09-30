// ROME REMASTERED bloom00 - additive bloom (texture x cb0[1].z, alpha 0) into the R11G11B10 buffer.
// Slider (HDR only) scales the bloom.

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0) {
  float4 cb0[2];
}

#define cmp -
#include "../shared.h"

void main(
    float4 v0 : SV_POSITION0,
    float2 v1 : TEXCOORD0,
    out float4 o0 : SV_TARGET0) {
  float bloom_scale = cb0[1].z * ((HDR >= 0.5f) ? SI.bloom : 1.f);
  float3 bloom = t0.Sample(s0_s, v1.xy).xyz * bloom_scale;

  // stock triangular dither at R11G11B10 precision
  float noise = t1.Load(int3(uint2(v0.xy + float2(13.f, 11.f)) & 63u, 0)).x;
  float2 tri_sign = (noise < 0.5f) ? float2(1.f, -1.f) : float2(-1.f, 1.f);
  float tri = tri_sign.x * sqrt(1.f - abs(noise * 2.f - 1.f)) + tri_sign.y;
  float3 offset = min(bloom * 2.f + 1.f, bloom * 8.f);
  float3 color = bloom + offset;
  float3 ulp = max(asfloat((asuint(color) & 0x7F800000u) + uint3(0xFD000000u, 0xFD000000u, 0xFD800000u)), 0.f);
  color += float3(ulp.x, -ulp.y, ulp.z) * tri;

  o0.xyz = color - offset;
  o0.w = 0.f;
}
