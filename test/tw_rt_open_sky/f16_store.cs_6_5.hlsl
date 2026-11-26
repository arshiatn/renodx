/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// tests.cpp part 5 (X11): the helper's StoredHalf (dlaa_helper/rt_path.hlsli) through a typed R16_FLOAT store, the 5090's
// typed store emulated (f32tof16: toward zero) as the control, and WARP's own typed store of the float (reported only).
#include "../../src/games/twattila/dlaa_helper/rt_path.hlsli"

Texture2D<float> f16_values : register(t0, space9);
RWTexture2D<float> f16_nearest : register(u0, space9);
RWTexture2D<float> f16_truncated : register(u1, space9);
RWTexture2D<float> f16_raw : register(u2, space9);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const float value = f16_values[id.xy];
  f16_nearest[id.xy] = StoredHalf(value).x;
  f16_truncated[id.xy] = f16tof32(f32tof16(value));
  f16_raw[id.xy] = value;
}
