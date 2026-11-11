/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows (screen_space_shadows.hpp): one of the dispatches sss_setup listed (b3: its index), Bend
// Studio's WriteScreenSpaceShadow (Shogun 2's copy, twshogun2completeedition/shaders/bend_sss_gpu.hlsli: Apache 2.0,
// Copyright 2023 Sony Interactive Entertainment; its sample count at run time) over the depth (t0, standard depth: near
// 0, far 1), written as occlusion (BEND_SSS_OCCLUSION: 1 - the shadow term) into the mask the patched sun shaders read.
// SSS_EXTENDED (sss_bend_extended): with Bend's extended march (BEND_SSS_EXTENDED: Auto Thickness, banners at t2) and what
// it found written too: the penumbra's radius (u1, Softer Away from the Caster), the banners' colour (u2, Coloured
// Shadows, from the G-buffer's colour at t3); or, in light mode (b3's y: torch and fire shadows, screen_space_lights.hpp),
// every listed light's dispatches in one (t4, t5: sss_lights'), each pixel's bit of its light cleared in u3 where it is
// shadowed (rt_local_lights.hlsli's mask).
#include "./sss.hlsli"

#define SAMPLE_COUNT        1920
#define HARD_SHADOW_SAMPLES 4
#define FADE_OUT_SAMPLES    8
#define BEND_SSS_OCCLUSION
#define BEND_SSS_LIGHT_CASTERS
#ifdef SSS_EXTENDED
#define BEND_SSS_EXTENDED
#endif
#ifdef SSS_AUTO_THICKNESS
#define BEND_SSS_AUTO_THICKNESS
#endif
#ifdef SSS_FIND
#define BEND_SSS_FIND
#endif
#include "../twshogun2completeedition/shaders/bend_sss_gpu.hlsli"

cbuffer sss_dispatch : register(b3)
{
  uint g_sss_dispatch;
#ifdef SSS_EXTENDED
  uint g_sss_light_mode;
#endif
}

Texture2D<float> t_sss_depth : register(t0);
StructuredBuffer<int4> t_sss_parameters : register(t1);
SamplerState s_sss_depth : register(s0);
RWTexture2D<unorm float> t_rw_occlusion : register(u0);

#ifdef SSS_EXTENDED
cbuffer camera_VS_PS : register(b0)
{
  float4x4 projection : packoffset(c5);
  float4x4 inv_projection : packoffset(c17);
}

Texture2D<float> t_sss_thin : register(t2);
Texture2D<float4> t_sss_colour : register(t3);
// sss_lights': [0] the dispatches' count and their groups; each dispatch's two entries: its first group, waves y and z, its
// light; its wave offset.
StructuredBuffer<int4> t_sss_light_dispatches : register(t4);
StructuredBuffer<float4> t_sss_light_coordinates : register(t5);
RWTexture2D<float> t_rw_penumbra : register(u1);
RWTexture2D<unorm float4> t_rw_tint : register(u2);
RWTexture2D<uint> t_rw_light_mask : register(u3);

static uint light_index = 0u;

// A render pixel's view position.
float3 ViewPosition(float2 pixel, float depth)
{
  const float2 uv = (floor(pixel) + 0.5f) / g_sss_render_size;
  const float4 view = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), inv_projection);
  return view.xyz / view.w;
}

void WriteExtended(DispatchParameters dp, int2 xy, float result, ExtendedResult extended)
{
  if (g_sss_light_mode != 0u)
  {
    if (result > 0.5f) InterlockedAnd(t_rw_light_mask[xy], ~(1u << light_index));
    return;
  }
  dp.OutputTexture[xy] = result;
  [branch] if (g_sss_sun_tangent > 0.f)
  {
    // The sun's disk widens the shadow by its distance from its caster: a radius of that x tan(half the disk), in render
    // pixels at the pixel's distance.
    float radius = 0.f;
    if (extended.hit && result > 0.f)
    {
      const float3 receiver = ViewPosition(float2(xy), extended.depth);
      const float caster_depth = abs(t_sss_depth.Load(int3(clamp(int2(extended.hit_xy), 0, int2(g_sss_render_size) - 1), 0)));
      const float3 caster = ViewPosition(extended.hit_xy, caster_depth);
      radius = distance(receiver, caster) * g_sss_sun_tangent * 0.5f * g_sss_render_size.y * projection._m11 / max(abs(receiver.z), 1e-3f);
    }
    t_rw_penumbra[xy] = min(radius, g_sss_soft_radius);
  }
  [branch] if (g_sss_coloured != 0.f)
  {
    // As the rays' ThinColour: the banner's colour over its brightest channel (the light through it keeps its brightness).
    float3 filter = 0.f;
    if (extended.thin)
    {
      const float3 albedo = pow(max(t_sss_colour.Load(int3(clamp(int2(extended.thin_xy), 0, int2(g_sss_render_size) - 1), 0)).rgb, 0.f), g_sss_albedo_power);
      filter = (1.f - albedo / max(max(albedo.r, max(albedo.g, albedo.b)), 1.0004024e-3f)) * extended.thin_weight;
    }
    t_rw_tint[xy] = float4(filter, 0.f);
  }
}
#endif

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
#ifdef SSS_EXTENDED
  // Auto Thickness: a run of w render pixels is w x tan(fov / 2) / render height of its distance wide; Bend's window is
  // SurfaceThickness of it, its shadow two windows deep.
  dp.AutoThicknessScale = (g_sss_auto_thickness != 0.f) ? 1.f / (projection._m11 * g_sss_render_size.y * g_sss_thickness) : 0.f;
  dp.ThinCasters = (g_sss_thin_light > 0.f);
  dp.ThinLight = g_sss_thin_light;
  dp.ThinDepth = t_sss_thin;
  dp.FindHit = (g_sss_sun_tangent > 0.f);
  dp.FindThin = (g_sss_coloured != 0.f);
  [branch] if (g_sss_light_mode != 0u)
  {
    // This group's dispatch (the one whose groups hold it) and its place in it.
    const int2 header = t_sss_light_dispatches[0].xy;
    const int group_index = group_id.x + group_id.y * 65535;
    if (group_index >= header.y) return;
    uint low = 0u;
    uint high = (uint)header.x - 1u;
    [loop] while (low < high)
    {
      const uint middle = (low + high + 1u) >> 1;
      if (t_sss_light_dispatches[1u + 2u * middle].x <= group_index) {
        low = middle;
      } else {
        high = middle - 1u;
      }
    }
    const int4 dispatch = t_sss_light_dispatches[1u + 2u * low];
    const uint local = (uint)(group_index - dispatch.x);
    const uint waves_y = (uint)dispatch.y;
    group_id = int3(local % WAVE_SIZE, (local / WAVE_SIZE) % waves_y, local / (WAVE_SIZE * waves_y));
    light_index = (uint)dispatch.w;
    dp.LightCoordinate = t_sss_light_coordinates[dispatch.w];
    dp.WaveOffset = t_sss_light_dispatches[2 + 2 * low].xy;
    dp.FarFade = 0.f;
    dp.NearLightShare = 0.f;
    dp.FindHit = false;
    dp.FindThin = false;
  }
#endif

  WriteScreenSpaceShadow(dp, group_id, thread_id, (int)g_sss_samples);
}
