// Tales of Arise deferred sun lighting, permutation 0xC28D2F1E: lighting00 (0xF2A6E883) with its t14.y test (r5.y)
// folded by the compiler in the shadow tint (0xC28D2F1E.ps_5_0.cso, 3Dmigoto, 2026-10-09 dumps). lighting00's notes:
// It adds the volumetric lighting (in-scattered sunlight) from 480x270 volumes with 8-pixel cells: t7 (RGBA16F x16, the
// light), t8 (R16F x16) and t6 (RGBA8 x5: the sun's visibility along the view, one channel per distance step in five
// distance bands, 0-480 / -960 / -1920 / -3840 / beyond). One trilinear sample per pixel left a saw-tooth edge where
// shadowed and lit cells meet (ATN: the mountains' shadow wedge in the haze around the sun;
// r.VolumetricLightingBufferPixelNumPerCell 2 hides it for ~25 fps; the v23 test: t6 draws it). Smooth Volumetric
// Light: t6 filtered along the sun's rays (SampleVisibility, `shaft`); Off is stock.
// Bandai's edge tests are sized in render pixels: below 100% DLSS, SI.filter_scale (render / output, from the DLSS
// addon) spaces them in output pixels. The shadow edge tint (the shadow mask 2.3 render pixels up-right) drew a bright
// band around characters (the halo ATN saw: about 7 output pixels wide at 33%, 28 at 8%); edges below one render pixel
// stay about one pixel wide and fade by the same ratio (as strong on average as at 100%).
#include "../shared.h"
#include "../rt_ambient.hlsli"

Texture2D<float4> t18 : register(t18);

Texture2D<float4> t17 : register(t17);

Texture2D<float4> t16 : register(t16);

TextureCube<float4> t15 : register(t15);

Texture2D<float4> t14 : register(t14);

Texture2D<float4> t13 : register(t13);

Texture2D<float4> t12 : register(t12);

Texture2D<float4> t11 : register(t11);

Texture2D<float4> t10 : register(t10);

Texture2D<float4> t9 : register(t9);

Texture3D<float4> t8 : register(t8);

Texture3D<float4> t7 : register(t7);

Texture3D<float4> t6 : register(t6);

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

cbuffer cb2 : register(b2)
{
  float4 cb2[8];
}

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


