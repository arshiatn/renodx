// Ray-traced bounce light, after the trace (0x7A1E0D13): the accumulation filtered across the surface (a 7x7 tent
// at every other pixel, weighted by how near each neighbour lies to this pixel's plane and how alike their normals are),
// times the strength, for the HDR addon's lighting pass (t42); the unfiltered accumulation kept for the next frame.
#include "./rt_surface.hlsli"

RWTexture2D<float4> accumulated : register(u2);
RWTexture2D<float4> history : register(u3);
RWTexture2D<float4> bounce_light : register(u4);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  accumulated.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float4 center = accumulated[id.xy];
  history[id.xy] = center;
  const float4 position = positions[id.xy];
  if (position.w <= 0.f) {
    bounce_light[id.xy] = 0.f;
    return;
  }
  const float3 normal = SurfaceNormal(int2(id.xy), int2(size), position);
  // Neighbours further than 2% of the depth from this pixel's plane belong to other surfaces.
  const float plane_tolerance = 2.f + 0.02f * position.w;
  float3 sum = 0.f;
  float weights = 0.f;
  // With Ray Reconstruction (it denoises the frame) the accumulation goes out unfiltered.
  [branch]
  if ((flags & RAY_RECONSTRUCTION) == 0u) {
  [unroll]
  for (int y = -3; y <= 3; ++y) {
    [unroll]
    for (int x = -3; x <= 3; ++x) {
      const int2 pixel = clamp(int2(id.xy) + int2(x, y) * 2, 0, int2(size) - 1);
      const float4 neighbour_position = positions[pixel];
      if (neighbour_position.w <= 0.f) continue;
      const float plane_distance = abs(dot(neighbour_position.xyz - position.xyz, normal));
      const float3 neighbour_normal = SurfaceNormal(pixel, int2(size), neighbour_position);
      const float weight = (4.f - abs(x)) * (4.f - abs(y)) * saturate(1.f - plane_distance / plane_tolerance)
                           * pow(saturate(dot(normal, neighbour_normal)), 8.f);
      sum += accumulated[pixel].rgb * weight;
      weights += weight;
    }
  }
  }
  const float3 filtered = (weights > 0.f) ? sum / weights : center.rgb;
  bounce_light[id.xy] = float4(filtered * bounce_strength, 1.f);
}
