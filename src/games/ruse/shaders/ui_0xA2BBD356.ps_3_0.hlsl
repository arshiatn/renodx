// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

sampler2D uniInterfaceMap;
float2 uniHDRParam;

struct PS_IN
{
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	float4 t0 = tex2D(uniInterfaceMap, i.texcoord) * i.color;
	return float4((half3)t0.xyz * uniHDRParam.yyy, (half)t0.w);
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
