// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 30 13:20:58 2026

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
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = -0.00392156886 + v1.w;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xy = v5.xy + -v4.xw;
  r0.zw = -v5.xy + v4.zy;
  r0.xyzw = cmp(r0.xyzw < float4(0,0,0,0));
  r0.xy = (int2)r0.zw | (int2)r0.xy;
  r0.x = (int)r0.y | (int)r0.x;
  if (r0.x != 0) discard;
  o0.xyzw = v1.xyzw;

  // Restore the UNORM clamp lost to the FP16 upgrade.
  o0.xyzw = saturate(o0.xyzw);

  // Vertex colour goes out unencoded (ignores in-game gamma), so it was made for a
  // 2.2 screen. The frame is 2.0.
  o0.xyz = renodx::color::gamma::EncodeSafe(renodx::color::gamma::DecodeSafe(o0.xyz), 2.0f);
  return;
}