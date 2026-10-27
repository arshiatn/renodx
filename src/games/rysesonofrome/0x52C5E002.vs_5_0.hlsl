/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Contact shadows (contact_shadows.hpp): a full-screen triangle.
float4 main(uint id : SV_VertexID) : SV_Position {
  const float2 corner = float2((id << 1) & 2, id & 2);
  return float4(corner * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
}
