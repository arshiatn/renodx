// ---- Created with 3Dmigoto v1.3.16 on Thu Sep 24 00:30:02 2026

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
  float2 g_campaign_shroud_uv_offset : packoffset(c1.z);
}

SamplerState s_diffuse_map_s : register(s0);
Texture2D<float4> s_diffuse_map : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float w2 : TEXCOORD7,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = s_diffuse_map.SampleLevel(s_diffuse_map_s, v2.xy, v2.y).xyzw;
  r1.x = saturate(r0.x);
  r1.x = log2(r1.x);
  r1.x = g_inv_gamma_output * r1.x;
  r1.w = exp2(r1.x);
  r1.xyz = float3(1,1,1);
  r2.xy = cmp(float2(0.5,0.5) < g_text_rendering_enabled_PS);
  r0.xz = r2.yy ? r0.zx : r0.xz;
  r0.xyzw = r2.xxxx ? r1.xyzw : r0.xyzw;
  r1.x = r0.w * v1.w + -0.00392156886;
  r0.xyzw = v1.xyzw * r0.xyzw;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.xy = v5.xy + -v4.xw;
  r1.zw = -v5.xy + v4.zy;
  r1.xyzw = cmp(r1.xyzw < float4(0,0,0,0));
  r1.xy = (int2)r1.zw | (int2)r1.xy;
  r1.x = (int)r1.y | (int)r1.x;
  if (r1.x != 0) discard;
  r1.x = cmp(g_model_time_PS >= -g_model_time_PS);
  r1.x = r1.x ? 1 : -1;
  r1.y = g_model_time_PS * r1.x;
  r1.y = frac(r1.y);
  r1.x = r1.x * r1.y;
  r1.x = 3.14159274 * r1.x;
  r1.x = sin(r1.x);
  r1.yzw = saturate(float3(3,3,3) * r0.xyz);
  r1.yzw = -r0.xyz * float3(0.25,0.25,0.25) + r1.yzw;
  r2.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  r1.xyz = r1.xxx * r1.yzw + r2.xyz;
  r1.w = cmp(0.5 < w2.x);
  r0.xyz = saturate(r1.www ? r1.xyz : r0.xyz);
  o0.w = r0.w;
  r0.xyz = log2(r0.xyz);
  r0.xyz = g_inv_gamma_output * r0.xyz;
  o0.xyz = exp2(r0.xyz);

  // Stock + UNORM clamp. Alpha can go above 1 (vertex alpha, edge fades); the
  // FP16 upgrade no longer clamps it, so the blend cooks what's underneath.
  o0 = saturate(o0);
  return;
}