// Tales of Arise deferred sun lighting without volumetric lighting (Bandai TO14). From 0xF95FABAA.ps_5_0.cso
// (3Dmigoto, 2026-10-09 dumps). As lighting00: Bandai's edge tests are sized in render pixels; below 100% DLSS,
// SI.filter_scale (render / output, from the DLSS addon) spaces them in output pixels, and edges below one render
// pixel stay about one pixel wide and fade by the same ratio.
#include "../shared.h"
#include "../rt_ambient.hlsli"

Texture2D<float4> t15 : register(t15);

Texture2D<float4> t14 : register(t14);

Texture2D<float4> t13 : register(t13);

TextureCube<float4> t12 : register(t12);

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
  float4 cb1[124];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[79];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3,
  out float4 o4 : SV_Target4)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t1.SampleLevel(s1_s, v0.xy, 0).xyzw;
  r1.xyzw = t3.SampleLevel(s3_s, v0.xy, 0).xyzw;
  r2.x = t0.SampleLevel(s0_s, v0.xy, 0).x;
  r2.y = r2.x * cb1[57].x + cb1[57].y;
  r2.x = r2.x * cb1[57].z + -cb1[57].w;
  r2.x = 1 / r2.x;
  r2.x = r2.y + r2.x;
  r3.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = 3.99900007 * r0.w;
  r0.x = (uint)r0.x;
  r0.x = (int)r0.x & 1;
  r0.x = (uint)r0.x;
  r0.yzw = v1.xyz * r2.xxx;
  r2.yzw = v1.xyz * r2.xxx + cb1[59].xyz;
  r4.xyz = t4.Sample(s4_s, v0.xy).xyz;
  r5.xyzw = t11.Sample(s4_s, v0.xy).xyzw;
  r4.w = 1 + -r5.x;
  r4.w = max(0, r4.w);
  r6.xyz = float3(0.00100000005,0.00100000005,0.00100000005) + r1.xyz;
  r7.xyz = r4.xyz / r6.xyz;
  r5.x = dot(cb0[53].xyz, r3.xyz);
  r5.y = cmp(0 != r5.y);
  // Ray-traced ambient occlusion (the DLSS addon, Experimental): 1 when it is off.
  r6.w = t5.Sample(s4_s, v0.xy).x * RayTracedAmbientOcclusion(v0.xy);
  r6.w = log2(r6.w);
  r1.w = r6.w * r1.w;
  r8.y = exp2(r1.w);
  r1.w = cmp(r8.y >= 0.899999976);
  if (r1.w != 0) {
    r8.xz = float2(1,1);
    r9.xyz = r8.zyz;
    r10.xyz = r4.xyz;
  } else {
    r8.x = 1.11111116 * r8.y;
  }
  r6.w = r2.x / cb0[58].x;
  r6.w = min(1, r6.w);
  r7.w = 1 + -cb0[59].z;
  r7.w = r6.w * r7.w + cb0[59].z;
  r8.z = log2(r8.x);
  r7.w = r8.z * r7.w;
  r7.w = exp2(r7.w);
  r9.w = cb0[59].y + -cb0[59].x;
  r9.w = r6.w * r9.w + cb0[59].x;
  r10.w = 1 + -r9.w;
  r7.w = r7.w * r10.w + r9.w;
  r6.xyz = log2(r6.xyz);
  r6.xyz = r7.www * r6.xyz;
  r6.xyz = exp2(r6.xyz);
  r6.xyz = r6.xyz * r7.xyz;
  r7.w = cb0[58].z + -cb0[58].y;
  r7.w = r6.w * r7.w + cb0[58].y;
  r9.w = 1 + -cb0[58].w;
  r6.w = r6.w * r9.w + cb0[58].w;
  r6.w = r6.w * r8.z;
  r6.w = exp2(r6.w);
  r8.z = 1 + -r7.w;
  r7.w = r6.w * r8.z + r7.w;
  r6.w = r7.w * r6.w;
  r6.w = min(1, r6.w);
  r7.w = cmp(r8.x >= 0.899999976);
  r8.w = r7.w ? 1 : r6.w;
  r8.xyz = r1.www ? r9.xyz : r8.xyw;
  r6.xyz = r1.www ? r10.xyz : r6.xyz;
  r1.w = saturate(dot(r7.xyz, float3(0.200000003,0.699999988,0.100000001)));
  r1.w = r1.w * r8.z;
  r6.w = saturate(9.99999975e-006 * r2.x);
  r7.w = min(1, r4.w);
  r8.z = 1 + -r7.w;
  r6.w = r6.w * r8.z + r7.w;
  r6.w = max(0.5, r6.w);
  r1.w = r6.w * r1.w;
  r6.w = cmp(r1.w == 1.000000);
  if (r6.w != 0) {
    r9.xyz = r6.xyz;
  }
  if (r6.w == 0) {
    r10.xyz = max(float3(9.99999975e-005,9.99999975e-005,9.99999975e-005), r1.xyz);
    r10.xyz = log2(r10.xyz);
    r10.xyz = cb0[50].www * r10.xyz;
    r10.xyz = exp2(r10.xyz);
    r6.w = cmp(0.100000001 >= r4.w);
    r7.w = cmp(r4.w >= 0.899999976);
    r6.w = (int)r6.w | (int)r7.w;
    if (r6.w != 0) {
      r11.xyz = cb0[50].xyz;
    }
    if (r6.w == 0) {
      r8.zw = cb0[0].zw * (float2(2.29999995,-2.29999995) * SI.filter_scale) + v0.xy;
      r12.xyz = t11.Gather(s4_s, r8.zw).yzw;
      r12.xyz = float3(1,1,1) + -r12.xzy;
      r6.w = r12.y + -r4.w;
      r8.zw = -r12.xz + r4.ww;
      r4.w = max(abs(r8.z), abs(r6.w));
      r4.w = max(r4.w, abs(r8.w));
      // Penumbras widen with the sun shadow distance (the same map over more ground): the edge measure is the change
      // per game-distance penumbra, so the edge color stays a thin line (ATN, distance 4: a wide pale patch).
      r4.w /= max(SI.sun_shadow_distance, 1.f);
      r4.w = log2(r4.w);
      r4.w = cb0[65].x * r4.w;
      r4.w = exp2(r4.w);
      r4.w = 100 * r4.w;
      r4.w = min(1, r4.w);
      // The band is about 1 + offset render pixels wide: 3.3 at 100%.
      r4.w = saturate(r4.w * (3.3f * SI.filter_scale / (1.f + 2.3f * SI.filter_scale)) * SI.shadow_edge_color);
      r6.w = cmp(r5.x >= 0);
      r6.w = r6.w ? 1.000000 : 0;
      r4.w = r6.w * r4.w;
      r6.w = max(0.400000006, cb0[53].w);
      r6.w = min(1, r6.w);
      r4.w = r6.w * r4.w;
      r12.xyz = cb0[63].xyz + -cb0[50].xyz;
      r11.xyz = r4.www * r12.xyz + cb0[50].xyz;
    }
    r4.w = saturate(r3.z);
    r4.w = 1 + r4.w;
    r11.xyz = r4.www * r11.xyz;
    r10.xyz = r11.xyz * r10.xyz;
    r11.xyz = r10.xyz * r7.xyz;
    r6.xyz = -r10.xyz * r7.xyz + r6.xyz;
    r9.xyz = r1.www * r6.xyz + r11.xyz;
  }
  r1.w = dot(r3.xyz, -cb0[53].xyz);
  r1.w = r1.w * 0.699999988 + 0.300000012;
  r1.w = max(0, r1.w);
  r4.w = cmp(r1.w < 0.00999999978);
  if (r4.w != 0) {
    r6.x = cmp(cb0[76].w == 0.000000);
    if (r6.x != 0) {
      r6.yzw = float3(0.00100000005,0.00100000005,0.00100000005);
      r7.xyzw = float4(0,0,0,0);
    }
    if (r6.x == 0) {
      r6.x = cmp(1000 >= r2.x);
      if (r6.x != 0) {
        r6.yzw = t8.SampleLevel(s4_s, v0.xy, 0).xyz;
      } else {
        r6.x = cmp(1500 >= r2.x);
        if (r6.x != 0) {
          r10.xyz = t8.SampleLevel(s4_s, v0.xy, 0).xyz;
          r11.xyz = t9.SampleLevel(s4_s, v0.xy, 0).xyz;
          r6.x = -1000 + r2.x;
          r6.x = saturate(0.00200000009 * r6.x);
          r11.xyz = r11.xyz + -r10.xyz;
          r6.yzw = r6.xxx * r11.xyz + r10.xyz;
        } else {
          r6.x = cmp(7000 >= r2.x);
          if (r6.x != 0) {
            r6.yzw = t9.SampleLevel(s4_s, v0.xy, 0).xyz;
          } else {
            r6.x = cmp(8000 >= r2.x);
            if (r6.x != 0) {
              r10.xyz = t9.SampleLevel(s4_s, v0.xy, 0).xyz;
              r11.xyz = t10.SampleLevel(s4_s, v0.xy, 0).xyz;
              r6.x = -7000 + r2.x;
              r6.x = saturate(0.00100000005 * r6.x);
              r11.xyz = r11.xyz + -r10.xyz;
              r6.yzw = r6.xxx * r11.xyz + r10.xyz;
            } else {
              r6.yzw = t10.SampleLevel(s4_s, v0.xy, 0).xyz;
            }
          }
        }
      }
      r7.xyzw = float4(0,0,0,0);
    }
  }
  if (r4.w == 0) {
    r4.w = 6.66666674e-005 * r2.x;
    r4.w = min(1, r4.w);
    r6.x = -5000 + r2.x;
    r7.w = saturate(0.000199999995 * r6.x);
    r6.x = cmp(cb0[76].w == 0.000000);
    if (r6.x != 0) {
      r6.yzw = float3(0.00100000005,0.00100000005,0.00100000005);
    }
    if (r6.x == 0) {
      r8.zw = cb1[9].xy * r3.yy;
      r8.zw = r3.xx * cb1[8].xy + r8.zw;
      r8.zw = r3.zz * cb1[10].xy + r8.zw;
      r6.x = r7.w * -0.899999976 + 1;
      r8.zw = r6.xx * r8.zw;
      r6.x = cmp(1000 >= r2.x);
      if (r6.x != 0) {
        r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
        r11.xyz = t8.SampleLevel(s4_s, r10.xy, 0).xyz;
        r10.xyz = t8.SampleLevel(s4_s, r10.zw, 0).xyz;
        r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
        r10.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
        r11.xy = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
        r11.xyz = t8.SampleLevel(s4_s, r11.xy, 0).xyz;
        r6.yzw = r11.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
      } else {
        r6.x = cmp(1500 >= r2.x);
        if (r6.x != 0) {
          r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
          r11.xyz = t8.SampleLevel(s4_s, r10.xy, 0).xyz;
          r12.xyz = t8.SampleLevel(s4_s, r10.zw, 0).xyz;
          r12.xyz = float3(0.300000012,0.300000012,0.300000012) * r12.xyz;
          r11.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r12.xyz;
          r12.xy = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
          r13.xyz = t8.SampleLevel(s4_s, r12.xy, 0).xyz;
          r11.xyz = r13.xyz * float3(0.300000012,0.300000012,0.300000012) + r11.xyz;
          r13.xyz = t9.SampleLevel(s4_s, r10.xy, 0).xyz;
          r10.xyz = t9.SampleLevel(s4_s, r10.zw, 0).xyz;
          r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
          r10.xyz = r13.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
          r12.xyz = t9.SampleLevel(s4_s, r12.xy, 0).xyz;
          r10.xyz = r12.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
          r6.x = -1000 + r2.x;
          r6.x = saturate(0.00200000009 * r6.x);
          r10.xyz = r10.xyz + -r11.xyz;
          r6.yzw = r6.xxx * r10.xyz + r11.xyz;
        } else {
          r6.x = cmp(7000 >= r2.x);
          if (r6.x != 0) {
            r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
            r11.xyz = t9.SampleLevel(s4_s, r10.xy, 0).xyz;
            r10.xyz = t9.SampleLevel(s4_s, r10.zw, 0).xyz;
            r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
            r10.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
            r11.xy = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
            r11.xyz = t9.SampleLevel(s4_s, r11.xy, 0).xyz;
            r6.yzw = r11.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
          } else {
            r6.x = cmp(8000 >= r2.x);
            if (r6.x != 0) {
              r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
              r11.xyz = t9.SampleLevel(s4_s, r10.xy, 0).xyz;
              r12.xyz = t9.SampleLevel(s4_s, r10.zw, 0).xyz;
              r12.xyz = float3(0.300000012,0.300000012,0.300000012) * r12.xyz;
              r11.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r12.xyz;
              r12.xy = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
              r13.xyz = t9.SampleLevel(s4_s, r12.xy, 0).xyz;
              r11.xyz = r13.xyz * float3(0.300000012,0.300000012,0.300000012) + r11.xyz;
              r13.xyz = t10.SampleLevel(s4_s, r10.xy, 0).xyz;
              r10.xyz = t10.SampleLevel(s4_s, r10.zw, 0).xyz;
              r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
              r10.xyz = r13.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
              r12.xyz = t10.SampleLevel(s4_s, r12.xy, 0).xyz;
              r10.xyz = r12.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
              r6.x = -7000 + r2.x;
              r6.x = saturate(0.00100000005 * r6.x);
              r10.xyz = r10.xyz + -r11.xyz;
              r6.yzw = r6.xxx * r10.xyz + r11.xyz;
            } else {
              r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
              r11.xyz = t10.SampleLevel(s4_s, r10.xy, 0).xyz;
              r10.xyz = t10.SampleLevel(s4_s, r10.zw, 0).xyz;
              r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
              r10.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
              r8.zw = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
              r11.xyz = t10.SampleLevel(s4_s, r8.zw, 0).xyz;
              r6.yzw = r11.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
            }
          }
        }
      }
    }
    r10.xyz = float3(0.00100000005,0.00100000005,0.00100000005) + r6.yzw;
    r6.x = dot(r10.xyz, float3(0.200000003,0.699999988,0.100000001));
    r10.xyz = r6.yzw / r6.xxx;
    r10.xyz = log2(r10.xyz);
    r10.xyz = cb0[61].www * r10.xyz;
    r10.xyz = exp2(r10.xyz);
    r11.xyz = float3(1,1,0.5) * cb0[53].xyz;
    r3.w = 0.5 * r3.z;
    r3.w = dot(r11.xyz, r3.xyw);
    r3.w = min(r5.x, r3.w);
    r3.w = r3.w * -0.5 + 0.5;
    r5.x = 1 + -cb0[66].w;
    r3.w = saturate(-r5.x + r3.w);
    r3.w = r3.w / cb0[66].w;
    r3.w = log2(r3.w);
    r3.w = 0.600000024 * r3.w;
    r3.w = exp2(r3.w);
    r5.x = cb0[67].x * r8.x;
    r3.w = r5.x * r3.w;
    r5.x = dot(r1.xyz, float3(0.200000003,0.699999988,0.100000001));
    r5.x = 0.5 + r5.x;
    r5.x = r5.x * r5.x;
    r11.xyz = log2(r1.xyz);
    r11.xyz = float3(0.200000003,0.200000003,0.200000003) * r11.xyz;
    r11.xyz = exp2(r11.xyz);
    r11.xyz = cb0[66].xyz * r11.xyz;
    r6.x = dot(float3(0.200000003,0.699999988,0.100000001), r11.xyz);
    r11.xyz = r11.xyz * r5.xxx;
    r5.x = 0.00100000005 + r6.x;
    r11.xyz = r11.xyz / r5.xxx;
    r11.xyz = r11.xyz + -r1.xyz;
    r11.xyz = r3.www * r11.xyz + r1.xyz;
    r1.w = r1.w * r8.x;
    r3.w = r4.w * 0.649999976 + 0.349999994;
    r8.xzw = cb0[61].xyz * r11.xyz;
    r8.xzw = r8.xzw * r10.xyz;
    r1.w = r3.w * r1.w;
    r7.xyz = r1.www * r8.xzw;
  }
  r7.xyz = r9.xyz + r7.xyz;
  r8.xz = v1.zz * r2.xx + float2(1000,-200);
  r8.xz = saturate(float2(0.000166666665,0.00100000005) * r8.xz);
  r1.w = 1 + -r7.w;
  r1.w = r8.x * r1.w;
  r3.w = -1 + cb0[69].z;
  r1.w = r1.w * r3.w + 1;
  r6.xyz = r6.yzw * r1.www;
  r6.xyz = cb0[69].yyy * r6.xyz;
  r1.w = log2(r8.y);
  r1.w = cb0[69].x * r1.w;
  r1.w = exp2(r1.w);
  r3.w = max(0.300000012, r8.y);
  r6.xyz = r3.www * r6.xyz;
  r7.xyz = r7.xyz * r1.www;
  r6.xyz = r6.xyz * r1.xyz + r7.xyz;
  r1.w = dot(r0.yzw, r0.yzw);
  r1.w = rsqrt(r1.w);
  r7.xyz = r1.www * r0.yzw;
  r0.y = dot(r7.xyz, r3.xyz);
  r0.y = r0.y + r0.y;
  r9.xyz = r3.xyz * -r0.yyy + r7.xyz;
  r0.y = saturate(-0.150000006 + r5.z);
  r0.z = cmp(0 < r0.y);
  if (r0.z != 0) {
    r10.x = 1.17647052 * r0.y;
    r0.y = 8 * r5.w;
    r0.z = cmp(cb0[77].w == 1.000000);
    if (r0.z != 0) {
      r11.xyz = cb0[78].xyz * float3(0.5,0.5,0.5) + cb0[77].xyz;
      r12.xyz = -cb0[78].xyz * float3(0.5,0.5,0.5) + cb0[77].xyz;
      r11.xyz = r11.xyz + -r2.yzw;
      r11.xyz = r11.xyz / r9.xyz;
      r12.xyz = r12.xyz + -r2.yzw;
      r12.xyz = r12.xyz / r9.xyz;
      r11.xyz = max(r12.xyz, r11.xyz);
      r0.z = min(r11.x, r11.y);
      r0.z = min(r0.z, r11.z);
      r11.xyz = r9.xyz * r0.zzz + r2.yzw;
      r11.xyz = -cb0[77].xyz + r11.xyz;
    } else {
      r11.xyz = r9.xyz;
    }
    r12.xyz = t12.SampleLevel(s5_s, r11.xyz, r0.y).xyz;
    r11.xyz = t12.SampleLevel(s6_s, r11.xyz, 8).xyz;
    r0.y = dot(float3(0.300000012,0.400000006,0.300000012), r11.xyz);
    r0.y = 9.99999975e-005 + r0.y;
    r11.xyz = r12.xyz / r0.yyy;
    // Ray-traced specular occlusion (the DLSS addon, Experimental): no sky tint where the reflection is covered.
    r0.y = cb0[75].w * r10.x * RayTracedSpecularOcclusion(v0.xy);
    r11.xyz = r6.xyz * r11.xyz + -r6.xyz;
    r11.xyz = r0.yyy * r11.xyz + r6.xyz;
    r10.yz = float2(1,0);
    r10.yzw = r11.xyz * float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) + r10.yzx;
    r10.yzw = r10.yzw + -r11.xyz;
    r6.xyz = cb0[73].xxx * r10.yzw + r11.xyz;
  } else {
    r10.x = 0;
  }
  r0.y = -cb0[75].y + r5.w;
  r0.y = saturate(cb0[75].z * r0.y);
  r0.z = saturate(r3.z);
  r5.xz = float2(-0.899999976,-0.699999988) + r0.zz;
  r5.xz = float2(10,3.33333325) * r5.xz;
  r5.x = saturate(r5.x);
  r1.w = max(0, r5.z);
  r1.w = r1.w + -r5.x;
  r1.w = r0.y * r1.w + r5.x;
  r3.w = 1 + -r0.y;
  r4.w = r3.w * r1.w;
  r4.w = cb0[75].x * r4.w;
  r0.w = saturate(-r0.w * 0.00999999978 + 1);
  r4.w = r4.w * r0.w;
  r4.w = cmp(0 < r4.w);
  r4.w = r5.y ? 0 : r4.w;
  if (r4.w != 0) {
    r4.w = t2.SampleLevel(s2_s, v0.xy, 0).y;
    r5.x = dot(r9.xyz, -r7.xyz);
    r5.x = saturate(r5.x * 0.5 + 0.5);
    r5.x = 1 + -r5.x;
    r5.zw = cb0[36].xy * v0.xy;
    r9.xyz = r9.xyz + r2.yzw;
    r10.yzw = cb1[5].xyw * r9.yyy;
    r9.xyw = r9.xxx * cb1[4].xyw + r10.yzw;
    r9.xyz = r9.zzz * cb1[6].xyw + r9.xyw;
    r9.xyz = cb1[7].xyw + r9.xyz;
    r2.yz = r9.xy / r9.zz;
    r2.yz = r2.yz * cb1[58].xy + cb1[58].wz;
    r2.yz = r2.yz * cb0[36].xy + -r5.zw;
    r6.w = dot(r2.yz, r2.yz);
    r6.w = sqrt(r6.w);
    r6.w = 0.00100000005 + r6.w;
    r6.w = 0.200000003 / r6.w;
    r0.z = 1 + -r0.z;
    r0.z = r6.w * r0.z;
    r2.yz = r0.zz * r2.yz + r5.zw;
    r0.z = cmp(0.5 >= r0.y);
    if (r0.z != 0) {
      r0.z = r0.y + r0.y;
      r0.z = min(1, r0.z);
      r0.z = 1 + -r0.z;
      r0.z = r0.z * r0.z;
      r9.xyzw = t13.SampleLevel(s7_s, r2.yz, 0).xyzw;
      r11.xyzw = t14.SampleLevel(s7_s, r2.yz, 0).xyzw;
      r9.xyzw = -r11.xyzw + r9.xyzw;
      r9.xyzw = r0.zzzz * r9.xyzw + r11.xyzw;
      // Ray-traced reflections (the DLSS addon, Experimental): the real reflection where its rays met a surface.
      r9.xyzw = RayTracedReflection(v0.xy, r9.xyzw);
      r0.z = r0.z * -0.5 + 1.5;
      r0.z = r9.w * r0.z;
    } else {
      r0.y = -0.5 + r0.y;
      r0.y = r0.y + r0.y;
      r0.y = max(0, r0.y);
      r11.xyzw = t14.SampleLevel(s7_s, r2.yz, 0).xyzw;
      r12.xyzw = t15.SampleLevel(s7_s, r2.yz, 0).xyzw;
      r12.xyzw = r12.xyzw + -r11.xyzw;
      r9.xyzw = r0.yyyy * r12.xyzw + r11.xyzw;
      // Ray-traced reflections (the DLSS addon, Experimental): the real reflection where its rays met a surface.
      r9.xyzw = RayTracedReflection(v0.xy, r9.xyzw);
      r0.y = r0.y * 0.5 + 1.5;
      r0.z = r9.w * r0.y;
    }
    r0.y = saturate(1.5 * r0.z);
    r0.y = r0.y * r1.w;
    r0.y = r0.y * r5.x;
    r0.z = r4.w * 0.850000024 + 0.150000006;
    r0.z = r3.w * r0.z;
    r1.w = 0.5 * r10.x;
    r2.yz = r10.xx * float2(0.899999976,0.899999976) + float2(0.100000001,-0.899999976);
    r1.xyz = float3(-1,-1,-1) + r1.xyz;
    r1.xyz = r1.www * r1.xyz + float3(1,1,1);
    r1.w = 1 + -r2.y;
    r0.z = r0.z * r1.w + r2.y;
    r0.y = r0.z * r0.y;
    r0.y = cb0[75].x * r0.y;
    r0.y = r0.y * r0.w;
    r0.z = r0.y * r2.z + 1;
    r1.xyz = r9.xyz * r1.xyz;
    r1.xyz = r1.xyz * r0.yyy;
    r1.xyz = r1.xyz * r8.yyy;
    r0.y = cmp(cb0[76].x == 0.000000);
    r5.xzw = r6.xyz * r0.zzz + r1.xyz;
    r6.xyz = r0.yyy ? r5.xzw : r6.xyz;
  } else {
    r1.xyz = float3(0,0,0);
    r0.z = 1;
  }
  const float normal_scaled = 1.29999995f * SI.filter_scale;
  const float normal_offset = max(normal_scaled, 1.f);
  r0.yw = -cb0[0].zw * float2(-0,normal_offset) + v0.xy;
  r5.xzw = t1.SampleLevel(s1_s, r0.yw, 0).xyz;
  r5.xzw = r5.xzw * float3(2,2,2) + float3(-1,-1,-1);
  r0.yw = cb0[0].zw * float2(normal_offset,0) + v0.xy;
  r8.xyw = t1.SampleLevel(s1_s, r0.yw, 0).xyz;
  r8.xyw = r8.xyw * float3(2,2,2) + float3(-1,-1,-1);
  r0.y = dot(r3.xyz, r5.xzw);
  r0.y = 1 + -abs(r0.y);
  r0.w = dot(r3.xyz, r8.xyw);
  r0.y = r0.y + -abs(r0.w);
  r0.y = 1 + r0.y;
  r0.w = 0.00100000005 + cb0[54].x;
  r0.y = r0.y / r0.w;
  r0.y = min(1, r0.y);
  r0.y *= normal_scaled / normal_offset;
  r0.w = -cb0[57].x + r2.x;
  r0.w = saturate(r0.w / cb0[57].y);
  r2.yz = float2(-1,-1) + cb0[57].zw;
  r2.yz = r0.ww * r2.yz + float2(1,1);
  r0.w = r5.y ? r2.z : r2.y;
  r0.y = r0.y * r0.w;
  r0.w = -cb0[62].z + r2.x;
  o1.z = saturate(r0.w / cb0[62].w);
  r0.w = cmp(r2.x < 1500);
  if (r0.w != 0) {
    r2.yz = -cb0[0].zz * (float2(1.29999995,1.29999995) * SI.filter_scale) + v0.xy;
    r2.yz = t0.Gather(s4_s, r2.yz).xz;
    r3.xy = cb0[0].zz * (float2(1.29999995,1.29999995) * SI.filter_scale) + v0.xy;
    r3.xy = t0.Gather(s4_s, r3.xy).xz;
    r2.yz = r3.yx + -r2.yz;
    r2.yz = float2(62.5,62.5) * r2.yz;
    r2.yz = min(float2(1,1), abs(r2.yz));
    r2.yz = r2.yz * r2.yz;
    r0.w = 0.00066666666 * r2.x;
    r0.w = min(1, r0.w);
    r2.yz = r0.ww * -r2.yz + r2.yz;
    r0.w = r2.y + r2.z;
    r0.w = cmp(0.00499999989 >= r0.w);
    if (r0.w != 0) {
      r3.xy = r2.yz;
    } else {
      r3.xy = float2(0,0);
    }
    o1.xy = r0.ww ? r3.xy : r2.yz;
  } else {
    o1.xy = float2(0,0);
  }
  r0.w = cb0[71].x + -r2.w;
  r0.w = saturate(r0.w / cb0[71].y);
  r3.xyz = float3(-1,-1,-1) + cb0[72].xyz;
  r3.xyz = r0.www * r3.xyz + float3(1,1,1);
  r3.xyz = r6.xyz * r3.xyz;
  r0.w = cb0[39].z * r2.w;
  r0.w = r0.w * -2 + r2.w;
  r1.w = cmp(cb0[40].y < r0.w);
  r2.y = cmp(300000 < r2.x);
  r1.w = (int)r1.w | (int)r2.y;
  r2.z = cmp(cb0[39].y == 1.000000);
  r1.w = r1.w ? r2.z : 0;
  if (r1.w != 0) {
    r1.w = saturate(r7.z);
    r2.z = saturate(-r7.z);
    r2.z = r2.z + -r1.w;
    r1.w = cb0[39].z * r2.z + r1.w;
    r2.z = cmp(r1.w >= 0.100000001);
    if (r2.z != 0) {
      r5.xz = cb1[123].xy / cb1[122].xy;
      r5.xz = v0.xy * r5.xz;
      r6.xyzw = t6.SampleLevel(s4_s, r5.xz, 0).xyzw;
    } else {
      r6.xyzw = float4(0,0,0,1);
    }
    r0.w = -cb0[40].y + r0.w;
    r0.w = saturate(0.000199999995 * r0.w);
    r2.z = -1 + r6.w;
    r2.z = r0.w * r2.z + 1;
    r5.xzw = r0.www * r6.xyz;
    r5.xzw = r2.yyy ? r6.xyz : r5.xzw;
    r0.w = r2.y ? r6.w : r2.z;
    r3.xyz = r0.www * r3.xyz + r5.xzw;
    r2.y = r2.y ? 0.199999988 : 0.699999988;
    r2.y = r0.w * r2.y + 0.300000012;
    r1.w = saturate(10 * r1.w);
    r1.w = 1 + -r1.w;
    r2.z = 1 + -r2.y;
    r1.w = r1.w * r2.z + r2.y;
  } else {
    r1.w = 1;
    r0.w = 1;
  }
  r2.y = saturate(-r7.z);
  r2.y = cb0[46].x * r2.y;
  r2.y = cmp(0 < r2.y);
  if (r2.y != 0) {
    r2.y = -cb0[46].y + r2.w;
    r2.y = saturate(-r2.y / cb0[46].z);
    r2.z = cmp(0 < r2.y);
    if (r2.z != 0) {
      r2.zw = cb1[123].xy / cb1[122].xy;
      r2.zw = v0.xy * r2.zw;
      r2.zw = t7.SampleLevel(s4_s, r2.zw, 0).xy;
      r2.w = 1 + -r2.w;
      r2.w = max(0, r2.w);
      r2.w = -1 + r2.w;
      r2.w = r2.y * r2.w + 1;
      r2.y = r2.y * r2.z;
      r5.xzw = cb0[47].xyz * r2.yyy;
      r3.xyz = r2.www * r3.xyz + r5.xzw;
      r2.y = r2.w * r1.w;
      r2.y = 0.699999988 * r2.y;
      o1.w = r1.w * 0.300000012 + r2.y;
    } else {
      o1.w = r1.w;
      r2.w = 1;
    }
  } else {
    o1.w = r1.w;
    r2.w = 1;
  }
  r1.w = cmp(cb0[42].w == 0.000000);
  if (r1.w != 0) {
    r5.xzw = float3(0,0,0);
  }
  if (r1.w == 0) {
    r1.w = cmp(0.00999999978 >= cb0[45].x);
    if (r1.w != 0) {
      r5.xzw = float3(0,0,0);
    }
    if (r1.w == 0) {
      r1.w = dot(r7.xyz, cb0[53].xyz);
      r1.w = saturate(r1.w * 0.5 + 0.600000024);
      r2.y = 10 * cb0[45].y;
      r1.w = log2(r1.w);
      r1.w = r2.y * r1.w;
      r1.w = exp2(r1.w);
      r2.y = r7.z * 0.5 + 0.300000012;
      r2.y = saturate(1.25 * r2.y);
      r2.z = cb0[45].z * 4.98999977 + 0.00999999978;
      r2.y = log2(r2.y);
      r2.y = r2.z * r2.y;
      r2.y = exp2(r2.y);
      r1.w = r2.y * r1.w;
      r2.y = 0.00100000005 + cb0[45].w;
      r2.y = saturate(r7.z / r2.y);
      r2.y = r2.y * r2.y;
      r6.xyz = -cb0[44].xyz + cb0[43].xyz;
      r6.xyz = r2.yyy * r6.xyz + cb0[44].xyz;
      r1.w = cb0[45].x * r1.w;
      r2.y = saturate(0.100000001 * cb0[53].w);
      r2.y = log2(r2.y);
      r2.y = 1.5 * r2.y;
      r2.y = exp2(r2.y);
      r1.w = r2.y * r1.w;
      r5.xzw = r1.www * r6.xyz;
    }
  }
  r3.xyz = r5.xzw + r3.xyz;
  r0.w = r2.w * r0.w;
  o2.xyzw = r0.yyyy * r0.wwww;
  r0.y = cmp(cb0[76].x != 0.000000);
  r1.xyz = r3.xyz * r0.zzz + r1.xyz;
  r0.yzw = r0.yyy ? r1.xyz : r3.xyz;
  // Ray-traced bounce light (the DLSS addon, Experimental), before the fog: 0 when it is off.
  r0.yzw += RayTracedBounceLight(v0.xy, t3.SampleLevel(s3_s, v0.xy, 0).xyz);
  r1.x = -cb0[48].x + r2.x;
  r1.x = saturate(r1.x / cb0[48].y);
  r1.y = -1 + cb0[49].w;
  r1.y = r1.x * r1.y + 1;
  r1.xzw = cb0[49].xyz * r1.xxx;
  r0.yzw = r1.yyy * r0.yzw + r1.xzw;
  r1.x = dot(float3(0.333000004,0.333000004,0.333000004), r0.yzw);
  r0.yzw = -r1.xxx + r0.yzw;
  r0.yzw = cb0[70].xxx * r0.yzw + r1.xxx;
  r0.yzw = cb0[70].yzw * r0.yzw;
  r1.x = -1 + cb0[39].w;
  r0.x = r0.x * r1.x + 1;
  r0.xyz = r0.yzw * r0.xxx;
  r1.xy = float2(-20000,-2000) + r2.xx;
  r1.xy = saturate(float2(9.99999975e-005,0.000285714283) * r1.xy);
  r0.w = 0.5 * r1.x;
  r1.x = 1 + -r1.y;
  r1.x = r8.z * r1.x;
  o4.w = r5.y ? r1.x : 0;
  o4.xyz = r0.www * r0.xyz;
  o0.xyz = r0.xyz;
  o0.w = 0;
  o3.xyz = r4.xyz;
  o3.w = 0;
  return;
}