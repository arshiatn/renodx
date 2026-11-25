/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The upscaler's optional inputs (taa_dlss.hpp), in the colour input's space (dlss_inputs: its square root for Ray Reconstruction,
// the light as it is for FSR's reactive mask, which reads only the colour before). Unbound textures read 0 and unbound outputs take
// nothing, so each row costs its own memory only.
// Particle layer: 43606BB8 drew into t_particles without blending, so rgb is the particles' light and a their transmittance T; the
// colour has no particles then. NVIDIA blends rgb + (1 - a) x colour on the square roots, so rgb is what makes that equal the game's
// light + T x colour on the light. Never below 0: sqrt(T) >= T. The layer and the depth of field's guide are Ray Reconstruction's
// only, always on the square roots.
Texture2D<float4> t_color : register(t0);
Texture2D<float4> t_particles : register(t1);
Texture2D<float4> t_before_particles : register(t2);
Texture2D<float4> t_before_dof : register(t3);
RWTexture2D<float4> u_particle_layer : register(u0);
RWTexture2D<float4> u_color_before : register(u1);
RWTexture2D<float> u_dof_guide : register(u2);
cbuffer dlss_settings : register(b0) {
  float g_square_root;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const float3 color = max(t_color[id.xy].rgb, 0.f);
  const float3 encoded = sqrt(color);
  const float4 particles = t_particles[id.xy];
  const float transmittance = saturate(particles.a);
  u_particle_layer[id.xy] = float4(sqrt(color * transmittance + max(particles.rgb, 0.f)) - encoded * transmittance, 1.f - transmittance);
  const float3 before = max(t_before_particles[id.xy].rgb, 0.f);
  u_color_before[id.xy] = float4(g_square_root != 0.f ? sqrt(before) : before, 1.f);
  // NVIDIA's luminance; 0 where the blur changed nothing.
  const float3 blur = encoded - sqrt(max(t_before_dof[id.xy].rgb, 0.f));
  u_dof_guide[id.xy] = (blur.r + 2.f * blur.g + blur.b) / 4.f;
}
