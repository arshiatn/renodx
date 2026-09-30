// ---- Created with 3Dmigoto v1.3.16 on Thu Sep 24 00:42:33 2026

#include "./tonemappers.hlsli"

cbuffer _Globals : register(b0)
{
  float4 Params : packoffset(c0);
  row_major float4x4 ColorMatrix : packoffset(c1);
  float3 SourceColor : packoffset(c5);
  float3 TargetColor : packoffset(c6);
}

Texture2D<float4> Base : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.xyz = Base.Load(r0.xyz).xyz;
  float3 scene_linear = r0.xyz;
  [branch]
  if (HDR == 1.f) {
    o0 = divinity_tonemap::Apply(scene_linear, Params, ColorMatrix, SourceColor, TargetColor,
                               TONEMAP_MODE == TONEMAP_PRAGMAP);
  } else {
    r1.xyzw = float4(0.100000001,0.00999999978,0.300000012,11.1999998) * Params.yzzx;
    r2.xyz = Params.xxx * r0.xyz + r1.xxx;
    r2.xyz = r0.xyz * r2.xyz + r1.yyy;
    r3.xyz = Params.xxx * r0.xyz + Params.yyy;
    r0.xyz = r0.xyz * r3.xyz + r1.zzz;
    r0.xyz = r2.xyz / r0.xyz;
    r0.xyz = float3(-0.0333333351,-0.0333333351,-0.0333333351) + r0.xyz;
    r0.w = r1.w + r1.x;
    r0.w = r0.w * 11.1999998 + r1.y;
    r1.x = Params.x * 11.1999998 + Params.y;
    r1.x = r1.x * 11.1999998 + r1.z;
    r0.w = r0.w / r1.x;
    r0.w = -0.0333333351 + r0.w;
    r0.xyzw = max(float4(0,0,0,0), r0.xyzw);
    r0.xyz = r0.xyz / r0.www;
    r0.xyz = log2(r0.xyz);
    r0.xyz = float3(0.454545468,0.454545468,0.454545468) * r0.xyz;
    r0.xyz = exp2(r0.xyz);
    r0.xyz = min(float3(1,1,1), r0.xyz);
    r0.w = 1;
    r1.x = dot(ColorMatrix._m00_m01_m02_m03, r0.xyzw);
    r1.y = dot(ColorMatrix._m10_m11_m12_m13, r0.xyzw);
    r1.z = dot(ColorMatrix._m20_m21_m22_m23, r0.xyzw);
    r0.w = dot(ColorMatrix._m30_m31_m32_m33, r0.xyzw);
    r2.xyz = TargetColor.xyz + r1.xyz;
    r2.xyz = max(float3(-1,-1,-1), r2.xyz);
    r2.xyz = min(float3(1,1,1), r2.xyz);
    r2.xyz = r2.xyz + -r1.xyz;
    r3.xyz = -SourceColor.xyz + r1.xyz;
    r1.w = dot(r3.xyz, r3.xyz);
    r1.w = sqrt(r1.w);
    r1.w = 1 + -r1.w;
    r1.w = max(0, r1.w);
    r1.w = r1.w * r1.w;
    r0.xyz = r2.xyz * r1.www + r1.xyz;
    r1.xyzw = float4(0,0,0,1) + -r0.xyzw;
    r2.x = saturate(Params.w);
    o0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;

    // Restore the original UNORM target's clamp in the upgraded target.
    o0 = saturate(o0);
  }

  // Gamma-encoded transport with independent scene and UI white levels.
  o0.rgb *= pow(HDR_INTSCALING, 1.f / 2.2f);

  return;
}
