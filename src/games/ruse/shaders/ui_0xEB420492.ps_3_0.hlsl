// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

float4 uniScaleFormBackgroundColor : register(c2);
float4 uniScaleFormCxFormAdd : register(c1);
float4 uniScaleFormCxFormMul;
sampler2D uniScaleFormTexture0;

struct PS_IN
{
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
};

float4 OriginalUI(PS_IN i) : COLOR
{
	float4 o;

	float4 r0;
	float4 r1;
	r0 = tex2D(uniScaleFormTexture0, i.texcoord.xy);
	r0.w = r0.w * i.color.w;
	r0.xyz = i.color.xyz;
	r1 = uniScaleFormCxFormMul;
	r0 = r0 * r1 + uniScaleFormCxFormAdd;
	o = r0 * uniScaleFormBackgroundColor.w;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
