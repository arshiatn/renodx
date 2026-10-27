/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Visibility bitmask ambient occlusion for Attila's, Thrones of Britannia's and Rome II's AO pass (ssao.hlsl
// ps_pixel_resolve, the game's HBAO replaced): GT-VBAO (bitmask.hlsli: Therrien, Levesque and Gilet's visibility bitmask
// with Mirko Salm's ground-truth weighting): each slice's occlusion kept as 32 sectors and every depth sample taken as
// a surface of constant thickness, so light passes behind thin objects (soldiers, spears, poles, trunks) instead of the
// dark halos a height field gives them. Radius in metres, Strength an exponent (1: GT-VBAO's result, without the
// game's strength and minimum); the game's blur follows. Its noise: bitmask.hlsli BitmaskNoise (as XeGTAO's), moving every frame with Temporal Smoothing. Depth
// Levels (depth_levels.hlsli): samples far from the pixel read the depth at a lower resolution, so they stay in the
// texture cache. Below full resolution, with Temporal Smoothing or the Denoiser (ao_pass.hpp) it draws into the addon's
// target (its result and distance; below full resolution each AO pixel the one at its texel x SI.ssao_scale), then the
// pass's
// other builds: AO_RESOLVE (BitmaskResolve: the last frames' result held within this frame's neighbourhood),
// AO_DENOISE (BitmaskDenoise: 5 x 5 along the surface), AO_UPSAMPLE (BitmaskUpsample: into the game's target). The
// includer declares first (its shared.h, then the pass's
// camera_VS_PS at b0 with camera_position, inv_projection, inv_view_projection, g_screen_size, g_vpos_texel_offset,
// g_viewport_dimensions;
// ssao_resolve_PS with g_AOResolution and g_VerticalFieldOfView; gbuffer_channel_4_sampler and its sampler); the
// camera's view_projection at c9 in every one of them.
#include "./ao_upsample.hlsli"
#include "./bitmask.hlsli"
#include "./depth_levels.hlsli"

// The input of the addon's later passes (the result before them, its distance from the camera; 0: nothing there), and
// for the resolve the last frame's history (as the input, its distance from the camera then, how many frames it holds)
// and that frame's camera (only what is read). ao_pass.hpp binds them.
Texture2D<float2> t_ao_input : register(t40);
Texture2D<float3> t_ao_history : register(t42);
cbuffer previous_camera : register(b10)
{
  float3 previous_camera_position : packoffset(c0);
  float4x4 previous_view_projection : packoffset(c9);
}

// A texture uv (of the screen-size textures) x this: the view's own uv, as its projection takes it. DLSS below 100%
// draws the view into their top-left corner (g_viewport_dimensions the render size); 1 otherwise.
float2 AoViewScale()
{
  return g_screen_size.xy * g_viewport_dimensions.zw;
}

// View space (x right, y up, z forward, as the game's HBAO) at texture uv `uv`: z from `depth` through inv_projection.
float3 AoViewPositionOf(float2 uv, float depth, float2 tan_half_fov)
{
  const float4 ndc = float4(uv * AoViewScale() * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f);
  const float z = dot(ndc, inv_projection._m02_m12_m22_m32) / dot(ndc, inv_projection._m03_m13_m23_m33);
  return float3(ndc.xy * tan_half_fov * z, z);
}

// The game's depth at `uv`.
float3 AoViewPosition(float2 uv, float2 tan_half_fov)
{
  const float depth = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, uv, 0).x;
  return AoViewPositionOf(uv, depth, tan_half_fov);
}

float2 AoTanHalfFov()
{
  const float tan_half_y = tan(0.5f * g_VerticalFieldOfView);
  return float2(tan_half_y * g_AOResolution.x / g_AOResolution.y, tan_half_y);
}

// The game's size over the ambient occlusion's (1 full, 2 Half; the addon sets it): an AO pixel stands for the pixel at
// its texel x this.
float AoScale()
{
  return max(SI.ssao_scale, 1.f);
}

