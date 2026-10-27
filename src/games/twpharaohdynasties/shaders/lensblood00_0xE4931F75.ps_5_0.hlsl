// lenseflare.hlsl ps40_blood (the Blood pack): blood splattered on the lens in fights near the camera. Its alpha
// (texture x intensity, below 0.45 pushed down: x 20 - 8.55) and red (1 - alpha) go far outside 0-1: the game's 8-bit
// target clamped them, the upgraded FP16 one blended huge black and garbage polygons (ATN debug2: red -80, alpha 83).
// Clamped as the 8-bit target did, at Paper White as the lens flares (lensflare.hlsli).
#include "../lensflare.hlsli"

SamplerState lense_flare_sampler_s : register(s0);
Texture2D<float4> blood_splatter_texture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float3 v1 : TC0,
  out float4 o0 : SV_Target0)
{
  float amount = v1.z * blood_splatter_texture.Sample(lense_flare_sampler_s, v1.xy).y;
  if (amount < 0.45f) amount = amount * 20.f - 8.55f;
  o0 = ApplyPharaohLensOverlay(float4(1.f - amount, 0.f, 0.f, amount), 1.f);
}
