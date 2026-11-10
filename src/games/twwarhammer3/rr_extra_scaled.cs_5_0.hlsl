/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Below 100% (taa_dlss.hpp), Ray Reconstruction's particle inputs: in dlss_scaled_prepare's place when a particle row is on. The
// upscaler's colour there is the tone mapper's picture (t0, at the render size, top-left; gamma 2.2), while the particles were drawn
// long before it: 43606BB8's own result (t1: rgb the particles' light, a their transmittance T, drawn once more without blending)
// and the scene's colour just before it (t2), both scene light. The picture without particles is not drawn by anyone, so it is
// taken as the picture times before / after of the scene's light at the pixel (exact where the tone mapper is a factor, an
// estimate elsewhere; 1 where no particle is, so the picture stays to the bit there).
// NVIDIA blends layer.rgb + (1 - layer.a) x colour on the square roots: the layer is what makes that the picture again, whatever
// the estimate. Never below 0: T x sqrt(before) <= sqrt(T x before + light).
#include "../../shaders/color/gamma.hlsl"

Texture2D<float4> t_picture : register(t0);
Texture2D<float4> t_particles : register(t1);
Texture2D<float4> t_before_particles : register(t2);
RWTexture2D<float4> u_color : register(u0);
RWTexture2D<float4> u_particle_layer : register(u1);
RWTexture2D<float4> u_color_before : register(u2);
cbuffer dlss_settings : register(b0) {
  float g_particle_layer;  // 1: the colour goes without particles and the layer holds them; 0: Picture Before Particles alone
  float g_stretch;  // dlss_scaled_finish's
  float2 g_render_size;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(float2(id.xy) >= g_render_size)) return;
  const float4 picture = t_picture[id.xy];
  const float3 with_particles = max(mul(float3x3(0.6274040f, 0.3292820f, 0.0433136f,
                                                 0.0690970f, 0.9195400f, 0.0113612f,
                                                 0.0163916f, 0.0880132f, 0.8955950f),
                                        renodx::color::gamma::DecodeSafe(picture.rgb)),
                                    0.f);
  const float4 particles = t_particles[id.xy];
  const float transmittance = saturate(particles.a);
  const float3 scene_before = max(t_before_particles[id.xy].rgb, 0.f);
  const float3 scene_after = scene_before * transmittance + max(particles.rgb, 0.f);
  // Before over after is at most 1 / T; a dark, thick puff over a bright ground would make the estimate many times the picture.
  const float3 share = (any(particles.rgb > 0.f) || transmittance < 1.f) ? min(scene_before / max(scene_after, 1e-6f), 8.f) : 1.f;
  const float3 encoded = sqrt(with_particles);
  const float3 encoded_before = sqrt(with_particles * share);
  u_color[id.xy] = float4((g_particle_layer != 0.f) ? encoded_before : encoded, picture.a);
  u_particle_layer[id.xy] = float4(max(encoded - encoded_before * transmittance, 0.f), 1.f - transmittance);
  u_color_before[id.xy] = float4(encoded_before, 1.f);
}