// The result (as the game's: x the strength, above the minimum; in the addon's target with its distance).
float4 BitmaskAmbientOcclusion(float4 v0)
{
  const float scale = AoScale();
  const float2 uv = g_screen_size.zw * ((v0.xy - 0.5f) * scale + 0.5f + g_vpos_texel_offset);
  const float2 tan_half_fov = AoTanHalfFov();
  const float3 position = AoViewPosition(uv, tan_half_fov);
  const float radius = SI.ssao_radius;
  // The radius on screen (texture uv), as the game's: below one AO pixel nothing to sample (also the sky).
  const float2 radius_uv = 0.5f * radius / (max(position.z, 1e-6f) * tan_half_fov) / AoViewScale();
  const float2 texel = 1.f / g_AOResolution;
  const float radius_pixels = min(radius_uv.x * g_AOResolution.x, radius_uv.y * g_AOResolution.y);
  float value = 1.f;
  [branch]
  if (position.z > 0.f && radius_pixels >= 1.f) {
    // The normal from the nearer neighbour on each axis (no normals bound here, as the game's).
    const float3 right = AoViewPosition(uv + float2(texel.x, 0.f), tan_half_fov);
    const float3 left = AoViewPosition(uv - float2(texel.x, 0.f), tan_half_fov);
    const float3 down = AoViewPosition(uv + float2(0.f, texel.y), tan_half_fov);
    const float3 up = AoViewPosition(uv - float2(0.f, texel.y), tan_half_fov);
    const float3 tangent_x = (abs(right.z - position.z) < abs(position.z - left.z)) ? (right - position) : (position - left);
    const float3 tangent_y = (abs(down.z - position.z) < abs(position.z - up.z)) ? (down - position) : (position - up);
    const float3 view = normalize(-position);
    float3 normal = normalize(cross(tangent_x, tangent_y));
    if (dot(normal, view) < 0.f) normal = -normal;

    // Thin surfaces: a constant thickness behind each depth sample, growing with distance (the paper's option against
    // occlusion fading far away).
    const float thickness = SI.ssao_thickness * (1.f + 0.02f * position.z);
    // The slices' rotation, the steps' offsets and the sectors' edges (jittered per slice).
    const float3 noises = BitmaskNoise(uint2(v0.xy), (uint)SI.ssao_frame);
    const float noise = noises.x;
    // Directions (slices) around the view vector: 2^Directions.
    const uint slices = 1u << (uint)clamp(SI.ssao_directions, 0.f, 3.f);
    const float jitter = noises.z;
    // Samples per side of a slice: 8 x 2^Quality (8 to 256; more catch thin objects at a large radius), at most one per
    // AO pixel of the radius (more would read the same pixels).
    const uint steps = min(8u << (uint)clamp(SI.ssao_quality, 0.f, 5.f), max(4u, (uint)radius_pixels));
    uint2 depth_size;
    gbuffer_channel_4_sampler.GetDimensions(depth_size.x, depth_size.y);
    const DepthLevels levels = DepthLevelsBound();

    // The slices drawn in proportion to the projected normal: each counts the same.
    float visibility = 0.f;
    [loop]
    for (uint slice = 0; slice < slices; ++slice) {
      const BitmaskSlice slice_info = BitmaskSliceAt((slice + noise) / slices, normal, view, position);
      const float slice_jitter = frac(jitter + 0.618034f * slice);
      const float2 step_uv = slice_info.screen_direction * radius_uv;
      // Auto thickness: each side's occluder, the slice's random scale.
      BitmaskOccluder occluder_minus = {position, position};
      BitmaskOccluder occluder_plus = {position, position};
      const float occluder_scale = exp2(2.f * frac(jitter + 0.5f + 0.754877666f * slice) - 1.f);

      uint occluded = 0u;
      // Until every sector is covered (nothing farther can change it).
      [loop]
      for (uint step = 0; step < steps && occluded != 0xFFFFFFFFu; ++step) {
        float distance = (step + frac(noises.y + 0.618034f * (step + slice * steps))) / steps;
        // Denser near the pixel; at least one AO pixel away (the centre itself is no occluder).
        distance = max(distance * distance, 1.f / radius_pixels);
        const float2 offset = step_uv * distance;
        const uint level = BitmaskSampleLevel(length(offset * depth_size), levels.count);
        [unroll]
        for (int side = -1; side <= 1; side += 2) {
          const float2 sample_uv = uv + side * offset;
          // Past the view's edge (DLSS below 100%: stale texels there): nothing.
          if (any(sample_uv < 0.f) || any(sample_uv * AoViewScale() >= 1.f)) continue;
          float3 sample_position;
          [branch]
          if (level == 0u) {
            sample_position = AoViewPosition(sample_uv, tan_half_fov);
          } else {
            // Its depth texel's own position (on the surface, not moved to the sample's).
            float depth;
            const uint2 source = DepthLevelLoad(levels, level, sample_uv * depth_size, depth);
            sample_position = AoViewPositionOf((source + 0.5f) / depth_size, depth, tan_half_fov);
          }
          float sample_thickness = thickness;
          BitmaskOccluder occluder = occluder_minus;
          if (side > 0) occluder = occluder_plus;
          [branch]
          if (SI.ssao_auto_thickness != 0.f) {
            sample_thickness = BitmaskOccluderThickness(occluder, sample_position, view, thickness, occluder_scale);
            if (side > 0) {
              occluder_plus = occluder;
            } else {
              occluder_minus = occluder;
            }
          }
          const float3 delta = sample_position - position;
          const float delta_length = length(delta);
          if (!(delta_length > 1e-4f) || delta_length > radius) continue;
          const float3 front = delta / delta_length;
          // Its thickness along its own camera ray (auto thickness: its occluder's).
          float3 back;
          [branch]
          if (SI.ssao_auto_thickness != 0.f) {
            back = BitmaskOccluderBack(occluder, delta, position, view, sample_thickness);
          } else {
            back = normalize(delta + normalize(sample_position) * thickness);
          }
          occluded |= BitmaskSectors(front, back, view, side, slice_info, slice_jitter);
        }
      }
      visibility += 1.f - countbits(occluded) / (float)BITMASK_SECTORS;
    }
    value = exp2(log2(max(visibility / slices, 1e-4f)) * SI.ssao_strength);
  }
  // The addon's target: the result and its distance from the camera (0: nothing there); else the game's.
  [branch]
  if (AoScale() > 1.f || SI.ssao_temporal != 0.f || SI.ssao_denoise != 0.f) {
    const float distance = length(position);
    return float4(value, (position.z > 0.f && distance < 6e4f) ? distance : 0.f, 0.f, 0.f);
  }
  return value;
}

