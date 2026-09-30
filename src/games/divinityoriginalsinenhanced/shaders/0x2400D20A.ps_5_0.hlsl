// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 29 11:18:21 2026

cbuffer _Globals : register(b0)
{
  float2 WorldPositionOffset : packoffset(c0);
  float2 ShroudTextureDimensions : packoffset(c0.z);
  float4 TextureDimensions : packoffset(c1);
  float4 FOWWeights : packoffset(c2);
  float4 TexelSizes : packoffset(c3);
  float Region : packoffset(c4);
}

cbuffer PerView : register(b12)
{
  row_major float4x4 global_View : packoffset(c0);
  row_major float4x4 global_Projection : packoffset(c4);
  row_major float4x4 global_ViewProjection : packoffset(c8);
  float4 global_ViewPos : packoffset(c12);
  float4 global_ViewInfo : packoffset(c13);
}

SamplerState PointWrapSampler_s : register(s0);
SamplerState LinearClampSampler_s : register(s1);
SamplerState PointClampSampler_s : register(s2);
Texture2D<float4> Base : register(t0);
Texture2D<float4> Base2 : register(t1);
Texture2D<float4> Base3 : register(t2);
Texture2D<float4> Base4 : register(t3);
Texture2D<float4> Base5 : register(t4);
Texture2D<float4> Base6 : register(t5);
Texture2D<float4> Base7 : register(t6);
Texture2D<float4> Base8 : register(t7);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  float3 v2 : TexCoord1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = Base.Sample(PointWrapSampler_s, v1.xy).xyzw;
  r1.x = Base2.Sample(PointWrapSampler_s, v1.xy).x;
  r1.y = cmp(r1.x < 1);
  r1.xz = r1.xx * v2.xz + global_ViewPos.xz;
  r1.xz = -WorldPositionOffset.xy + r1.xz;
  r2.xyzw = r1.xzxz / TexelSizes.xxyy;
  r2.xyzw = float4(0.5,0.5,0.5,0.5) + r2.xyzw;
  r2.xy = r2.xy / ShroudTextureDimensions.xy;
  r2.zw = r2.zw / TextureDimensions.xy;
  r1.w = Base3.Sample(LinearClampSampler_s, r2.xy).x;
  r2.x = Base4.Sample(LinearClampSampler_s, r2.zw).x;
  r2.y = Base5.Sample(LinearClampSampler_s, r2.zw).x;
  r3.x = Base6.Sample(LinearClampSampler_s, r2.zw).x;
  r2.z = Base8.Sample(PointClampSampler_s, r2.zw).x;
  if (r1.y != 0) {
    r1.y = FOWWeights.y * r2.y;
    r1.y = r2.x * FOWWeights.x + r1.y;
    r1.y = saturate(r3.x * FOWWeights.z + r1.y);
    r2.x = r2.z * 255 + -Region;
    r2.x = min(1, abs(r2.x));
    r2.x = 1 + -r2.x;
    r2.y = cmp(0 < FOWWeights.w);
    if (r2.y != 0) {
      r1.xz = r1.xz / TexelSizes.zz;
      r1.xz = float2(0.5,0.5) + r1.xz;
      r1.xz = r1.xz / TextureDimensions.zw;
      r2.yz = float2(0,0);
      while (true) {
        r2.w = cmp((int)r2.z >= 3);
        if (r2.w != 0) break;
        r2.w = -1 + (int)r2.z;
        r3.x = (int)r2.w;
        r2.w = r2.y;
        r3.z = 0;
        while (true) {
          r3.w = cmp((int)r3.z >= 3);
          if (r3.w != 0) break;
          r3.w = -1 + (int)r3.z;
          r3.y = (int)r3.w;
          r3.yw = r3.xy / TextureDimensions.zw;
          r3.yw = r3.yw + r1.xz;
          r3.y = Base7.SampleLevel(LinearClampSampler_s, r3.yw, 0).x;
          r2.w = r3.y + r2.w;
          r3.z = (int)r3.z + 1;
        }
        r2.y = r2.w;
        r2.z = (int)r2.z + 1;
      }
    } else {
      r2.y = 0;
    }
    r1.x = r0.x + r0.y;
    r1.x = r1.x + r0.z;
    r3.xyz = r1.xxx * float3(0.333333343,0.333333343,0.333333343) + -r0.xyz;
    r3.xyz = r3.xyz * float3(0.300000012,0.300000012,0.300000012) + r0.xyz;
    r4.xyz = float3(0.75,0.75,0.75) * r3.xyz;
    r3.xyz = -r3.xyz * float3(0.75,0.75,0.75) + r0.xyz;
    r1.xyz = r1.yyy * r3.xyz + r4.xyz;
    r1.xyz = r1.xyz * r1.www;
    r3.xyz = r1.xyz * r2.xxx;
    r1.w = r3.x + r3.y;
    r1.w = r1.z * r2.x + r1.w;
    r4.xyz = r1.www * float3(0.333333343,0.333333343,0.333333343) + -r3.xyz;
    r3.xyz = r4.xyz * float3(0.800000012,0.800000012,0.800000012) + r3.xyz;
    r3.xyz = float3(0.75,0.75,0.75) * r3.xyz;
    r1.w = r2.y * 0.111111112 + -FOWWeights.w;
    r1.w = saturate(1 + r1.w);
    r1.xyz = r1.xyz * r2.xxx + -r3.xyz;
    o0.xyz = r1.www * r1.xyz + r3.xyz;
    o0.w = r0.w;
  } else {
    r1.xyzw = float4(0,0,0,1) + -r0.xyzw;
    o0.xyzw = TexelSizes.wwww * r1.xyzw + r0.xyzw;
  }
  return;
}