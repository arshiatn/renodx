// Ryse: Son of Rome sun shafts radial blur (run twice, toward the sun). From 0xF1FCED4E.ps_5_0.cso (3Dmigoto).
// Stock: 8 taps toward the sun (weights 1 to 0.125) x 0.25, faded with the distance to the sun, into an RGBA8 target
// that clamped each pass to 0-1. The addon upgrades that target to FP16 (back buffer aspect): saturated here, as the
// RGBA8 target did (see shafts00).

cbuffer PER_BATCH : register(b0)
{
  float4 PS_ScreenSize : packoffset(c0);
  float4 PS_HPosScale : packoffset(c1);
  float4 SunShafts_SunPos : packoffset(c2);
}

cbuffer PER_INSTANCE : register(b1)
{
  float4 PI_sunShaftsParams : packoffset(c0);
}

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xyzw = _tex0.Sample(_tex0_s, v1.xy).xyzw;
  r1.x = PI_sunShaftsParams.x * SunShafts_SunPos.w;
  r1.yz = SunShafts_SunPos.xy * PS_HPosScale.xy + -v1.xy;
  r2.xy = r1.yz * r1.xx + v1.xy;
  r1.xw = r1.yz * r1.xx;
  r1.yz = r1.yz / PS_HPosScale.xy;
  r2.xyzw = _tex0.Sample(_tex0_s, r2.xy).xyzw;
  r0.xyzw = r2.xyzw * float4(0.875,0.875,0.875,0.875) + r0.xyzw;
  r2.xy = r1.xw * float2(2,2) + v1.xy;
  r2.xyzw = _tex0.Sample(_tex0_s, r2.xy).xyzw;
  r0.xyzw = r2.xyzw * float4(0.75,0.75,0.75,0.75) + r0.xyzw;
  r2.xyzw = r1.xwxw * float4(3,3,4,4) + v1.xyxy;
  r3.xyzw = _tex0.Sample(_tex0_s, r2.xy).xyzw;
  r2.xyzw = _tex0.Sample(_tex0_s, r2.zw).xyzw;
  r0.xyzw = r3.xyzw * float4(0.625,0.625,0.625,0.625) + r0.xyzw;
  r0.xyzw = r2.xyzw * float4(0.5,0.5,0.5,0.5) + r0.xyzw;
  r2.xyzw = r1.xwxw * float4(5,5,6,6) + v1.xyxy;
  r1.xw = r1.xw * float2(7,7) + v1.xy;
  r3.xyzw = _tex0.Sample(_tex0_s, r1.xw).xyzw;
  r4.xyzw = _tex0.Sample(_tex0_s, r2.xy).xyzw;
  r2.xyzw = _tex0.Sample(_tex0_s, r2.zw).xyzw;
  r0.xyzw = r4.xyzw * float4(0.375,0.375,0.375,0.375) + r0.xyzw;
  r0.xyzw = r2.xyzw * float4(0.25,0.25,0.25,0.25) + r0.xyzw;
  r0.xyzw = r3.xyzw * float4(0.125,0.125,0.125,0.125) + r0.xyzw;
  r1.xw = PS_HPosScale.xy * PS_ScreenSize.yx;
  r1.x = 1.33299994 * r1.x;
  r2.y = r1.x / r1.w;
  r2.x = 1;
  r1.xy = r2.xy * r1.yz;
  r1.x = dot(r1.xy, r1.xy);
  r1.x = sqrt(r1.x);
  r1.x = saturate(PI_sunShaftsParams.y * r1.x);
  r1.x = 1 + -r1.x;
  r1.y = saturate(SunShafts_SunPos.w);
  r1.x = r1.y * r1.x;
  r0.xyz = r1.xxx * r0.xyz;
  r0.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  o0.xyz = saturate(r0.xyz);
  r0.x = saturate(SunShafts_SunPos.w * 0.100000001 + 0.899999976);
  r0.x = r0.w * 0.25 + -r0.x;
  o0.w = saturate(1 + r0.x);
}
