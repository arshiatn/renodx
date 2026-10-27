// Tales of Arise ambient occlusion (Bandai, pixel shader permutation 08). From 0x3A849493.ps_5_0.cso
// (3Dmigoto, 2026-10-09 dumps). AoHaloKeep (shared.h): the AO halo fix.
#include "../shared.h"

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[60];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[34];
}




// 3Dmigoto declarations
#define cmp -
#include "../bitmask_ao.hlsli"


void main(
  linear noperspective float4 v0 : TEXCOORD0,
  float4 v1 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  float4 x0[16];
  r0.xyz = t0.SampleLevel(s0_s, v0.xy, 0).xyz;
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.xy = cb0[29].xy * v0.zw + cb0[29].zw;
  r1.y = t2.SampleLevel(s2_s, r1.xy, 0).x;
  r2.xy = v0.zw;
  r2.z = 1;
  r3.xyz = cb1[9].xyz * r0.yyy;
  r0.xyw = r0.xxx * cb1[8].xyz + r3.xyz;
  r0.xyz = r0.zzz * cb1[10].xyz + r0.xyw;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r3.xyz = float3(0.000199999995,0.000500000024,0.00200000009) * r1.yyy;
  float ao_kept = 0.f;
  float ao_excluded = 0.f;
  float ao_offset_z = 0.f;
  r3.xy = saturate(r3.xy);
  r3.xy = r3.xy * float2(-140,55) + float2(150,5);
  // Halo fix: samples further in front of the pixel than twice the kernel reach (its widest ring,
  // 0.000608 x radius in NDC, at this depth) or 5% of the depth + 10 cm are left out.
  const float ao_limit = max(2.f * 0.000607656257f * r3.x * r1.y / cb0[28].x, 0.05f * r1.y + 10.f);
  x0[0].x = 0;
  x0[1].x = 0.5;
  x0[2].x = 0.125;
  x0[3].x = 0.625;
  x0[4].x = 0.75;
  x0[5].x = 0.219999999;
  x0[6].x = 0.875;
  x0[7].x = 0.375;
  x0[8].x = 0.1875;
  x0[9].x = 0.6875;
  x0[10].x = 0.0625;
  x0[11].x = 0.5625;
  x0[12].x = 0.9375;
  x0[13].x = 0.4375;
  x0[14].x = 0.8125;
  x0[15].x = 0.3125;
  r4.xyzw = float4(4,4,4,4) * v1.xyxy;
  r4.xyzw = cmp(r4.xyzw >= -r4.zwzw);
  r4.xyzw = r4.xyzw ? float4(4,4,0.25,0.25) : float4(-4,-4,-0.25,-0.25);
  r4.zw = v1.xy * r4.zw;
  r4.zw = frac(r4.zw);
  r4.xy = r4.xy * r4.zw;
  r4.xy = (uint2)r4.xy;
  r0.w = (uint)r4.x << 2;
  r0.w = (int)r4.y + (int)r0.w;
  r0.w = x0[r0.w+0].x;
  r1.w = 3 * r0.w;
  r4.y = cb0[28].y * cb0[28].x;
  r5.x = 1 / cb0[28].x;
  r5.y = 1 / r4.y;
  r5.z = 1;
  r4.x = cb0[28].x;
  r4.z = 1;
  r4.xyz = r4.xyz * r0.xyz;
  r4.xyz = r4.xyz * r3.zzz;
  r2.xyz = r2.xyz * r1.yyy + r4.xyz;
  r2.xyz = r2.xyz * -r5.xyz;
  sincos(r1.w, r4.x, r6.x);
  r7.xyzw = float4(0.00014468751,0.000257222215,0.000202552095,0.000360092585) * r3.xxxx;
  r3.zw = float2(0.984799981,0.642799973) * r6.xx;
  r4.yz = float2(0.984799981,0.642799973) * r4.xx;
  r8.xy = r6.xx * float2(-0.173600003,-0.765999973) + -r4.yz;
  r8.zw = r4.xx * float2(-0.173600003,-0.765999973) + r3.zw;
  r7.xyzw = r8.xzyw * r7.xyzw + v0.zwzw;
  r8.xyzw = cb0[29].xyxy * r7.xyzw + cb0[29].zwzw;
  r4.w = t2.SampleLevel(s2_s, r8.xy, 0).x;
  r4.yz = r4.ww * r7.xy;
  r4.yzw = r4.yzw * r5.xyz + r2.xyz;
  r1.w = dot(r4.yzw, r4.yzw);
  ao_offset_z = r4.w;
  r2.w = dot(r4.yzw, r0.xyz);
  r1.w = 0.00999999978 + r1.w;
  r1.w = r2.w / r1.w;
  r1.w = AoHaloTerm(r1.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r1.w = max(0, r1.w);
  r4.w = t2.SampleLevel(s2_s, r8.zw, 0).x;
  r4.yz = r4.ww * r7.zw;
  r4.yzw = r4.yzw * r5.xyz + r2.xyz;
  r2.w = dot(r4.yzw, r4.yzw);
  ao_offset_z = r4.w;
  r3.z = dot(r4.yzw, r0.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.z / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r1.w = r2.w + r1.w;
  r6.y = r4.x;
  r3.zw = r6.xy * r3.xx;
  r3.zw = r3.zw * float2(-0.00026041668,-0.000462962955) + v0.zw;
  r4.yz = cb0[29].xy * r3.zw + cb0[29].zw;
  r4.w = t2.SampleLevel(s2_s, r4.yz, 0).x;
  r4.yz = r4.ww * r3.zw;
  r4.yzw = r4.yzw * r5.xyz + r2.xyz;
  r2.w = dot(r4.yzw, r4.yzw);
  ao_offset_z = r4.w;
  r3.z = dot(r4.yzw, r0.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.z / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r1.w = r2.w + r1.w;
  r7.xyzw = float4(0.000318281265,0.000565833354,0.00037614585,0.000668703695) * r3.xxxx;
  r4.yzw = float3(-0.642799973,-0.984809995,-0.865999997) * r4.xxx;
  r8.x = r6.x * -0.765999973 + -r4.y;
  r6.yzw = float3(-0.642799973,-0.984809995,0.5) * r6.xxx;
  r8.y = r4.x * -0.765999973 + r6.y;
  r3.zw = r8.xy * r7.xy + v0.zw;
  r7.xy = cb0[29].xy * r3.zw + cb0[29].zw;
  r8.z = t2.SampleLevel(s2_s, r7.xy, 0).x;
  r8.xy = r8.zz * r3.zw;
  r8.xyz = r8.xyz * r5.xyz + r2.xyz;
  r2.w = dot(r8.xyz, r8.xyz);
  ao_offset_z = r8.z;
  r3.z = dot(r8.xyz, r0.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.z / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r1.w = r2.w + r1.w;
  r8.xy = r6.xx * float2(-0.173649997,0.5) + -r4.zw;
  r8.z = r4.x * -0.173649997 + r6.z;
  r3.zw = r8.xz * r7.zw + v0.zw;
  r4.yz = cb0[29].xy * r3.zw + cb0[29].zw;
  r4.w = t2.SampleLevel(s2_s, r4.yz, 0).x;
  r4.yz = r4.ww * r3.zw;
  r4.yzw = r4.yzw * r5.xyz + r2.xyz;
  r2.w = dot(r4.yzw, r4.yzw);
  ao_offset_z = r4.w;
  r3.z = dot(r4.yzw, r0.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.z / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r1.w = r2.w + r1.w;
  r7.xyzw = float4(0.000434010435,0.000771574036,0.000491875049,0.000874444435) * r3.xxxx;
  r4.yzw = float3(0.5,-0.342000008,0.342000008) * r4.xxx;
  r9.xyz = float3(-0.865999997,-0.342000008,0.342000008) * r6.xxx;
  r9.xzw = r4.xxx * float3(0.5,0.939700007,0.939700007) + r9.xyz;
  r8.w = r9.x;
  r3.zw = r8.yw * r7.xy + v0.zw;
  r6.yz = cb0[29].xy * r3.zw + cb0[29].zw;
  r8.z = t2.SampleLevel(s2_s, r6.yz, 0).x;
  r8.xy = r8.zz * r3.zw;
  r8.xyz = r8.xyz * r5.xyz + r2.xyz;
  r2.w = dot(r8.xyz, r8.xyz);
  ao_offset_z = r8.z;
  r3.z = dot(r8.xyz, r0.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.z / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r1.w = r2.w + r1.w;
  r9.xy = r6.xx * float2(0.939700007,0.939700007) + -r4.zw;
  r3.zw = r9.xz * r7.zw + v0.zw;
  r4.zw = cb0[29].xy * r3.zw + cb0[29].zw;
  r7.z = t2.SampleLevel(s2_s, r4.zw, 0).x;
  r7.xy = r7.zz * r3.zw;
  r7.xyz = r7.xyz * r5.xyz + r2.xyz;
  r2.w = dot(r7.xyz, r7.xyz);
  ao_offset_z = r7.z;
  r3.z = dot(r7.xyz, r0.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.z / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r1.w = r2.w + r1.w;
  r7.xyzw = float4(0.00054979173,0.000977407442,0.000607656257,0.00108027773) * r3.xxxx;
  r3.xz = r9.yw * r7.xy + v0.zw;
  r4.zw = cb0[29].xy * r3.xz + cb0[29].zw;
  r8.z = t2.SampleLevel(s2_s, r4.zw, 0).x;
  r8.xy = r8.zz * r3.xz;
  r3.xzw = r8.xyz * r5.xyz + r2.xyz;
  r2.w = dot(r3.xzw, r3.xzw);
  ao_offset_z = r3.w;
  r3.x = dot(r3.xzw, r0.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.x / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r1.w = r2.w + r1.w;
  r7.x = -r4.x * 0.865999997 + r6.w;
  r7.y = r6.x * 0.865999997 + r4.y;
  r3.xz = r7.xy * r7.zw + v0.zw;
  r4.xy = cb0[29].xy * r3.xz + cb0[29].zw;
  r4.z = t2.SampleLevel(s2_s, r4.xy, 0).x;
  r4.xy = r4.zz * r3.xz;
  r2.xyz = r4.xyz * r5.xyz + r2.xyz;
  r2.w = dot(r2.xyz, r2.xyz);
  ao_offset_z = r2.z;
  r0.x = dot(r2.xyz, r0.xyz);
  r0.y = 0.00999999978 + r2.w;
  r0.x = r0.x / r0.y;
  r0.x = AoHaloTerm(r0.x, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r0.x = max(0, r0.x);
  r0.x = r1.w + r0.x;
  r0.x = AoHaloSum(r0.x, ao_kept, ao_excluded);
  r0.y = cb0[32].x * r3.y;
  r0.x = r0.y * r0.x;
  r0.x = saturate(-r0.x * 0.222222224 + 1);
  r0.x = max(0.150000006, r0.x);
  // RenoDX: Ambient occlusion GT-VBAO (bitmask_ao.hlsli) in place of the spiral.
  [branch]
  if (SI.ssao_mode != 0.f) {
    r0.x = TalesBitmaskAmbientOcclusion(v0.zw, v0.xy, uint2(v1.xy), t2, s2_s,
                                          cb0[29].xy, cb0[29].zw);
  }
  r1.x = max(cb0[32].w, r0.x);
  r0.x = cmp(0 < cb0[33].x);
  r0.y = cmp(r1.y < 60000);
  r0.x = r0.y ? r0.x : 0;
  if (r0.x != 0) {
    r0.xy = v1.xy / cb0[33].yz;
    r0.xy = r0.xy * float2(2,-2) + float2(-1,1);
    r0.xy = r0.xy * r1.yy;
    r2.xyzw = cb1[45].xyzw * r0.yyyy;
    r2.xyzw = r0.xxxx * cb1[44].xyzw + r2.xyzw;
    r2.xyzw = r1.yyyy * cb1[46].xyzw + r2.xyzw;
    r2.xyzw = cb1[47].xyzw + r2.xyzw;
    r2.xyz = r2.xyz / r2.www;
    r0.xy = -cb0[27].yx + r2.yx;
    r0.xy = r0.xy / cb0[27].ww;
    r0.xy = r0.xy * float2(1,-1) + float2(0.5,0.5);
    r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
    r0.xy = float2(-1,-1) + abs(r0.xy);
    r0.xy = max(float2(0,0), r0.xy);
    r0.xy = float2(10,10) * r0.xy;
    r0.xy = min(float2(1,1), r0.xy);
    r0.xy = float2(1,1) + -r0.xy;
    r0.x = r0.x * r0.y;
    r0.y = cmp(r0.x >= 0.00100000005);
    if (r0.y != 0) {
      r0.yz = -cb1[59].xy + r2.xy;
      r1.w = dot(r0.yz, r0.yz);
      r1.w = rsqrt(r1.w);
      r3.xy = r1.ww * r0.yz;
      r3.z = 0;
      r2.w = 50 + r2.z;
      r0.yz = float2(0,-40);
      r1.w = 0;
      while (true) {
        r2.z = cmp((uint)r1.w >= 8);
        if (r2.z != 0) break;
        r2.z = r0.z * r0.w;
        r4.xyz = r2.zzz * r3.xyz + r2.xyw;
        r4.xy = -cb0[27].yx + r4.yx;
        r4.xy = r4.xy / cb0[27].ww;
        r4.xy = r4.xy * float2(1,-1) + float2(0.5,0.5);
        r4.xy = t1.SampleLevel(s1_s, r4.xy, 0).xy;
        r2.z = r4.x + r4.y;
        r2.z = cb0[27].z + -r2.z;
        r2.z = r2.z + -r4.z;
        r2.z = max(0, r2.z);
        r2.z = 0.00666666683 * r2.z;
        r2.z = min(1, r2.z);
        r2.z = -r2.z + r0.y;
        r0.y = 1 + r2.z;
        r0.z = 10 + r0.z;
        r1.w = (int)r1.w + 1;
      }
      r0.y = 0.125 * r0.y;
      r0.y = r0.y * r0.y;
      r0.z = 1 + -cb0[33].x;
      r0.z = max(0.0500000007, r0.z);
      r0.w = 1 + -r0.z;
      r0.y = r0.y * r0.w + r0.z;
      r0.z = -55000 + r1.y;
      r0.z = saturate(0.000199999995 * r0.z);
      r0.w = 1 + -r0.y;
      r0.y = r0.z * r0.w + r0.y;
      r0.y = -1 + r0.y;
      r0.x = r0.x * r0.y + 1;
      r1.x = r1.x * r0.x;
    }
  }
  r1.z = 0;
  o0.xyzw = r1.xyzz;
  return;
}