// Ryse: Son of Rome SSDO (screen-space directional occlusion, half resolution). From 0xD1106E8B.ps_5_0.cso (3Dmigoto,
// the DevKit dump of 2026-10-06). Stock: 8 depth samples around the pixel (two sets of 4, radius from SSDOParams, a
// 2 x 2 pattern) give the direction toward what covers it (camera space, then world) and its amount, plus the
// terrain's large-scale occlusion (LSDO, from the heightmap _texB): out 0.5 + 0.175 x the direction, 0.35 x the amount,
// which the game blurs (0x2822C42B) and its tiled lighting (cs 0x897232B5, t5) turns into ambient, light and
// reflection occlusion. Ambient Occlusion GT-VBAO (SI.ssao_mode 1): the SSDO term from the visibility bitmask instead
// (ssdo_bitmask.hlsli); the terrain's LSDO is added as the game's.
#include "../shared.h"
#include "../ssdo_bitmask.hlsli"

cbuffer PER_BATCH : register(b0)
{
  float4 g_WorldViewPos : packoffset(c0);
  row_major float3x4 SSDO_CameraMatrix : packoffset(c1);
  row_major float3x4 SSDO_CameraMatrixInv : packoffset(c4);
  float4 LSDO_TexToWorldScale : packoffset(c7);
  float4 LSDO_Params : packoffset(c8);
  float4 PS_NearFarClipDist : packoffset(c9);
  float4 LSDO_TexToWorldTranslation : packoffset(c10);
  float4 PS_ScreenSize : packoffset(c11);
  float4 vInvShadowMapSize : packoffset(c12);
  float4 SSDOParams : packoffset(c13);
  float4 ViewSpaceParams : packoffset(c14);
}

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
SamplerState _texB_s : register(s11);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);
Texture2D<float4> _texB : register(t11);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.z = 1;
  r1.xy = PS_ScreenSize.xy * v1.xy;
  r1.xy = (int2)r1.xy;
  r1.xy = (int2)r1.xy & int2(1,1);
  r2.xyz = float3(0.5,0.150000006,0.150000006) * SSDOParams.wxy;
  r0.w = r1.x ? r2.x : SSDOParams.w;
  r1.x = 0.5 * r0.w;
  r0.w = r1.y ? r1.x : r0.w;
  r1.x = _tex1.Sample(_tex1_s, v1.xy).x;
  r1.yz = r2.yz / r1.xx;
  r1.yz = max(SSDOParams.zz, r1.yz);
  r1.yz = min(r1.yz, r0.ww);
  r2.xy = frac(v1.zw);
  r1.w = dot(r2.xy, float2(1,0.25));
  r1.w = 6.28310013 * r1.w;
  sincos(r1.w, r2.x, r3.x);
  r4.x = -r2.x;
  r4.y = r3.x;
  r4.z = r2.x;
  r2.x = dot(float2(-0.0199999996,-0.140000001), r4.xy);
  r2.y = dot(float2(-0.0199999996,-0.140000001), r4.yz);
  r2.xy = r2.xy * r1.yz + v1.xy;
  r3.x = _tex1.SampleLevel(_tex1_s, r2.xy, 0).x;
  r0.xy = r2.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r2.x = dot(float2(0.239999995,-0.0399999991), r4.xy);
  r2.y = dot(float2(0.239999995,-0.0399999991), r4.yz);
  r2.xy = r2.xy * r1.yz + v1.xy;
  r3.y = _tex1.SampleLevel(_tex1_s, r2.xy, 0).x;
  r2.xy = r2.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r5.x = dot(float2(0.0799999982,0.360000014), r4.xy);
  r5.y = dot(float2(0.0799999982,0.360000014), r4.yz);
  r5.xy = r5.xy * r1.yz + v1.xy;
  r3.z = _tex1.SampleLevel(_tex1_s, r5.xy, 0).x;
  r5.xy = r5.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r6.x = dot(float2(-0.400000006,0.25999999), r4.xy);
  r6.y = dot(float2(-0.400000006,0.25999999), r4.yz);
  r1.zw = r6.xy * r1.yz + v1.xy;
  r1.y = r1.y * r1.x;
  r1.y = ViewSpaceParams.x * r1.y;
  r1.y = PS_NearFarClipDist.y * r1.y;
  r1.y = r1.y * r1.y;
  r3.w = _tex1.SampleLevel(_tex1_s, r1.zw, 0).x;
  r6.xy = r1.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r3.xyzw = PS_NearFarClipDist.yyyy * r3.xyzw;
  r7.xy = v1.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r7.z = 1;
  r7.xyz = r7.xyz * r1.xxx;
  r7.xyz = PS_NearFarClipDist.yyy * r7.xyz;
  r0.xyz = r0.xyz * r3.xxx + -r7.xyz;
  r8.x = dot(r0.xyz, r0.xyz);
  r2.z = 1;
  r2.xyz = r2.xyz * r3.yyy + -r7.xyz;
  r8.y = dot(r2.xyz, r2.xyz);
  r5.z = 1;
  r3.xyz = r5.xyz * r3.zzz + -r7.xyz;
  r8.z = dot(r3.xyz, r3.xyz);
  r6.z = 1;
  r5.xyz = r6.xyz * r3.www + -r7.xyz;
  r8.w = dot(r5.xyz, r5.xyz);
  r6.xyzw = rsqrt(r8.xyzw);
  r8.xyzw = r1.yyyy * float4(0.3926875,0.3926875,0.3926875,0.3926875) + r8.xyzw;
  r1.y = 0.3926875 * r1.y;
  r0.xyz = r6.xxx * r0.xyz;
  r9.xyz = _tex0.Sample(_tex0_s, v1.xy).xyz;
  r9.xyz = r9.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.z = dot(r9.xyz, r9.xyz);
  r1.z = rsqrt(r1.z);
  r9.xyz = r9.xyz * r1.zzz;
  r10.x = dot(SSDO_CameraMatrix._m00_m01_m02, r9.xyz);
  r10.y = dot(SSDO_CameraMatrix._m10_m11_m12, r9.xyz);
  r10.z = dot(SSDO_CameraMatrix._m20_m21_m22, r9.xyz);
  r1.z = dot(r10.xyz, r10.xyz);
  r1.z = rsqrt(r1.z);
  r9.xyz = r10.xyz * r1.zzz;
  r10.x = saturate(dot(r9.xyz, r0.xyz));
  r2.xyz = r6.yyy * r2.xyz;
  r10.y = saturate(dot(r9.xyz, r2.xyz));
  r3.xyz = r6.zzz * r3.xyz;
  r5.xyz = r6.www * r5.xyz;
  r10.z = saturate(dot(r9.xyz, r3.xyz));
  r10.w = saturate(dot(r9.xyz, r5.xyz));
  r6.xyzw = r10.xyzw * r1.yyyy;
  r6.xyzw = r6.xyzw / r8.xyzw;
  r1.yzw = r6.yyy * r2.xyz;
  r0.xyz = r6.xxx * r0.xyz + r1.yzw;
  r0.xyz = r6.zzz * r3.xyz + r0.xyz;
  r0.xyz = r6.www * r5.xyz + r0.xyz;
  r1.y = dot(r6.xyzw, float4(1,1,1,1));
  r1.zw = SSDOParams.xy / r1.xx;
  r1.zw = max(SSDOParams.zz, r1.zw);
  r1.zw = min(r1.zw, r0.ww);
  r2.x = dot(float2(-0.340000004,-0.439999998), r4.xy);
  r2.y = dot(float2(-0.340000004,-0.439999998), r4.yz);
  r2.zw = r2.xy * r1.zw + v1.xy;
  r3.x = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r5.xy = r2.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r6.x = dot(float2(0.400000006,-0.519999981), r4.xy);
  r6.y = dot(float2(0.400000006,-0.519999981), r4.yz);
  r2.zw = r6.xy * r1.zw + v1.xy;
  r3.y = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r8.xy = r2.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r10.x = dot(float2(0.680000007,0.300000012), r4.xy);
  r11.x = dot(float2(-0.319999993,0.839999974), r4.xy);
  r10.y = dot(float2(0.680000007,0.300000012), r4.yz);
  r11.y = dot(float2(-0.319999993,0.839999974), r4.yz);
  r2.zw = r10.xy * r1.zw + v1.xy;
  r3.z = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r4.xy = r2.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r2.zw = r11.xy * r1.zw + v1.xy;
  r0.w = r1.z * r1.x;
  r1.xzw = r1.xxx * v2.yzx + g_WorldViewPos.yzx;
  r0.w = ViewSpaceParams.x * r0.w;
  r0.w = PS_NearFarClipDist.y * r0.w;
  r0.w = r0.w * r0.w;
  r3.w = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r12.xy = r2.zw * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r3.xyzw = PS_NearFarClipDist.yyyy * r3.xyzw;
  r5.z = 1;
  r5.xyz = r5.xyz * r3.xxx + -r7.xyz;
  r13.x = dot(r5.xyz, r5.xyz);
  r8.z = 1;
  r8.xyz = r8.xyz * r3.yyy + -r7.xyz;
  r13.y = dot(r8.xyz, r8.xyz);
  r4.z = 1;
  r3.xyz = r4.xyz * r3.zzz + -r7.xyz;
  r13.z = dot(r3.xyz, r3.xyz);
  r12.z = 1;
  r4.xyz = r12.xyz * r3.www + -r7.xyz;
  r13.w = dot(r4.xyz, r4.xyz);
  r7.xyzw = rsqrt(r13.xyzw);
  r12.xyzw = r0.wwww * float4(1.96343756,1.96343756,1.96343756,1.96343756) + r13.xyzw;
  r0.w = 1.96343756 * r0.w;
  r5.xyz = r7.xxx * r5.xyz;
  r13.x = saturate(dot(r9.xyz, r5.xyz));
  r8.xyz = r8.xyz * r7.yyy;
  r13.y = saturate(dot(r9.xyz, r8.xyz));
  r3.xyz = r7.zzz * r3.xyz;
  r4.xyz = r7.www * r4.xyz;
  r13.z = saturate(dot(r9.xyz, r3.xyz));
  r13.w = saturate(dot(r9.xyz, r4.xyz));
  r7.xyzw = r13.xyzw * r0.wwww;
  r7.xyzw = r7.xyzw / r12.xyzw;
  r8.xyz = r7.yyy * r8.xyz;
  r5.xyz = r7.xxx * r5.xyz + r8.xyz;
  r3.xyz = r7.zzz * r3.xyz + r5.xyz;
  r3.xyz = r7.www * r4.xyz + r3.xyz;
  r0.w = dot(r7.xyzw, float4(1,1,1,1));
  r4.w = r1.y + r0.w;
  r0.xyz = r3.xyz + r0.xyz;
  r4.x = dot(SSDO_CameraMatrixInv._m00_m01_m02, r0.xyz);
  r4.y = dot(SSDO_CameraMatrixInv._m10_m11_m12, r0.xyz);
  r4.z = dot(SSDO_CameraMatrixInv._m20_m21_m22, r0.xyz);
  // Ambient Occlusion GT-VBAO: the SSDO term (r4: the direction in world space, the amount) from the visibility
  // bitmask, the G-buffer normal in the same camera space as the game's.
  if (SI.ssao_mode == 1.f) {
    float3 normal = _tex0.Sample(_tex0_s, v1.xy).xyz * 2.f - 1.f;
    normal = float3(dot(SSDO_CameraMatrix._m00_m01_m02, normal), dot(SSDO_CameraMatrix._m10_m11_m12, normal),
                    dot(SSDO_CameraMatrix._m20_m21_m22, normal));
    const float4 occlusion = RyseBitmaskOcclusion(v1.xy, (uint2)v0.xy, normal, _tex1, _tex1_s, PS_NearFarClipDist.y,
                                                  ViewSpaceParams);
    r4.x = dot(SSDO_CameraMatrixInv._m00_m01_m02, occlusion.xyz);
    r4.y = dot(SSDO_CameraMatrixInv._m10_m11_m12, occlusion.xyz);
    r4.z = dot(SSDO_CameraMatrixInv._m20_m21_m22, occlusion.xyz);
    r4.w = occlusion.w;
  }
  r0.xy = -LSDO_TexToWorldTranslation.xy + r1.wx;
  r0.xy = r0.xy / LSDO_TexToWorldScale.xy;
  r0.z = 15 * vInvShadowMapSize.x;
  r2.zw = r2.xy * r0.zz + r0.xy;
  r0.w = _texB.SampleLevel(_texB_s, r2.zw, 0).x;
  r1.y = LSDO_TexToWorldTranslation.z + -r1.z;
  r3.x = r0.w * LSDO_TexToWorldScale.z + r1.y;
  r2.zw = LSDO_TexToWorldScale.xy * r0.zz;
  r3.yz = r2.xy * r2.zw;
  r5.x = dot(r3.xyz, r3.xyz);
  r2.xy = r6.xy * r0.zz + r0.xy;
  r6.xy = r6.xy * r2.zw;
  r0.w = _texB.SampleLevel(_texB_s, r2.xy, 0).x;
  r6.z = r0.w * LSDO_TexToWorldScale.z + r1.y;
  r5.y = dot(r6.xyz, r6.xyz);
  r2.xy = r10.xy * r0.zz + r0.xy;
  r7.xy = r10.xy * r2.zw;
  r8.xy = r11.xy * r2.zw;
  r0.xy = r11.xy * r0.zz + r0.xy;
  r0.x = _texB.SampleLevel(_texB_s, r0.xy, 0).x;
  r8.z = r0.x * LSDO_TexToWorldScale.z + r1.y;
  r0.x = _texB.SampleLevel(_texB_s, r2.xy, 0).x;
  r7.z = r0.x * LSDO_TexToWorldScale.z + r1.y;
  r5.z = dot(r7.xyz, r7.xyz);
  r5.w = dot(r8.xyz, r8.xyz);
  r0.xyzw = rsqrt(r5.xyzw);
  r2.xyz = r3.yzx * r0.xxx;
  r9.xyz = ddy_fine(r1.zwx);
  r1.xyz = ddx_fine(r1.xzw);
  r10.xyz = r9.xyz * r1.xyz;
  r1.xyz = r9.zxy * r1.yzx + -r10.xyz;
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r1.xyz = r1.zxy * r0.xxx;
  r2.x = saturate(dot(r1.yzx, r2.xyz));
  r6.xyw = r6.xyz * r0.yyy;
  r3.y = r6.z;
  r2.y = saturate(dot(r1.yzx, r6.xyw));
  r0.xyz = r7.xyz * r0.zzz;
  r3.z = r7.z;
  r6.xyz = r8.xyz * r0.www;
  r3.w = r8.z;
  r3.xyzw = max(float4(0,0,0,0), r3.xyzw);
  r0.w = dot(r3.xyzw, float4(0.25,0.25,0.25,0.25));
  r0.w = min(1, r0.w);
  r0.w = r0.w * 0.800000012 + 0.200000003;
  r2.w = saturate(dot(r1.yzx, r6.xyz));
  r2.z = saturate(dot(r1.yzx, r0.xyz));
  r1.x = saturate(r1.x);
  r0.x = r1.x * 0.75 + 0.25;
  r0.y = 19.6343746 * r0.x;
  r1.xyzw = r0.xxxx * float4(19.6343746,19.6343746,19.6343746,19.6343746) + r5.xyzw;
  r2.xyzw = r0.yyyy * r2.xyzw;
  r1.xyzw = r2.xyzw / r1.xyzw;
  r1.xyzw = min(float4(0.25,0.25,0.25,0.25), r1.xyzw);
  r1.w = dot(r1.xyzw, float4(1,1,1,1));
  r1.xyz = LSDO_Params.xxx;
  r0.xyzw = r1.xyzw * r0.wwww;
  r1.xyz = float3(0,0,0);
  r1.w = LSDO_Params.x;
  r0.xyzw = r0.xyzw * r1.xyzw + r4.xyzw;
  o0.xyz = r0.xyz * float3(0.174999997,0.174999997,0.174999997) + float3(0.5,0.5,0.5);
  o0.w = 0.349999994 * r0.w;
  return;
}
