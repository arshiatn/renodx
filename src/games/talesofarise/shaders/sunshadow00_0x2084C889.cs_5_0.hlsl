// Tales of Arise sun shadow mask (Bandai): the sun's shadow map filtered per pixel, cloud and far shadows.
// From 0x2084C889.cs_5_0.cso (3Dmigoto, 2026-10-09 dumps; declarations and the store fixed).
#include "../shared.h"

Texture2D<float4> t4 : register(t4);

Texture3D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

// Ray-traced sun shadows (the DLSS addon's Experimental option, rt_shadows.hpp): its visibility (0 blocked, 1 lit),
// bound only while it runs.
Texture2D<float> t40 : register(t40);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[133];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[45];
}




// 3Dmigoto declarations
#define cmp -

// Smooth Sun Shadows. Bandai filters the sun's single 8192 shadow map with Unreal's 5 x 5 PCF: 9 Gathers (6 x 6
// texels), each compared softly (saturate((depth - receiver) * cb0[0].z + 1)) and summed with box weights (1 inside,
// fading to 0 over the outer texel). A box follows the texel grid: when the sun shadow distance spreads the map over
// more ground (the DLSS addon's Sun shadow distance above the game's; SI.sun_shadow_distance is the ratio), texels grow
// on screen and edges show soft stairs. Then the same 9 Gathers are weighted by a tent around the sample point:
// Smooth half-width 3 texels, as wide an edge as the box (v48's wider tent, up to 5, made shadows blurrier and the
// penumbra's edge color wider: ATN debug15), no stairs; Sharp / Sharper / Sharpest 2 / 1.5 / 1.2 texels (ATN tests:
// harder edges, a thinner edge color; near 1 the texel grid shows again). At the game's distance Bandai's filter stays.
// Gather order: x (0, 1), y (1, 1), z (1, 0), w (0, 0); `center` is the texel at floor(p) (its centre), `f` frac(p).
float SunShadowTent(float2 center, float2 f, float receiver, float half_width) {
  float sum = 0.f;
  float weights = 0.f;
  [unroll]
  for (int y = -2; y <= 2; y += 2) {
    [unroll]
    for (int x = -2; x <= 2; x += 2) {
      const float4 lit = saturate((t1.Gather(s1_s, center, int2(x, y)) - receiver) * cb0[0].z + 1.f);
      const float2 near = max(0.f, 1.f - abs(float2(x, y) - f) / half_width);
      const float2 far = max(0.f, 1.f - abs(float2(x + 1, y + 1) - f) / half_width);
      const float4 weight = float4(near.x * far.y, far.x * far.y, far.x * near.y, near.x * near.y);
      sum += dot(lit, weight);
      weights += dot(weight, 1.f);
    }
  }
  return sum / weights;
}



