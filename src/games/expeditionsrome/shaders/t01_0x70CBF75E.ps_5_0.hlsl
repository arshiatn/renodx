// Expeditions: Rome scene tonemap, no vignette. From 0x70CBF75E.ps_5_0.cso (3Dmigoto).
// Scene (t1) x color scale + bloom (t2) x (bloom intensity + dirt mask t0 x tint),
// exposure, combined 32^3 LUT (t3) x 1.05 + dither. Alpha: luma for FXAA.
// HDR LUT alpha tag (1): no dither, Bloom slider.
#include "../shared.h"

Texture3D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[130];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[45];
}

void main(
  linear noperspective float2 v0 : TEXCOORD0,
  linear noperspective float2 w0 : TEXCOORD3,
  linear noperspective float3 v1 : TEXCOORD1,
  linear noperspective float4 v2 : TEXCOORD2,
  float2 v3 : TEXCOORD4,
  float4 v4 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  const bool hdr_lut = t3.Load(int4(0, 0, 0, 0)).w > 0.5f;

  // Bloom: dirt mask on viewport UV x tint + intensity.
  r0.xy = asuint(cb0[23].xy);
  r0.xy = v4.xy + -r0.xy;
  r0.xy = cb0[22].zw * r0.xy;
  r0.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r0.xyz = r0.xyz * cb2[0].xyz + cb0[39].xyz;
  r1.xy = max(cb0[44].xy, v0.xy);
  r1.xy = min(cb0[44].zw, r1.xy);
  r1.xyz = t2.Sample(s2_s, r1.xy).xyz;
  r1.xyz = cb1[129].zzz * r1.xyz;
  r0.xyz = r1.xyz * r0.xyz;
  if (hdr_lut) r0.xyz *= SI.bloom;
  // Scene.
  r1.xyz = t1.Sample(s1_s, v0.xy).xyz;
  r1.xyz = cb1[129].zzz * r1.xyz;
  r0.xyz = r1.xyz * cb0[38].xyz + r0.xyz;

  r0.xyz = r0.xyz * v1.xxx + float3(0.00266771927,0.00266771927,0.00266771927);
  r0.xyz = log2(r0.xyz);
  r0.xyz = saturate(r0.xyz * float3(0.0714285746,0.0714285746,0.0714285746) + float3(0.610726953,0.610726953,0.610726953));
  r0.xyz = r0.xyz * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  r0.xyz = t3.Sample(s3_s, r0.xyz).xyz;

  const float encoded_scale = EXR_ENCODED_SCALE;
  [branch]
  if (hdr_lut) {
    o0.xyz = 1.05f * r0.xyz;
    o0.w = saturate(dot(o0.xyz / encoded_scale, float3(0.298999995,0.587000012,0.114)));
    return;
  }
  // Dither.
  r0.w = v2.w * 543.309998 + v2.z;
  r0.w = sin(r0.w);
  r0.w = 493013 * r0.w;
  r0.w = frac(r0.w);
  r0.w = r0.w * 0.00390625 + -0.001953125;
  // Emulate the original UNORM render target before moving the SDR result into FP16.
  // Scale the dither with the scene, not with UI brightness.
  o0.xyz = saturate(r0.xyz * float3(1.04999995,1.04999995,1.04999995) + r0.www) * encoded_scale;
  r0.xyz = float3(1.04999995,1.04999995,1.04999995) * r0.xyz;
  o0.w = saturate(dot(r0.xyz, float3(0.298999995,0.587000012,0.114)));
}
