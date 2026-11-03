/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// NVIDIA's NRD's denoised outputs (nrd_d3d12.hpp) into the shared textures the addon reads, on NRD's root signature: what
// the denoiser keeps as its history stays its own, encoded; NRD does not write the sky. NRD_FINISH_SUN (SIGMA_SHADOW_
// TRANSLUCENCY; SIGMA_BackEnd_UnpackShadow is the square; the shadow: the translucency where blocked, 1 lit) into the
// visibility. NRD_FINISH_AMBIENT (REBLUR's occlusion) into the ambient. NRD_FINISH_BOUNCE (REBLUR) into the bounce's rgb,
// its w (the texel's distance from the camera, 0 the sky) kept. NRD_FINISH_REFLECT (REBLUR, on the light x the share that
// met) back to the light of what met into the reflection's rgb, its a (the share, the helper's filter's) kept.
// Build (build.ps1, with NRD's Shaders folder): dxc -T cs_6_5 -D NRD_FINISH_<signal> -Vn RT_NRD_FINISH_<signal>.
#include "NRD.hlsli"

Texture2D<float4> denoised : register(t0);
Texture2D<float4> positions : register(t1);
#if defined(NRD_FINISH_SUN) || defined(NRD_FINISH_AMBIENT)
RWTexture2D<unorm float> written : register(u0);
#else
RWTexture2D<float4> written : register(u0);
#endif

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
#if defined(NRD_FINISH_BOUNCE)
  const float4 before = written[id.xy];
  if (before.w <= 0.f) return;
  written[id.xy] = float4(REBLUR_BackEnd_UnpackRadianceAndNormHitDist(denoised[id.xy]).rgb, before.w);
#else
  if (positions[id.xy].w <= 0.f) {
#if defined(NRD_FINISH_REFLECT)
    return;
#else
    written[id.xy] = 1.f;
    return;
#endif
  }
#if defined(NRD_FINISH_SUN)
  const float4 shadow = denoised[id.xy] * denoised[id.xy];
  written[id.xy] = lerp(shadow.y, 1.f, shadow.x);
#elif defined(NRD_FINISH_AMBIENT)
  written[id.xy] = denoised[id.xy].x;
#elif defined(NRD_FINISH_REFLECT)
  const float share = written[id.xy].a;
  written[id.xy] = float4(REBLUR_BackEnd_UnpackRadianceAndNormHitDist(denoised[id.xy]).rgb / max(share, 1e-3f), share);
#endif
#endif
}
