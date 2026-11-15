// R.U.S.E.'s vertex shader 0x40DF7E18 for the D3D11 replay (bridge_replay.hpp), transpiled from the game's vs_3_0 by
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
static const float4 c15_def = float4(1, 0, -1, 2);
static const float4 c16_def = float4(0.00195924775, 0.00587774301, 0.100000001, 0.199999988);
static const float4 c17_def = float4(0.800000012, 750000, 9.99999997e-07, -0.00100000005);
static const float4 c18_def = float4(1.44269502, -1.44269502, 0, 0);
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
  r0.w = (c15_def.xxxx).w;
  r1.xyzw = ((v0.xyzx * c15_def.xxxy + c15_def.yyyx)).xyzw;
  r0.x = (dot(c[8], r1).xxxx).x;
  r0.y = (dot(c[9], r1).xxxx).y;
  r0.z = (dot(c[10], r1).xxxx).z;
  o0.x = (dot(c[0], r0).xxxx).x;
  o0.y = (dot(c[1], r0).xxxx).y;
  o0.z = (dot(c[2], r0).xxxx).z;
  o0.w = (dot(c[3], r0).xxxx).w;
  return o0;
}
