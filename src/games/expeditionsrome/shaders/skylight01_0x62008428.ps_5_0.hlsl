// ---- Created with 3Dmigoto v1.3.16 on Thu Oct 01 11:55:19 2026
// UE 4.23's sky light pass (ReflectionEnvironmentSkyLighting: the reflection captures, the sky's specular and its
// diffuse light; t7-t11 the G-buffer; cb1 the View: SkyLightColor c162, SkyIrradianceEnvironmentMap c163-c169, the
// light output scale c129.y; cb0[1] the sky light's occlusion tint): t12 the AO.
// Without distance field AO; in the dump only (level B).
// RenoDX: Path-Traced Ambient (ray_tracing.hpp OnSkyLight, shared.h ExrPathAmbient): while the paths bring the sky's
// light (the Light Bounce adds it), the diffuse sky light (SH x SkyLightColor x the AO, and the occlusion tint) is left
// out; the subsurface / two-sided sky and every specular term stay. Unbound: the game's.
#include "../shared.h"
Texture2D<float4> t12 : register(t12);

Texture2D<float4> t11 : register(t11);

Texture2D<float4> t10 : register(t10);

Texture2D<float4> t9 : register(t9);

Texture2D<float4> t8 : register(t8);

Texture2D<float4> t7 : register(t7);

Texture2D<float4> t6 : register(t6);

Texture2D<float4> t5 : register(t5);

Buffer<uint4> t4 : register(t4);

Buffer<uint4> t3 : register(t3);

TextureCubeArray<float4> t2 : register(t2);

TextureCube<float4> t1 : register(t1);

TextureCube<float4> t0 : register(t0);

SamplerState s6_s : register(s6);

