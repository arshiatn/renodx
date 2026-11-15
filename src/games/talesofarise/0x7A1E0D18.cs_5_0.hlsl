// Ray-traced scene: an alpha-tested material's opacity mask (its t0's alpha, any format) into a slice of the shared
// atlas D3D12 tests ray hits against (ATLAS_SIZE square, bilinear from the mip nearest that size, wrapping as the
// materials sample).
Texture2D<float4> source : register(t0);
SamplerState linear_wrap : register(s0);
RWTexture2DArray<unorm float> atlas : register(u0);

cbuffer Copy : register(b0) {
  uint slice;
  float mip;
  uint size;
  uint copy_padding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= size)) return;
  const float2 uv = (float2(id.xy) + 0.5f) / (float)size;
  atlas[uint3(id.xy, slice)] = source.SampleLevel(linear_wrap, uv, mip).w;
}
