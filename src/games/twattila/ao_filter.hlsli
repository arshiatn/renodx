/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_AO_FILTER_HLSLI_
#define SRC_GAMES_TWATTILA_AO_FILTER_HLSLI_

// AO filter (ao_filter.hpp): Temporal Smoothing and the Denoiser for the games whose ambient occlusion ao_pass.hpp does
// not draw (Troy, Pharaoh, Warhammer I-III: their own passes and line sweeps), on the texture their sun light draws read,
// at the display's size: as Attila's (bitmask_ssao.hlsli BitmaskResolve / BitmaskDenoise), with each pixel's position
// from the sun draw's G-buffer depth (AO_FILTER_MS: multisampled, sample 0) and camera (in Attila's layout).

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 inv_view_projection : packoffset(c21);
}

cbuffer previous_camera : register(b1)
{
  float3 previous_camera_position : packoffset(c0);
  float4x4 previous_view_projection : packoffset(c9);
}

cbuffer ao_filter_settings : register(b2)
{
  float2 g_filter_size;          // the 3D image's (DLSS: the top-left corner of the textures; else the display's)
  float2 g_filter_inverse_size;
  float g_filter_history;        // 1: last frame's history is there (Temporal Smoothing)
  float g_filter_edge_fix;       // 1: Temporal Edge Fix
  float g_filter_denoise;        // 1: the Denoiser (else the resolve's result as it is)
  float g_filter_padding;
  float2 g_filter_texture_inverse_size;  // the textures' (the display's)
  float2 g_filter_padding2;
}

SamplerState s_linear_clamp : register(s0);
Texture2D<float4> t_game_ao : register(t0);   // the game's (any size: read as its sun draws read it)
#ifdef AO_FILTER_MS
Texture2DMS<float> t_depth : register(t1);
#else
Texture2D<float> t_depth : register(t1);
#endif
Texture2D<float4> t_history : register(t2);   // the resolve: last frame's history; the denoise: this frame's
RWTexture2D<float4> u_output : register(u0);  // the resolve: this frame's history; the denoise: the sun draws' AO

float FilterDepth(int2 texel)
{
#ifdef AO_FILTER_MS
  return t_depth.Load(texel, 0);
#else
  return t_depth.Load(int3(texel, 0));
#endif
}

// Where the game's AO texture holds the pixel.
float2 FilterUv(int2 pixel)
{
  return (float2(pixel) + 0.5f) * g_filter_texture_inverse_size;
}

// The world position at a pixel of the 3D image.
float3 FilterWorld(int2 pixel)
{
  const float2 uv = (float2(pixel) + 0.5f) * g_filter_inverse_size;
  const float4 world = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), FilterDepth(pixel), 1.f), inv_view_projection);
  return world.xyz / world.w;
}

// Its distance from the camera, 0 for the sky.
float FilterDistance(float3 world)
{
  const float distance = length(world - camera_position);
  return (distance > 1e-3f && distance < 6e4f) ? distance : 0.f;
}

