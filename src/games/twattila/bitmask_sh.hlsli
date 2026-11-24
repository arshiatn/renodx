/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_BITMASK_SH_HLSLI_
#define SRC_GAMES_TWATTILA_BITMASK_SH_HLSLI_

// Directional light from the visibility bitmask (Directional Light Bounce, Experimental; light_bounce.hpp): a texel's
// light as its exact value Y0 at the trace normal n0 (the bounce itself) and how it changes with the normal, so each
// pixel of a lower-resolution trace gets its own normal map's shading back (shg_ design Rev 1.1, 3.1):
//   Y(n) = Y0 - w + g.n + A (3 (n.a)^2 - 1),  a = g / |g|,  w = g.n0 + A (3 (n0.a)^2 - 1)  (so Y(n0) = Y0),
// stored as (g, w) and A beside the bounce: linear, so every filter blends them with the bounce's weights. From the
// trace's L1 moments (Ramamoorthi and Hanrahan 2001), integrated exactly over each sample's arc of its slice (GTAO's
// slice form), ZH3's hallucinated zonal L2 along the light's axis (Roughton, Sloan, Silvennoinen, Iwanicki, Shirley,
// "ZH3: Quadratic Zonal Harmonics", I3D 2024; its curve refitted over the hemisphere) and the least-squares step of
// Roughton, Sloan, Silvennoinen and Shirley's "Hemispherical Lighting Insights" (ATVI-TR-24-01, 2024) for its residual at
// n0. Luminance only (the colour stays the bounce's): NRD's YCoCg Y. Units F = E / pi (what the bounce holds). No
// textures and no constant buffers: FXC SM5 and DXC SM6.5 (the helper) compile it.

static const float3 BITMASK_SH_LUMINANCE = float3(0.25f, 0.5f, 0.25f);
static const float BITMASK_SH_HALF_PI = 1.57079633f;
// ZH3's zonal share f02 / f0 = b0 + b1 p + b2 p^2 over p = |f1| / f0, fitted on hemisphere light fields with its zero at
// a uniform sky's p = sqrt(3) / 2, where L1 is exact (shg_ S0 12.1: exact there, held-out fields .054 at 30 degrees).
static const float3 BITMASK_SH_ZONAL_CURVE = float3(-1.052823f, 0.518202f, 0.805396f);

// The stored form: (g, w) and the zonal amplitude A, each as fp16 holds it.
struct BitmaskShLight {
  float3 g;
  float w;
  float zonal;
};

// `normal` turned toward `view_direction` (the trace's receiver: two-sided leaves and banners hold either side).
float3 BitmaskShFacing(float3 normal, float3 view_direction) {
  return (dot(normal, view_direction) < 0.f) ? -normal : normal;
}

// The trace normal of a block of pixels: the mean of the normals on the top-left pixel's surface (within about 20
// degrees of its plane, the apply's test), each turned toward the view first; the top-left's where they cancel (front
// and back faces in one block: the mean below half a unit). Zero-initialized, then each pixel (the top-left first), then
// BitmaskShBlockNormal (shg_ S0 11.3: two-sided blocks .187 -> .038, blocks across an edge .278 -> .026).
struct BitmaskShBlock {
  float3 origin;
  float3 top_left;
  float3 sum;
  float count;
};

void BitmaskShBlockAdd(inout BitmaskShBlock block, float3 position, float3 normal) {
  const float3 facing = BitmaskShFacing(normal, -position / length(position));
  if (block.count == 0.f) {
    block.origin = position;
    block.top_left = facing;
  }
  const float3 offset = position - block.origin;
  if (abs(dot(offset, block.top_left)) <= 0.35f * length(offset) && dot(facing, block.top_left) >= 0.5f) {
    block.sum += facing;
    block.count += 1.f;
  }
}

float3 BitmaskShBlockNormal(BitmaskShBlock block) {
  const float sum_length = length(block.sum);
  return (sum_length >= 0.5f * block.count) ? (block.sum / sum_length) : block.top_left;
}

