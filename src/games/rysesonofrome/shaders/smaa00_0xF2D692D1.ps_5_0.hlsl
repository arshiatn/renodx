// Ryse: Son of Rome SMAA neighborhood blending. From 0xF2D692D1.ps_5_0.cso (3Dmigoto).
// Stock: lerp of squared colors (gamma 2 -> linear), sqrt. Here signed (x * |x|, signed sqrt) so
// wide-gamut negatives stay negative; identical for positive values. Writes the FP16 clone.
#include "../shared.h"

cbuffer PER_BATCH : register(b0)
{
  float4 PS_ScreenSize : packoffset(c0);
}

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  r0.xz = _tex0.Sample(_tex0_s, v1.xy).xz;
  r1.xyzw = PS_ScreenSize.zwzw * float4(0,2,2,0) + v1.xyxy;
  r0.y = _tex0.Sample(_tex0_s, r1.xy).y;
  r0.w = _tex0.Sample(_tex0_s, r1.zw).w;
  r1.x = dot(r0.xyzw, float4(1,1,1,1));
  r1.x = cmp(r1.x < 9.99999975e-006);
  if (r1.x != 0) {
    o0.xyzw = _tex1.SampleLevel(_tex1_s, v1.xy, 0).xyzw;
    return;
  }
  r1.xy = PS_ScreenSize.zw + PS_ScreenSize.zw;
  r1.z = cmp(r0.z < r0.w);
  r2.x = r1.z ? r0.w : -r0.z;
  r0.z = cmp(r0.x < r0.y);
  r2.w = r0.z ? r0.y : -r0.x;
  r0.x = cmp(abs(r2.w) < abs(r2.x));
  r2.yz = float2(0,0);
  r0.xy = r0.xx ? r2.xy : r2.zw;
  r2.xyzw = _tex1.SampleLevel(_tex1_s, v1.xy, 0).xyzw;
  r0.zw = cmp(float2(0,0) < r0.xy);
  r1.zw = cmp(r0.xy < float2(0,0));
  r0.zw = (int2)-r0.zw + (int2)r1.zw;
  r0.zw = (int2)r0.zw;
  r0.zw = r0.zw * r1.xy + v1.xy;
  r1.xyzw = _tex1.SampleLevel(_tex1_s, r0.zw, 0).xyzw;
  r2.xyzw = r2.xyzw * abs(r2.xyzw);
  r0.x = max(abs(r0.y), abs(r0.x));
  r1.xyzw = r1.xyzw * abs(r1.xyzw) + -r2.xyzw;
  r0.xyzw = r0.xxxx * r1.xyzw + r2.xyzw;
  o0.xyzw = sign(r0.xyzw) * sqrt(abs(r0.xyzw));
}