// Temporal Smoothing: the game's AO at the pixel (as its sun draws read it) and the last frames' at this point (moved with
// the camera, bilinear from the 4 history texels around it, each only if it held the same surface: its distance from
// the camera then within 5%), held within this frame's 3 x 3 of the same surface (mean +- 1.5 x their spread, at least
// 0.05), frames counted (newly seen spots average their first frames evenly, then 95% history); the Edge Fix as
// Attila's. Without history this frame's. Into this frame's history: the result, its distance, the frames.
void FilterResolve(int2 pixel)
{
  const int2 size = int2(g_filter_size);
  if (any(pixel >= size)) return;
  const float3 world = FilterWorld(pixel);
  const float distance = FilterDistance(world);
  const float current = t_game_ao.SampleLevel(s_linear_clamp, FilterUv(pixel), 0).x;
  if (!(distance > 0.f)) {
    u_output[pixel] = float4(current, 0.f, 1.f, 0.f);
    return;
  }
  float value = current;
  float frames = 0.f;
  const float4 clip = mul(float4(world, 1.f), previous_view_projection);
  [branch]
  if (g_filter_history != 0.f && clip.w > 0.f) {
    float3 moments = 0.f;  // count, sum, sum of squares
    [unroll]
    for (int y = -1; y <= 1; ++y) {
      [unroll]
      for (int x = -1; x <= 1; ++x) {
        const int2 neighbour = clamp(pixel + int2(x, y), 0, size - 1);
        const float other = FilterDistance(FilterWorld(neighbour));
        const float ao = t_game_ao.SampleLevel(s_linear_clamp, FilterUv(neighbour), 0).x;
        if (abs(other - distance) < 0.05f * distance) moments += float3(1.f, ao, ao * ao);
      }
    }
    const float mean = moments.y / moments.x;
    const float spread = sqrt(max(moments.z / moments.x - mean * mean, 0.f));
    const float2 coordinate = (clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f) * g_filter_size - 0.5f;
    const int2 base = int2(floor(coordinate));
    const float2 blend = coordinate - base;
    const float expected = length(world - previous_camera_position);
    const bool edge = (g_filter_edge_fix != 0.f) && (moments.x < 9.f);
    const float tolerance = edge ? 0.02f : 0.05f;
    float3 sum = 0.f;  // result, frames, weight
    float3 nearest = 0.f;
    float nearest_weight = 0.f;
    [unroll]
    for (int corner = 0; corner < 4; ++corner) {
      const int2 offset = int2(corner & 1, corner >> 1);
      const int2 texel = base + offset;
      const float3 previous = t_history.Load(int3(clamp(texel, 0, size - 1), 0)).xyz;
      const float2 weights = (offset == 1) ? blend : (1.f - blend);
      const float weight = all(texel == clamp(texel, 0, size - 1)) ? weights.x * weights.y : 0.f;
      const bool same = abs(previous.y - expected) < tolerance * expected;
      sum += float3(previous.xz, 1.f) * (same ? weight : 0.f);
      [flatten]
      if (same && weight > nearest_weight) {
        nearest = float3(previous.xz, 1.f);
        nearest_weight = weight;
      }
    }
    if (edge && nearest_weight > 0.f) sum = nearest;
    [flatten]
    if (sum.z > 0.01f) {
      float width = 1.f;
      if (moments.x >= 5.f) {
        width = max(1.5f * spread, 0.05f);
      } else if (edge) {
        width = 0.2f;
      }
      const float held = clamp(sum.x / sum.z, mean - width, mean + width);
      frames = sum.y / sum.z * saturate(2.f * sum.z);
      value = lerp(current, held, min(frames / (frames + 1.f), 0.95f));
    }
  }
  u_output[pixel] = float4(value, distance, min(frames + 1.f, 32.f), 0.f);
}

// The Denoiser (XeGTAO's 5 x 5 depth-aware filter, as Attila's): this frame's history around the pixel by binomial weights,
// only where its distance is within 3% of this one's. Into the sun draws' AO: the result where the game's has its AO,
// the game's other channels kept.
void FilterDenoise(int2 pixel)
{
  const int2 size = int2(g_filter_size);
  if (any(pixel >= size)) return;
  const float4 game = t_game_ao.SampleLevel(s_linear_clamp, FilterUv(pixel), 0);
  const float2 center = t_history.Load(int3(pixel, 0)).xy;
  float value = center.x;
  [branch]
  if (g_filter_denoise != 0.f && center.y > 0.f) {
    static const float BINOMIAL[5] = {1.f, 4.f, 6.f, 4.f, 1.f};
    float2 sum = 0.f;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
      [unroll]
      for (int x = -2; x <= 2; ++x) {
        const float2 other = t_history.Load(int3(clamp(pixel + int2(x, y), 0, size - 1), 0)).xy;
        const float alike = saturate(1.f - abs(other.y - center.y) / (0.03f * center.y));
        sum += float2(other.x, 1.f) * (BINOMIAL[x + 2] * BINOMIAL[y + 2] * ((other.y > 0.f) ? alike : 0.f));
      }
    }
    value = sum.x / sum.y;
  }
  u_output[pixel] = float4(value, game.yzw);
}

#endif  // SRC_GAMES_TWATTILA_AO_FILTER_HLSLI_
