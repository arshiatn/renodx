/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The quad shaders' common part (rt_core.hpp CaptureParticles, SmokeVertexShader::quad_shader: Pharaoh's and Rome II's
// rt_particle_quads): the record they stream out and the geometry of a quad's triangle.

#ifndef SRC_GAMES_TWATTILA_RT_PARTICLE_QUADS_HLSLI_
#define SRC_GAMES_TWATTILA_RT_PARTICLE_QUADS_HLSLI_

// rt_core.hpp's stream output declaration: rt_smoke's centre and age, colour and opacity, size and scale, emitter.
struct Record {
  float4 centre : SV_POSITION;
  float4 colour : COLOR;
  float3 size : TEXCOORD3;
  float4 emitter : TEXCOORD0;  // x (a register of its own: the declaration takes its first component)
};

float Cross(float2 a, float2 b) {
  return (a.x * b.y) - (a.y * b.x);
}

// What one unit of the corner's x and y moves a value that is affine in the corner, from its values at the three corners
// (`along_first`, `along_second`: the corners' offsets from the first, `area` their cross product).
void Axes(float4 first, float4 second, float4 third, float2 along_first, float2 along_second, float area, out float4 axis_x, out float4 axis_y) {
  axis_x = (((second - first) * along_second.y) - ((third - first) * along_first.y)) / area;
  axis_y = (((third - first) * along_first.x) - ((second - first) * along_second.x)) / area;
}

// The inverse of the pixels' change along the corner's axes, from clip space's at the centre (`clip`) and its change along
// them (`clip_x`, `clip_y`; Axes), on a viewport twice `half_viewport`.
float2x2 PerPixel(float4 clip, float4 clip_x, float4 clip_y, float2 half_viewport) {
  const float2 pixels_x = ((clip_x.xy * clip.w) - (clip.xy * clip_x.w)) / (clip.w * clip.w) * half_viewport;
  const float2 pixels_y = ((clip_y.xy * clip.w) - (clip.xy * clip_y.w)) / (clip.w * clip.w) * half_viewport;
  return float2x2(pixels_y.y, -pixels_y.x, -pixels_x.y, pixels_x.x) / Cross(pixels_x, pixels_y);
}

// The mip level of a sample at the quad's centre, as the pixel shader's own (D3D's isotropic level): the uv's change
// across one pixel, `uv_x` and `uv_y` its change along the corner's axes, `per_pixel` the inverse of the pixels' change.
float MipLevel(float2 uv_x, float2 uv_y, float2x2 per_pixel, float2 texels) {
  const float2 across = ((uv_x * per_pixel[0][0]) + (uv_y * per_pixel[1][0])) * texels;
  const float2 down = ((uv_x * per_pixel[0][1]) + (uv_y * per_pixel[1][1])) * texels;
  return log2(max(length(across), length(down)));
}

#endif  // SRC_GAMES_TWATTILA_RT_PARTICLE_QUADS_HLSLI_
