// TROY lensflare04 - sun starburst / streak composite.
//
// No cbuffer; samples both lense_flare_2 and lense_flare_3. A per-sprite angle makes a
// one-sided UV gradient, and lense_flare_3 is multiplied by it, by intensity squared,
// and by a hard 20x before being added over lense_flare_2. That 20x blows out on
// purpose - stock's UNORM target clipped it to a white core, the saturate in
// ApplyTroyLensFlare restores that, and SI.lensflare is the way to push it back up.
//
// DECOMPILER BUG. All three inputs pack into v1, and 3Dmigoto names scalar inputs after
// the register, so it emitted "float w1 : COLOR0" twice and read w1.x for all three.
// They are different components:
//     asm 3   mul r0.x, v1.w, l(3.14)   angle       = COLOR1
//     asm 9   mul r1.x, v1.z, v1.z      intensity^2 = COLOR0
//     asm 14  mov o0.w, v1.z            alpha       = COLOR0
// Collapsing them locks the streak angle to the sprite's intensity. Declared in
// signature order below so the compiler repacks v1 as stock did.
//
// Draws after t01 (467-471) so it never gets t01's paper-white multiply and would sit
// at UI brightness; ApplyTroyLensFlare applies the same ratio, in linear because the
// frame is gamma-encoded by then. Not god rays - those are composited inside t01.

SamplerState lense_flare_sampler_s : register(s0);
Texture2D<float4> lense_flare_2_texture : register(t0);
Texture2D<float4> lense_flare_3_texture : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// Shared by all five lens flare passes. Stock colour in, final colour out.
float4 ApplyTroyLensFlare(float4 stock_color)
{
  // Restores the write clamp the upgraded FP16 target no longer provides.
  float4 flare = saturate(stock_color);

  if (HDR >= 0.5f)
  {
    // Scale in linear, by the same ratio t01 applies to the scene.
    float3 flare_linear = renodx::color::gamma::DecodeSafe(flare.xyz);
    flare_linear *= SI.lensflare * (SI.diffuse_white_nits / SI.graphics_white_nits);
    flare.xyz = renodx::color::gamma::EncodeSafe(flare_linear);
  }

  return flare;
}


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float w1 : COLOR0,   // packs to v1.z - sprite intensity, also the output alpha
  float w2 : COLOR1,   // packs to v1.w - streak angle
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Streak angle -> direction vector -> one-sided UV gradient.
  // ASM 3: v1.w, i.e. COLOR1.
  r0.x = 3.1400001 * w2.x;
  sincos(r0.x, r0.x, r1.x);
  r1.y = r0.x;
  r0.x = dot(v1.xy, r1.xy);
  r0.x = saturate(-0.300000012 + r0.x);   // ASM 7: add_sat

  // Starburst layer: texture * intensity^2 * 20.
  // ASM 9: v1.z, i.e. COLOR0.
  r0.yzw = lense_flare_3_texture.Sample(lense_flare_sampler_s, v1.xy).xyz;
  r1.x = w1.x * w1.x;
  r0.yzw = r1.xxx * r0.yzw;
  r0.yzw = float3(20,20,20) * r0.yzw;

  // Base flare layer.
  r1.xyz = lense_flare_2_texture.Sample(lense_flare_sampler_s, v1.xy).xyz;

  float4 stock_color;
  stock_color.xyz = r0.yzw * r0.xxx + r1.xyz;
  stock_color.w = w1.x;                   // ASM 14: v1.z, i.e. COLOR0

  o0.xyzw = ApplyTroyLensFlare(stock_color);
  return;
}
