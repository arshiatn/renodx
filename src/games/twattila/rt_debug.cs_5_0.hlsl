/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The ray tracing's Debug View (rt_core.hpp DrawDebugView): an effect's result this frame as the picture, into u0
// (copied over the tone mapper's output); All (9) tiles the eight in a 3 x 3 grid, in their order. Sun Shadows (1),
// Ambient Occlusion (2), Specular Occlusion (5), Fog Shadows (7) and Screen-Space AO (8: the game's AO pass, its or
// GT-VBAO's): white open or lit, black blocked. Light Bounce (3) and Reflections (4): their light (x / (1 + x)), the
// Light Bounce's sky dark blue. Point Light Shadows (6): redder the more of the traced lights are blocked. Magenta: the
// effect made nothing this frame (off, or not traced).
Texture2D<float> sun_visibility : register(t0);
Texture2D<float> ambient : register(t1);
Texture2D<float4> bounce : register(t2);
Texture2D<float4> reflection : register(t3);
Texture2D<float> specular : register(t4);
Texture2D<uint> light_mask : register(t5);
Texture2D<float> fog : register(t6);
Texture2D<float4> screen_space_ambient : register(t7);  // its own size, the view's share of it (its AO in red)
RWTexture2D<float4> picture : register(u0);

cbuffer rt_debug : register(b0)
{
  uint4 rt_debug;  // x the view, y bit v - 1: view v made this frame, z the traced lights, w the Light Bounce's texel (render pixels a side)
  float4 size;     // xy the render size, zw the picture's
#ifdef RT_DEBUG_AO_MAPPING
  float4 ao_mapping;  // x how much further the sun draws read the AO texture than a pixel's share of the picture
#endif
}

static const uint VIEWS = 8u;
static const uint ALL = 9u;

// A view's colour at a render pixel.
float3 ViewColour(uint view, uint2 render)
{
  if ((rt_debug.y & (1u << (view - 1u))) == 0u) return float3(0.6f, 0.f, 0.6f);
  switch (view) {
    case 1u: return 0.8f * sun_visibility.Load(int3(render, 0));
    case 2u: return 0.8f * ambient.Load(int3(render, 0));
    case 3u: {
      const float4 light = bounce.Load(int3(render / max(rt_debug.w, 1u), 0));
      return (light.a > 0.f) ? light.rgb / (1.f + light.rgb) : float3(0.f, 0.f, 0.15f);
    }
    case 4u: {
      const float3 reflected = reflection.Load(int3(render, 0)).rgb;
      return reflected / (1.f + reflected);
    }
    case 5u: return 0.8f * specular.Load(int3(render, 0));
    case 6u: {
      const uint traced = (rt_debug.z >= 32u) ? 0xFFFFFFFFu : ((1u << rt_debug.z) - 1u);
      const float blocked = (float)countbits(~light_mask.Load(int3(render, 0)) & traced);
      return lerp(float3(0.8f, 0.8f, 0.8f), float3(0.8f, 0.f, 0.f), saturate(blocked / 3.f));
    }
    case 7u: return 0.8f * fog.Load(int3(render / 4u, 0));
    default: {
      // Its texture is a share of the picture's size (the game's AO at a fraction of it), the view at its top-left.
      uint2 dimensions;
      screen_space_ambient.GetDimensions(dimensions.x, dimensions.y);
#ifdef RT_DEBUG_AO_MAPPING
      const uint2 texel = min(uint2((float2(render) + 0.5f) * ao_mapping.x * float2(dimensions) / size.zw), dimensions - 1u);
#else
      const uint2 texel = min(uint2((float2(render) + 0.5f) * float2(dimensions) / size.zw), dimensions - 1u);
#endif
      return 0.8f * screen_space_ambient.Load(int3(texel, 0)).r;
    }
  }
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(float2(pixel) >= size.zw)) return;
  // The results are at the render size, top-left; a tile shows all of it.
  uint view = rt_debug.x;
  float2 area = size.zw;
  float2 local = float2(pixel) + 0.5f;
  if (view == ALL) {
    area = size.zw / 3.f;
    const uint2 tile = min(uint2(local / area), 2u);
    view = tile.y * 3u + tile.x + 1u;
    local -= float2(tile) * area;
  }
  const uint2 render = min(uint2(local * size.xy / area), uint2(size.xy) - 1u);
  picture[pixel] = float4((view <= VIEWS) ? ViewColour(view, render) : 0.f, 1.f);
}
