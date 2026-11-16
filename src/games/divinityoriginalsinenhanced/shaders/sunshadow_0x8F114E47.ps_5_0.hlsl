// ---- Created with 3Dmigoto v1.3.16 on Sun Nov 15 18:24:31 2026
// CascadedPCSS (the sun's shadow filter at ShadowQuality 0): the light size LightProperties.x sets the blocker search
// and the penumbra; Sun Shadow Softness scales it (1 = the game's).
#include "../shared.h"

cbuffer _Globals : register(b0)
{
  float4 ShadowProperties : packoffset(c0);
  float4 LightProperties : packoffset(c1);
  float4 shadowFarPlanes : packoffset(c2);
  float4 SliceDistances : packoffset(c3);
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
}

SamplerState PointSampler_s : register(s0);
Texture2D<float4> GBuffer1 : register(t0);
Texture2D<float4> Shadow : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  float3 v2 : TexCoord1,
  float3 v3 : TexCoord2,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 1.000000, 0, 0, 0},
                              { 0, 1.000000, 0, 0},
                              { 0, 0, 1.000000, 0},
                              { 0, 0, 0, 1.000000},
                              { 0, 0, 0.500000, 0.500000},
                              { 0, 0.500000, 0.500000, 1.000000},
                              { 0.500000, 0, 1.000000, 0.500000},
                              { 0.500000, 0.500000, 1.000000, 1.000000} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.x = GBuffer1.Load(r0.xyz).x;
  r0.y = global_ViewInfo.x * r0.x;
  r1.xyz = cmp(SliceDistances.xyz < r0.yyy);
  r2.xyzw = cmp(SliceDistances.xyzw >= r0.yyyy);
  r0.yzw = r1.xyz ? r2.yzw : 0;
  r0.y = r0.y ? 1 : 4;
  r0.y = r2.x ? 0 : r0.y;
  r1.xyz = ShadowProperties.xxy * ShadowProperties.yww;
  r1.w = cmp(2 < r1.x);
  r0.w = r0.w ? 3 : r0.y;
  r0.z = r0.z ? 2 : r0.w;
  r0.y = r1.w ? r0.z : r0.y;
  r0.z = (int)r0.y;
  r0.w = cmp(r0.z < r1.x);
  r2.xyz = r0.xxx * v2.xyz + v3.xyz;
  r1.x = (uint)r0.y << 2;
  r2.w = 1;
  r3.x = dot(ProjectionMatrix[r1.x/4]._m00_m01_m02_m03, r2.xyzw);
  r3.y = dot(ProjectionMatrix[r1.x/4]._m10_m11_m12_m13, r2.xyzw);
  r1.w = dot(ProjectionMatrix[r1.x/4]._m30_m31_m32_m33, r2.xyzw);
  r3.xyzw = r3.xyxy / r1.wwww;
  r1.w = dot(ViewMatrix[r1.x/4]._m20_m21_m22_m23, r2.xyzw);
  r2.x = dot(shadowFarPlanes.xyzw, icb[r0.y+0].xyzw);
  r1.w = saturate(r1.w / r2.x);
  r1.x = ProjectionMatrix[r1.x/4]._m00 / ProjectionMatrix[0]._m00;
  r2.x = LightProperties.w / r2.x;
  r1.x = LightProperties.x * SUN_SHADOW_SOFTNESS * r1.x;
  r2.y = 10 * r1.x;
  r2.zw = r1.xx * float2(-9.4201622,-3.99062157) + r3.zw;
  r4.xyzw = float4(2,2,2,2) / ShadowProperties.xyxy;
  r5.xyzw = float4(0.5,0.5,0.5,0.5) / r1.yzyz;
  r6.xyzw = icb[r0.y+4].xyxy * r4.xyxy + r5.zwzw;
  r4.xyzw = icb[r0.y+4].zwzw * r4.zwzw + -r5.xyzw;
  r1.yz = max(r6.xy, r2.zw);
  r1.yz = min(r1.yz, r4.xy);
  r1.z = Shadow.Sample(PointSampler_s, r1.yz).x;
  r2.x = r2.x * r1.w;
  r2.z = -r2.x * 5 + r1.z;
  r2.z = cmp(r2.z < r1.w);
  r1.y = 1;
  r1.yz = r2.zz ? r1.yz : 0;
  r5.xyzw = r1.xxxx * float4(9.45586109,-7.68907261,-0.941841006,-9.29388714) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r2.w = Shadow.Sample(PointSampler_s, r5.xy).x;
  r5.x = -r2.x * 5 + r2.w;
  r5.x = cmp(r5.x < r1.w);
  r7.y = r2.w + r1.z;
  r7.x = r2.z ? 2 : 1;
  r1.yz = r5.xx ? r7.xy : r1.yz;
  r2.z = Shadow.Sample(PointSampler_s, r5.zw).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r5.xyzw = r1.xxxx * float4(3.44959378,2.93877602,-9.1588583,4.57714319) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r2.z = Shadow.Sample(PointSampler_s, r5.xy).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r2.z = Shadow.Sample(PointSampler_s, r5.zw).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r5.xyzw = r1.xxxx * float4(-8.15442276,-8.79124641,-3.82775426,2.76768446) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r2.z = Shadow.Sample(PointSampler_s, r5.xy).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r2.z = Shadow.Sample(PointSampler_s, r5.zw).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r5.xyzw = r1.xxxx * float4(9.74843979,7.56483793,4.43233252,-9.75115585) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r2.z = Shadow.Sample(PointSampler_s, r5.xy).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r2.z = Shadow.Sample(PointSampler_s, r5.zw).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r5.xyzw = r1.xxxx * float4(5.3742981,-4.73734188,-2.6496911,-4.18930244) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r2.z = Shadow.Sample(PointSampler_s, r5.xy).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r2.z = Shadow.Sample(PointSampler_s, r5.zw).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r5.xyzw = r1.xxxx * float4(7.91975117,1.90901875,-2.41888404,9.97065067) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r2.z = Shadow.Sample(PointSampler_s, r5.xy).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r2.z = Shadow.Sample(PointSampler_s, r5.zw).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r5.xyzw = r1.xxxx * float4(-8.14099503,9.14375877,1.99841261,7.8641367) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r2.z = Shadow.Sample(PointSampler_s, r5.xy).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r2.z = Shadow.Sample(PointSampler_s, r5.zw).x;
  r2.w = -r2.x * 5 + r2.z;
  r2.w = cmp(r2.w < r1.w);
  r5.y = r2.z + r1.z;
  r5.x = 1 + r1.y;
  r1.yz = r2.ww ? r5.xy : r1.yz;
  r2.zw = r1.xx * float2(1.43831611,-1.410079) + r3.zw;
  r2.zw = max(r2.zw, r6.xy);
  r2.zw = min(r2.zw, r4.xy);
  r1.x = Shadow.Sample(PointSampler_s, r2.zw).x;
  r2.z = -r2.x * 5 + r1.x;
  r2.z = cmp(r2.z < r1.w);
  r5.y = r1.z + r1.x;
  r5.x = 1 + r1.y;
  r1.xy = r2.zz ? r5.xy : r1.yz;
  r1.y = r1.y / r1.x;
  r1.z = r1.w + -r1.y;
  r1.y = r1.z / r1.y;
  r1.y = r1.y * r2.y;
  r1.y = max(0.000500000024, r1.y);
  r5.xyzw = r1.yyyy * float4(-0.942016244,-0.399062157,0.945586085,-0.768907249) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r1.z = Shadow.Sample(PointSampler_s, r5.xy).x;
  r2.y = Shadow.Sample(PointSampler_s, r5.zw).x;
  r5.xyzw = r1.yyyy * float4(-0.0941841006,-0.929388702,0.344959378,0.293877602) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r2.z = Shadow.Sample(PointSampler_s, r5.xy).x;
  r2.w = Shadow.Sample(PointSampler_s, r5.zw).x;
  r5.xyzw = r1.yyyy * float4(-0.915885806,0.457714319,-0.815442324,-0.879124641) + r3.zwzw;
  r5.xyzw = max(r5.xyzw, r6.xyzw);
  r5.xyzw = min(r5.xyzw, r4.xyzw);
  r5.x = Shadow.Sample(PointSampler_s, r5.xy).x;
  r5.y = Shadow.Sample(PointSampler_s, r5.zw).x;
  r7.xyzw = r1.yyyy * float4(-0.382775426,0.276768446,0.974843979,0.756483793) + r3.zwzw;
  r7.xyzw = max(r7.xyzw, r6.xyzw);
  r7.xyzw = min(r7.xyzw, r4.xyzw);
  r5.z = Shadow.Sample(PointSampler_s, r7.xy).x;
  r5.w = Shadow.Sample(PointSampler_s, r7.zw).x;
  r7.xyzw = r1.yyyy * float4(0.443233252,-0.975115538,0.53742981,-0.4737342) + r3.zwzw;
  r7.xyzw = max(r7.xyzw, r6.xyzw);
  r7.xyzw = min(r7.xyzw, r4.xyzw);
  r7.x = Shadow.Sample(PointSampler_s, r7.xy).x;
  r7.y = Shadow.Sample(PointSampler_s, r7.zw).x;
  r8.xyzw = r1.yyyy * float4(-0.26496911,-0.418930233,0.791975141,0.190901875) + r3.zwzw;
  r8.xyzw = max(r8.xyzw, r6.xyzw);
  r8.xyzw = min(r8.xyzw, r4.xyzw);
  r7.z = Shadow.Sample(PointSampler_s, r8.xy).x;
  r7.w = Shadow.Sample(PointSampler_s, r8.zw).x;
  r8.xyzw = r1.yyyy * float4(-0.241888404,0.997065067,-0.81409955,0.914375901) + r3.zwzw;
  r8.xyzw = max(r8.xyzw, r6.xyzw);
  r8.xyzw = min(r8.xyzw, r4.xyzw);
  r8.x = Shadow.Sample(PointSampler_s, r8.xy).x;
  r8.y = Shadow.Sample(PointSampler_s, r8.zw).x;
  r3.xyzw = r1.yyyy * float4(0.199841261,0.78641367,0.143831611,-0.1410079) + r3.xyzw;
  r3.xyzw = max(r3.xyzw, r6.xyzw);
  r3.xyzw = min(r3.xyzw, r4.xyzw);
  r1.y = Shadow.Sample(PointSampler_s, r3.xy).x;
  r3.x = Shadow.Sample(PointSampler_s, r3.zw).x;
  if (r0.w != 0) {
    r0.w = cmp(r1.x >= 1);
    if (r0.w != 0) {
      r0.w = r2.x * 5 + r1.z;
      r0.w = cmp(r1.w < r0.w);
      r1.x = r0.w ? 1.000000 : 0;
      r1.z = r2.x * 5 + r2.y;
      r1.z = cmp(r1.w < r1.z);
      r0.w = r0.w ? 2 : 1;
      r0.w = r1.z ? r0.w : r1.x;
      r1.x = r2.x * 5 + r2.z;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r2.w;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r5.x;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r5.y;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r5.z;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r5.w;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r7.x;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r7.y;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r7.z;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r7.w;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r8.x;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r8.y;
      r1.x = cmp(r1.w < r1.x);
      r1.z = 1 + r0.w;
      r0.w = r1.x ? r1.z : r0.w;
      r1.x = r2.x * 5 + r1.y;
      r1.x = cmp(r1.w < r1.x);
      r1.y = 1 + r0.w;
      r0.w = r1.x ? r1.y : r0.w;
      r1.x = r2.x * 5 + r3.x;
      r1.x = cmp(r1.w < r1.x);
      r1.y = 1 + r0.w;
      r0.w = r1.x ? r1.y : r0.w;
      r0.w = 0.0625 * r0.w;
    } else {
      r0.w = 1;
    }
    r1.x = ShadowProperties.x * ShadowProperties.y + -1;
    r0.z = cmp(r0.z == r1.x);
    if (r0.z != 0) {
      r0.z = (int)r0.y + -1;
      r0.z = dot(SliceDistances.xyzw, icb[r0.z+0].xyzw);
      r0.x = global_ViewInfo.x * r0.x + -r0.z;
      r0.y = dot(SliceDistances.xyzw, icb[r0.y+0].xyzw);
      r0.y = r0.y + -r0.z;
      r0.x = saturate(r0.x / r0.y);
      r0.y = 1 + -LightProperties.z;
      r0.x = saturate(r0.x + -r0.y);
      r0.y = 1 / LightProperties.z;
      r0.x = saturate(r0.x * r0.y);
      r0.y = 1 + -r0.w;
      r0.w = r0.x * r0.y + r0.w;
    }
  } else {
    r0.w = 1;
  }
  o0.xyzw = r0.wwww;
  return;
}