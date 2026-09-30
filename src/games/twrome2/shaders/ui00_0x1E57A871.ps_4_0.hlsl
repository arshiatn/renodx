// ---- Created with 3Dmigoto v1.3.16 on Mon Sep 14 16:51:52 2026

cbuffer colorimetry_VS_PS : register(b0)
{
  float g_brightness : packoffset(c0);
  float g_gamma_output : packoffset(c0.y);
  float g_inv_gamma_output : packoffset(c0.z);
}

cbuffer sprite_PS : register(b1)
{
  float g_windows_time_PS : packoffset(c0);
  float g_model_time_PS : packoffset(c0.y);
  float g_text_rendering_enabled_PS : packoffset(c0.z);
  float g_switch_diffuse_channels : packoffset(c0.w);
  float2 g_screen_dimensions_PS : packoffset(c1);
}

SamplerState s_diffuse_map_s : register(s0);
Texture2D<float4> s_diffuse_map : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float2 v6 : TEXCOORD5,
  float w6 : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = s_diffuse_map.SampleLevel(s_diffuse_map_s, v2.xy, v2.y).xyzw;
  r1.x = saturate(r0.x);
  r1.x = log2(r1.x);
  r1.x = g_inv_gamma_output * r1.x;
  r1.w = exp2(r1.x);
  r2.xy = cmp(float2(0.5,0.5) < g_text_rendering_enabled_PS);
  r0.xz = r2.yy ? r0.zx : r0.xz;
  r1.xyz = float3(1,1,1);
  r0.xyzw = r2.xxxx ? r1.xyzw : r0.xyzw;
  r0.xyzw = v1.xyzw * r0.xyzw;
  r1.x = cmp(r0.w == 0.000000);
  r1.yz = cmp(float2(0.0500000007,0.0500000007) < w2.xy);
  r1.x = r1.y ? r1.x : 0;
  r1.x = r1.z ? r1.x : 0;
  if (r1.x != 0) {
    r1.xy = v4.xy * float2(-0.00100000005,-0.00100000005) + v2.xy;
    r1.xyzw = s_diffuse_map.SampleLevel(s_diffuse_map_s, r1.xy, r1.y).xyzw;
    r1.x = saturate(r1.x);
    r1.x = log2(r1.x);
    r1.x = g_inv_gamma_output * r1.x;
    r1.x = exp2(r1.x);
    r1.x = r2.x ? r1.x : r1.w;
    r1.y = cmp(0 < r1.x);
    r1.x = v4.z * r1.x;
    r2.w = v1.w * r1.x;
    r2.xyz = float3(0,0,0);
    r0.xyzw = r1.yyyy ? r2.xyzw : r0.xyzw;
  }
  r1.x = -0.00392156886 + r0.w;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.xy = v6.xy + -v5.xw;
  r1.zw = -v6.xy + v5.zy;
  r1.xyzw = cmp(r1.xyzw < float4(0,0,0,0));
  r1.xy = (int2)r1.zw | (int2)r1.xy;
  r1.x = (int)r1.y | (int)r1.x;
  if (r1.x != 0) discard;
  r1.x = cmp(0.5 < w6.x);
  r1.y = cmp(g_model_time_PS >= -g_model_time_PS);
  r1.y = r1.y ? 1 : -1;
  r1.z = g_model_time_PS * r1.y;
  r1.z = frac(r1.z);
  r1.y = r1.y * r1.z;
  r1.y = 3.14159274 * r1.y;
  r1.y = sin(r1.y);
  r2.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  r3.xyz = saturate(float3(3,3,3) * r0.xyz);
  r3.xyz = -r0.xyz * float3(0.25,0.25,0.25) + r3.xyz;
  r1.yzw = r1.yyy * r3.xyz + r2.xyz;
  r1.xyz = saturate(r1.xxx ? r1.yzw : r0.xyz);
  r1.xyz = log2(r1.xyz);
  r1.xyz = g_inv_gamma_output * r1.xyz;
  r0.xyz = exp2(r1.xyz);
  o0.xyzw = r0.xyzw;
  o0.w = saturate(o0.w);
  return;
}