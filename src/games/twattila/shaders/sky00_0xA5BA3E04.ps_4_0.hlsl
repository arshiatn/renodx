// ---- Created with 3Dmigoto v1.3.16 (RenoDX pack dump, 2026-10-25)

// skydome.hlsl ps30_main (sm_3_0): the sky, forward. Sky Debanding in sky.hlsli.

#include "../sky.hlsli"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyz = SkySample(v1.xyz - camera_position.xyz, v0.xy).xyz;
  o0.w = 1;
}
