/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// NVIDIA's NRD's denoised sun shadows (SIGMA_SHADOW_TRANSLUCENCY, rt_nrd_front.hlsl's inputs) into the visibility
// the addon reads: what the denoiser keeps as its history stays its own, encoded (NRD.hlsli's SIGMA_BackEnd_UnpackShadow is
// the square; its final shadow: the translucency where blocked, 1 lit). NRD does not write the sky.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_nrd_shadow.h -Vn RT_NRD_SHADOW.
Texture2D<float4> denoised : register(t0);
Texture2D<float4> positions : register(t1);
RWTexture2D<unorm float> visibility : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (positions[id.xy].w <= 0.f) {
    visibility[id.xy] = 1.f;
    return;
  }
  const float4 shadow = denoised[id.xy] * denoised[id.xy];
  visibility[id.xy] = lerp(shadow.y, 1.f, shadow.x);
}
