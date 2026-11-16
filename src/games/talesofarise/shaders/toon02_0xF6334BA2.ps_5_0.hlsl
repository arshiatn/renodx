// ---- Created with 3Dmigoto v1.3.16 on Fri Oct 09 11:07:55 2026
// Tales of Arise toon lighting (Bandai TO14 deferred lighting of characters and foliage, behind their own stencil
// before lighting00-05): t7 the base pass's light, t6 Bandai's AO, t1-t5 the G-buffer, Bandai's screen-space GI (10 x 6,
// t28) behind the menu's switch cb0[59].w. From 0xF6334BA2.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps); traced in run_080739 (level A).
// RenoDX: one AO method: the rays' AO or Path-Traced Ambient in place of Bandai's AO (t41; rt_ambient.hlsli, unbound:
// Bandai's). One GI method: under ours (SI.gtvbgi: GT-VBGI, the rays' bounce light or path tracing) Bandai's GI add
// ((1.2 x GI)^1.5 x the base colour x its AO share) goes and ours (t42, the light arriving) takes its place there
// (shared.h TOA_GAME_GI_SHARE; Game's GI Under the Open Sky keeps its share above the 0.3 floor). Bandai's GI colour
// still tints the lit colour and the rims (terms ours does not replace): its chain keeps running.
#include "../shared.h"
#include "../rt_ambient.hlsli"
Texture2D<float4> t28 : register(t28);

TextureCube<float4> t27 : register(t27);

Texture2D<float4> t23 : register(t23);

Texture2D<float4> t22 : register(t22);

Texture2D<float4> t21 : register(t21);

Texture2D<float4> t20 : register(t20);

Texture2D<float4> t16 : register(t16);

Texture2D<float4> t15 : register(t15);

Texture2D<float4> t14 : register(t14);

Texture2D<float4> t13 : register(t13);

Texture2D<float4> t12 : register(t12);

Texture2D<float4> t11 : register(t11);

Texture3D<float4> t10 : register(t10);

Texture3D<float4> t9 : register(t9);

Texture3D<float4> t8 : register(t8);

Texture2D<float4> t7 : register(t7);

Texture2D<float4> t6 : register(t6);

Texture2D<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s8_s : register(s8);

SamplerState s7_s : register(s7);

SamplerState s6_s : register(s6);

