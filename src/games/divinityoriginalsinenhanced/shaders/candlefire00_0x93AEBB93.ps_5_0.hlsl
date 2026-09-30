//for candles's fire

#include "../shared.h"

cbuffer _Globals : register(b0)
{
    float _OpacityFade : packoffset(c0);
    float FloatParameter_Brightness : packoffset(c0.y);
}

SamplerState _DefaultClampSampler_s : register(s0);
Texture2D<float4> Texture2DParameter_DiffuseTexture_DefaultClampSampler_SRGB : register(t0);

struct PSInput
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float z : TEXCOORD1;
  float w : TEXCOORD2;
  float4 color : COLOR0;
};

void main(
    PSInput input,
    out float4 o0: SV_Target0)
{
  float4 r0;

  r0.xyz = Texture2DParameter_DiffuseTexture_DefaultClampSampler_SRGB
               .Sample(_DefaultClampSampler_s, input.uv) .xyz;

  r0.xyz *= input.color.xyz;
  r0.xyz *= FloatParameter_Brightness;

  // TEXCOORD1/2. Same math and input layout as the stock asm (v1.z, v1.w).
  r0.xyz *= input.z;
  r0.xyz *= input.w;

  
  r0.xyz *= _OpacityFade;

  r0.w = _OpacityFade;

  o0 = max(r0, 0.0);

  if (TONEMAP_MODE == TONEMAP_PRAGMAP) {
    // A=(2,1.5), B=(5,0.85), D=(8,0.7), D=(12.5,0.65) with  f(x)=TrendPoly({A,B,C,D},3). Same as baziers but DIVIDE BY 1.5
    float c3 = -0.0017166372722f;
    float c2 = 0.0442680776014f;
    float c1 = -0.3873721340388f;
    float c0 = 1.6114050558495f;
    float p = HDR_PEAK;
    float x = ((c3 * p + c2) * p + c1) * p + c0;
    x = clamp(x, 0.30f, 3.f);
    o0.rgb *= p * x * SI.candlehighlightmultiplier;
  } else if (TONEMAP_MODE == TONEMAP_PSYCHO) {
    o0.rgb *= SI.candlehighlightmultiplierpsycho;
  }
}