// Native UI math (census decompile); brightness is applied only at the final UI draw.
#include "./ui.hlsli"

float4 uniScaleFormBackgroundColor : register(c2);
float4 uniScaleFormCxFormAdd : register(c1);
float4 uniScaleFormCxFormMul;
sampler2D uniScaleFormTexture0;
sampler2D uniScaleFormTexture1;

struct PS_IN
{
	float4 color1 : COLOR1;
	float2 texcoord : TEXCOORD;
	float2 texcoord1 : TEXCOORD1;
};

float4 OriginalUI(PS_IN i) : COLOR
{
	float4 o;

	float4 r0;
	float4 r1;
	float4 r2;
	r0 = tex2D(uniScaleFormTexture0, i.texcoord.xy);
	r1 = tex2D(uniScaleFormTexture1, i.texcoord1.xy);
	r2 = lerp(r1, r0, i.color1.z);
	r0 = uniScaleFormCxFormMul;
	r0 = r2 * r0 + uniScaleFormCxFormAdd;
	r0.w = r0.w * i.color1.w;
	o = r0 * uniScaleFormBackgroundColor.w;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
