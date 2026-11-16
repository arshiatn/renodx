// ---- Created with 3Dmigoto v1.3.16 on Fri Oct 09 11:07:52 2026
// Tales of Arise toon lighting (Bandai TO14 deferred lighting of characters and foliage, behind their own stencil
// before lighting00-05): t7 the base pass's light, t6 Bandai's AO, t1-t5 the G-buffer, Bandai's screen-space GI (10 x 6,
// t26) behind the menu's switch cb0[59].w. From 0x90EF2450.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps); in the dump only (level B).
// RenoDX: one AO method: the rays' AO or Path-Traced Ambient in place of Bandai's AO (t41; rt_ambient.hlsli, unbound:
// Bandai's). One GI method: under ours (SI.gtvbgi: GT-VBGI, the rays' bounce light or path tracing) Bandai's GI add
// ((1.2 x GI)^1.5 x the base colour x its AO share) goes and ours (t42, the light arriving) takes its place there
// (shared.h TOA_GAME_GI_SHARE; Game's GI Under the Open Sky keeps its share above the 0.3 floor). Bandai's GI colour
// still tints the lit colour and the rims (terms ours does not replace): its chain keeps running.
// Decompile fix: the shading model 2 select wrote the integer 1 as the float bits 1.4e-45 (flushed to 0, so the
// pixel took the shading model 0 branch); the value 1 here, as the register reads it as a number after.
#include "../shared.h"
#include "../rt_ambient.hlsli"
Texture2D<float4> t26 : register(t26);

TextureCube<float4> t25 : register(t25);

Texture2D<float4> t21 : register(t21);

Texture2D<float4> t20 : register(t20);

Texture2D<float4> t19 : register(t19);

Texture2D<float4> t18 : register(t18);

Texture2D<float4> t14 : register(t14);

Texture2D<float4> t13 : register(t13);

Texture2D<float4> t12 : register(t12);

Texture2D<float4> t11 : register(t11);

Texture2D<float4> t10 : register(t10);

Texture2D<float4> t9 : register(t9);

Texture2D<float4> t8 : register(t8);

Texture2D<float4> t7 : register(t7);

Texture2D<float4> t6 : register(t6);

Texture2D<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s9_s : register(s9);

SamplerState s8_s : register(s8);

SamplerState s7_s : register(s7);

SamplerState s6_s : register(s6);

