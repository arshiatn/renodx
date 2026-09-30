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
	float4 t0 = tex2D(uniInterfaceMap, i.texcoord);
	return float4((half)(t0.x - 0.5 >= 0 ? (half)(2 * (1 - t0.x) * -(1 - i.color.x) + 1) : (half)dot(i.color.xx, t0.x)) * uniHDRParam.y, (half)(t0.y - 0.5 >= 0 ? (half)(2 * (1 - t0.y) * -(1 - i.color.y) + 1) : (half)dot(i.color.yy, t0.y)) * uniHDRParam.y, (half)(t0.z - 0.5 >= 0 ? (half)(2 * (1 - t0.z) * -(1 - i.color.z) + 1) : (half)dot(i.color.zz, t0.z)) * uniHDRParam.y, t0.w * i.color.w);
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
