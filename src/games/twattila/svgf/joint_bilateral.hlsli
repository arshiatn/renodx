/*
 * Copyright (C) 2026 ArshiaTN
 * Copyright (c) 2025 Carlos Lopez Jr.
 * SPDX-License-Identifier: MIT
 * Adapted from Toru77/renodx, src/games/falcomengine-plus/world/shaders/world_svgf_upsample.cs_5_0.hlsl (blob 3e4b1db1)
 * and world_rt_denoise.hlsli (blob 8f85613b).
 * Licence and permission notice: ./LICENSE.txt
 */

#ifndef SRC_GAMES_TWATTILA_SVGF_JOINT_BILATERAL_HLSLI_
#define SRC_GAMES_TWATTILA_SVGF_JOINT_BILATERAL_HLSLI_

// Toru's 2x2 joint bilateral upsample (world_svgf_upsample's algorithm 0, its nearest-valid fallback on): a full-size
// pixel's value from the four low-grid texels around its place there, tap t at (t & 1, t >> 1) from the floor of that
// place (`fraction`: the rest). Each tap's bilinear weight x its edge weight against the pixel's surface (`position`,
// `normal`; view space, the camera at the origin): its representative pixel's distance from the pixel's plane (sigma 2%
// of the pixel's distance) and its normal's agreement (power 16; world_rt_denoise's FilterPlaneNormalWeight); a tap not
// `valid` (no surface) none. The weights, summing to 1; where every edge weight vanishes, the valid tap with the largest
// bilinear weight alone; none valid: all 0.
float4 JointBilateralWeights(float2 fraction, float3 position, float3 normal, float3 tap_positions[4], float3 tap_normals[4], bool4 valid)
{
  const float distance = length(position);
  float weights[4];
  float4 nearest = 0.f;
  float nearest_weight = -1.f;
  [unroll] for (int tap = 0; tap < 4; ++tap)
  {
    weights[tap] = 0.f;
    if (valid[tap])
    {
      const float bilinear = ((tap & 1) ? fraction.x : 1.f - fraction.x) * ((tap >> 1) ? fraction.y : 1.f - fraction.y);
      if (bilinear > nearest_weight)
      {
        nearest_weight = bilinear;
        nearest = float4(tap == 0, tap == 1, tap == 2, tap == 3);
      }
      const float plane = dot(normal, tap_positions[tap] - position) / (0.02f * distance);
      weights[tap] = bilinear * exp(-0.5f * plane * plane) * pow(saturate(dot(normal, tap_normals[tap])), 16.f);
    }
  }
  const float4 joint = float4(weights[0], weights[1], weights[2], weights[3]);
  const float sum = dot(joint, 1.f);
  return (sum > 1e-6f) ? joint / sum : nearest;
}

#endif  // SRC_GAMES_TWATTILA_SVGF_JOINT_BILATERAL_HLSLI_
