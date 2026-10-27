// ---- Created with 3Dmigoto v1.3.16 (RenoDX dump of FeralShaders, 2026-10-20)

// ResolveMainColor.hlsl PSMain (another permutation): resolve.hlsli with tonemap off, lut on, vignette off, tint off.

Texture2D<float4> t3 : register(t3);

Texture3D<float4> t2 : register(t2);

Texture2D<float4> t0 : register(t0);

SamplerState s5_s : register(s5);

cbuffer cb0 : register(b0)
{
  float4 cb0[5];
}

#define RMC_TONEMAP 0
#define RMC_LUT 1
#define RMC_VIGNETTE 0
#define RMC_TINT 0
#include "../resolve.hlsli"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  o0 = ResolveMainColor(v1);
}
