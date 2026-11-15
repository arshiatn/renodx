// R.U.S.E.'s vertex shader 0x5278BE00 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c18_def = float4(-1, 1, 0, 2);
static const float4 c19_def = float4(0.5625, 750000, 9.99999997e-07, -0.00100000005);
static const float4 c20_def = float4(1.44269502, -1.44269502, 0.5, 1);
static const float4 c21_def = float4(0.99609375, 0, 0.00390625, 0);
static const float4 c22_def = float4(0.350000024, -0.0499999523, 0.200000003, 0.649999976);
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
  int4 a0 = 0;
  float4 o0 = 0;
  r0.xyzw = ((v0.xyzx * c18_def.yyyz + c18_def.zzzy)).xyzw;
  o0.x = (dot(c[4], r0).xxxx).x;
  o0.y = (dot(c[5], r0).xxxx).y;
  o0.z = (dot(c[6], r0).xxxx).z;
  o0.w = (dot(c[7], r0).xxxx).w;
  return o0;
}
