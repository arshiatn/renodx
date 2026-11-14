// indicator.hlsl ps30_main_deferred (the units' and buildings' ground indicators, faded where scenery stands over them): the
// game's, its colour x Unit/Building Indicator Brightness in HDR (SI.indicator_brightness, 1 the game's; Attila's
// some_indicators). Decompiled from the game's C454C1EA by 3Dmigoto v1.3.16; WARP test/tw_shogun2_shaders part 15. Below
// 100% its uv is SV_Position over the whole target (right for the top-left render part) and its NDC feeds only the view
// depth (inv_projection's z and w columns: no x / y terms), so it stays as the game wrote it.
#include "../shared.h"

cbuffer camera_VS_PS : register(b0)
{
  float4x4 inv_projection : packoffset(c26);
  float g_texel_offset : packoffset(c35.z);
  float4 screen_size : packoffset(c37);
}

SamplerState indicator_texture_sampler_s : register(s0);
Texture2D<float4> indicator_texture_sampler : register(t0);
Texture2D<float4> gbuffer_constants_texture : register(t1);
Texture2D<float4> gbuffer_readable_depth_texture : register(t2);

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_texel_offset + v0.xy;
  r0.xy = r0.xy / screen_size.xy;
  r0.zw = float2(-1,-1) + screen_size.xy;
  r0.zw = r0.xy * r0.zw;
  r1.xy = (int2)r0.zw;
  r1.zw = float2(0,0);
  r1.z = gbuffer_readable_depth_texture.Load(r1.xyz).x;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r0.z = dot(r1.xyzw, inv_projection._m02_m12_m22_m32);
  r0.w = dot(r1.xyzw, inv_projection._m03_m13_m23_m33);
  r0.z = r0.z / r0.w;
  r0.w = cmp(r0.z < v4.z);
  if (r0.w != 0) {
    gbuffer_constants_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r1.xy = uiDest.xy;
    r1.zw = (uint2)r1.xy;
    r0.xy = r1.zw * r0.xy;
    r0.xy = floor(r0.xy);
    r0.xy = (int2)r0.xy;
    r1.xy = (int2)r1.xy + int2(-1,-1);
    r0.xy = max(int2(0,0), (int2)r0.xy);
    r1.xy = min((int2)r0.xy, (int2)r1.xy);
    r1.zw = float2(0,0);
    r0.x = gbuffer_constants_texture.Load(r1.xyz).w;
    r0.x = 255 * r0.x;
    r0.x = round(r0.x);
    r0.x = (uint)r0.x;
    r0.x = cmp((uint)r0.x < 16);
    if (r0.x != 0) discard;
    r0.y = v4.z + -r0.z;
    r0.yz = cmp(r0.yy < float2(25,5));
    r0.z = r0.z ? 1 : 0.0700000003;
    r0.y = r0.y ? r0.z : 0;
    r0.x = r0.x ? 1 : r0.y;
  } else {
    r0.x = 1;
  }
  r1.xyzw = indicator_texture_sampler.Sample(indicator_texture_sampler_s, v1.xy).xyzw;
  r0.y = -0.00392156886 + r1.w;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;
  o0.w = r1.w * r0.x;
  o0.xyz = r1.xyz;
  if (HDR >= 1.f) o0.xyz *= SI.indicator_brightness;
}
