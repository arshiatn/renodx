/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2's smoke shadows' quad shader (twattila/rt_core.hpp CaptureParticles with a quad shader, as Pharaoh's
// rt_particle_quads): particle.hlsl vs particle_vertex_30 (BEA3F2D4) draws each particle as an instance of a quad (6 indices,
// the battle trace's 1536 indices for 256 instances), so its stream output gives corners. This takes each instance's first
// triangle (SV_PrimitiveID even) and writes one record (rt_smoke's three float4): the centre and the size of the quad, its
// colour and the opacity its lit pixel shader (volumetric_particle_alpha 3D0BC5F1: bytes read) draws at
// that centre. The vertex shader's outputs (fxc /dumpbin of BEA3F2D4): TEXCOORD5 the corner in world space (what it projects
// with view_projection), COLOR0 the colour and alpha, TEXCOORD0.xy / TEXCOORD1.xy the two cells' uv, TEXCOORD3.w the blend
// between them, TEXCOORD4.z the alpha scale. The pixel shader's alpha: saturate(TEXCOORD4.z x COLOR0.w x lerp(the texture's
// alpha at both cells)), its texture t0 and sampler s3; its campaign shroud and soft edge (view-dependent) left out. The
// triangle is half of a rectangle: its longest edge the quad's diagonal, the corner across from it the quad's (0, 0), so the
// centre is that edge's middle and the size the other two edges' lengths; the mip level as the pixel shader's sample takes it
// there (twattila/rt_particle_quads.hlsli's helpers on those quad coordinates and camera_VS_PS's g_viewport_dimensions, c38).
// CMake's shader rule takes no gs_ target: after a change run from the repository's root
// bin\fxc.exe src\games\twshogun2completeedition\rt_particle_quads.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_particle_quads /Fh src\games\twshogun2completeedition\rt_particle_quads_gs.h
#include "../twattila/rt_particle_quads.hlsli"

struct Corner {
  float4 position : SV_POSITION;
  float4 colour : COLOR0;
  float4 uv : TEXCOORD0;      // xy: the first cell's
  float4 uv_next : TEXCOORD1;  // xy: the second cell's
  float4 view : TEXCOORD3;    // w: the blend to the second cell
  float4 alpha : TEXCOORD4;   // z: the alpha scale
  float4 world : TEXCOORD5;
};

cbuffer camera_VS_PS : register(b0) {
  float4 camera[39];  // [0] the camera's position, [38] g_viewport_dimensions
}

Texture2D<float4> particle_texture : register(t0);
SamplerState particle_sampler : register(s3);

[maxvertexcount(1)]
void main(triangle Corner corners[3], uint primitive : SV_PrimitiveID, inout PointStream<Record> records) {
  if ((primitive & 1u) != 0u) return;
  // The corner across from the longest edge: the quad's (0, 0); the next two its (1, 0) and (0, 1).
  float longest = -1.f;
  uint right = 0u;
  [unroll]
  for (uint index = 0u; index < 3u; ++index) {
    const float3 edge = corners[(index + 2u) % 3u].world.xyz - corners[(index + 1u) % 3u].world.xyz;
    if (dot(edge, edge) > longest) {
      longest = dot(edge, edge);
      right = index;
    }
  }
  const Corner origin = corners[right];
  const Corner along_x = corners[(right + 1u) % 3u];
  const Corner along_y = corners[(right + 2u) % 3u];
  const float3 centre = 0.5f * (along_x.world.xyz + along_y.world.xyz);
  const float2 size = float2(distance(along_x.world.xyz, origin.world.xyz), distance(along_y.world.xyz, origin.world.xyz));
  const float4 uvs = 0.5f * (float4(along_x.uv.xy, along_x.uv_next.xy) + float4(along_y.uv.xy, along_y.uv_next.xy));
  const float4 uv_x = float4(along_x.uv.xy, along_x.uv_next.xy) - float4(origin.uv.xy, origin.uv_next.xy);
  const float4 uv_y = float4(along_y.uv.xy, along_y.uv_next.xy) - float4(origin.uv.xy, origin.uv_next.xy);
  const float4 clip = 0.5f * (along_x.position + along_y.position);
  const float2x2 per_pixel = PerPixel(clip, along_x.position - origin.position, along_y.position - origin.position, 0.5f * camera[38].xy);
  uint width = 0u;
  uint height = 0u;
  particle_texture.GetDimensions(width, height);
  const float2 texels = float2(width, height);
  const float cell = particle_texture.SampleLevel(particle_sampler, uvs.xy, MipLevel(uv_x.xy, uv_y.xy, per_pixel, texels)).w;
  const float next_cell = particle_texture.SampleLevel(particle_sampler, uvs.zw, MipLevel(uv_x.zw, uv_y.zw, per_pixel, texels)).w;
  const float alpha = saturate(origin.alpha.z * (origin.colour.w * lerp(cell, next_cell, origin.view.w)));
  const bool around_camera = distance(centre, camera[0].xyz) < 0.5f * max(size.x, size.y);
  const bool placed = all(isfinite(centre)) && size.x * size.y > 0.f;

  Record record;
  record.centre = float4(placed ? centre : asfloat(0x7FC00000u).xxx, 0.f);
  record.colour = float4(origin.colour.xyz, (placed && !around_camera) ? alpha : 0.f);
  record.size = float3(placed ? size : float2(0.f, 0.f), 1.f);
  record.emitter = 0.f;
  records.Append(record);
}
