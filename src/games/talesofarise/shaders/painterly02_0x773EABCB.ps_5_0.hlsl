// Tales of Arise painterly blur, vertical (Bandai: edge-aware, +-4 taps 1.5 pixels apart). From 0x773EABCB.ps_5_0.cso
// (3Dmigoto, 2026-10-09 dumps).
// Below 100% DLSS, SI.filter_scale (render / output, from the DLSS addon) spaces the taps in output pixels: the
// same size on screen as at 100% (in render pixels it grew as the scale dropped).
#include "../shared.h"

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[30];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t1.Sample(s1_s, v0.xy).xyzw;
  r0.x = max(r0.x, r0.y);
  r0.x = cmp(r0.y >= r0.x);
  r0.x = r0.x ? 0.100000024 : 1;
  r0.y = -r0.y * 100 + 1;
  r0.x = max(r0.y, r0.x);
  r0.y = 1.5 * cb0[0].w * SI.filter_scale;
  r0.x = r0.y * r0.x;
  r1.xyz = float3(-4,-3,-2) * r0.xxx;
  r0.y = -r0.x;
  r1.w = 0;
  r2.xyzw = v0.xyxy + -r1.wxwy;
  r3.xyz = t0.Sample(s0_s, r2.xy).xyz;
  r2.xyz = t0.Sample(s0_s, r2.zw).xyz;
  r4.xyzw = v0.xyxy + r1.wxwy;
  r5.xyz = t0.Sample(s0_s, r4.xy).xyz;
  r4.xyz = t0.Sample(s0_s, r4.zw).xyz;
  r1.x = dot(r5.xyz, float3(0.200000003,0.699999988,0.100000001));
  r6.xyz = t0.Sample(s0_s, v0.xy).xyz;
  r1.y = dot(r6.xyz, float3(0.200000003,0.699999988,0.100000001));
  r1.x = r1.x + -r1.y;
  r2.w = cb0[29].z + -cb0[28].w;
  r0.z = r0.z * r2.w + cb0[28].w;
  r2.w = saturate(r1.y);
  r0.z = r2.w * r0.z;
  r0.z = max(9.99999975e-006, r0.z);
  r2.w = abs(r1.x) / r0.z;
  r2.w = 1 + -r2.w;
  r2.w = max(0, r2.w);
  r2.w = r2.w * r2.w;
  r2.w = r2.w * r2.w;
  r7.w = 0.400000006 * r2.w;
  r8.xyz = r7.www * r3.xyz;
  r2.w = dot(r3.xyz, float3(0.200000003,0.699999988,0.100000001));
  r2.w = r2.w + -r1.y;
  r1.x = cmp(abs(r1.x) < abs(r2.w));
  r7.xyz = r7.www * r5.xyz;
  r8.w = r7.w;
  r3.xyzw = r1.xxxx ? r7.xyzw : r8.xyzw;
  r1.x = dot(r4.xyz, float3(0.200000003,0.699999988,0.100000001));
  r1.x = r1.x + -r1.y;
  r2.w = abs(r1.x) / r0.z;
  r2.w = 1 + -r2.w;
  r2.w = max(0, r2.w);
  r2.w = r2.w * r2.w;
  r2.w = r2.w * r2.w;
  r4.w = 0.550000012 * r2.w;
  r5.w = r2.w * 0.550000012 + r3.w;
  r5.xyz = r4.www * r4.xyz + r3.xyz;
  r3.xyz = r4.www * r2.xyz + r3.xyz;
  r2.x = dot(r2.xyz, float3(0.200000003,0.699999988,0.100000001));
  r2.x = r2.x + -r1.y;
  r1.x = cmp(abs(r1.x) < abs(r2.x));
  r3.w = r5.w;
  r2.xyzw = r1.xxxx ? r5.xyzw : r3.xyzw;
  r3.xy = v0.xy + -r1.wz;
  r1.xz = v0.xy + r1.wz;
  r1.xzw = t0.Sample(s0_s, r1.xz).xyz;
  r3.xyz = t0.Sample(s0_s, r3.xy).xyz;
  r3.w = dot(r1.xzw, float3(0.200000003,0.699999988,0.100000001));
  r3.w = r3.w + -r1.y;
  r4.x = abs(r3.w) / r0.z;
  r4.x = 1 + -r4.x;
  r4.x = max(0, r4.x);
  r4.x = r4.x * r4.x;
  r4.x = r4.x * r4.x;
  r4.y = 0.700000048 * r4.x;
  r5.w = r4.x * 0.700000048 + r2.w;
  r7.xyz = r4.yyy * r3.xyz + r2.xyz;
  r5.xyz = r4.yyy * r1.xzw + r2.xyz;
  r1.x = dot(r3.xyz, float3(0.200000003,0.699999988,0.100000001));
  r1.x = r1.x + -r1.y;
  r1.x = cmp(abs(r3.w) < abs(r1.x));
  r7.w = r5.w;
  r2.xyzw = r1.xxxx ? r5.xyzw : r7.xyzw;
  r0.x = 0;
  r1.xz = v0.xy + -r0.xy;
  r0.xy = v0.xy + r0.xy;
  r3.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r1.xzw = t0.Sample(s0_s, r1.xz).xyz;
  r0.x = dot(r3.xyz, float3(0.200000003,0.699999988,0.100000001));
  r0.x = r0.x + -r1.y;
  r0.y = abs(r0.x) / r0.z;
  r0.y = 1 + -r0.y;
  r0.y = max(0, r0.y);
  r0.y = r0.y * r0.y;
  r0.y = r0.y * r0.y;
  r0.z = 0.850000024 * r0.y;
  r4.w = r0.y * 0.850000024 + r2.w;
  r5.xyz = r0.zzz * r1.xzw + r2.xyz;
  r4.xyz = r0.zzz * r3.xyz + r2.xyz;
  r0.y = dot(r1.xzw, float3(0.200000003,0.699999988,0.100000001));
  r0.y = r0.y + -r1.y;
  r0.x = cmp(abs(r0.x) < abs(r0.y));
  r5.w = r4.w;
  r1.xyzw = r0.xxxx ? r4.xyzw : r5.xyzw;
  r0.xyz = r1.xyz * float3(2,2,2) + r6.xyz;
  r1.x = r1.w * 1.20000005 + 1;
  r0.xyz = r0.xyz / r1.xxx;
  r0.xyz = r0.xyz + -r6.xyz;
  o0.xyz = (r0.w * SI.painterly) * r0.xyz + r6.xyz;
  o0.w = 0;
  return;
}