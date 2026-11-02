#ifndef SRC_TWATTILA_SKY_SMOOTH_HLSLI_
#define SRC_TWATTILA_SKY_SMOOTH_HLSLI_

#include "./shared.h"

// The sky cube is made at run time (the .environment's SKYGEN), low resolution and 8-bit: one code between texels shows
// as blocks in a flat sky, and its horizon row, which all below the horizon reads (y clamped at 0, as the game's), as
// vertical lines through thick fog. Fog & Sky Smoothing (SI.sky_smooth: Low, Medium, High): 4, 8 or 12 taps to each side, as
// wide across as they are many (4, 9, 13 degrees each way at Strength 1, each tap at its own azimuth) and narrow up and down
// (the sky's gradient near the horizon stays). Its mips aren't used: they may not exist.
float3 SkySmooth(TextureCube<float4> sky, SamplerState sky_sampler, float3 direction)
{
  static const float2 TAPS[4] = {
    float2(0.016f, 0.011f), float2(0.036f, -0.015f), float2(0.056f, 0.006f), float2(0.076f, -0.002f)
  };
  const int taps = 4 * (int)SI.sky_smooth;
  const float3 view_direction = normalize(direction);
  const float3 up = (abs(view_direction.y) < 0.99f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f);
  const float3 tangent = normalize(cross(view_direction, up));
  const float3 bitangent = cross(view_direction, tangent);
  float3 sum = sky.SampleLevel(sky_sampler, view_direction, 0.f).rgb;
  [loop]
  for (int index = 0; index < taps; ++index)
  {
    // Each further four 0.08 wider.
    const float across = (TAPS[index % 4].x + 0.08f * float(index / 4)) * SI.sky_smooth_strength;
    const float3 offset = tangent * across + bitangent * TAPS[index % 4].y * SI.sky_smooth_strength;
    const float3 ahead = view_direction + offset;
    const float3 behind = view_direction - offset;
    sum += sky.SampleLevel(sky_sampler, float3(ahead.x, max(0.f, ahead.y), ahead.z), 0.f).rgb;
    sum += sky.SampleLevel(sky_sampler, float3(behind.x, max(0.f, behind.y), behind.z), 0.f).rgb;
  }
  return sum / float(1 + 2 * taps);
}

#endif  // SRC_TWATTILA_SKY_SMOOTH_HLSLI_
