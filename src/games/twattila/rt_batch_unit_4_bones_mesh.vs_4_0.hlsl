// Batched Unit Capture (rt_core.hpp, SceneShader::batch_mesh_shader) of the 4-bone units (main_4_bones, with or without
// dismemberment): a draw's mesh streamed out once, a point a vertex of its first instance through the draw's remap indices:
// the inputs rt_capture_unit_4_bones_* 's world position reads per vertex (POSITION0-2: the place, the four bones' indices,
// three of their weights), as the input assembler gave them.
void main(float4 v0 : POSITION0, float4 v1 : POSITION1, float4 v2 : POSITION2, out float4 o0 : TEXCOORD0, out float4 o1 : TEXCOORD1,
          out float4 o2 : TEXCOORD2)
{
  o0 = v0;
  o1 = v1;
  o2 = v2;
}
