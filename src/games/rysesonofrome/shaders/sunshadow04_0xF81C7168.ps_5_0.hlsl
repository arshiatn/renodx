// Ryse: Son of Rome sun shadow mask, far cascade (8 samples). From 0xF81C7168.ps_5_0.cso (3Dmigoto, the dump of
// 2026-10-03). CryEngine draws each cascade into the shadow mask (1 = shadow) with a rotated irregular kernel;
// Sun Shadow Softness (SI.sun_shadow_softness) swaps it for contact-hardening PCSS (sun_shadow_pcss.hlsli).
#include "../shared.h"
#include "../sun_shadow_pcss.hlsli"

cbuffer PER_BATCH : register(b0)
{
  float4 vBasisMagnitudes : packoffset(c0);
  float4 vWBasisY : packoffset(c1);
  float4 vAdaption : packoffset(c2);
  float4 vWBasisX : packoffset(c3);
  float4 BlendInfo : packoffset(c4);
  float4 irreg_kernel_2d[8] : packoffset(c5);
  float4 PS_NearFarClipDist : packoffset(c13);
  row_major float4x4 TexGen2 : packoffset(c14);
  float4 fDepthShift : packoffset(c18);
  float4 fShadowFadingDist : packoffset(c19);
  float4 vCamPos : packoffset(c20);
  float4 fOneDivFarDist : packoffset(c21);
  float4 vWBasisZ : packoffset(c22);
  float4 fDepthTestBias : packoffset(c23);
  float4 vInvShadowMapSize : packoffset(c24);
  float4 fKernelRadius : packoffset(c25);
}

