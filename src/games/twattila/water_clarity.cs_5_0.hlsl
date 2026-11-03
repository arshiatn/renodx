/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Water Clarity (addon.cpp OnWaterDraw): a river or lake draw's lighting constants (lighting_VS_PS, copied raw) with
// g_sea_bed_light_scatter (c10: the share of light a metre of water leaves, per colour; w along the camera's ray) raised
// to 1 / clarity, the rest as they were. The water's riverbed then shows as through 1 / clarity of the water.
ByteAddressBuffer lighting : register(t0);
RWByteAddressBuffer clarified : register(u0);

cbuffer water_clarity : register(b0)
{
  uint4 water_clarity;  // x the constants' dwords, y 1 / clarity (asfloat)
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (id.x >= water_clarity.x) return;
  uint value = lighting.Load(id.x * 4u);
  if (id.x >= 40u && id.x < 44u) value = asuint(pow(max(asfloat(value), 9.998323e-7f), asfloat(water_clarity.y)));
  clarified.Store(id.x * 4u, value);
}
