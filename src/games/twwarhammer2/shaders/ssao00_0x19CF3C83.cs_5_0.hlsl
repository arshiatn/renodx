// ssao.hlsl cs50_collect, written from the disassembly: the ambient occlusion of the 36 line-sweep directions (6 x 6
// interleaved per pixel) averaged; the blur reads every second pixel of it (at the settings where a blur runs: not at
// every one, ATN's AO trace). The game's code only: with the Visibility Bitmask the addon dispatches its own collect
// instead (ao_bitmask_collect, line_sweep_ao.hpp), so Game's ambient occlusion costs what it did.
#include "../shared.h"

// The game's names: depth_texture is the sweep's occlusion here.
Texture2D<float> depth_texture : register(t0);
RWTexture2D<unorm float> t_rw_ssao : register(u0);

[numthreads(32, 32, 1)]
void main(uint3 coords : SV_DispatchThreadID) {
  uint2 out_size;
  t_rw_ssao.GetDimensions(out_size.x, out_size.y);
  if (any(coords.xy >= out_size * 2u)) return;
  float2 in_size;
  depth_texture.GetDimensions(in_size.x, in_size.y);
  float2 position = float2(coords.xy << 1u) * (in_size / float2(out_size) * 0.5f);
  position = min(max(position, 16.f), float2(out_size * 2u) - 17.f) + float2(0.5f, -5.5f);
  float occlusion = 0.f;
  for (float y = 0.f; y < 6.f; y += 1.f) {
    for (float x = 0.f; x < 6.f; x += 1.f) {
      occlusion += depth_texture.Load(int3(uint2(position + float2(x, y)), 0));
    }
  }
  t_rw_ssao[coords.xy] = occlusion * 0.027777778f;
}
