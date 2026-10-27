#ifndef SRC_TALESOFARISE_TONEMAP_HLSLI_
#define SRC_TALESOFARISE_TONEMAP_HLSLI_

// Tales of Arise scene tonemapper (UE4 4.21 PostProcessTonemap, Bandai Namco additions), all permutations.
// Each shaders/tonemapNN file sets the permutation's features (0/1) and includes this file:
//   TOA_GRAIN_JITTER     scene UV jittered by the grain noise (cb0[42].z)
//   TOA_COLOR_FRINGE     chromatic aberration: red and green sampled shifted (cb0[55], cb0[45], cb0[43])
//   TOA_SHARPEN          5-tap sharpen (cb0[40].y)
//   TOA_VIGNETTE         vignette (cb0[40].x)
//   TOA_GRAIN_INTENSITY  film grain (cb0[42].xy)
//   TOA_NOISE_OVERLAY    animated texture overlay (t6 x tint, alpha by 3D noise t7 over time; cb0[51], cb0[52].x)
// Stock: scene (t2) x color scale + bloom (t3) x (tint + dirt mask t1), screen overlay (t5, game), noise
// overlay (game), exposure, vignette, grain, combined 32^3 LUT (t4) x 1.05 + dither, then the game's gamma option (pow cb0[50].w).
// Alpha: luma for FXAA. cb0[53].x == 1: stencil debug view (stock).
// Mod (HDR LUT, alpha tag 1 from lut00): Bloom / Vignette / Sharpening / Film Grain / Chromatic Aberration
// sliders, no 8-bit dither, the gamma option on the value relative to Paper White.
// SDR: the stock UNORM clip emulated, then scaled by Paper White / UI.

#include "./shared.h"

Texture3D<float4> t7 : register(t7);
Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture3D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);
SamplerState s7_s : register(s7);
SamplerState s6_s : register(s6);
SamplerState s5_s : register(s5);
SamplerState s4_s : register(s4);
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
  float4 cb1[133];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[56];
}

#define cmp -

