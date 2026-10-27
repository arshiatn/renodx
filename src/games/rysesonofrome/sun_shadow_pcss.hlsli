/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Sun Shadow Softness (SI.sun_shadow_softness, degrees): contact-hardening sun shadows (PCSS,
// twshogun2completeedition/shaders/pcss.hlsli) in CryEngine's sun shadow mask passes (sunshadow00-04: one cascade
// each, an orthographic map of its own). Their constants give the screen's basis in the cascade's space
// (CShadowUtils::ProjectScreenToWorldExpansionBasis: one pixel right, one down, the ray to the top-left far corner,
// each normalized with its length in vBasisMagnitudes), not the world's scale. That comes from the orthographic map
// itself: its u and v rows are orthogonal with equal length (sigma) in camera space, which fixes the field of view's
// tangent (tx; ty = tx x height / width), and then sigma / the depth row's length (zeta) is the world's texels per
// unit of depth (the far distance cancels).
#ifndef SRC_GAMES_RYSESONOFROME_SUN_SHADOW_PCSS_HLSLI_
#define SRC_GAMES_RYSESONOFROME_SUN_SHADOW_PCSS_HLSLI_

#include "../twshogun2completeedition/shaders/pcss.hlsli"

// The penumbra's widest (and the blocker search's reach), in texels.
static const float RYSE_PCSS_MAX_TEXELS = 32.f;

// The lit share at `tc` (the cascade map's uv and depth) with the sun SI.sun_shadow_softness degrees across.
// `basis_x` / `basis_y` / `basis_z` and `magnitudes`: the pass's vWBasisX / Y / Z and vBasisMagnitudes;
// `screen_size`: the render size (the scene depth's); `pixel`: SV_Position.
float RyseSunShadowPcss(Texture2D shadow_map, SamplerComparisonState comparison, float3 tc, float3 basis_x,
                        float3 basis_y, float3 basis_z, float3 magnitudes, float2 screen_size, float2 pixel)
{
  // The camera's right, up and forward to the far plane, in the cascade's space (x u, y v, z depth).
  const float3 right = basis_x * magnitudes.x * (0.5f * screen_size.x);
  const float3 up = -basis_y * magnitudes.y * (0.5f * screen_size.y);
  const float3 forward = basis_z * magnitudes.z + right - up;
  // The map's rows in camera space (right, up, forward columns).
  const float3 u_row = float3(right.x, up.x, forward.x);
  const float3 v_row = float3(right.y, up.y, forward.y);
  const float3 depth_row = float3(right.z, up.z, forward.z);
  const float aspect_squared = (screen_size.y * screen_size.y) / (screen_size.x * screen_size.x);
  // 1 / tx^2 from u and v being orthogonal, else from their equal length: whichever is better conditioned.
  const float orthogonal_denominator = u_row.x * v_row.x + u_row.y * v_row.y / aspect_squared;
  const float length_denominator = (u_row.x * u_row.x - v_row.x * v_row.x) + (u_row.y * u_row.y - v_row.y * v_row.y) / aspect_squared;
  const float inverse_tangent_squared = (abs(orthogonal_denominator) > abs(length_denominator))
                                            ? (-u_row.z * v_row.z / orthogonal_denominator)
                                            : (-(u_row.z * u_row.z - v_row.z * v_row.z) / length_denominator);
  // A degenerate solve (or an implausible field of view): about 70 degrees across.
  const float tangent_x = (inverse_tangent_squared > 1.f / 16.f && inverse_tangent_squared < 100.f)
                              ? rsqrt(inverse_tangent_squared)
                              : 0.7f;
  const float3 to_unit = 1.f / float3(tangent_x, tangent_x * sqrt(aspect_squared), 1.f);
  const float sigma = length(u_row * to_unit);
  const float zeta = length(depth_row * to_unit);
  float2 map_size;
  shadow_map.GetDimensions(map_size.x, map_size.y);
  return PcssShadow(shadow_map, comparison, tc, map_size, 1.f / max(sigma * map_size.x, 1e-20f), 1.f / max(zeta, 1e-20f),
                    tan(radians(0.5f * SI.sun_shadow_softness)), RYSE_PCSS_MAX_TEXELS, PcssRotation(pixel, SI.ssao_frame));
}

#endif  // SRC_GAMES_RYSESONOFROME_SUN_SHADOW_PCSS_HLSLI_
