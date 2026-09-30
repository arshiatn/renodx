// ---- Created with 3Dmigoto v1.3.16 on Sat Sep 19 15:33:24 2026

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
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v5.xyy + -v4.xwy;
  r1.xy = r0.xy;
  r0.xyw = -v5.xyy + v4.zyw;
  r1.zw = r0.xy;
  r2.xyzw = cmp(r1.xyzw < float4(0,0,0,0));
  r1.yz = (int2)r2.zw | (int2)r2.xy;
  r0.y = (int)r1.z | (int)r1.y;
  if (r0.y != 0) discard;
  r1.yz = float2(0.5,0.5) * g_screen_dimensions_PS.xy;
  r1.yz = float2(1,1) / r1.yz;
  r1.zw = -v3.yw * r1.zz;
  r2.xy = v3.xz * r1.yy;
  r0.y = r0.z / r1.z;
  r0.z = r0.w / r1.w;
  r0.x = r0.x / r2.y;
  r0.w = r1.x / r2.x;
  r1.xyzw = s_diffuse_map.SampleLevel(s_diffuse_map_s, v2.xy, v2.y).xyzw;
  r2.x = saturate(r1.x);
  r2.x = log2(r2.x);
  r2.x = g_inv_gamma_output * r2.x;
  r2.w = exp2(r2.x);
  r2.xyz = float3(1,1,1);
  r3.xy = cmp(float2(0.5,0.5) < g_text_rendering_enabled_PS);
  r1.xz = r3.yy ? r1.zx : r1.xz;
  r1.xyzw = r3.xxxx ? r2.xyzw : r1.xyzw;
  r1.xyzw = v1.xyzw * r1.xyzw;
  r2.xyzw = cmp(float4(0,0,0,0) < v3.xyzw);
  r0.w = r2.x ? r0.w : r1.w;
  r0.y = min(r0.w, r0.y);
  r0.y = r2.y ? r0.y : r0.w;
  r0.x = min(r0.y, r0.x);
  r0.x = r2.z ? r0.x : r0.y;
  r0.y = min(r0.x, r0.z);
  o0.w = r2.w ? r0.y : r0.x;
  r0.xyz = saturate(float3(3,3,3) * r1.xyz);
  r0.xyz = -r1.xyz * float3(0.25,0.25,0.25) + r0.xyz;
  r0.w = cmp(g_model_time_PS >= -g_model_time_PS);
  r0.w = r0.w ? 1 : -1;
  r1.w = g_model_time_PS * r0.w;
  r1.w = frac(r1.w);
  r0.w = r1.w * r0.w;
  r0.w = 3.14159274 * r0.w;
  r0.w = sin(r0.w);
  r2.xyz = float3(0.25,0.25,0.25) * r1.xyz;
  r0.xyz = r0.www * r0.xyz + r2.xyz;
  r0.w = cmp(0.5 < w2.x);
  r0.xyz = saturate(r0.www ? r0.xyz : r1.xyz);
  r0.xyz = log2(r0.xyz);
  r0.xyz = g_inv_gamma_output * r0.xyz;
  o0.xyz = exp2(r0.xyz);
  o0 = saturate(o0);
  return;
}