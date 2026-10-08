// Expeditions: Rome cutscene images: Slate UI material. From 0xEDD3DA15.ps_5_0.cso (3Dmigoto).
// UV pan/zoom (cb1[1..2]), color offset (cb1[3]), then Slate contrast and sRGB; alpha = vertex alpha.
// Mod: SDR, shown at Paper White instead of UI brightness.
#include "../shared.h"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[4];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[3];
}

#define cmp -

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : ORIGINAL_POSITION0,
  float2 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;

  r0.xy = v4.xy * v4.zw;
  r1.x = dot(r0.xy, cb1[2].xy);
  r1.y = dot(r0.xy, cb1[2].zw);
  r0.xy = cb1[1].xy + r1.xy;
  r0.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r0.xyz = cb1[3].xyz + r0.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz);
  r1.xyz = r0.xyz * v1.xyz + float3(-0.25,-0.25,-0.25);
  r0.xyz = v1.xyz * r0.xyz;
  r1.xyz = saturate(r1.xyz * cb0[2].www + float3(0.25,0.25,0.25));
  r2.xy = cmp(cb0[2].wy != float2(1,1));
  r0.xyz = r2.xxx ? r1.xyz : r0.xyz;
  r1.xyz = log2(r0.xyz);
  r1.xyz = cb0[2].xxx * r1.xyz;
  r2.xzw = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r2.xzw = exp2(r2.xzw);
  r2.xzw = r2.xzw * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  r3.xyz = float3(12.9200001,12.9200001,12.9200001) * r1.xyz;
  r1.xyz = cmp(r1.xyz >= float3(0.00313066994,0.00313066994,0.00313066994));
  r1.xyz = r1.xyz ? r2.xzw : r3.xyz;
  o0.xyz = r2.yyy ? r1.xyz : r0.xyz;
  o0.w = v1.w;

  // sRGB output (back buffer): clip like the 8-bit target, then Paper White.
  if (cb0[2].y != 1.f) {
    o0.xyz = saturate(o0.xyz) * EXR_ENCODED_SCALE;
  }
}
