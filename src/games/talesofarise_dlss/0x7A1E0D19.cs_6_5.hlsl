// Ray-traced scene, after this frame's vertices reach D3D12: per captured triangle its normal into the normal pool, an
// alpha-tested one's three UVs into the UV pool, and each capture's record (the first triangle's thread): where its
// UVs and normals are, its opacity mask's atlas slice and flags, its alpha threshold (from 0x7A1E0D11). Captures
// without a place (normal offset ~0) are left out.
#include "./rt_scene.hlsli"

Buffer<float4> vertex_positions : register(t11);  // this frame's vertices (w: the UV as two halves)
Texture2D<float4> capture_params : register(t12);  // per capture: x its alpha threshold
// Per capture: first vertex, vertex count, normal offset, UV offset; record, atlas slice | flags << 16, 0, 0.
StructuredBuffer<uint4> capture_table : register(t16);
RWBuffer<uint4> records_written : register(u9);
RWBuffer<uint> uv_pool_written : register(u10);
RWBuffer<uint> normal_pool_written : register(u11);

cbuffer Dispatch : register(b1) {
  uint capture_count;
  uint triangle_count;
  uint dispatch_padding0;
  uint dispatch_padding1;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint triangle_index = id.x;
  if (triangle_index >= triangle_count) return;
  const uint vertex = triangle_index * 3u;
  uint low = 0u;
  uint high = capture_count - 1u;
  while (low < high) {
    const uint middle = (low + high + 1u) / 2u;
    if (capture_table[middle * 2u].x <= vertex) {
      low = middle;
    } else {
      high = middle - 1u;
    }
  }
  const uint4 capture = capture_table[low * 2u];
  const uint4 placement = capture_table[low * 2u + 1u];
  // A capture without a place this frame (no room): left out.
  if (capture.z == 0xFFFFFFFFu) return;
  const uint local = (vertex - capture.x) / 3u;
  const float4 a = vertex_positions[vertex];
  const float4 b = vertex_positions[vertex + 1u];
  const float4 c = vertex_positions[vertex + 2u];
  const float3 face = cross(b.xyz - a.xyz, c.xyz - a.xyz);
  const float face_length = length(face);
  normal_pool_written[capture.z + local] = (face_length > 0.f) ? OctahedralEncode(face / face_length) : OctahedralEncode(float3(0.f, 0.f, 1.f));
  const bool masked = ((placement.y >> 16) & RECORD_MASKED) != 0u;
  if (masked) {
    uv_pool_written[capture.w + local * 3u] = asuint(a.w);
    uv_pool_written[capture.w + local * 3u + 1u] = asuint(b.w);
    uv_pool_written[capture.w + local * 3u + 2u] = asuint(c.w);
  }
  if (local == 0u) {
    records_written[placement.x] = uint4(capture.w, capture.z, placement.y, masked ? asuint(capture_params[uint2(low, 0)].x) : 0u);
  }
}
