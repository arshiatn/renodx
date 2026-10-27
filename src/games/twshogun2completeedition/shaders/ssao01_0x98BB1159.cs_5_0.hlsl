// ssao_3k.hlsl cs50_new_collect: the ambient occlusion from the line sweeps (each pixel's 4 directions in a 3 x 3
// neighbourhood, weighted by depth), written from the game's source. The game's code only: with the Visibility Bitmask
// the addon dispatches its own shader instead (ao_bitmask_collect, twwarhammer2/line_sweep_ao.hpp), so Game's ambient
// occlusion costs what it did.
#include "../shared.h"

Texture2D<float> gbuffer_depth_texture : register(t0);
Texture2D<unorm float> t_line_buffer : register(t1);
Texture2D<uint4> t_line_offset_lut : register(t2);
Texture2D<uint4> t_line_coords_lut : register(t3);
RWTexture2D<unorm float> t_rw_ssao : register(u0);

float WeightDepth(float center_depth, float depth) {
  return 1.f / (1.f + 1000.f * abs(center_depth - depth));
}

void GameCollect(uint2 coords) {
  uint width, height;
  t_line_offset_lut.GetDimensions(width, height);
  if (coords.x >= width || coords.y >= height) return;
  const float center_depth = gbuffer_depth_texture[coords];
  float sum = 0.f;
  float sum_weight = 0.f;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      const int2 coords2 = int2(coords) + int2(x, y);
      if (any(coords2 < 0) || any(coords2 >= int2(width, height))) continue;
      const uint4 line_offsets = t_line_offset_lut[coords2];
      const uint4 line_coords = t_line_coords_lut[coords2];
      [unroll]
      for (int i = 0; i < 4; ++i) {
        const uint2 offset = uint2(line_offsets[i] & 0xFFFF, line_offsets[i] >> 16);
        const uint2 sample_coords = uint2(coords2) + uint2(line_coords[i] & 0xFF, line_coords[i] >> 8) - uint2(128, 128);
        const float weight = WeightDepth(center_depth, gbuffer_depth_texture[sample_coords]);
        sum += weight * t_line_buffer[offset];
        sum_weight += weight;
      }
    }
  }
  t_rw_ssao[coords] = sum / sum_weight;
}

[numthreads(32, 32, 1)]
void main(uint3 coords : SV_DispatchThreadID) {
  GameCollect(coords.xy);
}
