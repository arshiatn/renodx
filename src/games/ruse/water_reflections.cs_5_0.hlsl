/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// R.U.S.E.'s screen-space water reflections (ray_tracing.hpp, the water shaders' s13): for each pixel whose depth faces
// up (world z), the eye ray mirrored by a flat surface (the water's mean plane), marched in screen space through this
// frame's start depth (the ground pre-pass's, last frame's main depth over it); a hit reads last frame's scene where that
// point was. Out: the reflected light in the water shaders' units (the scene's gamma 2.2: what their cube map holds) and
// its share (alpha: 0 = the game's cube, at misses, the screen's edges and the end of the reach).
Texture2D<float> t_depth : register(t0);
Texture2D<float4> t_light : register(t1);  // last frame's scene as light (the bridge's colour: linear)
RWTexture2D<float4> u_reflection : register(u0);

cbuffer water_reflection_settings : register(b0) {
  row_major float4x4 clip_to_relative;      // this frame's clip to offsets from the eye (homogeneous)
  row_major float4x4 relative_to_clip;      // offsets from the eye to this frame's clip
  row_major float4x4 relative_to_previous;  // offsets from this frame's eye to last frame's clip
  float4 size;                              // width, height, 1 / width, 1 / height
  float4 projection;                        // P00, P11, A, B: the depth is A + B / view z
  float4 range;                             // reach (world units), thickness (world units), 0, 0
};

static const uint STEPS = 48u;
static const uint REFINEMENTS = 5u;
static const float FLAT = 0.8f;  // the depth's normal's z above this: a surface that may be water

float ViewZ(float depth) {
  return projection.w / min(depth - projection.z, -1e-9f);
}

float3 RelativePosition(float2 pixel_centre, float depth) {
  const float2 ndc = float2(pixel_centre.x * size.z * 2.f - 1.f, 1.f - pixel_centre.y * size.w * 2.f);
  const float4 relative = mul(clip_to_relative, float4(ndc, depth, 1.f));
  return relative.xyz / relative.w;
}

float DepthAt(int2 pixel) {
  return t_depth.Load(int3(clamp(pixel, int2(0, 0), int2(size.xy) - 1), 0));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= (uint2)size.xy)) return;
  const int2 pixel = int2(id.xy);
  float4 reflection = 0.f;
  const float depth = DepthAt(pixel);
  [branch]
  if (depth < 1.f) {
    // The depth's normal from the nearer neighbour on each axis (no step across an edge); only surfaces facing up.
    const float3 position = RelativePosition(pixel + 0.5f, depth);
    const float3 right = RelativePosition(pixel + float2(1.5f, 0.5f), DepthAt(pixel + int2(1, 0)));
    const float3 left = RelativePosition(pixel + float2(-0.5f, 0.5f), DepthAt(pixel - int2(1, 0)));
    const float3 down = RelativePosition(pixel + float2(0.5f, 1.5f), DepthAt(pixel + int2(0, 1)));
    const float3 up = RelativePosition(pixel + float2(0.5f, -0.5f), DepthAt(pixel - int2(0, 1)));
    const float3 tangent_x = (distance(right, position) < distance(position, left)) ? (right - position) : (position - left);
    const float3 tangent_y = (distance(down, position) < distance(position, up)) ? (down - position) : (position - up);
    float3 normal = normalize(cross(tangent_x, tangent_y));
    if (dot(normal, position) > 0.f) normal = -normal;
    const float3 view = normalize(position);
    const float3 mirrored = float3(view.xy, -view.z);
    [branch]
    if (normal.z > FLAT && view.z < 0.f) {
      // The ray's ends in this frame's clip, the far one pulled in front of the eye.
      const float4 start = mul(relative_to_clip, float4(position, 1.f));
      float4 end = mul(relative_to_clip, float4(position + mirrored * range.x, 1.f));
      const float near_w = 1e-3f * start.w;
      if (end.w < near_w) end = lerp(start, end, (start.w - near_w) / (start.w - end.w));
      const float2 start_pixel = (start.xy / start.w * float2(0.5f, -0.5f) + 0.5f) * size.xy;
      const float2 end_pixel = (end.xy / end.w * float2(0.5f, -0.5f) + 0.5f) * size.xy;
      const float start_depth = start.z / start.w;
      const float end_depth = end.z / end.w;
      // Screen depth is linear in screen space; the march stops at the screen's edge.
      const float2 travel = end_pixel - start_pixel;
      float reach = 1.f;
      [unroll]
      for (int axis = 0; axis < 2; ++axis) {
        if (travel[axis] > 1e-6f) reach = min(reach, (size[axis] - 0.5f - start_pixel[axis]) / travel[axis]);
        if (travel[axis] < -1e-6f) reach = min(reach, (0.5f - start_pixel[axis]) / travel[axis]);
      }
      const float travel_pixels = length(travel) * reach;
      [branch]
      if (travel_pixels > 2.f) {
        // A fixed per-pixel offset of the steps (interleaved gradient noise; none in time: the upscaler would smear it).
        const float noise = frac(52.9829189f * frac(dot(float2(pixel), float2(0.06711056f, 0.00583715f))));
        const float first = 1.5f / travel_pixels;
        float before = 0.f;
        float hit = -1.f;
        [loop]
        for (uint step = 0u; step < STEPS; ++step) {
          const float t = reach * max(first, (step + noise) / STEPS * (step + noise) / STEPS);
          const float2 at = lerp(start_pixel, end_pixel, t);
          const float scene = DepthAt(int2(at));
          const float ray_z = ViewZ(lerp(start_depth, end_depth, t));
          const float scene_z = ViewZ(scene);
          if (scene < 1.f && ray_z > scene_z && ray_z - scene_z < range.y) {
            hit = t;
            break;
          }
          before = t;
        }
        [branch]
        if (hit >= 0.f) {
          [unroll]
          for (uint refinement = 0u; refinement < REFINEMENTS; ++refinement) {
            const float t = 0.5f * (before + hit);
            const float scene = DepthAt(int2(lerp(start_pixel, end_pixel, t)));
            if (scene < 1.f && ViewZ(lerp(start_depth, end_depth, t)) > ViewZ(scene)) {
              hit = t;
            } else {
              before = t;
            }
          }
          const float2 hit_pixel = lerp(start_pixel, end_pixel, hit);
          const float3 hit_position = RelativePosition(floor(hit_pixel) + 0.5f, DepthAt(int2(hit_pixel)));
          const float4 previous = mul(relative_to_previous, float4(hit_position, 1.f));
          const float2 uv = previous.xy / max(previous.w, 1e-9f) * float2(0.5f, -0.5f) + 0.5f;
          [branch]
          if (previous.w > 0.f && all(uv >= 0.f) && all(uv < 1.f)) {
            const float2 edge = min(uv, 1.f - uv) * 20.f;
            const float share = saturate(min(edge.x, edge.y)) * saturate((1.f - hit) * 5.f);
            const float3 light = t_light.Load(int3(uv * size.xy, 0)).rgb;
            reflection = float4(pow(max(light, 0.f), 1.f / 2.2f), share);
          }
        }
      }
    }
  }
  u_reflection[pixel] = reflection;
}
