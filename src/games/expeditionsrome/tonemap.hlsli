#ifndef SRC_EXPEDITIONSROME_TONEMAP_HLSLI_
#define SRC_EXPEDITIONSROME_TONEMAP_HLSLI_

// Expeditions: Rome scene tonemapper (UE4 PostProcessTonemap), all permutations. Each shaders/tNN file sets the
// permutation's features (0/1) and includes this file:
//   EXR_GRAIN_JITTER     scene UV jittered by the grain noise (cb0[42].z)
//   EXR_COLOR_FRINGE     chromatic aberration: red and green sampled shifted (cb0[47], cb0[45], cb0[43])
//   EXR_SHARPEN          sharpen (cb0[40].y), 2 taps + 2 quad derivatives
//   EXR_VIGNETTE         vignette (cb0[40].x)
//   EXR_GRAIN_INTENSITY  film grain (cb0[42].xy)
// Stock: scene (t1) x color scale + bloom (t2) x (tint + dirt mask t0 x cb2[0]), both x pre-exposure (cb1[129].z),
// exposure, vignette, grain, combined 32^3 LUT (t3) x 1.05 + dither. Alpha: luma for FXAA.
// Mod (HDR LUT, alpha tag 1 from lut00): Bloom / Vignette / Sharpening / Film Grain / Chromatic Aberration sliders,
// no 8-bit dither. SDR: the stock UNORM clip emulated, then scaled by Paper White / UI.

#include "./shared.h"

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
  float4 cb0[48];
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
  float4 r0,r1;
  const bool hdr_lut = t3.Load(int4(0, 0, 0, 0)).w > 0.5f;
  const float pre_exposure = cb1[129].z;

  // Grain noise (dither; with the grain features also jitter and intensity).
  float grain = v2.w * 543.309998 + v2.z;
  grain = sin(grain);
  grain = 493013 * grain;
  grain = frac(grain);

  float2 uv = v0.xy;
#if EXR_GRAIN_JITTER
  float jitter = (-grain * grain + 1) * cb0[42].z;
  if (hdr_lut) jitter *= SI.film_grain;
  const float4 jitter_offset = (v2.xyxy + -v0.xyxy) * jitter;
  uv = jitter_offset.zw + v0.xy;
#endif

  float3 scene;
#if EXR_COLOR_FRINGE
  {
    // Red and green shifted outward from the screen center (stock: SceneColorFringe).
    r0.xyzw = cmp(float4(0,0,0,0) < w0.xyxy);
    r1.xyzw = cmp(w0.xyxy < float4(0,0,0,0));
    r0.xyzw = (int4)-r0.xyzw + (int4)r1.xyzw;
    r0.xyzw = (int4)r0.xyzw;
    r1.xyzw = saturate(-cb0[47].zzzz + abs(w0.xyxy));
    r0.xyzw = r1.xyzw * r0.xyzw;
    float4 fringe = cb0[47].xxyy;
    if (hdr_lut) fringe *= SI.chromatic_aberration;
    r0.xyzw = -r0.xyzw * fringe + w0.xyxy;
    r0.xyzw = r0.xyzw * cb0[45].xyxy + cb0[45].zwzw;
#if EXR_GRAIN_JITTER
    // Jittered: all three samples clamped to the view rect.
    r0.xyzw = r0.xyzw * cb0[0].zwzw + jitter_offset;
    r0.xyzw = max(cb0[43].xyxy, r0.xyzw);
    r0.xyzw = min(cb0[43].zwzw, r0.xyzw);
    scene.x = t1.Sample(s1_s, r0.xy).x;
    scene.y = t1.Sample(s1_s, r0.zw).y;
    r0.xy = max(cb0[43].xy, uv);
    r0.xy = min(cb0[43].zw, r0.xy);
    scene.z = t1.Sample(s1_s, r0.xy).z;
#else
    r0.xyzw = cb0[0].zwzw * r0.xyzw;
    scene.x = t1.Sample(s1_s, r0.xy).x;
    scene.y = t1.Sample(s1_s, r0.zw).y;
    scene.z = t1.Sample(s1_s, uv).z;
#endif
  }