RWTexture2D<float4> u0 : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (uint2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r0.zw = cb1[123].zw * r0.xy;
  r1.x = t0.SampleLevel(s0_s, r0.zw, 0).x;
  r1.y = r1.x * cb1[57].x + cb1[57].y;
  r1.x = r1.x * cb1[57].z + -cb1[57].w;
  r1.x = 1 / r1.x;
  r1.x = r1.y + r1.x;
  r0.xy = r0.xy * cb1[123].zw + -cb1[58].wz;
  r0.xy = r0.xy / cb1[58].xy;
  r0.xy = r0.xy * r1.xx;
  r1.yzw = cb1[45].xyz * r0.yyy;
  r1.yzw = r0.xxx * cb1[44].xyz + r1.yzw;
  r1.yzw = r1.xxx * cb1[46].xyz + r1.yzw;
  r1.yzw = cb1[47].xyz + r1.yzw;
  r2.x = t2.SampleLevel(s2_s, r0.zw, 0).w;
  r2.y = cmp(r2.x == 1.000000);
  if (r2.y != 0) {
    r2.z = cmp(cb0[40].w == 0.000000);
    if (r2.z != 0) {
      r2.w = 1;
    }
    if (r2.z == 0) {
      r2.z = -300000 + r1.x;
      r2.z = saturate(9.99999975e-006 * r2.z);
      r3.x = cmp(r2.z >= 0.999000013);
      if (r3.x != 0) {
        r2.w = 1;
      }
      if (r3.x == 0) {
        r3.x = -cb0[42].x + r1.x;
        r3.x = saturate(r3.x / cb0[42].y);
        r3.y = cmp(r3.x < 0.00100000005);
        if (r3.y != 0) {
          r3.y = 100 / cb0[39].x;
          r3.z = 0.00999999978 * cb1[132].z;
          r3.zw = cb0[39].yz * r3.zz;
          r3.zw = r3.zw * r3.yy;
          r4.xy = r3.yy * r1.yz + r3.zw;
          r4.z = dot(cb1[132].zz, cb0[39].ww);
          r3.yz = t3.SampleLevel(s3_s, r4.xyz, 0).xw;
          r3.w = dot(r3.zz, r3.yy);
          r3.w = r3.w + -r3.y;
          r3.w = cb0[40].y * r3.w + r3.y;
          r3.z = cb0[40].z * r3.z;
          r3.y = 1 + -r3.y;
          r3.y = r3.z * r3.y;
          r3.y = saturate(r3.y * 3 + r3.w);
        } else {
          r3.z = cmp(r3.x < 1);
          if (r3.z != 0) {
            r3.z = 100 / cb0[39].x;
            r3.w = 0.00999999978 * cb1[132].z;
            r4.xy = cb0[39].yz * r3.ww;
            r4.xy = r4.xy * r3.zz;
            r4.xy = r3.zz * r1.yz + r4.xy;
            r4.z = dot(cb1[132].zz, cb0[39].ww);
            r5.xy = t3.SampleLevel(s3_s, r4.xyz, 0).xw;
            r3.z = dot(r5.yy, r5.xx);
            r3.z = r3.z + -r5.x;
            r3.z = cb0[40].y * r3.z + r5.x;
            r4.w = cb0[40].z * r5.y;
            r5.x = 1 + -r5.x;
            r4.w = r5.x * r4.w;
            r3.z = saturate(r4.w * 3 + r3.z);
            r4.w = 100 / cb0[41].x;
            r5.xy = cb0[41].yz * r3.ww;
            r5.xy = r5.xy * r4.ww;
            r4.xy = r4.ww * r1.yz + r5.xy;
            r4.xy = t3.SampleLevel(s3_s, r4.xyz, 0).xw;
            r3.w = dot(r4.yy, r4.xx);
            r3.w = r3.w + -r4.x;
            r3.w = cb0[40].y * r3.w + r4.x;
            r4.y = cb0[40].z * r4.y;
            r4.x = 1 + -r4.x;
            r4.x = r4.y * r4.x;
            r3.w = saturate(r4.x * 3 + r3.w);
            r3.w = r3.w + -r3.z;
            r3.y = r3.x * r3.w + r3.z;
          } else {
            r3.x = 100 / cb0[41].x;
            r3.z = 0.00999999978 * cb1[132].z;
            r3.zw = cb0[41].yz * r3.zz;
            r3.zw = r3.zw * r3.xx;
            r4.xy = r3.xx * r1.yz + r3.zw;
            r4.z = dot(cb1[132].zz, cb0[39].ww);
            r3.xz = t3.SampleLevel(s3_s, r4.xyz, 0).xw;
            r3.w = dot(r3.zz, r3.xx);
            r3.w = r3.w + -r3.x;
            r3.w = cb0[40].y * r3.w + r3.x;
            r3.z = cb0[40].z * r3.z;
            r3.x = 1 + -r3.x;
            r3.x = r3.z * r3.x;
            r3.y = saturate(r3.x * 3 + r3.w);
          }
        }
        r3.x = 1 + -r3.y;
        r3.y = 1 + -cb0[40].x;
        r3.x = r3.x * r3.y + cb0[40].x;
        r3.y = 1 + -r3.x;
        r2.w = r2.z * r3.y + r3.x;
      }
    }
    r2.z = cmp(cb0[43].w == 0.000000);
    if (r2.z != 0) {
      r2.z = cmp(5000 < r1.x);
      r3.x = cmp(0 < cb0[43].x);
      r2.z = r2.z ? r3.x : 0;
      if (r2.z != 0) {
        r2.z = t4.SampleLevel(s4_s, r0.zw, 0).x;
      } else {
        r2.z = 1;
      }
      r3.x = -10000 + r1.x;
      r3.x = saturate(9.99999975e-005 * r3.x);
      r3.y = cb0[43].y + -cb0[43].x;
      r3.x = r3.x * r3.y + cb0[43].x;
      r2.z = -1 + r2.z;
      r2.z = r3.x * r2.z + 1;
      r3.x = 1;
    } else {
      r3.y = -4000 + r1.x;
      r3.y = saturate(0.00100000005 * r3.y);
      r3.x = 1 + -r3.y;
      r2.z = r3.x;
    }
    r2.z = r2.w * r2.z;
    r4.xyz = sqrt(r2.zzz);
    r4.w = sqrt(r3.x);
  }
  if (r2.y == 0) {
    r1.w = -cb0[44].y + r1.w;
    r1.w = saturate(-r1.w / cb0[44].z);
    r1.w = cb0[44].x * r1.w;
    r2.x = cmp(r2.x < 0.00999999978);
    r2.y = cmp(r1.w == 0.000000);
    r2.x = r2.y ? r2.x : 0;
    if (r2.x != 0) {
      r2.xyzw = float4(0,0,0,0);
    } else {
      r3.xyzw = cb0[29].xyzw * r0.yyyy;
      r3.xyzw = r0.xxxx * cb0[28].xyzw + r3.xyzw;
      r3.xyzw = r1.xxxx * cb0[30].xyzw + r3.xyzw;
      r3.xyzw = cb0[31].xyzw + r3.xyzw;
      r0.xy = r3.xy / r3.ww;
      r3.x = min(0.999989986, r3.z);
      r0.xy = r0.xy * cb0[1].xy + float2(-0.5,-0.5);
      r3.yz = frac(r0.xy);
      r0.xy = floor(r0.xy);
      r0.xy = float2(0.5,0.5) + r0.xy;
      r0.xy = cb0[1].zw * r0.xy;
      [branch]
      if (SI.sun_shadow_smoothing != 0.f && SI.sun_shadow_distance > 1.f) {
        r2.w = SunShadowTent(r0.xy, r3.yz, r3.x, 6.f / (SI.sun_shadow_smoothing + 1.f));
      } else {
        r5.xyzw = t1.Gather(s1_s, r0.xy, int2(-2, -2)).xyzw;
        r5.xyzw = r5.xyzw + -r3.xxxx;
        r5.xyzw = saturate(r5.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r6.xyzw = t1.Gather(s1_s, r0.xy, int2(0, -2)).xyzw;
        r6.xyzw = r6.xyzw + -r3.xxxx;
        r6.xyzw = saturate(r6.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r7.xyzw = t1.Gather(s1_s, r0.xy, int2(2, -2)).xyzw;
        r7.xyzw = r7.xyzw + -r3.xxxx;
        r7.xyzw = saturate(r7.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r8.xy = float2(1,1) + -r3.yz;
        r5.xy = r5.wx * r8.xx + r5.zy;
        r5.xy = r5.xy + r6.wx;
        r5.xy = r5.xy + r6.zy;
        r5.xy = r5.xy + r7.wx;
        r5.xy = r7.zy * r3.yy + r5.xy;
        r3.w = r5.x * r8.y + r5.y;
        r5.xyzw = t1.Gather(s1_s, r0.xy, int2(-2, 0)).xyzw;
        r5.xyzw = r5.xyzw + -r3.xxxx;
        r5.xyzw = saturate(r5.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r6.xyzw = t1.Gather(s1_s, r0.xy).xyzw;
        r6.xyzw = r6.xyzw + -r3.xxxx;
        r6.xyzw = saturate(r6.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r7.xyzw = t1.Gather(s1_s, r0.xy, int2(2, 0)).xyzw;
        r7.xyzw = r7.xyzw + -r3.xxxx;
        r7.xyzw = saturate(r7.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r5.xy = r5.wx * r8.xx + r5.zy;
        r5.xy = r5.xy + r6.wx;
        r5.xy = r5.xy + r6.zy;
        r5.xy = r5.xy + r7.wx;
        r5.xy = r7.zy * r3.yy + r5.xy;
        r5.x = r5.x + r5.y;
        r3.w = r5.x + r3.w;
        r5.xyzw = t1.Gather(s1_s, r0.xy, int2(-2, 2)).xyzw;
        r5.xyzw = r5.xyzw + -r3.xxxx;
        r5.xyzw = saturate(r5.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r6.xyzw = t1.Gather(s1_s, r0.xy, int2(0, 2)).xyzw;
        r6.xyzw = r6.xyzw + -r3.xxxx;
        r6.xyzw = saturate(r6.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r7.xyzw = t1.Gather(s1_s, r0.xy, int2(2, 2)).xyzw;
        r7.xyzw = r7.xyzw + -r3.xxxx;
        r7.xyzw = saturate(r7.xyzw * cb0[0].zzzz + float4(1,1,1,1));
        r0.xy = r5.wx * r8.xx + r5.zy;
        r0.xy = r0.xy + r6.wx;
        r0.xy = r0.xy + r6.zy;
        r0.xy = r0.xy + r7.wx;
        r0.xy = r7.zy * r3.yy + r0.xy;
        r0.x = r0.y * r3.z + r0.x;
        r0.x = r3.w + r0.x;
        r2.w = 0.0399999991 * r0.x;
      }
      // Thin Sun Shadow Edges: either filter's edge is about 3 texels wide, so it widens with the sun shadow distance
      // (ATN: a wide halo at 4 x and 8192). A contrast around half shadow narrows it by the distance ratio (at 1: the
      // game's width on the ground); where the edge lies still comes from the smooth filter (no stairs, little flicker).
      if (SI.sun_shadow_edge > 0.f && SI.sun_shadow_distance > 1.f) {
        r2.w = saturate((r2.w - 0.5f) * (1.f + (SI.sun_shadow_distance - 1.f) * SI.sun_shadow_edge) + 0.5f);
      }
      // Ray-traced sun shadows: the darker of the shadow map and the traced visibility (shadows from solid static
      // geometry at every distance; leaves and characters stay the shadow map's).
      uint traced_width, traced_height;
      t40.GetDimensions(traced_width, traced_height);
      if (traced_width != 0u) {
        r2.w = min(r2.w, t40.Load(int3(vThreadID.xy, 0)));
      }
      r0.x = cmp(cb0[40].w == 0.000000);
      if (r0.x != 0) {
        r2.x = 1;
      }
      if (r0.x == 0) {
        r0.x = -300000 + r1.x;
        r0.x = saturate(9.99999975e-006 * r0.x);
        r0.y = cmp(r0.x >= 0.999000013);
        if (r0.y != 0) {
          r2.x = 1;
        }
        if (r0.y == 0) {
          r0.y = -cb0[42].x + r1.x;
          r0.y = saturate(r0.y / cb0[42].y);
          r3.x = cmp(r0.y < 0.00100000005);
          if (r3.x != 0) {
            r3.x = 100 / cb0[39].x;
            r3.y = 0.00999999978 * cb1[132].z;
            r3.yz = cb0[39].yz * r3.yy;
            r3.yz = r3.yz * r3.xx;
            r3.xy = r3.xx * r1.yz + r3.yz;
            r3.z = dot(cb1[132].zz, cb0[39].ww);
            r3.xy = t3.SampleLevel(s3_s, r3.xyz, 0).xw;
            r3.z = dot(r3.yy, r3.xx);
            r3.z = r3.z + -r3.x;
            r3.z = cb0[40].y * r3.z + r3.x;
            r3.y = cb0[40].z * r3.y;
            r3.x = 1 + -r3.x;
            r3.x = r3.y * r3.x;
            r3.x = saturate(r3.x * 3 + r3.z);
          } else {
            r3.y = cmp(r0.y < 1);
            if (r3.y != 0) {
              r3.y = 100 / cb0[39].x;
              r3.z = 0.00999999978 * cb1[132].z;
              r5.xy = cb0[39].yz * r3.zz;
              r5.xy = r5.xy * r3.yy;
              r5.xy = r3.yy * r1.yz + r5.xy;
              r5.z = dot(cb1[132].zz, cb0[39].ww);
              r3.yw = t3.SampleLevel(s3_s, r5.xyz, 0).xw;
              r5.w = dot(r3.ww, r3.yy);
              r5.w = r5.w + -r3.y;
              r5.w = cb0[40].y * r5.w + r3.y;
              r3.w = cb0[40].z * r3.w;
              r3.y = 1 + -r3.y;
              r3.y = r3.w * r3.y;
              r3.y = saturate(r3.y * 3 + r5.w);
              r3.w = 100 / cb0[41].x;
              r6.xy = cb0[41].yz * r3.zz;
              r6.xy = r6.xy * r3.ww;
              r5.xy = r3.ww * r1.yz + r6.xy;
              r3.zw = t3.SampleLevel(s3_s, r5.xyz, 0).xw;
              r5.x = dot(r3.ww, r3.zz);
              r5.x = r5.x + -r3.z;
              r5.x = cb0[40].y * r5.x + r3.z;
              r3.w = cb0[40].z * r3.w;
              r3.z = 1 + -r3.z;
              r3.z = r3.w * r3.z;
              r3.z = saturate(r3.z * 3 + r5.x);
              r3.z = r3.z + -r3.y;
              r3.x = r0.y * r3.z + r3.y;
            } else {
              r0.y = 100 / cb0[41].x;
              r3.y = 0.00999999978 * cb1[132].z;
              r3.yz = cb0[41].yz * r3.yy;
              r3.yz = r3.yz * r0.yy;
              r5.xy = r0.yy * r1.yz + r3.yz;
              r5.z = dot(cb1[132].zz, cb0[39].ww);
              r1.yz = t3.SampleLevel(s3_s, r5.xyz, 0).xw;
              r0.y = dot(r1.zz, r1.yy);
              r0.y = r0.y + -r1.y;
              r0.y = cb0[40].y * r0.y + r1.y;
              r1.z = cb0[40].z * r1.z;
              r1.y = 1 + -r1.y;
              r1.y = r1.z * r1.y;
              r3.x = saturate(r1.y * 3 + r0.y);
            }
          }
          r0.y = 1 + -r3.x;
          r1.y = 1 + -cb0[40].x;
          r0.y = r0.y * r1.y + cb0[40].x;
          r1.y = 1 + -r0.y;
          r2.x = r0.x * r1.y + r0.y;
        }
      }
      r0.x = cmp(cb0[43].w == 0.000000);
      if (r0.x != 0) {
        r0.x = cmp(5000 < r1.x);
        r0.y = cmp(0 < cb0[43].x);
        r0.x = r0.y ? r0.x : 0;
        if (r0.x != 0) {
          r0.x = t4.SampleLevel(s4_s, r0.zw, 0).x;
        } else {
          r0.x = 1;
        }
        r0.y = -10000 + r1.x;
        r0.y = saturate(9.99999975e-005 * r0.y);
        r0.z = cb0[43].y + -cb0[43].x;
        r0.y = r0.y * r0.z + cb0[43].x;
        r0.x = -1 + r0.x;
        r2.y = r0.y * r0.x + 1;
        r2.z = 1;
      } else {
        r0.x = -4000 + r1.x;
        r0.x = saturate(0.00100000005 * r0.x);
        r2.yz = float2(1,1) + -r0.xx;
      }
    }
    r0.x = 1 + -r2.w;
    r0.x = r1.w * r0.x + r2.w;
    r0.y = -0.5 + r0.x;
    r0.y = saturate(r0.y * cb0[26].z + 0.5);
    r0.y = r0.y * r0.y + -1;
    r0.y = cb0[26].y * r0.y + 1;
    r0.x = r0.x * r0.x + -1;
    r0.x = cb0[26].y * r0.x + 1;
    r0.xyz = r0.xyy * r2.yyz;
    r0.xy = r0.xy * r2.xx;
    r4.xyzw = sqrt(r0.yxyz);
  }
u0[vThreadID.xy] = r4.xyzw;
  return;
}