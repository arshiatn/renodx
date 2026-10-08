// Ryse: Son of Rome TAA (CryEngine, SMAA 1TX style). From 0x121C45E4.ps_5_0.cso (3Dmigoto).
// Stock: velocity t3 (packed object motion; 0 = camera reprojection of the linear depth t2 with
// mViewProjPrev), current t4 (X pattern box), history t5 clipped to the box (kept when its
// neighbors are inside it), blend 1/2.5..1/5 by the color change, sRGB out. Runs after the tonemap.
// The game read t4/t5 through sRGB views (decode, then filter). Here they are FP16 clones (no
// hardware decode): decoded by hand, history filtered after decoding. Paper White = 1 (the post
// pass applies the Paper White-UI gain, so the history does not change with the UI slider).
// Signed (wide gamut), no clamp.
#include "../shared.h"

cbuffer PER_BATCH : register(b0)
{
  row_major float4x4 mViewProjPrev : packoffset(c0);
  float4 WorldViewPos : packoffset(c4);
  float4 PS_NearFarClipDist : packoffset(c5);
  float4 PS_ScreenSize : packoffset(c6);
}

SamplerState _tex2_s : register(s2);
SamplerState _tex3_s : register(s3);
SamplerState _tex4_s : register(s4);
SamplerState _tex5_s : register(s5);
Texture2D<float4> _tex2 : register(t2);
Texture2D<float4> _tex3 : register(t3);
Texture2D<float4> _tex4 : register(t4);
Texture2D<float4> _tex5 : register(t5);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;

  // Velocity: object motion, or camera reprojection.
  r0.xy = _tex3.Sample(_tex3_s, v1.xy).xy;
  r0.z = _tex2.Sample(_tex2_s, v1.xy).x;
  r1.xyz = v2.xyz * r0.zzz + WorldViewPos.xyz;
  r2.xyz = mViewProjPrev._m20_m21_m23 * r1.zzz + mViewProjPrev._m30_m31_m33;
  r1.yzw = mViewProjPrev._m10_m11_m13 * r1.yyy + r2.xyz;
  r1.xyz = mViewProjPrev._m00_m01_m03 * r1.xxx + r1.yzw;
  r0.zw = r1.xy / r1.zz;
  r1.x = cmp(r0.x == 0.000000);
  r0.zw = -v1.xy + r0.zw;
  r0.xy = float2(-0.498039216,-0.498039216) + r0.xy;
  r1.yz = r0.xy + r0.xy;
  r1.yz = r1.yz * r1.yz;
  r0.xy = cmp(float2(0,0) < r0.xy);
  r0.xy = r0.xy ? float2(1,1) : float2(-1,-1);
  r0.xy = r1.yz * r0.xy;
  r0.xy = r1.xx ? r0.zw : r0.xy;

  // Current: center + diagonal neighbors (texel centers, so decode after the fetch).
  r1.xyz = RyseDecodeSRGB(_tex4.Sample(_tex4_s, v1.xy).xyz);
  r2.xyzw = PS_ScreenSize.zwzw * float4(-2,-2,2,-2) + v1.xyxy;
  r3.xyz = RyseDecodeSRGB(_tex4.Sample(_tex4_s, r2.xy).xyz);
  r2.xyz = RyseDecodeSRGB(_tex4.Sample(_tex4_s, r2.zw).xyz);
  r0.zw = PS_ScreenSize.zw * float2(-2,2) + v1.xy;
  r4.xyz = RyseDecodeSRGB(_tex4.Sample(_tex4_s, r0.zw).xyz);
  r0.zw = PS_ScreenSize.zw * float2(2,2) + v1.xy;
  r5.xyz = RyseDecodeSRGB(_tex4.Sample(_tex4_s, r0.zw).xyz);

  // History.
  r0.xy = v1.xy + r0.xy;
  r6.xyz = RyseSampleSRGB(_tex5, r0.xy, 1.f).xyz;
  r0.zw = r0.xy * float2(2,2) + float2(-1,-1);
  r0.z = max(abs(r0.z), abs(r0.w));
  r0.z = cmp(r0.z < 1);
  if (r0.z != 0) {
    r7.xyz = min(r3.xyz, r2.xyz);
    r7.xyz = min(r7.xyz, r4.xyz);
    r7.xyz = min(r7.xyz, r5.xyz);
    r7.xyz = min(r7.xyz, r1.xyz);
    r2.xyz = max(r3.xyz, r2.xyz);
    r2.xyz = max(r2.xyz, r4.xyz);
    r2.xyz = max(r2.xyz, r5.xyz);
    r2.xyz = max(r2.xyz, r1.xyz);
    r3.xyz = r2.xyz + r7.xyz;
    r4.xyz = -r3.xyz * float3(0.5,0.5,0.5) + r2.xyz;
    r5.xyz = -r6.xyz + r1.xyz;
    r3.xyz = -r3.xyz * float3(0.5,0.5,0.5) + r6.xyz;
    r0.z = dot(r5.xyz, r5.xyz);
    r0.z = sqrt(r0.z);
    r0.z = cmp(r0.z >= 9.99999997e-007);
    r5.xyz = rcp(r5.xyz);
    r8.xyz = r4.xyz + -r3.xyz;
    r8.xyz = r8.xyz * r5.xyz;
    r3.xyz = -r4.xyz + -r3.xyz;
    r3.xyz = r3.xyz * r5.xyz;
    r3.xyz = min(r8.xyz, r3.xyz);
    r0.w = max(r3.x, r3.y);
    r0.w = saturate(max(r0.w, r3.z));
    r0.z = r0.z ? r0.w : 1;
    // History neighbors inside the box: keep the history.
    r3.xyzw = PS_ScreenSize.zwzw * float4(-2,-2,2,-2) + r0.xyxy;
    r4.xyz = RyseSampleSRGB(_tex5, r3.xy, 1.f).xyz;
    r3.xyz = RyseSampleSRGB(_tex5, r3.zw, 1.f).xyz;
    r5.xy = PS_ScreenSize.zw * float2(-2,2) + r0.xy;
    r5.xyz = RyseSampleSRGB(_tex5, r5.xy, 1.f).xyz;
    r0.xy = PS_ScreenSize.zw * float2(2,2) + r0.xy;
    r0.xyw = RyseSampleSRGB(_tex5, r0.xy, 1.f).xyz;
    r8.xyz = max(r4.xyz, r7.xyz);
    r8.xyz = min(r8.xyz, r2.xyz);
    r4.xyz = r8.xyz + -r4.xyz;
    r1.w = dot(r4.xyz, r4.xyz);
    r1.w = sqrt(r1.w);
    r4.xyz = max(r3.xyz, r7.xyz);
    r4.xyz = min(r4.xyz, r2.xyz);
    r3.xyz = r4.xyz + -r3.xyz;
    r2.w = dot(r3.xyz, r3.xyz);
    r2.w = sqrt(r2.w);
    r1.w = r2.w + r1.w;
    r3.xyz = max(r5.xyz, r7.xyz);
    r3.xyz = min(r3.xyz, r2.xyz);
    r3.xyz = r3.xyz + -r5.xyz;
    r2.w = dot(r3.xyz, r3.xyz);
    r2.w = sqrt(r2.w);
    r1.w = r2.w + r1.w;
    r3.xyz = max(r0.xyw, r7.xyz);
    r2.xyz = min(r3.xyz, r2.xyz);
    r0.xyw = r2.xyz + -r0.xyw;
    r0.x = dot(r0.xyw, r0.xyw);
    r0.x = sqrt(r0.x);
    r0.x = r1.w + r0.x;
    r0.x = cmp(r0.x < 0.0199999996);
    r0.x = r0.x ? 0 : r0.z;
  } else {
    r0.x = 1;
  }

  // Blend.
  r0.yzw = r6.xyz + -r1.xyz;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = sqrt(r0.y);
  r0.y = 10 * r0.y;
  r0.y = min(1, r0.y);
  r2.xyz = -r6.xyz + r1.xyz;
  r0.xzw = r0.xxx * r2.xyz + r6.xyz;
  r0.y = r0.y * 2.5 + 2.5;
  r0.y = rcp(r0.y);
  r1.xyz = r1.xyz + -r0.xzw;
  r0.xyz = r0.yyy * r1.xyz + r0.xzw;

  // Stock sRGB (signed, unclamped).
  o0.xyz = RyseEncodeSRGB(r0.xyz);
  o0.w = 0;
}
