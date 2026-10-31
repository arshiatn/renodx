// Attila campaign plant rows (rt_scene.hpp): the placement of the game's rigidcampaignvegetation (0x924D7813, 0x819A414C) for
// ray tracing, one point per instance through stream output: its size and turn around the vertical, standing
// (rt_capture_campaign_vegetation).

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float4 v2 : POSITION1,
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out uint4 record : TEXCOORD3)
{
  float turn_sin, turn_cos;
  sincos(v2.w, turn_sin, turn_cos);
  row0 = float4(0.4f * turn_cos, 0.f, 0.4f * turn_sin, v2.x);
  row1 = float4(0.f, 0.4f, 0.f, v2.y);
  row2 = float4(-0.4f * turn_sin, 0.f, 0.4f * turn_cos, v2.z);
  record = 0u;
}
