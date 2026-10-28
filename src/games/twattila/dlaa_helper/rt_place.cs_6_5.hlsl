/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's placed meshes (dlaa_helper/rt_d3d12.hpp, bridge v14): each of this request's placed
// instances as a TLAS instance desc, after the CPU's. A placed mesh's BLAS is its first capture's first instance in world
// space; a drawn instance goes where its rows (this request's, in the vertex buffer) put it: its rows times the inverse
// of the build's rows (kept by record). Carried instances (meshes drawn lately but not now) are copied from the last
// dispatch's descs. Degenerate rows: an inactive instance (no BLAS).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_place.h -Vn RT_PLACE.

ByteAddressBuffer instances_before : register(t0);  // the last dispatch's descs (the carried ones' source)
Buffer<float4> vertex_positions : register(t7);     // the request's vertices: an instance's rows 0-2 in four texels
// Per job (two uint4): its first placed instance, instances, source (rows texel; carried: the first desc before), flags;
// record, BLAS address (low, high), 0.
StructuredBuffer<uint4> jobs : register(t8);
RWByteAddressBuffer instances_written : register(u6);  // D3D12_RAYTRACING_INSTANCE_DESC, 64 bytes each
RWStructuredBuffer<float4> build_rows : register(u7);  // three per record

cbuffer Dispatch : register(b1) {
  uint job_count;
  uint instance_count;
  uint first_instance;  // the CPU's instances before these
  uint dispatch_padding;
};

static const uint PLACE_FOLIAGE = 1u;  // alpha-tested: not forced opaque
static const uint PLACE_CARRIED = 2u;
static const uint INSTANCE_CULL_DISABLE = 1u;  // D3D12_RAYTRACING_INSTANCE_FLAG_*
static const uint INSTANCE_FORCE_OPAQUE = 4u;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint placed = id.x;
  if (placed >= instance_count) return;
  uint low = 0u;
  uint high = job_count - 1u;
  while (low < high) {
    const uint middle = (low + high + 1u) / 2u;
    if (jobs[middle * 2u].x <= placed) {
      low = middle;
    } else {
      high = middle - 1u;
    }
  }
  const uint4 job = jobs[low * 2u];
  const uint4 mesh = jobs[low * 2u + 1u];
  const uint instance = placed - job.x;
  const uint written = (first_instance + placed) * 64u;
  if ((job.w & PLACE_CARRIED) != 0u) {
    const uint read = (job.z + instance) * 64u;
    [unroll]
    for (uint part = 0u; part < 48u; part += 16u) {
      instances_written.Store4(written + part, instances_before.Load4(read + part));
    }
    // Its BLAS as it is now (compacted since); inactive ones stay so.
    const uint4 tail = instances_before.Load4(read + 48u);
    instances_written.Store4(written + 48u, ((tail.z | tail.w) != 0u) ? uint4(tail.xy, mesh.y, mesh.z) : tail);
    return;
  }
  const float4 build0 = build_rows[mesh.x * 3u];
  const float4 build1 = build_rows[mesh.x * 3u + 1u];
  const float4 build2 = build_rows[mesh.x * 3u + 2u];
  const float4 now0 = vertex_positions[job.z + instance * 4u];
  const float4 now1 = vertex_positions[job.z + instance * 4u + 1u];
  const float4 now2 = vertex_positions[job.z + instance * 4u + 2u];
  // The build's 3 x 3 inverted (its cofactors' columns over its determinant), then its translation.
  const float3 cofactor0 = cross(build1.xyz, build2.xyz);
  const float3 cofactor1 = cross(build2.xyz, build0.xyz);
  const float3 cofactor2 = cross(build0.xyz, build1.xyz);
  const float determinant = dot(build0.xyz, cofactor0);
  const float3x3 inverse = transpose(float3x3(cofactor0, cofactor1, cofactor2)) / determinant;
  const float3x3 rotation = mul(float3x3(now0.xyz, now1.xyz, now2.xyz), inverse);
  const float3 translation = float3(now0.w, now1.w, now2.w) - mul(rotation, float3(build0.w, build1.w, build2.w));
  const bool valid = abs(determinant) > 1e-12f && all(isfinite(rotation[0])) && all(isfinite(rotation[1])) && all(isfinite(rotation[2]))
                     && all(isfinite(translation));
  if (!valid) {
    [unroll]
    for (uint part = 0u; part < 64u; part += 16u) {
      instances_written.Store4(written + part, uint4(0u, 0u, 0u, 0u));
    }
    return;
  }
  instances_written.Store4(written, asuint(float4(rotation[0], translation.x)));
  instances_written.Store4(written + 16u, asuint(float4(rotation[1], translation.y)));
  instances_written.Store4(written + 32u, asuint(float4(rotation[2], translation.z)));
  // InstanceID (the record) and mask 0xFF; no hit group offset, the flags; the BLAS.
  const uint flags = INSTANCE_CULL_DISABLE | (((job.w & PLACE_FOLIAGE) != 0u) ? 0u : INSTANCE_FORCE_OPAQUE);
  instances_written.Store4(written + 48u, uint4((mesh.x & 0xFFFFFFu) | (0xFFu << 24), flags << 24, mesh.y, mesh.z));
}
