// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

float2 uniHDRParam;

struct PS_IN
{
	float4 color : COLOR;
	float4 color1 : COLOR1;
	float2 texcoord1 : TEXCOORD1;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	float t0 = rcp(-2 * i.texcoord1.y + 1);
	float t1 = abs(ddy(i.texcoord1.x)) + abs(ddx(i.texcoord1.x));
	float2 t2 = float2(-0.5, 0.5) * t1 + i.texcoord1.x;
	float t3 = saturate(rcp(t1 * t0) * (saturate(-t0 * i.texcoord1.y + t2.y * t0) - saturate(-t0 * i.texcoord1.y + t2.x * t0))) * i.color1.w;
	float t4 = saturate(rcp(0.105263159 * t1) * (saturate(0.105263159 * t2.y - 0.0526315793) - saturate(0.105263159 * t2.x - 0.0526315793)));
	float4 t5 = float4(lerp(i.color.xyz, i.color1.xyz, t4), t4 * -i.color.w + i.color.w);
	float4 t6 = lerp(t5, i.color1, t3);
	return float4((half3)t6.xyz * uniHDRParam.yyy, (half)t6.w);
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
