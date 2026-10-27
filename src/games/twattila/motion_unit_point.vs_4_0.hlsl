/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion's record points (object_motion.hpp): one per soldier or horse (a draw's first vertex of each
// instance) at the viewport's top-left pixel, with its placement (TEXCOORD0-2: the instance's world rows) and bone base
// (TEXCOORD3.x), as every unit vertex shader takes them (motion_unit_*.vs), for motion_unit_record.ps.
cbuffer variant_animation_size : register(b2)
{
  float2 g_animation_texture_height : packoffset(c0);
  float2 g_animation_texture_width : packoffset(c0.z);
}

void main(float4 row0 : TEXCOORD0, float4 row1 : TEXCOORD1, float4 row2 : TEXCOORD2, float4 bone_base : TEXCOORD3,
          out float4 position : SV_Position0, out nointerpolation float4 point_row0 : TEXCOORD2,
          out nointerpolation float4 point_row1 : TEXCOORD3, out nointerpolation float4 point_row2 : TEXCOORD4,
          out nointerpolation float4 point_bones : TEXCOORD5)
{
  position = float4(-0.999f, 0.999f, 0.5f, 1.f);
  point_row0 = row0;
  point_row1 = row1;
  point_row2 = row2;
  point_bones = float4(bone_base.x, g_animation_texture_width.x, 0.f, 0.f);
}
