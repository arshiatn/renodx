/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows (screen_space_shadows.hpp): one of the dispatches sss_setup listed (b3: its index), Bend
// Studio's WriteScreenSpaceShadow (Shogun 2's copy, twshogun2completeedition/shaders/bend_sss_gpu.hlsli: Apache 2.0,
// Copyright 2023 Sony Interactive Entertainment; its sample count at run time) over the depth (t0, standard depth: near
// 0, far 1), written as occlusion (BEND_SSS_OCCLUSION: 1 - the shadow term) into the mask the patched sun shaders read.
#include "./sss.hlsli"

#define SAMPLE_COUNT        1920
#define HARD_SHADOW_SAMPLES 4
#define FADE_OUT_SAMPLES    8
#define BEND_SSS_OCCLUSION
#define BEND_SSS_LIGHT_CASTERS
#include "../twshogun2completeedition/shaders/bend_sss_gpu.hlsli"

cbuffer sss_dispatch : register(b3)
{
  uint g_sss_dispatch;
}

Texture2D<float> t_sss_depth : register(t0);
StructuredBuffer<int4> t_sss_parameters : register(t1);
SamplerState s_sss_depth : register(s0);
RWTexture2D<unorm float> t_rw_occlusion : register(u0);

// No shadow map in this pass (Shogun 2's combines its cascades here).
float ReadShadowMap(float2 pixel_xy)
{
  return 1;
}

[numthreads(WAVE_SIZE, 1, 1)]
void main(int3 group_id : SV_GroupID, int thread_id : SV_GroupThreadID)
{
  DispatchParameters dp;
  dp.SetDefaults();
  dp.SurfaceThickness = g_sss_thickness;
  dp.ShadowContrast = g_sss_contrast;
  dp.BilinearThreshold = g_sss_edge_threshold;
  dp.IgnoreEdgePixels = (g_sss_ignore_edges != 0.f);
  dp.UsePrecisionOffset = (g_sss_precision != 0.f);
  dp.BilinearSamplingOffsetMode = (g_sss_along_ray != 0.f);
  dp.HardShadowSamples = (int)g_sss_hard_samples;
  dp.FadeOutSamples = (int)g_sss_fade_samples;
  dp.MirrorEdges = (g_sss_mirror_edges != 0.f);
  dp.CasterLight = g_sss_foliage_light;
  // None past 60% of the far plane's distance (from 40%): Attila's campaign backdrop sits near 75%; battles' far plane
  // is 13 km away.
  dp.FarFade = float2(0.4f, 0.6f);
  dp.LightCoordinate = asfloat(t_sss_parameters[0]);
  dp.WaveOffset = t_sss_parameters[1 + g_sss_dispatch].xy;
  dp.FarDepthValue = 1.f;
  dp.NearDepthValue = 0.f;
  dp.InvDepthTextureSize = g_sss_inverse_size;
  dp.FullSize = g_sss_size;
  dp.RenderSize = g_sss_render_size;
  dp.DepthTexture = t_sss_depth;
  dp.OutputTexture = t_rw_occlusion;
  dp.PointBorderSampler = s_sss_depth;

  WriteScreenSpaceShadow(dp, group_id, thread_id, (int)g_sss_samples);
}
