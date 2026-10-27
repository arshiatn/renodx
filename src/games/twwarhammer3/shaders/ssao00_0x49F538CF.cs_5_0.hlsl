// fx_ssao depth downsample (written from the disassembly): the AO's depth, 2 x 2 max, over the AO's overscan area.
// Replaced so the AO trace (Advanced) can hook it: hooks on passes without a replacement never run.
cbuffer cb_ssao : register(b0) {
  float warscape_3d_overscan : packoffset(c0.x);
  float ssao_overscan : packoffset(c0.y);
}

Texture2D<float> t_depth : register(t0);
RWTexture2D<float> t_rw_depth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 coords : SV_DispatchThreadID) {
  float2 out_size;
  t_rw_depth.GetDimensions(out_size.x, out_size.y);
  float2 in_size;
  t_depth.GetDimensions(in_size.x, in_size.y);
  const float2 source = in_size * warscape_3d_overscan;
  const float2 position = fmod(float2(coords.xy) * (source / (out_size * ssao_overscan)), source);
  float depth = max(t_depth.Load(int3(uint2(position), 0)), 0.f);
  depth = max(t_depth.Load(int3(uint2(position + float2(0.f, 1.f)), 0)), depth);
  depth = max(t_depth.Load(int3(uint2(position + float2(1.f, 0.f)), 0)), depth);
  depth = max(t_depth.Load(int3(uint2(position + float2(1.f, 1.f)), 0)), depth);
  t_rw_depth[coords.xy] = depth;
}