#else
  scene = t1.Sample(s1_s, uv).xyz;
#endif
  scene = pre_exposure * scene;

#if EXR_SHARPEN
  {
    // Horizontal and vertical neighbors: one sampled on the side given by the pixel's quad position, the other
    // from the quad derivative.
    const float2 quad_side = (float2)((uint2)w0.xy & uint2(1, 1)) * float2(2,2) + float2(-1,-1);
    const float3 side_x = t1.Sample(s1_s, float2(cb0[0].z * quad_side.x, 0) + uv).xyz;
    const float3 side_y = t1.Sample(s1_s, float2(0, quad_side.y) * cb0[0].zw + uv).xyz;
    float3 laplacian = -ddx_fine(scene) * quad_side.x + scene;
    laplacian = side_x * pre_exposure + laplacian;
    laplacian = side_y * pre_exposure + laplacian;
    laplacian = laplacian + (-ddy_fine(scene) * quad_side.y + scene);
    laplacian = -scene * float3(4,4,4) + laplacian;
    const float center = LumaSharpen(scene);
    float4 delta = center - float4(LumaSharpen(pre_exposure * side_x), LumaSharpen(pre_exposure * side_y),
                                   -ddx_fine(center) * quad_side.x + center, -ddy_fine(center) * quad_side.y + center);
    delta.xy = max(abs(delta.xz), abs(delta.yw));
    float amount = saturate(-v1.x * max(delta.x, delta.y) + 1);
    float sharpen = cb0[40].y;
    if (hdr_lut) sharpen *= SI.sharpening;
    amount = sharpen * -amount;
    scene = laplacian * amount + scene;
  }
#endif

  // Bloom x (tint + dirt mask).
  r0.xy = asuint(cb0[23].xy);
  r0.xy = v4.xy + -r0.xy;
  r0.xy = cb0[22].zw * r0.xy;
  r0.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r0.xyz = r0.xyz * cb2[0].xyz + cb0[39].xyz;
  r1.xy = max(cb0[44].xy, v0.xy);
  r1.xy = min(cb0[44].zw, r1.xy);
  r1.xyz = t2.Sample(s2_s, r1.xy).xyz;
  r1.xyz = pre_exposure * r1.xyz;
  r0.xyz = r1.xyz * r0.xyz;
  if (hdr_lut) r0.xyz *= SI.bloom;
  r0.xyz = scene * cb0[38].xyz + r0.xyz;

  // Exposure.
  r0.xyz = v1.xxx * r0.xyz;
#if EXR_VIGNETTE
  r1.xy = cb0[40].xx * v1.yz;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = 1 + r0.w;
  r0.w = rcp(r0.w);
  r0.w = r0.w * r0.w;
  if (hdr_lut) r0.w = 1.f - saturate((1.f - r0.w) * SI.vignette);
  r0.xyz = r0.xyz * r0.www;
#endif
#if EXR_GRAIN_INTENSITY
  r0.w = grain * cb0[42].x + cb0[42].y;
  if (hdr_lut) r0.w = lerp(1.f, r0.w, SI.film_grain);
  r0.xyz = r0.xyz * r0.www;
#endif
  r0.xyz = r0.xyz + float3(0.00266771927,0.00266771927,0.00266771927);
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
  // Dither, then the original UNORM target emulated before moving the SDR result into FP16.
  // Scaled with the scene, not with UI brightness.
  r0.w = grain * 0.00390625 + -0.001953125;
  o0.xyz = saturate(r0.xyz * float3(1.04999995,1.04999995,1.04999995) + r0.www) * encoded_scale;
  r0.xyz = float3(1.04999995,1.04999995,1.04999995) * r0.xyz;
  o0.w = saturate(dot(r0.xyz, float3(0.298999995,0.587000012,0.114)));
}

#endif  // SRC_EXPEDITIONSROME_TONEMAP_HLSLI_
