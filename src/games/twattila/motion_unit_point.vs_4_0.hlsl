/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion's record points (object_motion.hpp): one per soldier or horse (a draw's first vertex of each
// instance) on a pixel of its own (motion.hlsli MotionScatterPoint), with its placement (TEXCOORD0-2: the instance's
// world rows) and bone base (TEXCOORD3.x), as every unit vertex shader takes them (motion_unit_*.vs), for
// motion_unit_record.ps. The inputs before them are declared as the game's (MOTION_UNIT_4_BONES:
// motion_unit_point_4_bones.vs, one position more): D3D11 feeds a vertex shader's inputs by register, as its input layout
// was made for the game's shader.
#include "./motion.hlsli"

cbuffer variant_animation_size : register(b2)
{
  float2 g_animation_texture_height : packoffset(c0);
  float2 g_animation_texture_width : packoffset(c0.z);
}

void main(float4 position0 : POSITION0, float4 position1 : POSITION1, float4 position2 : POSITION2,
#ifdef MOTION_UNIT_4_BONES
          float4 position3 : POSITION3, float2 position4 : POSITION4, float4 position5 : POSITION5,
          float4 position6 : POSITION6,
#else
          float2 position3 : POSITION3, float4 position4 : POSITION4, float4 position5 : POSITION5,
#endif
          float4 row0 : TEXCOORD0, float4 row1 : TEXCOORD1, float4 row2 : TEXCOORD2, float4 bone_base : TEXCOORD3,
          uint instance : SV_InstanceID, out float4 position : SV_Position0, out nointerpolation float4 point_row0 : TEXCOORD2,
          out nointerpolation float4 point_row1 : TEXCOORD3, out nointerpolation float4 point_row2 : TEXCOORD4,
          out nointerpolation float4 point_bones : TEXCOORD5)
{
  position = MotionScatterPoint(instance);
  point_row0 = row0;
  point_row1 = row1;
  point_row2 = row2;
  point_bones = float4(bone_base.x, g_animation_texture_width.x, 0.f, 0.f);
}
