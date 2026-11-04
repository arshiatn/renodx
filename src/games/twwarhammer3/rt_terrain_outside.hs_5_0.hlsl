// Warhammer III's ray tracing scene: the terrain's patches its hull shader (F253B1D0) culls for being out of the view, so that
// hills behind the camera give AO, light bounce and reflections their hits. Drawn into the stream output only, after the game's
// draw went through with its own hull shader: the same control points, the opposite plane test, its edge factors by distance
// and the inside at their largest, as its own reads the screen size, which means nothing behind the camera.
// CMake's shader rule takes no hs_ target: after a change run in this folder
// bin\fxc.exe rt_terrain_outside.hs_5_0.hlsl -T hs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_terrain_outside /Fh rt_terrain_outside_hs.h

struct ControlPoint {
  float4 world : WORLD_POSITION;
  float2 original : ORIGINAL_POSITION;
  float quality : QUALITY;
  uint mask : CONTROL_POINT_MASK;
  uint visibility : VISIBILITY_MASK;
  float2 height_range : HEIGHT_RANGE;
};

struct Output {
  float4 world : WORLD_POSITION;
  float2 original : ORIGINAL_POSITION;
  uint mask : CONTROL_POINT_MASK;
  uint visibility : VISIBILITY_MASK;
};

struct Factors {
  float edges[4] : SV_TessFactor;
  float inside[2] : SV_InsideTessFactor;
};

cbuffer camera : register(b0) {
  float3 camera_position;
  float4 before_projection_w[8];
  float4 projection_w;
  float4 before_planes[47];
  float4 g_frustum_planes[6];
};

cbuffer tessellation_cb : register(b1) {
  float4 before_patch_triangles[2];
  float4 patch_triangles;  // z: g_patch_external_triangles
  float4 before_clip_pass[4];
  float4 clip_pass;  // y: g_clip_pass
  float4 before_visibility[5];
  uint2 g_patch_dimensions;
  uint g_visibility_mask_value;
};

float EdgeFactor(ControlPoint a, ControlPoint b) {
  const float distance_to_edge = distance((a.world.xyz + b.world.xyz) * 0.5f, camera_position);
  const float scale = min(128.f / dot(float2(distance_to_edge, 1.f), projection_w.zw), 1.f);
  return min(max(floor(max((a.quality + b.quality) * patch_triangles.z * scale * 0.5f, 1.f) * 0.25f) * 4.f, 1.f), 64.f);
}

Factors PatchFactors(InputPatch<ControlPoint, 4> points) {
  Factors factors = (Factors)0;
  float3 low = float3(points[0].world.x, points[0].height_range.x, points[0].world.z);
  float3 high = float3(points[0].world.x, points[0].height_range.y, points[0].world.z);
  bool any_hidden = false;
  bool any_kept = false;
  const uint keep_bit = (clip_pass.y != 0.f ? 16u : 32u);
  for (uint corner = 0u; corner < 4u; ++corner) {
    low = min(low, float3(points[corner].world.x, points[corner].height_range.x, points[corner].world.z));
    high = max(high, float3(points[corner].world.x, points[corner].height_range.y, points[corner].world.z));
    any_hidden = any_hidden || (points[corner].visibility & g_visibility_mask_value) == 0u;
    any_kept = any_kept || (points[corner].mask & keep_bit) != 0u;
  }
  if (any_hidden || !any_kept) return factors;

  const float3 centre = (low + high) * 0.5f;
  const float radius = length(low - centre);
  bool in_view = true;
  for (uint plane = 0u; plane < 6u; ++plane) {
    in_view = in_view && (dot(g_frustum_planes[plane].xyz, centre) + g_frustum_planes[plane].w) >= -radius;
  }
  if (in_view) return factors;

  factors.edges[0] = EdgeFactor(points[2], points[0]);
  factors.edges[1] = EdgeFactor(points[0], points[1]);
  factors.edges[2] = EdgeFactor(points[1], points[3]);
  factors.edges[3] = EdgeFactor(points[3], points[2]);
  factors.inside[0] = max(max(factors.edges[0], factors.edges[1]), max(factors.edges[2], factors.edges[3]));
  factors.inside[1] = factors.inside[0];
  return factors;
}

[domain("quad")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("PatchFactors")]
[maxtessfactor(64.f)]
Output main(InputPatch<ControlPoint, 4> points, uint id : SV_OutputControlPointID) {
  const Output output = {points[id].world, points[id].original, points[id].mask, 0u};
  return output;
}
