// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

float4 uniScaleFormBackgroundColor : register(c2);
float4 uniScaleFormCxFormAdd : register(c1);
float4 uniScaleFormCxFormMul;

struct PS_IN
{
	float4 color : COLOR;
	float4 color1 : COLOR1;
};

float4 OriginalUI(PS_IN i) : COLOR
{
	float4 t0 = i.color * uniScaleFormCxFormMul + uniScaleFormCxFormAdd;
	return float4(t0.xyz, t0.w * i.color1.w) * uniScaleFormBackgroundColor.w;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
