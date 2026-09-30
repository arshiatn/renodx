// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

sampler2D uniInterfaceMap;
float2 uniHDRParam;

struct PS_IN
{
	float4 color : COLOR;
	float4 color1 : COLOR1;
	float2 texcoord : TEXCOORD;
	float2 texcoord1 : TEXCOORD1;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	half4 o;

	float4 r0;
	float4 r1;
	half4 r2;
	r0.xy = -0.5 + i.texcoord.xy;
	r0.z = ddy(r0.y);
	r0.z = r0.z * i.texcoord1.x;
	r0.w = ddx(r0.x);
	r1.x = r0.w * -i.texcoord1.y;
	r0.z = abs(r0.z) + abs(r1.x);
	r1.xy = float2(-1, 1) * i.texcoord1.yx;
	r1.x = dot(r1.xy, r0.xy) + 0;
	r1.xy = r0.zz * float2(-0.5, 0.5) + r1.xx;
	r0.z = r0.z * 0.01;
	r0.z = 1 / r0.z;
	r1.xy = saturate(r1.xy * 0.01 + 1);
	r1.x = -r1.x + r1.y;
	r0.z = saturate(r0.z * r1.x);
	r1.xy = r0.ww * float2(-0.5, 0.5) + r0.xx;
	r0.w = r0.w * 0.01;
	r0.w = 1 / r0.w;
	r1.xy = saturate(r1.xy * 0.01);
	r1.x = -r1.x + r1.y;
	r0.w = saturate(r0.w * r1.x);
	r1.x = min(r0.z, r0.w);
	r1.y = max(r0.w, r0.z);
	r0.z = dot(i.texcoord1.xy, r0.xy) + 0;
	r0.w = (-r0.y >= 0) ? -0 : -1;
	r0.x = dot(r0.xy, r0.xy) + -1;
	r0.y = (r0.z >= 0) ? 0 : r0.w;
	r0.z = (r0.y >= 0) ? r1.x : 0;
	r0.y = (r0.y >= 0) ? r1.y : 1;
	r0.y = (i.texcoord1.x >= 0) ? r0.z : r0.y;
	r1 = i.color;
	r1 = -r1 + i.color1;
	r1 = half4(r0.y * r1 + i.color);
	r2 = r1.xyzx * float4(1, 1, 1, 0);
	r0 = half4((r0.x >= 0) ? r2 : r1);
	r1 = half4(tex2D(uniInterfaceMap, i.texcoord.xy));
	r0 = half4(r0 * r1);
	o.xyz = r0.xyz * uniHDRParam.yyy;
	o.w = r0.w;

	return o;
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
