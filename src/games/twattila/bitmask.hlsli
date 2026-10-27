/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The visibility bitmask (Therrien, Levesque and Gilet 2023, "Screen Space Indirect Lighting with Visibility Bitmask"),
// shared by the ambient occlusion and the Light Bounce: a slice's hemisphere around the projected normal as 32 sectors;
// every depth sample, a surface of constant thickness, covers the sectors between its front and its back, so light
// passes behind thin objects. As GT-VBAO and GT-VBGI (Mirko Salm, 2024, "Ground Truth Visibility Bitmask Ambient
// Occlusion", Shadertoy XXGSDd, and its extension to light, lfdBWn; CC0, ported here): the slices drawn around the view
// vector in proportion to the projected normal (BitmaskSliceAt), the sectors equal in cosine-weighted share of the
// slice (its angles through their cumulative distribution, the pole at the view vector included), the samples taken as
// points (the slice's sector edges jittered as a whole) and each sample's thickness along its own camera ray: a slice's
// open share is then its cosine-weighted visibility as a ray tracer's, and the light a sample adds its light x the
// share of the sectors it is the first to cover (a WARP test against a ray-traced reference on the same depth: half
// the original's error, open ground less grey, contact darker; the original bounce was too dark).
static const float BITMASK_PI = 3.14159265f;
static const uint BITMASK_SECTORS = 32;

// The level a sample `distance` texels from the pixel reads, of a pass's levels (each half the size of the one above,
// `count` past the first): 8 x 2^n to 16 x 2^n texels away, level n, whose texels are 1/16 to 1/8 of the distance (as
// McGuire, Mara and Luebke's Scalable Ambient Obscurance, 2012), so a pixel's samples and its neighbours' stay in the
// texture cache.
uint BitmaskSampleLevel(float distance, uint count) {
  return (uint)clamp((int)firstbithigh((uint)distance) - 3, 0, (int)count);
}

// The pixel's place along a 64 x 64 Hilbert curve (the standard xy-to-index walk): neighbouring pixels get nearby places.
uint BitmaskHilbertIndex(uint2 pixel) {
  uint index = 0u;
  for (uint level = 32u; level > 0u; level >>= 1) {
    const uint right = ((pixel.x & level) != 0u) ? 1u : 0u;
    const uint up = ((pixel.y & level) != 0u) ? 1u : 0u;
    index += level * level * ((3u * right) ^ up);
    if (up == 0u) {
      if (right == 1u) pixel = 63u - pixel;
      pixel = pixel.yx;
    }
  }
  return index;
}

// Three noise values (0-1) for a pixel and frame, as Intel's XeGTAO draws its: the pixel's Hilbert index, moved 288 a
// frame (64 frames), through the R3 low-discrepancy sequence, so neighbouring pixels and successive frames get
// well-spread values (smoother on screen and settling faster over frames than per-pixel hash noise).
float3 BitmaskNoise(uint2 pixel, uint frame) {
  const uint index = BitmaskHilbertIndex(pixel & 63u) + 288u * (frame % 64u);
  return frac(0.5f + index * float3(0.8191725134f, 0.6710436067f, 0.5497004779f));
}

// acos from GTAO's fast polynomial with one more term (GT-VBAO's): far finer than a sector, and cheap.
float BitmaskAcos(float x) {
  const float magnitude = min(abs(x), 1.f);
  const float polynomial = 0.5f * BITMASK_PI + (-0.20491203f + 0.04832927f * magnitude) * magnitude;
  const float angle = polynomial * sqrt(1.f - magnitude);
  return (x >= 0.f) ? angle : (BITMASK_PI - angle);
}

struct BitmaskSlice {
  float3 direction;         // in view space, across the view vector
  float normal_angle;       // the projected normal's angle from the view vector, positive toward +direction
  float cos_normal;         // its cosine
  float2 screen_direction;  // +direction on screen (pixels run down)
};

