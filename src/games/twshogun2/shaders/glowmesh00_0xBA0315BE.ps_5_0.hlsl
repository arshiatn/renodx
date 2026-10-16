// rigidglowcolour.hlsl ps30_main: glowing meshes in their vertex color (burning buildings' fire panels).
// HDR: x the draw's VFX Brightness (SI.vfx_glow_draw, by the addon: Big Fires unless set otherwise).
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  o0.xyz = 1.7f * v2.xyz;
  if (HDR >= 1.f) o0.xyz *= SI.vfx_glow_draw;
  o0.w = 1;
}
