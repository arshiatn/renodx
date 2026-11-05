/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray Reconstruction's specular albedo: F0 with its Fresnel, pre-integrated at this view and roughness. NVIDIA's DLSS-RR
// programming guide, 4.2.1 (EnvBRDFApprox2, Ray Tracing Gems chapter 32), alpha the roughness guide's square. The shaders'
// own diffuse albedo keeps the bare F0: the game's sun takes it that way.
float3 RrSpecularAlbedo(float3 f0, float linear_roughness, float n_dot_v) {
  const float alpha = linear_roughness * linear_roughness;
  const float cosine = abs(n_dot_v);
  const float4 powers_of_cosine = float4(1.f, cosine, cosine * cosine, cosine * cosine * cosine);
  const float3 powers_of_alpha = float3(1.f, alpha, alpha * alpha * alpha);
  const float2x2 m1 = float2x2(0.99044f, -1.28514f, 1.29678f, -0.755907f);
  const float3x3 m2 = float3x3(1.f, 2.92338f, 59.4188f, 20.3225f, -27.0302f, 222.592f, 121.563f, 626.13f, 316.627f);
  const float2x2 m3 = float2x2(0.0365463f, 3.32707f, 9.0632f, -9.04756f);
  const float3x3 m4 = float3x3(1.f, 3.59685f, -1.36772f, 9.04401f, -16.3174f, 9.22949f, 5.56589f, 19.7886f, -20.2123f);
  const float bias = dot(mul(m1, powers_of_cosine.xy), powers_of_alpha.xy) / dot(mul(m2, powers_of_cosine.xyw), powers_of_alpha);
  const float scale = dot(mul(m3, powers_of_cosine.xy), powers_of_alpha.xy) / dot(mul(m4, powers_of_cosine.xzw), powers_of_alpha);
  // The guide's own rule for a specular reflectance of 0.
  return saturate(f0 * max(scale, 0.f) + max(bias * saturate(f0.g * 50.f), 0.f));
}
