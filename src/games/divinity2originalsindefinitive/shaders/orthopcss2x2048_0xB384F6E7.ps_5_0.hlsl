// ---- Created with 3Dmigoto v1.3.16 on Sun Nov 15 18:38:20 2026
// Orthographic/CascadedPCSS_2x2048: the sun's shadow filter at the PCSS shadow setting.
// tan(LightProperties.x), the sun's size, sets each cascade's blocker search and penumbra; Sun Shadow Softness
// scales it (1 = the game's).
#include "../shared.h"


cbuffer _Globals : register(b0)
{
  float4 VPtoRTScaleBias : packoffset(c0);
  float4 LightProperties : packoffset(c1);
  float4 ShadowFarPlanes : packoffset(c2);
  float4 ShadowFrustumWidths : packoffset(c3);
  row_major float4x4 ViewMatrix[5] : packoffset(c4);
  row_major float4x4 ProjectionMatrix[4] : packoffset(c24);
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

SamplerState PointSampler_s : register(s0);
SamplerComparisonState ShadowSampler_s : register(s1);
Texture2D<float4> LinearDepth : register(t0);
Texture2D<float4> Shadow : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float3 v1 : TexCoord0,
  float3 v2 : TexCoord1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.z = LinearDepth.Load(r0.xyz).x;
  r1.xy = -VPtoRTScaleBias.zw * global_ViewInfo.zw + v0.xy;
  r1.z = global_ViewInfo.w + -r1.y;
  r1.xy = r1.xz / global_ViewInfo.zw;
  r0.xy = r1.xy * float2(2,2) + float2(-1,-1);
  r0.xyz = v1.xyz * r0.xyz;
  r1.xyz = global_View._m10_m11_m12 * r0.yyy;
  r0.xyw = r0.xxx * global_View._m00_m01_m02 + r1.xyz;
  r0.xyz = r0.zzz * global_View._m20_m21_m22 + r0.xyw;
  r0.xyz = global_ViewPos.xyz + r0.xyz;
  r0.w = 1;
  r1.x = dot(ProjectionMatrix[0]._m00_m01_m02_m03, r0.xyzw);
  r1.y = dot(ProjectionMatrix[0]._m10_m11_m12_m13, r0.xyzw);
  r1.z = dot(ProjectionMatrix[0]._m20_m21_m22_m23, r0.xyzw);
  r1.w = dot(ProjectionMatrix[0]._m30_m31_m32_m33, r0.xyzw);
  r1.xyz = r1.xyz / r1.www;
  r1.w = ShadowFrustumWidths.x * LightProperties.z;
  r1.w = 0.5 * r1.w;
  r2.xy = saturate(r1.xy + r1.xy);
  r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
  r2.xy = float2(1,1) + -abs(r2.xy);
  r2.xy = saturate(r2.xy / LightProperties.zz);
  r2.z = saturate(1 + -r1.z);
  r2.z = ShadowFarPlanes.x * r2.z;
  r1.w = saturate(r2.z / r1.w);
  r2.x = r2.x * r2.y;
  r2.y = r2.x * r1.w;
  r2.z = cmp(0 < r2.y);
  if (r2.z != 0) {
    sincos(LightProperties.x, r3.x, r4.x);
    r2.w = r3.x / r4.x * SI.sun_shadow_softness;
    r3.x = LightProperties.y * r2.w;
    r3.y = r3.x / ShadowFrustumWidths.x;
    r3.y = 0.5 * r3.y;
    r3.y = max(0.000244140625, r3.y);
    r3.y = min(0.00244140625, r3.y);
    r3.z = LightProperties.w / ShadowFarPlanes.x;
    r3.z = -r3.z + r1.z;
    r4.xyzw = r3.yyyy * float4(-0.641573071,-0.170358405,-0.220173702,0.0666730404) + r1.xyxy;
    r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
    r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
    r4.y = Shadow.SampleLevel(PointSampler_s, r4.xy, 0).x;
    r3.w = cmp(r4.y < r3.z);
    r4.x = 1;
    r4.xy = r3.ww ? r4.xy : 0;
    r4.z = Shadow.SampleLevel(PointSampler_s, r4.zw, 0).x;
    r4.w = cmp(r4.z < r3.z);
    r5.y = r4.y + r4.z;
    r5.x = r3.w ? 2 : 1;
    r4.xy = r4.ww ? r5.xy : r4.xy;
    r5.xyzw = r3.yyyy * float4(-0.56526792,0.40981409,-0.372753888,-0.69012928) + r1.xyxy;
    r5.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r5.xyzw);
    r5.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r5.xyzw);
    r3.w = Shadow.SampleLevel(PointSampler_s, r5.xy, 0).x;
    r4.z = cmp(r3.w < r3.z);
    r5.y = r4.y + r3.w;
    r5.x = 1 + r4.x;
    r4.xy = r4.zz ? r5.xy : r4.xy;
    r3.w = Shadow.SampleLevel(PointSampler_s, r5.zw, 0).x;
    r4.z = cmp(r3.w < r3.z);
    r5.y = r4.y + r3.w;
    r5.x = 1 + r4.x;
    r4.xy = r4.zz ? r5.xy : r4.xy;
    r5.xyzw = r3.yyyy * float4(-0.963876784,0.0795568228,0.0895406604,-0.433672994) + r1.xyxy;
    r5.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r5.xyzw);
    r5.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r5.xyzw);
    r3.y = Shadow.SampleLevel(PointSampler_s, r5.xy, 0).x;
    r3.w = cmp(r3.y < r3.z);
    r5.y = r4.y + r3.y;
    r5.x = 1 + r4.x;
    r3.yw = r3.ww ? r5.xy : r4.xy;
    r4.x = Shadow.SampleLevel(PointSampler_s, r5.zw, 0).x;
    r4.y = cmp(r4.x < r3.z);
    r5.y = r4.x + r3.w;
    r5.x = 1 + r3.y;
    r3.yw = r4.yy ? r5.xy : r3.yw;
    r4.x = max(1, r3.y);
    r3.w = r3.w / r4.x;
    r3.y = cmp(r3.y != 0.000000);
    if (r3.y != 0) {
      r1.z = -r3.w + r1.z;
      r1.z = ShadowFarPlanes.x * r1.z;
      r1.z = dot(r2.ww, r1.zz);
      r1.z = r1.z / ShadowFrustumWidths.x;
      r1.z = 0.5 * r1.z;
      r1.z = max(0.000244140625, r1.z);
      r1.z = min(0.00244140625, r1.z);
      r4.xyzw = r1.zzzz * float4(-0.641573071,-0.170358405,-0.220173702,0.0666730404) + r1.xyxy;
      r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
      r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
      r3.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r3.z).x;
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r3.z).x;
      r3.y = r3.y + r3.w;
      r4.xyzw = r1.zzzz * float4(-0.56526792,0.40981409,-0.372753888,-0.69012928) + r1.xyxy;
      r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
      r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r3.z).x;
      r3.y = r3.y + r3.w;
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r3.z).x;
      r3.y = r3.y + r3.w;
      r4.xyzw = r1.zzzz * float4(-0.963876784,0.0795568228,0.0895406604,-0.433672994) + r1.xyxy;
      r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
      r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r3.z).x;
      r3.y = r3.y + r3.w;
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r3.z).x;
      r3.y = r3.y + r3.w;
      r4.xyzw = r1.zzzz * float4(-0.815210223,-0.533595324,-0.0262216199,0.725986183) + r1.xyxy;
      r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
      r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r3.z).x;
      r3.y = r3.y + r3.w;
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r3.z).x;
      r3.y = r3.y + r3.w;
      r4.xyzw = r1.zzzz * float4(0.152993307,0.216891706,-0.394465297,0.896840572) + r1.xyxy;
      r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
      r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r3.z).x;
      r3.y = r3.y + r3.w;
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r3.z).x;
      r3.y = r3.y + r3.w;
      r4.xyzw = r1.zzzz * float4(0.566493392,0.677397907,0.65583998,-0.0949900225) + r1.xyxy;
      r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
      r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r3.z).x;
      r3.y = r3.y + r3.w;
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r3.z).x;
      r3.y = r3.y + r3.w;
      r4.xyzw = r1.zzzz * float4(0.0658113286,-0.885984778,0.465619504,-0.59089911) + r1.xyxy;
      r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
      r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r3.z).x;
      r3.y = r3.y + r3.w;
      r3.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r3.z).x;
      r3.y = r3.y + r3.w;
      r4.xyzw = r1.zzzz * float4(0.771781623,0.291234612,0.862963498,-0.501030385) + r1.xyxy;
      r4.xyzw = max(float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313), r4.xyzw);
      r4.xyzw = min(float4(0.49987793,0.49987793,0.49987793,0.49987793), r4.xyzw);
      r1.x = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r3.z).x;
      r1.x = r3.y + r1.x;
      r1.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r3.z).x;
      r1.x = r1.x + r1.y;
      r1.x = 0.0625 * r1.x;
    } else {
      r1.x = 1;
    }
    r1.x = r1.x * r2.y;
    r1.y = cmp(r2.y < 1);
    if (r1.y != 0) {
      r4.x = dot(ProjectionMatrix[1]._m00_m01_m02_m03, r0.xyzw);
      r4.y = dot(ProjectionMatrix[1]._m10_m11_m12_m13, r0.xyzw);
      r4.z = dot(ProjectionMatrix[1]._m20_m21_m22_m23, r0.xyzw);
      r1.y = dot(ProjectionMatrix[1]._m30_m31_m32_m33, r0.xyzw);
      r3.yzw = r4.xyz / r1.yyy;
      r1.y = ShadowFrustumWidths.y * LightProperties.z;
      r1.y = 0.5 * r1.y;
      r4.xy = float2(-0,-0.5) + r3.yz;
      r4.xy = saturate(r4.xy + r4.xy);
      r4.xy = r4.xy * float2(2,2) + float2(-1,-1);
      r4.xy = float2(1,1) + -abs(r4.xy);
      r4.xy = saturate(r4.xy / LightProperties.zz);
      r1.z = saturate(1 + -r3.w);
      r1.z = ShadowFarPlanes.y * r1.z;
      r1.y = saturate(r1.z / r1.y);
      r1.z = r4.x * r4.y;
      r1.y = r1.z * r1.y;
      r1.y = cmp(0 < r1.y);
      if (r1.y != 0) {
        r1.y = -r2.x * r1.w + 1;
        r1.z = ShadowFrustumWidths.y / ShadowFrustumWidths.x;
        r1.w = r3.x / ShadowFrustumWidths.y;
        r1.w = 0.5 * r1.w;
        r2.xy = float2(0.000244140625,0.00244140625) / r1.zz;
        r1.w = max(r2.x, r1.w);
        r1.w = min(r1.w, r2.y);
        r3.x = LightProperties.w / ShadowFarPlanes.y;
        r1.z = -r3.x * r1.z + r3.w;
        r4.xyzw = r1.wwww * float4(-0.641573071,-0.170358405,-0.220173702,0.0666730404) + r3.yzyz;
        r4.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r4.xyzw);
        r4.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r4.xyzw);
        r4.y = Shadow.SampleLevel(PointSampler_s, r4.xy, 0).x;
        r3.x = cmp(r4.y < r1.z);
        r4.x = 1;
        r4.xy = r3.xx ? r4.xy : 0;
        r4.z = Shadow.SampleLevel(PointSampler_s, r4.zw, 0).x;
        r4.w = cmp(r4.z < r1.z);
        r5.y = r4.y + r4.z;
        r5.x = r3.x ? 2 : 1;
        r4.xy = r4.ww ? r5.xy : r4.xy;
        r5.xyzw = r1.wwww * float4(-0.56526792,0.40981409,-0.372753888,-0.69012928) + r3.yzyz;
        r5.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r5.xyzw);
        r5.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r5.xyzw);
        r3.x = Shadow.SampleLevel(PointSampler_s, r5.xy, 0).x;
        r4.z = cmp(r3.x < r1.z);
        r5.y = r4.y + r3.x;
        r5.x = 1 + r4.x;
        r4.xy = r4.zz ? r5.xy : r4.xy;
        r3.x = Shadow.SampleLevel(PointSampler_s, r5.zw, 0).x;
        r4.z = cmp(r3.x < r1.z);
        r5.y = r4.y + r3.x;
        r5.x = 1 + r4.x;
        r4.xy = r4.zz ? r5.xy : r4.xy;
        r5.xyzw = r1.wwww * float4(-0.963876784,0.0795568228,0.0895406604,-0.433672994) + r3.yzyz;
        r5.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r5.xyzw);
        r5.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r5.xyzw);
        r1.w = Shadow.SampleLevel(PointSampler_s, r5.xy, 0).x;
        r3.x = cmp(r1.w < r1.z);
        r5.y = r4.y + r1.w;
        r5.x = 1 + r4.x;
        r4.xy = r3.xx ? r5.xy : r4.xy;
        r1.w = Shadow.SampleLevel(PointSampler_s, r5.zw, 0).x;
        r3.x = cmp(r1.w < r1.z);
        r5.y = r4.y + r1.w;
        r5.x = 1 + r4.x;
        r4.xy = r3.xx ? r5.xy : r4.xy;
        r1.w = max(1, r4.x);
        r1.w = r4.y / r1.w;
        r3.x = cmp(r4.x != 0.000000);
        if (r3.x != 0) {
          r1.w = r3.w + -r1.w;
          r1.w = ShadowFarPlanes.y * r1.w;
          r1.w = dot(r2.ww, r1.ww);
          r1.w = r1.w / ShadowFrustumWidths.y;
          r1.w = 0.5 * r1.w;
          r1.w = max(r1.w, r2.x);
          r1.w = min(r1.w, r2.y);
          r4.xyzw = r1.wwww * float4(-0.641573071,-0.170358405,-0.220173702,0.0666730404) + r3.yzyz;
          r4.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r4.xyzw);
          r4.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r4.xyzw);
          r2.x = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r1.z).x;
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r1.z).x;
          r2.x = r2.x + r2.y;
          r4.xyzw = r1.wwww * float4(-0.56526792,0.40981409,-0.372753888,-0.69012928) + r3.yzyz;
          r4.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r4.xyzw);
          r4.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r4.xyzw);
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r1.z).x;
          r2.x = r2.x + r2.y;
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r1.z).x;
          r2.x = r2.x + r2.y;
          r4.xyzw = r1.wwww * float4(-0.963876784,0.0795568228,0.0895406604,-0.433672994) + r3.yzyz;
          r4.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r4.xyzw);
          r4.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r4.xyzw);
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r1.z).x;
          r2.x = r2.x + r2.y;
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r1.z).x;
          r2.x = r2.x + r2.y;
          r4.xyzw = r1.wwww * float4(-0.815210223,-0.533595324,-0.0262216199,0.725986183) + r3.yzyz;
          r4.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r4.xyzw);
          r4.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r4.xyzw);
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r1.z).x;
          r2.x = r2.x + r2.y;
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r1.z).x;
          r2.x = r2.x + r2.y;
          r4.xyzw = r1.wwww * float4(0.152993307,0.216891706,-0.394465297,0.896840572) + r3.yzyz;
          r4.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r4.xyzw);
          r4.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r4.xyzw);
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r1.z).x;
          r2.x = r2.x + r2.y;
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r1.z).x;
          r2.x = r2.x + r2.y;
          r4.xyzw = r1.wwww * float4(0.566493392,0.677397907,0.65583998,-0.0949900225) + r3.yzyz;
          r4.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r4.xyzw);
          r4.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r4.xyzw);
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.xy, r1.z).x;
          r2.x = r2.x + r2.y;
          r2.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r4.zw, r1.z).x;
          r2.x = r2.x + r2.y;
          r3.xyzw = r1.wwww * float4(0.0658113286,-0.885984778,0.465619504,-0.59089911) + r3.yzyz;
          r3.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r3.xyzw);
          r3.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r3.xyzw);
          r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r3.xy, r1.z).x;
          r1.w = r2.x + r1.w;
          r1.z = Shadow.SampleCmpLevelZero(ShadowSampler_s, r3.zw, r1.z).x;
          r1.z = r1.w + r1.z;
          r1.z = 0.0714285746 * r1.z;
        } else {
          r1.z = 1;
        }
        r1.x = r1.y * r1.z + r1.x;
      }
    }
  } else {
    r1.x = 1;
  }
  if (r2.z == 0) {
    r2.x = dot(ProjectionMatrix[1]._m00_m01_m02_m03, r0.xyzw);
    r2.y = dot(ProjectionMatrix[1]._m10_m11_m12_m13, r0.xyzw);
    r2.z = dot(ProjectionMatrix[1]._m20_m21_m22_m23, r0.xyzw);
    r0.x = dot(ProjectionMatrix[1]._m30_m31_m32_m33, r0.xyzw);
    r0.xyz = r2.xyz / r0.xxx;
    r0.w = ShadowFrustumWidths.y * LightProperties.z;
    r0.w = 0.5 * r0.w;
    r1.yz = float2(-0,-0.5) + r0.xy;
    r1.yz = saturate(r1.yz + r1.yz);
    r1.yz = r1.yz * float2(2,2) + float2(-1,-1);
    r1.yz = float2(1,1) + -abs(r1.yz);
    r1.yz = saturate(r1.yz / LightProperties.zz);
    r1.w = saturate(1 + -r0.z);
    r1.w = ShadowFarPlanes.y * r1.w;
    r0.w = saturate(r1.w / r0.w);
    r1.y = r1.y * r1.z;
    r0.w = r1.y * r0.w;
    r1.y = cmp(0 < r0.w);
    if (r1.y != 0) {
      r1.y = ShadowFrustumWidths.y / ShadowFrustumWidths.x;
      sincos(LightProperties.x, r2.x, r3.x);
      r1.z = r2.x / r3.x * SI.sun_shadow_softness;
      r1.w = LightProperties.y * r1.z;
      r1.w = r1.w / ShadowFrustumWidths.y;
      r1.w = 0.5 * r1.w;
      r2.xy = float2(0.000244140625,0.00244140625) / r1.yy;
      r1.w = max(r2.x, r1.w);
      r1.w = min(r1.w, r2.y);
      r2.z = LightProperties.w / ShadowFarPlanes.y;
      r1.y = -r2.z * r1.y + r0.z;
      r3.xyzw = r1.wwww * float4(-0.641573071,-0.170358405,-0.220173702,0.0666730404) + r0.xyxy;
      r3.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r3.xyzw);
      r3.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r3.xyzw);
      r2.w = Shadow.SampleLevel(PointSampler_s, r3.xy, 0).x;
      r3.x = cmp(r2.w < r1.y);
      r2.z = 1;
      r2.zw = r3.xx ? r2.zw : 0;
      r3.y = Shadow.SampleLevel(PointSampler_s, r3.zw, 0).x;
      r3.z = cmp(r3.y < r1.y);
      r4.y = r3.y + r2.w;
      r4.x = r3.x ? 2 : 1;
      r2.zw = r3.zz ? r4.xy : r2.zw;
      r3.xyzw = r1.wwww * float4(-0.56526792,0.40981409,-0.372753888,-0.69012928) + r0.xyxy;
      r3.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r3.xyzw);
      r3.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r3.xyzw);
      r3.x = Shadow.SampleLevel(PointSampler_s, r3.xy, 0).x;
      r3.y = cmp(r3.x < r1.y);
      r4.y = r3.x + r2.w;
      r4.x = 1 + r2.z;
      r2.zw = r3.yy ? r4.xy : r2.zw;
      r3.x = Shadow.SampleLevel(PointSampler_s, r3.zw, 0).x;
      r3.y = cmp(r3.x < r1.y);
      r4.y = r3.x + r2.w;
      r4.x = 1 + r2.z;
      r2.zw = r3.yy ? r4.xy : r2.zw;
      r3.xyzw = r1.wwww * float4(-0.963876784,0.0795568228,0.0895406604,-0.433672994) + r0.xyxy;
      r3.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r3.xyzw);
      r3.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r3.xyzw);
      r1.w = Shadow.SampleLevel(PointSampler_s, r3.xy, 0).x;
      r3.x = cmp(r1.w < r1.y);
      r4.y = r2.w + r1.w;
      r4.x = 1 + r2.z;
      r2.zw = r3.xx ? r4.xy : r2.zw;
      r1.w = Shadow.SampleLevel(PointSampler_s, r3.zw, 0).x;
      r3.x = cmp(r1.w < r1.y);
      r4.y = r2.w + r1.w;
      r4.x = 1 + r2.z;
      r2.zw = r3.xx ? r4.xy : r2.zw;
      r1.w = max(1, r2.z);
      r1.w = r2.w / r1.w;
      r2.z = cmp(r2.z != 0.000000);
      if (r2.z != 0) {
        r0.z = -r1.w + r0.z;
        r0.z = ShadowFarPlanes.y * r0.z;
        r0.z = dot(r1.zz, r0.zz);
        r0.z = r0.z / ShadowFrustumWidths.y;
        r0.z = 0.5 * r0.z;
        r0.z = max(r0.z, r2.x);
        r0.z = min(r0.z, r2.y);
        r2.xyzw = r0.zzzz * float4(-0.641573071,-0.170358405,-0.220173702,0.0666730404) + r0.xyxy;
        r2.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r2.xyzw);
        r2.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r2.xyzw);
        r1.z = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.xy, r1.y).x;
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.zw, r1.y).x;
        r1.z = r1.z + r1.w;
        r2.xyzw = r0.zzzz * float4(-0.56526792,0.40981409,-0.372753888,-0.69012928) + r0.xyxy;
        r2.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r2.xyzw);
        r2.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r2.xyzw);
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.xy, r1.y).x;
        r1.z = r1.z + r1.w;
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.zw, r1.y).x;
        r1.z = r1.z + r1.w;
        r2.xyzw = r0.zzzz * float4(-0.963876784,0.0795568228,0.0895406604,-0.433672994) + r0.xyxy;
        r2.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r2.xyzw);
        r2.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r2.xyzw);
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.xy, r1.y).x;
        r1.z = r1.z + r1.w;
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.zw, r1.y).x;
        r1.z = r1.z + r1.w;
        r2.xyzw = r0.zzzz * float4(-0.815210223,-0.533595324,-0.0262216199,0.725986183) + r0.xyxy;
        r2.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r2.xyzw);
        r2.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r2.xyzw);
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.xy, r1.y).x;
        r1.z = r1.z + r1.w;
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.zw, r1.y).x;
        r1.z = r1.z + r1.w;
        r2.xyzw = r0.zzzz * float4(0.152993307,0.216891706,-0.394465297,0.896840572) + r0.xyxy;
        r2.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r2.xyzw);
        r2.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r2.xyzw);
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.xy, r1.y).x;
        r1.z = r1.z + r1.w;
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.zw, r1.y).x;
        r1.z = r1.z + r1.w;
        r2.xyzw = r0.zzzz * float4(0.566493392,0.677397907,0.65583998,-0.0949900225) + r0.xyxy;
        r2.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r2.xyzw);
        r2.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r2.xyzw);
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.xy, r1.y).x;
        r1.z = r1.z + r1.w;
        r1.w = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.zw, r1.y).x;
        r1.z = r1.z + r1.w;
        r2.xyzw = r0.zzzz * float4(0.0658113286,-0.885984778,0.465619504,-0.59089911) + r0.xyxy;
        r2.xyzw = max(float4(0.000122070313,0.50012207,0.000122070313,0.50012207), r2.xyzw);
        r2.xyzw = min(float4(0.49987793,0.99987793,0.49987793,0.99987793), r2.xyzw);
        r0.x = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.xy, r1.y).x;
        r0.x = r1.z + r0.x;
        r0.y = Shadow.SampleCmpLevelZero(ShadowSampler_s, r2.zw, r1.y).x;
        r0.x = r0.x + r0.y;
        r0.x = 0.0714285746 * r0.x;
      } else {
        r0.x = 1;
      }
      r0.x = r0.x * r0.w + -r0.w;
      r1.x = 1 + r0.x;
    }
  }
  o0.xyzw = r1.xxxx;
  return;
}