// ssao2.hlsl ps50_blur_x / ps50_blur_y (Troy's and Pharaoh's AO blurs 7F637259 / BFD285BD: the same bytes): the game's blur of its AO along one axis,
// BLUR_STEPS taps a side, a texel (g_Resolution.zw: the texture's, at any scale) apart, each weighted by its likeness to
// the centre and its distance. Below 100% the pass draws the top-left share of its half-size target and its uv (TEXCOORD0 of
// the full-screen vertex shader 31C49ED2) runs 0-1 over that share: the centre is read at uv x the share and the taps stay a
// texel apart, held on the share's last texel as the point clamp sampler holds them on the texture's at 100% (view_share's
// region read scaled the taps too: a third of a texel apart at 33%). At 100% (cb12 unbound) the game's bytes' result.
#include "../shared.h"
#include "../../twattila/render_scale.hlsli"

cbuffer ssao2_PS : register(b0)
{
  float2 g_DepthLinearization : packoffset(c0);
  float4 g_Resolution : packoffset(c1);
  float4 g_ToViewTransform : packoffset(c2);
  float2 g_CameraFovXY : packoffset(c3);
  float2 g_AngleBias : packoffset(c3.z);
  float3 g_RadiusAndPower1 : packoffset(c4);
  float3 g_RadiusAndPower2 : packoffset(c5);
  float3 g_RadiusAndPower3 : packoffset(c6);
  float3 g_Distances : packoffset(c7);
  float g_MaxRadius : packoffset(c7.w);
  float NUM_DIRS : packoffset(c8);
  float NUM_STEPS : packoffset(c8.y);
  float BLUR_STEPS : packoffset(c8.z);
}

SamplerState s_point_clamp_sampler_s : register(s0);
Texture2D<float> ssao2_generated_texture : register(t0);

// `axis`: (1, 0) blur_x, (0, 1) blur_y. Its AO and the centre's for the next pass.
float2 GameAoBlur(float2 uv, float2 axis)
{
  const float2 share = FrameRenderShare();
  const float2 last = share - 0.5f * g_Resolution.zw;
  const float2 centre_uv = uv * share;
  const float centre = ssao2_generated_texture.Sample(s_point_clamp_sampler_s, centre_uv).x;
  const float inverse_steps = 1.f / BLUR_STEPS;
  const float likeness = 1.f / (1.f + centre);
  float2 sum = float2(centre, 1.f);
  [loop]
  for (int step = 1; (float)step < BLUR_STEPS; ++step) {
    const float2 offset = axis * ((float)step * g_Resolution.zw);
    const float near = -(float)step * inverse_steps + 1.f;
    const float plus = ssao2_generated_texture.Sample(s_point_clamp_sampler_s, clamp(centre_uv + offset, 0.f, last)).x;
    const float plus_weight = near * max(-abs(centre - plus) * 1.25f + 1.f, 0.f);
    const float plus_sum = plus * (likeness * plus_weight) + sum.x;
    const float plus_count = plus_weight * likeness + sum.y;
    const float minus = ssao2_generated_texture.Sample(s_point_clamp_sampler_s, clamp(centre_uv - offset, 0.f, last)).x;
    const float minus_weight = near * max(-abs(centre - minus) * 1.25f + 1.f, 0.f);
    sum = float2(minus * (likeness * minus_weight) + plus_sum, minus_weight * likeness + plus_count);
  }
  return float2(sum.x / sum.y, centre);
}
