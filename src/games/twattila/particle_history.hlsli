/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smooth Half-Size Particles (Experimental), the resolve (particle_history.hpp): this frame's half-size particle layer (t50;
// with PARTICLE_OPTICAL_DEPTH its optical depth at t51, else its alpha is the share of the scene it leaves) and the last
// frames' at the same place (t53-t55, moved with the camera by the depth the layer was drawn against, t52; read through a
// Catmull-Rom filter, so panning doesn't blur it). The history is held within this frame's 3 x 3 (their least and most: what
// left goes) and kept by how close this frame's 3 x 3 mean is to the last frame's there (a spark flipping to the next texel
// keeps its mean and is averaged, a new or moved spark changes it and comes through whole), frames counted as the Light
// Bounce's resolve does (a newly seen spot averages its first frames evenly). Out: the layer (SV_Target0), its optical depth
// and frames (SV_Target1), the 3 x 3 mean's luminance and coverage (SV_Target2). Game-neutral: the includer's
// light_bounce.hlsli first (its camera at b11, the last frame's at b10).
#include "./particle_layer.hlsli"

Texture2D<float4> t_layer : register(t50);
Texture2D<float> t_layer_optical_depth : register(t51);
Texture2D<float> t_layer_depth : register(t52);
Texture2D<float4> t_history : register(t53);
Texture2D<float4> t_history_extra : register(t54);  // x the optical depth, y the frames
Texture2D<float2> t_history_means : register(t55);  // the last frame's 3 x 3 mean: x luminance, y coverage
SamplerState s_history : register(s12);              // linear, clamped

// How much of the scene behind the layer is covered (0: none).
float ParticleCoverage(float4 layer, float optical_depth) {
#ifdef PARTICLE_OPTICAL_DEPTH
  return 1.f - exp2(-1.442695f * optical_depth);
#else
  return 1.f - layer.a;
#endif
}

// `history` at `coordinate` (in texels, a texel's centre at a whole number + 0.5) through a Catmull-Rom filter in 5 bilinear
// reads (Jimenez, "Filmic SMAA", 2016: the 4 corner taps left out).
float4 ParticleCatmullRom(Texture2D<float4> history, float2 coordinate, float2 size) {
  const float2 centre = floor(coordinate - 0.5f) + 0.5f;
  const float2 f = coordinate - centre;
  const float2 w0 = f * (-0.5f + f * (1.f - 0.5f * f));
  const float2 w1 = 1.f + f * f * (-2.5f + 1.5f * f);
  const float2 w2 = f * (0.5f + f * (2.f - 1.5f * f));
  const float2 w3 = f * f * (-0.5f + 0.5f * f);
  const float2 w12 = w1 + w2;
  const float2 at0 = (centre - 1.f) / size;
  const float2 at3 = (centre + 2.f) / size;
  const float2 at12 = (centre + w2 / w12) / size;
  const float4 sum = history.SampleLevel(s_history, float2(at12.x, at0.y), 0) * (w12.x * w0.y)
                     + history.SampleLevel(s_history, float2(at0.x, at12.y), 0) * (w0.x * w12.y)
                     + history.SampleLevel(s_history, at12, 0) * (w12.x * w12.y)
                     + history.SampleLevel(s_history, float2(at3.x, at12.y), 0) * (w3.x * w12.y)
                     + history.SampleLevel(s_history, float2(at12.x, at3.y), 0) * (w12.x * w3.y);
  return sum / (w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y);
}

// How far apart two values of a frame's look are (0: the same, 1: one of them is none), at least `floor` apart to count.
float ParticleChange(float a, float b, float floor) {
  return abs(a - b) / max(max(a, b), floor);
}

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0, out float2 o1 : SV_Target1, out float2 o2 : SV_Target2) {
  const int2 texel = int2(v0.xy);
  const int2 last = int2(PL.region_width, PL.region_height) - 1;
  const float4 layer = t_layer.Load(int3(texel, 0));
#ifdef PARTICLE_OPTICAL_DEPTH
  const float optical_depth = t_layer_optical_depth.Load(int3(texel, 0));
#else
  const float optical_depth = 0.f;
#endif
  float4 lowest = layer;
  float4 highest = layer;
  float4 sum = 0.f;
  float optical_lowest = optical_depth;
  float optical_highest = optical_depth;
  float optical_sum = 0.f;
  [unroll]
  for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
      const int3 at = int3(clamp(texel + int2(x, y), 0, last), 0);
      const float4 neighbour = t_layer.Load(at);
      lowest = min(lowest, neighbour);
      highest = max(highest, neighbour);
      sum += neighbour;
#ifdef PARTICLE_OPTICAL_DEPTH
      const float optical_neighbour = t_layer_optical_depth.Load(at);
      optical_lowest = min(optical_lowest, optical_neighbour);
      optical_highest = max(optical_highest, optical_neighbour);
      optical_sum += optical_neighbour;
#endif
    }
  }
  const float3 LUMINANCE = float3(0.2126f, 0.7152f, 0.0722f);
  const float4 mean = sum / 9.f;
  const float2 means = float2(dot(mean.rgb, LUMINANCE), ParticleCoverage(mean, optical_sum / 9.f));
  o0 = layer;
  o1 = float2(optical_depth, 1.f);
  o2 = means;
  if (!(PL.history_most > 0.f)) return;

  // Where it was: its first render pixel at the depth it was drawn against, moved with the camera; in layer texels (that
  // pixel's centre: half a pixel in).
  const float scale = (float)ParticleLayerScale();
  const float3 previous = BouncePrevious(BounceViewPositionAt(texel * ParticleLayerScale(), t_layer_depth.Load(int3(texel, 0))));
  const float2 coordinate = previous.xy / scale + (0.5f - (0.5f / scale));
  const bool inside = all(coordinate >= 0.f) && all(coordinate <= float2(PL.region_width, PL.region_height));
  // A camera cut (or the camera moved farther than a frame's flight): nothing of before.
  const bool cut = distance(camera_position, previous_camera_position) > PL.camera_cut;
  if (!inside || cut) return;

  float2 size;
  t_history.GetDimensions(size.x, size.y);
  const float4 history = clamp(ParticleCatmullRom(t_history, coordinate, size), lowest, highest);
  const float history_optical_depth = clamp(ParticleCatmullRom(t_history_extra, coordinate, size).x, optical_lowest, optical_highest);
  const float frames = t_history_extra.Load(int3(clamp(int2(coordinate), 0, last), 0)).y;
  const float2 previous_means = t_history_means.SampleLevel(s_history, coordinate / size, 0);
  const float change = max(ParticleChange(means.x, previous_means.x, 1e-4f), ParticleChange(means.y, previous_means.y, 1e-3f));
  const float kept = min(frames / (frames + 1.f), PL.history_most) * saturate(1.f - 2.f * change);
  o0 = lerp(layer, history, kept);
  o1 = float2(lerp(optical_depth, history_optical_depth, kept), (change > 0.5f) ? 1.f : min(frames + 1.f, 16.f));
}
