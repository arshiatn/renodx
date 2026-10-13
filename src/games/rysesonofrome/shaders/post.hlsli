#ifndef RYSE_POST_HLSLI_
#define RYSE_POST_HLSLI_

// Ryse: Son of Rome post (CryEngine, after TAA): film grain (+ sharpening) (+ lens flares).
// From 0xD0B247FF / 0xB022AFEA / 0xC3A469C1 / 0xCC6DF78E.ps_5_0.cso (3Dmigoto); the four differ
// only in sharpening and lens flares.
// Stock: sharpening = 4 diagonal half-texel taps vs the center, squared (gamma 2 -> linear), lerp
// by vParams.x, saturate, sqrt; grain overlay (3D noise, strength by exposure); lens flares (t5,
// red shifted by vLensOpticsParams.w) screen-blended. All in [0, 1].
// Addon: in Paper White = 1; out x the Paper White-UI gain (applied here, after the TAA / DLSS:
// their history then does not change while the UI slider moves). SDR: stock. HDR: Sharpening slider, faded out from 1x to
// 2x Paper White, no undershoot below black (wide-gamut negatives pass); grain as stock up to white;
// lens flares screen-blended as stock, smooth past white (RyseScreen) (Film Grain, Lens Flares
// sliders); the game's full-screen chromatic aberration (Chromatic Aberration slider, off by default).
#ifndef RYSE_POST_SHARPEN
#define RYSE_POST_SHARPEN 1
#endif
#ifndef RYSE_POST_LENS
#define RYSE_POST_LENS 0
#endif

#include "../shared.h"

cbuffer PER_BATCH : register(b0)
{
#if RYSE_POST_SHARPEN
  float4 vParams : packoffset(c0);
  float4 HDREyeAdaptation : packoffset(c1);
  float4 HDRParams : packoffset(c2);
  float4 PS_ScreenSize : packoffset(c3);
#if RYSE_POST_LENS
  float4 vLensOpticsParams : packoffset(c4);
  float Time : packoffset(c5);
#else
  float Time : packoffset(c4);
#endif
#else
  float4 HDREyeAdaptation : packoffset(c0);
  float4 HDRParams : packoffset(c1);
  float4 PS_ScreenSize : packoffset(c2);
#if RYSE_POST_LENS
  float4 vLensOpticsParams : packoffset(c3);
  float Time : packoffset(c4);
#else
  float Time : packoffset(c3);
#endif
#endif
}

SamplerState _tex0_s : register(s0);
#if RYSE_POST_LENS
SamplerState _tex5_s : register(s5);
#endif
SamplerState filmGrainMap_s : register(s6);
SamplerState _tex7_s : register(s7);
Texture2D<float4> _tex0 : register(t0);
#if RYSE_POST_LENS
Texture2D<float4> _tex5 : register(t5);
#endif
Texture3D<float4> filmGrainMap : register(t6);
Texture2D<float4> _tex7 : register(t7);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  const bool hdr = HDR >= 1.f;
  const float scale = RYSE_ENCODED_SCALE;

  // Grain value: 3D noise, its strength set by the exposure.
  r0.x = _tex7.Sample(_tex7_s, v1.xy).x;
  r0.x = -HDRParams.x + r0.x;
  r0.x = HDRParams.y * r0.x + HDRParams.x;
  r0.y = 1 + r0.x;
  r0.y = log2(r0.y);
  r0.y = 2 + r0.y;
  r0.y = 2 / r0.y;
  r0.y = 1.02999997 + -r0.y;
  r0.x = r0.y / r0.x;
  r0.x = max(HDREyeAdaptation.y, r0.x);
  r0.x = min(HDREyeAdaptation.z, r0.x);
  r0.x = HDRParams.z * r0.x;
  r0.x = sqrt(r0.x);
  r1.x = PS_ScreenSize.x / PS_ScreenSize.y;
  r0.yz = float2(4,1) * v1.xy;
  r1.y = 4;
  r1.xy = r1.xy * r0.yz;
  r1.z = 3 * Time;
  r0.y = filmGrainMap.Sample(filmGrainMap_s, r1.xyz).x;
  r0.y = -0.5 + r0.y;
  r0.y = HDRParams.w * r0.y + 0.5;
  r0.z = 0.5 + -r0.y;
  r0.x = r0.x * r0.z + r0.y;
  r0.y = 1 + -r0.x;

  // Texel offsets from the size of the frame read (t0), which PS_ScreenSize.zw (0.5 / size) matches in the game's own
  // draws. The DLSS addon draws this pass again at the display size, where PS_ScreenSize stays the render size: the
  // offsets grew by display / render (7x at 14%).
  uint frame_width, frame_height;
  _tex0.GetDimensions(frame_width, frame_height);
  const float2 half_texel = 0.5f / float2(frame_width, frame_height);

  float4 center = _tex0.Sample(_tex0_s, v1.xy);
  // HDR: the game's full-screen chromatic aberration (its CA_Sharpening pass 0xC53CE326, which runs only while
  // r_ChromaticAberration > 0; the game keeps it at 0): red scaled away from the screen center by
  // 2 x amount x half a texel, blue toward it. Chromatic Aberration slider: 1 = amount 12, twice the strength
  // the game gives its lens flares' shift (r_FlaresChromaShift 6).
  [branch]
  if (hdr && SI.chromatic_aberration > 0.f) {
    const float2 shift = 2.f * (12.f * SI.chromatic_aberration) * half_texel;
    const float2 from_center = v1.xy - 0.5f;
    center.x = _tex0.Sample(_tex0_s, from_center * (1.f + shift) + 0.5f).x;
    center.z = _tex0.Sample(_tex0_s, from_center * (1.f - shift) + 0.5f).z;
  }

