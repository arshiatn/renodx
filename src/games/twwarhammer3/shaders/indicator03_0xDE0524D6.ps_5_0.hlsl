// 0xDE0524D6 (fx_indicator): an indicator without fades: the texture x the vertex colour.
// Unit/Building Indicator Brightness (SI.someindicators) scales the colour. The add-on draws this copy only with the
// slider off 1 in an HDR mode; otherwise the game's own bytes run (addon.cpp, BuildRuntimeData).

SamplerState indicator_texture_sampler_s : register(s0);
Texture2D<float4> g_indicator_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = g_indicator_texture.Sample(indicator_texture_sampler_s, v1.xy).xyzw;
  r1.x = r0.w * v3.w + -0.00392156886;
  r0.xyzw = v3.xyzw * r0.xyzw;
  o0.xyzw = r0.xyzw;
  r0.x = cmp(r1.x < 0);
  if (r0.x != 0) discard;
  o0.xyz *= SI.someindicators;
  return;
}
