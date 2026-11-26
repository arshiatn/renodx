/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// R.U.S.E.'s guides for the helper's rays (ray_tracing.hpp): the main pass's guide targets of last frame (the patched
// shaders' oC1-oC3: albedo, world normal, the water's surface from the eye) where this frame's start depth was then, into the
// helper's guides (world normals, linear diffuse albedo, roughness: the water a mirror, the rest out of the reflections),
// and the water's surface into the start depth (the water's draws write no depth: B0 trace, 64 of its 68 draws).
Texture2D<float4> t_albedo : register(t0);  // oC1: albedo (the game's gamma 2.2), blended as the scene
Texture2D<float4> t_normal : register(t1);  // oC2: world normal, blended as the scene
Texture2D<float4> t_water : register(t2);   // oC3: the water's surface from last frame's eye / 1024, w 1 where it drew
Texture2D<float> t_kept : register(t3);     // last frame's main depth
RWTexture2D<uint> u_start : register(u0);   // this frame's start depth (asuint)
RWTexture2D<unorm float4> u_albedo : register(u1);
RWTexture2D<float4> u_normals : register(u2);
RWTexture2D<unorm float> u_roughness : register(u3);

cbuffer guide_settings : register(b0) {
  row_major float4x4 to_previous;  // this frame's clip to last frame's
  row_major float4x4 to_clip;      // an offset from this frame's eye to this frame's clip
  float4 eye;                      // last frame's eye less this frame's (world units)
  float4 size;                     // width, height, 1 / width, 1 / height
  float4 projection;               // P00, P11, A, B: the depth is A + B / view z
};

static const float SAME_SURFACE = 0.02f;  // of the view depth: last frame's pixel saw the same surface
static const float UNKNOWN_ALBEDO = 0.35f;  // the helper's UNSEEN_ALBEDO
static const float WATER_ROUGHNESS = 0.02f;  // a mirror (the helper's below 0.05)

float ViewDepth(float depth) {
  return projection.w / (depth - projection.z);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint2 pixel = id.xy;
  if (any(pixel >= (uint2)size.xy)) return;
  const float depth = asfloat(u_start[pixel]);
  float3 albedo = UNKNOWN_ALBEDO;
  float3 normal = 0.f;
  float roughness = 1.f;
  if (depth < 1.f) {
    const float2 ndc = float2((pixel.x + 0.5f) * size.z * 2.f - 1.f, 1.f - (pixel.y + 0.5f) * size.w * 2.f);
    const float4 previous = mul(to_previous, float4(ndc, depth, 1.f));
    const float2 uv = previous.xy / previous.w * float2(0.5f, -0.5f) + 0.5f;
    if (previous.w > 0.f && all(uv >= 0.f) && all(uv < 1.f)) {
      const int3 seen = int3(uv * size.xy, 0);
      const float expected = ViewDepth(previous.z / previous.w);
      if (abs(ViewDepth(t_kept.Load(seen)) - expected) <= SAME_SURFACE * abs(expected)) {
        const float4 guide_normal = t_normal.Load(seen);
        const float length_normal = length(guide_normal.xyz);
        if (length_normal > 0.25006637f) {
          normal = guide_normal.xyz / length_normal;
          albedo = pow(saturate(t_albedo.Load(seen).rgb), 2.2f);
        }
        // The water's surface over the riverbed this pixel shows: its depth in this frame's view, a mirror there.
        const float4 water = t_water.Load(seen);
        if (water.w > 0.5f) {
          const float4 surface = mul(to_clip, float4(water.xyz / water.w * 1024.f + eye.xyz, 1.f));
          const float surface_depth = surface.z / surface.w;
          if (surface.w > 0.f && surface_depth < depth) u_start[pixel] = asuint(surface_depth);
          roughness = WATER_ROUGHNESS;
        }
      }
    }
  }
  u_albedo[pixel] = float4(albedo, 1.f);
  u_normals[pixel] = float4(normal, 0.f);
  u_roughness[pixel] = roughness;
}
