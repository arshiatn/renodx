// Ryse: Son of Rome motion blur velocity pack. From 0x799A5EB8.ps_5_0.cso (3Dmigoto).
// Stock: object velocity (t2, packed) or camera reprojection of the depth (t0), scaled and
// length-limited by MotionBlurParams, packed (signed sqrt) with depth for the blur passes.
// Its own target: the TAA reads the unscaled velocity buffer. HDR: camera motion and object
// motion (object velocity minus the camera's at its depth) x their sliders, before the stock
// length limit.
#include "../shared.h"

cbuffer PER_BATCH : register(b0)
{
  row_major float4x4 mViewProjPrev : packoffset(c0);
  float4 WorldViewPos : packoffset(c4);
  float4 MotionBlurParams : packoffset(c5);
  float4 PS_NearFarClipDist : packoffset(c6);
}

SamplerState _tex0_s : register(s0);
SamplerState _tex2_s : register(s2);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex2 : register(t2);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;

  r0.xy = _tex2.Sample(_tex2_s, v1.xy).xy;
  r0.yz = float2(-0.498039216,-0.498039216) + r0.xy;
  r0.x = cmp(r0.x == 0.000000);
  r1.xy = r0.yz + r0.yz;
  r0.yz = cmp(float2(0,0) < r0.yz);
  r0.yz = r0.yz ? float2(1,1) : float2(-1,-1);
  r1.xy = r1.xy * r1.xy;
  r0.yz = r1.xy * r0.yz;
  r0.w = _tex0.Sample(_tex0_s, v1.xy).x;
  r1.xyz = v2.xyz * r0.www + WorldViewPos.xyz;
  r2.w = PS_NearFarClipDist.y * r0.w;
  r3.xyz = mViewProjPrev._m20_m21_m23 * r1.zzz + mViewProjPrev._m30_m31_m33;
  r1.yzw = mViewProjPrev._m10_m11_m13 * r1.yyy + r3.xyz;
  r1.xyz = mViewProjPrev._m00_m01_m03 * r1.xxx + r1.yzw;
  r1.xy = r1.xy / r1.zz;
  r0.yz = r0.xx ? r1.xy : r0.yz;
  r0.xw = r0.xx ? v1.xy : 0;
  r0.xw = MotionBlurParams.zz * r0.xw;
  r0.xy = r0.yz * MotionBlurParams.zz + -r0.xw;
  // HDR: camera part (static scenery at this depth) and the object's own part, scaled apart.
  if (HDR >= 1.f) {
    float2 camera = (r1.xy - v1.xy) * MotionBlurParams.zz;
    r0.xy = camera * SI.motion_blur_camera + (r0.xy - camera) * SI.motion_blur_object;
  }
  r0.z = dot(r0.xy, r0.xy);
  r0.z = rsqrt(r0.z);
  r0.z = saturate(MotionBlurParams.w * r0.z);
  r0.xy = r0.xy * r0.zz;
  r0.xy = MotionBlurParams.xy * r0.xy;
  r0.z = dot(r0.xy, r0.xy);
  r0.z = sqrt(r0.z);
  r0.z = 32 * r0.z;
  r2.z = sqrt(r0.z);
  o0.zw = float2(1,0.00392156886) * r2.zw;
  r0.zw = cmp(float2(0,0) < r0.xy);
  r0.xy = sqrt(abs(r0.xy));
  r0.zw = r0.zw ? float2(1,1) : float2(-1,-1);
  r0.xy = r0.xy * r0.zw;
  o0.xy = r0.xy * float2(0.5,0.5) + float2(0.498039216,0.498039216);
  return;
}