// Ryse: Son of Rome depth of field composite. From 0x910E0E0C.ps_5_0.cso (3Dmigoto).
// Stock: far blur (t1, 4 taps, premultiplied by CoC) over the scene (t2) by the depth CoC
// (vDofParamsFocus0/1), then the near-field layer (t0) by its own coverage. Before the tonemap.
// HDR: both blends x Depth of Field slider.
#include "../shared.h"

cbuffer PER_BATCH : register(b0)
{
  float4 vDofParamsFocus1 : packoffset(c0);
  float4 vDofParamsFocus0 : packoffset(c1);
  float4 PS_NearFarClipDist : packoffset(c2);
  float4 PS_ScreenSize : packoffset(c3);
  float4 PS_HPosScale : packoffset(c4);
}

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
SamplerState _tex2_s : register(s2);
SamplerState _tex3_s : register(s3);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);
Texture2D<float4> _tex2 : register(t2);
Texture2D<float4> _tex3 : register(t3);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;

  r0.xy = v1.xy / PS_HPosScale.xy;
  r1.xyzw = PS_ScreenSize.zwzw * float4(-2,-2,2,-2) + r0.xyxy;
  r2.xyzw = _tex1.Sample(_tex1_s, r1.xy).xyzw;
  r3.xyzw = _tex1.Sample(_tex1_s, r1.zw).xyzw;
  r1.xyzw = _tex0.Sample(_tex0_s, r1.xy).xyzw;
  r2.xyzw = r3.xyzw + r2.xyzw;
  r0.zw = PS_ScreenSize.zw * float2(-2,2) + r0.xy;
  r0.xy = PS_ScreenSize.zw * float2(2,2) + r0.xy;
  r3.xyzw = _tex1.Sample(_tex1_s, r0.xy).xyzw;
  r0.xyzw = _tex1.Sample(_tex1_s, r0.zw).xyzw;
  r0.xyzw = r2.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw + r3.xyzw;
  r2.x = cmp(0 < r0.w);
  r0.xyzw = float4(0.25,0.25,0.25,0.25) * r0.xyzw;
  r0.w = r2.x ? r0.w : 1;
  r0.xyz = r0.xyz / r0.www;
  r2.xyz = _tex2.Sample(_tex2_s, v1.xy).xyz;
  r0.xyz = -r2.xyz + r0.xyz;
  r3.x = _tex3.Sample(_tex3_s, v1.xy).x;
  r3.x = PS_NearFarClipDist.y * r3.x;
  r3.x = saturate(r3.x * vDofParamsFocus0.x + vDofParamsFocus0.y);
  r3.x = r3.x * r3.x;
  r3.x = saturate(vDofParamsFocus1.w * r3.x);
  // Depth of Field slider (HDR).
  if (HDR >= 1.f) r3.x = saturate(r3.x * SI.depth_of_field);
  r2.w = 0;
  r0.w = 1;
  r0.xyzw = r3.xxxx * r0.xyzw + r2.xyzw;
  r2.w = 1;
  r3.x = cmp(0 < r1.w);
  r3.x = r3.x ? r1.w : 1;
  r2.xyz = r1.xyz / r3.xxx;
  r1.x = r1.w + r1.w;
  r1.x = sqrt(r1.x);
  r1.x = min(1, r1.x);
  if (HDR >= 1.f) r1.x = saturate(r1.x * SI.depth_of_field);
  r2.xyzw = r2.xyzw + -r0.xyzw;
  o0.xyzw = r1.xxxx * r2.xyzw + r0.xyzw;
  return;
}