SamplerState s5_s : register(s5);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb4 : register(b4)
{
  float4 cb4[1023];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[4];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[2];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[183];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[2];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = cb1[127].zw * v0.xy;
  r0.zw = -cb1[125].xy + v0.xy;
  r1.xy = r0.zw * cb1[126].zw + float2(-0.5,-0.5);
  r1.xy = v0.ww * r1.xy;
  r1.xy = float2(2,-2) * r1.xy;
  r2.xyz = t7.SampleLevel(s0_s, r0.xy, 0).xyz;
  r3.xyzw = t8.SampleLevel(s0_s, r0.xy, 0).xywz;
  r4.xyzw = t9.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r5.xyzw = t10.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r1.zw = cb1[127].xy * r0.xy;
  r1.zw = (uint2)r1.zw;
  r2.w = (uint)cb1[146].x;
  r1.z = (int)r1.w + (int)r1.z;
  r1.z = (int)r2.w + (int)r1.z;
  r1.z = (int)r1.z & 1;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.w = dot(r2.xyz, r2.xyz);
  r1.w = rsqrt(r1.w);
  r6.xyz = r2.xyz * r1.www;
  r7.xy = float2(255,0.0799999982) * r3.zy;
  r2.w = round(r7.x);
  r2.w = (uint)r2.w;
  r7.xz = (int2)r2.ww & int2(15,16);
  r5.xyzw = r7.zzzz ? float4(0,0,0,0) : r5.xyzw;
  r7.zw = cmp((int2)r7.xx == int2(9,5));
  r2.w = r7.z ? 0 : r3.x;
  r8.xyz = -r3.yyy * float3(0.0799999982,0.0799999982,0.0799999982) + r4.xyz;
  r8.xyz = r2.www * r8.xyz + r7.yyy;
  r7.y = (int)r7.z | (int)r7.w;
  r7.z = cmp(0 < cb1[150].x);
  r7.w = cmp(0 < cb1[182].z);
  r7.z = r7.w ? r7.z : 0;
  r7.w = r1.z ? 0 : 1;
  r9.xyz = r8.xyz * r7.www;
  r9.w = r7.w * r3.y;
  r8.w = r3.y;
  r9.xyzw = r7.zzzz ? r9.xyzw : r8.xyzw;
  r7.w = t12.SampleLevel(s6_s, r0.xy, 0).x;
  if (r7.x != 0) {
    r10.x = cmp(0 != cb1[150].x);
    r10.xyz = r10.xxx ? float3(1,1,1) : r4.xyz;
    r1.z = r1.z ? 1 : 0;
    r10.xyz = r7.zzz ? r1.zzz : r10.xyz;
    r10.w = r9.w;
    r11.xyz = r4.xyz;
    r11.w = r8.w;
    r10.xyzw = r7.yyyy ? r10.xyzw : r11.xyzw;
    r4.xyz = -r10.xyz * r2.www + r10.xyz;
    r4.xyz = r4.xyz * cb1[130].www + cb1[130].xyz;
    r1.z = min(1, r7.w);
    r11.xyzw = cmp((int4)r7.xxxx == int4(6,2,3,7));
    if (r11.x != 0) {
      r12.xyz = -r6.xyz;
      r12.w = 1;
      r13.x = dot(cb1[163].xyzw, r12.xyzw);
      r13.y = dot(cb1[164].xyzw, r12.xyzw);
      r13.z = dot(cb1[165].xyzw, r12.xyzw);
      r12.xyzw = r6.xyzz * r6.yzzx;
      r14.x = dot(cb1[166].xyzw, r12.xyzw);
      r14.y = dot(cb1[167].xyzw, r12.xyzw);
      r14.z = dot(cb1[168].xyzw, r12.xyzw);
      r7.z = r6.y * r6.y;
      r7.z = r6.x * r6.x + -r7.z;
      r12.xyz = r14.xyz + r13.xyz;
      r12.xyz = cb1[169].xyz * r7.zzz + r12.xyz;
      r12.xyz = max(float3(0,0,0), r12.xyz);
      r12.xyz = cb1[162].xyz * r12.xyz;
      r13.xyz = r5.xyz * r5.xyz;
      r12.xyz = r12.xyz * r1.zzz;
      r12.xyz = r12.xyz * r13.xyz;
    } else {
      r12.xyz = float3(0,0,0);
    }
    r7.z = (int)r11.z | (int)r11.y;
    r11.xyz = r5.xyz * r5.xyz + r4.xyz;
    r4.xyz = r7.zzz ? r11.xyz : r4.xyz;
    if (r11.w != 0) {
      r11.xyz = cb1[49].xyz * r1.yyy;
      r11.xyz = r1.xxx * cb1[48].xyz + r11.xyz;
      r11.xyz = cb1[50].xyz + r11.xyz;
      r7.z = dot(r11.xyz, r11.xyz);
      r7.z = rsqrt(r7.z);
      r11.xyz = r11.xyz * r7.zzz;
      r7.z = dot(-r11.xyz, r6.xyz);
      r13.xyz = -r6.xyz * r7.zzz + -r11.xyz;
      r8.w = dot(r13.xyz, r13.xyz);
      r9.w = rsqrt(r8.w);
      r14.xyz = r13.xyz * r9.www;
      r9.w = max(0.00392156886, r3.w);
      r9.w = min(1, r9.w);
      r11.x = dot(-r11.xyz, r14.xyz);
      r11.y = dot(r6.xyz, r14.xyz);
      r11.z = abs(r7.z) * -0.156582996 + 1.57079637;
      r11.w = 1 + -abs(r7.z);
      r11.w = sqrt(r11.w);
      r12.w = r11.z * r11.w;
      r13.w = cmp(r7.z >= 0);
      r11.z = -r11.z * r11.w + 3.14159274;
      r11.z = r13.w ? r12.w : r11.z;
      r11.w = abs(r11.y) * -0.156582996 + 1.57079637;
      r12.w = 1 + -abs(r11.y);
      r13.w = sqrt(r12.w);
      r15.x = r13.w * r11.w;
      r15.y = cmp(r11.y >= 0);
      r11.w = -r11.w * r13.w + 3.14159274;
      r11.w = r15.y ? r15.x : r11.w;
      r11.zw = float2(1.57079637,1.57079637) + -r11.zw;
      r11.z = r11.z + -r11.w;
      r11.z = 0.5 * abs(r11.z);
      r11.z = cos(r11.z);
      r15.xyz = -r11.yyy * r6.xyz + r14.xyz;
      r11.w = dot(r15.xyz, r13.xyz);
      r13.x = dot(r15.xyz, r15.xyz);
      r8.w = r13.x * r8.w + 9.99999975e-005;
      r8.w = rsqrt(r8.w);
      r8.w = r11.w * r8.w;
      r13.xy = r8.ww * float2(0.5,17) + float2(0.5,-16.7800007);
      r13.x = saturate(r13.x);
      r8.w = sqrt(r13.x);
      r11.w = r9.w * r9.w;
      r9.w = r9.w * r9.w + 0.200000003;
      r11.w = r11.w * 2 + 0.200000003;
      r13.x = 0.997551024 * r8.w;
      r13.z = -r7.z * r7.z + 1;
      r13.z = sqrt(r13.z);
      r13.w = -0.0699428469 * r7.z;
      r13.x = r13.x * r13.z + r13.w;
      r9.w = r9.w * r8.w;
      r13.zw = float2(1.41421354,3.54490757) * r9.ww;
      r7.z = r11.y + r7.z;
      r9.w = -r13.x * -0.139885694 + r7.z;
      r9.w = r9.w * r9.w;
      r9.w = -0.5 * r9.w;
      r11.y = r13.z * r13.z;
      r9.w = r9.w / r11.y;
      r9.w = 1.44269502 * r9.w;
      r9.w = exp2(r9.w);
      r9.w = r9.w / r13.w;
      r8.w = r9.w * r8.w;
      r9.w = saturate(r11.x * 0.5 + 0.5);
      r9.w = sqrt(r9.w);
      r9.w = 1 + -r9.w;
      r11.y = r9.w * r9.w;
      r11.y = r11.y * r11.y;
      r9.w = r11.y * r9.w;
      r9.w = r9.w * 0.953479409 + 0.0465205684;
      r8.w = r9.w * r8.w;
      r8.w = r8.w * r10.w;
      r8.w = 0.5 * r8.w;
      r11.x = saturate(-r11.x);
      r9.w = 1 + -r11.x;
      r7.z = -0.140000001 + r7.z;
      r7.z = r7.z * r7.z;
      r7.z = -0.5 * r7.z;
      r10.w = r11.w * r11.w;
      r7.z = r7.z / r10.w;
      r7.z = 1.44269502 * r7.z;
      r7.z = exp2(r7.z);
      r10.w = 2.50662827 * r11.w;
      r7.z = r7.z / r10.w;
      r10.w = -r11.z * 0.5 + 1;
      r11.x = r10.w * r10.w;
      r11.x = r11.x * r11.x;
      r10.w = r11.x * r10.w;
      r10.w = r10.w * 0.953479409 + 0.0465205684;
      r11.x = 1 + -r10.w;
      r11.x = r11.x * r11.x;
      r10.w = r11.x * r10.w;
      r11.x = 0.800000012 / r11.z;
      r11.yzw = log2(r10.xyz);
      r11.xyz = r11.xxx * r11.yzw;
      r11.xyz = exp2(r11.xyz);
      r11.w = 1.44269502 * r13.y;
      r11.w = exp2(r11.w);
      r7.z = r11.w * r7.z;
      r7.z = r7.z * r10.w;
      r11.xyz = r7.zzz * r11.xyz;
      r11.xyz = r8.www * r9.www + r11.xyz;
      r7.z = dot(r14.xyz, r14.xyz);
      r7.z = 1 + r7.z;
      r7.z = 0.25 * r7.z;
      r7.z = min(1, r7.z);
      r8.w = r12.w + -r7.z;
      r7.z = r8.w * 0.330000013 + r7.z;
      r2.w = r7.z * r2.w;
      r2.w = 0.318309873 * r2.w;
      r10.xyz = sqrt(r10.xyz);
      r10.xyz = r10.xyz * r2.www + r11.xyz;
      r10.xyz = min(float3(0,0,0), -r10.xyz);
      r4.xyz = float3(-3.14159274,-3.14159274,-3.14159274) * r10.xyz;
    } else {
      r14.xyz = r6.xyz;
    }
    r2.w = cmp((int)r7.x == 8);
    r10.xyz = r5.xyz * r5.www + r4.xyz;
    r4.xyz = r2.www ? r10.xyz : r4.xyz;
    r14.w = 1;
    r10.x = dot(cb1[163].xyzw, r14.xyzw);
    r10.y = dot(cb1[164].xyzw, r14.xyzw);
    r10.z = dot(cb1[165].xyzw, r14.xyzw);
    r11.xyzw = r14.xyzz * r14.yzzx;
    r13.x = dot(cb1[166].xyzw, r11.xyzw);
    r13.y = dot(cb1[167].xyzw, r11.xyzw);
    r13.z = dot(cb1[168].xyzw, r11.xyzw);
    r2.w = r14.y * r14.y;
    r2.w = r14.x * r14.x + -r2.w;
    r10.xyz = r13.xyz + r10.xyz;
    r10.xyz = cb1[169].xyz * r2.www + r10.xyz;
    r10.xyz = max(float3(0,0,0), r10.xyz);
    r10.xyz = cb1[162].xyz * r10.xyz;
    r2.w = 1 + -r1.z;
    r11.xyz = cb0[1].xyz * r2.www;
    r10.xyz = r1.zzz * r10.xyz + r11.xyz;
    // Path-Traced Ambient: the paths' sky in place of this diffuse sky light (shared.h).
    if (ExrPathAmbient()) {
      r10.xyz = 0;
    }
    r4.xyz = r10.xyz * r4.xyz + r12.xyz;
    r4.xyz = cb1[129].yyy * r4.xyz;
    r1.z = cmp(cb1[182].z == 0.000000);
    r2.w = dot(r4.xyz, float3(0.300000012,0.589999974,0.109999999));
    r2.w = r1.z ? r2.w : 0;
    r2.w = (int)r2.w & (int)r7.y;
    o0.w = r1.z ? r2.w : 0;
  } else {
    r4.xyz = float3(0,0,0);
    o0.w = 0;
  }
  r5.zw = cmp((int2)r7.xx != int2(0,7));
  r1.z = r5.w ? r5.z : 0;
  if (r1.z != 0) {
    r1.z = t6.SampleLevel(s0_s, r0.xy, 0).x;
    r2.w = r1.z * cb1[61].x + cb1[61].y;
    r1.z = r1.z * cb1[61].z + -cb1[61].w;
    r1.z = 1 / r1.z;
    r1.z = r2.w + r1.z;
    r2.w = r4.w * 16 + -8;
    r2.w = exp2(r2.w);
    r2.w = -0.00390625 + r2.w;
    r2.w = cb1[129].z * r2.w;
    r8.xyz = r7.yyy ? r9.xyz : r8.xyz;
    r8.xyz = r8.xyz * cb1[131].www + cb1[131].xyz;
    r1.xy = r1.xy * r1.zz;
    r9.xyz = cb1[49].xyz * r1.yyy;
    r9.xyz = r1.xxx * cb1[48].xyz + r9.xyz;
    r9.xyz = r1.zzz * cb1[50].xyz + r9.xyz;
    r9.xyz = cb1[51].xyz + r9.xyz;
    r10.xyz = -cb1[63].xyz + r9.xyz;
    r1.x = dot(r10.xyz, r10.xyz);
    r1.x = rsqrt(r1.x);
    r10.xyz = r10.xyz * r1.xxx;
    r1.xy = cmp(float2(0,0) < cb2[0].yz);
    r1.y = r1.y ? r1.x : 0;
    if (r1.y != 0) {
      r1.y = dot(r6.xyz, r6.xyz);
      r1.y = sqrt(r1.y);
      r6.w = 1;
      r11.x = dot(cb1[163].xyzw, r6.xyzw);
      r11.y = dot(cb1[164].xyzw, r6.xyzw);
      r11.z = dot(cb1[165].xyzw, r6.xyzw);
      r12.xyzw = r6.xyzz * r6.yzzx;
      r13.x = dot(cb1[166].xyzw, r12.xyzw);
      r13.y = dot(cb1[167].xyzw, r12.xyzw);
      r13.z = dot(cb1[168].xyzw, r12.xyzw);
      r4.w = r6.y * r6.y;
      r4.w = r6.x * r6.x + -r4.w;
      r11.xyz = r13.xyz + r11.xyz;
      r11.xyz = cb1[169].xyz * r4.www + r11.xyz;
      r11.xyz = max(float3(0,0,0), r11.xyz);
      r11.xyz = cb1[162].xyz * r11.xyz;
      r4.w = dot(r11.xyz, float3(0.300000012,0.589999974,0.109999999));
      r2.w = r4.w * r1.y + r2.w;
    }
    r3.x = dot(-r10.xyz, r6.xyz);
    r1.y = r3.x + r3.x;
    r10.yzw = r1.yyy * r6.xyz + r10.xyz;
    r3.x = saturate(r3.x);
    r1.y = r3.w * r3.w;
    r4.w = -r3.w * r3.w + 1;
    r5.z = sqrt(r4.w);
    r5.z = r3.w * r3.w + r5.z;
    r4.w = r5.z * r4.w;
    r2.xyz = -r2.xyz * r1.www + r10.yzw;
    r6.yzw = r4.www * r2.xyz + r6.xyz;
    r11.xyzw = t11.Sample(s5_s, r0.xy).xyzw;
    r0.x = 1 + -r11.w;
    r0.y = cmp((int)r7.x == 4);
    r12.xyzw = r11.xyzw * float4(1,1,1,-1) + float4(0,0,0,1);
    r13.xyzw = float4(0,0,0,1) + -r12.xyzw;
    r13.xyzw = r5.xxxx * r13.xyzw + r12.xyzw;
    r12.xyzw = r0.yyyy ? r13.xyzw : r12.xyzw;
    r1.w = r3.x + r7.w;
    r1.w = log2(r1.w);
    r1.y = r1.y * r1.w;
    r1.y = exp2(r1.y);
    r1.y = r1.y + r7.w;
    r1.y = saturate(-1 + r1.y);
    r1.w = r12.w * r1.y;
    r0.zw = (uint2)r0.zw;
    r1.z = r1.z * cb3[3].x + cb3[3].y;
    r1.z = log2(r1.z);
    r1.z = cb3[3].z * r1.z;
    r1.z = max(0, r1.z);
    r1.z = (uint)r1.z;
    r2.x = asint(cb3[1].z) + -1;
    r1.z = min((uint)r2.x, (uint)r1.z);
    r0.zw = (uint2)r0.zw >> asuint(cb3[2].xx);
    r0.w = mad((int)r1.z, asint(cb3[1].y), (int)r0.w);
    r0.z = mad((int)r0.w, asint(cb3[1].x), (int)r0.z);
    r0.z = (int)r0.z + asint(cb3[0].w);
    r0.w = (uint)r0.z << 1;
    r0.w = t3.Load(r0.w).x;
    r0.w = min(asuint(cb3[0].y), (uint)r0.w);
    bitmask.z = ((~(-1 << 31)) << 1) & 0xffffffff;  r0.z = (((uint)r0.z << 1) & bitmask.z) | ((uint)1 & ~bitmask.z);
    r0.z = t3.Load(r0.z).x;
    r1.z = log2(r3.w);
    r1.z = -r1.z * 1.20000005 + 1;
    r2.x = -1 + cb1[170].z;
    r2.y = r2.x + -r1.z;
    r6.x = 0;
    r7.xyz = float3(0,0,0);
    r2.z = r1.w;
    r4.w = 0;
    r5.zw = float2(1,0);
    while (true) {
      r7.w = cmp((uint)r5.w >= (uint)r0.w);
      if (r7.w != 0) break;
      r7.w = cmp(r2.z < 0.00100000005);
      if (r7.w != 0) {
        break;
      }
      r7.w = (int)r0.z + (int)r5.w;
      r7.w = t4.Load(r7.w).x;
      r13.xyz = -cb4[r7.w+0].xyz + r9.xyz;
      r8.w = dot(r13.xyz, r13.xyz);
      r9.w = sqrt(r8.w);
      r11.w = cmp(r9.w < cb4[r7.w+0].w);
      if (r11.w != 0) {
        r9.w = saturate(r9.w / cb4[r7.w+0].w);
        r11.w = dot(r6.yzw, r13.xyz);
        r8.w = -cb4[r7.w+0].w * cb4[r7.w+0].w + r8.w;
        r8.w = r11.w * r11.w + -r8.w;
        r12.w = cmp(r8.w >= 0);
        r8.w = sqrt(r8.w);
        r8.w = r8.w + -r11.w;
        r13.xyz = r8.www * r6.yzw + r13.xyz;
        r13.yzw = -cb4[r7.w+682].xyz + r13.xyz;
        r8.w = r9.w * 2.5 + -1.5;
        r8.w = max(0, r8.w);
        r9.w = r8.w * r8.w;
        r8.w = -r8.w * 2 + 3;
        r13.x = -r9.w * r8.w + 1;
        r13.xyzw = r12.wwww ? r13.xyzw : r6.xyzw;
        r14.xyz = r13.yzw;
        r14.w = cb4[r7.w+341].y;
        r14.xyzw = t2.SampleLevel(s3_s, r14.xyzw, r2.y).xyzw;
        r13.yzw = cb4[r7.w+341].xxx * r14.xyz;
        r13.yzw = r13.yzw * r13.xxx;
        r7.xyz = r13.yzw * r2.zzz + r7.xyz;
        r8.w = -r14.w * r13.x + 1;
        r2.z = r8.w * r2.z;
        r7.w = cb4[r7.w+682].w * r13.x;
        r4.w = r7.w * r5.z + r4.w;
        r7.w = 1 + -r13.x;
        r5.z = r7.w * r5.z;
      }
      r5.w = (int)r5.w + 1;
    }
    r7.xyz = cb1[151].xyz * r7.xyz;
    r1.w = dot(cb1[151].xyz, float3(0.300000012,0.589999974,0.109999999));
    r7.w = r4.w * r1.w;
    if (r1.x != 0) {
      r1.z = cb2[0].x + -r1.z;
      r1.z = -1 + r1.z;
      r13.xyz = t0.SampleLevel(s1_s, r6.yzw, r1.z).xyz;
      r2.y = dot(cb1[162].xyz, float3(0.300000012,0.589999974,0.109999999));
      r2.y = cb2[1].x * r2.y;
      r13.xyz = cb1[162].xyz * r13.xyz;
      r4.w = cmp(0 < cb2[0].w);
      if (r4.w != 0) {
        r6.xyz = t1.SampleLevel(s2_s, r6.yzw, r1.z).xyz;
        r6.xyz = r6.xyz * cb1[162].xyz + -r13.xyz;
        r13.xyz = cb2[0].www * r6.xyz + r13.xyz;
      }
      r1.z = cmp(cb2[0].z < 1);
      r6.xyz = r2.zzz * r13.xyz + r7.xyz;
      r6.w = r2.y * r5.z + r7.w;
      r13.xyz = r1.zzz ? float3(0,0,0) : r13.xyz;
      r7.xyzw = r1.zzzz ? r6.xyzw : r7.xyzw;
    } else {
      r13.xyz = float3(0,0,0);
    }
    r1.z = saturate(r3.w * cb1[172].x + cb1[172].y);
    r2.y = r1.z * -2 + 3;
    r1.z = r1.z * r1.z;
    r1.z = r2.y * r1.z;
    r2.y = max(9.99999975e-005, r7.w);
    r2.y = r2.w / r2.y;
    r2.y = min(cb1[172].z, r2.y);
    r2.y = -1 + r2.y;
    r1.z = r1.z * r2.y + 1;
    r6.xyz = r13.xyz * r2.zzz;
    r6.xyz = r7.xyz * r1.zzz + r6.xyz;
    r6.xyz = cb1[129].yyy * r6.xyz + r12.xyz;
    if (r0.y != 0) {
      r3.y = r3.w;
      r2.yz = t5.SampleLevel(s4_s, r3.xy, 0).xy;
      r0.y = saturate(50 * r8.y);
      r0.y = r2.z * r0.y;
      r1.z = 1 + -r5.x;
      r0.y = r1.z * r0.y;
      r7.xyz = r8.xyz * r2.yyy + r0.yyy;
      r7.xyz = r7.xyz * r6.xyz;
      r3.z = r5.y;
      r2.yz = t5.SampleLevel(s4_s, r3.xz, 0).xy;
      r0.y = r2.y * 0.0399999991 + r2.z;
      r1.z = r0.y * r5.x;
      r0.y = -r0.y * r5.x + 1;
      r5.xzw = r11.xyz * r1.zzz;
      r5.xzw = r7.xyz * r0.yyy + r5.xzw;
      r0.x = r1.z * r0.x;
      r0.x = r0.x * r1.y;
      r0.y = log2(r5.y);
      r0.y = -r0.y * 1.20000005 + 1;
      r1.y = r2.x + -r0.y;
      r10.x = 0;
      r2.xyz = float3(0,0,0);
      r1.z = r0.x;
      r3.yz = float2(0,1);
      r4.w = 0;
      while (true) {
        r6.w = cmp((uint)r4.w >= (uint)r0.w);
        if (r6.w != 0) break;
        r6.w = cmp(r1.z < 0.00100000005);
        if (r6.w != 0) {
          break;
        }
        r6.w = (int)r0.z + (int)r4.w;
        r6.w = t4.Load(r6.w).x;
        r7.xyz = -cb4[r6.w+0].xyz + r9.xyz;
        r7.w = dot(r7.xyz, r7.xyz);
        r8.w = sqrt(r7.w);
        r9.w = cmp(r8.w < cb4[r6.w+0].w);
        if (r9.w != 0) {
          r8.w = saturate(r8.w / cb4[r6.w+0].w);
          r9.w = dot(r10.yzw, r7.xyz);
          r7.w = -cb4[r6.w+0].w * cb4[r6.w+0].w + r7.w;
          r7.w = r9.w * r9.w + -r7.w;
          r11.x = cmp(r7.w >= 0);
          r7.w = sqrt(r7.w);
          r7.w = r7.w + -r9.w;
          r7.xyz = r7.www * r10.yzw + r7.xyz;
          r7.yzw = -cb4[r6.w+682].xyz + r7.xyz;
          r8.w = r8.w * 2.5 + -1.5;
          r8.w = max(0, r8.w);
          r9.w = r8.w * r8.w;
          r8.w = -r8.w * 2 + 3;
          r7.x = -r9.w * r8.w + 1;
          r7.xyzw = r11.xxxx ? r7.xyzw : r10.xyzw;
          r11.xyz = r7.yzw;
          r11.w = cb4[r6.w+341].y;
          r11.xyzw = t2.SampleLevel(s3_s, r11.xyzw, r1.y).xyzw;
          r7.yzw = cb4[r6.w+341].xxx * r11.xyz;
          r7.yzw = r7.yzw * r7.xxx;
          r2.xyz = r7.yzw * r1.zzz + r2.xyz;
          r7.y = -r11.w * r7.x + 1;
          r1.z = r7.y * r1.z;
          r6.w = cb4[r6.w+682].w * r7.x;
          r3.y = r6.w * r3.z + r3.y;
          r6.w = 1 + -r7.x;
          r3.z = r6.w * r3.z;
        }
        r4.w = (int)r4.w + 1;
      }
      r7.xyz = cb1[151].xyz * r2.xyz;
      r7.w = r3.y * r1.w;
      if (r1.x != 0) {
        r0.x = cb2[0].x + -r0.y;
        r0.x = -1 + r0.x;
        r0.yzw = t0.SampleLevel(s1_s, r10.yzw, r0.x).xyz;
        r1.x = dot(cb1[162].xyz, float3(0.300000012,0.589999974,0.109999999));
        r1.x = cb2[1].x * r1.x;
        r0.yzw = cb1[162].xyz * r0.yzw;
        r1.y = cmp(0 < cb2[0].w);
        if (r1.y != 0) {
          r2.xyz = t1.SampleLevel(s2_s, r10.yzw, r0.x).xyz;
          r2.xyz = r2.xyz * cb1[162].xyz + -r0.yzw;
          r0.yzw = cb2[0].www * r2.xyz + r0.yzw;
        }
        r0.x = cmp(cb2[0].z < 1);
        r9.xyz = r1.zzz * r0.yzw + r7.xyz;
        r9.w = r1.x * r3.z + r7.w;
        r0.yzw = r0.xxx ? float3(0,0,0) : r0.yzw;
        r7.xyzw = r0.xxxx ? r9.xyzw : r7.xyzw;
      } else {
        r0.yzw = float3(0,0,0);
      }
      r0.x = saturate(r5.y * cb1[172].x + cb1[172].y);
      r1.x = r0.x * -2 + 3;
      r0.x = r0.x * r0.x;
      r0.xyzw = r1.xzzz * r0.xyzw;
      r1.x = max(9.99999975e-005, r7.w);
      r1.x = r2.w / r1.x;
      r1.x = min(cb1[172].z, r1.x);
      r1.x = -1 + r1.x;
      r0.x = r0.x * r1.x + 1;
      r0.xyz = r7.xyz * r0.xxx + r0.yzw;
      r0.xyz = cb1[129].yyy * r0.xyz + r5.xzw;
    } else {
      r1.xy = t5.SampleLevel(s4_s, r3.xw, 0).xy;
      r0.w = saturate(50 * r8.y);
      r0.w = r0.w * r1.y;
      r1.xyz = r8.xyz * r1.xxx + r0.www;
      r0.xyz = r6.xyz * r1.xyz;
    }
    r0.xyz = min(float3(0,0,0), -r0.xyz);
    o0.xyz = r4.xyz + -r0.xyz;
  } else {
    o0.xyz = r4.xyz;
  }
  return;
}