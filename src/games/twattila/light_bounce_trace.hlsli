/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental), the trace (light_bounce.hpp): the light bouncing onto each texel from what the camera
// sees around it, as GT-VBGI (bitmask.hlsli: Therrien, Levesque and Gilet's "Screen Space Indirect Lighting with
// Visibility Bitmask" with Mirko Salm's ground-truth weighting): along slices around the view vector drawn in proportion
// to the projected normal, every sample covers sectors of a visibility bitmask (equal cosine-weighted shares), and its
// light counts for the share of the sectors it is the first to cover. Samples: the prepared light
// and geometry (light_bounce_prepare) at the trace's resolution, far ones from their levels (light_bounce_levels: in the
// texture cache). Its noise (bitmask.hlsli BitmaskNoise) moves every frame with history; light_bounce_resolve adds up
// the frames. Drawn once a frame with the addon's full-screen triangle into the raw target; alpha: the texel's distance
// from the camera (0: nothing; negative: a Checkerboard texel holding its history already). Game-neutral: the includer
// defines first (its
// light_bounce.hlsli, the camera at b11 and the last frame's at b10):
//   float3 BounceViewPositionAt(int2 texel, float depth)  the view-space position at a pixel for a depth buffer value
//   float BouncePixelsPerMetre(float distance)     pixels per metre (vertically) that far from the camera
//   float2 BounceViewportSize()                    the view's size in pixels
//   float3 BouncePrevious(float3 view_position)    that position in the last frame: its pixel (x, y), its distance (z)
#include "./bitmask.hlsli"
#include "./light_bounce.h"
#include "./light_bounce_geometry.hlsli"
#include "./light_bounce_history.hlsli"

