/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing's G-buffer guides (ray_tracing.hpp; the helper's reflections and paths read them) from Ryse's G-buffer as
// the tiled lighting (cs 0x897232B5) decodes it: world normals (x 0.5 + 0.5), the gloss (the specular's x; roughness
// 1 - gloss, as its reflection blur), the specular colour (luma y, chroma zw - 0.5 (none on translucent surfaces: the
// normal's w past 0.504), squared) and the diffuse colour (squared), at the render pixels.
Texture2D<float4> normal_texture : register(t0);
Texture2D<float4> specular_texture : register(t1);
Texture2D<float4> diffuse_texture : register(t2);
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width, height;
  normal_texture.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  const float4 normal = normal_texture[id.xy] * 2.f - 1.f;
  const float4 specular = specular_texture[id.xy];
  const float2 chroma = (normal.w > 0.00787401572f) ? 0.f : specular.zw - 0.5f;
  const float3 specular_colour = float3(specular.y + 1.402f * chroma.y, specular.y - 0.3441f * chroma.x - 0.7141f * chroma.y,
                                        specular.y + 1.772f * chroma.x);
  const float3 diffuse = diffuse_texture[id.xy].rgb;
  diffuse_albedo[id.xy] = float4(diffuse * diffuse, 1.f);
  specular_albedo[id.xy] = float4(specular_colour * specular_colour, 1.f);
  normals[id.xy] = float4((dot(normal.xyz, normal.xyz) > 0.f) ? normalize(normal.xyz) : 0.f, 0.f);
  roughness[id.xy] = 1.f - specular.x;
}
