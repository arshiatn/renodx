/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_AA_HLSLI_
#define SRC_GAMES_TWATTILA_AA_HLSLI_

// Anti-aliasing (anti_aliasing.hpp): the settings of its compute shaders, on the tone mapper's output (2.0 encoded,
// signed BT.709: wide colours below 0).
cbuffer aa_settings : register(b0)
{
  float4 g_aa_metrics;              // 1 / width, 1 / height, width, height (SMAA_RT_METRICS)
  float g_smaa_threshold;           // SMAA's SMAA_THRESHOLD
  float g_smaa_search_steps;        // SMAA_MAX_SEARCH_STEPS
  float g_smaa_search_steps_diag;   // SMAA_MAX_SEARCH_STEPS_DIAG
  float g_smaa_corner_rounding;     // SMAA_CORNER_ROUNDING
  float g_cmaa2_edge_threshold;     // CMAA2's edge threshold (its quality preset)
  float g_cmaa2_local_contrast;     // CMAA2's local contrast adaptation (its extra sharpness)
  float g_cmaa2_simple_shape_blur;  // CMAA2's simple shape blurriness (its extra sharpness)
  float g_aa_frame_gamma;           // the frame's gamma (frame_gamma.hlsli TW_FRAME_GAMMA): 2.2 AaFrameGamma22; else 2.0
  float4 g_smaa_subsample_indices;  // SMAA T2x: this frame's sample (SMAA 1x: 0)
  float4 g_dlaa_debug;              // DLAA's Debug View (anti_aliasing.hpp Settings debug_view): x the view, yz the input size;
                                    // w the colour NVIDIA gets: 1 its square root (presets L and M, Ray
                                    // Reconstruction), else linear
}

// TAA and SMAA T2x (aa_taa_*.cs): the cameras in the game's register layout (mul(float4(p, 1), m) as its shaders).
cbuffer aa_taa : register(b1)
{
  float4x4 g_taa_view_projection;           // this frame's camera, without the jitter
  float4x4 g_taa_inverse_view_projection;   // this frame's camera as the depth was drawn (jittered)
  float4x4 g_taa_previous_view_projection;  // the last frame's, without the jitter
  float2 g_taa_projection_z;                // this frame's projection _m22, _m32: depth to distance
  float g_taa_history_weight;               // the history's share: 0.5 (SMAA T2x) to 0.97
  float g_taa_reset;                        // 1: no history (first frame, new size)
  float g_taa_sharpening;                   // 0 none
  float2 g_taa_jitter;                      // this frame's: the scene moved by it, in pixels (x right, y down)
  float g_taa_object_motion;                // TAA Motion Vectors on Objects: 1 (t4-t6: what moved this frame)
}

// The passes that take the tone mapper's 2.0-encoded output to linear and back (DLAA's prepare and finish, CMAA2's) in
// the frame's own curve: with Gamma Correction on the swap chain decodes the frame at 2.2 (Attila and Thrones hand it
// over), so they decode and encode at 2.2 too; else their own 2.0 (square and square root, their bits as before).
bool AaFrameGamma22()
{
  return g_aa_frame_gamma == 2.2f;
}

float3 AaDecodeFrame22(float3 color)
{
  return sign(color) * pow(abs(color), 2.2f);
}

float3 AaEncodeFrame22(float3 color)
{
  return sign(color) * pow(abs(color), 1.f / 2.2f);
}

#endif  // SRC_GAMES_TWATTILA_AA_HLSLI_
