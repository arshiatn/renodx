// Planar video conversion, verified against the supplied CSO and assembly.
// Preserve the game's limited-range YUV coefficients, chroma UVs, tint and fades.
#include "../shared.h"

float4 YDimensions : register(c0);
float4 ChromaDimensions : register(c1);
sampler2D YTexture : register(s0);
sampler2D UTexture : register(s1);
sampler2D VTexture : register(s2);

#include "video.hlsli"

struct PS_IN {
  float2 texcoord : TEXCOORD0;
  float4 color : COLOR0;
};

float4 main(PS_IN i) : COLOR0 {
  half4 native_color = RuseDecodeVideo(i.texcoord.xy) * i.color;
  return RuseScaleVideo(native_color);
}
