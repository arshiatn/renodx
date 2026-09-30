// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

sampler2D uniAuto_OARTexture0;
float uniDateEnSecondes : register(c1);
float2 uniHDRParam;

struct PS_IN
{
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
	float2 texcoord1 : TEXCOORD1;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	float t0 = frac(0.0199999996 * uniDateEnSecondes);
	float t1 = 6.28318501 * t0;
	float2 t2 = float2(sin(t1) * (i.texcoord.x - 0.5) + 0.5 + cos(t1) * -(i.texcoord.y - 0.5), cos(t1) * (i.texcoord.x - 0.5) + 0.5 + sin(t1) * (i.texcoord.y - 0.5));
	float t3 = i.texcoord1.y * i.texcoord1.x;
	float2 t4 = ddx(i.texcoord);
	float t5 = dot(t4, i.texcoord - 0.5);
	float t6 = abs(t5) + abs(t5);
	float2 t7 = ddy(i.texcoord);
	float t8 = dot(t7, i.texcoord - 0.5);
	float t9 = abs(t8) + abs(t8);
	float2 t10 = i.texcoord - 0.5;
	float t11 = sqrt(dot(t10, t10) + max(dot(t4, t4) + t6, dot(t7, t7) + t9));
	float t12 = sqrt(dot(t10, t10) + min(dot(t7, t7) - t9, dot(t4, t4) - t6));
	float t13 = t11 + t12;
	float t14 = t11 - t12;
	float t15 = 0.899999976 * t14;
	float2 t16 = 0.5 * t13 + float2(-0.449999988, 0.449999988) * t14;
	float t17 = -0.449999988 * t3 + 0.5;
	float t18 = -0.0500000007 * t3 + 0.5;
	float2 t19 = rcp(-0.324999988 * t3 + 0.5 - float2(t17, t18));
	float2 t20 = saturate(-t19.y * t18 + t16.yx * t19.y);
	return float4(uniHDRParam.yyy, tex2D(uniAuto_OARTexture0, t2).w * i.color.w * max(saturate(rcp(t15 * t19.x) * (saturate(-t19.x * t17 + t16.y * t19.x) - saturate(-t19.x * t17 + t16.x * t19.x))), 0.5 * saturate(rcp(t15 * t19.y) * 0.5 * (t20.x * t20.x - t20.y * t20.y))));
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
