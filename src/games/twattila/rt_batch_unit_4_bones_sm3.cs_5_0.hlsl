// Batched Unit Capture (rt_core.hpp, SceneShader::batch_compute) of the game's 0x0ECFE74C:
// rt_capture_unit_4_bones_sm3's world position, its own code, from its draw's streamed mesh
// (rt_batch_unit_4_bones_mesh) and instance rows (rt_batch_unit_4_bones_rows), the draw's animation texture at t0 and its
// width at b2.

#define main RtCaptureUnitMain
#include "./rt_capture_unit_4_bones_sm3.vs_4_0.hlsl"
#undef main

#define RT_BATCH_MESH_TEXELS 3
#include "./rt_batch_unit.hlsli"
