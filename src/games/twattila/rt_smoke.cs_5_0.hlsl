/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila ray tracing (rt_core.hpp, Smoke Shadows): the lit particles streamed out since the last trace (the particle
// vertex shader's outputs: centre, colour and opacity, size) as quads facing the sun after the frame's captures, two
// triangles (six texels) a particle: a smoke capture (RT_SMOKE) whose particles dim the helper's sun rays
// (dlaa_helper/rt_scene.hlsli). A corner's w: its UV (+-1 at the quad's corners) times 1 + the particle's opacity, as two
// halves (the helper finds both); a particle without opacity, size or a finite centre goes empty (a NaN x, which DXR
// skips).
Buffer<float4> particles : register(t0);  // three a particle: centre (w its age), colour (w its opacity), size (xy)
Texture2D<float4> sun : register(t1);     // texel 0: toward the sun (w 0: none yet)
RWTexture2D<float4> vertices : register(u0);

cbuffer rt_smoke : register(b0)
{
  uint4 rt_smoke;  // x the first vertex, y the particles, z the texture's width
}

static const float2 CORNERS[6] = {float2(-1.f, -1.f), float2(1.f, -1.f), float2(1.f, 1.f),
                                  float2(-1.f, -1.f), float2(1.f, 1.f), float2(-1.f, 1.f)};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (id.x >= rt_smoke.y) return;
  const float3 centre = particles[id.x * 3u].xyz;
  const float opacity = saturate(particles[id.x * 3u + 1u].w);
  const float2 size = particles[id.x * 3u + 2u].xy;  // the particle's width and height
  const float radius = 0.5f * max(size.x, size.y);
  const float4 toward_sun = sun.Load(int3(0, 0, 0));
  // The quad's plane faces the sun: two directions across it.
  const float3 normal = normalize(toward_sun.xyz);
  const float3 across = normalize(cross((abs(normal.y) < 0.99f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f), normal));
  const float3 up = cross(normal, across);
  const bool empty = toward_sun.w == 0.f || opacity < 1.f / 255.f || !(radius > 0.f) || !all(isfinite(centre)) || !all(isfinite(normal));
  const float scale = 1.f + opacity;
  [unroll]
  for (uint corner = 0u; corner < 6u; ++corner) {
    const float2 at = CORNERS[corner];
    const uint uv = f32tof16(at.x * scale) | (f32tof16(at.y * scale) << 16);
    const uint texel = rt_smoke.x + id.x * 6u + corner;
    vertices[uint2(texel % rt_smoke.z, texel / rt_smoke.z)] =
        float4(empty ? asfloat(0x7FC00000u).xxx : centre + ((across * at.x) + (up * at.y)) * radius, asfloat(uv));
  }
}