// Temporal Smoothing: this frame's result (t40) and the last frames' at this point (t42: moved with the camera,
// bilinear from the 4 history texels around it, each only if it held the same surface, its distance from the camera
// then within 5%), the history held within this frame's 3 x 3 results of the same surface (mean +- 1.5 x their spread,
// at least 0.05: variance clipping, Salvi 2016; as SVGF's temporal step), so a soldier walking by leaves no trail although
// only the camera's motion is known. The history's frames counted (as SVGF's history length): a newly seen spot averages
// its first frames evenly, then 95% history (about the last 20 frames). Edge Fix (SI.ssao_edge_fix; ATN: thin halos
// along outlines): on an outline (a 3 x 3 neighbour of another surface) the one history texel nearest to where the
// point was, within 2% (no blend with what lies behind it), and with fewer than 5 of the same surface the history held
// within 0.2 of this frame's (not free: old shading lingered there; WARP: 0.1 tripled the worst outline flicker at one
// direction, 0.2 doubles it, 0.3 none). Into the next history: the result, its distance
// and the count.
float4 BitmaskResolve(float4 v0)
{
  const int2 texel = int2(v0.xy);
  const float2 current = t_ao_input.Load(int3(texel, 0));
  if (!(current.y > 0.f)) return float4(current, 1.f, 0.f);
  float3 moments = 0.f;  // count, sum, sum of squares
  [unroll]
  for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
      const float2 neighbour = t_ao_input.Load(int3(texel + int2(x, y), 0));
      if (abs(neighbour.y - current.y) < 0.05f * current.y) moments += float3(1.f, neighbour.x, neighbour.x * neighbour.x);
    }
  }
  const float mean = moments.y / moments.x;
  const float spread = sqrt(max(moments.z / moments.x - mean * mean, 0.f));
  const float scale = AoScale();
  const float2 uv = g_screen_size.zw * ((v0.xy - 0.5f) * scale + 0.5f + g_vpos_texel_offset);
  const float depth = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, uv, 0).x;
  const float4 world = mul(float4(uv * AoViewScale() * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), inv_view_projection);
  const float3 world_position = world.xyz / world.w;
  const float4 clip = mul(float4(world_position, 1.f), previous_view_projection);
  float value = current.x;
  float frames = 0.f;  // the history's
  [branch]
  if (clip.w > 0.f) {
    const float2 previous_uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
    const float2 coordinate = (previous_uv / AoViewScale() / g_screen_size.zw - g_vpos_texel_offset - 0.5f) / scale;
    const int2 base = int2(floor(coordinate));
    const float2 blend = coordinate - base;
    const float expected = length(world_position - previous_camera_position);
    const bool edge = (SI.ssao_edge_fix != 0.f) && (moments.x < 9.f);
    const float tolerance = edge ? 0.02f : 0.05f;
    float3 sum = 0.f;  // result, frames, weight
    float3 nearest = 0.f;
    float nearest_weight = 0.f;
    [unroll]
    for (int y = 0; y <= 1; ++y) {
      [unroll]
      for (int x = 0; x <= 1; ++x) {
        const float3 previous = t_ao_history.Load(int3(base + int2(x, y), 0));
        const float2 weights = (int2(x, y) == 1) ? blend : (1.f - blend);
        const float weight = weights.x * weights.y;
        const bool same = abs(previous.y - expected) < tolerance * expected;
        sum += float3(previous.xz, 1.f) * (same ? weight : 0.f);
        [flatten]
        if (same && weight > nearest_weight) {
          nearest = float3(previous.xz, 1.f);
          nearest_weight = weight;
        }
      }
    }
    if (edge && nearest_weight > 0.f) sum = nearest;
    [flatten]
    if (sum.z > 0.01f) {
      // Fewer than 5 of the same surface (thin things, edges): their spread says nothing (one alone has none, which
      // would hold the history to this frame's noise), only the distance check; with the Edge Fix a loose hold.
      float width = 1.f;
      if (moments.x >= 5.f) {
        width = max(1.5f * spread, 0.05f);
      } else if (edge) {
        width = 0.2f;
      }
      const float held = clamp(sum.x / sum.z, mean - width, mean + width);
      // Partly the same surface: fewer frames trusted.
      frames = sum.y / sum.z * saturate(2.f * sum.z);
      value = lerp(current.x, held, min(frames / (frames + 1.f), 0.95f));
    }
  }
  return float4(value, current.y, min(frames + 1.f, 32.f), 0.f);
}

