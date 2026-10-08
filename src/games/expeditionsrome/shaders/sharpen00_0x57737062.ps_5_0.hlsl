// Expeditions: Rome CAS (FidelityFX, no scaling). From 0x57737062.ps_5_0.cso.
// The decompile's int ops are float bit tricks (APrxLoRcp, APrxLoSqrt, APrxMedRcp), restored here.
// Stock: input squared (gamma 2 -> linear), sharpened, clamped to [0, 1], sqrt.
// Addon: values / Paper White-UI gain. SDR: stock clip. HDR: amount from the clipped neighborhood,
// faded out from 1x to 2x paper white (encoded), no clip above, no undershoot below black
// (wide-gamut negatives pass), Sharpening slider.
#include "../shared.h"

Texture2D<float4> t0 : register(t0);

cbuffer cb0 : register(b0)
{
  float4 cb0[28];
}

float RcpLo(float x) { return asfloat(0x7ef07ebbu - asuint(x)); }
float SqrtLo(float x) { return asfloat((asuint(x) >> 1u) + 0x1fbc4639u); }
float RcpMed(float x) {
  float r = asfloat(0x7ef19fffu - asuint(x));
  return r * (-r * x + 2.f);
}

void main(
  linear noperspective float4 v0 : TEXCOORD0,
  float4 v1 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  const bool hdr = HDR >= 1.f;
  const float scale = EXR_ENCODED_SCALE;

  int2 p = (int2)(cb0[0].xy * v0.xy);
  float3 b = t0.Load(int3(p + int2(0, -1), 0)).xyz / scale;
  float3 d = t0.Load(int3(p + int2(-1, 0), 0)).xyz / scale;
  float3 e = t0.Load(int3(p, 0)).xyz / scale;
  float3 f = t0.Load(int3(p + int2(1, 0), 0)).xyz / scale;
  float3 h = t0.Load(int3(p + int2(0, 1), 0)).xyz / scale;

  // Amount from the [0, 1] neighborhood (green).
  float gb = saturate(b.g), gd = saturate(d.g), ge = saturate(e.g), gf = saturate(f.g), gh = saturate(h.g);
  gb *= gb; gd *= gd; ge *= ge; gf *= gf; gh *= gh;
  float mn = min(min(min(ge, gf), gd), min(gh, gb));
  float mx = max(max(max(ge, gf), gd), max(gh, gb));
  float amp = saturate(min(mn, 1.f - mx) * RcpLo(mx));
  float w = -SqrtLo(amp) * rcp(saturate(cb0[27].x) * -3.f + 8.f);

  [branch]
  if (!hdr) {
    b = saturate(b); d = saturate(d); e = saturate(e); f = saturate(f); h = saturate(h);
  } else {
    float peak = max(max(max(renodx::math::Max(b), renodx::math::Max(d)), max(renodx::math::Max(f), renodx::math::Max(h))),
                     renodx::math::Max(e));
    w = max(w * SI.sharpening, -0.2f) * saturate(2.f - peak);
  }

  // Squared (signed: wide-gamut negatives stay negative).
  b *= abs(b); d *= abs(d); e *= abs(e); f *= abs(f); h *= abs(h);
  float3 sum = d * w;
  sum = b * w + sum;
  sum = f * w + sum;
  sum = h * w + sum;
  sum = e + sum;
  sum *= RcpMed(w * 4.f + 1.f);

  [branch]
  if (hdr) {
    sum = max(sum, min(min(min(min(b, d), min(f, h)), e), 0.f));
    o0.xyz = renodx::math::SignSqrt(sum) * scale;
  } else {
    o0.xyz = sqrt(saturate(sum)) * scale;
  }
  o0.w = 0;
}
