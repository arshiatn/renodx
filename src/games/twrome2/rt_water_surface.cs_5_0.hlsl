/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Rome II's Ray-Traced Water Reflections (addon.cpp TraceWaterReflections, before the water's reflections 7C9C8ECF): the
// water is drawn forward after them with no G-buffer of its own, so the rays get its surface here, where 7C9C8ECF finds
// it: each render pixel's view ray against the water's height (the height map t1 at the scene's point: x the height
// between min_water_height and max_water_height, w under 0.5 no water; 7C9C8ECF's b0 and s2). Where the water lies
// between the camera and the scene: its depth (u0, through 7C9C8ECF's view_projection) and the guides of water (u1-u4:
// Attila's aa_rr_guides' water: no diffuse albedo, specular albedo 0.02, normal up, roughness at least 0.03); elsewhere
// the scene's depth, the guides as written.

cbuffer screen_space_refl_params : register(b0) {
  float4x4 view_projection_matrix : packoffset(c0);
  float4x4 inv_view_projection_matrix : packoffset(c4);
  float4 camera_position_and_reflection_height : packoffset(c8);
  float min_water_height : packoffset(c9);
  float max_water_height : packoffset(c9.y);
  float tiles_per_dim : packoffset(c9.z);
  float playable_area_size : packoffset(c9.w);
  float quality_factor : packoffset(c10);
}

cbuffer water_surface : register(b1) {
  uint2 g_render_size;
  float g_water_roughness;  // Softer Water Reflections' (0: the water's own)
};

SamplerState water_height_sam_s : register(s0);
Texture2D<float> depth_tex : register(t0);  // the depth before the water (7C9C8ECF's t1)
Texture2D<float4> water_height_tex : register(t1);
RWTexture2D<float> surface_depth : register(u0);
RWTexture2D<unorm float4> diffuse_albedo : register(u1);
RWTexture2D<unorm float4> specular_albedo : register(u2);
RWTexture2D<float4> normals : register(u3);
RWTexture2D<unorm float> roughness : register(u4);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= g_render_size)) return;
  const float depth = depth_tex.Load(int3(id.xy, 0));
  const float2 uv = (float2(id.xy) + 0.5f) / float2(g_render_size);
  const float4 on_ray = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), inv_view_projection_matrix);
  const float3 scene = on_ray.xyz / on_ray.w;
  const float4 water = water_height_tex.SampleLevel(water_height_sam_s, (scene.xz / (0.5f * playable_area_size) + 1.f) * 0.5f, 0.f);
  const float height = water.x * (max_water_height - min_water_height) + min_water_height;
  const float3 camera = camera_position_and_reflection_height.xyz;
  float surface = depth;
  if (water.w >= 0.5f && camera.y > height && scene.y < height) {
    const float3 on_water = camera + (scene - camera) * ((camera.y - height) / (camera.y - scene.y));
    const float4 clip = mul(float4(on_water, 1.f), view_projection_matrix);
    surface = clip.z / clip.w;
    diffuse_albedo[id.xy] = float4(0.f, 0.f, 0.f, 1.f);
    specular_albedo[id.xy] = float4(0.02f, 0.02f, 0.02f, 1.f);
    normals[id.xy] = float4(0.f, 1.f, 0.f, 0.f);
    roughness[id.xy] = max(0.03f, g_water_roughness);
  }
  surface_depth[id.xy] = surface;
}