// Smooth Volumetric Light: SampleVisibility (shared.h) on t6.

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
  r5.xyzw = t14.Sample(s4_s, v0.xy).xyzw;
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
      r11.xyz = cb0[52].xyz;
    }
    if (r6.w == 0) {
      r8.zw = cb0[0].zw * (float2(2.29999995,-2.29999995) * SI.filter_scale) + v0.xy;
      r12.xyz = t14.Gather(s4_s, r8.zw).yzw;
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
      r12.xyz = cb0[64].xyz + -cb0[52].xyz;
      r11.xyz = r4.www * r12.xyz + cb0[52].xyz;
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
        r6.yzw = t11.SampleLevel(s4_s, v0.xy, 0).xyz;
      } else {
        r6.x = cmp(1500 >= r2.x);
        if (r6.x != 0) {
          r10.xyz = t11.SampleLevel(s4_s, v0.xy, 0).xyz;
          r11.xyz = t12.SampleLevel(s4_s, v0.xy, 0).xyz;
          r6.x = -1000 + r2.x;
          r6.x = saturate(0.00200000009 * r6.x);
          r11.xyz = r11.xyz + -r10.xyz;
          r6.yzw = r6.xxx * r11.xyz + r10.xyz;
        } else {
          r6.x = cmp(7000 >= r2.x);
          if (r6.x != 0) {
            r6.yzw = t12.SampleLevel(s4_s, v0.xy, 0).xyz;
          } else {
            r6.x = cmp(8000 >= r2.x);
            if (r6.x != 0) {
              r10.xyz = t12.SampleLevel(s4_s, v0.xy, 0).xyz;
              r11.xyz = t13.SampleLevel(s4_s, v0.xy, 0).xyz;
              r6.x = -7000 + r2.x;
              r6.x = saturate(0.00100000005 * r6.x);
              r11.xyz = r11.xyz + -r10.xyz;
              r6.yzw = r6.xxx * r11.xyz + r10.xyz;
            } else {
              r6.yzw = t13.SampleLevel(s4_s, v0.xy, 0).xyz;
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
        r11.xyz = t11.SampleLevel(s4_s, r10.xy, 0).xyz;
        r10.xyz = t11.SampleLevel(s4_s, r10.zw, 0).xyz;
        r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
        r10.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
        r11.xy = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
        r11.xyz = t11.SampleLevel(s4_s, r11.xy, 0).xyz;
        r6.yzw = r11.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
      } else {
        r6.x = cmp(1500 >= r2.x);
        if (r6.x != 0) {
          r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
          r11.xyz = t11.SampleLevel(s4_s, r10.xy, 0).xyz;
          r12.xyz = t11.SampleLevel(s4_s, r10.zw, 0).xyz;
          r12.xyz = float3(0.300000012,0.300000012,0.300000012) * r12.xyz;
          r11.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r12.xyz;
          r12.xy = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
          r13.xyz = t11.SampleLevel(s4_s, r12.xy, 0).xyz;
          r11.xyz = r13.xyz * float3(0.300000012,0.300000012,0.300000012) + r11.xyz;
          r13.xyz = t12.SampleLevel(s4_s, r10.xy, 0).xyz;
          r10.xyz = t12.SampleLevel(s4_s, r10.zw, 0).xyz;
          r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
          r10.xyz = r13.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
          r12.xyz = t12.SampleLevel(s4_s, r12.xy, 0).xyz;
          r10.xyz = r12.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
          r6.x = -1000 + r2.x;
          r6.x = saturate(0.00200000009 * r6.x);
          r10.xyz = r10.xyz + -r11.xyz;
          r6.yzw = r6.xxx * r10.xyz + r11.xyz;
        } else {
          r6.x = cmp(7000 >= r2.x);
          if (r6.x != 0) {
            r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
            r11.xyz = t12.SampleLevel(s4_s, r10.xy, 0).xyz;
            r10.xyz = t12.SampleLevel(s4_s, r10.zw, 0).xyz;
            r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
            r10.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
            r11.xy = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
            r11.xyz = t12.SampleLevel(s4_s, r11.xy, 0).xyz;
            r6.yzw = r11.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
          } else {
            r6.x = cmp(8000 >= r2.x);
            if (r6.x != 0) {
              r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
              r11.xyz = t12.SampleLevel(s4_s, r10.xy, 0).xyz;
              r12.xyz = t12.SampleLevel(s4_s, r10.zw, 0).xyz;
              r12.xyz = float3(0.300000012,0.300000012,0.300000012) * r12.xyz;
              r11.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r12.xyz;
              r12.xy = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
              r13.xyz = t12.SampleLevel(s4_s, r12.xy, 0).xyz;
              r11.xyz = r13.xyz * float3(0.300000012,0.300000012,0.300000012) + r11.xyz;
              r13.xyz = t13.SampleLevel(s4_s, r10.xy, 0).xyz;
              r10.xyz = t13.SampleLevel(s4_s, r10.zw, 0).xyz;
              r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
              r10.xyz = r13.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
              r12.xyz = t13.SampleLevel(s4_s, r12.xy, 0).xyz;
              r10.xyz = r12.xyz * float3(0.300000012,0.300000012,0.300000012) + r10.xyz;
              r6.x = -7000 + r2.x;
              r6.x = saturate(0.00100000005 * r6.x);
              r10.xyz = r10.xyz + -r11.xyz;
              r6.yzw = r6.xxx * r10.xyz + r11.xyz;
            } else {
              r10.xyzw = r8.zwzw * float4(0.100000001,-0.100000001,0.200000003,-0.200000003) + v0.xyxy;
              r11.xyz = t13.SampleLevel(s4_s, r10.xy, 0).xyz;
              r10.xyz = t13.SampleLevel(s4_s, r10.zw, 0).xyz;
              r10.xyz = float3(0.300000012,0.300000012,0.300000012) * r10.xyz;
              r10.xyz = r11.xyz * float3(0.400000006,0.400000006,0.400000006) + r10.xyz;
              r8.zw = r8.zw * float2(0.300000012,-0.300000012) + v0.xy;
              r11.xyz = t13.SampleLevel(s4_s, r8.zw, 0).xyz;
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
  r9.xyz = r6.yzw * r1.www;
  r9.xyz = cb0[69].yyy * r9.xyz;
  r1.w = log2(r8.y);
  r1.w = cb0[69].x * r1.w;
  r1.w = exp2(r1.w);
  r3.w = max(0.300000012, r8.y);
  r9.xyz = r3.www * r9.xyz;
  r7.xyz = r7.xyz * r1.www;
  r7.xyz = r9.xyz * r1.xyz + r7.xyz;
  r1.w = dot(r0.yzw, r0.yzw);
  r1.w = rsqrt(r1.w);
  r9.xyz = r1.www * r0.yzw;
  r1.w = dot(r9.xyz, r3.xyz);
  r1.w = r1.w + r1.w;
  r10.xyz = r3.xyz * -r1.www + r9.xyz;
  r1.w = saturate(-0.150000006 + r5.z);
  r3.w = cmp(0 < r1.w);
  if (r3.w != 0) {
    r11.x = 1.17647052 * r1.w;
    r1.w = 8 * r5.w;
    r3.w = cmp(cb0[77].w == 1.000000);
    if (r3.w != 0) {
      r12.xyz = cb0[78].xyz * float3(0.5,0.5,0.5) + cb0[77].xyz;
      r13.xyz = -cb0[78].xyz * float3(0.5,0.5,0.5) + cb0[77].xyz;
      r12.xyz = r12.xyz + -r2.yzw;
      r12.xyz = r12.xyz / r10.xyz;
      r13.xyz = r13.xyz + -r2.yzw;
      r13.xyz = r13.xyz / r10.xyz;
      r12.xyz = max(r13.xyz, r12.xyz);
      r3.w = min(r12.x, r12.y);
      r3.w = min(r3.w, r12.z);
      r12.xyz = r10.xyz * r3.www + r2.yzw;
      r12.xyz = -cb0[77].xyz + r12.xyz;
    } else {
      r12.xyz = r10.xyz;
    }
    r13.xyz = t15.SampleLevel(s5_s, r12.xyz, r1.w).xyz;
    r12.xyz = t15.SampleLevel(s6_s, r12.xyz, 8).xyz;
    r1.w = dot(float3(0.300000012,0.400000006,0.300000012), r12.xyz);
    r1.w = 9.99999975e-005 + r1.w;
    r12.xyz = r13.xyz / r1.www;
    // Ray-traced specular occlusion (the DLSS addon, Experimental): no sky tint where the reflection is covered.
    r1.w = cb0[75].w * r11.x * RayTracedSpecularOcclusion(v0.xy);
    r12.xyz = r7.xyz * r12.xyz + -r7.xyz;
    r12.xyz = r1.www * r12.xyz + r7.xyz;
    r11.yz = float2(1,0);
    r11.yzw = r12.xyz * float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) + r11.yzx;
    r11.yzw = r11.yzw + -r12.xyz;
    r7.xyz = cb0[73].xxx * r11.yzw + r12.xyz;
  } else {
    r11.x = 0;
  }
  r1.w = -cb0[75].y + r5.w;
  r1.w = saturate(cb0[75].z * r1.w);
  r3.w = saturate(r3.z);
  r5.xz = float2(-0.899999976,-0.699999988) + r3.ww;
  r5.xz = float2(10,3.33333325) * r5.xz;
  r5.x = saturate(r5.x);
  r4.w = max(0, r5.z);
  r4.w = r4.w + -r5.x;
  r4.w = r1.w * r4.w + r5.x;
  r5.x = 1 + -r1.w;
  r5.z = r5.x * r4.w;
  r5.z = cb0[75].x * r5.z;
  r5.w = saturate(-r0.w * 0.00999999978 + 1);
  r5.z = r5.z * r5.w;
  r5.z = cmp(0 < r5.z);
  r5.z = r5.y ? 0 : r5.z;
  if (r5.z != 0) {
    r5.z = t2.SampleLevel(s2_s, v0.xy, 0).y;
    r6.x = dot(r10.xyz, -r9.xyz);
    r6.x = saturate(r6.x * 0.5 + 0.5);
    r6.x = 1 + -r6.x;
    r8.xw = cb0[36].xy * v0.xy;
    r10.xyz = r10.xyz + r2.yzw;
    r11.yzw = cb1[5].xyw * r10.yyy;
    r10.xyw = r10.xxx * cb1[4].xyw + r11.yzw;
    r10.xyz = r10.zzz * cb1[6].xyw + r10.xyw;
    r10.xyz = cb1[7].xyw + r10.xyz;
    r2.yz = r10.xy / r10.zz;
    r2.yz = r2.yz * cb1[58].xy + cb1[58].wz;
    r2.yz = r2.yz * cb0[36].xy + -r8.xw;
    r7.w = dot(r2.yz, r2.yz);
    r7.w = sqrt(r7.w);
    r7.w = 0.00100000005 + r7.w;
    r7.w = 0.200000003 / r7.w;
    r3.w = 1 + -r3.w;
    r3.w = r7.w * r3.w;
    r2.yz = r3.ww * r2.yz + r8.xw;
    r3.w = cmp(0.5 >= r1.w);
    if (r3.w != 0) {
      r3.w = r1.w + r1.w;
      r3.w = min(1, r3.w);
      r3.w = 1 + -r3.w;
      r3.w = r3.w * r3.w;
      r10.xyzw = t16.SampleLevel(s7_s, r2.yz, 0).xyzw;
      r12.xyzw = t17.SampleLevel(s7_s, r2.yz, 0).xyzw;
      r10.xyzw = -r12.xyzw + r10.xyzw;
      r10.xyzw = r3.wwww * r10.xyzw + r12.xyzw;
      // Ray-traced reflections (the DLSS addon, Experimental): the real reflection where its rays met a surface.
      r10.xyzw = RayTracedReflection(v0.xy, r10.xyzw);
      r3.w = r3.w * -0.5 + 1.5;
      r3.w = r10.w * r3.w;
    } else {
      r1.w = -0.5 + r1.w;
      r1.w = r1.w + r1.w;
      r1.w = max(0, r1.w);
      r12.xyzw = t17.SampleLevel(s7_s, r2.yz, 0).xyzw;
      r13.xyzw = t18.SampleLevel(s7_s, r2.yz, 0).xyzw;
      r13.xyzw = r13.xyzw + -r12.xyzw;
      r10.xyzw = r1.wwww * r13.xyzw + r12.xyzw;
      // Ray-traced reflections (the DLSS addon, Experimental): the real reflection where its rays met a surface.
      r10.xyzw = RayTracedReflection(v0.xy, r10.xyzw);
      r1.w = r1.w * 0.5 + 1.5;
      r3.w = r10.w * r1.w;
    }
    r1.w = saturate(1.5 * r3.w);
    r1.w = r1.w * r4.w;
    r1.w = r1.w * r6.x;
    r2.y = r5.z * 0.850000024 + 0.150000006;
    r2.y = r5.x * r2.y;
    r2.z = 0.5 * r11.x;
    r5.xz = r11.xx * float2(0.899999976,0.899999976) + float2(0.100000001,-0.899999976);
    r1.xyz = float3(-1,-1,-1) + r1.xyz;
    r1.xyz = r2.zzz * r1.xyz + float3(1,1,1);
    r2.z = 1 + -r5.x;
    r2.y = r2.y * r2.z + r5.x;
    r1.w = r2.y * r1.w;
    r1.w = cb0[75].x * r1.w;
    r1.w = r1.w * r5.w;
    r2.y = r1.w * r5.z + 1;
    r1.xyz = r10.xyz * r1.xyz;
    r1.xyz = r1.xyz * r1.www;
    r1.xyz = r1.xyz * r8.yyy;
    r1.w = cmp(cb0[76].x == 0.000000);
    r5.xzw = r7.xyz * r2.yyy + r1.xyz;
    r7.xyz = r1.www ? r5.xzw : r7.xyz;
  } else {
    r1.xyz = float3(0,0,0);
    r2.y = 1;
  }
  const float normal_scaled = 1.29999995f * SI.filter_scale;
  const float normal_offset = max(normal_scaled, 1.f);
  r5.xz = -cb0[0].zw * float2(-0,normal_offset) + v0.xy;
  r5.xzw = t1.SampleLevel(s1_s, r5.xz, 0).xyz;
  r5.xzw = r5.xzw * float3(2,2,2) + float3(-1,-1,-1);
  r8.xy = cb0[0].zw * float2(normal_offset,0) + v0.xy;
  r8.xyw = t1.SampleLevel(s1_s, r8.xy, 0).xyz;
  r8.xyw = r8.xyw * float3(2,2,2) + float3(-1,-1,-1);
  r1.w = dot(r3.xyz, r5.xzw);
  r1.w = 1 + -abs(r1.w);
  r2.z = dot(r3.xyz, r8.xyw);
  r1.w = -abs(r2.z) + r1.w;
  r1.w = 1 + r1.w;
  r2.z = 0.00100000005 + cb0[54].x;
  r1.w = r1.w / r2.z;
  r1.w = min(1, r1.w);
  r1.w *= normal_scaled / normal_offset;
  r2.z = -cb0[57].x + r2.x;
  r2.z = saturate(r2.z / cb0[57].y);
  r3.xy = float2(-1,-1) + cb0[57].zw;
  r3.xy = r2.zz * r3.xy + float2(1,1);
  r2.z = r5.y ? r3.y : r3.x;
  r1.w = r2.z * r1.w;
  r2.z = -cb0[62].z + r2.x;
  o1.z = saturate(r2.z / cb0[62].w);
  r2.z = cmp(r2.x < 1500);
  if (r2.z != 0) {
    r3.xy = -cb0[0].zz * (float2(1.29999995,1.29999995) * SI.filter_scale) + v0.xy;
    r3.xy = t0.Gather(s4_s, r3.xy).xz;
    r3.zw = cb0[0].zz * (float2(1.29999995,1.29999995) * SI.filter_scale) + v0.xy;
    r3.zw = t0.Gather(s4_s, r3.zw).xz;
    r3.xy = -r3.xy + r3.wz;
    r3.xy = float2(62.5,62.5) * r3.xy;
    r3.xy = min(float2(1,1), abs(r3.xy));
    r3.xy = r3.xy * r3.xy;
    r2.z = 0.00066666666 * r2.x;
    r2.z = min(1, r2.z);
    r3.xy = r2.zz * -r3.xy + r3.xy;
    r2.z = r3.x + r3.y;
    r2.z = cmp(0.00499999989 >= r2.z);
    if (r2.z != 0) {
      r3.zw = r3.xy;
    } else {
      r3.zw = float2(0,0);
    }
    o1.xy = r2.zz ? r3.zw : r3.xy;
  } else {
    o1.xy = float2(0,0);
  }
  r2.z = cb0[71].x + -r2.w;
  r2.z = saturate(r2.z / cb0[71].y);
  r3.xyz = float3(-1,-1,-1) + cb0[72].xyz;
  r3.xyz = r2.zzz * r3.xyz + float3(1,1,1);
  r10.xy = float2(-1,-1) + cb0[71].zw;
  r10.z = -1 + cb0[72].w;
  r5.xzw = r2.zzz * r10.xyz + float3(1,1,1);
  r3.xyz = r7.xyz * r3.xyz;
  r2.z = cb0[39].z * r2.w;
  r2.z = r2.z * -2 + r2.w;
  r3.w = cmp(cb0[40].y < r2.z);
  r4.w = cmp(300000 < r2.x);
  r3.w = (int)r3.w | (int)r4.w;
  r6.x = cmp(cb0[39].y == 1.000000);
  r3.w = r3.w ? r6.x : 0;
  if (r3.w != 0) {
    r3.w = saturate(r9.z);
    r6.x = saturate(-r9.z);
    r6.x = r6.x + -r3.w;
    r3.w = cb0[39].z * r6.x + r3.w;
    r6.x = cmp(r3.w >= 0.100000001);
    if (r6.x != 0) {
      r7.xy = cb1[123].xy / cb1[122].xy;
      r7.xy = v0.xy * r7.xy;
      r7.xyzw = t9.SampleLevel(s4_s, r7.xy, 0).xyzw;
    } else {
      r7.xyzw = float4(0,0,0,1);
    }
    r2.z = -cb0[40].y + r2.z;
    r8.y = saturate(0.000199999995 * r2.z);
    r2.z = -1 + r7.w;
    r8.x = r8.y * r2.z + 1;
    r10.xyz = r8.yyy * r7.xyz;
    r10.xyz = r4.www ? r7.xyz : r10.xyz;
    r7.x = 0;
    r7.xy = r4.ww ? r7.wx : r8.xy;
    r3.xyz = r7.xxx * r3.xyz + r10.xyz;
    r2.z = r4.w ? 0.199999988 : 0.699999988;
    r2.z = r7.x * r2.z + 0.300000012;
    r4.w = saturate(10 * r3.w);
    r4.w = 1 + -r4.w;
    r6.x = 1 + -r2.z;
    r2.z = r4.w * r6.x + r2.z;
    r4.w = cmp(cb0[40].w == 1.000000);
    r6.x = dot(r10.xzy, float3(0.300000012,0.300000012,0.400000006));
    r6.x = saturate(0.5 * r6.x);
    r6.x = r7.y * -r6.x + 1;
    r7.yzw = float3(3000000,3000000,3000000) * r9.xyz;
    r8.xyw = v1.xyz * r2.xxx + -r7.yzw;
    r7.yzw = r6.xxx * r8.xyw + r7.yzw;
    r0.yzw = r4.www ? r7.yzw : r0.yzw;
  } else {
    r2.z = 1;
    r7.x = 1;
    r3.w = 1;
  }
  r4.w = saturate(-r9.z);
  r4.w = cb0[46].x * r4.w;
  r4.w = cmp(0 < r4.w);
  if (r4.w != 0) {
    r2.w = -cb0[46].y + r2.w;
    r2.w = saturate(-r2.w / cb0[46].z);
    r4.w = cmp(0 < r2.w);
    if (r4.w != 0) {
      r7.yz = cb1[123].xy / cb1[122].xy;
      r7.yz = v0.xy * r7.yz;
      r7.yz = t10.SampleLevel(s4_s, r7.yz, 0).xy;
      r4.w = 1 + -r7.z;
      r4.w = max(0, r4.w);
      r4.w = -1 + r4.w;
      r4.w = r2.w * r4.w + 1;
      r2.w = r7.y * r2.w;
      r7.yzw = cb0[47].xyz * r2.www;
      r3.xyz = r4.www * r3.xyz + r7.yzw;
      r2.w = r4.w * r2.z;
      r2.w = 0.699999988 * r2.w;
      o1.w = r2.z * 0.300000012 + r2.w;
    } else {
      o1.w = r2.z;
      r4.w = 1;
    }
  } else {
    o1.w = r2.z;
    r4.w = 1;
  }
  r2.z = saturate(r9.z / cb0[41].y);
  r2.z = log2(r2.z);
  r2.z = cb0[41].z * r2.z;
  r2.z = exp2(r2.z);
  r2.z = cb0[41].x * r2.z;
  r7.yzw = -cb2[1].xyz + cb0[42].xyz;
  r7.yzw = r2.zzz * r7.yzw + cb2[1].xyz;
  r8.xyw = cb0[42].xyz * cb0[53].www + -cb2[6].xyz;
  r8.xyw = r2.zzz * r8.xyw + cb2[6].xyz;
  r2.z = dot(r0.yzw, r0.yzw);
  r2.w = rsqrt(r2.z);
  r6.x = r2.z * r2.w;
  r10.xyz = r2.www * r0.yzw;
  r0.y = cmp(0.00999999978 < abs(r0.w));
  r0.y = r0.y ? r0.w : 0.00999999978;
  r0.z = cmp(r0.w < 0);
  r9.w = 9.99999975e-005 * r6.x;
  r9.w = min(1, r9.w);
  r10.w = saturate(abs(r0.w) / cb0[35].w);
  r11.x = -1 + cb0[35].z;
  r10.w = r10.w * r11.x + 1;
  r9.w = log2(r9.w);
  r9.w = 0.600000024 * r9.w;
  r9.w = exp2(r9.w);
  r11.x = 1 + -r10.w;
  r9.w = r9.w * r11.x + r10.w;
  r9.w = r9.w * r0.y;
  r0.y = r0.z ? r9.w : r0.y;
  r0.y = cb2[0].y * r0.y;
  r0.y = max(-127, r0.y);
  r0.z = exp2(-r0.y);
  r0.z = 1 + -r0.z;
  r0.z = cb2[0].x * r0.z;
  r0.y = r0.z / r0.y;
  r0.z = r2.z * r2.w + -cb2[0].w;
  r0.zw = max(float2(0,0), r0.zw);
  r0.z = r0.y * r0.z;
  r9.w = cmp(0 < cb2[5].w);
  r10.x = saturate(dot(r10.xyz, cb2[5].xyz));
  r10.x = log2(r10.x);
  r10.x = cb2[6].w * r10.x;
  r10.x = exp2(r10.x);
  r8.xyw = r10.xxx * r8.xyw;
  r2.z = r2.z * r2.w + -cb2[7].x;
  r2.z = max(0, r2.z);
  r0.y = r2.z * r0.y;
  r0.y = exp2(-r0.y);
  r0.y = min(1, r0.y);
  r0.y = 1 + -r0.y;
  r8.xyw = r8.xyw * r0.yyy;
  r10.xyz = r9.www ? r8.xyw : 0;
  r0.y = exp2(-r0.z);
  r0.y = min(1, r0.y);
  r10.w = max(cb2[1].w, r0.y);
  r0.y = cmp(0 < cb2[2].w);
  r0.z = cmp(cb2[2].w < r6.x);
  r0.y = r0.z ? r0.y : 0;
  r10.xyzw = r0.yyyy ? float4(0,0,0,1) : r10.xyzw;
  r0.y = 1 + -r10.w;
  r8.xyw = r7.yzw * r0.yyy + r10.xyz;
  r6.xyz = log2(r6.yzw);
  r6.xyz = cb0[35].yyy * r6.xyz;
  r6.xyz = exp2(r6.xyz);
  r6.xyz = cb0[35].xxx * r6.xyz;
  r0.z = dot(float3(0.200000003,0.699999988,0.300000012), r6.xyz);
  r2.z = max(0.200000003, r0.z);
  r0.z = 0.00100000005 + r0.z;
  r0.z = r2.z / r0.z;
  r10.xyz = r6.xyz * r0.zzz;
  r0.w = 0.000166666665 * r0.w;
  r0.w = min(1, r0.w);
  r2.z = log2(r10.w);
  r2.w = 0.600000024 * r2.z;
  r2.w = exp2(r2.w);
  r0.w = r0.w * 0.5 + -0.5;
  r6.w = saturate(cb0[35].x);
  r0.w = r2.w * r0.w + -0.199999988;
  r0.w = r6.w * r0.w + 1;
  r8.xyw = r0.www * r8.xyw;
  r8.xyw = r10.xyz * r0.yyy + r8.xyw;
  // Smooth Volumetric Light: the screen direction toward the sun's point on screen, where the view ray v1 (linear across the
  // screen) points along the sun direction cb0[53].xyz. Cramer's rule on v1 + ddx(v1) dx + ddy(v1) dy = k cb0[53].xyz;
  // the common divisor only flips the sign. Per pixel, then in volume uv (cb0[36].xy * v0.xy, as r11.xy below).
  const float3 ray_dx = ddx(v1.xyz);
  const float3 ray_dy = ddy(v1.xyz);
  const float2 shaft = float2(dot(v1.xyz, cross(ray_dy, cb0[53].xyz)), dot(ray_dx, cross(v1.xyz, cb0[53].xyz)))
                     * cb0[36].xy * float2(ddx(v0.x), ddy(v0.y));
  r11.xy = cb0[36].xy * v0.xy;
  r0.y = cmp(cb0[42].w == 0.000000);
  if (r0.y != 0) {
    r0.w = 0.5;
  }
  if (r0.y == 0) {
    r11.z = 0.000166666665 * r2.x;
    r0.w = t8.SampleLevel(s4_s, r11.xyz, 0).x;
  }
  r2.w = saturate(r9.z);
  r6.w = log2(r2.w);
  r6.w = 1.5 * r6.w;
  r6.w = exp2(r6.w);
  r6.w = r6.w + r6.w;
  r6.w = min(1, r6.w);
  r9.w = r9.z * 0.5 + 0.5;
  r9.w = saturate(1.81818175 * r9.w);
  r12.x = 1.85000002 * r0.w;
  r12.y = r9.w * r9.w;
  r9.w = r12.y * r9.w;
  r12.y = -r0.w * 1.85000002 + 0.300000012;
  r9.w = r9.w * r12.y + r12.x;
  r9.w = r9.w + -r0.w;
  r0.w = cb0[41].w * r9.w + r0.w;
  r9.w = r0.w * r0.w;
  r12.x = 3 * r9.w;
  r12.y = -r9.w * 3 + 1;
  r12.x = r6.w * r12.y + r12.x;
  r0.w = r0.w * 1.98000002 + 0.0199999996;
  r12.y = 1 + -r0.w;
  r0.w = r6.w * r12.y + r0.w;
  r0.w = r0.w * r2.z;
  r0.w = exp2(r0.w);
  r2.z = saturate(cb0[37].w);
  r0.w = r0.w + -r10.w;
  r0.w = r2.z * r0.w + r10.w;
  r6.w = -1 + r12.x;
  r2.z = r2.z * r6.w + 1;
  r6.w = r9.w + r9.w;
  r10.w = 1 + -r0.w;
  r12.x = log2(r10.w);
  r12.x = 0.800000012 * r12.x;
  r12.x = exp2(r12.x);
  r6.w = r12.x * r6.w;
  r12.xyz = -cb0[37].xyz + r7.yzw;
  r12.xyz = r12.xyz * float3(0.699999988,0.699999988,0.699999988) + cb0[37].xyz;
  r12.xyz = r12.xyz * r6.www;
  r12.xyz = cb0[37].www * r12.xyz;
  r6.xyz = -r6.xyz * r0.zzz + float3(1,1,1);
  r6.xyz = r6.xyz * float3(0.5,0.5,0.5) + r10.xyz;
  r6.xyz = r12.xyz * r6.xyz;
  r6.xyz = r8.xyw * r2.zzz + r6.xyz;
  r0.z = saturate(2.22222233 * r3.w);
  r2.z = cb0[40].x + -cb0[40].z;
  r0.z = r0.z * r2.z + cb0[40].z;
  r2.z = r7.x * r7.x;
  r3.w = 1 + -r0.z;
  r0.z = r2.z * r3.w + r0.z;
  r0.w = -1 + r0.w;
  r0.w = r0.z * r0.w + 1;
  r6.xyz = r0.zzz * r6.xyz;
  r6.xyz = r6.xyz * r5.xzw;
  r3.xyz = r0.www * r3.xyz + r6.xyz;
  r2.z = cmp(cb0[42].w != 0.000000);
  if (r2.z != 0) {
    if (r0.y != 0) {
      r2.z = 0;
    }
    if (r0.y == 0) {
      r3.w = cmp(480 >= r2.x);
      if (r3.w != 0) {
        r11.w = 0.100000001;
        r6.xyzw = SampleVisibility(t6, s4_s, r11.xyw, shaft, 1.f / cb0[0].z).xyzw;
        r3.w = 0.00833333377 * r2.x;
        r3.w = r3.w * r6.x;
        r8.xyw = r2.xxx * float3(0.00833333377,0.00833333377,0.00833333377) + float3(-1,-2,-3);
        r10.xyz = r6.yzw + -r6.xyz;
        r12.xyz = cmp(float3(120,240,360) >= r2.xxx);
        r6.xyz = r8.xyw * r10.xyz + r6.xyz;
        r6.y = r12.z ? r6.y : r6.z;
        r6.x = r12.y ? r6.x : r6.y;
        r2.z = r12.x ? r3.w : r6.x;
      } else {
        r3.w = cmp(960 >= r2.x);
        if (r3.w != 0) {
          r11.z = 0.300000012;
          r6.xyzw = SampleVisibility(t6, s4_s, r11.xyz, shaft, 1.f / cb0[0].z).xyzw;
          r3.w = -480 + r2.x;
          r8.x = cmp(120 >= r3.w);
          if (r8.x != 0) {
            r8.x = 0.00833333377 * r3.w;
            r11.w = 0.100000009;
            r8.y = SampleVisibility(t6, s4_s, r11.xyw, shaft, 1.f / cb0[0].z).w;
            r8.w = -r8.y + r6.x;
            r2.z = r8.x * r8.w + r8.y;
          } else {
            r8.xyw = r3.www * float3(0.00833333377,0.00833333377,0.00833333377) + float3(-1,-2,-3);
            r10.xyz = r6.yzw + -r6.xyz;
            r12.xy = cmp(float2(240,360) >= r3.ww);
            r6.xyz = r8.xyw * r10.xyz + r6.xyz;
            r3.w = r12.y ? r6.y : r6.z;
            r2.z = r12.x ? r6.x : r3.w;
          }
        } else {
          r3.w = cmp(1920 >= r2.x);
          if (r3.w != 0) {
            r11.z = 0.5;
            r6.xyzw = SampleVisibility(t6, s4_s, r11.xyz, shaft, 1.f / cb0[0].z).xyzw;
            r3.w = -960 + r2.x;
            r8.x = cmp(240 >= r3.w);
            if (r8.x != 0) {
              r8.x = 0.00416666688 * r3.w;
              r11.w = 0.300000012;
              r8.y = SampleVisibility(t6, s4_s, r11.xyw, shaft, 1.f / cb0[0].z).w;
              r8.w = -r8.y + r6.x;
              r2.z = r8.x * r8.w + r8.y;
            } else {
              r8.xyw = r3.www * float3(0.00416666688,0.00416666688,0.00416666688) + float3(-1,-2,-3);
              r10.xyz = r6.yzw + -r6.xyz;
              r12.xy = cmp(float2(480,720) >= r3.ww);
              r6.xyz = r8.xyw * r10.xyz + r6.xyz;
              r3.w = r12.y ? r6.y : r6.z;
              r2.z = r12.x ? r6.x : r3.w;
            }
          } else {
            r3.w = cmp(3840 >= r2.x);
            if (r3.w != 0) {
              r11.z = 0.699999988;
              r6.xyzw = SampleVisibility(t6, s4_s, r11.xyz, shaft, 1.f / cb0[0].z).xyzw;
              r3.w = -1920 + r2.x;
              r8.x = cmp(480 >= r3.w);
              if (r8.x != 0) {
                r8.x = 0.00208333344 * r3.w;
                r11.w = 0.5;
                r8.y = SampleVisibility(t6, s4_s, r11.xyw, shaft, 1.f / cb0[0].z).w;
                r8.w = -r8.y + r6.x;
                r2.z = r8.x * r8.w + r8.y;
              } else {
                r8.xyw = r3.www * float3(0.00208333344,0.00208333344,0.00208333344) + float3(-1,-2,-3);
                r10.xyz = r6.yzw + -r6.xyz;
                r12.xy = cmp(float2(960,1440) >= r3.ww);
                r6.xy = r8.xy * r10.xy + r6.xy;
                r3.w = min(1, r8.w);
                r3.w = r3.w * r10.z + r6.z;
                r3.w = r12.y ? r6.y : r3.w;
                r2.z = r12.x ? r6.x : r3.w;
              }
            } else {
              r11.z = 0.899999976;
              r6.xyzw = SampleVisibility(t6, s4_s, r11.xyz, shaft, 1.f / cb0[0].z).xyzw;
              r3.w = -3840 + r2.x;
              r8.x = cmp(960 >= r3.w);
              if (r8.x != 0) {
                r8.x = 0.00104166672 * r3.w;
                r11.w = 0.699999988;
                r8.y = SampleVisibility(t6, s4_s, r11.xyw, shaft, 1.f / cb0[0].z).w;
                r8.w = -r8.y + r6.x;
                r2.z = r8.x * r8.w + r8.y;
              } else {
                r8.xyw = r3.www * float3(0.00104166672,0.00104166672,0.00104166672) + float3(-1,-2,-3);
                r10.xyz = r6.yzw + -r6.xyz;
                r12.xy = cmp(float2(1920,2880) >= r3.ww);
                r6.xy = r8.xy * r10.xy + r6.xy;
                r3.w = min(1, r8.w);
                r3.w = r3.w * r10.z + r6.z;
                r3.w = r12.y ? r6.y : r3.w;
                r2.z = r12.x ? r6.x : r3.w;
              }
            }
          }
        }
      }
    }
    r2.z = 1 + -r2.z;
    r3.w = dot(cb0[53].xyz, r9.xyz);
    r6.x = r3.w * 0.5 + 0.5;
    r3.w = -r3.w * 0.600000024 + 1.09000003;
    r3.w = log2(r3.w);
    r3.w = 1.5 * r3.w;
    r3.w = exp2(r3.w);
    r3.w = 12.5663681 * r3.w;
    r3.w = 0.910000026 / r3.w;
    r3.w = r3.w + r3.w;
    r3.w = min(1, r3.w);
    r6.y = -2500 + r2.x;
    r6.y = saturate(9.99999975e-005 * r6.y);
    r6.z = r3.w * r10.w;
    r6.y = r6.y * -0.699999988 + 1;
    r6.y = r6.z * r6.y;
    r6.z = min(5, cb0[53].w);
    r6.y = r6.z * r6.y;
    r6.yzw = cb0[37].xyz * r6.yyy;
    r8.x = r6.x * -2.79999995 + 3;
    r8.y = r2.z * r2.z;
    r8.w = r8.y * r8.y;
    r8.y = -r8.y * r8.w + 1;
    r3.w = log2(r3.w);
    r3.w = 0.300000012 * r3.w;
    r3.w = exp2(r3.w);
    r3.w = r8.y * r3.w;
    r8.xyw = r8.xxx * cb0[38].xyz + float3(-1,-1,-1);
    r8.xyw = r8.xyw * r3.www;
    r3.w = 1 + -cb0[38].w;
    r3.w = r6.x * r3.w + cb0[38].w;
    r8.xyw = r3.www * r8.xyw + float3(1,1,1);
    r2.w = 0.5 + -r2.w;
    r2.w = max(0, r2.w);
    r2.w = r2.w + r2.w;
    r3.w = r2.w * r2.w;
    r2.w = -r2.w * r2.w + 1;
    r2.w = cb0[36].w * r2.w + r3.w;
    r2.z = log2(r2.z);
    r2.z = 1.5 * r2.z;
    r2.z = exp2(r2.z);
    r2.z = r2.w * r2.z;
    r0.z = r2.z * r0.z;
    r6.xyz = r0.zzz * r6.yzw;
    r5.xzw = r6.xyz * r5.xzw;
    // Sun Haze: the haze's tint to the slider's power (stays positive above 1), its in-scattered light x the slider.
    r3.xyz = r3.xyz * pow(max(r8.xyw, 0.f), SI.sun_haze) + r5.xzw * SI.sun_haze;
  } else {
    r8.xyw = float3(1,1,1);
  }
  r0.z = cmp(0 < cb0[39].x);
  if (r0.z != 0) {
    r0.z = -250 + r2.x;
    r11.z = 0.000125000006 * r0.z;
    r5.xzw = t7.SampleLevel(s4_s, r11.xyz, 0).xyz;
    r11.z = saturate(r11.z);
    r5.xzw = r11.zzz * r5.xzw;
    r0.z = 3.5 * r9.w;
    r5.xzw = r5.xzw * r0.zzz;
    r6.xyz = float3(-1,-1,-1) + r7.yzw;
    r6.xyz = r6.xyz * float3(0.200000003,0.200000003,0.200000003) + float3(1,1,1);
    r3.xyz = (r5.xzw * SI.sun_haze) * r6.xyz + r3.xyz;
  }
  if (r0.y != 0) {
    r5.xzw = float3(0,0,0);
  }
  if (r0.y == 0) {
    r0.y = cmp(0.00999999978 >= cb0[45].x);
    if (r0.y != 0) {
      r5.xzw = float3(0,0,0);
    }
    if (r0.y == 0) {
      r0.y = dot(r9.xyz, cb0[53].xyz);
      r0.y = saturate(r0.y * 0.5 + 0.600000024);
      r0.z = 10 * cb0[45].y;
      r0.y = log2(r0.y);
      r0.y = r0.z * r0.y;
      r0.y = exp2(r0.y);
      r0.z = r9.z * 0.5 + 0.300000012;
      r0.z = saturate(1.25 * r0.z);
      r2.z = cb0[45].z * 4.98999977 + 0.00999999978;
      r0.z = log2(r0.z);
      r0.z = r2.z * r0.z;
      r0.z = exp2(r0.z);
      r0.y = r0.y * r0.z;
      r0.z = 0.00100000005 + cb0[45].w;
      r0.z = saturate(r9.z / r0.z);
      r0.z = r0.z * r0.z;
      r6.xyz = -cb0[44].xyz + cb0[43].xyz;
      r6.xyz = r0.zzz * r6.xyz + cb0[44].xyz;
      r0.y = cb0[45].x * r0.y;
      r0.z = saturate(0.100000001 * cb0[53].w);
      r0.z = log2(r0.z);
      r0.z = 1.5 * r0.z;
      r0.z = exp2(r0.z);
      r0.y = r0.y * r0.z;
      r6.xyz = r6.xyz * r8.xyw;
      r5.xzw = r6.xyz * r0.yyy;
    }
  }
  r3.xyz = r5.xzw + r3.xyz;
  r0.y = r0.w * r7.x;
  r0.y = r0.y * r4.w;
  o2.xyzw = r1.wwww * r0.yyyy;
  r0.y = cmp(cb0[76].x != 0.000000);
  r1.xyz = r3.xyz * r2.yyy + r1.xyz;
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