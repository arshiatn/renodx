/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's and Warhammer II's Ray Reconstruction guides (twattila/anti_aliasing.hpp, at the frame's first sun light
// draw of the main view: its G-buffer channels 1-3) from their G-buffer as their sun light reads it (light_bounce.hlsli):
// diffuse albedo the game's ambient (albedo x (1 - specular colour)), specular albedo the specular colour with its
// Fresnel at this view (as Attila's: twattila/rr_specular.hlsli; water, the material flag 64, its bare specular colour
// as before), world normals from channel 1's spherical angles (xy), roughness 1 - its z (gloss), at the G-buffer's size.
// The sky (no G-buffer write: channels 1 and 3 cleared to 0): src: RRG 3.4.3 / 3.4.4: normal (0, 0, 0) and roughness 0 (it
// decoded to (0, -1, 0), a valid angle pair, and roughness 1 before). ours: a texel that is 0 in both channels, because
// xy 0 alone is a valid normal here (straight down); a drawn one that is (down, gloss 0, no specular, no flags) is guided
// as sky.
#include "../twattila/rr_specular.hlsli"

Texture2D<float4> gbuffer_normal : register(t0);     // world normal (spherical angles in xy), gloss
Texture2D<float4> gbuffer_colour : register(t1);     // albedo
Texture2D<float4> gbuffer_constants : register(t2);  // specular colour, the material's flags (w x 255)
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);
cbuffer guides_camera : register(b0) {
  float4x4 g_inverse_view_projection;  // as the depth was drawn (anti_aliasing.hpp GuidesCamera)
  float3 g_camera_position;
  float2 g_inverse_render_size;  // starts the next register, as GuidesCamera's padding keeps it
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  uint2 size;
  roughness.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float4 normal = gbuffer_normal[id.xy];
  const float4 constants = gbuffer_constants[id.xy];
  const bool sky = all(normal == 0.f) && all(constants == 0.f);
  const float3 specular = saturate(constants.rgb);
  diffuse_albedo[id.xy] = float4(gbuffer_colour[id.xy].rgb * (1.f - specular), 1.f);
  const float2 angles = normal.xy * float2(3.14159265f, 6.2831853f) - float2(1.57079633f, 3.14159265f);
  float2 sines;
  float2 cosines;
  sincos(angles, sines, cosines);
  const float3 world_normal = float3(cosines.y * cosines.x, sines.x, sines.y * cosines.x);
  const float linear_roughness = saturate(1.f - normal.z);
  const bool water = (uint(constants.w * 255.f + 0.5f) & 64u) != 0u;
  const float2 uv = (float2(id.xy) + 0.5f) * g_inverse_render_size;
  const float4 on_ray = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.5f, 1.f), g_inverse_view_projection);
  const float n_dot_v = dot(world_normal, normalize(g_camera_position - on_ray.xyz / on_ray.w));
  specular_albedo[id.xy] = float4(water ? specular : RrSpecularAlbedo(specular, linear_roughness, n_dot_v), 1.f);
  normals[id.xy] = float4(sky ? (float3)0.f : world_normal, 0.f);
  roughness[id.xy] = sky ? 0.f : linear_roughness;
}
