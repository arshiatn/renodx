/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private RGB/metadata merge, not a game replacement registration. The Upscaling Debug View instead (DebugView 1): the
// motion the upscaler got (t2, the render size at the top left) stretched over the picture, as Attila's: the direction as
// the hue, brighter the faster; the scene dimmed under it (t3, the upscaler's colour input).
#include "../twattila/motion.hlsli"

Texture2D<float4> DlaaColor : register(t0);
Texture2D<float4> StockTaaColor : register(t1);
Texture2D<float2> UpscalerMotion : register(t2);
Texture2D<float4> UpscalerColor : register(t3);
RWTexture2D<float4> OutputColor : register(u0);

// The input preparation's constants (0xB69D8215's, dlss_graphics.hpp PrepareConstants).
cbuffer PreparationConstants : register(b0) {
  float4 ClipToPrevious[4];
  uint2 Extent;
  float2 PixelJitter;
  uint CopyColor;
  uint DebugView;
  uint2 Padding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  uint width;
  uint height;
  OutputColor.GetDimensions(width, height);
  if (dispatch_id.x >= width || dispatch_id.y >= height) return;

  const float4 stock_color = StockTaaColor.Load(int3(dispatch_id.xy, 0));
  if (DebugView == 1u) {
    const int2 input = min(int2((float2(dispatch_id.xy) + 0.5f) * float2(Extent) / float2(width, height)), int2(Extent) - 1);
    const float3 dim = 0.1f * dot(abs(UpscalerColor.Load(int3(input, 0)).rgb), float3(0.2126f, 0.7152f, 0.0722f));
    OutputColor[dispatch_id.xy] = float4(max(MotionColour(UpscalerMotion.Load(int3(input, 0))), dim), stock_color.a);
    return;
  }
  float3 dlaa_rgb = DlaaColor.Load(int3(dispatch_id.xy, 0)).rgb;
  if (!all(isfinite(dlaa_rgb))) {
    dlaa_rgb = stock_color.rgb;
  }

  // Scene TAA alpha is engine metadata, not DLSS-produced transparency.
  OutputColor[dispatch_id.xy] = float4(dlaa_rgb, stock_color.a);
}
