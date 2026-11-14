// ssao2.hlsl ps50_blur_y (ssao_blur.hlsli).
#include "./ssao_blur.hlsli"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float2 o0 : SV_Target0)
{
  o0 = GameAoBlur(v1, float2(0.f, 1.f));
}
