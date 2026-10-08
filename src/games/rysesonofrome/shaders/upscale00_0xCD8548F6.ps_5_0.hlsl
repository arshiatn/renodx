// Ryse: Son of Rome native upscaling. From 0xCD8548F6.ps_5_0.cso (3Dmigoto).
// Stock: bilinear at a smoothstep-warped texel position (vParams.xy = source size), sRGB out.
// The game read t0 through an sRGB view (decode, then filter). Here it is an FP16 clone (no
// hardware decode): decoded by hand, filtered after decoding. In / Paper White-UI gain, out x it.
#include "../shared.h"

cbuffer PER_BATCH : register(b0)
{
  float4 vParams : packoffset(c0);
}

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  const float scale = RYSE_ENCODED_SCALE;

  r0.xy = v1.xy * vParams.xy + float2(0.5,0.5);
  r0.zw = floor(r0.xy);
  r0.xy = r0.xy + -r0.zw;
  r1.xy = r0.xy * r0.xy;
  r0.xy = -r0.xy * float2(2,2) + float2(3,3);
  r0.xy = r1.xy * r0.xy + r0.zw;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.xy = r0.xy / vParams.xy;
  r0.xyzw = RyseSampleSRGB(_tex0, r0.xy, scale);
  // Stock sRGB (signed, unclamped) x Paper White-UI gain.
  o0.xyz = RyseEncodeSRGB(r0.xyz) * scale;
  o0.w = r0.w;
}
