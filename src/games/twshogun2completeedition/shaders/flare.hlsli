// lenseflare.hlsl ps30_flare_0-3: the sun's flare sprites, a flare texture (turned for 0, 2 and 3) x the vertex colour.
// HDR: x Lens Flares.
#include "../shared.h"

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
}

// The game's rotate_vector names sincos' outputs the wrong way round: a turn of 90 degrees minus the angle, a fixed
// quarter turn at 0 degrees a second. Kept.
float4 Flare(Texture2D<float4> flare, SamplerState flare_sampler, float4 colour, float2 tex, bool rotate, float angular_velocity)
{
  float2 frame_tex = tex + half_texel_offset;
  if (rotate) {
    float2 direction = 2.f * (frame_tex - 0.5f);
    float2 sin_cos;
    sincos(radians(angular_velocity) * time, sin_cos.x, sin_cos.y);
    direction = float2(direction.x * sin_cos.x - direction.y * sin_cos.y, direction.x * sin_cos.y + direction.y * sin_cos.x);
    frame_tex = direction / 2.f + 0.5f;
  }
  float4 result = colour * flare.Sample(flare_sampler, frame_tex);
  result.rgb *= (HDR >= 1.f) ? SI.lens_flare : 1.f;
  return result;
}
