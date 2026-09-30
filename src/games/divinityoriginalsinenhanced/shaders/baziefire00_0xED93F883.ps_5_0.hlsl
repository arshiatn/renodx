//BAZIER'S FIRE SHADER


cbuffer _Globals : register(b0)
{
  float _OpacityFade : packoffset(c0);
}

cbuffer PerFrame : register(b13)
{
  float4x4 global_LightPropertyMatrix : packoffset(c0);
  float4x3 global_FogPropertyMatrix : packoffset(c4);
  float4 global_Data : packoffset(c7);
}

SamplerState _DefaultWrapSampler_s : register(s0);
Texture2D<float4> Texture2DParameter_6cacb6396fe94aef83ac4f17e82f5b83_DefaultWrapSampler_SRGB : register(t0);
Texture2D<float4> Texture2DParameter_e959b8cec9fd47cf85d3ef19b8b76a7c_DefaultWrapSampler_SRGB : register(t1);

// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float w1 : TEXCOORD1,   // packs to v1.z, like stock (asm 24)
  float w2 : TEXCOORD2,   // packs to v1.w, like stock (asm 29)
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = float4(0.330000013,0.330000013,-0.330000013,-0.330000013) * global_Data.xxxx;
  r0.xyzw = v1.xyxy * float4(2.5,2.5,2.5,2.5) + r0.xyzw;
  r0.x = Texture2DParameter_6cacb6396fe94aef83ac4f17e82f5b83_DefaultWrapSampler_SRGB.Sample(_DefaultWrapSampler_s, r0.xy).x;
  r0.y = Texture2DParameter_6cacb6396fe94aef83ac4f17e82f5b83_DefaultWrapSampler_SRGB.Sample(_DefaultWrapSampler_s, r0.zw).z;
  r0.xy = r0.xy * float2(0.150000006,0.150000006) + v1.xy;
  r0.xy = float2(-0.0250000004,-0.0250000004) + r0.xy;
  r0.x = Texture2DParameter_e959b8cec9fd47cf85d3ef19b8b76a7c_DefaultWrapSampler_SRGB.Sample(_DefaultWrapSampler_s, r0.xy).x;
  r0.y = r0.x * 0.5 + 0.5;
  
  r1.x = global_FogPropertyMatrix._m10;
  r1.y = global_FogPropertyMatrix._m11;
  r1.z = global_FogPropertyMatrix._m12;

  r0.yzw = r0.yyy * v2.xyz + -r1.xyz;
  
  r0.yzw = w1.xxx * r0.yzw + r1.xyz;
  
  r1.x = global_FogPropertyMatrix._m00;
  r1.y = global_FogPropertyMatrix._m01;
  r1.z = global_FogPropertyMatrix._m02;
  
  r0.yzw = -r1.xyz + r0.yzw;
  
  r1.xyz = w2.xxx * r0.yzw + r1.xyz;
  
  r0.y = 1 + -v2.w;
  r0.x = saturate(r0.x + -r0.y);
  r1.w = _OpacityFade * r0.x;
  
  o0.xyzw = max(float4(0,0,0,0), r1.xyzw);

  if (TONEMAP_MODE == TONEMAP_PRAGMAP) {
    // A=(2,1.5), B=(5,0.85), D=(8,0.7), D=(12.5,0.65) with  f(x)=TrendPoly({A,B,C,D},3)
    float c3 = -0.0025749559083f;
    float c2 = 0.0664021164021f;
    float c1 = -0.5810582010582f;
    float c0 = 2.4171075837743f;
    float p = HDR_PEAK;
    float x = ((c3 * p + c2) * p + c1) * p + c0;
    x = clamp(x, 0.5f, 3.0f);
    o0.rgb *= p * x * SI.firehighlightmultiplier;
  } else if (TONEMAP_MODE == TONEMAP_PSYCHO && SI.brazier_boost_enabled == 1.f) {
    // Original custom color/brightness boost from the older archive.
    o0.r *= 6.0f;
    o0.g *= 6.0f / 4.f;
    o0.b *= 6.0f / 5.f;
  } else if (TONEMAP_MODE == TONEMAP_PSYCHO && SI.brazier_boost_enabled == 2.f) {
    // Original custom color/brightness boost from the older archive.
    o0.r *= 6.0f;
    o0.g *= 6.0f / 2.0f;
    o0.b *= 6.0f / 2.5f;
  }

  return;
}