SamplerState s5_s : register(s5);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
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
  r2.y = (int)r1.w & 16;
  r4.xyzw = r2.yyyy ? float4(0,0,0,0) : r4.xyzw;
  r2.yzw = r3.xyz * cb1[126].www + cb1[126].xyz;
  r3.xyz = t5.Sample(s6_s, v0.xy).xyz;
  r5.x = dot(r1.xyz, cb0[51].xyz);
  r5.y = r5.x * 0.5 + 0.5;
  r5.z = t8.Sample(s7_s, v0.xy).w;
  r5.w = saturate(-r0.w * 0.00033333333 + 1);
  r5.z = -r5.z * r5.w + 1;
  r5.w = max(cb0[44].x, r5.z);
  r6.x = dot(float3(0.300000012,0.400000006,0.300000012), r2.yzw);
  r6.y = r5.y * 18 + 1;
  r6.y = r6.x * r6.y + 1;
  r7.xyz = log2(r2.yzw);
  r6.yzw = r7.xyz * r6.yyy;
  r6.yzw = exp2(r6.yzw);
  r7.x = 0.899999976 * r6.x;
  r6.xyz = -r6.xxx * float3(0.899999976,0.899999976,0.899999976) + r6.yzw;
  r6.xyz = r5.yyy * r6.xyz + r7.xxx;
  r7.xyz = float3(1,1,1) + -r6.xyz;
  r6.xyz = r5.www * r7.xyz + r6.xyz;
  r0.xyz = r6.xyz * r0.xyz;
  r5.w = saturate(r5.y);
  r5.w = 1.25 * r5.w;
  r5.w = r5.w * r5.w;
  r3.w = r3.w + r3.w;
  r3.w = max(0.300000012, r3.w);
  r6.xyz = r0.xyz * r3.www + -r0.xyz;
  r0.xyz = cb0[45].xxx * r6.xyz + r0.xyz;
  r6.x = saturate(dot(r0.xzy, float3(0.300000012,0.300000012,0.400000006)));
  r4.w = 7 * r4.w;
  r4.w = (uint)r4.w;
  r4.w = (uint)r4.w;
  r4.w = min(6, r4.w);
  r7.x = (uint)r4.w;
  r4.w = cmp((int)r7.x == 2);
  r7.y = 1;
  r6.yz = r4.ww ? float2(1,0) : r7.xy;
  if (r6.y == 0) {
    r7.xyz = t18.SampleLevel(s8_s, r6.xx, 0).xyz;
    r8.xyz = t11.SampleLevel(s8_s, r5.ww, 0).xyz;
  } else {
    r4.w = cmp((int)r6.y == 1);
    if (r4.w != 0) {
      r9.xyz = t19.SampleLevel(s8_s, r6.xx, 0).xyz;
      r10.xyz = t12.SampleLevel(s8_s, r5.ww, 0).xyz;
      r4.w = saturate(0.100000001 + r5.x);
      r4.w = r4.w * r4.w;
      r6.w = dot(float3(0.300000012,0.400000006,0.300000012), r9.xyz);
      r7.w = dot(float3(0.300000012,0.400000006,0.300000012), r10.xyz);
      r11.xyz = r6.www + -r9.xyz;
      r7.xyz = r4.www * r11.xyz + r9.xyz;
      r9.xyz = r7.www + -r10.xyz;
      r8.xyz = r4.www * r9.xyz + r10.xyz;
    } else {
      r4.w = cmp((int)r6.y == 2);
      if (r4.w != 0) {
        r9.xyz = t20.SampleLevel(s8_s, r6.xx, 0).xyz;
        r10.xyz = t13.SampleLevel(s8_s, r5.ww, 0).xyz;
        r4.w = saturate(0.100000001 + r5.x);
        r4.w = r4.w * r4.w;
        r6.w = dot(float3(0.300000012,0.400000006,0.300000012), r9.xyz);
        r7.w = dot(float3(0.300000012,0.400000006,0.300000012), r10.xyz);
        r11.xyz = r6.www + -r9.xyz;
        r7.xyz = r4.www * r11.xyz + r9.xyz;
        r9.xyz = r7.www + -r10.xyz;
        r8.xyz = r4.www * r9.xyz + r10.xyz;
      } else {
        r9.xyz = t21.SampleLevel(s8_s, r6.xx, 0).xyz;
        r10.xyz = t14.SampleLevel(s8_s, r5.ww, 0).xyz;
        r4.w = saturate(0.100000001 + r5.x);
        r4.w = r4.w * r4.w;
        r5.x = dot(float3(0.300000012,0.400000006,0.300000012), r9.xyz);
        r6.x = dot(float3(0.300000012,0.400000006,0.300000012), r10.xyz);
        r11.xyz = r5.xxx + -r9.xyz;
        r7.xyz = r4.www * r11.xyz + r9.xyz;
        r9.xyz = r6.xxx + -r10.xyz;
        r8.xyz = r4.www * r9.xyz + r10.xyz;
      }
    }
  }
  r0.xyz = r7.xyz * r0.xyz;
  r4.w = dot(r0.xyz, float3(0.200000003,0.699999988,0.100000001));
  r4.w = r4.w * r5.z;
  r5.x = rsqrt(r5.y);
  r5.x = 1 / r5.x;
  r4.w = r5.x * r4.w;
  r5.x = r5.w * 0.5 + 0.5;
  r4.w = r5.x * r4.w;
  r4.w = saturate(1.20000005 * r4.w);
  r4.w = r4.w * r4.w;
  r4.w = r4.w * r4.w;
  r5.xyz = r2.yzw + -r4.xyz;
  r4.xyz = r4.www * r5.xyz + r4.xyz;
  r4.xyz = r4.xyz + -r2.yzw;
  r2.yzw = cb0[45].yyy * r4.xyz + r2.yzw;
  r3.w = min(1, r3.w);
  r3.w = r3.w * r3.w;
  r3.w = r3.w * -0.5 + 1.5;
  r4.xyz = log2(r2.yzw);
  r4.xyz = r4.xyz * r3.www;
  r4.xyz = exp2(r4.xyz);
  r4.xyz = r4.xyz + -r2.yzw;
  r2.yzw = cb0[45].xxx * r4.xyz + r2.yzw;
  r0.xyz = r2.yzw * r0.xyz;
  r0.xyz = r0.xyz * r8.xyz;
  r0.xyz = r0.xyz * cb0[53].xyz + r3.xyz;
  r3.x = cmp((uint)r6.y >= 3);
  if (r3.x != 0) {
    r1.w = (int)r1.w & 15;
    r2.x = log2(r2.x);
    r2.x = 1.60000002 * r2.x;
    r2.x = exp2(r2.x);
    r1.w = (uint)r1.w;
    r1.w = -14 + r1.w;
    r1.w = max(0, r1.w);
    r3.x = 0.699999988 + -r2.x;
    r1.w = r1.w * r3.x + r2.x;
    r3.xy = cb1[9].xy * r1.yy;
    r3.xy = r1.xx * cb1[8].xy + r3.xy;
    r3.xy = r1.zz * cb1[10].xy + r3.xy;
    r2.x = RayTracedAmbientOcclusion(v0.xy, t6.SampleLevel(s5_s, v0.xy, 0).x);
    r2.x = r2.x * 0.699999988 + 0.300000012;
    r3.z = cmp(cb0[59].w == 0.000000);
    if (r3.z != 0) {
      r4.xyz = float3(0,0,0);
    }
    if (r3.z == 0) {
      r3.zw = float2(0.949999988,0.949999988) * v0.xy;
      r5.xyzw = r3.xyxy * float4(0.150000006,-0.150000006,0.25,-0.25) + r3.zwzw;
      r7.xyz = t26.SampleLevel(s6_s, r5.xy, 0).xyz;
      r5.xyz = t26.SampleLevel(s6_s, r5.zw, 0).xyz;
      r5.xyz = float3(0.300000012,0.300000012,0.300000012) * r5.xyz;
      r5.xyz = r7.xyz * float3(0.400000006,0.400000006,0.400000006) + r5.xyz;
      r3.xy = r3.xy * float2(0.349999994,-0.349999994) + r3.zw;
      r3.xyz = t26.SampleLevel(s6_s, r3.xy, 0).xyz;
      r4.xyz = r3.xyz * float3(0.300000012,0.300000012,0.300000012) + r5.xyz;
    }
    r3.xyz = float3(1.20000005,1.20000005,1.20000005) * r4.xyz;
    r3.xyz = log2(r3.xyz);
    r3.xyz = float3(1.5,1.5,1.5) * r3.xyz;
    r3.xyz = exp2(r3.xyz);
    r3.xyz = r3.xyz * r2.yzw;
    // Our GI in place of Bandai's add (shared.h); its share above the 0.3 floor under the open sky.
    r3.xyz = r3.xyz * TOA_GAME_GI_SHARE(r2.x, r2.x - 0.3f);
    r3.xyz = r0.xyz * r2.xxx + r3.xyz;
    r3.xyz += RayTracedBounceLight(v0.xy, r2.yzw);
    r3.w = dot(r1.xyz, -cb0[51].xyz);
    r3.w = saturate(r3.w * 0.5 + 0.5);
    r4.x = r3.w * -0.5 + 1.5;
    r4.y = dot(float3(0.300000012,0.400000006,0.300000012), cb1[155].xyz);
    r5.xyz = cb1[155].xyz + -r4.yyy;
    r5.xyz = r5.xyz * float3(0.400000006,0.400000006,0.400000006) + r4.yyy;
    r4.z = min(0.300000012, r4.y);
    r4.y = 0.00999999978 + r4.y;
    r4.y = r4.z / r4.y;
    r4.yzw = r5.xyz * r4.yyy;
    r3.w = log2(r3.w);
    r3.w = 1.5 * r3.w;
    r3.w = exp2(r3.w);
    r3.w = r3.w + r3.w;
    r4.yzw = r3.www * r4.yzw;
    r2.yzw = r4.yzw * r2.yzw;
    r2.yzw = r3.xyz * r4.xxx + r2.yzw;
    r3.xyz = v1.xyz * r0.www;
    r3.w = dot(r3.xyz, r3.xyz);
    r3.w = rsqrt(r3.w);
    r3.xyz = r3.xyz * r3.www;
    r3.w = dot(r3.xyz, r1.xyz);
    r3.w = r3.w + r3.w;
    r4.xyz = r1.xyz * -r3.www + r3.xyz;
    r1.w = 0.600000024 * r1.w;
    r1.w = min(1, r1.w);
    r1.w = log2(r1.w);
    r1.w = -r1.w * 1.20000005 + 1;
    r1.w = 6.5 + -r1.w;
    r3.w = saturate(0.000125000006 * r0.w);
    r3.w = 8 * r3.w;
    r1.w = max(r3.w, r1.w);
    r5.xyz = t25.SampleLevel(s9_s, r4.xyz, r1.w).xyz;
    r4.xyz = t25.SampleLevel(s9_s, r4.xyz, 7).xyz;
    r1.w = dot(r4.xzy, float3(0.300000012,0.300000012,0.400000006));
    r1.w = 1 / r1.w;
    r4.xyz = r5.xyz * r1.www;
    r1.w = dot(float3(0.300000012,0.400000006,0.300000012), r2.yzw);
    r3.x = dot(-r3.xyz, r1.xyz);
    r3.x = saturate(1 + -r3.x);
    r3.y = 0.600000024 * r2.x;
    r3.yzw = r3.yyy * r4.xyz;
    r2.x = r3.x * r2.x;
    r1.w = r2.x * r1.w;
    r4.xyz = r1.www * r4.xyz;
    r3.xyz = r3.yzw * r2.yzw + r4.xyz;
    r3.xyz = r2.yzw * float3(0.699999988,0.699999988,0.699999988) + r3.xyz;
    r1.w = (int)r6.y + -3;
    r1.w = (uint)r1.w;
    r1.w = r1.w * -0.266666681 + 1;
    r3.xyz = r3.xyz + -r2.yzw;
    r2.xyz = r1.www * r3.xyz + r2.yzw;
  } else {
    r1.w = dot(r1.xyz, -cb0[51].xyz);
    r1.w = saturate(r1.w * 0.5 + 0.5);
    r1.w = r1.w * r1.w;
    r3.xyz = v1.xyz * r0.www;
    r2.w = dot(r3.xyz, r3.xyz);
    r2.w = rsqrt(r2.w);
    r3.xyz = r3.xyz * r2.www;
    r2.w = dot(r1.xyz, r3.xyz);
    r3.w = 1 + -abs(r2.w);
    r3.w = max(0, r3.w);
    r4.x = r3.w * r3.w;
    r4.x = r4.x * r4.x;
    r3.w = r4.x * r3.w;
    r1.w = r3.w * r1.w;
    r4.xy = cb1[9].xy * r1.yy;
    r4.xy = r1.xx * cb1[8].xy + r4.xy;
    r4.xy = r1.zz * cb1[10].xy + r4.xy;
    r3.w = cmp(cb0[59].w == 0.000000);
    if (r3.w != 0) {
      r5.xyz = float3(0,0,0);
    }
    if (r3.w == 0) {
      r4.zw = float2(0.949999988,0.949999988) * v0.xy;
      r7.xyzw = r4.xyxy * float4(0.150000006,-0.150000006,0.25,-0.25) + r4.zwzw;
      r6.xyw = t26.SampleLevel(s6_s, r7.xy, 0).xyz;
      r7.xyz = t26.SampleLevel(s6_s, r7.zw, 0).xyz;
      r7.xyz = float3(0.300000012,0.300000012,0.300000012) * r7.xyz;
      r6.xyw = r6.xyw * float3(0.400000006,0.400000006,0.400000006) + r7.xyz;
      r4.xy = r4.xy * float2(0.349999994,-0.349999994) + r4.zw;
      r4.xyz = t26.SampleLevel(s6_s, r4.xy, 0).xyz;
      r5.xyz = r4.xyz * float3(0.300000012,0.300000012,0.300000012) + r6.xyw;
    }
    r3.w = saturate(cb0[53].w);
    r3.w = r3.w * -5 + 6;
    r4.xyz = r3.www * r5.xyz;
    r2.w = r2.w + r2.w;
    r5.xyz = r1.xyz * -r2.www + r3.xyz;
    r5.xyz = r1.xyz * float3(3,3,3) + r5.xyz;
    r1.x = dot(r5.xyz, r5.xyz);
    r1.x = rsqrt(r1.x);
    r5.xyz = r5.xyz * r1.xxx;
    r1.x = log2(cb0[56].w);
    r1.x = -r1.x * 1.20000005 + 1;
    r1.x = 6.5 + -r1.x;
    r1.y = saturate(0.000125000006 * r0.w);
    r1.y = 8 * r1.y;
    r1.x = max(r1.x, r1.y);
    r6.xyw = t25.SampleLevel(s9_s, r5.xyz, r1.x).xyz;
    r7.xyz = t25.SampleLevel(s9_s, r5.xyz, 7).xyz;
    r1.x = dot(r7.xzy, float3(0.300000012,0.300000012,0.400000006));
    r1.x = 1 / r1.x;
    r6.xyw = r6.xyw * r1.xxx;
    r1.x = dot(float3(0.300000012,0.400000006,0.300000012), r6.xyw);
    r1.y = dot(-r3.xyz, r5.xyz);
    r1.y = saturate(1 + -r1.y);
    r2.w = cmp(r1.z < 0);
    r3.xyz = cb0[56].xyz + -cb0[55].xyz;
    r3.xyz = abs(r1.zzz) * r3.xyz + cb0[55].xyz;
    r5.xyz = -cb0[55].xyz + cb0[54].xyz;
    r5.xyz = r1.zzz * r5.xyz + cb0[55].xyz;
    r3.xyz = r2.www ? r3.xyz : r5.xyz;
    r5.xyz = r6.xyw * r0.xyz;
    r5.xyz = float3(0.400000006,0.400000006,0.400000006) * r5.xyz;
    r5.xyz = r0.xyz * float3(0.699999988,0.699999988,0.699999988) + r5.xyz;
    r5.xyz = r5.xyz + -r0.xyz;
    r5.xyz = cb0[55].www * r5.xyz + r0.xyz;
    r1.y = cb0[54].w * r1.y;
    r1.x = r1.y * r1.x;
    r1.xyz = r1.xxx * r3.xyz;
    r0.xyz = r1.xyz * r0.xyz + r5.xyz;
    r1.x = RayTracedAmbientOcclusion(v0.xy, t6.SampleLevel(s5_s, v0.xy, 0).x);
    r1.x = r1.x * 0.5 + -0.5;
    r1.x = r6.z * r1.x + 1;
    r1.y = 1.20000005 * r1.w;
    r1.yzw = r1.yyy * r4.xyz;
    r2.xyz = r0.xyz * r1.xxx + r1.yzw;
  }
  r0.xyz = v1.xyz * r0.www;
  r1.x = v1.z * r0.w + cb1[59].z;
  r1.y = dot(r0.xyz, r0.xyz);
  r1.y = rsqrt(r1.y);
  r0.xyz = r1.yyy * r0.xyz;
  r1.y = cb0[31].z * r1.x;
  r1.y = r1.y * -2 + r1.x;
  r1.z = cmp(cb0[32].y < r1.y);
  r1.w = cmp(300000 < r0.w);
  r1.z = (int)r1.w | (int)r1.z;
  r2.w = cmp(cb0[31].y == 1.000000);
  r1.z = r1.z ? r2.w : 0;
  if (r1.z != 0) {
    r1.z = saturate(r0.z);
    r2.w = saturate(-r0.z);
    r2.w = r2.w + -r1.z;
    r1.z = cb0[31].z * r2.w + r1.z;
    r1.z = cmp(r1.z >= 0.100000001);
    if (r1.z != 0) {
      r3.xy = cb1[123].xy / cb1[122].xy;
      r3.xy = v0.xy * r3.xy;
      r3.xyzw = t9.SampleLevel(s6_s, r3.xy, 0).xyzw;
    } else {
      r3.xyzw = float4(0,0,0,1);
    }
    r1.y = -cb0[32].y + r1.y;
    r1.y = saturate(0.000199999995 * r1.y);
    r1.z = -1 + r3.w;
    r4.w = r1.y * r1.z + 1;
    r4.xyz = r1.yyy * r3.xyz;
    r3.xyzw = r1.wwww ? r3.xyzw : r4.xyzw;
    r1.yzw = r3.www * r2.xyz + r3.xyz;
  } else {
    r1.yzw = r2.xyz;
  }
  r2.w = saturate(-r0.z);
  r2.w = cb0[38].x * r2.w;
  r2.w = cmp(0 < r2.w);
  if (r2.w != 0) {
    r1.x = -cb0[38].y + r1.x;
    r1.x = saturate(-r1.x / cb0[38].z);
    r2.w = cmp(0 < r1.x);
    if (r2.w != 0) {
      r3.xy = cb1[123].xy / cb1[122].xy;
      r3.xy = v0.xy * r3.xy;
      r3.xy = t10.SampleLevel(s6_s, r3.xy, 0).xy;
      r2.w = 1 + -r3.y;
      r2.w = max(0, r2.w);
      r2.w = -1 + r2.w;
      r2.w = r1.x * r2.w + 1;
      r1.x = r3.x * r1.x;
      r3.xyz = cb0[39].xyz * r1.xxx;
      r1.yzw = r2.www * r1.yzw + r3.xyz;
    }
  }
  r1.x = cmp(cb0[34].w == 0.000000);
  if (r1.x != 0) {
    r3.xyz = float3(0,0,0);
  }
  if (r1.x == 0) {
    r1.x = cmp(0.00999999978 >= cb0[37].x);
    if (r1.x != 0) {
      r3.xyz = float3(0,0,0);
    }
    r0.x = dot(r0.xyz, cb0[51].xyz);
    r0.x = saturate(r0.x * 0.5 + 0.600000024);
    r0.y = 10 * cb0[37].y;
    r0.x = log2(r0.x);
    r0.x = r0.y * r0.x;
    r0.x = exp2(r0.x);
    r0.y = r0.z * 0.5 + 0.300000012;
    r0.y = saturate(1.25 * r0.y);
    r2.w = cb0[37].z * 4.98999977 + 0.00999999978;
    r0.y = log2(r0.y);
    r0.y = r2.w * r0.y;
    r0.y = exp2(r0.y);
    r0.x = r0.x * r0.y;
    r0.y = 0.00100000005 + cb0[37].w;
    r0.y = saturate(r0.z / r0.y);
    r0.y = r0.y * r0.y;
    r4.xyz = -cb0[36].xyz + cb0[35].xyz;
    r4.xyz = r0.yyy * r4.xyz + cb0[36].xyz;
    r0.x = cb0[37].x * r0.x;
    r0.x = 0.0316227786 * r0.x;
    r0.xyz = r0.xxx * r4.xyz;
    r3.xyz = r1.xxx ? r3.xyz : r0.xyz;
  }
  r0.xyz = r3.xyz + r1.yzw;
  r0.xyz = r0.xyz + -r2.xyz;
  r0.xyz = cb0[59].xxx * r0.xyz + r2.xyz;
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