// Native video / loading-screen math (census decompile); Paper White, Video AutoHDR and its dithering at the
// final draw.
#include "./ui.hlsli"

sampler2D uniAuto_OARTexture0;
sampler2D uniAuto_OARTexture1;
sampler2D uniAuto_OARTexture2;

half4 OriginalVideo(float2 texcoord : TEXCOORD) : COLOR
{
	half4 o;

	float4 r0;
	float4 r1;
	r0 = tex2D(uniAuto_OARTexture2, texcoord.xy);
	r0.x = r0.x + -0.5;
	r0.y = r0.x * 1.596;
	r1 = tex2D(uniAuto_OARTexture0, texcoord.xy);
	r0.z = r1.x + -0.0625;
	o.x = r0.z * 1.164 + r0.y;
	r1 = tex2D(uniAuto_OARTexture1, texcoord.xy);
	r0.y = r1.x + -0.5;
	r0.yw = r0.yy * float2(0.391, 2.018);
	r0.y = r0.z * 1.164 + -r0.y;
	o.z = r0.z * 1.164 + r0.w;
	o.y = r0.x * -0.813 + r0.y;
	o.w = 1;

	return o;
}

float4 main(float2 texcoord : TEXCOORD, float2 vpos : VPOS) : COLOR0 {
  return RuseScaleVideo(OriginalVideo(texcoord), vpos);
}
