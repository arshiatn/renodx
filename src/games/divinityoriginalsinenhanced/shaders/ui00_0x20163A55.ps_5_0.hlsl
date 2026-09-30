//UI-Shader that gets loaded as last


Texture2D<float4> Base : register(t0);

// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0, 0);
  r0 = Base.Load(r0.xyz);

  // This copy must also be safe when Base contains the composed HDR scene.
  // Keep alpha bounded for blending, but do not clamp HDR RGB to paper white.
  o0 = HDR == 0.f ? saturate(r0) : float4(r0.rgb, saturate(r0.a));
  o0.xyzw *= SI.ui;

  return;
}
