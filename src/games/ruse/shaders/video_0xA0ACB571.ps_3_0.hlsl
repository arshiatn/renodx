// Native video / loading-screen math (census decompile); Paper White, Video AutoHDR and its dithering at the
// final draw.
#include "./ui.hlsli"

float4 uniAutoTexelDUDVAndDimensions_uniAuto_OARTexture1 : register(c1);
sampler2D uniAuto_OARTexture0;
sampler2D uniAuto_OARTexture1;
sampler2D uniAuto_OARTexture2;
float2 uniHDRParam;
float4 uniLoadingTransparency : register(c2);

half4 OriginalVideo(float2 texcoord : TEXCOORD) : COLOR
{
	half4 o;

	float4 r0;
	float4 r1;
	float4 r2;
	float3 r3;
	r0.x = 1 / uniAutoTexelDUDVAndDimensions_uniAuto_OARTexture1.z;
	r0.y = 1 / uniAutoTexelDUDVAndDimensions_uniAuto_OARTexture1.w;
	r0.z = ddx(texcoord.x);
	r1.x = 1 / r0.z;
	r0.z = ddy(texcoord.y);
	r1.y = 1 / r0.z;
	r0.z = r0.z * 1080;
	r1.zw = r1.xy * r0.zz;
	r0.xy = r0.xy * r1.zw;
	r2.x = min(r0.y, r0.x);
	r0.x = min(r2.x, 1);
	r0.xy = r0.xx * uniAutoTexelDUDVAndDimensions_uniAuto_OARTexture1.zw;
	r0.zw = r0.zz * r1.xy + -r0.xy;
	r0.zw = r0.zw * float2(0.5, 0.35);
	r0.zw = texcoord.xy * r1.zw + -r0.zw;
	r1.x = 1 / r0.x;
	r1.y = 1 / r0.y;
	r0.xy = r0.zw * r1.xy;
	r1.zw = saturate(r0.xy);
	r2 = tex2D(uniAuto_OARTexture1, r0.xy);
	r0.xy = r0.zw * r1.xy + -r1.zw;
	r0.x = dot(r0.xy, -r0.xy) + 1E-06;
	r0.x = (r0.x >= 0) ? r2.w : 0;
	r1 = tex2D(uniAuto_OARTexture0, texcoord.xy);
	r3 = lerp(r1.xyz, r2.xyz, r0.xxx);
	r0 = tex2D(uniAuto_OARTexture2, texcoord.xy);
	r0.w = uniLoadingTransparency.y * uniLoadingTransparency.x;
	r1.xyz = half3(lerp(r0.xyz, r3.xyz, r0.www));
	o.xyz = r1.xyz * uniHDRParam.yyy;
	o.w = 1;

	return o;
}

float4 main(float2 texcoord : TEXCOORD, float2 vpos : VPOS) : COLOR0 {
  return RuseScaleVideo(OriginalVideo(texcoord), vpos);
}
