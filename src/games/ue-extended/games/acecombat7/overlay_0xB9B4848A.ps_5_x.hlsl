#include "../../common.hlsli"

// ---- Created with 3Dmigoto v1.3.16 on Sun Sep 20 19:53:02 2026
Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[137];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = cb0[128].zw * v0.xy;
  r1.xy = -cb0[126].xy + v0.xy;
  r1.xy = r1.xy * cb0[127].zw + float2(-0.5,-0.5);
  r1.xy = v0.ww * r1.xy;
  r1.z = v0.w;
  r1.xyz = float3(2,-2,1) * r1.xyz;
  r1.xy = r1.xy / r1.zz;
  r1.xy = r1.xy * cb0[54].xy + cb0[54].wz;
  r1.xyz = t1.Sample(s1_s, r1.xy).xyz;
  r0.w = 0.00100000005 * cb0[136].z;
  r1.w = cmp(r0.w >= -r0.w);
  r0.w = frac(abs(r0.w));
  r0.z = r1.w ? r0.w : -r0.w;
  r2.xyz = float3(384,384,384000) * r0.xyz;
  r3.xyz = r2.xyz;
  r0.zw = float2(0,1);
  r1.w = 0;
  while (true) {
    r2.w = cmp((uint)r1.w >= 2);
    if (r2.w != 0) break;
    r2.w = dot(r3.xyz, float3(0.333333343,0.333333343,0.333333343));
    r4.xyz = r3.xyz + r2.www;
    r5.xyz = floor(r4.xyz);
    r6.xyz = float3(1,1,1) + r5.xyz;
    r4.xyz = -r5.xyz + r4.xyz;
    r2.w = max(r4.y, r4.z);
    r2.w = max(r4.x, r2.w);
    r3.w = min(r4.y, r4.z);
    r3.w = min(r4.x, r3.w);
    r7.xyz = cmp(r4.xyz == r2.www);
    r7.xyz = r7.xyz ? float3(1,1,1) : 0;
    r7.xyz = r7.xyz + r5.xyz;
    r4.xyz = cmp(r4.xyz != r3.www);
    r4.xyz = r4.xyz ? float3(1,1,1) : 0;
    r4.xyz = r5.xyz + r4.xyz;
    r8.xy = r5.zz * float2(17,89) + r5.xy;
    r8.xy = float2(0.5,0.5) + r8.xy;
    r8.xy = float2(0.0078125,0.0078125) * r8.xy;
    r8.xyz = t0.SampleLevel(s0_s, r8.xy, 0).xyz;
    r8.xyz = r8.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r9.xy = r6.zz * float2(17,89) + r6.xy;
    r9.xy = float2(0.5,0.5) + r9.xy;
    r9.xy = float2(0.0078125,0.0078125) * r9.xy;
    r9.xyz = t0.SampleLevel(s0_s, r9.xy, 0).xyz;
    r9.xyz = r9.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r10.xy = r7.zz * float2(17,89) + r7.xy;
    r10.xy = float2(0.5,0.5) + r10.xy;
    r10.xy = float2(0.0078125,0.0078125) * r10.xy;
    r10.xyz = t0.SampleLevel(s0_s, r10.xy, 0).xyz;
    r10.xyz = r10.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r11.xy = r4.zz * float2(17,89) + r4.xy;
    r11.xy = float2(0.5,0.5) + r11.xy;
    r11.xy = float2(0.0078125,0.0078125) * r11.xy;
    r11.xyz = t0.SampleLevel(s0_s, r11.xy, 0).xyz;
    r11.xyz = r11.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r2.w = dot(r5.xyz, float3(0.166666672,0.166666672,0.166666672));
    r5.xyz = r5.xyz + -r2.www;
    r2.w = dot(r6.xyz, float3(0.166666672,0.166666672,0.166666672));
    r6.xyz = r6.xyz + -r2.www;
    r2.w = dot(r7.xyz, float3(0.166666672,0.166666672,0.166666672));
    r7.xyz = r7.xyz + -r2.www;
    r2.w = dot(r4.xyz, float3(0.166666672,0.166666672,0.166666672));
    r4.xyz = r4.xyz + -r2.www;
    r5.xyz = -r5.xyz + r3.xyz;
    r2.w = dot(r5.xyz, r5.xyz);
    r2.w = 0.600000024 + -r2.w;
    r2.w = max(0, r2.w);
    r2.w = r2.w * r2.w;
    r2.w = r2.w * r2.w;
    r3.w = dot(r8.xyz, r5.xyz);
    r5.xyz = -r6.xyz + r3.xyz;
    r4.w = dot(r5.xyz, r5.xyz);
    r4.w = 0.600000024 + -r4.w;
    r4.w = max(0, r4.w);
    r4.w = r4.w * r4.w;
    r4.w = r4.w * r4.w;
    r5.x = dot(r9.xyz, r5.xyz);
    r4.w = r5.x * r4.w;
    r5.xyz = -r7.xyz + r3.xyz;
    r5.w = dot(r5.xyz, r5.xyz);
    r5.w = 0.600000024 + -r5.w;
    r5.w = max(0, r5.w);
    r5.w = r5.w * r5.w;
    r5.x = dot(r10.xyz, r5.xyz);
    r4.xyz = -r4.xyz + r3.xyz;
    r5.y = dot(r4.xyz, r4.xyz);
    r5.y = 0.600000024 + -r5.y;
    r5.y = max(0, r5.y);
    r5.yw = r5.yw * r5.yw;
    r5.y = r5.y * r5.y;
    r4.x = dot(r11.xyz, r4.xyz);
    r2.w = r3.w * r2.w + r4.w;
    r2.w = r5.x * r5.w + r2.w;
    r2.w = r4.x * r5.y + r2.w;
    r2.w = r2.w * r0.w;
    r0.z = r2.w * 32 + r0.z;
    r3.xyz = r3.xyz + r3.xyz;
    r0.w = 0.5 * r0.w;
    r1.w = (int)r1.w + 1;
  }
  r0.z = r0.z * 0.5 + 0.5;
  r2.xyz = cb1[2].xyz * r0.zzz;
  r0.z = cb0[127].x / cb0[127].y;
  r3.xz = float2(1,1) / r0.zz;
  r3.yw = float2(1,1);
  r0.xyzw = r0.xyxy / r3.zwzw;
  r0.xyzw = float4(0.5,0.5,0.5,0.5) + r0.xyzw;
  r3.xyzw = float4(0.5,0.5,0.5,0.5) / r3.xyzw;
  r0.xyzw = -r3.xyzw + r0.xyzw;
  r0.xyzw = float4(-0.100000001,-0.5,-0.899999976,-0.5) + r0.xyzw;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = sqrt(r0.x);
  r0.x = r0.x / cb1[4].x;
  r0.x = 1 + -r0.x;
  r0.y = cb1[4].y * r0.x;
  r0.y = r0.y * r0.y;
  r0.y = 1.44269514 * r0.y;
  r0.y = exp2(r0.y);
  r0.y = 1 / r0.y;
  r1.w = cmp(9.99999975e-06 < abs(r0.x));
  r0.x = cmp(r0.x >= 0);
  r0.y = 1 + -r0.y;
  r0.y = 1 + -r0.y;
  r0.x = r0.x ? r0.y : 1;
  r0.x = r1.w ? r0.x : 1;
  r0.y = dot(r0.zw, r0.zw);
  r0.y = sqrt(r0.y);
  r0.y = r0.y / cb1[4].x;
  r0.y = 1 + -r0.y;
  r0.z = cb1[4].y * r0.y;
  r0.z = r0.z * r0.z;
  r0.z = 1.44269514 * r0.z;
  r0.z = exp2(r0.z);
  r0.z = 1 / r0.z;
  r0.w = cmp(9.99999975e-06 < abs(r0.y));
  r0.y = cmp(r0.y >= 0);
  r0.z = 1 + -r0.z;
  r0.z = 1 + -r0.z;
  r0.y = r0.y ? r0.z : 1;
  r0.y = r0.w ? r0.y : 1;
  r0.x = r0.x * r0.y;
  r0.yz = v0.xy * cb0[128].zw + float2(-0.5,-0.5);
  r0.y = dot(r0.yz, r0.yz);
  r0.y = sqrt(r0.y);
  r0.y = r0.y / cb1[4].z;
  r0.y = 1 + -r0.y;
  r0.z = cb1[4].w * r0.y;
  r0.z = r0.z * r0.z;
  r0.z = 1.44269514 * r0.z;
  r0.z = exp2(r0.z);
  r0.z = 1 / r0.z;
  r0.w = cmp(9.99999975e-06 < abs(r0.y));
  r0.y = cmp(r0.y >= 0);
  r0.z = 1 + -r0.z;
  r0.z = 1 + -r0.z;
  r0.y = r0.y ? r0.z : 1;
  r0.y = r0.w ? r0.y : 1;
  r0.x = r0.x * r0.y;
  r0.xyz = r2.xyz * r0.xxx;
  
  //r0.xyz = saturate(r0.xyz * cb1[5].xxx + r1.xyz);
  r0.xyz = (r0.xyz * cb1[5].xxx + r1.xyz);
  r0.xyz = renodx::math::Select(RENODX_TONE_MAP_TYPE == 0, saturate(r0.xyz), r0.xyz);
  
  r0.xyz = cb1[3].xyz + r0.xyz;

  // o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.xyz = renodx::math::Select(RENODX_TONE_MAP_TYPE == 0, max(0, r0.xyz), r0.xyz);
  
  o0.w = 1;
  return;
}