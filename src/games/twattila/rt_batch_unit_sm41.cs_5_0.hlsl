// Batched Unit Capture (rt_core.hpp, SceneShader::batch_compute) of the game's 0xA691CFF4: rt_capture_unit_sm41's world
// position, its own code, from its draw's streamed mesh (rt_batch_unit_mesh) and instance rows (rt_batch_unit_rows), the
// draw's animation texture at t0 and its width at b2.

#define main RtCaptureUnitMain
#include "./rt_capture_unit_sm41.vs_4_1.hlsl"
#undef main

#define RT_BATCH_MESH_TEXELS 2

float4 RtBatchWorld(float4 mesh[RT_BATCH_MESH_TEXELS], float4 rows[4])
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  float4 o8 = 0;
  float4 o9 = 0;
  float4 o10 = 0;
  uint4 p10 = 0;
  float4 o11 = 0;
  // As RtCaptureUnitMain: identity rows, the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  GameMain(mesh[0], mesh[1], 0, 0, 0, 0, rows[0], rows[1], rows[2], float4(rows[3].x, 0, 0, 0), 0, 0, 0, 0, 0, o0, o1, o2, o3, o4, o5,
           o6, o7, o8, o9, o10, p10, o11);
  return o0;
}

#include "./rt_batch_unit.hlsli"
