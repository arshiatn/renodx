/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_AA_SMAA_HLSLI_
#define SRC_GAMES_TWATTILA_AA_SMAA_HLSLI_

// SMAA 1x (smaa/SMAA.hlsl, MIT, Jimenez et al. 2013) as compute shaders: its vertex and pixel functions called per
// pixel; its quality from the settings (anti_aliasing.hpp's presets). Its edge detection's discard returns no edge.
#include "./aa.hlsli"

// The tone mapper's output: 0 its square root (Attila, Thrones, WH3...: SMAA's luma edges take it as it is), 1 linear
// (Pharaoh, Troy: their wrappers define it): the edges read it 2.0 encoded, as SMAA.hlsl's luma edge detection needs
// gamma-corrected colours; the blending reads it linear, SMAA's own choice for its last pass.
#ifndef TW_AA_FRAME_LINEAR
#define TW_AA_FRAME_LINEAR 0
#endif

SamplerState aa_linear : register(s0);
SamplerState aa_point : register(s1);

float4 AaEdgeColour(float4 colour)
{
  return (TW_AA_FRAME_LINEAR ? float4(sign(colour.rgb) * sqrt(abs(colour.rgb)), colour.a) : colour);
}

#define SMAA_CUSTOM_SL 1
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(aa_linear, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(aa_point, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(aa_linear, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(aa_linear, coord, 0)
// The edge detections' reads alone (SMAA.hlsl reads nothing else through it).
#define SMAASamplePoint(tex, coord) AaEdgeColour(tex.SampleLevel(aa_point, coord, 0))
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(aa_linear, coord, 0, offset)
#define SMAAGather(tex, coord) tex.Gather(aa_linear, coord, 0)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAA_RT_METRICS g_aa_metrics
#define SMAA_THRESHOLD g_smaa_threshold
#define SMAA_MAX_SEARCH_STEPS g_smaa_search_steps
#define SMAA_MAX_SEARCH_STEPS_DIAG g_smaa_search_steps_diag
#define SMAA_CORNER_ROUNDING g_smaa_corner_rounding
#define discard return 0
#include "./smaa/SMAA.hlsl"
#undef discard

// The pixel's texture coordinate; false: outside the screen.
bool AaTexcoord(uint2 pixel, out float2 texcoord)
{
  texcoord = (float2(pixel) + 0.5f) * g_aa_metrics.xy;
  return all(float2(pixel) < g_aa_metrics.zw);
}

#endif  // SRC_GAMES_TWATTILA_AA_SMAA_HLSLI_
