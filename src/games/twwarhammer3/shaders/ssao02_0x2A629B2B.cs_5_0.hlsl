// fx_ssao collect without depth (written from the disassembly; the one ATN's settings run): the ambient occlusion of
// the line sweeps, each pixel's 4 directions in a 3 x 3 neighbourhood, averaged. The replacement is that code only (Game's
// costs what it did); with AO_BITMASK the addon's own pass (ao_bitmask_collect02, Visibility Bitmask:
// line_sweep_bitmask.hlsli) from the AO's depth, which the addon binds at t2 (the depth downsample's output, this
// frame's); with AO_UPSAMPLE its Half resolution upsample.
#include "../shared.h"

cbuffer cb_ssao : register(b0) {
  float ssao_overscan : packoffset(c0.y);
}

Texture2D<unorm float> t_line_buffer : register(t0);
Texture2D<uint4> t_line_offset_lut : register(t1);
Texture2D<float> ao_depth : register(t2);
RWTexture2D<unorm float> t_rw_ssao : register(u0);

#include "../line_sweep_bitmask.hlsli"

void GameCollect(uint2 coords, uint2 size) {
  if (any(coords >= size)) return;
  float sum = 0.f;
  float count = 0.f;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      const int2 position = int2(coords) + int2(x, y);
      if (any(position < 0) || any(position >= int2(size))) continue;
      const uint4 line_offsets = t_line_offset_lut[position];
      [unroll]
      for (int i = 0; i < 4; ++i) {
        sum += t_line_buffer[uint2(line_offsets[i] & 0xFFFF, line_offsets[i] >> 16)];
      }
      count += 4.f;
    }
  }
  t_rw_ssao[coords] = sum / count;
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
