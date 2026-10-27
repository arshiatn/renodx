// ---- Created with 3Dmigoto v1.3.16 (RenoDX pack dump, 2026-10-26)

// skydome.hlsl ps30_gbuffer / ps30_gbuffer_skygen (sm_3_0): the sky into the G-buffer (colour in target 1). Sky
// Debanding in twattila/sky.hlsli.

#include "../shared.h"
#include "../../twattila/sky.hlsli"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3,
  out uint o4 : SV_Target4)
{
  o0.xyzw = float4(0,0,0,0);
  o1.xyz = saturate(SkySample(v1.xyz - camera_position.xyz, v0.xy).xyz);
  o1.w = 0;
  o2.xyzw = float4(0,0,0,0);
  o3.xyzw = float4(0,0,0,0);
}
