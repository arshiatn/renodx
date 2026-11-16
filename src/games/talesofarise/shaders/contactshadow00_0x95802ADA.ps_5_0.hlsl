// Tales of Arise character contact shadows (Bandai, the menu's ScreenSpaceShadow: r.EnableScreenSpaceShadowTO14): two
// short screen-space marches over its character shading models' pixels (GBufferB alpha's model 15, then 14), their
// occlusion into an RG8 mask Bandai blurs (A49AC875) and combines (7A51D0B1) into what its character lighting
// (0A9D482A / DF1B494D / 1C8D11B6) shades with. From 0x95802ADA.ps_5_0.cso (3Dmigoto, 2026-10-09 dumps; bytes read
// with fxc /dumpbin). The first march goes toward the light (cb0[42], view space): with Bend's screen-space shadows on
// (talesofarise_dlss/ue_screen_space_shadows.hpp, SI.screen_space_shadows) it is left out, theirs shade the sun's light
// instead. The second goes toward the camera's up and the camera (Bandai's character look, not the sun's): it stays.
#include "../shared.h"

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[59];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[44];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  float4 v2 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  float4 x0[16];
  r0.x = t1.SampleLevel(s1_s, v0.xy, 0).z;
  r0.y = t0.SampleLevel(s0_s, v0.xy, 0).x;
  r0.z = r0.y * cb1[57].x + cb1[57].y;
  r0.y = r0.y * cb1[57].z + -cb1[57].w;
  r0.y = 1 / r0.y;
  r0.y = r0.z + r0.y;
  r0.x = r0.x * 2 + -1;
  r0.yzw = v1.xyz * r0.yyy;
  r1.xyz = cb1[13].xyz * -cb0[42].yyy;
  r1.xyz = -cb0[42].xxx * cb1[12].xyz + r1.xyz;
  r1.xyz = -cb0[42].zzz * cb1[14].xyz + r1.xyz;
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
  r2.xyzw = float4(4,4,4,4) * v2.xyxy;
  r2.xyzw = cmp(r2.xyzw >= -r2.zwzw);
  r2.xyzw = r2.xyzw ? float4(4,4,0.25,0.25) : float4(-4,-4,-0.25,-0.25);
  r2.zw = v2.xy * r2.zw;
  r2.zw = frac(r2.zw);
  r2.xy = r2.xy * r2.zw;
  r2.xy = (uint2)r2.xy;
  r1.w = (uint)r2.x << 2;
  r1.w = (int)r2.y + (int)r1.w;
  r1.w = x0[r1.w+0].x;
  r1.w = -0.5 + r1.w;
  r2.xyz = r1.xyz * float3(1.05000007,1.05000007,1.05000007) + r0.yzw;
  r3.xy = float2(0,1);
  // Left out under Bend's screen-space shadows (no occlusion: r3.x 0).
  [branch]
  if (SI.screen_space_shadows == 0.f) {
    r4.xyz = r2.xyz;
    r2.w = 0;
    while (true) {
      r3.z = cmp((uint)r2.w >= 20);
      if (r3.z != 0) break;
      r5.xyz = cb1[1].xyw * r4.yyy;
      r5.xyz = r4.xxx * cb1[0].xyw + r5.xyz;
      r5.xyz = r4.zzz * cb1[2].xyw + r5.xyz;
      r5.xyz = cb1[3].xyw + r5.xyz;
      r3.zw = r5.xy / r5.zz;
      r3.zw = r3.zw * cb1[58].xy + cb1[58].wz;
      r4.w = t2.SampleLevel(s2_s, r3.zw, 0).w;
      r4.w = 255 * r4.w;
      r4.w = round(r4.w);
      r4.w = (uint)r4.w;
      r4.w = (int)r4.w & 15;
      r4.w = cmp((int)r4.w != 15);
      if (r4.w != 0) {
        break;
      } else {
        r4.w = cb1[9].z * r4.y;
        r4.w = r4.x * cb1[8].z + r4.w;
        r4.w = r4.z * cb1[10].z + r4.w;
        r4.w = cb1[11].z + r4.w;
        r3.z = t0.SampleLevel(s0_s, r3.zw, 0).x;
        r3.w = r3.z * cb1[57].x + cb1[57].y;
        r3.z = r3.z * cb1[57].z + -cb1[57].w;
        r3.z = 1 / r3.z;
        r3.z = r3.w + r3.z;
        r3.z = cb0[42].w + r3.z;
        r3.z = cmp(r4.w >= r3.z);
        r5.xy = float2(1,1) + r3.xy;
        r3.xy = r3.zz ? r5.xy : r3.xy;
        r4.xyz = r1.xyz * float3(0.075000003,0.075000003,0.075000003) + r4.xyz;
      }
      r2.w = (int)r2.w + 1;
    }
  }
  r1.x = saturate(r3.x / r3.y);
  r1.x = 1.25 * r1.x;
  r1.y = dot(r0.yzw, r0.yzw);
  r1.y = rsqrt(r1.y);
  r1.y = r1.y * r0.w;
  r2.y = 0.375 * r1.w;
  r2.z = 1;
  r2.xy = abs(r1.yy) * float2(0,-1) + r2.yz;
  r2.yzw = cb1[13].xyz * r2.yyy;
  r2.xyz = r2.xxx * cb1[12].xyz + r2.yzw;
  r2.xyz = -cb1[14].xyz + r2.xyz;
  r3.xyz = float3(2.5,2.5,2.5) * r2.xyz;
  r1.z = r1.w * 0.75 + 1;
  r3.xyz = r1.zzz * r3.xyz + r0.yzw;
  r1.zw = float2(0,1);
  r4.xyz = r3.xyz;
  r2.w = 0;
  while (true) {
    r3.w = cmp((uint)r2.w >= 20);
    if (r3.w != 0) break;
    r5.xyz = cb1[1].xyw * r4.yyy;
    r5.xyz = r4.xxx * cb1[0].xyw + r5.xyz;
    r5.xyz = r4.zzz * cb1[2].xyw + r5.xyz;
    r5.xyz = cb1[3].xyw + r5.xyz;
    r5.xy = r5.xy / r5.zz;
    r5.xy = r5.xy * cb1[58].xy + cb1[58].wz;
    r3.w = t2.SampleLevel(s2_s, r5.xy, 0).w;
    r3.w = 255 * r3.w;
    r3.w = round(r3.w);
    r3.w = (uint)r3.w;
    r3.w = (int)r3.w & 15;
    r3.w = cmp((int)r3.w != 14);
    if (r3.w != 0) {
      break;
    } else {
      r3.w = t0.SampleLevel(s0_s, r5.xy, 0).x;
      r4.w = r3.w * cb1[57].x + cb1[57].y;
      r3.w = r3.w * cb1[57].z + -cb1[57].w;
      r3.w = 1 / r3.w;
      r3.w = r4.w + r3.w;
      r4.w = cb1[9].z * r4.y;
      r4.w = r4.x * cb1[8].z + r4.w;
      r4.w = r4.z * cb1[10].z + r4.w;
      r4.w = cb1[11].z + r4.w;
      r5.xyz = v1.xyz * r3.www + -r0.yzw;
      r5.x = dot(r5.xyz, r5.xyz);
      r5.x = sqrt(r5.x);
      r5.xy = float2(0.0500000007,0.100000001) * r5.xx;
      r5.xy = min(float2(1,1), r5.xy);
      r5.x = 1 + -r5.x;
      r3.w = cmp(r4.w >= r3.w);
      r6.y = 1 + r1.w;
      r6.x = r5.x * r5.y + r1.z;
      r1.zw = r3.ww ? r6.xy : r1.zw;
      r4.xyz = r2.xyz * float3(2.5,2.5,2.5) + r4.xyz;
    }
    r2.w = (int)r2.w + 1;
  }
  r0.y = r1.z / r1.w;
  r0.y = r0.y + r0.y;
  r0.y = max(r1.x, r0.y);
  r0.x = saturate(-2 * r0.x);
  r0.x = r0.x * -0.600000024 + 1;
  r0.z = 1 + -r0.x;
  r0.x = cb0[43].w * r0.z + r0.x;
  r1.y = saturate(-r1.y);
  r0.z = 1 + -r1.y;
  r0.x = r0.z * r0.x;
  r0.x = r0.y * r0.x;
  r0.yz = saturate(float2(7,7) * v0.xy);
  r0.y = r0.y * r0.z;
  r0.x = r0.x * r0.y;
  o0.xzw = cb0[43].zzz * -r0.xxx + r0.xxx;
  o0.y = 1;
  return;
}