// One sample's arc on `side` (-1 / 1) of a slice: from its front to its back (unit vectors from the receiver), clipped
// to the hemisphere around the projected normal at `normal_angle` from `view_direction`. Angles t from the view vector,
// positive toward the slice direction d; w(t) = cos t v + sin t d. Returns the integrals over the arc of
// (cos t |sin t|, sin t |sin t|, |sin t|): with the arc's luminance and 1 / (2 W) of its slice (W: the slice's
// cosine-weighted width, its pdf x pi) the first two are its c1 along v and d, half the third its c0.
float3 BitmaskShArc(float3 front, float3 back, float3 view_direction, float side, float normal_angle) {
  float2 angles = side * acos(clamp(float2(dot(front, view_direction), dot(back, view_direction)), -1.f, 1.f));
  angles = clamp(angles, normal_angle - BITMASK_SH_HALF_PI, normal_angle + BITMASK_SH_HALF_PI);
  angles = float2(min(angles.x, angles.y), max(angles.x, angles.y));
  float2 sines;
  float2 cosines;
  sincos(angles, sines, cosines);
  // The antiderivatives: sign(t) (sin^2 t / 2, t / 2 - sin t cos t / 2, 1 - cos t).
  const float2 signs = sign(angles);
  const float3 low = signs.x * float3(0.5f * sines.x * sines.x, 0.5f * (angles.x - sines.x * cosines.x), 1.f - cosines.x);
  const float3 high = signs.y * float3(0.5f * sines.y * sines.y, 0.5f * (angles.y - sines.y * cosines.y), 1.f - cosines.y);
  return high - low;
}

// The stored form pinned at `normal` (w rewritten there: Y(normal) = Y0 whatever was blended in), g and A as fp16 holds
// them (rvs_ N5).
BitmaskShLight BitmaskShPinned(float3 g, float zonal, float3 normal) {
  BitmaskShLight light;
  light.g = f16tof32(f32tof16(g));
  light.zonal = f16tof32(f32tof16(zonal));
  const float g_squared = dot(light.g, light.g);
  const float axis_cosine = (g_squared > 0.f) ? dot(normal, light.g) * rsqrt(g_squared) : 0.f;
  light.w = dot(light.g, normal) + light.zonal * (3.f * axis_cosine * axis_cosine - 1.f);
  return light;
}

// A texel's stored form from its exact light `y0` at `normal` and its moments c0, c1 (the trace's): ZH3's zonal
// amplitude from c0 and |c1|, then with `memo` the memo's step g = c1 + 1.5 delta n0 for ZH3's residual delta at n0
// (else ZH3 alone, g = c1: S2's A/B).
BitmaskShLight BitmaskShFromMoments(float y0, float c0, float3 c1, float3 normal, bool memo) {
  const float c1_length = length(c1);
  // p = |f1| / f0 = sqrt(3) / 2 |c1| / c0; A = Y20 f02 / 4 = sqrt(5) / 8 c0 (b0 + b1 p + b2 p^2).
  const float p = 0.866025404f * c1_length / max(c0, 1e-12f);
  const float zonal = 0.279508497f * c0 * (BITMASK_SH_ZONAL_CURVE.x + p * (BITMASK_SH_ZONAL_CURVE.y + p * BITMASK_SH_ZONAL_CURVE.z));
  float3 g = c1;
  [branch]
  if (memo) {
    const float axis_cosine = (c1_length > 0.f) ? dot(normal, c1) / c1_length : 0.f;
    g += 1.5f * (y0 - (c0 + dot(c1, normal) + zonal * (3.f * axis_cosine * axis_cosine - 1.f))) * normal;
  }
  return BitmaskShPinned(g, zonal, normal);
}

// Y at `normal` from a texel's (or a blend of texels') light `y0` and stored (g, w) and zonal amplitude.
float BitmaskShEvaluate(float y0, float4 g_w, float zonal, float3 normal) {
  const float g_squared = dot(g_w.xyz, g_w.xyz);
  const float axis_cosine = (g_squared > 0.f) ? dot(normal, g_w.xyz) * rsqrt(g_squared) : 0.f;
  return y0 - g_w.w + dot(g_w.xyz, normal) + zonal * (3.f * axis_cosine * axis_cosine - 1.f);
}

#endif  // SRC_GAMES_TWATTILA_BITMASK_SH_HLSLI_
