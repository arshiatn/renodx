/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private downsample, not a game replacement registration. DLSS upscaling replaces the game's TAA: its render-size
// output (read by bloom, eye adaptation, the motion blur tiles and the next frame's reflections) gets DLSS's
// output-size result. Four bilinear taps a quarter of an output pixel apart: an exact 2x2 box at 50%, a tent over
// the footprint at other scales.
Texture2D<float4> Source : register(t0);
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> Output : register(u0);

cbuffer DownsampleConstants : register(b0) {
  uint2 Extent;  // the game's view, from the buffer's origin
  uint2 Padding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  if (any(dispatch_id.xy >= Extent)) return;

  const float2 texel = 1.f / float2(Extent);
  const float2 uv = (float2(dispatch_id.xy) + 0.5f) * texel;
  const float2 offset = 0.25f * texel;
  const float4 color = Source.SampleLevel(LinearClamp, uv + float2(-offset.x, -offset.y), 0)
                       + Source.SampleLevel(LinearClamp, uv + float2(offset.x, -offset.y), 0)
                       + Source.SampleLevel(LinearClamp, uv + float2(-offset.x, offset.y), 0)
                       + Source.SampleLevel(LinearClamp, uv + float2(offset.x, offset.y), 0);
  Output[dispatch_id.xy] = (all(isfinite(color)) ? 0.25f * color : 0.f);
}
