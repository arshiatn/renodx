// The first-hit probe (RENODX_TW_PROBE, a test path for the harness's first-hit dump; rt_d3d12.hpp probe_*): each ray of
// the harness's list traced against the scene the request's trace used (the TLAS or the partitioned TLAS), every
// triangle taken as a hit (no alpha test, no micromaps): which instance a ray meets first and how far, not what it sees.
RaytracingAccelerationStructure scene : register(t0);
StructuredBuffer<float4> rays : register(t8);  // a ray: origin and TMax, then direction and instance mask (asfloat)
// A ray's answer: status (1: a triangle), InstanceID, InstanceIndex, InstanceContributionToHitGroupIndex; then asuint(T),
// GeometryIndex, PrimitiveIndex, front face.
RWStructuredBuffer<uint4> hits : register(u0, space1);
cbuffer Dispatch : register(b1) {
  uint ray_count;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ray_count) return;
  const float4 origin = rays[id.x * 2u];
  const float4 direction = rays[id.x * 2u + 1u];
  RayDesc ray;
  ray.Origin = origin.xyz;
  ray.TMin = 0.f;
  ray.Direction = direction.xyz;
  ray.TMax = origin.w;
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, asuint(direction.w) & 0xFFu, ray);
  while (query.Proceed()) {
    if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) query.CommitNonOpaqueTriangleHit();
  }
  if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    hits[id.x * 2u] = uint4(1u, query.CommittedInstanceID(), query.CommittedInstanceIndex(), query.CommittedInstanceContributionToHitGroupIndex());
    hits[id.x * 2u + 1u] = uint4(asuint(query.CommittedRayT()), query.CommittedGeometryIndex(), query.CommittedPrimitiveIndex(),
                                 query.CommittedTriangleFrontFace() ? 1u : 0u);
  } else {
    hits[id.x * 2u] = uint4(0u, 0u, 0u, 0u);
    hits[id.x * 2u + 1u] = uint4(asuint(-1.f), 0u, 0u, 0u);
  }
}