// With its levels (light_bounce_levels).
Texture2D<float4> t_prepared_light : register(t46);

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0) {
  o0 = 0.f;
  const BounceGeometry center = LoadGeometry(int2(v0.xy), 0u, 0u);
  const float distance = length(center.position);
  // The sky (also a reversed infinite far plane): nothing.
  if (!(distance > 1e-3f && distance < 1e5f)) return;
  o0.a = distance;

  // Checkerboard: every other 16 x 16 block of texels each frame (whole GPU waves skip the trace), the others keeping
  // the last frames' bounce where it held the same surface (else traced).
  [branch]
  if (LB.checkerboard > 0.f && ((uint(v0.x) / 16u + uint(v0.y) / 16u + uint(LB.frame)) & 1u) != 0u) {
    const float4 history = BounceHistory(center.position);
    [branch]
    if (history.a > 0.5f) {
      o0 = float4(history.rgb, -distance);
      return;
    }
  }

  const float2 viewport = BounceViewportSize() / LB.scale;
  // The radius on screen in texels, at most a quarter of its height (the samples' cache misses).
  const float radius_texels = min(LB.radius * BouncePixelsPerMetre(distance) / LB.scale, 0.25f * viewport.y);
  float3 bounce = 0.f;
  [branch]
  if (radius_texels >= 1.f) {
    const float3 view_direction = -center.position / distance;
    const float3 normal = (dot(center.normal, view_direction) < 0.f) ? -center.normal : center.normal;
    // A constant thickness behind each sample, growing with distance (as the ambient occlusion).
    const float thickness = LB.thickness * (1.f + 0.02f * distance);
    // The slices' rotation, the steps' offsets and the sectors' edges (jittered per slice), moved every frame with
    // history.
    const float3 noises = BitmaskNoise(uint2(v0.xy), (uint)LB.frame);
    const float noise = noises.x;
    // At most one step per texel of the radius (more would read the same texels).
    const uint steps = min(max((uint)LB.steps, 1u), max(4u, (uint)radius_texels));
    const uint slices = max((uint)LB.slices, 1u);
    const float jitter = noises.z;
    uint2 prepared_size;
    uint prepared_levels;
    t_prepared_geometry.GetDimensions(0, prepared_size.x, prepared_size.y, prepared_levels);

    [loop]
    for (uint slice = 0; slice < slices; ++slice) {
      const BitmaskSlice slice_info = BitmaskSliceAt((slice + noise) / slices, normal, view_direction, center.position);
      const float slice_jitter = frac(jitter + 0.618034f * slice);
      const float2 step_texels = slice_info.screen_direction * radius_texels;
      // Back-face rejection: the slice's plane and the projected normal (GT-VBGI's frame).
      const float3 slice_axis = cross(view_direction, slice_info.direction);
      const float3 projected_normal = normal - slice_axis * dot(normal, slice_axis);
      const float3 slice_tangent = cross(slice_axis, projected_normal);
      // Auto thickness: each side's occluder, the slice's random scale.
      BitmaskOccluder occluder_minus = {center.position, center.position};
      BitmaskOccluder occluder_plus = {center.position, center.position};
      const float occluder_scale = exp2(2.f * frac(jitter + 0.5f + 0.754877666f * slice) - 1.f);
      uint occluded = 0u;
      // Until every sector is covered (nothing farther can be seen).
      [loop]
      for (uint step = 0; step < steps && occluded != 0xFFFFFFFFu; ++step) {
        float t = (step + frac(noises.y + 0.618034f * (step + slice * steps))) / steps;
        // Denser near the texel; at least one texel away (the texel itself is no source).
        t = max(t * t, 1.f / radius_texels);
        const float2 offset = step_texels * t;
        const uint level = BitmaskSampleLevel(length(offset), max(prepared_levels, 1u) - 1u);
        const uint2 level_last = BounceRegionLast(level, max(prepared_size >> level, 1u));
        [unroll]
        for (int side = -1; side <= 1; side += 2) {
          const float2 sample_point = v0.xy + side * offset;
          if (any(sample_point < 0.f) || any(sample_point >= viewport)) continue;
          const int2 sample_texel = int2(min(uint2(sample_point) >> level, level_last));
          const BounceGeometry source = LoadGeometry(sample_texel, level, prepared_size);
          float sample_thickness = thickness;
          BitmaskOccluder occluder = occluder_minus;
          if (side > 0) occluder = occluder_plus;
          [branch]
          if (LB.auto_thickness > 0.f) {
            sample_thickness =
                BitmaskOccluderThickness(occluder, source.position, view_direction, thickness, occluder_scale);
            if (side > 0) {
              occluder_plus = occluder;
            } else {
              occluder_minus = occluder;
            }
          }
          const float3 delta = source.position - center.position;
          const float delta_length = length(delta);
          if (!(delta_length > 1e-4f) || delta_length > LB.radius) continue;
          const float3 front = delta / delta_length;
          // Its thickness along its own camera ray (auto thickness: its occluder's).
          float3 back;
          [branch]
          if (LB.auto_thickness > 0.f) {
            back = BitmaskOccluderBack(occluder, delta, center.position, view_direction, sample_thickness);
          } else {
            back = normalize(delta + normalize(source.position) * thickness);
          }
          const uint covered = BitmaskSectors(front, back, view_direction, side, slice_info, slice_jitter);
          uint fresh = covered & ~occluded;
          // Back-face rejection: only the sectors from which the sample's front side faces this texel.
          [branch]
          if (fresh != 0u && LB.backface > 0.f) {
            const float3 source_projected = source.normal - slice_axis * dot(source.normal, slice_axis);
            const float lengths = length(projected_normal) * length(source_projected);
            if (lengths > 0.f) {
              const bool flip = dot(slice_tangent, source.normal) < 0.f;
              float edge = BitmaskAcos(dot(projected_normal, source_projected) / lengths) / BITMASK_PI;
              if (flip) edge = 1.f - edge;
              edge = BitmaskCosineShare(edge, slice_info.normal_angle, slice_info.cos_normal,
                                        edge * BITMASK_PI - 0.5f * BITMASK_PI > -slice_info.normal_angle);
              const uint first = (uint)floor(saturate(edge + slice_jitter / BITMASK_SECTORS) * BITMASK_SECTORS);
              const uint facing = (first < BITMASK_SECTORS) ? (0xFFFFFFFFu << first) : 0u;
              fresh &= flip ? facing : ~facing;
            }
          }
          if (fresh != 0u) {
            const float share = countbits(fresh) / (float)BITMASK_SECTORS;
            bounce += t_prepared_light.Load(int3(sample_texel, level)).rgb * share;
          }
          occluded |= covered;
        }
      }
    }
    // The sectors are equal cosine-weighted shares: a surface seeing only light L in every sector gets L.
    bounce /= slices;
  }
  o0.rgb = bounce;
}
