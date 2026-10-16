/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private replacement of the game's screen-space reflection trace (ps 0xE0B7E115, half resolution), drawn with its
// bindings: more steps along a longer ray, and exact hits. The game takes 4 + 28 x gloss steps along 1.5 x the pixel's
// distance and stops at the first sample within ~0.6 steps of the depth buffer, in front or behind. Here each crossing
// behind the depth buffer is bisected to the surface and kept when it lands within the game's thickness. Output as the
// game's: the previous frame's scene at the hit, alpha its fade.
cbuffer PER_BATCH : register(b0) {
  float4 g_WorldViewPos : packoffset(c0);
  float4 g_ScreenSize : packoffset(c1);
  row_major float4x4 g_mViewProjPrev : packoffset(c2);
  float4 PS_NearFarClipDist : packoffset(c6);
  row_major float4x4 g_mViewProj : packoffset(c7);
  float4 PS_HPosScale : packoffset(c11);
}

cbuffer PER_FRAME : register(b2) {
  float4 g_PS_ProjRatio : packoffset(c223);
}

// Must match ReflectionConstants in addon.cpp.
cbuffer ReflectionConstants : register(b12) {
  float StepScale;    // steps x the game's
  float LengthScale;  // ray length x the game's
  float2 Padding;
}

SamplerState SceneDepthSampler : register(s0);
SamplerState NormalsSampler : register(s1);
SamplerState GlossSampler : register(s2);
SamplerState TraceDepthSampler : register(s3);
SamplerState PreviousSceneSampler : register(s4);
SamplerState LuminanceSampler : register(s5);
Texture2D<float4> SceneDepth : register(t0);  // linear, 0-1 of the far plane
Texture2D<float4> Normals : register(t1);
Texture2D<float4> Gloss : register(t2);
Texture2D<float4> TraceDepth : register(t3);
Texture2D<float4> PreviousScene : register(t4);
Texture2D<float4> Luminance : register(t5);  // 1x1: the scene's adapted luminance

// How far the ray at t (0-1) is behind the depth buffer (linear depth; negative: in front). The ray holds the screen
// position x w (viewport scaled), linear depth and w, all linear along it.
float BehindDepth(float4 start, float4 delta, float t, out bool on_screen) {
  const float4 position = start + delta * t;
  const float2 screen_uv = position.xy / position.w;
  on_screen = all(screen_uv >= 0) && all(screen_uv <= PS_HPosScale.xy);
  return position.z - TraceDepth.SampleLevel(TraceDepthSampler, screen_uv, 0).x;
}

float4 main(float4 position : SV_Position0, float4 uv : TEXCOORD0, float4 view_ray : TEXCOORD1) : SV_Target0 {
  const float2 gbuffer_uv = g_ScreenSize.zw + uv.xy;
  const float3 normal = normalize(Normals.Sample(NormalsSampler, gbuffer_uv).xyz * 2 - 1);
  const float depth = SceneDepth.Sample(SceneDepthSampler, uv.xy).x;
  const float3 view_direction = normalize(view_ray.xyz * depth);
  const float3 reflected = normalize(reflect(view_direction, normal));
  const float game_length = PS_NearFarClipDist.y * depth * 1.5;
  // The game's fade of rays toward the camera (with its ray length): those never reach the far plane.
  const float facing = saturate(0.5 + dot(view_direction, reflected * game_length));
  if (facing < 0.01 || depth == 1) return 0;

  const float3 origin = depth * view_ray.xyz + g_WorldViewPos.xyz;
  const float3 ray = reflected * (game_length * LengthScale);
  const float4 origin_clip = mul(float4(origin, 1), g_mViewProj);
  const float4 start = float4(PS_HPosScale.xy * (float2(0.5, -0.5) * origin_clip.xy + 0.5 * origin_clip.w), depth, origin_clip.w);
  const float4 end_clip = mul(float4(origin + ray, 1), g_mViewProj);
  const float end_depth = g_PS_ProjRatio.y / (end_clip.z / end_clip.w - g_PS_ProjRatio.x);
  const float4 end = float4(PS_HPosScale.xy * (float2(0.5, -0.5) * end_clip.xy + 0.5 * end_clip.w), end_depth, end_clip.w);
  const float4 delta = end - start;

  const float game_steps = trunc(Gloss.SampleLevel(GlossSampler, gbuffer_uv, 0).x * 28 + 4);
  // The game's hit tolerance: how thick a surface in the depth buffer is taken to be.
  const float thickness = game_length / (1.6 * game_steps) / PS_NearFarClipDist.y;
  const int steps = (int)(game_steps * StepScale);
  const float step = 1.0 / steps;
  // From the game's first sample (as far from the surface): no hits on the surface itself.
  float t = 1.0 / (game_steps * LengthScale);
  float previous_t = t;
  float previous_behind = -1;
  float hit_t = 0;
  [loop]
  for (int index = 0; index <= steps && t <= 1; ++index) {
    bool on_screen;
    const float behind = BehindDepth(start, delta, t, on_screen);
    if (!on_screen) break;
    if (behind > 0 && previous_behind <= 0) {
      // Crossed behind the depth buffer since the last sample: bisect to the crossing.
      float front = previous_t;
      float back = t;
      [unroll]
      for (int refine = 0; refine < 6; ++refine) {
        const float middle = 0.5 * (front + back);
        bool middle_on_screen;
        if (BehindDepth(start, delta, middle, middle_on_screen) > 0) {
          back = middle;
        } else {
          front = middle;
        }
      }
      // A surface, or the ray passing behind an object's edge (then it goes on).
      bool hit_on_screen;
      if (BehindDepth(start, delta, back, hit_on_screen) < thickness) {
        hit_t = back;
        break;
      }
    }
    previous_t = t;
    previous_behind = behind;
    t += step;
  }
  if (hit_t <= 0) return 0;

  // The hit in the previous frame's scene, faded toward its edges (the game's).
  const float3 previous_clip = mul(float4(origin + ray * hit_t, 1), g_mViewProjPrev).xyw;
  const float2 previous_uv = saturate(previous_clip.xy / previous_clip.z);
  const float edge = 0.07 * PS_HPosScale.x;
  const float border = min(min(previous_uv.x, previous_uv.y), 1 - max(previous_uv.x, previous_uv.y));
  const float edge_fade = (edge < border) ? 1 : sqrt(border / edge);
  const float limit = 100 * Luminance.SampleLevel(LuminanceSampler, uv.xy, 0).x;
  float3 color = min(PreviousScene.SampleLevel(PreviousSceneSampler, previous_uv * PS_HPosScale.zw, 0).xyz, limit);
  const bool3 finite = (asuint(color) & 0x7f800000) != 0x7f800000;
  color = finite ? color : 0;
  return float4(color, edge_fade * facing);
}
