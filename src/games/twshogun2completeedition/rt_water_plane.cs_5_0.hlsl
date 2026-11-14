/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2's Ray-Traced Water Reflections (ray_tracing.hpp WaterSurface, before the reflections' first pass ssrhiz00): the
// water (the sea, the battles' rivers and lakes) is drawn forward after the reflections with no G-buffer of its own, so the
// rays get its surface here, as Rome II's rt_water_surface. The sea is a plane (its vertex shader's
// g_decal_ocean_projection_plane, from its last draw): each render pixel's view ray against it where the scene lies under it
// (g_sea). The water planes' meshes were drawn again depth-only (t1, g_meshes): where they are nearer than the scene. There:
// the water's depth (u0, the plane's through the camera's view_projection), the guides of water (u1-u4: no diffuse albedo,
// specular albedo 0.02, normal up, roughness 0.03 or Softer Water Reflections' more) and the water mask (u5: 1, for the
// resolve and the water's pixel shaders); elsewhere the scene's depth, the guides as written, mask 0.

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view_projection : packoffset(c10);
  float4x4 inv_view_projection : packoffset(c30);
}

cbuffer water_plane : register(b1)
{
  float4 g_plane;  // the sea: normal (up), w: dot(normal, p) + w is 0 on the water
  uint2 g_render_size;
  uint g_sea;     // the plane is the sea's (drawn last frame)
  uint g_meshes;  // t1 holds the water planes' depth
  float g_water_roughness;  // Softer Water Reflections' least roughness (0: the water's own)
};

Texture2D<float> depth_tex : register(t0);   // ssrhiz00's t0
Texture2D<float> meshes_tex : register(t1);  // the water planes' depth (cleared to 1)
RWTexture2D<float> surface_depth : register(u0);
RWTexture2D<unorm float4> diffuse_albedo : register(u1);
RWTexture2D<unorm float4> specular_albedo : register(u2);
RWTexture2D<float4> normals : register(u3);
RWTexture2D<unorm float> roughness : register(u4);
RWTexture2D<unorm float> water_mask : register(u5);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (any(id.xy >= g_render_size)) return;
  const float depth = depth_tex.Load(int3(id.xy, 0));
  float surface = depth;
  float3 normal = float3(0.f, 1.f, 0.f);
  if (g_sea != 0u)
  {
    const float2 uv = (float2(id.xy) + 0.5f) / float2(g_render_size);
    const float4 on_ray = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), inv_view_projection);
    const float3 scene = on_ray.xyz / on_ray.w;
    const float camera_side = dot(g_plane.xyz, camera_position) + g_plane.w;
    const float scene_side = dot(g_plane.xyz, scene) + g_plane.w;
    if (camera_side > 0.f && scene_side < 0.f)
    {
      const float3 on_water = camera_position + (scene - camera_position) * (camera_side / (camera_side - scene_side));
      const float4 clip = mul(float4(on_water, 1.f), view_projection);
      surface = clip.z / clip.w;
      normal = g_plane.xyz;
    }
  }
  if (g_meshes != 0u)
  {
    const float mesh = meshes_tex.Load(int3(id.xy, 0));
    if (mesh < surface)
    {
      surface = mesh;
      normal = float3(0.f, 1.f, 0.f);
    }
  }
  const bool water = surface != depth;
  if (water)
  {
    diffuse_albedo[id.xy] = float4(0.f, 0.f, 0.f, 1.f);
    specular_albedo[id.xy] = float4(0.02f, 0.02f, 0.02f, 1.f);
    normals[id.xy] = float4(normal, 0.f);
    roughness[id.xy] = max(0.03f, g_water_roughness);
  }
  surface_depth[id.xy] = surface;
  water_mask[id.xy] = water ? 1.f : 0.f;
}
