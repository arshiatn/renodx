/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's ray tracing scene: the terrain's triangles its pixel shaders would keep. fx_rigidterraintessellation's
// PS (4A87AE19, BB65FB61) discard by t_tile_mask_map where the map's own meshes take over the ground (fx_rigidterrainblend),
// and there the height map may lie over those meshes: in the scene it covered them, black patches in every ray. A stream
// output runs no pixel shader, so this tests each triangle's centre as they test a pixel. Bound with the domain shader's
// tessellation_cb and the pixel shader's mask (rt_core.hpp Capture), its one output streamed out in place of the domain shader's.
// CMake's shader rule takes no gs_ target: after a change run in this folder
// bin\fxc.exe rt_terrain_keep.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_terrain_keep /Fh rt_terrain_keep_gs.h
struct Corner {
  float4 position : SV_Position;
  float4 world : TEXCOORD0;
  float view_depth : TEXCOORD1;
  float2 original : ORIGINAL_POSITION;
};

struct Output {
  float4 position : SV_Position;
};

Texture2D<float4> t_tile_mask_map : register(t0);

cbuffer tessellation_cb : register(b1) {
  float4 before_bounds[5];
  float4 g_full_world_bounds;
  float4 g_terrain_decal_bounds;
  uint g_height_pass;
  uint g_clip_pass;
};

[maxvertexcount(3)]
void main(triangle Corner corners[3], inout TriangleStream<Output> stream) {
  const float2 centre = (corners[0].world.xz + corners[1].world.xz + corners[2].world.xz) / 3.f;
  uint2 mask_size;
  t_tile_mask_map.GetDimensions(mask_size.x, mask_size.y);
  const float2 uv = saturate((centre - g_full_world_bounds.xy) / (g_full_world_bounds.zw - g_full_world_bounds.xy));
  const uint texel = uint(round(t_tile_mask_map.Load(int3(uint2(uv * mask_size), 0)).x * 255.f));
  if ((texel & (g_clip_pass != 0u ? 16u : 32u)) == 0u) return;
  for (uint corner = 0u; corner < 3u; ++corner) {
    Output output;
    output.position = corners[corner].position;
    stream.Append(output);
  }
}
