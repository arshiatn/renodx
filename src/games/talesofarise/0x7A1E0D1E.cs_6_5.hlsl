// Reflections and specular occlusion after the paths (rt_path.hlsli): the accumulation kept for the next frame, then
// filtered across the surface as far as the lobe is wide (none for mirrors, up to 5 x 5 for the roughest traced; plane
// and normal weights as 0x7A1E0D14) into the shared textures the HDR addon's lighting pass reads: the reflection (rgb,
// the share that met a surface) at t43, the specular occlusion at t44.
#include "./rt_surface.hlsli"

Texture2D<float4> normals : register(t6);    // G-buffer A: world normal x 0.5 + 0.5
Texture2D<float4> material : register(t13);  // G-buffer B: metallic, specular, roughness, shading model
RWTexture2D<float4> reflection : register(u8);
RWTexture2D<float4> specular_accumulated : register(u16);
RWTexture2D<float4> specular_history : register(u17);
RWTexture2D<unorm float> specular_occlusion : register(u18);
RWTexture2D<float2> specular_meta : register(u23);
RWTexture2D<float2> specular_meta_history : register(u24);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  reflection.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float4 center = specular_accumulated[id.xy];
  const float2 center_meta = specular_meta[id.xy];
  specular_history[id.xy] = center;
  specular_meta_history[id.xy] = center_meta;
  const float4 position = positions[id.xy];
  const float3 encoded = normals[id.xy].xyz;
  if (position.w <= 0.f || dot(encoded, encoded) == 0.f) {
    reflection[id.xy] = 0.f;
    specular_occlusion[id.xy] = 1.f;
    return;
  }
  const float3 normal = normalize(encoded * 2.f - 1.f);
  // With Ray Reconstruction (it denoises the frame) no filter.
  const int radius = ((flags & RAY_RECONSTRUCTION) != 0u) ? 0 : (int)round(saturate(material[id.xy].b * 3.f) * 2.f);
  const float plane_tolerance = 2.f + 0.02f * position.w;
  float4 sum = 0.f;
  float open = 0.f;
  float weights = 0.f;
  [loop]
  for (int y = -radius; y <= radius; ++y) {
    [loop]
    for (int x = -radius; x <= radius; ++x) {
      const int2 pixel = clamp(int2(id.xy) + int2(x, y), 0, int2(size) - 1);
      const float4 neighbour_position = positions[pixel];
      const float3 neighbour_encoded = normals[pixel].xyz;
      if (neighbour_position.w <= 0.f || dot(neighbour_encoded, neighbour_encoded) == 0.f) continue;
      const float plane_distance = abs(dot(neighbour_position.xyz - position.xyz, normal));
      const float weight = (radius + 1.f - abs(x)) * (radius + 1.f - abs(y)) * saturate(1.f - plane_distance / plane_tolerance)
                           * pow(saturate(dot(normal, normalize(neighbour_encoded * 2.f - 1.f))), 8.f);
      sum += specular_accumulated[pixel] * weight;
      open += specular_meta[pixel].x * weight;
      weights += weight;
    }
  }
  reflection[id.xy] = (weights > 0.f) ? sum / weights : center;
  specular_occlusion[id.xy] = (weights > 0.f) ? open / weights : center_meta.x;
}
