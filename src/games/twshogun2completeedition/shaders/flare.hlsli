// lenseflare.hlsl ps30_flare_0-3: the sun's flare sprites, a flare texture (turning with time for 0 and 2) x the vertex
// colour. HDR: x Lens Flares.
#include "../shared.h"

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
}

float4 Flare(Texture2D<float4> flare, SamplerState flare_sampler, float4 colour, float2 tex, float angular_velocity)
{
  float2 frame_tex = tex + half_texel_offset;
  [flatten]
  if (angular_velocity != 0.f) {
    float2 direction = 2.f * (frame_tex - 0.5f);
    float2 cos_sin;
    sincos(radians(angular_velocity) * time, cos_sin.y, cos_sin.x);
    direction = float2(direction.x * cos_sin.x - direction.y * cos_sin.y, direction.x * cos_sin.y + direction.y * cos_sin.x);
    frame_tex = direction / 2.f + 0.5f;
  }
  float4 result = colour * flare.Sample(flare_sampler, frame_tex);
  result.rgb *= (HDR >= 1.f) ? SI.lens_flare : 1.f;
  return result;
}
