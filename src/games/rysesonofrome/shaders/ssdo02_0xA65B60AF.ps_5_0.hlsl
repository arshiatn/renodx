// Ryse: Son of Rome SSDO without the terrain's large-scale occlusion (no LSDO_Params / _texB; PER_BATCH 16 bytes
// tighter from c8). From 0xA65B60AF.ps_5_0.cso (3Dmigoto, the DevKit dump of 2026-10-14). The same 8 depth samples
// as ssdo00 (0xD1106E8B) give the direction toward what covers the pixel and its amount: out 0.5 + 0.175 x the
// direction (world), 0.35 x the amount. This frame's AO method (SI.ao_method) on that term as ssdo00's: GT-VBAO (1)
// from the visibility bitmask, the ray-traced AO (2) the amount 0 (no LSDO here: nothing left of it).
#include "../shared.h"
#include "../ssdo_bitmask.hlsli"

cbuffer PER_BATCH : register(b0)
{
  float4 g_WorldViewPos : packoffset(c0);
  row_major float3x4 SSDO_CameraMatrix : packoffset(c1);
  row_major float3x4 SSDO_CameraMatrixInv : packoffset(c4);
  float4 LSDO_TexToWorldScale : packoffset(c7);
  float4 PS_NearFarClipDist : packoffset(c8);
  float4 LSDO_TexToWorldTranslation : packoffset(c9);
  float4 PS_ScreenSize : packoffset(c10);
  float4 vInvShadowMapSize : packoffset(c11);
  float4 SSDOParams : packoffset(c12);
  float4 ViewSpaceParams : packoffset(c13);
}

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = PS_ScreenSize.xy * v1.xy;
  r0.xy = (int2)r0.xy;
  r0.xy = (int2)r0.xy & int2(1,1);
  r1.xyz = float3(0.5,0.150000006,0.150000006) * SSDOParams.wxy;
  r0.x = r0.x ? r1.x : SSDOParams.w;
  r0.z = 0.5 * r0.x;
  r0.x = r0.y ? r0.z : r0.x;
  r0.y = _tex1.Sample(_tex1_s, v1.xy).x;
  r0.zw = r1.yz / r0.yy;
  r0.zw = max(SSDOParams.zz, r0.zw);
  r0.zw = min(r0.zw, r0.xx);
  r1.x = r0.z * r0.y;
  r1.x = ViewSpaceParams.x * r1.x;
  r1.x = PS_NearFarClipDist.y * r1.x;
  r1.x = r1.x * r1.x;
  r2.z = 1;
  r1.zw = frac(v1.zw);
  r1.z = dot(r1.zw, float2(1,0.25));
  r1.yz = float2(0.3926875,6.28310013) * r1.xz;
  sincos(r1.z, r3.x, r4.x);
  r5.x = -r3.x;
  r5.y = r4.x;
  r5.z = r3.x;
  r3.x = dot(float2(-0.0199999996,-0.140000001), r5.xy);
  r3.y = dot(float2(-0.0199999996,-0.140000001), r5.yz);
  r1.zw = r3.xy * r0.zw + v1.xy;
  r3.x = _tex1.SampleLevel(_tex1_s, r1.zw, 0).x;
  r2.xy = r1.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r4.x = dot(float2(0.239999995,-0.0399999991), r5.xy);
  r4.y = dot(float2(0.239999995,-0.0399999991), r5.yz);
  r1.zw = r4.xy * r0.zw + v1.xy;
  r3.y = _tex1.SampleLevel(_tex1_s, r1.zw, 0).x;
  r4.xy = r1.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r6.x = dot(float2(0.0799999982,0.360000014), r5.xy);
  r6.y = dot(float2(0.0799999982,0.360000014), r5.yz);
  r1.zw = r6.xy * r0.zw + v1.xy;
  r3.z = _tex1.SampleLevel(_tex1_s, r1.zw, 0).x;
  r6.xy = r1.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r7.x = dot(float2(-0.400000006,0.25999999), r5.xy);
  r7.y = dot(float2(-0.400000006,0.25999999), r5.yz);
  r0.zw = r7.xy * r0.zw + v1.xy;
  r3.w = _tex1.SampleLevel(_tex1_s, r0.zw, 0).x;
  r7.xy = r0.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r3.xyzw = PS_NearFarClipDist.yyyy * r3.xyzw;
  r8.xy = v1.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r8.z = 1;
  r8.xyz = r8.xyz * r0.yyy;
  r8.xyz = PS_NearFarClipDist.yyy * r8.xyz;
  r2.xyz = r2.xyz * r3.xxx + -r8.xyz;
  r9.x = dot(r2.xyz, r2.xyz);
  r4.z = 1;
  r4.xyz = r4.xyz * r3.yyy + -r8.xyz;
  r9.y = dot(r4.xyz, r4.xyz);
  r6.z = 1;
  r3.xyz = r6.xyz * r3.zzz + -r8.xyz;
  r9.z = dot(r3.xyz, r3.xyz);
  r7.z = 1;
  r6.xyz = r7.xyz * r3.www + -r8.xyz;
  r9.w = dot(r6.xyz, r6.xyz);
  r7.xyzw = rsqrt(r9.xyzw);
  r9.xyzw = r1.xxxx * float4(0.3926875,0.3926875,0.3926875,0.3926875) + r9.xyzw;
  r1.xzw = r7.xxx * r2.xyz;
  r2.xyz = _tex0.Sample(_tex0_s, v1.xy).xyz;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  r2.xyz = r2.xyz * r0.zzz;
  r10.x = dot(SSDO_CameraMatrix._m00_m01_m02, r2.xyz);
  r10.y = dot(SSDO_CameraMatrix._m10_m11_m12, r2.xyz);
  r10.z = dot(SSDO_CameraMatrix._m20_m21_m22, r2.xyz);
  r0.z = dot(r10.xyz, r10.xyz);
  r0.z = rsqrt(r0.z);
  r2.xyz = r10.xyz * r0.zzz;
  r10.x = saturate(dot(r2.xyz, r1.xzw));
  r4.xyz = r7.yyy * r4.xyz;
  r10.y = saturate(dot(r2.xyz, r4.xyz));
  r3.xyz = r7.zzz * r3.xyz;
  r6.xyz = r7.www * r6.xyz;
  r10.z = saturate(dot(r2.xyz, r3.xyz));
  r10.w = saturate(dot(r2.xyz, r6.xyz));
  r7.xyzw = r10.xyzw * r1.yyyy;
  r7.xyzw = r7.xyzw / r9.xyzw;
  r4.xyz = r7.yyy * r4.xyz;
  r1.xyz = r7.xxx * r1.xzw + r4.xyz;
  r1.xyz = r7.zzz * r3.xyz + r1.xyz;
  r1.xyz = r7.www * r6.xyz + r1.xyz;
  r0.z = dot(r7.xyzw, float4(1,1,1,1));
  r3.xy = SSDOParams.xy / r0.yy;
  r3.xy = max(SSDOParams.zz, r3.xy);
  r0.xw = min(r3.xy, r0.xx);
  r3.x = dot(float2(-0.340000004,-0.439999998), r5.xy);
  r3.y = dot(float2(-0.340000004,-0.439999998), r5.yz);
  r3.xy = r3.xy * r0.xw + v1.xy;
  r4.x = _tex1.SampleLevel(_tex1_s, r3.xy, 0).x;
  r3.xy = r3.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r6.x = dot(float2(0.400000006,-0.519999981), r5.xy);
  r6.y = dot(float2(0.400000006,-0.519999981), r5.yz);
  r6.xy = r6.xy * r0.xw + v1.xy;
  r4.y = _tex1.SampleLevel(_tex1_s, r6.xy, 0).x;
  r6.xy = r6.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r7.x = dot(float2(0.680000007,0.300000012), r5.xy);
  r9.x = dot(float2(-0.319999993,0.839999974), r5.xy);
  r7.y = dot(float2(0.680000007,0.300000012), r5.yz);
  r9.y = dot(float2(-0.319999993,0.839999974), r5.yz);
  r5.xy = r9.xy * r0.xw + v1.xy;
  r5.zw = r7.xy * r0.xw + v1.xy;
  r0.x = r0.x * r0.y;
  r0.x = ViewSpaceParams.x * r0.x;
  r0.x = PS_NearFarClipDist.y * r0.x;
  r0.x = r0.x * r0.x;
  r4.z = _tex1.SampleLevel(_tex1_s, r5.zw, 0).x;
  r7.xy = r5.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r4.w = _tex1.SampleLevel(_tex1_s, r5.xy, 0).x;
  r5.xy = r5.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r4.xyzw = PS_NearFarClipDist.yyyy * r4.xyzw;
  r3.z = 1;
  r3.xyz = r3.xyz * r4.xxx + -r8.xyz;
  r9.x = dot(r3.xyz, r3.xyz);
  r6.z = 1;
  r6.xyz = r6.xyz * r4.yyy + -r8.xyz;
  r9.y = dot(r6.xyz, r6.xyz);
  r7.z = 1;
  r4.xyz = r7.xyz * r4.zzz + -r8.xyz;
  r9.z = dot(r4.xyz, r4.xyz);
  r5.z = 1;
  r5.xyz = r5.xyz * r4.www + -r8.xyz;
  r9.w = dot(r5.xyz, r5.xyz);
  r7.xyzw = r0.xxxx * float4(1.96343756,1.96343756,1.96343756,1.96343756) + r9.xyzw;
  r8.xyzw = rsqrt(r9.xyzw);
  r0.x = 1.96343756 * r0.x;
  r3.xyz = r8.xxx * r3.xyz;
  r9.x = saturate(dot(r2.xyz, r3.xyz));
  r6.xyz = r8.yyy * r6.xyz;
  r9.y = saturate(dot(r2.xyz, r6.xyz));
  r4.xyz = r8.zzz * r4.xyz;
  r5.xyz = r8.www * r5.xyz;
  r9.z = saturate(dot(r2.xyz, r4.xyz));
  r9.w = saturate(dot(r2.xyz, r5.xyz));
  r2.xyzw = r9.xyzw * r0.xxxx;
  r2.xyzw = r2.xyzw / r7.xyzw;
  r0.xyw = r2.yyy * r6.xyz;
  r0.xyw = r2.xxx * r3.xyz + r0.xyw;
  r0.xyw = r2.zzz * r4.xyz + r0.xyw;
  r0.xyw = r2.www * r5.xyz + r0.xyw;
  r1.w = dot(r2.xyzw, float4(1,1,1,1));
  r0.z = r1.w + r0.z;
  r1.w = r0.z;
  r0.xyz = r1.xyz + r0.xyw;
  r1.x = dot(SSDO_CameraMatrixInv._m00_m01_m02, r0.xyz);
  r1.y = dot(SSDO_CameraMatrixInv._m10_m11_m12, r0.xyz);
  r1.z = dot(SSDO_CameraMatrixInv._m20_m21_m22, r0.xyz);
  // This frame's AO method on the SSDO term (r1: the direction in world space, the amount).
  r1 = RyseSsdoTerm(r1, v1.xy, (uint2)v0.xy, _tex0, _tex0_s, (float3x3)SSDO_CameraMatrix, (float3x3)SSDO_CameraMatrixInv, _tex1,
                    _tex1_s, PS_NearFarClipDist.y, ViewSpaceParams);
  o0.w = 0.349999994 * r1.w;
  o0.xyz = r1.xyz * float3(0.174999997,0.174999997,0.174999997) + float3(0.5,0.5,0.5);
  return;
}
