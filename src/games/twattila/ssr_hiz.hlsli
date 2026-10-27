/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_SSR_HIZ_HLSLI_
#define SRC_GAMES_TWATTILA_SSR_HIZ_HLSLI_

// environment_specular_light (ssr00 / ssr01): the screen-space reflections' ray march as a hierarchical one
// (ssr_march.hlsli): exact hits, no gaps between steps, far fewer steps than the game's linear march, and no length
// limit. One ray, the game's own direction (its roughness jitter included). Misses keep the game's environment cube.
// Include after the shader's declarations (camera_VS_PS, gbuffer_channel_1_sampler, s_light_accumulation_buffer).
#ifndef SI
#include "./shared.h"
#endif

// A view-space position's screen position: uv and depth (0 near, 1 far).
float3 SsrProject(float3 view_position)
{
  const float4 clip = mul(float4(view_position, 1.f), projection);
  return float3(clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f, clip.z / clip.w);
}

float3 FFX_SSSR_LoadWorldSpaceNormal(int2 pixel)
{
  return normalize(gbuffer_channel_1_sampler.Load(int3(pixel, 0)).xyz * 2.f - 1.f);
}

float3 FFX_SSSR_ScreenSpaceToViewSpace(float3 screen_position)
{
  const float4 view = mul(float4(screen_position.xy * float2(2.f, -2.f) + float2(-1.f, 1.f), screen_position.z, 1.f),
                          inv_projection);
  return view.xyz / view.w;
}

// The view's pixels (the render size with DLSS below 100%): the march's screen.
#define SSR_VIEW_SIZE g_viewport_dimensions.xy
#include "./ssr_march.hlsli"

// The reflection at `pixel` of a ray from `start_view` along `direction_view` (view space; `direction_world` the same
// in world space): what it hits on screen, faded into `environment` by the confidence, at least by `environment_floor`
// (the game's depth falloff). `hit_info` for the resolve (ssr_resolve.cs): the reflected distance (view space) and the
// confidence; y -1: not traced this frame (Reflection Rays Half / Quarter: the pixels take turns, the resolve fills the
// rest).
float3 SsrHierarchical(float2 pixel, float3 start_view, float3 direction_view, float3 direction_world, float3 environment,
                       float environment_floor, out float2 hit_info)
{
  hit_info = float2(0.f, 0.f);
  const uint2 position = uint2(pixel);
  const uint frame = (uint)SI.ssr_frame;
  [branch]
  if ((SI.ssr_rays == 1.f && ((position.x + position.y + frame) & 1u) != 0u)
      || (SI.ssr_rays == 2.f && ((position.x & 1u) + 2u * (position.y & 1u)) != (frame & 3u)))
  {
    hit_info = float2(0.f, -1.f);
    return environment;
  }
  // Its end in front of the near plane: the screen-space line is then the projection of the straight one.
  float ray_length = 10000.f;
  if (direction_view.z < 0.f)
  {
    ray_length = min(ray_length, (start_view.z - camera_near_far.x * 1.01f) / -direction_view.z);
  }
  const float3 origin = SsrProject(start_view);
  // Reflection Quality: x the most steps.
  float3 hit;
  const float confidence = SsrTrace(origin, SsrProject(start_view + direction_view * ray_length) - origin, direction_world,
                                    (uint)clamp(64.f * SI.ssr_quality, 16.f, 1024.f), 0.f, hit);
  [branch]
  if (confidence <= 0.f) return environment;
  hit_info = float2(length(FFX_SSSR_ScreenSpaceToViewSpace(hit) - start_view), confidence);
  // The hit's uv is the view's: the light buffer's (screen-size) uv is that x the view's share of it.
  const float3 reflected =
      s_light_accumulation_buffer.SampleLevel(s_light_accumulation_buffer_s, hit.xy * g_viewport_dimensions.xy * g_screen_size.zw, 0).xyz;
  return lerp(reflected, environment, max(1.f - confidence, environment_floor));
}

#endif  // SRC_GAMES_TWATTILA_SSR_HIZ_HLSLI_
