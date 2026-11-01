#ifndef SRC_TWATTILA_SKY_SMOOTH_HLSLI_
#define SRC_TWATTILA_SKY_SMOOTH_HLSLI_

#include "./shared.h"

// The sky cube is made at run time (the .environment's SKYGEN), low resolution and 8-bit: one code between texels shows
// as blocks in a flat sky, and its horizon row, which all below the horizon reads (y clamped at 0, as the game's), as
// vertical lines through thick fog. Fog & Sky Smoothing (SI.sky_smooth): 9 taps, wide across (4 degrees to each side,
// each tap at its own azimuth) and narrow up and down (the sky's gradient near the horizon stays). Its mips aren't used:
// they may not exist.
float3 SkySmooth(TextureCube<float4> sky, SamplerState sky_sampler, float3 direction)
{
  static const float2 TAPS[4] = {
    float2(0.016f, 0.011f), float2(0.036f, -0.015f), float2(0.056f, 0.006f), float2(0.076f, -0.002f)
  };
  const float3 view_direction = normalize(direction);
  const float3 up = (abs(view_direction.y) < 0.99f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f);
  const float3 tangent = normalize(cross(view_direction, up));
  const float3 bitangent = cross(view_direction, tangent);
  float3 sum = sky.SampleLevel(sky_sampler, view_direction, 0.f).rgb;
  [unroll]
  for (int index = 0; index < 4; ++index)
  {
    const float3 offset = tangent * TAPS[index].x + bitangent * TAPS[index].y;
    const float3 ahead = view_direction + offset;
    const float3 behind = view_direction - offset;
    sum += sky.SampleLevel(sky_sampler, float3(ahead.x, max(0.f, ahead.y), ahead.z), 0.f).rgb;
    sum += sky.SampleLevel(sky_sampler, float3(behind.x, max(0.f, behind.y), behind.z), 0.f).rgb;
  }
  return sum / 9.f;
}

#endif  // SRC_TWATTILA_SKY_SMOOTH_HLSLI_
