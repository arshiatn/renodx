// fx_ssao collect with depth (written from the disassembly): the ambient occlusion of the line sweeps, each pixel's 4
// directions in a 3 x 3 neighbourhood, weighted by depth. The replacement is that code only (Game's costs what it did);
// with AO_BITMASK the addon's own pass (ao_bitmask_collect01, Visibility Bitmask: line_sweep_bitmask.hlsli), with
// AO_UPSAMPLE its Half resolution upsample.
#include "../shared.h"

cbuffer cb_ssao : register(b0) {
  float ssao_overscan : packoffset(c0.y);
}

Texture2D<float> ao_depth : register(t0);  // the game's t_depth
Texture2D<unorm float> t_line_buffer : register(t1);
Texture2D<uint4> t_line_offset_lut : register(t2);
Texture2D<uint4> t_line_coords_lut : register(t3);
RWTexture2D<unorm float> t_rw_ssao : register(u0);

#include "../line_sweep_bitmask.hlsli"

void GameCollect(uint2 coords, uint2 size) {
  if (any(coords >= size)) return;
  const float center_depth = ao_depth[coords];
  float sum = 0.f;
  float sum_weight = 0.f;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      const int2 position = int2(coords) + int2(x, y);
      if (any(position < 0) || any(position >= int2(size))) continue;
      const uint4 line_offsets = t_line_offset_lut[position];
      const uint4 line_coords = t_line_coords_lut[position];
      [unroll]
      for (int i = 0; i < 4; ++i) {
        const uint2 offset = uint2(line_offsets[i] & 0xFFFF, line_offsets[i] >> 16);
        const uint2 depth_position = uint2(position) - 128u + uint2(line_coords[i] & 0xFF, line_coords[i] >> 8);
        const float weight = WeightDepth(center_depth, ao_depth[depth_position]);
        sum += weight * t_line_buffer[offset];
        sum_weight += weight;
      }
    }
  }
  t_rw_ssao[coords] = sum / sum_weight;
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 coords : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID,
          uint thread_index : SV_GroupIndex) {
  uint2 lut_size;
  t_line_offset_lut.GetDimensions(lut_size.x, lut_size.y);
  const uint2 size = uint2(float2(lut_size) * ssao_overscan);
#if defined(AO_UPSAMPLE)
  BitmaskUpsample(coords.xy, size);
#elif defined(AO_BITMASK)
  BitmaskCollect(coords.xy, group.xy, thread.xy, thread_index, size);
#else
  GameCollect(coords.xy, size);
#endif
}
