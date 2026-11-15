// Native UI math (census decompile); brightness is applied only at the final UI draw.
#include "./ui.hlsli"

sampler2D uniInterfaceMap;
float2 uniHDRParam;

struct PS_IN
{
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
	float2 texcoord1 : TEXCOORD1;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	half4 o;

	float4 r0;
	float4 r1;
	r0.x = i.texcoord.x;
	r0.x = r0.x + -i.texcoord1.x;
	r0.y = 1 / i.texcoord1.y;
	r0.x = saturate(r0.y * abs(r0.x));
	r0.x = -r0.x + 1;
	r0.y = -r0.x + 1;
	r0.z = r0.x * r0.x;
	r0.xy = r0.xy * r0.zz;
	r0.x = r0.y * 3 + r0.x;
	r0 = r0.x * float4(0.5, 0.5, 0.5, 0);
	r1 = tex2D(uniInterfaceMap, i.texcoord.xy);
	r0 = half4(r1 * i.color + r0);
	o.xyz = r0.xyz * uniHDRParam.yyy;
	o.w = r0.w;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
