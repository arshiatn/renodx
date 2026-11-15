// Batched Unit Capture (rt_core.hpp, SceneShader::batch_compute) of the game's 0x5F9CD405: rt_capture_unit_sm3's world
// position, its own code, from its draw's streamed mesh (rt_batch_unit_mesh) and instance rows (rt_batch_unit_rows), the
// draw's animation texture at t0 and its width at b2.

#define main RtCaptureUnitMain
#include "./rt_capture_unit_sm3.vs_4_0.hlsl"
#undef main

#define RT_BATCH_MESH_TEXELS 2
#include "./rt_batch_unit.hlsli"