SamplerState s5_s : register(s5);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[8];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[156];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[60];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = t7.Sample(s6_s, v0.xy).xyz;
  r1.xyz = t1.SampleLevel(s1_s, v0.xy, 0).xyz;
  r2.xy = t2.SampleLevel(s2_s, v0.xy, 0).zw;
  r3.xyzw = t3.SampleLevel(s3_s, v0.xy, 0).xyzw;
  r4.xyzw = t4.SampleLevel(s4_s, v0.xy, 0).xyzw;
  r0.w = t0.SampleLevel(s0_s, v0.xy, 0).x;
  r1.w = r0.w * cb1[57].x + cb1[57].y;
  r0.w = r0.w * cb1[57].z + -cb1[57].w;
  r0.w = 1 / r0.w;
  r0.w = r1.w + r0.w;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.w = 255 * r2.y;
  r1.w = round(r1.w);
  r1.w = (uint)r1.w;
  r2.yz = (int2)r1.ww & int2(15,16);
  r4.xyzw = r2.zzzz ? float4(0,0,0,0) : r4.xyzw;
  r3.xyz = r3.xyz * cb1[126].www + cb1[126].xyz;
  r5.xyz = t5.Sample(s6_s, v0.xy).xyz;
  r1.w = dot(r1.xyz, cb0[51].xyz);
  r2.z = saturate(r1.w * 0.5 + 0.5);
  r2.z = 1.25 * r2.z;
  r2.z = r2.z * r2.z;
  r2.w = r3.w + r3.w;
  r2.w = max(0.300000012, r2.w);
  r6.xyz = r0.xyz * r2.www + -r0.xyz;
  r0.xyz = cb0[45].xxx * r6.xyz + r0.xyz;
  r3.w = saturate(dot(r0.xzy, float3(0.300000012,0.300000012,0.400000006)));
  r4.w = 7 * r4.w;
  r4.w = (uint)r4.w;
  r4.w = (uint)r4.w;
  r4.w = min(6, r4.w);
  r4.w = (uint)r4.w;
  if (r4.w == 0) {
    r6.xyz = t20.SampleLevel(s7_s, r3.ww, 0).xyz;
    r7.xyz = t13.SampleLevel(s7_s, r2.zz, 0).xyz;
  } else {
    r5.w = cmp((int)r4.w == 1);
    if (r5.w != 0) {
      r8.xyz = t21.SampleLevel(s7_s, r3.ww, 0).xyz;
      r9.xyz = t14.SampleLevel(s7_s, r2.zz, 0).xyz;
      r5.w = saturate(0.100000001 + r1.w);
      r5.w = r5.w * r5.w;
      r6.w = dot(float3(0.300000012,0.400000006,0.300000012), r8.xyz);
      r7.w = dot(float3(0.300000012,0.400000006,0.300000012), r9.xyz);
      r10.xyz = r6.www + -r8.xyz;
      r6.xyz = r5.www * r10.xyz + r8.xyz;
      r8.xyz = r7.www + -r9.xyz;
      r7.xyz = r5.www * r8.xyz + r9.xyz;
    } else {
      r5.w = cmp((int)r4.w == 2);
      if (r5.w != 0) {
        r8.xyz = t22.SampleLevel(s7_s, r3.ww, 0).xyz;
        r9.xyz = t15.SampleLevel(s7_s, r2.zz, 0).xyz;
        r5.w = saturate(0.100000001 + r1.w);
        r5.w = r5.w * r5.w;
        r6.w = dot(float3(0.300000012,0.400000006,0.300000012), r8.xyz);
        r7.w = dot(float3(0.300000012,0.400000006,0.300000012), r9.xyz);
        r10.xyz = r6.www + -r8.xyz;
        r6.xyz = r5.www * r10.xyz + r8.xyz;
        r8.xyz = r7.www + -r9.xyz;
        r7.xyz = r5.www * r8.xyz + r9.xyz;
      } else {
        r8.xyz = t23.SampleLevel(s7_s, r3.ww, 0).xyz;
        r9.xyz = t16.SampleLevel(s7_s, r2.zz, 0).xyz;
        r3.w = saturate(0.100000001 + r1.w);
        r3.w = r3.w * r3.w;
        r5.w = dot(float3(0.300000012,0.400000006,0.300000012), r8.xyz);
        r6.w = dot(float3(0.300000012,0.400000006,0.300000012), r9.xyz);
        r10.xyz = r5.www + -r8.xyz;
        r6.xyz = r3.www * r10.xyz + r8.xyz;
        r8.xyz = r6.www + -r9.xyz;
        r7.xyz = r3.www * r8.xyz + r9.xyz;
      }
    }
  }
  r3.w = dot(float3(0.300000012,0.400000006,0.300000012), r7.xyz);
  r7.xyz = r7.xyz + -r3.www;
  r7.xyz = r7.xyz * float3(0.5,0.5,0.5) + r3.www;
  r0.xyz = r6.xyz * r0.xyz;
  r2.z = 1.20000005 * r2.z;
  r2.z = min(1, r2.z);
  r2.z = r2.z * r2.z;
  r2.z = r2.z * r2.z;
  r6.xyz = r3.xyz + -r4.xyz;
  r4.xyz = r2.zzz * r6.xyz + r4.xyz;
  r4.xyz = r4.xyz + -r3.xyz;
  r3.xyz = cb0[45].yyy * r4.xyz + r3.xyz;
  r2.z = min(1, r2.w);
  r2.z = r2.z * r2.z;
  r2.z = r2.z * -0.5 + 1.5;
  r4.xyz = log2(r3.xyz);
  r4.xyz = r4.xyz * r2.zzz;
  r4.xyz = exp2(r4.xyz);
  r4.xyz = r4.xyz + -r3.xyz;
  r3.xyz = cb0[45].xxx * r4.xyz + r3.xyz;
  r0.xyz = r3.xyz * r0.xyz;
  r2.y = cmp((int)r2.y == 13);
  if (r2.y != 0) {
    r2.x = log2(r2.x);
    r2.y = dot(r1.xyz, r1.xyz);
    r2.y = rsqrt(r2.y);
    r2.yzw = r2.yyy * r1.xyz;
    r4.xyz = v1.xyz * r0.www;
    r3.w = dot(r4.xyz, r4.xyz);
    r3.w = rsqrt(r3.w);
    r4.xyz = r4.xyz * r3.www;
    r3.w = dot(r4.xyz, r2.yzw);
    r3.w = r3.w + r3.w;
    r2.yzw = r2.yzw * -r3.www + r4.xyz;
    r2.x = -r2.x * 1.92000008 + 1;
    r2.x = 6.5 + -r2.x;
    r3.w = saturate(0.000125000006 * r0.w);
    r3.w = 8 * r3.w;
    r2.x = max(r3.w, r2.x);
    r4.xyz = t27.SampleLevel(s8_s, r2.yzw, r2.x).xyz;
    r2.xyz = t27.SampleLevel(s8_s, r2.yzw, 7).xyz;
    r2.x = dot(r2.xzy, float3(0.300000012,0.300000012,0.400000006));
    r2.x = 1 / r2.x;
    r2.xyz = r4.xyz * r2.xxx;
    r2.w = cmp(cb0[59].w == 0.000000);
    if (r2.w != 0) {
      r4.xyz = float3(1,1,1);
    }
    if (r2.w == 0) {
      r6.xy = cb1[9].xy * r1.yy;
      r6.xy = r1.xx * cb1[8].xy + r6.xy;
      r6.xy = r1.zz * cb1[10].xy + r6.xy;
      r6.zw = float2(0.949999988,0.949999988) * v0.xy;
      r8.xyzw = r6.xyxy * float4(0.150000006,-0.150000006,0.25,-0.25) + r6.zwzw;
      r9.xyz = t28.SampleLevel(s6_s, r8.xy, 0).xyz;
      r8.xyz = t28.SampleLevel(s6_s, r8.zw, 0).xyz;
      r8.xyz = float3(0.300000012,0.300000012,0.300000012) * r8.xyz;
      r8.xyz = r9.xyz * float3(0.400000006,0.400000006,0.400000006) + r8.xyz;
      r6.xy = r6.xy * float2(0.349999994,-0.349999994) + r6.zw;
      r6.xyz = t28.SampleLevel(s6_s, r6.xy, 0).xyz;
      r4.xyz = r6.xyz * float3(0.300000012,0.300000012,0.300000012) + r8.xyz;
    }
    r2.w = dot(float3(0.300000012,0.400000006,0.300000012), r4.xyz);
    r2.w = 0.00100000005 + r2.w;
    r4.xyz = r4.xyz / r2.www;
    r1.w = r1.w * -0.5 + 0.5;
    r1.w = r1.w * r1.w;
    r4.xyz = float3(-1,-1,-1) + r4.xyz;
    r4.xyz = r1.www * r4.xyz + float3(1,1,1);
    r4.xyz = r4.xyz * r0.xyz;
    r0.xyz = r4.xyz * r2.xyz;
  }
  r0.xyz = r0.xyz * r7.xyz;
  r0.xyz = r0.xyz * cb0[53].xyz + r5.xyz;
  r1.w = cmp((uint)r4.w >= 3);
  if (r1.w != 0) {
    r2.xy = cb1[9].xy * r1.yy;
    r2.xy = r1.xx * cb1[8].xy + r2.xy;
    r2.xy = r1.zz * cb1[10].xy + r2.xy;
    r1.w = RayTracedAmbientOcclusion(v0.xy, t6.SampleLevel(s5_s, v0.xy, 0).x);
    r1.w = r1.w * 0.699999988 + 0.300000012;
    r2.z = cmp(cb0[59].w == 0.000000);
    if (r2.z != 0) {
      r4.xyz = float3(0,0,0);
    }
    if (r2.z == 0) {
      r2.zw = float2(0.949999988,0.949999988) * v0.xy;
      r5.xyzw = r2.xyxy * float4(0.150000006,-0.150000006,0.25,-0.25) + r2.zwzw;
      r6.xyz = t28.SampleLevel(s6_s, r5.xy, 0).xyz;
      r5.xyz = t28.SampleLevel(s6_s, r5.zw, 0).xyz;
      r5.xyz = float3(0.300000012,0.300000012,0.300000012) * r5.xyz;
      r5.xyz = r6.xyz * float3(0.400000006,0.400000006,0.400000006) + r5.xyz;
      r2.xy = r2.xy * float2(0.349999994,-0.349999994) + r2.zw;
      r2.xyz = t28.SampleLevel(s6_s, r2.xy, 0).xyz;
      r4.xyz = r2.xyz * float3(0.300000012,0.300000012,0.300000012) + r5.xyz;
    }
    r2.xyz = float3(1.20000005,1.20000005,1.20000005) * r4.xyz;
    r2.xyz = log2(r2.xyz);
    r2.xyz = float3(1.5,1.5,1.5) * r2.xyz;
    r2.xyz = exp2(r2.xyz);
    r2.xyz = r2.xyz * r3.xyz;
    // Our GI in place of Bandai's add (shared.h); its share above the 0.3 floor under the open sky.
    r2.xyz = r2.xyz * TOA_GAME_GI_SHARE(r1.w, r1.w - 0.3f);
    r2.xyz = r0.xyz * r1.www + r2.xyz;
    r2.xyz += RayTracedBounceLight(v0.xy, r3.xyz);
    r1.x = dot(r1.xyz, -cb0[51].xyz);
    r1.x = saturate(r1.x * 0.5 + 0.5);
    r1.y = r1.x * -0.5 + 1.5;
    r1.z = dot(float3(0.300000012,0.400000006,0.300000012), cb1[155].xyz);
    r4.xyz = cb1[155].xyz + -r1.zzz;
    r4.xyz = r4.xyz * float3(0.400000006,0.400000006,0.400000006) + r1.zzz;
    r1.w = min(0.300000012, r1.z);
    r1.z = 0.00999999978 + r1.z;
    r1.z = r1.w / r1.z;
    r4.xyz = r4.xyz * r1.zzz;
    r1.x = log2(r1.x);
    r1.x = 1.5 * r1.x;
    r1.x = exp2(r1.x);
    r1.x = r1.x + r1.x;
    r1.xzw = r1.xxx * r4.xyz;
    r1.xzw = r1.xzw * r3.xyz;
    r1.xyz = r2.xyz * r1.yyy + r1.xzw;
    r1.w = 2;
  } else {
    r2.x = RayTracedAmbientOcclusion(v0.xy, t6.SampleLevel(s5_s, v0.xy, 0).x);
    r2.x = r2.x * 0.5 + 0.5;
    r1.xyz = r2.xxx * r0.xyz;
    r1.w = 2.3499999;
  }
  r0.xyz = v1.xyz * r0.www;
  r2.x = v1.z * r0.w + cb1[59].z;
  r2.yzw = t28.Sample(s6_s, v0.xy).xyz;
  r3.x = dot(r0.xyz, r0.xyz);
  r3.x = rsqrt(r3.x);
  r3.xyz = r3.xxx * r0.xyz;
  r3.w = cb0[31].z * r2.x;
  r3.w = r3.w * -2 + r2.x;
  r4.x = cmp(cb0[32].y < r3.w);
  r4.y = cmp(300000 < r0.w);
  r4.x = (int)r4.y | (int)r4.x;
  r4.z = cmp(cb0[31].y == 1.000000);
  r4.x = r4.z ? r4.x : 0;
  if (r4.x != 0) {
    r4.x = saturate(r3.z);
    r4.z = saturate(-r3.z);
    r4.z = r4.z + -r4.x;
    r4.x = cb0[31].z * r4.z + r4.x;
    r4.z = cmp(r4.x >= 0.100000001);
    if (r4.z != 0) {
      r4.zw = cb1[123].xy / cb1[122].xy;
      r4.zw = v0.xy * r4.zw;
      r5.xyzw = t11.SampleLevel(s6_s, r4.zw, 0).xyzw;
    } else {
      r5.xyzw = float4(0,0,0,1);
    }
    r3.w = -cb0[32].y + r3.w;
    r6.y = saturate(0.000199999995 * r3.w);
    r3.w = -1 + r5.w;
    r6.x = r6.y * r3.w + 1;
    r7.xyz = r6.yyy * r5.xyz;
    r7.xyz = r4.yyy ? r5.xyz : r7.xyz;
    r5.x = 0;
    r4.yz = r4.yy ? r5.wx : r6.xy;
    r5.xyz = r4.yyy * r1.xyz + r7.xyz;
    r3.w = cmp(cb0[32].w == 1.000000);
    r4.w = dot(r7.xzy, float3(0.300000012,0.300000012,0.400000006));
    r4.w = saturate(0.5 * r4.w);
    r4.z = r4.z * -r4.w + 1;
    r6.xyz = float3(3000000,3000000,3000000) * r3.xyz;
    r7.xyz = v1.xyz * r0.www + -r6.xyz;
    r6.xyz = r4.zzz * r7.xyz + r6.xyz;
    r0.xyz = r3.www ? r6.xyz : r0.xyz;
  } else {
    r5.xyz = r1.xyz;
    r4.xy = float2(1,1);
  }
  r3.w = saturate(-r3.z);
  r3.w = cb0[38].x * r3.w;
  r3.w = cmp(0 < r3.w);
  if (r3.w != 0) {
    r2.x = -cb0[38].y + r2.x;
    r2.x = saturate(-r2.x / cb0[38].z);
    r3.w = cmp(0 < r2.x);
    if (r3.w != 0) {
      r4.zw = cb1[123].xy / cb1[122].xy;
      r4.zw = v0.xy * r4.zw;
      r4.zw = t12.SampleLevel(s6_s, r4.zw, 0).xy;
      r3.w = 1 + -r4.w;
      r3.w = max(0, r3.w);
      r3.w = -1 + r3.w;
      r3.w = r2.x * r3.w + 1;
      r2.x = r4.z * r2.x;
      r6.xyz = cb0[39].xyz * r2.xxx;
      r5.xyz = r3.www * r5.xyz + r6.xyz;
    }
  }
  r2.x = saturate(r3.z / cb0[33].y);
  r2.x = log2(r2.x);
  r2.x = cb0[33].z * r2.x;
  r2.x = exp2(r2.x);
  r2.x = cb0[33].x * r2.x;
  r6.xyz = -cb2[1].xyz + cb0[34].xyz;
  r6.xyz = r2.xxx * r6.xyz + cb2[1].xyz;
  r7.xyz = -cb2[6].xyz + cb0[34].xyz;
  r7.xyz = r2.xxx * r7.xyz + cb2[6].xyz;
  r2.x = dot(r0.xyz, r0.xyz);
  r3.w = rsqrt(r2.x);
  r4.z = r3.w * r2.x;
  r8.xyz = r3.www * r0.xyz;
  r0.x = cmp(0.00999999978 < abs(r0.z));
  r0.x = r0.x ? r0.z : 0.00999999978;
  r0.y = cmp(r0.z < 0);
  r4.w = 9.99999975e-005 * r4.z;
  r4.w = min(1, r4.w);
  r5.w = saturate(abs(r0.z) / cb0[27].w);
  r6.w = -1 + cb0[27].z;
  r5.w = r5.w * r6.w + 1;
  r4.w = log2(r4.w);
  r4.w = 0.600000024 * r4.w;
  r4.w = exp2(r4.w);
  r6.w = 1 + -r5.w;
  r4.w = r4.w * r6.w + r5.w;
  r4.w = r4.w * r0.x;
  r0.x = r0.y ? r4.w : r0.x;
  r0.x = cb2[0].y * r0.x;
  r0.x = max(-127, r0.x);
  r0.y = exp2(-r0.x);
  r0.y = 1 + -r0.y;
  r0.y = cb2[0].x * r0.y;
  r0.x = r0.y / r0.x;
  r0.y = r2.x * r3.w + -cb2[0].w;
  r0.yz = max(float2(0,0), r0.yz);
  r0.y = r0.x * r0.y;
  r4.w = cmp(0 < cb2[5].w);
  r5.w = saturate(dot(r8.xyz, cb2[5].xyz));
  r5.w = log2(r5.w);
  r5.w = cb2[6].w * r5.w;
  r5.w = exp2(r5.w);
  r7.xyz = r7.xyz * r5.www;
  r2.x = r2.x * r3.w + -cb2[7].x;
  r2.x = max(0, r2.x);
  r0.x = r2.x * r0.x;
  r0.x = exp2(-r0.x);
  r0.x = min(1, r0.x);
  r0.x = 1 + -r0.x;
  r7.xyz = r7.xyz * r0.xxx;
  r7.xyz = r4.www ? r7.xyz : 0;
  r0.x = exp2(-r0.y);
  r0.x = min(1, r0.x);
  r7.w = max(cb2[1].w, r0.x);
  r0.x = cmp(0 < cb2[2].w);
  r0.y = cmp(cb2[2].w < r4.z);
  r0.x = r0.y ? r0.x : 0;
  r7.xyzw = r0.xxxx ? float4(0,0,0,1) : r7.xyzw;
  r0.x = 1 + -r7.w;
  r7.xyz = r6.xyz * r0.xxx + r7.xyz;
  r2.xyz = log2(r2.yzw);
  r2.xyz = cb0[27].yyy * r2.xyz;
  r2.xyz = exp2(r2.xyz);
  r2.xyz = cb0[27].xxx * r2.xyz;
  r0.y = dot(float3(0.200000003,0.699999988,0.300000012), r2.xyz);
  r2.w = max(0.200000003, r0.y);
  r0.y = 0.00100000005 + r0.y;
  r0.y = r2.w / r0.y;
  r8.xyz = r2.xyz * r0.yyy;
  r0.z = 0.000166666665 * r0.z;
  r0.z = min(1, r0.z);
  r2.w = log2(r7.w);
  r3.w = 0.600000024 * r2.w;
  r3.w = exp2(r3.w);
  r0.z = r0.z * 0.5 + -0.5;
  r4.z = saturate(cb0[27].x);
  r0.z = r3.w * r0.z + -0.199999988;
  r0.z = r4.z * r0.z + 1;
  r7.xyz = r0.zzz * r7.xyz;
  r7.xyz = r8.xyz * r0.xxx + r7.xyz;
  r9.xy = cb0[28].xy * v0.xy;
  r0.x = cmp(cb0[34].w == 0.000000);
  if (r0.x != 0) {
    r0.z = 0.5;
  }
  if (r0.x == 0) {
    r9.z = 0.000166666665 * r0.w;
    r0.z = t10.SampleLevel(s6_s, r9.xyz, 0).x;
  }
  r3.w = saturate(r3.z);
  r4.z = log2(r3.w);
  r4.z = 1.5 * r4.z;
  r4.z = exp2(r4.z);
  r4.z = r4.z + r4.z;
  r4.z = min(1, r4.z);
  r4.w = r3.z * 0.5 + 0.5;
  r4.w = saturate(1.81818175 * r4.w);
  r5.w = 1.85000002 * r0.z;
  r6.w = r4.w * r4.w;
  r4.w = r6.w * r4.w;
  r6.w = -r0.z * 1.85000002 + 0.300000012;
  r4.w = r4.w * r6.w + r5.w;
  r4.w = r4.w + -r0.z;
  r0.z = cb0[33].w * r4.w + r0.z;
  r4.w = r0.z * r0.z;
  r5.w = 3 * r4.w;
  r6.w = -r4.w * 3 + 1;
  r5.w = r4.z * r6.w + r5.w;
  r0.z = r0.z * 1.98000002 + 0.0199999996;
  r6.w = 1 + -r0.z;
  r0.z = r4.z * r6.w + r0.z;
  r0.z = r0.z * r2.w;
  r0.z = exp2(r0.z);
  r2.w = saturate(cb0[29].w);
  r0.z = r0.z + -r7.w;
  r0.z = r2.w * r0.z + r7.w;
  r4.z = -1 + r5.w;
  r2.w = r2.w * r4.z + 1;
  r4.z = r4.w + r4.w;
  r5.w = 1 + -r0.z;
  r5.w = log2(r5.w);
  r5.w = 0.800000012 * r5.w;
  r5.w = exp2(r5.w);
  r4.z = r5.w * r4.z;
  r10.xyz = -cb0[29].xyz + r6.xyz;
  r10.xyz = r10.xyz * float3(0.699999988,0.699999988,0.699999988) + cb0[29].xyz;
  r10.xyz = r10.xyz * r4.zzz;
  r10.xyz = cb0[29].www * r10.xyz;
  r2.xyz = -r2.xyz * r0.yyy + float3(1,1,1);
  r2.xyz = r2.xyz * float3(0.5,0.5,0.5) + r8.xyz;
  r2.xyz = r10.xyz * r2.xyz;
  r2.xyz = r7.xyz * r2.www + r2.xyz;
  r0.z = saturate(r0.z);
  r0.y = r0.z * r0.z;
  r0.z = saturate(2.22222233 * r4.x);
  r2.w = cb0[32].x + -cb0[32].z;
  r0.z = r0.z * r2.w + cb0[32].z;
  r2.w = r4.y * r4.y;
  r4.x = 1 + -r0.z;
  r0.z = r2.w * r4.x + r0.z;
  r2.w = r0.y * r0.y + -1;
  r2.w = r0.z * r2.w + 1;
  r2.xyz = r0.zzz * r2.xyz;
  r2.xyz = r2.xyz * r1.www;
  r2.xyz = r2.www * r5.xyz + r2.xyz;
  r2.w = cmp(cb0[34].w != 0.000000);
  if (r2.w != 0) {
    if (r0.x != 0) {
      r2.w = 0;
    }
    if (r0.x == 0) {
      r4.x = cmp(480 >= r0.w);
      if (r4.x != 0) {
        r9.w = 0.100000001;
        r5.xyzw = t8.SampleLevel(s6_s, r9.xyw, 0).xyzw;
        r4.x = 0.00833333377 * r0.w;
        r4.x = r4.x * r5.x;
        r7.xyz = r0.www * float3(0.00833333377,0.00833333377,0.00833333377) + float3(-1,-2,-3);
        r8.xyz = r5.yzw + -r5.xyz;
        r10.xyz = cmp(float3(120,240,360) >= r0.www);
        r5.xyz = r7.xyz * r8.xyz + r5.xyz;
        r4.y = r10.z ? r5.y : r5.z;
        r4.y = r10.y ? r5.x : r4.y;
        r2.w = r10.x ? r4.x : r4.y;
      } else {
        r4.x = cmp(960 >= r0.w);
        if (r4.x != 0) {
          r9.z = 0.300000012;
          r5.xyzw = t8.SampleLevel(s6_s, r9.xyz, 0).xyzw;
          r4.x = -480 + r0.w;
          r4.y = cmp(120 >= r4.x);
          if (r4.y != 0) {
            r4.y = 0.00833333377 * r4.x;
            r9.w = 0.100000009;
            r4.z = t8.SampleLevel(s6_s, r9.xyw, 0).w;
            r6.w = r5.x + -r4.z;
            r2.w = r4.y * r6.w + r4.z;
          } else {
            r7.xyz = r4.xxx * float3(0.00833333377,0.00833333377,0.00833333377) + float3(-1,-2,-3);
            r8.xyz = r5.yzw + -r5.xyz;
            r4.xy = cmp(float2(240,360) >= r4.xx);
            r5.xyz = r7.xyz * r8.xyz + r5.xyz;
            r4.y = r4.y ? r5.y : r5.z;
            r2.w = r4.x ? r5.x : r4.y;
          }
        } else {
          r4.x = cmp(1920 >= r0.w);
          if (r4.x != 0) {
            r9.z = 0.5;
            r5.xyzw = t8.SampleLevel(s6_s, r9.xyz, 0).xyzw;
            r4.x = -960 + r0.w;
            r4.y = cmp(240 >= r4.x);
            if (r4.y != 0) {
              r4.y = 0.00416666688 * r4.x;
              r9.w = 0.300000012;
              r4.z = t8.SampleLevel(s6_s, r9.xyw, 0).w;
              r6.w = r5.x + -r4.z;
              r2.w = r4.y * r6.w + r4.z;
            } else {
              r7.xyz = r4.xxx * float3(0.00416666688,0.00416666688,0.00416666688) + float3(-1,-2,-3);
              r8.xyz = r5.yzw + -r5.xyz;
              r4.xy = cmp(float2(480,720) >= r4.xx);
              r5.xyz = r7.xyz * r8.xyz + r5.xyz;
              r4.y = r4.y ? r5.y : r5.z;
              r2.w = r4.x ? r5.x : r4.y;
            }
          } else {
            r4.x = cmp(3840 >= r0.w);
            if (r4.x != 0) {
              r9.z = 0.699999988;
              r5.xyzw = t8.SampleLevel(s6_s, r9.xyz, 0).xyzw;
              r4.x = -1920 + r0.w;
              r4.y = cmp(480 >= r4.x);
              if (r4.y != 0) {
                r4.y = 0.00208333344 * r4.x;
                r9.w = 0.5;
                r4.z = t8.SampleLevel(s6_s, r9.xyw, 0).w;
                r6.w = r5.x + -r4.z;
                r2.w = r4.y * r6.w + r4.z;
              } else {
                r7.xyz = r4.xxx * float3(0.00208333344,0.00208333344,0.00208333344) + float3(-1,-2,-3);
                r8.xyz = r5.yzw + -r5.xyz;
                r4.xy = cmp(float2(960,1440) >= r4.xx);
                r5.xy = r7.xy * r8.xy + r5.xy;
                r4.z = min(1, r7.z);
                r4.z = r4.z * r8.z + r5.z;
                r4.y = r4.y ? r5.y : r4.z;
                r2.w = r4.x ? r5.x : r4.y;
              }
            } else {
              r9.z = 0.899999976;
              r5.xyzw = t8.SampleLevel(s6_s, r9.xyz, 0).xyzw;
              r4.x = -3840 + r0.w;
              r4.y = cmp(960 >= r4.x);
              if (r4.y != 0) {
                r4.y = 0.00104166672 * r4.x;
                r9.w = 0.699999988;
                r4.z = t8.SampleLevel(s6_s, r9.xyw, 0).w;
                r6.w = r5.x + -r4.z;
                r2.w = r4.y * r6.w + r4.z;
              } else {
                r7.xyz = r4.xxx * float3(0.00104166672,0.00104166672,0.00104166672) + float3(-1,-2,-3);
                r8.xyz = r5.yzw + -r5.xyz;
                r4.xy = cmp(float2(1920,2880) >= r4.xx);
                r5.xy = r7.xy * r8.xy + r5.xy;
                r4.z = min(1, r7.z);
                r4.z = r4.z * r8.z + r5.z;
                r4.y = r4.y ? r5.y : r4.z;
                r2.w = r4.x ? r5.x : r4.y;
              }
            }
          }
        }
      }
    }
    r2.w = 1 + -r2.w;
    r4.x = dot(cb0[51].xyz, r3.xyz);
    r4.y = r4.x * 0.5 + 0.5;
    r4.x = -r4.x * 0.600000024 + 1.09000003;
    r4.x = log2(r4.x);
    r4.x = 1.5 * r4.x;
    r4.x = exp2(r4.x);
    r4.x = 12.5663681 * r4.x;
    r4.x = 0.910000026 / r4.x;
    r4.x = r4.x + r4.x;
    r4.x = min(1, r4.x);
    r4.z = -2500 + r0.w;
    r4.z = saturate(9.99999975e-005 * r4.z);
    r0.y = -r0.y * r0.y + 1;
    r0.y = r4.x * r0.y;
    r4.z = r4.z * -0.699999988 + 1;
    r0.y = r4.z * r0.y;
    r5.xyz = cb0[29].xyz * r0.yyy;
    r0.y = r4.y * -0.800000012 + 1;
    r4.z = r2.w * r2.w;
    r5.w = r4.z * r4.z;
    r4.z = -r4.z * r5.w + 1;
    r4.x = log2(r4.x);
    r4.x = 0.300000012 * r4.x;
    r4.x = exp2(r4.x);
    r4.x = r4.z * r4.x;
    r7.xyz = r0.yyy * cb0[30].xyz + float3(-1,-1,-1);
    r7.xyz = r7.xyz * r4.xxx;
    r0.y = 1 + -cb0[30].w;
    r0.y = r4.y * r0.y + cb0[30].w;
    r4.xyz = r0.yyy * r7.xyz + float3(1,1,1);
    r0.y = 0.5 + -r3.w;
    r0.y = max(0, r0.y);
    r0.y = r0.y + r0.y;
    r3.w = r0.y * r0.y;
    r0.y = -r0.y * r0.y + 1;
    r0.y = cb0[28].w * r0.y + r3.w;
    r2.w = log2(r2.w);
    r2.w = 1.5 * r2.w;
    r2.w = exp2(r2.w);
    r0.y = r2.w * r0.y;
    r0.y = r0.y * r0.z;
    r5.xyz = r0.yyy * r5.xyz;
    r5.xyz = r5.xyz * r1.www;
    r2.xyz = r2.xyz * r4.xyz + r5.xyz;
  } else {
    r4.xyz = float3(1,1,1);
  }
  r0.y = cmp(0 < cb0[31].x);
  if (r0.y != 0) {
    r0.y = -250 + r0.w;
    r9.z = 0.000125000006 * r0.y;
    r5.xyz = t9.SampleLevel(s6_s, r9.xyz, 0).xyz;
    r9.z = saturate(r9.z);
    r5.xyz = r9.zzz * r5.xyz;
    r0.y = 3.5 * r4.w;
    r5.xyz = r5.xyz * r0.yyy;
    r6.xyz = float3(-1,-1,-1) + r6.xyz;
    r6.xyz = r6.xyz * float3(0.200000003,0.200000003,0.200000003) + float3(1,1,1);
    r5.xyz = r6.xyz * r5.xyz;
    r2.xyz = r5.xyz * float3(2,2,2) + r2.xyz;
  }
  if (r0.x != 0) {
    r5.xyz = float3(0,0,0);
  }
  if (r0.x == 0) {
    r0.x = cmp(0.00999999978 >= cb0[37].x);
    if (r0.x != 0) {
      r5.xyz = float3(0,0,0);
    }
    r0.y = dot(r3.xyz, cb0[51].xyz);
    r0.y = saturate(r0.y * 0.5 + 0.600000024);
    r0.z = 10 * cb0[37].y;
    r0.y = log2(r0.y);
    r0.y = r0.z * r0.y;
    r0.y = exp2(r0.y);
    r0.z = r3.z * 0.5 + 0.300000012;
    r0.z = saturate(1.25 * r0.z);
    r1.w = cb0[37].z * 4.98999977 + 0.00999999978;
    r0.z = log2(r0.z);
    r0.z = r1.w * r0.z;
    r0.z = exp2(r0.z);
    r0.y = r0.y * r0.z;
    r0.z = 0.00100000005 + cb0[37].w;
    r0.z = saturate(r3.z / r0.z);
    r0.z = r0.z * r0.z;
    r3.xyz = -cb0[36].xyz + cb0[35].xyz;
    r3.xyz = r0.zzz * r3.xyz + cb0[36].xyz;
    r0.y = cb0[37].x * r0.y;
    r0.y = 0.0316227786 * r0.y;
    r3.xyz = r3.xyz * r4.xyz;
    r3.xyz = r3.xyz * r0.yyy;
    r5.xyz = r0.xxx ? r5.xyz : r3.xyz;
  }
  r0.xyz = r5.xyz + r2.xyz;
  r0.xyz = r0.xyz + -r1.xyz;
  r0.xyz = cb0[59].xxx * r0.xyz + r1.xyz;
  r0.w = -cb0[40].x + r0.w;
  r0.w = saturate(r0.w / cb0[40].y);
  r1.x = -1 + cb0[41].w;
  r1.x = r0.w * r1.x + 1;
  r1.yzw = cb0[41].xyz * r0.www;
  r0.xyz = r1.xxx * r0.xyz + r1.yzw;
  r0.w = dot(float3(0.333000004,0.333000004,0.333000004), r0.xyz);
  r1.xyzw = float4(-1,-1,-1,-1) + cb0[57].xyzw;
  r1.xyzw = cb0[58].xyyy * r1.xyzw + float4(1,1,1,1);
  r0.xyz = r0.xyz + -r0.www;
  r0.xyz = r1.xxx * r0.xyz + r0.www;
  o0.xyz = r0.xyz * r1.yzw;
  o0.w = 0;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,0);
  return;
}