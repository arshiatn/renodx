// Ray-traced scene, DXR 1.2: an alpha-tested mesh's opacity micromaps, one per triangle at `level` (4^level
// micro-triangles, 2 bits each: transparent 0, opaque 1, unknown 3), from its UVs and opacity mask (seven samples per
// micro-triangle: corners, edge middles, centre; all solid opaque, none solid transparent, else unknown and tested by
// the ray queries). Micro-triangles follow DXR's "bird curve" order (index -> barycentrics as below; omm_test.cpp
// checks it against the hardware). A triangle that is wholly opaque or wholly transparent gets the special index
// instead (no micromap is read for it).
#include "./rt_scene.hlsli"

RWByteAddressBuffer omm_data : register(u12);       // this mesh's micromaps from its start
RWStructuredBuffer<uint> omm_indices : register(u13);  // this mesh's, per triangle
RWStructuredBuffer<uint2> omm_descs : register(u21);   // per micromap: byte offset, level | format << 16

cbuffer Bake : register(b1) {
  uint record;
  uint triangle_count;
  uint level;
  uint bake_padding;
};

static const uint STATE_TRANSPARENT = 0u;
static const uint STATE_OPAQUE = 1u;
static const uint STATE_UNKNOWN = 3u;
static const uint FORMAT_4_STATE = 2u;
static const uint SPECIAL_TRANSPARENT = 0xFFFFFFFFu;  // -1
static const uint SPECIAL_OPAQUE = 0xFFFFFFFEu;       // -2

uint ExtractEvenBits(uint x) {
  x &= 0x55555555u;
  x = (x | (x >> 1)) & 0x33333333u;
  x = (x | (x >> 2)) & 0x0F0F0F0Fu;
  x = (x | (x >> 4)) & 0x00FF00FFu;
  x = (x | (x >> 8)) & 0x0000FFFFu;
  return x;
}

uint PrefixEor(uint x) {
  x ^= x >> 1;
  x ^= x >> 2;
  x ^= x >> 4;
  x ^= x >> 8;
  return x;
}

// The micro-triangle `index`'s corners as barycentrics (x: vertex 1's weight, y: vertex 2's).
void MicroTriangle(uint index, out float2 corner0, out float2 corner1, out float2 corner2) {
  const uint b0 = ExtractEvenBits(index);
  const uint b1 = ExtractEvenBits(index >> 1);
  const uint fx = PrefixEor(b0);
  const uint fy = PrefixEor(b0 & ~b1);
  const uint t = fy ^ b1;
  const uint mask = (1u << level) - 1u;
  uint u = ((fx & ~t) | (b0 & ~t) | (~b0 & ~fx & t)) & mask;
  uint v = (fy ^ b0) & mask;
  const uint w = ((~fx & ~t) | (b0 & ~t) | (~b0 & fx & t)) & mask;
  const bool upright = ((u ^ v ^ w) & 1u) != 0u;
  if (!upright) {
    u += 1u;
    v += 1u;
  }
  const float scale = 1.f / (float)(1u << level);
  const float step = upright ? scale : -scale;
  corner0 = float2(u, v) * scale;
  corner1 = corner0 + float2(step, 0.f);
  corner2 = corner0 + float2(0.f, step);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint triangle_index = id.x;
  if (triangle_index >= triangle_count) return;
  const uint4 mesh = records[record];
  const uint first = mesh.x + triangle_index * 3u;
  const float2 uv0 = UnpackUv(uv_pool[first]);
  const float2 uv1 = UnpackUv(uv_pool[first + 1u]);
  const float2 uv2 = UnpackUv(uv_pool[first + 2u]);
  const float slice = (float)(mesh.z & 0xFFFFu);
  const float threshold = asfloat(mesh.w);
  const uint micro_count = 1u << (2u * level);
  const uint words = max(micro_count / 16u, 1u);
  const uint byte_offset = triangle_index * words * 4u;
  bool all_opaque = true;
  bool all_transparent = true;
  for (uint word_index = 0u; word_index < words; ++word_index) {
    uint word = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
      const uint micro = word_index * 16u + bit;
      if (micro >= micro_count) break;
      float2 corner0, corner1, corner2;
      MicroTriangle(micro, corner0, corner1, corner2);
      const float2 points[7] = {corner0, corner1, corner2, (corner0 + corner1) * 0.5f, (corner1 + corner2) * 0.5f,
                                (corner2 + corner0) * 0.5f, (corner0 + corner1 + corner2) / 3.f};
      uint solid = 0u;
      [unroll]
      for (uint corner = 0u; corner < 7u; ++corner) {
        const float2 uv = uv0 * (1.f - points[corner].x - points[corner].y) + uv1 * points[corner].x + uv2 * points[corner].y;
        solid += (alpha_atlas.SampleLevel(alpha_sampler, float3(uv, slice), 0.f) >= threshold) ? 1u : 0u;
      }
      const uint state = (solid == 7u) ? STATE_OPAQUE : ((solid == 0u) ? STATE_TRANSPARENT : STATE_UNKNOWN);
      all_opaque = all_opaque && state == STATE_OPAQUE;
      all_transparent = all_transparent && state == STATE_TRANSPARENT;
      word |= state << (bit * 2u);
    }
    omm_data.Store(byte_offset + word_index * 4u, word);
  }
  omm_descs[triangle_index] = uint2(byte_offset, level | (FORMAT_4_STATE << 16));
  omm_indices[triangle_index] = all_opaque ? SPECIAL_OPAQUE : (all_transparent ? SPECIAL_TRANSPARENT : triangle_index);
}
