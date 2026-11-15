/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced object shadows (ray_tracing.hpp, O1): last frame's main depth (the objects; the bridge's prepare pass wrote
// it at the bloom) scattered into this frame's view over the ground pre-pass's depth, the nearer of the two kept: the
// rays start from both. Depth is INTZ's (0 near), so the nearest is the smallest float, and as uints the same order.
Texture2D<float> previous_depth : register(t0);
RWTexture2D<uint> depth : register(u0);

cbuffer reprojection : register(b0) {
  row_major float4x4 previous_to_current;  // clip to clip: this frame's view-projection x last frame's inverse
  float4 jitter;                            // the bridge's NDC jitter offsets: last frame's .xy, this frame's .zw
  float4 size;                              // width, height, 1 / width, 1 / height
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= uint2(size.xy))) return;
  const float z = previous_depth[id.xy];
  if (z >= 1.f) return;  // the sky
  const float2 ndc = (float2(id.xy) + 0.5f) * size.zw * float2(2.f, -2.f) + float2(-1.f, 1.f) - jitter.xy;
  const float4 clip = mul(previous_to_current, float4(ndc, z, 1.f));
  if (clip.w <= 0.f) return;
  const float3 current = clip.xyz / clip.w;
  const float2 pixel = ((current.xy + jitter.zw) * float2(0.5f, -0.5f) + 0.5f) * size.xy;
  if (any(pixel < 0.f) || any(pixel >= size.xy) || current.z < 0.f || current.z >= 1.f) return;
  InterlockedMin(depth[uint2(pixel)], asuint(current.z));
}
