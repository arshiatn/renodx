// Bloom.hlsl PS_ApplyThresholdAndBloomDownsample (the bloom source: threshold, cap, downsample, dither)
Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

cbuffer cb0 : register(b0)
{
  float4 cb0[2];
}




// 3Dmigoto declarations
#define cmp -
#include "../bloom.hlsli"


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = cb0[1].xyxy * float4(-0.5,-0.5,0.5,0.5) + v1.xyxy;
  r1.xyz = t0.Sample(s1_s, r0.xy).xyz;
  r0.xyz = t0.Sample(s1_s, r0.zw).xyz;
  r0.w = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.w = cb0[0].x / r0.w;
  r0.w = cmp(cb0[0].x < r0.w);
  r1.w = 1 + -r1.w;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = r0.www ? r1.xyz : 0;
  r2.xyzw = cb0[1].xyxy * float4(-0.5,0.5,0.5,-0.5) + v1.xyxy;
  r3.xyz = t0.Sample(s1_s, r2.xy).xyz;
  r2.xyz = t0.Sample(s1_s, r2.zw).xyz;
  r0.w = dot(r3.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.w = cb0[0].x / r0.w;
  r0.w = cmp(cb0[0].x < r0.w);
  r1.w = 1 + -r1.w;
  r3.xyz = r3.xyz * r1.www + r1.xyz;
  r1.xyz = r0.www ? r3.xyz : r1.xyz;
  r0.w = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.w = cb0[0].x / r0.w;
  r0.w = cmp(cb0[0].x < r0.w);
  r1.w = 1 + -r1.w;
  r2.xyz = r2.xyz * r1.www + r1.xyz;
  r1.xyz = r0.www ? r2.xyz : r1.xyz;
  r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.w = cb0[0].x / r0.w;
  r0.w = cmp(cb0[0].x < r0.w);
  r1.w = 1 + -r1.w;
  r0.xyz = r0.xyz * r1.www + r1.xyz;
  r0.xyz = r0.www ? r0.xyz : r1.xyz;
  r0.xyz = cb0[0].yyy * r0.xyz;
  r0.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  // Stock: the brightest channel capped at cb0[0].z (bloom.hlsli: a shoulder in HDR).
  r0.xyz = CapBloomChannels(r0.xyz, cb0[0].z);
  r1.xyz = (int3)r0.xyz & int3(0x7f800000,0x7f800000,0x7f800000);
  r1.xyz = (int3)r1.xyz + int3(0xfd000000,0xfd000000,0xfd800000);
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r1.w = -r1.y;
  r2.xy = (uint2)v0.xy;
  r2.xy = (int2)r2.xy & int2(63,63);
  r2.zw = float2(0,0);
  r0.w = t1.Load(r2.xyz).x;
  r1.y = r0.w * 2 + -1;
  r0.w = cmp(r0.w < 0.5);
  r2.xy = r0.ww ? float2(1,-1) : float2(-1,1);
  r0.w = 1 + -abs(r1.y);
  r0.w = sqrt(r0.w);
  r0.w = r2.x * r0.w + r2.y;
  o0.xyz = r1.xwz * r0.www + r0.xyz;
  o0.w = 0;
  return;
}