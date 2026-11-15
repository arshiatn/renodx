// Ray-traced radiance cache, filled (rt_cache.hlsli): one pixel of every 2x2 (another each frame) adds last frame's
// scene colour to its world cell, seen from last frame's camera.
#include "./rt_cache.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  previous_positions.GetDimensions(size.x, size.y);
  const uint2 pixel = id.xy * 2u + uint2(frame & 1u, (frame >> 1u) & 1u);
  if (any(pixel >= size)) return;
  const float4 position = previous_positions[pixel];
  if (position.w <= 0.f) return;
  CacheAdd(position.xyz, previous_sun[uint2(1, 0)].xyz, max(previous_color[pixel].rgb, 0.f));
}
