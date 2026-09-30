// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

float2 uniHDRParam;

half4 OriginalUI(float4 color : COLOR) : COLOR
{
	half4 o;

	o.xyz = uniHDRParam.yyy * color.xyz;
	o.w = color.w;

	return o;
}

float4 main(float4 color : COLOR) : COLOR0 {
  return RuseScaleUI(OriginalUI(color));
}
