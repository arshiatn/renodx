// ---- Created with 3Dmigoto v1.3.16 on Thu Sep 24 04:50:57 2026

// ROME REMASTERED cutscene00 - video, BT.601 limited-range YUV -> RGB x vertex colour.
// Stock has no clamp; the YUV maths overshoots below 0 and above 1, which the UNORM
// target used to hide. Video is mastered for a 2.2 screen; scene paper white.

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s5_s : register(s5);


// 3Dmigoto declarations
#define cmp -
#include "../autohdr.hlsli"


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = t2.Sample(s5_s, v2.xy).w;
  r0.x = -0.501960814 + r0.x;
  r0.xy = float2(1.59602678,0.812967658) * r0.xx;
  r0.z = t1.Sample(s5_s, v2.xy).w;
  r0.z = -0.501960814 + r0.z;
  r0.y = r0.z * 0.391762286 + r0.y;
  r0.z = 2.01723218 * r0.z;
  r0.w = t0.Sample(s5_s, v2.xy).w;
  r0.w = -0.0627451017 + r0.w;
  r1.y = r0.w * 1.16438353 + -r0.y;
  r1.x = r0.w * 1.16438353 + r0.x;
  r1.z = r0.w * 1.16438353 + r0.z;
  o0.xyz = v1.xyz * r1.xyz;
  o0.w = v1.w;

  // Restore the UNORM clamp lost to the FP16 upgrade.
  o0 = saturate(o0);

  // Video AutoHDR stretches 8-bit steps: Video Dithering adds 1 code value (peak to peak) of interleaved gradient
  // noise first, static in screen space.
  if (HDR >= 1.f && SI.video_autohdr > 0.f && SI.video_dithering != 0.f) {
    o0.xyz += (frac(52.9829189f * frac(dot(floor(v0.xy), float2(0.06711056f, 0.00583715f)))) - 0.5f) / 255.f;
  }
  o0.xyz = renodx::color::gamma::EncodeSafe(
      AutoHdrHighlights(renodx::color::gamma::DecodeSafe(saturate(o0.xyz)), SI.video_autohdr)
      * (SI.diffuse_white_nits / SI.graphics_white_nits));
  return;
}
