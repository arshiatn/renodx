// effects_overlay.hlsl ps30_overlay: the particles' buffers onto the scene (read at the VFX dynamic resolution,
// input_view_scale), drawn twice: the additive particles (fire, sparks, glows, trails: their own shaders scale them),
// then the alpha-blended ones (smoke, dust), premultiplied colour and transmittance (blend: one, source alpha).
// HDR, smoke draw (SI.vfx_smoke_draw, set per draw by the addon from the blend state): colour x VFX Brightness: Smoke &
// Dust, opacity by VFX Opacity: Smoke & Dust (transmittance^k, Beer-Lambert: thicker or thinner, black smoke too).
#include "../shared.h"

cbuffer camera_VS_PS : register(b0)
{
  float g_texel_offset : packoffset(c35.z);
  float2 g_inv_screen_size_minus_one : packoffset(c42);
}

cbuffer overlay_cb : register(b1)
{
  float input_view_scale : packoffset(c0);
}

SamplerState input_sampler_s : register(s0);
Texture2D<float4> input_sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  const float2 uv = (g_texel_offset + v0.xy - 0.5f) * g_inv_screen_size_minus_one * input_view_scale;
  // The game's tex2Dlod(TC.xyyy): its mip level is TC.y.
  o0 = input_sampler.SampleLevel(input_sampler_s, uv, uv.y);
  [branch]
  if (HDR >= 1.f && SI.vfx_smoke_draw != 0.f) {
    const float transmittance = saturate(o0.a);
    const float thinned = pow(max(transmittance, 1e-6f), SI.vfx_smoke_opacity);
    // Premultiplied colour with the opacity: x (1 - T^k) / (1 - T), k where nothing covers yet.
    const float opacity_scale = (transmittance < 0.9999f) ? (1.f - thinned) / (1.f - transmittance) : SI.vfx_smoke_opacity;
    o0.rgb *= opacity_scale * SI.vfx_smoke;
    o0.a = thinned;
  }
}
