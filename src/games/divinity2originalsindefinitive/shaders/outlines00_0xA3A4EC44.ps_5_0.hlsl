// ---- Created with 3Dmigoto v1.3.16 on Sun Sep 06 12:19:18 2026

cbuffer _Globals : register(b0)
{
  float4 VPtoRTScaleBias : packoffset(c0);
  float4 SightProperties : packoffset(c1);
  float2 WorldPositionOffset : packoffset(c2);
  float2 AiGridWorldPositionOffset : packoffset(c2.z);
  float3 SightSneakTextureInfo : packoffset(c3);
  float3 HeightTextureInfo : packoffset(c4);
  float2 Weights : packoffset(c5);
}

cbuffer PerView : register(b12)
{
  row_major float4x4 global_View : packoffset(c0);
  row_major float4x4 global_Projection : packoffset(c4);
  row_major float4x4 global_ViewProjection : packoffset(c8);
  float4 global_ViewPos : packoffset(c12);
  float4 global_ViewInfo : packoffset(c13);
  float4 global_ScaleAndBias : packoffset(c14);
}

SamplerState LinearClampSampler_s : register(s0);
SamplerState PointWrapSampler_s : register(s1);
Texture2D<float4> Base : register(t0);
Texture2D<float4> LinearDepth : register(t1);
Texture2D<float4> Base4 : register(t2);
Texture2D<float4> Base5 : register(t3);
Texture2D<float4> Base7 : register(t4);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  float3 v2 : TexCoord1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;


  // added this 
  const float tactical_ui_scale = DOS2_CORRECT_HDR ? (SI.graphics_white_nits / 300.f) : 1.f;


  r0.xyz = Base.Sample(LinearClampSampler_s, v1.xy).xyz;
  r0.w = LinearDepth.Sample(PointWrapSampler_s, v1.xy).x;
  r1.x = cmp(r0.w < 1);
  if (r1.x != 0) {
    r1.xyz = r0.www * v2.xyz + global_ViewPos.xyz;
    // DXBC reads the complete cb0[2] register. RDEF names its xy and zw
    // halves as two float2 values, which the original decompiler conflated.
    r2.xyzw = r1.xzxz
               - float4(WorldPositionOffset.xy, AiGridWorldPositionOffset.xy);
    r2.xy = r2.xy / SightSneakTextureInfo.zz;
    r2.xy = r2.xy / SightSneakTextureInfo.xy;
    r2.zw = r2.zw / HeightTextureInfo.zz;
    r2.zw = r2.zw / HeightTextureInfo.xy;
    r2.zw = Base7.SampleLevel(LinearClampSampler_s, r2.zw, 0).xy;
    r0.w = r2.z + -r1.y;
    r0.w = min(1, abs(r0.w));
    r0.w = -0.200000003 + r0.w;
    r0.w = saturate(r0.w + r0.w);
    r1.y = r0.w * -2 + 3;
    r0.w = r0.w * r0.w;
    r0.w = -r1.y * r0.w + 1;
    r3.xyz = log2(r0.xyz);
    r3.xyz = float3(0.0126833133,0.0126833133,0.0126833133) * r3.xyz;
    r3.xyz = exp2(r3.xyz);
    r4.xyz = float3(-0.8359375,-0.8359375,-0.8359375) + r3.xyz;
    r4.xyz = max(float3(0,0,0), r4.xyz);
    r3.xyz = -r3.xyz * float3(18.6875,18.6875,18.6875) + float3(18.8515625,18.8515625,18.8515625);
    r3.xyz = r4.xyz / r3.xyz;
    r3.xyz = log2(r3.xyz);
    r3.xyz = float3(6.27739477,6.27739477,6.27739477) * r3.xyz;
    r3.xyz = exp2(r3.xyz);
    r4.xyz = float3(10000,10000,10000) * r3.xyz;
    r1.yw = cmp(float2(0,0) < Weights.xy);
    if (r1.y != 0) {
      r5.xyz = Base4.SampleLevel(LinearClampSampler_s, r2.xy, 0).xyw;
      r5.xy = ceil(r5.yx);
      r1.y = r5.x + -r5.y;
      r2.z = dot(r4.xyz, float3(0.262706608,0.677999616,0.0592937991));
      r2.z = log2(r2.z);
      r2.z = 0.699999988 * r2.z;
      r2.z = exp2(r2.z);
      r3.w = 1 + -SightProperties.w;
      r5.xyw = -r3.xyz * float3(10000,10000,10000) + r2.zzz;
      r5.xyw = r3.www * r5.xyw + r4.xyz;
      r6.xyzw = float4(1.5,1.5,1.5,1.5) * r1.xzxz;
      r6.xyzw = frac(r6.xyzw);
      r1.xz = cmp(float2(0.5,0.5) >= r6.zw);
      r1.xz = r1.xz ? float2(1,1) : 0;
      r6.xyzw = cmp(r6.xyzw >= float4(0.25,0.25,0.75,0.75));
      r6.xy = r6.xy ? float2(1,1) : 0;
      r1.xz = r6.xy * r1.xz;
      r1.x = r1.x + r1.z;
      r6.xy = r6.zw ? float2(-1,-1) : float2(-0,-0);
      r1.x = r6.x + r1.x;
      r1.x = saturate(r1.x + r6.y);
      r6.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * SightProperties.xyz;
      r7.xyz = float3(0.0549999997,0.0549999997,0.0549999997) + SightProperties.xyz;
      r7.xyz = float3(0.947867334,0.947867334,0.947867334) * r7.xyz;
      r7.xyz = log2(r7.xyz);
      r7.xyz = float3(2.4000001,2.4000001,2.4000001) * r7.xyz;
      r7.xyz = exp2(r7.xyz);
      r8.xyz = cmp(float3(0.0404499993,0.0404499993,0.0404499993) >= SightProperties.xyz);
      r6.xyz = r8.xyz ? r6.xyz : r7.xyz;
      r7.x = dot(float3(0.627399981,0.329299986,0.0432999991), r6.xyz);
      r7.y = dot(float3(0.0691,0.919499993,0.0114000002), r6.xyz);
      r7.z = dot(float3(0.0164000001,0.0879999995,0.895600021), r6.xyz);
      r1.z = SightProperties.w * 0.225000009 + 0.075000003;

      // 240-nit blue fill
      r6.xyz = r7.xyz * (240.f * tactical_ui_scale) + -r4.xyz;
      // r6.xyz = r7.xyz * float3(240,240,240) + -r4.xyz;
      r6.xyz = r1.zzz * r6.xyz + r4.xyz;

      // Fixed green target
      r8.xyz = -r3.xyz * float3(10000.f, 10000.f, 10000.f) + float3(85.0906219f, 169.43338f, 25.4893398f) * tactical_ui_scale;
      // r8.xyz = -r3.xyz * float3(10000,10000,10000) + float3(85.0906219,169.43338,25.4893398);


      r8.xyz = r8.xyz * float3(0.400000006,0.400000006,0.400000006) + r4.xyz;
      r1.x = r1.y * r1.x;
      r8.xyz = r8.xyz + -r6.xyz;
      r1.xyz = r1.xxx * r8.xyz + r6.xyz;
      r1.xyz = -r3.xyz * float3(10000,10000,10000) + r1.xyz;
      r1.xyz = r0.www * r1.xyz + r4.xyz;
      r1.xyz = log2(r1.xyz);
      r1.xyz = float3(1.04999995,1.04999995,1.04999995) * r1.xyz;
      r1.xyz = exp2(r1.xyz);
      r1.xyz = r3.xyz * float3(10,10,10) + r1.xyz;
      r2.z = cmp(r5.z >= 0.430000007);
      r2.z = r2.z ? 1.000000 : 0;
      r3.w = cmp(0.569999993 >= r5.z);
      r3.w = r3.w ? 1.000000 : 0;
      r2.z = r3.w * r2.z;
      r3.w = saturate(2.32558131 * r5.z);
      r4.w = r3.w * -2 + 3;
      r3.w = r3.w * r3.w;
      r3.w = r4.w * r3.w;
      r4.w = -0.670000017 + r5.z;
      r4.w = saturate(-9.99999809 * r4.w);
      r6.x = r4.w * -2 + 3;
      r4.w = r4.w * r4.w;
      r4.w = r6.x * r4.w;
      r3.w = saturate(r3.w * r4.w + -r2.z);
      r1.xyz = r1.xyz + -r5.xyw;
      r1.xyz = r5.zzz * r1.xyz + r5.xyw;
      r3.w = r3.w * r0.w;
      r3.w = 0.5 * r3.w;
      r1.xyz = r3.www * -r1.xyz + r1.xyz;
      r2.z = r2.z * r0.w;
      r2.z = 1.5 * r2.z;

      // 300-nit bright contour
      r5.xyz = r7.xyz * (300.f * tactical_ui_scale) + -r1.xyz;
      // r5.xyz = r7.xyz * float3(300,300,300) + -r1.xyz;


      r1.xyz = r2.zzz * r5.xyz + r1.xyz;
      r1.xyz = -r3.xyz * float3(10000,10000,10000) + r1.xyz;
      r4.xyz = Weights.xxx * r1.xyz + r4.xyz;
    }
    if (r1.w != 0) {
      r1.x = Base5.SampleLevel(LinearClampSampler_s, r2.xy, 0).x;
      r1.y = r1.x + r1.x;
      r1.y = saturate(r1.y);
      r1.z = r1.y * r2.w;

      // Fixed red/orange targets — both occurrences
      r2.xyz = float3(91.8181915f, 12.2531338f, 4.74269295f) * tactical_ui_scale + -r4.xyz;
      // r2.xyz = float3(91.8181915,12.2531338,4.74269295) + -r4.xyz;


      r3.xyz = r2.xyz * float3(0.400000006,0.400000006,0.400000006) + r4.xyz;
      r2.xyz = r2.xyz * float3(0.150000006,0.150000006,0.150000006) + r4.xyz;
      r1.x = saturate(r1.x * 2 + -1);
      r1.x = -0.400000006 + r1.x;
      r1.x = saturate(4.99999952 * r1.x);
      r1.w = r1.x * -2 + 3;
      r1.x = r1.x * r1.x;
      r1.x = r1.w * r1.x;
      r3.xyz = r3.xyz + -r2.xyz;
      r2.xyz = r1.xxx * r3.xyz + r2.xyz;
      r2.xyz = r2.xyz + -r4.xyz;
      r2.xyz = r2.xyz * r0.www;
      r1.x = cmp(r1.z >= 0.430000007);
      r1.w = cmp(0.569999993 >= r1.z);
      r1.xw = r1.xw ? float2(1,1) : 0;
      r1.x = r1.x * r1.w;
      r1.w = saturate(2.32558131 * r1.z);
      r3.x = r1.w * -2 + 3;
      r1.w = r1.w * r1.w;
      r1.w = r3.x * r1.w;
      r1.y = r1.y * r2.w + -0.670000017;
      r1.y = saturate(-9.99999809 * r1.y);
      r2.w = r1.y * -2 + 3;
      r1.y = r1.y * r1.y;
      r1.y = r2.w * r1.y;
      r1.y = saturate(r1.w * r1.y + -r1.x);
      r2.xyz = r1.zzz * r2.xyz + r4.xyz;
      r1.y = r1.y * r0.w;
      r1.y = 0.5 * r1.y;
      r1.yzw = r1.yyy * -r2.xyz + r2.xyz;
      r0.w = r1.x * r0.w;
      r0.w = 1.5 * r0.w;

      // Later occurrence:
      r2.xyz = float3(91.8181915f, 12.2531338f, 4.74269295f) * tactical_ui_scale + -r1.yzw;
      // r2.xyz = float3(91.8181915,12.2531338,4.74269295) + -r1.yzw;


      r1.xyz = r0.www * r2.xyz + r1.yzw;
      r1.xyz = r1.xyz + -r4.xyz;
      r4.xyz = Weights.yyy * r1.xyz + r4.xyz;
    }
    r4.xyz = DOS2_CORRECT_HDR ? max(0.f, r4.xyz) : r4.xyz;
    r1.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) * r4.xyz;
    r1.xyz = log2(r1.xyz);
    r1.xyz = float3(0.159301758,0.159301758,0.159301758) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r2.xyz = r1.xyz * float3(18.8515625,18.8515625,18.8515625) + float3(0.8359375,0.8359375,0.8359375);
    r1.xyz = r1.xyz * float3(18.6875,18.6875,18.6875) + float3(1,1,1);
    r1.xyz = r2.xyz / r1.xyz;
    r1.xyz = log2(r1.xyz);
    r1.xyz = float3(78.84375,78.84375,78.84375) * r1.xyz;
    r0.xyz = exp2(r1.xyz);
  }
  o0.xyz = r0.xyz;
  o0.w = 1;
  return;
}
