// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

sampler2D uniAuto_OARTexture0;
float2 uniHDRParam;

struct PS_IN
{
	float2 texcoord : TEXCOORD;
	half4 color : COLOR;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	half4 o;

	half4 r0;
	r0 = tex2D(uniAuto_OARTexture0, i.texcoord.xy);
	o.w = r0.w * i.color.w;
	r0.xyz = saturate(r0.xyz + -0.05);
	r0.xyz = r0.xyz * i.color.xyz;
	o.xyz = r0.xyz * uniHDRParam.yyy;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