SamplerState sceneDepthSampler_s : register(s0);
SamplerState sRotSampler_s : register(s2);
SamplerComparisonState SAMPLER_STATE_depthMapSampler_s : register(s1);
Texture2D<float4> sceneDepthSampler : register(t0);
Texture2D<float4> sRotSampler : register(t1);
Texture2D<float4> depthMapSampler0 : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = sceneDepthSampler.Sample(sceneDepthSampler_s, v2.xy).x;
  r0.yzw = vBasisMagnitudes.xyz * r0.xxx;
  r0.yz = v0.xy * r0.yz;
  r1.xyzw = vWBasisX.xyzw * r0.yyyy;
  r1.xyzw = vWBasisZ.xyzw * r0.wwww + r1.xyzw;
  r1.xyzw = vWBasisY.xyzw * r0.zzzz + r1.xyzw;
  r1.xyzw = vCamPos.xyzw + r1.xyzw;
  r0.y = -fDepthTestBias.x + r1.z;
  r0.zw = r1.xy / r1.ww;
  r0.y = max(0, r0.y);
  r0.y = min(0.999998987, r0.y);
  // Sun Shadow Softness: the lit share from PCSS in place of the game's rotated 12-tap filter (r0.zw the map's uv,
  // r0.y the receiver's depth).
  float pcss_lit = -1.f;
  if (SI.sun_shadow_softness > 0.f) {
    float2 screen_size;
    sceneDepthSampler.GetDimensions(screen_size.x, screen_size.y);
    pcss_lit = RyseSunShadowPcss(depthMapSampler0, SAMPLER_STATE_depthMapSampler_s, float3(r0.zw, r0.y), vWBasisX.xyz,
                                 vWBasisY.xyz, vWBasisZ.xyz, vBasisMagnitudes.xyz, screen_size, v0.xy);
  }
  r1.xy = v1.zw * r0.xx + v1.xy;
  r1.z = 15 * fKernelRadius.y;
  r1.xy = r1.xy * r1.zz;
  r1.xy = sRotSampler.Sample(sRotSampler_s, r1.xy).xy;
  r2.xyz = fKernelRadius.yxx * vInvShadowMapSize.xxx;
  r1.xyz = r2.xyz * r1.yxx;
  r1.w = -r1.x;
  r2.xyzw = irreg_kernel_2d[0].yyzz * r1.xzzw;
  r2.xyzw = r1.zwxz * irreg_kernel_2d[0].xxww + r2.xyzw;
  r2.xyzw = r2.xyzw + r0.zwzw;
  r3.x = depthMapSampler0.SampleCmpLevelZero(SAMPLER_STATE_depthMapSampler_s, r2.xy, r0.y).x;
  r3.y = depthMapSampler0.SampleCmpLevelZero(SAMPLER_STATE_depthMapSampler_s, r2.zw, r0.y).x;
  r2.xyzw = irreg_kernel_2d[1].yyzz * r1.xzzw;
  r2.xyzw = r1.zwxz * irreg_kernel_2d[1].xxww + r2.xyzw;
  r2.xyzw = r2.xyzw + r0.zwzw;
  r3.z = depthMapSampler0.SampleCmpLevelZero(SAMPLER_STATE_depthMapSampler_s, r2.xy, r0.y).x;
  r3.w = depthMapSampler0.SampleCmpLevelZero(SAMPLER_STATE_depthMapSampler_s, r2.zw, r0.y).x;
  r2.x = dot(r3.xyzw, float4(0.125,0.125,0.125,0.125));
  r3.xyzw = irreg_kernel_2d[2].yyzz * r1.xzzw;
  r3.xyzw = r1.zwxz * irreg_kernel_2d[2].xxww + r3.xyzw;
  r3.xyzw = r3.xyzw + r0.zwzw;
  r4.x = depthMapSampler0.SampleCmpLevelZero(SAMPLER_STATE_depthMapSampler_s, r3.xy, r0.y).x;
  r4.y = depthMapSampler0.SampleCmpLevelZero(SAMPLER_STATE_depthMapSampler_s, r3.zw, r0.y).x;
  r3.xyzw = irreg_kernel_2d[3].yyzz * r1.xyzw;
  r1.xyzw = r1.zwxz * irreg_kernel_2d[3].xxww + r3.xyzw;
  r1.xyzw = r1.xyzw + r0.zwzw;
  r0.zw = float2(-0.5,-0.5) + r0.zw;
  r0.zw = abs(r0.zw) + abs(r0.zw);
  r0.z = max(r0.z, r0.w);
  r0.z = r0.z * r0.z;
  r0.z = r0.z * r0.z;
  r0.z = r0.z * r0.z;
  r0.z = saturate(-fShadowFadingDist.x * r0.z + 1);
  r4.z = depthMapSampler0.SampleCmpLevelZero(SAMPLER_STATE_depthMapSampler_s, r1.xy, r0.y).x;
  r4.w = depthMapSampler0.SampleCmpLevelZero(SAMPLER_STATE_depthMapSampler_s, r1.zw, r0.y).x;
  r0.y = dot(r4.xyzw, float4(0.125,0.125,0.125,0.125));
  r0.y = r2.x + r0.y;
  if (pcss_lit >= 0.f) r0.y = pcss_lit;
  r0.y = -r0.y * r0.y + 1;
  r0.y = r0.y * r0.z;
  r1.xy = v0.xy;
  r1.z = 1;
  r0.xzw = r1.xyz * r0.xxx;
  r1.xyzw = TexGen2._m10_m11_m12_m13 * r0.zzzz;
  r1.xyzw = r0.xxxx * TexGen2._m00_m01_m02_m03 + r1.xyzw;
  r1.xyzw = r0.wwww * TexGen2._m20_m21_m22_m23 + r1.xyzw;
  r1.xyzw = TexGen2._m30_m31_m32_m33 + r1.xyzw;
  r0.xz = r1.xy / r1.ww;
  r0.w = r1.z * fOneDivFarDist.x + -0.975000024;
  r0.w = 40.0000381 * r0.w;
  r0.xz = r0.xz * float2(2,2) + float2(-1,-1);
  r0.xz = min(float2(1,1), abs(r0.xz));
  r0.xz = -BlendInfo.zz + r0.xz;
  r0.xz = BlendInfo.ww * r0.xz;
  r0.x = max(r0.x, r0.z);
  r0.x = saturate(max(r0.x, r0.w));
  r0.x = r0.y * r0.x;
  r0.z = cmp(TexGen2._m33 != 0.000000);
  o0.xyzw = r0.zzzz ? r0.xxxx : r0.yyyy;
  return;
}