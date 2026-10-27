// ssao.hlsl cs50_downsample_depth, written from the disassembly (the game's code): each output texel the farthest of
// the 2 x 2 depth texels at its scaled position. Replaced so the addon's hook keeps its input (the frame's depth) for
// the Visibility Bitmask in the collect.
Texture2D<float> depth_texture : register(t0);
RWTexture2D<float> t_rw_depth : register(u0);

[numthreads(32, 32, 1)]
void main(uint3 coords : SV_DispatchThreadID) {
  uint2 out_dimensions;
  t_rw_depth.GetDimensions(out_dimensions.x, out_dimensions.y);
  float2 in_size;
  depth_texture.GetDimensions(in_size.x, in_size.y);
  const float2 scaled = float2(coords.xy) * (in_size / float2(out_dimensions));
  const uint2 position = uint2(in_size * frac(scaled / in_size));
  float depth = max(0.f, depth_texture.Load(int3(position, 0)));
  depth = max(depth, depth_texture.Load(int3(position + uint2(0, 1), 0)));
  depth = max(depth, depth_texture.Load(int3(position + uint2(1, 0), 0)));
  depth = max(depth, depth_texture.Load(int3(position + uint2(1, 1), 0)));
  t_rw_depth[coords.xy] = depth;
}
