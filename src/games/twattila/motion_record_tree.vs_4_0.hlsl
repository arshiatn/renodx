// Attila tree records (rt_scene.hpp): the instance rows of the game's branches and leaves (0x77B993FD, 0xB8C3E83D,
// 0x6EEF4062, 0x46C616F0, 0x5B880546, 0xCABCB9D1: one input signature) for ray tracing's placements, one point per
// instance through stream output.
#include "./motion.hlsli"

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float4 v4 : POSITION4,
  float2 v5 : POSITION5,
  float4 v6 : POSITION6,
  float4 v7 : POSITION7,
  float4 v8 : TEXCOORD0,
  float4 v9 : TEXCOORD1,
  float4 v10 : TEXCOORD2,
  float4 v11 : TEXCOORD3,
  float4 v12 : TEXCOORD4,
  float4 v13 : TEXCOORD5,
  float3 v14 : TEXCOORD6,
  uint v15 : SV_InstanceID0,
  out float4 motion_row0 : TEXCOORD0,
  out float4 motion_row1 : TEXCOORD1,
  out float4 motion_row2 : TEXCOORD2,
  out uint4 motion_record : TEXCOORD3)
{
  motion_row0 = v8;
  motion_row1 = v9;
  motion_row2 = v10;
  motion_record = uint4(motion_unit.x, motion_unit.w, 0u, 0u);
}
