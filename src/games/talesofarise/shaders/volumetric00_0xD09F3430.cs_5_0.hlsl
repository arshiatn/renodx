// Tales of Arise volumetric lighting (Bandai): the sun's visibility per cell (u0, 5 slices) and the light
// scattering (u1, 16 slices), marching the 2048 closest-depth shadow map. From 0xD09F3430.cs_5_0.cso (3Dmigoto,
// 2026-10-09 dumps; declarations, stores and the integer subtraction on cb0[19] fixed against the bytecode).
#include "../shared.h"

Texture3D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[133];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[27];
}




// 3Dmigoto declarations
#define cmp -

// Smooth Volumetric Light (Soft / Along Sun Rays): each of the march's 160 shadow lookups weights the 2 x 2 texels
// Gather returns bilinearly instead of averaging them (Bandai), so the sun's visibility no longer steps at every texel
// of the 2048 shadow map (closest depth of 4 x 4 of the 8192 map, cs 0x81785A81): with Max volumetric fog's 2-pixel
// cells those steps showed as stair-stepped light shafts (ATN, debug14); the game's 8-pixel cells hid them. Same mean.
// Gather order: x (0, 1), y (1, 1), z (1, 0), w (0, 0).
float ShadowGatherVisibility(float4 lit, float2 uv) {
  if (SI.volumetric_light_smoothing == 0.f) return 0.25f * (lit.x + lit.y + lit.z + lit.w);
  float2 size;
  t0.GetDimensions(size.x, size.y);
  const float2 f = frac(uv * size - 0.5f);
  return lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);
}



