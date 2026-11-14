/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing (ray_tracing.hpp WritePositions): each pixel's world position from the scene's depth for the helper's trace,
// as rt_core's rt_positions writes them, but unprojected in doubles. Near 1 m and far 25 km put the depth of everything
// past a few hundred metres within 2e-4 of its value at infinity: in floats the inverse's w row cancels to metres of noise
// at a kilometre. The jittered view-projection's inverse (dlaa.hpp, made in doubles; rows act on row vectors: world =
// (x, y, z, 1) x it, then / w); 1 / w by Newton from the float's (no double division, which needs extended doubles).
Texture2D<float> depth : register(t0);
RWTexture2D<float4> positions : register(u0);
RWTexture2D<float4> sun : register(u1);

cbuffer rt_positions_double : register(b0)
{
  uint4 inverse_bits[8];  // the inverse's 16 doubles, row by row, each its low and high halves
  float4 camera;          // xyz the camera's position
  uint4 size;             // xy the render size
}

double Inverse(uint index) {
  const uint4 pair = inverse_bits[index / 2u];
  return ((index & 1u) == 0u) ? asdouble(pair.x, pair.y) : asdouble(pair.z, pair.w);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (all(id.xy == 0u)) sun[uint2(1, 0)] = float4(camera.xyz, 1.f);
  if (any(id.xy >= size.xy)) return;
  const float z = depth[id.xy];
  if (z <= 0.f || z >= 1.f) {
    positions[id.xy] = 0.f;
    return;
  }
  const float2 ndc = (float2(id.xy) + 0.5f) / float2(size.xy) * float2(2.f, -2.f) + float2(-1.f, 1.f);
  const double clip[4] = {(double)ndc.x, (double)ndc.y, (double)z, 1.0L};
  double world[4] = {0.0L, 0.0L, 0.0L, 0.0L};
  [unroll]
  for (uint row = 0u; row < 4u; ++row) {
    [unroll]
    for (uint column = 0u; column < 4u; ++column) world[column] += clip[row] * Inverse((row * 4u) + column);
  }
  double reciprocal = (double)(1.f / (float)world[3]);
  reciprocal = reciprocal * (2.0L - (world[3] * reciprocal));
  reciprocal = reciprocal * (2.0L - (world[3] * reciprocal));
  const float3 offset = float3((float)((world[0] * reciprocal) - (double)camera.x), (float)((world[1] * reciprocal) - (double)camera.y),
                               (float)((world[2] * reciprocal) - (double)camera.z));
  positions[id.xy] = float4(camera.xyz + offset, length(offset));
}
