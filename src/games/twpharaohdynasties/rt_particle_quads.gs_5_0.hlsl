/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Pharaoh's smoke shadows (twattila/rt_core.hpp CaptureParticles with a quad shader): the lit particles' vertex shader
// 109B79CE (vs50_main_instancing) draws each particle as an instance of a quad (6 indices, vb0's corners at +-0.5), so its
// stream output gives corners, not particles. This takes each instance's first triangle (SV_PrimitiveID even: two a
// quad) and writes one record (rt_smoke's three float4): the centre and the size of the quad from
// its three corners, colour, and the opacity 3F1B86E1 (ps_main_high_quality) draws at that centre. One record an
// instance, also for particles the vertex shader culled (NaN centre, opacity 0): a record's place in the stream is its
// instance's.
// - The corners' world positions (TEXCOORD4) carry the emitter's depth bias toward the camera (the vertex shader moves
//   view z only, after it projected x, y and w): moved back along the view's z axis until their view-projection w is
//   SV_POSITION's w again.
// - The centre and the uv at corner (0, 0) (TEXCOORD8, the quad's own coordinates) from the triangle's barycentric
//   coordinates; the size from the corners' world spans along the quad's two axes.
// - Opacity as 3F1B86E1 computes its alpha (bytes read, its RDEF slots; E8524BB1's alpha is the same, its views and
//   samplers arrive here shifted to these slots): the diffuse array's alpha at both cells
//   (t6, s3; the emitter's m_diffuse_id) at the mip level its sample takes at that centre (the uv's change across a
//   pixel of the camera's g_viewport_dimensions), sRGB-decoded, times COLOR's w; then the alpha crush (m_behaviour_mask
//   0x100000).
//   Its soft edge (0x200000: against the scene's depth) depends on the view: left out. Only lit emitters cast (0x80000:
//   the pixel shader's sun lighting); fire's glow is the texture's colour above its alpha, so it casts nothing. A
//   particle around the camera (haze kept there) casts none, nor one the vertex shader drew as a mesh (TEXCOORD0's y
//   without 0x200000, its draw flags' quad bit).
// The input is the vertex shader's whole output signature (fxc /dumpbin of the game's bytes).
// CMake's shader rule takes no gs_ target: after a change run from the repository's root
// bin\fxc.exe src\games\twpharaohdynasties\rt_particle_quads.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_particle_quads /Fh src\games\twpharaohdynasties\rt_particle_quads_gs.h
// Troy (its lit particles 563BA3E8 bind the diffuse array, the emitters and their sampler at t7 / t9 / s5): the same with
// /D RT_PARTICLE_DIFFUSE=t7 /D RT_PARTICLE_EMITTERS=t9 /D RT_PARTICLE_SAMPLER=s5 /Fh src\games\twtroy\rt_particle_quads_gs.h

#include "../twattila/rt_particle_quads.hlsli"

struct Corner {
  float4 position : SV_POSITION;  // clip space, z with the depth bias
  float4 colour : COLOR;          // w: the particle's alpha
  uint4 emitter : TEXCOORD0;      // x: the emitter's constants
  float4 uv : TEXCOORD1;          // xy: the diffuse array's
  float4 size : TEXCOORD2;        // w: the particle's random number
  float3 uv_next : TEXCOORD3;     // xy: the next cell's uv, z: the blend to it
  float4 world : TEXCOORD4;       // xyz: the corner with the depth bias, w: the particle's age (0 to 1)
  float4 frame_x : TEXCOORD5;
  float4 frame_y : TEXCOORD6;
  float4 frame_z : TEXCOORD7;
  float3 corner : TEXCOORD8;      // the corner in the quad's own coordinates
};

// 3F1B86E1's EMITTER_CONSTANTS_PS.
struct EmitterConstants {
  uint behaviour;
  uint diffuse;
  uint normal;
  uint draw_flags;
  float4 shadows_and_lighting;
  float4 lighting;
  uint sharp_transition;
  float crush_min;
  float crush_max;
  float crush_variation;
  float4 soft_edge;
};

cbuffer camera : register(b0) {
  // The vertex shader's: [0] the camera's position, [12] view_projection's w row, [13-15] inv_view, [29] g_viewport_dimensions.
  float4 camera[30];
}

#ifndef RT_PARTICLE_DIFFUSE
#define RT_PARTICLE_DIFFUSE t6
#define RT_PARTICLE_EMITTERS t8
#define RT_PARTICLE_SAMPLER s3
#endif
Texture2DArray<float4> diffuse : register(RT_PARTICLE_DIFFUSE);
StructuredBuffer<EmitterConstants> emitters : register(RT_PARTICLE_EMITTERS);
SamplerState diffuse_sampler : register(RT_PARTICLE_SAMPLER);

