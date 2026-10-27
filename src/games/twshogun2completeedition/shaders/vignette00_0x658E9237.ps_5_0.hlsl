// lenseflare.hlsl ps30_vignette: black at the screen's edges by alpha (off with the sun flare off). HDR: x Vignette.
#include "../shared.h"

cbuffer lense_flare_PS : register(b0)
{
  float2 half_texel_offset : packoffset(c0.y);
  float vignette_strength : packoffset(c0.w);
  float sun_flare_ps : packoffset(c1);
}

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float2 frame_tex = v1 + half_texel_offset;
  float d = distance(frame_tex, 0.5f) * 2.f;
  float alpha = (sun_flare_ps > 0.01f) ? pow(saturate(d - 0.6f), 2.f) : 0.f;
  // zero_alpha_ref: 1 / 255.
  clip(alpha - 0.00392156886f);
  o0 = float4(0.f, 0.f, 0.f, saturate(alpha * vignette_strength * ((HDR >= 1.f) ? SI.vignette : 1.f)));
}
