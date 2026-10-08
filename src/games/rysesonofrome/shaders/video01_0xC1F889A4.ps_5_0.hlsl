// Ryse: Son of Rome video with alpha (Scaleform YUVA, BT.601 limited). From 0xC1F889A4.ps_5_0.cso
// (3Dmigoto). Mod: shown at Paper White instead of UI brightness; HDR: Video Highlights.
#include "../shared.h"

cbuffer PER_BATCH : register(b0)
{
  row_major float2x4 cBitmapColorTransform : packoffset(c0);
  float cPremultiplyAlpha : packoffset(c2);
}

SamplerState texMapY_s : register(s0);
SamplerState texMapU_s : register(s1);
SamplerState texMapV_s : register(s2);
SamplerState texMapA_s : register(s3);
Texture2D<float4> texMapY : register(t0);
Texture2D<float4> texMapU : register(t1);
Texture2D<float4> texMapV : register(t2);
Texture2D<float4> texMapA : register(t3);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  r0.w = texMapA.Sample(texMapA_s, v1.xy).w;
  r1.x = texMapV.Sample(texMapV_s, v1.xy).w;
  r1.x = -0.501960814 + r1.x;
  r1.xy = float2(1.59599996,0.813000023) * r1.xx;
  r1.z = texMapU.Sample(texMapU_s, v1.xy).w;
  r1.z = -0.501960814 + r1.z;
  r1.y = r1.z * -0.39199999 + -r1.y;
  r1.z = 2.01699996 * r1.z;
  r1.w = texMapY.Sample(texMapY_s, v1.xy).w;
  r1.w = -0.0627451017 + r1.w;
  r0.y = saturate(r1.w * 1.16400003 + r1.y);
  r0.x = saturate(r1.w * 1.16400003 + r1.x);
  r0.z = saturate(r1.w * 1.16400003 + r1.z);
  r0.xyzw = r0.xyzw * cBitmapColorTransform._m00_m01_m02_m03 + cBitmapColorTransform._m10_m11_m12_m13;
  // Video Highlights (HDR): inverse tone mapping (ITM), Video Dithering. Paper White instead of UI.
  if (HDR >= 1.f && SI.video_highlights > 0.f) r0.xyz = RyseExpandVideo(r0.xyz, v0.xy);
  r0.xyz *= RYSE_ENCODED_SCALE;
  r1.xyzw = r0.xyzw * r0.wwww;
  r2.x = cmp(0 != cPremultiplyAlpha);
  o0.xyzw = r2.xxxx ? r1.xyzw : r0.xyzw;
}
