// ---- Created with 3Dmigoto v1.3.16 on Fri Oct 09 11:07:49 2026
// Tales of Arise's deferred local lights (UE 4.21 point and spot lights, drawn as light volumes into the scene colour):
// t0 the scene depth, t1-t4 the G-buffer, t5 the light attenuation (its shadow at .z where the light casts them,
// FDeferredLightUniformStruct's ShadowedBits cb1[8].x), cb1 the light (LightPosition cb1[0].xyz, absolute world,
// centimetres). From 0x038D003A.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps).
// RenoDX: Ray Tracing in the Helper's torch shadows (talesofarise/ray_tracing.hpp OnLocalLight; rt_core's local lights,
// twattila/rt_local_lights.hlsli at t42 / b9): where the rays traced this light at the pixel, their visibility in place
// of the game's shadow (one method); elsewhere, or nothing bound, the game's own.
#include "../../twattila/rt_local_lights.hlsli"
Texture2D<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s5_s : register(s5);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[9];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[175];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : SV_POSITION0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v0.xy / v0.ww;
  r0.zw = r0.xy * cb0[58].xy + cb0[58].wz;
  r1.xyzw = t2.SampleLevel(s2_s, r0.zw, 0).xyzw;
  r1.w = 255 * r1.w;
  r1.w = round(r1.w);
  r1.w = (uint)r1.w;
  r2.x = (int)r1.w & 15;
  r2.y = cmp((uint)r2.x < 11);
  if (r2.y == 0) {
    r2.y = log2(r1.z);
    r2.y = 1.60000002 * r2.y;
    r2.y = exp2(r2.y);
    r2.z = (uint)r2.x;
    r2.z = -14 + r2.z;
    r2.z = max(0, r2.z);
    r2.w = 0.699999988 + -r2.y;
    r1.z = r2.z * r2.w + r2.y;
    r1.xy = float2(0,0);
  }
  r2.y = t0.SampleLevel(s0_s, r0.zw, 0).x;
  r2.z = r2.y * cb0[57].x + cb0[57].y;
  r2.y = r2.y * cb0[57].z + -cb0[57].w;
  r2.y = 1 / r2.y;
  r2.y = r2.z + r2.y;
  r0.xy = r2.yy * r0.xy;
  r3.xyzw = cb0[45].xyzw * r0.yyyy;
  r3.xyzw = r0.xxxx * cb0[44].xyzw + r3.xyzw;
  r3.xyzw = r2.yyyy * cb0[46].xyzw + r3.xyzw;
  r3.xyzw = cb0[47].xyzw + r3.xyzw;
  r2.yzw = r3.xyz / r3.www;
  r3.xyz = cb1[0].xyz + -r2.yzw;
  r0.x = dot(r3.xyz, r3.xyz);
  r0.x = rsqrt(r0.x);
  r4.xyz = r3.xyz * r0.xxx;
  r5.xyz = cb1[0].www * r3.xyz;
  r0.y = dot(r5.xyz, r5.xyz);
  r0.y = min(1, r0.y);
  r0.y = 1 + -r0.y;
  r0.y = log2(r0.y);
  r0.y = cb1[1].w * r0.y;
  r0.y = exp2(r0.y);
  r3.w = dot(r4.xyz, cb1[2].xyz);
  r3.w = -cb1[4].x + r3.w;
  r3.w = saturate(cb1[4].y * r3.w);
  r3.w = r3.w * r3.w;
  r4.w = cmp(0 < r0.y);
  r5.x = cmp(0 < r3.w);
  r4.w = r4.w ? r5.x : 0;
  if (r4.w != 0) {
    r5.xyz = t1.SampleLevel(s1_s, r0.zw, 0).xyz;
    r6.xyz = t3.SampleLevel(s3_s, r0.zw, 0).xyz;
    r7.xyzw = t4.SampleLevel(s4_s, r0.zw, 0).xyzw;
    r8.xy = cb0[123].xy * r0.zw;
    r8.xy = (uint2)r8.xy;
    r5.w = (uint)cb0[141].x;
    r6.w = (int)r8.y + (int)r8.x;
    r5.w = (int)r5.w + (int)r6.w;
    r5.w = (int)r5.w & 1;
    r5.xyz = r5.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r6.w = dot(r5.xyz, r5.xyz);
    r6.w = rsqrt(r6.w);
    r5.xyz = r6.www * r5.xyz;
    r1.w = (int)r1.w & 16;
    r7.xyzw = r1.wwww ? float4(0,0,0,0) : r7.xyzw;
    r1.w = 0.0799999982 * r1.y;
    r8.xyz = -r1.yyy * float3(0.0799999982,0.0799999982,0.0799999982) + r6.xyz;
    r8.xyz = r1.xxx * r8.xyz + r1.www;
    r9.xyz = cmp((int3)r2.xxx == int3(5,9,6));
    r1.y = (int)r9.y | (int)r9.x;
    r1.w = cmp(0 < cb0[145].x);
    r6.w = cmp(0 < cb0[174].w);
    r1.w = r1.w ? r6.w : 0;
    r6.w = cmp(0 != cb0[145].x);
    r10.xyz = r6.www ? float3(1,1,1) : r6.xyz;
    r9.yw = r5.ww ? float2(1,0) : float2(0,1);
    r11.xyz = r9.www * r8.xyz;
    r10.xyz = r1.www ? r9.yyy : r10.xyz;
    r11.xyz = r1.www ? r11.xyz : r8.xyz;
    r8.xyz = r1.yyy ? r11.xyz : r8.xyz;
    r6.xyz = r1.yyy ? r10.xyz : r6.xyz;
    r8.xyz = r8.xyz * cb0[127].www + cb0[127].xyz;
    r2.yzw = -cb0[59].xyz + r2.yzw;
    r1.y = dot(r2.yzw, r2.yzw);
    r1.y = rsqrt(r1.y);
    r2.yzw = r2.yzw * r1.yyy;
    r1.y = dot(r5.xyz, r4.xyz);
    r1.w = saturate(r1.y);
    float traced_visibility;
    if (RtLocalLightTraced(int2(v1.xy), cb1[0].xyz * 0.01f, traced_visibility)) {
      r0.z = traced_visibility;
    } else if (cb1[8].x != 0) {
      r0.z = t5.SampleLevel(s5_s, r0.zw, 0).z;
      r0.z = r0.z * r0.z;
      r0.z = min(1, r0.z);
      r0.w = saturate(r1.y * 6 + -0.200000003);
      r0.z = r0.z * r0.w;
    } else {
      r0.z = 1;
    }
    r0.y = r3.w * r0.y;
    r0.y = r0.y * r0.z;
    r1.x = r7.y;
    r0.zw = max(float2(0.0399999991,0.0399999991), r1.xz);
    r1.xy = r0.zw * r0.zw;
    r1.z = dot(r2.yzw, r5.xyz);
    r1.z = r1.z + r1.z;
    r4.xyz = r5.xyz * -r1.zzz + r2.yzw;
    r1.z = cmp(0 < cb1[5].y);
    if (r1.z != 0) {
      r1.z = saturate(cb1[5].y * r0.x);
      r9.yw = r1.zz * float2(0.5,0.5) + r1.xy;
      r9.yw = min(float2(1,1), r9.yw);
      r9.yw = r1.xy / r9.yw;
      r10.xyz = cb1[5].yyy * cb1[2].xyz;
      r11.xyz = -r10.xyz * float3(0.5,0.5,0.5) + r3.xyz;
      r1.z = dot(r4.xyz, r10.xyz);
      r12.xyz = r1.zzz * r4.xyz + -r10.xyz;
      r3.w = dot(r11.xyz, r12.xyz);
      r1.z = r1.z * r1.z;
      r1.z = cb1[5].y * cb1[5].y + -r1.z;
      r1.z = saturate(r3.w / r1.z);
      r3.xyz = r1.zzz * r10.xyz + r11.xyz;
    } else {
      r9.yw = float2(1,1);
    }
    r1.z = cmp(0 < cb1[4].w);
    if (r1.z != 0) {
      r0.x = saturate(cb1[4].w * r0.x);
      r10.xy = r0.xx * float2(0.5,0.5) + r1.xy;
      r10.xy = min(float2(1,1), r10.xy);
      r1.xy = r1.xy / r10.xy;
      r1.xy = r1.xy * r1.xy;
      r9.yw = r9.yw * r1.xy;
      r0.x = dot(r3.xyz, r4.xyz);
      r1.xyz = r0.xxx * r4.xyz + -r3.xyz;
      r0.x = dot(r1.xyz, r1.xyz);
      r0.x = rsqrt(r0.x);
      r0.x = saturate(cb1[4].w * r0.x);
      r3.xyz = r1.xyz * r0.xxx + r3.xyz;
    }
    r0.x = dot(r3.xyz, r3.xyz);
    r0.x = rsqrt(r0.x);
    r1.xyz = r3.xyz * r0.xxx;
    switch (r2.x) {
      case 4 :      r10.xyz = r3.xyz * r0.xxx + -r2.yzw;
      r3.w = dot(r10.xyz, r10.xyz);
      r3.w = rsqrt(r3.w);
      r10.xyz = r10.xyz * r3.www;
      r3.w = saturate(dot(r5.xyz, r1.xyz));
      r5.w = dot(r5.xyz, -r2.yzw);
      r5.w = 9.99999975e-006 + abs(r5.w);
      r5.w = min(1, r5.w);
      r6.w = saturate(dot(r5.xyz, r10.xyz));
      r8.w = saturate(dot(-r2.yzw, r10.xyz));
      r10.xy = r6.ww * r0.zw + -r6.ww;
      r10.xy = r10.xy * r6.ww + float2(1,1);
      r10.xy = r10.xy * r10.xy;
      r10.xy = float2(3.14159274,3.14159274) * r10.xy;
      r10.xy = r0.zw / r10.xy;
      r10.xy = r10.xy * r9.yw;
      r6.w = r8.w * r8.w;
      r6.w = r6.w * 4 + 9.99999975e-006;
      r6.w = rcp(r6.w);
      r8.w = 1 + -r8.w;
      r10.z = r8.w * r8.w;
      r10.z = r10.z * r10.z;
      r10.w = r10.z * r8.w;
      r8.w = -r10.z * r8.w + 1;
      r10.z = r8.w * 0.0399999991 + r10.w;
      r11.x = r10.z * r7.x;
      r6.w = r10.x * r6.w;
      r10.x = -r10.z * r7.x + 1;
      r10.z = sqrt(r0.w);
      r11.y = 1 + -r10.z;
      r11.z = r5.w * r11.y + r10.z;
      r10.z = r3.w * r11.y + r10.z;
      r5.w = r10.z * r5.w;
      r3.w = r3.w * r11.z + r5.w;
      r3.w = rcp(r3.w);
      r3.w = r10.y * r3.w;
      r5.w = saturate(50 * r8.y);
      r11.yzw = r8.www * r8.xyz;
      r10.yzw = r5.www * r10.www + r11.yzw;
      r3.w = 0.5 * r3.w;
      r10.yzw = r3.www * r10.yzw;
      r10.yzw = r6.xyz * float3(0.318309873,0.318309873,0.318309873) + r10.yzw;
      r10.xyz = r10.yzw * r10.xxx;
      r4.xyz = r6.www * r11.xxx + r10.xyz;
      break;
      case 8 :      r10.xyzw = saturate(r7.xyzw);
      r3.w = dot(r5.xyz, r1.xyz);
      r5.w = dot(r5.xyz, -r2.yzw);
      r6.w = dot(r1.xyz, -r2.yzw);
      r8.w = r6.w * 2 + 2;
      r8.w = rsqrt(r8.w);
      r11.x = r5.w + r3.w;
      r11.x = saturate(r11.x * r8.w);
      r6.w = saturate(r8.w * r6.w + r8.w);
      r3.w = saturate(r3.w);
      r5.w = 9.99999975e-006 + abs(r5.w);
      r5.w = min(1, r5.w);
      r6.w = 1 + -r6.w;
      r8.w = r6.w * r6.w;
      r8.w = r8.w * r8.w;
      r11.y = r8.w * r6.w;
      r11.z = 50 * r10.y;
      r11.z = min(1, r11.z);
      r6.w = -r8.w * r6.w + 1;
      r10.xyz = r6.www * r10.xyz;
      r10.xyz = r11.zzz * r11.yyy + r10.xyz;
      r8.w = -r0.w * r11.x + r11.x;
      r8.w = r8.w * r11.x + r0.w;
      r11.z = 4 * r0.w;
      r11.w = r0.w * 4 + 1;
      r11.w = 3.14159274 * r11.w;
      r11.w = rcp(r11.w);
      r11.z = r11.z * r0.w;
      r8.w = r8.w * r8.w;
      r8.w = r11.z / r8.w;
      r8.w = 1 + r8.w;
      r8.w = r11.w * r8.w;
      r11.z = r5.w + r3.w;
      r11.z = -r3.w * r5.w + r11.z;
      r11.z = 4 * r11.z;
      r11.z = rcp(r11.z);
      r8.w = r11.z * r8.w;
      r11.z = saturate(50 * r8.y);
      r12.xyz = r6.www * r8.xyz;
      r11.yzw = r11.zzz * r11.yyy + r12.xyz;
      r6.w = r0.w * r11.x + -r11.x;
      r6.w = r6.w * r11.x + 1;
      r6.w = r6.w * r6.w;
      r6.w = 3.14159274 * r6.w;
      r6.w = r0.w / r6.w;
      r6.w = r6.w * r9.w;
      r11.x = sqrt(r0.w);
      r12.x = 1 + -r11.x;
      r12.y = r5.w * r12.x + r11.x;
      r11.x = r3.w * r12.x + r11.x;
      r5.w = r11.x * r5.w;
      r3.w = r3.w * r12.y + r5.w;
      r3.w = rcp(r3.w);
      r3.w = r6.w * r3.w;
      r3.w = 0.5 * r3.w;
      r11.xyz = r3.www * r11.yzw;
      r10.xyz = r8.www * r10.xyz + -r11.xyz;
      r10.xyz = r10.www * r10.xyz + r11.xyz;
      r4.xyz = r6.xyz * float3(0.318309873,0.318309873,0.318309873) + r10.xyz;
      break;
      case 9 :      r3.w = dot(r5.xyz, r1.xyz);
      r5.w = dot(r5.xyz, -r2.yzw);
      r6.w = dot(r1.xyz, -r2.yzw);
      r8.w = r6.w * 2 + 2;
      r8.w = rsqrt(r8.w);
      r10.x = r5.w + r3.w;
      r10.x = saturate(r10.x * r8.w);
      r6.w = saturate(r8.w * r6.w + r8.w);
      r3.w = saturate(r3.w);
      r5.w = 9.99999975e-006 + abs(r5.w);
      r5.w = min(1, r5.w);
      r8.w = r10.x * r0.w + -r10.x;
      r8.w = r8.w * r10.x + 1;
      r8.w = r8.w * r8.w;
      r8.w = 3.14159274 * r8.w;
      r8.w = r0.w / r8.w;
      r8.w = r8.w * r9.w;
      r10.x = sqrt(r0.w);
      r10.y = 1 + -r10.x;
      r10.z = r5.w * r10.y + r10.x;
      r10.x = r3.w * r10.y + r10.x;
      r5.w = r10.x * r5.w;
      r3.w = r3.w * r10.z + r5.w;
      r3.w = rcp(r3.w);
      r3.w = r8.w * r3.w;
      r5.w = 1 + -r6.w;
      r6.w = r5.w * r5.w;
      r6.w = r6.w * r6.w;
      r8.w = r6.w * r5.w;
      r10.x = saturate(50 * r8.y);
      r5.w = -r6.w * r5.w + 1;
      r10.yzw = r5.www * r8.xyz;
      r10.xyz = r10.xxx * r8.www + r10.yzw;
      r3.w = 0.5 * r3.w;
      r4.xyz = r3.www * r10.xyz;
      break;
      default :
      r4.xyz = float3(0.318309873,0.318309873,0.318309873) * r6.xyz;
      break;
    }
    switch (r2.x) {
      case 4 :      r11.xyz = r3.xyz * r0.xxx + -r2.yzw;
      r2.x = dot(r11.xyz, r11.xyz);
      r2.x = rsqrt(r2.x);
      r11.xyz = r11.xyz * r2.xxx;
      r2.x = saturate(dot(r5.xyz, r1.xyz));
      r3.w = dot(r5.xyz, -r2.yzw);
      r3.w = 9.99999975e-006 + abs(r3.w);
      r3.w = min(1, r3.w);
      r5.w = saturate(dot(r5.xyz, r11.xyz));
      r6.w = saturate(dot(-r2.yzw, r11.xyz));
      r11.xy = r5.ww * r0.zw + -r5.ww;
      r11.xy = r11.xy * r5.ww + float2(1,1);
      r11.xy = r11.xy * r11.xy;
      r11.xy = float2(3.14159274,3.14159274) * r11.xy;
      r11.xy = r0.zw / r11.xy;
      r11.xy = r11.xy * r9.yw;
      r0.z = r6.w * r6.w;
      r0.z = r0.z * 4 + 9.99999975e-006;
      r0.z = rcp(r0.z);
      r5.w = 1 + -r6.w;
      r6.w = r5.w * r5.w;
      r6.w = r6.w * r6.w;
      r8.w = r6.w * r5.w;
      r5.w = -r6.w * r5.w + 1;
      r6.w = r5.w * 0.0399999991 + r8.w;
      r9.y = r6.w * r7.x;
      r0.z = r11.x * r0.z;
      r6.w = -r6.w * r7.x + 1;
      r10.w = sqrt(r0.w);
      r11.x = 1 + -r10.w;
      r11.z = r3.w * r11.x + r10.w;
      r10.w = r2.x * r11.x + r10.w;
      r3.w = r10.w * r3.w;
      r2.x = r2.x * r11.z + r3.w;
      r2.x = rcp(r2.x);
      r2.x = r11.y * r2.x;
      r3.w = saturate(50 * r8.y);
      r11.xyz = r5.www * r8.xyz;
      r11.xyz = r3.www * r8.www + r11.xyz;
      r2.x = 0.5 * r2.x;
      r11.xyz = r2.xxx * r11.xyz;
      r11.xyz = r6.xyz * float3(0.318309873,0.318309873,0.318309873) + r11.xyz;
      r11.xyz = r11.xyz * r6.www;
      r10.xyz = r0.zzz * r9.yyy + r11.xyz;
      break;
      case 8 :      r7.xyzw = saturate(r7.xyzw);
      r0.z = dot(r5.xyz, r1.xyz);
      r2.x = dot(r5.xyz, -r2.yzw);
      r3.w = dot(r1.xyz, -r2.yzw);
      r5.w = r3.w * 2 + 2;
      r5.w = rsqrt(r5.w);
      r6.w = r2.x + r0.z;
      r6.w = saturate(r6.w * r5.w);
      r3.w = saturate(r5.w * r3.w + r5.w);
      r0.z = saturate(r0.z);
      r2.x = 9.99999975e-006 + abs(r2.x);
      r2.x = min(1, r2.x);
      r3.w = 1 + -r3.w;
      r5.w = r3.w * r3.w;
      r5.w = r5.w * r5.w;
      r8.w = r5.w * r3.w;
      r9.y = 50 * r7.y;
      r9.y = min(1, r9.y);
      r3.w = -r5.w * r3.w + 1;
      r7.xyz = r3.www * r7.xyz;
      r7.xyz = r9.yyy * r8.www + r7.xyz;
      r5.w = -r0.w * r6.w + r6.w;
      r5.w = r5.w * r6.w + r0.w;
      r9.y = 4 * r0.w;
      r10.w = r0.w * 4 + 1;
      r10.w = 3.14159274 * r10.w;
      r10.w = rcp(r10.w);
      r9.y = r9.y * r0.w;
      r5.w = r5.w * r5.w;
      r5.w = r9.y / r5.w;
      r5.w = 1 + r5.w;
      r5.w = r10.w * r5.w;
      r9.y = r2.x + r0.z;
      r9.y = -r0.z * r2.x + r9.y;
      r9.y = 4 * r9.y;
      r9.y = rcp(r9.y);
      r5.w = r9.y * r5.w;
      r9.y = saturate(50 * r8.y);
      r11.xyz = r3.www * r8.xyz;
      r11.xyz = r9.yyy * r8.www + r11.xyz;
      r3.w = r0.w * r6.w + -r6.w;
      r3.w = r3.w * r6.w + 1;
      r3.w = r3.w * r3.w;
      r3.w = 3.14159274 * r3.w;
      r3.w = r0.w / r3.w;
      r3.w = r3.w * r9.w;
      r6.w = sqrt(r0.w);
      r8.w = 1 + -r6.w;
      r9.y = r2.x * r8.w + r6.w;
      r6.w = r0.z * r8.w + r6.w;
      r2.x = r6.w * r2.x;
      r0.z = r0.z * r9.y + r2.x;
      r0.z = rcp(r0.z);
      r0.z = r3.w * r0.z;
      r0.z = 0.5 * r0.z;
      r11.xyz = r0.zzz * r11.xyz;
      r7.xyz = r5.www * r7.xyz + -r11.xyz;
      r7.xyz = r7.www * r7.xyz + r11.xyz;
      r10.xyz = r6.xyz * float3(0.318309873,0.318309873,0.318309873) + r7.xyz;
      break;
      case 9 :      r0.z = dot(r5.xyz, r1.xyz);
      r2.x = dot(r5.xyz, -r2.yzw);
      r3.w = dot(r1.xyz, -r2.yzw);
      r5.w = r3.w * 2 + 2;
      r5.w = rsqrt(r5.w);
      r6.x = r2.x + r0.z;
      r6.x = saturate(r6.x * r5.w);
      r3.w = saturate(r5.w * r3.w + r5.w);
      r0.z = saturate(r0.z);
      r2.x = 9.99999975e-006 + abs(r2.x);
      r2.x = min(1, r2.x);
      r5.w = r6.x * r0.w + -r6.x;
      r5.w = r5.w * r6.x + 1;
      r5.w = r5.w * r5.w;
      r5.w = 3.14159274 * r5.w;
      r5.w = r0.w / r5.w;
      r5.w = r5.w * r9.w;
      r6.x = sqrt(r0.w);
      r6.y = 1 + -r6.x;
      r6.z = r2.x * r6.y + r6.x;
      r6.x = r0.z * r6.y + r6.x;
      r2.x = r6.x * r2.x;
      r0.z = r0.z * r6.z + r2.x;
      r0.z = rcp(r0.z);
      r0.z = r5.w * r0.z;
      r2.x = 1 + -r3.w;
      r3.w = r2.x * r2.x;
      r3.w = r3.w * r3.w;
      r5.w = r3.w * r2.x;
      r6.x = saturate(50 * r8.y);
      r2.x = -r3.w * r2.x + 1;
      r6.yzw = r2.xxx * r8.xyz;
      r6.xyz = r6.xxx * r5.www + r6.yzw;
      r0.z = 0.5 * r0.z;
      r10.xyz = r0.zzz * r6.xyz;
      break;
      default :
      r3.xyz = r3.xyz * r0.xxx + -r2.yzw;
      r0.x = dot(r3.xyz, r3.xyz);
      r0.x = rsqrt(r0.x);
      r3.xyz = r3.xyz * r0.xxx;
      r0.x = saturate(dot(r5.xyz, r1.xyz));
      r0.z = dot(r5.xyz, -r2.yzw);
      r0.z = 9.99999975e-006 + abs(r0.z);
      r0.z = min(1, r0.z);
      r1.x = saturate(dot(r5.xyz, r3.xyz));
      r1.y = saturate(dot(-r2.yzw, r3.xyz));
      r1.z = r1.x * r0.w + -r1.x;
      r1.x = r1.z * r1.x + 1;
      r1.x = r1.x * r1.x;
      r1.x = 3.14159274 * r1.x;
      r1.x = r0.w / r1.x;
      r1.x = r1.x * r9.w;
      r0.w = sqrt(r0.w);
      r1.z = 1 + -r0.w;
      r2.x = r0.z * r1.z + r0.w;
      r0.w = r0.x * r1.z + r0.w;
      r0.z = r0.z * r0.w;
      r0.x = r0.x * r2.x + r0.z;
      r0.x = rcp(r0.x);
      r0.x = r1.x * r0.x;
      r0.z = 1 + -r1.y;
      r0.w = r0.z * r0.z;
      r0.w = r0.w * r0.w;
      r1.x = r0.w * r0.z;
      r1.y = saturate(50 * r8.y);
      r0.z = -r0.w * r0.z + 1;
      r2.xyz = r0.zzz * r8.xyz;
      r1.xyz = r1.yyy * r1.xxx + r2.xyz;
      r0.x = 0.5 * r0.x;
      r10.xyz = r0.xxx * r1.xyz;
      break;
    }
    r0.xzw = r10.xyz + r4.xyz;
    r0.y = r1.w * r0.y;
    r1.xyz = cb1[1].xyz * r0.yyy;
    r0.xyz = r1.xyz * r0.xzw;
    r1.w = cmp(cb0[174].w == 0.000000);
    r1.xyz = float3(0.318309873,0.318309873,0.318309873) * r1.xyz;
    r1.x = dot(r1.xyz, float3(0.300000012,0.589999974,0.109999999));
    r1.x = r1.w ? r1.x : 0;
    r1.x = r9.x ? r1.x : 0;
    r2.x = r9.z ? 1.000000 : 0;
    r0.w = 0;
    r3.xyzw = r0.xyzw;
  } else {
    r0.xyz = float3(0,0,0);
    r1.x = 0;
    r2.x = 0;
  }
  r1.y = cmp(cb0[174].w == 0.000000);
  r0.w = r1.y ? r1.x : 0;
  r0.xyzw = r4.wwww ? r3.xyzw : r0.xyzw;
  o0.xyzw = r0.xyzw;
  r2.y = r0.w;
  r2.z = 0;
  o1.xyzw = r2.yxzz;
  return;
}