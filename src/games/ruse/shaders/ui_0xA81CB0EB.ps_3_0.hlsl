// Native UI math (census decompile); brightness is applied only at the final UI draw.
#include "./ui.hlsli"

sampler2D uniInterfaceMap;
float2 uniHDRParam;

struct PS_IN
{
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	half4 o;

	float4 r0;
	float2 r1;
	float2 r2;
	r0 = tex2D(uniInterfaceMap, i.texcoord.xy);
	r1.x = -r0.z + r0.y;
	r1 = (r1.xx >= 0) ? r0.zy : r0.yz;
	r2.x = min(r1.x, r0.x);
	r2.y = max(r0.x, r1.y);
	r0.xyz = lerp(r2.xxx, r2.yyy, 0.5);
	r0 = half4(r0 * i.color);
	o.xyz = r0.xyz * uniHDRParam.yyy;
	o.w = r0.w;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