// The rotation of (0, 0, -1) onto `to` (a quaternion with z 0: xy, w), or of (0, 0, 1) when `to` points along +z (a
// view space whose camera looks down -z).
float4 BitmaskViewQuaternion(float3 to) {
  const float axis = (to.z <= 0.f) ? -1.f : 1.f;
  const float u = rsqrt(max(0.f, axis * to.z * 0.5f + 0.5f));
  return float4(float3(-axis * to.y, axis * to.x, 0.f) * (u * 0.5f), 1.f / u);
}

// `v` rotated by `q` (its z 0).
float3 BitmaskRotate(float3 v, float4 q) {
  const float k = v.y * q.x - v.x * q.y;
  const float g = 2.f * (v.z * q.w + k);
  return float3(v.xy + q.yx * float2(g, -g), v.z + 2.f * (q.w * k - v.z * dot(q.xy, q.xy)));
}

// (v, 0) rotated by `q` (its z 0).
float3 BitmaskRotateFlat(float2 v, float4 q) {
  const float o = q.x * v.y;
  const float c = q.y * v.x;
  return float3(v, 0.f) + 2.f * (float3(o - c, c - o, o - c) * q.yxw);
}

// A slice direction around the view vector (`normal_vv`: the normal in the view vector's frame, z along it) drawn in
// proportion to the projected normal from `u` (0-1): GT-VBAO's approximate inverse cumulative distribution.
float2 BitmaskSampleSliceDirection(float3 normal_vv, float u) {
  float2 direction0 = float2(cos(u * BITMASK_PI), sin(u * BITMASK_PI));
  const float l = length(normal_vv.xy);
  if (l == 0.f) return direction0;
  // Into the half circle of the normal.
  direction0 *= (dot(direction0, normal_vv.xy) < 0.f) ? -1.f : 1.f;
  const float2 n = normal_vv.xy / l;
  float x = direction0.x * n.y - direction0.y * n.x;
  // The stretch of the curve (a quadratic bias of the normal's sine), applied through an approximate inverse asin.
  const float s = l + (l - l * l) * 0.15f;
  const float k = 0.21545f;
  const float stretch = 0.5f - 0.5f * s;
  x *= (0.5f + 0.5f / k) - sqrt((0.5f - 0.5f / k) * (0.5f - 0.5f / k) + (4.f / k) * stretch * stretch);
  // Approximate acos.
  const float y = abs(((x > 0.f) ? 2.f : 0.f) - sqrt(saturate((abs(x) * k - k - 1.f) * abs(x) + 1.f)));
  float2 direction;
  direction.y = (1.f - y) / s;
  direction.x = sqrt(saturate(1.f - direction.y * direction.y));
  return float2(direction.x * n.x - direction.y * n.y, direction.y * n.x + direction.x * n.y);
}

// The slice drawn from `u` (0-1) for the pixel at view-space `position`, its direction on screen that of a step along
// it from the pixel (view space x right, y up; z either way).
BitmaskSlice BitmaskSliceAt(float u, float3 normal, float3 view_direction, float3 position) {
  const float4 to_view = BitmaskViewQuaternion(view_direction);
  const float4 from_view = float4(-to_view.xyz, to_view.w);
  BitmaskSlice slice;
  slice.direction = BitmaskRotateFlat(BitmaskSampleSliceDirection(BitmaskRotate(normal, from_view), u), to_view);
  const float3 axis = cross(view_direction, slice.direction);
  const float3 projected_normal = normal - axis * dot(normal, axis);
  slice.cos_normal = clamp(dot(projected_normal, view_direction) / max(length(projected_normal), 1e-5f), -1.f, 1.f);
  slice.normal_angle = ((dot(slice.direction, projected_normal) < 0.f) ? -1.f : 1.f) * BitmaskAcos(slice.cos_normal);
  const float2 screen = slice.direction.xy - position.xy * (slice.direction.z / position.z);
  slice.screen_direction = normalize(float2(screen.x, -screen.y));
  return slice;
}

