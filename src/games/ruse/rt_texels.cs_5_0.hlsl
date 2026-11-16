/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// R.U.S.E.'s ray-traced results as the patched shaders read them by screen position (ray_tracing.hpp), in the shaders'
// own gamma 2.2 units:
// - the Light Bounce (the helper's bounce texels: the light arriving, its distance from the camera) into the s14 texel
//   of the terrain and object shaders: x what their ambient keeps (the occlusion; 0 with Path-Traced Ambient, whose
//   paths bring the sky in its place), yzw the light; each pixel takes the texel near it at its own distance;
// - with REFLECTION_TEXELS the helper's reflections (the light, the share of rays that met a surface) into the water
//   shaders' s13 texel (its share blends it over their cube map).
#ifdef REFLECTION_TEXELS
Texture2D<float4> t_reflection : register(t0);
#else
Texture2D<float4> t_bounce : register(t0);
Texture2D<float4> t_positions : register(t1);  // the helper's world positions, w their distance from the camera (0: the sky)
Texture2D<float> t_occlusion : register(t2);   // the ambient occlusion, the rays' or else GT-VBAO's (flags.y 1)
#endif
RWTexture2D<float4> u_texel : register(u0);

cbuffer texel_settings : register(b0) {
  float4 strength;  // x: the light's
  uint4 texels;   // bounce width, height, scale; render width
  uint4 flags;    // x 1: the ambient replaced; y 1: an occlusion in t2; z render height
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
#ifdef REFLECTION_TEXELS
  uint2 size;
  u_texel.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float4 reflection = t_reflection.Load(int3(id.xy, 0));
  u_texel[id.xy] = float4(pow(max(reflection.rgb, 0.f), 1.f / 2.2f), saturate(reflection.a));
#else
  if (id.x >= texels.w || id.y >= flags.z) return;
  const float4 position = t_positions.Load(int3(id.xy, 0));
  const float occlusion = (flags.y != 0u) ? t_occlusion.Load(int3(id.xy, 0)) : 1.f;
  float3 light = 0.f;
  if (position.w > 0.f) {
    // The four texels around the pixel's place in the bounce, the one whose distance is nearest its own.
    const float distance_here = position.w;
    const int2 nearest = int2((id.xy + 0.5f) / texels.z - 0.5f);
    float closest = 3.402823466e+38f;
    [unroll]
    for (int y = 0; y < 2; ++y) {
      [unroll]
      for (int x = 0; x < 2; ++x) {
        const int2 texel = clamp(nearest + int2(x, y), 0, int2(texels.xy) - 1);
        const float4 bounce = t_bounce.Load(int3(texel, 0));
        const float apart = abs(bounce.w - distance_here);
        if (bounce.w > 0.f && apart < closest) {
          closest = apart;
          light = bounce.rgb;
        }
      }
    }
  }
  u_texel[id.xy] = float4((flags.x != 0u) ? 0.f : occlusion, pow(max(light, 0.f), 1.f / 2.2f) * strength.x);
#endif
}