[maxvertexcount(1)]
void main(triangle Corner corners[3], uint primitive : SV_PrimitiveID, inout PointStream<Record> records) {
  if ((primitive & 1u) != 0u) return;
  const float3 forward = float3(camera[13].z, camera[14].z, camera[15].z);
  const float bias_step = dot(camera[12].xyz, forward);
  float3 world[3];
  [unroll]
  for (uint index = 0u; index < 3u; ++index) {
    const float3 biased = corners[index].world.xyz;
    const float w = dot(camera[12].xyz, biased) + camera[12].w;
    world[index] = biased + forward * ((abs(bias_step) > 1e-6f) ? ((corners[index].position.w - w) / bias_step) : 0.f);
  }
  const float2 along_first = corners[1].corner.xy - corners[0].corner.xy;
  const float2 along_second = corners[2].corner.xy - corners[0].corner.xy;
  const float area = Cross(along_first, along_second);
  float4 axis_x;
  float4 axis_y;
  Axes(float4(world[0], 0.f), float4(world[1], 0.f), float4(world[2], 0.f), along_first, along_second, area, axis_x, axis_y);
  const float2 corner_min = min(min(corners[0].corner.xy, corners[1].corner.xy), corners[2].corner.xy);
  const float2 corner_max = max(max(corners[0].corner.xy, corners[1].corner.xy), corners[2].corner.xy);
  const float2 size = float2(length(axis_x.xyz), length(axis_y.xyz)) * (corner_max - corner_min);
  // Barycentric coordinates of the corner (0, 0).
  const float second = Cross(-corners[0].corner.xy, along_second) / area;
  const float third = Cross(along_first, -corners[0].corner.xy) / area;
  const float first = 1.f - second - third;
  const float3 centre = (world[0] * first) + (world[1] * second) + (world[2] * third);
  // Both cells' uv (xy, zw) and their axes; the pixels' change along the axes from SV_POSITION's x, y and w.
  const float4 uv_corners[3] = {float4(corners[0].uv.xy, corners[0].uv_next.xy), float4(corners[1].uv.xy, corners[1].uv_next.xy),
                                float4(corners[2].uv.xy, corners[2].uv_next.xy)};
  const float4 uvs = (uv_corners[0] * first) + (uv_corners[1] * second) + (uv_corners[2] * third);
  float4 uv_x;
  float4 uv_y;
  Axes(uv_corners[0], uv_corners[1], uv_corners[2], along_first, along_second, area, uv_x, uv_y);
  float4 clip_x;
  float4 clip_y;
  Axes(corners[0].position, corners[1].position, corners[2].position, along_first, along_second, area, clip_x, clip_y);
  const float4 clip = (corners[0].position * first) + (corners[1].position * second) + (corners[2].position * third);
  const float2x2 per_pixel = PerPixel(clip, clip_x, clip_y, 0.5f * camera[29].xy);

  const Corner particle = corners[0];
  const EmitterConstants emitter = emitters[particle.emitter.x];
  float alpha = particle.colour.w;
  if ((emitter.behaviour & 0x10000u) != 0u) {
    uint width = 0u;
    uint height = 0u;
    uint slices = 0u;
    diffuse.GetDimensions(width, height, slices);
    const float2 texels = float2(width, height);
    const float slice = (float)emitter.diffuse;
    const float4 cell = diffuse.SampleLevel(diffuse_sampler, float3(uvs.xy, slice), MipLevel(uv_x.xy, uv_y.xy, per_pixel, texels));
    const float4 next_cell = diffuse.SampleLevel(diffuse_sampler, float3(uvs.zw, slice), MipLevel(uv_x.zw, uv_y.zw, per_pixel, texels));
    const float texture_alpha = lerp(cell.w, next_cell.w, particle.uv_next.z);
    // The pixel shader's literal for 1 / 1.055 (0x3F72A76F; fxc rounds 0.947867298 one bit lower).
    const float decoded = (texture_alpha <= 0.04045f) ? (texture_alpha * 0.0773993805f) : exp2(log2((texture_alpha + 0.055f) * asfloat(0x3F72A76Fu)) * 2.4f);
    alpha = saturate(decoded * particle.colour.w);
  }
  if ((emitter.behaviour & 0x100000u) != 0u) {
    const float2 random = frac(particle.size.w * float2(3276.f, 4099.f));
    const float lowest = max(1.f - emitter.crush_variation, emitter.crush_min);
    const float equal_end = (random.x * (lowest - 1.f)) + 1.f;
    const float varied_end = (random.y * (max((1.f - emitter.crush_variation) * emitter.crush_max, emitter.crush_min) - emitter.crush_max)) + emitter.crush_max;
    const float end = (emitter.crush_min == emitter.crush_max) ? equal_end : varied_end;
    const float threshold = saturate((particle.world.w - emitter.crush_min) / max(end - emitter.crush_min, 0.001f));
    const float eroded = saturate(alpha - threshold) / (1.001f - threshold);
    alpha = (emitter.sharp_transition != 0u) ? (eroded * ((eroded - threshold >= 0.f) ? 1.f : 0.f)) : eroded;
  }
  const bool lit = (emitter.behaviour & 0x80000u) != 0u && (particle.emitter.y & 0x200000u) != 0u;
  const bool around_camera = distance(centre, camera[0].xyz) < 0.5f * max(size.x, size.y);
  const bool placed = abs(area) > 1e-6f && all(isfinite(centre)) && size.x * size.y > 0.f;

  Record record;
  record.centre = float4(placed ? centre : asfloat(0x7FC00000u).xxx, particle.world.w);
  record.colour = float4(particle.colour.xyz, (placed && lit && !around_camera) ? alpha : 0.f);
  record.size = float3(placed ? size : float2(0.f, 0.f), 1.f);
  record.emitter = float4((float)particle.emitter.x, 0.f, 0.f, 0.f);
  records.Append(record);
}