// The cosine-weighted share of the slice up to `x` (0-1 across the hemisphere around the projected normal) on the
// `positive` side of the view vector (the pole), for a slice whose normal is `normal_angle` from the view vector.
float BitmaskCosineShare(float x, float normal_angle, float cos_normal, bool positive) {
  if (x <= 0.f || x >= 1.f) return x;
  const float phi = x * BITMASK_PI - 0.5f * BITMASK_PI;
  const float angle = -normal_angle;
  const float sin_angle = sin(angle);
  const float n0 = positive ? 3.f : 1.f;
  const float n1 = positive ? -1.f : 1.f;
  const float n2 = positive ? 4.f : 0.f;
  const float share =
      n0 * cos_normal + n1 * cos(angle - 2.f * phi) + (n2 * angle + 2.f * n1 * phi + BITMASK_PI) * sin_angle;
  return share / (4.f * (cos_normal + angle * sin_angle));
}

// Auto thickness (Bjorn Ottosson's variation of GT-VBAO, Shadertoy 3clGWB, CC0; ported here): an occluder taken to be
// as thick as it is wide. Along one side of a slice, a sample stepping mostly along the view vector from the one before
// starts a new occluder; each sample's thickness is `base` plus its distance from where its occluder started, x `scale`
// (a per-pixel random 0.5-2, so the guess does not show as edges), and its back no nearer than its occluder's start's.
struct BitmaskOccluder {
  float3 start;
  float3 previous;
};

float BitmaskOccluderThickness(inout BitmaskOccluder occluder, float3 sample_position, float3 view_direction, float base,
                               float scale) {
  float3 step = sample_position - occluder.previous;
  step.xy *= scale;
  // |cos| of the step against the view vector above 0.9, without a square root.
  const float along = dot(step, view_direction);
  if (along * along > 0.81f * dot(step, step)) occluder.start = sample_position;
  occluder.previous = sample_position;
  return scale * (base + length(sample_position - occluder.start));
}

// The back of a sample (`delta` from the pixel at `position`) `thickness` along the view vector, no nearer than its
// occluder's start's (its direction from the pixel).
float3 BitmaskOccluderBack(BitmaskOccluder occluder, float3 delta, float3 position, float3 view_direction,
                           float thickness) {
  const float3 back = normalize(delta - view_direction * thickness);
  const float3 start_back = normalize(occluder.start - position - view_direction * thickness);
  return (dot(start_back, view_direction) < dot(back, view_direction)) ? start_back : back;
}

// The sectors a sample covers: `front` and `back` the unit vectors to its front and to its back (thickness behind), on
// `side` (-1 / 1) of the view vector; its edges moved by `jitter` (0-1 of a sector, the same for the slice's samples)
// and taken as points.
uint BitmaskSectors(float3 front, float3 back, float3 view_direction, float side, BitmaskSlice slice, float jitter) {
  float2 angles = side * float2(BitmaskAcos(dot(front, view_direction)), BitmaskAcos(dot(back, view_direction)));
  // 0 to 1 across the hemisphere around the projected normal, then its cosine-weighted share.
  angles = saturate((angles - slice.normal_angle + 0.5f * BITMASK_PI) / BITMASK_PI);
  float2 shares = float2(min(angles.x, angles.y), max(angles.x, angles.y));
  shares.x = BitmaskCosineShare(shares.x, slice.normal_angle, slice.cos_normal, side > 0.f);
  shares.y = BitmaskCosineShare(shares.y, slice.normal_angle, slice.cos_normal, side > 0.f);
  const uint2 edges = uint2(floor(saturate(shares + jitter / BITMASK_SECTORS) * BITMASK_SECTORS));
  const uint low = (edges.x < BITMASK_SECTORS) ? (0xFFFFFFFFu << edges.x) : 0u;
  const uint high = (edges.y != 0u) ? (0xFFFFFFFFu >> (BITMASK_SECTORS - edges.y)) : 0u;
  return low & high;
}
