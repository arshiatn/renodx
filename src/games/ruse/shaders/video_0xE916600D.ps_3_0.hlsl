// Native video / loading-screen math (census decompile); Paper White, Video AutoHDR and its dithering at the
// final draw.
#include "./ui.hlsli"

sampler2D uniAuto_OARTexture0;
sampler2D uniAuto_OARTexture1;
sampler2D uniAuto_OARTexture2;
sampler2D uniAuto_OARTexture3;
float4 uniLoadingTransparency;

struct PS_IN
{
	float2 texcoord : TEXCOORD;
	float2 texcoord1 : TEXCOORD1;
};

half4 OriginalVideo(PS_IN i) : COLOR
{
	half4 o;

	float4 r0;
	float4 r1;
	float4 r2;
	float4 r3;
	r0.xy = i.texcoord.xy * -0.5 + 0.5;
	r0.xy = -r0.xy + i.texcoord1.xy;
	r1.x = 1 / i.texcoord.x;
	r1.y = 1 / i.texcoord.y;
	r0.zw = r0.xy * -r1.xy + 1;
	r0.xy = r0.xy * r1.xy;
	r0.zw = (r0.zw >= 0) ? 1 : 0;
	r1.xy = (r0.xy >= 0) ? 1 : 0;
	r0.xy = saturate(r0.xy);
	r0.z = dot(r0.zw, r1.xy) + -2;
	r1 = tex2D(uniAuto_OARTexture1, r0.xy);
	r0.w = r1.x + -0.5;
	r1.xy = r0.ww * float2(0.391, 2.018);
	r2 = tex2D(uniAuto_OARTexture0, r0.xy);
	r3 = tex2D(uniAuto_OARTexture2, r0.xy);
	r0.x = r3.x + -0.5;
	r0.y = r2.x + -0.0625;
	r0.w = r0.y * 1.164 + -r1.x;
	r1.z = r0.y * 1.164 + r1.y;
	r1.y = r0.x * -0.813 + r0.w;
	r0.x = r0.x * 1.596;
	r1.x = r0.y * 1.164 + r0.x;
	r0.xyz = (r0.zzz >= 0) ? r1.xyz : 0;
	r1 = tex2D(uniAuto_OARTexture3, i.texcoord1.xy);
	r0.xyz = r0.xyz + -r1.xyz;
	o.xyz = uniLoadingTransparency.xxx * r0.xyz + r1.xyz;
	o.w = 1;

	return o;
}

float4 main(PS_IN i, float2 vpos : VPOS) : COLOR0 {
  return RuseScaleVideo(OriginalVideo(i), vpos);
}
