/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Ambient (ray_tracing.hpp; the helper's rt_path.hlsli AmbientCube / OpenSky) from the sun pass's PerFrame
// (b0: its b13, column-major global_LightPropertyMatrix; DeferredDirectionalWithSSAO 0xCCCB7253 lights a surface's
// diffuse colour by lerp(ground c3.xyz, sky c0-c2's y, world normal y x 0.5 + 0.5)):
// - u0: the sky toward +x, -x, +y, -y, +z, -z into the bridge's sun texture's texels 3-8 as the pass lights a white
//   surface facing that way: the sky's up, the ground's down, halfway between them sideways; w: texel 3 1 (a sky),
//   texel 4 1 (its scale), texel 6 0 (no stretch of y). Without Path-Traced Ambient (x 0): zeros.
// - u1: PerFrame's 8 registers with the game's ambient zeroed (c0-c2's y, c3.xyz), the sun pass's b13 while the paths
//   bring the sky.
cbuffer PerFrame : register(b0) {
  float4 frame[8];
};

cbuffer sky_settings : register(b1) {
  float4 sky_settings;  // x 1: Path-Traced Ambient on
};

RWTexture2D<float4> sun : register(u0);
RWBuffer<float4> without_ambient : register(u1);

[numthreads(8, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float4 value = frame[id.x];
  if (id.x < 3u) value.y = 0.f;
  if (id.x == 3u) value.xyz = 0.f;
  without_ambient[id.x] = value;
  if (id.x >= 6u) return;
  const float3 sky = float3(frame[0].y, frame[1].y, frame[2].y);
  const float3 ground = frame[3].xyz;
  // The pass's own form: (sky - ground) x (n.y x 0.5 + 0.5) + ground.
  const float up = (id.x == 2u) ? 1.f : ((id.x == 3u) ? 0.f : .5f);
  const float3 light = (sky - ground) * up + ground;
  sun[uint2(3u + id.x, 0u)] = (sky_settings.x != 0.f) ? float4(light, (id.x < 2u) ? 1.f : 0.f) : 0.f;
}
