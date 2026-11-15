// Batched Unit Capture (rt_core.hpp, SceneShader::batch_mesh_shader): a unit draw's mesh streamed out once, a point a vertex
// of its first instance through the draw's remap indices: the inputs rt_capture_unit_sm3 / sm41's world position reads per
// vertex (POSITION0, POSITION1: the place and the bones' weight and indices), as the input assembler gave them.
void main(float4 v0 : POSITION0, float4 v1 : POSITION1, out float4 o0 : TEXCOORD0, out float4 o1 : TEXCOORD1)
{
  o0 = v0;
  o1 = v1;
}
