/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Contact shadows (contact_shadows.hpp): the sun light's constants (UE 4.26 FDeferredLightUniformStruct, 9 vectors)
// copied with ContactShadowLength (c1.z) set; the copy is bound in their place for the light's draw.
cbuffer Light : register(b0) {
  uint4 light[16];
};

cbuffer ContactShadows : register(b1) {
  float contact_shadow_length;  // Unreal's: positive, a share of the screen
};

RWByteAddressBuffer light_copy : register(u0);

[numthreads(16, 1, 1)]
void main(uint index : SV_GroupIndex) {
  uint4 value = light[index];
  if (index == 1u) {
    value.z = asuint(contact_shadow_length);
  }
  light_copy.Store4(index * 16u, value);
}
