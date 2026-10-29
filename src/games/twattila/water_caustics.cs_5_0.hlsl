/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's Water Caustics (addon.cpp): the light rippling on river and lake beds, a tile of the world (WATER_CAUSTICS_TILE
// metres a side, its texture repeated) the patched water shaders read at t51 by their world position (the refracted
// bed's colour x (1 + this)). Bright borders between drifting cells (where a point's nearest and second-nearest moving
// feature points are about as far: light focused by the waves above), two sizes over each other; the cells wrap at the
// tile's edges (it repeats seamlessly). Times the strength.
RWTexture2D<float> caustics : register(u0);

cbuffer water_caustics : register(b0)
{
  float time;      // seconds
  float strength;  // Water Caustics
  float2 padding;
}

// A cell's two random numbers in [0, 1).
float2 CellRandom(int2 cell)
{
  uint n = uint(cell.x) * 1973u + uint(cell.y) * 9277u + 26699u;
  n = (n << 13u) ^ n;
  n = n * (n * n * 15731u + 789221u) + 1376312589u;
  return float2(n & 0xFFFFu, (n >> 16) & 0xFFFFu) / 65536.f;
}

// The borders of `cells` x `cells` cells across the tile, their points circling at `speed` radians a second; the tile
// bent by waves of whole cycles across it (curved borders, still repeating), up to 0.3 of a cell.
float Borders(float2 uv, int cells, float speed)
{
  const float tau = 6.2831853f;
  const float2 bend = float2(sin(tau * (3.f * uv.y + uv.x) + time * 0.7f * speed), sin(tau * (2.f * uv.x - 3.f * uv.y) - time * 0.9f * speed));
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
      const float2 random = CellRandom((cell % cells + cells) % cells);
      const float angle = 6.2831853f * random.x + time * speed * (0.75f + 0.5f * random.y);
      const float distance_to = distance(place, float2(cell) + 0.5f + 0.35f * float2(cos(angle), sin(angle)));
      second = (distance_to < nearest) ? nearest : min(second, distance_to);
      nearest = min(nearest, distance_to);
    }
  }
  return pow(saturate(1.f - (second - nearest) * 2.5f), 6.f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  uint2 size;
  caustics.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float2 uv = (float2(id.xy) + 0.5f) / float2(size);
  caustics[id.xy] = saturate(Borders(uv, 7, 0.6f) + 0.5f * Borders(uv, 11, -0.8f)) * strength;
}
