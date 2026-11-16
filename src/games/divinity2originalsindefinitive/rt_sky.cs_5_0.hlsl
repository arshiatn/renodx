/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Ambient's sky (ray_tracing.hpp; the helper's rt_path.hlsli AmbientCube / OpenSky): the game's distant IBL
// diffuse toward +x, -x, +y, -y, +z, -z into the bridge's sun texture's texels 3-8, as DeferredSky (518005B9) lights a
// white surface facing that way: IBL_Diffuse_RGB x ProbeProperties.z x 100 (its / DEFAULT_EXPOSURE_MULTIPLIER) x the
// exposure (PreExpose; 0: 0.01), its LUT's diffuse factor left to the paths. w: texel 3 1 (a sky), texel 4 1 (its
// scale), texel 6 0 (no stretch of y). Without the sky (present 0): zeros.
TextureCube<float4> diffuse_sky : register(t0);  // the sky draw's IBL_Diffuse_RGB (its t1)
Texture2D<float> exposure : register(t1);        // the sky draw's Exposure (its t0)
SamplerState linear_sampler : register(s0);
RWTexture2D<float4> sun : register(u0);

cbuffer sky_globals : register(b0) {
  float4 sky_registers[4];  // the sky draw's $Globals: ProbeProperties is c3 (z: its intensity)
};

cbuffer sky_settings : register(b1) {
  float4 sky_settings;  // x 1: the sky present
};

[numthreads(6, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint axis = id.x / 2u;
  const float3 direction = float3(axis == 0u, axis == 1u, axis == 2u) * ((id.x % 2u == 0u) ? 1.f : -1.f);
  const float stored = exposure.Load(int3(0, 0, 0));
  const float scale = sky_registers[3].z * 100.f * ((stored == 0.f) ? 0.01f : stored);
  const float3 light = diffuse_sky.SampleLevel(linear_sampler, direction, 0.f).rgb * scale;
  const float w = (id.x == 0u || id.x == 1u) ? 1.f : 0.f;
  sun[uint2(3u + id.x, 0u)] = (sky_settings.x != 0.f) ? float4(light, w) : 0.f;
}