float LumaSharpen(float3 color)
{
  return dot(color, float3(0.300000012,0.589999974,0.109999999));
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
  float4 r0,r1,r2,r3,r4;
  const bool hdr_lut = t4.Load(int4(0, 0, 0, 0)).w > 0.5f;

  // Grain noise (dither; with the grain features also jitter and intensity).
  float grain = v2.w * 543.309998 + v2.z;
  grain = sin(grain);
  grain = 493013 * grain;
  grain = frac(grain);

  float2 uv = v0.xy;
#if TOA_GRAIN_JITTER
  float jitter = (-grain * grain + 1) * cb0[42].z;
  if (hdr_lut) jitter *= SI.film_grain;
  const float4 jitter_offset = (v2.xyxy + -v0.xyxy) * jitter;
  uv = jitter_offset.zw + v0.xy;
#endif

  float3 scene;
#if TOA_COLOR_FRINGE
  {
    // Red and green shifted outward from the screen center (stock: SceneColorFringe).
    r1.xyzw = cmp(float4(0,0,0,0) < w0.xyxy);
    r3.xyzw = cmp(w0.xyxy < float4(0,0,0,0));
    r1.xyzw = (int4)-r1.xyzw + (int4)r3.xyzw;
    r1.xyzw = (int4)r1.xyzw;
    r3.xyzw = saturate(-cb0[55].zzzz + abs(w0.xyxy));
    r1.xyzw = r3.xyzw * r1.xyzw;
    float4 fringe = cb0[55].xxyy;
    if (hdr_lut) fringe *= SI.chromatic_aberration;
    r1.xyzw = -r1.xyzw * fringe + w0.xyxy;
    r1.xyzw = r1.xyzw * cb0[45].xyxy + cb0[45].zwzw;
#if TOA_GRAIN_JITTER
    // Jittered: all three samples clamped to the view rect.
    r1.xyzw = r1.xyzw * cb0[0].zwzw + jitter_offset;
    r1.xyzw = max(cb0[43].xyxy, r1.xyzw);
    r1.xyzw = min(cb0[43].zwzw, r1.xyzw);
    scene.x = t2.Sample(s2_s, r1.xy).x;
    scene.y = t2.Sample(s2_s, r1.zw).y;
    r1.xy = max(cb0[43].xy, uv);
    r1.xy = min(cb0[43].zw, r1.xy);
    scene.z = t2.Sample(s2_s, r1.xy).z;
#else
    r1.xyzw = cb0[0].zwzw * r1.xyzw;
    scene.x = t2.Sample(s2_s, r1.xy).x;
    scene.y = t2.Sample(s2_s, r1.zw).y;
    scene.z = t2.Sample(s2_s, uv).z;
#endif
  }
#else
  scene = t2.Sample(s2_s, uv).xyz;
#endif

#if TOA_SHARPEN
  {
    const float3 left = t2.SampleLevel(s2_s, uv, 0, int2(-1, 0)).xyz;
    const float3 right = t2.SampleLevel(s2_s, uv, 0, int2(1, 0)).xyz;
    const float3 up_left = t2.SampleLevel(s2_s, uv, 0, int2(-1, -1)).xyz;
    const float3 down_left = t2.SampleLevel(s2_s, uv, 0, int2(-1, 1)).xyz;
    const float center = LumaSharpen(scene);
    float4 delta = center - float4(LumaSharpen(left), LumaSharpen(right), LumaSharpen(up_left), LumaSharpen(down_left));
    delta.xy = max(abs(delta.xz), abs(delta.yw));
    float amount = saturate(-v1.x * max(delta.x, delta.y) + 1);
    float sharpen = cb0[40].y;
    if (hdr_lut) sharpen *= SI.sharpening;
    amount = sharpen * -amount;
    const float3 laplacian = -scene * float3(4,4,4) + (right + left + up_left + down_left);
    scene = laplacian * amount + scene;
    if (hdr_lut) scene = max(scene, 0.f);  // Sharpening above 1 can overshoot below black.
  }
#endif

  // Bloom x (tint + dirt mask).
  r1.xy = max(cb0[44].xy, v0.xy);
  r1.xy = min(cb0[44].zw, r1.xy);
  r1.xyz = t3.Sample(s3_s, r1.xy).xyz;
  r0.w = cmp(cb0[50].x == 1.000000);
  if (r0.w != 0) {
    r2.xy = asuint(cb0[23].xy);
    r2.xy = v4.xy + -r2.xy;
    r2.xy = cb0[22].zw * r2.xy;
    r2.xyz = t1.SampleLevel(s1_s, r2.xy, 0).xyz;
    r2.xyz = cb2[0].xyz * r2.xyz;
  } else {
    r2.xyz = float3(0,0,0);
  }
  r2.xyz = cb0[39].xyz + r2.xyz;
  r1.xyz = r2.xyz * r1.xyz;
  if (hdr_lut) r1.xyz *= SI.bloom;
  r0.xyz = scene * cb0[38].xyz + r1.xyz;

  // Screen overlay (game; stock).
  r0.w = cmp(0 < cb0[47].x);
  if (r0.w != 0) {
    r1.xy = cb1[123].yx * cb1[122].wz;
    r1.zw = uv.yx * r1.xy;
    r2.xyzw = cmp(float4(0,0,0,0) < cb0[49].xyzw);
    r1.xy = -uv.yx * r1.xy + float2(1,1);
    r1.xy = r2.yx ? r1.xy : r1.zw;
    r1.xy = float2(-0.5,-0.5) + r1.xy;
    r1.zw = cb0[48].ww * r1.xy;
    r2.x = cb0[47].w * r1.y + -r1.z;
    r2.y = cb0[47].w * r1.x + r1.w;
    r1.xy = float2(0.5,0.5) + r2.xy;
    r1.xyzw = t5.SampleLevel(s5_s, r1.xy, 0).xyzw;
    r0.w = r2.w ? r2.z : 0;
    r2.x = 1 + -r1.w;
    r3.xyz = saturate(r2.xxx);
    r4.xyz = float3(0,0,0);
    r4.w = r1.w;
    r3.w = 1;
    r2.xyzw = r2.wwww ? r4.xyzw : r3.xyzw;
    r1.xyzw = r0.wwww ? r1.xyzw : r2.xyzw;
    r1.xyzw = float4(-0,-0,-0,-1) + r1.xyzw;
    r1.xyzw = cb0[47].yyyy * r1.xyzw + float4(0,0,0,1);
    r0.w = dot(float3(0.300000012,0.400000006,0.300000012), r1.xyz);
    r0.w = saturate(0.00100000005 + r0.w);
    r2.x = log2(r0.w);
    r2.x = cb0[47].z * r2.x;
    r2.x = exp2(r2.x);
    r0.w = 0.00100000005 + r0.w;
    r0.w = r2.x / r0.w;
    r1.xyz = r0.www * r1.xyz;
    r1.xyz = cb0[48].xyz * r1.xyz;
    r0.xyz = r1.www * r0.xyz + r1.xyz;
  }

  r0.w = cmp(cb0[53].x == 1.000000);
  if (r0.w != 0) {
    // Stencil debug view (stock).
    r0.w = t0.SampleLevel(s0_s, uv, 0).w;
    r0.w = 255 * r0.w;
    r0.w = round(r0.w);
    r0.w = (uint)r0.w;
    r0.w = (int)r0.w & 15;
    r0.w = cmp((uint)r0.w < 11);
    r1.xyz = r0.www ? float3(0,1,1) : float3(1,1,0);
    o0.xyz = renodx::math::SignPow(r1.xyx, cb0[50].w);
    o0.w = r1.z;
    return;
  }

#if TOA_NOISE_OVERLAY
  {
    // Animated texture overlay (game; stock): t6 x tint toward which the scene is blended, its alpha flickering
    // with 3D noise (t7) slices stepped over time.
    static const float NOISE_SLICES[12] = { 0, 0.300000012, 0.150000006, 0.449999988, 0.300000012, 0.600000024,
                                            0.449999988, 0.75, 0.600000024, 0.899999976, 0.75, 0 };
    const float2 noise_uv = cb1[123].xy * cb1[122].zw * uv;
    const float3 phase = float3(1.5,2.4000001,0.13636364) * (cb1[132].z * cb0[52].x);
    const float flicker = sin(phase.y) * 0.5 + 0.5;
    const uint slice = (uint)min(10, max(0, floor(-floor(phase.z) * 11 + phase.x)));
    const float noise_a = t7.SampleLevel(s7_s, float3(noise_uv, NOISE_SLICES[slice]), 0).y;
    const float noise_b = t7.SampleLevel(s7_s, float3(noise_uv, NOISE_SLICES[slice + 1]), 0).y;
    const float noise = saturate(frac(phase.x) * (noise_b - noise_a) + noise_a);
    const float4 overlay = t6.SampleLevel(s6_s, noise_uv, 0);
    float alpha = saturate(overlay.w * 0.100000001 + 1.89999998 * (overlay.w * noise));
    alpha = cb0[51].x * exp2(log2(alpha) * (flicker * 0.350000024 + 0.850000024));
    r0.xyz = alpha * (overlay.xyz * cb0[51].yzw + -r0.xyz) + r0.xyz;
  }
#endif

  // Exposure.
  r0.xyz = v1.xxx * r0.xyz;
#if TOA_VIGNETTE
  r1.xy = cb0[40].xx * v1.yz;
  r1.z = dot(r1.xy, r1.xy);
  r1.z = 1 + r1.z;
  r1.z = rcp(r1.z);
  r1.z = r1.z * r1.z;
  if (hdr_lut) r1.z = 1.f - saturate((1.f - r1.z) * SI.vignette);
  r0.xyz = r0.xyz * r1.zzz;
#endif
#if TOA_GRAIN_INTENSITY
  r1.x = grain * cb0[42].x + cb0[42].y;
  if (hdr_lut) r1.x = max(lerp(1.f, r1.x, SI.film_grain), 0.f);
  r0.xyz = r0.xyz * r1.xxx;
#endif
  r0.xyz = r0.xyz + float3(0.00266771927,0.00266771927,0.00266771927);
  r0.xyz = log2(r0.xyz);
  r0.xyz = saturate(r0.xyz * float3(0.0714285746,0.0714285746,0.0714285746) + float3(0.610726953,0.610726953,0.610726953));
  r0.xyz = r0.xyz * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  r0.xyz = t4.Sample(s4_s, r0.xyz).xyz;
  r0.xyz = float3(1.04999995,1.04999995,1.04999995) * r0.xyz;

  const float encoded_scale = TOA_ENCODED_SCALE;
  [branch]
  if (hdr_lut) {
    // The gamma option on the value relative to Paper White (identity at 1).
    r0.xyz /= encoded_scale;
    o0.w = saturate(dot(r0.xyz, float3(0.298999995,0.587000012,0.114)));
    o0.xyz = renodx::math::SignPow(r0.xyz, cb0[50].w) * encoded_scale;
    return;
  }
  o0.w = saturate(dot(r0.xyz, float3(0.298999995,0.587000012,0.114)));
  // Dither, then the original UNORM target emulated before moving the SDR result into FP16.
  // Scaled with the scene, not with UI brightness.
  r0.w = grain * 0.00390625 + -0.001953125;
  o0.xyz = pow(saturate(r0.xyz + r0.www), cb0[50].w) * encoded_scale;
}

#endif  // SRC_TALESOFARISE_TONEMAP_HLSLI_
