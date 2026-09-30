// TROY vignette00 (same shader as Pharaoh) - writes black with the vignette amount as alpha, blended
// multiplicatively over the frame.
//
// HDR: SI.vignette scales the darkening. SDR is stock.

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
  float vignette_strength : packoffset(c0.w);
  float vignette_falloff : packoffset(c1);
  float sun_flare_ps : packoffset(c1.y);
  float4 photomode_crop : packoffset(c2);
}



// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = saturate(photomode_crop.xz * v0.xy + photomode_crop.yw);
  r0.xy = half_texel_offset.xy + r0.xy;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.xy = r0.xy + r0.xy;
  r0.x = dot(r0.xy, r0.xy);

  // log2(0) at the exact screen centre is -Inf, and 0 * -Inf is NaN at falloff 0.
  r0.x = log2(max(r0.x, 1e-12f));
  r0.x = vignette_falloff * r0.x;
  r0.x = exp2(r0.x);
  r0.x = vignette_strength * r0.x;
  r0.x = r0.x * r0.x + 1;
  r0.x = r0.x * r0.x;
  r0.x = 1 / r0.x;
  r0.xy = float2(1,0.996078432) + -r0.xx;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;
  r0.z = 0;

  // r0.x is now the darkening amount (stock writes black with this as alpha), so the
  // slider scales it directly. Vignette has to be on in the in-game settings, or
  // vignette_strength is 0 and there is nothing to scale.
  if (HDR >= 0.5f) r0.x = saturate(r0.x * SI.vignette);

  o0.xyzw = r0.zzzx;
  return;
}