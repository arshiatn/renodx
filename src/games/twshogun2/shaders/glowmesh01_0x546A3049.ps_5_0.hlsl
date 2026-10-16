// rigidglowtexture.hlsl ps30_main: glowing meshes in their texture's color.
// HDR: x the draw's VFX Brightness (SI.vfx_glow_draw, by the addon: Fire & Glow unless set otherwise).
#include "../shared.h"

SamplerState s_diffuse_map_s : register(s0);
Texture2D<float4> s_diffuse_map : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  o0.xyz = 2.f * s_diffuse_map.Sample(s_diffuse_map_s, v2.xy).xyz;
  if (HDR >= 1.f) o0.xyz *= SI.vfx_glow_draw;
  o0.w = 1;
}