RWTexture3D<float4> u0 : register(u0);
RWTexture3D<float4> u1 : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = asint(cb0[19].zw) - asint(cb0[19].xy);
  r0.xy = (uint2)r0.xy;
  r0.zw = (uint2)vThreadID.xy;
  r0.zw = cb0[20].xx * r0.zw;
  r0.xy = r0.zw / r0.xy;
  r0.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r0.xy = float2(10,10) * r0.xy;
  r1.xyzw = cb1[45].xyzw * r0.yyyy;
  r0.xyzw = r0.xxxx * cb1[44].xyzw + r1.xyzw;
  r0.xyzw = cb1[46].xyzw * float4(10,10,10,10) + r0.xyzw;
  r0.xyzw = cb1[47].xyzw + r0.xyzw;
  r0.xyz = r0.xyz / r0.www;
  r1.xyz = cb1[62].xyz + r0.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r2.xyz = cb0[22].xyz * r0.yyy;
  r2.xyz = r0.xxx * cb0[21].xyz + r2.xyz;
  r2.xyz = r0.zzz * cb0[23].xyz + r2.xyz;
  r2.xyz = cb0[24].xyz + r2.xyz;
  r3.xy = cb0[20].zz * float2(15,-0.125) + float2(5,0.200000003);
  r3.zw = float2(2,0.200000003) + -r3.xy;
  r3.xy = cb0[20].ww * r3.zw + r3.xy;
  r4.xyz = r1.xyz * float3(15,15,15) + r0.xyz;
  r5.xyz = cb0[22].xyz * r4.yyy;
  r4.xyw = r4.xxx * cb0[21].xyz + r5.xyz;
  r4.xyz = r4.zzz * cb0[23].xyz + r4.xyw;
  r4.xyz = cb0[24].xyz + r4.xyz;
  r4.xyz = r4.xyz + -r2.xyz;
  r5.xyz = r2.xyz;
  r6.x = 0;
  r0.w = 0;
  while (true) {
    r1.w = cmp((uint)r0.w >= 8);
    if (r1.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r5.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r5.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r1.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r5.xy);
    r2.w = -r6.x * 0.400000006 + 1;
    r1.w = r2.w * r1.w;
    r6.x = r1.w * 0.0299999993 + r6.x;
    r5.xyz = r5.xyz + r4.xyz;
    r0.w = (int)r0.w + 1;
  }
  r2.xyz = r5.xyz;
  r6.y = r6.x;
  r0.w = 0;
  while (true) {
    r1.w = cmp((uint)r0.w >= 8);
    if (r1.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r2.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r1.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r2.xy);
    r2.w = -r6.y * 0.400000006 + 1;
    r1.w = r2.w * r1.w;
    r6.y = r1.w * 0.0299999993 + r6.y;
    r2.xyz = r2.xyz + r4.xyz;
    r0.w = (int)r0.w + 1;
  }
  r5.xyz = r2.xyz;
  r6.z = r6.y;
  r0.w = 0;
  while (true) {
    r1.w = cmp((uint)r0.w >= 8);
    if (r1.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r5.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r5.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r1.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r5.xy);
    r2.w = -r6.z * 0.400000006 + 1;
    r1.w = r2.w * r1.w;
    r6.z = r1.w * 0.0299999993 + r6.z;
    r5.xyz = r5.xyz + r4.xyz;
    r0.w = (int)r0.w + 1;
  }
  r2.xyz = r5.xyz;
  r6.w = r6.z;
  r0.w = 0;
  while (true) {
    r1.w = cmp((uint)r0.w >= 8);
    if (r1.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r2.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r1.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r2.xy);
    r2.w = -r6.w * 0.400000006 + 1;
    r1.w = r2.w * r1.w;
    r6.w = r1.w * 0.0299999993 + r6.w;
    r2.xyz = r2.xyz + r4.xyz;
    r0.w = (int)r0.w + 1;
  }
  u0[uint3(vThreadID.xy, 0)] = r6.xyzw;
  r0.w = 1 + -r6.w;
  r5.xyz = r2.xyz;
  r7.x = r6.w;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r8.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r9.xyzw = cmp(r8.xyzw < r5.zzzz);
    r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
    r8.xyzw = -r8.xyzw + r5.zzzz;
    r8.xyzw = r8.xyzw + -r3.yyyy;
    r8.xyzw = saturate(r8.xyzw * r3.xxxx);
    r8.xyzw = float4(1,1,1,1) + -r8.xyzw;
    r3.zw = r9.xy * r8.xy;
    r2.w = ShadowGatherVisibility(r9.xyzw * r8.xyzw, r5.xy);
    r3.z = -r7.x * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r7.x = r2.w * 0.0299999993 + r7.x;
    r5.xyz = r5.xyz + r4.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = r5.xyz;
  r7.y = r7.x;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r6.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r6.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r6.xyzw = -r6.xyzw + r2.zzzz;
    r6.xyzw = r6.xyzw + -r3.yyyy;
    r6.xyzw = saturate(r6.xyzw * r3.xxxx);
    r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
    r3.zw = r8.xy * r6.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r6.xyzw, r2.xy);
    r3.z = -r7.y * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r7.y = r2.w * 0.0299999993 + r7.y;
    r2.xyz = r2.xyz + r4.xyz;
    r1.w = (int)r1.w + 1;
  }
  r5.xyz = r2.xyz;
  r7.z = r7.y;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r6.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r8.xyzw = cmp(r6.xyzw < r5.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r6.xyzw = -r6.xyzw + r5.zzzz;
    r6.xyzw = r6.xyzw + -r3.yyyy;
    r6.xyzw = saturate(r6.xyzw * r3.xxxx);
    r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
    r3.zw = r8.xy * r6.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r6.xyzw, r5.xy);
    r3.z = -r7.z * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r7.z = r2.w * 0.0299999993 + r7.z;
    r5.xyz = r5.xyz + r4.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = r5.xyz;
  r7.w = r7.z;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r6.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r6.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r6.xyzw = -r6.xyzw + r2.zzzz;
    r6.xyzw = r6.xyzw + -r3.yyyy;
    r6.xyzw = saturate(r6.xyzw * r3.xxxx);
    r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
    r3.zw = r8.xy * r6.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r6.xyzw, r2.xy);
    r3.z = -r7.w * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r7.w = r2.w * 0.0299999993 + r7.w;
    r2.xyz = r2.xyz + r4.xyz;
    r1.w = (int)r1.w + 1;
  }
  r5.xyzw = r7.xyzw * r0.wwww;
  u0[uint3(vThreadID.xy, 1)] = r5.xyzw;
  r5.xyz = r2.xyz;
  r6.x = r7.w;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r8.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r9.xyzw = cmp(r8.xyzw < r5.zzzz);
    r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
    r8.xyzw = -r8.xyzw + r5.zzzz;
    r8.xyzw = r8.xyzw + -r3.yyyy;
    r8.xyzw = saturate(r8.xyzw * r3.xxxx);
    r8.xyzw = float4(1,1,1,1) + -r8.xyzw;
    r3.zw = r9.xy * r8.xy;
    r2.w = ShadowGatherVisibility(r9.xyzw * r8.xyzw, r5.xy);
    r3.z = -r6.x * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r6.x = r2.w * 0.0299999993 + r6.x;
    r5.xyz = r4.xyz * float3(2,2,2) + r5.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = r5.xyz;
  r6.y = r6.x;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r2.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r2.xy);
    r3.z = -r6.y * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r6.y = r2.w * 0.0299999993 + r6.y;
    r2.xyz = r4.xyz * float3(2,2,2) + r2.xyz;
    r1.w = (int)r1.w + 1;
  }
  r5.xyz = r2.xyz;
  r6.z = r6.y;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r5.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r5.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r5.xy);
    r3.z = -r6.z * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r6.z = r2.w * 0.0299999993 + r6.z;
    r5.xyz = r4.xyz * float3(2,2,2) + r5.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = r5.xyz;
  r6.w = r6.z;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r2.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r2.xy);
    r3.z = -r6.w * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r6.w = r2.w * 0.0299999993 + r6.w;
    r2.xyz = r4.xyz * float3(2,2,2) + r2.xyz;
    r1.w = (int)r1.w + 1;
  }
  r5.xyzw = r6.xyzw * r0.wwww;
  u0[uint3(vThreadID.xy, 2)] = r5.xyzw;
  r5.xyz = r2.xyz;
  r7.x = r6.w;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r8.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r9.xyzw = cmp(r8.xyzw < r5.zzzz);
    r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
    r8.xyzw = -r8.xyzw + r5.zzzz;
    r8.xyzw = r8.xyzw + -r3.yyyy;
    r8.xyzw = saturate(r8.xyzw * r3.xxxx);
    r8.xyzw = float4(1,1,1,1) + -r8.xyzw;
    r3.zw = r9.xy * r8.xy;
    r2.w = ShadowGatherVisibility(r9.xyzw * r8.xyzw, r5.xy);
    r3.z = -r7.x * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r7.x = r2.w * 0.0299999993 + r7.x;
    r5.xyz = r4.xyz * float3(4,4,4) + r5.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = r5.xyz;
  r7.y = r7.x;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r6.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r6.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r6.xyzw = -r6.xyzw + r2.zzzz;
    r6.xyzw = r6.xyzw + -r3.yyyy;
    r6.xyzw = saturate(r6.xyzw * r3.xxxx);
    r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
    r3.zw = r8.xy * r6.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r6.xyzw, r2.xy);
    r3.z = -r7.y * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r7.y = r2.w * 0.0299999993 + r7.y;
    r2.xyz = r4.xyz * float3(4,4,4) + r2.xyz;
    r1.w = (int)r1.w + 1;
  }
  r5.xyz = r2.xyz;
  r7.z = r7.y;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r6.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r8.xyzw = cmp(r6.xyzw < r5.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r6.xyzw = -r6.xyzw + r5.zzzz;
    r6.xyzw = r6.xyzw + -r3.yyyy;
    r6.xyzw = saturate(r6.xyzw * r3.xxxx);
    r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
    r3.zw = r8.xy * r6.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r6.xyzw, r5.xy);
    r3.z = -r7.z * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r7.z = r2.w * 0.0299999993 + r7.z;
    r5.xyz = r4.xyz * float3(4,4,4) + r5.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = r5.xyz;
  r7.w = r7.z;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r6.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r6.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r6.xyzw = -r6.xyzw + r2.zzzz;
    r6.xyzw = r6.xyzw + -r3.yyyy;
    r6.xyzw = saturate(r6.xyzw * r3.xxxx);
    r6.xyzw = float4(1,1,1,1) + -r6.xyzw;
    r3.zw = r8.xy * r6.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r6.xyzw, r2.xy);
    r3.z = -r7.w * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r7.w = r2.w * 0.0299999993 + r7.w;
    r2.xyz = r4.xyz * float3(4,4,4) + r2.xyz;
    r1.w = (int)r1.w + 1;
  }
  r5.xyzw = r7.xyzw * r0.wwww;
  u0[uint3(vThreadID.xy, 3)] = r5.xyzw;
  r5.xyz = r2.xyz;
  r6.x = r7.w;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r8.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r9.xyzw = cmp(r8.xyzw < r5.zzzz);
    r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
    r8.xyzw = -r8.xyzw + r5.zzzz;
    r8.xyzw = r8.xyzw + -r3.yyyy;
    r8.xyzw = saturate(r8.xyzw * r3.xxxx);
    r8.xyzw = float4(1,1,1,1) + -r8.xyzw;
    r3.zw = r9.xy * r8.xy;
    r2.w = ShadowGatherVisibility(r9.xyzw * r8.xyzw, r5.xy);
    r3.z = -r6.x * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r6.x = r2.w * 0.0299999993 + r6.x;
    r5.xyz = r4.xyz * float3(8,8,8) + r5.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = r5.xyz;
  r6.y = r6.x;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r2.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r2.xy);
    r3.z = -r6.y * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r6.y = r2.w * 0.0299999993 + r6.y;
    r2.xyz = r4.xyz * float3(8,8,8) + r2.xyz;
    r1.w = (int)r1.w + 1;
  }
  r5.xyz = r2.xyz;
  r6.z = r6.y;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r5.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r5.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r5.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r5.xy);
    r3.z = -r6.z * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r6.z = r2.w * 0.0299999993 + r6.z;
    r5.xyz = r4.xyz * float3(8,8,8) + r5.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = r5.xyz;
  r6.w = r6.z;
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 8);
    if (r2.w != 0) break;
    r7.xyzw = t0.Gather(s0_s, r2.xy).xyzw;
    r8.xyzw = cmp(r7.xyzw < r2.zzzz);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r7.xyzw = -r7.xyzw + r2.zzzz;
    r7.xyzw = r7.xyzw + -r3.yyyy;
    r7.xyzw = saturate(r7.xyzw * r3.xxxx);
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.zw = r8.xy * r7.xy;
    r2.w = ShadowGatherVisibility(r8.xyzw * r7.xyzw, r2.xy);
    r3.z = -r6.w * 0.400000006 + 1;
    r2.w = r3.z * r2.w;
    r6.w = r2.w * 0.0299999993 + r6.w;
    r2.xyz = r4.xyz * float3(8,8,8) + r2.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyzw = r6.xyzw * r0.wwww;
  u0[uint3(vThreadID.xy, 4)] = r2.xyzw;
  r2.xyz = cb1[132].zzz * cb0[25].xyz + r0.xyz;
  r0.xyz = cb1[132].zzz * cb0[26].xyz + r0.xyz;
  r1.xyz = float3(0.00799999945,0.00799999945,0.00799999945) * r1.xyz;
  r2.xyw = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) * r2.xyz;
  r0.xyw = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) * r0.xyz;
  r1.w = -3000 + r2.z;
  r0.z = -3000 + r0.z;
  r3.xy = vThreadID.xy;
  r4.xyz = r2.xyw;
  r5.xyz = r0.xyw;
  r2.z = 0;
  r4.w = 8;
  r5.w = 0;
  while (true) {
    r6.x = cmp((uint)r5.w >= 16);
    if (r6.x != 0) break;
    r6.x = t1.SampleLevel(s1_s, r4.xyz, 0).w;
    r6.y = r6.x * 0.5 + 1;
    r6.yzw = r6.yyy * r1.xyz + r4.xyz;
    r7.x = r6.w * 10000 + -r1.w;
    r7.x = saturate(0.000125000006 * r7.x);
    r7.x = 1 + -r7.x;
    r7.x = r7.x * r7.x;
    r6.x = r7.x * r6.x;
    r7.x = t1.SampleLevel(s1_s, r6.yzw, 0).w;
    r7.y = r7.x * 0.5 + 1;
    r6.yzw = r7.yyy * r1.xyz + r6.yzw;
    r7.y = r6.w * 10000 + -r1.w;
    r7.y = saturate(0.000125000006 * r7.y);
    r7.y = 1 + -r7.y;
    r7.y = r7.y * r7.y;
    r7.x = r7.x * r7.y;
    r7.x = 3 * r7.x;
    r6.x = r6.x * 3 + r7.x;
    r7.x = t1.SampleLevel(s1_s, r6.yzw, 0).w;
    r7.y = r7.x * 0.5 + 1;
    r6.yzw = r7.yyy * r1.xyz + r6.yzw;
    r7.y = r6.w * 10000 + -r1.w;
    r7.y = saturate(0.000125000006 * r7.y);
    r7.y = 1 + -r7.y;
    r7.y = r7.y * r7.y;
    r7.x = r7.x * r7.y;
    r6.x = r7.x * 3 + r6.x;
    r7.x = t1.SampleLevel(s1_s, r6.yzw, 0).w;
    r7.y = r7.x * 0.5 + 1;
    r4.xyz = r7.yyy * r1.xyz + r6.yzw;
    r6.y = r4.z * 10000 + -r1.w;
    r6.y = saturate(0.000125000006 * r6.y);
    r6.y = 1 + -r6.y;
    r6.y = r6.y * r6.y;
    r6.y = r7.x * r6.y;
    r6.x = r6.y * 3 + r6.x;
    r6.x = r6.x + r2.z;
    r6.y = t1.SampleLevel(s1_s, r5.xyz, 0).w;
    r6.z = r6.y * 0.5 + 1;
    r7.xyz = r6.zzz * r1.xyz + r5.xyz;
    r6.z = r7.z * 10000 + -r0.z;
    r6.z = saturate(0.000125000006 * r6.z);
    r6.z = 1 + -r6.z;
    r6.z = r6.z * r6.z;
    r6.y = r6.y * r6.z;
    r6.z = t1.SampleLevel(s1_s, r7.xyz, 0).w;
    r6.w = r6.z * 0.5 + 1;
    r7.xyz = r6.www * r1.xyz + r7.xyz;
    r6.w = r7.z * 10000 + -r0.z;
    r6.w = saturate(0.000125000006 * r6.w);
    r6.w = 1 + -r6.w;
    r6.w = r6.w * r6.w;
    r6.z = r6.z * r6.w;
    r6.z = 3 * r6.z;
    r6.y = r6.y * 3 + r6.z;
    r6.z = t1.SampleLevel(s1_s, r7.xyz, 0).w;
    r6.w = r6.z * 0.5 + 1;
    r7.xyz = r6.www * r1.xyz + r7.xyz;
    r6.w = r7.z * 10000 + -r0.z;
    r6.w = saturate(0.000125000006 * r6.w);
    r6.w = 1 + -r6.w;
    r6.w = r6.w * r6.w;
    r6.z = r6.z * r6.w;
    r6.y = r6.z * 3 + r6.y;
    r6.z = t1.SampleLevel(s1_s, r7.xyz, 0).w;
    r6.w = r6.z * 0.5 + 1;
    r5.xyz = r6.www * r1.xyz + r7.xyz;
    r6.w = r5.z * 10000 + -r0.z;
    r6.w = saturate(0.000125000006 * r6.w);
    r6.w = 1 + -r6.w;
    r6.w = r6.w * r6.w;
    r6.z = r6.z * r6.w;
    r6.y = r6.z * 3 + r6.y;
    r2.z = r6.x + r6.y;
    r6.x = r2.z / r4.w;
    r4.w = 8 + r4.w;
    u1[uint3(vThreadID.xy, uint(r5.w))] = r6.xxxx;
    r5.w = (int)r5.w + 1;
  }
  return;
}