#if RYSE_POST_SHARPEN
  // Sharpening taps.
  r0.zw = -half_texel + v1.xy;
  r1.xyz = _tex0.Sample(_tex0_s, r0.zw).xyz;
  r2.xyzw = half_texel.xyxy * float4(1,-1,-1,1) + v1.xyxy;
  r3.xyz = _tex0.Sample(_tex0_s, r2.xy).xyz;
  r2.xyz = _tex0.Sample(_tex0_s, r2.zw).xyz;
  r0.zw = half_texel + v1.xy;
  float3 tap3 = _tex0.Sample(_tex0_s, r0.zw).xyz;
#endif

#if RYSE_POST_LENS
  // Lens flares, red channel shifted from the center (r_FlaresChromaShift).
  float4 lens_uv;
  lens_uv.xy = vLensOpticsParams.ww * half_texel;
  lens_uv.xy = lens_uv.xy * float2(2,2) + float2(1,1);
  lens_uv.zw = float2(-0.5,-0.5) + v1.xy;
  lens_uv.xy = lens_uv.zw * lens_uv.xy + float2(0.5,0.5);
  float3 lens;
  lens.x = _tex5.Sample(_tex5_s, lens_uv.xy).x;
  lens.yz = _tex5.Sample(_tex5_s, v1.xy).yz;
#endif

  [branch]
  if (!hdr) {
    // Stock.
#if RYSE_POST_SHARPEN
    r3.xyz = r3.xyz * r3.xyz;
    r1.xyz = r1.xyz * r1.xyz + r3.xyz;
    r1.xyz = r2.xyz * r2.xyz + r1.xyz;
    r1.xyz = tap3 * tap3 + r1.xyz;
    r1.xyz = float3(0.25,0.25,0.25) * r1.xyz;
    r2.xyzw = center;
    r2.xyz = r2.xyz * r2.xyz + -r1.xyz;
    r1.xyz = saturate(vParams.xxx * r2.xyz + r1.xyz);
    r1.xyz = sqrt(r1.xyz);
#else
    r1.xyz = center.xyz;
#endif
    o0.w = center.w;
    r2.xyz = float3(1,1,1) + -r1.xyz;
    r2.xyz = r2.xyz + r2.xyz;
    r0.yzw = -r2.xyz * r0.yyy + float3(1,1,1);
    r2.xyz = r1.xyz * r0.xxx;
    r1.xyz = cmp(r1.xyz >= float3(0.5,0.5,0.5));
    r1.xyz = r1.xyz ? float3(1,1,1) : 0;
    r0.xyz = -r2.xyz * float3(2,2,2) + r0.yzw;
    r2.xyz = r2.xyz + r2.xyz;
#if RYSE_POST_LENS
    r0.xyz = r1.xyz * r0.xyz + r2.xyz;
    r1.xyz = float3(1,1,1) + -r0.xyz;
    o0.xyz = lens * r1.xyz + r0.xyz;
#else
    o0.xyz = r1.xyz * r0.xyz + r2.xyz;
#endif
    o0.xyz *= scale;
    return;
  }

#if RYSE_POST_SHARPEN
  // HDR sharpening (signed squares).
  float3 s0 = r1.xyz * abs(r1.xyz);
  float3 s1 = r3.xyz * abs(r3.xyz);
  float3 s2 = r2.xyz * abs(r2.xyz);
  float3 s3 = tap3 * abs(tap3);
  float3 blur = 0.25f * (s0 + s1 + s2 + s3);
  float3 center2 = center.xyz * abs(center.xyz);
  float neighborhood_max = max(max(max(renodx::math::Max(r1.xyz), renodx::math::Max(r3.xyz)),
                                   max(renodx::math::Max(r2.xyz), renodx::math::Max(tap3))),
                               renodx::math::Max(center.xyz));
  float amount = 1.f + (vParams.x - 1.f) * SI.sharpening * saturate(2.f - neighborhood_max);
  float3 sharpened = amount * (center2 - blur) + blur;
  sharpened = max(sharpened, min(min(min(s0, s1), min(s2, s3)), min(center2, 0.f)));
  float3 color = sign(sharpened) * sqrt(abs(sharpened));
#else
  float3 color = center.xyz;
#endif

  // Grain: the stock overlay up to white (it fades out toward white, none past it).
  float3 base = saturate(color);
  float3 overlay = lerp(2.f * base * r0.x, 1.f - 2.f * (1.f - base) * r0.y, step(0.5f, base));
  color += (overlay - base) * SI.film_grain;

#if RYSE_POST_LENS
  // Lens flares: the stock screen, smooth past white.
  color = RyseScreen(color, lens * SI.lens_flare);
#endif

  o0.xyz = color * scale;
  o0.w = center.w;
}

#endif  // RYSE_POST_HLSLI_
