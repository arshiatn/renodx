// Tales of Arise ambient occlusion (Bandai, compute permutation 00). From 0xE243580F.cs_5_0.cso
// (3Dmigoto, 2026-10-09 dumps). AoHaloKeep (shared.h): the AO halo fix.
#include "../shared.h"

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);

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


// Below 70% DLSS the samples read the full-resolution depth (t1) instead of mip 0 of the closest-depth pyramid (t3,
// each texel the nearest of 2x2 pixels): its texels cover more of the screen the lower the render scale, so leaves
// looked thicker to the AO and plants darkened (ATN: DLSS Performance); one render pixel there is about the
// pyramid's footprint at 100%. The full-resolution path assumes the view fills its buffer (DLSS render sizes do).
float AoSampleDepth(float2 pyramid_uv) {
  if (SI.filter_scale >= 0.7f) return t3.SampleLevel(s3_s, pyramid_uv, 0).x;
  const float2 viewport_uv = (pyramid_uv - cb0[29].zw) / cb0[29].xy * float2(0.5f, -0.5f) + 0.5f;
  return t1.SampleLevel(s1_s, viewport_uv * cb0[30].zw / cb0[28].zw, 0).x;
}

RWTexture2D<float4> u0 : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10;
  uint4 bitmask, uiDest;
  float4 fDest;

  float4 x0[16];
  r0.xy = (uint2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r0.zw = cb0[28].zw * r0.xy;
  r1.xy = cb0[30].zw * r0.xy;
  r2.xy = r0.zw * float2(2,-2) + float2(-1,1);
  r3.xyz = t0.SampleLevel(s0_s, r1.xy, 0).xyz;
  r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.y = t1.SampleLevel(s1_s, r1.xy, 0).x;
  r2.z = 1;
  r4.xyz = cb1[9].xyz * r3.yyy;
  r3.xyw = r3.xxx * cb1[8].xyz + r4.xyz;
  r3.xyz = r3.zzz * cb1[10].xyz + r3.xyw;
  r0.z = dot(r3.xyz, r3.xyz);
  r0.z = rsqrt(r0.z);
  r3.xyz = r3.xyz * r0.zzz;
  r4.xyz = float3(0.000199999995,0.000500000024,0.00200000009) * r1.yyy;
  float ao_kept = 0.f;
  float ao_excluded = 0.f;
  float ao_offset_z = 0.f;
  r4.xy = saturate(r4.xy);
  r0.zw = r4.xy * float2(-98,55) + float2(100,5);
  // Halo fix: samples further in front of the pixel than twice the kernel reach (its widest ring,
  // 0.000608 x radius in NDC, at this depth) or 5% of the depth + 10 cm are left out.
  const float ao_limit = max(2.f * 0.000607656257f * r0.z * r1.y / cb0[28].x, 0.05f * r1.y + 10.f);
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
  r4.xy = float2(0.25,0.25) * r0.xy;
  r4.xy = frac(r4.xy);
  r4.xy = float2(4,4) * r4.xy;
  r4.xy = (uint2)r4.xy;
  r1.w = mad((int)r4.x, 4, (int)r4.y);
  r1.w = x0[r1.w+0].x;
  r2.w = 3 * r1.w;
  r5.y = cb0[28].y * cb0[28].x;
  r6.x = 1 / cb0[28].x;
  r6.y = 1 / r5.y;
  r6.z = 1;
  r5.x = cb0[28].x;
  r5.z = 1;
  r4.xyw = r5.xyz * r3.xyz;
  r4.xyz = r4.zzz * r4.xyw;
  r4.xyz = r2.xyz * r1.yyy + r4.xyz;
  r4.xyz = r4.xyz * -r6.xyz;
  sincos(r2.w, r5.x, r7.x);
  r8.xyzw = float4(0.00014468751,0.000257222215,0.000202552095,0.000360092585) * r0.zzzz;
  r2.zw = float2(0.984799981,0.642799973) * r7.xx;
  r5.yz = float2(0.984799981,0.642799973) * r5.xx;
  r9.xy = r7.xx * float2(-0.173600003,-0.765999973) + -r5.yz;
  r9.zw = r5.xx * float2(-0.173600003,-0.765999973) + r2.zw;
  r8.xyzw = r9.xzyw * r8.xyzw + r2.xyxy;
  r9.xyzw = cb0[29].xyxy * r8.xyzw + cb0[29].zwzw;
  r5.w = AoSampleDepth(r9.xy);
  r5.yz = r5.ww * r8.xy;
  r5.yzw = r5.yzw * r6.xyz + r4.xyz;
  r2.z = dot(r5.yzw, r5.yzw);
  ao_offset_z = r5.w;
  r2.w = dot(r5.yzw, r3.xyz);
  r2.z = 0.00999999978 + r2.z;
  r2.z = r2.w / r2.z;
  r2.z = AoHaloTerm(r2.z, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r5.w = AoSampleDepth(r9.zw);
  r5.yz = r5.ww * r8.zw;
  r5.yzw = r5.yzw * r6.xyz + r4.xyz;
  r2.w = dot(r5.yzw, r5.yzw);
  ao_offset_z = r5.w;
  r3.w = dot(r5.yzw, r3.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.w / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.zw = max(float2(0,0), r2.zw);
  r2.z = r2.z + r2.w;
  r7.y = r5.x;
  r5.yz = r7.xy * r0.zz;
  r5.yz = r5.yz * float2(-0.00026041668,-0.000462962955) + r2.xy;
  r7.yz = cb0[29].xy * r5.yz + cb0[29].zw;
  r7.w = AoSampleDepth(r7.yz);
  r7.yz = r7.ww * r5.yz;
  r5.yzw = r7.yzw * r6.xyz + r4.xyz;
  r2.w = dot(r5.yzw, r5.yzw);
  ao_offset_z = r5.w;
  r3.w = dot(r5.yzw, r3.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.w / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r2.z = r2.z + r2.w;
  r8.xyzw = float4(0.000318281265,0.000565833354,0.00037614585,0.000668703695) * r0.zzzz;
  r5.yzw = float3(-0.642799973,-0.984809995,-0.865999997) * r5.xxx;
  r9.x = r7.x * -0.765999973 + -r5.y;
  r7.yzw = float3(-0.642799973,-0.984809995,0.5) * r7.xxx;
  r9.y = r5.x * -0.765999973 + r7.y;
  r8.xy = r9.xy * r8.xy + r2.xy;
  r9.xy = cb0[29].xy * r8.xy + cb0[29].zw;
  r9.z = AoSampleDepth(r9.xy);
  r9.xy = r9.zz * r8.xy;
  r9.xyz = r9.xyz * r6.xyz + r4.xyz;
  r2.w = dot(r9.xyz, r9.xyz);
  ao_offset_z = r9.z;
  r3.w = dot(r9.xyz, r3.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.w / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r2.z = r2.z + r2.w;
  r9.xy = r7.xx * float2(-0.173649997,0.5) + -r5.zw;
  r9.z = r5.x * -0.173649997 + r7.z;
  r5.yz = r9.xz * r8.zw + r2.xy;
  r7.yz = cb0[29].xy * r5.yz + cb0[29].zw;
  r8.z = AoSampleDepth(r7.yz);
  r8.xy = r8.zz * r5.yz;
  r5.yzw = r8.xyz * r6.xyz + r4.xyz;
  r2.w = dot(r5.yzw, r5.yzw);
  ao_offset_z = r5.w;
  r3.w = dot(r5.yzw, r3.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.w / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r2.z = r2.z + r2.w;
  r8.xyzw = float4(0.000434010435,0.000771574036,0.000491875049,0.000874444435) * r0.zzzz;
  r5.yzw = float3(0.5,-0.342000008,0.342000008) * r5.xxx;
  r10.xyz = float3(-0.865999997,-0.342000008,0.342000008) * r7.xxx;
  r10.xzw = r5.xxx * float3(0.5,0.939700007,0.939700007) + r10.xyz;
  r9.w = r10.x;
  r7.yz = r9.yw * r8.xy + r2.xy;
  r8.xy = cb0[29].xy * r7.yz + cb0[29].zw;
  r9.z = AoSampleDepth(r8.xy);
  r9.xy = r9.zz * r7.yz;
  r9.xyz = r9.xyz * r6.xyz + r4.xyz;
  r2.w = dot(r9.xyz, r9.xyz);
  ao_offset_z = r9.z;
  r3.w = dot(r9.xyz, r3.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.w / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r2.z = r2.z + r2.w;
  r10.xy = r7.xx * float2(0.939700007,0.939700007) + -r5.zw;
  r5.zw = r10.xz * r8.zw + r2.xy;
  r7.yz = cb0[29].xy * r5.zw + cb0[29].zw;
  r8.z = AoSampleDepth(r7.yz);
  r8.xy = r8.zz * r5.zw;
  r8.xyz = r8.xyz * r6.xyz + r4.xyz;
  r2.w = dot(r8.xyz, r8.xyz);
  ao_offset_z = r8.z;
  r3.w = dot(r8.xyz, r3.xyz);
  r2.w = 0.00999999978 + r2.w;
  r2.w = r3.w / r2.w;
  r2.w = AoHaloTerm(r2.w, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.w = max(0, r2.w);
  r2.z = r2.z + r2.w;
  r8.xyzw = float4(0.00054979173,0.000977407442,0.000607656257,0.00108027773) * r0.zzzz;
  r5.zw = r10.yw * r8.xy + r2.xy;
  r7.yz = cb0[29].xy * r5.zw + cb0[29].zw;
  r9.z = AoSampleDepth(r7.yz);
  r9.xy = r9.zz * r5.zw;
  r9.xyz = r9.xyz * r6.xyz + r4.xyz;
  r0.z = dot(r9.xyz, r9.xyz);
  ao_offset_z = r9.z;
  r2.w = dot(r9.xyz, r3.xyz);
  r0.z = 0.00999999978 + r0.z;
  r0.z = r2.w / r0.z;
  r0.z = AoHaloTerm(r0.z, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r0.z = max(0, r0.z);
  r0.z = r2.z + r0.z;
  r8.x = -r5.x * 0.865999997 + r7.w;
  r8.y = r7.x * 0.865999997 + r5.y;
  r2.xy = r8.xy * r8.zw + r2.xy;
  r2.zw = cb0[29].xy * r2.xy + cb0[29].zw;
  r5.z = AoSampleDepth(r2.zw);
  r5.xy = r5.zz * r2.xy;
  r2.xyz = r5.xyz * r6.xyz + r4.xyz;
  r2.w = dot(r2.xyz, r2.xyz);
  ao_offset_z = r2.z;
  r2.x = dot(r2.xyz, r3.xyz);
  r2.y = 0.00999999978 + r2.w;
  r2.x = r2.x / r2.y;
  r2.x = AoHaloTerm(r2.x, ao_offset_z, ao_limit, ao_kept, ao_excluded);
  r2.x = max(0, r2.x);
  r0.z = r2.x + r0.z;
  r0.z = AoHaloSum(r0.z, ao_kept, ao_excluded);
  r0.w = cb0[32].x * r0.w;
  r0.z = r0.w * r0.z;
  r0.z = saturate(-r0.z * 0.222222224 + 1);
  r0.z = r0.z * r0.z;
  r0.z = max(0.150000006, r0.z);
  // RenoDX: Ambient occlusion GT-VBAO (bitmask_ao.hlsli) in place of the spiral.
  [branch]
  if (SI.ssao_mode != 0.f) {
    r0.z = TalesBitmaskAmbientOcclusion((cb0[28].zw * ((float2)vThreadID.xy + 0.5f)) * float2(2.f, -2.f) + float2(-1.f, 1.f), cb0[30].zw * ((float2)vThreadID.xy + 0.5f), vThreadID.xy, t1, s1_s,
                                          float2(0.5f, -0.5f) * cb0[30].zw / cb0[28].zw, 0.5f * cb0[30].zw / cb0[28].zw);
  }
  r1.x = max(cb0[32].w, r0.z);
  r0.z = cmp(0 < cb0[33].x);
  r0.w = cmp(r1.y < 60000);
  r0.z = r0.w ? r0.z : 0;
  if (r0.z != 0) {
    r0.xy = r0.xy / cb0[33].yz;
    r0.xy = r0.xy * float2(2,-2) + float2(-1,1);
    r0.xy = r0.xy * r1.yy;
    r2.xyzw = cb1[45].xyzw * r0.yyyy;
    r0.xyzw = r0.xxxx * cb1[44].xyzw + r2.xyzw;
    r0.xyzw = r1.yyyy * cb1[46].xyzw + r0.xyzw;
    r0.xyzw = cb1[47].xyzw + r0.xyzw;
    r0.xyz = r0.xyz / r0.www;
    r2.xy = -cb0[27].yx + r0.yx;
    r2.xy = r2.xy / cb0[27].ww;
    r2.xy = r2.xy * float2(1,-1) + float2(0.5,0.5);
    r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
    r2.xy = float2(-1,-1) + abs(r2.xy);
    r2.xy = max(float2(0,0), r2.xy);
    r2.xy = float2(10,10) * r2.xy;
    r2.xy = min(float2(1,1), r2.xy);
    r2.xy = float2(1,1) + -r2.xy;
    r2.x = r2.x * r2.y;
    r2.y = cmp(r2.x >= 0.00100000005);
    if (r2.y != 0) {
      r2.yz = -cb1[59].xy + r0.xy;
      r2.w = dot(r2.yz, r2.yz);
      r2.w = rsqrt(r2.w);
      r3.xy = r2.yz * r2.ww;
      r3.z = 0;
      r2.y = 300 * r1.w;
      r0.w = 50 + r0.z;
      r2.yzw = -r2.yyy * r3.xyz + r0.xyw;
      r2.yz = -cb0[27].yx + r2.zy;
      r2.yz = r2.yz / cb0[27].ww;
      r2.yz = r2.yz * float2(1,-1) + float2(0.5,0.5);
      r2.yz = t2.SampleLevel(s2_s, r2.yz, 0).xy;
      r0.z = r2.y + r2.z;
      r0.z = cb0[27].z + -r0.z;
      r0.z = r0.z + -r2.w;
      r0.z = max(0, r0.z);
      r0.z = 0.00200000009 * r0.z;
      r0.z = min(1, r0.z);
      r0.z = 1 + -r0.z;
      r1.w = 0.200000003 + r1.w;
      r1.w = 600 * r1.w;
      r0.xyw = -r1.www * r3.xyz + r0.xyw;
      r0.xy = -cb0[27].yx + r0.yx;
      r0.xy = r0.xy / cb0[27].ww;
      r0.xy = r0.xy * float2(1,-1) + float2(0.5,0.5);
      r0.xy = t2.SampleLevel(s2_s, r0.xy, 0).xy;
      r0.x = r0.x + r0.y;
      r0.x = cb0[27].z + -r0.x;
      r0.x = r0.x + -r0.w;
      r0.x = max(0, r0.x);
      r0.x = 0.00200000009 * r0.x;
      r0.x = min(1, r0.x);
      r0.x = r0.z + -r0.x;
      r0.x = 1 + r0.x;
      r0.x = 0.5 * r0.x;
      r0.y = 1 + -cb0[33].x;
      r0.y = max(0.0500000007, r0.y);
      r0.z = 1 + -r0.y;
      r0.x = r0.x * r0.z + r0.y;
      r0.y = -55000 + r1.y;
      r0.y = saturate(0.000199999995 * r0.y);
      r0.z = 1 + -r0.x;
      r0.x = r0.y * r0.z + r0.x;
      r0.x = -1 + r0.x;
      r0.x = r2.x * r0.x + 1;
      r1.x = r1.x * r0.x;
    }
  }
  r1.z = 0;
u0[vThreadID.xy] = r1.xyzz;
  return;
}