// Planar cutscene video with the native background/fade blend, from CSO/ASM.
#include "../shared.h"

float4 BackgroundColor : register(c0);
float4 YDimensions : register(c1);
float4 ChromaDimensions : register(c2);
sampler2D YTexture : register(s0);
sampler2D UTexture : register(s1);
sampler2D VTexture : register(s2);

#include "video.hlsli"

float4 main(float2 texcoord : TEXCOORD0) : COLOR0 {
  float4 video = RuseDecodeVideo(texcoord);
  half4 native_color = BackgroundColor.w * (video - BackgroundColor) + BackgroundColor;
  return RuseScaleVideo(native_color);
}
