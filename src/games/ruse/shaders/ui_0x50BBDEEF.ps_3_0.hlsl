// Native UI math from the supplied dump; brightness is applied only at the final UI draw.
#include "./ui.hlsli"

float2 uniAutoTexelDUDV_uniInterfaceMap : register(c1);
float uniAuto_BorderCutValue : register(c2);
float uniAuto_BorderSmoothing : register(c3);
sampler2D uniInterfaceMap;
float2 uniHDRParam;

struct PS_IN
{
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
	float2 texcoord1 : TEXCOORD1;
	float2 texcoord2 : TEXCOORD2;
};

half4 OriginalUI(PS_IN i) : COLOR
{
	float t0 = rcp(10 - uniAuto_BorderCutValue);
	float4 t1 = float4(0.5, 0.5, -0.5, -0.5) * ddx(i.texcoord.xyxy) + i.texcoord.xyxy;
	float4 t2 = float4(0.5, 0.5, -0.5, -0.5) * ddy(i.texcoord.xyxy) + i.texcoord.xyxy;
	float t3 = -t0 * uniAuto_BorderCutValue;
	float3 t4 = tex2D(uniInterfaceMap, t2.zw).xyz;
	float3 t5 = tex2D(uniInterfaceMap, t2.xy).xyz;
	float3 t6 = tex2D(uniInterfaceMap, t1.zw).xyz;
	float3 t7 = tex2D(uniInterfaceMap, t1.xy).xyz;
	float2 t8 = -0.5 * uniAutoTexelDUDV_uniInterfaceMap + i.texcoord2;
	float4 t9 = t8.xyxy - t1;
	float4 t10 = t8.xyxy - t2;
	float2 t11 = 0.5 * uniAutoTexelDUDV_uniInterfaceMap + i.texcoord1;
	float4 t12 = t1 - t11.xyxy;
	float3 t13 = t12.x >= 0 ? t12.y >= 0 ? t9.x >= 0 ? t9.y >= 0 ? t7 : 0 : 0 : 0 : 0;
	float3 t14 = t12.z >= 0 ? t12.w >= 0 ? t9.z >= 0 ? t9.w >= 0 ? t6 : 0 : 0 : 0 : 0;
	float4 t15 = t2 - t11.xyxy;
	float3 t16 = t15.x >= 0 ? t15.y >= 0 ? t10.x >= 0 ? t10.y >= 0 ? t5 : 0 : 0 : 0 : 0;
	float3 t17 = t15.z >= 0 ? t15.w >= 0 ? t10.z >= 0 ? t10.w >= 0 ? t4 : 0 : 0 : 0 : 0;
	float3 t18 = min(min(t17, t16), min(t14, t13));
	float3 t19 = max(max(t13, t14), max(t16, t17));
	float t20 = 1.29999995 * max(max(max(t19.x - t18.x, 0.00100000005), max(t19.z - t18.z, 0.00100000005)), max(t19.y - t18.y, 0.00100000005)) + uniAuto_BorderSmoothing;
	float t21 = clamp(0.5 * (t18.x + t19.x), 0.5 * (t18.z + t19.z), 0.5 * (t18.y + t19.y));
	float t22 = saturate(rcp(t20 * t0) * (saturate(t3 + (0.5 * t20 + t21) * t0) - saturate(t3 + (-0.5 * t20 + t21) * t0)));
	return float4((half3)lerp(i.color.xyz, i.color.xyz, t22) * uniHDRParam.yyy, (half)lerp(0, i.color.w, t22));
}

float4 main(PS_IN i) : COLOR0 {
  return RuseScaleUI(OriginalUI(i));
}
