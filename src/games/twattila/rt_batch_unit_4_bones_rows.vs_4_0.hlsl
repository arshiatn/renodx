// Batched Unit Capture (rt_core.hpp, SceneShader::batch_rows_shader) of the 4-bone units (main_4_bones, with or without
// dismemberment): a draw's instances streamed out as points, one an instance: what rt_capture_unit_4_bones_* 's world
// position reads per instance, its rows (TEXCOORD0-2) and its bone base (TEXCOORD3.x), with the animation texture's width
// the draw had (b2) beside it, as rt_batch_unit_rows' for vs30_main.
cbuffer variant_animation_size : register(b2)
{
  float2 g_animation_texture_height : packoffset(c0);
  float2 g_animation_texture_width : packoffset(c0.z);
}

// The game's inputs in its order, as far as TEXCOORD3: D3D11 feeds a vertex shader's inputs by register, as the input layout
// was made for the game's shader (main_4_bones has POSITION0-6).
void main(float4 v0 : POSITION0, float4 v1 : POSITION1, float4 v2 : POSITION2, float4 v3 : POSITION3, float2 v4 : POSITION4,
          float4 v5 : POSITION5, float4 v6 : POSITION6, float4 v7 : TEXCOORD0, float4 v8 : TEXCOORD1, float4 v9 : TEXCOORD2,
          float4 v10 : TEXCOORD3, out float4 o0 : TEXCOORD0, out float4 o1 : TEXCOORD1, out float4 o2 : TEXCOORD2,
          out float4 o3 : TEXCOORD3)
{
  o0 = v7;
  o1 = v8;
  o2 = v9;
  o3 = float4(v10.x, g_animation_texture_width.x, 0, 0);
}
