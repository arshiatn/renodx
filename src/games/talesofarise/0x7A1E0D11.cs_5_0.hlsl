// Ray-traced scene: the depth prepass's stream output (clip positions, three per triangle; alpha-tested draws also
// their UV) to world positions in the texture D3D12 copies its BLAS inputs from (4096 per row; w the UV as two halves,
// scaled as the material scales it), for one run of captures drawn with the same View. Each alpha-tested capture's
// first vertex also writes its material's alpha threshold (0.3333 / the material's opacity factor) for D3D12.
ByteAddressBuffer captured : register(t0);
RWTexture2D<float4> vertices : register(u0);
RWTexture2D<float4> capture_params : register(u1);

cbuffer Run : register(b0) {
  uint first_capture;  // this run's captures in the table
  uint capture_count;
  uint run_first_vertex;
  uint run_vertex_count;
};

// UE 4.21's View uniform buffer the draws used: ClipToTranslatedWorld c36-c39, PreViewTranslation c62.
cbuffer View : register(b1) {
  float4 view[63];
};

// Per capture: its first output vertex, vertex count, byte offset in the stream output and stride; flags (CAPTURE_*),
// 0, 0, 0.
cbuffer Captures : register(b2) {
  uint4 capture_table[512];
};

// Per capture: its material's UV scale register and opacity factor register (copied from the draw's pixel shader).
cbuffer Materials : register(b3) {
  float4 material_rows[512];
};

static const uint CAPTURE_MASKED = 1u;
static const uint CAPTURE_UV_SCALE = 2u;    // bits 2-3: u's component, 4-5: v's
static const uint CAPTURE_ALPHA_SCALE = 64u;  // bits 7-8: the factor's component
static const float ALPHA_CUTOFF = 0.3333f;    // UE's opacity mask clip value in every masked prepass shader

[numthreads(256, 1, 1)]
void main(uint id : SV_DispatchThreadID) {
  if (id >= run_vertex_count) return;
  const uint vertex = run_first_vertex + id;
  // The capture this vertex belongs to (captures are in output order).
  uint low = first_capture;
  uint high = first_capture + capture_count - 1u;
  while (low < high) {
    const uint middle = (low + high + 1u) / 2u;
    if (capture_table[middle * 2u].x <= vertex) {
      low = middle;
    } else {
      high = middle - 1u;
    }
  }
  const uint4 capture = capture_table[low * 2u];
  const uint capture_flags = capture_table[low * 2u + 1u].x;
  const uint local = vertex - capture.x;
  const uint address = capture.z + local * capture.w;
  const float4 clip = asfloat(captured.Load4(address));
  const float4 translated = clip.x * view[36] + clip.y * view[37] + clip.z * view[38] + clip.w * view[39];
  // Instances a prepass shader collapses (LOD fades multiply the whole position by 0) give inactive triangles: DXR
  // skips triangles with a NaN x.
  float3 world = asfloat(0x7FC00000u).xxx;
  if (clip.w != 0.f) {
    world = translated.xyz / translated.w - view[62].xyz;
  }
  uint packed_uv = 0u;
  if ((capture_flags & CAPTURE_MASKED) != 0u) {
    float2 uv = asfloat(captured.Load2(address + 16u));
    if ((capture_flags & CAPTURE_UV_SCALE) != 0u) {
      const float4 scale = material_rows[low * 2u];
      uv *= float2(scale[(capture_flags >> 2) & 3u], scale[(capture_flags >> 4) & 3u]);
    }
    packed_uv = f32tof16(uv.x) | (f32tof16(uv.y) << 16);
    if (local == 0u) {
      float factor = 1.f;
      if ((capture_flags & CAPTURE_ALPHA_SCALE) != 0u) factor = material_rows[low * 2u + 1u][(capture_flags >> 7) & 3u];
      // Never solid at a factor of 0 or below.
      capture_params[uint2(low, 0)] = float4((factor > 0.f) ? ALPHA_CUTOFF / factor : 2.f, 0.f, 0.f, 0.f);
    }
  }
  vertices[uint2(vertex % 4096u, vertex / 4096u)] = float4(world, asfloat(packed_uv));
}
