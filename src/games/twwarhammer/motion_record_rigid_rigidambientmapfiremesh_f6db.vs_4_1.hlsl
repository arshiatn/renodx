// Attila rigid motion's record (anti_aliasing.hpp): the instance rows of the game's 0xF6DBA4F4 for
// motion_rigid.cs, one point per instance through stream output. Made by tools\tw\attila_motion\make_wind_motion_vs.py.
#include "./motion.hlsli"

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : POSITION1,
  float4 v7 : POSITION2,
  float4 v8 : POSITION3,
  float4 v9 : POSITION4,
  float4 v10 : POSITION5,
  float4 v11 : POSITION6,
  float4 v12 : POSITION7,
  uint v13 : SV_InstanceID0,
  out float4 motion_row0 : TEXCOORD0,
  out float4 motion_row1 : TEXCOORD1,
  out float4 motion_row2 : TEXCOORD2,
  out uint4 motion_record : TEXCOORD3)
{
  motion_row0 = v6;
  motion_row1 = v7;
  motion_row2 = v8;
  motion_record = uint4(motion_unit.x, motion_unit.w, 0u, 0u);
}
