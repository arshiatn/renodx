// Native UI math (census decompile); brightness is applied only at the final UI draw.
#include "./ui.hlsli"

float4 uniAutoTexelDUDVAndDimensions_uniAuto_OARTexture0 : register(c1);
sampler2D uniAuto_OARTexture0;
float2 uniHDRParam;

struct PS_IN
{
	float2 texcoord : TEXCOORD;
	half4 color : COLOR;
	half4 texcoord2 : TEXCOORD2;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	half4 o;

	float4 r0;
	float4 r1;
	float4 r2;
	float4 r3;
	r0.xy = ddx(i.texcoord.xy);
	r0.zw = ddy(i.texcoord.xy);
	r1 = uniAutoTexelDUDVAndDimensions_uniAuto_OARTexture0.zwzw * i.texcoord2.zwzw;
	r2 = r0 * r1;
	r2 = r2 * r2;
	r1.xy = r2.yw + r2.xz;
	r2.x = max(r1.x, r1.y);
	r1.x = rsqrt(r2.x);
	r1.x = 1 / r1.x;
	r2.x = max(1, r1.x);
	r3.xz = 1 / r1.zz;
	r3.yw = 1 / r1.ww;
	r1 = r2.x * -r3 + 1;
	r0 = r0 * r1;
	r0 = r0 * i.texcoord2.zwzw;
	r1.zw = frac(i.texcoord.xy);
	r1.zw = r1.zw + -0.5;
	r1.xy = r1.zw * r1.xy + 0.5;
	r1.xy = r1.xy * i.texcoord2.zw + i.texcoord2.xy;
	r0 = half4(tex2Dgrad(uniAuto_OARTexture0, r1.xy, r0.xy, r0.zw));
	o.w = r0.w * i.color.w;
	r0.xyz = half3(saturate(r0.xyz + -0.05));
	r0.xyz = half3(r0.xyz * i.color.xyz);
	o.xyz = r0.xyz * uniHDRParam.yyy;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
