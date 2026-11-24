// Native UI math (census decompile); brightness is applied only at the final UI draw.
#include "./ui.hlsli"

sampler2D uniAuto_OARTexture0;

struct PS_IN
{
	float2 texcoord : TEXCOORD;
	float4 color : COLOR;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	half4 o;

	float4 r0;
	r0 = tex2D(uniAuto_OARTexture0, i.texcoord.xy);
	o = r0 * i.color;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
