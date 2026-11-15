/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private RGB/metadata merge, not a game replacement registration.
Texture2D<float4> DlaaColor : register(t0);
Texture2D<float4> StockTaaColor : register(t1);
RWTexture2D<float4> OutputColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  uint width;
  uint height;
  OutputColor.GetDimensions(width, height);
  if (dispatch_id.x >= width || dispatch_id.y >= height) return;

  const float4 stock_color = StockTaaColor.Load(int3(dispatch_id.xy, 0));
  float3 dlaa_rgb = DlaaColor.Load(int3(dispatch_id.xy, 0)).rgb;
  if (!all(isfinite(dlaa_rgb))) {
    dlaa_rgb = stock_color.rgb;
  }

  // Scene TAA alpha is engine metadata, not DLSS-produced transparency.
  OutputColor[dispatch_id.xy] = float4(dlaa_rgb, stock_color.a);
}
