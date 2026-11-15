// Native UI math (census decompile); brightness is applied only at the final UI draw.
#include "./ui.hlsli"

sampler2D uniInterfaceMap;

struct PS_IN
{
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	half4 o;

	float4 r0;
	r0 = tex2D(uniInterfaceMap, i.texcoord.xy);
	o.w = r0.w * i.color.w;
	o.xyz = i.color.xyz;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
