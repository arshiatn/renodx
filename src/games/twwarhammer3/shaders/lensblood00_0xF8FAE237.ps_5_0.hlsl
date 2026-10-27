// ---- Created with 3Dmigoto v1.3.16 (RenoDX pack dump, 2026-10-20)

// lenseflare.hlsl blood splatter on the lens


SamplerState lense_flare_sampler_s : register(s0);
Texture2D<float4> blood_splatter_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../lensflare.hlsli"


void main(
  float4 v0 : SV_Position0,
  float3 v1 : TC0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = blood_splatter_texture.Sample(lense_flare_sampler_s, v1.xy).y;
  r0.x = v1.z * r0.x;
  r0.y = cmp(r0.x < 0.449999988);
  r0.z = r0.x * 20 + -8.55000019;
  r0.x = r0.y ? r0.z : r0.x;
  o0.x = 1 + -r0.x;
  o0.w = r0.x;
  o0.yz = float2(0,0);
  // The red reached ~9.5 where the 8-bit target clipped it to 1 (FP16 now): clamped, at Paper White as the flares.
  o0 = ScaleLensOverlay(o0, 1.f);
  return;
}