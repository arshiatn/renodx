// R.U.S.E.'s vertex shader 0xF01502EA for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
// tools/nontw/ruse/scripts/ru_vs_transpile.py (proved against it as vs_3_0 on random and traced constants).
#if __SHADER_TARGET_MAJOR >= 4
cbuffer GameVertexConstants : register(b0) { float4 c[256]; };
// The integer vertex formats ReShade reports come in normalized: scale / bias give the game's values back.
cbuffer InputConversion : register(b1) { float4 input_scale[16]; float4 input_bias[16]; };
#define POSITION_OUT SV_Position
#else
float4 c[CONSTANTS] : register(c0);
#define POSITION_OUT POSITION
#endif
static const float4 c13_def = float4(3.05185094e-05, 1, -1, 0);
struct Input {
  float4 v0 : POSITION0;
};

float4 main(Input input) : POSITION_OUT {
#if __SHADER_TARGET_MAJOR >= 4
  const float4 v0 = input.v0 * input_scale[0] + input_bias[0];
#else
  const float4 v0 = input.v0;
#endif
  float4 r0 = 0;
  float4 r1 = 0;
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyz = ((c13_def.xxxx * v0)).xyz;
  r0.xyz = ((r0 * c[12])).xyz;
  r1.xyz = ((v0 * (-c13_def.xxxx) + c13_def.yyyy)).xyz;
  r0.xyz = ((r1 * c[11] + r0)).xyz;
  r0.w = (c13_def.yyyy).w;
  o0.w = (dot(c[3], r0).xxxx).w;
  r1.x = (dot(c[0], r0).xxxx).x;
  r1.y = (dot(c[1], r0).xxxx).y;
  r1.z = (dot(c[2], r0).xxxx).z;
  o0.xyz = (r1).xyz;
  return o0;
}
