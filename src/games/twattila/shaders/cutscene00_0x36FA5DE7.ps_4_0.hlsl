// ---- Created with 3Dmigoto v1.3.16 on Sat Sep 19 15:36:22 2026

SamplerState s_diffuse_map_y_s : register(s0);
SamplerState s_diffuse_map_u_s : register(s1);
SamplerState s_diffuse_map_v_s : register(s2);
Texture2D<float4> s_diffuse_map_y : register(t0);
Texture2D<float4> s_diffuse_map_v : register(t1);
Texture2D<float4> s_diffuse_map_u : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


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
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = s_diffuse_map_u.SampleLevel(s_diffuse_map_u_s, v2.xy, v2.y).xyzw;
  r0.xyz = float3(0,-0.391448975,2.01782227) * r0.www;
  r1.xyzw = s_diffuse_map_v.SampleLevel(s_diffuse_map_v_s, v2.xy, v2.y).xyzw;
  r0.xyz = r1.www * float3(1.59579468,-0.813476563,0) + r0.xyz;
  r1.xyzw = s_diffuse_map_y.SampleLevel(s_diffuse_map_y_s, v2.xy, v2.y).xyzw;
  r0.xyz = r1.www * float3(1.16412354,1.16412354,1.16412354) + r0.xyz;
  o0.xyz = float3(-0.87065506,0.529705048,-1.08166885) + r0.xyz;
  o0.w = v1.w;
  o0 = saturate(o0);

  // Video is mastered for a 2.2 screen; the frame is 2.0. Scene paper white.
  o0.xyz = renodx::color::gamma::EncodeSafe(
      renodx::color::gamma::DecodeSafe(o0.xyz)
      * (SI.diffuse_white_nits / SI.graphics_white_nits), 2.0f);
  return;
}