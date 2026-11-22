// Water Clarity and Water Caustics (Experimental, the WaterClarity / WaterCaustics rows; Attila's water_effects.hpp) for
// the battles' water planes (waterplane00 high / medium / ultra, waterplane01 low): the water's depth (t_terrain_refraction's w)
// before it takes the deep water colour divided by the clarity; the refracted bed x (1 + Attila's caustics tile at the
// surface's world position x the strength), worked out here (no tile texture). Included after camera_VS_PS (time_in_sec) and
// shared.h (SI).

// Attila's water_caustics.cs at `uv` (the tile's 0..1, repeated every WATER_CAUSTICS_TILE metres): bright borders between
// drifting cells, `cells` across, their points circling at `speed` radians a second.
float CausticsBorders(float2 uv, int cells, float speed)
{
  const float tau = 6.2831853f;
  const float2 bend = float2(sin(tau * (3.f * uv.y + uv.x) + time_in_sec * 0.7f * speed),
                             sin(tau * (2.f * uv.x - 3.f * uv.y) - time_in_sec * 0.9f * speed));
  const float2 place = uv * cells + 0.3f * bend;
  const int2 base = int2(floor(place));
  float nearest = 9.f;
  float second = 9.f;
  [unroll]
  for (int y = -1; y <= 1; ++y)
  {
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
      const int2 cell = base + int2(x, y);
      const int2 wrapped = (cell % cells + cells) % cells;
      uint n = uint(wrapped.x) * 1973u + uint(wrapped.y) * 9277u + 26699u;
      n = (n << 13u) ^ n;
      n = n * (n * n * 15731u + 789221u) + 1376312589u;
      const float2 random = float2(n & 0xFFFFu, (n >> 16) & 0xFFFFu) / 65536.f;
      const float angle = tau * random.x + time_in_sec * speed * (0.75f + 0.5f * random.y);
      const float distance_to = distance(place, float2(cell) + 0.5f + 0.35f * float2(cos(angle), sin(angle)));
      second = (distance_to < nearest) ? nearest : min(second, distance_to);
      nearest = min(nearest, distance_to);
    }
  }
  return pow(saturate(1.f - (second - nearest) * 2.5f), 6.f);
}

// The refracted bed `bed` under the water's surface at `world`, with the caustics (strength 0: the game's).
float3 WaterCaustics(float3 bed, float3 world)
{
  if (SI.water_caustics <= 0.f) return bed;
  const float2 tile = frac(world.xz / 4.f);
  return bed * (1.f + saturate(CausticsBorders(tile, 7, 0.6f) + 0.5f * CausticsBorders(tile, 11, -0.8f)) * SI.water_caustics);
}

// What the water's depth is divided by before it takes the deep water colour (0: the game's 1).
float WaterClarity()
{
  return (SI.water_clarity > 0.f) ? SI.water_clarity : 1.f;
}
