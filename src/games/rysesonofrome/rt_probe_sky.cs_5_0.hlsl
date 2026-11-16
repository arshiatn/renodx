/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Ambient (ray_tracing.hpp): the paths' sky from Ryse's own ambient. The tiled lighting (cs 0x897232B5) takes
// its diffuse ambient from the probes alone (its t32 list's lightType 1 entries: DiffuseCubeArray at t34, layer resIndex,
// sampled with the world normal, times the entry's colour and its attenuationParams.x); the level's own probe is the
// widest. Its light toward +x, -x, +y, -y, +z, -z goes into the sun texture's texels 3-8 (w 1: texel 4's the sky's scale,
// the game's units; texel 5's the share the Light Bounce adds; texel 6's w 0: OpenSky takes the cube at the normal as it
// is, as the lighting samples the probes; its w 1 is Attila's y four times over, a y-up habit, wrong in Ryse's z-up
// world), the ambient cube the helper's paths take as their sky (dlaa_helper/rt_path.hlsli SkyLight, OpenSky). The list holds the frame's lights up to the sun (lightType 8, the last
// the game adds): past it the entries are older frames'. No list (t0 unbound) or no probe: texel 3's w 0 (no sky).
struct TiledLightShadeInfo {
  uint light_type;
  uint resource_index;
  uint shadow_mask_index;
  uint padding;
  float4 position_radius;
  float2 attenuation;
  float2 shadow_parameters;
  float3x4 clip_volume;
  float4 color;
  float4x4 projector;
  float4x4 shadow;
  float4 shadow_channel;
};

StructuredBuffer<TiledLightShadeInfo> lights : register(t0);
TextureCubeArray<float4> diffuse_probes : register(t1);
SamplerState s_linear : register(s0);
RWTexture2D<float4> sun : register(u0);

static const float3 SIDES[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};

[numthreads(1, 1, 1)]
void main() {
  uint count;
  uint stride;
  lights.GetDimensions(count, stride);
  uint widest = 0xFFFFFFFFu;
  float widest_radius = 0.f;
  [loop]
  for (uint index = 0; index < min(count, 4096u); ++index) {
    const uint light_type = lights[index].light_type;
    if (light_type == 8u) break;
    if (light_type == 1u && lights[index].position_radius.w > widest_radius) {
      widest = index;
      widest_radius = lights[index].position_radius.w;
    }
  }
  [unroll]
  for (uint side = 0u; side < 6u; ++side) {
    float4 light = 0.f;
    if (widest != 0xFFFFFFFFu) {
      const TiledLightShadeInfo probe = lights[widest];
      const float3 irradiance = diffuse_probes.SampleLevel(s_linear, float4(SIDES[side], (float)probe.resource_index), 0).rgb;
      light = float4(irradiance * probe.color.rgb * probe.attenuation.x, (side == 3u) ? 0.f : 1.f);
    }
    sun[uint2(3u + side, 0)] = light;
  }
}