// The Denoiser (Intel XeGTAO's 5 x 5 depth-aware filter, written here): the 5 x 5 results around this one (t40), each by
// its distance (binomial weights) and only where its distance from the camera is within 3% of this one's (no blending
// across edges). The result and its distance.
float4 BitmaskDenoise(float4 v0)
{
  const int2 texel = int2(v0.xy);
  const float2 center = t_ao_input.Load(int3(texel, 0));
  if (!(center.y > 0.f)) return float4(center, 0.f, 0.f);
  static const float BINOMIAL[5] = {1.f, 4.f, 6.f, 4.f, 1.f};
  float2 sum = 0.f;
  [unroll]
  for (int y = -2; y <= 2; ++y) {
    [unroll]
    for (int x = -2; x <= 2; ++x) {
      const float2 other = t_ao_input.Load(int3(texel + int2(x, y), 0));
      const float alike = saturate(1.f - abs(other.y - center.y) / (0.03f * center.y));
      sum += float2(other.x, 1.f) * (BINOMIAL[x + 2] * BINOMIAL[y + 2] * ((other.y > 0.f) ? alike : 0.f));
    }
  }
  return float4(sum.x / sum.y, center.y, 0.f, 0.f);
}

// Into the game's target from the addon's (t40): at Half resolution from the half-size results (ao_upsample.hlsli), else
// the pixel's own; the sky open.
float4 BitmaskUpsample(float4 v0)
{
  const float2 uv = g_screen_size.zw * (v0.xy + g_vpos_texel_offset);
  const float3 position = AoViewPosition(uv, AoTanHalfFov());
  const float distance = length(position);
  if (!(position.z > 0.f && distance < 6e4f)) return 1.f;
  if (AoScale() > 1.f) return AoUpsample(t_ao_input, int2(v0.xy), distance, AoScale());
  return t_ao_input.Load(int3(v0.xy, 0)).x;
}
