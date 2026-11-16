// ---- Created with 3Dmigoto v1.3.16 on Thu Oct 01 11:55:19 2026
// UE 4.23's sky light pass (ReflectionEnvironmentSkyLighting: the reflection captures, the sky's specular and its
// diffuse light; t7-t11 the G-buffer; cb1 the View: SkyLightColor c162, SkyIrradianceEnvironmentMap c163-c169, the
// light output scale c129.y; cb0[1] the sky light's occlusion tint): t13 the AO, t6 the distance field AO's bent normal.
// Traced: the menu and epic scenes of tools\nontw\expeditions\g1 (level A).
// RenoDX: Path-Traced Ambient (ray_tracing.hpp OnSkyLight, shared.h ExrPathAmbient): while the paths bring the sky's
// light (the Light Bounce adds it), the diffuse sky light (SH x SkyLightColor x the AO, and the occlusion tint) is left
// out; the subsurface / two-sided sky and every specular term stay. Unbound: the game's.
#include "../shared.h"
Texture2D<float4> t13 : register(t13);

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

SamplerState s7_s : register(s7);

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
  float4 cb0[10];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = cb1[127].zw * v0.xy;
  r0.zw = -cb1[125].xy + v0.xy;
  r1.xy = r0.zw * cb1[126].zw + float2(-0.5,-0.5);
  r1.xy = v0.ww * r1.xy;
  r1.xy = float2(2,-2) * r1.xy;
  r2.xyz = t8.SampleLevel(s0_s, r0.xy, 0).xyz;
  r3.xyzw = t9.SampleLevel(s0_s, r0.xy, 0).xywz;
  r4.xyzw = t10.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r5.xyzw = t11.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r1.z = t7.SampleLevel(s0_s, r0.xy, 0).x;
  r1.w = r1.z * cb1[61].x + cb1[61].y;
  r1.z = r1.z * cb1[61].z + -cb1[61].w;
  r1.z = 1 / r1.z;
  r1.z = r1.w + r1.z;
  r6.xy = cb1[127].xy * r0.xy;
  r6.xy = (uint2)r6.xy;
  r1.w = (uint)cb1[146].x;
  r2.w = (int)r6.y + (int)r6.x;
  r1.w = (int)r1.w + (int)r2.w;
  r1.w = (int)r1.w & 1;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.w = dot(r2.xyz, r2.xyz);
  r2.w = rsqrt(r2.w);
  r6.xyz = r2.xyz * r2.www;
  r7.xy = float2(255,0.0799999982) * r3.zy;
  r7.x = round(r7.x);
  r7.x = (uint)r7.x;
  r7.xz = (int2)r7.xx & int2(15,16);
  r5.xyzw = r7.zzzz ? float4(0,0,0,0) : r5.xyzw;
  r7.zw = cmp((int2)r7.xx == int2(9,5));
  r8.x = r7.z ? 0 : r3.x;
  r8.yzw = -r3.yyy * float3(0.0799999982,0.0799999982,0.0799999982) + r4.xyz;
  r9.xyz = r8.xxx * r8.yzw + r7.yyy;
  r7.y = (int)r7.z | (int)r7.w;
  r7.z = cmp(0 < cb1[150].x);
  r7.w = cmp(0 < cb1[182].z);
  r7.z = r7.w ? r7.z : 0;
  r7.w = r1.w ? 0 : 1;
  r10.xyz = r9.xyz * r7.www;
  r10.w = r7.w * r3.y;
  r9.w = r3.y;
  r10.xyzw = r7.zzzz ? r10.xyzw : r9.xyzw;
  r7.w = t13.SampleLevel(s7_s, r0.xy, 0).x;
  r8.yz = -cb1[125].xy * cb1[127].zw + r0.xy;
  r8.yz = min(cb0[6].xy, r8.yz);
  r11.xy = float2(0.5,0.5) * cb1[127].xy;
  r11.xy = floor(r11.xy);
  r12.xy = float2(1,1) / r11.xy;
  r11.zw = r8.yz * r11.xy + float2(-0.5,-0.5);
  r11.zw = floor(r11.zw);
  r11.zw = r11.zw / r11.xy;
  r11.zw = r12.xy * float2(0.5,0.5) + r11.zw;
  r8.yz = -r11.zw + r8.yz;
  r13.xy = r8.yz * r11.xy;
  r14.xyzw = t6.SampleLevel(s5_s, r11.zw, 0).xyzw;
  r12.z = 0;
  r15.xyzw = r11.zwzw + r12.xzzy;
  r16.xyzw = t6.SampleLevel(s5_s, r15.xy, 0).xyzw;
  r15.xyzw = t6.SampleLevel(s5_s, r15.zw, 0).xyzw;
  r11.zw = r11.zw + r12.xy;
  r12.xyzw = t6.SampleLevel(s5_s, r11.zw, 0).xyzw;
  r8.yz = -r8.zy * r11.yx + float2(1,1);
  r11.x = r8.y * r8.z;
  r11.yz = r8.yz * r13.xy;
  r11.w = r13.y * r13.x;
  r13.x = r14.w;
  r13.y = r16.w;
  r13.z = r15.w;
  r13.w = r12.w;
  r13.xyzw = r13.xyzw + -r1.zzzz;
  r13.xyzw = float4(9.99999975e-005,9.99999975e-005,9.99999975e-005,9.99999975e-005) + abs(r13.xyzw);
  r13.xyzw = float4(1,1,1,1) / r13.xyzw;
  r11.xyzw = r13.xyzw * r11.xyzw;
  r8.y = dot(r11.xyzw, float4(1,1,1,1));
  r8.y = 1 / r8.y;
  r13.xyz = r11.yyy * r16.xyz;
  r13.xyz = r11.xxx * r14.xyz + r13.xyz;
  r11.xyz = r11.zzz * r15.xyz + r13.xyz;
  r11.xyz = r11.www * r12.xyz + r11.xyz;
  r8.yzw = r11.xyz * r8.yyy;
  if (r7.x != 0) {
    r11.x = cmp(0 != cb1[150].x);
    r11.xyz = r11.xxx ? float3(1,1,1) : r4.xyz;
    r1.w = r1.w ? 1 : 0;
    r11.xyz = r7.zzz ? r1.www : r11.xyz;
    r11.w = r10.w;
    r12.xyz = r4.xyz;
    r12.w = r9.w;
    r11.xyzw = r7.yyyy ? r11.xyzw : r12.xyzw;
    r4.xyz = -r11.xyz * r8.xxx + r11.xyz;
    r4.xyz = r4.xyz * cb1[130].www + cb1[130].xyz;
    r1.w = dot(r8.yzw, r8.yzw);
    r1.w = sqrt(r1.w);
    r7.z = max(9.99999975e-006, r1.w);
    r12.xyz = r8.yzw / r7.zzz;
    r13.xyz = r2.xyz * r2.www + -r12.xyz;
    r13.xyz = r1.www * r13.xyz + r12.xyz;
    r7.z = dot(r12.xyz, r6.xyz);
    r9.w = 1 + -r7.z;
    r7.z = r1.w * r9.w + r7.z;
    r1.w = r1.w * 10 + -5;
    r1.w = -cb0[8].x * r1.w;
    r1.w = 1.44269502 * r1.w;
    r1.w = exp2(r1.w);
    r1.w = 1 + r1.w;
    r1.w = 1 / r1.w;
    r1.w = saturate(r1.w * cb0[8].y + cb0[8].z);
    r1.w = log2(r1.w);
    r1.w = cb0[8].w * r1.w;
    r1.w = exp2(r1.w);
    r9.w = 1 + -r1.w;
    r1.w = cb0[1].w * r9.w + r1.w;
    r9.w = cmp(cb0[9].x == 0.000000);
    r10.w = min(1, r7.w);
    r12.x = min(r10.w, r1.w);
    r1.w = r10.w * r1.w;
    r1.w = r9.w ? r12.x : r1.w;
    r12.xyzw = cmp((int4)r7.xxxx == int4(6,2,3,7));
    if (r12.x != 0) {
      r14.xyz = -r6.xyz;
      r14.w = 1;
      r15.x = dot(cb1[163].xyzw, r14.xyzw);
      r15.y = dot(cb1[164].xyzw, r14.xyzw);
      r15.z = dot(cb1[165].xyzw, r14.xyzw);
      r14.xyzw = r6.xyzz * r6.yzzx;
      r16.x = dot(cb1[166].xyzw, r14.xyzw);
      r16.y = dot(cb1[167].xyzw, r14.xyzw);
      r16.z = dot(cb1[168].xyzw, r14.xyzw);
      r9.w = r6.y * r6.y;
      r9.w = r6.x * r6.x + -r9.w;
      r14.xyz = r16.xyz + r15.xyz;
      r14.xyz = cb1[169].xyz * r9.www + r14.xyz;
      r14.xyz = max(float3(0,0,0), r14.xyz);
      r14.xyz = cb1[162].xyz * r14.xyz;
      r15.xyz = r5.xyz * r5.xyz;
      r14.xyz = r14.xyz * r1.www;
      r14.xyz = r14.xyz * r15.xyz;
    } else {
      r14.xyz = float3(0,0,0);
    }
    r9.w = (int)r12.z | (int)r12.y;
    r12.xyz = r5.xyz * r5.xyz + r4.xyz;
    r4.xyz = r9.www ? r12.xyz : r4.xyz;
    if (r12.w != 0) {
      r12.xyz = cb1[49].xyz * r1.yyy;
      r12.xyz = r1.xxx * cb1[48].xyz + r12.xyz;
      r12.xyz = cb1[50].xyz + r12.xyz;
      r9.w = dot(r12.xyz, r12.xyz);
      r9.w = rsqrt(r9.w);
      r12.xyz = r12.xyz * r9.www;
      r9.w = dot(-r12.xyz, r6.xyz);
      r15.xyz = -r6.xyz * r9.www + -r12.xyz;
      r10.w = dot(r15.xyz, r15.xyz);
      r12.w = rsqrt(r10.w);
      r13.xyz = r15.xyz * r12.www;
      r12.w = max(0.00392156886, r3.w);
      r12.w = min(1, r12.w);
      r12.x = dot(-r12.xyz, r13.xyz);
      r12.y = dot(r6.xyz, r13.xyz);
      r12.z = abs(r9.w) * -0.156582996 + 1.57079637;
      r14.w = 1 + -abs(r9.w);
      r14.w = sqrt(r14.w);
      r15.w = r14.w * r12.z;
      r16.x = cmp(r9.w >= 0);
      r12.z = -r12.z * r14.w + 3.14159274;
      r12.z = r16.x ? r15.w : r12.z;
      r12.z = 1.57079637 + -r12.z;
      r14.w = abs(r12.y) * -0.156582996 + 1.57079637;
      r15.w = 1 + -abs(r12.y);
      r16.x = sqrt(r15.w);
      r16.y = r16.x * r14.w;
      r16.z = cmp(r12.y >= 0);
      r14.w = -r14.w * r16.x + 3.14159274;
      r14.w = r16.z ? r16.y : r14.w;
      r14.w = 1.57079637 + -r14.w;
      r12.z = -r14.w + r12.z;
      r12.z = 0.5 * abs(r12.z);
      r12.z = cos(r12.z);
      r16.xyz = -r12.yyy * r6.xyz + r13.xyz;
      r14.w = dot(r16.xyz, r15.xyz);
      r15.x = dot(r16.xyz, r16.xyz);
      r10.w = r15.x * r10.w + 9.99999975e-005;
      r10.w = rsqrt(r10.w);
      r10.w = r14.w * r10.w;
      r15.xy = r10.ww * float2(0.5,17) + float2(0.5,-16.7800007);
      r15.x = saturate(r15.x);
      r10.w = sqrt(r15.x);
      r14.w = r12.w * r12.w;
      r12.w = r12.w * r12.w + 0.200000003;
      r14.w = r14.w * 2 + 0.200000003;
      r15.x = 0.997551024 * r10.w;
      r15.z = -r9.w * r9.w + 1;
      r15.z = sqrt(r15.z);
      r16.x = -0.0699428469 * r9.w;
      r15.x = r15.x * r15.z + r16.x;
      r12.w = r12.w * r10.w;
      r16.xy = float2(1.41421354,3.54490757) * r12.ww;
      r9.w = r12.y + r9.w;
      r12.y = -r15.x * -0.139885694 + r9.w;
      r12.y = r12.y * r12.y;
      r12.y = -0.5 * r12.y;
      r12.w = r16.x * r16.x;
      r12.y = r12.y / r12.w;
      r12.y = 1.44269502 * r12.y;
      r12.y = exp2(r12.y);
      r12.y = r12.y / r16.y;
      r10.w = r12.y * r10.w;
      r12.y = saturate(r12.x * 0.5 + 0.5);
      r12.y = sqrt(r12.y);
      r12.y = 1 + -r12.y;
      r12.w = r12.y * r12.y;
      r12.w = r12.w * r12.w;
      r12.y = r12.w * r12.y;
      r12.y = r12.y * 0.953479409 + 0.0465205684;
      r10.w = r12.y * r10.w;
      r10.w = r10.w * r11.w;
      r10.w = 0.5 * r10.w;
      r12.x = saturate(-r12.x);
      r11.w = 1 + -r12.x;
      r9.w = -0.140000001 + r9.w;
      r9.w = r9.w * r9.w;
      r9.w = -0.5 * r9.w;
      r12.x = r14.w * r14.w;
      r9.w = r9.w / r12.x;
      r9.w = 1.44269502 * r9.w;
      r9.w = exp2(r9.w);
      r12.x = 2.50662827 * r14.w;
      r9.w = r9.w / r12.x;
      r12.x = -r12.z * 0.5 + 1;
      r12.y = r12.x * r12.x;
      r12.y = r12.y * r12.y;
      r12.x = r12.y * r12.x;
      r12.x = r12.x * 0.953479409 + 0.0465205684;
      r12.y = 1 + -r12.x;
      r12.y = r12.y * r12.y;
      r12.x = r12.y * r12.x;
      r12.y = 0.800000012 / r12.z;
      r16.xyz = log2(r11.xyz);
      r12.yzw = r16.xyz * r12.yyy;
      r12.yzw = exp2(r12.yzw);
      r14.w = 1.44269502 * r15.y;
      r14.w = exp2(r14.w);
      r9.w = r14.w * r9.w;
      r9.w = r9.w * r12.x;
      r12.xyz = r9.www * r12.yzw;
      r12.xyz = r10.www * r11.www + r12.xyz;
      r9.w = dot(r13.xyz, r13.xyz);
      r9.w = 1 + r9.w;
      r9.w = 0.25 * r9.w;
      r9.w = min(1, r9.w);
      r10.w = r15.w + -r9.w;
      r9.w = r10.w * 0.330000013 + r9.w;
      r8.x = r9.w * r8.x;
      r8.x = 0.318309873 * r8.x;
      r11.xyz = sqrt(r11.xyz);
      r11.xyz = r11.xyz * r8.xxx + r12.xyz;
      r11.xyz = min(float3(0,0,0), -r11.xyz);
      r4.xyz = float3(-3.14159274,-3.14159274,-3.14159274) * r11.xyz;
    }
    r8.x = cmp((int)r7.x == 8);
    r11.xyz = r5.xyz * r5.www + r4.xyz;
    r4.xyz = r8.xxx ? r11.xyz : r4.xyz;
    r13.w = 1;
    r11.x = dot(cb1[163].xyzw, r13.xyzw);
    r11.y = dot(cb1[164].xyzw, r13.xyzw);
    r11.z = dot(cb1[165].xyzw, r13.xyzw);
    r12.xyzw = r13.xyzz * r13.yzzx;
    r15.x = dot(cb1[166].xyzw, r12.xyzw);
    r15.y = dot(cb1[167].xyzw, r12.xyzw);
    r15.z = dot(cb1[168].xyzw, r12.xyzw);
    r5.z = r13.y * r13.y;
    r5.z = r13.x * r13.x + -r5.z;
    r11.xyz = r15.xyz + r11.xyz;
    r11.xyz = cb1[169].xyz * r5.zzz + r11.xyz;
    r11.xyz = max(float3(0,0,0), r11.xyz);
    r11.xyz = cb1[162].xyz * r11.xyz;
    r5.z = r1.w * r7.z;
    r1.w = 1 + -r1.w;
    r12.xyz = cb0[1].xyz * r1.www;
    r11.xyz = r5.zzz * r11.xyz + r12.xyz;
    // Path-Traced Ambient: the paths' sky in place of this diffuse sky light (shared.h).
    if (ExrPathAmbient()) {
      r11.xyz = 0;
    }
    r4.xyz = r11.xyz * r4.xyz + r14.xyz;
    r4.xyz = cb1[129].yyy * r4.xyz;
    r1.w = cmp(cb1[182].z == 0.000000);
    r5.z = dot(r4.xyz, float3(0.300000012,0.589999974,0.109999999));
    r5.z = r1.w ? r5.z : 0;
    r5.z = (int)r5.z & (int)r7.y;
    o0.w = r1.w ? r5.z : 0;
  } else {
    r4.xyz = float3(0,0,0);
    o0.w = 0;
  }
  r5.zw = cmp((int2)r7.xx != int2(0,7));
  r1.w = r5.w ? r5.z : 0;
  if (r1.w != 0) {
    r1.w = r4.w * 16 + -8;
    r1.w = exp2(r1.w);
    r1.w = -0.00390625 + r1.w;
    r1.w = cb1[129].z * r1.w;
    r9.xyz = r7.yyy ? r10.xyz : r9.xyz;
    r9.xyz = r9.xyz * cb1[131].www + cb1[131].xyz;
    r1.xy = r1.xy * r1.zz;
    r10.xyz = cb1[49].xyz * r1.yyy;
    r10.xyz = r1.xxx * cb1[48].xyz + r10.xyz;
    r10.xyz = r1.zzz * cb1[50].xyz + r10.xyz;
    r10.xyz = cb1[51].xyz + r10.xyz;
    r11.xyz = -cb1[63].xyz + r10.xyz;
    r1.x = dot(r11.xyz, r11.xyz);
    r1.x = rsqrt(r1.x);
    r11.xyz = r11.xyz * r1.xxx;
    r1.xy = cmp(float2(0,0) < cb2[0].yz);
    r1.y = r1.y ? r1.x : 0;
    if (r1.y != 0) {
      r1.y = dot(r8.yzw, r8.yzw);
      r1.y = sqrt(r1.y);
      r6.w = 1;
      r12.x = dot(cb1[163].xyzw, r6.xyzw);
      r12.y = dot(cb1[164].xyzw, r6.xyzw);
      r12.z = dot(cb1[165].xyzw, r6.xyzw);
      r13.xyzw = r6.xyzz * r6.yzzx;
      r14.x = dot(cb1[166].xyzw, r13.xyzw);
      r14.y = dot(cb1[167].xyzw, r13.xyzw);
      r14.z = dot(cb1[168].xyzw, r13.xyzw);
      r4.w = r6.y * r6.y;
      r4.w = r6.x * r6.x + -r4.w;
      r12.xyz = r14.xyz + r12.xyz;
      r12.xyz = cb1[169].xyz * r4.www + r12.xyz;
      r12.xyz = max(float3(0,0,0), r12.xyz);
      r12.xyz = cb1[162].xyz * r12.xyz;
      r4.w = dot(r12.xyz, float3(0.300000012,0.589999974,0.109999999));
      r1.w = r4.w * r1.y + r1.w;
    }
    r3.x = dot(-r11.xyz, r6.xyz);
    r1.y = r3.x + r3.x;
    r11.yzw = r1.yyy * r6.xyz + r11.xyz;
    r3.x = saturate(r3.x);
    r1.y = r3.w * r3.w;
    r4.w = -r3.w * r3.w + 1;
    r5.z = sqrt(r4.w);
    r5.z = r3.w * r3.w + r5.z;
    r4.w = r5.z * r4.w;
    r2.xyz = -r2.xyz * r2.www + r11.yzw;
    r2.yzw = r4.www * r2.xyz + r6.xyz;
    r6.xyzw = t12.Sample(s6_s, r0.xy).xyzw;
    r0.x = 1 + -r6.w;
    r5.zw = cmp((int2)r7.xx == int2(4,6));
    r12.xyzw = r6.xyzw * float4(1,1,1,-1) + float4(0,0,0,1);
    r13.xyzw = float4(0,0,0,1) + -r12.xyzw;
    r13.xyzw = r5.xxxx * r13.xyzw + r12.xyzw;
    r12.xyzw = r5.zzzz ? r13.xyzw : r12.xyzw;
    r0.y = r3.x + r7.w;
    r0.y = log2(r0.y);
    r0.y = r1.y * r0.y;
    r0.y = exp2(r0.y);
    r0.y = r0.y + r7.w;
    r0.y = saturate(-1 + r0.y);
    r1.y = r12.w * r0.y;
    r0.zw = (uint2)r0.zw;
    r1.z = r1.z * cb3[3].x + cb3[3].y;
    r1.z = log2(r1.z);
    r1.z = cb3[3].z * r1.z;
    r1.z = max(0, r1.z);
    r1.z = (uint)r1.z;
    r4.w = asint(cb3[1].z) + -1;
    r1.z = min((uint)r4.w, (uint)r1.z);
    r0.zw = (uint2)r0.zw >> asuint(cb3[2].xx);
    r0.w = mad((int)r1.z, asint(cb3[1].y), (int)r0.w);
    r0.z = mad((int)r0.w, asint(cb3[1].x), (int)r0.z);
    r0.z = (int)r0.z + asint(cb3[0].w);
    r0.w = (uint)r0.z << 1;
    r0.w = t3.Load(r0.w).x;
    r0.w = min(asuint(cb3[0].y), (uint)r0.w);
    bitmask.z = ((~(-1 << 31)) << 1) & 0xffffffff;  r0.z = (((uint)r0.z << 1) & bitmask.z) | ((uint)1 & ~bitmask.z);
    r0.z = t3.Load(r0.z).x;
    r1.z = cmp(0 < cb0[0].x);
    if (r1.z != 0) {
      r4.w = dot(r8.yzw, r8.yzw);
      r4.w = sqrt(r4.w);
      if (cb1[171].x == 0) {
        r6.w = r4.w;
      } else {
        if (r5.w != 0) {
          r6.w = r4.w;
        } else {
          r5.w = max(0.100000001, r3.w);
          r7.x = cb0[0].y * r4.w;
          r7.y = 3.14159274 * r7.x;
          r7.z = dot(r8.yzw, r2.yzw);
          r7.w = max(0.00100000005, r4.w);
          r7.z = r7.z / r7.w;
          r7.w = 1 + -abs(r7.z);
          r7.w = sqrt(r7.w);
          r8.x = abs(r7.z) * -0.0187292993 + 0.0742610022;
          r8.x = r8.x * abs(r7.z) + -0.212114394;
          r8.x = r8.x * abs(r7.z) + 1.57072878;
          r9.w = r8.x * r7.w;
          r9.w = r9.w * -2 + 3.14159274;
          r7.z = cmp(r7.z < -r7.z);
          r7.z = r7.z ? r9.w : 0;
          r7.z = r8.x * r7.w + r7.z;
          r7.w = r5.w * 3.14159274 + -r7.y;
          r7.z = r7.z + -abs(r7.w);
          r5.w = r5.w * 3.14159274 + r7.y;
          r5.w = r5.w + -abs(r7.w);
          r5.w = saturate(r7.z / r5.w);
          r5.w = 1 + -r5.w;
          r7.y = r5.w * -2 + 3;
          r5.w = r5.w * r5.w;
          r5.w = r7.y * r5.w;
          r7.x = r7.x * 3.14159274 + -0.100000001;
          r7.x = saturate(5 * r7.x);
          r6.w = r7.x * r5.w;
        }
      }
      r5.w = 1 + -r6.w;
      r5.w = cb0[1].w * r5.w + r6.w;
      r6.w = 1 + -r5.w;
      r7.xyz = cb0[1].xyz * r6.www;
      r6.w = 1 + -r4.w;
      r4.w = cb0[1].w * r6.w + r4.w;
    } else {
      r7.xyz = float3(0,0,0);
      r5.w = 1;
      r4.w = 1;
    }
    r4.w = r4.w * r1.w;
    r6.w = log2(r3.w);
    r6.w = -r6.w * 1.20000005 + 1;
    r7.w = -1 + cb1[170].z;
    r8.x = r7.w + -r6.w;
    r2.x = 0;
    r9.w = r1.y;
    r10.w = 0;
    r12.w = 1;
    r13.xyzw = float4(0,0,0,0);
    while (true) {
      r14.x = cmp((uint)r13.w >= (uint)r0.w);
      if (r14.x != 0) break;
      r14.x = cmp(r9.w < 0.00100000005);
      if (r14.x != 0) {
        break;
      }
      r14.x = (int)r0.z + (int)r13.w;
      r14.x = t4.Load(r14.x).x;
      r14.yzw = -cb4[r14.x+0].xyz + r10.xyz;
      r15.x = dot(r14.yzw, r14.yzw);
      r15.y = sqrt(r15.x);
      r15.z = cmp(r15.y < cb4[r14.x+0].w);
      if (r15.z != 0) {
        r15.y = saturate(r15.y / cb4[r14.x+0].w);
        r15.z = dot(r2.yzw, r14.yzw);
        r15.x = -cb4[r14.x+0].w * cb4[r14.x+0].w + r15.x;
        r15.x = r15.z * r15.z + -r15.x;
        r15.w = cmp(r15.x >= 0);
        r15.x = sqrt(r15.x);
        r15.x = r15.x + -r15.z;
        r14.yzw = r15.xxx * r2.yzw + r14.yzw;
        r16.yzw = -cb4[r14.x+682].xyz + r14.yzw;
        r14.y = r15.y * 2.5 + -1.5;
        r14.y = max(0, r14.y);
        r14.z = r14.y * r14.y;
        r14.y = -r14.y * 2 + 3;
        r16.x = -r14.z * r14.y + 1;
        r15.xyzw = r15.wwww ? r16.xyzw : r2.xyzw;
        r16.xyz = r15.yzw;
        r16.w = cb4[r14.x+341].y;
        r16.xyzw = t2.SampleLevel(s3_s, r16.xyzw, r8.x).xyzw;
        r14.yzw = cb4[r14.x+341].xxx * r16.xyz;
        r14.yzw = r14.yzw * r15.xxx;
        r14.yzw = r14.yzw * r9.www;
        r13.xyz = r14.yzw * r5.www + r13.xyz;
        r14.y = -r16.w * r15.x + 1;
        r9.w = r14.y * r9.w;
        r14.x = cb4[r14.x+682].w * r15.x;
        r10.w = r14.x * r12.w + r10.w;
        r14.x = 1 + -r15.x;
        r12.w = r14.x * r12.w;
      }
      r13.w = (int)r13.w + 1;
    }
    r13.xyz = cb1[151].xyz * r13.xyz;
    r1.y = dot(cb1[151].xyz, float3(0.300000012,0.589999974,0.109999999));
    r13.w = r10.w * r1.y;
    if (r1.x != 0) {
      r2.x = cb2[0].x + -r6.w;
      r2.x = -1 + r2.x;
      r14.xyz = t0.SampleLevel(s1_s, r2.yzw, r2.x).xyz;
      r6.w = dot(cb1[162].xyz, float3(0.300000012,0.589999974,0.109999999));
      r6.w = cb2[1].x * r6.w;
      r14.xyz = cb1[162].xyz * r14.xyz;
      r8.x = cmp(0 < cb2[0].w);
      if (r8.x != 0) {
        r2.xyz = t1.SampleLevel(s2_s, r2.yzw, r2.x).xyz;
        r2.xyz = r2.xyz * cb1[162].xyz + -r14.xyz;
        r14.xyz = cb2[0].www * r2.xyz + r14.xyz;
      }
      r2.x = cmp(cb2[0].z < 1);
      r2.yzw = r14.xyz * r9.www;
      r15.xyz = r2.yzw * r5.www + r13.xyz;
      r15.w = r6.w * r12.w + r13.w;
      r2.yzw = r14.xyz * r5.www + r7.xyz;
      r7.xyz = r2.xxx ? r7.xyz : r2.yzw;
      r13.xyzw = r2.xxxx ? r15.xyzw : r13.xyzw;
    }
    r2.x = saturate(r3.w * cb1[172].x + cb1[172].y);
    r2.y = r2.x * -2 + 3;
    r2.x = r2.x * r2.x;
    r2.x = r2.y * r2.x;
    r2.y = max(9.99999975e-005, r13.w);
    r2.y = r4.w / r2.y;
    r2.y = min(cb1[172].z, r2.y);
    r2.y = -1 + r2.y;
    r2.x = r2.x * r2.y + 1;
    r2.yzw = r9.www * r7.xyz;
    r2.xyz = r13.xyz * r2.xxx + r2.yzw;
    r2.xyz = cb1[129].yyy * r2.xyz + r12.xyz;
    if (r5.z != 0) {
      r3.y = r3.w;
      r5.zw = t5.SampleLevel(s4_s, r3.xy, 0).xy;
      r2.w = saturate(50 * r9.y);
      r2.w = r5.w * r2.w;
      r3.y = 1 + -r5.x;
      r2.w = r3.y * r2.w;
      r7.xyz = r9.xyz * r5.zzz + r2.www;
      r7.xyz = r7.xyz * r2.xyz;
      r3.z = r5.y;
      r3.yz = t5.SampleLevel(s4_s, r3.xz, 0).xy;
      r2.w = r3.y * 0.0399999991 + r3.z;
      r3.y = r2.w * r5.x;
      r2.w = -r2.w * r5.x + 1;
      r5.xzw = r6.xyz * r3.yyy;
      r5.xzw = r7.xyz * r2.www + r5.xzw;
      r0.x = r3.y * r0.x;
      r0.x = r0.x * r0.y;
      if (r1.z != 0) {
        r0.y = dot(r8.yzw, r8.yzw);
        r0.y = sqrt(r0.y);
        if (cb1[171].x == 0) {
          r1.z = r0.y;
        } else {
          r2.w = max(0.100000001, r5.y);
          r3.y = cb0[0].y * r0.y;
          r3.z = 3.14159274 * r3.y;
          r4.w = dot(r8.yzw, r11.yzw);
          r6.x = max(0.00100000005, r0.y);
          r4.w = r4.w / r6.x;
          r6.x = 1 + -abs(r4.w);
          r6.x = sqrt(r6.x);
          r6.y = abs(r4.w) * -0.0187292993 + 0.0742610022;
          r6.y = r6.y * abs(r4.w) + -0.212114394;
          r6.y = r6.y * abs(r4.w) + 1.57072878;
          r6.z = r6.y * r6.x;
          r6.z = r6.z * -2 + 3.14159274;
          r4.w = cmp(r4.w < -r4.w);
          r4.w = r4.w ? r6.z : 0;
          r4.w = r6.y * r6.x + r4.w;
          r6.x = r2.w * 3.14159274 + -r3.z;
          r4.w = -abs(r6.x) + r4.w;
          r2.w = r2.w * 3.14159274 + r3.z;
          r2.w = r2.w + -abs(r6.x);
          r2.w = saturate(r4.w / r2.w);
          r2.w = 1 + -r2.w;
          r3.z = r2.w * -2 + 3;
          r2.w = r2.w * r2.w;
          r2.w = r3.z * r2.w;
          r3.y = r3.y * 3.14159274 + -0.100000001;
          r3.y = saturate(5 * r3.y);
          r1.z = r3.y * r2.w;
        }
        r2.w = 1 + -r1.z;
        r1.z = cb0[1].w * r2.w + r1.z;
        r2.w = 1 + -r1.z;
        r6.xyz = cb0[1].xyz * r2.www;
        r2.w = 1 + -r0.y;
        r0.y = cb0[1].w * r2.w + r0.y;
      } else {
        r6.xyz = float3(0,0,0);
        r1.z = 1;
        r0.y = 1;
      }
      r0.y = r1.w * r0.y;
      r1.w = log2(r5.y);
      r1.w = -r1.w * 1.20000005 + 1;
      r2.w = r7.w + -r1.w;
      r11.x = 0;
      r7.xyz = float3(0,0,0);
      r3.y = r0.x;
      r3.z = 0;
      r4.w = 1;
      r6.w = 0;
      while (true) {
        r7.w = cmp((uint)r6.w >= (uint)r0.w);
        if (r7.w != 0) break;
        r7.w = cmp(r3.y < 0.00100000005);
        if (r7.w != 0) {
          break;
        }
        r7.w = (int)r0.z + (int)r6.w;
        r7.w = t4.Load(r7.w).x;
        r8.xyz = -cb4[r7.w+0].xyz + r10.xyz;
        r8.w = dot(r8.xyz, r8.xyz);
        r9.w = sqrt(r8.w);
        r10.w = cmp(r9.w < cb4[r7.w+0].w);
        if (r10.w != 0) {
          r9.w = saturate(r9.w / cb4[r7.w+0].w);
          r10.w = dot(r11.yzw, r8.xyz);
          r8.w = -cb4[r7.w+0].w * cb4[r7.w+0].w + r8.w;
          r8.w = r10.w * r10.w + -r8.w;
          r12.x = cmp(r8.w >= 0);
          r8.w = sqrt(r8.w);
          r8.w = r8.w + -r10.w;
          r8.xyz = r8.www * r11.yzw + r8.xyz;
          r8.yzw = -cb4[r7.w+682].xyz + r8.xyz;
          r9.w = r9.w * 2.5 + -1.5;
          r9.w = max(0, r9.w);
          r10.w = r9.w * r9.w;
          r9.w = -r9.w * 2 + 3;
          r8.x = -r10.w * r9.w + 1;
          r8.xyzw = r12.xxxx ? r8.xyzw : r11.xyzw;
          r12.xyz = r8.yzw;
          r12.w = cb4[r7.w+341].y;
          r12.xyzw = t2.SampleLevel(s3_s, r12.xyzw, r2.w).xyzw;
          r8.yzw = cb4[r7.w+341].xxx * r12.xyz;
          r8.yzw = r8.yzw * r8.xxx;
          r8.yzw = r8.yzw * r3.yyy;
          r7.xyz = r8.yzw * r1.zzz + r7.xyz;
          r8.y = -r12.w * r8.x + 1;
          r3.y = r8.y * r3.y;
          r7.w = cb4[r7.w+682].w * r8.x;
          r3.z = r7.w * r4.w + r3.z;
          r7.w = 1 + -r8.x;
          r4.w = r7.w * r4.w;
        }
        r6.w = (int)r6.w + 1;
      }
      r7.xyz = cb1[151].xyz * r7.xyz;
      r7.w = r3.z * r1.y;
      if (r1.x != 0) {
        r0.x = cb2[0].x + -r1.w;
        r0.x = -1 + r0.x;
        r1.xyw = t0.SampleLevel(s1_s, r11.yzw, r0.x).xyz;
        r0.z = dot(cb1[162].xyz, float3(0.300000012,0.589999974,0.109999999));
        r0.z = cb2[1].x * r0.z;
        r1.xyw = cb1[162].xyz * r1.xyw;
        r0.w = cmp(0 < cb2[0].w);
        if (r0.w != 0) {
          r8.xyz = t1.SampleLevel(s2_s, r11.yzw, r0.x).xyz;
          r8.xyz = r8.xyz * cb1[162].xyz + -r1.xyw;
          r1.xyw = cb2[0].www * r8.xyz + r1.xyw;
        }
        r0.x = cmp(cb2[0].z < 1);
        r8.xyz = r3.yyy * r1.xyw;
        r8.xyz = r8.xyz * r1.zzz + r7.xyz;
        r8.w = r0.z * r4.w + r7.w;
        r1.xyz = r1.xyw * r1.zzz + r6.xyz;
        r6.xyz = r0.xxx ? r6.xyz : r1.xyz;
        r7.xyzw = r0.xxxx ? r8.xyzw : r7.xyzw;
      }
      r0.x = saturate(r5.y * cb1[172].x + cb1[172].y);
      r0.z = r0.x * -2 + 3;
      r0.x = r0.x * r0.x;
      r0.x = r0.z * r0.x;
      r0.z = max(9.99999975e-005, r7.w);
      r0.y = r0.y / r0.z;
      r0.y = min(cb1[172].z, r0.y);
      r0.y = -1 + r0.y;
      r0.x = r0.x * r0.y + 1;
      r0.yzw = r6.xyz * r3.yyy;
      r0.xyz = r7.xyz * r0.xxx + r0.yzw;
      r0.xyz = cb1[129].yyy * r0.xyz + r5.xzw;
    } else {
      r1.xy = t5.SampleLevel(s4_s, r3.xw, 0).xy;
      r0.w = saturate(50 * r9.y);
      r0.w = r0.w * r1.y;
      r1.xyz = r9.xyz * r1.xxx + r0.www;
      r0.xyz = r2.xyz * r1.xyz;
    }
    r0.xyz = min(float3(0,0,0), -r0.xyz);
    o0.xyz = r4.xyz + -r0.xyz;
  } else {
    o0.xyz = r4.xyz;
  }
  return;
}